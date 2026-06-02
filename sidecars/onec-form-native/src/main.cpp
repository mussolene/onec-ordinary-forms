#include <cctype>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    std::cerr << "Usage: oof-native <compact|listout|stats> < stream.txt\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        usage();
        return 2;
    }

    try {
        const std::string command = argv[1];
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

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
