#pragma once

#include <cstdlib>
#include <cstdint>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace oof::platform {

struct CompositeID {
    std::string text;
};

struct TypeDomainPatternItem {
    std::string code;
    std::string type_name;
    std::string kind;
    std::string uuid;
    std::string digits;
    std::string fraction_digits;
    std::string allowed_sign;
    std::string length;
    std::string allowed_length;
    std::string date_parts;
};

struct TypeDomainPattern {
    std::vector<TypeDomainPatternItem> items;
};

struct LocalWString {
    std::string value;
};

struct LocalizedStringItem {
    std::string lang;
    LocalWString text;
};

struct LocalizedStringRecord {
    std::string version = "1";
    std::vector<LocalizedStringItem> items;
};

struct FormattedString {
    LocalWString text;
};

struct Color {
    std::uint32_t value = 0;
};

struct Font {
    std::string name;
    std::int32_t size = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeout = false;
};

struct V8Border {
    std::int32_t style = 0;
    std::int32_t width = 0;
    Color color;
};

struct V8Picture {
    std::string storage_id;
};

struct ShortCut {
    std::uint32_t value = 0;
};

struct Date {
    std::string value;
};

struct Numeric {
    std::string value;
};

struct PersistenceStorage {
    std::vector<std::uint8_t> payload;
};

struct GenericValue {
    using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string, CompositeID, TypeDomainPattern>;

    Value value;
};

inline std::string clean_atom(const std::string& value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return value;
    }
    std::string result;
    for (size_t index = 1; index + 1 < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '"' && index + 1 < value.size() - 1 && value[index + 1] == '"') {
            result.push_back('"');
            ++index;
            continue;
        }
        if (ch == '\\' && index + 1 < value.size() - 1 && (value[index + 1] == '\\' || value[index + 1] == '"')) {
            result.push_back(value[index + 1]);
            ++index;
            continue;
        }
        result.push_back(ch);
    }
    return result;
}

inline std::string quote_atom(const std::string& value) {
    std::string result = "\"";
    for (char ch : value) {
        if (ch == '\\') {
            result += "\\\\";
        } else if (ch == '"') {
            result += "\"\"";
        } else {
            result.push_back(ch);
        }
    }
    result += "\"";
    return result;
}

inline bool is_integer_atom(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    std::strtoll(value.c_str(), &end, 10);
    return end != value.c_str() && *end == '\0';
}

inline bool is_localized_lang(const std::string& value) {
    static const std::regex localized_lang_re(R"(^(#|[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*)$)");
    return std::regex_match(value, localized_lang_re);
}

inline CompositeID parse_composite_id(const std::string& value) {
    static const std::regex composite_id_re(
        R"(^-?[0-9]+(?::[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})?$)"
    );
    const std::string text = clean_atom(value);
    if (!std::regex_match(text, composite_id_re)) {
        throw std::runtime_error("Invalid 1C CompositeID: " + text);
    }
    return CompositeID{text};
}

inline std::string type_name_for_code(const std::string& code) {
    if (code == "S") {
        return "xs:string";
    }
    if (code == "N") {
        return "xs:decimal";
    }
    if (code == "B") {
        return "xs:boolean";
    }
    if (code == "D") {
        return "xs:dateTime";
    }
    if (code == "U") {
        return "xs:anyType";
    }
    if (code == "#") {
        return "cfg:uuid";
    }
    return "unknown:" + code;
}

inline TypeDomainPattern parse_type_domain_pattern(const std::vector<std::string>& pattern) {
    TypeDomainPattern result;
    size_t index = 0;
    while (index < pattern.size()) {
        const std::string code = clean_atom(pattern[index]);
        if (code == "#") {
            const std::string uuid = index + 1 < pattern.size() ? clean_atom(pattern[index + 1]) : "";
            TypeDomainPatternItem item;
            item.code = "#";
            item.uuid = uuid;
            item.type_name = uuid.empty() ? "cfg:unknown" : "cfg:uuid." + uuid;
            item.kind = "reference";
            result.items.push_back(std::move(item));
            index += 2;
            continue;
        }
        if (code == "N" && index + 3 < pattern.size()) {
            const std::string digits = clean_atom(pattern[index + 1]);
            const std::string fraction_digits = clean_atom(pattern[index + 2]);
            const std::string allowed_sign = clean_atom(pattern[index + 3]);
            if (is_integer_atom(digits) && is_integer_atom(fraction_digits) && is_integer_atom(allowed_sign)) {
                TypeDomainPatternItem item;
                item.code = "N";
                item.type_name = "xs:decimal";
                item.kind = "primitive";
                item.digits = digits;
                item.fraction_digits = fraction_digits;
                item.allowed_sign = allowed_sign == "0" ? "Any" : allowed_sign == "1" ? "NonNegative" : "code:" + allowed_sign;
                result.items.push_back(std::move(item));
                index += 4;
                continue;
            }
        }
        if (code == "S" && index + 2 < pattern.size()) {
            const std::string length = clean_atom(pattern[index + 1]);
            const std::string allowed_length = clean_atom(pattern[index + 2]);
            if (is_integer_atom(length) && is_integer_atom(allowed_length)) {
                TypeDomainPatternItem item;
                item.code = "S";
                item.type_name = "xs:string";
                item.kind = "primitive";
                item.length = length;
                item.allowed_length = allowed_length == "0" ? "Variable" : allowed_length == "1" ? "Fixed" : "code:" + allowed_length;
                result.items.push_back(std::move(item));
                index += 3;
                continue;
            }
        }
        if (code == "D" && index + 1 < pattern.size()) {
            const std::string date_parts = clean_atom(pattern[index + 1]);
            if (is_integer_atom(date_parts)) {
                TypeDomainPatternItem item;
                item.code = "D";
                item.type_name = "xs:dateTime";
                item.kind = "primitive";
                item.date_parts = "code:" + date_parts;
                result.items.push_back(std::move(item));
                index += 2;
                continue;
            }
        }
        TypeDomainPatternItem item;
        item.code = code;
        item.type_name = type_name_for_code(code);
        item.kind = item.type_name.rfind("unknown:", 0) == 0 ? "unknown" : "primitive";
        result.items.push_back(std::move(item));
        ++index;
    }
    return result;
}

