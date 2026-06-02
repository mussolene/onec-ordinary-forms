#include <cctype>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "platform_mechanism.hpp"
#include "platform_value.hpp"

namespace {

struct Node {
    bool is_list = false;
    std::string atom;
    std::vector<Node> items;

    static Node make_atom(std::string value) {
        Node node;
        node.atom = std::move(value);
        return node;
    }

    static Node make_list(std::vector<Node> value) {
        Node node;
        node.is_list = true;
        node.items = std::move(value);
        return node;
    }
};

struct Token {
    enum class Kind { open_brace, close_brace, open_bracket, close_bracket, comma, atom };
    Kind kind;
    std::string value;
    size_t start = 0;
};

[[noreturn]] void parse_error(const std::string& message) {
    throw std::runtime_error("list-stream parse error: " + message);
}

std::vector<Token> tokenize(const std::string& text) {
    std::vector<Token> tokens;
    size_t index = 0;
    while (index < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (std::isspace(ch)) {
            ++index;
            continue;
        }
        const size_t start = index;
        switch (text[index]) {
            case '{':
                tokens.push_back({Token::Kind::open_brace, "{", start});
                ++index;
                continue;
            case '}':
                tokens.push_back({Token::Kind::close_brace, "}", start});
                ++index;
                continue;
            case '[':
                tokens.push_back({Token::Kind::open_bracket, "[", start});
                ++index;
                continue;
            case ']':
                tokens.push_back({Token::Kind::close_bracket, "]", start});
                ++index;
                continue;
            case ',':
                tokens.push_back({Token::Kind::comma, ",", start});
                ++index;
                continue;
            case '"': {
                ++index;
                while (index < text.size()) {
                    const char current = text[index++];
                    if (current == '"') {
                        if (index < text.size() && text[index] == '"') {
                            ++index;
                            continue;
                        }
                        tokens.push_back({Token::Kind::atom, text.substr(start, index - start), start});
                        goto next_token;
                    }
                }
                parse_error("unterminated string literal");
            }
            default:
                while (index < text.size()) {
                    const unsigned char current = static_cast<unsigned char>(text[index]);
                    if (std::isspace(current) || text[index] == '{' || text[index] == '}' ||
                        text[index] == '[' || text[index] == ']' || text[index] == ',') {
                        break;
                    }
                    ++index;
                }
                if (index == start) {
                    parse_error("unexpected character at offset " + std::to_string(start));
                }
                tokens.push_back({Token::Kind::atom, text.substr(start, index - start), start});
                continue;
        }
    next_token:
        continue;
    }
    return tokens;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Node parse_document() {
        Node root = parse_value();
        if (index_ != tokens_.size()) {
            parse_error("unexpected trailing token at offset " + std::to_string(tokens_[index_].start));
        }
        return root;
    }

private:
    Node parse_value() {
        if (index_ >= tokens_.size()) {
            parse_error("unexpected end of input");
        }
        const Token token = tokens_[index_];
        if (token.kind == Token::Kind::open_brace) {
            return parse_list(Token::Kind::close_brace);
        }
        if (token.kind == Token::Kind::open_bracket) {
            return parse_list(Token::Kind::close_bracket);
        }
        if (token.kind == Token::Kind::atom) {
            ++index_;
            return Node::make_atom(token.value);
        }
        parse_error("unexpected token at offset " + std::to_string(token.start));
    }

    Node parse_list(Token::Kind close_kind) {
        ++index_;
        std::vector<Node> items;
        bool expecting_value = true;
        bool saw_separator = false;
        while (index_ < tokens_.size()) {
            const Token token = tokens_[index_];
            if (token.kind == close_kind) {
                if (expecting_value && saw_separator) {
                    items.push_back(Node::make_atom(""));
                }
                ++index_;
                return Node::make_list(std::move(items));
            }
            if (token.kind == Token::Kind::comma) {
                if (expecting_value) {
                    items.push_back(Node::make_atom(""));
                }
                ++index_;
                expecting_value = true;
                saw_separator = true;
                continue;
            }
            items.push_back(parse_value());
            expecting_value = false;
            saw_separator = false;
        }
        parse_error("missing closing list delimiter");
    }

    std::vector<Token> tokens_;
    size_t index_ = 0;
};

std::string dump_compact(const Node& node) {
    if (!node.is_list) {
        return node.atom;
    }
    std::string out = "{";
    for (size_t i = 0; i < node.items.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += dump_compact(node.items[i]);
    }
    out += "}";
    return out;
}

bool contains_list(const Node& node) {
    for (const Node& item : node.items) {
        if (item.is_list) {
            return true;
        }
    }
    return false;
}

std::string dump_listout(const Node& node) {
    if (!node.is_list) {
        return node.atom;
    }
    if (!contains_list(node)) {
        return dump_compact(node);
    }
    std::string out = "{";
    for (size_t i = 0; i < node.items.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        if (node.items[i].is_list) {
            out += "\r\n";
        }
        out += dump_listout(node.items[i]);
    }
    if (!node.items.empty() && node.items.back().is_list) {
        out += "\r\n";
    }
    out += "}";
    return out;
}

struct Stats {
    size_t lists = 0;
    size_t atoms = 0;
    size_t max_depth = 0;
};

void collect_stats(const Node& node, size_t depth, Stats& stats) {
    if (node.is_list) {
        ++stats.lists;
        if (depth > stats.max_depth) {
            stats.max_depth = depth;
        }
        for (const Node& item : node.items) {
            collect_stats(item, depth + 1, stats);
        }
    } else {
        ++stats.atoms;
    }
}
std::string read_stdin() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

void usage() {
    std::cerr << "Usage: oof-native <compact|listout|stats|mechanism|type-domain|value|localized> < stream.txt\n";
}

void print_json_string(std::string_view value) {
    std::cout << '"';
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            std::cout << '\\' << ch;
        } else {
            std::cout << ch;
        }
    }
    std::cout << '"';
}

