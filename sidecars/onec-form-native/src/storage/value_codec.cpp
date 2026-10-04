#include "oof/storage/value_codec.hpp"

#include "oof/model/metamodel.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <limits>
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
constexpr std::uint32_t shortcut_version = 0;
constexpr std::uint32_t shortcut_allowed_flags = 16U | 8U | 4U;
constexpr std::string_view type_domain_root = "Pattern";
constexpr std::string_view null_uuid = "00000000-0000-0000-0000-000000000000";
constexpr std::string_view value_list_type_uuid = "4772b3b4-f4a3-49c0-a1a5-8cb5961511a3";
constexpr std::string_view value_table_type_uuid = "acf6192e-81ca-46ef-93a6-5a6968b78663";

std::string_view term_token(model::TypeDomainTerm term) {
    switch (term) {
        case model::TypeDomainTerm::unknown:
            return "#";
        case model::TypeDomainTerm::list:
            return "L";
        case model::TypeDomainTerm::boolean:
            return "B";
        case model::TypeDomainTerm::binary:
            throw std::runtime_error("BinaryData type-domain encoding is not established");
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
        case model::TypeDomainTerm::value_list:
            return "#";
        case model::TypeDomainTerm::value_table:
            return "#";
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
        return model::TypeDomainTerm::boolean;
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
        case model::FontKind::style_reference:
            return 2;
        case model::FontKind::automatic:
            return 3;
        case model::FontKind::windows_font:
            break;
    }
    if (kind == model::FontKind::windows_font) {
        throw std::runtime_error("WindowsFont is not supported by the ordinary-form Font codec");
    }
    throw std::runtime_error("unknown ordinary-form Font kind " +
        std::to_string(static_cast<unsigned int>(kind)));
}

