#pragma once

#include <charconv>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace oof::storage::list_stream {

struct ListValue {
    enum class AtomKind { raw, string };

    bool is_list = false;
    AtomKind atom_kind = AtomKind::raw;
    std::string atom;
    std::vector<ListValue> items;

    static ListValue raw_atom(std::string value) {
        ListValue node;
        node.atom = std::move(value);
        return node;
    }

    static ListValue string_atom(std::string value) {
        ListValue node;
        node.atom_kind = AtomKind::string;
        node.atom = std::move(value);
        return node;
    }

    static ListValue list(std::vector<ListValue> value) {
        ListValue node;
        node.is_list = true;
        node.items = std::move(value);
        return node;
    }
};

inline std::string quote_string(std::string_view value) {
    std::string out = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out += ch;
        }
    }
    out += "\"";
    return out;
}

inline std::string dump_compact(const ListValue& value) {
    if (!value.is_list) {
        if (value.atom_kind == ListValue::AtomKind::string) {
            return quote_string(value.atom);
        }
        return value.atom;
    }
    std::string out = "{";
    for (std::size_t index = 0; index < value.items.size(); ++index) {
        if (index != 0) {
            out += ",";
        }
        out += dump_compact(value.items[index]);
    }
    out += "}";
    return out;
}