void print_mechanism() {
    std::cout << "{";
    std::cout << "\"formats\":{";
    std::cout << "\"cf_form_controls8\":" << oof::platform::cf_form_controls8 << ",";
    std::cout << "\"cf_form_controls_position8\":" << oof::platform::cf_form_controls_position8 << ",";
    std::cout << "\"cf_form_controls_info8\":" << oof::platform::cf_form_controls_info8;
    std::cout << "},";
    std::cout << "\"recordSizes\":{";
    std::cout << "\"count\":" << oof::platform::transfer_count_size << ",";
    std::cout << "\"info\":" << oof::platform::info_transfer_record_size << ",";
    std::cout << "\"position\":" << oof::platform::position_transfer_record_size << ",";
    std::cout << "\"formatEntry\":" << oof::platform::format_entry_record_size;
    std::cout << "},";
    std::cout << "\"entries\":[";
    for (size_t i = 0; i < oof::platform::mechanism_entries.size(); ++i) {
        const auto& entry = oof::platform::mechanism_entries[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"address\":";
        print_json_string(entry.address);
        std::cout << ",\"role\":";
        print_json_string(entry.role);
        std::cout << ",\"platformSymbols\":";
        print_json_string(entry.platform_symbols);
        std::cout << ",\"nativeTarget\":";
        print_json_string(entry.native_target);
        std::cout << "}";
    }
    std::cout << "],\"coreValueSurface\":[";
    for (size_t i = 0; i < oof::platform::core_value_surface.size(); ++i) {
        if (i != 0) {
            std::cout << ",";
        }
        print_json_string(oof::platform::core_value_surface[i]);
    }
    std::cout << "],\"formObjectSurface\":[";
    for (size_t i = 0; i < oof::platform::form_object_surface.size(); ++i) {
        if (i != 0) {
            std::cout << ",";
        }
        print_json_string(oof::platform::form_object_surface[i]);
    }
    std::cout << "],\"metadataObjectSurface\":[";
    for (size_t i = 0; i < oof::platform::metadata_object_surface.size(); ++i) {
        if (i != 0) {
            std::cout << ",";
        }
        print_json_string(oof::platform::metadata_object_surface[i]);
    }
    std::cout << "],\"typeTreeSurface\":[";
    for (size_t i = 0; i < oof::platform::type_tree_surface.size(); ++i) {
        if (i != 0) {
            std::cout << ",";
        }
        print_json_string(oof::platform::type_tree_surface[i]);
    }
    std::cout << "],\"controlObjectDescriptions\":[";
    for (size_t i = 0; i < oof::platform::control_object_descriptions.size(); ++i) {
        const auto& item = oof::platform::control_object_descriptions[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"publicName\":";
        print_json_string(item.public_name);
        std::cout << ",\"platformName\":";
        print_json_string(item.platform_name);
        std::cout << ",\"managedEquivalent\":";
        print_json_string(item.managed_equivalent);
        std::cout << "}";
    }
    std::cout << "]}\n";
}

std::string trim_copy(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    return value.substr(start);
}

std::vector<std::string> atom_list(const Node& node) {
    if (!node.is_list) {
        throw std::runtime_error("expected list-stream list");
    }
    std::vector<std::string> result;
    for (const Node& item : node.items) {
        if (item.is_list) {
            throw std::runtime_error("expected flat atom list");
        }
        result.push_back(item.atom);
    }
    return result;
}

void print_json_string_array(const std::vector<std::string>& values) {
    std::cout << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            std::cout << ",";
        }
        print_json_string(values[i]);
    }
    std::cout << "]";
}