model::FontKind parse_font_kind(std::uint32_t code) {
    switch (code) {
        case 0:
            return model::FontKind::absolute;
        case 2:
            return model::FontKind::style_reference;
        case 3:
            return model::FontKind::automatic;
        default:
            throw std::runtime_error("unsupported ordinary-form Font kind " + std::to_string(code));
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

namespace {
void validate_date_parts(std::string_view digits) {
    if (digits.size() != 14 || !std::ranges::all_of(digits, [](unsigned char ch) {
            return ch >= '0' && ch <= '9';
        })) {
        throw std::runtime_error("date must use local second-precision YYYYMMDDHHMMSS without a timezone or fraction");
    }
    std::array<int, 6> parts{};
    constexpr std::array<std::size_t, 6> offsets{0, 4, 6, 8, 10, 12};
    constexpr std::array<std::size_t, 6> widths{4, 2, 2, 2, 2, 2};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto result = std::from_chars(digits.data() + offsets[i],
            digits.data() + offsets[i] + widths[i], parts[i]);
        if (result.ec != std::errc{}) throw std::runtime_error("date contains an invalid numeric field");
    }
    const auto [year, month, day, hour, minute, second] = parts;
    constexpr std::array<int, 12> month_days{31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int max_day = month >= 1 && month <= 12
        ? month_days[static_cast<std::size_t>(month - 1)] + (month == 2 && leap ? 1 : 0) : 0;
    if (year < 1 || year > 9999 || day < 1 || day > max_day ||
        hour > 23 || minute > 59 || second > 59) {
        throw std::runtime_error("date is outside the supported Gregorian calendar fields");
    }
}
}  // namespace

std::string date_to_platform(std::string_view canonical) {
    if (canonical.size() != 19 || canonical[4] != '-' || canonical[7] != '-' ||
        canonical[10] != 'T' || canonical[13] != ':' || canonical[16] != ':') {
        throw std::runtime_error("date must be YYYY-MM-DDTHH:MM:SS without a timezone or fraction");
    }
    std::string digits;
    digits.reserve(14);
    for (const auto index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U, 11U, 12U, 14U, 15U, 17U, 18U}) {
        digits.push_back(canonical[index]);
    }
    validate_date_parts(digits);
    return digits;
}

std::string date_from_platform(std::string_view atom) {
    validate_date_parts(atom);
    std::string result;
    result.reserve(19);
    result.append(atom.substr(0, 4)); result.push_back('-');
    result.append(atom.substr(4, 2)); result.push_back('-');
    result.append(atom.substr(6, 2)); result.push_back('T');
    result.append(atom.substr(8, 2)); result.push_back(':');
    result.append(atom.substr(10, 2)); result.push_back(':');
    result.append(atom.substr(12, 2));
    return result;
}

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
            case model::TypeDomainTerm::value_list:
                if (entry.type_uuid.has_value() || entry.numeric != model::NumericQualifiers{} ||
                    entry.string != model::LengthQualifiers{} ||
                    entry.binary != model::LengthQualifiers{} ||
                    entry.date != model::DateQualifiers{}) {
                    throw std::runtime_error("ValueList type-domain term cannot carry UUID or qualifiers");
                }
                out.write_guid(std::string(value_list_type_uuid));
                break;
            case model::TypeDomainTerm::value_table:
                if (entry.type_uuid.has_value() || entry.numeric != model::NumericQualifiers{} ||
                    entry.string != model::LengthQualifiers{} ||
                    entry.binary != model::LengthQualifiers{} ||
                    entry.date != model::DateQualifiers{}) {
                    throw std::runtime_error("ValueTable type-domain term cannot carry UUID or qualifiers");
                }
                out.write_guid(std::string(value_table_type_uuid));
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
            case model::TypeDomainTerm::boolean:
                break;
            case model::TypeDomainTerm::binary:
                throw std::runtime_error("BinaryData type-domain encoding is not established");
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
                    if (entry.term == model::TypeDomainTerm::unknown &&
                        entry.type_uuid->canonical == value_list_type_uuid) {
                        entry.term = model::TypeDomainTerm::value_list;
                        entry.type_uuid.reset();
                    } else if (entry.term == model::TypeDomainTerm::unknown &&
                        entry.type_uuid->canonical == value_table_type_uuid) {
                        entry.term = model::TypeDomainTerm::value_table;
                        entry.type_uuid.reset();
                    }
                }
                break;
            case model::TypeDomainTerm::value_list:
                throw std::runtime_error("ValueList uses the platform unknown type token");
            case model::TypeDomainTerm::value_table:
                throw std::runtime_error("ValueTable uses the platform unknown type token");
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
            case model::TypeDomainTerm::boolean:
                if (in.has_next()) {
                    throw std::runtime_error("Boolean type-domain term must not have qualifiers");
                }
                break;
            case model::TypeDomainTerm::binary:
                throw std::runtime_error("BinaryData type-domain decoding is not established");
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
    const auto has_fields = [&] {
        return value.face_name.has_value() || value.height.has_value() || value.bold.has_value() ||
            value.italic.has_value() || value.underline.has_value() || value.strikeout.has_value() ||
            value.scale != 100.0 || value.scale_override;
    };
    const bool has_style = !std::holds_alternative<std::monostate>(value.style);
    const std::uint32_t kind = font_kind_code(value.kind);
    if (value.kind == model::FontKind::automatic) {
        if (has_style || has_fields()) throw std::runtime_error("automatic Font cannot carry overrides");
        out.begin_list(); out.write_uint32(8); out.write_uint32(kind); out.write_uint32(0);
        out.write_uint32(1); out.write_uint32(100); out.end_list();
        return;
    }
    if (value.kind == model::FontKind::style_reference) {
        const auto* style = std::get_if<model::QualifiedName>(&value.style);
        if (style == nullptr || style->value != "StyleFonts.TextFont" || has_fields()) {
            throw std::runtime_error("only unmodified StyleFonts.TextFont is supported");
        }
        out.begin_list(); out.write_uint32(8); out.write_uint32(kind); out.write_uint32(0);
        out.begin_list(); out.write_int64(-20); out.end_list();
        out.write_uint32(1); out.write_uint32(100); out.end_list();
        return;
    }
    if (value.kind != model::FontKind::absolute || has_style) {
        throw std::runtime_error("unsupported Font kind or style reference");
    }
    if (!value.face_name || value.face_name->empty()) {
        throw std::runtime_error("absolute Font requires a non-empty faceName");
    }

    std::uint32_t flags = 0;
    if (value.face_name) flags |= 1U;
    if (value.height) flags |= 2U;
    if (value.bold) flags |= 4U;
    if (value.italic) flags |= 8U;
    if (value.underline) flags |= 16U;
    if (value.strikeout) flags |= 32U;
    if (value.scale_override) flags |= 512U;
    const auto scaled_integer = [](std::optional<double> number, double factor, std::string_view field) {
        if (!number) return std::uint32_t{};
        if (!std::isfinite(*number) || *number < 0 || *number * factor > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("Font " + std::string(field) + " is outside its supported range");
        }
        const double scaled = *number * factor;
        const double rounded = std::round(scaled);
        if (std::abs(scaled - rounded) > 1e-9) {
            throw std::runtime_error("Font " + std::string(field) + " is not representable in storage units");
        }
        return static_cast<std::uint32_t>(rounded);
    };
    const auto height = scaled_integer(value.height, 10.0, "height");
    const auto scale = scaled_integer(value.scale, 1.0, "scale");
    out.begin_list();
    out.write_uint32(8); out.write_uint32(kind); out.write_uint32(flags); out.write_uint32(height);
    out.write_uint32(0); out.write_uint32(0); out.write_uint32(0);
    out.write_uint32(value.bold.value_or(false) ? 700 : 400);
    out.write_bool(value.italic.value_or(false));
    out.write_bool(value.underline.value_or(false));
    out.write_bool(value.strikeout.value_or(false));
    out.write_uint32(0); out.write_uint32(0); out.write_uint32(0); out.write_uint32(0); out.write_uint32(0);
    out.write_string(value.face_name.value_or(std::string{}));
    out.write_uint32(1); out.write_uint32(scale); out.write_uint32(0);
    out.end_list();
}

