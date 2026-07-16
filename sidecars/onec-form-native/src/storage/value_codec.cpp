#include "oof/storage/value_codec.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace oof::storage::value_codec {
namespace {

constexpr std::uint32_t localized_string_version = 1;
constexpr std::uint32_t formatted_string_version = 1;
constexpr std::uint32_t color_field_count = 3;
constexpr std::uint32_t font_field_count = 6;
constexpr std::string_view type_domain_root = "Pattern";
constexpr std::string_view null_uuid = "00000000-0000-0000-0000-000000000000";

std::string_view term_token(model::TypeDomainTerm term) {
    switch (term) {
        case model::TypeDomainTerm::unknown:
            return "#";
        case model::TypeDomainTerm::list:
            return "L";
        case model::TypeDomainTerm::binary:
            return "B";
        case model::TypeDomainTerm::date:
            return "D";
        case model::TypeDomainTerm::numeric:
            return "N";
        case model::TypeDomainTerm::reference:
            return "R";
        case model::TypeDomainTerm::string:
            return "S";
        case model::TypeDomainTerm::type:
            return "T";
    }
    throw std::runtime_error("unsupported type-domain term");
}

model::TypeDomainTerm parse_term(std::string_view token) {
    if (token == "#") {
        return model::TypeDomainTerm::unknown;
    }
    if (token == "L") {
        return model::TypeDomainTerm::list;
    }
    if (token == "B") {
        return model::TypeDomainTerm::binary;
    }
    if (token == "D") {
        return model::TypeDomainTerm::date;
    }
    if (token == "N") {
        return model::TypeDomainTerm::numeric;
    }
    if (token == "R") {
        return model::TypeDomainTerm::reference;
    }
    if (token == "S") {
        return model::TypeDomainTerm::string;
    }
    if (token == "T") {
        return model::TypeDomainTerm::type;
    }
    throw std::runtime_error("unsupported type-domain term " + std::string(token));
}

bool is_null_uuid(const model::UuidValue& value) noexcept {
    return value.canonical == null_uuid;
}

std::uint32_t color_kind_code(model::ColorKind kind) {
    switch (kind) {
        case model::ColorKind::absolute:
            return 0;
        case model::ColorKind::automatic:
            return 1;
        case model::ColorKind::style_reference:
            return 2;
    }
    throw std::runtime_error("unsupported color kind");
}

model::ColorKind parse_color_kind(std::uint32_t code) {
    switch (code) {
        case 0:
            return model::ColorKind::absolute;
        case 1:
            return model::ColorKind::automatic;
        case 2:
            return model::ColorKind::style_reference;
        default:
            throw std::runtime_error("unsupported color kind " + std::to_string(code));
    }
}

std::uint32_t font_kind_code(model::FontKind kind) {
    switch (kind) {
        case model::FontKind::absolute:
            return 0;
        case model::FontKind::windows_font:
            return 1;
        case model::FontKind::style_reference:
            return 2;
        case model::FontKind::automatic:
            return 3;
    }
    throw std::runtime_error("unsupported font kind");
}

model::FontKind parse_font_kind(std::uint32_t code) {
    switch (code) {
        case 0:
            return model::FontKind::absolute;
        case 1:
            return model::FontKind::windows_font;
        case 2:
            return model::FontKind::style_reference;
        case 3:
            return model::FontKind::automatic;
        default:
            throw std::runtime_error("unsupported font kind " + std::to_string(code));
    }
}

std::uint8_t checked_channel(std::uint32_t value) {
    if (value > 255) {
        throw std::runtime_error("color channel is outside 0..255");
    }
    return static_cast<std::uint8_t>(value);
}

void require_style_reference_consistency(
    bool expects_reference,
    const model::StyleReference& reference,
    std::string_view value_name) {
    const bool has_reference = !std::holds_alternative<std::monostate>(reference);
    if (expects_reference != has_reference) {
        throw std::runtime_error(
            std::string(value_name) +
            (expects_reference ? " requires a style reference" : " cannot carry a style reference"));
    }
}

template <typename Value, typename Write>
std::string encode_value(const Value& value, Write&& write) {
    list_stream::ListOutStream out;
    write(out, value);
    return out.text();
}

}  // namespace

