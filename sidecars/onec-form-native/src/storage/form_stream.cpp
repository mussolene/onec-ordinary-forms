#include "oof/storage/form_stream.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

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

LV encoded_localized(std::string_view text) {
    model::LocalizedStringValue value;
    if (!text.empty()) {
        value.items.push_back({"ru", std::string(text)});
    }
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
    return decoded.items.empty() ? std::string{} : decoded.items.front().text;
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

LV canonical_root_panel_payload(std::int32_t width, std::int32_t height) {
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
    model::Page default_page;
    default_page.id = model::ObjectId{1};
    default_page.name = "Страница1";
    model::LocalizedStringValue default_title;
    default_title.items.push_back({"ru", "Страница1"});
    default_page.title.set(std::move(default_title));
    const auto page_table = encode_page_table(std::vector<model::Page>{std::move(default_page)});
    if (!page_table) throw DecodeFailure(page_table.diagnostics().front());
    value.items[1].items[11] = page_table.value();
    model::Position position;
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
    auto boundaries = encode_page_position(position, 0);
    if (!boundaries) throw DecodeFailure(boundaries.diagnostics().front());
    value.items[1].items.insert(value.items[1].items.begin() + 16,
        boundaries.value().items.begin(), boundaries.value().items.end());
    return value;
}

LV canonical_button_base(bool enabled) {
    auto value = parse_constant(
        "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,"
        "{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},"
        "{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"
        "{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}");
    value.items[1] = raw(enabled ? "1" : "0");
    return value;
}

LV canonical_button_properties(bool enabled, std::string_view caption, bool multi_line) {
    return list({
        canonical_button_base(enabled),
        raw("14"),
        encoded_localized(caption),
        raw("1"),
        raw("1"),
        raw("0"),
        raw("0"),
        raw("0"),
        parse_constant("{4,0,{0},\"\",-1,-1,1,0,\"\"}"),
        parse_constant("{0,0,0}"),
        raw(multi_line ? "1" : "0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("1"),
    });
}

LV canonical_label_properties(std::string_view caption, std::int32_t horizontal_align) {
    return list({
        parse_constant(
            "{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,"
            "{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},"
            "{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"
            "{1,0},0,0,100,2,2,1,2,{4,4,{0},4}}"),
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


LV canonical_check_box_info(bool enabled, std::string_view caption) {
    return list({
        raw("1"),
        list({
            list({
                canonical_button_base(enabled),
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


LV canonical_input_field_info(
    const model::TypeDomainPatternValue& type,
    bool enabled,
    bool read_only) {
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
    value.items[2].items[0].items[13] = raw(read_only ? "1" : "0");
    value.items[2].items[0].items[14] = raw(std::to_string(type.entries.front().string.length));
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
    std::uint32_t next = 0;
};

void validate_geometry_context(const GeometryContext& context, std::string_view path) {
    if (const auto* panel = std::get_if<model::ControlRef>(&context.owner);
        panel != nullptr && (panel->id().value() == 0 ||
            panel->id().value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
        fail("OOF1122", child_path(path, 0), "positive int64 Panel ID",
            std::to_string(panel->id().value()), "Geometry owner Panel ID is invalid");
    }
    if (context.sibling_ordinal == std::numeric_limits<std::uint32_t>::max()) {
        fail("OOF1122", child_path(path, 2), "sibling ordinal below uint32 maximum", "uint32 maximum",
            "Control geometry next index would overflow");
    }
}

GeometryContext root_geometry_context(std::size_t sibling_ordinal, std::string_view path) {
    if (sibling_ordinal >= std::numeric_limits<std::uint32_t>::max()) {
        fail("OOF1122", std::string(path), "sibling ordinal below uint32 maximum",
            std::to_string(sibling_ordinal), "Form child ordinal cannot be encoded");
    }
    return GeometryContext{FormGeometryOwner{}, 0, static_cast<std::uint32_t>(sibling_ordinal)};
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
    decoded.next = integer_atom<std::uint32_t>(geometry.items[cursor + 2], child_path(path, cursor + 2));
    if (context.sibling_ordinal == std::numeric_limits<std::uint32_t>::max() ||
        decoded.ordinal != context.sibling_ordinal || decoded.next != context.sibling_ordinal + 1) {
        fail("OOF1114", child_path(path, cursor + 1), "logical ordinal and next index", describe(geometry),
            "Geometry child ordinal or next index is inconsistent");
    }
    const bool manual_horizontal = bool_atom(geometry.items[cursor + 3], child_path(path, cursor + 3));
    const bool manual_vertical = bool_atom(geometry.items[cursor + 4], child_path(path, cursor + 4));
    if (manual_horizontal) position.bindings.manual_horizontal.set(true);
    if (manual_vertical) position.bindings.manual_vertical.set(true);
    return decoded;
}

std::uint32_t geometry_ordinal(
    const LV& geometry,
    std::string_view path,
    std::uint32_t expected_page,
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
    if (page != expected_page) {
        fail("OOF1114", child_path(path, cursor), "geometry page index matching its owner context",
            std::to_string(page), "Geometry page index does not match its owner context");
    }
    const auto ordinal = integer_atom<std::uint32_t>(geometry.items[cursor + 1], child_path(path, cursor + 1));
    ordinal_slot = cursor + 1;
    const auto next = integer_atom<std::uint32_t>(geometry.items[cursor + 2], child_path(path, cursor + 2));
    if (ordinal == std::numeric_limits<std::uint32_t>::max() || next != ordinal + 1) {
        fail("OOF1114", child_path(path, cursor + 2), "next index equal to ordinal plus one", std::to_string(next),
            "Geometry next index is inconsistent");
    }
    return ordinal;
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
    if (position.default_control.is_explicit() || position.tab_order.is_explicit() ||
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
    values.push_back(raw(std::to_string(context.sibling_ordinal + 1)));
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

bool is_single_boolean_type_domain(const model::TypeDomainPatternValue& value) {
    return value.entries.size() == 1 && value.entries.front().term == model::TypeDomainTerm::boolean;
}

LV canonical_event_table(std::optional<std::string_view> handler) {
    if (!handler) {
        return list({raw("0")});
    }
    const auto* descriptor = model::metamodel::find_event(model::ControlKind::button, "Click");
    if (descriptor == nullptr || descriptor->storage_tag.empty()) {
        throw std::logic_error("Button.Click has no executable storage tag");
    }
    const LV presentation = encoded_localized(*handler);
    return list({
        raw("1"),
        list({
            raw("0"),
            raw(std::string(descriptor->storage_tag)),
            list({
                raw("3"),
                string_value(std::string(*handler)),
                list({
                    raw("1"),
                    string_value(std::string(*handler)),
                    presentation,
                    presentation,
                    presentation,
                    parse_constant("{4,0,{0},\"\",-1,-1,1,0,\"\"}"),
                    parse_constant("{0,0,0}"),
                }),
            }),
        }),
    });
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

    const auto& payload = event_record.items[2];
    const std::string payload_path = child_path(event_path, 2);
    require_arity(payload, 3, payload_path);
    require_raw_constant(payload.items[0], "3", child_path(payload_path, 0));
    const std::string handler = string_atom(payload.items[1], child_path(payload_path, 1));
    if (handler.empty()) {
        fail(
            "OOF1115",
            child_path(payload_path, 1),
            "non-empty event handler",
            "empty",
            "Button.Click handler cannot be empty");
    }

    const auto& action = payload.items[2];
    const std::string action_path = child_path(payload_path, 2);
    require_arity(action, 7, action_path);
    require_raw_constant(action.items[0], "1", child_path(action_path, 0));
    const std::string action_handler = string_atom(action.items[1], child_path(action_path, 1));
    if (action_handler != handler) {
        fail(
            "OOF1114",
            child_path(action_path, 1),
            handler,
            action_handler,
            "Event action handler differs from the event record handler");
    }
    for (std::size_t index = 2; index <= 4; ++index) {
        const std::string presentation = decoded_single_language_text(action.items[index], child_path(action_path, index));
        if (presentation != handler) {
            fail("OOF1114", child_path(action_path, index), handler, presentation,
                "Button event action presentation differs from its handler; action presentation semantics are unsupported");
        }
    }
    require_exact(
        action.items[5],
        parse_constant("{4,0,{0},\"\",-1,-1,1,0,\"\"}"),
        child_path(action_path, 5),
        "Button event style record is unsupported");
    require_exact(
        action.items[6],
        parse_constant("{0,0,0}"),
        child_path(action_path, 6),
        "Button event tail record is unsupported");
    return handler;
}

struct DecodedControl {
    model::ControlNode control;
    std::optional<std::string> click_handler;
    IncomingAnchorLists incoming;
};

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
    require_arity(properties, 16, properties_path);
    const auto& base = properties.items[0];
    const std::string base_path = child_path(properties_path, 0);
    require_arity(base, 21, base_path);
    const bool enabled = bool_atom(base.items[1], child_path(base_path, 1));
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
        canonical_button_base(enabled),
        base_path,
        "Button base record contains an unsupported property variation");
    const std::string caption = decoded_single_language_text(
        properties.items[2],
        child_path(properties_path, 2));
    const bool multi_line = bool_atom(
        properties.items[10], child_path(properties_path, 10));
    auto normalized_properties = properties;
    normalized_properties.items[0] = std::move(normalized_base);
    require_exact(
        normalized_properties,
        canonical_button_properties(enabled, caption, multi_line),
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
    if (multi_line) {
        control.properties().set_explicit(model::PropertyId::from_name("MultiLine"), true);
    }
    if (!enabled) {
        control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    }
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), click_handler, std::move(decoded_geometry.incoming)};
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
    const std::string caption = decoded_single_language_text(
        properties.items[2], child_path(properties_path, 2));
    const std::int32_t horizontal_align = integer_atom<std::int32_t>(
        properties.items[3], child_path(properties_path, 3));
    if (horizontal_align != 0 && horizontal_align != 4) {
        fail("OOF1122", child_path(properties_path, 3), "HorizontalAlign storage value 0 (Left) or 4 (Auto)",
            std::to_string(horizontal_align), "LabelDecoration.HorizontalAlign storage value is unsupported");
    }
    auto normalized_properties = properties;
    normalized_properties.items[2] = encoded_localized(caption);
    require_exact(
        normalized_properties,
        canonical_label_properties(caption, horizontal_align),
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
    control.properties().set_explicit(
        model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{
            "HorizontalAlign", horizontal_align == 4 ? "Auto" : "Left"});
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming)};
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
    const std::string caption = decoded_single_language_text(
        properties.items[2], child_path(properties_path, 2));
    auto normalized_info = info;
    normalized_info.items[1].items[0].items[2] = encoded_localized(caption);
    require_exact(
        normalized_info,
        canonical_check_box_info(enabled, caption),
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
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming)};
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
    const bool read_only = bool_atom(payload.items[13], child_path(payload_path, 13));
    require_exact(
        info,
        canonical_input_field_info(control_type, enabled, read_only),
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
    if (read_only) control.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
    control.position = std::move(decoded_geometry.position);
    return {std::move(control), std::nullopt, std::move(decoded_geometry.incoming)};
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
    std::string_view default_value = {}) {
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
    std::int32_t default_value) {
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
            fail("OOF1121", "$", "integral decimal property", decimal, "Storage dimension must be an integer");
        }
    } else {
        fail("OOF1121", "$", "integer property", "different value kind", "Storage encoder received an invalid property value");
    }
    if (parsed < std::numeric_limits<std::int32_t>::min() ||
        parsed > std::numeric_limits<std::int32_t>::max()) {
        fail("OOF1120", "$", "int32 property", std::to_string(parsed), "Storage integer is out of range");
    }
    return static_cast<std::int32_t>(parsed);
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
        control.position.tab_order.is_explicit() || control.position.z_order.is_explicit() ||
        control.position.collapse.is_explicit() || !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$", "plain top-level Button", control.name, "Button uses a storage concept outside the executable slice");
    }
    require_allowed_properties(control.properties(), {"Caption", "Enabled", "MultiLine"}, "$/Button");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string caption = explicit_string(control.properties(), "Caption");
    const bool multi_line = explicit_bool(control.properties(), "MultiLine", false);
    const auto handler = button_click_handler(document, control);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::button);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        list({
            raw("1"),
            canonical_button_properties(enabled, caption, multi_line),
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

LV encode_label(const model::ControlNode& control, const GeometryContext& context) {
    if (control.kind() != model::ControlKind::label_decoration ||
        control.id.value() == 0 || control.id.value() > std::numeric_limits<std::int64_t>::max()) {
        fail("OOF1122", "$/Form/ChildItems", "LabelDecoration with positive int64 ID", control.name, "LabelDecoration is outside the supported profile");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || !control.events.empty() ||
        control.position.default_control.is_explicit() || control.position.tab_order.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/LabelDecoration", "plain top-level LabelDecoration", control.name, "LabelDecoration uses a storage concept outside the executable slice");
    }
    require_allowed_properties(control.properties(), {"Caption", "HorizontalAlign"}, "$/LabelDecoration");
    const std::string caption = explicit_string(control.properties(), "Caption");
    std::int32_t horizontal_align = 0;
    if (const auto* entry = control.properties().find(model::PropertyId::from_name("HorizontalAlign"))) {
        if (!std::holds_alternative<model::EnumerationValue>(entry->value)) {
            fail("OOF1122", "$/LabelDecoration/HorizontalAlign", "EnumerationValue of HorizontalAlign",
                "non-enumeration", "LabelDecoration.HorizontalAlign has the wrong value type");
        }
        const auto& value = std::get<model::EnumerationValue>(entry->value);
        if (value.type_name != "HorizontalAlign" ||
            (value.member != "Auto" && value.member != "Left")) {
            fail("OOF1122", "$/LabelDecoration/HorizontalAlign", "HorizontalAlign Auto or Left",
                value.type_name + "." + value.member, "LabelDecoration.HorizontalAlign value is unsupported");
        }
        horizontal_align = value.member == "Auto" ? 4 : 0;
    }
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::label_decoration);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        list({raw("3"), canonical_label_properties(caption, horizontal_align), list({raw("0")})}),
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
        control.position.default_control.is_explicit() || control.position.tab_order.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/CheckBox", "named CheckBox with direct DataPath and plain Position", control.name,
            "CheckBox uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"Enabled", "Caption"}, "$/CheckBox");
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
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::check_box);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_check_box_info(enabled, caption),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
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
        control.position.default_control.is_explicit() || control.position.tab_order.is_explicit() ||
        control.position.z_order.is_explicit() || control.position.collapse.is_explicit() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$/InputField", "named InputField with direct DataPath and plain Position", control.name, "InputField uses a storage concept outside the supported profile");
    }
    require_allowed_properties(control.properties(), {"Enabled", "ReadOnly"}, "$/InputField");
    const auto* attribute = document.find_attribute(control.data_path->attribute.id());
    if (attribute == nullptr) {
        fail("OOF1123", "$/InputField/DataPath", "existing linked Attribute", std::to_string(control.data_path->attribute.id().value()), "InputField DataPath does not resolve");
    }
    if (!is_single_string_type_domain(attribute->type)) {
        fail("OOF1122", "$/InputField/DataPath", "linked single-string Attribute", attribute->name, "InputField type is outside the supported profile");
    }
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const bool read_only = explicit_bool(control.properties(), "ReadOnly", false);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::input_field);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        canonical_input_field_info(attribute->type, enabled, read_only),
        encode_geometry(control.position, context, IncomingAnchorLists{}),
        list({raw("14"), string_value(control.name), raw("4294967295"), raw("0"), raw("0"), raw("0")}),
        list({raw("0")}),
    });
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
        const auto& version = at(record, 0, path);
        require_arity(version, 1, child_path(path, 0));
        require_raw_constant(version.items[0], "-1", child_path(child_path(path, 0), 0));

        AttributesRecord result;
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
            list_stream::ListValue::list({list_stream::ListValue::raw_atom("-1")}),
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

        require_exact(
            payload.items[3],
            parse_constant("{00000000-0000-0000-0000-000000000000,0}"),
            "$/3",
            "Unsupported root identity record");
        require_exact(payload.items[4], list({raw("0")}), "$/4", "Unsupported root record");
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
        require_raw_constant(header.items[2], "4294967295", "$/1/1/2");

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
        const auto expected_root_panel_envelope = canonical_root_panel_payload(width, height);
        require_exact(root_panel_envelope.items[0], expected_root_panel_envelope.items[0], "$/1/2/1/0",
            "Root panel envelope marker is unsupported");
        require_exact(root_panel_envelope.items[2], expected_root_panel_envelope.items[2], "$/1/2/1/2",
            "Root panel envelope trailer is unsupported");
        const auto& root_panel_payload = root_panel_envelope.items[1];
        std::size_t root_incoming_end = 2;
        const auto root_incoming = decode_incoming_anchor_lists(root_panel_payload, root_incoming_end, "$/1/2/1");
        auto normalized_root_panel_payload = root_panel_payload;
        normalized_root_panel_payload.items.erase(
            normalized_root_panel_payload.items.begin() + 2,
            normalized_root_panel_payload.items.begin() + static_cast<std::ptrdiff_t>(root_incoming_end));
        auto expected_root_panel_payload = expected_root_panel_envelope.items[1];
        expected_root_panel_payload.items.erase(
            expected_root_panel_payload.items.begin() + 2,
            expected_root_panel_payload.items.begin() + 8);
        require_exact(
            normalized_root_panel_payload,
            expected_root_panel_payload,
            "$/1/2/1",
            "Root panel payload differs from the proven canonical layout after anchor fanout");

        const auto attributes_result = decode_attributes(payload.items[2]);
        if (!attributes_result) {
            throw DecodeFailure(attributes_result.diagnostics().front());
        }
        const auto& attributes = attributes_result.value();

        model::Form form;
        form.id = model::ObjectId{1};
        form.name = std::string(form_name);
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

        const auto& children = root_panel.items[2];
        require_list(children, "$/1/2/2");
        if (children.items.empty()) {
            fail("OOF1103", "$/1/2/2/0", "control count", "missing", "Root control table has no count");
        }
        const std::uint32_t control_count = integer_atom<std::uint32_t>(children.items[0], "$/1/2/2/0");
        if (children.items.size() != static_cast<std::size_t>(control_count) + 1) {
            fail(
                "OOF1102",
                "$/1/2/2",
                "list arity " + std::to_string(static_cast<std::size_t>(control_count) + 1),
                "list arity " + std::to_string(children.items.size()),
                "Root control table count does not match its records");
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
        std::vector<std::optional<DecodedControl>> decoded_controls(control_count);
        const auto& input_descriptor = model::metamodel::descriptor_for(model::ControlKind::input_field);
        for (std::uint32_t index = 0; index < control_count; ++index) {
            const auto path = child_path("$/1/2/2", static_cast<std::size_t>(index) + 1);
            const auto& child_record = children.items[index + 1];
            if (!child_record.is_list || child_record.items.empty()) {
                require_arity(child_record, 1, path);
            }
            static_cast<void>(raw_atom(at(child_record, 1, path), child_path(path, 1)));
            const auto geometry_path = child_path(path, 3);
            const auto& geometry = at(child_record, 3, path);
            std::size_t ordinal_slot = 0;
            const auto logical_index = geometry_ordinal(geometry, geometry_path, 0, ordinal_slot);
            if (logical_index >= control_count) {
                fail("OOF1114", child_path(geometry_path, ordinal_slot), "ChildItems ordinal below control count", std::to_string(logical_index), "Control geometry ordinal is outside the ChildItems range");
            }
            if (decoded_controls[logical_index]) {
                fail("OOF1114", child_path(geometry_path, ordinal_slot), "unique ChildItems ordinal", std::to_string(logical_index), "Control geometry ordinal is duplicated");
            }
            const GeometryContext geometry_context{FormGeometryOwner{}, 0, logical_index};
            const std::string child_guid = raw_atom(child_record.items[0], child_path(path, 0));
            const auto& button_descriptor = model::metamodel::descriptor_for(model::ControlKind::button);
            const auto& label_descriptor = model::metamodel::descriptor_for(model::ControlKind::label_decoration);
            if (child_guid == button_descriptor.guid) {
                auto decoded = decode_button(child_record, path, geometry_context);
                actual_max_id = std::max(actual_max_id, decoded.control.id.value());
                decoded_controls[logical_index].emplace(std::move(decoded));
            } else if (child_guid == label_descriptor.guid) {
                auto decoded = decode_label(child_record, path, geometry_context);
                actual_max_id = std::max(actual_max_id, decoded.control.id.value());
                decoded_controls[logical_index].emplace(std::move(decoded));
            } else if (child_guid == input_descriptor.guid ||
                       child_guid == model::metamodel::descriptor_for(model::ControlKind::check_box).guid) {
                const auto candidate_id = integer_atom<std::uint64_t>(at(child_record, 1, path), child_path(path, 1));
                if (candidate_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    fail("OOF1122", child_path(path, 1), "linked control ID representable in int64", std::to_string(candidate_id),
                        "Control ID cannot be resolved through the attribute-link table");
                }
                const auto candidate_key = static_cast<std::int64_t>(candidate_id);
                const auto link_it = links_by_control.find(candidate_key);
                if (link_it == links_by_control.end()) {
                    fail("OOF1122", "$/2/3", "DataPath link for each InputField or CheckBox",
                        std::to_string(candidate_id), "Linked control has no attribute link");
                }
                consumed_link_ids.insert(candidate_key);
                const auto& link = *link_it->second;
                if (!link.attribute_id.is_null || link.attribute_id.uuid.canonical != null_uuid) {
                    fail("OOF1122", "$/2/3", "null-UUID attribute link", describe(child_record),
                        "Control DataPath link uses an unsupported target");
                }
                const auto attribute_it = attributes_by_id.find(link.attribute_id.object_id);
                if (attribute_it == attributes_by_id.end()) {
                    fail("OOF1122", "$/2/3", "link to an existing Attribute",
                        std::to_string(link.attribute_id.object_id), "DataPath target is unresolved");
                }
                const auto& attribute = *attribute_it->second;
                DecodedControl linked_control = child_guid == input_descriptor.guid
                    ? decode_input_field(child_record, path, attribute, geometry_context)
                    : decode_check_box(child_record, path, attribute, geometry_context);
                linked_control.control.data_path = model::DataPath{
                    model::AttributeRef{model::ObjectId{static_cast<std::uint64_t>(attribute.id.object_id)}},
                    {},
                };
                actual_max_id = std::max(actual_max_id, linked_control.control.id.value());
                decoded_controls[logical_index].emplace(std::move(linked_control));
            } else {
                fail("OOF1122", path, "supported top-level Button, LabelDecoration, InputField, or CheckBox record", child_guid, "Control payload is unsupported");
            }
        }
        for (std::size_t index = 0; index < decoded_controls.size(); ++index) {
            if (!decoded_controls[index]) {
                fail("OOF1114", "$/1/2/2", "permutation of ChildItems ordinals", std::to_string(index), "Control geometry ordinals do not cover every ChildItems position");
            }
        }
        std::unordered_map<std::uint64_t, IncomingAnchorLists> expected_control_incoming;
        IncomingAnchorLists expected_form_incoming;
        std::unordered_set<std::uint64_t> decoded_control_ids;
        for (const auto& decoded_slot : decoded_controls) {
            decoded_control_ids.insert(decoded_slot->control.id.value());
        }
        for (const auto& decoded_slot : decoded_controls) {
            const auto source_id = decoded_slot->control.id.value();
            for (const auto& binding : decoded_slot->control.position.bindings.anchors) {
                const auto source_edge = source_platform_edge(binding.coordinate, "$/Position/Bindings/coordinate");
                const auto append_target = [&](const std::optional<model::ControlRef>& target, model::BindingCoordinate coordinate) {
                    const auto target_edge = target_dependency_bucket(
                        target_platform_edge(coordinate, "$/Position/Bindings/targetCoordinate"),
                        "$/Position/Bindings/targetCoordinate");
                    if (!target.has_value()) {
                        expected_form_incoming[target_edge].push_back({source_id, source_edge});
                        return;
                    }
                    const auto target_id = target->id().value();
                    if (!decoded_control_ids.contains(target_id)) {
                        fail("OOF1114", "$/1/2/2", "binding target resolving to a decoded control", std::to_string(target_id),
                            "Primary or proportional binding target is dangling or unsupported");
                    }
                    expected_control_incoming[target_id][target_edge].push_back({source_id, source_edge});
                };
                append_target(binding.target, binding.target_coordinate);
                if (binding.proportional.has_value()) {
                    append_target(binding.proportional->target, binding.proportional->coordinate);
                }
            }
        }
        require_incoming_graph(root_incoming, std::move(expected_form_incoming), "$/1/2/1");
        for (std::size_t index = 0; index < decoded_controls.size(); ++index) {
            const auto& decoded_slot = decoded_controls[index];
            const auto control_id = decoded_slot->control.id.value();
            const auto expected = expected_control_incoming.find(control_id);
            const IncomingAnchorLists empty;
            require_incoming_graph(
                decoded_slot->incoming,
                expected == expected_control_incoming.end() ? empty : expected->second,
                child_path("$/1/2/2", index + 1) + "/3");
        }
        if (consumed_link_ids.size() != links_by_control.size()) {
            fail("OOF1122", "$/2/3", "one matching link per decoded InputField or CheckBox", std::to_string(links_by_control.size() - consumed_link_ids.size()), "Attribute-link table contains unconsumed links");
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
        // допускаются значение свежего Designer (1) и значение текущего сборщика.
        const std::string expected_slot_count = empty_attributes
            ? "1 or " + std::to_string(expected_slots)
            : std::to_string(expected_slots);
        if (expected_slots > std::numeric_limits<std::uint32_t>::max() ||
            (attributes.slot_count != expected_slots &&
             !(empty_attributes && attributes.slot_count == 1))) {
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
        for (auto& decoded_slot : decoded_controls) {
            auto& decoded_control = *decoded_slot;
            if (decoded_control.click_handler) {
                if (synthetic_event_offset >=
                    std::numeric_limits<std::uint64_t>::max() - stored_max_id) {
                    fail("OOF1120", "$/1/1/1", "allocatable event ID", "uint64 max", "Synthetic event ID overflows");
                }
                const model::ObjectId event_id{stored_max_id + ++synthetic_event_offset};
                decoded_control.control.events.push_back(model::EventRef{event_id});
                document.add_event(model::Event{
                    event_id,
                    "Click",
                    *decoded_control.click_handler,
                    model::ControlRef{decoded_control.control.id},
                });
            }
            form.children.push_back(model::ControlRef{decoded_control.control.id});
            document.add_control(std::move(decoded_control.control));
        }
        document.set_form(std::move(form));

        const auto report = document.validate();
        if (!report.ok()) {
            fail(
                "OOF1123",
                "$",
                "valid OrdinaryFormDocument",
                std::to_string(report.violations.size()) + " invariant violations",
                "Decoded storage does not satisfy the product object model");
        }
        return document;
    });
}

Result<list_stream::ListValue> encode_document(
    const model::OrdinaryFormDocument& document) {
    return capture_decode_failure<list_stream::ListValue>([&document] {
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
        if (!document.assets().empty() || !document.collections().pages.empty() ||
            !document.collections().commands.empty() || !document.form().events.empty()) {
            fail(
                "OOF1122",
                "$",
                "no pictures, pages, commands, or form events",
                "unsupported document collections",
                "Document contains a storage concept without an executable codec");
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

        std::vector<LV> child_records;
        child_records.push_back(raw(std::to_string(document.form().children.size())));
        std::vector<form_stream::AttributeLink> ordered_control_links;
        std::unordered_map<std::uint64_t, std::size_t> ordinal_by_control;
        std::unordered_map<std::uint64_t, IncomingAnchorLists> control_incoming;
        IncomingAnchorLists form_incoming;
        std::uint64_t max_id = document.form().id.value();
        for (std::size_t sibling_index = 0; sibling_index < document.form().children.size(); ++sibling_index) {
            const auto geometry_context = root_geometry_context(sibling_index, "$/Form/ChildItems");
            const auto& child = document.form().children[sibling_index];
            if (!std::holds_alternative<model::ControlRef>(child)) {
                fail("OOF1122", "$/Form/ChildItems", "Button reference", "Page reference", "Page storage is not implemented");
            }
            const auto control_id = std::get<model::ControlRef>(child).id();
            const auto* control = document.find_control(control_id);
            if (control == nullptr) {
                fail("OOF1123", "$/Form/ChildItems", "existing control", std::to_string(control_id.value()), "Child reference is dangling");
            }
            ordinal_by_control.emplace(control_id.value(), sibling_index);
            if (control->kind() == model::ControlKind::button) {
                child_records.push_back(encode_button(document, *control, geometry_context));
            } else if (control->kind() == model::ControlKind::label_decoration) {
                child_records.push_back(encode_label(*control, geometry_context));
            } else if (control->kind() == model::ControlKind::input_field) {
                child_records.push_back(encode_input_field(document, *control, geometry_context));
            } else if (control->kind() == model::ControlKind::check_box) {
                child_records.push_back(encode_check_box(document, *control, geometry_context));
            } else {
                fail("OOF1122", "$/Form/ChildItems", "supported top-level Button, LabelDecoration, InputField, or CheckBox", control->name, "Control payload is unsupported");
            }
            if (control->kind() == model::ControlKind::input_field ||
                control->kind() == model::ControlKind::check_box) {
                ordered_control_links.push_back(form_stream::AttributeLink{
                    static_cast<std::int64_t>(control->id.value()),
                    model::CompositeIdValue{
                        static_cast<std::int64_t>(control->data_path->attribute.id().value()),
                        model::UuidValue{std::string(null_uuid)}, true},
                });
            }
            max_id = std::max(max_id, control_id.value());
        }
        for (const auto& child : document.form().children) {
            const auto control_id = std::get<model::ControlRef>(child).id();
            const auto* control = document.find_control(control_id);
            if (control == nullptr) {
                fail("OOF1123", "$/Form/ChildItems", "existing control", std::to_string(control_id.value()), "Child reference is dangling");
            }
            for (const auto& binding : control->position.bindings.anchors) {
                const auto source_edge = source_platform_edge(binding.coordinate, "$/Position/Bindings/coordinate");
                const auto append_target = [&](const std::optional<model::ControlRef>& target, model::BindingCoordinate coordinate) {
                    const auto target_edge = target_dependency_bucket(
                        target_platform_edge(coordinate, "$/Position/Bindings/targetCoordinate"),
                        "$/Position/Bindings/targetCoordinate");
                    if (!target.has_value()) {
                        form_incoming[target_edge].push_back({control_id.value(), source_edge});
                        return;
                    }
                    const auto target_id = target->id().value();
                    if (!ordinal_by_control.contains(target_id)) {
                        fail("OOF1123", "$/Position/Bindings/targetId", "target control in encoded root ChildItems",
                            std::to_string(target_id), "Primary or proportional binding target is outside the encoded control graph");
                    }
                    control_incoming[target_id][target_edge].push_back({control_id.value(), source_edge});
                };
                append_target(binding.target, binding.target_coordinate);
                if (binding.proportional.has_value()) {
                    append_target(binding.proportional->target, binding.proportional->coordinate);
                }
            }
        }
        for (std::size_t index = 1; index < child_records.size(); ++index) {
            const auto control_id = integer_atom<std::uint64_t>(child_records[index].items[1], "$/Form/ChildItems");
            const auto* control = document.find_control(model::ObjectId{control_id});
            const auto incoming = control_incoming.find(control_id);
            const IncomingAnchorLists empty;
            child_records[index].items[3] = encode_geometry(
                control->position,
                root_geometry_context(ordinal_by_control.at(control_id), "$/Form/ChildItems"),
                incoming == control_incoming.end() ? empty : incoming->second);
        }
        std::sort(child_records.begin() + 1, child_records.end(), [](const LV& left, const LV& right) {
            return integer_atom<std::uint64_t>(left.items[1], "$/Form/ChildItems") <
                   integer_atom<std::uint64_t>(right.items[1], "$/Form/ChildItems");
        });

        AttributesRecord attributes;
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
            if (control.kind() == model::ControlKind::input_field ||
                control.kind() == model::ControlKind::check_box) {
                ++linked_control_count;
            }
        }
        if (linked_control_count != attributes.links.size()) {
            fail("OOF1122", "$/Form/ChildItems", "one DataPath link per InputField or CheckBox",
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

        auto root_panel_payload = canonical_root_panel_payload(width, height);
        insert_incoming_anchor_lists(root_panel_payload.items[1], form_incoming, 2);
        const auto root_panel = list({
            raw(std::string(root_panel_guid)),
            std::move(root_panel_payload),
            list(std::move(child_records)),
        });
        const std::uint32_t serialization_counter = static_cast<std::uint32_t>(
            3 + document.form().children.size() * 6);
        const auto form_section = list({
            raw("18"),
            list({
                encoded_localized(caption),
                raw(std::to_string(max_id)),
                raw("4294967295"),
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
        const auto encoded = list({
            raw("27"),
            form_section,
            encoded_attributes_result.value(),
            parse_constant("{00000000-0000-0000-0000-000000000000,0}"),
            list({raw("0")}),
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