model::FontValue read_font(list_stream::ListInStream& in) {
    in.begin_list();
    if (in.read_uint32() != 8) throw std::runtime_error("Font version marker is unsupported");
    model::FontValue value;
    value.kind = parse_font_kind(in.read_uint32());
    if (value.kind == model::FontKind::automatic) {
        if (in.read_uint32() != 0 || in.read_uint32() != 1 || in.read_uint32() != 100 || in.has_next())
            throw std::runtime_error("automatic Font record is not canonical");
        in.end_list();
        return value;
    }
    if (value.kind == model::FontKind::style_reference) {
        if (in.read_uint32() != 0) throw std::runtime_error("style Font flags are unsupported");
        in.begin_list();
        if (in.read_int64() != -20 || in.has_next()) throw std::runtime_error("style Font is not TextFont");
        in.end_list();
        if (in.read_uint32() != 1 || in.read_uint32() != 100 || in.has_next())
            throw std::runtime_error("style Font record is not canonical");
        value.style = model::QualifiedName{"StyleFonts.TextFont"};
        in.end_list();
        return value;
    }
    if (value.kind != model::FontKind::absolute) throw std::runtime_error("unsupported Font kind");
    const std::uint32_t flags = in.read_uint32();
    constexpr std::uint32_t allowed_flags = 1U | 2U | 4U | 8U | 16U | 32U | 512U;
    if ((flags & ~allowed_flags) != 0) throw std::runtime_error("Font contains unknown presence flags");
    const auto height = in.read_uint32();
    if (height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error("Font height exceeds the supported signed 32-bit storage range");
    }
    for (int i = 0; i < 3; ++i) if (in.read_uint32() != 0) throw std::runtime_error("Font reserved field is nonzero");
    const auto weight = in.read_uint32();
    const bool italic = in.read_bool();
    const bool underline = in.read_bool();
    const bool strikeout = in.read_bool();
    for (int i = 0; i < 5; ++i) if (in.read_uint32() != 0) throw std::runtime_error("Font reserved field is nonzero");
    const auto face_name = in.read_string();
    if (in.read_uint32() != 1) throw std::runtime_error("Font reserved marker is unsupported");
    const auto scale = in.read_uint32();
    if (scale > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error("Font scale exceeds the supported signed 32-bit storage range");
    }
    if (in.read_uint32() != 0 || in.has_next()) throw std::runtime_error("Font trailer is unsupported");
    if ((flags & 1U) == 0 || face_name.empty()) {
        throw std::runtime_error("absolute Font requires a non-empty faceName and its presence flag");
    }
    value.face_name = face_name;
    if ((flags & 2U) != 0) value.height = static_cast<double>(height) / 10.0;
    else if (height != 0) throw std::runtime_error("Font height is present without its flag");
    if ((flags & 4U) != 0) {
        if (weight != 400 && weight != 700) throw std::runtime_error("Font weight is unsupported");
        value.bold = weight == 700;
    } else if (weight != 400) throw std::runtime_error("Font weight is present without its flag");
    if ((flags & 8U) != 0) value.italic = italic;
    else if (italic) throw std::runtime_error("Font italic is present without its flag");
    if ((flags & 16U) != 0) value.underline = underline;
    else if (underline) throw std::runtime_error("Font underline is present without its flag");
    if ((flags & 32U) != 0) value.strikeout = strikeout;
    else if (strikeout) throw std::runtime_error("Font strikeout is present without its flag");
    value.scale = scale;
    value.scale_override = (flags & 512U) != 0;
    in.end_list();
    return value;
}