void write_localized_string(
    list_stream::ListOutStream& out,
    const model::LocalizedStringValue& value) {
    out.begin_list();
    out.write_uint32(localized_string_version);
    out.write_uint32(static_cast<std::uint32_t>(value.items.size()));
    for (const auto& item : value.items) {
        out.begin_list();
        out.write_string(item.language);
        out.write_string(item.text);
        out.end_list();
    }
    out.end_list();
}

model::LocalizedStringValue read_localized_string(list_stream::ListInStream& in) {
    in.begin_list();
    const std::uint32_t version = in.read_uint32();
    if (version != localized_string_version) {
        throw std::runtime_error(
            "LocalizedString unsupported platform version " + std::to_string(version));
    }
    const std::uint32_t count = in.read_uint32();
    model::LocalizedStringValue value;
    value.items.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        in.begin_list();
        model::LocalizedStringItem item;
        item.language = in.read_string();
        item.text = in.read_string();
        in.end_list();
        value.items.push_back(std::move(item));
    }
    in.end_list();
    return value;
}

void write_formatted_string(
    list_stream::ListOutStream& out,
    const model::FormattedStringValue& value) {
    out.begin_list();
    out.write_uint32(formatted_string_version);
    write_localized_string(out, value.value);
    out.write_bool(value.formatted);
    out.end_list();
}

model::FormattedStringValue read_formatted_string(list_stream::ListInStream& in) {
    in.begin_list();
    const std::uint32_t version = in.read_uint32();
    if (version != formatted_string_version) {
        throw std::runtime_error(
            "FormattedString unsupported platform version " + std::to_string(version));
    }
    model::FormattedStringValue value;
    value.value = read_localized_string(in);
    value.formatted = in.read_bool();
    in.end_list();
    return value;
}

void write_composite_id(
    list_stream::ListOutStream& out,
    const model::CompositeIdValue& value) {
    out.begin_list();
    out.write_int64(value.object_id);
    if (!value.is_null) {
        out.write_guid(value.uuid.canonical);
    }
    out.end_list();
}

model::CompositeIdValue read_composite_id(list_stream::ListInStream& in) {
    model::CompositeIdValue value;
    in.begin_list();
    value.object_id = in.read_int64();
    if (in.has_next()) {
        value.uuid.canonical = in.read_guid();
        value.is_null = value.object_id == 0 && is_null_uuid(value.uuid);
    } else {
        value.uuid.canonical = std::string(null_uuid);
        value.is_null = true;
    }
    in.end_list();
    return value;
}

void write_type_domain(
    list_stream::ListOutStream& out,
    const model::TypeDomainPatternValue& value) {
    out.begin_list();
    out.write_string(std::string(type_domain_root));
    for (const auto& entry : value.entries) {
        out.begin_list();
        out.write_string(std::string(term_token(entry.term)));
        switch (entry.term) {
            case model::TypeDomainTerm::type:
            case model::TypeDomainTerm::reference:
            case model::TypeDomainTerm::list:
            case model::TypeDomainTerm::unknown:
                if (entry.type_uuid.has_value()) {
                    out.write_guid(entry.type_uuid->canonical);
                }
                break;
            case model::TypeDomainTerm::numeric:
                if (entry.numeric.length != 0 || entry.numeric.precision != 0 ||
                    entry.numeric.non_negative) {
                    out.write_uint32(entry.numeric.length);
                    out.write_uint32(entry.numeric.precision);
                    out.write_bool(entry.numeric.non_negative);
                }
                break;
            case model::TypeDomainTerm::string:
                if (entry.string.length != 0) {
                    out.write_uint32(entry.string.length);
                    out.write_bool(entry.string.variable);
                }
                break;
            case model::TypeDomainTerm::binary:
                if (entry.binary.length != 0) {
                    out.write_uint32(entry.binary.length);
                    out.write_bool(entry.binary.variable);
                }
                break;
            case model::TypeDomainTerm::date:
                if (!(entry.date.date && entry.date.time)) {
                    std::string flags;
                    if (entry.date.date) {
                        flags += 'D';
                    }
                    if (entry.date.time) {
                        flags += 'T';
                    }
                    out.write_string(std::move(flags));
                }
                break;
        }
        out.end_list();
    }
    out.end_list();
}

