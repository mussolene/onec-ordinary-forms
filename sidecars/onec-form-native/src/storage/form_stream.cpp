#include "oof/storage/form_stream.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <zlib.h>

#include "oof/model/metamodel.hpp"
#include "oof/storage/value_codec.hpp"

namespace oof::storage::form_stream {
namespace {

class DecodeFailure final : public std::runtime_error {
public:
    explicit DecodeFailure(Diagnostic diagnostic)
        : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}

    [[nodiscard]] const Diagnostic& diagnostic() const noexcept {
        return diagnostic_;
    }

private:
    Diagnostic diagnostic_;
};

std::string child_path(std::string_view parent, std::size_t index) {
    return std::string(parent) + "/" + std::to_string(index);
}

std::string_view without_utf8_bom(std::string_view text) noexcept {
    constexpr std::string_view bom{"\xef\xbb\xbf", 3};
    return text.starts_with(bom) ? text.substr(bom.size()) : text;
}

std::string describe(const list_stream::ListValue& value) {
    std::string actual = list_stream::dump_compact(value);
    constexpr std::size_t limit = 120;
    if (actual.size() > limit) {
        actual.resize(limit);
        actual += "...";
    }
    return actual;
}

[[noreturn]] void fail(
    std::string code,
    std::string path,
    std::string expected,
    std::string actual,
    std::string message) {
    throw DecodeFailure(Diagnostic{
        std::move(code),
        DiagnosticSeverity::error,
        {},
        std::move(path),
        {},
        std::move(expected),
        std::move(actual),
        std::move(message),
    });
}

void require_list(const list_stream::ListValue& value, std::string_view path) {
    if (!value.is_list) {
        fail(
            "OOF1101",
            std::string(path),
            "list",
            describe(value),
            "Storage record must be a list");
    }
}

void require_arity(
    const list_stream::ListValue& value,
    std::size_t arity,
    std::string_view path) {
    require_list(value, path);
    if (value.items.size() != arity) {
        fail(
            "OOF1102",
            std::string(path),
            "list arity " + std::to_string(arity),
            "list arity " + std::to_string(value.items.size()),
            "Storage record has unexpected arity");
    }
}

const list_stream::ListValue& at(
    const list_stream::ListValue& value,
    std::size_t index,
    std::string_view path) {
    require_list(value, path);
    if (index >= value.items.size()) {
        fail(
            "OOF1103",
            child_path(path, index),
            "existing record slot",
            "missing",
            "Storage record slot is missing");
    }
    return value.items[index];
}

std::string raw_atom(const list_stream::ListValue& value, std::string_view path) {
    if (value.is_list || value.atom_kind != list_stream::ListValue::AtomKind::raw) {
        fail(
            "OOF1104",
            std::string(path),
            "raw atom",
            describe(value),
            "Storage slot has unexpected value kind");
    }
    return value.atom;
}

std::string string_atom(const list_stream::ListValue& value, std::string_view path) {
    if (value.is_list || value.atom_kind != list_stream::ListValue::AtomKind::string) {
        fail(
            "OOF1104",
            std::string(path),
            "string atom",
            describe(value),
            "Storage slot has unexpected value kind");
    }
    return value.atom;
}

template <typename Integer>
Integer integer_atom(const list_stream::ListValue& value, std::string_view path) {
    const std::string atom = raw_atom(value, path);
    Integer result{};
    const char* const begin = atom.data();
    const char* const end = begin + atom.size();
    const auto parsed = std::from_chars(begin, end, result, 10);
    if (atom.empty() || parsed.ec != std::errc{} || parsed.ptr != end) {
        fail(
            "OOF1105",
            std::string(path),
            "base-10 integer",
            atom,
            "Storage integer is malformed or out of range");
    }
    return result;
}

bool bool_atom(const list_stream::ListValue& value, std::string_view path) {
    const std::uint32_t parsed = integer_atom<std::uint32_t>(value, path);
    if (parsed > 1) {
        fail(
            "OOF1105",
            std::string(path),
            "0 or 1",
            std::to_string(parsed),
            "Storage Boolean is malformed");
    }
    return parsed != 0;
}

void require_raw_constant(
    const list_stream::ListValue& value,
    std::string_view expected,
    std::string_view path) {
    const std::string actual = raw_atom(value, path);
    if (actual != expected) {
        fail(
            "OOF1106",
            std::string(path),
            std::string(expected),
            actual,
            "Storage constant does not match the record contract");
    }
}

model::UuidValue uuid_atom(const list_stream::ListValue& value, std::string_view path) {
    try {
        list_stream::ListInStream in(value);
        return model::UuidValue{in.read_guid()};
    } catch (const std::exception& error) {
        fail(
            "OOF1105",
            std::string(path),
            "canonical UUID atom",
            describe(value),
            error.what());
    }
}

model::CompositeIdValue composite_id(
    const list_stream::ListValue& value,
    std::string_view path) {
    try {
        list_stream::ListInStream in(value);
        return value_codec::read_composite_id(in);
    } catch (const std::exception& error) {
        fail(
            "OOF1107",
            std::string(path),
            "CompositeID record",
            describe(value),
            error.what());
    }
}

model::TypeDomainPatternValue type_domain(
    const list_stream::ListValue& value,
    std::string_view path) {
    try {
        list_stream::ListInStream in(value);
        return value_codec::read_type_domain(in);
    } catch (const std::exception& error) {
        fail(
            "OOF1108",
            std::string(path),
            "TypeDomainPattern record",
            describe(value),
            error.what());
    }
}

template <typename T, typename Operation>
Result<T> capture_decode_failure(Operation&& operation) {
    try {
        return Result<T>::success(operation());
    } catch (const DecodeFailure& error) {
        return Result<T>::failure({error.diagnostic()});
    } catch (const std::exception& error) {
        return Result<T>::failure({Diagnostic{
            "OOF1100",
            DiagnosticSeverity::error,
            {},
            "$",
            {},
            "valid ordinary-form storage record",
            {},
            error.what(),
        }});
    }
}

list_stream::ListValue encoded_composite_id(
    const model::CompositeIdValue& value,
    std::string_view path) {
    try {
        return list_stream::parse(value_codec::encode_composite_id(value));
    } catch (const std::exception& error) {
        fail(
            "OOF1107",
            std::string(path),
            "valid CompositeID",
            {},
            error.what());
    }
}

list_stream::ListValue encoded_type_domain(
    const model::TypeDomainPatternValue& value,
    std::string_view path) {
    try {
        return list_stream::parse(value_codec::encode_type_domain(value));
    } catch (const std::exception& error) {
        fail(
            "OOF1108",
            std::string(path),
            "valid TypeDomainPattern",
            {},
            error.what());
    }
}

using LV = list_stream::ListValue;
namespace ls = list_stream;

constexpr std::string_view null_uuid = "00000000-0000-0000-0000-000000000000";
constexpr std::string_view root_panel_guid = "09ccdc77-ea1a-4a6d-ab1c-3435eada2433";
constexpr std::int32_t default_form_width = 400;
constexpr std::int32_t default_form_height = 300;

LV raw(std::string value) {
    return LV::raw_atom(std::move(value));
}

LV string_value(std::string value) {
    return LV::string_atom(std::move(value));
}

LV list(std::vector<LV> values) {
    return LV::list(std::move(values));
}

LV parse_constant(std::string_view text) {
    return list_stream::parse(text);
}

void require_exact(
    const LV& actual,
    const LV& expected,
    std::string_view path,
    std::string_view message) {
    const std::string actual_text = list_stream::dump_compact(actual);
    const std::string expected_text = list_stream::dump_compact(expected);
    if (actual_text != expected_text) {
        fail(
            "OOF1114",
            std::string(path),
            expected_text,
            actual_text,
            std::string(message));
    }
}

std::string normalize_model_line_endings(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
            normalized.push_back('\n');
            ++index;
        } else {
            normalized.push_back(text[index]);
        }
    }
    return normalized;
}

std::string normalize_storage_line_endings(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
            normalized.append("\r\n");
            ++index;
        } else if (text[index] == '\n') {
            normalized.append("\r\n");
        } else {
            normalized.push_back(text[index]);
        }
    }
    return normalized;
}

LV encoded_localized(std::string_view text) {
    model::LocalizedStringValue value;
    if (!text.empty()) {
        value.items.push_back({"ru", normalize_storage_line_endings(text)});
    }
    return list_stream::parse(value_codec::encode_localized_string(value));
}

bool is_canonical_spreadsheet_decimal(std::string_view value) {
    try {
        return value_codec::canonical_decimal(value) == value;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

LV encoded_spreadsheet_cell_value(const model::PropertyValue& value, std::string_view path) {
    if (const auto* text = std::get_if<std::string>(&value))
        return list({string_value("S"), string_value(*text)});
    if (const auto* decimal = std::get_if<model::DecimalValue>(&value)) {
        if (!is_canonical_spreadsheet_decimal(decimal->canonical))
            fail("OOF1122", std::string(path), "canonical xs:decimal", decimal->canonical,
                "SpreadsheetDocumentField Number must use a canonical decimal without exponent notation");
        return list({string_value("N"), raw(decimal->canonical)});
    }
    if (const auto* boolean = std::get_if<bool>(&value))
        return list({string_value("B"), raw(*boolean ? "1" : "0")});
    if (const auto* date = std::get_if<model::DateValue>(&value))
        return list({string_value("D"), raw(value_codec::date_to_platform(date->canonical))});
    fail("OOF1122", std::string(path), "String, Number, Boolean, or Date", "unsupported value kind",
        "SpreadsheetDocumentField typed cell value is unsupported");
}

model::PropertyValue decoded_spreadsheet_cell_value(const LV& value, std::string_view path) {
    require_arity(value, 2, path);
    const auto kind = string_atom(value.items[0], child_path(path, 0));
    if (kind == "S") return string_atom(value.items[1], child_path(path, 1));
    if (kind == "N") {
        const auto decimal = raw_atom(value.items[1], child_path(path, 1));
        if (!is_canonical_spreadsheet_decimal(decimal))
            fail("OOF1114", child_path(path, 1), "canonical xs:decimal", decimal,
                "SpreadsheetDocumentField Number must use a canonical decimal without exponent notation");
        return model::DecimalValue{decimal};
    }
    if (kind == "B") return bool_atom(value.items[1], child_path(path, 1));
    if (kind == "D") {
        return model::DateValue{value_codec::date_from_platform(raw_atom(value.items[1], child_path(path, 1)))};
    }
    fail("OOF1114", child_path(path, 0), "S, N, B, or D typed cell value", kind,
        "SpreadsheetDocumentField typed cell value kind is unsupported");
}

bool spreadsheet_cell_value_is_default(const model::SpreadsheetDocumentCellValue& typed) {
    if (typed.type.entries.size() != 1) return false;
    switch (typed.type.entries.front().term) {
        case model::TypeDomainTerm::string:
            return std::holds_alternative<std::string>(typed.value) && std::get<std::string>(typed.value).empty();
        case model::TypeDomainTerm::numeric:
            return std::holds_alternative<model::DecimalValue>(typed.value) &&
                std::get<model::DecimalValue>(typed.value).canonical == "0";
        case model::TypeDomainTerm::boolean:
            return std::holds_alternative<bool>(typed.value) && !std::get<bool>(typed.value);
        case model::TypeDomainTerm::date:
            return typed.type.entries.front().date == model::DateQualifiers{true, true} &&
                std::holds_alternative<model::DateValue>(typed.value) &&
                std::get<model::DateValue>(typed.value).canonical == "0001-01-01T00:00:00";
        default:
            return false;
    }
}

LV encoded_spreadsheet_cell_control(const model::SpreadsheetDocumentCell& cell, std::string_view path);
LV canonical_spreadsheet_field_info(
    const model::SpreadsheetDocumentFieldPayload& payload,
    bool fresh_add_default = false) {
    LV info = list_stream::parse(R"LS({18,0,0,0,0,5,5,1,1,{4,4,{0},4},{3,1,{-18},0,0,0},{8,1,12,{"ru","ru",1,1,"ru","Русский","Русский",1},{128,72},{0},0,{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},0,2,0,{0,0,00000000-0000-0000-0000-000000000000,0},0,0,0,0,0,0,0,0,0,{0},{0},{0},{0},"",{{0,6,6,{"N",1000},7,{"N",1000},8,{"N",1000},9,{"N",1000},10,{"N",1000},11,{"N",1000}}},{0,-1,-1,-1,-1,00000000-0000-0000-0000-000000000000},0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,{4,3,{-1},3},{4,3,{-3},3},0,0,0,"",0,{3,0,0,100,1,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,"",0,0,0,0,0,0,0},{0},0,0,0,1,0,0,0},0,1,{3,0,0,100,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,"ru",0,0,0,0,0,0,0},1,1,{0},0,0,0,0,0,1,0,1,1,0,0,0,0,1,1})LS");
    auto& document_info = info.items.at(11);
    auto& view_info = info.items.at(14);
    if (info.items.size() != 33 || !document_info.is_list || document_info.items.size() != 65 ||
        raw_atom(document_info.items.at(14), "$") != "2" || !view_info.is_list ||
        view_info.items.size() != 29) {
        throw std::logic_error("invalid SpreadsheetDocumentField canonical info descriptor");
    }
    std::map<std::uint32_t, std::vector<const model::SpreadsheetDocumentCell*>> rows;
    std::vector<const model::SpreadsheetDocumentCell*> typed_cells;
    for (const auto& cell : payload.cells) {
        rows[cell.row - 1].push_back(&cell);
    }
    std::vector<LV> row_items;
    for (auto& [row, cells] : rows) {
        std::sort(cells.begin(), cells.end(), [](const auto* left, const auto* right) {
            return left->column < right->column;
        });
        row_items.push_back(raw(std::to_string(row)));
        row_items.push_back(raw("0"));
        row_items.push_back(raw(std::to_string(cells.size())));
        for (const auto* cell : cells) {
            row_items.push_back(raw(std::to_string(cell->column - 1)));
            if (cell->typed_value.has_value()) {
                const auto& typed = *cell->typed_value;
                const auto reference = typed_cells.size() + 1;
                const bool default_value = spreadsheet_cell_value_is_default(typed);
                std::vector<LV> cell_items{raw(std::to_string((default_value ? 0 : 2) +
                    (cell->control.has_value() ? 1 : 0))), raw(std::to_string(reference))};
                if (cell->control.has_value()) {
                    cell_items.push_back(raw("1"));
                    cell_items.push_back(encoded_spreadsheet_cell_control(*cell,
                        "$/SpreadsheetDocumentField/Cell/Control"));
                }
                if (!default_value) cell_items.push_back(encoded_spreadsheet_cell_value(typed.value,
                    "$/SpreadsheetDocumentField/Cell/Value"));
                row_items.push_back(list(std::move(cell_items)));
                typed_cells.push_back(cell);
            } else {
                const auto value = cell->text.empty()
                    ? list({raw("1"), raw("0")})
                    : encoded_localized(cell->text);
                row_items.push_back(list({raw("16"), raw("0"), value, raw("0")}));
            }
        }
    }
    document_info.items.insert(document_info.items.begin() + 16, row_items.begin(), row_items.end());
    document_info.items[15] = raw(std::to_string(rows.size()));
    std::uint32_t columns = 0;
    std::uint32_t row_extent = 0;
    for (const auto& cell : payload.cells) {
        columns = std::max(columns, cell.column);
        row_extent = std::max(row_extent, cell.row);
    }
    const auto composite_index = 16 + row_items.size();
    document_info.items[composite_index] = list({raw(std::to_string(columns)), raw("0"),
        raw("00000000-0000-0000-0000-000000000000"), raw("0")});
    document_info.items[composite_index + 1] = raw(std::to_string(row_extent));

    if (!typed_cells.empty()) {
        const std::size_t tail_start = composite_index + 2;
        const std::size_t typed_count = typed_cells.size();
        document_info.items[tail_start + 25] = raw(std::to_string(typed_count));
        std::vector<LV> type_references;
        std::vector<LV> type_domains;
        type_references.reserve(typed_count);
        type_domains.reserve(typed_count);
        for (std::size_t index = 0; index < typed_count; ++index) {
            const auto& typed = *typed_cells[index]->typed_value;
            type_references.push_back(list({raw("46137344"), raw("1"), raw(std::to_string(index)), raw("0")}));
            type_domains.push_back(encoded_type_domain(typed.type,
                "$/SpreadsheetDocumentField/Cell/ValueType"));
        }
        document_info.items.insert(document_info.items.begin() + static_cast<std::ptrdiff_t>(tail_start + 26),
            type_references.begin(), type_references.end());
        const std::size_t domain_count_index = tail_start + 28 + typed_count;
        document_info.items[domain_count_index] = raw(std::to_string(typed_count));
        document_info.items.insert(document_info.items.begin() + static_cast<std::ptrdiff_t>(domain_count_index + 1),
            type_domains.begin(), type_domains.end());
        const std::size_t type_owner_index = tail_start + 29 + typed_count * 2;
        document_info.items[type_owner_index] = raw("1");
        document_info.items.insert(document_info.items.begin() + static_cast<std::ptrdiff_t>(type_owner_index + 1),
            raw("381ed624-9217-4e63-85db-c4c3cb87daae"));
    }

    if (fresh_add_default) {
        view_info.items[1] = raw("0");
        view_info.items[2] = raw("0");
        view_info.items[23] = raw("0");
        return info;
    }
    view_info.items[1] = raw("0");
    view_info.items[2] = raw("0");
    view_info.items[23] = raw("1");
    view_info.items.insert(view_info.items.begin() + 24,
        list({raw("3"), raw("0"), raw("0"), raw("0"), raw("0"), raw("00000000-0000-0000-0000-000000000000")}));
    return info;
}

LV encoded_localized(const model::LocalizedStringValue& value) {
    return list_stream::parse(value_codec::encode_localized_string(value));
}

std::string decoded_single_language_text(const LV& value, std::string_view path) {
    model::LocalizedStringValue decoded;
    try {
        list_stream::ListInStream in(value);
        decoded = value_codec::read_localized_string(in);
    } catch (const std::exception& error) {
        fail(
            "OOF1108",
            std::string(path),
            "single-language LocalizedString record",
            describe(value),
            error.what());
    }
    if (decoded.items.size() > 1 ||
        (!decoded.items.empty() && decoded.items.front().language != "ru")) {
        fail(
            "OOF1115",
            std::string(path),
            "zero or one ru localization",
            describe(value),
            "The product slice cannot normalize a multilingual storage title without loss");
    }
    return decoded.items.empty() ? std::string{} : normalize_model_line_endings(decoded.items.front().text);
}

LV canonical_style_record(std::uint32_t version) {
    auto value = parse_constant(
        "{10,0,{4,0,{0},\"\",-1,-1,1,0,\"\"},"
        "{4,0,{0},\"\",-1,-1,1,0,\"\"},"
        "{4,0,{0},\"\",-1,-1,1,0,\"\"},100,0,0,0,0,0}");
    value.items[0] = raw(std::to_string(version));
    return value;
}

void validate_localized_languages(
    const model::LocalizedStringValue& value,
    std::string_view path) {
    std::unordered_set<std::string> languages;
    for (std::size_t index = 0; index < value.items.size(); ++index) {
        const auto& language = value.items[index].language;
        if (!languages.insert(language).second) {
            fail("OOF1122", child_path(path, index), "one localized title item per language", language,
                "Localized Page title contains a duplicate language");
        }
    }
}

LV encode_page_record(const model::Page& page, std::string_view path) {
    if (page.id.value() == 0 || page.name.empty()) {
        fail("OOF1122", std::string(path), "Page with positive ID and non-empty Name",
            page.name, "Page identity cannot be encoded");
    }
    const auto& title = page.title.value();
    if (title.items.size() > std::numeric_limits<std::uint32_t>::max()) {
        fail("OOF1112", child_path(path, 1), "localized title entry count within uint32 range",
            std::to_string(title.items.size()), "Page title has too many localizations");
    }
    validate_localized_languages(title, child_path(path, 1));
    LV encoded_title;
    try {
        encoded_title = list_stream::parse(value_codec::encode_localized_string(title));
    } catch (const std::exception& error) {
        fail("OOF1108", child_path(path, 1), "encodable LocalizedString", error.what(),
            "Page title cannot be encoded");
    }
    return list({
        raw("6"),
        std::move(encoded_title),
        canonical_style_record(10),
        raw("-1"),
        raw(page.visible.value() ? "1" : "0"),
        raw(page.enabled.value() ? "1" : "0"),
        string_value(page.name),
        raw("1"),
        parse_constant("{4,4,{0},4}"),
        parse_constant("{4,4,{0},4}"),
        parse_constant("{8,3,0,1,100}"),
        raw("1"),
    });
}

model::Page decode_page_record(const LV& row, model::ObjectId id, std::string_view path) {
    require_arity(row, 12, path);
    require_raw_constant(row.items[0], "6", child_path(path, 0));
    model::LocalizedStringValue title;
    try {
        list_stream::ListInStream in(row.items[1]);
        title = value_codec::read_localized_string(in);
        if (in.has_next()) {
            fail("OOF1108", child_path(path, 1), "complete LocalizedString record", describe(row.items[1]),
                "Page title has trailing values");
        }
    } catch (const DecodeFailure&) {
        throw;
    } catch (const std::exception& error) {
        fail("OOF1108", child_path(path, 1), "LocalizedString record", describe(row.items[1]), error.what());
    }
    validate_localized_languages(title, child_path(path, 1));
    require_exact(row.items[2], canonical_style_record(10), child_path(path, 2),
        "Page style contains an unsupported variation");
    require_raw_constant(row.items[3], "-1", child_path(path, 3));
    const bool visible = bool_atom(row.items[4], child_path(path, 4));
    const bool enabled = bool_atom(row.items[5], child_path(path, 5));
    const auto name = string_atom(row.items[6], child_path(path, 6));
    if (name.empty()) {
        fail("OOF1115", child_path(path, 6), "non-empty Page Name", "empty", "Page Name is required");
    }
    require_raw_constant(row.items[7], "1", child_path(path, 7));
    require_exact(row.items[8], parse_constant("{4,4,{0},4}"), child_path(path, 8),
        "Page default property contains an unsupported variation");
    require_exact(row.items[9], parse_constant("{4,4,{0},4}"), child_path(path, 9),
        "Page default property contains an unsupported variation");
    require_exact(row.items[10], parse_constant("{8,3,0,1,100}"), child_path(path, 10),
        "Page default property contains an unsupported variation");
    require_raw_constant(row.items[11], "1", child_path(path, 11));

    model::Page page;
    page.id = id;
    page.name = name;
    page.title.set(std::move(title));
    if (!visible) page.visible.set(false);
    if (!enabled) page.enabled.set(false);
    return page;
}

model::Page canonical_default_page() {
    model::Page page;
    page.id = model::ObjectId{1};
    page.name = "Страница1";
    model::LocalizedStringValue title;
    title.items.push_back({"ru", "Страница1"});
    page.title.set(std::move(title));
    page.position.set(model::Position{});
    return page;
}

LV canonical_root_panel_payload(
    std::int32_t width,
    std::int32_t height,
    const std::vector<model::Page>& pages = {},
    std::optional<model::ControlRef> page_owner = std::nullopt) {
    if (width < 8 || height < 8) {
        fail(
            "OOF1120",
            "$/1",
            "form dimensions at least 8x8",
            std::to_string(width) + "x" + std::to_string(height),
            "The platform root panel requires non-negative inner dimensions");
    }
    auto value = parse_constant(R"OOF(
{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},26,0,0,0,0,0,0,{10,1,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},0,1,{1,0},1,1,0,4,0,4294967295,5,64,0,{4,4,{0},4},0,0,57,0,0},{0}}
)OOF");
    if (!value.is_list || value.items.size() != 3 ||
        !value.items[1].is_list || value.items[1].items.size() != 27) {
        throw std::logic_error("canonical root-panel codec template is malformed");
    }
    const auto effective_pages = pages.empty() ? std::vector<model::Page>{canonical_default_page()} : pages;
    const auto page_table = encode_page_table(effective_pages);
    if (!page_table) throw DecodeFailure(page_table.diagnostics().front());
    value.items[1].items[11] = page_table.value();
    std::vector<LV> boundary_rows;
    for (std::size_t index = 0; index < effective_pages.size(); ++index) {
        model::Position position;
        if (pages.empty()) {
            position.left.set(8);
            position.top.set(8);
            position.width.set(width - 16);
            position.height.set(height - 16);
            for (const auto edge : {model::BindingCoordinate::right, model::BindingCoordinate::bottom}) {
                model::AnchorBinding binding;
                binding.coordinate = edge;
                binding.target_coordinate = edge;
                binding.offset.set(-8);
                position.bindings.anchors.push_back(std::move(binding));
            }
        } else {
            position = effective_pages[index].position.value();
        }
        const auto page_index = static_cast<std::uint32_t>(index);
        auto boundaries = encode_page_position(position, page_index, page_owner);
        if (!boundaries) throw DecodeFailure(boundaries.diagnostics().front());
        boundary_rows.insert(boundary_rows.end(), boundaries.value().items.begin(), boundaries.value().items.end());
    }
    value.items[1].items.insert(value.items[1].items.begin() + 16,
        boundary_rows.begin(), boundary_rows.end());
    value.items[1].items[9] = raw(effective_pages.size() > 1 ? "1" : "0");
    value.items[1].items[15] = raw(std::to_string(effective_pages.size() * 4));
    if (effective_pages.size() > 1) {
        const std::size_t repeated_marker_start = 16 + effective_pages.size() * 4 + 2;
        std::vector<LV> markers;
        for (std::size_t index = 1; index < effective_pages.size(); ++index) {
            markers.push_back(raw("4294967295"));
        }
        value.items[1].items.insert(value.items[1].items.begin() + static_cast<std::ptrdiff_t>(repeated_marker_start),
            markers.begin(), markers.end());
    }
    return value;
}

LV canonical_panel_payload(const std::vector<model::Page>& pages, model::ControlRef owner) {
    if (pages.empty()) {
        fail("OOF1122", "$/Panel/Pages", "at least one named Panel Page", "empty",
            "Panel page storage requires explicit Pages");
    }
    auto envelope = canonical_root_panel_payload(8, 8, pages, owner);
    envelope.items[1].items[9] = raw("1");
    envelope.items[1].items[12] = raw("0");
    return envelope;
}


model::ColorValue button_color_default(std::string_view property) {
    if (property == "BorderColor") {
        return model::ColorValue{};
    }
    model::ColorValue value;
    value.kind = model::ColorKind::style_reference;
    if (property == "ButtonTextColor") {
        value.style = model::QualifiedName{"StyleColors.ButtonTextColor"};
    } else if (property == "ButtonBackColor") {
        value.style = model::QualifiedName{"StyleColors.ButtonBackColor"};
    } else {
        fail("OOF1122", "$/Button", "known Button color property", std::string(property),
            "Button color default is not declared");
    }
    return value;
}

LV encode_button_color(const model::ColorValue& color, std::string_view path) {
    const bool default_channels = color.red == 0 && color.green == 0 &&
        color.blue == 0 && color.alpha == 255;
    if (color.kind == model::ColorKind::automatic) {
        if (!default_channels || !std::holds_alternative<std::monostate>(color.style)) {
            fail("OOF1122", std::string(path), "automatic color defaults", "non-default fields",
                "Button automatic color carries unsupported channels or a style reference");
        }
        return list({raw("4"), raw("4"), list({raw("0")}), raw("4")});
    }
    if (color.kind == model::ColorKind::absolute) {
        if (color.alpha != 255 || !std::holds_alternative<std::monostate>(color.style)) {
            fail("OOF1122", std::string(path), "opaque absolute RGB without a style", "alpha or style",
                "Button absolute colors support opaque RGB values only");
        }
        const std::uint32_t packed = (static_cast<std::uint32_t>(color.blue) << 16) |
            (static_cast<std::uint32_t>(color.green) << 8) | color.red;
        return list({raw("4"), raw("0"), list({raw(std::to_string(packed))}), raw("0")});
    }
    if (color.kind != model::ColorKind::style_reference) {
        fail("OOF1122", std::string(path), "ColorKind absolute, automatic, or style_reference",
            std::to_string(static_cast<unsigned int>(color.kind)),
            "Button color kind is unsupported");
    }
    if (!default_channels) {
        fail("OOF1122", std::string(path), "named style color without channels", "non-default channels",
            "Button style colors cannot carry explicit channels");
    }
    const auto* name = std::get_if<model::QualifiedName>(&color.style);
    if (name == nullptr) {
        fail("OOF1122", std::string(path), "known QualifiedName style reference", "non-named style",
            "Button colors support named style references only");
    }
    std::int32_t style_id = 0;
    if (name->value == "StyleColors.ButtonTextColor") style_id = -21;
    else if (name->value == "StyleColors.ButtonBackColor") style_id = -7;
    else if (name->value == "StyleColors.ButtonBorderColor") style_id = -34;
    else if (name->value == "StyleColors.BorderColor") style_id = -22;
    else fail("OOF1122", std::string(path), "known Button StyleColors property", name->value,
        "Button color style reference is unsupported");
    return list({raw("4"), raw("3"), list({raw(std::to_string(style_id))}), raw("3")});
}

model::ColorValue decode_button_color(const LV& value, std::string_view path) {
    require_arity(value, 4, path);
    require_raw_constant(value.items[0], "4", child_path(path, 0));
    const auto kind = integer_atom<std::int32_t>(value.items[1], child_path(path, 1));
    if (kind == 4) {
        require_exact(value, list({raw("4"), raw("4"), list({raw("0")}), raw("4")}),
            path, "Button automatic color record is malformed");
        return model::ColorValue{};
    }
    if (kind == 0) {
        require_raw_constant(value.items[3], "0", child_path(path, 3));
        require_arity(value.items[2], 1, child_path(path, 2));
        const auto packed = integer_atom<std::int64_t>(value.items[2].items[0], child_path(path, 2) + "/0");
        if (packed < 0 || packed > 0x00ffffff) {
            fail("OOF1114", child_path(path, 2) + "/0", "packed RGB in 0..16777215",
                std::to_string(packed), "Button packed RGB color is out of range");
        }
        model::ColorValue color;
        color.kind = model::ColorKind::absolute;
        color.red = static_cast<std::uint8_t>(packed & 0xff);
        color.green = static_cast<std::uint8_t>((packed >> 8) & 0xff);
        color.blue = static_cast<std::uint8_t>((packed >> 16) & 0xff);
        return color;
    }
    if (kind == 3) {
        require_raw_constant(value.items[3], "3", child_path(path, 3));
        require_arity(value.items[2], 1, child_path(path, 2));
        const auto style_id = integer_atom<std::int32_t>(value.items[2].items[0], child_path(path, 2) + "/0");
        std::string_view style_name;
        if (style_id == -21) style_name = "StyleColors.ButtonTextColor";
        else if (style_id == -7) style_name = "StyleColors.ButtonBackColor";
        else if (style_id == -34) style_name = "StyleColors.ButtonBorderColor";
        else if (style_id == -22) style_name = "StyleColors.BorderColor";
        else fail("OOF1114", child_path(path, 2) + "/0", "known Button StyleColors ID",
            std::to_string(style_id), "Button color style identifier is unsupported");
        model::ColorValue color;
        color.kind = model::ColorKind::style_reference;
        color.style = model::QualifiedName{std::string(style_name)};
        return color;
    }
    fail("OOF1114", child_path(path, 1), "Button color kind 0, 3, or 4", std::to_string(kind),
        "Button color storage kind is unsupported");
}

model::ColorValue explicit_button_color(const model::PropertySet& properties, std::string_view name,
                                        std::string_view control_name = "Button") {
    const auto* entry = properties.find(model::PropertyId::from_name(name));
    if (entry == nullptr) return button_color_default(name);
    if (!std::holds_alternative<model::ColorValue>(entry->value)) {
        fail("OOF1122", "$/" + std::string(control_name) + "/" + std::string(name), "ColorValue", "different value type",
            "Control color property has the wrong value type");
    }
    return std::get<model::ColorValue>(entry->value);
}

model::ColorValue explicit_splitter_color(const model::PropertySet& properties, std::string_view name) {
    const auto* entry = properties.find(model::PropertyId::from_name(name));
    if (entry == nullptr) return model::ColorValue{};
    if (!std::holds_alternative<model::ColorValue>(entry->value)) {
        fail("OOF1122", std::string("$/Splitter/") + std::string(name), "ColorValue",
            "different value type", "Splitter color property has the wrong value type");
    }
    const auto color = std::get<model::ColorValue>(entry->value);
    if (color.kind != model::ColorKind::automatic && color.kind != model::ColorKind::absolute) {
        fail("OOF1122", std::string("$/Splitter/") + std::string(name),
            "automatic or absolute RGB color", "unobserved color kind",
            "Splitter style colors are outside the supported storage profile");
    }
    return color;
}

LV encode_control_font(const model::FontValue& font, std::string_view property_path) {
    try {
        return list_stream::parse(value_codec::encode_font(font));
    } catch (const std::exception& error) {
        fail("OOF1122", std::string(property_path), "supported named Font value", error.what(),
            "Font cannot be represented by the supported platform codec");
    }
}

constexpr std::array<std::pair<model::ControlBorderType, std::uint32_t>, 8> native_border_types{{
    {model::ControlBorderType::without_border, 0}, {model::ControlBorderType::single, 1},
    {model::ControlBorderType::double_line, 200}, {model::ControlBorderType::embossed, 2},
    {model::ControlBorderType::indented, 3}, {model::ControlBorderType::underline, 4},
    {model::ControlBorderType::double_underline, 5}, {model::ControlBorderType::overline, 7}}};

LV encode_control_border(const model::BorderValue& border, std::string_view path) {
    if (border.kind == model::BorderKind::style_reference) {
        const auto* name = std::get_if<model::QualifiedName>(&border.style);
        if (name == nullptr || name->value != "StyleBorders.ControlBorder" ||
            border.border_type != model::ControlBorderType::without_border || border.width != 0)
            fail("OOF1122", std::string(path), "named StyleBorders.ControlBorder without absolute fields",
                "unsupported style border", "Border style is unsupported");
        return parse_constant("{3,1,{-18},0,0,0}");
    }
    const auto type = std::ranges::find(native_border_types, border.border_type, [](const auto& entry) { return entry.first; });
    if (border.kind != model::BorderKind::absolute || type == native_border_types.end() ||
        !std::holds_alternative<std::monostate>(border.style) || border.width > 5 ||
        (border.border_type == model::ControlBorderType::without_border && border.width > 1))
        fail("OOF1122", std::string(path), "supported absolute Border type and integer width 0..5",
            "unsupported border", "Border cannot be represented by this control");
    const auto state = border == model::BorderValue{} ? "0" : "3";
    return list({raw("3"), raw("0"), list({raw("0")}), raw(std::to_string(type->second)),
        raw(std::to_string(border.width)), raw(state), raw("48312c09-257f-4b29-b280-284dd89efc1e")});
}

model::BorderValue decode_control_border(const LV& value, std::string_view path) {
    require_list(value, path);
    const auto kind = integer_atom<unsigned>(at(value, 1, path), child_path(path, 1));
    model::BorderValue border;
    if (kind == 1) {
        require_exact(value, parse_constant("{3,1,{-18},0,0,0}"), path, "Unsupported named Border style");
        border.kind = model::BorderKind::style_reference;
        border.style = model::QualifiedName{"StyleBorders.ControlBorder"};
        return border;
    }
    if (kind != 0) fail("OOF1114", child_path(path, 1), "absolute or known style Border", std::to_string(kind), "Unsupported Border kind");
    require_arity(value, 7, path);
    const auto type_code = integer_atom<std::uint32_t>(value.items[3], child_path(path, 3));
    const auto type = std::ranges::find(native_border_types, type_code, [](const auto& entry) { return entry.second; });
    if (type == native_border_types.end()) fail("OOF1114", child_path(path, 3), "supported Border type", std::to_string(type_code), "Unsupported Border type");
    border.border_type = type->first;
    border.width = integer_atom<std::uint32_t>(value.items[4], child_path(path, 4));
    if (border.width > 5 || (border.border_type == model::ControlBorderType::without_border && border.width > 1))
        fail("OOF1114", child_path(path, 4), "integer Border width 0..5 (WithoutBorder at most 1)",
            std::to_string(border.width), "Unsupported Border width");
    const auto state = integer_atom<unsigned>(value.items[5], child_path(path, 5));
    if (state != 0 && state != 3) fail("OOF1114", child_path(path, 5), "observed Border state 0 or 3", std::to_string(state), "Unsupported Border state");
    auto normalized = value;
    const auto expected = encode_control_border(border, path);
    normalized.items[5] = expected.items[5];
    // The platform XDTO reader restores these named enum values from an absent factory.
    if (state == 0 && border.width == 1 &&
        (border.border_type == model::ControlBorderType::underline || border.border_type == model::ControlBorderType::overline) &&
        !value.items[6].is_list && value.items[6].atom_kind == LV::AtomKind::raw &&
        value.items[6].atom == "00000000-0000-0000-0000-000000000000")
        normalized.items[6] = expected.items[6];
    require_exact(normalized, expected, path, "Border record contains unsupported fields");
    return border;
}

model::FontValue decode_control_font(const LV& value, std::string_view path) {
    try {
        return value_codec::decode_font(list_stream::dump_compact(value));
    } catch (const std::exception& error) {
        fail("OOF1114", std::string(path), "supported canonical Font record", error.what(),
            "Font record is malformed or unsupported");
    }
}

model::FontValue explicit_control_font(const model::PropertySet& properties, std::string_view property_path) {
    const auto* entry = properties.find(model::PropertyId::from_name("Font"));
    if (entry == nullptr) return {};
    if (!std::holds_alternative<model::FontValue>(entry->value)) {
        fail("OOF1122", std::string(property_path), "FontValue", "different value type",
            "Font property has the wrong value type");
    }
    return std::get<model::FontValue>(entry->value);
}

LV encode_button_shortcut(const model::ShortcutValue& shortcut) {
    try {
        return list_stream::parse(value_codec::encode_shortcut(shortcut));
    } catch (const std::exception& error) {
        fail("OOF1122", "$/Button/Shortcut", "supported named Shortcut value", error.what(),
            "Button Shortcut cannot be represented by the platform codec");
    }
}

model::ShortcutValue decode_button_shortcut(const LV& value, std::string_view path) {
    try {
        return value_codec::decode_shortcut(list_stream::dump_compact(value));
    } catch (const std::exception& error) {
        fail("OOF1114", std::string(path), "supported canonical Button Shortcut record", error.what(),
            "Button Shortcut record is malformed or unsupported");
    }
}

model::ShortcutValue explicit_button_shortcut(const model::PropertySet& properties) {
    const auto* entry = properties.find(model::PropertyId::from_name("Shortcut"));
    if (entry == nullptr) return {};
    if (!std::holds_alternative<model::ShortcutValue>(entry->value)) {
        fail("OOF1122", "$/Button/Shortcut", "ShortcutValue", "different value type",
            "Button Shortcut property has the wrong value type");
    }
    return std::get<model::ShortcutValue>(entry->value);
}

LV canonical_button_base(bool enabled, std::string_view tool_tip = {},
    const model::ColorValue* border_color = nullptr,
    const model::ColorValue* button_text_color = nullptr,
    const model::ColorValue* button_back_color = nullptr,
    const model::FontValue* font = nullptr, std::string_view font_path = "$/Button/Font") {
    auto value = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,"
        "{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},"
        "{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"
        "{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    value.items[1] = raw(enabled ? "1" : "0");
    value.items[12] = encoded_localized(tool_tip);
    if (font != nullptr) value.items[4] = encode_control_font(*font, font_path);
    if (border_color != nullptr) value.items[6] = encode_button_color(*border_color, "$/Button/BorderColor");
    if (button_text_color != nullptr) value.items[10] = encode_button_color(*button_text_color, "$/Button/ButtonTextColor");
    if (button_back_color != nullptr) value.items[9] = encode_button_color(*button_back_color, "$/Button/ButtonBackColor");
    return value;
}

constexpr std::string_view command_bar_root_marker = "b78f2e80-ec68-11d4-9dcf-0050bae2bc79";

LV canonical_command_bar_base(bool enabled, std::string_view tool_tip,
                              const model::ColorValue* border_color = nullptr,
                              const model::ColorValue* button_text_color = nullptr,
                              const model::ColorValue* back_color = nullptr,
                              const model::BorderValue* border = nullptr, bool transparent = false,
                              const model::ColorValue* button_back_color = nullptr) {
    auto value = parse_constant(R"OOF({19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}})OOF");
    value.items[1] = raw(enabled ? "1" : "0");
    value.items[5] = raw(transparent ? "1" : "0");
    value.items[12] = encoded_localized(tool_tip);
    if (border_color != nullptr) value.items[6] = encode_button_color(*border_color, "$/CommandBar/BorderColor");
    if (button_text_color != nullptr) value.items[10] = encode_button_color(*button_text_color, "$/CommandBar/ButtonTextColor");
    if (back_color != nullptr) value.items[2] = encode_button_color(*back_color, "$/CommandBar/BackColor");
    if (button_back_color != nullptr) value.items[9] = encode_button_color(*button_back_color, "$/CommandBar/ButtonBackColor");
    if (border != nullptr) value.items[11] = encode_control_border(*border, "$/CommandBar/Border");
    return value;
}

LV encode_button_menu(const std::vector<model::CommandBarButton>& entries,
                      const model::OrdinaryFormDocument& document, std::uint64_t control_id,
                      std::string_view root_marker, std::uint64_t root_group_id);

// Menu item IDs are assigned in preorder by encode_button_menu, including submenus.
std::uint64_t default_menu_item_id(const std::vector<model::CommandBarButton>& entries) {
    std::uint64_t next = 0, selected = 0;
    const auto visit = [&](const auto& self, const auto& items, std::size_t depth) -> void {
        if (depth > 256) fail("OOF1122", "$/CommandBar/Buttons", "bounded menu depth", "too deep", "Menu nesting is too deep");
        for (const auto& item : items) {
            ++next;
            if (item.default_button) {
                if (selected != 0 || depth != 0 || item.type != model::CommandBarButtonKind::action)
                    fail("OOF1122", "$/CommandBar/Buttons/DefaultButton", "one top-level Action", item.name, "Unsupported default menu action");
                selected = next;
            }
            self(self, item.buttons, depth + 1);
        }
    };
    visit(visit, entries, 0);
    return selected;
}

std::uint64_t default_command_bar_id(const model::OrdinaryFormDocument& document) {
    std::uint64_t selected = 4294967295;
    for (const auto& control : document.collections().controls) {
        const auto* bar = std::get_if<model::CommandBarPayload>(&control.payload);
        if (bar != nullptr && default_menu_item_id(bar->buttons) != 0) {
            if (selected != 4294967295 || control.id.value() >= 4294967295)
                fail("OOF1122", "$/CommandBar/Buttons/DefaultButton", "one owner with ID below uint32 sentinel", control.name, "Default action owner is invalid");
            selected = control.id.value();
        }
    }
    return selected;
}

LV canonical_command_bar_properties(bool enabled, std::string_view tool_tip,
                                     const std::vector<model::CommandBarButton>& buttons,
                                     const model::OrdinaryFormDocument& document, std::uint64_t control_id, bool secondary = true,
                                     const model::ColorValue* border_color = nullptr,
                                     const model::ColorValue* button_text_color = nullptr,
                                     const model::ColorValue* back_color = nullptr,
                                     const model::BorderValue* border = nullptr, bool auto_fill = false,
                                     bool transparent = false, std::int32_t orientation = 2,
                                     std::int32_t buttons_alignment = 0,
                                     const model::ColorValue* button_back_color = nullptr) {
    std::vector<LV> properties(14, raw("0"));
    properties[0] = canonical_command_bar_base(enabled, tool_tip, border_color, button_text_color, back_color, border, transparent, button_back_color);
    // Slot 1 observed canonical default.
    properties[1] = raw("9");
    properties[2] = raw(std::to_string(orientation));
    properties[3] = raw(auto_fill ? "1" : "0");
    properties[4] = raw(std::to_string(buttons_alignment));
    // Slot 5 observed canonical default.
    properties[5] = raw(secondary ? "1" : "0");
    // Slot 6 observed canonical default.
    properties[6] = raw("1");
    // Slot 10 observed canonical default.
    properties[10] = raw("9d0a2e40-b978-11d4-84b6-008048da06df");
    // Slot 11 observed canonical default.
    properties[11] = raw("0");
    // Slot 12 observed canonical default.
    properties[12] = raw("0");
    // Slot 13 observed canonical default.
    properties[13] = raw("0");
    // Slot 8 is the observed stable root-owner marker.
    properties[8] = raw("b78f2e80-ec68-11d4-9dcf-0050bae2bc79");
    properties[7] = encode_button_menu(buttons, document, control_id, command_bar_root_marker, control_id);
    properties[9] = raw(std::to_string(control_id));
    const auto selected = default_menu_item_id(buttons);
    if (selected != 0) {
        properties[10] = properties[7].items[1];
        properties[11] = raw(std::to_string(selected));
    }
    return list(std::move(properties));
}


LV canonical_usual_group_properties(bool enabled, std::string_view caption, std::string_view tool_tip) {
    auto base = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,4,700,1,100},0,{4,4,{0},4},{4,4,{0},4},"
        "{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"
        "{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    base.items[1] = raw(enabled ? "1" : "0");
    base.items[12] = encoded_localized(tool_tip);
    return list({raw("0"), list({
        std::move(base),
        raw("8"),
        encoded_localized(caption),
        parse_constant("{3,0,{0},6,1,0,cf48d3ca-5bd4-45b9-bb8f-a0922a8335f2}"),
        raw("0"),
    })});
}

LV canonical_picture_properties(bool enabled, std::string_view tool_tip = {}) {
    auto properties = parse_constant(
        R"({{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},20,0,0,{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,2,0,0,1,2},{0,0,0},1,1,0,0,{1,0},0,1,1,1})");
    properties.items[0] = canonical_button_base(enabled, tool_tip);
    return properties;
}

LV canonical_splitter_properties(bool enabled, std::int32_t orientation, std::string_view tool_tip,
    const model::ColorValue& border_color, const model::ColorValue& back_color) {
    auto base = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},1,{4,4,{0},4},"
        "{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},"
        "{3,0,{-18},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"
        "{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    if (!base.is_list || base.items.size() != 21) {
        throw std::logic_error("canonical Splitter base record is malformed");
    }
    base.items[1] = raw(enabled ? "1" : "0");
    base.items[12] = encoded_localized(tool_tip);
    base.items[2] = encode_button_color(back_color, "$/Splitter/BackColor");
    base.items[6] = encode_button_color(border_color, "$/Splitter/BorderColor");
    return list({std::move(base), raw("2"), raw(std::to_string(orientation)), raw("0")});
}

LV canonical_button_properties(
    bool enabled,
    std::string_view caption,
    std::int32_t horizontal_align,
    std::int32_t vertical_align,
    std::int32_t picture_location,
    std::int32_t picture_size,
    std::int32_t menu_mode,
    bool multi_line,
    std::string_view tool_tip,
    const model::ColorValue& border_color,
    const model::ColorValue& button_text_color,
    const model::ColorValue& button_back_color,
    const model::FontValue& font,
    const model::ShortcutValue& shortcut) {
    auto properties = list({
        canonical_button_base(enabled, tool_tip, &border_color, &button_text_color, &button_back_color, &font),
        raw("14"),
        encoded_localized(caption),
        raw(std::to_string(horizontal_align)),
        raw(std::to_string(vertical_align)),
        raw("0"),
        raw(std::to_string(picture_location)),
        raw(std::to_string(picture_size)),
        parse_constant("{4,0,{0},\"\",-1,-1,1,0,\"\"}"),
        encode_button_shortcut(shortcut),
        raw(multi_line ? "1" : "0"),
        raw(std::to_string(menu_mode)),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("1"),
    });
    if (menu_mode != 0) {
        properties.items.insert(properties.items.begin() + 12, parse_constant(
            "{5,53232d71-06b1-4ec1-a94d-77fafadef407,0,1,0,1,"
            "{5,31946946-0a9b-40a2-95cf-82f200778341,0,0,0,{-1,0,{0}}}}"));
    }
    return properties;
}

LV canonical_label_properties(
    std::string_view caption,
    std::int32_t horizontal_align,
    bool enabled,
    std::string_view tool_tip) {
    auto base_properties = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,"
        "{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},"
        "{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"
        "{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    if (!base_properties.is_list || base_properties.items.size() != 21) {
        throw std::logic_error("canonical LabelDecoration base properties are malformed");
    }
    base_properties.items[1] = raw(enabled ? "1" : "0");
    base_properties.items[12] = encoded_localized(tool_tip);
    return list({
        std::move(base_properties),
        raw("11"),
        encoded_localized(caption),
        raw(std::to_string(horizontal_align)),
        raw("1"),
        raw("0"),
        raw("0"),
        raw("0"),
        parse_constant("{0,0,0}"),
        raw("0"),
        parse_constant("{1,0}"),
        raw("1"),
        parse_constant(
            "{10,0,{4,0,{0},\"\",-1,-1,1,0,\"\"},"
            "{4,0,{0},\"\",-1,-1,1,0,\"\"},"
            "{4,0,{0},\"\",-1,-1,1,0,\"\"},100,2,0,0,1,2}"),
        raw("4"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
    });
}

LV encoded_radio_selection_value(const std::optional<model::DecimalValue>& value);

LV canonical_radio_button_info(
    const model::TypeDomainPatternValue& value_type,
    std::optional<model::DecimalValue> selection_value,
    bool enabled,
    std::string_view caption,
    std::string_view tool_tip) {
    const auto properties = list({
        canonical_button_base(enabled, tool_tip),
        raw("7"),
        encoded_localized(caption),
        raw("1"),
        raw("0"),
        raw("1"),
        raw("0"),
        raw("100"),
        raw("1"),
    });
    return list({
        raw("4"),
        encoded_type_domain(value_type, "$/RadioButton/ValueType"),
        list({std::move(properties), raw("4"), raw("0"), raw("0"), raw("0"), raw("0")}),
        raw("0"),
        encoded_radio_selection_value(selection_value),
        list({raw("0")}),
    });
}

LV canonical_control_base_properties(bool enabled, std::string_view tool_tip);

LV canonical_choice_field_info(bool enabled, std::string_view tool_tip) {
    auto properties = parse_constant(R"OOF(
{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},31,0,0,1,0,1,0,0,0,0,1,0,0,255,0,0,4,0,{"U"},{"U"},"",0,1,1,0,0,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},0,0,0,{0,0,0},{1,0},0,0,0,0,0,0,0,16777215,2,0,0}
)OOF");
    if (properties.items.size() != 46 || properties.items[0].items.size() != 21) {
        throw std::logic_error("canonical ChoiceField info profile is malformed");
    }
    properties.items[0] = canonical_control_base_properties(enabled, tool_tip);
    return list({raw("2"), std::move(properties), list({raw("0")})});
}

LV canonical_html_document_field_data(std::int32_t output) {
    return list({
        raw("5"),
        raw("0"),
        list({raw("0")}),
        list({raw("4"), raw("4"), list({raw("0")}), raw("4")}),
        list({raw("3"), raw("1"), list({raw("-18")}), raw("0"), raw("0"), raw("0")}),
        raw("1"),
        raw(std::to_string(output)),
    });
}

LV canonical_check_box_info(bool enabled, std::string_view caption, std::string_view tool_tip,
    const model::FontValue* font = nullptr) {
    return list({
        raw("1"),
        list({
            list({
                canonical_button_base(enabled, tool_tip, nullptr, nullptr, nullptr, font, "$/CheckBox/Font"),
                raw("7"),
                encoded_localized(caption),
                raw("1"),
                raw("0"),
                raw("1"),
                raw("0"),
                raw("100"),
                raw("1"),
            }),
            raw("4"),
            raw("0"),
            raw("0"),
            raw("0"),
            raw("0"),
            raw("0"),
        }),
        list({raw("0")}),
    });
}

// The remaining fixed CalendarField values come from one controlled synthetic Add sample.
// Their domain meanings are unknown; this profile exposes only Enabled and BeginOfDisplayPeriod.
LV canonical_calendar_field_info(bool enabled, std::string_view begin_period = "00010101000000") {
    const LV zero_record = list({raw("0")});
    const LV canonical_4_4_record = list({raw("4"), raw("4"), zero_record, raw("4")});
    const LV canonical_neg7_record = list({raw("4"), raw("3"), list({raw("-7")}), raw("3")});
    const LV canonical_neg21_record = list({raw("4"), raw("3"), list({raw("-21")}), raw("3")});
    const LV canonical_neg18_record = list({raw("3"), raw("1"), list({raw("-18")}), raw("0"), raw("0"), raw("0")});
    const LV base_properties = list({
        raw("19"), raw(enabled ? "1" : "0"), canonical_4_4_record, canonical_4_4_record,
        list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), raw("0"),
        canonical_4_4_record, canonical_4_4_record, canonical_4_4_record, canonical_neg7_record, canonical_neg21_record,
        canonical_neg18_record, list({raw("1"), raw("0")}), raw("0"), raw("0"), raw("100"),
        raw("2"), raw("2"), raw("1"), raw("2"), canonical_4_4_record,
    });
    const LV properties = list({
        base_properties, raw("9"),
        list({raw("4"), raw("3"), list({raw("-16")}), raw("3")}),
        list({raw("4"), raw("3"), list({raw("-14")}), raw("3")}),
        list({raw("4"), raw("3"), list({raw("-15")}), raw("3")}),
        raw(std::string(begin_period)), raw("00010101000000"),
        raw("1"), raw("1"), raw("0"), raw("0"), raw("0"), raw("0"), raw("1"),
    });
    return list({raw("1"), properties, zero_record});
}

LV canonical_text_document_field_info(bool enabled, const model::ColorValue& border_color,
                                      const model::FontValue& font) {
    auto base = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},"
        "{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},"
        "{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"
        "{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    base.items[1] = raw(enabled ? "1" : "0");
    base.items[4] = encode_control_font(font, "$/TextDocumentField/Font");
    base.items[6] = encode_button_color(border_color, "$/TextDocumentField/BorderColor");
    return list({std::move(base), raw("6"), raw("1"),
        raw("00000000-0000-0000-0000-000000000000"), list({raw("0")}), raw("0"), raw("0")});
}


enum class InputFieldFlagScope { payload, base_info, root_info };

struct InputFieldFlagMapping {
    std::string_view name;
    InputFieldFlagScope scope;
    std::size_t slot;
    bool default_value;
    std::size_t paired_slot;
};

constexpr std::size_t no_paired_slot = std::numeric_limits<std::size_t>::max();
constexpr std::array input_field_flag_mappings{
    InputFieldFlagMapping{"Wrap", InputFieldFlagScope::payload, 4, true, no_paired_slot},
    InputFieldFlagMapping{"ChooseType", InputFieldFlagScope::root_info, 6, true, no_paired_slot},
    InputFieldFlagMapping{"MarkNegatives", InputFieldFlagScope::payload, 27, false, no_paired_slot},
    InputFieldFlagMapping{"ChoiceButton", InputFieldFlagScope::payload, 7, false, no_paired_slot},
    InputFieldFlagMapping{"OpenButton", InputFieldFlagScope::payload, 10, false, no_paired_slot},
    InputFieldFlagMapping{"ClearButton", InputFieldFlagScope::payload, 8, false, no_paired_slot},
    InputFieldFlagMapping{"SpinButton", InputFieldFlagScope::payload, 9, false, no_paired_slot},
    InputFieldFlagMapping{"ChoiceListButton", InputFieldFlagScope::payload, 6, false, no_paired_slot},
    InputFieldFlagMapping{"Transparent", InputFieldFlagScope::base_info, 5, false, no_paired_slot},
    InputFieldFlagMapping{"MultiLine", InputFieldFlagScope::payload, 26, false, 3},
    InputFieldFlagMapping{"ExtendedEdit", InputFieldFlagScope::payload, 38, false, 6},
    InputFieldFlagMapping{"PasswordMode", InputFieldFlagScope::payload, 5, false, 5},
    InputFieldFlagMapping{"AutoMarkIncomplete", InputFieldFlagScope::payload, 35, false, no_paired_slot},
    InputFieldFlagMapping{"AutoChoiceIncomplete", InputFieldFlagScope::payload, 36, false, no_paired_slot},
};

list_stream::ListValue& input_field_flag_value(LV& info, const InputFieldFlagMapping& mapping) {
    switch (mapping.scope) {
        case InputFieldFlagScope::payload: return info.items[2].items[0].items[mapping.slot];
        case InputFieldFlagScope::base_info: return info.items[2].items[0].items[0].items[mapping.slot];
        case InputFieldFlagScope::root_info: return info.items[mapping.slot];
    }
    throw std::logic_error("unknown InputField flag scope");
}

const list_stream::ListValue& input_field_flag_value(
    const LV& info, const InputFieldFlagMapping& mapping) {
    switch (mapping.scope) {
        case InputFieldFlagScope::payload: return info.items[2].items[0].items[mapping.slot];
        case InputFieldFlagScope::base_info: return info.items[2].items[0].items[0].items[mapping.slot];
        case InputFieldFlagScope::root_info: return info.items[mapping.slot];
    }
    throw std::logic_error("unknown InputField flag scope");
}

using InputFieldFlagValues = std::array<bool, input_field_flag_mappings.size()>;

struct InputFieldTextValues {
    std::string tool_tip;
    std::string format;
};

struct InputFieldLayoutValues {
    std::int32_t horizontal_align = 4;
    std::int32_t vertical_align = 0;
    std::int32_t choice_list_height = 0;
};

InputFieldFlagValues decode_input_field_flags(const LV& info, std::string_view path) {
    const auto paired_path = child_path(path, 3);
    require_arity(info.items[3], 2, paired_path);
    require_arity(info.items[3].items[1], 2, child_path(paired_path, 1));
    require_arity(info.items[3].items[1].items[1], 7, child_path(child_path(paired_path, 1), 1));
    InputFieldFlagValues flags{};
    for (std::size_t index = 0; index < input_field_flag_mappings.size(); ++index) {
        const auto& mapping = input_field_flag_mappings[index];
        const auto flag_path = mapping.scope == InputFieldFlagScope::root_info ?
            child_path(path, mapping.slot) : mapping.scope == InputFieldFlagScope::base_info ?
                child_path(child_path(child_path(child_path(path, 2), 0), 0), mapping.slot) :
                child_path(child_path(child_path(path, 2), 0), mapping.slot);
        flags[index] = bool_atom(input_field_flag_value(info, mapping), flag_path);
        if (mapping.paired_slot != no_paired_slot) {
            const auto paired_value = bool_atom(
                info.items[3].items[1].items[1].items[mapping.paired_slot],
                child_path(child_path(child_path(child_path(path, 3), 1), 1), mapping.paired_slot));
            if (paired_value != flags[index]) {
                fail("OOF1122", flag_path, "matching paired InputField Boolean", paired_value ? "1" : "0",
                    "InputField payload and paired control-info flags disagree");
            }
        }
    }
    return flags;
}

LV canonical_input_field_info(
    const model::TypeDomainPatternValue& type,
    bool enabled,
    bool read_only,
    const InputFieldFlagValues& flags,
    const InputFieldTextValues& text_values,
    const InputFieldLayoutValues& layout_values) {
    auto value = parse_constant(R"OOF(
{9,{"Pattern",{"S",10,1}},{{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},31,0,0,1,0,0,0,0,0,0,1,0,0,10,0,0,4,0,{"U"},{"U"},"",0,1,0,0,0,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},0,0,0,{0,0,0},{1,0},0,0,0,0,0,0,0,16777215,2,0,0}},{1,{9a7643d2-19e9-45e2-8893-280bc9195a97,{4,{"U"},{"U"},0,"",0,0}}},{0},0,1,0,{1,0},0}
)OOF");
    if (value.items.size() != 10 || value.items[2].items.size() != 1 ||
        value.items[2].items[0].items.size() != 46 ||
        value.items[2].items[0].items[0].items.size() != 21) {
        throw std::logic_error("canonical InputField profile is malformed");
    }
    value.items[1] = encoded_type_domain(type, "$/InputField/TypeDomain");
    value.items[2].items[0].items[0].items[1] = raw(enabled ? "1" : "0");
    value.items[2].items[0].items[0].items[12] = encoded_localized(text_values.tool_tip);
    value.items[2].items[0].items[13] = raw(read_only ? "1" : "0");
    value.items[2].items[0].items[34] = encoded_localized(text_values.format);
    value.items[2].items[0].items[17] = raw(std::to_string(layout_values.horizontal_align));
    value.items[2].items[0].items[18] = raw(std::to_string(layout_values.vertical_align));
    value.items[2].items[0].items[31] = raw(std::to_string(layout_values.choice_list_height));
    if (type.entries.empty()) {
        value.items[2].items[0].items[14] = raw("0");
    } else if (type.entries.size() == 1 && type.entries.front().term == model::TypeDomainTerm::string) {
        value.items[2].items[0].items[14] = raw(std::to_string(type.entries.front().string.length));
    } else {
        throw std::logic_error("canonical InputField profile only supports empty or single-string TypeDomain");
    }
    for (std::size_t index = 0; index < input_field_flag_mappings.size(); ++index) {
        const auto& mapping = input_field_flag_mappings[index];
        const bool encoded = flags[index];
        input_field_flag_value(value, mapping) = raw(encoded ? "1" : "0");
        if (mapping.paired_slot != no_paired_slot) {
            value.items[3].items[1].items[1].items[mapping.paired_slot] = raw(encoded ? "1" : "0");
        }
    }
    return value;
}


using IncomingAnchorLists = std::array<std::vector<GeometryIncomingAnchor>, 6>;

constexpr std::array<model::BindingCoordinate, 6> geometry_coordinates{
    model::BindingCoordinate::top,
    model::BindingCoordinate::bottom,
    model::BindingCoordinate::left,
    model::BindingCoordinate::right,
    model::BindingCoordinate::vertical_center,
    model::BindingCoordinate::horizontal_center,
};

std::int32_t source_platform_edge(model::BindingCoordinate coordinate, std::string_view path) {
    switch (coordinate) {
        case model::BindingCoordinate::top: return 0;
        case model::BindingCoordinate::bottom: return 1;
        case model::BindingCoordinate::left: return 2;
        case model::BindingCoordinate::right: return 3;
        case model::BindingCoordinate::vertical_center:
        case model::BindingCoordinate::horizontal_center:
            fail("OOF1122", std::string(path), "source edge Top, Bottom, Left, or Right", "center coordinate",
                "Center coordinates are supported only as binding targets");
    }
    fail("OOF1122", std::string(path), "valid binding coordinate", "out of range", "Binding coordinate is invalid");
}

std::int32_t target_platform_edge(model::BindingCoordinate coordinate, std::string_view path) {
    switch (coordinate) {
        case model::BindingCoordinate::top: return 0;
        case model::BindingCoordinate::bottom: return 1;
        case model::BindingCoordinate::left: return 2;
        case model::BindingCoordinate::right: return 3;
        case model::BindingCoordinate::vertical_center: return 4;
        case model::BindingCoordinate::horizontal_center: return 5;
    }
    fail("OOF1122", std::string(path), "valid target coordinate", "out of range", "Binding target coordinate is invalid");
}

model::BindingCoordinate model_target_coordinate(std::int32_t edge, std::string_view path) {
    switch (edge) {
        case 0: return model::BindingCoordinate::top;
        case 1: return model::BindingCoordinate::bottom;
        case 2: return model::BindingCoordinate::left;
        case 3: return model::BindingCoordinate::right;
        case 4: return model::BindingCoordinate::vertical_center;
        case 5: return model::BindingCoordinate::horizontal_center;
        default:
            fail("OOF1114", std::string(path), "platform target edge 0..5", std::to_string(edge),
                "Binding target edge is unsupported");
    }
}

std::size_t target_dependency_bucket(std::int32_t edge, std::string_view path) {
    static_cast<void>(model_target_coordinate(edge, path));
    switch (edge) {
        case 0: return 0;
        case 1: case 4: return 1;
        case 2: return 2;
        case 3: case 5: return 3;
        default: throw std::logic_error("validated target edge is outside dependency buckets");
    }
}

std::size_t geometry_slot(model::BindingCoordinate coordinate) {
    for (std::size_t index = 0; index < geometry_coordinates.size(); ++index) {
        if (geometry_coordinates[index] == coordinate) return index;
    }
    return geometry_coordinates.size();
}

struct DecodedGeometry {
    model::Position position;
    IncomingAnchorLists incoming;
    std::uint32_t ordinal = 0;
};

void validate_geometry_context(const GeometryContext& context, std::string_view path) {
    if (const auto* panel = std::get_if<model::ControlRef>(&context.owner);
        panel != nullptr && (panel->id().value() == 0 ||
            panel->id().value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
        fail("OOF1122", child_path(path, 0), "positive int64 Panel ID",
            std::to_string(panel->id().value()), "Geometry owner Panel ID is invalid");
    }
    if (context.sibling_ordinal >= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        fail("OOF1122", child_path(path, 2), "sibling ordinal below int32 maximum", "ordinal overflow",
            "Default control TabOrder would overflow");
    }
}

GeometryIncomingAnchor decode_incoming_anchor(
    const LV& tuple,
    std::string_view path) {
    require_arity(tuple, 3, path);
    require_raw_constant(tuple.items[0], "0", child_path(path, 0));
    const auto source_id = integer_atom<std::uint64_t>(tuple.items[1], child_path(path, 1));
    if (source_id == 0 || source_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1114", child_path(path, 1), "positive int64 source control ID", std::to_string(source_id),
            "Incoming anchor source ID is invalid");
    }
    const auto source_edge = integer_atom<std::int32_t>(tuple.items[2], child_path(path, 2));
    if (source_edge < 0 || source_edge > 3) {
        fail("OOF1114", child_path(path, 2), "incoming source edge 0..3", std::to_string(source_edge),
            "Incoming anchor source edge is unsupported");
    }
    return GeometryIncomingAnchor{source_id, source_edge};
}

IncomingAnchorLists decode_incoming_anchor_lists(
    const LV& value,
    std::size_t& cursor,
    std::string_view path) {
    IncomingAnchorLists incoming;
    for (std::size_t edge = 0; edge < incoming.size(); ++edge) {
        if (cursor >= value.items.size()) {
            fail("OOF1103", child_path(path, cursor), "incoming anchor count", "missing", "Geometry anchor count is missing");
        }
        const auto count = integer_atom<std::uint32_t>(value.items[cursor], child_path(path, cursor));
        ++cursor;
        if (count > value.items.size() || cursor > value.items.size() ||
            static_cast<std::size_t>(count) > value.items.size() - cursor) {
            fail("OOF1102", std::string(path), "complete incoming anchor records", "truncated", "Geometry anchor records exceed the record boundary");
        }
        for (std::uint32_t item = 0; item < count; ++item) {
            incoming[edge].push_back(decode_incoming_anchor(value.items[cursor], child_path(path, cursor)));
            ++cursor;
        }
    }
    return incoming;
}

DecodedGeometry decode_geometry(
    const LV& geometry,
    std::string_view path,
    const GeometryContext& context) {
    validate_geometry_context(context, path);
    require_list(geometry, path);
    if (geometry.items.size() < 23) {
        fail("OOF1102", std::string(path), "geometry prefix, six counts, and five tail values", describe(geometry),
            "Geometry record is too short");
    }
    require_raw_constant(at(geometry, 0, path), "8", child_path(path, 0));
    const auto left = integer_atom<std::int32_t>(geometry.items[1], child_path(path, 1));
    const auto top = integer_atom<std::int32_t>(geometry.items[2], child_path(path, 2));
    const auto right = integer_atom<std::int32_t>(geometry.items[3], child_path(path, 3));
    const auto bottom = integer_atom<std::int32_t>(geometry.items[4], child_path(path, 4));
    const bool visible = bool_atom(geometry.items[5], child_path(path, 5));
    if (right < left || bottom < top) {
        fail("OOF1120", std::string(path), "right >= left and bottom >= top", describe(geometry),
            "Control geometry has negative dimensions");
    }
    const auto width = static_cast<std::int64_t>(right) - left;
    const auto height = static_cast<std::int64_t>(bottom) - top;
    if (width > std::numeric_limits<std::int32_t>::max() || height > std::numeric_limits<std::int32_t>::max()) {
        fail("OOF1120", std::string(path), "int32 dimensions", describe(geometry), "Control geometry overflows int32");
    }

    DecodedGeometry decoded;
    auto& position = decoded.position;
    if (left != 0) position.left.set(left);
    if (top != 0) position.top.set(top);
    if (width != 0) position.width.set(static_cast<std::int32_t>(width));
    if (height != 0) position.height.set(static_cast<std::int32_t>(height));
    if (!visible) position.visible.set(false);

    for (std::size_t slot = 0; slot < 6; ++slot) {
        const auto& record = geometry.items[6 + slot];
        const auto slot_path = child_path(path, 6 + slot);
        require_arity(record, 3, slot_path);
        require_raw_constant(record.items[0], "0", child_path(slot_path, 0));
        const auto& primary = record.items[1];
        const auto primary_path = child_path(slot_path, 1);
        require_arity(primary, 4, primary_path);
        require_raw_constant(primary.items[0], "2", child_path(primary_path, 0));
        const auto target_id = integer_atom<std::int64_t>(primary.items[1], child_path(primary_path, 1));
        const auto target_edge = integer_atom<std::int32_t>(primary.items[2], child_path(primary_path, 2));
        const auto offset = integer_atom<std::int32_t>(primary.items[3], child_path(primary_path, 3));
        std::optional<model::AnchorBinding> binding_value;
        if (target_id == -1) {
            require_exact(primary, parse_constant("{2,-1,6,0}"), primary_path,
                "Empty primary binding must use the platform sentinel");
        } else {
            if (target_id < 0) {
                fail("OOF1114", child_path(primary_path, 1), "Form ID 0 or positive control ID", std::to_string(target_id),
                    "Primary binding target ID is invalid");
            }
            model::AnchorBinding binding;
            binding.coordinate = geometry_coordinates[slot];
            binding.target_coordinate = model_target_coordinate(target_edge, child_path(primary_path, 2));
            if (target_id != 0) {
                binding.target = model::ControlRef{model::ObjectId{static_cast<std::uint64_t>(target_id)}};
            } else if (const auto* panel = std::get_if<model::ControlRef>(&context.owner)) {
                binding.target = *panel;
            }
            if (offset != 0) binding.offset.set(offset);
            binding_value = std::move(binding);
        }
        const auto secondary_path = child_path(slot_path, 2);
        const auto& secondary = record.items[2];
        require_arity(secondary, 4, secondary_path);
        require_raw_constant(secondary.items[0], "2", child_path(secondary_path, 0));
        const auto secondary_id = integer_atom<std::int64_t>(secondary.items[1], child_path(secondary_path, 1));
        const auto secondary_edge = integer_atom<std::int32_t>(secondary.items[2], child_path(secondary_path, 2));
        const auto secondary_offset = integer_atom<std::int32_t>(secondary.items[3], child_path(secondary_path, 3));
        if (secondary_id == -1) {
            require_exact(secondary, parse_constant("{2,-1,6,0}"), secondary_path,
                "Empty proportional binding must use the platform sentinel");
        } else {
            if (!binding_value.has_value()) {
                fail("OOF1122", secondary_path, "proportional binding with a primary binding", describe(secondary),
                    "Proportional binding without a primary binding is not representable in the model");
            }
            if (secondary_id < 0) {
                fail("OOF1114", child_path(secondary_path, 1), "Form ID 0 or positive control ID", std::to_string(secondary_id),
                    "Proportional binding target ID is invalid");
            }
            model::AnchorBindingTarget target;
            target.coordinate = model_target_coordinate(secondary_edge, child_path(secondary_path, 2));
            if (secondary_id != 0) {
                target.target = model::ControlRef{model::ObjectId{static_cast<std::uint64_t>(secondary_id)}};
            } else if (const auto* panel = std::get_if<model::ControlRef>(&context.owner)) {
                target.target = *panel;
            }
            if (secondary_offset != 0) target.offset.set(secondary_offset);
            binding_value->proportional = std::move(target);
        }
        if (binding_value.has_value()) position.bindings.anchors.push_back(std::move(*binding_value));
    }

    std::size_t cursor = 12;
    decoded.incoming = decode_incoming_anchor_lists(geometry, cursor, path);
    if (geometry.items.size() != cursor + 5) {
        fail("OOF1102", std::string(path), "dynamic incoming records followed by five tail values", describe(geometry),
            "Geometry record has an unexpected trailing shape");
    }
    const auto page = integer_atom<std::uint32_t>(geometry.items[cursor], child_path(path, cursor));
    if (page != context.page_index) {
        fail("OOF1114", child_path(path, cursor), "geometry page index matching its owner context",
            std::to_string(page), "Geometry page index does not match its owner context");
    }
    decoded.ordinal = integer_atom<std::uint32_t>(geometry.items[cursor + 1], child_path(path, cursor + 1));
    if (decoded.ordinal != context.sibling_ordinal) {
        fail("OOF1114", child_path(path, cursor + 1), "logical child ordinal", describe(geometry),
            "Geometry child ordinal is inconsistent");
    }
    const auto tab_order = integer_atom<std::int32_t>(geometry.items[cursor + 2], child_path(path, cursor + 2));
    if (tab_order <= 0) fail("OOF1114", child_path(path, cursor + 2), "positive TabOrder", std::to_string(tab_order),
        "Geometry TabOrder must be positive");
    if (static_cast<std::uint64_t>(tab_order) != static_cast<std::uint64_t>(context.sibling_ordinal) + 1)
        position.tab_order.set(std::optional<std::int32_t>{tab_order});
    const bool manual_horizontal = bool_atom(geometry.items[cursor + 3], child_path(path, cursor + 3));
    const bool manual_vertical = bool_atom(geometry.items[cursor + 4], child_path(path, cursor + 4));
    if (manual_horizontal) position.bindings.manual_horizontal.set(true);
    if (manual_vertical) position.bindings.manual_vertical.set(true);
    return decoded;
}

struct GeometryPageOrdinal { std::uint32_t page; std::uint32_t ordinal; std::int32_t tab_order; };

GeometryPageOrdinal geometry_page_ordinal(
    const LV& geometry,
    std::string_view path,
    std::size_t& ordinal_slot) {
    require_list(geometry, path);
    std::size_t cursor = 12;
    for (std::size_t edge = 0; edge < 6; ++edge) {
        if (cursor >= geometry.items.size()) {
            fail("OOF1103", child_path(path, cursor), "incoming anchor count", "missing", "Geometry anchor count is missing");
        }
        const auto count = integer_atom<std::uint32_t>(geometry.items[cursor], child_path(path, cursor));
        ++cursor;
        if (count > geometry.items.size() || static_cast<std::size_t>(count) > geometry.items.size() - cursor) {
            fail("OOF1102", std::string(path), "complete incoming anchor records", "truncated", "Geometry anchor records exceed the record boundary");
        }
        cursor += count;
    }
    if (geometry.items.size() != cursor + 5) {
        fail("OOF1102", std::string(path), "dynamic incoming records followed by five tail values", describe(geometry),
            "Geometry record has an unexpected trailing shape");
    }
    const auto page = integer_atom<std::uint32_t>(geometry.items[cursor], child_path(path, cursor));
    const auto ordinal = integer_atom<std::uint32_t>(geometry.items[cursor + 1], child_path(path, cursor + 1));
    ordinal_slot = cursor + 1;
    const auto tab_order = integer_atom<std::int32_t>(geometry.items[cursor + 2], child_path(path, cursor + 2));
    if (ordinal == std::numeric_limits<std::uint32_t>::max() || tab_order <= 0) {
        fail("OOF1114", child_path(path, cursor + 2), "positive TabOrder and safe child ordinal", std::to_string(tab_order),
            "Geometry order values are invalid");
    }
    return {page, ordinal, tab_order};
}

std::vector<GeometryIncomingAnchor> sorted_incoming(std::vector<GeometryIncomingAnchor> incoming) {
    std::sort(incoming.begin(), incoming.end(), [](const auto& left, const auto& right) {
        return std::tie(left.source_control_id, left.source_edge) <
               std::tie(right.source_control_id, right.source_edge);
    });
    return incoming;
}

LV encode_geometry(
    const model::Position& position,
    const GeometryContext& context,
    const IncomingAnchorLists& incoming) {
    validate_geometry_context(context, "$/Position");
    if (position.default_control.is_explicit() ||
        position.z_order.is_explicit() || position.collapse.is_explicit() ||
        !position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/Position", "geometry Position without unsupported control or dimension properties",
            "unsupported Position property", "Control geometry codec cannot represent this Position property");
    }
    const auto left = position.left.value();
    const auto top = position.top.value();
    const auto width = position.width.value();
    const auto height = position.height.value();
    if (width < 0 || height < 0 || left > std::numeric_limits<std::int32_t>::max() - width ||
        top > std::numeric_limits<std::int32_t>::max() - height) {
        fail("OOF1120", "$/Position", "non-negative geometry without int32 overflow", "invalid geometry",
            "Control geometry cannot be represented by the platform record");
    }
    std::array<std::optional<LV>, 6> primary;
    for (const auto& binding : position.bindings.anchors) {
        const auto slot = geometry_slot(binding.coordinate);
        if (slot >= primary.size()) {
            fail("OOF1122", "$/Position/Bindings", "supported primary source edge", "center coordinate",
                "Center-edge binding storage has not been established");
        }
        if (primary[slot].has_value()) {
            fail("OOF1122", "$/Position/Bindings", "unique source edge", "duplicate", "Duplicate primary binding coordinate");
        }
        static_cast<void>(source_platform_edge(binding.coordinate, "$/Position/Bindings/coordinate"));
        const auto target_edge = target_platform_edge(binding.target_coordinate, "$/Position/Bindings/targetCoordinate");
        const auto encoded_target_id = [&](const std::optional<model::ControlRef>& target, std::string_view path) {
            const auto* panel = std::get_if<model::ControlRef>(&context.owner);
            if (!target.has_value()) {
                if (panel != nullptr) {
                    fail("OOF1122", std::string(path), "owning Panel target for nested geometry", "Form target",
                        "Form target is not representable in nested geometry");
                }
                return std::uint64_t{0};
            }
            const auto target_id = target->id().value();
            if (target_id == 0 || target_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                fail("OOF1122", std::string(path), "positive int64 target control ID", std::to_string(target_id),
                    "Control binding target ID cannot be represented");
            }
            if (panel != nullptr && target_id == panel->id().value()) return std::uint64_t{0};
            return target_id;
        };
        const std::uint64_t target_id = encoded_target_id(binding.target, "$/Position/Bindings/targetId");
        LV secondary = parse_constant("{2,-1,6,0}");
        if (binding.proportional.has_value()) {
            const auto& target = *binding.proportional;
            const auto proportional_edge = target_platform_edge(target.coordinate, "$/Position/Bindings/ProportionalBinding/targetCoordinate");
            const std::uint64_t proportional_id = encoded_target_id(
                target.target, "$/Position/Bindings/ProportionalBinding/targetId");
            secondary = list({raw("2"), raw(std::to_string(proportional_id)), raw(std::to_string(proportional_edge)),
                raw(std::to_string(target.offset.value()))});
        }
        primary[slot] = list({raw("0"), list({raw("2"), raw(std::to_string(target_id)), raw(std::to_string(target_edge)),
            raw(std::to_string(binding.offset.value()))}), std::move(secondary)});
    }

    std::vector<LV> values{
        raw("8"), raw(std::to_string(left)), raw(std::to_string(top)), raw(std::to_string(left + width)),
        raw(std::to_string(top + height)), raw(position.visible.value() ? "1" : "0")};
    for (std::size_t slot = 0; slot < primary.size(); ++slot) {
        values.push_back(primary[slot].value_or(parse_constant("{0,{2,-1,6,0},{2,-1,6,0}}")));
    }
    for (const auto& list_for_edge : incoming) {
        auto ordered = sorted_incoming(list_for_edge);
        values.push_back(raw(std::to_string(ordered.size())));
        for (const auto& anchor : ordered) {
            values.push_back(list({raw("0"), raw(std::to_string(anchor.source_control_id)), raw(std::to_string(anchor.source_edge))}));
        }
    }
    for (std::size_t edge = 0; edge < incoming.size(); ++edge) {
        for (std::size_t index = 0; index < incoming[edge].size(); ++index) {
            const auto& anchor = incoming[edge][index];
            const auto anchor_path = "$/Position/Incoming/" + std::to_string(edge) + "/" + std::to_string(index);
            if (anchor.source_control_id == 0 ||
                anchor.source_control_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                fail("OOF1122", anchor_path, "positive int64 source control ID",
                    std::to_string(anchor.source_control_id), "Incoming anchor source ID cannot be represented");
            }
            if (anchor.source_edge < 0 || anchor.source_edge > 3) {
                fail("OOF1122", anchor_path, "incoming source edge 0..3", std::to_string(anchor.source_edge),
                    "Incoming anchor source edge is unsupported");
            }
        }
    }
    values.push_back(raw(std::to_string(context.page_index)));
    values.push_back(raw(std::to_string(context.sibling_ordinal)));
    const auto tab_order = position.tab_order.is_explicit()
        ? position.tab_order.value() : std::optional<std::int32_t>{static_cast<std::int32_t>(context.sibling_ordinal + 1)};
    if (!tab_order || *tab_order <= 0) fail("OOF1122", "$/Position/TabOrder", "positive integer TabOrder", "invalid value",
        "Control TabOrder must be a positive integer");
    values.push_back(raw(std::to_string(*tab_order)));
    values.push_back(raw(position.bindings.manual_horizontal.value() ? "1" : "0"));
    values.push_back(raw(position.bindings.manual_vertical.value() ? "1" : "0"));
    return list(std::move(values));
}

void insert_incoming_anchor_lists(LV& value, const IncomingAnchorLists& incoming, std::size_t prefix_count) {
    std::vector<LV> records;
    for (const auto& list_for_edge : incoming) {
        auto ordered = sorted_incoming(list_for_edge);
        records.push_back(raw(std::to_string(ordered.size())));
        for (const auto& anchor : ordered) {
            records.push_back(list({raw("0"), raw(std::to_string(anchor.source_control_id)), raw(std::to_string(anchor.source_edge))}));
        }
    }
    if (value.items.size() < prefix_count + incoming.size()) {
        throw std::logic_error("root-panel canonical layout has no six incoming count slots");
    }
    value.items.erase(
        value.items.begin() + static_cast<std::ptrdiff_t>(prefix_count),
        value.items.begin() + static_cast<std::ptrdiff_t>(prefix_count + incoming.size()));
    value.items.insert(value.items.begin() + static_cast<std::ptrdiff_t>(prefix_count), records.begin(), records.end());
}

bool explicit_bool(const model::PropertySet& properties, std::string_view name, bool default_value);
void require_allowed_properties(
    const model::PropertySet& properties,
    std::initializer_list<std::string_view> allowed,
    std::string_view path);

struct DecodedOwnerPages {
    std::vector<model::Page> pages;
    IncomingAnchorLists incoming;
    model::PanelPayload panel;
};

DecodedOwnerPages decode_owner_pages(
    const LV& envelope,
    std::uint64_t& next_page_id,
    std::optional<model::ControlRef> owner,
    bool panel,
    std::string_view path) {
    require_arity(envelope, 3, path);
    require_exact(envelope.items[0], raw("1"), child_path(path, 0), "Owner page envelope marker is unsupported");
    require_exact(envelope.items[2], list({raw("0")}), child_path(path, 2),
        "Owner page envelope trailer is unsupported");
    const auto& payload = envelope.items[1];
    std::size_t incoming_end = 2;
    auto incoming = decode_incoming_anchor_lists(payload, incoming_end, path);
    auto normalized = payload;
    normalized.items.erase(normalized.items.begin() + 2,
        normalized.items.begin() + static_cast<std::ptrdiff_t>(incoming_end));
    auto decoded_pages = decode_page_table(at(normalized, 5, path), next_page_id, child_path(path, 5));
    if (!decoded_pages) throw DecodeFailure(decoded_pages.diagnostics().front());
    auto pages = std::move(decoded_pages.value());
    if (pages.empty()) {
        fail("OOF1122", child_path(path, 5), "non-empty owner Page table", "empty",
            "Owner Page table cannot be empty");
    }
    const auto count = pages.size();
    if (integer_atom<std::uint32_t>(at(normalized, 9, path), child_path(path, 9)) != count * 4) {
        fail("OOF1114", child_path(path, 9), std::to_string(count * 4), describe(at(normalized, 9, path)),
            "Owner boundary count does not match its Page table");
    }
    if (normalized.items.size() < 10 + count * 4) {
        fail("OOF1102", std::string(path), "four boundary rows per Page", describe(normalized),
            "Owner Page boundary table is truncated");
    }
    for (std::size_t page_index = 0; page_index < count; ++page_index) {
        LV boundaries = list(std::vector<LV>(
            normalized.items.begin() + static_cast<std::ptrdiff_t>(10 + page_index * 4),
            normalized.items.begin() + static_cast<std::ptrdiff_t>(14 + page_index * 4)));
        auto position = decode_page_position(boundaries, static_cast<std::uint32_t>(page_index), owner);
        if (!position) throw DecodeFailure(position.diagnostics().front());
        pages[page_index].position.set(std::move(position.value()));
    }
    auto expected = panel ? canonical_panel_payload(pages, *owner) :
        canonical_root_panel_payload(8, 8, pages, owner);
    expected.items[1].items.erase(expected.items[1].items.begin() + 2,
        expected.items[1].items.begin() + 8);
    const auto auto_tab_order = bool_atom(normalized.items[7], child_path(child_path(path, 1), 7 + incoming_end - 2));
    expected.items[1].items[7] = raw(auto_tab_order ? "1" : "0");
    model::PanelPayload decoded_panel;
    if (!auto_tab_order) decoded_panel.properties.set_explicit(model::PropertyId::from_name("AutoTabOrder"), false);
    require_arity(normalized.items[0], expected.items[1].items[0].items.size(), child_path(child_path(path, 1), 0));
    for (const auto& [name, slot] : std::array<std::pair<std::string_view, std::size_t>, 3>{{
        {"BorderColor", 6}, {"TextColor", 3}, {"BackColor", 2}}}) {
        const auto color_path = child_path(child_path(child_path(path, 1), 0), slot);
        const auto color = decode_button_color(normalized.items[0].items[slot], color_path);
        if (color != model::ColorValue{}) decoded_panel.properties.set_explicit(model::PropertyId::from_name(name), color);
        expected.items[1].items[0].items[slot] = encode_button_color(color, color_path);
        if (name == "BackColor") expected.items[1].items[expected.items[1].items.size() - 6] = encode_button_color(color, color_path);
    }
    const auto mismatch = std::mismatch(normalized.items.begin(), normalized.items.end(),
        expected.items[1].items.begin(), expected.items[1].items.end(), [](const auto& left, const auto& right) {
            return list_stream::dump_compact(left) == list_stream::dump_compact(right);
        });
    if (mismatch.first != normalized.items.end() || mismatch.second != expected.items[1].items.end()) {
        const auto index = static_cast<std::size_t>(mismatch.first - normalized.items.begin());
        const auto original_index = index < 2 ? index : index + incoming_end - 2;
        fail("OOF1114", child_path(child_path(path, 1), original_index), "supported named owner property",
            "changed owner property", "Owner page property contains an unsupported variation");
    }
    if (pages.size() > std::numeric_limits<std::uint64_t>::max() - next_page_id) {
        fail("OOF1122", std::string(path), "Page IDs within uint64 range", std::to_string(pages.size()),
            "Page ID allocation overflows uint64");
    }
    next_page_id += pages.size();
    return DecodedOwnerPages{std::move(pages), std::move(incoming), std::move(decoded_panel)};
}

LV encode_owner_pages(
    const std::vector<model::Page>& pages,
    const IncomingAnchorLists& incoming,
    bool panel,
    std::optional<model::ControlRef> owner = std::nullopt,
    std::int32_t width = 8,
    std::int32_t height = 8,
    const model::PropertySet& properties = {}) {
    auto envelope = panel ? canonical_panel_payload(pages, *owner) :
        canonical_root_panel_payload(width, height, pages, owner);
    require_allowed_properties(properties, {"AutoTabOrder", "BorderColor", "TextColor", "BackColor"}, "$/Panel");
    envelope.items[1].items[13] = raw(explicit_bool(properties, "AutoTabOrder", true) ? "1" : "0");
    for (const auto& [name, slot] : std::array<std::pair<std::string_view, std::size_t>, 3>{{
        {"BorderColor", 6}, {"TextColor", 3}, {"BackColor", 2}}}) {
        const auto color = properties.contains(model::PropertyId::from_name(name)) ? explicit_button_color(properties, name) : model::ColorValue{};
        const auto encoded_color = encode_button_color(color, std::string("$/Panel/") + std::string(name));
        envelope.items[1].items[0].items[slot] = encoded_color;
        if (name == "BackColor") envelope.items[1].items[envelope.items[1].items.size() - 6] = encoded_color;
    }
    insert_incoming_anchor_lists(envelope.items[1], incoming, 2);
    return envelope;
}


void require_incoming_graph(
    const IncomingAnchorLists& observed,
    IncomingAnchorLists expected,
    std::string_view path) {
    for (std::size_t edge = 0; edge < observed.size(); ++edge) {
        auto actual = sorted_incoming(observed[edge]);
        auto wanted = sorted_incoming(std::move(expected[edge]));
        if (actual != wanted) {
            fail("OOF1114", child_path(path, edge), "incoming tuples matching primary bindings", "stale or dangling tuples",
                "Incoming anchor fanout disagrees with the control binding graph");
        }
    }
}

bool is_single_string_type_domain(const model::TypeDomainPatternValue& value) {
    return value.entries.size() == 1 && value.entries.front().term == model::TypeDomainTerm::string;
}

bool is_single_numeric_type_domain(const model::TypeDomainPatternValue& value) {
    return value.entries.size() == 1 && value.entries.front().term == model::TypeDomainTerm::numeric;
}

bool decimal_fits_numeric_qualifiers(
    std::string_view canonical,
    const model::NumericQualifiers& qualifiers) {
    if (canonical.empty()) return false;
    const bool negative = canonical.front() == '-';
    if (negative && qualifiers.non_negative) return false;
    if (negative) canonical.remove_prefix(1);
    const auto point = canonical.find('.');
    const auto integer = point == std::string_view::npos ? canonical : canonical.substr(0, point);
    const auto fraction = point == std::string_view::npos ? std::string_view{} : canonical.substr(point + 1);
    const auto significant_integer = integer.find_first_not_of('0');
    const std::size_t integer_digits = significant_integer == std::string_view::npos
        ? 0 : integer.size() - significant_integer;
    return qualifiers.precision <= qualifiers.length &&
           integer_digits <= qualifiers.length - qualifiers.precision &&
           fraction.size() <= qualifiers.precision;
}

LV encoded_radio_selection_value(const std::optional<model::DecimalValue>& value) {
    if (!value) return list({string_value("U")});
    try {
        return list({string_value("N"), raw(value_codec::canonical_decimal(value->canonical))});
    } catch (const std::invalid_argument& error) {
        fail("OOF1122", "$/RadioButton/SelectionValue", "xs:decimal", value->canonical, error.what());
    }
}

std::optional<model::DecimalValue> decoded_radio_selection_value(
    const LV& value,
    std::string_view path) {
    require_list(value, path);
    if (value.items.size() == 1 && string_atom(value.items[0], child_path(path, 0)) == "U") {
        return std::nullopt;
    }
    require_arity(value, 2, path);
    if (string_atom(value.items[0], child_path(path, 0)) != "N") {
        fail("OOF1122", std::string(path), "numeric SelectionValue", describe(value),
            "RadioButton SelectionValue is outside the numeric observed profile");
    }
    try {
        return model::DecimalValue{value_codec::canonical_decimal(
            raw_atom(value.items[1], child_path(path, 1)))};
    } catch (const std::invalid_argument&) {
        fail("OOF1122", std::string(path), "canonical numeric SelectionValue", describe(value),
            "RadioButton SelectionValue has an invalid numeric atom");
    }
}

bool is_single_boolean_type_domain(const model::TypeDomainPatternValue& value) {
    return value.entries.size() == 1 && value.entries.front().term == model::TypeDomainTerm::boolean;
}

bool is_single_value_table_type_domain(const model::TypeDomainPatternValue& value) {
    model::TypeDomainEntry expected;
    expected.term = model::TypeDomainTerm::value_table;
    return value.entries.size() == 1 && value.entries.front() == expected;
}

enum class ActionMetadataPolicy { handler_derived, empty };

LV encode_action(std::string_view handler, ActionMetadataPolicy policy, std::string_view path) {
    if (handler.empty()) {
        fail("OOF1122", std::string(path), "non-empty Action handler", "empty", "Action handler cannot be empty");
    }
    const auto name = policy == ActionMetadataPolicy::handler_derived ? handler : std::string_view{};
    const auto presentation = encoded_localized(name);
    return list({raw("3"), string_value(std::string(handler)), list({
        raw("1"), string_value(std::string(name)), presentation, presentation, presentation,
        parse_constant("{4,0,{0},\"\",-1,-1,1,0,\"\"}"), parse_constant("{0,0,0}")})});
}

std::string decode_action(const LV& payload, ActionMetadataPolicy policy, std::string_view path) {
    require_arity(payload, 3, path);
    require_raw_constant(payload.items[0], "3", child_path(path, 0));
    const auto handler = string_atom(payload.items[1], child_path(path, 1));
    if (handler.empty()) {
        fail("OOF1115", child_path(path, 1), "non-empty Action handler", "empty", "Action handler cannot be empty");
    }
    const auto& metadata = payload.items[2];
    const auto metadata_path = child_path(path, 2);
    require_arity(metadata, 7, metadata_path);
    // Build the exact defaults through the same invariant boundary used by the writer.
    const auto expected = encode_action(handler, policy, path).items[2];
    require_exact(metadata.items[0], expected.items[0], child_path(metadata_path, 0), "Action metadata version is unsupported");
    require_exact(metadata.items[1], expected.items[1], child_path(metadata_path, 1), "Action name differs from its metadata policy");
    for (std::size_t index = 2; index <= 4; ++index) {
        const auto field_path = child_path(metadata_path, index);
        if (policy == ActionMetadataPolicy::handler_derived) {
            // Preserve the existing localized-string reader and its line-ending normalization.
            const auto presentation = decoded_single_language_text(metadata.items[index], field_path);
            if (presentation != handler) {
                fail("OOF1114", field_path, handler, presentation, "Action presentation differs from its handler");
            }
        } else {
            require_exact(metadata.items[index], expected.items[index], field_path, "Action presentation must be empty");
        }
    }
    require_exact(metadata.items[5], expected.items[5], child_path(metadata_path, 5), "Action style record is unsupported");
    require_exact(metadata.items[6], expected.items[6], child_path(metadata_path, 6), "Action tail record is unsupported");
    return handler;
}

LV canonical_event_table(std::optional<std::string_view> handler) {
    if (!handler) {
        return list({raw("0")});
    }
    const auto* descriptor = model::metamodel::find_event(model::ControlKind::button, "Click");
    if (descriptor == nullptr || descriptor->storage_tag.empty()) {
        throw std::logic_error("Button.Click has no executable storage tag");
    }
    return list({raw("1"), list({raw("0"), raw(std::string(descriptor->storage_tag)),
        encode_action(*handler, ActionMetadataPolicy::handler_derived, "$/Button/Events/Click")})});
}

std::optional<std::string> decode_button_event(const LV& value, std::string_view path) {
    require_list(value, path);
    if (value.items.empty()) {
        fail("OOF1103", child_path(path, 0), "event count", "missing", "Event table has no count");
    }
    const std::uint32_t count = integer_atom<std::uint32_t>(value.items[0], child_path(path, 0));
    if (count == 0) {
        require_arity(value, 1, path);
        return std::nullopt;
    }
    if (count != 1) {
        fail(
            "OOF1116",
            std::string(path),
            "zero or one Button.Click event",
            describe(value),
            "The product slice does not support multiple button event records");
    }
    require_arity(value, 2, path);
    const auto& event_record = value.items[1];
    const std::string event_path = child_path(path, 1);
    require_arity(event_record, 3, event_path);
    require_raw_constant(event_record.items[0], "0", child_path(event_path, 0));
    const auto* descriptor = model::metamodel::find_event(model::ControlKind::button, "Click");
    if (descriptor == nullptr || descriptor->storage_tag.empty()) {
        throw std::logic_error("Button.Click has no executable storage tag");
    }
    require_raw_constant(
        event_record.items[1],
        descriptor->storage_tag,
        child_path(event_path, 1));

    return decode_action(event_record.items[2], ActionMetadataPolicy::handler_derived, child_path(event_path, 2));
}

LV encode_form_close_events(const model::OrdinaryFormDocument& document) {
    const auto& references = document.form().events;
    if (references.empty()) return list({raw("0")});
    if (references.size() != 1) fail("OOF1122", "$/Form/Events", "zero or one owned Form.OnClose", std::to_string(references.size()), "Form event collection is unsupported");
    const auto* event = document.find_event(references.front().id());
    if (event == nullptr || event->name != "OnClose" || event->handler.empty() ||
        !std::holds_alternative<model::FormRef>(event->owner) ||
        std::get<model::FormRef>(event->owner).id() != document.form().id) {
        fail("OOF1122", "$/Form/Events", "owned Form.OnClose with non-empty handler", "different event", "Form event cannot be encoded");
    }
    const auto* descriptor = model::metamodel::find_form_event("OnClose");
    if (descriptor == nullptr || descriptor->storage_codec != model::metamodel::StorageCodec::event_record || descriptor->storage_tag.empty())
        throw std::logic_error("Form.OnClose has no executable storage descriptor");
    return list({raw("1"), list({raw("70003"), raw(std::string(descriptor->storage_tag)),
        encode_action(event->handler, ActionMetadataPolicy::empty, "$/Form/Events/OnClose")})});
}

std::optional<std::string> decode_form_close_events(const LV& value, std::string_view path) {
    require_list(value, path);
    if (value.items.empty()) fail("OOF1103", std::string(path), "event count", "missing", "Form event table has no count");
    const auto count = integer_atom<std::uint32_t>(value.items[0], child_path(path, 0));
    if (count == 0) { require_arity(value, 1, path); return std::nullopt; }
    if (count != 1) fail("OOF1116", std::string(path), "zero or one Form.OnClose", std::to_string(count), "Multiple form events are unsupported");
    require_arity(value, 2, path);
    const auto& event = value.items[1]; const auto event_path = child_path(path, 1);
    require_arity(event, 3, event_path);
    require_raw_constant(event.items[0], "70003", child_path(event_path, 0));
    const auto* descriptor = model::metamodel::find_form_event("OnClose");
    if (descriptor == nullptr || descriptor->storage_codec != model::metamodel::StorageCodec::event_record || descriptor->storage_tag.empty())
        throw std::logic_error("Form.OnClose has no executable storage descriptor");
    require_raw_constant(event.items[1], descriptor->storage_tag, child_path(event_path, 1));
    return decode_action(event.items[2], ActionMetadataPolicy::empty, child_path(event_path, 2));
}

struct DecodedPictureDescriptor {
    std::vector<std::uint8_t> bytes;
    bool transparent = false;
    model::PictureFormat format = model::PictureFormat::gif;
    std::optional<std::string> standard_name;
};

LV canonical_button_picture() {
    return parse_constant("{4,0,{0},\"\",-1,-1,1,0,\"\"}");
}

std::string_view picture_format_extension(model::PictureFormat format) {
    switch (format) {
        case model::PictureFormat::gif: return "gif";
        case model::PictureFormat::png: return "png";
        case model::PictureFormat::jpeg: return "jpeg";
        case model::PictureFormat::bmp: return "bmp";
    }
    return {};
}

std::optional<model::PictureFormat> picture_format_from_bytes(const std::vector<std::uint8_t>& bytes) {
    constexpr std::array<std::uint8_t, 6> gif87{'G','I','F','8','7','a'};
    constexpr std::array<std::uint8_t, 6> gif89{'G','I','F','8','9','a'};
    constexpr std::array<std::uint8_t, 8> png{137,80,78,71,13,10,26,10};
    if (bytes.size() >= gif87.size() &&
        (std::equal(gif87.begin(), gif87.end(), bytes.begin()) || std::equal(gif89.begin(), gif89.end(), bytes.begin()))) return model::PictureFormat::gif;
    if (bytes.size() >= png.size() && std::equal(png.begin(), png.end(), bytes.begin())) return model::PictureFormat::png;
    if (bytes.size() >= 3 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) return model::PictureFormat::jpeg;
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') return model::PictureFormat::bmp;
    return std::nullopt;
}

std::string encode_base64(std::span<const std::uint8_t> bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t a = bytes[index];
        const std::uint32_t b = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const std::uint32_t c = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const std::uint32_t triple = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(triple >> 18) & 63]);
        output.push_back(alphabet[(triple >> 12) & 63]);
        output.push_back(index + 1 < bytes.size() ? alphabet[(triple >> 6) & 63] : '=');
        output.push_back(index + 2 < bytes.size() ? alphabet[triple & 63] : '=');
    }
    return output;
}

bool same_list_value(const LV& left, const LV& right) {
    if (left.is_list != right.is_list || left.atom_kind != right.atom_kind || left.atom != right.atom || left.items.size() != right.items.size()) return false;
    for (std::size_t index = 0; index < left.items.size(); ++index) {
        if (!same_list_value(left.items[index], right.items[index])) return false;
    }
    return true;
}

std::vector<std::uint8_t> decode_base64(std::string_view input, std::string_view path) {
    auto value_of = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };
    if (input.empty() || input.size() % 4 != 0) {
        fail("OOF1114", std::string(path), "valid non-empty base64 picture bytes", "malformed", "Button picture data is malformed");
    }
    std::vector<std::uint8_t> output;
    output.reserve((input.size() / 4) * 3);
    for (std::size_t index = 0; index < input.size(); index += 4) {
        const bool last = index + 4 == input.size();
        const int a = value_of(input[index]);
        const int b = value_of(input[index + 1]);
        const int c = input[index + 2] == '=' ? 0 : value_of(input[index + 2]);
        const int d = input[index + 3] == '=' ? 0 : value_of(input[index + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (!last && (input[index + 2] == '=' || input[index + 3] == '=')) ||
            (input[index + 2] == '=' && input[index + 3] != '=')) {
            fail("OOF1114", std::string(path), "canonical base64 picture bytes", "malformed", "Button picture data is malformed");
        }
        const std::uint32_t triple = (static_cast<std::uint32_t>(a) << 18) |
            (static_cast<std::uint32_t>(b) << 12) | (static_cast<std::uint32_t>(c) << 6) | static_cast<std::uint32_t>(d);
        output.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xff));
        if (input[index + 2] != '=') output.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xff));
        if (input[index + 3] != '=') output.push_back(static_cast<std::uint8_t>(triple & 0xff));
    }
    return output;
}

constexpr std::size_t max_column_editor_packet_size = 1024 * 1024;
constexpr std::array<std::uint8_t, 18> table_column_editor_header{
    0x02, 0x01, 0x53, 0x4b, 0x6f, 0xf4, 0x88, 0x8d, 0xc1,
    0x4e, 0xa0, 0xd5, 0xeb, 0xb6, 0xbd, 0xa0, 0xa7, 0x0d};

struct InflateState {
    z_stream stream{};
    bool initialized = false;

    ~InflateState() {
        if (initialized) inflateEnd(&stream);
    }
};

struct DeflateState {
    z_stream stream{};
    bool initialized = false;

    ~DeflateState() {
        if (initialized) deflateEnd(&stream);
    }
};

std::vector<std::uint8_t> decode_table_column_editor_packet(
    const LV& encoded_packet,
    std::string_view path) {
    require_arity(encoded_packet, 2, path);
    const auto& encoded_chunks = encoded_packet.items[0];
    require_list(encoded_chunks, child_path(path, 0));
    require_raw_constant(encoded_packet.items[1], "0", child_path(path, 1));
    constexpr std::size_t max_encoded_packet_size = ((max_column_editor_packet_size + 2) / 3) * 4;
    if (encoded_chunks.items.empty() || encoded_chunks.items.size() > (max_encoded_packet_size + 63) / 64) {
        fail("OOF1114", child_path(path, 0), "bounded non-empty base64 packet chunks",
            std::to_string(encoded_chunks.items.size()), "Table Column editor packet has an invalid chunk count");
    }
    std::string encoded;
    for (std::size_t index = 0; index < encoded_chunks.items.size(); ++index) {
        const auto& chunk = encoded_chunks.items[index];
        const std::string chunk_path = child_path(child_path(path, 0), index);
        if (chunk.is_list || chunk.atom_kind != LV::AtomKind::raw) {
            fail("OOF1114", chunk_path, "raw base64 packet chunk", describe(chunk),
                "Table Column editor packet chunk has an invalid value kind");
        }
        const std::string_view atom = chunk.atom;
        const bool first = index == 0;
        if ((first && !atom.starts_with("#base64:")) || (!first && atom.starts_with("#base64:"))) {
            fail("OOF1114", chunk_path, "one initial #base64: marker followed by packet chunks",
                std::string(atom), "Table Column editor packet has malformed base64 chunk markers");
        }
        const auto data = first ? atom.substr(8) : atom;
        if (data.empty() || data.size() > 64 || (index + 1 < encoded_chunks.items.size() && data.size() != 64) ||
            data.size() > max_encoded_packet_size - encoded.size()) {
            fail("OOF1114", chunk_path, "bounded canonical 64-character base64 chunks", {},
                "Table Column editor packet chunk size is unsupported");
        }
        encoded.append(data);
    }
    auto packet = decode_base64(encoded, child_path(path, 0));
    if (packet.size() <= table_column_editor_header.size() || packet.size() > max_column_editor_packet_size ||
        !std::equal(table_column_editor_header.begin(), table_column_editor_header.end(), packet.begin())) {
        fail("OOF1114", std::string(path), "bounded Table Column InputField packet with version 2.1 header",
            std::to_string(packet.size()), "Table Column editor packet header or size is unsupported");
    }

    InflateState state;
    if (inflateInit2(&state.stream, -MAX_WBITS) != Z_OK) {
        fail("OOF1124", std::string(path), "available raw DEFLATE decoder", {},
            "Could not initialize Table Column editor decompressor");
    }
    state.initialized = true;
    state.stream.next_in = packet.data() + table_column_editor_header.size();
    state.stream.avail_in = static_cast<uInt>(packet.size() - table_column_editor_header.size());
    std::vector<std::uint8_t> inflated;
    std::array<std::uint8_t, 4096> chunk{};
    int result = Z_OK;
    while (result == Z_OK) {
        state.stream.next_out = chunk.data();
        state.stream.avail_out = static_cast<uInt>(chunk.size());
        result = inflate(&state.stream, Z_NO_FLUSH);
        const std::size_t produced = chunk.size() - state.stream.avail_out;
        if (produced > max_column_editor_packet_size - inflated.size()) {
            fail("OOF1114", std::string(path), "inflated Table Column editor within 1 MiB", {},
                "Table Column editor packet exceeds the decompression limit");
        }
        inflated.insert(inflated.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));
        if (result != Z_OK && result != Z_STREAM_END) {
            fail("OOF1114", std::string(path), "valid raw DEFLATE stream", std::to_string(result),
                "Table Column editor packet is corrupt");
        }
        if (result == Z_OK && state.stream.avail_in == 0 && produced == 0) {
            fail("OOF1114", std::string(path), "complete raw DEFLATE stream", {},
                "Table Column editor packet is truncated");
        }
    }
    if (state.stream.avail_in != 0 || inflated.size() < 11) {
        fail("OOF1114", std::string(path), "single complete raw DEFLATE stream with envelope", {},
            "Table Column editor packet has trailing bytes or a truncated envelope");
    }
    std::uint64_t declared_size = 0;
    for (std::size_t index = 0; index < sizeof(declared_size); ++index) {
        declared_size |= static_cast<std::uint64_t>(inflated[index]) << (index * 8);
    }
    constexpr std::array<std::uint8_t, 3> utf8_bom{0xef, 0xbb, 0xbf};
    if (declared_size != inflated.size() - sizeof(declared_size) ||
        !std::equal(utf8_bom.begin(), utf8_bom.end(), inflated.begin() + sizeof(declared_size))) {
        fail("OOF1114", std::string(path), "matching uncompressed size and UTF-8 BOM", {},
            "Table Column editor packet envelope is invalid");
    }
    return inflated;
}

// Cell editors use a BOM-prefixed textual stream, independently of Table Column compression.
LV spreadsheet_cell_editor_info(const model::TypeDomainPatternValue& type, bool read_only,
                                std::string_view path) {
    if (type.entries.size() != 1)
        fail("OOF1114", std::string(path), "one scalar Cell.ValueType", {}, "Cell editor requires one scalar type");
    InputFieldFlagValues flags{};
    for (std::size_t index = 0; index < flags.size(); ++index)
        flags[index] = input_field_flag_mappings[index].default_value;
    // Keep the standalone InputField qualifier guard intact. Cell-specific adaptation is explicit here.
    auto info = canonical_input_field_info(model::TypeDomainPatternValue{}, true, read_only, flags,
        InputFieldTextValues{}, InputFieldLayoutValues{});
    info.items[1] = encoded_type_domain(type, path);
    auto& editor = info.items[2].items[0];
    const auto& entry = type.entries.front();
    switch (entry.term) {
        case model::TypeDomainTerm::string:
            editor.items[14] = raw(std::to_string(entry.string.length));
            break;
        case model::TypeDomainTerm::numeric:
            editor.items[4] = raw("0");
            editor.items[14] = raw(std::to_string(entry.numeric.length));
            editor.items[15] = raw(std::to_string(entry.numeric.precision));
            editor.items[22] = raw("1");
            break;
        case model::TypeDomainTerm::boolean:
            break;
        case model::TypeDomainTerm::date:
            if (entry.date != model::DateQualifiers{true, true})
                fail("OOF1114", std::string(path), "DateTime Cell editor", {}, "Cell editor date qualifiers are unsupported");
            editor.items[4] = raw("0");
            editor.items[22] = raw("2");
            editor.items[30] = raw("2");
            break;
        default:
            fail("OOF1114", std::string(path), "String, Number, Boolean, or DateTime Cell editor", {},
                "Cell editor type is unsupported");
    }
    return info;
}

LV encoded_spreadsheet_cell_control(const model::SpreadsheetDocumentCell& cell, std::string_view path) {
    const auto& control = *cell.control;
    if (control.kind != model::ControlKind::input_field || !cell.typed_value.has_value())
        fail("OOF1114", std::string(path), "InputField on a typed Cell", {}, "Cell editor owner or kind is unsupported");
    bool read_only = false;
    control.properties.for_each_explicit([&](const model::PropertyEntry& entry) {
        if (entry.id != model::PropertyId::from_name("ReadOnly") || !std::holds_alternative<bool>(entry.value))
            fail("OOF1114", std::string(path), "Boolean ReadOnly only", {}, "Cell editor property is unsupported");
        read_only = std::get<bool>(entry.value);
    });
    const auto envelope = list({raw("2"), raw("1"), raw("381ed624-9217-4e63-85db-c4c3cb87daae"),
        list({spreadsheet_cell_editor_info(cell.typed_value->type, read_only, path)}), raw("0")});
    const auto wrapped = list_stream::dump_listout(envelope);
    const std::string text = std::string("\xef\xbb\xbf") + wrapped.substr(1, wrapped.size() - 2);
    const auto encoded = encode_base64(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    std::vector<LV> chunks;
    for (std::size_t index = 0; index < encoded.size(); index += 64)
        chunks.push_back(raw((index == 0 ? "#base64:" : "") + encoded.substr(index, 64)));
    return list(std::move(chunks));
}

model::SpreadsheetDocumentCellControl decoded_spreadsheet_cell_control(
    const LV& packet, const model::TypeDomainPatternValue& type, std::string_view path) {
    require_list(packet, path);
    constexpr std::size_t max_encoded_size = 1024 * 1024;
    if (packet.items.empty() || packet.items.size() > (max_encoded_size + 63) / 64)
        fail("OOF1114", std::string(path), "bounded nonempty Cell editor packet", {}, "Cell editor packet size is invalid");
    std::string encoded;
    for (std::size_t index = 0; index < packet.items.size(); ++index) {
        const auto atom = raw_atom(packet.items[index], child_path(path, index));
        const bool first = index == 0;
        if ((first && !atom.starts_with("#base64:")) || (!first && atom.starts_with("#base64:")))
            fail("OOF1114", std::string(path), "one initial base64 marker", {}, "Cell editor packet marker is invalid");
        const auto chunk = first ? atom.substr(8) : atom;
        if (chunk.empty() || chunk.size() > 64 || (index + 1 < packet.items.size() && chunk.size() != 64) ||
            chunk.size() > max_encoded_size - encoded.size())
            fail("OOF1114", std::string(path), "bounded 64-character Cell editor chunks", {}, "Cell editor chunk size is invalid");
        encoded.append(chunk);
    }
    const auto bytes = decode_base64(encoded, path);
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!text.starts_with("\xef\xbb\xbf"))
        fail("OOF1114", std::string(path), "UTF-8 BOM Cell editor stream", {}, "Cell editor packet lacks its BOM");
    LV envelope;
    try { envelope = list_stream::parse("{" + text.substr(3) + "}"); }
    catch (const std::exception&) {
        fail("OOF1114", std::string(path), "complete textual Cell editor stream", {}, "Cell editor stream is malformed");
    }
    require_arity(envelope, 5, path);
    require_raw_constant(envelope.items[0], "2", path);
    require_raw_constant(envelope.items[1], "1", path);
    require_raw_constant(envelope.items[2], "381ed624-9217-4e63-85db-c4c3cb87daae", path);
    require_raw_constant(envelope.items[4], "0", path);
    require_arity(envelope.items[3], 1, path);
    const auto& info = envelope.items[3].items[0];
    require_arity(info, 10, path);
    require_arity(info.items[2], 1, path);
    require_arity(info.items[2].items[0], 46, path);
    const bool read_only = bool_atom(info.items[2].items[0].items[13], path);
    require_exact(info, spreadsheet_cell_editor_info(type, read_only, path), path,
        "Cell editor contains an unsupported kind, property, qualifier, or type mismatch");
    model::SpreadsheetDocumentCellControl control;
    control.properties.set_explicit(model::PropertyId::from_name("ReadOnly"), read_only);
    return control;
}

LV encode_table_column_editor_packet(const LV& info, std::string_view path) {
    const std::string text = list_stream::dump_listout(info);
    constexpr std::array<std::uint8_t, 3> utf8_bom{0xef, 0xbb, 0xbf};
    const std::size_t declared_size = utf8_bom.size() + text.size();
    if (declared_size > max_column_editor_packet_size) {
        fail("OOF1114", std::string(path), "Table Column editor under 1 MiB", {},
            "Table Column editor payload exceeds the serialization limit");
    }
    std::vector<std::uint8_t> envelope(sizeof(std::uint64_t) + declared_size);
    const auto size64 = static_cast<std::uint64_t>(declared_size);
    for (std::size_t index = 0; index < sizeof(size64); ++index) {
        envelope[index] = static_cast<std::uint8_t>((size64 >> (index * 8)) & 0xff);
    }
    std::copy(utf8_bom.begin(), utf8_bom.end(), envelope.begin() + sizeof(size64));
    std::copy(text.begin(), text.end(), envelope.begin() + sizeof(size64) + utf8_bom.size());

    DeflateState state;
    if (deflateInit2(&state.stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
            Z_DEFAULT_STRATEGY) != Z_OK) {
        fail("OOF1124", std::string(path), "available raw DEFLATE encoder", {},
            "Could not initialize Table Column editor compressor");
    }
    state.initialized = true;
    state.stream.next_in = envelope.data();
    state.stream.avail_in = static_cast<uInt>(envelope.size());
    std::vector<std::uint8_t> compressed;
    std::array<std::uint8_t, 4096> chunk{};
    int result = Z_OK;
    while (result == Z_OK) {
        state.stream.next_out = chunk.data();
        state.stream.avail_out = static_cast<uInt>(chunk.size());
        result = deflate(&state.stream, state.stream.avail_in == 0 ? Z_FINISH : Z_NO_FLUSH);
        const std::size_t produced = chunk.size() - state.stream.avail_out;
        if (produced > max_column_editor_packet_size - compressed.size()) {
            fail("OOF1114", std::string(path), "compressed Table Column editor within 1 MiB", {},
                "Table Column editor packet exceeds the serialization limit");
        }
        compressed.insert(compressed.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));
        if (result != Z_OK && result != Z_STREAM_END) {
            fail("OOF1124", std::string(path), "successful raw DEFLATE encoding", std::to_string(result),
                "Could not encode Table Column editor packet");
        }
    }
    if (compressed.size() + table_column_editor_header.size() > max_column_editor_packet_size) {
        fail("OOF1114", std::string(path), "bounded Table Column editor packet", {},
            "Table Column editor packet exceeds the serialization limit");
    }
    std::vector<std::uint8_t> packet(table_column_editor_header.begin(), table_column_editor_header.end());
    packet.insert(packet.end(), compressed.begin(), compressed.end());
    const std::string encoded = encode_base64(packet);
    std::vector<LV> chunks;
    for (std::size_t begin = 0; begin < encoded.size(); begin += 64) {
        const std::string part = encoded.substr(begin, std::min<std::size_t>(64, encoded.size() - begin));
        chunks.push_back(raw((begin == 0 ? "#base64:" : "") + part));
    }
    return list({list(std::move(chunks)), raw("0")});
}

std::string explicit_string(
    const model::PropertySet& properties,
    std::string_view name,
    std::string_view default_value = {});

LV canonical_table_column_editor_info(const model::TableColumnControl& control) {
    constexpr std::string_view path = "$/Table/Columns/Column/Control";
    if (control.kind == model::ControlKind::input_field) {
        require_allowed_properties(control.properties, {"Enabled", "ReadOnly"}, path);
        InputFieldFlagValues flags{};
        for (std::size_t index = 0; index < input_field_flag_mappings.size(); ++index) {
            flags[index] = input_field_flag_mappings[index].default_value;
        }
        const model::TypeDomainPatternValue empty_type;
        const bool enabled = explicit_bool(control.properties, "Enabled", true);
        const bool read_only = explicit_bool(control.properties, "ReadOnly", false);
        if (!enabled || read_only) {
            fail("OOF1122", std::string(path), "Enabled=true and ReadOnly=false",
                enabled ? "ReadOnly=true" : "Enabled=false",
                "Table Column editor property value is outside the supported persisted profile");
        }
        LV info = canonical_input_field_info(
            empty_type, enabled, read_only, flags, InputFieldTextValues{}, InputFieldLayoutValues{});
        info.items[3] = list({raw("0")});
        return info;
    }
    if (control.kind == model::ControlKind::choice_field) {
        require_allowed_properties(control.properties, {"Enabled", "ToolTip"}, path);
        const bool enabled = explicit_bool(control.properties, "Enabled", true);
        const std::string tool_tip = explicit_string(control.properties, "ToolTip");
        if (!enabled || !tool_tip.empty()) {
            fail("OOF1122", std::string(path), "Enabled=true and empty ToolTip",
                enabled ? "ToolTip is non-empty" : "Enabled=false",
                "Table Column ChoiceField is outside the observed default profile");
        }
        return canonical_choice_field_info(enabled, tool_tip);
    }
    if (control.kind == model::ControlKind::check_box) {
        require_allowed_properties(control.properties, {"Enabled", "Caption", "ToolTip", "Font"}, path);
        const bool enabled = explicit_bool(control.properties, "Enabled", true);
        const std::string caption = explicit_string(control.properties, "Caption");
        const std::string tool_tip = explicit_string(control.properties, "ToolTip");
        const auto font = explicit_control_font(control.properties, std::string(path) + "/Font");
        if (!enabled || !caption.empty() || !tool_tip.empty() || font != model::FontValue{}) {
            fail("OOF1122", std::string(path), "default CheckBox properties",
                "nondefault property", "Table Column CheckBox is outside its candidate default profile");
        }
        return canonical_check_box_info(enabled, caption, tool_tip, &font);
    }
    fail("OOF1122", std::string(path), "InputField, ChoiceField, or CheckBox", "unsupported kind",
        "Table Column editor kind is unsupported");
}

LV canonical_table_column_record(const model::TableColumn& column, std::string_view path) {
    auto properties = parse_constant(R"OOF(
{23,{0},{1,0},{1,0},2.1e2,0,0,-1,-1,12590592,
{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},
16,16,d2314b5d-8da4-4e0f-822b-45e7500eae09,
{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},
{8,3,0,1,100},{8,3,0,1,100},{8,3,0,1,100},1,0,0,4,0,"",{},30,0,{1,0},{"Pattern"},0,1,
381ed624-9217-4e63-85db-c4c3cb87daae,{{},0},0,0,0,0,0,2.1e2,0,1,0,0,2,0}
)OOF");
    if (properties.items.size() != 52) {
        throw std::logic_error("canonical Table Column property record has the wrong arity");
    }
    properties.items[1] = encoded_localized(column.header);
    properties.items[30] = string_value(column.name);
    properties.items[35] = encoded_type_domain(model::TypeDomainPatternValue{}, std::string(path) + "/Control/TypeRestriction");
    properties.items[38] = raw(std::string(model::metamodel::descriptor_for(column.control.kind).guid));
    properties.items[39] = encode_table_column_editor_packet(
        canonical_table_column_editor_info(column.control), std::string(path) + "/Control");
    return list({
        raw("737535a4-21e6-4971-8513-3e3173a9fedd"),
        list({raw("8"), list({raw("8"), std::move(properties), list({raw("-1")}), list({raw("-1")}), list({raw("-1")})}),
            string_value(column.data_path), string_value(""), string_value(""), raw("0")})});
}

LV canonical_table_control_info(
    const model::TypeDomainPatternValue& source_type,
    const std::vector<model::TableColumn>& columns,
    bool read_only,
    std::string_view path) {
    if (columns.empty() || columns.size() > 256) {
        fail("OOF1122", std::string(path) + "/Columns", "1 to 256 Columns",
            std::to_string(columns.size()), "Table Columns collection is outside the supported profile");
    }
    auto base = parse_constant(R"OOF(
{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},
{4,3,{-7},3},{4,3,{-21},3},{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}
)OOF");
    auto properties = parse_constant(R"OOF(
{23,117643809,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,0,{12741203},0},
{4,3,{-15},3},{4,3,{-13},3},2,2,0,0,0,0,0,1,1,{8,2,0,{-20},1,100},{8,2,0,{-20},1,100},0,0,
1,{0},0,0,0,0,0,0,0,100,1,2,1,1,0,0,2}
)OOF");
    if (base.items.size() != 21 || properties.items.size() != 39) {
        throw std::logic_error("canonical Table control property record has the wrong arity");
    }
    if (!read_only) properties.items[1] = raw(std::to_string(117643809U | 0x400U));
    std::vector<LV> column_records;
    column_records.reserve(columns.size() + 1);
    column_records.push_back(raw(std::to_string(columns.size())));
    for (std::size_t index = 0; index < columns.size(); ++index) {
        column_records.push_back(canonical_table_column_record(
            columns[index], std::string(path) + "/Columns/Column[" + std::to_string(index) + "]"));
    }
    properties.items[23] = list(std::move(column_records));
    return list({
        raw("5"), encoded_type_domain(source_type, std::string(path) + "/ValueType"),
        list({std::move(base), std::move(properties)}),
        list({raw("342cf854-134c-42bb-8af9-a2103d5d9723"), list({raw("5"), raw("0"), raw("0"), raw("1")})}),
        list({raw("0")})});
}

LV encode_button_picture(const model::PictureAsset& asset, std::string_view path) {
    if (asset.bytes.empty()) fail("OOF1122", std::string(path), "non-empty external picture bytes", "empty", "Button picture asset has no bytes");
    const auto actual_format = picture_format_from_bytes(asset.bytes);
    if (!actual_format || *actual_format != asset.format) {
        fail("OOF1122", std::string(path), std::string(picture_format_extension(asset.format)) + " image signature",
            actual_format ? std::string(picture_format_extension(*actual_format)) : "unknown",
            "Picture format does not match the image signature");
    }
    const std::string base64 = encode_base64(asset.bytes);
    std::vector<LV> chunks;
    for (std::size_t begin = 0; begin < base64.size(); begin += 64) {
        const std::string chunk = base64.substr(begin, std::min<std::size_t>(64, base64.size() - begin));
        chunks.push_back(raw((begin == 0 ? "#base64:" : "") + chunk));
    }
    return list({raw("4"), raw("3"), parse_constant("{0}"), string_value(""), raw("-1"), raw("-1"),
        raw(asset.transparent ? "1" : "0"), list({list(std::move(chunks))}), raw("0"), string_value("")});
}

LV encode_standard_button_picture(const model::metamodel::StandardPictureDescriptor& picture) {
    LV identity = !picture.guid.empty()
        ? list({raw("0"), raw(std::string(picture.guid))})
        : list({raw(std::to_string(picture.storage_id))});
    return list({raw("4"), raw("1"), std::move(identity), string_value(""),
        raw("-1"), raw("-1"), raw("0"), raw("0"), string_value("")});
}

std::optional<DecodedPictureDescriptor> decode_button_picture(const LV& value, std::string_view path) {
    if (same_list_value(value, canonical_button_picture())) return std::nullopt;
    if (value.items.size() >= 2 && value.items[0].atom == "4" && value.items[1].atom == "1") {
        require_arity(value, 9, path);
        require_raw_constant(value.items[0], "4", child_path(path, 0));
        require_raw_constant(value.items[1], "1", child_path(path, 1));
        const auto& identity = value.items[2];
        const model::metamodel::StandardPictureDescriptor* descriptor = nullptr;
        if (identity.is_list) {
            if (identity.items.size() == 1) {
                const auto storage_id = integer_atom<std::int32_t>(identity.items[0], child_path(child_path(path, 2), 0));
                descriptor = model::metamodel::find_standard_picture_by_storage_id(storage_id);
            } else if (identity.items.size() == 2) {
                require_raw_constant(identity.items[0], "0", child_path(child_path(path, 2), 0));
                descriptor = model::metamodel::find_standard_picture_by_guid(
                    raw_atom(identity.items[1], child_path(child_path(path, 2), 1)));
            } else {
                fail("OOF1114", child_path(path, 2), "{negativeStorageId} or {0,GUID}",
                    std::to_string(identity.items.size()) + " identity fields",
                    "Standard picture identity is malformed");
            }
        } else {
            fail("OOF1114", child_path(path, 2), "list-wrapped standard picture identity", "atom",
                "Standard picture identity is malformed");
        }
        if (descriptor == nullptr) fail("OOF1114", child_path(path, 2), "known standard picture identity", "unknown", "Button standard picture is unsupported");
        require_exact(value.items[3], string_value(""), child_path(path, 3), "Standard picture descriptor is unsupported");
        require_raw_constant(value.items[4], "-1", child_path(path, 4));
        require_raw_constant(value.items[5], "-1", child_path(path, 5));
        require_raw_constant(value.items[6], "0", child_path(path, 6));
        require_raw_constant(value.items[7], "0", child_path(path, 7));
        require_exact(value.items[8], string_value(""), child_path(path, 8), "Standard picture descriptor is unsupported");
        DecodedPictureDescriptor picture;
        picture.standard_name = std::string(descriptor->runtime_name);
        return picture;
    }
    require_arity(value, 10, path);
    require_raw_constant(value.items[0], "4", child_path(path, 0));
    require_raw_constant(value.items[1], "3", child_path(path, 1));
    require_exact(value.items[2], parse_constant("{0}"), child_path(path, 2), "Button picture descriptor is unsupported");
    require_exact(value.items[3], string_value(""), child_path(path, 3), "Button picture descriptor is unsupported");
    require_raw_constant(value.items[4], "-1", child_path(path, 4));
    require_raw_constant(value.items[5], "-1", child_path(path, 5));
    const auto transparency = integer_atom<std::int32_t>(value.items[6], child_path(path, 6));
    if (transparency != 0 && transparency != 1) fail("OOF1114", child_path(path, 6), "transparency flag 0 or 1", std::to_string(transparency), "Button picture transparency is unsupported");
    const auto& outer_chunks = value.items[7];
    require_list(outer_chunks, child_path(path, 7));
    require_arity(outer_chunks, 1, child_path(path, 7));
    const auto& chunks = outer_chunks.items[0];
    require_list(chunks, child_path(child_path(path, 7), 0));
    std::string base64;
    for (std::size_t index = 0; index < chunks.items.size(); ++index) {
        const auto chunk_path = child_path(child_path(child_path(path, 7), 0), index);
        const std::string chunk = raw_atom(chunks.items[index], chunk_path);
        const bool first = index == 0;
        const std::string_view encoded = first ? std::string_view(chunk).substr(8) : std::string_view(chunk);
        if ((first && !chunk.starts_with("#base64:")) || (!first && chunk.starts_with("#base64:")) ||
            encoded.empty() || encoded.size() > 64 || (index + 1 < chunks.items.size() && encoded.size() != 64)) {
            fail("OOF1114", chunk_path, "canonical 64-character base64 chunk", chunk, "Button picture data is malformed");
        }
        base64.append(encoded);
    }
    require_raw_constant(value.items[8], "0", child_path(path, 8));
    require_exact(value.items[9], string_value(""), child_path(path, 9), "Button picture descriptor is unsupported");
    DecodedPictureDescriptor picture;
    picture.bytes = decode_base64(base64, child_path(path, 7));
    picture.transparent = transparency == 1;
    const auto format = picture_format_from_bytes(picture.bytes);
    if (!format) fail("OOF1114", child_path(path, 7), "GIF, PNG, JPEG, or BMP image bytes", "unknown signature", "Button picture format is unsupported");
    picture.format = *format;
    return picture;
}

constexpr std::string_view menu_owner_guid = "31946946-0a9b-40a2-95cf-82f200778341";
constexpr std::string_view menu_action_guid = "e1692cc2-605b-4535-84dd-28440238746c";
constexpr std::string_view menu_reference_guid = "abde0c9a-18a6-4e0c-bbaa-af26b911b3e6";

std::string menu_identity(std::uint64_t owner, std::uint64_t item) {
    std::array<char, 32> digits;
    digits.fill('0');
    for (std::size_t half = 0; half < 2; ++half) {
        std::array<char, 16> converted{};
        const auto result = std::to_chars(converted.data(), converted.data() + converted.size(), half == 0 ? owner : item, 16);
        const auto length = static_cast<std::size_t>(result.ptr - converted.data());
        std::copy_n(converted.data(), length, digits.data() + half * 16 + 16 - length);
    }
    return std::string(digits.data(), 8) + "-" + std::string(digits.data() + 8, 4) + "-" +
        std::string(digits.data() + 12, 4) + "-" + std::string(digits.data() + 16, 4) + "-" + std::string(digits.data() + 20, 12);
}

LV menu_entry_properties(const model::CommandBarButton& entry, std::string_view owner, std::uint64_t id) {
    const auto type = entry.type == model::CommandBarButtonKind::action ? 0 :
        entry.type == model::CommandBarButtonKind::submenu ? 1 : 2;
    const auto representation = entry.representation == model::ButtonRepresentation::automatic ? 0 :
        entry.representation == model::ButtonRepresentation::text ? 1 :
        entry.representation == model::ButtonRepresentation::picture ? 2 : 3;
    return list({raw("8"), string_value(entry.name), raw(entry.changes_data ? "1" : "0"), raw("1"),
        encoded_localized(entry.text), raw(entry.text.empty() ? "0" : "1"), raw(std::string(owner)),
        raw(std::to_string(id)), raw("1e2"), raw(std::to_string(type)), raw(std::to_string(representation)),
        raw(entry.enabled ? "1" : "0"), raw(entry.checked ? "1" : "0"),
        raw(entry.type == model::CommandBarButtonKind::separator ? "0" : "1"), raw("0"), raw("0")});
}

LV menu_picture(const model::PictureRef& reference, const model::OrdinaryFormDocument& document) {
    if (reference.standard_name) {
        const auto* descriptor = model::metamodel::find_standard_picture(reference.standard_name->value);
        if (!descriptor || reference.asset) fail("OOF1122", "$/Button/Buttons/Picture", "valid standard picture",
            reference.standard_name->value, "Menu picture reference is inconsistent");
        return encode_standard_button_picture(*descriptor);
    }
    const auto* asset = document.find_asset(reference.asset.id());
    if (!asset) fail("OOF1123", "$/Button/Buttons/Picture", "existing picture asset", "missing", "Menu picture reference is dangling");
    return encode_button_picture(*asset, "$/Button/Buttons/Picture");
}

std::string menu_group_key(std::string_view marker, std::uint64_t id) {
    return std::string(marker) + ":" + std::to_string(id);
}

LV encode_button_menu(const std::vector<model::CommandBarButton>& entries,
                      const model::OrdinaryFormDocument& document, std::uint64_t control_id,
                      std::string_view root_marker = menu_owner_guid, std::uint64_t root_group_id = 0) {
    const auto owner = menu_identity(control_id | 0x8000000000000000ULL, 0);
    struct Item { const model::CommandBarButton* entry; std::uint64_t id; std::string action_id; };
    std::vector<Item> items;
    std::unordered_map<const model::CommandBarButton*, std::uint64_t> ids;
    std::function<void(const std::vector<model::CommandBarButton>&, std::size_t)> collect;
    collect = [&](const auto& collection, std::size_t depth) {
        if (depth > 256) fail("OOF1122", "$/Button/Buttons", "menu depth at most 256", "too deep", "Menu nesting is too deep");
        for (const auto& entry : collection) {
            const auto id = items.size() + 1;
            ids.emplace(&entry, id);
            items.push_back({&entry, id, menu_identity(control_id, id)});
            collect(entry.buttons, depth + 1);
        }
    };
    collect(entries, 0);
    const auto max_id = static_cast<std::uint64_t>(items.size());
    std::vector<LV> result{raw("5"), raw(owner), raw(std::to_string(max_id)), raw("1"), raw(std::to_string(items.size()))};
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        const auto& entry = *it->entry;
        LV action = entry.type == model::CommandBarButtonKind::action
            ? encode_action(*entry.action, ActionMetadataPolicy::empty, "$/Button/Buttons/Action")
            : entry.type == model::CommandBarButtonKind::submenu
                ? list({raw("1"), raw(owner), raw(std::to_string(it->id))})
                : parse_constant("{1,9d0a2e40-b978-11d4-84b6-008048da06df,0}");
        unsigned mask = (entry.picture ? 1 : 0) | (!entry.tooltip.empty() ? 2 : 0) |
            (!entry.explanation.empty() ? 4 : 0) | (entry.shortcut != model::ShortcutValue{} ? 8 : 0);
        std::vector<LV> record{raw("8"), raw(it->action_id), raw("1"),
            raw(std::string(entry.type == model::CommandBarButtonKind::action ? menu_action_guid : menu_reference_guid)),
            std::move(action), raw(std::to_string(mask))};
        if (!entry.tooltip.empty()) record.push_back(encoded_localized(entry.tooltip));
        if (!entry.explanation.empty()) record.push_back(encoded_localized(entry.explanation));
        if (entry.picture) record.push_back(menu_picture(*entry.picture, document));
        if (entry.shortcut != model::ShortcutValue{}) record.push_back(encode_button_shortcut(entry.shortcut));
        record.push_back(raw("0")); record.push_back(raw("0"));
        result.push_back(list(std::move(record)));
    }
    result.push_back(raw(std::to_string(1 + std::count_if(items.begin(), items.end(), [](const auto& item) {
        return item.entry->type == model::CommandBarButtonKind::submenu;
    }))));
    const auto group = [&](const auto& collection, std::string_view marker, std::uint64_t id) {
        std::vector<LV> value{raw("5"), raw(std::string(marker)), raw(std::to_string(id)),
            raw("0"), raw(std::to_string(collection.size()))};
        for (const auto& entry : collection) {
            const auto entry_id = ids.at(&entry);
            value.push_back(raw(items[entry_id - 1].action_id));
            value.push_back(menu_entry_properties(entry, owner, entry_id));
        }
        std::vector<LV> submenu_refs{raw(std::to_string(std::count_if(collection.begin(), collection.end(), [](const auto& entry) {
            return entry.type == model::CommandBarButtonKind::submenu;
        })))};
        for (const auto& entry : collection) if (entry.type == model::CommandBarButtonKind::submenu) {
            submenu_refs.push_back(raw(owner));
            submenu_refs.push_back(raw(std::to_string(ids.at(&entry))));
            submenu_refs.push_back(raw(entry.order == model::CommandBarButtonOrder::ascending ? "1" :
                entry.order == model::CommandBarButtonOrder::descending ? "2" : "0"));
        }
        value.push_back(list({raw("-1"), raw("0"), list(std::move(submenu_refs))}));
        return list(std::move(value));
    };
    result.push_back(group(entries, root_marker, root_group_id));
    for (const auto& item : items) if (item.entry->type == model::CommandBarButtonKind::submenu)
        result.push_back(group(item.entry->buttons, owner, item.id));
    return list(std::move(result));
}

struct DecodedMenu {
    std::vector<model::CommandBarButton> entries;
    std::vector<model::PictureAsset> assets;
};

DecodedMenu decode_button_menu(const LV& menu, std::string_view path, std::string_view control_name,
                               std::string_view root_marker = menu_owner_guid, std::uint64_t root_group_id = 0,
                               std::uint64_t default_item_id = 0) {
    require_list(menu, path);
    if (menu.items.size() < 7) fail("OOF1102", std::string(path), "menu header and collections", describe(menu), "Menu is incomplete");
    require_raw_constant(menu.items[0], "5", path);
    const auto owner = uuid_atom(menu.items[1], child_path(path, 1)).canonical;
    const auto max_id = integer_atom<std::uint64_t>(menu.items[2], path);
    require_raw_constant(menu.items[3], "1", path);
    const auto action_count = integer_atom<std::size_t>(menu.items[4], path);
    if (action_count > menu.items.size() - 6) fail("OOF1102", std::string(path), "bounded action count", describe(menu), "Menu action count exceeds its record");
    std::unordered_map<std::string, const LV*> actions;
    for (std::size_t i = 0; i < action_count; ++i) {
        const auto& action = menu.items[5 + i];
        require_list(action, path);
        if (action.items.size() < 8) fail("OOF1102", std::string(path), "action header", describe(action), "Menu action is incomplete");
        const auto guid = uuid_atom(action.items[1], path).canonical;
        if (!actions.emplace(guid, &action).second) fail("OOF1114", std::string(path), "unique actions", guid, "Duplicate menu action");
    }
    const auto group_count = integer_atom<std::size_t>(menu.items[5 + action_count], path);
    if (group_count != menu.items.size() - 6 - action_count || group_count == 0)
        fail("OOF1102", std::string(path), "exact menu collection count", describe(menu), "Menu collection count is inconsistent");
    std::unordered_map<std::string, const LV*> groups;
    std::unordered_map<std::string, std::unordered_map<std::uint64_t, model::CommandBarButtonOrder>> submenu_orders;
    for (std::size_t i = 0; i < group_count; ++i) {
        const auto& group = menu.items[6 + action_count + i];
        require_list(group, path);
        if (group.items.size() < 6) fail("OOF1102", std::string(path), "collection header", describe(group), "Menu collection is incomplete");
        require_raw_constant(group.items[0], "5", path);
        const auto id = integer_atom<std::uint64_t>(group.items[2], path);
        const auto marker = uuid_atom(group.items[1], path).canonical;
        const bool root_group = marker == root_marker && id == root_group_id;
        if (!root_group) require_raw_constant(group.items[1], owner, path);
        require_raw_constant(group.items[3], "0", path);
        const auto key = menu_group_key(marker, id);
        if (!groups.emplace(key, &group).second) fail("OOF1114", std::string(path), "unique owner/ID collection pairs", std::to_string(id), "Duplicate menu collection");
        const auto& footer = group.items.back();
        require_arity(footer, 3, path);
        const bool empty_command_bar_root = root_group && root_marker == command_bar_root_marker &&
            integer_atom<std::size_t>(group.items[4], path) == 0;
        require_raw_constant(footer.items[0],
            empty_command_bar_root && footer.items[0].atom == "0" ? "0" : "-1", path);
        require_raw_constant(footer.items[1], "0", path);
        require_list(footer.items[2], path);
        const auto& refs = footer.items[2];
        const auto ref_count = integer_atom<std::size_t>(at(refs, 0, path), path);
        if (ref_count > (refs.items.size() - 1) / 3 || refs.items.size() != 1 + 3 * ref_count)
            fail("OOF1114", std::string(path), "exact submenu order footer size", describe(refs), "Menu order footer count is inconsistent");
        auto& orders = submenu_orders[key];
        for (std::size_t ref = 0; ref < ref_count; ++ref) {
            const auto base = 1 + ref * 3;
            require_raw_constant(refs.items[base], owner, path);
            const auto submenu_id = integer_atom<std::uint64_t>(refs.items[base + 1], path);
            const auto order = integer_atom<unsigned>(refs.items[base + 2], path);
            if (submenu_id == 0 || order > 2 || !orders.emplace(submenu_id,
                order == 1 ? model::CommandBarButtonOrder::ascending : order == 2 ? model::CommandBarButtonOrder::descending : model::CommandBarButtonOrder::none).second)
                fail("OOF1114", std::string(path), "unique submenu IDs and order 0, 1, or 2", std::to_string(submenu_id), "Menu order footer is invalid");
        }
    }
    DecodedMenu decoded;
    std::unordered_set<std::string> consumed_groups;
    std::unordered_set<std::uint64_t> entry_ids;
    std::unordered_set<std::string> consumed_actions;
    std::function<std::vector<model::CommandBarButton>(std::string, std::uint64_t, std::string, std::size_t)> visit;
    visit = [&](std::string group_marker, std::uint64_t group_id, std::string asset_path, std::size_t depth) {
        const auto group_key = menu_group_key(group_marker, group_id);
        if (depth > 256 || !consumed_groups.insert(group_key).second || !groups.contains(group_key))
            fail("OOF1114", std::string(path), "existing acyclic owned collections", std::to_string(group_id), "Menu ownership is invalid");
        const auto& group = *groups.at(group_key);
        const auto count = integer_atom<std::size_t>(group.items[4], path);
        if (count > (group.items.size() - 6) / 2 || group.items.size() != 6 + 2 * count)
            fail("OOF1102", std::string(path), "exact collection size", describe(group), "Menu item count is inconsistent");
        std::vector<LV> submenu_refs{raw("0")};
        std::size_t submenu_count = 0;
        std::vector<model::CommandBarButton> entries;
        std::unordered_set<std::string> names;
        for (std::size_t i = 0; i < count; ++i) {
            const auto guid = uuid_atom(group.items[5 + 2 * i], path).canonical;
            if (!actions.contains(guid) || !consumed_actions.insert(guid).second)
                fail("OOF1114", std::string(path), "unique existing action reference", guid, "Menu action is dangling or shared");
            const auto& props = group.items[6 + 2 * i];
            require_arity(props, 16, path);
            model::CommandBarButton entry;
            entry.name = string_atom(props.items[1], path);
            if (entry.name.empty() || !names.insert(entry.name).second) fail("OOF1114", std::string(path), "unique named items", entry.name, "Menu name is empty or duplicated");
            entry.changes_data = bool_atom(props.items[2], path);
            entry.text = decoded_single_language_text(props.items[4], path);
            const auto id = integer_atom<std::uint64_t>(props.items[7], path);
            if (id == 0 || id > max_id || !entry_ids.insert(id).second)
                fail("OOF1114", std::string(path), "positive unique item ID within maximum", std::to_string(id), "Menu identity is invalid");
            const auto type = integer_atom<unsigned>(props.items[9], path);
            if (type > 2) fail("OOF1114", std::string(path), "Action, Submenu, or Separator", std::to_string(type), "Menu item type is unsupported");
            entry.type = type == 0 ? model::CommandBarButtonKind::action : type == 1 ? model::CommandBarButtonKind::submenu : model::CommandBarButtonKind::separator;
            if (id == default_item_id) {
                if (type != 0 || depth != 0)
                    fail("OOF1122", std::string(path), "top-level Action for DefaultButton", entry.name, "Unsupported default menu action");
                entry.default_button = true;
            }
            if (entry.type == model::CommandBarButtonKind::submenu) {
                const auto order = submenu_orders[group_key].find(id);
                if (order == submenu_orders[group_key].end()) fail("OOF1114", std::string(path), "submenu order footer entry", std::to_string(id), "Submenu order is missing");
                entry.order = order->second;
            } else if (submenu_orders[group_key].contains(id)) {
                fail("OOF1114", std::string(path), "submenu order footer entry only", std::to_string(id), "Non-submenu has an order footer entry");
            }
            const auto representation = integer_atom<unsigned>(props.items[10], path);
            if (representation > 3) fail("OOF1114", std::string(path), "known representation", std::to_string(representation), "Menu representation is unsupported");
            entry.representation = representation == 0 ? model::ButtonRepresentation::automatic : representation == 1 ? model::ButtonRepresentation::text : representation == 2 ? model::ButtonRepresentation::picture : model::ButtonRepresentation::picture_text;
            entry.enabled = bool_atom(props.items[11], path);
            entry.checked = bool_atom(props.items[12], path);
            auto normalized = props;
            normalized.items[8] = raw("1e2");
            if (raw_atom(props.items[8], path) != "1e2" && raw_atom(props.items[8], path) != "100")
                fail("OOF1114", std::string(path), "scale 100", describe(props.items[8]), "Menu scale is unsupported");
            const auto expected_properties = menu_entry_properties(entry, owner, id);
            const auto mismatch = std::mismatch(normalized.items.begin(), normalized.items.end(),
                expected_properties.items.begin(), expected_properties.items.end(), [](const auto& left, const auto& right) {
                    return list_stream::dump_compact(left) == list_stream::dump_compact(right);
                });
            if (mismatch.first != normalized.items.end()) {
                const auto index = static_cast<std::size_t>(mismatch.first - normalized.items.begin());
                fail("OOF1114", child_path(path, index), "canonical menu item property",
                    describe(normalized.items.at(index)), "Menu item property is unsupported");
            }
            const auto& action = *actions.at(guid);
            require_raw_constant(action.items[0], "8", path); require_raw_constant(action.items[2], "1", path);
            require_raw_constant(action.items[3], type == 0 ? menu_action_guid : menu_reference_guid, path);
            if (type == 0) {
                entry.action = decode_action(action.items[4], ActionMetadataPolicy::empty, path);
            } else {
                require_exact(action.items[4], type == 1 ? list({raw("1"), raw(owner), raw(std::to_string(id))}) :
                    parse_constant("{1,9d0a2e40-b978-11d4-84b6-008048da06df,0}"), path, "Menu action target is unsupported");
            }
            const auto mask = integer_atom<unsigned>(action.items[5], path);
            if (mask > 15) fail("OOF1114", std::string(path), "known property flags", std::to_string(mask), "Menu action flags are unsupported");
            std::size_t cursor = 6;
            const auto take = [&]() -> const LV& { return at(action, cursor++, path); };
            if (mask & 2) entry.tooltip = decoded_single_language_text(take(), path);
            if (mask & 4) entry.explanation = decoded_single_language_text(take(), path);
            if (mask & 1) {
                const auto picture = decode_button_picture(take(), path);
                if (!picture) fail("OOF1114", std::string(path), "nonempty picture", "empty", "Menu picture flag is inconsistent");
                if (picture->standard_name) entry.picture = model::PictureRef{model::PictureAssetRef{}, model::QualifiedName{*picture->standard_name}};
                else {
                    decoded.assets.push_back(model::PictureAsset{model::ObjectId{decoded.assets.size() + 1},
                        asset_path + "/" + entry.name + "/Picture." + std::string(picture_format_extension(picture->format)),
                        picture->format, picture->bytes, picture->transparent});
                    entry.picture = model::PictureRef{model::PictureAssetRef{decoded.assets.back().id}};
                }
            }
            if (mask & 8) entry.shortcut = decode_button_shortcut(take(), path);
            require_raw_constant(take(), "0", path); require_raw_constant(take(), "0", path);
            require_arity(action, cursor, path);
            if (type == 1) {
                ++submenu_count;
                submenu_refs.push_back(raw(owner)); submenu_refs.push_back(raw(std::to_string(id)));
                submenu_refs.push_back(raw(entry.order == model::CommandBarButtonOrder::ascending ? "1" :
                    entry.order == model::CommandBarButtonOrder::descending ? "2" : "0"));
                entry.buttons = visit(owner, id, asset_path + "/" + entry.name + "/Buttons", depth + 1);
            }
            entries.push_back(std::move(entry));
        }
        submenu_refs[0] = raw(std::to_string(submenu_count));
        require_exact(group.items.back(), list({group.items.back().items[0], raw("0"), list(std::move(submenu_refs))}),
            path, "Menu submenu references are inconsistent");
        return entries;
    };
    decoded.entries = visit(std::string(root_marker), root_group_id, "Items/" + std::string(control_name) + "/Buttons", 0);
    if (default_item_id != 0 && !entry_ids.contains(default_item_id))
        fail("OOF1114", std::string(path), "existing default menu item", std::to_string(default_item_id), "DefaultButton references a missing menu item");
    if (consumed_groups.size() != groups.size() || consumed_actions.size() != actions.size())
        fail("OOF1114", std::string(path), "all owned menu objects consumed", "orphan", "Menu has unconsumed objects");
    return decoded;
}

struct DecodedControl {
    model::ControlNode control;
    std::optional<std::string> click_handler;
    IncomingAnchorLists incoming;
    std::optional<model::PictureAsset> picture_asset;
    std::vector<model::PictureAsset> menu_assets;
};

std::size_t control_geometry_slot(std::string_view guid) {
    return guid == model::metamodel::descriptor_for(model::ControlKind::chart).guid ||
        guid == model::metamodel::descriptor_for(model::ControlKind::geographical_schema_field).guid ? 4 : 3;
}

LV canonical_graphical_schema_field_info() {
    return parse_constant(R"OOF({{19,1,{4,3,{-10},3},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},5,{{5,{{1,{4,3,{-10},3},1,20,20,3,6,6,{"N",10},7,{"N",10},8,{"N",10},9,{"N",10},13,{"N",0},16,{"N",0}}},0,0}},{0},0,0})OOF");
}

LV canonical_geographical_schema_field_info() {
    return parse_constant("{19,1,{4,3,{-10},3},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,1,3,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
}

LV canonical_geographical_schema() {
    return parse_constant("{2,2,{{1,0,0,0},{0,0,0,0,0,{}},{1,{1,0},{8,2,0,{-20},1,100},{4,3,{-3},3},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},1,{4,3,{-10},3},0,0,0,95},{1,{8,2,0,{-20},1,100},{4,3,{-3},3},{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},1,{4,3,{-10},3},0,{},75,0,5,0,1},{{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},1,{4,3,{-10},3},0,25,5,0},{0,{}},0,1,0,0,0,0},{0},0}");
}

// Стандартное состояние Designer включает демонстрационную диаграмму без источника и ролей.
LV canonical_default_pivot_chart_info() {
    return parse_constant(R"OOF({3,{0,{11},{75,5,4,1,4,{4,0,{10053120},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,{1,1,{"ru","<Элемент 2>"}},1,0,0,2,{"U"},{"U"},0,{4,0,{13434624},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},1,{1,1,{"ru","<Элемент 3>"}},1,0,0,3,{"U"},{"U"},0,{4,0,{10053120},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},2,{1,1,{"ru","<Элемент 5>"}},1,0,0,4,{"U"},{"U"},0,{4,0,{13434624},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,{1,1,{"ru","<Элемент 6>"}},1,0,0,5,{"U"},{"U"},0,{4,0,{11837108},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},1,{1,1,{"ru","Сводная"}},0,0,0,1,{"U"},{"U"},0,1,4,{1,1,{"ru","<Элемент 2>"}},1,1,{4,0,{16762956},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},4,0,0,{"U"},{"U"},0,{1,1,{"ru","<Элемент 3>"}},1,2,{4,0,{7964671},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},4,0,0,{"U"},{"U"},0,{1,1,{"ru","<Элемент 5>"}},1,3,{4,0,{49407},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},4,0,0,{"U"},{"U"},0,{1,1,{"ru","<Элемент 6>"}},1,4,{4,0,{7258719},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},4,0,0,{"U"},{"U"},0,3,3,6,0,", ",4,{1,0},{1,0},{4,3,{-3},3},0,0,{1,1,{"ru","СводнаяДиаграмма1"}},1,1,{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},0,{4,3,{-1},3},1,{4,3,{-1},3},1,{4,3,{-1},3},0,{4,0,{16777215},0},{4,3,{-3},3},{4,3,{-3},3},{4,3,{-3},3},{8,3,0,1,100},{8,3,0,1,100},{8,3,0,1,100},1,1,1,1,1,{1,0},0,{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},{4,4,{0},4},1,1,0,4,30,1,0,0,0,0,1,0,0,1,0,1,1,2,{1,0},1,0,0,1,{4,0,{169},0},0,0,{1,0,0,0},0,180,5,1,0,4,{4,0,{11119017},0},1,0,1,0,0,0,0,2.3125e-1,0,7.6875e-1,3.527777777777778e-1,0,3.138888888888889e-1,7.6875e-1,0,0,9.611111111111111e-1,0,{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"",0,0,{"N",2},{"U"},"<Элемент 1> <Элемент 2>"\000A<Элемент 1> <Элемент 2>"\000A2",{"N",3},{"U"},"<Элемент 1> <Элемент 2>"\000A<Элемент 1> <Элемент 3>"\000A3",{"N",3},{"U"},"<Элемент 1> <Элемент 2>"\000A<Элемент 4> <Элемент 5>"\000A3",{"N",1},{"U"},"<Элемент 1> <Элемент 2>"\000A<Элемент 4> <Элемент 6>"\000A1",{"N",2},{"U"},"<Элемент 1> <Элемент 3>"\000A<Элемент 1> <Элемент 2>"\000A2",{"N",4},{"U"},"<Элемент 1> <Элемент 3>"\000A<Элемент 1> <Элемент 3>"\000A4",{"N",2},{"U"},"<Элемент 1> <Элемент 3>"\000A<Элемент 4> <Элемент 5>"\000A2",{"N",3},{"U"},"<Элемент 1> <Элемент 3>"\000A<Элемент 4> <Элемент 6>"\000A3",{"N",2},{"U"},"<Элемент 4> <Элемент 5>"\000A<Элемент 1> <Элемент 2>"\000A2",{"N",4},{"U"},"<Элемент 4> <Элемент 5>"\000A<Элемент 1> <Элемент 3>"\000A4",{"N",4},{"U"},"<Элемент 4> <Элемент 5>"\000A<Элемент 4> <Элемент 5>"\000A4",{"N",3},{"U"},"<Элемент 4> <Элемент 5>"\000A<Элемент 4> <Элемент 6>"\000A3",{"N",3},{"U"},"<Элемент 4> <Элемент 6>"\000A<Элемент 1> <Элемент 2>"\000A3",{"N",2},{"U"},"<Элемент 4> <Элемент 6>"\000A<Элемент 1> <Элемент 3>"\000A2",{"N",5},{"U"},"<Элемент 4> <Элемент 6>"\000A<Элемент 4> <Элемент 5>"\000A5",{"N",4},{"U"},"<Элемент 4> <Элемент 6>"\000A<Элемент 4> <Элемент 6>"\000A4",14,2,{8,3,0,1,100},1,{4,4,{0},4},{3,0,{0},1,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},1,1,1,0,0,95,1e-1,1e-1,3e-2,{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},{4,0,{0},0},2,255,0,7352523,00000000-0000-0000-0000-000000000000,0,{0,0},{0,0},{0,0},{0,0},{0,0},0,{0,0,{0,1,0,1,0},0,0},{0,0,{0,1,0,1,0},0,0},0,0,2,-2,1,10,1,20,0,0,{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},0,0,{4,4,{0},4},{4,4,{0},4},0,{{4,4,{0},4}},{{4,4,{0},4}},{{4,4,{0},4}},{{4,4,{0},4}},{{4,0,{10053120},0},4,0,0,0,"",{1,0},{1,0},{1,0},0},{{4,0,{13434624},0},4,0,0,0,"",{1,0},{1,0},{1,0},0},{{4,0,{10053120},0},4,0,0,0,"",{1,0},{1,0},{1,0},0},{{4,0,{13434624},0},4,0,0,0,"",{1,0},{1,0},{1,0},0},{{4,4,{0},4},4,0,0,0,"",{1,0},{1,0},{1,0},0},0,0,0.23125,0,0.76875,0.352777777777777777777777778,0,0.313888888888888888888888889,0.76875,0,0,0.961111111111111111111111111,0,0,0,0,0,0.17,0,0.83,0.08,0,0,0.83,0,0,0.92,{0,0},{0,0},{0,0},{0,0},{0,14,{4,4,{0},4},{4,4,{0},4},0,0},{0,14,{4,4,{0},4},{4,4,{0},4},0,0},0,0,{0,0,0,0,0},{0,0,0,0},0,{{1,{1,1,{"#","<Элемент 1> <Элемент 2>"\000A<Элемент 1> <Элемент 2>"\000A2"}},0},0},{{1,{1,1,{"#","<Элемент 1> <Элемент 2>"\000A<Элемент 1> <Элемент 3>"\000A3"}},0},0},{{1,{1,1,{"#","<Элемент 1> <Элемент 2>"\000A<Элемент 4> <Элемент 5>"\000A3"}},0},0},{{1,{1,1,{"#","<Элемент 1> <Элемент 2>"\000A<Элемент 4> <Элемент 6>"\000A1"}},0},0},{{1,{1,1,{"#","<Элемент 1> <Элемент 3>"\000A<Элемент 1> <Элемент 2>"\000A2"}},0},0},{{1,{1,1,{"#","<Элемент 1> <Элемент 3>"\000A<Элемент 1> <Элемент 3>"\000A4"}},0},0},{{1,{1,1,{"#","<Элемент 1> <Элемент 3>"\000A<Элемент 4> <Элемент 5>"\000A2"}},0},0},{{1,{1,1,{"#","<Элемент 1> <Элемент 3>"\000A<Элемент 4> <Элемент 6>"\000A3"}},0},0},{{1,{1,1,{"#","<Элемент 4> <Элемент 5>"\000A<Элемент 1> <Элемент 2>"\000A2"}},0},0},{{1,{1,1,{"#","<Элемент 4> <Элемент 5>"\000A<Элемент 1> <Элемент 3>"\000A4"}},0},0},{{1,{1,1,{"#","<Элемент 4> <Элемент 5>"\000A<Элемент 4> <Элемент 5>"\000A4"}},0},0},{{1,{1,1,{"#","<Элемент 4> <Элемент 5>"\000A<Элемент 4> <Элемент 6>"\000A3"}},0},0},{{1,{1,1,{"#","<Элемент 4> <Элемент 6>"\000A<Элемент 1> <Элемент 2>"\000A3"}},0},0},{{1,{1,1,{"#","<Элемент 4> <Элемент 6>"\000A<Элемент 1> <Элемент 3>"\000A2"}},0},0},{{1,{1,1,{"#","<Элемент 4> <Элемент 6>"\000A<Элемент 4> <Элемент 5>"\000A5"}},0},0},{{1,{1,1,{"#","<Элемент 4> <Элемент 6>"\000A<Элемент 4> <Элемент 6>"\000A4"}},0},0},,60,{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{0,0,{0,1,0,1,0},0,0},0,0,0,0,0,0,0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4}}},{0,{0,{3,0,1,0,{1,{8,0,0,0,0,0,{"U"},{1,0},{"U"},0,4294901761},0},{0,1,{0,{4,0,{0},0},{4,0,{0},0}}},1,0}},{0,{3,0,1,0,{1,{8,0,0,0,0,0,{"U"},{1,0},{"U"},0,4294901761},0},{0,1,{0,{4,0,{0},0},{4,0,{0},0}}},1,0}},{0,0},1,1},1,6,12,1,2,1,0,{4,3,{-7},3},{4,3,{-3},3},1})OOF");
}

DecodedControl decode_default_complex_control(
    const LV& record, std::string_view path, const GeometryContext& context,
    model::ControlKind kind) {
    const auto& descriptor = model::metamodel::descriptor_for(kind);
    const auto geometry_slot = control_geometry_slot(descriptor.guid);
    require_arity(record, geometry_slot + 3, path);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (id == 0 || id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        fail("OOF1122", child_path(path, 1), "positive int64 control ID", std::to_string(id), "Default complex control ID is invalid");
    if (kind == model::ControlKind::graphical_schema_field) {
        require_exact(record.items[2], canonical_graphical_schema_field_info(), child_path(path, 2),
            "GraphicalSchemaField contains nondefault properties or nonempty schema content");
    } else if (kind == model::ControlKind::pivot_chart) {
        require_exact(record.items[2], canonical_default_pivot_chart_info(), child_path(path, 2),
            "PivotChart contains nondefault diagram, source, role, or settings values");
    } else {
        require_exact(record.items[2], canonical_geographical_schema_field_info(), child_path(path, 2),
            "GeographicalSchemaField contains nondefault properties");
        require_exact(record.items[3], canonical_geographical_schema(), child_path(path, 3),
            "GeographicalSchemaField contains nondefault or nonempty schema content");
    }
    auto geometry = decode_geometry(record.items[geometry_slot], child_path(path, geometry_slot), context);
    const auto metadata_slot = geometry_slot + 1;
    const auto& metadata = record.items[metadata_slot];
    const auto metadata_path = child_path(path, metadata_slot);
    require_arity(metadata, 6, metadata_path);
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty", "Default complex control name is required");
    require_exact(metadata, list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "Default complex control contains unsupported metadata");
    require_exact(record.items[geometry_slot + 2], list({raw("0")}), child_path(path, geometry_slot + 2),
        "Default complex control cannot contain form controls");
    model::ControlPayload payload = kind == model::ControlKind::graphical_schema_field
        ? model::ControlPayload{model::GraphicalSchemaFieldPayload{}}
        : kind == model::ControlKind::pivot_chart
        ? model::ControlPayload{model::PivotChartPayload{}}
        : model::ControlPayload{model::GeographicalSchemaFieldPayload{}};
    model::ControlNode control{model::ObjectId{id}, name, std::move(payload)};
    control.position = std::move(geometry.position);
    return {std::move(control), std::nullopt, std::move(geometry.incoming), std::nullopt, {}};
}

LV canonical_empty_gantt_info() {
    return list({
        raw("19"),
        list({raw("0"), list({raw("11")}), list({raw("75"), raw("1"), raw("0"), raw("1"), raw("0"), list({raw("4"), raw("0"), list({raw("11837108")}), raw("0")}), list({raw("4"), raw("0"), list({raw("0")}), raw("1"), raw("2"), raw("0"), raw("e5cabe59-d992-4d31-8086-3116931aff81"), raw("0")}), raw("1"), list({raw("1"), raw("1"), list({string_value("ru"), string_value("Сводная")})}), raw("0"), raw("0"), raw("0"), raw("1"), list({string_value("U")}), list({string_value("U")}), raw("0"), raw("1"), raw("0"), raw("-1"), raw("0"), raw("4"), raw("0"), string_value(", "), raw("4"), list({raw("1"), raw("0")}), list({raw("1"), raw("0")}), list({raw("4"), raw("3"), list({raw("-3")}), raw("3")}), raw("0"), raw("0"), list({raw("1"), raw("0")}), raw("1"), raw("1"), list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("0"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}), list({raw("4"), raw("3"), list({raw("-22")}), raw("3")}), list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("0"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}), list({raw("4"), raw("3"), list({raw("-22")}), raw("3")}), list({raw("3"), raw("0"), list({raw("0")}), raw("1"), raw("1"), raw("0"), raw("00000000-0000-0000-0000-000000000000")}), list({raw("4"), raw("3"), list({raw("-22")}), raw("3")}), raw("0"), list({raw("4"), raw("3"), list({raw("-1")}), raw("3")}), raw("1"), list({raw("4"), raw("3"), list({raw("-1")}), raw("3")}), raw("1"), list({raw("4"), raw("3"), list({raw("-1")}), raw("3")}), raw("0"), list({raw("4"), raw("0"), list({raw("16777215")}), raw("0")}), list({raw("4"), raw("3"), list({raw("-3")}), raw("3")}), list({raw("4"), raw("3"), list({raw("-3")}), raw("3")}), list({raw("4"), raw("3"), list({raw("-3")}), raw("3")}), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), raw("1"), raw("1"), raw("1"), raw("1"), raw("1"), list({raw("1"), raw("0")}), raw("0"), list({raw("4"), raw("0"), list({raw("0")}), raw("1"), raw("1"), raw("0"), raw("e5cabe59-d992-4d31-8086-3116931aff81"), raw("0")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("1"), raw("1"), raw("0"), raw("4"), raw("30"), raw("1"), raw("0"), raw("0"), raw("0"), raw("0"), raw("1"), raw("0"), raw("0"), raw("0"), raw("0"), raw("1"), raw("1"), raw("2"), list({raw("1"), raw("0")}), raw("1"), raw("0"), raw("0"), raw("1"), list({raw("4"), raw("0"), list({raw("169")}), raw("0")}), raw("0"), raw("0"), list({raw("1"), raw("0"), raw("0"), raw("0")}), raw("0"), raw("180"), raw("5"), raw("1"), raw("0"), raw("4"), list({raw("4"), raw("0"), list({raw("11119017")}), raw("0")}), raw("1"), raw("0"), raw("1"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("1"), raw("1"), raw("0"), raw("0"), raw("1"), raw("1"), raw("0"), list({raw("4"), raw("3"), list({raw("-22")}), raw("3")}), list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("0"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}), string_value(""), raw("0"), raw("1"), raw("14"), raw("2"), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), raw("1"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("3"), raw("0"), list({raw("0")}), raw("1"), raw("1"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("1"), raw("1"), raw("1"), raw("0"), raw("0"), raw("95"), raw("1e-1"), raw("1e-1"), raw("3e-2"), list({raw("4"), raw("0"), list({raw("0")}), raw("1"), raw("1"), raw("0"), raw("e5cabe59-d992-4d31-8086-3116931aff81"), raw("0")}), list({raw("4"), raw("0"), list({raw("0")}), raw("0")}), raw("2"), raw("255"), raw("0"), raw("0"), raw("00000000-0000-0000-0000-000000000000"), raw("0"), list({raw("0"), raw("0")}), raw("0"), list({raw("0"), raw("0"), list({raw("0"), raw("1"), raw("0"), raw("1"), raw("0")}), raw("0"), raw("0")}), list({raw("0"), raw("0"), list({raw("0"), raw("1"), raw("0"), raw("1"), raw("0")}), raw("0"), raw("0")}), raw("0"), raw("0"), raw("2"), raw("-2"), raw("1"), raw("10"), raw("1"), raw("20"), raw("0"), raw("0"), list({raw("2"), raw("0"), raw("0"), raw("2"), list({raw("1"), raw("0")}), list({raw("1"), raw("4"), raw("0.5"), raw("0.5"), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("1"), list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("1"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("4"), raw("2"), raw("0")}), raw("2"), raw("0"), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("2"), list({raw("1"), raw("0")}), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0")}), list({raw("2"), raw("0"), raw("0"), raw("2"), list({raw("1"), raw("0")}), list({raw("1"), raw("4"), raw("0.5"), raw("0.5"), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("1"), list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("1"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("4"), raw("2"), raw("0")}), raw("2"), raw("0"), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("2"), list({raw("1"), raw("0")}), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0")}), list({raw("2"), raw("0"), raw("0"), raw("2"), list({raw("1"), raw("0")}), list({raw("1"), raw("4"), raw("0.5"), raw("0.5"), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("1"), list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("1"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("4"), raw("2"), raw("0")}), raw("2"), raw("0"), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("2"), list({raw("1"), raw("0")}), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0")}), raw("0"), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("0"), list({list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("4"), raw("0"), raw("0"), raw("0"), string_value(""), list({raw("1"), raw("0")}), list({raw("1"), raw("0")}), list({raw("1"), raw("0")}), raw("0")}), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("1"), raw("1"), raw("0"), raw("0"), raw("1"), raw("1"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0.17"), raw("0"), raw("0.83"), raw("0.08"), raw("0"), raw("0"), raw("0.83"), raw("0"), raw("0"), raw("0.92"), list({raw("0"), raw("0")}), list({raw("0"), raw("0")}), list({raw("0"), raw("0")}), list({raw("0"), raw("0")}), list({raw("0"), raw("14"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("0"), raw("0")}), list({raw("0"), raw("14"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("0"), raw("0")}), raw("0"), raw("0"), list({raw("0"), raw("0"), raw("0"), raw("0"), raw("0")}), list({raw("0"), raw("0"), raw("0"), raw("0")}), raw("0"), raw(""), raw("60"), list({raw("2"), raw("0"), raw("0"), raw("2"), list({raw("1"), raw("0")}), list({raw("1"), raw("4"), raw("0.5"), raw("0.5"), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("1"), list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("1"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("4"), raw("2"), raw("0")}), raw("2"), raw("0"), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("2"), list({raw("1"), raw("0")}), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0")}), list({raw("0"), raw("0"), list({raw("0"), raw("1"), raw("0"), raw("1"), raw("0")}), raw("0"), raw("0")}), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")})})}),
        list({raw("1"), list({raw("3"), raw("0"), raw("1"), raw("0"), list({raw("2"), list({raw("8"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), list({string_value("U")}), list({raw("1"), raw("0")}), list({string_value("U")}), raw("0"), raw("4294901761")}), list({raw("4"), raw("0"), list({raw("0")}), string_value(""), raw("-1"), raw("-1"), raw("1"), raw("0"), string_value("")}), list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")})}), list({raw("0"), raw("1"), list({raw("0"), list({raw("0"), list({raw("4"), raw("0"), list({raw("0")}), raw("0")}), list({raw("4"), raw("0"), list({raw("0")}), raw("0")})}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")})})}), raw("1"), raw("0")})}),
        list({raw("0"), list({raw("3"), raw("0"), raw("1"), raw("0"), list({raw("3"), list({raw("8"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), list({string_value("U")}), list({raw("1"), raw("0")}), list({string_value("U")}), raw("0"), raw("4294901761")})}), list({raw("0"), raw("1"), list({raw("0"), list({raw("0"), list({raw("4"), raw("0"), list({raw("0")}), raw("0")}), list({raw("4"), raw("0"), list({raw("0")}), raw("0")})}), list({raw("4"), raw("0"), list({raw("0")}), raw("0")})})}), raw("1"), raw("0")})}),
        raw("0"),
        raw("0"),
        raw("1"),
        list({raw("3"), raw("0"), raw("1"), list({raw("8"), raw("30"), raw("1"), raw("1"), list({raw("4"), raw("0"), list({raw("0")}), raw("2"), raw("1"), raw("0"), raw("e5cabe59-d992-4d31-8086-3116931aff81"), raw("0")}), list({raw("4"), raw("0"), list({raw("12632256")}), raw("0")}), raw("3"), list({raw("1"), raw("0")}), list({raw("0"), list({raw("1"), raw("0"), raw("0")})}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("1")}), raw("0"), list({raw("4"), raw("3"), list({raw("-10")}), raw("3")}), list({raw("4"), raw("3"), list({raw("-3")}), raw("3")}), raw("0")}),
        raw("2"),
        raw("50"),
        raw("1"),
        raw("1"),
        raw("20261001000000"),
        raw("20261101000000"),
        raw("20261001000000"),
        raw("0"),
        raw("3"),
        raw("30"),
        raw("0"),
        raw("1"),
        raw("0"),
        list({raw("1"), raw("0")}),
        list({raw("4"), raw("0"), list({raw("16777215")}), raw("0")}),
        list({raw("3"), list({raw("0"), list({raw("1"), raw("0"), raw("0")}), raw("0")}), list({raw("0"), raw("0")})}),
        raw("0"),
        list({raw("4"), raw("0"), list({raw("5592405")}), raw("0")}),
        list({raw("4"), raw("0"), list({raw("0")}), raw("1"), raw("1"), raw("0"), raw("e5cabe59-d992-4d31-8086-3116931aff81"), raw("0")}),
        list({raw("0"), raw("0"), raw("0")}),
        raw("0"),
        raw("0"),
        raw("1"),
        raw("0"),
        raw("0")
    });
}

LV gantt_dimension_value(model::ObjectId id, std::string_view value,
                         std::string_view text, std::uint64_t next_id,
                         std::uint64_t cache_key) {
    if (id.value() > UINT32_MAX || cache_key > UINT32_MAX)
        fail("OOF1112", "$/GanttChart/Dimensions", "dimension ID and cache key within uint32", "overflow", "Gantt dimension key is too large");
    return list({raw("8"), raw(std::to_string(id.value())), raw("0"), raw("0"),
        raw(std::to_string(next_id)), raw("0"),
        list({string_value("S"), string_value(std::string(value))}),
        encoded_localized(text), list({string_value("U")}),
        raw(std::to_string(cache_key)), raw("0")});
}

LV gantt_default_dimension_value(std::uint64_t first_id, std::uint64_t last_id) {
    return list({raw("8"), raw("0"), raw("0"), raw(std::to_string(first_id)), raw("0"), raw(std::to_string(last_id)),
        list({string_value("U")}), list({raw("1"), raw("0")}), list({string_value("U")}),
        raw("0"), raw("4294901761")});
}

LV gantt_palette(const model::GanttChartPayload& data, bool points) {
    const auto count = std::min<std::size_t>(points ? data.points.size() : data.series.size(), 16);
    const auto packed_color = [](std::uint32_t value) {
        return list({raw("4"), raw("0"), list({raw(std::to_string(value))}), raw("0")});
    };
    const LV absolute_black = packed_color(0);
    const LV automatic = list({raw("4"), raw("4"), list({raw("0")}), raw("4")});
    // chart.so 8.5.1.1343: инициализатор 0xda0e60, методы ColorManager 0xda1620/0xda16e0.
    constexpr std::array<std::uint32_t, 16> default_colors{
        15700567, 5410297, 6733657, 10517142, 7829482, 2525628, 11110461, 12341090,
        13469947, 6029465, 7597468, 3634944, 14283127, 10183424, 10526880, 5263440};
    const auto color_at = [&](std::size_t index) {
        if (index == 0) return std::pair<LV, LV>{absolute_black, absolute_black};
        const auto ordinal = (index - 1) / 2;
        return std::pair<LV, LV>{packed_color(default_colors[ordinal % default_colors.size()]),
            index % 2 == 0 ? packed_color(default_colors[(ordinal + 1) % default_colors.size()]) : absolute_black};
    };
    std::vector<LV> entries{raw("0"), raw(std::to_string(count * 2 + 1))};
    for (std::size_t index = 0; index < count * 2 + 1; ++index) {
        const auto [first, second] = color_at(index);
        LV pair = list({raw("0"), first, second});
        if (points) entries.push_back(list({raw("0"), std::move(pair), automatic, automatic}));
        else entries.push_back(list({raw("0"), std::move(pair), absolute_black}));
    }
    return list(std::move(entries));
}

LV gantt_dimension_table(const model::GanttChartPayload& data, bool points) {
    const std::size_t count = points ? data.points.size() : data.series.size();
    if (count > (std::numeric_limits<std::uint32_t>::max() - 1u) / 2u)
        fail("OOF1112", points ? "$/Gantt/Points" : "$/Gantt/Series", "safe uint32 palette count", "count overflow", "Gantt palette count is too large");
    std::vector<LV> rows{raw("3"), raw("0"), raw(std::to_string(count + 1))};
    if (points) {
        for (std::size_t index = count; index > 0; --index) {
            const auto& item = data.points[index - 1];
            const std::uint64_t next = index < count ? data.points[index].id.value() : 0;
            if (item.id.value() > UINT32_MAX || index > UINT32_MAX / 2u)
                fail("OOF1112", "$/Gantt/Points", "IDs and ordinal cache keys within uint32", "overflow", "Gantt point key is too large");
            rows.push_back(raw(std::to_string(item.id.value())));
            rows.push_back(list({raw("2"), gantt_dimension_value(item.id, item.value, item.text, next,
                static_cast<std::uint64_t>((index - 1) % 16 + 1) * 2u),
                list({raw("4"), raw("0"), list({raw("0")}), string_value(""), raw("-1"), raw("-1"), raw("1"), raw("0"), string_value("")}),
                list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")})}));
        }
    } else {
        for (std::size_t index = count; index > 0; --index) {
            const auto& item = data.series[index - 1];
            const std::uint64_t next = index < count ? data.series[index].id.value() : 0;
            if (item.id.value() > UINT32_MAX || index > UINT32_MAX / 2u)
                fail("OOF1112", "$/Gantt/Series", "IDs and ordinal cache keys within uint32", "overflow", "Gantt series key is too large");
            rows.push_back(raw(std::to_string(item.id.value())));
            rows.push_back(list({raw("3"), gantt_dimension_value(item.id, item.value, item.text, next,
                static_cast<std::uint64_t>((index - 1) % 16 + 1) * 2u)}));
        }
    }
    rows.push_back(raw("0"));
    if (points) {
        const auto first_id = count == 0 ? 0 : data.points.front().id.value();
        const auto last_id = count == 0 ? 0 : data.points.back().id.value();
        rows.push_back(list({raw("2"), gantt_default_dimension_value(first_id, last_id),
            list({raw("4"), raw("0"), list({raw("0")}), string_value(""), raw("-1"), raw("-1"), raw("1"), raw("0"), string_value("")}),
            list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")})}));
    } else {
        const auto first_id = count == 0 ? 0 : data.series.front().id.value();
        const auto last_id = count == 0 ? 0 : data.series.back().id.value();
        rows.push_back(list({raw("3"), gantt_default_dimension_value(first_id, last_id)}));
    }
    rows.push_back(gantt_palette(data, points));
    rows.push_back(raw("1"));
    rows.push_back(raw("0"));
    return list({raw(points ? "1" : "0"), list(std::move(rows))});
}

LV gantt_interval_record(const model::GanttInterval& interval, std::uint64_t pair,
                         std::size_t ordinal) {
    const LV color = list({raw("4"), raw("4"), list({raw("0")}), raw("4")});
    return list({raw("5"), raw(std::to_string(pair)),
        raw(value_codec::date_to_platform(interval.start_date.canonical)),
        raw(value_codec::date_to_platform(interval.end_date.canonical)), encoded_localized(interval.text),
        list({string_value("U")}), color, raw(std::to_string(ordinal)), color});
}

LV gantt_next_table() {
    return list({raw("3"), raw("0"), raw("1"),
        list({raw("8"), raw("30"), raw("1"), raw("1"),
            list({raw("4"), raw("0"), list({raw("0")}), raw("2"), raw("1"), raw("0"),
                raw("e5cabe59-d992-4d31-8086-3116931aff81"), raw("0")}),
            list({raw("4"), raw("0"), list({raw("12632256")}), raw("0")}), raw("3"),
            list({raw("1"), raw("0")}), list({raw("0"), list({raw("1"), raw("0"), raw("0")})}),
            list({raw("4"), raw("4"), list({raw("0")}), raw("4")}),
            list({raw("4"), raw("4"), list({raw("0")}), raw("4")}), raw("1")}),
        raw("0"), list({raw("4"), raw("3"), list({raw("-10")}), raw("3")}),
        list({raw("4"), raw("3"), list({raw("-3")}), raw("3")}), raw("0")});
}

LV encode_gantt_chart_info(const model::ControlNode& control) {
    const auto* data = std::get_if<model::GanttChartPayload>(&control.payload);
    if (!data) fail("OOF1122", "$/GanttChart", "GanttChartPayload", "different payload", "Gantt payload is invalid");
    if (std::ranges::any_of(data->series, [](const auto& item) { return item.color.has_value(); }) ||
        std::ranges::any_of(data->points, [](const auto& item) { return item.color.has_value(); }))
        fail("OOF1122", "$/GanttChart/Color", "no explicit Series or Point Color until native mapping is proven", "explicit Color", "Gantt dimension colors are not supported by the proven storage profile");
    std::map<std::uint64_t, std::size_t> pair_ordinals;
    std::map<std::uint64_t, bool> pairs;
    for (const auto& interval : data->intervals) {
        const auto point = interval.point_ref.value();
        const auto series = interval.series_ref.value();
        if (point > UINT32_MAX || series > UINT32_MAX)
            fail("OOF1122", "$/GanttChart/Intervals", "dimension IDs within uint32", "ID overflow", "Gantt pair key cannot represent this ID");
        ++pair_ordinals[(point << 32) | series];
        pairs.emplace((point << 32) | series, true);
    }
    const std::size_t n = data->intervals.size(), u = pairs.size();
    if (n > UINT32_MAX || u > UINT32_MAX)
        fail("OOF1112", "$/GanttChart/Intervals", "interval and pair counts within uint32", "count overflow", "Gantt data count is too large");
    if (n > (std::numeric_limits<std::size_t>::max() - 33) / 2 ||
        u > (std::numeric_limits<std::size_t>::max() - 33 - 2 * n) / 2)
        fail("OOF1112", "$/GanttChart/Intervals", "safe dynamic count arithmetic", "overflow", "Gantt data is too large");
    LV base = canonical_empty_gantt_info();
    require_arity(base, 33, "$/GanttChart/InfoDefaults");
    base.items[2] = gantt_dimension_table(*data, true);
    base.items[3] = gantt_dimension_table(*data, false);
    base.items[4] = raw(std::to_string(n));
    std::vector<LV> items(base.items.begin(), base.items.begin() + 5);
    pair_ordinals.clear();
    for (const auto& interval : data->intervals) {
        const auto pair = (interval.point_ref.value() << 32) | interval.series_ref.value();
        items.push_back(raw(std::to_string(pair)));
        items.push_back(gantt_interval_record(interval, pair, ++pair_ordinals[pair]));
    }
    items.push_back(raw(std::to_string(u)));
    const LV color = list({raw("4"), raw("4"), list({raw("0")}), raw("4")});
    for (const auto& [pair, unused] : pairs) {
        static_cast<void>(unused);
        items.push_back(raw(std::to_string(pair)));
        items.push_back(list({raw("4"), raw(std::to_string(pair)), list({raw("1"), raw("0")}),
            list({string_value("U")}), raw("0"), color, color, color, color,
            list({raw("0"), raw("1"), raw("0"), raw("0"), raw("0")}), raw("0")}));
    }
    items.push_back(raw("1"));
    items.push_back(gantt_next_table());
    items.insert(items.end(), base.items.begin() + 8, base.items.end());
    LV info = list(std::move(items));
    const std::size_t shift = 2 * n + 2 * u;
    bool auto_full = true;
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("AutoFullInterval"))) {
        const auto* value = std::get_if<bool>(&entry->value);
        if (!value) fail("OOF1122", "$/GanttChart/AutoFullInterval", "Boolean", "different value", "Gantt AutoFullInterval is invalid");
        auto_full = *value;
    }
    const bool has_begin = control.properties().find(model::PropertyId::from_name("FullIntervalBegin")) != nullptr;
    const bool has_end = control.properties().find(model::PropertyId::from_name("FullIntervalEnd")) != nullptr;
    if (auto_full && (has_begin || has_end))
        fail("OOF1122", "$/GanttChart/FullInterval", "omitted explicit bounds when AutoFullInterval is true",
            "explicit bounds", "Gantt storage cannot prove round-trip of explicit bounds in automatic mode");
    info.items.at(11 + shift) = raw(auto_full ? "1" : "0");
    for (const auto [name, slot] : {std::pair<std::string_view, std::size_t>{"FullIntervalBegin", 12}, {"FullIntervalEnd", 13}}) {
        const auto* entry = control.properties().find(model::PropertyId::from_name(name));
        if (entry != nullptr) {
            const auto* date = std::get_if<model::DateValue>(&entry->value);
            if (!date) fail("OOF1122", "$/GanttChart/" + std::string(name), "DateValue", "different value", "Gantt date property is invalid");
            info.items.at(slot + shift) = raw(value_codec::date_to_platform(date->canonical));
        }
    }
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("FullIntervalBegin"))) {
        const auto* date = std::get_if<model::DateValue>(&entry->value);
        if (!date) fail("OOF1122", "$/GanttChart/FullIntervalBegin", "DateValue", "different value", "Gantt date property is invalid");
        info.items.at(14 + shift) = raw(value_codec::date_to_platform(date->canonical));
    }
    if (info.items.size() != 33 + shift)
        fail("OOF1114", "$/GanttChart/Info", "33 + 2*N + 2*U slots", std::to_string(info.items.size()), "Gantt cursor invariant failed");
    return info;
}

std::vector<std::tuple<model::ObjectId, std::string, std::string>> decode_gantt_dimensions(
    const LV& table, bool points, std::string_view path) {
    require_arity(table, 2, path);
    require_raw_constant(table.items[0], points ? "1" : "0", child_path(path, 0));
    const std::string rows_path = child_path(path, 1);
    const LV& rows = table.items[1];
    require_list(rows, rows_path);
    if (rows.items.size() < 8)
        fail("OOF1103", rows_path, "Gantt table header and default row", describe(rows), "Gantt dimension table is truncated");
    require_raw_constant(rows.items[0], "3", child_path(rows_path, 0));
    require_raw_constant(rows.items[1], "0", child_path(rows_path, 1));
    const auto declared = integer_atom<std::uint32_t>(rows.items[2], child_path(rows_path, 2));
    if (declared == 0) fail("OOF1103", child_path(rows_path, 2), "dimension count including default row", "0", "Gantt dimension table is empty");
    const std::size_t count = declared - 1;
    if (count > (std::numeric_limits<std::size_t>::max() - 8) / 2)
        fail("OOF1102", rows_path, "safe dimension count", std::to_string(count), "Gantt dimension count overflows its record layout");
    if (rows.items.size() != 8 + 2 * count)
        fail("OOF1102", rows_path, "dimension rows matching declared count", describe(rows), "Gantt dimension table count is inconsistent");
    std::map<std::uint64_t, std::tuple<model::ObjectId, std::string, std::string, std::uint64_t>> by_id;
    std::set<std::uint64_t> referenced;
    for (std::size_t i = 0; i <= count; ++i) {
        const std::size_t cursor = 3 + 2 * i;
        const auto key = integer_atom<std::uint64_t>(rows.items[cursor], child_path(rows_path, cursor));
        const LV& row = rows.items[cursor + 1];
        require_arity(row, points ? 4 : 2, child_path(rows_path, cursor + 1));
        require_raw_constant(row.items[0], points ? "2" : "3", child_path(rows_path, cursor + 1) + "/0");
        const LV& value = row.items[1];
        require_arity(value, 11, child_path(rows_path, cursor + 1) + "/1");
        require_raw_constant(value.items[0], "8", child_path(rows_path, cursor + 1) + "/1/0");
        if (integer_atom<std::uint64_t>(value.items[1], child_path(rows_path, cursor + 1) + "/1/1") != key)
            fail("OOF1114", child_path(rows_path, cursor), std::to_string(key), describe(value), "Gantt dimension key differs from its native row ID");
        if (key == 0) continue;
        const auto next = integer_atom<std::uint64_t>(value.items[4], child_path(rows_path, cursor + 1) + "/1/4");
        if (next != 0 && !referenced.insert(next).second)
            fail("OOF1114", child_path(rows_path, cursor + 1) + "/1/4", "unique next ID", std::to_string(next), "Gantt dimension chain branches");
        const LV& stored_value = value.items[6];
        require_arity(stored_value, 2, child_path(rows_path, cursor + 1) + "/1/6");
        if (string_atom(stored_value.items[0], child_path(rows_path, cursor + 1) + "/1/6/0") != "S")
            fail("OOF1114", child_path(rows_path, cursor + 1) + "/1/6/0", "String value marker S", describe(stored_value.items[0]), "Gantt dimension value type is unsupported");
        const std::string dimension_value = string_atom(stored_value.items[1], child_path(rows_path, cursor + 1) + "/1/6/1");
        const std::string dimension_text = decoded_single_language_text(value.items[7], child_path(rows_path, cursor + 1) + "/1/7");
        if (points)
            require_exact(row.items[2], list({raw("4"), raw("0"), list({raw("0")}), string_value(""), raw("-1"), raw("-1"), raw("1"), raw("0"), string_value("")}),
                child_path(rows_path, cursor + 1) + "/2", "Gantt Point private row defaults differ from the proven profile");
        if (!by_id.emplace(key, std::make_tuple(model::ObjectId{key}, dimension_value, dimension_text, next)).second)
            fail("OOF1114", child_path(rows_path, cursor), "unique Gantt dimension ID", std::to_string(key), "Gantt dimension ID is duplicated");
    }
    const std::size_t default_cursor = 3 + 2 * count;
    require_raw_constant(rows.items[default_cursor + 3], "1", child_path(rows_path, default_cursor + 3));
    require_raw_constant(rows.items[default_cursor + 4], "0", child_path(rows_path, default_cursor + 4));
    const auto palette_path = child_path(rows_path, default_cursor + 2);
    const LV& palette = rows.items[default_cursor + 2];
    require_list(palette, palette_path);
    const std::size_t expected_palette = 2 * std::min<std::size_t>(count, 16) + 1;
    require_arity(palette, 2 + expected_palette, palette_path);
    require_raw_constant(palette.items[0], "0", child_path(palette_path, 0));
    if (integer_atom<std::uint32_t>(palette.items[1], child_path(palette_path, 1)) != expected_palette)
        fail("OOF1114", child_path(palette_path, 1), std::to_string(expected_palette), describe(palette.items[1]), "Gantt palette count is inconsistent");
    std::uint64_t first = 0;
    for (const auto& [key, item] : by_id) {
        if (!referenced.contains(key)) {
            if (first != 0) fail("OOF1114", rows_path, "one head for Gantt dimension chain", std::to_string(key), "Gantt dimension chain has multiple heads");
            first = key;
        }
    }
    std::vector<std::tuple<model::ObjectId, std::string, std::string>> output;
    std::set<std::uint64_t> visited;
    while (first != 0) {
        const auto found = by_id.find(first);
        if (found == by_id.end() || !visited.insert(first).second)
            fail("OOF1114", rows_path, "acyclic Gantt dimension next chain", std::to_string(first), "Gantt dimension chain is dangling or cyclic");
        output.emplace_back(std::get<0>(found->second), std::get<1>(found->second), std::get<2>(found->second));
        first = std::get<3>(found->second);
    }
    if (visited.size() != count) fail("OOF1114", rows_path, "all Gantt dimensions reachable from chain head", std::to_string(visited.size()), "Gantt dimension rows are unlinked");
    return output;
}

void require_gantt_dimension_table_match(const LV& actual, const LV& expected,
                                         std::string_view path) {
    require_arity(actual, 2, path);
    require_arity(expected, 2, path);
    if (list_stream::dump_compact(actual.items[0]) != list_stream::dump_compact(expected.items[0]))
        fail("OOF1114", child_path(path, 0), list_stream::dump_compact(expected.items[0]),
            list_stream::dump_compact(actual.items[0]), "Gantt dimension kind changed");
    const LV& actual_rows = actual.items[1];
    const LV& expected_rows = expected.items[1];
    require_list(actual_rows, child_path(path, 1));
    require_list(expected_rows, child_path(path, 1));
    if (actual_rows.items.size() < 8 || expected_rows.items.size() < 8)
        fail("OOF1103", child_path(path, 1), "Gantt dimension table with header and default row", "truncated", "Gantt dimension table is truncated");
    const auto count = integer_atom<std::uint32_t>(expected_rows.items[2], child_path(path, 1) + "/2") - 1;
    if (actual_rows.items.size() != 8 + 2 * count || expected_rows.items.size() != 8 + 2 * count)
        fail("OOF1102", child_path(path, 1), std::to_string(8 + 2 * count),
            std::to_string(actual_rows.items.size()), "Gantt dimension row count differs from its canonical table");
    for (std::size_t i = 0; i < 3; ++i) {
        if (list_stream::dump_compact(actual_rows.items[i]) != list_stream::dump_compact(expected_rows.items[i]))
            fail("OOF1114", child_path(path, 1) + "/" + std::to_string(i),
                list_stream::dump_compact(expected_rows.items[i]), list_stream::dump_compact(actual_rows.items[i]),
                "Gantt dimension table header differs from its canonical value");
    }
    const auto keyed_rows = [&](const LV& rows, std::string_view rows_path) {
        std::map<std::uint64_t, std::string> by_key;
        for (std::size_t i = 0; i <= count; ++i) {
            const auto cursor = 3 + 2 * i;
            const auto key = integer_atom<std::uint64_t>(rows.items[cursor], child_path(rows_path, cursor));
            if (!by_key.emplace(key, list_stream::dump_compact(rows.items[cursor + 1])).second)
                fail("OOF1114", child_path(rows_path, cursor), "unique dimension ID", std::to_string(key), "Gantt dimension ID is duplicated");
        }
        return by_key;
    };
    const auto actual_by_key = keyed_rows(actual_rows, child_path(path, 1));
    const auto expected_by_key = keyed_rows(expected_rows, child_path(path, 1));
    if (actual_by_key != expected_by_key)
        fail("OOF1114", child_path(path, 1), "same named row records by dimension ID", "row content differs", "Gantt dimension row differs from its canonical named value");
    const std::size_t tail = 5 + 2 * count;
    for (std::size_t i = tail; i < actual_rows.items.size(); ++i) {
        if (list_stream::dump_compact(actual_rows.items[i]) != list_stream::dump_compact(expected_rows.items[i]))
            fail("OOF1114", child_path(path, 1) + "/" + std::to_string(i),
                list_stream::dump_compact(expected_rows.items[i]), list_stream::dump_compact(actual_rows.items[i]),
                "Gantt dimension defaults or palette differ from the proven profile");
    }
}

void normalize_gantt_runtime_layout_values(const LV& actual, LV& normalized, std::string_view path) {
    constexpr std::array<std::size_t, 8> platform_adjusted_slots{102, 104, 106, 107, 167, 169, 171, 172};
    const std::string section_path = child_path(path, 1) + "/2";
    const LV& actual_section = at(at(actual, 1, path), 2, child_path(path, 1));
    LV& normalized_section = normalized.items.at(1).items.at(2);
    require_arity(actual_section, 222, section_path);
    require_arity(normalized_section, 222, section_path);
    for (const std::size_t slot : platform_adjusted_slots) {
        const auto slot_path = child_path(section_path, slot);
        const std::string value = raw_atom(actual_section.items[slot], slot_path);
        double parsed = 0.0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed,
            std::chars_format::general);
        if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
            !std::isfinite(parsed) || parsed < 0.0 || parsed > 1.0) {
            fail("OOF1114", slot_path, "finite raw numeric layout value in [0,1]", value,
                "Gantt platform-adjusted layout value is outside the verified numeric domain");
        }
        normalized_section.items[slot] = actual_section.items[slot];
    }
}


DecodedControl decode_gantt_chart(const LV& record, std::string_view path,
                                  const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::gantt_chart);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    const LV& info = record.items[2];
    const std::string info_path = child_path(path, 2);
    require_list(info, info_path);
    if (info.items.size() < 33) fail("OOF1103", info_path, "at least 33 Gantt info fields", describe(info), "Gantt info record is truncated");
    require_raw_constant(info.items[0], "19", child_path(info_path, 0));
    const auto interval_count = integer_atom<std::uint32_t>(info.items[4], child_path(info_path, 4));
    const auto points = decode_gantt_dimensions(info.items[2], true, child_path(info_path, 2));
    const auto series = decode_gantt_dimensions(info.items[3], false, child_path(info_path, 3));
    const std::size_t n = interval_count;
    if (n > (std::numeric_limits<std::size_t>::max() - 33) / 2)
        fail("OOF1102", child_path(info_path, 4), "safe interval count", std::to_string(n), "Gantt interval count overflows its record layout");
    const std::size_t interval_cursor = 5;
    if (info.items.size() < interval_cursor + 2 * n + 1)
        fail("OOF1103", info_path, "interval rows and pair count", describe(info), "Gantt dynamic section is truncated");
    std::set<std::uint64_t> point_ids;
    std::set<std::uint64_t> series_ids;
    for (const auto& point : points) point_ids.insert(std::get<0>(point).value());
    for (const auto& item : series) series_ids.insert(std::get<0>(item).value());
    model::GanttChartPayload payload;
    for (const auto& [item_id, value, text] : series) {
        payload.series.push_back({item_id, value, text, std::nullopt});
    }
    for (const auto& [item_id, value, text] : points) {
        payload.points.push_back({item_id, value, text, std::nullopt});
    }
    std::map<std::uint64_t, std::size_t> pair_ordinals;
    for (std::size_t i = 0; i < n; ++i) {
        const auto cursor = interval_cursor + 2 * i;
        const auto pair = integer_atom<std::uint64_t>(info.items[cursor], child_path(info_path, cursor));
        const auto point = pair >> 32;
        const auto item_series = pair & 0xffffffffu;
        if (!point_ids.contains(point) || !series_ids.contains(item_series))
            fail("OOF1114", child_path(info_path, cursor), "pair key references known Point and Series IDs", std::to_string(pair), "Gantt interval refers to an unknown dimension");
        const LV& interval = info.items[cursor + 1];
        const auto interval_path = child_path(info_path, cursor + 1);
        require_arity(interval, 9, interval_path);
        require_raw_constant(interval.items[0], "5", child_path(interval_path, 0));
        if (integer_atom<std::uint64_t>(interval.items[1], child_path(interval_path, 1)) != pair)
            fail("OOF1114", child_path(interval_path, 1), std::to_string(pair), describe(interval.items[1]), "Gantt interval pair key does not match its record");
        const std::size_t expected_ordinal = ++pair_ordinals[pair];
        if (integer_atom<std::size_t>(interval.items[7], child_path(interval_path, 7)) != expected_ordinal)
            fail("OOF1114", child_path(interval_path, 7), std::to_string(expected_ordinal), describe(interval.items[7]), "Gantt interval ordinal is inconsistent");
        payload.intervals.push_back({model::ObjectId{point}, model::ObjectId{item_series},
            model::DateValue{value_codec::date_from_platform(raw_atom(interval.items[2], child_path(interval_path, 2)))},
            model::DateValue{value_codec::date_from_platform(raw_atom(interval.items[3], child_path(interval_path, 3)))},
            decoded_single_language_text(interval.items[4], child_path(interval_path, 4))});
    }
    const std::size_t pair_count_cursor = interval_cursor + 2 * n;
    const auto pair_count = integer_atom<std::uint32_t>(info.items[pair_count_cursor], child_path(info_path, pair_count_cursor));
    std::set<std::uint64_t> actual_pairs;
    for (const auto& [pair, ordinal] : pair_ordinals) { static_cast<void>(ordinal); actual_pairs.insert(pair); }
    if (pair_count != actual_pairs.size())
        fail("OOF1114", child_path(info_path, pair_count_cursor), std::to_string(actual_pairs.size()), std::to_string(pair_count), "Gantt pair-value count is inconsistent");

    if (actual_pairs.size() > (std::numeric_limits<std::size_t>::max() - 33 - 2 * n) / 2)
        fail("OOF1102", child_path(info_path, pair_count_cursor), "safe pair count", std::to_string(actual_pairs.size()), "Gantt pair count overflows its record layout");
    const std::size_t shift = 2 * n + 2 * actual_pairs.size();
    if (info.items.size() != 33 + shift)
        fail("OOF1102", info_path, "33 + 2*N + 2*U fields", std::to_string(info.items.size()), "Gantt dynamic info cursor differs from the supported profile");
    bool auto_full = bool_atom(info.items[11 + shift], child_path(info_path, 11 + shift));
    model::ControlNode control{model::ObjectId{id}, "", std::move(payload)};
    if (!auto_full) control.properties().set_explicit(model::PropertyId::from_name("AutoFullInterval"), false);
    if (!auto_full) {
        control.properties().set_explicit(model::PropertyId::from_name("FullIntervalBegin"),
            model::DateValue{value_codec::date_from_platform(raw_atom(info.items[12 + shift], child_path(info_path, 12 + shift)))});
        control.properties().set_explicit(model::PropertyId::from_name("FullIntervalEnd"),
            model::DateValue{value_codec::date_from_platform(raw_atom(info.items[13 + shift], child_path(info_path, 13 + shift)))});
    }
    const LV expected = encode_gantt_chart_info(control);
    require_gantt_dimension_table_match(info.items[2], expected.items[2], child_path(info_path, 2));
    require_gantt_dimension_table_match(info.items[3], expected.items[3], child_path(info_path, 3));
    LV normalized = info;
    normalized.items[2] = expected.items[2];
    normalized.items[3] = expected.items[3];
    LV expected_with_platform_layout = expected;
    normalize_gantt_runtime_layout_values(info, expected_with_platform_layout, info_path);
    if (list_stream::dump_compact(expected_with_platform_layout) != list_stream::dump_compact(normalized))
        fail("OOF1114", info_path, "canonical Gantt info generated from named data", "unsupported private settings or table fields", "Gantt info differs from the proven typed storage profile");

    const LV& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) fail("OOF1115", child_path(metadata_path, 1), "non-empty Gantt name", "empty", "Control name is required");
    require_exact(metadata, list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")} ), metadata_path, "Gantt metadata differs from the supported profile");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5), "Gantt storage children are unsupported");
    control.name = name;
    const auto geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    control.position = geometry.position;
    return {std::move(control), std::nullopt, geometry.incoming, std::nullopt, {}};
}

DecodedControl decode_command_bar(const LV& record, std::string_view path, const GeometryContext& context, model::ObjectId form_id) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::command_bar);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        fail("OOF1105", child_path(path, 1), "positive int64 CommandBar ID", std::to_string(raw_id), "CommandBar ID is invalid");
    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 2, info_path);
    require_raw_constant(info.items[0], "2", child_path(info_path, 0));
    const auto properties_path = child_path(info_path, 1);
    const auto& properties = info.items[1];
    require_arity(properties, 14, properties_path);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty", "Control name is required");
    const auto& base = properties.items[0];
    const auto base_path = child_path(properties_path, 0);
    require_arity(base, 21, base_path);
    const bool enabled = bool_atom(base.items[1], child_path(base_path, 1));
    const auto tool_tip = decoded_single_language_text(base.items[12], child_path(base_path, 12));
    const auto border_color = decode_button_color(base.items[6], child_path(base_path, 6));
    const auto button_text_color = decode_button_color(base.items[10], child_path(base_path, 10));
    const auto back_color = decode_button_color(base.items[2], child_path(base_path, 2));
    const bool transparent = bool_atom(base.items[5], child_path(base_path, 5));
    const auto button_back_color = decode_button_color(base.items[9], child_path(base_path, 9));
    const bool auto_fill = bool_atom(properties.items[3], child_path(properties_path, 3));
    const auto orientation = integer_atom<std::int32_t>(properties.items[2], child_path(properties_path, 2));
    const auto buttons_alignment = integer_atom<std::int32_t>(properties.items[4], child_path(properties_path, 4));
    if (orientation < 0 || orientation > 2)
        fail("OOF1114", child_path(properties_path, 2), "Orientation code 0..2", std::to_string(orientation), "Unsupported Orientation");
    if (buttons_alignment < 0 || buttons_alignment > 2)
        fail("OOF1114", child_path(properties_path, 4), "ButtonsAlignment code 0..2", std::to_string(buttons_alignment), "Unsupported ButtonsAlignment");
    const auto border = decode_control_border(base.items[11], child_path(base_path, 11));
    const auto runtime_state = raw_atom(base.items[17], child_path(base_path, 17));
    if (runtime_state != "1" && runtime_state != "2")
        fail("OOF1114", child_path(base_path, 17), "observed CommandBar state 1 or 2", runtime_state,
            "CommandBar base record contains an unsupported state");
    auto normalized_base = base;
    // Платформа при загрузке переводит наблюденное состояние создания 1 в 2.
    normalized_base.items[17] = raw("2");
    normalized_base.items[12] = encoded_localized(tool_tip);
    normalized_base.items[11] = encode_control_border(border, child_path(base_path, 11));
    require_raw_constant(properties.items[8], command_bar_root_marker, child_path(properties_path, 8));
    const auto root_group_id = integer_atom<std::uint64_t>(properties.items[9], child_path(properties_path, 9));
    const bool secondary = bool_atom(properties.items[5], child_path(properties_path, 5));
    const auto default_owner = uuid_atom(properties.items[10], child_path(properties_path, 10)).canonical;
    const auto default_id = integer_atom<std::uint64_t>(properties.items[11], child_path(properties_path, 11));
    if ((default_id == 0) != (default_owner == "9d0a2e40-b978-11d4-84b6-008048da06df") ||
        (default_id != 0 && (secondary || default_owner == null_uuid)))
        fail("OOF1122", properties_path, "consistent default Action reference on primary CommandBar", "inconsistent reference", "Unsupported DefaultButton context");
    auto menu = decode_button_menu(properties.items[7], child_path(properties_path, 7), name,
        command_bar_root_marker, root_group_id, default_id);

    model::Form empty_form;
    empty_form.id = model::ObjectId{1};
    empty_form.name = "CommandBarDefault";
    model::OrdinaryFormDocument empty_document(std::move(empty_form));
    auto normalized = properties;
    normalized.items[0] = std::move(normalized_base);
    // Native forms keep a separate root collection identity. After resolving
    // the actual marker/ID pair, rebuild that identity from the named owner.
    normalized.items[7] = encode_button_menu({}, empty_document, raw_id, command_bar_root_marker, raw_id);
    normalized.items[9] = raw(std::to_string(raw_id));
    // Designer relocates the selected action source UUID on load. The public model owns
    // the named action selection, not the transient source identity.
    normalized.items[10] = raw("9d0a2e40-b978-11d4-84b6-008048da06df");
    normalized.items[11] = raw("0");
    const auto expected = canonical_command_bar_properties(enabled, tool_tip, {}, empty_document, raw_id, secondary,
        &border_color, &button_text_color, &back_color, &border, auto_fill, transparent, orientation, buttons_alignment, &button_back_color);
    const auto& expected_base = expected.items[0];
    const auto base_mismatch = std::mismatch(normalized.items[0].items.begin(), normalized.items[0].items.end(),
        expected_base.items.begin(), expected_base.items.end(), [](const auto& left, const auto& right) {
            return list_stream::dump_compact(left) == list_stream::dump_compact(right);
        });
    if (base_mismatch.first != normalized.items[0].items.end()) {
        const auto index = static_cast<std::size_t>(base_mismatch.first - normalized.items[0].items.begin());
        fail("OOF1114", child_path(child_path(properties_path, 0), index), "observed canonical CommandBar base default",
            "changed base default", "CommandBar differs from the observed canonical default profile");
    }
    const auto default_mismatch = std::mismatch(normalized.items.begin(), normalized.items.end(),
        expected.items.begin(), expected.items.end(), [](const auto& left, const auto& right) {
            return list_stream::dump_compact(left) == list_stream::dump_compact(right);
        });
    if (default_mismatch.first != normalized.items.end()) {
        const auto index = static_cast<std::size_t>(default_mismatch.first - normalized.items.begin());
        fail("OOF1114", child_path(properties_path, index), "observed canonical CommandBar default profile",
            "changed default slot", "CommandBar differs from the observed canonical default profile");
    }

    const auto geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto action_source = integer_atom<std::uint32_t>(metadata.items[2], child_path(metadata_path, 2));
    require_exact(metadata, list({raw("14"), string_value(name), raw(std::to_string(action_source)), raw("0"), raw("0"), raw("0")}),
        metadata_path, "CommandBar metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5), "CommandBar cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::CommandBarPayload{}};
    if (action_source == 0)
        control.extension_properties.set_explicit(model::PropertyId::from_name("ActionSource"), model::FormRef{form_id});
    else if (action_source != std::numeric_limits<std::uint32_t>::max())
        control.extension_properties.set_explicit(model::PropertyId::from_name("ActionSource"), model::ControlRef{model::ObjectId{action_source}});
    if (auto_fill) control.properties().set_explicit(model::PropertyId::from_name("AutoFill"), true);
    if (transparent) control.properties().set_explicit(model::PropertyId::from_name("Transparent"), true);
    if (button_back_color != model::ColorValue{})
        control.properties().set_explicit(model::PropertyId::from_name("ButtonBackColor"), button_back_color);
    if (orientation != 2) {
        constexpr std::array<std::string_view, 3> members{"Vertical", "Horizontal", "Auto"};
        control.properties().set_explicit(model::PropertyId::from_name("Orientation"),
            model::EnumerationValue{"Orientation", std::string(members[orientation])});
    }
    if (buttons_alignment != 0) {
        constexpr std::array<std::string_view, 3> members{"Left", "Center", "Right"};
        control.properties().set_explicit(model::PropertyId::from_name("ButtonsAlignment"),
            model::EnumerationValue{"CommandBarButtonAlignment", std::string(members[buttons_alignment])});
    }
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    if (!secondary) control.properties().set_explicit(model::PropertyId::from_name("Secondary"), false);
    if (border_color != model::ColorValue{}) control.properties().set_explicit(model::PropertyId::from_name("BorderColor"), border_color);
    if (button_text_color != button_color_default("ButtonTextColor"))
        control.properties().set_explicit(model::PropertyId::from_name("ButtonTextColor"), button_text_color);
    if (back_color != model::ColorValue{}) control.properties().set_explicit(model::PropertyId::from_name("BackColor"), back_color);
    if (border != model::BorderValue{}) control.properties().set_explicit(model::PropertyId::from_name("Border"), border);
    std::get<model::CommandBarPayload>(control.payload).buttons = std::move(menu.entries);
    control.position = geometry.position;
    return {std::move(control), std::nullopt, geometry.incoming, std::nullopt, std::move(menu.assets)};
}


DecodedControl decode_usual_group(const LV& record, std::string_view path, const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::usual_group);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 UsualGroup ID", std::to_string(raw_id),
            "UsualGroup ID is invalid");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 2, info_path);
    require_raw_constant(info.items[0], "0", child_path(info_path, 0));
    const auto& properties = info.items[1];
    const auto properties_path = child_path(info_path, 1);
    require_arity(properties, 5, properties_path);
    const auto& base = properties.items[0];
    const auto base_path = child_path(properties_path, 0);
    require_arity(base, 21, base_path);
    const bool enabled = bool_atom(base.items[1], child_path(base_path, 1));
    const std::string tool_tip = decoded_single_language_text(base.items[12], child_path(base_path, 12));
    const std::string caption = decoded_single_language_text(properties.items[2], child_path(properties_path, 2));
    auto normalized = properties;
    auto normalized_base = base;
    normalized_base.items[12] = encoded_localized(tool_tip);
    normalized.items[0] = std::move(normalized_base);
    normalized.items[2] = encoded_localized(caption);
    require_exact(normalized, canonical_usual_group_properties(enabled, caption, tool_tip).items[1],
        properties_path, "UsualGroup contains an unsupported property or event variation");

    auto decoded_geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty",
        "Control name is required");
    require_exact(metadata, list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "UsualGroup metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "UsualGroup children are not supported by the executable storage profile");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::UsualGroupPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (!caption.empty()) control.properties().set_explicit(model::PropertyId::from_name("Caption"), caption);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

DecodedControl decode_button(const LV& record, std::string_view path, const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::button);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const std::uint64_t raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0) {
        fail("OOF1105", child_path(path, 1), "positive object ID", "0", "Control ID is invalid");
    }

    const auto& info = record.items[2];
    const std::string info_path = child_path(path, 2);
    require_arity(info, 3, info_path);
    require_raw_constant(info.items[0], "1", child_path(info_path, 0));
    const auto& properties = info.items[1];
    const std::string properties_path = child_path(info_path, 1);
    require_list(properties, properties_path);
    if (properties.items.size() < 12) {
        fail("OOF1102", properties_path, "Button properties including MenuMode", describe(properties),
            "Button property record is too short");
    }
    const auto& base = properties.items[0];
    const std::string base_path = child_path(properties_path, 0);
    require_arity(base, 21, base_path);
    const bool enabled = bool_atom(base.items[1], child_path(base_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        base.items[12], child_path(base_path, 12));
    const auto border_color = decode_button_color(base.items[6], child_path(base_path, 6));
    const auto button_back_color = decode_button_color(base.items[9], child_path(base_path, 9));
    const auto button_text_color = decode_button_color(base.items[10], child_path(base_path, 10));
    const auto font = decode_control_font(base.items[4], child_path(base_path, 4));
    const std::string observed_state = raw_atom(base.items[17], child_path(base_path, 17));
    if (observed_state != "1" && observed_state != "2") {
        fail(
            "OOF1114",
            child_path(base_path, 17),
            "observed internal state 1 or 2",
            observed_state,
            "Button base record contains an unsupported property variation");
    }
    // Наблюдались 1 у нетронутой записи и 2 после изменения Button в Designer.
    // Это внутреннее состояние, его общая семантика не установлена.
    auto normalized_base = base;
    normalized_base.items[17] = raw("2");
    require_exact(
        normalized_base,
        canonical_button_base(enabled, tool_tip, &border_color, &button_text_color, &button_back_color, &font),
        base_path,
        "Button base record contains an unsupported property variation");
    const std::string caption = decoded_single_language_text(
        properties.items[2],
        child_path(properties_path, 2));
    const auto horizontal_align = integer_atom<std::int32_t>(
        properties.items[3], child_path(properties_path, 3));
    if (horizontal_align < 0 || horizontal_align > 2) {
        fail("OOF1114", child_path(properties_path, 3), "HorizontalAlign storage value 0, 1, or 2",
            std::to_string(horizontal_align), "Button.HorizontalAlign storage value is unsupported");
    }
    const auto vertical_align = integer_atom<std::int32_t>(
        properties.items[4], child_path(properties_path, 4));
    if (vertical_align < 0 || vertical_align > 2) {
        fail("OOF1114", child_path(properties_path, 4), "VerticalAlign storage value 0, 1, or 2",
            std::to_string(vertical_align), "Button.VerticalAlign storage value is unsupported");
    }
    const auto picture_location = integer_atom<std::int32_t>(
        properties.items[6], child_path(properties_path, 6));
    if (picture_location < 0 || picture_location > 1) {
        fail("OOF1114", child_path(properties_path, 6), "PictureLocation storage value 0 or 1",
            std::to_string(picture_location), "Button.PictureLocation storage value is unsupported");
    }
    const auto picture_size = integer_atom<std::int32_t>(
        properties.items[7], child_path(properties_path, 7));
    if (picture_size < 0 || picture_size > 7 || picture_size == 5 || picture_size == 6) {
        fail("OOF1114", child_path(properties_path, 7), "PictureSize storage value 0, 1, 2, 3, 4, or 7",
            std::to_string(picture_size), "Button.PictureSize storage value is unsupported");
    }
    const auto menu_mode = integer_atom<std::int32_t>(
        properties.items[11], child_path(properties_path, 11));
    if (menu_mode < 0 || menu_mode > 2) {
        fail("OOF1114", child_path(properties_path, 11), "MenuMode storage value 0, 1, or 2",
            std::to_string(menu_mode), "Button.MenuMode storage value is unsupported");
    }
    require_arity(properties, menu_mode == 0 ? 16 : 17, properties_path);
    const bool multi_line = bool_atom(
        properties.items[10], child_path(properties_path, 10));
    const auto shortcut = decode_button_shortcut(
        properties.items[9], child_path(properties_path, 9));
    const auto picture = decode_button_picture(properties.items[8], child_path(properties_path, 8));
    auto normalized_properties = properties;
    normalized_properties.items[0] = std::move(normalized_base);
    normalized_properties.items[8] = canonical_button_picture();
    if (menu_mode != 0) normalized_properties.items[12] = parse_constant(
        "{5,53232d71-06b1-4ec1-a94d-77fafadef407,0,1,0,1,"
        "{5,31946946-0a9b-40a2-95cf-82f200778341,0,0,0,{-1,0,{0}}}}");
    const auto control_state = integer_atom<unsigned>(normalized_properties.items.back(), properties_path);
    if (control_state != 1 && control_state != 2) fail("OOF1114", properties_path,
        "known internal control state", std::to_string(control_state), "Button control state is unsupported");
    normalized_properties.items.back() = raw("1");
    require_exact(
        normalized_properties,
        canonical_button_properties(enabled, caption, horizontal_align, vertical_align, picture_location,
            picture_size, menu_mode, multi_line, tool_tip, border_color, button_text_color,
            button_back_color, font, shortcut),
        properties_path,
        "Button payload contains an unsupported property variation");

    const auto click_handler = decode_button_event(info.items[2], child_path(info_path, 2));

    const auto geometry_path = child_path(path, 3);
    auto decoded_geometry = decode_geometry(record.items[3], geometry_path, context);
    const auto& metadata = record.items[4];
    const std::string metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty", "Control name is required");
    }
    std::optional<model::PictureAsset> picture_asset;
    if (picture && !picture->standard_name) {
        picture_asset = model::PictureAsset{
            {}, "Items/" + name + "/Picture." + std::string(picture_format_extension(picture->format)),
            picture->format, picture->bytes, picture->transparent};
    }
    require_exact(
        metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path,
        "Button metadata record is unsupported");
    require_exact(
        record.items[5],
        list({raw("0")}),
        child_path(path, 5),
        "Button cannot contain storage children");

    model::ControlNode control{
        model::ObjectId{raw_id},
        name,
        model::ButtonPayload{},
    };
    if (!caption.empty()) {
        control.properties().set_explicit(model::PropertyId::from_name("Caption"), caption);
    }
    if (!tool_tip.empty()) {
        control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    }
    if (border_color != button_color_default("BorderColor")) {
        control.properties().set_explicit(model::PropertyId::from_name("BorderColor"), border_color);
    }
    if (button_text_color != button_color_default("ButtonTextColor")) {
        control.properties().set_explicit(model::PropertyId::from_name("ButtonTextColor"), button_text_color);
    }
    if (button_back_color != button_color_default("ButtonBackColor")) {
        control.properties().set_explicit(model::PropertyId::from_name("ButtonBackColor"), button_back_color);
    }
    if (font != model::FontValue{}) {
        control.properties().set_explicit(model::PropertyId::from_name("Font"), font);
    }
    if (shortcut != model::ShortcutValue{}) {
        control.properties().set_explicit(model::PropertyId::from_name("Shortcut"), shortcut);
    }
    if (horizontal_align != 1) {
        static constexpr std::string_view members[] = {"Left", "Center", "Right"};
        control.properties().set_explicit(
            model::PropertyId::from_name("HorizontalAlign"),
            model::EnumerationValue{"HorizontalAlign", std::string(members[horizontal_align])});
    }
    if (vertical_align != 1) {
        static constexpr std::string_view members[] = {"Top", "Center", "Bottom"};
        control.properties().set_explicit(
            model::PropertyId::from_name("VerticalAlign"),
            model::EnumerationValue{"VerticalAlign", std::string(members[vertical_align])});
    }
    if (picture_location != 0) {
        control.properties().set_explicit(
            model::PropertyId::from_name("PictureLocation"),
            model::EnumerationValue{"PictureLocation", "Right"});
    }
    if (picture_size != 0) {
        std::string_view member;
        switch (picture_size) {
            case 1: member = "Stretch"; break;
            case 2: member = "Proportionally"; break;
            case 3: member = "Tile"; break;
            case 4: member = "AutoSize"; break;
            case 7: member = "ByFontSize"; break;
            default: break;
        }
        control.properties().set_explicit(
            model::PropertyId::from_name("PictureSize"),
            model::EnumerationValue{"PictureSize", std::string(member)});
    }
    if (menu_mode != 0) {
        static constexpr std::string_view members[] = {"DontUse", "Use", "UseExtra"};
        control.properties().set_explicit(
            model::PropertyId::from_name("MenuMode"),
            model::EnumerationValue{"MenuMode", std::string(members[menu_mode])});
    }
    if (multi_line) {
        control.properties().set_explicit(model::PropertyId::from_name("MultiLine"), true);
    }
    if (!enabled) {
        control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    }
    control.position = std::move(decoded_geometry.position);
    if (picture && picture->standard_name) {
        control.properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
                model::QualifiedName{*picture->standard_name}});
    }
    DecodedMenu menu;
    if (menu_mode != 0) {
        menu = decode_button_menu(properties.items[12], child_path(properties_path, 12), name);
        std::get<model::ButtonPayload>(control.payload).buttons = std::move(menu.entries);
    }
    return {std::move(control), click_handler, std::move(decoded_geometry.incoming), std::move(picture_asset), std::move(menu.assets)};
}

DecodedControl decode_picture_decoration(
    const LV& record,
    std::string_view path,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::picture_decoration);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const std::uint64_t raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 PictureDecoration ID",
            std::to_string(raw_id), "PictureDecoration ID is invalid");
    }

    const auto& info = record.items[2];
    const std::string info_path = child_path(path, 2);
    require_arity(info, 3, info_path);
    require_raw_constant(info.items[0], "1", child_path(info_path, 0));
    const auto& picture_properties = info.items[1];
    const std::string properties_path = child_path(info_path, 1);
    require_arity(picture_properties, 15, properties_path);
    const auto& base_properties = picture_properties.items[0];
    const std::string base_path = child_path(properties_path, 0);
    require_arity(base_properties, 21, base_path);
    const bool enabled = bool_atom(base_properties.items[1], child_path(base_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        base_properties.items[12], child_path(base_path, 12));
    const std::string picture_slot_path = child_path(properties_path, 4);
    const auto& picture_slot = picture_properties.items[4];
    require_arity(picture_slot, 11, picture_slot_path);
    const auto picture = decode_button_picture(
        picture_slot.items[2], child_path(picture_slot_path, 2));
    auto normalized_properties = picture_properties;
    auto normalized_base = base_properties;
    normalized_base.items[12] = encoded_localized(tool_tip);
    normalized_properties.items[0] = std::move(normalized_base);
    normalized_properties.items[4].items[2] = canonical_button_picture();
    require_exact(
        normalized_properties,
        canonical_picture_properties(enabled, tool_tip),
        properties_path,
        "PictureDecoration base record differs from the supported default profile");
    require_exact(info.items[2], list({raw("0")}), child_path(info_path, 2),
        "PictureDecoration events are unsupported");

    auto decoded_geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    const std::string metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty",
            "Control name is required");
    }
    require_exact(
        metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path,
        "PictureDecoration metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "PictureDecoration cannot contain storage children");

    std::optional<model::PictureAsset> picture_asset;
    if (picture && !picture->standard_name) {
        picture_asset = model::PictureAsset{
            {}, "Items/" + name + "/Picture." + std::string(picture_format_extension(picture->format)),
            picture->format, picture->bytes, picture->transparent};
    }
    model::ControlNode control{
        model::ObjectId{raw_id},
        name,
        model::PictureDecorationPayload{},
    };
    if (!enabled) {
        control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    }
    if (!tool_tip.empty()) {
        control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    }
    if (picture && picture->standard_name) {
        control.properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
                model::QualifiedName{*picture->standard_name}});
    }
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::move(picture_asset), {}};
}

DecodedControl decode_splitter(
    const LV& record,
    std::string_view path,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::splitter);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 Splitter ID", std::to_string(raw_id),
            "Splitter ID is invalid");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 2, info_path);
    require_raw_constant(info.items[0], "0", child_path(info_path, 0));
    const auto& properties = info.items[1];
    const auto properties_path = child_path(info_path, 1);
    require_arity(properties, 4, properties_path);
    const auto& base = properties.items[0];
    const auto base_path = child_path(properties_path, 0);
    require_arity(base, 21, base_path);
    const bool enabled = bool_atom(base.items[1], child_path(base_path, 1));
    const std::string tool_tip = decoded_single_language_text(base.items[12], child_path(base_path, 12));
    const auto back_color = decode_button_color(base.items[2], child_path(base_path, 2));
    const auto border_color = decode_button_color(base.items[6], child_path(base_path, 6));
    if ((back_color.kind != model::ColorKind::automatic && back_color.kind != model::ColorKind::absolute) ||
        (border_color.kind != model::ColorKind::automatic && border_color.kind != model::ColorKind::absolute)) {
        fail("OOF1114", properties_path, "automatic or observed absolute Splitter colors", describe(properties),
            "Splitter style colors are outside the supported storage profile");
    }
    const auto orientation_storage = integer_atom<std::int32_t>(properties.items[2], child_path(properties_path, 2));
    std::string orientation_member;
    if (orientation_storage == 2) orientation_member = "Auto";
    else if (orientation_storage == 0) orientation_member = "Vertical";
    else if (orientation_storage == 1) orientation_member = "Horizontal";
    else fail("OOF1114", child_path(properties_path, 2), "Orientation Auto(2), Vertical(0), or Horizontal(1)",
        std::to_string(orientation_storage), "Splitter orientation value is unsupported");

    require_exact(properties,
        canonical_splitter_properties(enabled, orientation_storage, tool_tip, border_color, back_color),
        properties_path, "Splitter properties differ from the supported exact record");
    const auto decoded_geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty",
        "Control name is required");
    require_exact(metadata, list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "Splitter metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "Splitter cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::SplitterPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (orientation_member != "Auto") control.properties().set_explicit(
        model::PropertyId::from_name("Orientation"), model::EnumerationValue{"Orientation", orientation_member});
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    if (back_color.kind != model::ColorKind::automatic) control.properties().set_explicit(
        model::PropertyId::from_name("BackColor"), back_color);
    if (border_color.kind != model::ColorKind::automatic) control.properties().set_explicit(
        model::PropertyId::from_name("BorderColor"), border_color);
    control.position = decoded_geometry.position;
    return {std::move(control), std::nullopt, decoded_geometry.incoming, std::nullopt, {}};
}

DecodedControl decode_radio_button(
    const LV& record,
    std::string_view path,
    const AttributeRecord* linked_attribute,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::radio_button);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 RadioButton ID", std::to_string(raw_id),
            "RadioButton ID is invalid");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 6, info_path);
    require_raw_constant(info.items[0], "4", child_path(info_path, 0));
    const auto value_type = type_domain(info.items[1], child_path(info_path, 1));
    if (!value_type.entries.empty() && !is_single_numeric_type_domain(value_type)) {
        fail("OOF1122", child_path(info_path, 1), "empty or single Numeric ValueType", describe(info.items[1]),
            "RadioButton ValueType is outside the supported numeric profile");
    }
    if (linked_attribute != nullptr && value_type != linked_attribute->type) {
        fail("OOF1122", child_path(info_path, 1), "ValueType matching linked Attribute", describe(info.items[1]),
            "RadioButton ValueType differs from its linked Attribute type");
    }
    if (linked_attribute == nullptr && !value_type.entries.empty()) {
        fail("OOF1122", child_path(info_path, 1), "empty ValueType without DataPath", describe(info.items[1]),
            "RadioButton numeric ValueType requires a linked Attribute");
    }
    const auto& control_info = info.items[2];
    const auto control_info_path = child_path(info_path, 2);
    require_arity(control_info, 6, control_info_path);
    const auto& properties = control_info.items[0];
    const auto properties_path = child_path(control_info_path, 0);
    require_arity(properties, 9, properties_path);
    const auto& base_properties = properties.items[0];
    const auto base_path = child_path(properties_path, 0);
    require_arity(base_properties, 21, base_path);
    const bool enabled = bool_atom(base_properties.items[1], child_path(base_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        base_properties.items[12], child_path(base_path, 12));
    const std::string caption = decoded_single_language_text(
        properties.items[2], child_path(properties_path, 2));
    const auto selection_value = decoded_radio_selection_value(info.items[4], child_path(info_path, 4));
    const bool inherited_group_selection = selection_value && value_type.entries.empty() && linked_attribute == nullptr;
    if (selection_value && !inherited_group_selection && (!is_single_numeric_type_domain(value_type) ||
        !decimal_fits_numeric_qualifiers(selection_value->canonical, value_type.entries.front().numeric))) {
        fail("OOF1122", child_path(info_path, 4), "SelectionValue within numeric ValueType qualifiers",
            describe(info.items[4]), "RadioButton SelectionValue does not fit its numeric ValueType");
    }
    auto normalized_info = info;
    auto normalized_properties = properties;
    auto normalized_base = base_properties;
    normalized_base.items[12] = encoded_localized(tool_tip);
    normalized_properties.items[0] = std::move(normalized_base);
    normalized_properties.items[2] = encoded_localized(caption);
    normalized_info.items[2].items[0] = std::move(normalized_properties);
    require_exact(normalized_info, canonical_radio_button_info(value_type, selection_value, enabled, caption, tool_tip), info_path,
        "RadioButton contains a property, binding, event, or storage variation outside the observed basic profile");

    auto decoded_geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const bool first_in_group = bool_atom(metadata.items[5], child_path(metadata_path, 5));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty",
            "Control name is required");
    }
    require_exact(metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw(first_in_group ? "1" : "0")}),
        metadata_path, "RadioButton metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "RadioButton cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::RadioButtonPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (!caption.empty()) control.properties().set_explicit(model::PropertyId::from_name("Caption"), caption);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    if (selection_value) control.properties().set_explicit(model::PropertyId::from_name("SelectionValue"), *selection_value);
    if (first_in_group) control.extension_properties.set_explicit(model::PropertyId::from_name("FirstInGroup"), true);
    if (!value_type.entries.empty()) control.extension_properties.set_explicit(model::PropertyId::from_name("ValueType"), value_type);
    if (linked_attribute != nullptr) control.data_path = model::DataPath{model::AttributeRef{
        model::ObjectId{static_cast<std::uint64_t>(linked_attribute->id.object_id)}}, {}};
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

void validate_radio_group(
    const model::OrdinaryFormDocument& document,
    const std::vector<const model::ControlNode*>& group,
    std::string_view path) {
    if (group.empty()) return;
    const auto first_entry = group.front()->extension_properties.find(model::PropertyId::from_name("FirstInGroup"));
    const bool starts_group = first_entry != nullptr && std::holds_alternative<bool>(first_entry->value) &&
        std::get<bool>(first_entry->value);
    if (!starts_group) {
        bool unsupported_extension = false;
        group.front()->extension_properties.for_each_explicit([&](const model::PropertyEntry& entry) {
            if (entry.id == model::PropertyId::from_name("FirstInGroup")) {
                const auto* value = std::get_if<bool>(&entry.value);
                if (value == nullptr || *value) unsupported_extension = true;
            } else if (entry.id == model::PropertyId::from_name("ValueType")) {
                const auto* value = std::get_if<model::TypeDomainPatternValue>(&entry.value);
                if (value == nullptr || !value->entries.empty()) unsupported_extension = true;
            } else {
                unsupported_extension = true;
            }
        });
        if (group.size() != 1 || group.front()->data_path || unsupported_extension ||
            group.front()->properties().find(model::PropertyId::from_name("SelectionValue"))) {
            fail("OOF1122", std::string(path), "unbound standalone RadioButton or supported group member",
                group.front()->name, "RadioButton storage state is outside the observed standalone profile");
        }
        return;
    }
    const auto& head = *group[0];
    const auto* head_type_entry = head.extension_properties.find(model::PropertyId::from_name("ValueType"));
    const auto* head_selection_entry = head.properties().find(model::PropertyId::from_name("SelectionValue"));
    const model::TypeDomainPatternValue* head_type = head_type_entry == nullptr ? nullptr :
        std::get_if<model::TypeDomainPatternValue>(&head_type_entry->value);
    const model::DecimalValue* head_selection = head_selection_entry == nullptr ? nullptr :
        std::get_if<model::DecimalValue>(&head_selection_entry->value);
    const bool has_numeric_effective_type = head.data_path && head_type != nullptr &&
        is_single_numeric_type_domain(*head_type);
    for (std::size_t index = 1; index < group.size(); ++index) {
        const auto& member = *group[index];
        const auto* member_type_entry = member.extension_properties.find(model::PropertyId::from_name("ValueType"));
        const auto* member_selection_entry = member.properties().find(model::PropertyId::from_name("SelectionValue"));
        const auto* member_type = member_type_entry == nullptr ? nullptr :
            std::get_if<model::TypeDomainPatternValue>(&member_type_entry->value);
        const auto* member_selection = member_selection_entry == nullptr ? nullptr :
            std::get_if<model::DecimalValue>(&member_selection_entry->value);
        const bool value_fits_effective_type = member_selection != nullptr &&
            (has_numeric_effective_type
                ? decimal_fits_numeric_qualifiers(member_selection->canonical, head_type->entries.front().numeric)
                : value_codec::canonical_decimal(member_selection->canonical) == "0");
        if (member.data_path || (member_type != nullptr && !member_type->entries.empty()) || !value_fits_effective_type) {
            fail("OOF1122", std::string(path), "group member with Pattern ValueType and SelectionValue fitting the group head type",
                member.name, "RadioButton group member differs from the observed group value type");
        }
    }
    if (head.data_path) {
        if (head_type == nullptr || !is_single_numeric_type_domain(*head_type) || head_selection == nullptr) {
            fail("OOF1122", std::string(path), "numeric head ValueType, DataPath, and SelectionValue",
                head.name, "RadioButton group head is missing its numeric binding profile");
        }
        const auto* attribute = document.find_attribute(head.data_path->attribute.id());
        if (attribute == nullptr || attribute->type != *head_type ||
            !decimal_fits_numeric_qualifiers(head_selection->canonical, head_type->entries.front().numeric)) {
            fail("OOF1122", std::string(path), "head numeric Attribute and matching SelectionValue",
                head.name, "RadioButton group head numeric values do not agree");
        }
        return;
    }
    if ((head_type != nullptr && !head_type->entries.empty()) || head_selection != nullptr) {
        fail("OOF1122", std::string(path), "unbound group head with Pattern ValueType and Undefined SelectionValue",
            head.name, "Unbound RadioButton group head differs from the observed profile");
    }
}

void validate_radio_groups(const model::OrdinaryFormDocument& document) {
    std::vector<const model::ControlNode*> group;
    const auto flush_group = [&](std::string_view path) {
        validate_radio_group(document, group, path);
        group.clear();
    };
    std::function<void(const std::vector<model::ChildItemRef>&, std::string_view)> visit;
    visit = [&](const std::vector<model::ChildItemRef>& children, std::string_view path) {
        std::size_t ordinal = 0;
        for (const auto& child : children) {
            const auto child_path_text = std::string(path) + "/" + std::to_string(ordinal++);
            if (const auto* control_ref = std::get_if<model::ControlRef>(&child)) {
                const auto* control = document.find_control(control_ref->id());
                if (control == nullptr) continue;
                if (control->kind() == model::ControlKind::radio_button) {
                    const auto* first = control->extension_properties.find(model::PropertyId::from_name("FirstInGroup"));
                    if (first != nullptr && std::holds_alternative<bool>(first->value) && std::get<bool>(first->value)) {
                        flush_group(child_path_text);
                        group.push_back(control);
                    } else if (group.empty()) {
                        validate_radio_group(document, std::vector<const model::ControlNode*>{control}, child_path_text);
                    } else {
                        group.push_back(control);
                    }
                    continue;
                }
                flush_group(child_path_text);
                visit(control->children, child_path_text);
            } else if (const auto* page_ref = std::get_if<model::PageRef>(&child)) {
                flush_group(child_path_text);
                const auto* page = document.find_page(page_ref->id());
                if (page != nullptr) visit(page->children, child_path_text);
            } else {
                flush_group(child_path_text);
            }
        }
        flush_group(path);
    };
    visit(document.form().children, "$/Form/ChildItems");
}

DecodedControl decode_html_document_field(
    const LV& record,
    std::string_view path,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::html_document_field);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 HTMLDocumentField ID", std::to_string(raw_id),
            "HTMLDocumentField ID is invalid");
    }

    const auto& data = record.items[2];
    const auto data_path = child_path(path, 2);
    require_arity(data, 7, data_path);
    const auto output = integer_atom<std::int32_t>(data.items[6], child_path(data_path, 6));
    if (output < 0 || output > 2) {
        fail("OOF1114", child_path(data_path, 6), "UseOutput storage value 0, 1, or 2",
            std::to_string(output), "HTMLDocumentField.Output storage value is unsupported");
    }
    auto normalized_data = data;
    normalized_data.items[6] = raw("0");
    require_exact(normalized_data, canonical_html_document_field_data(0), data_path,
        "HTMLDocumentField contains an unproven property or storage variation");

    auto decoded_geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty",
            "Control name is required");
    }
    require_exact(metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "HTMLDocumentField metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "HTMLDocumentField cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::HtmlDocumentFieldPayload{}};
    static constexpr std::array<std::string_view, 3> members{"Auto", "Enable", "Disable"};
    if (output != 0) {
        control.properties().set_explicit(
            model::PropertyId::from_name("Output"),
            model::EnumerationValue{"Output", std::string(members[static_cast<std::size_t>(output)])});
    }
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

DecodedControl decode_label(const LV& record, std::string_view path, const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::label_decoration);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const std::uint64_t raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0) {
        fail("OOF1105", child_path(path, 1), "positive object ID", "0", "Control ID is invalid");
    }

    const auto& info = record.items[2];
    const std::string info_path = child_path(path, 2);
    require_arity(info, 3, info_path);
    require_raw_constant(info.items[0], "3", child_path(info_path, 0));
    const auto& properties = info.items[1];
    const std::string properties_path = child_path(info_path, 1);
    require_arity(properties, 21, properties_path);
    const auto& base_properties = properties.items[0];
    const std::string base_properties_path = child_path(properties_path, 0);
    require_arity(base_properties, 21, base_properties_path);
    const bool enabled = bool_atom(base_properties.items[1], child_path(base_properties_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        base_properties.items[12], child_path(base_properties_path, 12));
    const std::string caption = decoded_single_language_text(
        properties.items[2], child_path(properties_path, 2));
    const std::int32_t horizontal_align = integer_atom<std::int32_t>(
        properties.items[3], child_path(properties_path, 3));
    if (horizontal_align != 0 && horizontal_align != 1 && horizontal_align != 2 && horizontal_align != 4) {
        fail("OOF1122", child_path(properties_path, 3),
            "LabelDecoration HorizontalAlign storage value 0 (Left), 1 (Center), 2 (Right), or 4 (Auto)",
            std::to_string(horizontal_align), "LabelDecoration.HorizontalAlign storage value is unsupported");
    }
    auto normalized_properties = properties;
    auto normalized_base_properties = base_properties;
    normalized_base_properties.items[12] = encoded_localized(tool_tip);
    normalized_properties.items[0] = std::move(normalized_base_properties);
    normalized_properties.items[2] = encoded_localized(caption);
    require_exact(
        normalized_properties,
        canonical_label_properties(caption, horizontal_align, enabled, tool_tip),
        properties_path,
        "LabelDecoration properties differ from the supported default profile");
    require_exact(info.items[2], list({raw("0")}), child_path(info_path, 2), "LabelDecoration events are unsupported");

    const auto geometry_path = child_path(path, 3);
    auto decoded_geometry = decode_geometry(record.items[3], geometry_path, context);
    const auto& metadata = record.items[4];
    const std::string metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty", "Control name is required");
    }
    require_exact(
        metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path,
        "LabelDecoration metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5), "LabelDecoration cannot contain storage children");

    model::ControlNode control{
        model::ObjectId{raw_id},
        name,
        model::LabelDecorationPayload{},
    };
    if (!caption.empty()) {
        control.properties().set_explicit(model::PropertyId::from_name("Caption"), caption);
    }
    if (!enabled) {
        control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    }
    if (!tool_tip.empty()) {
        control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    }
    control.properties().set_explicit(
        model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{
            "HorizontalAlign",
            horizontal_align == 4 ? "Auto" : horizontal_align == 2 ? "Right" :
                horizontal_align == 1 ? "Center" : "Left"});
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

static std::vector<ls::ListValue> canonical_chart_series_defaults(){return {
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("11837108")}),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("e5cabe59-d992-4d31-8086-3116931aff81"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("1"),ls::ListValue::list({ls::ListValue::string_atom("ru"),ls::ListValue::string_atom("Сводная")})}),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("1"),
ls::ListValue::list({ls::ListValue::string_atom("U")}),
ls::ListValue::list({ls::ListValue::string_atom("U")}),
ls::ListValue::raw_atom("0"),
};}
static std::vector<ls::ListValue> canonical_chart_middle_defaults(){return {
ls::ListValue::raw_atom("-1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("4"),
ls::ListValue::raw_atom("0"),
ls::ListValue::string_atom(", "),
ls::ListValue::raw_atom("4"),
ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-3")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("1"),ls::ListValue::list({ls::ListValue::string_atom("ru"),ls::ListValue::string_atom("Диаграмма1")})}),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-22")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-22")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-22")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-1")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-1")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-1")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("16777215")}),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-3")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-3")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-3")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),
ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),
ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("e5cabe59-d992-4d31-8086-3116931aff81"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("4"),
ls::ListValue::raw_atom("30"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("2"),
ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("169")}),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("180"),
ls::ListValue::raw_atom("5"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("4"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("11119017")}),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("1.520833333333333e-1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("8.479166666666666e-1"),
ls::ListValue::raw_atom("4.083333333333333e-1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("3.694444444444444e-1"),
ls::ListValue::raw_atom("8.479166666666666e-1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("9.611111111111111e-1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("3"),ls::ListValue::list({ls::ListValue::raw_atom("-22")}),ls::ListValue::raw_atom("3")}),
ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),
};}
static std::vector<ls::ListValue> canonical_chart_data_defaults(){return {
ls::ListValue::raw_atom("14"),
ls::ListValue::raw_atom("2"),
ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("95"),
ls::ListValue::raw_atom("1e-1"),
ls::ListValue::raw_atom("1e-1"),
ls::ListValue::raw_atom("3e-2"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("e5cabe59-d992-4d31-8086-3116931aff81"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("2"),
ls::ListValue::raw_atom("255"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("00000000-0000-0000-0000-000000000000"),
ls::ListValue::raw_atom("0"),
};}
static std::vector<ls::ListValue> canonical_chart_style_defaults(){return {
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("2"),
ls::ListValue::raw_atom("-2"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("10"),
ls::ListValue::raw_atom("1"),
ls::ListValue::raw_atom("20"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("2"),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0.5"),ls::ListValue::raw_atom("0.5"),ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("1"),ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("2"),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("2"),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0.5"),ls::ListValue::raw_atom("0.5"),ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("1"),ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("2"),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("2"),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0.5"),ls::ListValue::raw_atom("0.5"),ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("1"),ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("2"),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::raw_atom("0"),
};}
static std::vector<ls::ListValue> canonical_chart_render_defaults(){return {
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0.152083333333333333333333333"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0.847916666666666666666666667"),
ls::ListValue::raw_atom("0.408333333333333333333333333"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0.369444444444444444444444444"),
ls::ListValue::raw_atom("0.847916666666666666666666667"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0.961111111111111111111111111"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0.17"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0.83"),
ls::ListValue::raw_atom("0.08"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0.83"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0.92"),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("14"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("14"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("0"),
};}
static std::vector<ls::ListValue> canonical_chart_tail_defaults(){return {
ls::ListValue::raw_atom(""),
ls::ListValue::raw_atom("60"),
ls::ListValue::list({ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("2"),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("0.5"),ls::ListValue::raw_atom("0.5"),ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("1"),ls::ListValue::list({ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("48312c09-257f-4b29-b280-284dd89efc1e")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("2"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::list({ls::ListValue::raw_atom("8"),ls::ListValue::raw_atom("3"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("100")}),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("2"),ls::ListValue::list({ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0"),ls::ListValue::list({ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("1"),ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("0"),ls::ListValue::raw_atom("0")}),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::raw_atom("0"),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
ls::ListValue::list({ls::ListValue::raw_atom("4"),ls::ListValue::raw_atom("4"),ls::ListValue::list({ls::ListValue::raw_atom("0")}),ls::ListValue::raw_atom("4")}),
};}

std::uint32_t chart_color_rgb(const model::ColorValue& value, std::string_view path) {
    if (value.kind != model::ColorKind::absolute || value.alpha != 255 ||
        !std::holds_alternative<std::monostate>(value.style)) {
        fail("OOF1122", std::string(path), "automatic or absolute opaque RGB color", "unsupported ColorValue", "Chart color is outside the supported profile");
    }
    return (static_cast<std::uint32_t>(value.blue) << 16) |
        (static_cast<std::uint32_t>(value.green) << 8) | static_cast<std::uint32_t>(value.red);
}

LV encode_chart_color_property(const model::ColorValue& value, std::string_view path) {
    (void)chart_color_rgb(value, path);
    return encode_button_color(value, path);
}

model::ColorValue decode_chart_color_property(const LV& value, std::string_view path) {
    auto color = decode_button_color(value, path);
    if (color.kind != model::ColorKind::absolute) {
        fail("OOF1122", std::string(path), "automatic or absolute opaque RGB color", "unsupported ColorValue", "Chart color is outside the supported profile");
    }
    return color;
}

std::uint32_t chart_marker_value(const model::EnumerationValue& value, std::string_view path) {
    if (value.type_name != "ChartMarkerType") fail("OOF1122", std::string(path), "ChartMarkerType enum", value.type_name, "Chart Marker has the wrong enumeration type");
    if (value.member == "None") return 0;
    if (value.member == "Rect") return 1;
    if (value.member == "Circle") return 2;
    if (value.member == "Rhomb") return 3;
    if (value.member == "Auto") return 4;
    if (value.member == "Alternation") return 5;
    fail("OOF1122", std::string(path), "help-known Chart Marker member", value.member, "Chart Marker member is unsupported");
}

model::EnumerationValue decode_chart_marker(std::uint32_t value, std::string_view path) {
    constexpr std::array<std::string_view, 6> members{"None", "Rect", "Circle", "Rhomb", "Auto", "Alternation"};
    if (value >= members.size()) fail("OOF1115", std::string(path), "ChartMarkerType ordinal 0..5", std::to_string(value), "Chart Marker ordinal is unsupported");
    return {"ChartMarkerType", std::string(members[value])};
}

std::uint32_t chart_marker_cache(const model::EnumerationValue& value, std::string_view path) {
    const auto ordinal = chart_marker_value(value, path);
    return value.member == "Auto" ? 3u : ordinal;
}

std::size_t chart_info_item_count(std::size_t series_count, std::size_t point_count, std::string_view path) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (series_count > maximum / 13 || point_count > maximum / 12 ||
        (series_count != 0 && point_count > maximum / series_count)) {
        fail("OOF1102", std::string(path), "non-overflowing Chart dimensions", "oversized dimensions", "Chart dimensions overflow the native record size");
    }
    const std::size_t series_items = 13 * series_count;
    const std::size_t point_items = 12 * point_count;
    const std::size_t cells = series_count * point_count;
    if (cells > maximum / 4 || series_items > maximum - 222 ||
        point_items > maximum - 222 - series_items || 4 * cells > maximum - 222 - series_items - point_items) {
        fail("OOF1102", std::string(path), "non-overflowing Chart dimensions", "oversized dimensions", "Chart dimensions overflow the native record size");
    }
    return 222 + series_items + point_items + 4 * cells;
}

void apply_chart_render_cache(const LV& actual, LV& expected,
    std::size_t middle_start, std::size_t render_start,
    std::string_view path) {
    constexpr std::array<std::size_t, 7> middle_cache_offsets{84, 86, 87, 89, 90, 92, 93};
    constexpr std::array<std::size_t, 7> render_cache_offsets{2, 4, 5, 7, 8, 10, 11};
    const auto normalize = [&](std::size_t index) {
        if (index >= actual.items.size() || index >= expected.items.size()) {
            fail("OOF1102", child_path(path, index), "existing Chart render-cache scalar", "out of range", "Chart cache offset exceeds its record");
        }
        const auto& value = actual.items[index];
        if (value.is_list || value.atom.empty() || value.atom_kind != LV::AtomKind::raw) {
            fail("OOF1115", child_path(path, index), "finite numeric Chart render-cache scalar", describe(value), "Chart render cache has an unsupported value shape");
        }
        double parsed = 0.0;
        const auto* begin = value.atom.data();
        const auto* end = begin + value.atom.size();
        const auto result = std::from_chars(begin, end, parsed, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) {
            fail("OOF1115", child_path(path, index), "finite numeric Chart render-cache scalar", value.atom, "Chart render cache is not a finite number");
        }
        expected.items[index] = value;
    };
    for (const auto offset : middle_cache_offsets) normalize(middle_start + offset);
    for (const auto offset : render_cache_offsets) normalize(render_start + offset);
}

LV encode_chart_info(const model::ChartPayload& chart, std::string_view title) {
    const auto series_count = chart.series.size();
    const auto point_count = chart.points.size();
    const auto expected = chart_info_item_count(series_count, point_count, "$/Chart/Info");
    const auto expected_cell_count = series_count * point_count;
    using ChartValueKey = std::pair<std::uint64_t, std::uint64_t>;
    std::map<ChartValueKey, const model::ChartValue*> value_index;
    for (const auto& value : chart.values) {
        const auto key = ChartValueKey{value.series_ref.value(), value.point_ref.value()};
        if (!value_index.emplace(key, &value).second) {
            fail("OOF1122", "$/Chart/Values", "unique Series/Point references", std::to_string(key.first) + "/" + std::to_string(key.second), "Chart matrix contains a duplicate pair");
        }
    }
    if (value_index.size() != expected_cell_count) {
        fail("OOF1122", "$/Chart/Values", "complete Series by Point matrix", std::to_string(value_index.size()), "Chart matrix does not contain exactly one value for each pair");
    }
    const auto max_series_id = chart.series.empty() ? std::uint64_t{1} :
        std::max_element(chart.series.begin(), chart.series.end(), [](const auto& left, const auto& right) { return left.id.value() < right.id.value(); })->id.value();
    const auto max_point_id = chart.points.empty() ? std::uint64_t{0} :
        std::max_element(chart.points.begin(), chart.points.end(), [](const auto& left, const auto& right) { return left.id.value() < right.id.value(); })->id.value();
    std::vector<LV> info{raw("75"), raw(std::to_string(max_series_id)),
        raw(std::to_string(max_point_id)), raw("1"), raw(std::to_string(series_count))};
    const auto append = [&](std::vector<LV> values) {
        info.insert(info.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
    };
    std::vector<LV> rows;
    rows.reserve(series_count);
    for (std::size_t index = 0; index < series_count; ++index) {
        const auto& item = chart.series[index];
        auto row = canonical_chart_series_defaults();
        row[0] = encode_chart_color_property(item.color, "$/Chart/Series/Color");
        row[2] = raw(std::to_string(chart_marker_cache(item.marker, "$/Chart/Series/Marker")));
        row[3] = encoded_localized(item.text);
        row[4] = raw("1");
        row[7] = raw(std::to_string(item.id.value()));
        rows.insert(rows.end(), row.begin(), row.end());
    }
    append(std::move(rows));
    append(canonical_chart_series_defaults());
    info.push_back(raw("1"));
    info.push_back(raw(std::to_string(point_count)));
    for (std::size_t index = 0; index < point_count; ++index) {
        const auto& item = chart.points[index];
        const auto row = canonical_chart_series_defaults();
        append({encoded_localized(item.text), raw("1"), raw(std::to_string(item.id.value())), encode_chart_color_property(item.color, "$/Chart/Points/Color"), row[1], raw("4"), raw("0"), raw("0"), row[8], row[9], raw("0")});
    }
    auto middle = canonical_chart_middle_defaults();
    if (middle.size() <= 11) fail("OOF1122", "$/Chart/Title", "canonical Chart title slot", std::to_string(middle.size()), "Chart defaults do not contain the title slot");
    middle[11] = encoded_localized(title);
    append(std::move(middle));
    append({string_value(""), raw("0"), raw(chart.series.empty() || chart.points.empty() ? "1" : "0")});
    for (const auto& series : chart.series) {
        for (const auto& point : chart.points) {
            const auto found = value_index.find(ChartValueKey{series.id.value(), point.id.value()});
            if (found == value_index.end()) fail("OOF1122", "$/Chart/Values", "complete Series by Point matrix", "missing pair", "Chart matrix contains a missing pair");
            if (const auto* decimal = std::get_if<model::DecimalValue>(&found->second->value)) append({list({string_value("N"), raw(decimal->canonical)}), list({string_value("U")}), string_value(found->second->tooltip)});
            else append({list({string_value("U")}), list({string_value("U")}), string_value(found->second->tooltip)});
        }
    }
    append(canonical_chart_data_defaults());
    const auto companion = list({raw("0"), raw("0")});
    for (std::size_t index = 0; index <= series_count; ++index) append({companion});
    append(canonical_chart_style_defaults());
    const auto no_text = list({raw("1"), raw("0")});
    for (const auto& point : chart.points) append({list({encode_chart_color_property(point.color, "$/Chart/Points/Color")})});
    for (std::size_t index = 0; index <= series_count; ++index) {
        const bool summary = index == series_count;
        const auto style_color = summary ? list({raw("4"), raw("4"), list({raw("0")}), raw("4")}) : encode_chart_color_property(chart.series[index].color, "$/Chart/Series/Color");
        const auto marker = summary ? 4u : chart_marker_value(chart.series[index].marker, "$/Chart/Series/Marker");
        const auto style = list({style_color, raw(std::to_string(marker)), raw("0"), raw("0"), raw("0"), string_value(""), no_text, no_text, no_text, raw("0")});
        append({style});
    }
    append(canonical_chart_render_defaults());
    for (const auto& series : chart.series) {
        for (const auto& point : chart.points) {
            const auto& tooltip = value_index.at(ChartValueKey{series.id.value(), point.id.value()})->tooltip;
            const auto text = tooltip.empty() ? no_text
                : list({raw("1"), raw("1"), list({string_value("#"), string_value(tooltip)})});
            append({list({list({raw("1"), text, raw("0")}), raw("0")})});
        }
    }
    append(canonical_chart_tail_defaults());
    if (info.size() != expected) fail("OOF1122", "$/Chart/Info", "count-derived Chart grammar", std::to_string(info.size()), "Chart Info cardinality does not match the supported collection profile");
    return list(std::move(info));
}


LV encode_chart(const model::ControlNode& control, const GeometryContext& context) {
    const auto* chart = std::get_if<model::ChartPayload>(&control.payload);
    if (chart == nullptr || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Chart", "Chart payload with nonempty dimensions and positive ID", control.name, "Chart is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        control.properties().size() > 1 ||
        (control.properties().size() == 1 && !control.properties().contains(model::PropertyId::from_name("Title"))) ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/Chart", "Chart named collections and plain Position", control.name, "Chart uses a storage concept outside the supported profile");
    }
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::chart);
    std::string title;
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("Title"))) {
        const auto* text = std::get_if<std::string>(&entry->value);
        if (text == nullptr) fail("OOF1121", "$/Chart/Title", "string Title", "different value kind", "Chart Title has the wrong type");
        title = *text;
    }
    return list({raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value())),
        list({raw("11")}), encode_chart_info(*chart, title),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")})});
}

DecodedControl decode_chart(const LV& record, std::string_view path, const GeometryContext& context) {
    require_arity(record, 7, path);
    require_raw_constant(at(record, 0, path), model::metamodel::descriptor_for(model::ControlKind::chart).guid, child_path(path, 0));
    require_exact(at(record, 2, path), list({raw("11")}), child_path(path, 2), "Chart geometry header is unsupported");
    require_exact(at(record, 6, path), list({raw("0")}), child_path(path, 6), "Chart child-record section is unsupported");
    const auto id = integer_atom<std::uint64_t>(at(record, 1, path), child_path(path, 1));
    const auto& info = at(record, 3, path);
    require_list(info, child_path(path, 3));
    const auto info_path = child_path(path, 3);
    if (info.items.size() < 222) fail("OOF1102", info_path, "Chart info base and collection counts", describe(info), "Chart Info is truncated");
    require_raw_constant(at(info, 0, info_path), "75", child_path(info_path, 0));
    const auto series_count = integer_atom<std::uint32_t>(at(info, 4, info_path), child_path(info_path, 4));
    const auto series_size = static_cast<std::size_t>(series_count);
    if (series_size >= std::numeric_limits<std::size_t>::max() / 11) {
        fail("OOF1102", info_path, "non-overflowing Series count", std::to_string(series_count), "Chart Series count overflows its record offset");
    }
    const auto points_count_offset = std::size_t{5} + (series_size + 1) * 11 + 1;
    if (points_count_offset >= info.items.size()) fail("OOF1102", info_path, "point count after Series records", describe(info), "Chart Info is truncated");
    const auto point_count = integer_atom<std::uint32_t>(at(info, points_count_offset, info_path), child_path(info_path, points_count_offset));
    const auto expected = chart_info_item_count(static_cast<std::size_t>(series_count), static_cast<std::size_t>(point_count), info_path);
    if (info.items.size() != expected) fail("OOF1102", info_path, "supported dense Chart collection cardinality", std::to_string(info.items.size()), "Chart Info dimensions are unsupported");
    model::ChartPayload payload;
    for (std::size_t index = 0; index < series_count; ++index) {
        const auto row_offset = std::size_t{5} + index * 11;
        const auto series_id = integer_atom<std::uint64_t>(at(info, row_offset + 7, info_path), child_path(info_path, row_offset + 7));
        model::ChartSeries series;
        series.id = model::ObjectId{series_id};
        series.text = decoded_single_language_text(at(info, row_offset + 3, info_path), child_path(info_path, row_offset + 3));
        payload.series.push_back(std::move(series));
    }
    const auto points_start = points_count_offset + 1;
    for (std::size_t index = 0; index < point_count; ++index) {
        const auto row_offset = points_start + index * 11;
        const auto point_id = integer_atom<std::uint64_t>(at(info, row_offset + 2, info_path), child_path(info_path, row_offset + 2));
        model::ChartPoint point;
        point.id = model::ObjectId{point_id};
        point.text = decoded_single_language_text(at(info, row_offset, info_path), child_path(info_path, row_offset));
        payload.points.push_back(std::move(point));
    }
    const auto middle_start = points_start + static_cast<std::size_t>(point_count) * 11;
    const auto title_offset = middle_start + 11;
    const std::string title = decoded_single_language_text(at(info, title_offset, info_path), child_path(info_path, title_offset));
    const auto middle_defaults = canonical_chart_middle_defaults();
    const auto data_start = middle_start + middle_defaults.size() + 3;
    for (std::size_t series = 0; series < series_count; ++series) {
        for (std::size_t point = 0; point < point_count; ++point) {
            const auto cell_offset = data_start + (series * point_count + point) * 3;
            const auto& encoded = at(info, cell_offset, info_path);
            require_list(encoded, child_path(info_path, cell_offset));
            if (encoded.items.empty() || encoded.items.size() > 2) fail("OOF1102", child_path(info_path, cell_offset), "typed numeric or Undefined Chart cell", describe(encoded), "Chart cell has an unsupported value shape");
            const auto type = string_atom(encoded.items[0], child_path(info_path, cell_offset));
            require_exact(at(info, cell_offset + 1, info_path), list({string_value("U")}), child_path(info_path, cell_offset + 1), "Chart value info is unsupported");
            model::ChartValue value;
            value.tooltip = string_atom(at(info, cell_offset + 2, info_path), child_path(info_path, cell_offset + 2));
            value.series_ref = payload.series[series].id;
            value.point_ref = payload.points[point].id;
            if (type == "N" && encoded.items.size() == 2) value.value = model::DecimalValue{raw_atom(encoded.items[1], child_path(info_path, cell_offset + 1))};
            else if (type == "U" && encoded.items.size() == 1) value.value = model::UndefinedValue{};
            else fail("OOF1115", child_path(info_path, cell_offset), "N or U Chart value tag", type, "Chart cell uses an unsupported value kind");
            payload.values.push_back(std::move(value));
        }
    }
    const auto data_defaults = canonical_chart_data_defaults();
    const auto style_defaults = canonical_chart_style_defaults();
    const auto cells_end = data_start + static_cast<std::size_t>(series_count) * point_count * 3;
    const auto series_companions_start = cells_end + data_defaults.size();
    const auto style_defaults_start = series_companions_start + series_size + 1;
    const auto point_styles_start = style_defaults_start + style_defaults.size();
    const auto series_styles_start = point_styles_start + static_cast<std::size_t>(point_count);
    std::vector<std::optional<std::uint32_t>> derived_marker_caches(series_count);
    for (std::size_t point = 0; point < point_count; ++point) {
        const auto style_path = child_path(info_path, point_styles_start + point);
        const auto& point_style = at(info, point_styles_start + point, info_path);
        require_arity(point_style, 1, style_path);
        auto color = decode_chart_color_property(point_style.items[0], child_path(style_path, 0));
        const auto row_offset = points_start + point * 11;
        const auto row_color = decode_chart_color_property(at(info, row_offset + 3, info_path), child_path(info_path, row_offset + 3));
        if (chart_color_rgb(color, child_path(style_path, 0)) != chart_color_rgb(row_color, child_path(info_path, row_offset + 3))) {
            fail("OOF1115", style_path, "Point Color matching resolved RGB", "mismatch", "Chart Point Color differs from its resolved native RGB");
        }
        payload.points[point].color = color;
    }
    for (std::size_t series = 0; series < series_count; ++series) {
        const auto style_path = child_path(info_path, series_styles_start + series);
        const auto& series_style = at(info, series_styles_start + series, info_path);
        require_arity(series_style, 10, style_path);
        auto color = decode_chart_color_property(series_style.items[0], child_path(style_path, 0));
        const auto marker_id = integer_atom<std::uint32_t>(series_style.items[1], child_path(style_path, 1));
        const auto marker = decode_chart_marker(marker_id, child_path(style_path, 1));
        const auto row_offset = std::size_t{5} + series * 11;
        const auto row_color = decode_chart_color_property(at(info, row_offset, info_path), child_path(info_path, row_offset));
        if (chart_color_rgb(color, child_path(style_path, 0)) != chart_color_rgb(row_color, child_path(info_path, row_offset))) {
            fail("OOF1115", style_path, "Series Color matching resolved RGB", "mismatch", "Chart Series Color differs from its resolved native RGB");
        }
        const auto marker_cache_path = child_path(info_path, row_offset + 2);
        const auto rendered_marker = integer_atom<std::uint32_t>(at(info, row_offset + 2, info_path), marker_cache_path);
        if (marker.member == "Auto" || marker.member == "Alternation") {
            if (rendered_marker > 5) {
                fail("OOF1115", marker_cache_path, "derived Marker cache ordinal 0..5", std::to_string(rendered_marker), "Chart Series Marker cache is outside the supported range");
            }
            derived_marker_caches[series] = rendered_marker;
        } else if (rendered_marker != chart_marker_value(marker, child_path(style_path, 1))) {
            fail("OOF1115", marker_cache_path, "concrete Marker cache matching its named Marker", std::to_string(rendered_marker), "Chart Series Marker differs from its resolved native marker");
        }
        payload.series[series].color = color;
        payload.series[series].marker = marker;
    }
    model::ControlNode control{model::ObjectId{id}, string_atom(at(at(record, 5, path), 1, child_path(path, 5)), child_path(child_path(path, 5), 1)), std::move(payload)};
    if (!title.empty()) control.properties().set_explicit(model::PropertyId::from_name("Title"), title);
    const auto expected_info = encode_chart_info(std::get<model::ChartPayload>(control.payload), title);
    const auto render_start = cells_end + data_defaults.size() + series_size + 1 + style_defaults.size() +
        static_cast<std::size_t>(point_count) + series_size + 1;
    auto normalized_expected_info = expected_info;
    for (std::size_t series = 0; series < series_count; ++series) {
        if (derived_marker_caches[series]) {
            const auto row_offset = std::size_t{5} + series * 11;
            normalized_expected_info.items[row_offset + 2] = raw(std::to_string(*derived_marker_caches[series]));
        }
    }
    apply_chart_render_cache(info, normalized_expected_info, middle_start, render_start, info_path);
    require_exact(info, normalized_expected_info, info_path, "Chart Info contains unsupported non-named values");
    const auto expected_metadata = list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")});
    require_exact(at(record, 5, path), expected_metadata, child_path(path, 5), "Chart metadata contains unsupported fields");
    const auto geometry = decode_geometry(at(record, 4, path), child_path(path, 4), context);
    control.position = geometry.position;
    return {std::move(control), std::nullopt, geometry.incoming, std::nullopt, {}};
}

LV canonical_control_base_properties(bool enabled, std::string_view tool_tip) {
    auto properties = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},"
        "{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},"
        "{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    properties.items[1] = raw(enabled ? "1" : "0");
    properties.items[12] = encoded_localized(tool_tip);
    return properties;
}

LV canonical_progress_bar_info(
    bool enabled,
    std::string_view tool_tip,
    std::int32_t max_value = 100,
    std::int32_t min_value = 0,
    std::int32_t step = 1) {
    return list({canonical_control_base_properties(enabled, tool_tip), raw("3"), raw(std::to_string(min_value)),
        raw(std::to_string(max_value)), raw(std::to_string(step)), raw("1"), raw("0"), raw("2")});
}

LV canonical_track_bar_properties(bool enabled, std::string_view tool_tip) {
    auto properties = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},"
        "{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},"
        "{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    properties.items[1] = raw(enabled ? "1" : "0");
    properties.items[12] = encoded_localized(tool_tip);
    return properties;
}

LV canonical_track_bar_info(
    bool enabled,
    std::string_view tool_tip,
    std::int32_t min_value = 0,
    std::int32_t max_value = 100,
    std::int32_t step = 1) {
    return list({raw("1"),
        list({canonical_track_bar_properties(enabled, tool_tip), raw("5"), raw(std::to_string(min_value)),
            raw(std::to_string(max_value)), raw(std::to_string(step)), raw("10"), raw("2"), raw("2"),
            raw("5"), raw("100")}),
        list({raw("0")})});
}

LV canonical_list_box_properties(bool enabled, std::string_view tool_tip) {
    auto properties = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},"
        "{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},"
        "{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    properties.items[1] = raw(enabled ? "1" : "0");
    properties.items[12] = encoded_localized(tool_tip);
    return properties;
}

// Fixed ListBox slots are retained only for the observed public flags below.
LV canonical_list_box_info(
    bool enabled,
    bool show_picture,
    bool show_check_box,
    bool read_only,
    std::string_view tool_tip) {
    auto property_flags = parse_constant(
        "{23,100743712,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},"
        "{4,0,{12741203},0},{4,3,{-15},3},{4,3,{-13},3},2,2,0,0,0,1,0,1,1,"
        "{8,2,0,{-20},1,100},{8,2,0,{-20},1,100},0,0,1,0,0,0,0,0,0,0,"
        "100,1,2,2,2,0,0,2}");
    property_flags.items[1] = raw(read_only ? "100743712" : "100744736");
    return list({
        raw("1"),
        list({
            canonical_list_box_properties(enabled, tool_tip),
            std::move(property_flags),
            raw("6"),
            raw("0"),
            raw(show_picture ? "1" : "0"),
            raw(show_check_box ? "1" : "0"),
            raw("0"),
        }),
        list({raw("0")}),
    });
}

DecodedControl decode_progress_bar(
    const LV& record,
    std::string_view path,
    const GeometryContext& context,
    const AttributeRecord* linked_attribute) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::progress_bar);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const std::uint64_t raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 ProgressBar ID", std::to_string(raw_id),
            "ProgressBar ID is invalid");
    }
    if (linked_attribute && !(linked_attribute->type.entries.size() == 1 &&
        linked_attribute->type.entries.front().term == model::TypeDomainTerm::numeric &&
        !linked_attribute->type.entries.front().type_uuid)) {
        fail("OOF1122", "$/2/3", "link to a single numeric Attribute", linked_attribute->name,
            "ProgressBar DataPath must target a single numeric attribute");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 2, info_path);
    require_raw_constant(info.items[0], "0", child_path(info_path, 0));
    const auto& info_list = info.items[1];
    const auto info_list_path = child_path(info_path, 1);
    require_arity(info_list, 8, info_list_path);
    const auto& properties = info_list.items[0];
    const auto properties_path = child_path(info_list_path, 0);
    require_arity(properties, 21, properties_path);
    const bool enabled = bool_atom(properties.items[1], child_path(properties_path, 1));
    const std::string tool_tip = decoded_single_language_text(properties.items[12], child_path(properties_path, 12));
    const auto min_path = child_path(info_list_path, 2);
    const auto max_path = child_path(info_list_path, 3);
    const auto step_path = child_path(info_list_path, 4);
    const std::int32_t min_value = integer_atom<std::int32_t>(info_list.items[2], min_path);
    const std::int32_t max_value = integer_atom<std::int32_t>(info_list.items[3], max_path);
    const std::int32_t step = integer_atom<std::int32_t>(info_list.items[4], step_path);
    auto normalized_properties = properties;
    normalized_properties.items[1] = raw("1");
    normalized_properties.items[12] = encoded_localized("");
    auto normalized_info = info_list;
    normalized_info.items[0] = std::move(normalized_properties);
    normalized_info.items[2] = raw("0");
    normalized_info.items[3] = raw("100");
    normalized_info.items[4] = raw("1");
    require_exact(normalized_info, canonical_progress_bar_info(true, ""), info_list_path,
        "ProgressBar contains a property outside the supported profile");

    const auto geometry_path = child_path(path, 3);
    auto decoded_geometry = decode_geometry(record.items[3], geometry_path, context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty", "Control name is required");
    }
    require_exact(metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "ProgressBar metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "ProgressBar cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::ProgressBarPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    if (max_value != 100) control.properties().set_explicit(
        model::PropertyId::from_name("MaxValue"), static_cast<std::int64_t>(max_value));
    if (min_value != 0) control.properties().set_explicit(
        model::PropertyId::from_name("MinValue"), static_cast<std::int64_t>(min_value));
    if (step != 1) control.properties().set_explicit(
        model::PropertyId::from_name("Step"), static_cast<std::int64_t>(step));
    if (linked_attribute) control.data_path = model::DataPath{
        model::AttributeRef{model::ObjectId{static_cast<std::uint64_t>(linked_attribute->id.object_id)}}, {}};
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

DecodedControl decode_track_bar(
    const LV& record,
    std::string_view path,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::track_bar);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const std::uint64_t raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 TrackBar ID", std::to_string(raw_id),
            "TrackBar ID is invalid");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 3, info_path);
    require_raw_constant(info.items[0], "1", child_path(info_path, 0));
    const auto& info_payload = info.items[1];
    const auto payload_path = child_path(info_path, 1);
    require_arity(info_payload, 10, payload_path);
    const auto& properties = info_payload.items[0];
    const auto properties_path = child_path(payload_path, 0);
    require_arity(properties, 21, properties_path);
    const bool enabled = bool_atom(properties.items[1], child_path(properties_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        properties.items[12], child_path(properties_path, 12));
    const std::int32_t min_value = integer_atom<std::int32_t>(
        info_payload.items[2], child_path(payload_path, 2));
    const std::int32_t max_value = integer_atom<std::int32_t>(
        info_payload.items[3], child_path(payload_path, 3));
    const std::int32_t step = integer_atom<std::int32_t>(
        info_payload.items[4], child_path(payload_path, 4));
    if (max_value < 0) {
        fail("OOF1122", child_path(payload_path, 3), "non-negative TrackBar MaxValue",
            std::to_string(max_value), "TrackBar MaxValue below zero was clamped by the platform runtime");
    }
    if (min_value < 0) {
        fail("OOF1122", child_path(payload_path, 2), "non-negative TrackBar MinValue",
            std::to_string(min_value), "TrackBar MinValue below zero was not accepted by the platform runtime");
    }
    if (step <= 0) {
        fail("OOF1122", child_path(payload_path, 4), "positive TrackBar Step", std::to_string(step),
            "TrackBar Step at or below zero was not accepted by the platform runtime");
    }

    auto normalized_info = info;
    normalized_info.items[1].items[0].items[1] = raw("1");
    normalized_info.items[1].items[0].items[12] = encoded_localized("");
    normalized_info.items[1].items[2] = raw("0");
    normalized_info.items[1].items[3] = raw("100");
    normalized_info.items[1].items[4] = raw("1");
    require_exact(normalized_info, canonical_track_bar_info(true, ""), info_path,
        "TrackBar contains a property outside the supported profile");

    const auto geometry_path = child_path(path, 3);
    auto decoded_geometry = decode_geometry(record.items[3], geometry_path, context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty",
            "Control name is required");
    }
    require_exact(metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "TrackBar metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "TrackBar cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::TrackBarPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    if (max_value != 100) control.properties().set_explicit(
        model::PropertyId::from_name("MaxValue"), static_cast<std::int64_t>(max_value));
    if (min_value != 0) control.properties().set_explicit(
        model::PropertyId::from_name("MinValue"), static_cast<std::int64_t>(min_value));
    if (step != 1) control.properties().set_explicit(
        model::PropertyId::from_name("Step"), static_cast<std::int64_t>(step));
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

DecodedControl decode_list_box(
    const LV& record,
    std::string_view path,
    const AttributeRecord& linked_attribute,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::list_box);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 ListBox ID", std::to_string(raw_id),
            "ListBox ID is invalid");
    }
    if (linked_attribute.type.entries.size() != 1 ||
        linked_attribute.type.entries.front().term != model::TypeDomainTerm::value_list ||
        linked_attribute.type.entries.front().type_uuid.has_value()) {
        fail("OOF1122", "$/2/3", "link to one ValueList Attribute", linked_attribute.name,
            "ListBox DataPath must target a single ValueList Attribute");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 3, info_path);
    require_raw_constant(info.items[0], "1", child_path(info_path, 0));
    const auto& properties = info.items[1];
    const auto properties_path = child_path(info_path, 1);
    require_arity(properties, 7, properties_path);
    const auto base_path = child_path(properties_path, 0);
    require_arity(properties.items[0], 21, base_path);
    const bool enabled = bool_atom(properties.items[0].items[1], child_path(base_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        properties.items[0].items[12], child_path(base_path, 12));
    const auto flags_path = child_path(properties_path, 1);
    require_list(properties.items[1], flags_path);
    require_arity(properties.items[1], 38, flags_path);
    const auto read_only_code = integer_atom<std::uint32_t>(properties.items[1].items[1], child_path(flags_path, 1));
    if (read_only_code != 100743712 && read_only_code != 100744736) {
        fail("OOF1122", child_path(flags_path, 1), "observed ListBox ReadOnly state", std::to_string(read_only_code),
            "ListBox ReadOnly record is outside the supported profile");
    }
    const bool read_only = read_only_code == 100743712;
    require_raw_constant(properties.items[2], "6", child_path(properties_path, 2));
    require_raw_constant(properties.items[3], "0", child_path(properties_path, 3));
    const bool show_picture = bool_atom(properties.items[4], child_path(properties_path, 4));
    const bool show_check_box = bool_atom(properties.items[5], child_path(properties_path, 5));
    auto normalized_info = info;
    normalized_info.items[1].items[0].items[1] = raw("1");
    normalized_info.items[1].items[0].items[12] = encoded_localized("");
    normalized_info.items[1].items[1].items[1] = raw("100743712");
    normalized_info.items[1].items[4] = raw("0");
    normalized_info.items[1].items[5] = raw("0");
    require_exact(normalized_info,
        canonical_list_box_info(true, false, false, true, ""), info_path,
        "ListBox contains a property outside the supported profile");

    auto decoded_geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) fail("OOF1115", child_path(metadata_path, 1), "non-empty ListBox Name", "empty",
        "ListBox Name is required");
    require_exact(metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")} ),
        metadata_path, "ListBox metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "ListBox cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::ListBoxPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (show_picture) control.properties().set_explicit(model::PropertyId::from_name("ShowPicture"), true);
    if (show_check_box) control.properties().set_explicit(model::PropertyId::from_name("ShowCheckBox"), true);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    if (!read_only) control.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), false);
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

DecodedControl decode_check_box(
    const LV& record,
    std::string_view path,
    const AttributeRecord& linked_attribute,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::check_box);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 CheckBox ID", std::to_string(raw_id), "CheckBox ID is invalid");
    }

    if (!is_single_boolean_type_domain(linked_attribute.type)) {
        fail("OOF1122", "$/2/3", "link to a single Boolean Attribute", linked_attribute.name,
            "CheckBox DataPath must target a Boolean attribute");
    }
    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 3, info_path);
    require_raw_constant(info.items[0], "1", child_path(info_path, 0));
    const auto& info_payload = info.items[1];
    const auto payload_path = child_path(info_path, 1);
    require_arity(info_payload, 7, payload_path);
    const auto& properties = info_payload.items[0];
    const auto properties_path = child_path(payload_path, 0);
    require_arity(properties, 9, properties_path);
    const auto& base_properties = properties.items[0];
    const auto base_path = child_path(properties_path, 0);
    require_arity(base_properties, 21, base_path);
    const bool enabled = bool_atom(base_properties.items[1], child_path(base_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        base_properties.items[12], child_path(base_path, 12));
    const auto font = decode_control_font(base_properties.items[4], child_path(base_path, 4));
    const std::string caption = decoded_single_language_text(
        properties.items[2], child_path(properties_path, 2));
    auto normalized_info = info;
    normalized_info.items[1].items[0].items[0].items[12] = encoded_localized(tool_tip);
    normalized_info.items[1].items[0].items[2] = encoded_localized(caption);
    require_exact(
        normalized_info,
        canonical_check_box_info(enabled, caption, tool_tip, &font),
        info_path,
        "CheckBox uses an unsupported property, event, or storage variation");

    const auto geometry_path = child_path(path, 3);
    auto decoded_geometry = decode_geometry(record.items[3], geometry_path, context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty", "Control name is required");
    }
    require_exact(
        metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path,
        "CheckBox metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5), "CheckBox cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::CheckBoxPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (!caption.empty()) control.properties().set_explicit(model::PropertyId::from_name("Caption"), caption);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    if (font != model::FontValue{}) {
        control.properties().set_explicit(model::PropertyId::from_name("Font"), font);
    }
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

DecodedControl decode_choice_field(
    const LV& record,
    std::string_view path,
    const AttributeRecord* linked_attribute,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::choice_field);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 ChoiceField ID", std::to_string(raw_id),
            "ChoiceField ID is invalid");
    }
    if (linked_attribute != nullptr && !is_single_string_type_domain(linked_attribute->type)) {
        fail("OOF1122", "$/2/3", "link to a single String Attribute", linked_attribute->name,
            "ChoiceField DataPath must target a single String attribute");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 3, info_path);
    require_raw_constant(info.items[0], "2", child_path(info_path, 0));
    const auto& info_properties = info.items[1];
    const auto info_properties_path = child_path(info_path, 1);
    require_arity(info_properties, 46, info_properties_path);
    const auto& base_properties = info_properties.items[0];
    const auto base_path = child_path(info_properties_path, 0);
    require_arity(base_properties, 21, base_path);
    const bool enabled = bool_atom(base_properties.items[1], child_path(base_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        base_properties.items[12], child_path(base_path, 12));
    auto normalized_info = info;
    normalized_info.items[1].items[0].items[12] = encoded_localized(tool_tip);
    require_exact(normalized_info, canonical_choice_field_info(enabled, tool_tip), info_path,
        "ChoiceField contains a property, event, or storage variation outside the observed basic profile");

    auto decoded_geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty",
            "ChoiceField name is required");
    }
    require_exact(metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "ChoiceField metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "ChoiceField cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::ChoiceFieldPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

DecodedControl decode_text_document_field(
    const LV& record, std::string_view path, const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::text_document_field);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        fail("OOF1122", child_path(path, 1), "positive int64 TextDocumentField ID", std::to_string(raw_id), "TextDocumentField ID is invalid");
    const auto& info = record.items[2];
    require_arity(info, 7, child_path(path, 2));
    const auto& base = info.items[0];
    require_arity(base, 21, child_path(child_path(path, 2), 0));
    const bool enabled = bool_atom(base.items[1], child_path(child_path(path, 2), 0) + "/1");
    const auto border_color = decode_button_color(base.items[6], child_path(child_path(path, 2), 0) + "/6");
    const auto font = decode_control_font(base.items[4], child_path(child_path(path, 2), 0) + "/4");
    require_exact(info, canonical_text_document_field_info(enabled, border_color, font), child_path(path, 2),
        "TextDocumentField contains an unsupported persisted property or record variant");
    auto decoded_geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    require_arity(metadata, 6, child_path(path, 4));
    require_raw_constant(metadata.items[0], "14", child_path(child_path(path, 4), 0));
    const auto name = string_atom(metadata.items[1], child_path(child_path(path, 4), 1));
    if (name.empty()) fail("OOF1115", child_path(path, 4), "non-empty control name", "empty", "Control name is required");
    require_exact(metadata, list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        child_path(path, 4), "TextDocumentField metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5), "TextDocumentField cannot contain storage children");
    model::ControlNode control{model::ObjectId{raw_id}, name, model::TextDocumentFieldPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (border_color != model::ColorValue{}) control.properties().set_explicit(model::PropertyId::from_name("BorderColor"), border_color);
    if (font != model::FontValue{}) control.properties().set_explicit(model::PropertyId::from_name("Font"), font);
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

DecodedControl decode_calendar_field(
    const LV& record,
    std::string_view path,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::calendar_field);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 CalendarField ID", std::to_string(raw_id),
            "CalendarField ID is invalid");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 3, info_path);
    require_raw_constant(info.items[0], "1", child_path(info_path, 0));
    const auto& properties = info.items[1];
    const auto properties_path = child_path(info_path, 1);
    require_list(properties, properties_path);
    if (properties.items.empty() || !properties.items[0].is_list) {
        fail("OOF1102", properties_path, "CalendarField base properties", describe(properties),
            "CalendarField property record is incomplete");
    }
    const auto& base_properties = properties.items[0];
    require_arity(base_properties, 21, child_path(properties_path, 0));
    const bool enabled = bool_atom(base_properties.items[1], child_path(child_path(properties_path, 0), 1));
    require_arity(properties, 14, properties_path);
    const auto begin_path = child_path(properties_path, 5);
    const auto begin_atom = raw_atom(properties.items[5], begin_path);
    if (begin_atom != "00010101000000") {
        try {
            (void)value_codec::date_from_platform(begin_atom);
        } catch (const std::exception& error) {
            fail("OOF1122", begin_path, "local Gregorian date atom YYYYMMDDHHMMSS", begin_atom,
                std::string("CalendarField BeginOfDisplayPeriod is invalid: ") + error.what());
        }
    }
    require_exact(
        info,
        canonical_calendar_field_info(enabled, begin_atom),
        info_path,
        "CalendarField contains an unsupported property or storage variation");

    const auto geometry_path = child_path(path, 3);
    auto decoded_geometry = decode_geometry(record.items[3], geometry_path, context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty",
            "Control name is required");
    }
    require_exact(
        metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path,
        "CalendarField metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "CalendarField cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::CalendarFieldPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (begin_atom != "00010101000000") {
        control.properties().set_explicit(
            model::PropertyId::from_name("BeginOfDisplayPeriod"),
            model::DateValue{value_codec::date_from_platform(begin_atom)});
    }
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

LV canonical_dendrogram_data(std::int32_t orientation) {
    static constexpr std::string_view defaults = R"OOFDEF({0,{0,{11},{75,1,0,1,0,{4,0,{11837108},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},1,{1,1,{"ru","Сводная"}},0,0,0,1,{"U"},{"U"},0,1,0,-1,0,4,0,", ",4,{1,0},{1,0},{4,3,{-3},3},0,0,{1,0},1,0,{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},0,{4,3,{-1},3},1,{4,3,{-1},3},1,{4,3,{-1},3},0,{4,0,{16777215},0},{4,3,{-3},3},{4,3,{-3},3},{4,3,{-3},3},{8,3,0,1,100},{8,3,0,1,100},{8,3,0,1,100},1,1,1,1,1,{1,0},0,{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},{4,4,{0},4},1,1,0,4,30,1,0,0,0,0,1,0,0,0,0,1,1,2,{1,0},1,0,0,0,{4,0,{169},0},0,0,{1,0,0,0},0,180,5,1,0,4,{4,0,{11119017},0},1,0,1,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"",0,1,14,2,{8,3,0,1,100},1,{4,4,{0},4},{3,0,{0},1,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},1,1,1,0,0,95,1e-1,1e-1,3e-2,{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},{4,0,{0},0},2,255,0,0,00000000-0000-0000-0000-000000000000,0,{0,0},0,{0,0,{0,1,0,1,0},0,0},{0,0,{0,1,0,1,0},0,0},0,0,2,-2,1,10,1,20,0,0,{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},0,0,{4,4,{0},4},{4,4,{0},4},0,{{4,4,{0},4},4,0,0,0,"",{1,0},{1,0},{1,0},0},0,0,0,0,0,0,1,1,0,0,1,1,0,6,0,0,0,0.17,0,0.83,0.08,0,0,0.83,0,0,0.92,{0,0},{0,0},{0,0},{0,0},{0,14,{4,4,{0},4},{4,4,{0},4},0,0},{0,14,{4,4,{0},4},{4,4,{0},4},0,0},0,0,{0,0,0,0,0},{0,0,0,0},0,,60,{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{0,0,{0,1,0,1,0},0,0},0,0,0,0,0,0,0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4}}},{0,{3,0,1,0,{0,{8,0,0,0,0,0,{"U"},{1,0},{"U"},0,4294901761}},{0,1,{0,{4,0,{0},0},{4,0,{0},0}}},1,0}},{0,{3,0,1,0,{0,{8,0,0,0,0,0,{"U"},{1,0},{"U"},0,4294901761},0,0,0},{0,1,{0,{4,0,{0},0},{4,0,{0},0}}},1,0}},0,1,6,12,{4,0,{8388608},0},{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},0})OOFDEF";
    auto data = parse_constant(defaults);
    require_arity(data, 11, "$/Dendrogram/Data");
    data.items[4] = raw(std::to_string(orientation));
    return data;
}

void validate_dendrogram_distance(std::string_view value, std::string_view path);

LV dendrogram_item_row(std::uint64_t key, std::uint64_t next_key, std::uint64_t cache_key,
                       std::string_view value, const model::LocalizedStringValue& text) {
    LV encoded_text;
    try { encoded_text = list_stream::parse(value_codec::encode_localized_string(text)); }
    catch (const std::exception& error) {
        fail("OOF1108", "$/Dendrogram/Items/Text", "encodable LocalizedString", error.what(), "Dendrogram item text cannot be encoded");
    }
    return list({raw("8"), raw(std::to_string(key)), raw("0"), raw("0"), raw(std::to_string(next_key)), raw("0"),
        list({string_value("S"), string_value(std::string(value))}), std::move(encoded_text), parse_constant("{\"U\"}"),
        raw(std::to_string(cache_key)), raw("0")});
}

LV dendrogram_link_row(std::uint64_t key, std::uint64_t next_key, std::uint64_t cache_key,
                       const model::DendrogramLink& link, std::uint64_t first_key, std::uint64_t second_key) {
    LV encoded_title;
    try { encoded_title = list_stream::parse(value_codec::encode_localized_string(link.title)); }
    catch (const std::exception& error) {
        fail("OOF1108", "$/Dendrogram/Links/Title", "encodable LocalizedString", error.what(), "Dendrogram link title cannot be encoded");
    }
    LV encoded_distance;
    try { validate_dendrogram_distance(link.distance.canonical, "$/Dendrogram/Links/Distance"); encoded_distance = parse_constant(link.distance.canonical); }
    catch (const std::exception& error) {
        fail("OOF1122", "$/Dendrogram/Links/Distance", "canonical decimal", error.what(), "Dendrogram link distance cannot be encoded");
    }
    return list({raw("8"), raw(std::to_string(key)), raw("0"), raw("0"), raw(std::to_string(next_key)), raw("0"),
        parse_constant("{\"U\"}"), std::move(encoded_title), parse_constant("{\"U\"}"), raw(std::to_string(cache_key)), raw("0"),
        raw(std::to_string(first_key)), raw(std::to_string(second_key)), std::move(encoded_distance)});
}

LV dendrogram_sentinel(std::size_t last_key) {
    auto sentinel = parse_constant(R"({8,0,0,1,0,0,{"U"},{1,0},{"U"},0,4294901761})");
    sentinel.items[5] = raw(std::to_string(last_key));
    return sentinel;
}

void validate_dendrogram_cache(const LV& sequence, bool links, std::string_view path) {
    const auto defaults = canonical_dendrogram_data(0);
    const auto& default_elements = defaults.items[2].items[1].items[5];
    const auto& default_links = defaults.items[3].items[1].items[5];
    const LV& cache = sequence.items[sequence.items.size() - 3];
    const auto& expected_default = links ? default_links : default_elements;
    if (list_stream::dump_compact(cache) == list_stream::dump_compact(expected_default)) return;
    fail("OOF1114", std::string(path), "the single default appearance cache candidate", describe(cache), "Dendrogram appearance cache is unsupported");
}

void validate_dendrogram_distance(std::string_view value, std::string_view path) {
    if (!value.empty() && (value.front() == '+' || value.front() == '-')) {
        value.remove_prefix(1);
    }
    const auto point = value.find('.');
    if (value.empty() || (point != std::string_view::npos && value.find('.', point + 1) != std::string_view::npos))
        fail("OOF1122", std::string(path), "xs:decimal Distance without exponent notation", std::string(value), "Dendrogram link distance is invalid");
    const auto integer = point == std::string_view::npos ? value : value.substr(0, point);
    const auto fraction = point == std::string_view::npos ? std::string_view{} : value.substr(point + 1);
    const auto digits_only = [](std::string_view digits) {
        return std::all_of(digits.begin(), digits.end(), [](unsigned char character) {
            return character >= '0' && character <= '9';
        });
    };
    if ((!integer.empty() && !digits_only(integer)) || (!fraction.empty() && !digits_only(fraction)) ||
        (integer.empty() && fraction.empty()))
        fail("OOF1122", std::string(path), "xs:decimal Distance without exponent notation", std::string(value), "Dendrogram link distance is invalid");
}

void validate_dendrogram_graph(const model::DendrogramPayload& graph, std::string_view path) {
    const auto max_key = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 1);
    const auto max_collection_rows = (std::vector<LV>{}.max_size() - 6) / 2 - 1;
    const auto max_rows = std::min(max_key, max_collection_rows);
    if (graph.items.size() > max_rows || graph.links.size() > max_rows)
        fail("OOF1122", std::string(path), "Dendrogram collection size representable by uint32 keys and list arity",
            std::to_string(graph.items.size()) + "/" + std::to_string(graph.links.size()), "Dendrogram graph exceeds the encodable collection range");
    std::map<std::string, std::size_t> keys;
    for (std::size_t index = 0; index < graph.items.size(); ++index) {
        const auto& item = graph.items[index];
        if (item.value.empty() || !keys.emplace(item.value, index + 1).second || item.text.items.empty())
            fail("OOF1122", std::string(path), "unique non-empty item values with localized text", item.value, "Dendrogram item is invalid");
        std::set<std::string> languages;
        for (const auto& localized : item.text.items)
            if (localized.language.empty() || !languages.insert(localized.language).second)
                fail("OOF1122", std::string(path), "one text per language", localized.language, "Dendrogram item localization is invalid");
    }
    std::set<std::pair<std::string, std::string>> edges;
    std::vector<std::size_t> parent(graph.items.size());
    for (std::size_t index = 0; index < parent.size(); ++index) parent[index] = index;
    for (const auto& link : graph.links) {
        const auto first = keys.find(link.first_item), second = keys.find(link.second_item);
        if (first == keys.end() || second == keys.end() || first == second || link.title.items.empty())
            fail("OOF1122", std::string(path), "distinct resolved endpoints and localized title", link.first_item + "->" + link.second_item, "Dendrogram link is invalid");
        const auto edge = std::minmax(link.first_item, link.second_item);
        if (!edges.emplace(edge.first, edge.second).second)
            fail("OOF1122", std::string(path), "unique link edges", link.first_item + "->" + link.second_item, "Duplicate Dendrogram link");
        auto root = [&](std::size_t node) { while (parent[node] != node) node = parent[node]; return node; };
        const auto first_root = root(first->second - 1), second_root = root(second->second - 1);
        if (first_root == second_root) fail("OOF1122", std::string(path), "acyclic links", link.first_item + "->" + link.second_item, "Dendrogram cycle is unsupported");
        parent[first_root] = second_root;
        validate_dendrogram_distance(link.distance.canonical, child_path(path, 0));
        std::set<std::string> languages;
        for (const auto& localized : link.title.items)
            if (localized.language.empty() || !languages.insert(localized.language).second)
                fail("OOF1122", std::string(path), "one title per language", localized.language, "Dendrogram link localization is invalid");
    }
}

void fill_dendrogram_collection(LV& wrapper, const std::vector<LV>& rows, const LV& sentinel,
                                bool links, std::string_view path) {
    if (rows.empty()) return;
    const auto max_key = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 1);
    const auto max_collection_rows = (std::vector<LV>{}.max_size() - 6) / 2 - 1;
    if (rows.size() > std::min(max_key, max_collection_rows))
        fail("OOF1122", std::string(path), "Dendrogram collection size representable by uint32 keys and list arity",
            std::to_string(rows.size()), "Dendrogram collection is too large to encode");
    require_arity(wrapper, 2, path);
    auto& sequence = wrapper.items[1];
    require_arity(sequence, 8, child_path(path, 1));
    const LV cache = sequence.items[5];
    const LV auto_text = sequence.items[6];
    const LV reverse = sequence.items[7];
    std::vector<LV> fields{raw("3"), raw("0"), raw(std::to_string(rows.size() + 1)), raw("1")};
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (index != 0) fields.push_back(raw(std::to_string(index + 1)));
        if (links) {
            if (!rows[index].is_list || rows[index].items.size() != 14)
                fail("OOF1114", std::string(path), "14-field internal link row", describe(rows[index]), "Dendrogram link row is malformed");
            LV base_row = list(std::vector<LV>(rows[index].items.begin(), rows[index].items.begin() + 11));
            fields.push_back(list({raw("0"), std::move(base_row), rows[index].items[11], rows[index].items[12], rows[index].items[13]}));
        } else {
            fields.push_back(list({raw("0"), rows[index]}));
        }
    }
    fields.push_back(raw("0"));
    fields.push_back(links ? list({raw("0"), sentinel, raw("0"), raw("0"), raw("0")}) : list({raw("0"), sentinel}));
    fields.push_back(cache);
    fields.push_back(auto_text);
    fields.push_back(reverse);
    sequence = list(std::move(fields));
}

struct DecodedDendrogramItems {
    std::vector<model::DendrogramItem> values_in_chain_order;
    std::map<std::uint32_t, std::string> values_by_key;
};

DecodedDendrogramItems decode_dendrogram_items(const LV& wrapper, std::string_view path) {
    require_arity(wrapper, 2, path);
    require_raw_constant(wrapper.items[0], "0", child_path(path, 0));
    const auto& sequence = wrapper.items[1];
    if (!sequence.is_list || sequence.items.size() < 8 || sequence.items[0].atom != "3" || sequence.items[1].atom != "0")
        fail("OOF1114", std::string(path), "Dendrogram item collection", describe(sequence), "Dendrogram Items collection is malformed");
    const auto count = integer_atom<std::uint32_t>(sequence.items[2], child_path(path, 1));
    if (count == 0) fail("OOF1122", std::string(path), "one sentinel and an item collection", std::to_string(count), "Dendrogram item collection count is invalid");
    if (count == 1) {
        const auto defaults = canonical_dendrogram_data(0);
        require_exact(wrapper, defaults.items[2], path, "Empty Dendrogram Items must retain the canonical factory collection");
        return {};
    }
    const auto indexed_fields = sequence.items.size() - 6;
    if (indexed_fields % 2 != 0 || indexed_fields / 2 != static_cast<std::size_t>(count))
        fail("OOF1114", std::string(path), "canonical linked item sequence", describe(sequence), "Dendrogram Items linked sequence is malformed");
    const auto row_count = static_cast<std::size_t>(count) - 1;
    std::map<std::uint32_t, model::DendrogramItem> items_by_key;
    std::map<std::uint32_t, std::uint32_t> next_by_key;
    DecodedDendrogramItems result;
    std::set<std::string> values;
    for (std::size_t index = 0; index < row_count; ++index) {
        const auto entry_index = 4 + index * 2;
        const auto key_index = 3 + index * 2;
        const auto pointer = integer_atom<std::uint32_t>(sequence.items[key_index], child_path(path, key_index));
        const auto& wrapped = sequence.items[entry_index];
        require_arity(wrapped, 2, child_path(path, entry_index));
        require_raw_constant(wrapped.items[0], "0", child_path(path, entry_index));
        const auto& row = wrapped.items[1];
        require_arity(row, 11, child_path(path, entry_index));
        if (integer_atom<std::uint32_t>(row.items[0], path) != 8 ||
            integer_atom<std::uint32_t>(row.items[1], path) != pointer ||
            integer_atom<std::uint32_t>(row.items[2], path) != 0 ||
            integer_atom<std::uint32_t>(row.items[3], path) != 0 ||
            integer_atom<std::uint32_t>(row.items[5], path) != 0 || integer_atom<std::uint32_t>(row.items[10], path) != 0)
            fail("OOF1114", child_path(path, entry_index), "supported Dendrogram item row", describe(row), "Dendrogram item metadata is unsupported");
        if (pointer == 0 || pointer > row_count || items_by_key.contains(pointer))
            fail("OOF1114", child_path(path, key_index), "unique item key in the collection range", std::to_string(pointer), "Dendrogram item key is invalid");
        const auto next_key = integer_atom<std::uint32_t>(row.items[4], child_path(child_path(path, entry_index), 4));
        if (next_key > row_count)
            fail("OOF1114", child_path(child_path(path, entry_index), 4), "next item key in the collection range or zero", std::to_string(next_key), "Dendrogram item chain key is invalid");
        require_arity(row.items[6], 2, child_path(path, 6));
        const auto value_kind = string_atom(row.items[6].items[0], child_path(child_path(path, 6), 0));
        if (value_kind != "S") fail("OOF1122", child_path(path, 6), "string item Value", describe(row.items[6]), "Only string Dendrogram values are supported");
        const auto value_text = string_atom(row.items[6].items[1], child_path(child_path(path, 6), 1));
        model::LocalizedStringValue text;
        try {
            list_stream::ListInStream in(row.items[7]);
            text = value_codec::read_localized_string(in);
        } catch (const std::exception& error) {
            fail("OOF1108", child_path(path, 7), "LocalizedString title", error.what(), "Dendrogram item text is malformed");
        }
        model::DendrogramItem item{value_text, std::move(text)};
        if (item.value.empty() || !values.insert(item.value).second)
            fail("OOF1122", child_path(path, 6), "unique non-empty string item Value", item.value, "Dendrogram item Value is invalid");
        require_exact(row.items[8], parse_constant("{\"U\"}"), child_path(path, 8), "Dendrogram item details are unsupported");
        if (integer_atom<std::uint32_t>(row.items[9], path) != 0)
            fail("OOF1114", child_path(path, 9), "zero cache key for diagnostic candidate", describe(row.items[9]), "Dendrogram item cache key is unsupported");
        result.values_by_key.emplace(pointer, item.value);
        items_by_key.emplace(pointer, std::move(item));
        next_by_key.emplace(pointer, next_key);
    }
    const auto sentinel_index = sequence.items.size() - 5;
    if (sequence.items[sentinel_index].atom != "0") fail("OOF1114", std::string(path), "zero sentinel key", describe(sequence.items[sentinel_index]), "Dendrogram sentinel key is malformed");
    const auto& sentinel = sequence.items[sentinel_index + 1];
    require_arity(sentinel, 2, child_path(path, sentinel_index + 1));
    require_raw_constant(sentinel.items[0], "0", child_path(path, sentinel_index + 1));
    require_exact(sentinel.items[1], dendrogram_sentinel(count - 1), child_path(path, sentinel_index + 1), "Dendrogram item sentinel is unsupported");
    require_raw_constant(sequence.items[sequence.items.size() - 2], "1", child_path(path, sequence.items.size() - 2));
    require_raw_constant(sequence.items.back(), "0", child_path(path, sequence.items.size() - 1));
    validate_dendrogram_cache(sequence, false, path);
    const auto first_key = integer_atom<std::uint32_t>(sentinel.items[1].items[3], child_path(path, sentinel_index + 1));
    const auto last_key = integer_atom<std::uint32_t>(sentinel.items[1].items[5], child_path(path, sentinel_index + 1));
    std::set<std::uint32_t> visited;
    auto current_key = first_key;
    for (;;) {
        const auto current = items_by_key.find(current_key);
        if (current == items_by_key.end())
            fail("OOF1114", std::string(path), "complete item chain from sentinel first key", std::to_string(current_key), "Dendrogram item chain references a missing key");
        if (!visited.insert(current_key).second)
            fail("OOF1114", std::string(path), "acyclic item chain", std::to_string(current_key), "Dendrogram item chain contains a cycle");
        result.values_in_chain_order.push_back(std::move(current->second));
        const auto next_key = next_by_key.at(current_key);
        if (next_key == 0) {
            if (current_key != last_key)
                fail("OOF1114", std::string(path), "sentinel last key at the end of the item chain", std::to_string(current_key), "Dendrogram item chain ends before its sentinel tail");
            break;
        }
        if (current_key == last_key)
            fail("OOF1114", std::string(path), "zero next key after the sentinel last key", std::to_string(next_key), "Dendrogram item chain continues beyond its sentinel tail");
        current_key = next_key;
    }
    if (visited.size() != row_count)
        fail("OOF1114", std::string(path), "every item reachable from the sentinel first key", std::to_string(visited.size()), "Dendrogram item chain omits stored rows");
    return result;
}

std::vector<model::DendrogramLink> decode_dendrogram_links(const LV& wrapper, const DecodedDendrogramItems& items,
                                                           std::string_view path) {
    require_arity(wrapper, 2, path);
    require_raw_constant(wrapper.items[0], "0", child_path(path, 0));
    const auto& sequence = wrapper.items[1];
    if (!sequence.is_list || sequence.items.size() < 8 || sequence.items[0].atom != "3" || sequence.items[1].atom != "0")
        fail("OOF1114", std::string(path), "Dendrogram link collection", describe(sequence), "Dendrogram Links collection is malformed");
    const auto count = integer_atom<std::uint32_t>(sequence.items[2], child_path(path, 1));
    if (count == 0) fail("OOF1122", std::string(path), "one sentinel and a link collection", std::to_string(count), "Dendrogram link collection count is invalid");
    if (count == 1) {
        const auto defaults = canonical_dendrogram_data(0);
        require_exact(wrapper, defaults.items[3], path, "Empty Dendrogram Links must retain the canonical factory collection");
        return {};
    }
    const auto indexed_fields = sequence.items.size() - 6;
    if (indexed_fields % 2 != 0 || indexed_fields / 2 != static_cast<std::size_t>(count))
        fail("OOF1114", std::string(path), "canonical linked link sequence", describe(sequence), "Dendrogram Links linked sequence is malformed");
    const auto row_count = static_cast<std::size_t>(count) - 1;
    std::map<std::uint32_t, model::DendrogramLink> links_by_key;
    std::map<std::uint32_t, std::uint32_t> next_by_key;
    for (std::size_t index = 0; index < row_count; ++index) {
        const auto entry_index = 4 + index * 2;
        const auto key_index = 3 + index * 2;
        const auto pointer = integer_atom<std::uint32_t>(sequence.items[key_index], child_path(path, key_index));
        const auto& wrapped = sequence.items[entry_index];
        require_arity(wrapped, 5, child_path(path, entry_index));
        require_raw_constant(wrapped.items[0], "0", child_path(path, entry_index));
        const auto& row = wrapped.items[1];
        require_arity(row, 11, child_path(path, entry_index));
        if (integer_atom<std::uint32_t>(row.items[0], path) != 8 ||
            integer_atom<std::uint32_t>(row.items[1], path) != pointer || integer_atom<std::uint32_t>(row.items[2], path) != 0 ||
            integer_atom<std::uint32_t>(row.items[3], path) != 0 ||
            integer_atom<std::uint32_t>(row.items[5], path) != 0 || integer_atom<std::uint32_t>(row.items[10], path) != 0)
            fail("OOF1114", child_path(path, entry_index), "supported Dendrogram link row", describe(row), "Dendrogram link metadata is unsupported");
        if (pointer == 0 || pointer > row_count || links_by_key.contains(pointer))
            fail("OOF1114", child_path(path, key_index), "unique link key in the collection range", std::to_string(pointer), "Dendrogram link key is invalid");
        const auto next_key = integer_atom<std::uint32_t>(row.items[4], child_path(child_path(path, entry_index), 4));
        if (next_key > row_count)
            fail("OOF1114", child_path(child_path(path, entry_index), 4), "next link key in the collection range or zero", std::to_string(next_key), "Dendrogram link chain key is invalid");
        require_exact(row.items[6], parse_constant("{\"U\"}"), child_path(path, 6), "Dendrogram link value is unsupported");
        model::LocalizedStringValue title;
        try { list_stream::ListInStream in(row.items[7]); title = value_codec::read_localized_string(in); }
        catch (const std::exception& error) { fail("OOF1108", child_path(path, 7), "LocalizedString title", error.what(), "Dendrogram link title is malformed"); }
        require_exact(row.items[8], parse_constant("{\"U\"}"), child_path(path, 8), "Dendrogram link details are unsupported");
        const auto first_key = integer_atom<std::uint32_t>(wrapped.items[2], child_path(path, entry_index + 2));
        const auto second_key = integer_atom<std::uint32_t>(wrapped.items[3], child_path(path, entry_index + 3));
        if (first_key == 0 || second_key == 0 || !items.values_by_key.contains(first_key) || !items.values_by_key.contains(second_key))
            fail("OOF1122", std::string(path), "endpoints resolving to named items", describe(row), "Dendrogram link endpoint is unresolved");
        model::DendrogramLink link;
        link.first_item = items.values_by_key.at(first_key);
        link.second_item = items.values_by_key.at(second_key);
        link.title = std::move(title);
        const auto distance = list_stream::dump_compact(wrapped.items[4]);
        validate_dendrogram_distance(distance, child_path(path, entry_index + 4));
        link.distance = model::DecimalValue{distance};
        if (integer_atom<std::uint32_t>(row.items[9], path) != 0)
            fail("OOF1114", child_path(path, 9), "zero cache key for diagnostic candidate", describe(row.items[9]), "Dendrogram link cache key is unsupported");
        links_by_key.emplace(pointer, std::move(link));
        next_by_key.emplace(pointer, next_key);
    }
    const auto sentinel_index = sequence.items.size() - 5;
    if (sequence.items[sentinel_index].atom != "0") fail("OOF1114", std::string(path), "zero sentinel key", describe(sequence.items[sentinel_index]), "Dendrogram link sentinel key is malformed");
    const auto& sentinel = sequence.items[sentinel_index + 1];
    require_arity(sentinel, 5, child_path(path, sentinel_index + 1));
    require_raw_constant(sentinel.items[0], "0", child_path(path, sentinel_index + 1));
    require_exact(sentinel.items[1], dendrogram_sentinel(count - 1), child_path(path, sentinel_index + 1), "Dendrogram link sentinel is unsupported");
    require_raw_constant(sentinel.items[2], "0", child_path(path, sentinel_index + 1));
    require_raw_constant(sentinel.items[3], "0", child_path(path, sentinel_index + 1));
    require_raw_constant(sentinel.items[4], "0", child_path(path, sentinel_index + 1));
    require_raw_constant(sequence.items[sequence.items.size() - 2], "1", child_path(path, sequence.items.size() - 2));
    require_raw_constant(sequence.items.back(), "0", child_path(path, sequence.items.size() - 1));
    validate_dendrogram_cache(sequence, true, path);
    const auto first_key = integer_atom<std::uint32_t>(sentinel.items[1].items[3], child_path(path, sentinel_index + 1));
    const auto last_key = integer_atom<std::uint32_t>(sentinel.items[1].items[5], child_path(path, sentinel_index + 1));
    std::vector<model::DendrogramLink> result;
    std::set<std::uint32_t> visited;
    auto current_key = first_key;
    for (;;) {
        const auto current = links_by_key.find(current_key);
        if (current == links_by_key.end())
            fail("OOF1114", std::string(path), "complete link chain from sentinel first key", std::to_string(current_key), "Dendrogram link chain references a missing key");
        if (!visited.insert(current_key).second)
            fail("OOF1114", std::string(path), "acyclic link chain", std::to_string(current_key), "Dendrogram link chain contains a cycle");
        result.push_back(std::move(current->second));
        const auto next_key = next_by_key.at(current_key);
        if (next_key == 0) {
            if (current_key != last_key)
                fail("OOF1114", std::string(path), "sentinel last key at the end of the link chain", std::to_string(current_key), "Dendrogram link chain ends before its sentinel tail");
            break;
        }
        if (current_key == last_key)
            fail("OOF1114", std::string(path), "zero next key after the sentinel last key", std::to_string(next_key), "Dendrogram link chain continues beyond its sentinel tail");
        current_key = next_key;
    }
    if (visited.size() != row_count)
        fail("OOF1114", std::string(path), "every link reachable from the sentinel first key", std::to_string(visited.size()), "Dendrogram link chain omits stored rows");
    return result;
}

DecodedControl decode_dendrogram(
    const LV& record,
    std::string_view path,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::dendrogram);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 Dendrogram ID", std::to_string(raw_id),
            "Dendrogram ID is invalid");
    }
    require_arity(record.items[2], 11, child_path(path, 2));
    const auto orientation = integer_atom<std::int32_t>(at(record.items[2], 4, child_path(path, 2)),
        child_path(child_path(path, 2), 4));
    if (orientation != 0 && orientation != 1) {
        fail("OOF1122", child_path(child_path(path, 2), 4), "Dendrogram orientation storage value 0 or 1",
            std::to_string(orientation), "Dendrogram orientation is unsupported");
    }
    auto normalized_data = record.items[2];
    auto expected_data = canonical_dendrogram_data(orientation);
    const auto data_path = child_path(path, 2);
    auto items = decode_dendrogram_items(record.items[2].items[2], child_path(data_path, 2));
    auto links = decode_dendrogram_links(record.items[2].items[3], items, child_path(data_path, 3));
    model::DendrogramPayload graph;
    graph.items = items.values_in_chain_order;
    graph.links = links;
    validate_dendrogram_graph(graph, child_path(data_path, 2));
    normalized_data.items[2] = expected_data.items[2];
    normalized_data.items[3] = expected_data.items[3];
    require_exact(normalized_data, expected_data, data_path,
        "Dendrogram contains unsupported tree style or extension values");
    auto geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto metadata_path = child_path(path, 4);
    require_arity(record.items[4], 6, metadata_path);
    require_raw_constant(record.items[4].items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(record.items[4].items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty Dendrogram name", "empty",
            "Dendrogram name is required");
    }
    require_exact(record.items[4], list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "Dendrogram metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "Dendrogram cannot contain storage children");
    model::ControlNode control{model::ObjectId{raw_id}, name, std::move(graph)};
    if (orientation != 0) {
        control.properties().set_explicit(model::PropertyId::from_name("Orientation"), model::EnumerationValue{
            "DendrogramOrientation", "Down"});
    }
    control.position = std::move(geometry.position);
    return {std::move(control), std::nullopt, std::move(geometry.incoming), std::nullopt, {}};
}


DecodedControl decode_spreadsheet_document_field(
    const LV& record,
    std::string_view path,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::spreadsheet_document_field);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        fail("OOF1122", child_path(path, 1), "positive int64 SpreadsheetDocumentField ID", std::to_string(raw_id), "Control ID is invalid");

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 33, info_path);
    const auto& document_info = at(info, 11, info_path);
    const auto document_path = child_path(info_path, 11);
    require_list(document_info, document_path);
    if (document_info.items.size() < 65) fail("OOF1102", document_path, "complete SpreadsheetDocumentField info", describe(document_info), "Document info is incomplete");
    require_raw_constant(document_info.items[14], "2", child_path(document_path, 14));
    const auto row_count = integer_atom<std::uint32_t>(document_info.items[15], child_path(document_path, 15));
    const auto row_payload_size = document_info.items.size() - 65;
    if (static_cast<std::size_t>(row_count) > row_payload_size / 3)
        fail("OOF1114", child_path(document_path, 15), "row count matching bounded row records", std::to_string(row_count), "Spreadsheet Document row count is invalid");
    model::SpreadsheetDocumentFieldPayload payload;
    std::vector<std::size_t> typed_cell_indices;
    std::vector<bool> typed_cell_defaults;
    std::vector<std::uint32_t> typed_format_references;
    std::vector<std::size_t> typed_cell_record_indices;
    std::size_t cursor = 16;
    std::uint32_t previous_row = 0;
    bool first_row = true;
    for (std::uint32_t row_number = 0; row_number < row_count; ++row_number) {
        const auto row_path = child_path(document_path, cursor);
        const auto row_index = integer_atom<std::uint32_t>(at(document_info, cursor, document_path), row_path);
        ++cursor;
        if (row_index == std::numeric_limits<std::uint32_t>::max() || (!first_row && row_index <= previous_row))
            fail("OOF1114", row_path, "strictly increasing supported row indices", std::to_string(row_index), "Spreadsheet row index is invalid");
        require_raw_constant(at(document_info, cursor, document_path), "0", child_path(document_path, cursor));
        ++cursor;
        const auto cell_count = integer_atom<std::uint32_t>(at(document_info, cursor, document_path), child_path(document_path, cursor));
        ++cursor;
        if (cell_count == 0 || cursor > document_info.items.size() ||
            static_cast<std::size_t>(cell_count) > (document_info.items.size() - cursor) / 2)
            fail("OOF1114", child_path(document_path, cursor - 1), "nonempty row with matching cell records", std::to_string(cell_count), "Spreadsheet row cell count is invalid");
        std::uint32_t previous_column = 0;
        bool first_column = true;
        for (std::uint32_t cell_index = 0; cell_index < cell_count; ++cell_index) {
            const auto item_index = cursor++;
            const auto column = integer_atom<std::uint32_t>(at(document_info, item_index, document_path), child_path(document_path, item_index));
            if (column == std::numeric_limits<std::uint32_t>::max() || (!first_column && column <= previous_column))
                fail("OOF1114", child_path(document_path, item_index), "strictly increasing supported column indices", std::to_string(column), "Spreadsheet column index is invalid");
            const auto value_index = cursor++;
            const auto& value = at(document_info, value_index, document_path);
            const auto value_path = child_path(document_path, value_index);
            if (value.is_list && value.items.size() == 4 && !value.items[0].is_list &&
                value.items[0].atom == "16") {
                require_raw_constant(value.items[0], "16", child_path(value_path, 0));
                require_raw_constant(value.items[1], "0", child_path(value_path, 1));
                const auto text = decoded_single_language_text(value.items[2], child_path(value_path, 2));
                require_raw_constant(value.items[3], "0", child_path(value_path, 3));
                payload.cells.push_back({row_index + 1, column + 1, text, std::nullopt});
            } else if (value.is_list && value.items.size() >= 2 && !value.items[0].is_list &&
                (value.items[0].atom == "0" || value.items[0].atom == "1" ||
                 value.items[0].atom == "2" || value.items[0].atom == "3")) {
                const auto flags = integer_atom<std::uint32_t>(value.items[0], child_path(value_path, 0));
                const bool has_control = (flags & 1) != 0;
                const bool has_value = (flags & 2) != 0;
                require_arity(value, 2 + (has_control ? 2 : 0) + (has_value ? 1 : 0), value_path);
                if (has_control) require_raw_constant(value.items[2], "1", child_path(value_path, 2));
                const auto reference = integer_atom<std::uint32_t>(value.items[1], child_path(value_path, 1));
                auto typed = has_value ? decoded_spreadsheet_cell_value(value.items.back(), value_path) :
                    model::PropertyValue{model::UndefinedValue{}};
                typed_cell_indices.push_back(payload.cells.size());
                typed_cell_defaults.push_back(!has_value);
                typed_format_references.push_back(reference);
                typed_cell_record_indices.push_back(value_index);
                payload.cells.push_back({row_index + 1, column + 1, {},
                    model::SpreadsheetDocumentCellValue{model::TypeDomainPatternValue{}, std::move(typed)}});
            } else {
                fail("OOF1114", value_path, "plain Text or a supported typed cell record", describe(value),
                    "Spreadsheet Document cell record is unsupported");
            }
            previous_column = column;
            first_column = false;
        }
        previous_row = row_index;
        first_row = false;
    }
    const std::size_t tail_start = cursor + 2;
    if (tail_start > document_info.items.size() || document_info.items.size() - tail_start < 29)
        fail("OOF1102", document_path, "complete SpreadsheetDocumentField typed-value tail", describe(document_info),
            "Spreadsheet Document typed-value table is incomplete");
    const auto typed_count = integer_atom<std::uint32_t>(at(document_info, tail_start + 25, document_path),
        child_path(document_path, tail_start + 25));
    const std::size_t tail_available = document_info.items.size() - tail_start;
    if (tail_available < 29 || static_cast<std::size_t>(typed_count) > tail_available - 29)
        fail("OOF1114", child_path(document_path, tail_start + 25),
            "bounded format table", std::to_string(typed_count),
            "Spreadsheet Document format count exceeds its record");
    if (typed_cell_indices.empty() != (typed_count == 0))
        fail("OOF1114", child_path(document_path, tail_start + 25),
            "format records exactly when typed cells exist", std::to_string(typed_count),
            "Spreadsheet Document format table does not match typed cells");
    const std::size_t domain_count_index = tail_start + 28 + static_cast<std::size_t>(typed_count);
    const auto domain_count = integer_atom<std::uint32_t>(at(document_info, domain_count_index, document_path),
        child_path(document_path, domain_count_index));
    if (static_cast<std::size_t>(domain_count) > tail_available - 29 - static_cast<std::size_t>(typed_count))
        fail("OOF1114", child_path(document_path, domain_count_index),
            "bounded ValueType table", std::to_string(domain_count),
            "Spreadsheet Document ValueType count exceeds its record");
    if (typed_cell_indices.empty() != (domain_count == 0))
        fail("OOF1114", child_path(document_path, domain_count_index),
            "ValueType records exactly when typed cells exist", std::to_string(domain_count),
            "Spreadsheet Document ValueType table does not match typed cells");
    std::vector<std::uint32_t> format_domain_references;
    format_domain_references.reserve(typed_count);
    for (std::size_t index = 0; index < static_cast<std::size_t>(typed_count); ++index) {
        const std::size_t reference_index = tail_start + 26 + index;
        const auto& reference = at(document_info, reference_index, document_path);
        const auto reference_path = child_path(document_path, reference_index);
        require_arity(reference, 4, reference_path);
        require_raw_constant(reference.items[0], "46137344", child_path(reference_path, 0));
        require_raw_constant(reference.items[1], "1", child_path(reference_path, 1));
        const auto domain_reference = integer_atom<std::uint32_t>(reference.items[2], child_path(reference_path, 2));
        if (domain_reference >= domain_count)
            fail("OOF1114", child_path(reference_path, 2), "in-range zero-based ValueType reference",
                std::to_string(domain_reference), "Spreadsheet Document format references a missing ValueType");
        require_raw_constant(reference.items[3], "0", child_path(reference_path, 3));
        format_domain_references.push_back(domain_reference);
    }
    std::vector<model::TypeDomainPatternValue> types;
    types.reserve(domain_count);
    std::vector<bool> used_domain(domain_count, false);
    for (std::size_t index = 0; index < static_cast<std::size_t>(domain_count); ++index) {
        const std::size_t type_index = domain_count_index + 1 + index;
        types.push_back(type_domain(at(document_info, type_index, document_path),
            child_path(document_path, type_index)));
    }
    std::vector<bool> used_format(typed_count, false);
    for (std::size_t index = 0; index < typed_cell_indices.size(); ++index) {
        const auto format_reference = typed_format_references[index];
        if (format_reference == 0 || format_reference > typed_count)
            fail("OOF1114", child_path(document_path, typed_cell_record_indices[index]),
                "in-range one-based format reference", std::to_string(format_reference),
                "Spreadsheet Document cell references a missing format record");
        const std::size_t format_index = static_cast<std::size_t>(format_reference - 1);
        used_format[format_index] = true;
        const std::size_t domain_reference = format_domain_references[format_index];
        used_domain[domain_reference] = true;
        const std::size_t type_index = domain_count_index + 1 + domain_reference;
        const auto& type = types[domain_reference];
        auto& typed = *payload.cells[typed_cell_indices[index]].typed_value;
        if (type.entries.size() != 1)
            fail("OOF1114", child_path(document_path, type_index), "one ValueType entry for a typed cell",
                describe(at(document_info, type_index, document_path)),
                "Spreadsheet Document typed cell requires exactly one ValueType entry");
        if (typed_cell_defaults[index]) {
            const auto& entry = type.entries.front();
            switch (entry.term) {
                case model::TypeDomainTerm::string:
                    typed.value = std::string{};
                    break;
                case model::TypeDomainTerm::numeric:
                    typed.value = model::DecimalValue{"0"};
                    break;
                case model::TypeDomainTerm::boolean:
                    typed.value = false;
                    break;
                case model::TypeDomainTerm::date:
                    if (entry.date != model::DateQualifiers{true, true})
                        fail("OOF1114", child_path(document_path, type_index), "known DateTime default",
                            describe(at(document_info, type_index, document_path)),
                            "Spreadsheet Document date default is unsupported");
                    typed.value = model::DateValue{"0001-01-01T00:00:00"};
                    break;
                default:
                    fail("OOF1114", child_path(document_path, type_index),
                        "String, Number, Boolean, or Date default", describe(at(document_info, type_index, document_path)),
                        "Spreadsheet Document default cell ValueType is unsupported");
            }
        } else {
            const auto& entry = type.entries.front();
            const bool matches = (entry.term == model::TypeDomainTerm::string && std::holds_alternative<std::string>(typed.value)) ||
                (entry.term == model::TypeDomainTerm::numeric && std::holds_alternative<model::DecimalValue>(typed.value)) ||
                (entry.term == model::TypeDomainTerm::boolean && std::holds_alternative<bool>(typed.value)) ||
                (entry.term == model::TypeDomainTerm::date && std::holds_alternative<model::DateValue>(typed.value));
            if (!matches)
                fail("OOF1114", child_path(document_path, type_index), "ValueType matching explicit cell value",
                    describe(at(document_info, type_index, document_path)),
                    "Spreadsheet Document cell value conflicts with its ValueType");
        }
        typed.type = type;
        const auto& cell_record = document_info.items.at(typed_cell_record_indices[index]);
        if ((integer_atom<std::uint32_t>(cell_record.items[0], document_path) & 1) != 0)
            payload.cells[typed_cell_indices[index]].control = decoded_spreadsheet_cell_control(
                cell_record.items[3], type, child_path(document_path, typed_cell_record_indices[index]));
    }
    if (std::ranges::find(used_format, false) != used_format.end() ||
        std::ranges::find(used_domain, false) != used_domain.end())
        fail("OOF1114", child_path(document_path, tail_start + 25),
            "format and ValueType records referenced by typed cells", "unreferenced table entry",
            "Spreadsheet Document typed-value table contains unused records");
    const auto view_path = child_path(info_path, 14);
    const auto& view_info = at(info, 14, info_path);
    require_list(view_info, view_path);
    if (view_info.items.size() != 29 && view_info.items.size() != 30)
        fail("OOF1114", view_path, "fresh Add or normalized R1C1 default envelope", describe(view_info),
            "SpreadsheetDocumentField cannot persist nondefault view settings");
    const auto area_count = integer_atom<std::uint32_t>(view_info.items.at(23), child_path(view_path, 23));
    bool fresh_add_default = false;
    if (view_info.items.size() == 29 && area_count == 0) {
        fresh_add_default = true;
    } else if (view_info.items.size() == 30 && area_count == 1) {
        fresh_add_default = false;
    } else {
        fail("OOF1114", view_path, "fresh Add or normalized R1C1 default view envelope",
            std::to_string(view_info.items.size()) + " fields with " + std::to_string(area_count) + " areas",
            "SpreadsheetDocumentField cannot persist nondefault view settings");
    }
    const LV expected_info = canonical_spreadsheet_field_info(payload, fresh_add_default);
    LV normalized_info = info;
    auto& normalized_document_info = normalized_info.items.at(11);
    const auto& expected_document_info = expected_info.items.at(11);
    for (std::size_t index = 0; index < typed_cell_record_indices.size(); ++index) {
        auto& cell_record = normalized_document_info.items.at(typed_cell_record_indices[index]);
        cell_record.items.at(1) = raw(std::to_string(index + 1));
        if (payload.cells[typed_cell_indices[index]].control.has_value())
            cell_record.items.at(3) = encoded_spreadsheet_cell_control(payload.cells[typed_cell_indices[index]], document_path);
    }
    const auto expected_format_count = integer_atom<std::uint32_t>(
        expected_document_info.items.at(tail_start + 25), child_path(document_path, tail_start + 25));
    const auto expected_domain_count_index = tail_start + 28 + static_cast<std::size_t>(expected_format_count);
    const auto expected_domain_count = integer_atom<std::uint32_t>(
        expected_document_info.items.at(expected_domain_count_index), child_path(document_path, expected_domain_count_index));
    const std::size_t input_format_end = tail_start + 26 + static_cast<std::size_t>(typed_count);
    const std::size_t input_interstitial_end = tail_start + 28 + static_cast<std::size_t>(typed_count);
    const std::size_t input_domain_end = tail_start + 29 + static_cast<std::size_t>(typed_count) +
        static_cast<std::size_t>(domain_count);
    const std::size_t expected_format_end = tail_start + 26 + static_cast<std::size_t>(expected_format_count);
    const std::size_t expected_domain_end = tail_start + 29 + static_cast<std::size_t>(expected_format_count) +
        static_cast<std::size_t>(expected_domain_count);
    if (input_domain_end > normalized_document_info.items.size() ||
        expected_domain_end > expected_document_info.items.size() || input_format_end > input_interstitial_end ||
        expected_format_end > expected_domain_count_index) {
        fail("OOF1114", document_path, "canonical typed-value table placement", std::to_string(tail_start),
            "Spreadsheet Document typed-value table cannot be normalized");
    }
    std::vector<LV> normalized_document_items;
    normalized_document_items.reserve(normalized_document_info.items.size());
    normalized_document_items.insert(normalized_document_items.end(), normalized_document_info.items.begin(),
        normalized_document_info.items.begin() + static_cast<std::ptrdiff_t>(tail_start + 25));
    normalized_document_items.insert(normalized_document_items.end(),
        expected_document_info.items.begin() + static_cast<std::ptrdiff_t>(tail_start + 25),
        expected_document_info.items.begin() + static_cast<std::ptrdiff_t>(expected_format_end));
    normalized_document_items.insert(normalized_document_items.end(),
        normalized_document_info.items.begin() + static_cast<std::ptrdiff_t>(input_format_end),
        normalized_document_info.items.begin() + static_cast<std::ptrdiff_t>(input_interstitial_end));
    normalized_document_items.insert(normalized_document_items.end(),
        expected_document_info.items.begin() + static_cast<std::ptrdiff_t>(expected_domain_count_index),
        expected_document_info.items.begin() + static_cast<std::ptrdiff_t>(expected_domain_end));
    normalized_document_items.insert(normalized_document_items.end(),
        normalized_document_info.items.begin() + static_cast<std::ptrdiff_t>(input_domain_end),
        normalized_document_info.items.end());
    normalized_document_info.items = std::move(normalized_document_items);
    require_exact(normalized_info, expected_info, info_path,
        fresh_add_default
            ? "SpreadsheetDocumentField fresh Add record contains an unsupported setting or storage variation"
            : "SpreadsheetDocumentField contains an unsupported nondefault view setting or storage variation");

    auto geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty", "Control name is required");
    require_exact(metadata, list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path, "SpreadsheetDocumentField metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5), "SpreadsheetDocumentField cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::SpreadsheetDocumentFieldPayload{}};
    std::get<model::SpreadsheetDocumentFieldPayload>(control.payload) = std::move(payload);
    control.position = std::move(geometry.position);
    return {std::move(control), std::nullopt, std::move(geometry.incoming), std::nullopt, {}};
}

LV encode_spreadsheet_document_field(
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::spreadsheet_document_field || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        !control.events.empty() || control.data_path || !control.extension_properties.empty() || !control.children.empty()) {
        fail("OOF1122", "$", "plain SpreadsheetDocumentField with no events, binding, extensions, or children",
            control.name, "Control uses an unsupported storage concept");
    }
    if (!control.properties().empty())
        fail("OOF1122", "$/SpreadsheetDocumentField", "no additional control properties", "present", "SpreadsheetDocumentField property is outside the supported profile");
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::spreadsheet_document_field);
    const auto info = canonical_spreadsheet_field_info(std::get<model::SpreadsheetDocumentFieldPayload>(control.payload));
    const auto metadata = list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")});
    return list({raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value())), info,
        encode_geometry(control.position, context, IncomingAnchorLists{}), metadata, list({raw("0")})});
}

DecodedControl decode_input_field(
    const LV& record,
    std::string_view path,
    const AttributeRecord& linked_attribute,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::input_field);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0) {
        fail("OOF1122", child_path(path, 1), "positive InputField ID", "0", "InputField ID is invalid");
    }

    const auto& info = record.items[2];
    const auto info_path = child_path(path, 2);
    require_arity(info, 10, info_path);
    require_raw_constant(info.items[0], "9", child_path(info_path, 0));
    const auto control_type = type_domain(info.items[1], child_path(info_path, 1));
    if (!is_single_string_type_domain(control_type) || control_type != linked_attribute.type) {
        fail(
            "OOF1122",
            child_path(info_path, 1),
            "single-string InputField TypeDomainPattern matching linked Attribute",
            describe(info.items[1]),
            "InputField type must match its linked attribute");
    }
    const auto& control_info = info.items[2];
    const auto control_info_path = child_path(info_path, 2);
    require_arity(control_info, 1, control_info_path);
    const auto& payload = at(control_info, 0, control_info_path);
    const auto payload_path = child_path(control_info_path, 0);
    require_arity(payload, 46, payload_path);
    const auto& base_info = at(payload, 0, payload_path);
    const auto base_info_path = child_path(payload_path, 0);
    require_arity(base_info, 21, base_info_path);
    const bool enabled = bool_atom(base_info.items[1], child_path(base_info_path, 1));
    const std::string tool_tip = decoded_single_language_text(
        base_info.items[12], child_path(base_info_path, 12));
    const bool read_only = bool_atom(payload.items[13], child_path(payload_path, 13));
    const std::string format = decoded_single_language_text(
        payload.items[34], child_path(payload_path, 34));
    const auto horizontal_align = integer_atom<std::int32_t>(
        payload.items[17], child_path(payload_path, 17));
    if (horizontal_align < 0 || horizontal_align > 4) {
        fail("OOF1114", child_path(payload_path, 17), "HorizontalAlign storage value 0 through 4",
            std::to_string(horizontal_align), "InputField.HorizontalAlign storage value is unsupported");
    }
    const auto vertical_align = integer_atom<std::int32_t>(
        payload.items[18], child_path(payload_path, 18));
    if (vertical_align < 0 || vertical_align > 2) {
        fail("OOF1114", child_path(payload_path, 18), "VerticalAlign storage value 0 through 2",
            std::to_string(vertical_align), "InputField.VerticalAlign storage value is unsupported");
    }
    const auto choice_list_height = integer_atom<std::int32_t>(
        payload.items[31], child_path(payload_path, 31));
    const auto input_field_flags = decode_input_field_flags(info, info_path);
    const InputFieldTextValues text_values{tool_tip, format};
    const InputFieldLayoutValues layout_values{horizontal_align, vertical_align, choice_list_height};
    require_exact(
        info,
        canonical_input_field_info(control_type, enabled, read_only, input_field_flags, text_values, layout_values),
        info_path,
        "InputField uses an unsupported property, event, or storage variation");

    const auto geometry_path = child_path(path, 3);
    auto decoded_geometry = decode_geometry(record.items[3], geometry_path, context);
    const auto& metadata = record.items[4];
    const auto metadata_path = child_path(path, 4);
    require_arity(metadata, 6, metadata_path);
    require_raw_constant(metadata.items[0], "14", child_path(metadata_path, 0));
    const auto name = string_atom(metadata.items[1], child_path(metadata_path, 1));
    if (name.empty()) {
        fail("OOF1115", child_path(metadata_path, 1), "non-empty control name", "empty", "Control name is required");
    }
    require_exact(
        metadata,
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        metadata_path,
        "InputField metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5), "InputField cannot contain storage children");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::InputFieldPayload{}};
    if (!enabled) control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    if (!tool_tip.empty()) control.properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    if (read_only) control.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
    if (!format.empty()) control.properties().set_explicit(model::PropertyId::from_name("Format"), format);
    if (horizontal_align != 4) {
        constexpr std::array<std::string_view, 5> members{"Left", "Center", "Right", "Justify", "Auto"};
        control.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
            model::EnumerationValue{"HorizontalAlign", std::string(members[static_cast<std::size_t>(horizontal_align)])});
    }
    if (vertical_align != 0) {
        constexpr std::array<std::string_view, 3> members{"Top", "Center", "Bottom"};
        control.properties().set_explicit(model::PropertyId::from_name("VerticalAlign"),
            model::EnumerationValue{"VerticalAlign", std::string(members[static_cast<std::size_t>(vertical_align)])});
    }
    if (choice_list_height != 0) {
        control.properties().set_explicit(model::PropertyId::from_name("ChoiceListHeight"),
            static_cast<std::int64_t>(choice_list_height));
    }
    for (std::size_t index = 0; index < input_field_flag_mappings.size(); ++index) {
        if (input_field_flags[index] != input_field_flag_mappings[index].default_value) {
            control.properties().set_explicit(
                model::PropertyId::from_name(input_field_flag_mappings[index].name), input_field_flags[index]);
        }
    }
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming), std::nullopt, {}};
}

model::TableColumn decode_table_column(const LV& value, std::string_view path) {
    require_arity(value, 2, path);
    require_raw_constant(value.items[0], "737535a4-21e6-4971-8513-3e3173a9fedd", child_path(path, 0));
    const auto body_path = child_path(path, 1);
    const auto& body = value.items[1];
    require_arity(body, 6, body_path);
    require_raw_constant(body.items[0], "8", child_path(body_path, 0));
    const auto info_path = child_path(body_path, 1);
    const auto& info = body.items[1];
    require_arity(info, 5, info_path);
    require_raw_constant(info.items[0], "8", child_path(info_path, 0));
    const auto properties_path = child_path(info_path, 1);
    const auto& properties = info.items[1];
    require_arity(properties, 52, properties_path);

    model::TableColumn column;
    column.name = string_atom(properties.items[30], child_path(properties_path, 30));
    column.data_path = string_atom(body.items[2], child_path(body_path, 2));
    try {
        list_stream::ListInStream localized(properties.items[1]);
        column.header = value_codec::read_localized_string(localized);
    } catch (const std::exception& error) {
        fail("OOF1108", child_path(properties_path, 1), "LocalizedString Header", describe(properties.items[1]), error.what());
    }
    model::ControlKind editor_kind;
    const auto& editor_guid = properties.items[38];
    if (!editor_guid.is_list && editor_guid.atom ==
        model::metamodel::descriptor_for(model::ControlKind::input_field).guid) {
        editor_kind = model::ControlKind::input_field;
    } else if (!editor_guid.is_list && editor_guid.atom ==
        model::metamodel::descriptor_for(model::ControlKind::choice_field).guid) {
        editor_kind = model::ControlKind::choice_field;
    } else if (!editor_guid.is_list && editor_guid.atom ==
        model::metamodel::descriptor_for(model::ControlKind::check_box).guid) {
        editor_kind = model::ControlKind::check_box;
    } else {
        fail("OOF1122", child_path(properties_path, 38), "InputField, ChoiceField, or CheckBox GUID",
            describe(editor_guid), "Table Column editor kind is unsupported");
    }
    static_cast<void>(type_domain(properties.items[35], child_path(properties_path, 35)));
    const auto inflated = decode_table_column_editor_packet(
        properties.items[39], child_path(properties_path, 39));
    constexpr std::size_t envelope_prefix_size = sizeof(std::uint64_t) + 3;
    const std::string_view editor_text(
        reinterpret_cast<const char*>(inflated.data() + envelope_prefix_size),
        inflated.size() - envelope_prefix_size);
    LV editor_info;
    try {
        editor_info = list_stream::parse(editor_text);
    } catch (const std::exception& error) {
        fail("OOF1114", child_path(properties_path, 39), "valid embedded Table Column editor ListStream", {}, error.what());
    }
    column.control.kind = editor_kind;
    const auto editor_path = child_path(properties_path, 39);
    if (editor_kind == model::ControlKind::input_field) {
        require_arity(editor_info, 10, editor_path);
        require_arity(editor_info.items[2], 1, child_path(editor_path, 2));
        const auto payload_path = editor_path + "/payload";
        const auto base_path = editor_path + "/base";
        require_arity(editor_info.items[2].items[0], 46, payload_path);
        require_arity(editor_info.items[2].items[0].items[0], 21, base_path);
        const bool enabled = bool_atom(editor_info.items[2].items[0].items[0].items[1], editor_path);
        const bool read_only = bool_atom(editor_info.items[2].items[0].items[13], editor_path);
        if (!enabled || read_only) {
            fail("OOF1122", editor_path, "Enabled=true and ReadOnly=false",
                enabled ? "ReadOnly=true" : "Enabled=false",
                "Table Column InputField is outside its persisted profile");
        }
    }
    require_exact(editor_info, canonical_table_column_editor_info(column.control), editor_path,
        "embedded Table Column editor is outside its typed default property profile");
    LV normalized = value;
    const LV canonical = canonical_table_column_record(column, path);
    normalized.items[1].items[1].items[1].items[39] = canonical.items[1].items[1].items[1].items[39];
    require_exact(normalized, canonical, path,
        "Table Column contains an unsupported property, event, or storage variation");
    return column;
}

DecodedControl decode_table(
    const LV& record,
    std::string_view path,
    const AttributeRecord& linked_attribute,
    const GeometryContext& context) {
    require_arity(record, 6, path);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::table);
    require_raw_constant(record.items[0], descriptor.guid, child_path(path, 0));
    const auto raw_id = integer_atom<std::uint64_t>(record.items[1], child_path(path, 1));
    if (raw_id == 0 || raw_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", child_path(path, 1), "positive int64 Table ID", std::to_string(raw_id), "Table ID is invalid");
    }
    if (!is_single_value_table_type_domain(linked_attribute.type)) {
        fail("OOF1122", child_path(path, 1), "DataPath to an unqualified ValueTable Attribute",
            linked_attribute.name, "Table DataPath target must have the named ValueTable type");
    }
    const auto info_path = child_path(path, 2);
    const auto& info = record.items[2];
    require_arity(info, 5, info_path);
    require_raw_constant(info.items[0], "5", child_path(info_path, 0));
    const auto stored_type = type_domain(info.items[1], child_path(info_path, 1));
    if (stored_type != linked_attribute.type) {
        fail("OOF1122", child_path(info_path, 1), "Table ValueType matching linked ValueTable Attribute",
            describe(info.items[1]), "Table DataPath and ValueType disagree");
    }
    require_arity(info.items[2], 2, child_path(info_path, 2));
    require_arity(info.items[2].items[1], 39, child_path(child_path(info_path, 2), 1));
    const auto columns_path = child_path(child_path(child_path(info_path, 2), 1), 23);
    const auto& stored_columns = info.items[2].items[1].items[23];
    require_list(stored_columns, columns_path);
    if (stored_columns.items.empty() || stored_columns.items.size() < 2 || stored_columns.items.size() > 257 ||
        integer_atom<std::size_t>(stored_columns.items[0], columns_path) != stored_columns.items.size() - 1) {
        fail("OOF1114", columns_path, "matching Table Column count from 1 to 256",
            std::to_string(stored_columns.items.size()), "Table Column collection is malformed");
    }
    auto geometry = decode_geometry(record.items[3], child_path(path, 3), context);
    const auto metadata_path = child_path(path, 4);
    require_arity(record.items[4], 6, metadata_path);
    require_raw_constant(record.items[4].items[0], "14", child_path(metadata_path, 0));
    const std::string name = string_atom(record.items[4].items[1], child_path(metadata_path, 1));
    if (name.empty()) fail("OOF1115", child_path(metadata_path, 1), "non-empty Table Name", "empty", "Table Name is required");

    model::ControlNode control{model::ObjectId{raw_id}, name, model::TablePayload{}};
    control.data_path = model::DataPath{model::AttributeRef{
        model::ObjectId{static_cast<std::uint64_t>(linked_attribute.id.object_id)}}, {}};
    control.position = geometry.position;
    const bool first_in_group = bool_atom(record.items[4].items[5], child_path(metadata_path, 5));
    if (first_in_group) {
        control.extension_properties.set_explicit(model::PropertyId::from_name("FirstInGroup"), true);
    }
    auto& table = std::get<model::TablePayload>(control.payload);
    table.columns.reserve(stored_columns.items.size() - 1);
    for (std::size_t index = 1; index < stored_columns.items.size(); ++index) {
        table.columns.push_back(decode_table_column(stored_columns.items[index], child_path(columns_path, index)));
    }
    LV normalized_info = info;
    const auto flags = integer_atom<std::uint32_t>(info.items[2].items[1].items[1],
        child_path(child_path(child_path(info_path, 2), 1), 1));
    const bool read_only = (flags & 0x400U) == 0;
    if (!read_only) control.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), false);
    const LV canonical_info = canonical_table_control_info(linked_attribute.type, table.columns, read_only, info_path);
    auto& normalized_columns = normalized_info.items[2].items[1].items[23];
    const auto& canonical_columns = canonical_info.items[2].items[1].items[23];
    for (std::size_t index = 1; index < normalized_columns.items.size(); ++index) {
        normalized_columns.items[index].items[1].items[1].items[1].items[39] =
            canonical_columns.items[index].items[1].items[1].items[1].items[39];
    }
    require_exact(normalized_info, canonical_info, info_path,
        "Table contains a property or storage variation outside the typed profile");
    require_exact(record.items[4],
        list({raw("14"), string_value(name), raw("4294967295"), raw("0"), raw("0"), raw(first_in_group ? "1" : "0")}),
        metadata_path, "Table metadata record is unsupported");
    require_exact(record.items[5], list({raw("0")}), child_path(path, 5),
        "Table cannot contain storage children");
    return {std::move(control), std::nullopt, std::move(geometry.incoming), std::nullopt, {}};
}

bool explicit_bool(const model::PropertySet& properties, std::string_view name, bool default_value) {
    const auto* value = properties.find(model::PropertyId::from_name(name));
    if (value == nullptr) {
        return default_value;
    }
    if (!std::holds_alternative<bool>(value->value)) {
        fail("OOF1121", "$", "Boolean property", "different value kind", "Storage encoder received an invalid property value");
    }
    return std::get<bool>(value->value);
}

std::string explicit_string(
    const model::PropertySet& properties,
    std::string_view name,
    std::string_view default_value) {
    const auto* value = properties.find(model::PropertyId::from_name(name));
    if (value == nullptr) {
        return std::string(default_value);
    }
    if (!std::holds_alternative<std::string>(value->value)) {
        fail("OOF1121", "$", "String property", "different value kind", "Storage encoder received an invalid property value");
    }
    return std::get<std::string>(value->value);
}

std::int32_t explicit_integer(
    const model::PropertySet& properties,
    std::string_view name,
    std::int32_t default_value,
    std::string_view property_path = "$") {
    const auto* value = properties.find(model::PropertyId::from_name(name));
    if (value == nullptr) {
        return default_value;
    }
    std::int64_t parsed = 0;
    if (std::holds_alternative<std::int64_t>(value->value)) {
        parsed = std::get<std::int64_t>(value->value);
    } else if (std::holds_alternative<model::DecimalValue>(value->value)) {
        const auto& decimal = std::get<model::DecimalValue>(value->value).canonical;
        const char* begin = decimal.data();
        const char* end = begin + decimal.size();
        const auto result = std::from_chars(begin, end, parsed, 10);
        if (decimal.empty() || result.ec != std::errc{} || result.ptr != end) {
            fail("OOF1121", std::string(property_path), "integral decimal property", decimal,
                "Storage integer property must be integral");
        }
    } else {
        fail("OOF1121", std::string(property_path), "integer property", "different value kind",
            "Storage encoder received an invalid property value");
    }
    if (parsed < std::numeric_limits<std::int32_t>::min() ||
        parsed > std::numeric_limits<std::int32_t>::max()) {
        fail("OOF1120", std::string(property_path), "int32 property", std::to_string(parsed),
            "Storage integer is out of range");
    }
    return static_cast<std::int32_t>(parsed);
}

std::int32_t explicit_enum_storage_value(
    const model::PropertySet& values,
    std::string_view owner,
    std::string_view name,
    std::string_view expected_type,
    std::int32_t default_value,
    std::initializer_list<std::pair<std::string_view, std::int32_t>> members) {
    const auto* entry = values.find(model::PropertyId::from_name(name));
    if (entry == nullptr) return default_value;
    const auto path = std::string("$/") + std::string(owner) + "/" + std::string(name);
    if (!std::holds_alternative<model::EnumerationValue>(entry->value)) {
        fail("OOF1122", path, "EnumerationValue of " + std::string(expected_type), "non-enumeration",
            std::string(owner) + " enumeration has the wrong value type");
    }
    const auto& value = std::get<model::EnumerationValue>(entry->value);
    if (value.type_name != expected_type) {
        fail("OOF1122", path, std::string(expected_type) + " enumeration",
            value.type_name + "." + value.member, std::string(owner) + " enumeration type is unsupported");
    }
    for (const auto& [member, storage_value] : members) {
        if (value.member == member) return storage_value;
    }
    fail("OOF1122", path, std::string(expected_type) + " supported member", value.member,
        std::string(owner) + " enum member is unsupported");
}

void require_allowed_properties(
    const model::PropertySet& properties,
    std::initializer_list<std::string_view> names,
    std::string_view path) {
    std::unordered_set<model::PropertyId, model::PropertyIdHash> allowed;
    for (const auto name : names) {
        allowed.insert(model::PropertyId::from_name(name));
    }
    properties.for_each_explicit([&](const model::PropertyEntry& entry) {
        if (!allowed.contains(entry.id)) {
            fail(
                "OOF1122",
                std::string(path),
                "property with a proven storage codec",
                std::to_string(entry.id.value()),
                "The document contains a property outside the executable storage slice");
        }
    });
}

std::optional<std::string_view> button_click_handler(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control) {
    if (control.events.empty()) {
        return std::nullopt;
    }
    if (control.events.size() != 1) {
        fail("OOF1122", "$", "zero or one Button.Click event", std::to_string(control.events.size()), "Button has unsupported events");
    }
    const auto* event = document.find_event(control.events.front().id());
    if (event == nullptr || event->name != "Click" || event->handler.empty() ||
        !std::holds_alternative<model::ControlRef>(event->owner) ||
        std::get<model::ControlRef>(event->owner).id() != control.id) {
        fail("OOF1122", "$", "owned Button.Click event", "different event", "Button event cannot be encoded");
    }
    return event->handler;
}

LV encode_button(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::button || control.id.value() == 0 ||
        control.id.value() > std::numeric_limits<std::int64_t>::max()) {
        fail("OOF1122", "$", "Button with positive int64 ID", std::to_string(control.id.value()), "Unsupported control record");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() ||
        control.position.collapse.is_explicit() || !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$", "plain Button", control.name, "Button uses a storage concept outside the executable slice");
    }
    require_allowed_properties(control.properties(),
        {"Caption", "Enabled", "MultiLine", "ToolTip", "HorizontalAlign", "VerticalAlign",
            "PictureLocation", "PictureSize", "MenuMode", "BorderColor", "ButtonTextColor", "ButtonBackColor", "Font", "Shortcut", "Picture"}, "$/Button");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string caption = explicit_string(control.properties(), "Caption");
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const auto border_color = explicit_button_color(control.properties(), "BorderColor");
    const auto button_text_color = explicit_button_color(control.properties(), "ButtonTextColor");
    const auto button_back_color = explicit_button_color(control.properties(), "ButtonBackColor");
    const auto font = explicit_control_font(control.properties(), "$/Button/Font");
    const model::PictureAsset* picture_asset = nullptr;
    const model::metamodel::StandardPictureDescriptor* standard_picture = nullptr;
    if (const auto* picture_entry = control.properties().find(model::PropertyId::from_name("Picture"))) {
        if (!std::holds_alternative<model::PictureRef>(picture_entry->value)) {
            fail("OOF1122", "$/Button/Picture", "PictureRef", "different value type", "Button.Picture has the wrong value type");
        }
        const auto& reference = std::get<model::PictureRef>(picture_entry->value);
        if (reference.standard_name) {
            if (reference.asset.id().value() != 0) {
                fail("OOF1122", "$/Button/Picture", "standard picture with empty asset ID",
                    std::to_string(reference.asset.id().value()), "Picture reference has conflicting targets");
            }
            standard_picture = model::metamodel::find_standard_picture(reference.standard_name->value);
            if (standard_picture == nullptr) fail("OOF1122", "$/Button/Picture", "known PictureLib name",
                reference.standard_name->value, "Standard picture name is unsupported");
        } else {
            if (reference.asset.id().value() == 0) {
                fail("OOF1122", "$/Button/Picture", "positive asset ID", "0", "Picture asset reference is empty");
            }
            picture_asset = document.find_asset(reference.asset.id());
            if (picture_asset == nullptr) {
                fail("OOF1123", "$/Button/Picture", "existing PictureAsset", "missing", "Button picture reference is dangling");
            }
        }
    }
    const auto horizontal_align = explicit_enum_storage_value(control.properties(), "Button", "HorizontalAlign",
        "HorizontalAlign", 1, {{"Left", 0}, {"Center", 1}, {"Right", 2}});
    const auto vertical_align = explicit_enum_storage_value(control.properties(), "Button", "VerticalAlign",
        "VerticalAlign", 1, {{"Top", 0}, {"Center", 1}, {"Bottom", 2}});
    const auto picture_location = explicit_enum_storage_value(control.properties(), "Button", "PictureLocation",
        "PictureLocation", 0, {{"Left", 0}, {"Right", 1}});
    const auto picture_size = explicit_enum_storage_value(control.properties(), "Button", "PictureSize", "PictureSize", 0,
        {{"RealSize", 0}, {"Stretch", 1}, {"Proportionally", 2}, {"Tile", 3},
            {"AutoSize", 4}, {"ByFontSize", 7}});
    const auto menu_mode = explicit_enum_storage_value(control.properties(), "Button", "MenuMode", "MenuMode", 0,
        {{"DontUse", 0}, {"Use", 1}, {"UseExtra", 2}});
    const bool multi_line = explicit_bool(control.properties(), "MultiLine", false);
    const auto shortcut = explicit_button_shortcut(control.properties());
    const auto handler = button_click_handler(document, control);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::button);
    auto button_properties = canonical_button_properties(enabled, caption, horizontal_align, vertical_align,
        picture_location, picture_size, menu_mode, multi_line, tool_tip,
        border_color, button_text_color, button_back_color, font, shortcut);
    if (picture_asset != nullptr) button_properties.items[8] = encode_button_picture(*picture_asset, "$/Button/Picture");
    const auto& menu_entries = std::get<model::ButtonPayload>(control.payload).buttons;
    if (!menu_entries.empty()) {
        if (menu_mode == 0) fail("OOF1122", "$/Button/Buttons", "enabled MenuMode", "DontUse", "Menu entries require MenuMode");
        button_properties.items[12] = encode_button_menu(menu_entries, document, control.id.value());
    }
    if (standard_picture != nullptr) button_properties.items[8] = encode_standard_button_picture(*standard_picture);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        list({
            raw("1"),
            std::move(button_properties),
            canonical_event_table(handler),
        }),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({
            raw("14"),
            string_value(control.name),
            raw("4294967295"),
            raw("0"),
            raw("0"),
            raw("0"),
        }),
        list({raw("0")}),
    });
}

LV encode_command_bar(const model::OrdinaryFormDocument& document, const model::ControlNode& control,
                      const GeometryContext& context) {
    if (control.kind() != model::ControlKind::command_bar || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        fail("OOF1122", "$/CommandBar", "CommandBar with positive int64 ID", std::to_string(control.id.value()),
            "Unsupported CommandBar record");
    if (!control.events.empty() || control.data_path || !control.children.empty())
        fail("OOF1122", "$/CommandBar", "CommandBar without events, DataPath, or child controls",
            control.name, "CommandBar uses a storage concept outside the supported profile");
    require_allowed_properties(control.properties(), {"Enabled", "ToolTip", "Secondary", "BorderColor", "ButtonTextColor", "BackColor", "Border", "AutoFill", "Transparent", "ButtonBackColor", "Orientation", "ButtonsAlignment"}, "$/CommandBar");
    require_allowed_properties(control.extension_properties, {"ActionSource"}, "$/CommandBar");
    std::uint32_t action_source = std::numeric_limits<std::uint32_t>::max();
    if (const auto* entry = control.extension_properties.find(model::PropertyId::from_name("ActionSource"))) {
        if (const auto* form = std::get_if<model::FormRef>(&entry->value)) {
            if (form->id() != document.form().id)
                fail("OOF1122", "$/CommandBar/ActionSource", "reference to this Form", "unknown Form", "ActionSource Form reference is invalid");
            action_source = 0;
        } else if (const auto* source = std::get_if<model::ControlRef>(&entry->value)) {
            if (source->id().value() == 0 || source->id().value() >= std::numeric_limits<std::uint32_t>::max() ||
                document.find_control(source->id()) == nullptr)
                fail("OOF1122", "$/CommandBar/ActionSource", "existing control with ID below UINT32_MAX", "invalid reference", "ActionSource control reference is invalid");
            const auto kind = document.find_control(source->id())->kind();
            if (kind != model::ControlKind::table && kind != model::ControlKind::html_document_field)
                fail("OOF1122", "$/CommandBar/ActionSource", "Table or HTMLDocumentField source", "unsupported control kind", "Control is not a command source");
            action_source = static_cast<std::uint32_t>(source->id().value());
        } else if (!std::holds_alternative<model::UndefinedValue>(entry->value)) {
            fail("OOF1122", "$/CommandBar/ActionSource", "Undefined, FormRef, or ControlRef", "different value type", "ActionSource has the wrong value type");
        }
    }
    const auto* payload = std::get_if<model::CommandBarPayload>(&control.payload);
    if (payload == nullptr) fail("OOF1122", "$/CommandBar", "CommandBarPayload", "different payload", "CommandBar payload is invalid");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const auto tool_tip = explicit_string(control.properties(), "ToolTip");
    const auto border_color = explicit_button_color(control.properties(), "BorderColor", "CommandBar");
    const auto button_text_color = explicit_button_color(control.properties(), "ButtonTextColor", "CommandBar");
    const auto back_color = control.properties().contains(model::PropertyId::from_name("BackColor"))
        ? explicit_button_color(control.properties(), "BackColor", "CommandBar") : model::ColorValue{};
    const auto button_back_color = control.properties().contains(model::PropertyId::from_name("ButtonBackColor"))
        ? explicit_button_color(control.properties(), "ButtonBackColor", "CommandBar") : model::ColorValue{};
    const auto auto_fill = explicit_bool(control.properties(), "AutoFill", false);
    const auto transparent = explicit_bool(control.properties(), "Transparent", false);
    const auto orientation = explicit_enum_storage_value(control.properties(), "CommandBar", "Orientation", "Orientation", 2,
        {{"Auto", 2}, {"Horizontal", 1}, {"Vertical", 0}});
    const auto buttons_alignment = explicit_enum_storage_value(control.properties(), "CommandBar", "ButtonsAlignment", "CommandBarButtonAlignment", 0,
        {{"Left", 0}, {"Center", 1}, {"Right", 2}});
    model::BorderValue border;
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("Border"))) {
        const auto* typed_border = std::get_if<model::BorderValue>(&entry->value);
        if (typed_border == nullptr) fail("OOF1122", "$/CommandBar/Border", "BorderValue", "different value type", "Border has the wrong value type");
        border = *typed_border;
    }
    const auto properties = canonical_command_bar_properties(enabled, tool_tip, payload->buttons, document, control.id.value(),
        explicit_bool(control.properties(), "Secondary", true), &border_color, &button_text_color, &back_color, &border, auto_fill, transparent, orientation, buttons_alignment, &button_back_color);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::command_bar);
    const auto info = list({raw("2"), properties});
    const auto metadata = list({raw("14"), string_value(control.name), raw(std::to_string(action_source)), raw("0"), raw("0"), raw("0")});
    return list({raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value())), info,
        encode_geometry(control.position, context, IncomingAnchorLists{}), metadata, list({raw("0")})});
}

LV encode_picture_decoration(
    const model::ControlNode& control,
    const model::OrdinaryFormDocument& document,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::picture_decoration || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "PictureDecoration with positive int64 ID", control.name,
            "PictureDecoration is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/PictureDecoration", "plain PictureDecoration", control.name,
            "PictureDecoration uses a storage concept outside the executable slice");
    }
    require_allowed_properties(control.properties(), {"Enabled", "ToolTip", "Picture"}, "$/PictureDecoration");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const model::metamodel::StandardPictureDescriptor* standard_picture = nullptr;
    const model::PictureAsset* picture_asset = nullptr;
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("Picture"))) {
        if (!std::holds_alternative<model::PictureRef>(entry->value)) {
            fail("OOF1122", "$/PictureDecoration/Picture", "PictureRef", "different value type",
                "PictureDecoration.Picture has the wrong value type");
        }
        const auto& reference = std::get<model::PictureRef>(entry->value);
        if (reference.standard_name) {
            if (reference.asset.id().value() != 0) {
                fail("OOF1122", "$/PictureDecoration/Picture", "standard picture with empty asset ID",
                    std::to_string(reference.asset.id().value()), "Picture reference has conflicting targets");
            }
            standard_picture = model::metamodel::find_standard_picture(reference.standard_name->value);
            if (standard_picture == nullptr) {
                fail("OOF1122", "$/PictureDecoration/Picture", "known PictureLib name",
                    reference.standard_name->value, "Standard picture name is unsupported");
            }
        } else {
            if (reference.asset.id().value() == 0) {
                fail("OOF1122", "$/PictureDecoration/Picture", "positive asset ID", "0",
                    "Picture asset reference is empty");
            }
            picture_asset = document.find_asset(reference.asset.id());
            if (picture_asset == nullptr) {
                fail("OOF1123", "$/PictureDecoration/Picture", "existing PictureAsset", "missing",
                    "PictureDecoration picture reference is dangling");
            }
        }
    }
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::picture_decoration);
    auto picture_properties = canonical_picture_properties(enabled, tool_tip);
    if (standard_picture != nullptr) {
        picture_properties.items[4].items[2] = encode_standard_button_picture(*standard_picture);
    } else if (picture_asset != nullptr) {
        picture_properties.items[4].items[2] = encode_button_picture(*picture_asset, "$/PictureDecoration/Picture");
    }
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        list({raw("1"), std::move(picture_properties), list({raw("0")})}),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_splitter(const model::ControlNode& control, const GeometryContext& context) {
    if (control.kind() != model::ControlKind::splitter || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "Splitter with positive int64 ID", control.name,
            "Splitter is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/Splitter", "named Splitter without events, DataPath, extensions, or children and plain Position",
            control.name, "Splitter uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"Enabled", "Orientation", "ToolTip", "BorderColor", "BackColor"},
        "$/Splitter");
    if (!std::holds_alternative<model::SplitterPayload>(control.payload)) {
        fail("OOF1122", "$/Splitter", "SplitterPayload", "different payload", "Splitter payload is invalid");
    }
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const std::int32_t orientation = explicit_enum_storage_value(control.properties(), "Splitter", "Orientation",
        "Orientation", 2, {{"Auto", 2}, {"Vertical", 0}, {"Horizontal", 1}});
    const auto back_color = explicit_splitter_color(control.properties(), "BackColor");
    const auto border_color = explicit_splitter_color(control.properties(), "BorderColor");
    const auto properties = canonical_splitter_properties(enabled, orientation, tool_tip, border_color, back_color);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::splitter);
    return list({raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value())),
        list({raw("0"), properties}), encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")})});
}

LV encode_label(const model::ControlNode& control, const GeometryContext& context) {
    if (control.kind() != model::ControlKind::label_decoration ||
        control.id.value() == 0 || control.id.value() > std::numeric_limits<std::int64_t>::max()) {
        fail("OOF1122", "$/Form/ChildItems", "LabelDecoration with positive int64 ID", control.name, "LabelDecoration is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/LabelDecoration", "plain LabelDecoration", control.name, "LabelDecoration uses a storage concept outside the executable slice");
    }
    require_allowed_properties(
        control.properties(), {"Caption", "HorizontalAlign", "Enabled", "ToolTip"}, "$/LabelDecoration");
    const std::string caption = explicit_string(control.properties(), "Caption");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    std::int32_t horizontal_align = 0;
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("HorizontalAlign"))) {
        if (!std::holds_alternative<model::EnumerationValue>(entry->value)) {
            fail("OOF1122", "$/LabelDecoration/HorizontalAlign", "EnumerationValue of HorizontalAlign",
                "non-enumeration", "LabelDecoration.HorizontalAlign has the wrong value type");
        }
        const auto& value = std::get<model::EnumerationValue>(entry->value);
        if (value.type_name != "HorizontalAlign" ||
            (value.member != "Auto" && value.member != "Left" &&
             value.member != "Center" && value.member != "Right")) {
            fail("OOF1122", "$/LabelDecoration/HorizontalAlign",
                "HorizontalAlign Auto, Left, Center, or Right",
                value.type_name + "." + value.member, "LabelDecoration.HorizontalAlign value is unsupported");
        }
        if (value.member == "Auto") horizontal_align = 4;
        else if (value.member == "Left") horizontal_align = 0;
        else if (value.member == "Center") horizontal_align = 1;
        else horizontal_align = 2;
    }
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::label_decoration);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        list({raw("3"), canonical_label_properties(caption, horizontal_align, enabled, tool_tip), list({raw("0")})}),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_progress_bar(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::progress_bar || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "ProgressBar with positive int64 ID", control.name,
            "ProgressBar is outside the supported profile");
    }
    if (control.name.empty() || (control.data_path && !control.data_path->members.empty()) || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/ProgressBar", "named ProgressBar with optional direct DataPath, no ValueType, Events, or storage children",
            control.name, "ProgressBar uses a storage concept outside the supported profile");
    }
    if (control.data_path) {
        const auto* attribute = document.find_attribute(control.data_path->attribute.id());
        if (attribute == nullptr) fail("OOF1123", "$/ProgressBar/DataPath", "existing linked Attribute",
            std::to_string(control.data_path->attribute.id().value()), "ProgressBar DataPath does not resolve");
        if (!(attribute->type.entries.size() == 1 &&
            attribute->type.entries.front().term == model::TypeDomainTerm::numeric &&
            !attribute->type.entries.front().type_uuid)) fail("OOF1122", "$/ProgressBar/DataPath",
            "single numeric Attribute", attribute->name, "ProgressBar DataPath must target a single numeric attribute");
    }
    require_allowed_properties(control.properties(), {"Enabled", "ToolTip", "MaxValue", "MinValue", "Step"}, "$/ProgressBar");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const std::int32_t max_value = explicit_integer(control.properties(), "MaxValue", 100, "$/ProgressBar/MaxValue");
    const std::int32_t min_value = explicit_integer(control.properties(), "MinValue", 0, "$/ProgressBar/MinValue");
    const std::int32_t step = explicit_integer(control.properties(), "Step", 1, "$/ProgressBar/Step");
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::progress_bar);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        list({raw("0"), canonical_progress_bar_info(enabled, tool_tip, max_value, min_value, step)}),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_track_bar(const model::ControlNode& control, const GeometryContext& context) {
    if (control.kind() != model::ControlKind::track_bar || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "TrackBar with positive int64 ID", control.name,
            "TrackBar is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/TrackBar", "named TrackBar without DataPath, Events, or storage children",
            control.name, "TrackBar uses a storage concept outside the supported profile");
    }
    require_allowed_properties(
        control.properties(), {"Enabled", "ToolTip", "MaxValue", "MinValue", "Step"}, "$/TrackBar");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const std::int32_t max_value = explicit_integer(
        control.properties(), "MaxValue", 100, "$/TrackBar/MaxValue");
    const std::int32_t min_value = explicit_integer(
        control.properties(), "MinValue", 0, "$/TrackBar/MinValue");
    const std::int32_t step = explicit_integer(control.properties(), "Step", 1, "$/TrackBar/Step");
    if (max_value < 0) {
        fail("OOF1122", "$/TrackBar/MaxValue", "non-negative TrackBar MaxValue", std::to_string(max_value),
            "TrackBar MaxValue below zero was clamped by the platform runtime");
    }
    if (min_value < 0) {
        fail("OOF1122", "$/TrackBar/MinValue", "non-negative TrackBar MinValue", std::to_string(min_value),
            "TrackBar MinValue values below zero were not accepted by the platform runtime");
    }
    if (step <= 0) {
        fail("OOF1122", "$/TrackBar/Step", "positive TrackBar Step", std::to_string(step),
            "TrackBar Step values at or below zero were not accepted by the platform runtime");
    }
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::track_bar);
    return list({raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value())),
        canonical_track_bar_info(enabled, tool_tip, min_value, max_value, step),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")})});
}

LV encode_list_box(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::list_box || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "ListBox with positive int64 ID", control.name,
            "ListBox is outside the supported profile");
    }
    if (control.name.empty() || !control.data_path || !control.data_path->members.empty() ||
        !control.extension_properties.empty() || !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/ListBox", "named ListBox with direct DataPath and ordinary Position",
            control.name, "ListBox uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(),
        {"Enabled", "ShowPicture", "ShowCheckBox", "ToolTip", "ReadOnly"}, "$/ListBox");
    const auto* attribute = document.find_attribute(control.data_path->attribute.id());
    if (attribute == nullptr) {
        fail("OOF1123", "$/ListBox/DataPath", "existing linked Attribute",
            std::to_string(control.data_path->attribute.id().value()), "ListBox DataPath does not resolve");
    }
    if (attribute->type.entries.size() != 1 ||
        attribute->type.entries.front().term != model::TypeDomainTerm::value_list ||
        attribute->type.entries.front().type_uuid.has_value()) {
        fail("OOF1122", "$/ListBox/DataPath", "single ValueList Attribute", attribute->name,
            "ListBox DataPath must target a single ValueList Attribute");
    }
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const bool show_picture = explicit_bool(control.properties(), "ShowPicture", false);
    const bool show_check_box = explicit_bool(control.properties(), "ShowCheckBox", false);
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const bool read_only = explicit_bool(control.properties(), "ReadOnly", true);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::list_box);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_list_box_info(enabled, show_picture, show_check_box, read_only, tool_tip),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_check_box(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::check_box || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "CheckBox with positive int64 ID", control.name, "CheckBox is outside the supported profile");
    }
    if (control.name.empty() || !control.data_path || !control.data_path->members.empty() ||
        !control.extension_properties.empty() || !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/CheckBox", "named CheckBox with direct DataPath and plain Position", control.name,
            "CheckBox uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"Enabled", "Caption", "ToolTip", "Font"}, "$/CheckBox");
    const auto* attribute = document.find_attribute(control.data_path->attribute.id());
    if (attribute == nullptr) {
        fail("OOF1123", "$/CheckBox/DataPath", "existing linked Attribute",
            std::to_string(control.data_path->attribute.id().value()), "CheckBox DataPath does not resolve");
    }
    if (!is_single_boolean_type_domain(attribute->type)) {
        fail("OOF1122", "$/CheckBox/DataPath", "linked Boolean Attribute", attribute->name,
            "CheckBox DataPath must target a Boolean attribute");
    }
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string caption = explicit_string(control.properties(), "Caption");
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const auto font = explicit_control_font(control.properties(), "$/CheckBox/Font");
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::check_box);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_check_box_info(enabled, caption, tool_tip, &font),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_choice_field(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::choice_field || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "ChoiceField with positive int64 ID", control.name,
            "ChoiceField is outside the supported profile");
    }
    if (control.name.empty() || (control.data_path && !control.data_path->members.empty()) ||
        !control.extension_properties.empty() || !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/ChoiceField", "named ChoiceField with optional direct DataPath and plain Position", control.name,
            "ChoiceField uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"Enabled", "ToolTip"}, "$/ChoiceField");
    if (control.data_path) {
        const auto* attribute = document.find_attribute(control.data_path->attribute.id());
        if (attribute == nullptr) {
            fail("OOF1123", "$/ChoiceField/DataPath", "existing linked Attribute",
                std::to_string(control.data_path->attribute.id().value()), "ChoiceField DataPath does not resolve");
        }
        if (!is_single_string_type_domain(attribute->type)) {
            fail("OOF1122", "$/ChoiceField/DataPath", "linked String Attribute", attribute->name,
                "ChoiceField DataPath must target a single String attribute");
        }
    }
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::choice_field);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_choice_field_info(enabled, tool_tip),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_radio_button(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::radio_button || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "RadioButton with positive int64 ID", control.name,
            "RadioButton is outside the supported profile");
    }
    if (control.name.empty() || (control.data_path && !control.data_path->members.empty()) ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/RadioButton", "named RadioButton with plain Position", control.name,
            "RadioButton uses a storage concept outside the supported basic profile");
    }
    require_allowed_properties(control.properties(), {"Enabled", "Caption", "ToolTip", "SelectionValue"}, "$/RadioButton");
    bool first_in_group = false;
    if (const auto* entry = control.extension_properties.find(model::PropertyId::from_name("FirstInGroup"))) {
        const auto* value = std::get_if<bool>(&entry->value);
        if (value == nullptr) fail("OOF1122", "$/RadioButton/FirstInGroup", "Boolean", "different value kind",
            "RadioButton FirstInGroup must be Boolean");
        first_in_group = *value;
    }
    model::TypeDomainPatternValue value_type;
    if (const auto* entry = control.extension_properties.find(model::PropertyId::from_name("ValueType"))) {
        const auto* value = std::get_if<model::TypeDomainPatternValue>(&entry->value);
        if (value == nullptr) fail("OOF1122", "$/RadioButton/ValueType", "TypeDomainPattern", "different value kind",
            "RadioButton ValueType must be a TypeDomainPattern");
        value_type = *value;
    }
    bool unsupported_extension = false;
    control.extension_properties.for_each_explicit([&](const model::PropertyEntry& entry) {
        if (entry.id != model::PropertyId::from_name("FirstInGroup") &&
            entry.id != model::PropertyId::from_name("ValueType")) unsupported_extension = true;
    });
    if (unsupported_extension) fail("OOF1122", "$/RadioButton", "FirstInGroup and ValueType extensions", "other property",
        "RadioButton has an unsupported extension property");
    if (!value_type.entries.empty() && !is_single_numeric_type_domain(value_type)) {
        fail("OOF1122", "$/RadioButton/ValueType", "empty or single Numeric TypeDomainPattern", "other type",
            "RadioButton ValueType is outside the supported numeric profile");
    }
    std::optional<model::DecimalValue> selection_value;
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("SelectionValue"))) {
        const auto* value = std::get_if<model::DecimalValue>(&entry->value);
        if (value == nullptr) fail("OOF1122", "$/RadioButton/SelectionValue", "DecimalValue", "different value kind",
            "RadioButton SelectionValue must be DecimalValue");
        try { selection_value = model::DecimalValue{value_codec::canonical_decimal(value->canonical)}; }
        catch (const std::invalid_argument& error) {
            fail("OOF1122", "$/RadioButton/SelectionValue", "xs:decimal", value->canonical, error.what());
        }
    }
    if (control.data_path && !is_single_numeric_type_domain(value_type)) {
        fail("OOF1122", "$/RadioButton/DataPath", "numeric ValueType with DataPath", "missing or non-numeric type",
            "RadioButton DataPath requires a numeric ValueType");
    }
    if (control.data_path) {
        const auto* attribute = document.find_attribute(control.data_path->attribute.id());
        if (attribute == nullptr || attribute->type != value_type) {
            fail("OOF1122", "$/RadioButton/DataPath", "existing Attribute with matching numeric ValueType",
                std::to_string(control.data_path->attribute.id().value()),
                "RadioButton DataPath does not resolve to an Attribute with the same numeric type");
        }
    }
    if (selection_value) {
        const bool contextual_member = value_type.entries.empty() && !first_in_group;
        if (!contextual_member && (!is_single_numeric_type_domain(value_type) ||
            !decimal_fits_numeric_qualifiers(selection_value->canonical, value_type.entries.front().numeric))) {
            fail("OOF1122", "$/RadioButton/SelectionValue", "SelectionValue within numeric ValueType qualifiers",
                selection_value->canonical, "RadioButton SelectionValue does not fit its numeric ValueType");
        }
    }
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string caption = explicit_string(control.properties(), "Caption");
    const std::string tool_tip = explicit_string(control.properties(), "ToolTip");
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::radio_button);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_radio_button_info(value_type, selection_value, enabled, caption, tool_tip),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw(first_in_group ? "1" : "0")}),
        list({raw("0")}),
    });
}

LV encode_html_document_field(const model::ControlNode& control, const GeometryContext& context) {
    if (control.kind() != model::ControlKind::html_document_field || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "HTMLDocumentField with positive int64 ID", control.name,
            "HTMLDocumentField is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/HTMLDocumentField", "named HTMLDocumentField with plain Position and no DataPath, Events, extensions, or children",
            control.name, "HTMLDocumentField uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"Output"}, "$/HTMLDocumentField");
    const auto output = explicit_enum_storage_value(
        control.properties(), "HTMLDocumentField", "Output", "Output", 0,
        {{"Auto", 0}, {"Enable", 1}, {"Disable", 2}});
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::html_document_field);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_html_document_field_data(output),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_text_document_field(const model::ControlNode& control, const GeometryContext& context) {
    if (control.kind() != model::ControlKind::text_document_field || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        fail("OOF1122", "$/Form/ChildItems", "TextDocumentField with positive int64 ID", control.name, "TextDocumentField is outside the supported profile");
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() || !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() || control.position.z_order.is_explicit() ||
        control.position.collapse.is_explicit() || !control.position.bindings.dimensions.empty())
        fail("OOF1122", "$/TextDocumentField", "named unbound TextDocumentField with plain Position", control.name, "TextDocumentField uses an unsupported storage concept");
    require_allowed_properties(control.properties(), {"Enabled", "BorderColor", "Font"}, "$/TextDocumentField");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const auto border_color = explicit_button_color(control.properties(), "BorderColor");
    const auto font = explicit_control_font(control.properties(), "$/TextDocumentField/Font");
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::text_document_field);
    return list({raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value())),
        canonical_text_document_field_info(enabled, border_color, font),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}), list({raw("0")})});
}

LV encode_default_complex_control(const model::ControlNode& control, const GeometryContext& context) {
    const auto& descriptor = model::metamodel::descriptor_for(control.kind());
    const std::string path = "$/" + std::string(descriptor.public_name);
    if (control.id.value() == 0 || control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        fail("OOF1122", path, "positive int64 control ID", std::to_string(control.id.value()), "Default complex control ID is invalid");
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty())
        fail("OOF1122", path, "named default complex control without DataPath, events, extensions, or children", control.name,
            "Default complex control uses an unsupported storage concept");
    require_allowed_properties(control.properties(), {}, path);
    std::vector<LV> fields{raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value()))};
    if (control.kind() == model::ControlKind::graphical_schema_field) {
        fields.push_back(canonical_graphical_schema_field_info());
    } else if (control.kind() == model::ControlKind::pivot_chart) {
        fields.push_back(canonical_default_pivot_chart_info());
    } else {
        fields.push_back(canonical_geographical_schema_field_info());
        fields.push_back(canonical_geographical_schema());
    }
    fields.push_back(encode_geometry(control.position, context, IncomingAnchorLists{}));
    fields.push_back(list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}));
    fields.push_back(list({raw("0")}));
    return list(std::move(fields));
}

LV encode_calendar_field(const model::ControlNode& control, const GeometryContext& context) {
    if (control.kind() != model::ControlKind::calendar_field || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "CalendarField with positive int64 ID", control.name,
            "CalendarField is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/CalendarField", "named CalendarField with plain Position", control.name,
            "CalendarField uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"Enabled", "BeginOfDisplayPeriod"}, "$/CalendarField");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    std::string begin_period = "00010101000000";
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("BeginOfDisplayPeriod"))) {
        if (std::holds_alternative<model::UndefinedValue>(entry->value)) {
            begin_period = "00010101000000";
        } else if (const auto* date = std::get_if<model::DateValue>(&entry->value)) {
            begin_period = value_codec::date_to_platform(date->canonical);
            if (begin_period == "00010101000000") {
                fail("OOF1122", "$/CalendarField/BeginOfDisplayPeriod", "date distinct from Undefined sentinel",
                    date->canonical, "CalendarField date collides with the Undefined storage sentinel");
            }
        } else {
            fail("OOF1122", "$/CalendarField/BeginOfDisplayPeriod", "Date or Undefined", "different value kind",
                "CalendarField BeginOfDisplayPeriod has the wrong value type");
        }
    }
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::calendar_field);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_calendar_field_info(enabled, begin_period),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_dendrogram(const model::ControlNode& control, const GeometryContext& context) {
    if (control.kind() != model::ControlKind::dendrogram || control.id.value() == 0 ||
        control.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("OOF1122", "$/Form/ChildItems", "Dendrogram with positive int64 ID", control.name,
            "Dendrogram is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/Dendrogram", "named Dendrogram with plain Position", control.name,
            "Dendrogram uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"Orientation"}, "$/Dendrogram");
    const auto orientation = explicit_enum_storage_value(control.properties(), "Dendrogram", "Orientation",
        "DendrogramOrientation", 0, {{"Up", 0}, {"Down", 1}});
    const auto& graph = std::get<model::DendrogramPayload>(control.payload);
    validate_dendrogram_graph(graph, "$/Dendrogram");
    LV data = canonical_dendrogram_data(orientation);
    std::vector<LV> item_rows;
    std::map<std::string, std::uint64_t> item_keys;
    for (std::size_t index = 0; index < graph.items.size(); ++index) {
        const auto key = static_cast<std::uint64_t>(index + 1);
        item_keys.emplace(graph.items[index].value, key);
        item_rows.push_back(dendrogram_item_row(key, index + 1 < graph.items.size() ? key + 1 : 0, 0,
            graph.items[index].value, graph.items[index].text));
    }
    fill_dendrogram_collection(data.items[2], item_rows, dendrogram_sentinel(item_rows.size()), false, "$/Dendrogram/Items");
    std::vector<LV> link_rows;
    for (std::size_t index = 0; index < graph.links.size(); ++index) {
        const auto& link = graph.links[index];
        link_rows.push_back(dendrogram_link_row(index + 1, index + 1 < graph.links.size() ? index + 2 : 0, 0,
            link, item_keys.at(link.first_item), item_keys.at(link.second_item)));
    }
    fill_dendrogram_collection(data.items[3], link_rows, dendrogram_sentinel(link_rows.size()), true, "$/Dendrogram/Links");
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::dendrogram);
    return list({raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value())),
        std::move(data), encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")})});
}

LV encode_input_field(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::input_field ||
        control.id.value() == 0) {
        fail("OOF1122", "$/Form/ChildItems", "InputField with positive ID at its ChildItems index", control.name, "InputField is outside the supported profile");
    }
    if (control.name.empty() || !control.data_path || !control.data_path->members.empty() ||
        !control.extension_properties.empty() || !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/InputField", "named InputField with direct DataPath and plain Position", control.name, "InputField uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {
        "Enabled", "ReadOnly", "ToolTip", "Format", "Wrap", "ChooseType", "MarkNegatives", "ChoiceButton", "OpenButton",
        "ClearButton", "SpinButton", "ChoiceListButton", "Transparent", "MultiLine",
        "ExtendedEdit", "PasswordMode", "AutoChoiceIncomplete", "AutoMarkIncomplete",
        "HorizontalAlign", "VerticalAlign", "ChoiceListHeight"}, "$/InputField");
    const auto* attribute = document.find_attribute(control.data_path->attribute.id());
    if (attribute == nullptr) {
        fail("OOF1123", "$/InputField/DataPath", "existing linked Attribute", std::to_string(control.data_path->attribute.id().value()), "InputField DataPath does not resolve");
    }
    if (!is_single_string_type_domain(attribute->type)) {
        fail("OOF1122", "$/InputField/DataPath", "linked single-string Attribute", attribute->name, "InputField type is outside the supported profile");
    }
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const bool read_only = explicit_bool(control.properties(), "ReadOnly", false);
    const InputFieldTextValues text_values{
        explicit_string(control.properties(), "ToolTip"),
        explicit_string(control.properties(), "Format"),
    };
    const InputFieldLayoutValues layout_values{
        explicit_enum_storage_value(control.properties(), "InputField", "HorizontalAlign", "HorizontalAlign", 4,
            {{"Left", 0}, {"Center", 1}, {"Right", 2}, {"Justify", 3}, {"Auto", 4}}),
        explicit_enum_storage_value(control.properties(), "InputField", "VerticalAlign", "VerticalAlign", 0,
            {{"Top", 0}, {"Center", 1}, {"Bottom", 2}}),
        explicit_integer(control.properties(), "ChoiceListHeight", 0, "$/InputField/ChoiceListHeight"),
    };
    InputFieldFlagValues input_field_flags{};
    for (std::size_t index = 0; index < input_field_flag_mappings.size(); ++index) {
        const auto& mapping = input_field_flag_mappings[index];
        input_field_flags[index] = explicit_bool(control.properties(), mapping.name, mapping.default_value);
    }
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::input_field);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_input_field_info(attribute->type, enabled, read_only, input_field_flags, text_values, layout_values),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
}

LV encode_table(
    const model::OrdinaryFormDocument& document,
    const model::ControlNode& control,
    const GeometryContext& context) {
    if (control.kind() != model::ControlKind::table || control.id.value() == 0) {
        fail("OOF1122", "$/Table", "Table with positive ID", control.name,
            "Table is outside the supported storage profile");
    }
    if (control.name.empty() || !control.data_path || !control.data_path->members.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/Table", "named Table with direct DataPath and basic Position", control.name,
            "Table uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"ReadOnly"}, "$/Table");
    require_allowed_properties(control.extension_properties, {"FirstInGroup"}, "$/Table");
    const bool first_in_group = explicit_bool(control.extension_properties, "FirstInGroup", false);
    const bool read_only = explicit_bool(control.properties(), "ReadOnly", true);
    const auto* attribute = document.find_attribute(control.data_path->attribute.id());
    if (attribute == nullptr) {
        fail("OOF1123", "$/Table/DataPath", "existing ValueTable Attribute",
            std::to_string(control.data_path->attribute.id().value()), "Table DataPath does not resolve");
    }
    if (!is_single_value_table_type_domain(attribute->type)) {
        fail("OOF1122", "$/Table/DataPath", "unqualified ValueTable Attribute", attribute->name,
            "Table DataPath must target the named ValueTable type");
    }
    const auto& table = std::get<model::TablePayload>(control.payload);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::table);
    return list({
        raw(std::string(descriptor.guid)), raw(std::to_string(control.id.value())),
        canonical_table_control_info(attribute->type, table.columns, read_only, "$/Table"),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw(first_in_group ? "1" : "0")}),
        list({raw("0")})});
}

void validate_form_extension_context(const model::OrdinaryFormDocument& document, std::string_view path) {
    if (document.form().main_attribute.id() && !document.form().extension)
        fail("OOF1122", std::string(path), "declared extension for the main object context",
            "absent extension", "MainAttribute context has no supported form extension");
    if (document.form().extension) {
        const auto* main = document.find_attribute(document.form().main_attribute.id());
        if (!main || main->type.entries.size() != 1 ||
            main->type.entries.front().term != model::TypeDomainTerm::object ||
            !main->type.entries.front().type_uuid)
            fail("OOF1122", std::string(path), "one concrete object Attribute selected by MainAttribute",
                "missing or incompatible object type", "DataProcessorFormExtension requires its object context");
        if (main->main.value() || main->stored_data.value())
            fail("OOF1122", std::string(path), "default flags for the main object Attribute",
                "nondefault flags", "DataProcessorFormExtension main Attribute flags are not supported");
    }
}

}  // namespace

Result<model::Position> decode_page_position(
    const LV& boundaries,
    std::uint32_t page_index,
    std::optional<model::ControlRef> owner) {
    return capture_decode_failure<model::Position>([&] {
        require_arity(boundaries, 4, "$/Page/Position");
        std::array<std::int32_t, 4> coordinates{};
        std::array<std::int32_t, 4> margins{};
        for (std::size_t edge = 0; edge < 4; ++edge) {
            const auto path = child_path("$/Page/Position", edge);
            const auto& row = boundaries.items[edge];
            require_arity(row, 9, path);
            require_raw_constant(row.items[0], "2", child_path(path, 0));
            coordinates[edge] = integer_atom<std::int32_t>(row.items[1], child_path(path, 1));
            require_raw_constant(row.items[2], edge % 2 == 0 ? "1" : "0", child_path(path, 2));
            require_raw_constant(row.items[3], "1", child_path(path, 3));
            require_raw_constant(row.items[4], std::to_string(edge + 1), child_path(path, 4));
            require_raw_constant(row.items[5], std::to_string(page_index), child_path(path, 5));
            require_raw_constant(row.items[6], "0", child_path(path, 6));
            margins[edge] = integer_atom<std::int32_t>(row.items[7], child_path(path, 7));
            require_raw_constant(row.items[8], "0", child_path(path, 8));
            if (edge < 2 && margins[edge] != 0) {
                fail("OOF1122", child_path(path, 7), "fixed Left/Top boundary", std::to_string(margins[edge]),
                    "Page Left/Top constraints are not established");
            }
            if (margins[edge] == std::numeric_limits<std::int32_t>::min()) {
                fail("OOF1120", child_path(path, 7), "representable signed binding offset", std::to_string(margins[edge]),
                    "Page boundary offset overflows int32");
            }
        }
        const auto width = static_cast<std::int64_t>(coordinates[2]) - coordinates[0];
        const auto height = static_cast<std::int64_t>(coordinates[3]) - coordinates[1];
        if (width < 0 || height < 0 || width > std::numeric_limits<std::int32_t>::max() ||
            height > std::numeric_limits<std::int32_t>::max()) {
            fail("OOF1120", "$/Page/Position", "non-negative int32 dimensions", "invalid rectangle",
                "Page boundary coordinates cannot be represented by Position");
        }
        model::Position position;
        position.left.set(coordinates[0]);
        position.top.set(coordinates[1]);
        position.width.set(static_cast<std::int32_t>(width));
        position.height.set(static_cast<std::int32_t>(height));
        for (std::size_t edge = 2; edge < 4; ++edge) {
            model::AnchorBinding binding;
            binding.coordinate = edge == 2 ? model::BindingCoordinate::right : model::BindingCoordinate::bottom;
            binding.target_coordinate = binding.coordinate;
            binding.target = owner;
            binding.offset.set(-margins[edge]);
            position.bindings.anchors.push_back(std::move(binding));
        }
        return position;
    });
}

Result<LV> encode_page_position(
    const model::Position& position,
    std::uint32_t page_index,
    std::optional<model::ControlRef> owner) {
    return capture_decode_failure<LV>([&] {
        if (position.default_control.value().has_value() || position.tab_order.value().has_value() ||
            position.z_order.value().has_value() || position.collapse.value().has_value() ||
            !position.visible.value() || !position.bindings.dimensions.empty() ||
            position.bindings.manual_horizontal.value() || position.bindings.manual_vertical.value()) {
            fail("OOF1122", "$/Page/Position", "page rectangle with Right/Bottom owner constraints", "control-only position values",
                "Page position contains properties without an established boundary codec");
        }
        const auto left = position.left.value();
        const auto top = position.top.value();
        const auto width = position.width.value();
        const auto height = position.height.value();
        if (width < 0 || height < 0 || left > std::numeric_limits<std::int32_t>::max() - width ||
            top > std::numeric_limits<std::int32_t>::max() - height) {
            fail("OOF1120", "$/Page/Position", "non-negative geometry without int32 overflow", "invalid rectangle",
                "Page position cannot be represented by the platform boundary coordinates");
        }
        std::array<std::optional<std::int32_t>, 2> margins;
        for (const auto& binding : position.bindings.anchors) {
            const bool right = binding.coordinate == model::BindingCoordinate::right;
            const bool bottom = binding.coordinate == model::BindingCoordinate::bottom;
            if (!(right || bottom) || binding.target_coordinate != binding.coordinate || binding.target != owner ||
                binding.proportional.has_value()) {
                fail("OOF1122", "$/Page/Position/Bindings", "Right/Bottom constraints to the owning Form or Panel", "unsupported binding",
                    "Page boundary binding has no established storage representation");
            }
            const std::size_t slot = right ? 0 : 1;
            if (margins[slot].has_value()) {
                fail("OOF1122", "$/Page/Position/Bindings", "unique Right/Bottom constraints", "duplicate binding",
                    "Page boundary source edge is duplicated");
            }
            if (binding.offset.value() == std::numeric_limits<std::int32_t>::min()) {
                fail("OOF1120", "$/Page/Position/Bindings", "representable platform boundary offset", "int32 minimum",
                    "Negating the Page binding offset overflows int32");
            }
            margins[slot] = -binding.offset.value();
        }
        if (!margins[0].has_value() || !margins[1].has_value()) {
            fail("OOF1122", "$/Page/Position/Bindings", "explicit Right and Bottom owner constraints", "missing binding",
                "Page boundary constraints cannot be inferred from coordinates alone");
        }
        const std::array<std::int32_t, 4> coordinates{left, top, left + width, top + height};
        std::vector<LV> boundaries;
        for (std::size_t edge = 0; edge < 4; ++edge) {
            boundaries.push_back(list({raw("2"), raw(std::to_string(coordinates[edge])),
                raw(edge % 2 == 0 ? "1" : "0"), raw("1"), raw(std::to_string(edge + 1)),
                raw(std::to_string(page_index)), raw("0"),
                raw(std::to_string(edge < 2 ? 0 : *margins[edge - 2])), raw("0")}));
        }
        return list(std::move(boundaries));
    });
}

Result<RuntimeEnvelope> decode_runtime_envelope(std::string_view text) {
    return capture_decode_failure<RuntimeEnvelope>([text] {
        list_stream::ListValue root;
        try {
            root = list_stream::parse(without_utf8_bom(text));
        } catch (const std::exception& error) {
            fail(
                "OOF1100",
                "$",
                "runtime envelope list",
                {},
                error.what());
        }
        require_arity(root, 3, "$" );
        const std::string marker = string_atom(at(root, 0, "$"), "$/0");
        if (marker != "#") {
            fail(
                "OOF1106",
                "$/0",
                "#",
                marker,
                "Runtime envelope marker is invalid");
        }
        RuntimeEnvelope envelope;
        envelope.runtime_uuid = uuid_atom(at(root, 1, "$"), "$/1");
        envelope.payload = at(root, 2, "$" );
        require_list(envelope.payload, "$/2");
        const auto layout = probe_layout(envelope.payload, "$/2");
        if (!layout) {
            throw DecodeFailure(layout.diagnostics().front());
        }
        return envelope;
    });
}

Result<std::string> encode_runtime_envelope(const RuntimeEnvelope& envelope) {
    return capture_decode_failure<std::string>([&envelope] {
        static_cast<void>(uuid_atom(
            list_stream::ListValue::raw_atom(envelope.runtime_uuid.canonical),
            "$/1"));
        const auto layout = probe_layout(envelope.payload, "$/2");
        if (!layout) {
            throw DecodeFailure(layout.diagnostics().front());
        }
        return list_stream::dump_compact(list_stream::ListValue::list({
            list_stream::ListValue::string_atom("#"),
            list_stream::ListValue::raw_atom(envelope.runtime_uuid.canonical),
            envelope.payload,
        }));
    });
}

Result<OuterFormat> probe_outer_format(
    const list_stream::ListValue& payload,
    std::string_view path) {
    return capture_decode_failure<OuterFormat>([&payload, path] {
        require_list(payload, path);
        if (payload.items.empty()) {
            fail(
                "OOF1110",
                child_path(path, 0),
                "format marker 26 or 27",
                "missing",
                "Form stream has no format marker");
        }
        const std::string marker_path = child_path(path, 0);
        const std::uint32_t marker = integer_atom<std::uint32_t>(payload.items[0], marker_path);
        if (marker == 26) {
            return OuterFormat::v26;
        }
        if (marker == 27) {
            return OuterFormat::v27;
        }
        fail(
            "OOF1111",
            marker_path,
            "supported format marker 26 or 27",
            std::to_string(marker),
            "Ordinary-form stream format is unsupported");
    });
}

Result<StorageLayout> probe_layout(
    const list_stream::ListValue& payload,
    std::string_view path) {
    return capture_decode_failure<StorageLayout>([&payload, path] {
        // 8.2 and 8.5 both emit outer 27; the paired nested records select the layout.
        const auto outer = probe_outer_format(payload, path);
        if (!outer) {
            throw DecodeFailure(outer.diagnostics().front());
        }
        if (outer.value() != OuterFormat::v27) {
            fail(
                "OOF1112",
                child_path(path, 0),
                "outer format 27 with a proven nested layout",
                "26",
                "Ordinary-form outer format has no proven storage layout");
        }

        require_arity(payload, 20, path);
        const std::string form_path = child_path(path, 1);
        const auto& form_section = at(payload, 1, path);
        require_list(form_section, form_path);
        const std::uint32_t form_version = integer_atom<std::uint32_t>(
            at(form_section, 0, form_path),
            child_path(form_path, 0));

        const std::string page_style_path = child_path(path, 13);
        const auto& page_style_section = at(payload, 13, path);
        require_list(page_style_section, page_style_path);

        if (form_version == 16) {
            require_arity(form_section, 11, form_path);
            require_arity(page_style_section, 3, page_style_path);
            require_raw_constant(
                page_style_section.items[0],
                "3",
                child_path(page_style_path, 0));
            return StorageLayout{
                OuterFormat::v27,
                LayoutKind::form_section_16,
                20,
                16,
                11,
                3,
                3,
            };
        }
        if (form_version == 18) {
            require_arity(form_section, 14, form_path);
            require_arity(page_style_section, 11, page_style_path);
            require_raw_constant(
                page_style_section.items[0],
                "10",
                child_path(page_style_path, 0));
            return StorageLayout{
                OuterFormat::v27,
                LayoutKind::form_section_18,
                20,
                18,
                14,
                10,
                11,
            };
        }
        fail(
            "OOF1113",
            child_path(form_path, 0),
            "supported form section version 16 or 18",
            std::to_string(form_version),
            "Ordinary-form nested storage layout is unsupported");
    });
}

Result<AttributesRecord> decode_attributes(
    const list_stream::ListValue& record,
    std::string_view path) {
    return capture_decode_failure<AttributesRecord>([&record, path] {
        require_arity(record, 4, path);
        AttributesRecord result;
        const auto main_attribute = composite_id(at(record, 0, path), child_path(path, 0));
        if (!main_attribute.is_null || main_attribute.uuid.canonical != null_uuid ||
            (main_attribute.object_id <= 0 && main_attribute.object_id != -1))
            fail("OOF1107", child_path(path, 0), "absent or positive one-component main Attribute reference",
                describe(at(record, 0, path)), "Main Attribute identity cannot be represented without loss");
        if (main_attribute.object_id != -1) result.main_attribute = main_attribute;
        result.slot_count = integer_atom<std::uint32_t>(
            at(record, 1, path),
            child_path(path, 1));

        const auto& table = at(record, 2, path);
        require_list(table, child_path(path, 2));
        if (table.items.empty()) {
            fail(
                "OOF1103",
                child_path(child_path(path, 2), 0),
                "attribute count",
                "missing",
                "Attribute table has no count");
        }
        const std::uint32_t attribute_count = integer_atom<std::uint32_t>(
            table.items[0],
            child_path(child_path(path, 2), 0));
        if (table.items.size() != static_cast<std::size_t>(attribute_count) + 1) {
            fail(
                "OOF1102",
                child_path(path, 2),
                "list arity " + std::to_string(static_cast<std::size_t>(attribute_count) + 1),
                "list arity " + std::to_string(table.items.size()),
                "Attribute table count does not match its records");
        }
        result.attributes.reserve(attribute_count);
        for (std::uint32_t index = 0; index < attribute_count; ++index) {
            const std::string record_path = child_path(child_path(path, 2), index + 1);
            const auto& item = table.items[index + 1];
            require_arity(item, 6, record_path);
            AttributeRecord attribute;
            attribute.id = composite_id(item.items[0], child_path(record_path, 0));
            attribute.main = bool_atom(item.items[1], child_path(record_path, 1));
            attribute.stored_data = bool_atom(item.items[2], child_path(record_path, 2));
            require_raw_constant(item.items[3], "1", child_path(record_path, 3));
            attribute.name = string_atom(item.items[4], child_path(record_path, 4));
            attribute.type = type_domain(item.items[5], child_path(record_path, 5));
            result.attributes.push_back(std::move(attribute));
        }
        if (result.main_attribute && std::none_of(result.attributes.begin(), result.attributes.end(),
            [&](const auto& attribute) { return attribute.id == *result.main_attribute; }))
            fail("OOF1107", child_path(path, 0), "main reference to a declared Attribute",
                describe(at(record, 0, path)), "Main Attribute reference is dangling");

        const auto& links = at(record, 3, path);
        require_list(links, child_path(path, 3));
        if (links.items.empty()) {
            fail(
                "OOF1103",
                child_path(child_path(path, 3), 0),
                "attribute-link count",
                "missing",
                "Attribute-link table has no count");
        }
        const std::uint32_t link_count = integer_atom<std::uint32_t>(
            links.items[0],
            child_path(child_path(path, 3), 0));
        if (links.items.size() != static_cast<std::size_t>(link_count) + 1) {
            fail(
                "OOF1102",
                child_path(path, 3),
                "list arity " + std::to_string(static_cast<std::size_t>(link_count) + 1),
                "list arity " + std::to_string(links.items.size()),
                "Attribute-link table count does not match its records");
        }
        result.links.reserve(link_count);
        for (std::uint32_t index = 0; index < link_count; ++index) {
            const std::string link_path = child_path(child_path(path, 3), index + 1);
            const auto& item = links.items[index + 1];
            require_arity(item, 2, link_path);
            AttributeLink link;
            link.control_id = integer_atom<std::int64_t>(item.items[0], child_path(link_path, 0));
            if (link.control_id < 0) {
                fail(
                    "OOF1105",
                    child_path(link_path, 0),
                    "non-negative control ID",
                    std::to_string(link.control_id),
                    "Attribute link uses an invalid control ID");
            }
            const auto& target = item.items[1];
            require_arity(target, 2, child_path(link_path, 1));
            require_raw_constant(
                target.items[0],
                "1",
                child_path(child_path(link_path, 1), 0));
            link.attribute_id = composite_id(
                target.items[1],
                child_path(child_path(link_path, 1), 1));
            result.links.push_back(std::move(link));
        }
        return result;
    });
}

Result<std::vector<model::Page>> decode_page_table(
    const list_stream::ListValue& table,
    std::uint64_t starting_id,
    std::string_view path) {
    return capture_decode_failure<std::vector<model::Page>>([&table, starting_id, path] {
        require_list(table, path);
        if (starting_id == 0) {
            fail("OOF1122", std::string(path), "positive starting Page ID", "0",
                "Page IDs must be positive");
        }
        if (table.items.size() < 2) {
            fail("OOF1102", std::string(path), "table marker and page count", describe(table),
                "Page table header is incomplete");
        }
        require_raw_constant(table.items[0], "1", child_path(path, 0));
        const auto count = integer_atom<std::uint32_t>(table.items[1], child_path(path, 1));
        if (static_cast<std::size_t>(count) != table.items.size() - 2) {
            fail("OOF1102", std::string(path), "page count matching table rows", describe(table),
                "Page table count does not match its rows");
        }
        const auto available_ids = std::numeric_limits<std::uint64_t>::max() - starting_id + 1;
        if (count > available_ids) {
            fail("OOF1122", std::string(path), "Page IDs within uint64 range",
                std::to_string(count), "Page ID allocation overflows uint64");
        }
        std::vector<model::Page> pages;
        pages.reserve(count);
        std::unordered_set<std::string> names;
        for (std::uint32_t index = 0; index < count; ++index) {
            auto page = decode_page_record(
                table.items[static_cast<std::size_t>(index) + 2],
                model::ObjectId{starting_id + index},
                child_path(path, static_cast<std::size_t>(index) + 2));
            if (!names.insert(page.name).second) {
                fail("OOF1122", child_path(child_path(path, static_cast<std::size_t>(index) + 2), 6),
                    "unique Page Name within owner table", page.name, "Page table contains a duplicate Name");
            }
            pages.push_back(std::move(page));
        }
        return pages;
    });
}

Result<list_stream::ListValue> encode_page_table(const std::vector<model::Page>& pages) {
    return capture_decode_failure<list_stream::ListValue>([&pages] {
        if (pages.size() > std::numeric_limits<std::uint32_t>::max()) {
            fail("OOF1112", "$/Pages", "page count within uint32 range", std::to_string(pages.size()),
                "Page table is too large");
        }
        std::unordered_set<std::uint64_t> ids;
        std::unordered_set<std::string> names;
        std::vector<LV> table{raw("1"), raw(std::to_string(pages.size()))};
        table.reserve(pages.size() + 2);
        for (std::size_t index = 0; index < pages.size(); ++index) {
            const auto& page = pages[index];
            if (!ids.insert(page.id.value()).second) {
                fail("OOF1122", child_path("$/Pages", index), "unique Page IDs",
                    std::to_string(page.id.value()), "Page table contains a duplicate ID");
            }
            if (!names.insert(page.name).second) {
                fail("OOF1122", child_path("$/Pages", index), "unique Page Name within owner table",
                    page.name, "Page table contains a duplicate Name");
            }
            table.push_back(encode_page_record(page, child_path("$/Pages", index)));
        }
        return list(std::move(table));
    });
}

Result<ControlGeometry> decode_control_geometry(
    const list_stream::ListValue& geometry,
    const GeometryContext& context) {
    return capture_decode_failure<ControlGeometry>([&geometry, &context] {
        validate_geometry_context(context, "$/Position");
        auto decoded = decode_geometry(geometry, "$", context);
        return ControlGeometry{std::move(decoded.position), std::move(decoded.incoming)};
    });
}

Result<list_stream::ListValue> encode_control_geometry(
    const ControlGeometry& geometry,
    const GeometryContext& context) {
    return capture_decode_failure<list_stream::ListValue>([&geometry, &context] {
        validate_geometry_context(context, "$/Position");
        return encode_geometry(geometry.position, context, geometry.incoming);
    });
}

Result<list_stream::ListValue> encode_attributes(const AttributesRecord& record) {
    return capture_decode_failure<list_stream::ListValue>([&record] {
        if (record.attributes.size() > std::numeric_limits<std::uint32_t>::max() ||
            record.links.size() > std::numeric_limits<std::uint32_t>::max()) {
            fail(
                "OOF1112",
                "$/2",
                "attribute and link counts within uint32 range",
                "count overflow",
                "Attribute collection is too large for the platform record");
        }

        std::vector<list_stream::ListValue> attributes;
        attributes.reserve(record.attributes.size() + 1);
        attributes.push_back(
            list_stream::ListValue::raw_atom(std::to_string(record.attributes.size())));
        for (std::size_t index = 0; index < record.attributes.size(); ++index) {
            const auto& attribute = record.attributes[index];
            const std::string record_path = "$/2/2/" + std::to_string(index + 1);
            attributes.push_back(list_stream::ListValue::list({
                encoded_composite_id(attribute.id, record_path + "/0"),
                list_stream::ListValue::raw_atom(attribute.main ? "1" : "0"),
                list_stream::ListValue::raw_atom(attribute.stored_data ? "1" : "0"),
                list_stream::ListValue::raw_atom("1"),
                list_stream::ListValue::string_atom(attribute.name),
                encoded_type_domain(attribute.type, record_path + "/5"),
            }));
        }

        std::vector<list_stream::ListValue> links;
        links.reserve(record.links.size() + 1);
        links.push_back(list_stream::ListValue::raw_atom(std::to_string(record.links.size())));
        for (std::size_t index = 0; index < record.links.size(); ++index) {
            const auto& link = record.links[index];
            const std::string link_path = "$/2/3/" + std::to_string(index + 1);
            if (link.control_id < 0) {
                fail(
                    "OOF1105",
                    link_path + "/0",
                    "non-negative control ID",
                    std::to_string(link.control_id),
                    "Attribute link uses an invalid control ID");
            }
            links.push_back(list_stream::ListValue::list({
                list_stream::ListValue::raw_atom(std::to_string(link.control_id)),
                list_stream::ListValue::list({
                    list_stream::ListValue::raw_atom("1"),
                    encoded_composite_id(link.attribute_id, link_path + "/1/1"),
                }),
            }));
        }

        auto encoded = list_stream::ListValue::list({
            record.main_attribute ? encoded_composite_id(*record.main_attribute, "$/2/0") : list({raw("-1")}),
            list_stream::ListValue::raw_atom(std::to_string(record.slot_count)),
            list_stream::ListValue::list(std::move(attributes)),
            list_stream::ListValue::list(std::move(links)),
        });
        const auto validation = decode_attributes(encoded);
        if (!validation) {
            throw DecodeFailure(validation.diagnostics().front());
        }
        return encoded;
    });
}

Result<model::OrdinaryFormDocument> decode_document(
    const list_stream::ListValue& payload,
    std::string_view form_name) {
    return capture_decode_failure<model::OrdinaryFormDocument>([&payload, form_name] {
        const auto layout = probe_layout(payload);
        if (!layout) {
            throw DecodeFailure(layout.diagnostics().front());
        }
        if (layout.value().kind != LayoutKind::form_section_18) {
            fail(
                "OOF1112",
                "$/1/0",
                "form section 18",
                std::to_string(layout.value().form_section_version),
                "The typed product codec currently supports only the proven 8.5 layout");
        }
        if (form_name.empty()) {
            fail("OOF1115", "$", "non-empty form name", "empty", "Form name is required");
        }

        std::optional<model::FormExtensionKind> form_extension;
        const auto& extension = payload.items[3];
        require_list(extension, "$/3");
        if (extension.items.empty())
            fail("OOF1114", "$/3", "declared Form extension or absent extension", "empty",
                "Form extension record is incomplete");
        const auto extension_guid = uuid_atom(extension.items[0], "$/3/0").canonical;
        if (extension_guid == null_uuid) {
            require_exact(extension, parse_constant("{00000000-0000-0000-0000-000000000000,0}"),
                "$/3", "Absent Form extension must not contain settings");
        } else {
            const auto& descriptor = model::metamodel::data_processor_form_extension;
            require_arity(extension, 3, "$/3");
            require_raw_constant(extension.items[0], descriptor.guid, "$/3/0");
            require_raw_constant(extension.items[1], "1", "$/3/1");
            require_exact(extension.items[2], parse_constant("{2,0,{0,0},{0},1}"), "$/3/2",
                "DataProcessorFormExtension contains unsupported nondefault settings");
            form_extension = descriptor.kind;
        }
        const auto form_close_handler = decode_form_close_events(payload.items[4], "$/4");
        require_raw_constant(payload.items[5], "1", "$/5");
        require_raw_constant(payload.items[6], "4", "$/6");
        require_raw_constant(payload.items[7], "1", "$/7");
        require_raw_constant(payload.items[8], "0", "$/8");
        require_raw_constant(payload.items[9], "0", "$/9");
        require_raw_constant(payload.items[10], "0", "$/10");
        require_exact(payload.items[11], list({raw("0")}), "$/11", "Unsupported root table");
        require_exact(payload.items[12], list({raw("0")}), "$/12", "Unsupported root table");
        require_exact(payload.items[13], canonical_style_record(10), "$/13", "Unsupported page-style record");
        require_raw_constant(payload.items[14], "1", "$/14");
        require_raw_constant(payload.items[15], "2", "$/15");
        require_raw_constant(payload.items[16], "0", "$/16");
        require_raw_constant(payload.items[17], "0", "$/17");
        require_raw_constant(payload.items[18], "1", "$/18");
        require_raw_constant(payload.items[19], "1", "$/19");

        const auto& form_section = payload.items[1];
        const auto& header = form_section.items[1];
        require_arity(header, 3, "$/1/1");
        const std::string caption = decoded_single_language_text(header.items[0], "$/1/1/0");
        const std::uint64_t stored_max_id = integer_atom<std::uint64_t>(header.items[1], "$/1/1/1");
        const auto default_owner_id = integer_atom<std::uint32_t>(header.items[2], "$/1/1/2");
        if (default_owner_id == 0) fail("OOF1114", "$/1/1/2", "default owner ID or uint32 sentinel", "0", "Default action owner is invalid");
        if (form_close_handler && stored_max_id == std::numeric_limits<std::uint64_t>::max())
            fail("OOF1120", "$/4", "allocatable Form.OnClose ID", "uint64 max", "Synthetic event ID overflows");

        const std::int32_t width = integer_atom<std::int32_t>(form_section.items[3], "$/1/3");
        const std::int32_t height = integer_atom<std::int32_t>(form_section.items[4], "$/1/4");
        if (width < 8 || height < 8) {
            fail(
                "OOF1120",
                "$/1",
                "form dimensions at least 8x8",
                std::to_string(width) + "x" + std::to_string(height),
                "Stored form dimensions are invalid");
        }
        require_raw_constant(form_section.items[5], "1", "$/1/5");
        require_raw_constant(form_section.items[6], "0", "$/1/6");
        require_raw_constant(form_section.items[7], "1", "$/1/7");
        require_raw_constant(form_section.items[8], "4", "$/1/8");
        require_raw_constant(form_section.items[9], "4", "$/1/9");
        // Slot 10 is a platform serialization counter. It is normalized by the writer.
        static_cast<void>(integer_atom<std::uint32_t>(form_section.items[10], "$/1/10"));
        if (integer_atom<std::int32_t>(form_section.items[11], "$/1/11") != width ||
            integer_atom<std::int32_t>(form_section.items[12], "$/1/12") != height) {
            fail(
                "OOF1114",
                "$/1/11",
                "dimensions repeated from $/1/3 and $/1/4",
                describe(form_section),
                "Form dimension records disagree");
        }
        require_raw_constant(form_section.items[13], "96", "$/1/13");

        const auto& root_panel = form_section.items[2];
        require_arity(root_panel, 3, "$/1/2");
        require_raw_constant(root_panel.items[0], root_panel_guid, "$/1/2/0");
        const auto& root_panel_envelope = root_panel.items[1];
        require_arity(root_panel_envelope, 3, "$/1/2/1");
        std::uint64_t next_page_id = stored_max_id + 1;
        auto root_owner = decode_owner_pages(root_panel_envelope, next_page_id, std::nullopt, false, "$/1/2/1");
        auto root_pages = std::move(root_owner.pages);
        const auto& root_incoming = root_owner.incoming;
        const model::LocalizedStringValue canonical_page_title{{{"ru", "Страница1"}}};
        bool implicit_default_page = root_pages.size() == 1 && root_pages[0].name == "Страница1" &&
            root_pages[0].title.value() == canonical_page_title && root_pages[0].visible.value() &&
            root_pages[0].enabled.value();
        if (implicit_default_page) {
            const auto& position = root_pages[0].position.value();
            const auto& anchors = position.bindings.anchors;
            implicit_default_page = position.left.value() == 8 && position.top.value() == 8 &&
                position.width.value() == width - 16 && position.height.value() == height - 16 &&
                anchors.size() == 2 && position.bindings.dimensions.empty();
            const std::array<model::BindingCoordinate, 2> expected_edges{
                model::BindingCoordinate::right, model::BindingCoordinate::bottom};
            for (std::size_t index = 0; implicit_default_page && index < anchors.size(); ++index) {
                implicit_default_page = anchors[index].coordinate == expected_edges[index] &&
                    anchors[index].target_coordinate == expected_edges[index] &&
                    !anchors[index].target && anchors[index].offset.value() == -8 &&
                    !anchors[index].proportional;
            }
        }

        const auto attributes_result = decode_attributes(payload.items[2]);
        if (!attributes_result) {
            throw DecodeFailure(attributes_result.diagnostics().front());
        }
        const auto& attributes = attributes_result.value();

        model::Form form;
        form.id = model::ObjectId{1};
        form.extension = form_extension;
        if (attributes.main_attribute)
            form.main_attribute = model::AttributeRef{model::ObjectId{static_cast<std::uint64_t>(attributes.main_attribute->object_id)}};
        form.name = std::string(form_name);
        form.panel = std::move(root_owner.panel);
        if (!caption.empty()) {
            form.properties.set_explicit(model::PropertyId::from_name("Caption"), caption);
        }
        if (width != default_form_width) {
            form.properties.set_explicit(
                model::PropertyId::from_name("Width"),
                static_cast<std::int64_t>(width));
        }
        if (height != default_form_height) {
            form.properties.set_explicit(
                model::PropertyId::from_name("Height"),
                static_cast<std::int64_t>(height));
        }
        model::OrdinaryFormDocument document(form);

        std::uint64_t actual_max_id = form.id.value();
        for (const auto& stored : attributes.attributes) {
            if (stored.id.object_id <= 0 || !stored.id.is_null ||
                stored.id.uuid.canonical != null_uuid) {
                fail(
                    "OOF1122",
                    "$/2/2",
                    "one-component positive attribute ID",
                    std::to_string(stored.id.object_id),
                    "Attribute identity cannot be represented by ObjectId without loss");
            }
            const auto object_id = static_cast<std::uint64_t>(stored.id.object_id);
            model::Attribute attribute;
            attribute.id = model::ObjectId{object_id};
            attribute.name = stored.name;
            attribute.type = stored.type;
            if (stored.main) {
                attribute.main.set(true);
            }
            if (stored.stored_data) {
                attribute.stored_data.set(true);
            }
            document.add_attribute(std::move(attribute));
            actual_max_id = std::max(actual_max_id, object_id);
        }

        std::unordered_map<std::int64_t, const AttributeRecord*> attributes_by_id;
        for (const auto& attribute : attributes.attributes) {
            if (!attributes_by_id.emplace(attribute.id.object_id, &attribute).second) {
                fail("OOF1122", "$/2/2", "unique Attribute IDs", std::to_string(attribute.id.object_id), "Attribute table contains a duplicate ID");
            }
        }
        std::unordered_map<std::int64_t, const AttributeLink*> links_by_control;
        for (const auto& link : attributes.links) {
            if (!links_by_control.emplace(link.control_id, &link).second) {
                fail("OOF1122", "$/2/3", "one DataPath link per control ID", std::to_string(link.control_id), "Attribute links contain an ambiguous control ID");
            }
        }
        std::unordered_set<std::int64_t> consumed_link_ids;
        std::vector<DecodedControl> pending_controls;
        std::vector<model::Page> nested_pages;
        const auto& button_descriptor = model::metamodel::descriptor_for(model::ControlKind::button);
        const auto& command_bar_descriptor = model::metamodel::descriptor_for(model::ControlKind::command_bar);
        const auto& picture_descriptor = model::metamodel::descriptor_for(model::ControlKind::picture_decoration);
        const auto& label_descriptor = model::metamodel::descriptor_for(model::ControlKind::label_decoration);
        const auto& calendar_descriptor = model::metamodel::descriptor_for(model::ControlKind::calendar_field);
        const auto& spreadsheet_descriptor = model::metamodel::descriptor_for(model::ControlKind::spreadsheet_document_field);
        const auto& text_document_descriptor = model::metamodel::descriptor_for(model::ControlKind::text_document_field);
        const auto& input_descriptor = model::metamodel::descriptor_for(model::ControlKind::input_field);
        const auto& checkbox_descriptor = model::metamodel::descriptor_for(model::ControlKind::check_box);
        const auto& progress_bar_descriptor = model::metamodel::descriptor_for(model::ControlKind::progress_bar);
        const auto& dendrogram_descriptor = model::metamodel::descriptor_for(model::ControlKind::dendrogram);
        const auto& track_bar_descriptor = model::metamodel::descriptor_for(model::ControlKind::track_bar);
        const auto& list_box_descriptor = model::metamodel::descriptor_for(model::ControlKind::list_box);
        const auto& panel_descriptor = model::metamodel::descriptor_for(model::ControlKind::panel);
        using DecodeChildTable = std::function<void(
            const LV&, std::vector<model::Page>&, GeometryOwner, const IncomingAnchorLists&, std::string_view)>;
        DecodeChildTable decode_child_table;
        decode_child_table = [&](const LV& child_table, std::vector<model::Page>& pages,
                                 GeometryOwner owner, const IncomingAnchorLists& owner_incoming,
                                 std::string_view path) {
            require_list(child_table, path);
            if (child_table.items.empty()) fail("OOF1103", child_path(path, 0), "control count", "missing", "Control table has no count");
            const auto count = integer_atom<std::uint32_t>(child_table.items[0], child_path(path, 0));
            if (child_table.items.size() != static_cast<std::size_t>(count) + 1) {
                fail("OOF1102", std::string(path), "control count matching records", describe(child_table), "Control table count does not match its records");
            }
            struct RecordSlot { const LV* record; std::string path; std::uint32_t ordinal; std::int32_t tab_order; std::string tab_order_path; };
            std::vector<std::vector<std::optional<RecordSlot>>> ordered(pages.size());
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto record_path = child_path(path, static_cast<std::size_t>(index) + 1);
                const auto& record = child_table.items[index + 1];
                require_list(record, record_path);
                static_cast<void>(raw_atom(at(record, 1, record_path), child_path(record_path, 1)));
                const std::string child_guid = raw_atom(at(record, 0, record_path), child_path(record_path, 0));
                const std::size_t geometry_slot = control_geometry_slot(child_guid);
                const auto geometry_path = child_path(record_path, geometry_slot);
                const auto& geometry = at(record, geometry_slot, record_path);
                std::size_t ordinal_slot = 0;
                const auto page_ordinal = geometry_page_ordinal(geometry, geometry_path, ordinal_slot);
                if (page_ordinal.page >= pages.size()) {
                    fail("OOF1114", child_path(geometry_path, ordinal_slot - 1), "owner Page index below Page count",
                        std::to_string(page_ordinal.page), "Control geometry references a missing Page");
                }
                if (page_ordinal.ordinal >= count) {
                    fail("OOF1114", child_path(geometry_path, ordinal_slot), "page-local ordinal below control count",
                        std::to_string(page_ordinal.ordinal), "Control geometry ordinal is outside its Page range");
                }
                auto& page_records = ordered[page_ordinal.page];
                if (page_records.size() <= page_ordinal.ordinal) page_records.resize(static_cast<std::size_t>(page_ordinal.ordinal) + 1);
                if (page_records[page_ordinal.ordinal]) {
                    fail("OOF1114", child_path(geometry_path, ordinal_slot), "unique page-local ordinal",
                        std::to_string(page_ordinal.ordinal), "Control geometry ordinal is duplicated");
                }
                page_records[page_ordinal.ordinal] = RecordSlot{&record, record_path, page_ordinal.ordinal, page_ordinal.tab_order, child_path(geometry_path, ordinal_slot + 1)};
            }
            for (std::size_t page_index = 0; page_index < ordered.size(); ++page_index) {
                auto& page_records = ordered[page_index];
                const auto record_count = static_cast<std::size_t>(std::count_if(
                    child_table.items.begin() + 1, child_table.items.end(), [&](const LV& record) {
                        std::size_t ordinal_slot = 0;
                        const auto guid = raw_atom(at(record, 0, path), child_path(path, 0));
                        const auto slot = control_geometry_slot(guid);
                        const auto page_ordinal = geometry_page_ordinal(at(record, slot, path), path, ordinal_slot);
                        return page_ordinal.page == page_index;
                    }));
                if (page_records.size() != record_count) {
                    fail("OOF1114", std::string(path), "contiguous page-local ChildItems ordinals",
                        std::to_string(page_index), "Page child ordinals do not form a permutation");
                }
                std::set<std::int32_t> tab_orders;
                for (const auto& slot : page_records) {
                    if (!slot) continue;
                    if (static_cast<std::size_t>(slot->tab_order) > record_count || !tab_orders.insert(slot->tab_order).second)
                        fail("OOF1114", slot->tab_order_path, "page-local TabOrder permutation 1..count",
                            std::to_string(slot->tab_order), "Control TabOrder is out of range or duplicated");
                }
                for (std::size_t ordinal = 0; ordinal < page_records.size(); ++ordinal) {
                    if (!page_records[ordinal]) fail("OOF1114", std::string(path), "page-local ordinal permutation",
                        std::to_string(ordinal), "Page child ordinals do not cover every position");
                }
            }
            std::vector<std::vector<DecodedControl>> decoded(ordered.size());
            std::unordered_set<std::uint64_t> owner_child_ids;
            std::unordered_map<std::uint64_t, std::string> child_record_paths;
            for (std::size_t page_index = 0; page_index < ordered.size(); ++page_index) {
                for (const auto& slot : ordered[page_index]) {
                    const auto& record = *slot->record;
                    const auto& record_path = slot->path;
                    const GeometryContext context{owner, static_cast<std::uint32_t>(page_index), slot->ordinal};
                    const std::string guid = raw_atom(at(record, 0, record_path), child_path(record_path, 0));
                    DecodedControl child;
                    if (guid == model::metamodel::descriptor_for(model::ControlKind::chart).guid) child = decode_chart(record, record_path, context);
                    else if (guid == button_descriptor.guid) child = decode_button(record, record_path, context);
                    else if (guid == command_bar_descriptor.guid) child = decode_command_bar(record, record_path, context, document.form().id);
                    else if (guid == model::metamodel::descriptor_for(model::ControlKind::usual_group).guid)
                        child = decode_usual_group(record, record_path, context);

                    else if (guid == model::metamodel::descriptor_for(model::ControlKind::html_document_field).guid)
                        child = decode_html_document_field(record, record_path, context);
                    else if (guid == picture_descriptor.guid) child = decode_picture_decoration(record, record_path, context);
                    else if (guid == model::metamodel::descriptor_for(model::ControlKind::splitter).guid)
                        child = decode_splitter(record, record_path, context);
                    else if (guid == label_descriptor.guid) child = decode_label(record, record_path, context);
                    else if (guid == calendar_descriptor.guid) child = decode_calendar_field(record, record_path, context);
                    else if (guid == dendrogram_descriptor.guid) {
                        const auto candidate_id = integer_atom<std::uint64_t>(at(record, 1, record_path),
                            child_path(record_path, 1));
                        if (candidate_id <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                            links_by_control.contains(static_cast<std::int64_t>(candidate_id))) {
                            fail("OOF1122", "$/2/3", "no DataPath link for Dendrogram",
                                std::to_string(candidate_id), "Dendrogram DataPath storage is unsupported");
                        }
                        child = decode_dendrogram(record, record_path, context);
                    }
                    else if (guid == spreadsheet_descriptor.guid) child = decode_spreadsheet_document_field(record, record_path, context);
                    else if (guid == text_document_descriptor.guid) child = decode_text_document_field(record, record_path, context);
                    else if (guid == model::metamodel::descriptor_for(model::ControlKind::pivot_chart).guid)
                        child = decode_default_complex_control(record, record_path, context, model::ControlKind::pivot_chart);
                    else if (guid == model::metamodel::descriptor_for(model::ControlKind::graphical_schema_field).guid)
                        child = decode_default_complex_control(record, record_path, context, model::ControlKind::graphical_schema_field);
                    else if (guid == model::metamodel::descriptor_for(model::ControlKind::geographical_schema_field).guid)
                        child = decode_default_complex_control(record, record_path, context, model::ControlKind::geographical_schema_field);
                    else if (guid == input_descriptor.guid || guid == checkbox_descriptor.guid ||
                             guid == model::metamodel::descriptor_for(model::ControlKind::choice_field).guid ||
                             guid == progress_bar_descriptor.guid || guid == list_box_descriptor.guid ||
                             guid == model::metamodel::descriptor_for(model::ControlKind::radio_button).guid ||
                             guid == model::metamodel::descriptor_for(model::ControlKind::table).guid) {
                        const auto candidate_id = integer_atom<std::uint64_t>(at(record, 1, record_path), child_path(record_path, 1));
                        if (candidate_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                            fail("OOF1122", child_path(record_path, 1), "linked control ID representable in int64", std::to_string(candidate_id),
                                "Control ID cannot be resolved through the attribute-link table");
                        }
                        const auto candidate_key = static_cast<std::int64_t>(candidate_id);
                        const auto link_it = links_by_control.find(candidate_key);
                        const bool choice_field_guid =
                            guid == model::metamodel::descriptor_for(model::ControlKind::choice_field).guid;
                        const bool radio_button_guid =
                            guid == model::metamodel::descriptor_for(model::ControlKind::radio_button).guid;
                        const bool required_link = guid != progress_bar_descriptor.guid && !choice_field_guid && !radio_button_guid;
                        if (link_it == links_by_control.end() && required_link) fail("OOF1122", "$/2/3",
                            "DataPath link for each InputField, CheckBox, or ListBox", std::to_string(candidate_id),
                            "Linked control has no attribute link");
                        const AttributeRecord* linked_attribute = nullptr;
                        if (link_it != links_by_control.end()) {
                            if (!consumed_link_ids.insert(candidate_key).second) fail("OOF1122", "$/2/3",
                                "one DataPath link per linked control", std::to_string(candidate_id),
                                "Attribute link was consumed more than once");
                            const auto& link = *link_it->second;
                            if (!link.attribute_id.is_null || link.attribute_id.uuid.canonical != null_uuid) {
                                fail("OOF1122", "$/2/3", "null-UUID attribute link", describe(record),
                                    "Control DataPath link uses an unsupported target");
                            }
                            const auto attribute_it = attributes_by_id.find(link.attribute_id.object_id);
                            if (attribute_it == attributes_by_id.end()) fail("OOF1122", "$/2/3",
                                "link to an existing Attribute", std::to_string(link.attribute_id.object_id),
                                "DataPath target is unresolved");
                            linked_attribute = attribute_it->second;
                        }
                        if (choice_field_guid) {
                            child = decode_choice_field(record, record_path, linked_attribute, context);
                            if (linked_attribute != nullptr) {
                                child.control.data_path = model::DataPath{model::AttributeRef{
                                    model::ObjectId{static_cast<std::uint64_t>(linked_attribute->id.object_id)}}, {}};
                            }
                        } else if (radio_button_guid) {
                            child = decode_radio_button(record, record_path, linked_attribute, context);
                        } else if (guid == input_descriptor.guid) {
                            child = decode_input_field(record, record_path, *linked_attribute, context);
                            child.control.data_path = model::DataPath{model::AttributeRef{
                                model::ObjectId{static_cast<std::uint64_t>(linked_attribute->id.object_id)}}, {}};
                        } else if (guid == checkbox_descriptor.guid) {
                            child = decode_check_box(record, record_path, *linked_attribute, context);
                            child.control.data_path = model::DataPath{model::AttributeRef{
                                model::ObjectId{static_cast<std::uint64_t>(linked_attribute->id.object_id)}}, {}};
                        } else if (guid == list_box_descriptor.guid) {
                            child = decode_list_box(record, record_path, *linked_attribute, context);
                            child.control.data_path = model::DataPath{model::AttributeRef{
                                model::ObjectId{static_cast<std::uint64_t>(linked_attribute->id.object_id)}}, {}};
                        } else if (guid == model::metamodel::descriptor_for(model::ControlKind::table).guid) {
                            child = decode_table(record, record_path, *linked_attribute, context);
                        } else {
                            child = decode_progress_bar(record, record_path, context, linked_attribute);
                        }
                    } else if (guid == track_bar_descriptor.guid) {
                        child = decode_track_bar(record, record_path, context);
                    } else if (guid == model::metamodel::descriptor_for(model::ControlKind::gantt_chart).guid) {
                        child = decode_gantt_chart(record, record_path, context);
                    } else if (guid == panel_descriptor.guid) {
                        require_arity(record, 6, record_path);
                        const auto raw_id = integer_atom<std::uint64_t>(at(record, 1, record_path), child_path(record_path, 1));
                        if (raw_id == 0) fail("OOF1105", child_path(record_path, 1), "positive Panel ID", "0", "Panel ID is invalid");
                        auto geometry = decode_geometry(at(record, 3, record_path), child_path(record_path, 3), context);
                        const auto& info = at(record, 4, record_path);
                        require_arity(info, 6, child_path(record_path, 4));
                        require_raw_constant(info.items[0], "14", child_path(child_path(record_path, 4), 0));
                        const auto name = string_atom(info.items[1], child_path(child_path(record_path, 4), 1));
                        if (name.empty()) fail("OOF1115", child_path(child_path(record_path, 4), 1), "non-empty Panel Name", "empty", "Panel Name is required");
                        require_raw_constant(info.items[2], "4294967295", child_path(child_path(record_path, 4), 2));
                        require_raw_constant(info.items[3], "0", child_path(child_path(record_path, 4), 3));
                        require_raw_constant(info.items[4], "0", child_path(child_path(record_path, 4), 4));
                        require_raw_constant(info.items[5], "0", child_path(child_path(record_path, 4), 5));
                        const model::ControlRef panel_ref{model::ObjectId{raw_id}};
                        auto panel_owner = decode_owner_pages(at(record, 2, record_path), next_page_id, panel_ref, true,
                            child_path(record_path, 2));
                        model::ControlNode panel{panel_ref.id(), name, model::PanelPayload{}};
                        panel.position = std::move(geometry.position);
                        panel.payload = std::move(panel_owner.panel);
                        for (const auto& page : panel_owner.pages) panel.children.push_back(model::PageRef{page.id});
                        decode_child_table(at(record, 5, record_path), panel_owner.pages, GeometryOwner{panel_ref},
                            panel_owner.incoming, child_path(record_path, 5));
                        for (auto& page : panel_owner.pages) nested_pages.push_back(std::move(page));
                        child = DecodedControl{std::move(panel), std::nullopt, std::move(geometry.incoming), std::nullopt, {}};
                    } else {
                        fail("OOF1122", record_path, "supported leaf controls or Panel", guid, "Control payload is unsupported");
                    }
                    const auto child_id = child.control.id.value();
                    if (!owner_child_ids.insert(child_id).second) fail("OOF1122", std::string(path), "unique immediate child Control IDs",
                        std::to_string(child_id), "Owner child table contains a duplicate Control ID");
                    child_record_paths.emplace(child_id, record_path);
                    actual_max_id = std::max(actual_max_id, child_id);
                    pages[page_index].children.push_back(model::ControlRef{child.control.id});
                    decoded[page_index].push_back(std::move(child));
                }
            }
            IncomingAnchorLists expected_owner_incoming;
            std::unordered_map<std::uint64_t, IncomingAnchorLists> expected_sibling_incoming;
            for (const auto& page_controls : decoded) for (const auto& child : page_controls) {
                const auto source_id = child.control.id.value();
                for (const auto& binding : child.control.position.bindings.anchors) {
                    const auto source_edge = source_platform_edge(binding.coordinate, "$/Position/Bindings/coordinate");
                    const auto append_target = [&](const std::optional<model::ControlRef>& target, model::BindingCoordinate coordinate) {
                        const auto target_edge = target_dependency_bucket(
                            target_platform_edge(coordinate, "$/Position/Bindings/targetCoordinate"),
                            "$/Position/Bindings/targetCoordinate");
                        const bool is_form_owner = std::holds_alternative<FormGeometryOwner>(owner) && !target;
                        const auto* panel_owner = std::get_if<model::ControlRef>(&owner);
                        const bool is_panel_owner = panel_owner && target && target->id() == panel_owner->id();
                        if (is_form_owner || is_panel_owner) {
                            expected_owner_incoming[target_edge].push_back({source_id, source_edge});
                            return;
                        }
                        if (!target || !owner_child_ids.contains(target->id().value())) {
                            fail("OOF1114", std::string(path), "binding target in the same owner child graph",
                                target ? std::to_string(target->id().value()) : "Form target", "Binding target is dangling or outside its owner");
                        }
                        expected_sibling_incoming[target->id().value()][target_edge].push_back({source_id, source_edge});
                    };
                    append_target(binding.target, binding.target_coordinate);
                    if (binding.proportional) append_target(binding.proportional->target, binding.proportional->coordinate);
                }
            }
            require_incoming_graph(owner_incoming, std::move(expected_owner_incoming), path);
            for (const auto& page_controls : decoded) for (const auto& child : page_controls) {
                const auto found = expected_sibling_incoming.find(child.control.id.value());
                const IncomingAnchorLists empty;
                require_incoming_graph(child.incoming, found == expected_sibling_incoming.end() ? empty : found->second,
                    child_path(child_record_paths.at(child.control.id.value()),
                        control_geometry_slot(model::metamodel::descriptor_for(child.control.kind()).guid)));
                pending_controls.push_back(child);
            }
        };
        decode_child_table(root_panel.items[2], root_pages, GeometryOwner{FormGeometryOwner{}}, root_incoming, "$/1/2/2");
        if (implicit_default_page) {
            for (const auto& child : root_pages[0].children) form.children.push_back(child);
        } else {
            for (const auto& page : root_pages) form.children.push_back(model::PageRef{page.id});
        }
        if (!implicit_default_page) {
            for (auto& page : root_pages) document.add_page(std::move(page));
        }
        for (auto& page : nested_pages) document.add_page(std::move(page));
        if (consumed_link_ids.size() != links_by_control.size()) {
            fail("OOF1122", "$/2/3", "one matching link per decoded DataPath control", std::to_string(links_by_control.size() - consumed_link_ids.size()), "Attribute-link table contains unconsumed links");
        }

        if (actual_max_id >= std::numeric_limits<std::uint32_t>::max()) {
            fail(
                "OOF1120",
                "$/1/1/1",
                "object IDs below uint32 max",
                std::to_string(actual_max_id),
                "Attribute slot count cannot represent the decoded object IDs");
        }
        const bool empty_attributes =
            attributes.attributes.empty() && attributes.links.empty();
        std::uint64_t attribute_max_id = 0;
        for (const auto& stored : attributes.attributes) {
            attribute_max_id = std::max(
                attribute_max_id,
                static_cast<std::uint64_t>(stored.id.object_id));
        }
        const std::uint64_t expected_slots = std::max<std::uint64_t>(
            3,
            (empty_attributes ? actual_max_id : attribute_max_id) + 1);
        // Пустой заголовок выделения реквизитов не связан с ID контролов;
        // допускаются подтвержденные минимальные значения Designer (1, 3) и текущий размер.
        const std::string expected_slot_count = empty_attributes
            ? "1, 3, or " + std::to_string(expected_slots)
            : std::to_string(expected_slots);
        if (expected_slots > std::numeric_limits<std::uint32_t>::max() ||
            (attributes.slot_count != expected_slots &&
             !(empty_attributes && (attributes.slot_count == 1 || attributes.slot_count == 3)))) {
            fail(
                "OOF1114",
                "$/2/1",
                expected_slot_count,
                std::to_string(attributes.slot_count),
                "Attribute slot count disagrees with the platform object-ID allocator");
        }
        if (stored_max_id != actual_max_id) {
            fail(
                "OOF1114",
                "$/1/1/1",
                std::to_string(actual_max_id),
                std::to_string(stored_max_id),
                "Form header max object ID disagrees with decoded objects");
        }

        std::uint64_t synthetic_event_offset = 0;
        const std::uint64_t event_id_base = std::max(stored_max_id, next_page_id - 1);
        std::vector<model::PictureAsset> decoded_picture_assets;
        if (form_close_handler) {
            if (event_id_base == std::numeric_limits<std::uint64_t>::max()) fail("OOF1120", "$/4", "allocatable Form.OnClose ID", "uint64 max", "Synthetic event ID overflows");
            const model::ObjectId event_id{event_id_base + ++synthetic_event_offset};
            form.events.push_back(model::EventRef{event_id});
            document.add_event(model::Event{event_id, "OnClose", *form_close_handler, model::FormRef{form.id}});
        }

        for (auto& decoded_control : pending_controls) {
            if (decoded_control.click_handler) {
                if (synthetic_event_offset >=
                    std::numeric_limits<std::uint64_t>::max() - event_id_base) {
                    fail("OOF1120", "$/1/1/1", "allocatable event ID", "uint64 max", "Synthetic event ID overflows");
                }
                const model::ObjectId event_id{event_id_base + ++synthetic_event_offset};
                decoded_control.control.events.push_back(model::EventRef{event_id});
                document.add_event(model::Event{
                    event_id,
                    "Click",
                    *decoded_control.click_handler,
                    model::ControlRef{decoded_control.control.id},
                });
            }
            if (decoded_control.picture_asset) {
                if (synthetic_event_offset >= std::numeric_limits<std::uint64_t>::max() - event_id_base) {
                    fail("OOF1120", "$/1/1/1", "allocatable picture asset ID", "uint64 max", "Synthetic picture asset ID overflows");
                }
                const model::ObjectId asset_id{event_id_base + ++synthetic_event_offset};
                decoded_control.picture_asset->id = asset_id;
                decoded_control.control.properties().set_explicit(
                    model::PropertyId::from_name("Picture"),
                    model::PictureRef{model::PictureAssetRef{asset_id}});
                decoded_picture_assets.push_back(std::move(*decoded_control.picture_asset));
            }
            if (!decoded_control.menu_assets.empty()) {
                std::unordered_map<std::uint64_t, model::ObjectId> asset_ids;
                for (auto& asset : decoded_control.menu_assets) {
                    if (synthetic_event_offset >= std::numeric_limits<std::uint64_t>::max() - event_id_base)
                        fail("OOF1120", "$/Button/Buttons/Picture", "allocatable picture ID", "overflow", "Picture ID overflows");
                    const model::ObjectId id{event_id_base + ++synthetic_event_offset};
                    asset_ids.emplace(asset.id.value(), id);
                    asset.id = id;
                    decoded_picture_assets.push_back(std::move(asset));
                }
                std::function<void(std::vector<model::CommandBarButton>&)> update;
                update = [&](auto& entries) {
                    for (auto& entry : entries) {
                        if (entry.picture && !entry.picture->standard_name)
                            entry.picture->asset = model::PictureAssetRef{asset_ids.at(entry.picture->asset.id().value())};
                        update(entry.buttons);
                    }
                };
                if (auto* button = std::get_if<model::ButtonPayload>(&decoded_control.control.payload)) update(button->buttons);
                else if (auto* command_bar = std::get_if<model::CommandBarPayload>(&decoded_control.control.payload)) update(command_bar->buttons);
            }
            document.add_control(std::move(decoded_control.control));
        }
        for (auto& asset : decoded_picture_assets) document.add_asset(std::move(asset));
        document.set_form(std::move(form));

        validate_radio_groups(document);
        const auto report = document.validate();
        if (!report.ok()) {
            fail(
                "OOF1123",
                "$",
                "valid OrdinaryFormDocument",
                std::to_string(report.violations.size()) + " invariant violations: " + report.violations.front().message,
                "Decoded storage does not satisfy the product object model");
        }
        validate_form_extension_context(document, "$/2/0");
        if (default_command_bar_id(document) != default_owner_id)
            fail("OOF1114", "$/1/1/2", "owner of the unique named DefaultButton", std::to_string(default_owner_id), "Default action owner is dangling or inconsistent");
        return document;
    });
}

Result<list_stream::ListValue> encode_document(
    const model::OrdinaryFormDocument& document) {
    return capture_decode_failure<list_stream::ListValue>([&document] {
        validate_radio_groups(document);
        const auto validation = document.validate();
        if (!validation.ok()) {
            fail(
                "OOF1123",
                "$",
                "valid OrdinaryFormDocument",
                std::to_string(validation.violations.size()) + " invariant violations",
                "Document invariants must pass before storage encoding");
        }
        if (document.form().id != model::ObjectId{1} || document.form().name.empty()) {
            fail(
                "OOF1122",
                "$",
                "Form id=1 with a non-empty name",
                std::to_string(document.form().id.value()),
                "Form identity is outside the executable storage slice");
        }
        if (!document.collections().commands.empty()) {
            fail(
                "OOF1122",
                "$",
                "no commands",
                "unsupported document collections",
                "Document contains a storage concept without an executable codec");
        }
        validate_form_extension_context(document, "$/MainAttribute");
        std::unordered_set<std::uint64_t> referenced_picture_ids;
        for (const auto& control : document.collections().controls) {
            const auto* picture = control.properties().find(model::PropertyId::from_name("Picture"));
            if (picture == nullptr) continue;
            if (!std::holds_alternative<model::PictureRef>(picture->value)) {
                fail("OOF1122", "$/Button/Picture", "PictureRef", "different value type", "Button.Picture has the wrong value type");
            }
            const auto& reference = std::get<model::PictureRef>(picture->value);
            if (reference.standard_name) {
                if (reference.asset.id().value() != 0 ||
                    model::metamodel::find_standard_picture(reference.standard_name->value) == nullptr) {
                    fail("OOF1122", "$/Button/Picture", "known standard picture with empty asset target",
                        reference.standard_name->value, "Picture reference has inconsistent targets");
                }
            } else {
                if (reference.asset.id().value() == 0) {
                    fail("OOF1122", "$/Button/Picture", "positive picture asset ID", "0", "Picture asset reference is empty");
                }
                referenced_picture_ids.insert(reference.asset.id().value());
            }
        }
        for (const auto& control : document.collections().controls) {
            if (control.kind() != model::ControlKind::button && control.kind() != model::ControlKind::command_bar) continue;
            std::function<void(const std::vector<model::CommandBarButton>&)> collect_pictures;
            collect_pictures = [&](const auto& entries) {
                for (const auto& entry : entries) {
                    if (entry.picture && !entry.picture->standard_name) referenced_picture_ids.insert(entry.picture->asset.id().value());
                    collect_pictures(entry.buttons);
                }
            };
            if (const auto* button = std::get_if<model::ButtonPayload>(&control.payload)) collect_pictures(button->buttons);
            if (const auto* command_bar = std::get_if<model::CommandBarPayload>(&control.payload)) collect_pictures(command_bar->buttons);
        }
        if (referenced_picture_ids.size() != document.assets().size()) {
            fail("OOF1122", "$/PictureAssets", "one referenced asset per declared asset", "orphan or duplicate ID",
                "Picture asset collection contains an unconsumed asset");
        }
        require_allowed_properties(
            document.form().properties,
            {"Caption", "Width", "Height"},
            "$/Form");

        const std::string caption = explicit_string(document.form().properties, "Caption");
        const std::int32_t width = explicit_integer(
            document.form().properties,
            "Width",
            default_form_width);
        const std::int32_t height = explicit_integer(
            document.form().properties,
            "Height",
            default_form_height);
        if (width < 8 || height < 8) {
            fail(
                "OOF1120",
                "$/Form",
                "form dimensions at least 8x8",
                std::to_string(width) + "x" + std::to_string(height),
                "Form dimensions cannot produce a platform root panel");
        }

        struct EncodedOwner {
            std::vector<model::Page> pages;
            LV child_table;
            IncomingAnchorLists incoming;
        };
        struct EncodedChild {
            const model::ControlNode* control;
            std::uint32_t page_index;
            std::size_t ordinal;
            LV record;
        };
        std::vector<form_stream::AttributeLink> ordered_control_links;
        std::uint64_t max_id = document.form().id.value();
        std::unordered_set<std::uint64_t> seen_control_ids;
        std::unordered_set<std::uint64_t> seen_page_ids;
        using EncodeOwnerFn = std::function<EncodedOwner(
            const std::vector<model::ChildItemRef>&, GeometryOwner, bool, std::string_view)>;
        EncodeOwnerFn encode_owner;
        encode_owner = [&](const std::vector<model::ChildItemRef>& owner_children, GeometryOwner owner,
                           bool panel_owner, std::string_view path) -> EncodedOwner {
            std::vector<model::Page> pages;
            bool has_page_refs = false;
            for (const auto& item : owner_children) {
                if (const auto* page_ref = std::get_if<model::PageRef>(&item)) {
                    has_page_refs = true;
                    const auto* page = document.find_page(page_ref->id());
                    if (page == nullptr || !seen_page_ids.insert(page->id.value()).second) {
                        fail("OOF1122", std::string(path), "unique reference to an existing Page", std::to_string(page_ref->id().value()),
                            "Page reference is dangling or assigned to more than one owner");
                    }
                    pages.push_back(*page);
                } else if (has_page_refs) {
                    fail("OOF1122", std::string(path), "Page references only", "mixed Page and Control references",
                        "Owner child sequence cannot mix Pages and direct controls");
                }
            }
            if (has_page_refs && std::any_of(owner_children.begin(), owner_children.end(), [](const auto& item) {
                    return !std::holds_alternative<model::PageRef>(item);
                })) {
                fail("OOF1122", std::string(path), "Page references only", "mixed Page and Control references",
                    "Owner child sequence cannot mix Pages and direct controls");
            }
            if (panel_owner && !has_page_refs) {
                fail("OOF1122", std::string(path), "explicit Page references inside Panel", "direct controls",
                    "Panel storage requires named Pages");
            }
            std::vector<EncodedChild> children;
            const auto append_control = [&](const model::ControlRef& reference, std::uint32_t page_index,
                                            std::size_t ordinal) {
                const auto* control = document.find_control(reference.id());
                if (control == nullptr) fail("OOF1123", std::string(path), "existing Control", std::to_string(reference.id().value()),
                    "Child reference is dangling");
                if (!seen_control_ids.insert(control->id.value()).second) fail("OOF1122", std::string(path), "unique Control reference",
                    std::to_string(control->id.value()), "Control is assigned to more than one owner");
                if (ordinal >= std::numeric_limits<std::uint32_t>::max()) fail("OOF1120", std::string(path),
                    "page-local ordinal within uint32 range", std::to_string(ordinal), "Child ordinal overflows");
                const GeometryContext context{owner, page_index, static_cast<std::uint32_t>(ordinal)};
                LV record;
                if (control->kind() == model::ControlKind::chart) {
                    record = encode_chart(*control, context);
                } else if (control->kind() == model::ControlKind::command_bar) {
                    record = encode_command_bar(document, *control, context);
                } else if (control->kind() == model::ControlKind::gantt_chart) {
                    if (control->id.value() == 0 || control->id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
                        control->name.empty() || control->data_path || !control->children.empty() || !control->events.empty() ||
                        !control->extension_properties.empty() || control->position.default_control.is_explicit() ||
                        control->position.z_order.is_explicit() ||
                        control->position.collapse.is_explicit() || !control->position.bindings.dimensions.empty())
                        fail("OOF1122", child_path(path, ordinal), "childless GanttChart with basic Position", control->name,
                            "GanttChart uses an unsupported storage concept");
                    require_allowed_properties(control->properties(), {"AutoFullInterval", "FullIntervalBegin", "FullIntervalEnd"},
                        child_path(path, ordinal) + "/GanttChart");
                    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::gantt_chart);
                    record = list({raw(std::string(descriptor.guid)), raw(std::to_string(control->id.value())),
                        encode_gantt_chart_info(*control),
                        encode_geometry(control->position, context, IncomingAnchorLists{}),
                        list({raw("14"), string_value(control->name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
                        list({raw("0")})});
                } else if (control->kind() == model::ControlKind::button) {
                    record = encode_button(document, *control, context);
                } else if (control->kind() == model::ControlKind::usual_group) {
                    if (control->id.value() == 0 || control->id.value() > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max())) {
                        fail("OOF1122", "$/UsualGroup/ID", "positive int64 UsualGroup ID",
                            std::to_string(control->id.value()), "UsualGroup ID is invalid");
                    }
                    if (control->name.empty() || control->data_path || !control->extension_properties.empty() ||
                        !control->children.empty() || !control->events.empty() ||
                        control->position.default_control.is_explicit() ||
                        control->position.z_order.is_explicit() || control->position.collapse.is_explicit() ||
                        !control->position.bindings.dimensions.empty()) {
                        fail("OOF1122", child_path(path, ordinal), "plain childless UsualGroup with basic Position",
                            control->name, "UsualGroup uses an unsupported storage concept");
                    }
                    require_allowed_properties(control->properties(), {"Caption", "Enabled", "ToolTip"},
                        child_path(path, ordinal) + "/UsualGroup");
                    const bool enabled = explicit_bool(control->properties(), "Enabled", true);
                    const auto caption = explicit_string(control->properties(), "Caption");
                    const auto tool_tip = explicit_string(control->properties(), "ToolTip");
                    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::usual_group);
                    record = list({raw(std::string(descriptor.guid)), raw(std::to_string(control->id.value())),
                        canonical_usual_group_properties(enabled, caption, tool_tip),
                        encode_geometry(control->position, context, IncomingAnchorLists{}),
                        list({raw("14"), string_value(control->name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
                        list({raw("0")})});
                } else if (control->kind() == model::ControlKind::radio_button) {
                    record = encode_radio_button(document, *control, context);
                } else if (control->kind() == model::ControlKind::html_document_field) {
                    record = encode_html_document_field(*control, context);
                } else if (control->kind() == model::ControlKind::picture_decoration) {
                    record = encode_picture_decoration(*control, document, context);
                } else if (control->kind() == model::ControlKind::splitter) {
                    record = encode_splitter(*control, context);
                } else if (control->kind() == model::ControlKind::label_decoration) {
                    record = encode_label(*control, context);
                } else if (control->kind() == model::ControlKind::calendar_field) {
                    record = encode_calendar_field(*control, context);
                } else if (control->kind() == model::ControlKind::dendrogram) {
                    record = encode_dendrogram(*control, context);
                } else if (control->kind() == model::ControlKind::spreadsheet_document_field) {
                    record = encode_spreadsheet_document_field(*control, context);
                } else if (control->kind() == model::ControlKind::text_document_field) {
                    record = encode_text_document_field(*control, context);
                } else if (control->kind() == model::ControlKind::graphical_schema_field ||
                           control->kind() == model::ControlKind::geographical_schema_field ||
                           control->kind() == model::ControlKind::pivot_chart) {
                    record = encode_default_complex_control(*control, context);
                } else if (control->kind() == model::ControlKind::input_field) {
                    record = encode_input_field(document, *control, context);
                } else if (control->kind() == model::ControlKind::check_box) {
                    record = encode_check_box(document, *control, context);
                } else if (control->kind() == model::ControlKind::choice_field) {
                    record = encode_choice_field(document, *control, context);
                } else if (control->kind() == model::ControlKind::progress_bar) {
                    record = encode_progress_bar(document, *control, context);
                } else if (control->kind() == model::ControlKind::track_bar) {
                    record = encode_track_bar(*control, context);
                } else if (control->kind() == model::ControlKind::list_box) {
                    record = encode_list_box(document, *control, context);
                } else if (control->kind() == model::ControlKind::table) {
                    record = encode_table(document, *control, context);
                } else if (control->kind() == model::ControlKind::panel) {
                    if (!control->events.empty() || control->data_path || !control->extension_properties.empty()) {
                        fail("OOF1122", child_path(path, ordinal), "Panel without Events, DataPath, or extension properties",
                            control->name, "Panel uses a storage concept outside the supported profile");
                    }
                    require_allowed_properties(control->properties(), {"AutoTabOrder", "BorderColor", "TextColor", "BackColor"}, child_path(path, ordinal) + "/Panel");
                    const model::ControlRef panel_ref{control->id};
                    auto panel_owner = encode_owner(control->children, GeometryOwner{panel_ref}, true,
                        child_path(path, ordinal) + "/Panel/ChildItems");
                    auto panel_properties = encode_owner_pages(panel_owner.pages, panel_owner.incoming, true, panel_ref, 8, 8,
                        control->properties());
                    const auto& panel_descriptor = model::metamodel::descriptor_for(model::ControlKind::panel);
                    const auto info = list({raw("14"), string_value(control->name), raw("4294967295"), raw("0"), raw("0"), raw("0")});
                    record = list({raw(std::string(panel_descriptor.guid)), raw(std::to_string(control->id.value())),
                        std::move(panel_properties), encode_geometry(control->position, context, IncomingAnchorLists{}),
                        info, std::move(panel_owner.child_table)});
                } else {
                    fail("OOF1122", std::string(path), "UsualGroup, Button, RadioButton, HTMLDocumentField, TextDocumentField, PictureDecoration, LabelDecoration, CalendarField, GanttChart, InputField, CheckBox, ChoiceField, ProgressBar, TrackBar, ListBox, or Panel", control->name,
                        "Control payload is unsupported");
                }
                if (control->data_path) {
                    ordered_control_links.push_back(form_stream::AttributeLink{
                        static_cast<std::int64_t>(control->id.value()),
                        model::CompositeIdValue{static_cast<std::int64_t>(control->data_path->attribute.id().value()),
                            model::UuidValue{std::string(null_uuid)}, true}});
                }
                max_id = std::max(max_id, control->id.value());
                children.push_back(EncodedChild{control, page_index, ordinal, std::move(record)});
            };
            if (has_page_refs) {
                for (std::size_t page_index = 0; page_index < pages.size(); ++page_index) {
                    if (page_index >= std::numeric_limits<std::uint32_t>::max()) fail("OOF1120", std::string(path),
                        "Page index within uint32 range", std::to_string(page_index), "Page index overflows");
                    for (std::size_t ordinal = 0; ordinal < pages[page_index].children.size(); ++ordinal) {
                        const auto* control_ref = std::get_if<model::ControlRef>(&pages[page_index].children[ordinal]);
                        if (!control_ref) fail("OOF1122", std::string(path), "Control references inside Page", "nested Page reference",
                            "A Page cannot directly contain another Page");
                        append_control(*control_ref, static_cast<std::uint32_t>(page_index), ordinal);
                    }
                }
            } else {
                for (std::size_t ordinal = 0; ordinal < owner_children.size(); ++ordinal) {
                    const auto* control_ref = std::get_if<model::ControlRef>(&owner_children[ordinal]);
                    if (!control_ref) fail("OOF1122", std::string(path), "Control references or Page references", "unsupported child item",
                        "Owner child sequence contains an unsupported item");
                    append_control(*control_ref, 0, ordinal);
                }
            }
            std::map<std::uint32_t, std::size_t> page_child_counts;
            for (const auto& child : children) ++page_child_counts[child.page_index];
            std::map<std::uint32_t, std::set<std::int32_t>> page_tab_orders;
            for (const auto& child : children) {
                const auto& property = child.control->position.tab_order;
                const auto order = property.is_explicit() ? property.value()
                    : std::optional<std::int32_t>{static_cast<std::int32_t>(child.ordinal + 1)};
                if (!order || *order <= 0 || static_cast<std::size_t>(*order) > page_child_counts.at(child.page_index) ||
                    !page_tab_orders[child.page_index].insert(*order).second)
                    fail("OOF1122", std::string(path) + "/Position/TabOrder", "page-local TabOrder permutation 1..count",
                        order ? std::to_string(*order) : "undefined", "Control TabOrder is invalid, out of range, or duplicated");
            }
            std::unordered_set<std::uint64_t> immediate_ids;
            for (const auto& child : children) immediate_ids.insert(child.control->id.value());
            IncomingAnchorLists owner_incoming;
            std::unordered_map<std::uint64_t, IncomingAnchorLists> child_incoming;
            const auto* panel = std::get_if<model::ControlRef>(&owner);
            for (const auto& child : children) {
                for (const auto& binding : child.control->position.bindings.anchors) {
                    const auto source_edge = source_platform_edge(binding.coordinate, "$/Position/Bindings/coordinate");
                    const auto add_target = [&](const std::optional<model::ControlRef>& target, model::BindingCoordinate coordinate) {
                        const auto target_edge = target_dependency_bucket(
                            target_platform_edge(coordinate, "$/Position/Bindings/targetCoordinate"),
                            "$/Position/Bindings/targetCoordinate");
                        const bool targets_owner = panel && target && target->id() == panel->id();
                        if ((!panel && !target) || targets_owner) {
                            owner_incoming[target_edge].push_back({child.control->id.value(), source_edge});
                        } else if (!target || !immediate_ids.contains(target->id().value())) {
                            fail("OOF1123", std::string(path), "binding target in the same owner child graph",
                                target ? std::to_string(target->id().value()) : "Form target",
                                "Binding target is dangling or outside its immediate owner");
                        } else {
                            child_incoming[target->id().value()][target_edge].push_back({child.control->id.value(), source_edge});
                        }
                    };
                    add_target(binding.target, binding.target_coordinate);
                    if (binding.proportional) add_target(binding.proportional->target, binding.proportional->coordinate);
                }
            }
            for (auto& child : children) {
                const auto found = child_incoming.find(child.control->id.value());
                const IncomingAnchorLists empty;
                child.record.items[control_geometry_slot(model::metamodel::descriptor_for(child.control->kind()).guid)] = encode_geometry(child.control->position,
                    GeometryContext{owner, child.page_index, static_cast<std::uint32_t>(child.ordinal)},
                    found == child_incoming.end() ? empty : found->second);
            }
            std::sort(children.begin(), children.end(), [](const EncodedChild& left, const EncodedChild& right) {
                return left.control->id.value() < right.control->id.value();
            });
            std::vector<LV> records{raw(std::to_string(children.size()))};
            for (auto& child : children) records.push_back(std::move(child.record));
            return EncodedOwner{std::move(pages), list(std::move(records)), std::move(owner_incoming)};
        };
        auto root_owner = encode_owner(document.form().children, GeometryOwner{FormGeometryOwner{}}, false, "$/Form/ChildItems");
        auto root_pages = std::move(root_owner.pages);
        auto child_records = std::move(root_owner.child_table);
        auto form_incoming = std::move(root_owner.incoming);
        if (seen_control_ids.size() != document.collections().controls.size()) {
            fail("OOF1122", "$/Form/ChildItems", "every Control assigned to an owner child graph",
                std::to_string(document.collections().controls.size() - seen_control_ids.size()),
                "Document contains an unowned Control");
        }
        if (seen_page_ids.size() != document.collections().pages.size()) {
            fail("OOF1122", "$/Form/ChildItems", "every Page assigned to an owner child graph",
                std::to_string(document.collections().pages.size() - seen_page_ids.size()),
                "Document contains an unowned Page");
        }

        AttributesRecord attributes;
        if (document.form().main_attribute.id())
            attributes.main_attribute = model::CompositeIdValue{
                static_cast<std::int64_t>(document.form().main_attribute.id().value()),
                model::UuidValue{std::string(null_uuid)}, true};
        std::uint64_t max_attribute_id = 0;
        for (const auto& attribute : document.collections().attributes) {
            if (attribute.id.value() == 0 ||
                attribute.id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                fail("OOF1122", "$/Attributes", "positive int64 attribute ID", std::to_string(attribute.id.value()), "Attribute ID cannot be encoded");
            }
            attributes.attributes.push_back(AttributeRecord{
                model::CompositeIdValue{
                    static_cast<std::int64_t>(attribute.id.value()),
                    model::UuidValue{std::string(null_uuid)},
                    true,
                },
                attribute.main.value(),
                attribute.stored_data.value(),
                attribute.name,
                attribute.type,
            });
            max_attribute_id = std::max(max_attribute_id, attribute.id.value());
            max_id = std::max(max_id, attribute.id.value());
        }
        attributes.links = std::move(ordered_control_links);
        std::sort(attributes.links.begin(), attributes.links.end(), [](const auto& left, const auto& right) {
            return left.control_id < right.control_id;
        });
        std::size_t linked_control_count = 0;
        for (const auto& control : document.collections().controls) {
            if (control.data_path) {
                ++linked_control_count;
            }
        }
        if (linked_control_count != attributes.links.size()) {
            fail("OOF1122", "$/Form/ChildItems", "one DataPath link per InputField, CheckBox, ListBox, or bound ProgressBar",
                std::to_string(linked_control_count), "Linked control and DataPath link counts do not match");
        }
        if (max_id >= std::numeric_limits<std::uint32_t>::max()) {
            fail("OOF1120", "$/Attributes", "object IDs below uint32 max", std::to_string(max_id), "Attribute slot count overflows");
        }
        const bool empty_attributes = attributes.attributes.empty() && attributes.links.empty();
        attributes.slot_count = static_cast<std::uint32_t>(std::max<std::uint64_t>(
            3,
            (empty_attributes ? max_id : max_attribute_id) + 1));
        const auto encoded_attributes_result = encode_attributes(attributes);
        if (!encoded_attributes_result) {
            throw DecodeFailure(encoded_attributes_result.diagnostics().front());
        }

        auto root_panel_payload = encode_owner_pages(root_pages, form_incoming, false, std::nullopt, width, height,
            document.form().panel.properties);
        const auto root_control_count = child_records.items.size() - 1;
        const auto root_panel = list({
            raw(std::string(root_panel_guid)),
            std::move(root_panel_payload),
            std::move(child_records),
        });
        const std::uint32_t serialization_counter = static_cast<std::uint32_t>(
            3 + root_control_count * 6);
        const auto form_section = list({
            raw("18"),
            list({
                encoded_localized(caption),
                raw(std::to_string(max_id)),
                raw(std::to_string(default_command_bar_id(document))),
            }),
            root_panel,
            raw(std::to_string(width)),
            raw(std::to_string(height)),
            raw("1"),
            raw("0"),
            raw("1"),
            raw("4"),
            raw("4"),
            raw(std::to_string(serialization_counter)),
            raw(std::to_string(width)),
            raw(std::to_string(height)),
            raw("96"),
        });
        LV encoded_extension = parse_constant("{00000000-0000-0000-0000-000000000000,0}");
        if (document.form().extension) {
            const auto& descriptor = model::metamodel::data_processor_form_extension;
            encoded_extension = list({raw(std::string(descriptor.guid)), raw("1"),
                parse_constant("{2,0,{0,0},{0},1}")});
        }
        const auto encoded = list({
            raw("27"),
            form_section,
            encoded_attributes_result.value(),
            std::move(encoded_extension),
            encode_form_close_events(document),
            raw("1"),
            raw("4"),
            raw("1"),
            raw("0"),
            raw("0"),
            raw("0"),
            list({raw("0")}),
            list({raw("0")}),
            canonical_style_record(10),
            raw("1"),
            raw("2"),
            raw("0"),
            raw("0"),
            raw("1"),
            raw("1"),
        });

        const auto decoded = decode_document(encoded, document.form().name);
        if (!decoded) {
            throw DecodeFailure(decoded.diagnostics().front());
        }
        return encoded;
    });
}

}  // namespace oof::storage::form_stream
