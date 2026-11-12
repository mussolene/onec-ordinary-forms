#include "oof/storage/value_codec.hpp"

#include "oof/model/metamodel.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace oof::storage::value_codec {

std::string canonical_decimal(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) text.remove_suffix(1);
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    const auto point = text.find('.');
    if (text.empty() || (point != std::string_view::npos && text.find('.', point + 1) != std::string_view::npos)) {
        throw std::invalid_argument("invalid xs:decimal lexical value");
    }
    auto integer = point == std::string_view::npos ? text : text.substr(0, point);
    auto fraction = point == std::string_view::npos ? std::string_view{} : text.substr(point + 1);
    const auto digits_only = [](std::string_view value) {
        return std::ranges::all_of(value, [](unsigned char ch) { return ch >= '0' && ch <= '9'; });
    };
    if ((!integer.empty() && !digits_only(integer)) || (!fraction.empty() && !digits_only(fraction)) ||
        (integer.empty() && fraction.empty())) {
        throw std::invalid_argument("invalid xs:decimal lexical value");
    }
    while (integer.size() > 1 && integer.front() == '0') integer.remove_prefix(1);
    while (!fraction.empty() && fraction.back() == '0') fraction.remove_suffix(1);
    const bool integer_zero = integer.empty() || std::ranges::all_of(integer, [](char ch) { return ch == '0'; });
    if (integer_zero && fraction.empty()) return "0";
    std::string result;
    if (negative) result.push_back('-');
    result.append(integer.empty() ? "0" : integer);
    if (!fraction.empty()) { result.push_back('.'); result.append(fraction); }
    return result;
}
std::string canonical_platform_decimal(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    const auto marker = text.find_first_of("eE");
    if (marker == std::string_view::npos) return canonical_decimal(text);
    auto exponent_text = text.substr(marker + 1);
    if (!exponent_text.empty() && exponent_text.front() == '+') {
        exponent_text.remove_prefix(1);
        if (!exponent_text.empty() && (exponent_text.front() == '-' || exponent_text.front() == '+'))
            throw std::invalid_argument("invalid platform decimal exponent sign");
    }
    int exponent = 0;
    const auto parsed = std::from_chars(exponent_text.data(), exponent_text.data() + exponent_text.size(), exponent);
    if (exponent_text.empty() || parsed.ec != std::errc{} || parsed.ptr != exponent_text.data() + exponent_text.size() ||
        exponent < -4096 || exponent > 4096)
        throw std::invalid_argument("invalid or excessive platform decimal exponent");
    auto mantissa = canonical_decimal(text.substr(0, marker));
    const bool negative = mantissa.front() == '-';
    if (negative) mantissa.erase(0, 1);
    const auto point = mantissa.find('.');
    const auto integer_size = point == std::string::npos ? mantissa.size() : point;
    if (point != std::string::npos) mantissa.erase(point, 1);
    const auto shifted = static_cast<std::int64_t>(integer_size) + exponent;
    std::string expanded = negative ? "-" : "";
    if (shifted <= 0) {
        expanded += "0.";
        expanded.append(static_cast<std::size_t>(-shifted), '0');
        expanded += mantissa;
    } else if (static_cast<std::size_t>(shifted) >= mantissa.size()) {
        expanded += mantissa;
        expanded.append(static_cast<std::size_t>(shifted) - mantissa.size(), '0');
    } else {
        expanded.append(mantissa, 0, static_cast<std::size_t>(shifted));
        expanded += '.';
        expanded.append(mantissa, static_cast<std::size_t>(shifted), std::string::npos);
    }
    return canonical_decimal(expanded);
}

std::string compact_platform_decimal(std::string_view text) {
    const auto decimal = canonical_decimal(text);
    if (decimal == "0") return decimal;

    const bool negative = decimal.front() == '-';
    const std::string_view magnitude(decimal.data() + (negative ? 1 : 0), decimal.size() - (negative ? 1 : 0));
    const auto point = magnitude.find('.');
    const auto integer_size = point == std::string_view::npos ? magnitude.size() : point;
    std::string digits;
    digits.reserve(magnitude.size());
    for (const char ch : magnitude) if (ch != '.') digits.push_back(ch);

    const auto first = digits.find_first_not_of('0');
    const auto last = digits.find_last_not_of('0');
    const bool nonnegative_exponent = integer_size > first;
    const auto exponent_magnitude = nonnegative_exponent ? integer_size - first - 1 : first - integer_size + 1;
    if (exponent_magnitude > 4096)
        throw std::invalid_argument("excessive platform decimal exponent");
    const auto exponent = nonnegative_exponent ? static_cast<int>(exponent_magnitude)
                                               : -static_cast<int>(exponent_magnitude);
    std::string scientific;
    if (negative) scientific.push_back('-');
    scientific.push_back(digits[first]);
    if (last > first) {
        scientific.push_back('.');
        scientific.append(digits, first + 1, last - first);
    }
    scientific.push_back('e');
    scientific += std::to_string(exponent);
    return scientific.size() < decimal.size() ? scientific : decimal;
}