void print_type_domain(const Node& root) {
    const oof::platform::TypeDomainPattern pattern = oof::platform::parse_type_domain_pattern(atom_list(root));
    std::cout << "{\"encoding\":\"TypeDomainPattern\",\"itemCount\":" << pattern.items.size() << ",\"items\":[";
    for (size_t i = 0; i < pattern.items.size(); ++i) {
        const auto& item = pattern.items[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"code\":";
        print_json_string(item.code);
        std::cout << ",\"typeName\":";
        print_json_string(item.type_name);
        std::cout << ",\"kind\":";
        print_json_string(item.kind);
        if (!item.uuid.empty()) {
            std::cout << ",\"uuid\":";
            print_json_string(item.uuid);
        }
        if (!item.digits.empty()) {
            std::cout << ",\"digits\":";
            print_json_string(item.digits);
        }
        if (!item.fraction_digits.empty()) {
            std::cout << ",\"fractionDigits\":";
            print_json_string(item.fraction_digits);
        }
        if (!item.allowed_sign.empty()) {
            std::cout << ",\"allowedSign\":";
            print_json_string(item.allowed_sign);
        }
        if (!item.length.empty()) {
            std::cout << ",\"length\":";
            print_json_string(item.length);
        }
        if (!item.allowed_length.empty()) {
            std::cout << ",\"allowedLength\":";
            print_json_string(item.allowed_length);
        }
        if (!item.date_parts.empty()) {
            std::cout << ",\"dateParts\":";
            print_json_string(item.date_parts);
        }
        std::cout << "}";
    }
    std::cout << "],\"roundtrip\":";
    print_json_string_array(oof::platform::dump_type_domain_pattern(pattern));
    std::cout << "}\n";
}

void print_value(const std::string& raw) {
    const oof::platform::GenericValue value = oof::platform::value_from_string_internal(raw);
    std::cout << "{";
    if (std::holds_alternative<std::monostate>(value.value)) {
        std::cout << "\"kind\":\"null\",\"value\":null";
    } else if (const auto* item = std::get_if<bool>(&value.value)) {
        std::cout << "\"kind\":\"boolean\",\"value\":" << (*item ? "true" : "false");
    } else if (const auto* item = std::get_if<std::int64_t>(&value.value)) {
        std::cout << "\"kind\":\"integer\",\"value\":" << *item;
    } else if (const auto* item = std::get_if<double>(&value.value)) {
        std::cout << "\"kind\":\"number\",\"value\":" << *item;
    } else if (const auto* item = std::get_if<std::string>(&value.value)) {
        std::cout << "\"kind\":\"string\",\"value\":";
        print_json_string(*item);
    } else {
        std::cout << "\"kind\":\"object\",\"value\":null";
    }
    std::cout << ",\"roundtrip\":";
    print_json_string(oof::platform::value_to_string_internal(value));
    std::cout << "}\n";
}

void print_localized(const Node& root) {
    if (!root.is_list || root.items.size() < 3 || root.items[0].is_list || root.items[1].is_list || !root.items[2].is_list) {
        throw std::runtime_error("localized record must have shape {version,count,{lang,text}}");
    }
    const std::string version = oof::platform::clean_atom(root.items[0].atom);
    const std::string count = oof::platform::clean_atom(root.items[1].atom);
    if (root.items[2].items.size() < 2 || root.items[2].items[0].is_list || root.items[2].items[1].is_list) {
        throw std::runtime_error("localized item must have shape {lang,text}");
    }
    const std::string lang = oof::platform::clean_atom(root.items[2].items[0].atom);
    const std::string text = oof::platform::clean_atom(root.items[2].items[1].atom);
    if (!oof::platform::is_localized_lang(lang)) {
        throw std::runtime_error("invalid localized string language: " + lang);
    }
    const oof::platform::LocalizedStringRecord record = oof::platform::localized_text_record(text, lang);
    std::cout << "{\"encoding\":\"LocalizedStringRecord\",\"version\":";
    print_json_string(version);
    std::cout << ",\"count\":";
    print_json_string(count);
    std::cout << ",\"lang\":";
    print_json_string(record.items.front().lang);
    std::cout << ",\"text\":";
    print_json_string(record.items.front().text.value);
    std::cout << ",\"roundtrip\":[";
    print_json_string(record.version);
    std::cout << ",";
    print_json_string(std::to_string(record.items.size()));
    std::cout << ",[";
    print_json_string(oof::platform::quote_atom(record.items.front().lang));
    std::cout << ",";
    print_json_string(oof::platform::quote_atom(record.items.front().text.value));
    std::cout << "]]}\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        usage();
        return 2;
    }

    try {
        const std::string command = argv[1];
        if (command == "mechanism") {
            print_mechanism();
            return 0;
        }
        if (command == "value") {
            print_value(trim_copy(read_stdin()));
            return 0;
        }

        Parser parser(tokenize(read_stdin()));
        const Node root = parser.parse_document();

        if (command == "compact") {
            std::cout << dump_compact(root) << "\n";
            return 0;
        }
        if (command == "listout") {
            std::cout << dump_listout(root) << "\n";
            return 0;
        }
        if (command == "stats") {
            Stats stats;
            collect_stats(root, 1, stats);
            std::cout << "{\"lists\":" << stats.lists << ",\"atoms\":" << stats.atoms
                      << ",\"maxDepth\":" << stats.max_depth << "}\n";
            return 0;
        }
        if (command == "type-domain") {
            print_type_domain(root);
            return 0;
        }
        if (command == "localized") {
            print_localized(root);
            return 0;
        }

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