void write_shortcut(list_stream::ListOutStream& out, const model::ShortcutValue& value) {
    const auto* key = model::metamodel::find_shortcut_key(value.key);
    if (key == nullptr) {
        throw std::runtime_error("Shortcut contains an unsupported named key " + value.key);
    }
    const std::uint32_t flags = (value.alt ? 16U : 0U) |
                                (value.ctrl ? 8U : 0U) |
                                (value.shift ? 4U : 0U);
    out.begin_list();
    out.write_uint32(shortcut_version);
    out.write_uint32(key->storage_code);
    out.write_uint32(flags);
    out.end_list();
}

model::ShortcutValue read_shortcut(list_stream::ListInStream& in) {
    in.begin_list();
    const auto version = in.read_uint32();
    if (version != shortcut_version) {
        throw std::runtime_error("Shortcut version marker is unsupported");
    }
    const auto key_code = in.read_uint32();
    const auto flags = in.read_uint32();
    if (in.has_next()) {
        throw std::runtime_error("Shortcut record has trailing fields");
    }
    in.end_list();
    if ((flags & ~shortcut_allowed_flags) != 0) {
        throw std::runtime_error("Shortcut record contains unknown modifier flags");
    }
    const auto keys = model::metamodel::shortcut_key_descriptors();
    const auto key = std::ranges::find(keys, key_code, &model::metamodel::ShortcutKeyDescriptor::storage_code);
    if (key == keys.end()) {
        throw std::runtime_error("Shortcut record contains an unsupported key code " +
                                 std::to_string(key_code));
    }
    return model::ShortcutValue{
        std::string(key->name),
        (flags & 16U) != 0,
        (flags & 8U) != 0,
        (flags & 4U) != 0,
    };
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

std::string encode_shortcut(const model::ShortcutValue& value) {
    return encode_value(value, write_shortcut);
}

model::ShortcutValue decode_shortcut(std::string_view text) {
    list_stream::ListInStream in(text);
    const auto value = read_shortcut(in);
    if (in.has_next()) {
        throw std::runtime_error("Shortcut stream contains trailing values");
    }
    return value;
}

}  // namespace oof::storage::value_codec