namespace detail {

inline bool contains_list(const ListValue& value) {
    for (const auto& item : value.items) {
        if (item.is_list) {
            return true;
        }
    }
    return false;
}

inline bool is_base64_payload_list(const ListValue& value) {
    return value.is_list &&
           !value.items.empty() &&
           !value.items[0].is_list &&
           value.items[0].atom.rfind("#base64:", 0) == 0;
}

inline bool is_ascii_hex_digit(char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

inline bool is_canonical_guid(std::string_view value) {
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const bool hyphen = index == 8 || index == 13 || index == 18 || index == 23;
        if (hyphen ? value[index] != '-' : !is_ascii_hex_digit(value[index])) {
            return false;
        }
    }
    return true;
}

template <typename Integer>
inline Integer parse_integer(std::string_view atom, std::string_view type_name) {
    Integer parsed{};
    const char* const begin = atom.data();
    const char* const end = begin + atom.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (atom.empty() || result.ec != std::errc{} || result.ptr != end) {
        throw std::runtime_error("ListInStream invalid " + std::string(type_name) + " atom");
    }
    return parsed;
}

struct Token {
    enum class Kind { open, close, comma, raw_atom, string_atom };
    Kind kind;
    std::string value;
    std::size_t offset = 0;
    bool preceded_by_whitespace = false;
};

inline bool is_token_separator(char ch) {
    return ch == '{' || ch == '}' || ch == ',';
}

inline std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> tokens;
    std::size_t index = 0;
    while (index < text.size()) {
        bool preceded_by_whitespace = false;
        while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index]))) {
            preceded_by_whitespace = true;
            ++index;
        }
        if (index == text.size()) {
            break;
        }
        const std::size_t start = index;
        if (text[index] == '{') {
            tokens.push_back({Token::Kind::open, "{", start, preceded_by_whitespace});
            ++index;
            continue;
        }
        if (text[index] == '}') {
            tokens.push_back({Token::Kind::close, "}", start, preceded_by_whitespace});
            ++index;
            continue;
        }
        if (text[index] == ',') {
            tokens.push_back({Token::Kind::comma, ",", start, preceded_by_whitespace});
            ++index;
            continue;
        }
        if (text[index] == '"') {
            ++index;
            std::string value;
            while (index < text.size()) {
                const char current = text[index++];
                if (current == '"') {
                    if (index < text.size() && text[index] == '"') {
                        value += '"';
                        ++index;
                        continue;
                    }
                    while (index < text.size() && text[index] == '\\') {
                        // Platform strings continue with a quoted UTF-16 code unit.
                        const auto read_code_unit = [&]() -> std::uint32_t {
                            if (text.size() - index < 6 || text[index] != '\\' ||
                                text[index + 5] != '"') {
                                throw std::runtime_error("ListInStream truncated UTF-16 string escape");
                            }
                            std::uint32_t unit = 0;
                            for (std::size_t digit = 1; digit <= 4; ++digit) {
                                const char ch = text[index + digit];
                                if (!is_ascii_hex_digit(ch)) {
                                    throw std::runtime_error("ListInStream invalid UTF-16 string escape");
                                }
                                unit = (unit << 4) | static_cast<std::uint32_t>(
                                    ch <= '9' ? ch - '0' :
                                    ch <= 'F' ? ch - 'A' + 10 : ch - 'a' + 10);
                            }
                            index += 6;
                            return unit;
                        };
                        std::uint32_t code_point = read_code_unit();
                        if (code_point >= 0xd800 && code_point <= 0xdbff) {
                            const auto low = read_code_unit();
                            if (low < 0xdc00 || low > 0xdfff) {
                                throw std::runtime_error("ListInStream invalid UTF-16 surrogate pair");
                            }
                            code_point = 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
                        } else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
                            throw std::runtime_error("ListInStream unpaired UTF-16 low surrogate");
                        }
                        if (code_point < 0x80) {
                            value += static_cast<char>(code_point);
                        } else if (code_point < 0x800) {
                            value += static_cast<char>(0xc0 | (code_point >> 6));
                            value += static_cast<char>(0x80 | (code_point & 0x3f));
                        } else if (code_point < 0x10000) {
                            value += static_cast<char>(0xe0 | (code_point >> 12));
                            value += static_cast<char>(0x80 | ((code_point >> 6) & 0x3f));
                            value += static_cast<char>(0x80 | (code_point & 0x3f));
                        } else {
                            value += static_cast<char>(0xf0 | (code_point >> 18));
                            value += static_cast<char>(0x80 | ((code_point >> 12) & 0x3f));
                            value += static_cast<char>(0x80 | ((code_point >> 6) & 0x3f));
                            value += static_cast<char>(0x80 | (code_point & 0x3f));
                        }
                    }
                    if (index < text.size()) {
                        const unsigned char next = static_cast<unsigned char>(text[index]);
                        if (!std::isspace(next) && !is_token_separator(text[index])) {
                            throw std::runtime_error(
                                "ListInStream invalid character after quoted string at offset " +
                                std::to_string(index));
                        }
                    }
                    tokens.push_back({Token::Kind::string_atom, value, start, preceded_by_whitespace});
                    goto next_token;
                }
                value += current;
            }
            throw std::runtime_error("ListInStream unterminated string at offset " + std::to_string(start));
        }
        while (index < text.size()) {
            const unsigned char current = static_cast<unsigned char>(text[index]);
            if (std::isspace(current) || is_token_separator(text[index])) {
                break;
            }
            if (text[index] == '"') {
                throw std::runtime_error(
                    "ListInStream unexpected quote in raw atom at offset " + std::to_string(index));
            }
            ++index;
        }
        tokens.push_back({
            Token::Kind::raw_atom,
            std::string(text.substr(start, index - start)),
            start,
            preceded_by_whitespace,
        });
    next_token:
        continue;
    }
    return tokens;
}

class ListParser {
public:
    explicit ListParser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    ListValue parse_document() {
        ListValue value = parse_value();
        if (index_ != tokens_.size()) {
            throw std::runtime_error("ListInStream trailing token at offset " + std::to_string(tokens_[index_].offset));
        }
        return value;
    }

private:
    ListValue parse_value() {
        if (index_ >= tokens_.size()) {
            throw std::runtime_error("ListInStream unexpected end of input");
        }
        const Token token = tokens_[index_++];
        if (token.kind == Token::Kind::open) {
            return parse_list();
        }
        if (token.kind == Token::Kind::raw_atom) {
            return ListValue::raw_atom(token.value);
        }
        if (token.kind == Token::Kind::string_atom) {
            return ListValue::string_atom(token.value);
        }
        throw std::runtime_error("ListInStream unexpected token at offset " + std::to_string(token.offset));
    }

