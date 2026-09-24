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
{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},26,0,0,0,0,0,0,{10,1,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},0,1,{1,1,{6,{1,1,{"ru","Страница1"}},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},-1,1,1,"Страница1",1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},1}},1,1,0,4,{2,8,1,1,1,0,0,0,0},{2,8,0,1,2,0,0,0,0},{2,392,1,1,3,0,0,8,0},{2,292,0,1,4,0,0,8,0},0,4294967295,5,64,0,{4,4,{0},4},0,0,57,0,0},{0}}
)OOF");
    if (!value.is_list || value.items.size() != 3 ||
        !value.items[1].is_list || value.items[1].items.size() != 31 ||
        !value.items[1].items[18].is_list || value.items[1].items[18].items.size() < 2 ||
        !value.items[1].items[19].is_list || value.items[1].items[19].items.size() < 2) {
        throw std::logic_error("canonical root-panel codec template is malformed");
    }
    value.items[1].items[18].items[1] = raw(std::to_string(width - 8));
    value.items[1].items[19].items[1] = raw(std::to_string(height - 8));
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

LV canonical_button_properties(bool enabled, std::string_view caption) {
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
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("1"),
    });
}

LV canonical_button_geometry(
    std::int32_t left,
    std::int32_t top,
    std::int32_t width,
    std::int32_t height,
    bool visible,
    std::size_t sibling_index) {
    if (width < 0 || height < 0 ||
        left > std::numeric_limits<std::int32_t>::max() - width ||
        top > std::numeric_limits<std::int32_t>::max() - height) {
        fail(
            "OOF1120",
            "$/1/2",
            "non-negative geometry without int32 overflow",
            std::to_string(left) + "," + std::to_string(top) + "," +
                std::to_string(width) + "," + std::to_string(height),
            "Button geometry cannot be represented by the platform record");
    }
    auto value = parse_constant(
        "{8,0,0,0,0,1,"
        "{0,{2,-1,6,0},{2,-1,6,0}},"
        "{0,{2,2,0,0},{2,-1,6,0}},"
        "{0,{2,-1,6,0},{2,-1,6,0}},"
        "{0,{2,2,2,0},{2,-1,6,0}},"
        "{0,{2,-1,6,0},{2,-1,6,0}},"
        "{0,{2,-1,6,0},{2,-1,6,0}},"
        "1,{0,2,1},0,1,{0,2,3},0,0,0,0,0,1,0,0}");
    value.items[1] = raw(std::to_string(left));
    value.items[2] = raw(std::to_string(top));
    value.items[3] = raw(std::to_string(left + width));
    value.items[4] = raw(std::to_string(top + height));
    value.items[5] = raw(visible ? "1" : "0");
    value.items[21] = raw(std::to_string(sibling_index));
    value.items[7].items[1].items[3] = raw(std::to_string(height));
    value.items[9].items[1].items[3] = raw(std::to_string(width));
    return value;
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
        static_cast<void>(decoded_single_language_text(action.items[index], child_path(action_path, index)));
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

struct DecodedButton {
    model::ControlNode control;
    std::optional<std::string> click_handler;
};

DecodedButton decode_button(const LV& record, std::string_view path, std::size_t sibling_index) {
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
    auto normalized_properties = properties;
    normalized_properties.items[0] = std::move(normalized_base);
    require_exact(
        normalized_properties,
        canonical_button_properties(enabled, caption),
        properties_path,
        "Button payload contains an unsupported property variation");

    const auto click_handler = decode_button_event(info.items[2], child_path(info_path, 2));

    const auto& geometry = record.items[3];
    const std::string geometry_path = child_path(path, 3);
    require_arity(geometry, 25, geometry_path);
    require_raw_constant(geometry.items[0], "8", child_path(geometry_path, 0));
    const std::int32_t left = integer_atom<std::int32_t>(geometry.items[1], child_path(geometry_path, 1));
    const std::int32_t top = integer_atom<std::int32_t>(geometry.items[2], child_path(geometry_path, 2));
    const std::int32_t right = integer_atom<std::int32_t>(geometry.items[3], child_path(geometry_path, 3));
    const std::int32_t bottom = integer_atom<std::int32_t>(geometry.items[4], child_path(geometry_path, 4));
    const bool visible = bool_atom(geometry.items[5], child_path(geometry_path, 5));
    if (right < left || bottom < top) {
        fail(
            "OOF1120",
            geometry_path,
            "right >= left and bottom >= top",
            describe(geometry),
            "Button geometry has negative dimensions");
    }
    const std::int64_t width64 = static_cast<std::int64_t>(right) - left;
    const std::int64_t height64 = static_cast<std::int64_t>(bottom) - top;
    if (width64 > std::numeric_limits<std::int32_t>::max() ||
        height64 > std::numeric_limits<std::int32_t>::max()) {
        fail("OOF1120", geometry_path, "int32 dimensions", describe(geometry), "Button geometry overflows int32");
    }
    const auto width = static_cast<std::int32_t>(width64);
    const auto height = static_cast<std::int32_t>(height64);
    require_exact(
        geometry,
        canonical_button_geometry(left, top, width, height, visible, sibling_index),
        geometry_path,
        "Button geometry contains unsupported bindings or flags");

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
    if (!enabled) {
        control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    }
    if (left != 0) {
        control.position.left.set(left);
    }
    if (top != 0) {
        control.position.top.set(top);
    }
    if (width != 0) {
        control.position.width.set(width);
    }
    if (height != 0) {
        control.position.height.set(height);
    }
    if (!visible) {
        control.position.visible.set(false);
    }
    return {std::move(control), click_handler};
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
    std::size_t sibling_index) {
    if (control.kind() != model::ControlKind::button || control.id.value() == 0 ||
        control.id.value() > std::numeric_limits<std::int64_t>::max()) {
        fail("OOF1122", "$", "Button with positive int64 ID", std::to_string(control.id.value()), "Unsupported control record");
    }
    if (control.name.empty() || control.data_path || !control.extension_properties.empty() ||
        !control.children.empty() || control.position.default_control.is_explicit() ||
        control.position.tab_order.is_explicit() || control.position.z_order.is_explicit() ||
        control.position.collapse.is_explicit() || !control.position.bindings.anchors.empty() ||
        !control.position.bindings.dimensions.empty()) {
        fail("OOF1122", "$", "plain top-level Button", control.name, "Button uses a storage concept outside the executable slice");
    }
    require_allowed_properties(control.properties(), {"Caption", "Enabled"}, "$/Button");
    const bool enabled = explicit_bool(control.properties(), "Enabled", true);
    const std::string caption = explicit_string(control.properties(), "Caption");
    const auto handler = button_click_handler(document, control);
    const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::button);
    return list({
        raw(std::string(descriptor.guid)),
        raw(std::to_string(control.id.value())),
        list({
            raw("1"),
            canonical_button_properties(enabled, caption),
            canonical_event_table(handler),
        }),
        canonical_button_geometry(
            control.position.left.value(),
            control.position.top.value(),
            control.position.width.value(),
            control.position.height.value(),
            control.position.visible.value(),
            sibling_index),
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

}  // namespace

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
        require_exact(
            root_panel.items[1],
            canonical_root_panel_payload(width, height),
            "$/1/2/1",
            "Root panel payload differs from the proven canonical layout");

        const auto attributes_result = decode_attributes(payload.items[2]);
        if (!attributes_result) {
            throw DecodeFailure(attributes_result.diagnostics().front());
        }
        const auto& attributes = attributes_result.value();
        if (!attributes.links.empty()) {
            fail(
                "OOF1122",
                "$/2/3",
                "empty attribute-link table for the Button slice",
                std::to_string(attributes.links.size()),
                "DataPath storage is not part of this executable slice");
        }

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
        std::vector<DecodedButton> decoded_buttons;
        decoded_buttons.reserve(control_count);
        for (std::uint32_t index = 0; index < control_count; ++index) {
            const auto path = child_path("$/1/2/2", static_cast<std::size_t>(index) + 1);
            decoded_buttons.push_back(decode_button(children.items[index + 1], path, index));
            actual_max_id = std::max(actual_max_id, decoded_buttons.back().control.id.value());
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
        for (auto& decoded_button : decoded_buttons) {
            if (decoded_button.click_handler) {
                if (synthetic_event_offset >=
                    std::numeric_limits<std::uint64_t>::max() - stored_max_id) {
                    fail("OOF1120", "$/1/1/1", "allocatable event ID", "uint64 max", "Synthetic event ID overflows");
                }
                const model::ObjectId event_id{stored_max_id + ++synthetic_event_offset};
                decoded_button.control.events.push_back(model::EventRef{event_id});
                document.add_event(model::Event{
                    event_id,
                    "Click",
                    *decoded_button.click_handler,
                    model::ControlRef{decoded_button.control.id},
                });
            }
            form.children.push_back(model::ControlRef{decoded_button.control.id});
            document.add_control(std::move(decoded_button.control));
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
        std::uint64_t max_id = document.form().id.value();
        for (std::size_t sibling_index = 0; sibling_index < document.form().children.size(); ++sibling_index) {
            const auto& child = document.form().children[sibling_index];
            if (!std::holds_alternative<model::ControlRef>(child)) {
                fail("OOF1122", "$/Form/ChildItems", "Button reference", "Page reference", "Page storage is not implemented");
            }
            const auto control_id = std::get<model::ControlRef>(child).id();
            const auto* control = document.find_control(control_id);
            if (control == nullptr) {
                fail("OOF1123", "$/Form/ChildItems", "existing control", std::to_string(control_id.value()), "Child reference is dangling");
            }
            child_records.push_back(encode_button(document, *control, sibling_index));
            max_id = std::max(max_id, control_id.value());
        }

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

        const auto root_panel = list({
            raw(std::string(root_panel_guid)),
            canonical_root_panel_payload(width, height),
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