inline std::vector<std::string> dump_type_domain_pattern(const TypeDomainPattern& pattern) {
    std::vector<std::string> result;
    for (const TypeDomainPatternItem& item : pattern.items) {
        if (item.code == "#") {
            result.push_back(quote_atom("#"));
            result.push_back(item.uuid);
        } else if (item.code == "N" && !item.digits.empty() && !item.fraction_digits.empty() && !item.allowed_sign.empty()) {
            std::string allowed_sign_code = item.allowed_sign;
            if (allowed_sign_code == "Any") {
                allowed_sign_code = "0";
            } else if (allowed_sign_code == "NonNegative") {
                allowed_sign_code = "1";
            } else if (allowed_sign_code.rfind("code:", 0) == 0) {
                allowed_sign_code = allowed_sign_code.substr(5);
            }
            result.push_back(quote_atom("N"));
            result.push_back(item.digits);
            result.push_back(item.fraction_digits);
            result.push_back(allowed_sign_code);
        } else if (item.code == "S" && !item.length.empty() && !item.allowed_length.empty()) {
            std::string allowed_length_code = item.allowed_length;
            if (allowed_length_code == "Variable") {
                allowed_length_code = "0";
            } else if (allowed_length_code == "Fixed") {
                allowed_length_code = "1";
            } else if (allowed_length_code.rfind("code:", 0) == 0) {
                allowed_length_code = allowed_length_code.substr(5);
            }
            result.push_back(quote_atom("S"));
            result.push_back(item.length);
            result.push_back(allowed_length_code);
        } else if (item.code == "D" && !item.date_parts.empty()) {
            std::string date_parts_code = item.date_parts;
            if (date_parts_code.rfind("code:", 0) == 0) {
                date_parts_code = date_parts_code.substr(5);
            }
            result.push_back(quote_atom("D"));
            result.push_back(date_parts_code);
        } else if (is_integer_atom(item.code)) {
            result.push_back(item.code);
        } else {
            result.push_back(quote_atom(item.code));
        }
    }
    return result;
}

inline GenericValue value_from_string_internal(const std::string& value) {
    if (value.empty()) {
        return GenericValue{std::monostate{}};
    }
    if (value == "true") {
        return GenericValue{true};
    }
    if (value == "false") {
        return GenericValue{false};
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return GenericValue{clean_atom(value)};
    }
    char* int_end = nullptr;
    const long long int_value = std::strtoll(value.c_str(), &int_end, 10);
    if (int_end != value.c_str() && *int_end == '\0') {
        return GenericValue{static_cast<std::int64_t>(int_value)};
    }
    char* double_end = nullptr;
    const double double_value = std::strtod(value.c_str(), &double_end);
    if (double_end != value.c_str() && *double_end == '\0') {
        return GenericValue{double_value};
    }
    return GenericValue{value};
}

inline std::string value_to_string_internal(const GenericValue& value) {
    if (std::holds_alternative<std::monostate>(value.value)) {
        return "";
    }
    if (const auto* item = std::get_if<bool>(&value.value)) {
        return *item ? "true" : "false";
    }
    if (const auto* item = std::get_if<std::int64_t>(&value.value)) {
        return std::to_string(*item);
    }
    if (const auto* item = std::get_if<double>(&value.value)) {
        std::string text = std::to_string(*item);
        while (text.size() > 1 && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.push_back('0');
        }
        return text;
    }
    if (const auto* item = std::get_if<std::string>(&value.value)) {
        return quote_atom(*item);
    }
    if (const auto* item = std::get_if<CompositeID>(&value.value)) {
        return item->text;
    }
    return "";
}

inline LocalizedStringRecord localized_text_record(const std::string& text, const std::string& lang = "ru") {
    if (!is_localized_lang(lang)) {
        throw std::runtime_error("Invalid localized string language: " + lang);
    }
    return LocalizedStringRecord{"1", {LocalizedStringItem{lang, LocalWString{text}}}};
}

}  // namespace oof::platform