    ListValue parse_list() {
        std::vector<ListValue> items;
        bool expecting_value = true;
        bool saw_separator = false;
        while (index_ < tokens_.size()) {
            const Token token = tokens_[index_];
            if (token.kind == Token::Kind::close) {
                if (expecting_value && saw_separator) {
                    items.push_back(ListValue::raw_atom(""));
                }
                ++index_;
                return ListValue::list(std::move(items));
            }
            if (token.kind == Token::Kind::comma) {
                if (expecting_value) {
                    items.push_back(ListValue::raw_atom(""));
                }
                ++index_;
                expecting_value = true;
                saw_separator = true;
                continue;
            }
            if (!expecting_value) {
                const bool is_base64_chunk =
                    !items.empty() &&
                    !items.front().is_list &&
                    items.front().atom_kind == ListValue::AtomKind::raw &&
                    items.front().atom.rfind("#base64:", 0) == 0 &&
                    !items.back().is_list &&
                    items.back().atom_kind == ListValue::AtomKind::raw &&
                    token.kind == Token::Kind::raw_atom &&
                    token.preceded_by_whitespace;
                if (!is_base64_chunk) {
                    throw std::runtime_error(
                        "ListInStream missing comma before value at offset " + std::to_string(token.offset));
                }
            }
            items.push_back(parse_value());
            expecting_value = false;
            saw_separator = false;
        }
        throw std::runtime_error("ListInStream missing closing list delimiter");
    }

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
};

}  // namespace detail

inline std::string dump_listout(const ListValue& value) {
    if (!value.is_list) {
        return dump_compact(value);
    }
    if (detail::is_base64_payload_list(value)) {
        std::string out = "{";
        bool previous_was_atom = false;
        for (std::size_t index = 0; index < value.items.size(); ++index) {
            if (index != 0) {
                out += (!value.items[index].is_list && previous_was_atom) ? "\r\n\r\n" : ",";
            }
            out += dump_listout(value.items[index]);
            previous_was_atom = !value.items[index].is_list;
        }
        out += "}";
        return out;
    }
    if (!detail::contains_list(value)) {
        return dump_compact(value);
    }
    std::string out = "{";
    for (std::size_t index = 0; index < value.items.size(); ++index) {
        if (index != 0) {
            out += ",";
        }
        if (value.items[index].is_list) {
            out += "\r\n";
        }
        out += dump_listout(value.items[index]);
    }
    if (!value.items.empty() && value.items.back().is_list) {
        out += "\r\n";
    }
    out += "}";
    return out;
}

class ListOutStream {
public:
    void begin_list() {
        stack_.push_back(ListValue::list({}));
    }

    void end_list() {
        if (stack_.empty()) {
            throw std::runtime_error("ListOutStream end_list without begin_list");
        }
        ListValue value = std::move(stack_.back());
        stack_.pop_back();
        append(std::move(value));
    }

    void write_uint32(std::uint32_t value) {
        append(ListValue::raw_atom(std::to_string(value)));
    }

    void write_int64(std::int64_t value) {
        append(ListValue::raw_atom(std::to_string(value)));
    }

    void write_double(double value) {
        if (!std::isfinite(value)) {
            throw std::runtime_error("ListOutStream cannot write non-finite double");
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(17) << value;
        append(ListValue::raw_atom(out.str()));
    }

    void write_bool(bool value) {
        append(ListValue::raw_atom(value ? "1" : "0"));
    }

    void write_guid(std::string value) {
        if (!detail::is_canonical_guid(value)) {
            throw std::runtime_error("ListOutStream expected canonical guid");
        }
        append(ListValue::raw_atom(std::move(value)));
    }

    void write_raw_atom(std::string value) {
        append(ListValue::raw_atom(std::move(value)));
    }

    void write_string(std::string value) {
        append(ListValue::string_atom(std::move(value)));
    }

    const ListValue& root() const {
        if (!root_ready_ || !stack_.empty()) {
            throw std::runtime_error("ListOutStream root requested before stream is closed");
        }
        return root_;
    }

    std::string text() const {
        return dump_compact(root());
    }

private:
    void append(ListValue value) {
        if (!stack_.empty()) {
            stack_.back().items.push_back(std::move(value));
            return;
        }
        if (root_ready_) {
            throw std::runtime_error("ListOutStream cannot write multiple root values");
        }
        root_ = std::move(value);
        root_ready_ = true;
    }

    ListValue root_;
    bool root_ready_ = false;
    std::vector<ListValue> stack_;
};

inline ListValue parse(std::string_view text) {
    return detail::ListParser(detail::tokenize(text)).parse_document();
}

class ListInStream {
public:
    explicit ListInStream(ListValue root) : root_(std::move(root)) {}
    explicit ListInStream(std::string_view text) : root_(parse(text)) {}