model::TypeDomainPatternValue read_type_domain(list_stream::ListInStream& in) {
    model::TypeDomainPatternValue value;
    in.begin_list();
    const std::string root = in.read_string();
    if (root != type_domain_root) {
        throw std::runtime_error("unsupported type-domain root " + root);
    }
    while (in.has_next()) {
        in.begin_list();
        model::TypeDomainEntry entry;
        entry.term = parse_term(in.read_string());
        switch (entry.term) {
            case model::TypeDomainTerm::type:
            case model::TypeDomainTerm::reference:
            case model::TypeDomainTerm::list:
            case model::TypeDomainTerm::unknown:
                if (in.has_next()) {
                    entry.type_uuid = model::UuidValue{in.read_guid()};
                }
                break;
            case model::TypeDomainTerm::numeric:
                if (in.has_next()) {
                    entry.numeric.length = in.read_uint32();
                    entry.numeric.precision = in.read_uint32();
                    entry.numeric.non_negative = in.read_bool();
                }
                break;
            case model::TypeDomainTerm::string:
                if (in.has_next()) {
                    entry.string.length = in.read_uint32();
                    entry.string.variable = in.read_bool();
                }
                break;
            case model::TypeDomainTerm::binary:
                if (in.has_next()) {
                    entry.binary.length = in.read_uint32();
                    entry.binary.variable = in.read_bool();
                }
                break;
            case model::TypeDomainTerm::date:
                if (in.has_next()) {
                    const std::string flags = in.read_string();
                    if (flags.find_first_not_of("DT") != std::string::npos ||
                        flags.find('D') != flags.rfind('D') ||
                        flags.find('T') != flags.rfind('T')) {
                        throw std::runtime_error("invalid type-domain date qualifier " + flags);
                    }
                    entry.date.date = flags.find('D') != std::string::npos;
                    entry.date.time = flags.find('T') != std::string::npos;
                }
                break;
        }
        in.end_list();
        value.entries.push_back(std::move(entry));
    }
    in.end_list();
    return value;
}

void write_style_reference(
    list_stream::ListOutStream& out,
    const model::StyleReference& value) {
    out.begin_list();
    std::visit(
        [&out](const auto& reference) {
            using Reference = std::remove_cvref_t<decltype(reference)>;
            if constexpr (std::is_same_v<Reference, std::monostate>) {
                out.write_string("none");
            } else if constexpr (std::is_same_v<Reference, model::CompositeIdValue>) {
                out.write_string("CompositeID");
                write_composite_id(out, reference);
            } else if constexpr (std::is_same_v<Reference, model::QualifiedName>) {
                out.write_string("QName");
                out.write_string(reference.value);
            }
        },
        value);
    out.end_list();
}

model::StyleReference read_style_reference(list_stream::ListInStream& in) {
    in.begin_list();
    const std::string kind = in.read_string();
    model::StyleReference value;
    if (kind == "none") {
        value = std::monostate{};
    } else if (kind == "CompositeID") {
        value = read_composite_id(in);
    } else if (kind == "QName") {
        value = model::QualifiedName{in.read_string()};
    } else {
        throw std::runtime_error("unsupported style reference kind " + kind);
    }
    in.end_list();
    return value;
}

void write_color(list_stream::ListOutStream& out, const model::ColorValue& value) {
    require_style_reference_consistency(
        value.kind == model::ColorKind::style_reference,
        value.style,
        "Color");
    out.begin_list();
    out.write_uint32(color_field_count);
    out.write_uint32(color_kind_code(value.kind));
    out.begin_list();
    out.write_uint32(value.red);
    out.write_uint32(value.green);
    out.write_uint32(value.blue);
    out.write_uint32(value.alpha);
    out.end_list();
    write_style_reference(out, value.style);
    out.end_list();
}