namespace {

struct FontLayout {
    std::uint32_t version;
    bool has_scale;
    bool scale_override_allowed;
};

constexpr std::array font_layouts{
    FontLayout{6, false, false},
    FontLayout{7, true, false},
    FontLayout{8, true, true},
};

constexpr std::uint32_t localized_string_version = 1;
constexpr std::uint32_t formatted_string_version = 1;
constexpr std::uint32_t color_field_count = 3;
constexpr std::uint32_t shortcut_version = 0;
constexpr std::uint32_t shortcut_allowed_flags = 16U | 8U | 4U;
constexpr std::string_view type_domain_root = "Pattern";
constexpr std::string_view null_uuid = "00000000-0000-0000-0000-000000000000";
constexpr std::string_view value_list_type_uuid = "4772b3b4-f4a3-49c0-a1a5-8cb5961511a3";
constexpr std::string_view value_table_type_uuid = "acf6192e-81ca-46ef-93a6-5a6968b78663";
constexpr std::string_view value_tree_type_uuid = "e603c0f2-92fb-4d47-8f38-a44a381cf235";

bool known_uuid_equal(std::string_view value, std::string_view canonical) noexcept {
    return std::equal(value.begin(), value.end(), canonical.begin(), canonical.end(),
        [](char actual, char expected) {
            return (actual >= 'A' && actual <= 'F' ? static_cast<char>(actual + ('a' - 'A')) : actual) == expected;
        });
}

std::string_view term_token(model::TypeDomainTerm term) {
    switch (term) {
        case model::TypeDomainTerm::unknown:
        case model::TypeDomainTerm::object:
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
        case model::TypeDomainTerm::value_tree:
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
            return 1;
    }
    throw std::runtime_error("unknown ordinary-form Font kind " +
        std::to_string(static_cast<unsigned int>(kind)));
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
    // Grow only after a complete item has been read. The count is untrusted.
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
    if (const auto* text = std::get_if<std::string>(&value.value)) {
        out.write_string("#");
        out.write_guid("140b5ff4-37b1-4df5-b5ec-a0bfd2b94f8f");
        out.write_bool(value.formatted);
        out.write_string(*text);
    } else {
        out.write_uint32(formatted_string_version);
        write_localized_string(out, std::get<model::LocalizedStringValue>(value.value));
        out.write_bool(value.formatted);
    }
    out.end_list();
}

model::FormattedStringValue read_formatted_string(list_stream::ListInStream& in) {
    in.begin_list();
    const auto& first = in.peek_value();
    if (!first.is_list && first.atom_kind == list_stream::ListValue::AtomKind::string && first.atom == "#") {
        static_cast<void>(in.read_string());
        if (in.read_guid() != "140b5ff4-37b1-4df5-b5ec-a0bfd2b94f8f")
            throw std::runtime_error("FormattedString has an unknown runtime type");
        const bool formatted = in.read_bool();
        model::FormattedStringValue value{in.read_string(), formatted};
        in.end_list();
        return value;
    }
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
        if (entry.type_name) {
            const bool configured = entry.term == model::TypeDomainTerm::object ||
                entry.term == model::TypeDomainTerm::reference || entry.term == model::TypeDomainTerm::list ||
                entry.term == model::TypeDomainTerm::type;
            if (!configured || !entry.type_uuid || entry.type_uuid->canonical == "00000000-0000-0000-0000-000000000000" ||
                entry.type_name->value.empty() || entry.type_name->value.find_first_of(" \t\r\n") != std::string::npos)
                throw std::runtime_error("Configuration type name requires a concrete type UUID and a nonempty qualified name");
        }
        out.begin_list();
        out.write_string(std::string(term_token(entry.term)));
        switch (entry.term) {
            case model::TypeDomainTerm::type:
            case model::TypeDomainTerm::reference:
            case model::TypeDomainTerm::list:
            case model::TypeDomainTerm::unknown:
            case model::TypeDomainTerm::object:
                if ((entry.term == model::TypeDomainTerm::object && !entry.type_uuid) ||
                    (entry.term == model::TypeDomainTerm::unknown && entry.type_uuid))
                    throw std::runtime_error("Object type requires UUID; unknown type cannot carry an object UUID");
                if (entry.term == model::TypeDomainTerm::object &&
                    (known_uuid_equal(entry.type_uuid->canonical, value_list_type_uuid) || known_uuid_equal(entry.type_uuid->canonical, value_table_type_uuid) ||
                     known_uuid_equal(entry.type_uuid->canonical, value_tree_type_uuid)))
                    throw std::runtime_error("Known collection types require their named type-domain term");
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
            case model::TypeDomainTerm::value_tree:
                if (entry.type_uuid.has_value() || entry.numeric != model::NumericQualifiers{} ||
                    entry.string != model::LengthQualifiers{} ||
                    entry.binary != model::LengthQualifiers{} ||
                    entry.date != model::DateQualifiers{}) {
                    throw std::runtime_error("ValueTree type-domain term cannot carry UUID or qualifiers");
                }
                out.write_guid(std::string(value_tree_type_uuid));
                break;
            case model::TypeDomainTerm::numeric:
                if (entry.numeric.length == 0 && entry.numeric.precision != 0)
                    throw std::runtime_error("Numeric type-domain precision must be zero when length is zero");
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
            case model::TypeDomainTerm::object:
                if (in.has_next()) {
                    entry.type_uuid = model::UuidValue{in.read_guid()};
                    if (entry.term == model::TypeDomainTerm::unknown &&
                        known_uuid_equal(entry.type_uuid->canonical, value_list_type_uuid)) {
                        entry.term = model::TypeDomainTerm::value_list;
                        entry.type_uuid.reset();
                    } else if (entry.term == model::TypeDomainTerm::unknown &&
                        known_uuid_equal(entry.type_uuid->canonical, value_table_type_uuid)) {
                        entry.term = model::TypeDomainTerm::value_table;
                        entry.type_uuid.reset();
                    } else if (entry.term == model::TypeDomainTerm::unknown &&
                        known_uuid_equal(entry.type_uuid->canonical, value_tree_type_uuid)) {
                        entry.term = model::TypeDomainTerm::value_tree;
                        entry.type_uuid.reset();
                    } else if (entry.term == model::TypeDomainTerm::unknown) {
                        entry.term = model::TypeDomainTerm::object;
                    }
                }
                break;
            case model::TypeDomainTerm::value_list:
                throw std::runtime_error("ValueList uses the platform unknown type token");
            case model::TypeDomainTerm::value_table:
                throw std::runtime_error("ValueTable uses the platform unknown type token");
            case model::TypeDomainTerm::value_tree:
                throw std::runtime_error("ValueTree uses the platform unknown type token");
            case model::TypeDomainTerm::numeric:
                if (in.has_next()) {
                    entry.numeric.length = in.read_uint32();
                    entry.numeric.precision = in.read_uint32();
                    entry.numeric.non_negative = in.read_bool();
                    if (entry.numeric.length == 0 && entry.numeric.precision != 0)
                        throw std::runtime_error("Numeric type-domain precision must be zero when length is zero");
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
    const bool has_style = !std::holds_alternative<std::monostate>(value.style);
    const std::uint32_t kind = font_kind_code(value.kind);
    if (value.kind != model::FontKind::windows_font && value.windows_font)
        throw std::runtime_error("Windows font selector belongs only to Windows fonts");
    const auto scaled_integer = [](std::optional<double> number, double factor, std::string_view field) {
        if (!number) return std::uint32_t{};
        if (!std::isfinite(*number) || *number < 0 || *number * factor > std::numeric_limits<std::int32_t>::max())
            throw std::runtime_error("Font " + std::string(field) + " is outside its supported range");
        const double scaled = *number * factor;
        const double rounded = std::round(scaled);
        if (std::abs(scaled - rounded) > 1e-9)
            throw std::runtime_error("Font " + std::string(field) + " is not representable in storage units");
        return static_cast<std::uint32_t>(rounded);
    };
    std::uint32_t flags = 0;
    if (value.face_name) flags |= 1U;
    if (value.height) flags |= 2U;
    if (value.bold) flags |= 4U;
    if (value.italic) flags |= 8U;
    if (value.underline) flags |= 16U;
    if (value.strikeout) flags |= 32U;
    if (value.scale_override) flags |= 512U;
    const auto height = scaled_integer(value.height, 10.0, "height");
    const auto scale = scaled_integer(value.scale, 1.0, "scale");
    if (value.kind != model::FontKind::absolute) {
        if (value.kind == model::FontKind::windows_font) {
            if (!value.windows_font || has_style || static_cast<std::uint32_t>(*value.windows_font) > 4)
                throw std::runtime_error("Windows font requires a known named selector");
        } else if (value.kind == model::FontKind::style_reference) {
            const auto* style = std::get_if<model::QualifiedName>(&value.style);
            if (!style || !model::metamodel::find_named_font(style->value))
                throw std::runtime_error("Font requires a known named platform style reference");
        } else if (value.kind != model::FontKind::automatic || has_style) {
            throw std::runtime_error("unsupported Font kind or style reference");
        }
        out.begin_list(); out.write_uint32(8); out.write_uint32(kind); out.write_uint32(flags);
        if (value.kind == model::FontKind::windows_font) {
            out.begin_list(); out.write_uint32(static_cast<std::uint32_t>(*value.windows_font)); out.end_list();
        } else if (value.kind == model::FontKind::style_reference) {
            const auto& name = std::get<model::QualifiedName>(value.style).value;
            out.begin_list(); out.write_int64(model::metamodel::find_named_font(name)->native_id); out.end_list();
        }
        if (value.height) out.write_uint32(height);
        if (value.bold) out.write_uint32(*value.bold ? 700 : 400);
        if (value.italic) out.write_bool(*value.italic);
        if (value.underline) out.write_bool(*value.underline);
        if (value.strikeout) out.write_bool(*value.strikeout);
        if (value.face_name) out.write_string(*value.face_name);
        out.write_bool(value.auto_char_set); out.write_uint32(scale); out.end_list();
        return;
    }
    if (has_style) {
        throw std::runtime_error("absolute Font cannot carry a style reference");
    }
    if (!value.face_name || value.face_name->empty()) {
        throw std::runtime_error("absolute Font requires a non-empty faceName");
    }

    out.begin_list();
    out.write_uint32(8); out.write_uint32(kind); out.write_uint32(flags); out.write_uint32(height);
    out.write_uint32(0); out.write_uint32(0); out.write_uint32(0);
    out.write_uint32(value.bold.value_or(false) ? 700 : 400);
    out.write_bool(value.italic.value_or(false));
    out.write_bool(value.underline.value_or(false));
    out.write_bool(value.strikeout.value_or(false));
    out.write_uint32(0); out.write_uint32(0); out.write_uint32(0); out.write_uint32(0); out.write_uint32(0);
    out.write_string(value.face_name.value_or(std::string{}));
    out.write_bool(value.auto_char_set); out.write_uint32(scale); out.write_uint32(0);
    out.end_list();
}

model::FontValue read_font(list_stream::ListInStream& in) {
    in.begin_list();
    const auto version = in.read_uint32();
    const auto layout = std::ranges::find(font_layouts, version, &FontLayout::version);
    if (layout == font_layouts.end()) throw std::runtime_error("Font version marker is unsupported");
    model::FontValue value;
    value.kind = parse_font_kind(in.read_uint32());
    if (value.kind != model::FontKind::absolute) {
        const auto flags = in.read_uint32();
        const std::uint32_t basic_flags = 1U | 2U | 4U | 8U | 16U | 32U;
        const std::uint32_t allowed_flags = layout->scale_override_allowed ? basic_flags | 512U : basic_flags;
        if (flags & ~allowed_flags) throw std::runtime_error("Font contains unknown presence flags");
        if (value.kind == model::FontKind::windows_font) {
            in.begin_list(); const auto selector = in.read_uint32();
            if (selector > 4 || in.has_next()) throw std::runtime_error("unknown Windows font selector");
            in.end_list(); value.windows_font = static_cast<model::WindowsFont>(selector);
        } else if (value.kind == model::FontKind::style_reference) {
            in.begin_list();
            const auto* style = model::metamodel::find_named_font(in.read_int64());
            if (!style || in.has_next()) throw std::runtime_error("Font style identifier is unsupported");
            in.end_list(); value.style = model::QualifiedName{std::string(style->name)};
        } else if (value.kind != model::FontKind::automatic) {
            throw std::runtime_error("unsupported Font kind");
        }
        if (flags & 2U) {
            const auto height = in.read_uint32();
            if (height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
                throw std::runtime_error("Font height exceeds the supported signed 32-bit storage range");
            value.height = static_cast<double>(height) / 10.0;
        }
        if (flags & 4U) {
            const auto weight = in.read_uint32();
            if (weight != 400 && weight != 700) throw std::runtime_error("Font weight is unsupported");
            value.bold = weight == 700;
        }
        if (flags & 8U) value.italic = in.read_bool();
        if (flags & 16U) value.underline = in.read_bool();
        if (flags & 32U) value.strikeout = in.read_bool();
        if (flags & 1U) value.face_name = in.read_string();
        value.auto_char_set = in.read_bool();
        const auto scale = layout->has_scale ? in.read_uint32() : 100U;
        if (scale > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || in.has_next())
            throw std::runtime_error("Font scale or trailer is unsupported");
        value.scale = scale;
        value.scale_override = (flags & 512U) != 0;
        in.end_list();
        return value;
    }
    const std::uint32_t flags = in.read_uint32();
    const std::uint32_t basic_flags = 1U | 2U | 4U | 8U | 16U | 32U;
    const std::uint32_t allowed_flags = layout->scale_override_allowed ? basic_flags | 128U | 256U | 512U : basic_flags;
    if ((flags & ~allowed_flags) != 0) throw std::runtime_error("Font contains unknown presence flags");
    const auto height = in.read_uint32();
    if (height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error("Font height exceeds the supported signed 32-bit storage range");
    }
    if (in.read_uint32() != 0) throw std::runtime_error("Font width is unsupported");
    if (in.read_uint32() != 0) throw std::runtime_error("Font nonzero escapement is unsupported");
    if (in.read_uint32() != 0) throw std::runtime_error("Font orientation is unsupported");
    const auto weight = in.read_uint32();
    const bool italic = in.read_bool();
    const bool underline = in.read_bool();
    const bool strikeout = in.read_bool();
    for (int i = 0; i < 4; ++i) if (in.read_uint32() != 0) throw std::runtime_error("Font reserved field is nonzero");
    // Absolute Font equality excludes the LOGFONT pitch/family selection hint.
    if (in.read_uint32() > 255U) throw std::runtime_error("Font pitch and family exceeds its byte storage range");
    const auto face_name = in.read_string();
    value.auto_char_set = in.read_bool();
    const auto scale = layout->has_scale ? in.read_uint32() : 100U;
    if (scale > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error("Font scale exceeds the supported signed 32-bit storage range");
    }
    if (in.read_uint32() != 0) throw std::runtime_error("Font nonzero lineHeight is unsupported");
    if (in.has_next()) throw std::runtime_error("Font trailer is unsupported");
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
    const auto value = read_formatted_string(in);
    if (in.has_next()) throw std::runtime_error("FormattedString stream contains trailing values");
    return value;
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

std::string encode_line(const model::LineValue& value) {
    const auto families = model::metamodel::line_family_descriptors();
    const auto family = std::ranges::find(families, value.type.type_name, &model::metamodel::LineFamilyDescriptor::name);
    if (family == families.end()) throw std::runtime_error("unknown line family");
    const auto member = std::ranges::find(family->members, value.type.member);
    if (member == family->members.end()) throw std::runtime_error("unknown line member");
    const bool cell = family->name == "SpreadsheetDocumentCellLineType";
    const bool drawing = family->name == "SpreadsheetDocumentDrawingLineType";
    const auto maximum = (value.type.member == "None" || (cell && value.type.member == "Double")) ? 1 : (cell || drawing ? 3 : 50);
    if (value.width > maximum || (value.gap && !cell)) throw std::runtime_error("invalid line width or gap");
    list_stream::ListOutStream out;
    out.begin_list(); out.write_uint32(4); out.write_uint32(0);
    out.begin_list(); out.write_uint32(0); out.end_list();
    out.write_uint32(static_cast<std::uint32_t>(member - family->members.begin()));
    out.write_uint32(static_cast<std::uint32_t>(value.width));
    out.write_uint32(value == model::LineValue{} ? 0 : 7);
    out.write_guid(std::string(family->guid)); out.write_bool(value.gap); out.end_list();
    return out.text();
}

model::LineValue decode_line(std::string_view text) {
    list_stream::ListInStream in(text);
    in.begin_list();
    if (in.read_uint32() != 4 || in.read_uint32() != 0) throw std::runtime_error("invalid line header");
    in.begin_list(); if (in.read_uint32() != 0) throw std::runtime_error("invalid line reserved value"); in.end_list();
    const auto ordinal = in.read_uint32();
    const auto width = in.read_uint32();
    // Absolute V8Line equality ignores the override flags used by nonabsolute lines.
    (void)in.read_uint32();
    const auto guid = in.read_guid();
    const auto gap = in.read_bool();
    in.end_list(); if (in.has_next()) throw std::runtime_error("line stream contains trailing values");
    const auto families = model::metamodel::line_family_descriptors();
    const auto family = std::ranges::find(families, guid, &model::metamodel::LineFamilyDescriptor::guid);
    if (family == families.end() || ordinal >= family->members.size()) throw std::runtime_error("unknown line family or member");
    const auto signed_width = width <= static_cast<std::uint32_t>(INT32_MAX)
        ? static_cast<std::int32_t>(width) : static_cast<std::int32_t>(static_cast<std::int64_t>(width) - 4294967296LL);
    model::LineValue value{{std::string(family->name), std::string(family->members[ordinal])}, signed_width, gap};
    (void)encode_line(value);
    return value;
}

namespace {

using SpreadsheetEnumMember = std::pair<std::string_view, std::int32_t>;

std::span<const SpreadsheetEnumMember> spreadsheet_enum_members(std::string_view type) {
    static constexpr SpreadsheetEnumMember horizontal[]{SpreadsheetEnumMember{"Left", 0}, {"Center", 6},
        {"Right", 2}, {"Justify", 4}, {"Auto", 5}};
    static constexpr SpreadsheetEnumMember vertical[]{SpreadsheetEnumMember{"Top", 0}, {"Center", 24}, {"Bottom", 8}};
    static constexpr SpreadsheetEnumMember placement[]{SpreadsheetEnumMember{"Auto", 0}, {"Cut", 1}, {"Block", 2}, {"Wrap", 3}};
    static constexpr SpreadsheetEnumMember fill[]{SpreadsheetEnumMember{"Text", 0}, {"Parameter", 1}, {"Template", 2}};
    static constexpr SpreadsheetEnumMember details[]{SpreadsheetEnumMember{"Cell", 0}, {"Row", 1}, {"WithoutProcessing", 2}};
    static constexpr SpreadsheetEnumMember highlight[]{SpreadsheetEnumMember{"Auto", 0}, {"Cell", 1}, {"Row", 2}, {"DontHighlight", 3}};
    static constexpr SpreadsheetEnumMember edit[]{SpreadsheetEnumMember{"Auto", 0}, {"AsInputField", 1}, {"Regular", 2}};
    static constexpr SpreadsheetEnumMember picture_size[]{SpreadsheetEnumMember{"RealSize", 0}, {"Stretch", 1},
        {"Proportionally", 2}, {"Tile", 3}, {"AutoSize", 4}, {"RealSizeIgnoreScale", 5},
        {"AutoSizeIgnoreScale", 6}, {"ByFontSize", 7}};
    static constexpr SpreadsheetEnumMember text_position[]{SpreadsheetEnumMember{"Left", 0}, {"Right", 1},
        {"Top", 2}, {"Bottom", 3}, {"OnTop", 4}, {"Auto", 5}};
    static constexpr SpreadsheetEnumMember size_change[]{SpreadsheetEnumMember{"Normal", 0}, {"QuickChange", 1}};
    static constexpr SpreadsheetEnumMember pattern[]{SpreadsheetEnumMember{"WithoutPattern", 255}, {"Solid", 0},
        {"Pattern1", 1}, {"Pattern2", 2}, {"Pattern3", 3}, {"Pattern4", 4}, {"Pattern5", 5},
        {"Pattern6", 6}, {"Pattern7", 7}, {"Pattern8", 8}, {"Pattern9", 9}, {"Pattern10", 10},
        {"Pattern11", 11}, {"Pattern12", 12}, {"Pattern13", 13}, {"Pattern14", 14},
        {"Pattern15", 15}, {"Pattern16", 16}, {"Pattern17", 17}};
    if (type == "HorizontalAlign") return horizontal;
    if (type == "VerticalAlign") return vertical;
    if (type == "SpreadsheetDocumentTextPlacementType") return placement;
    if (type == "SpreadsheetDocumentAreaFillType") return fill;
    if (type == "SpreadsheetDocumentDetailUse") return details;
    if (type == "SpreadsheetDocumentCellHighlightMode") return highlight;
    if (type == "SpreadsheetDocumentEditableCellShowType") return edit;
    if (type == "PictureSize") return picture_size;
    if (type == "TextPositionRelativeToPicture") return text_position;
    if (type == "SizeChangeMode") return size_change;
    if (type == "SpreadsheetDocumentPatternType") return pattern;
    throw std::runtime_error("Spreadsheet Format enumeration storage is not established: " + std::string(type));
}

template <typename T>
const T& spreadsheet_typed_value(const model::PropertyValue& value, std::string_view name) {
    const auto* typed = std::get_if<T>(&value);
    if (!typed) throw std::runtime_error("Spreadsheet Format property has the wrong value type: " + std::string(name));
    return *typed;
}

template <typename T>
std::int32_t intern_spreadsheet_resource(std::vector<T>& resources, const T& value,
    std::int64_t maximum, std::string_view name) {
    const auto existing = std::ranges::find(resources, value);
    const auto index = existing - resources.begin();
    if (index > maximum) throw std::runtime_error("Spreadsheet Format resource reference exceeds storage width: " + std::string(name));
    if (existing == resources.end()) resources.push_back(value);
    return static_cast<std::int32_t>(index);
}

template <typename T>
const T& spreadsheet_resource(const std::vector<T>& resources, std::int64_t reference, std::string_view name) {
    if (reference < 0 || static_cast<std::uint64_t>(reference) >= resources.size())
        throw std::runtime_error("Spreadsheet Format references a missing resource: " + std::string(name));
    return resources[static_cast<std::size_t>(reference)];
}

std::int64_t spreadsheet_integer(const list_stream::ListValue& atom, std::string_view name) {
    if (atom.is_list || atom.atom_kind != list_stream::ListValue::AtomKind::raw)
        throw std::runtime_error("Spreadsheet Format requires an integer atom: " + std::string(name));
    return list_stream::detail::parse_integer<std::int64_t>(atom.atom, name);
}

void require_spreadsheet_scalar_range(std::size_t bit, std::int64_t value, std::string_view name) {
    const auto minimum = bit == 0 || bit == 6 || bit == 7 ? INT16_MIN : INT32_MIN;
    const auto maximum = bit == 0 || bit == 6 || bit == 7 ? INT16_MAX : INT32_MAX;
    if (value < minimum || value > maximum)
        throw std::runtime_error("Spreadsheet Format value exceeds storage width: " + std::string(name));
}

}  // namespace

list_stream::ListValue encode_spreadsheet_format(
    const model::SpreadsheetDocumentFormat& value, SpreadsheetFormatResources& resources) {
    using model::metamodel::ValueCodec;
    const auto descriptors = model::metamodel::spreadsheet_format_properties();
    if (descriptors.size() != 48) throw std::logic_error("invalid Spreadsheet Format descriptor count");
    value.properties.for_each_explicit([&](const model::PropertyEntry& entry) {
        if (std::ranges::find(descriptors, entry.id, &model::metamodel::PropertyDescriptor::id) == descriptors.end())
            throw std::runtime_error("Spreadsheet Format contains an unknown property");
    });
    std::uint64_t mask = 0;
    std::vector<list_stream::ListValue> fields{list_stream::ListValue::raw_atom("0")};
    for (std::size_t bit = 0; bit < descriptors.size(); ++bit) {
        const auto& descriptor = descriptors[bit];
        const auto* entry = value.properties.find(descriptor.id);
        if (!entry) continue;
        const auto name = descriptor.xml_name;
        std::int64_t encoded = 0;
        switch (descriptor.value_codec) {
            case ValueCodec::font: {
                const auto& font = spreadsheet_typed_value<model::FontValue>(entry->value, name);
                (void)encode_font(font);
                encoded = intern_spreadsheet_resource(resources.fonts, font, INT16_MAX, name);
                break;
            }
            case ValueCodec::line: {
                const auto& line = spreadsheet_typed_value<model::LineValue>(entry->value, name);
                if (line.type.type_name != "SpreadsheetDocumentCellLineType")
                    throw std::runtime_error("Spreadsheet Format border requires SpreadsheetDocumentCellLineType");
                (void)encode_line(line);
                encoded = intern_spreadsheet_resource(resources.lines, line, UINT8_MAX, name);
                break;
            }
            case ValueCodec::color: {
                const auto& color = spreadsheet_typed_value<model::ColorValue>(entry->value, name);
                (void)encode_color(color);
                encoded = intern_spreadsheet_resource(resources.colors, color, INT32_MAX, name);
                break;
            }
            case ValueCodec::localized_string: {
                const auto& string = spreadsheet_typed_value<model::LocalizedStringValue>(entry->value, name);
                (void)encode_localized_string(string);
                encoded = intern_spreadsheet_resource(bit == 34 ? resources.masks : resources.strings,
                    string, INT32_MAX, name);
                break;
            }
            case ValueCodec::type_domain: {
                const auto& type = spreadsheet_typed_value<model::TypeDomainPatternValue>(entry->value, name);
                (void)encode_type_domain(type);
                encoded = intern_spreadsheet_resource(resources.types, type, INT32_MAX, name);
                break;
            }
            case ValueCodec::uuid: {
                const auto& uuid = spreadsheet_typed_value<model::UuidValue>(entry->value, name);
                if (!list_stream::detail::is_canonical_guid(uuid.canonical))
                    throw std::runtime_error("Spreadsheet Format ControlType requires a canonical UUID");
                encoded = intern_spreadsheet_resource(resources.control_types, uuid, INT32_MAX, name);
                break;
            }
            case ValueCodec::picture: {
                const auto& picture = spreadsheet_typed_value<model::PictureRef>(entry->value, name);
                encoded = static_cast<std::int64_t>(intern_spreadsheet_resource(resources.pictures,
                    picture, INT32_MAX - 1, name)) + 1;
                break;
            }
            case ValueCodec::boolean:
                encoded = spreadsheet_typed_value<bool>(entry->value, name) != (bit == 16);
                break;
            case ValueCodec::decimal: {
                const auto& decimal = spreadsheet_typed_value<model::DecimalValue>(entry->value, name);
                const auto canonical = canonical_decimal(decimal.canonical);
                encoded = list_stream::detail::parse_integer<std::int64_t>(canonical, name);
                require_spreadsheet_scalar_range(bit, encoded, name);
                break;
            }
            case ValueCodec::enumeration: {
                const auto& enumeration = spreadsheet_typed_value<model::EnumerationValue>(entry->value, name);
                if (enumeration.type_name != descriptor.enumeration_type)
                    throw std::runtime_error("Spreadsheet Format enumeration type disagrees with its descriptor: " + std::string(name));
                const auto members = spreadsheet_enum_members(enumeration.type_name);
                const auto member = std::ranges::find(members, enumeration.member, &SpreadsheetEnumMember::first);
                if (member == members.end())
                    throw std::runtime_error("Spreadsheet Format enumeration member is unsupported: " + enumeration.member);
                encoded = member->second;
                break;
            }
            default: throw std::logic_error("invalid Spreadsheet Format property codec");
        }
        mask |= std::uint64_t{1} << bit;
        fields.push_back(list_stream::ListValue::raw_atom(std::to_string(encoded)));
    }
    fields[0].atom = std::to_string(mask);
    return list_stream::ListValue::list(std::move(fields));
}

model::SpreadsheetDocumentFormat decode_spreadsheet_format(
    const list_stream::ListValue& value, const SpreadsheetFormatResources& resources) {
    using model::metamodel::ValueCodec;
    if (!value.is_list || value.items.empty()) throw std::runtime_error("Spreadsheet Format must be a nonempty list");
    const auto& mask_atom = value.items.front();
    if (mask_atom.is_list || mask_atom.atom_kind != list_stream::ListValue::AtomKind::raw)
        throw std::runtime_error("Spreadsheet Format mask must be an integer atom");
    const auto mask = list_stream::detail::parse_integer<std::uint64_t>(mask_atom.atom, "Spreadsheet Format mask");
    if ((mask >> 48) != 0) throw std::runtime_error("Spreadsheet Format contains unsupported property bits");
    if (value.items.size() != 1 + static_cast<std::size_t>(std::popcount(mask)))
        throw std::runtime_error("Spreadsheet Format field count disagrees with its mask");
    const auto descriptors = model::metamodel::spreadsheet_format_properties();
    if (descriptors.size() != 48) throw std::logic_error("invalid Spreadsheet Format descriptor count");
    model::SpreadsheetDocumentFormat result;
    std::size_t cursor = 1;
    for (std::size_t bit = 0; bit < descriptors.size(); ++bit) {
        if ((mask & (std::uint64_t{1} << bit)) == 0) continue;
        const auto& descriptor = descriptors[bit];
        const auto name = descriptor.xml_name;
        const auto encoded = spreadsheet_integer(value.items[cursor++], name);
        require_spreadsheet_scalar_range(bit, encoded, name);
        model::PropertyValue decoded;
        switch (descriptor.value_codec) {
            case ValueCodec::font: decoded = spreadsheet_resource(resources.fonts, encoded, name); break;
            case ValueCodec::line: {
                if (encoded < 0 || encoded > UINT8_MAX)
                    throw std::runtime_error("Spreadsheet Format border reference exceeds uint8");
                const auto& line = spreadsheet_resource(resources.lines, encoded, name);
                if (line.type.type_name != "SpreadsheetDocumentCellLineType")
                    throw std::runtime_error("Spreadsheet Format border resource has the wrong line family");
                decoded = line;
                break;
            }
            case ValueCodec::color: decoded = spreadsheet_resource(resources.colors, encoded, name); break;
            case ValueCodec::localized_string:
                decoded = spreadsheet_resource(bit == 34 ? resources.masks : resources.strings, encoded, name); break;
            case ValueCodec::type_domain: decoded = spreadsheet_resource(resources.types, encoded, name); break;
            case ValueCodec::uuid: decoded = spreadsheet_resource(resources.control_types, encoded, name); break;
            case ValueCodec::picture:
                if (encoded == 0) throw std::runtime_error("Spreadsheet Format null Picture reference is not established");
                decoded = spreadsheet_resource(resources.pictures, encoded - 1, name); break;
            case ValueCodec::boolean:
                if (encoded != 0 && encoded != 1) throw std::runtime_error("Spreadsheet Format Boolean must be 0 or 1: " + std::string(name));
                decoded = (encoded != 0) != (bit == 16);
                break;
            case ValueCodec::decimal: decoded = model::DecimalValue{std::to_string(encoded)}; break;
            case ValueCodec::enumeration: {
                const auto members = spreadsheet_enum_members(descriptor.enumeration_type);
                const auto member = std::ranges::find(members, encoded, &SpreadsheetEnumMember::second);
                if (member == members.end())
                    throw std::runtime_error("Spreadsheet Format enumeration storage value is unsupported: " + std::string(name));
                decoded = model::EnumerationValue{std::string(descriptor.enumeration_type), std::string(member->first)};
                break;
            }
            default: throw std::logic_error("invalid Spreadsheet Format property codec");
        }
        result.properties.set_explicit(descriptor.id, std::move(decoded));
    }
    return result;
}

std::string encode_font(const model::FontValue& value) {
    return encode_value(value, write_font);
}

model::FontValue decode_font(std::string_view text) {
    list_stream::ListInStream in(text);
    const auto value = read_font(in);
    if (in.has_next()) throw std::runtime_error("Font stream contains trailing values");
    return value;
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