    void begin_list() {
        const ListValue& value = take_value();
        if (!value.is_list) {
            throw std::runtime_error("ListInStream expected list");
        }
        stack_.push_back(Frame{&value, 0});
    }

    void end_list() {
        if (stack_.empty()) {
            throw std::runtime_error("ListInStream end_list without begin_list");
        }
        const Frame& frame = stack_.back();
        if (frame.index != frame.value->items.size()) {
            throw std::runtime_error("ListInStream list has unread items");
        }
        stack_.pop_back();
    }

    bool has_next() const {
        if (stack_.empty()) {
            return !root_consumed_;
        }
        const Frame& frame = stack_.back();
        return frame.index < frame.value->items.size();
    }

    std::uint32_t read_uint32() {
        const ListValue& value = take_raw_atom("uint32");
        return detail::parse_integer<std::uint32_t>(value.atom, "uint32");
    }

    std::int64_t read_int64() {
        const ListValue& value = take_raw_atom("int64");
        return detail::parse_integer<std::int64_t>(value.atom, "int64");
    }

    double read_double() {
        const ListValue& value = take_raw_atom("double");
        double parsed = 0.0;
        const char* const begin = value.atom.data();
        const char* const end = begin + value.atom.size();
        const auto result = std::from_chars(begin, end, parsed, std::chars_format::general);
        if (value.atom.empty() || result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) {
            throw std::runtime_error("ListInStream invalid double atom");
        }
        return parsed;
    }

    bool read_bool() {
        const std::uint32_t value = read_uint32();
        if (value > 1) {
            throw std::runtime_error("ListInStream expected bool atom");
        }
        return value != 0;
    }

    std::string read_guid() {
        const ListValue& value = take_raw_atom("guid");
        if (!detail::is_canonical_guid(value.atom)) {
            throw std::runtime_error("ListInStream invalid guid atom");
        }
        return value.atom;
    }

    std::string read_raw_atom() {
        return take_raw_atom("raw").atom;
    }

    std::string read_string() {
        const ListValue& value = take_value();
        if (value.is_list || value.atom_kind != ListValue::AtomKind::string) {
            throw std::runtime_error("ListInStream expected string atom");
        }
        return value.atom;
    }

private:
    struct Frame {
        const ListValue* value;
        std::size_t index;
    };

    const ListValue& take_raw_atom(std::string_view type_name) {
        const ListValue& value = take_value();
        if (value.is_list || value.atom_kind != ListValue::AtomKind::raw) {
            throw std::runtime_error("ListInStream expected " + std::string(type_name) + " atom");
        }
        return value;
    }

    const ListValue& take_value() {
        if (stack_.empty()) {
            if (root_consumed_) {
                throw std::runtime_error("ListInStream root already consumed");
            }
            root_consumed_ = true;
            return root_;
        }
        Frame& frame = stack_.back();
        if (frame.index >= frame.value->items.size()) {
            throw std::runtime_error("ListInStream unexpected end of list");
        }
        return frame.value->items[frame.index++];
    }

    ListValue root_;
    bool root_consumed_ = false;
    std::vector<Frame> stack_;
};

}  // namespace oof::storage::list_stream