model::ColorValue read_color(list_stream::ListInStream& in) {
    in.begin_list();
    const std::uint32_t fields = in.read_uint32();
    if (fields != color_field_count) {
        throw std::runtime_error("Color unsupported field count " + std::to_string(fields));
    }
    model::ColorValue value;
    value.kind = parse_color_kind(in.read_uint32());
    in.begin_list();
    value.red = checked_channel(in.read_uint32());
    value.green = checked_channel(in.read_uint32());
    value.blue = checked_channel(in.read_uint32());
    value.alpha = checked_channel(in.read_uint32());
    in.end_list();
    value.style = read_style_reference(in);
    in.end_list();
    require_style_reference_consistency(
        value.kind == model::ColorKind::style_reference,
        value.style,
        "Color");
    return value;
}

void write_font(list_stream::ListOutStream& out, const model::FontValue& value) {
    require_style_reference_consistency(
        value.kind == model::FontKind::style_reference,
        value.style,
        "Font");
    out.begin_list();
    out.write_uint32(font_field_count);
    out.write_uint32(font_kind_code(value.kind));
    out.write_uint32(value.mask);
    write_style_reference(out, value.style);
    out.write_string(value.face_name);
    out.write_double(value.height);
    out.begin_list();
    out.write_bool(value.bold);
    out.write_bool(value.italic);
    out.write_bool(value.underline);
    out.write_bool(value.strikeout);
    out.end_list();
    out.end_list();
}

model::FontValue read_font(list_stream::ListInStream& in) {
    in.begin_list();
    const std::uint32_t fields = in.read_uint32();
    if (fields != font_field_count) {
        throw std::runtime_error("Font unsupported field count " + std::to_string(fields));
    }
    model::FontValue value;
    value.kind = parse_font_kind(in.read_uint32());
    value.mask = in.read_uint32();
    value.style = read_style_reference(in);
    value.face_name = in.read_string();
    value.height = in.read_double();
    in.begin_list();
    value.bold = in.read_bool();
    value.italic = in.read_bool();
    value.underline = in.read_bool();
    value.strikeout = in.read_bool();
    in.end_list();
    in.end_list();
    require_style_reference_consistency(
        value.kind == model::FontKind::style_reference,
        value.style,
        "Font");
    return value;
}

std::string encode_localized_string(const model::LocalizedStringValue& value) {
    return encode_value(value, write_localized_string);
}

model::LocalizedStringValue decode_localized_string(std::string_view text) {
    list_stream::ListInStream in(text);
    return read_localized_string(in);
}

std::string encode_formatted_string(const model::FormattedStringValue& value) {
    return encode_value(value, write_formatted_string);
}

model::FormattedStringValue decode_formatted_string(std::string_view text) {
    list_stream::ListInStream in(text);
    return read_formatted_string(in);
}

std::string encode_composite_id(const model::CompositeIdValue& value) {
    return encode_value(value, write_composite_id);
}

model::CompositeIdValue decode_composite_id(std::string_view text) {
    list_stream::ListInStream in(text);
    return read_composite_id(in);
}

std::string encode_type_domain(const model::TypeDomainPatternValue& value) {
    return encode_value(value, write_type_domain);
}

model::TypeDomainPatternValue decode_type_domain(std::string_view text) {
    list_stream::ListInStream in(text);
    return read_type_domain(in);
}

std::string encode_style_reference(const model::StyleReference& value) {
    return encode_value(value, write_style_reference);
}

model::StyleReference decode_style_reference(std::string_view text) {
    list_stream::ListInStream in(text);
    return read_style_reference(in);
}

std::string encode_color(const model::ColorValue& value) {
    return encode_value(value, write_color);
}

model::ColorValue decode_color(std::string_view text) {
    list_stream::ListInStream in(text);
    return read_color(in);
}

std::string encode_font(const model::FontValue& value) {
    return encode_value(value, write_font);
}

model::FontValue decode_font(std::string_view text) {
    list_stream::ListInStream in(text);
    return read_font(in);
}

}  // namespace oof::storage::value_codec
