#include <array>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

#include "form_bin_container.hpp"
#include "ordinary_controls.hpp"
#include "ordinary_form_graph.hpp"
#include "platform_descriptor_registry.hpp"
#include "platform_form_descriptor_join.hpp"
#include "platform_form_schema.hpp"
#include "platform_guid_registry.hpp"
#include "platform_mechanism.hpp"
#include "platform_object_model.hpp"
#include "platform_object_schema.hpp"
#include "platform_property_registry.hpp"
#include "platform_runtime_binding.hpp"
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
    std::cerr << "Usage: oof-native <compact|listout|stats|mechanism|value-roundtrip|controls-codec|info8-codec|graph-codec|transfer-roundtrip|transfer-sections|formbin-selftest|formbin-package-selftest|formbin-platform-object-selftest|form-payload-structure-selftest|form-object-graph-selftest|form-transfer-linkage-selftest|raw-deflate-selftest> < stream.txt\n"
              << "       oof-native <formbin-info|formbin-roundtrip|form-payload-info|form-payload-structure|form-object-graph|form-transfer-linkage> Form.bin\n"
              << "       oof-native formbin-dump-package Form.bin Form.xml\n"
              << "       oof-native formbin-build-package base-Form.bin Form.xml rebuilt-Form.bin\n"
              << "       oof-native formbin-build-source-package Form.xml rebuilt-Form.bin\n"
              << "       oof-native formbin-xml-coverage Form.bin\n"
              << "       oof-native <formbin-platform-object|formbin-platform-object-get> Form.bin [objectId property]\n"
              << "       oof-native formbin-platform-object-set base-Form.bin rebuilt-Form.bin objectId property value\n"
              << "       oof-native runtime-form-dump-xml runtime-form-stream.txt Form.xml\n"
              << "       oof-native runtime-form-build-xml base-runtime-stream.txt Form.xml rebuilt-runtime-stream.txt\n"
              << "       oof-native <runtime-form-object-graph|runtime-form-roundtrip|runtime-platform-object> runtime-form-stream.txt\n"
              << "       oof-native runtime-form-semantic-diff left-runtime-stream.txt right-runtime-stream.txt\n"
              << "       oof-native runtime-form-rebuild runtime-form-stream.txt rebuilt-stream.txt\n"
              << "       oof-native runtime-form-rename runtime-form-stream.txt rebuilt-stream.txt objectId newName\n"
              << "       oof-native runtime-platform-object-get runtime-form-stream.txt objectId property\n"
              << "       oof-native runtime-platform-object-set runtime-form-stream.txt rebuilt-stream.txt objectId property value\n"
              << "       oof-native container-extract <1c-container> <out-dir>\n"
              << "       oof-native container-extract-inflate <1c-container> <out-dir>\n"
              << "       oof-native <platform-form-schema|platform-object-schema|platform-descriptor-join|platform-runtime-bindings|platform-property-registry>\n"
              << "       oof-native platform-guid-scan dsgnfrm.so\n"
              << "       oof-native platform-resource-descriptor-scan file.res [file.res ...]\n"
              << "       oof-native platform-xsd-inventory file.xsd [file.xsd ...]\n";
}

std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open input file: " + path);
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::string read_file_text_lossy(const std::string& path) {
    const auto bytes = read_file_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

std::string decode_text_file_bytes(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        offset = 3;
    }
    return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
}

void print_json_string(std::string_view value) {
    std::cout << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
            case '\\':
                std::cout << '\\' << static_cast<char>(ch);
                break;
            case '\b':
                std::cout << "\\b";
                break;
            case '\f':
                std::cout << "\\f";
                break;
            case '\n':
                std::cout << "\\n";
                break;
            case '\r':
                std::cout << "\\r";
                break;
            case '\t':
                std::cout << "\\t";
                break;
            default:
                if (ch < 0x20) {
                    std::cout << "\\u00" << hex[(ch >> 4) & 0x0F] << hex[ch & 0x0F];
                } else {
                    std::cout << static_cast<char>(ch);
                }
                break;
        }
    }
    std::cout << '"';
}

std::string ascii_lower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

void print_mechanism() {
    oof::platform::value::LocalWString sample_title;
    sample_title.add_item("ru", "Title");
    const oof::platform::value::FormattedString sample_formatted(sample_title, false);

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
    std::cout << "],\"platformValueSymbols\":[";
    for (size_t i = 0; i < oof::platform::value::localized_value_symbols.size(); ++i) {
        const auto& symbol = oof::platform::value::localized_value_symbols[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"name\":";
        print_json_string(symbol.name);
        std::cout << ",\"provider\":";
        print_json_string(symbol.provider);
        std::cout << ",\"address\":";
        print_json_string(symbol.address);
        std::cout << ",\"evidence\":";
        print_json_string(symbol.evidence);
        std::cout << "}";
    }
    std::cout << "],\"platformValueSurface\":[";
    for (size_t i = 0; i < oof::platform::value::value_surface.size(); ++i) {
        const auto& value = oof::platform::value::value_surface[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"type\":";
        print_json_string(value.type_name);
        std::cout << ",\"symbol\":";
        print_json_string(value.platform_symbol);
        std::cout << ",\"nativeRole\":";
        print_json_string(value.native_role);
        std::cout << "}";
    }
    std::cout << "],\"schemaValueSurface\":[";
    for (size_t i = 0; i < oof::platform::value::schema_value_surface.size(); ++i) {
        const auto& value = oof::platform::value::schema_value_surface[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"type\":";
        print_json_string(value.type_name);
        std::cout << ",\"schemaSource\":";
        print_json_string(value.schema_source);
        std::cout << ",\"platformEvidence\":";
        print_json_string(value.platform_evidence);
        std::cout << ",\"nativeRole\":";
        print_json_string(value.native_role);
        std::cout << ",\"layoutStatus\":";
        print_json_string(value.layout_status);
        std::cout << "}";
    }
    std::cout << "],\"localizedValueLayout\":{";
    std::cout << "\"localWStringVersion\":" << oof::platform::value::LocalWString::platform_version << ",";
    std::cout << "\"firstItemLanguageOffset\":" << oof::platform::value::LocalWString::first_item_language_offset << ",";
    std::cout << "\"firstItemTextOffset\":" << oof::platform::value::LocalWString::first_item_text_offset << ",";
    std::cout << "\"vectorOffset\":" << oof::platform::value::LocalWString::vector_offset << ",";
    std::cout << "\"vectorStride\":" << oof::platform::value::LocalWString::vector_stride << ",";
    std::cout << "\"vectorLanguageOffset\":" << oof::platform::value::LocalWString::vector_language_offset << ",";
    std::cout << "\"vectorTextOffset\":" << oof::platform::value::LocalWString::vector_text_offset << ",";
    std::cout << "\"formattedStringVersion\":" << oof::platform::value::FormattedString::platform_version << ",";
    std::cout << "\"formattedFlagOffset\":" << oof::platform::value::FormattedString::formatted_flag_offset << ",";
    std::cout << "\"sampleLocalWString\":";
    print_json_string(sample_title.serialize_list_stream());
    std::cout << ",\"sampleFormattedString\":";
    print_json_string(sample_formatted.serialize_list_stream());
    std::cout << "},\"ordinaryTransferRegistry\":[";
    for (size_t i = 0; i < oof::platform::ordinary::transfer_registry.size(); ++i) {
        const auto& descriptor = oof::platform::ordinary::transfer_registry[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"symbol\":";
        print_json_string(descriptor.symbol);
        std::cout << ",\"formatId\":" << descriptor.format_id;
        std::cout << ",\"recordSize\":" << descriptor.record_size;
        std::cout << ",\"countSemantics\":";
        print_json_string(descriptor.count_semantics);
        std::cout << ",\"boundary\":";
        print_json_string(descriptor.boundary);
        std::cout << ",\"platformRole\":";
        print_json_string(descriptor.platform_role);
        std::cout << ",\"nativeRole\":";
        print_json_string(descriptor.native_role);
        std::cout << "}";
    }
    std::cout << "],\"ordinaryTripletEntryPoints\":[";
    for (size_t i = 0; i < oof::platform::ordinary::triplet_entry_points.size(); ++i) {
        const auto& entry = oof::platform::ordinary::triplet_entry_points[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"address\":";
        print_json_string(entry.address);
        std::cout << ",\"role\":";
        print_json_string(entry.role);
        std::cout << "}";
    }
    std::cout << "]}\n";
}

std::string bytes_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const std::uint8_t byte : bytes) {
        out << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return out.str();
}

void print_value_roundtrip() {
    oof::platform::value::LocalWString title;
    title.add_item("ru", "Title");
    title.add_item("en", "Caption");
    const oof::platform::value::FormattedString formatted(title, true);

    oof::platform::stream::ListOutStream out;
    formatted.serialize(out);
    const std::string serialized = out.text();

    oof::platform::stream::ListInStream in(serialized);
    const oof::platform::value::FormattedString restored =
        oof::platform::value::FormattedString::deserialize(in);

    oof::platform::value::CompositeID composite;
    composite.object_id = 42;
    composite.guid = "09ccdc77-ea1a-4a6d-ab1c-3435eada2433";
    composite.null = false;
    const std::string composite_serialized = composite.serialize_list_stream();
    oof::platform::stream::ListInStream composite_stream(composite_serialized);
    const oof::platform::value::CompositeID restored_composite =
        oof::platform::value::CompositeID::deserialize(composite_stream);

    oof::platform::value::TypeDomainPattern type_domain;
    type_domain.add_type(std::string(oof::platform::value::TypeDomainPattern::type_term_guid));
    const std::string type_domain_serialized = type_domain.serialize_list_stream();
    oof::platform::stream::ListInStream type_domain_stream(type_domain_serialized);
    const oof::platform::value::TypeDomainPattern restored_type_domain =
        oof::platform::value::TypeDomainPattern::deserialize(type_domain_stream);

    oof::platform::value::GenericValue generic{"String", "Value"};
    const std::string generic_serialized = generic.serialize_list_stream();
    oof::platform::stream::ListInStream generic_stream(generic_serialized);
    const oof::platform::value::GenericValue restored_generic =
        oof::platform::value::GenericValue::deserialize(generic_stream);

    const oof::platform::value::Color absolute_color =
        oof::platform::value::Color::absolute_rgb(0x12, 0x34, 0x56);
    const std::string absolute_color_serialized = absolute_color.serialize_list_stream();
    oof::platform::stream::ListInStream color_stream(absolute_color_serialized);
    const oof::platform::value::Color restored_color =
        oof::platform::value::Color::deserialize(color_stream);

    oof::platform::value::Font font;
    font.kind = oof::platform::value::FontKind::absolute;
    font.mask = 0x0f;
    font.face_name = "Arial";
    font.height = 10.0;
    font.bold = true;
    const std::string font_serialized = font.serialize_list_stream();
    oof::platform::stream::ListInStream font_stream(font_serialized);
    const oof::platform::value::Font restored_font =
        oof::platform::value::Font::deserialize(font_stream);

    oof::platform::value::V8Border border;
    border.style = oof::platform::value::BorderType::single;
    border.width = 1;
    border.color = oof::platform::value::Color::auto_color();
    const std::string border_serialized = border.serialize_list_stream();
    oof::platform::stream::ListInStream border_stream(border_serialized);
    const oof::platform::value::V8Border restored_border =
        oof::platform::value::V8Border::deserialize(border_stream);

    oof::platform::value::V8Picture picture;
    picture.ref.ref = oof::platform::value::AbstractRef::named("ui:Picture");
    picture.storage_id = "storage";
    const std::string picture_serialized = picture.serialize_list_stream();
    oof::platform::stream::ListInStream picture_stream(picture_serialized);
    const oof::platform::value::V8Picture restored_picture =
        oof::platform::value::V8Picture::deserialize(picture_stream);

    std::cout << "{";
    std::cout << "\"serialized\":";
    print_json_string(serialized);
    std::cout << ",\"formatted\":" << (restored.formatted() ? "true" : "false");
    std::cout << ",\"itemCount\":" << restored.value().items().size();
    std::cout << ",\"firstLanguage\":";
    print_json_string(restored.value().items().at(0).language);
    std::cout << ",\"secondText\":";
    print_json_string(restored.value().items().at(1).text);
    std::cout << ",\"compositeID\":";
    print_json_string(composite_serialized);
    std::cout << ",\"compositeObjectId\":" << restored_composite.object_id;
    std::cout << ",\"compositeGuid\":";
    print_json_string(restored_composite.guid);
    std::cout << ",\"typeDomainPattern\":";
    print_json_string(type_domain_serialized);
    std::cout << ",\"typeDomainEntries\":" << restored_type_domain.entries.size();
    std::cout << ",\"genericValue\":";
    print_json_string(generic_serialized);
    std::cout << ",\"genericType\":";
    print_json_string(restored_generic.type_name);
    std::cout << ",\"color\":";
    print_json_string(absolute_color_serialized);
    std::cout << ",\"colorSchema\":";
    print_json_string(restored_color.schema_value());
    std::cout << ",\"font\":";
    print_json_string(font_serialized);
    std::cout << ",\"fontFace\":";
    print_json_string(restored_font.face_name);
    std::cout << ",\"border\":";
    print_json_string(border_serialized);
    std::cout << ",\"borderStyle\":";
    print_json_string(oof::platform::value::border_type_name(restored_border.style));
    std::cout << ",\"picture\":";
    print_json_string(picture_serialized);
    std::cout << ",\"pictureRef\":";
    print_json_string(restored_picture.ref.ref.schema_value());
    std::cout << "}\n";
}

void print_controls_codec() {
    oof::platform::ordinary::DiagnosticControlPayloadChunk controls;
    controls.words[0] = oof::platform::cf_form_controls8;
    controls.words[1] = 1;
    controls.words[9] = 9;
    const auto controls_bytes = controls.serialize();
    const auto restored_controls = oof::platform::ordinary::DiagnosticControlPayloadChunk::deserialize(controls_bytes);

    oof::platform::ordinary::ControlPositionRecord position;
    position.words[0] = oof::platform::cf_form_controls_position8;
    position.words[7] = 7;
    const auto position_bytes = position.serialize();
    const auto restored_position = oof::platform::ordinary::ControlPositionRecord::deserialize(position_bytes);

    oof::platform::ordinary::ControlInfoRecord info;
    info.words[0] = oof::platform::cf_form_controls_info8;
    info.words[3] = 3;
    const auto info_bytes = info.serialize();
    const auto restored_info = oof::platform::ordinary::ControlInfoRecord::deserialize(info_bytes);

    oof::platform::ordinary::FormFormatEnumerator enumerator;
    enumerator.add(oof::platform::ordinary::TransferFacet::position, 1);
    enumerator.add(oof::platform::ordinary::TransferFacet::controls, static_cast<std::uint32_t>(controls_bytes.size()));
    enumerator.add(oof::platform::ordinary::TransferFacet::info, 1);

    oof::platform::ordinary::OrdinaryTransferSet transfer_set;
    transfer_set.controls.bytes = controls_bytes;
    transfer_set.positions.push_back(position);
    transfer_set.infos.push_back(info);

    std::cout << "{";
    std::cout << "\"diagnosticControlsFixtureSize\":" << controls_bytes.size();
    std::cout << ",\"positionSize\":" << position_bytes.size();
    std::cout << ",\"infoSize\":" << info_bytes.size();
    std::cout << ",\"diagnosticControlsFirstWord\":" << restored_controls.words[0];
    std::cout << ",\"positionLastWord\":" << restored_position.words[7];
    std::cout << ",\"infoLastWord\":" << restored_info.words[3];
    std::cout << ",\"enumeratorHex\":";
    print_json_string(bytes_hex(enumerator.serialize_headers()));
    std::cout << ",\"transferSetSize\":" << transfer_set.serialize_records().size();
    std::cout << "}\n";
}

void print_info8_codec() {
    oof::platform::ordinary::ControlInfoRecord info;
    info.words[0] = 0x9d00;
    info.words[1] = 0x00000005;
    info.words[2] = 0x0000000e;
    info.words[3] = 0x00000014;

    const auto bytes = info.serialize();
    const auto restored = oof::platform::ordinary::ControlInfoRecord::deserialize(bytes);

    std::cout << "{\"symbol\":\"cf_form_controls_info8\"";
    std::cout << ",\"formatId\":" << oof::platform::cf_form_controls_info8;
    std::cout << ",\"recordSize\":" << oof::platform::info_transfer_record_size;
    std::cout << ",\"layoutStatus\":";
    print_json_string(oof::platform::ordinary::ControlInfoRecord::layout_status());
    std::cout << ",\"semanticsStatus\":";
    print_json_string(oof::platform::ordinary::ControlInfoRecord::semantics_status());
    std::cout << ",\"platformEvidence\":";
    print_json_string("dsgnfrm FUN_00270da0 copies linked-list node payload as two uint64 words; exported HGLOBAL is uint32 count + N * 16-byte records");
    std::cout << ",\"words\":[";
    for (std::size_t index = 0; index < restored.words.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        std::cout << restored.words[index];
    }
    std::cout << "],\"low64\":" << restored.low64();
    std::cout << ",\"high64\":" << restored.high64();
    std::cout << ",\"bytesHex\":";
    print_json_string(bytes_hex(bytes));
    std::cout << ",\"objectBindingStatus\":";
    print_json_string("pending binary/runtime correlation between record words and PlatformObjectSchemaMember platformMember values");
    std::cout << "}\n";
}

void print_graph_codec() {
    const auto graph = oof::platform::ordinary::make_single_control_graph(5, "Button1", "Button title");
    const auto& control = graph.controls().at(0);
    const auto transfer_bytes = graph.serialize_transfer_records();

    std::cout << "{";
    std::cout << "\"controlCount\":" << graph.controls().size();
    std::cout << ",\"objectId\":" << control.identity.object_id;
    std::cout << ",\"name\":";
    print_json_string(control.identity.name);
    std::cout << ",\"title\":";
    print_json_string(control.presentation.title.serialize_list_stream());
    std::cout << ",\"diagnosticPayloadFormat\":" << control.payload_fixture.words[0];
    std::cout << ",\"positionFormat\":" << control.position.words[0];
    std::cout << ",\"infoFormat\":" << control.info.words[0];
    std::cout << ",\"transferSetSize\":" << transfer_bytes.size();
    std::cout << ",\"transferSetHexPrefix\":";
    print_json_string(bytes_hex(std::vector<std::uint8_t>(transfer_bytes.begin(), transfer_bytes.begin() + 16)));
    std::cout << "}\n";
}

void print_transfer_roundtrip() {
    const auto graph = oof::platform::ordinary::make_single_control_graph(7, "Input1", "Input title");
    const auto transfer_bytes = graph.serialize_transfer_records();
    const auto transfer_set = oof::platform::ordinary::OrdinaryTransferSet::deserialize_records(transfer_bytes);

    std::cout << "{";
    std::cout << "\"bytes\":" << transfer_bytes.size();
    std::cout << ",\"controlsBytes\":" << transfer_set.controls.bytes.size();
    std::cout << ",\"positions\":" << transfer_set.positions.size();
    std::cout << ",\"infos\":" << transfer_set.infos.size();
    const auto control_fixture = oof::platform::ordinary::DiagnosticControlPayloadChunk::deserialize(transfer_set.controls.bytes);
    std::cout << ",\"diagnosticControlObjectId\":" << control_fixture.words[1];
    std::cout << ",\"positionObjectId\":" << transfer_set.positions.at(0).words[1];
    std::cout << ",\"infoObjectId\":" << transfer_set.infos.at(0).words[1];
    std::cout << "}\n";
}

void print_transfer_sections() {
    const auto graph = oof::platform::ordinary::make_single_control_graph(7, "Input1", "Input title");
    const auto transfer_bytes = graph.serialize_transfer_records();
    const auto sections = oof::platform::ordinary::inspect_transfer_sections(transfer_bytes);

    std::cout << "{";
    std::cout << "\"bytes\":" << transfer_bytes.size();
    std::cout << ",\"sections\":[";
    for (std::size_t index = 0; index < sections.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& section = sections[index];
        std::cout << "{";
        std::cout << "\"facet\":";
        print_json_string(oof::platform::ordinary::facet_name(section.facet));
        std::cout << ",\"formatId\":" << section.format_id;
        std::cout << ",\"recordSize\":" << section.record_size;
        std::cout << ",\"countOrBytes\":" << section.count_or_bytes;
        std::cout << ",\"payloadOffset\":" << section.payload_offset;
        std::cout << ",\"payloadSize\":" << section.payload_size;
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void print_formbin_info(const std::string& path) {
    const std::vector<std::uint8_t> data = read_file_bytes(path);
    const auto container = oof::platform::formbin::parse_container(data);

    std::cout << "{";
    std::cout << "\"containerSize\":" << data.size();
    std::cout << ",\"blockSize\":" << container.block_size;
    std::cout << ",\"fileCount\":" << container.files.size();
    std::cout << ",\"files\":[";
    for (size_t i = 0; i < container.files.size(); ++i) {
        const auto& file = container.files[i];
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"name\":";
        print_json_string(file.name);
        std::cout << ",\"payloadSize\":" << file.payload.size();
        std::cout << ",\"payloadHexPrefix\":";
        const auto prefix_end = file.payload.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(16, file.payload.size()));
        print_json_string(bytes_hex(std::vector<std::uint8_t>(file.payload.begin(), prefix_end)));
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void print_formbin_roundtrip(const std::string& path) {
    const std::vector<std::uint8_t> data = read_file_bytes(path);
    const auto container = oof::platform::formbin::parse_container(data);
    const std::vector<std::uint8_t> rebuilt = oof::platform::formbin::serialize_container(container);
    const auto reparsed = oof::platform::formbin::parse_container(rebuilt);

    bool logical_equal = container.files.size() == reparsed.files.size();
    for (size_t i = 0; logical_equal && i < container.files.size(); ++i) {
        logical_equal = container.files[i].name == reparsed.files[i].name &&
                        container.files[i].created == reparsed.files[i].created &&
                        container.files[i].modified == reparsed.files[i].modified &&
                        container.files[i].payload == reparsed.files[i].payload;
    }

    std::cout << "{";
    std::cout << "\"inputSize\":" << data.size();
    std::cout << ",\"rebuiltSize\":" << rebuilt.size();
    std::cout << ",\"byteEqual\":" << (data == rebuilt ? "true" : "false");
    std::cout << ",\"logicalEqual\":" << (logical_equal ? "true" : "false");
    std::cout << ",\"fileCount\":" << reparsed.files.size();
    std::cout << "}\n";
}

std::string safe_container_file_name(const std::string& name) {
    std::string safe;
    safe.reserve(name.size());
    for (const char ch : name) {
        const bool allowed = std::isalnum(static_cast<unsigned char>(ch)) ||
                             ch == '-' || ch == '_' || ch == '.';
        safe.push_back(allowed ? ch : '_');
    }
    if (safe.empty() || safe == "." || safe == "..") {
        return "unnamed";
    }
    return safe;
}

void write_file_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open output file: " + path.string());
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) {
        throw std::runtime_error("cannot write output file: " + path.string());
    }
}

std::vector<std::uint8_t> inflate_raw_deflate(const std::vector<std::uint8_t>& data) {
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    const int init_code = inflateInit2(&stream, -MAX_WBITS);
    if (init_code != Z_OK) {
        throw std::runtime_error("cannot initialize raw deflate inflater");
    }

    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, 16384> buffer{};
    int code = Z_OK;
    while (code == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        code = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = buffer.size() - stream.avail_out;
        output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    }
    inflateEnd(&stream);
    if (code != Z_STREAM_END) {
        throw std::runtime_error("raw deflate payload did not inflate cleanly");
    }
    return output;
}

void extract_container_files(const std::string& input_path, const std::string& output_dir, bool inflate_payloads) {
    const std::vector<std::uint8_t> data = read_file_bytes(input_path);
    const auto container = oof::platform::formbin::parse_container(data);
    const std::filesystem::path out(output_dir);
    std::filesystem::create_directories(out);

    std::cout << "{";
    std::cout << "\"containerSize\":" << data.size();
    std::cout << ",\"blockSize\":" << container.block_size;
    std::cout << ",\"fileCount\":" << container.files.size();
    std::cout << ",\"files\":[";
    for (size_t i = 0; i < container.files.size(); ++i) {
        const auto& file = container.files[i];
        const std::string safe_name = safe_container_file_name(file.name);
        const std::filesystem::path file_path = out / safe_name;
        const std::vector<std::uint8_t> payload =
            inflate_payloads ? inflate_raw_deflate(file.payload) : file.payload;
        write_file_bytes(file_path, payload);

        if (i != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"name\":";
        print_json_string(file.name);
        std::cout << ",\"path\":";
        print_json_string(file_path.string());
        std::cout << ",\"payloadSize\":" << payload.size();
        if (inflate_payloads) {
            std::cout << ",\"compressedPayloadSize\":" << file.payload.size();
            std::cout << ",\"inflated\":true";
        }
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void print_raw_deflate_selftest() {
    const std::vector<std::uint8_t> compressed{0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00};
    const auto inflated = inflate_raw_deflate(compressed);
    const std::string text(inflated.begin(), inflated.end());
    std::cout << "{";
    std::cout << "\"inflated\":";
    print_json_string(text);
    std::cout << ",\"byteEqual\":" << (text == "hello" ? "true" : "false");
    std::cout << "}\n";
}

bool is_hex_digit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool is_guid_text(std::string_view value) {
    constexpr std::array<std::size_t, 4> dashes{{8, 13, 18, 23}};
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        bool dash = false;
        for (const std::size_t dash_index : dashes) {
            dash = dash || index == dash_index;
        }
        if (dash) {
            if (value[index] != '-') {
                return false;
            }
        } else if (!is_hex_digit(value[index])) {
            return false;
        }
    }
    return true;
}

const oof::platform::formbin::OneCContainerFile& find_container_file(
    const oof::platform::formbin::OneCContainer& container,
    std::string_view name
) {
    for (const auto& file : container.files) {
        if (file.name == name) {
            return file;
        }
    }
    throw std::runtime_error("Form.bin does not contain required logical file");
}

oof::platform::formbin::OneCContainerFile& find_container_file_mut(
    oof::platform::formbin::OneCContainer& container,
    std::string_view name
) {
    for (auto& file : container.files) {
        if (file.name == name) {
            return file;
        }
    }
    throw std::runtime_error("Form.bin does not contain required logical file");
}

std::string decode_form_payload_text(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
        offset = 3;
    }
    return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
}

struct PayloadScanStats {
    std::size_t lists = 0;
    std::size_t atoms = 0;
    std::size_t guid_head_nodes = 0;
};

void collect_payload_scan(const oof::platform::stream::ListValue& value, PayloadScanStats& stats) {
    if (!value.is_list) {
        ++stats.atoms;
        return;
    }
    ++stats.lists;
    if (!value.items.empty() && !value.items[0].is_list && is_guid_text(value.items[0].atom)) {
        ++stats.guid_head_nodes;
    }
    for (const auto& item : value.items) {
        collect_payload_scan(item, stats);
    }
}

void print_guid_head_nodes(const oof::platform::stream::ListValue& value, std::size_t& printed) {
    if (!value.is_list) {
        return;
    }
    if (!value.items.empty() && !value.items[0].is_list && is_guid_text(value.items[0].atom)) {
        if (printed != 0) {
            std::cout << ",";
        }
        std::cout << "{";
        std::cout << "\"guid\":";
        print_json_string(value.items[0].atom);
        std::cout << ",\"arity\":" << value.items.size();
        if (value.items.size() > 1 && !value.items[1].is_list) {
            std::cout << ",\"slot1\":";
            print_json_string(value.items[1].atom);
        }
        std::cout << "}";
        ++printed;
    }
    for (const auto& item : value.items) {
        print_guid_head_nodes(item, printed);
    }
}

void print_form_payload_info(const std::string& path) {
    const std::vector<std::uint8_t> data = read_file_bytes(path);
    const auto container = oof::platform::formbin::parse_container(data);
    const auto& form_file = find_container_file(container, "form");
    const std::string text = decode_form_payload_text(form_file.payload);
    const auto root = oof::platform::stream::parse(text);
    if (!root.is_list) {
        throw std::runtime_error("form payload root is not a list");
    }

    PayloadScanStats stats;
    collect_payload_scan(root, stats);

    std::cout << "{";
    std::cout << "\"payloadSize\":" << form_file.payload.size();
    std::cout << ",\"rootArity\":" << root.items.size();
    if (!root.items.empty() && !root.items[0].is_list) {
        std::cout << ",\"rootVersion\":";
        print_json_string(root.items[0].atom);
    }
    if (root.items.size() > 1 && root.items[1].is_list && !root.items[1].items.empty() && !root.items[1].items[0].is_list) {
        std::cout << ",\"formSectionVersion\":";
        print_json_string(root.items[1].items[0].atom);
    }
    std::cout << ",\"lists\":" << stats.lists;
    std::cout << ",\"atoms\":" << stats.atoms;
    std::cout << ",\"guidHeadNodes\":" << stats.guid_head_nodes;
    std::cout << ",\"guidNodes\":[";
    std::size_t printed = 0;
    print_guid_head_nodes(root, printed);
    std::cout << "]}\n";
}

bool is_known_descriptor_pool_guid(std::string_view guid) {
    return oof::platform::form_descriptor::is_bound_descriptor_guid(guid);
}

std::string child_path(std::string_view path, std::size_t index) {
    std::string out(path);
    out += "/";
    out += std::to_string(index);
    return out;
}

struct GuidNodeInfo {
    std::string guid;
    std::string path;
    std::size_t arity = 0;
    std::string slot1;
    std::size_t scalar_children = 0;
    std::size_t list_children = 0;
    bool known_descriptor_pool_member = false;
    const oof::platform::form_descriptor::DescriptorSchemaBinding* descriptor_binding = nullptr;
};

struct FormatAtomInfo {
    std::uint32_t format_id = 0;
    std::string symbol;
    std::string path;
};

struct GeometryBindingRecord {
    std::string name;
    oof::platform::stream::ListValue value;
};

std::string first_base64_picture_payload(const oof::platform::stream::ListValue& value) {
    if (!value.is_list) {
        if (value.atom.rfind("#base64:", 0) == 0) {
            return value.atom;
        }
        return {};
    }
    for (std::size_t index = 0; index < value.items.size(); ++index) {
        const auto& item = value.items[index];
        if (!item.is_list && item.atom.rfind("#base64:", 0) == 0) {
            std::string payload = item.atom;
            for (std::size_t chunk_index = index + 1; chunk_index < value.items.size(); ++chunk_index) {
                const auto& chunk = value.items[chunk_index];
                if (chunk.is_list) {
                    break;
                }
                payload += chunk.atom;
            }
            return payload;
        }
        const std::string found = first_base64_picture_payload(item);
        if (!found.empty()) {
            return found;
        }
    }
    return {};
}

struct MaterializedFormEvent {
    std::string object_id;
    std::string owner_object_id;
    std::string id;
    std::string handler;
    std::string path;
};

struct MaterializedFormItem {
    std::string guid;
    std::string path;
    std::string object_id;
    std::string parent_object_id;
    std::string name;
    std::string title;
    std::string visible;
    std::string enabled;
    std::string left;
    std::string top;
    std::string right;
    std::string bottom;
    std::string picture_payload;
    std::vector<GeometryBindingRecord> bindings;
    std::vector<GeometryBindingRecord> dimension_bindings;
    std::vector<MaterializedFormEvent> events;
    std::size_t arity = 0;
    const oof::platform::form_descriptor::DescriptorSchemaBinding* descriptor_binding = nullptr;
};

struct MaterializedFormAttribute {
    std::string object_id;
    std::string id;
    std::string name;
    std::string main;
    std::string stored_data;
    std::string type_pattern;
    std::string path;
};

struct MaterializedFormCommand {
    std::string object_id;
    std::string id;
    std::string name;
    std::string handler;
    std::string modifies_data;
    std::string path;
};

void collect_form_payload_structure(
    const oof::platform::stream::ListValue& value,
    std::string_view path,
    std::vector<GuidNodeInfo>& guid_nodes,
    std::vector<FormatAtomInfo>& format_atoms
) {
    if (!value.is_list) {
        if (value.atom == std::to_string(oof::platform::cf_form_controls8)) {
            format_atoms.push_back({oof::platform::cf_form_controls8, "cf_form_controls8", std::string(path)});
        } else if (value.atom == std::to_string(oof::platform::cf_form_controls_position8)) {
            format_atoms.push_back({oof::platform::cf_form_controls_position8, "cf_form_controls_position8", std::string(path)});
        } else if (value.atom == std::to_string(oof::platform::cf_form_controls_info8)) {
            format_atoms.push_back({oof::platform::cf_form_controls_info8, "cf_form_controls_info8", std::string(path)});
        }
        return;
    }

    if (!value.items.empty() && !value.items[0].is_list && is_guid_text(value.items[0].atom)) {
        GuidNodeInfo info;
        info.guid = value.items[0].atom;
        info.path = std::string(path);
        info.arity = value.items.size();
        info.descriptor_binding = oof::platform::form_descriptor::binding_for_guid(info.guid);
        info.known_descriptor_pool_member = info.descriptor_binding != nullptr;
        if (value.items.size() > 1 && !value.items[1].is_list) {
            info.slot1 = value.items[1].atom;
        }
        for (const auto& child : value.items) {
            if (child.is_list) {
                ++info.list_children;
            } else {
                ++info.scalar_children;
            }
        }
        guid_nodes.push_back(std::move(info));
    }

    for (std::size_t index = 0; index < value.items.size(); ++index) {
        collect_form_payload_structure(value.items[index], child_path(path, index), guid_nodes, format_atoms);
    }
}

bool is_platform_name_record(const oof::platform::stream::ListValue& value) {
    return value.is_list &&
           value.items.size() >= 2 &&
           !value.items[0].is_list &&
           !value.items[1].is_list &&
           value.items[0].atom == "14" &&
           !value.items[1].atom.empty();
}

bool find_platform_name_record(
    const oof::platform::stream::ListValue& value,
    std::string& name
) {
    if (!value.is_list) {
        return false;
    }
    if (is_platform_name_record(value)) {
        name = value.items[1].atom;
        return true;
    }
    for (const auto& item : value.items) {
        if (find_platform_name_record(item, name)) {
            return true;
        }
    }
    return false;
}

bool find_first_localized_text(
    const oof::platform::stream::ListValue& value,
    std::string& text
) {
    if (!value.is_list) {
        return false;
    }
    if (value.items.size() >= 2 &&
        !value.items[0].is_list &&
        !value.items[1].is_list &&
        value.items[0].atom_kind == oof::platform::stream::ListValue::AtomKind::string &&
        value.items[1].atom_kind == oof::platform::stream::ListValue::AtomKind::string) {
        text = value.items[1].atom;
        return true;
    }
    for (const auto& item : value.items) {
        if (find_first_localized_text(item, text)) {
            return true;
        }
    }
    return false;
}

bool is_int_atom(const oof::platform::stream::ListValue& value) {
    if (value.is_list || value.atom.empty()) {
        return false;
    }
    std::size_t index = 0;
    if (value.atom[0] == '-') {
        index = 1;
    }
    if (index == value.atom.size()) {
        return false;
    }
    for (; index < value.atom.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(value.atom[index]))) {
            return false;
        }
    }
    return true;
}

bool is_geometry_record(const oof::platform::stream::ListValue& value) {
    return value.is_list &&
           value.items.size() >= 5 &&
           is_int_atom(value.items[1]) &&
           is_int_atom(value.items[2]) &&
           is_int_atom(value.items[3]) &&
           is_int_atom(value.items[4]);
}

const oof::platform::stream::ListValue* find_immediate_geometry_record(
    const oof::platform::stream::ListValue& value
) {
    if (!value.is_list) {
        return nullptr;
    }
    for (const auto& item : value.items) {
        if (is_geometry_record(item)) {
            return &item;
        }
    }
    return nullptr;
}

bool is_base_info_record(const oof::platform::stream::ListValue& value) {
    return value.is_list &&
           value.items.size() >= 13 &&
           !value.items[0].is_list &&
           (value.items[0].atom == "10" ||
            value.items[0].atom == "16" ||
            value.items[0].atom == "19");
}

const oof::platform::stream::ListValue* find_base_info_record(
    const oof::platform::stream::ListValue& value
) {
    if (!value.is_list) {
        return nullptr;
    }
    if (is_base_info_record(value)) {
        return &value;
    }
    for (const auto& item : value.items) {
        if (const auto* found = find_base_info_record(item)) {
            return found;
        }
    }
    return nullptr;
}

std::string platform_bool_text(std::string_view atom) {
    return atom == "1" ? "true" : "false";
}

std::string platform_bool_atom(std::string_view value) {
    if (value == "true" || value == "True" || value == "TRUE" || value == "1") {
        return "1";
    }
    if (value == "false" || value == "False" || value == "FALSE" || value == "0") {
        return "0";
    }
    throw std::runtime_error("expected platform boolean value true/false/1/0: " + std::string(value));
}

std::string binding_coordinate_name(std::size_t slot_index) {
    switch (slot_index) {
        case 6:
            return "top";
        case 7:
            return "bottom";
        case 8:
            return "left";
        case 9:
            return "right";
        case 10:
            return "verticalCenter";
        case 11:
            return "horizontalCenter";
        default:
            return {};
    }
}

std::size_t binding_coordinate_slot(std::string_view coordinate) {
    if (coordinate == "top") {
        return 6;
    }
    if (coordinate == "bottom") {
        return 7;
    }
    if (coordinate == "left") {
        return 8;
    }
    if (coordinate == "right") {
        return 9;
    }
    if (coordinate == "verticalCenter") {
        return 10;
    }
    if (coordinate == "horizontalCenter") {
        return 11;
    }
    return 0;
}

std::string dimension_binding_name(std::size_t slot_index) {
    switch (slot_index) {
        case 13:
            return "height";
        case 14:
            return "minHeight";
        case 15:
            return "stretch";
        case 16:
            return "width";
        default:
            return {};
    }
}

std::size_t dimension_binding_slot(std::string_view dimension) {
    if (dimension == "height") {
        return 13;
    }
    if (dimension == "minHeight") {
        return 14;
    }
    if (dimension == "stretch") {
        return 15;
    }
    if (dimension == "width") {
        return 16;
    }
    return 0;
}

std::string anchor_relation_name(std::string_view code) {
    if (code == "0") {
        return "none";
    }
    if (code == "1") {
        return "absolute";
    }
    if (code == "2") {
        return "targetEdgeOffset";
    }
    if (code == "3") {
        return "targetCenterOffset";
    }
    if (code == "4") {
        return "expression";
    }
    if (code == "5") {
        return "relative";
    }
    if (code == "6") {
        return "group";
    }
    return {};
}

std::string anchor_relation_code(std::string_view name) {
    if (name == "none") {
        return "0";
    }
    if (name == "absolute") {
        return "1";
    }
    if (name == "targetEdgeOffset" || name.empty()) {
        return "2";
    }
    if (name == "targetCenterOffset") {
        return "3";
    }
    if (name == "expression") {
        return "4";
    }
    if (name == "relative") {
        return "5";
    }
    if (name == "group") {
        return "6";
    }
    throw std::runtime_error("unsupported Binding anchor relation: " + std::string(name));
}

std::string anchor_side_name(std::string_view code) {
    if (code == "-1") {
        return "unknown";
    }
    if (code == "0") {
        return "top";
    }
    if (code == "1") {
        return "bottom";
    }
    if (code == "2") {
        return "left";
    }
    if (code == "3") {
        return "right";
    }
    if (code == "4") {
        return "width";
    }
    if (code == "5") {
        return "height";
    }
    if (code == "6") {
        return "none";
    }
    return {};
}

std::string anchor_side_code(std::string_view name) {
    if (name == "unknown") {
        return "-1";
    }
    if (name == "top") {
        return "0";
    }
    if (name == "bottom") {
        return "1";
    }
    if (name == "left") {
        return "2";
    }
    if (name == "right") {
        return "3";
    }
    if (name == "width") {
        return "4";
    }
    if (name == "height") {
        return "5";
    }
    if (name == "none" || name.empty()) {
        return "6";
    }
    throw std::runtime_error("unsupported Binding anchor side: " + std::string(name));
}

std::string anchor_target_name(std::string_view target_id) {
    if (target_id == "-1") {
        return "none";
    }
    if (target_id == "0") {
        return "parent";
    }
    return "element";
}

bool is_simple_platform_anchor(const oof::platform::stream::ListValue& value) {
    return value.is_list &&
           value.items.size() >= 4 &&
           !value.items[0].is_list &&
           !value.items[1].is_list &&
           !value.items[2].is_list &&
           !value.items[3].is_list &&
           !anchor_relation_name(value.items[0].atom).empty() &&
           !anchor_side_name(value.items[2].atom).empty();
}

bool is_public_form_control_binding_value(const oof::platform::stream::ListValue& value) {
    if (!value.is_list) {
        return true;
    }
    if (value.items.empty() || value.items[0].is_list) {
        return false;
    }
    for (std::size_t index = 1; index < value.items.size(); ++index) {
        if (!is_simple_platform_anchor(value.items[index])) {
            return false;
        }
    }
    return true;
}

bool is_public_form_control_dimension_binding_value(const oof::platform::stream::ListValue& value) {
    if (!value.is_list) {
        return true;
    }
    if (value.items.size() < 3 ||
        value.items[0].is_list ||
        value.items[1].is_list ||
        value.items[2].is_list ||
        anchor_side_name(value.items[2].atom).empty()) {
        return false;
    }
    for (std::size_t index = 3; index < value.items.size(); ++index) {
        if (!is_simple_platform_anchor(value.items[index])) {
            return false;
        }
    }
    return true;
}

const oof::platform::object_model::PlatformObject* find_object_by_id(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view object_id
) {
    if (object_id == "0") {
        return &form_object.form;
    }
    for (const auto& object : form_object.items.objects()) {
        if (object.object_id == object_id) {
            return &object;
        }
    }
    return nullptr;
}

bool is_materializable_object_candidate(const oof::platform::stream::ListValue& value) {
    if (!value.is_list || value.items.size() < 2 || value.items[0].is_list || value.items[1].is_list) {
        return false;
    }
    if (!is_guid_text(value.items[0].atom) || value.items[1].atom.empty()) {
        return false;
    }
    if (value.items.size() != 6 && value.items.size() != 7) {
        return false;
    }
    return oof::platform::form_descriptor::binding_for_guid(value.items[0].atom) != nullptr;
}

bool looks_like_materialized_form_event_record(const oof::platform::stream::ListValue& value) {
    if (!value.is_list || value.items.size() < 3 || value.items[0].is_list) {
        return false;
    }
    return value.items[0].atom == "event" ||
           value.items[0].atom == "Event" ||
           value.items[0].atom == "evt";
}

std::vector<MaterializedFormEvent> collect_materialized_form_events(
    const oof::platform::stream::ListValue& value,
    std::string_view owner_object_id,
    std::string_view path
) {
    std::vector<MaterializedFormEvent> events;
    if (!value.is_list) {
        return events;
    }
    if (looks_like_materialized_form_event_record(value)) {
        MaterializedFormEvent event;
        event.owner_object_id = std::string(owner_object_id);
        event.id = oof::platform::stream::dump_compact(value.items[1]);
        event.handler = !value.items[2].is_list ? value.items[2].atom : "";
        event.object_id = "event:" + std::string(owner_object_id) + ":" + event.id;
        event.path = std::string(path);
        if (!event.id.empty() && !event.handler.empty()) {
            events.push_back(std::move(event));
        }
        return events;
    }
    for (std::size_t index = 0; index < value.items.size(); ++index) {
        auto nested = collect_materialized_form_events(
            value.items[index],
            owner_object_id,
            child_path(path, index));
        events.insert(events.end(), std::make_move_iterator(nested.begin()), std::make_move_iterator(nested.end()));
    }
    return events;
}

void collect_materialized_form_items(
    const oof::platform::stream::ListValue& value,
    std::string_view path,
    const std::string& parent_object_id,
    std::vector<MaterializedFormItem>& items,
    std::size_t& guid_head_nodes,
    std::size_t& nested_unbound_guid_nodes
) {
    if (!value.is_list) {
        return;
    }

    std::string next_parent = parent_object_id;
    if (!value.items.empty() && !value.items[0].is_list && is_guid_text(value.items[0].atom)) {
        ++guid_head_nodes;
        const auto* binding = oof::platform::form_descriptor::binding_for_guid(value.items[0].atom);
        if (is_materializable_object_candidate(value)) {
            MaterializedFormItem item;
            item.guid = value.items[0].atom;
            item.path = std::string(path);
            item.object_id = value.items[1].atom;
            item.parent_object_id = parent_object_id;
            item.arity = value.items.size();
            item.descriptor_binding = binding;
            std::string name;
            if (find_platform_name_record(value, name)) {
                item.name = std::move(name);
            }
            if (value.items.size() > 2 && value.items[2].is_list) {
                std::string title;
                if (find_first_localized_text(value.items[2], title)) {
                    item.title = std::move(title);
                }
            }
            if (const auto* base_info = find_base_info_record(value)) {
                if (base_info->items.size() > 1 && !base_info->items[1].is_list) {
                    item.visible = platform_bool_text(base_info->items[1].atom);
                }
                if (base_info->items.size() > 5 && !base_info->items[5].is_list) {
                    item.enabled = platform_bool_text(base_info->items[5].atom);
                }
            }
            if (const auto* geometry = find_immediate_geometry_record(value)) {
                item.left = geometry->items[1].atom;
                item.top = geometry->items[2].atom;
                item.right = geometry->items[3].atom;
                item.bottom = geometry->items[4].atom;
                for (std::size_t index = 6; index <= 11 && index < geometry->items.size(); ++index) {
                    if (is_public_form_control_binding_value(geometry->items[index])) {
                        item.bindings.push_back({binding_coordinate_name(index), geometry->items[index]});
                    }
                }
                for (std::size_t index = 13; index <= 16 && index < geometry->items.size(); ++index) {
                    if (is_public_form_control_dimension_binding_value(geometry->items[index])) {
                        item.dimension_bindings.push_back({dimension_binding_name(index), geometry->items[index]});
                    }
                }
            }
            item.picture_payload = first_base64_picture_payload(value);
            item.events = collect_materialized_form_events(value, item.object_id, path);
            next_parent = item.object_id;
            items.push_back(std::move(item));
        } else if (binding == nullptr) {
            ++nested_unbound_guid_nodes;
        }
    }

    for (std::size_t index = 0; index < value.items.size(); ++index) {
        collect_materialized_form_items(
            value.items[index],
            child_path(path, index),
            next_parent,
            items,
            guid_head_nodes,
            nested_unbound_guid_nodes);
    }
}

bool is_materialized_form_property_block(const oof::platform::stream::ListValue& value) {
    // Platform logform.xsd Form sequence: elements, command*, property*.
    // In the runtime payload the property collection is the root slot that
    // contains the counted TypeDomainPattern/name records for form attributes.
    if (!value.is_list || value.items.size() < 3 || !value.items[2].is_list) {
        return false;
    }
    const auto& counted_properties = value.items[2];
    if (counted_properties.items.empty() || counted_properties.items[0].is_list) {
        return false;
    }
    for (std::size_t index = 1; index < counted_properties.items.size(); ++index) {
        const auto& record = counted_properties.items[index];
        if (record.is_list && record.items.size() >= 5 && record.items[0].is_list) {
            return true;
        }
    }
    return false;
}

const oof::platform::stream::ListValue* find_materialized_form_property_block(
    const oof::platform::stream::ListValue& payload,
    std::size_t* root_index = nullptr
) {
    if (!payload.is_list || payload.items.size() <= 2) {
        return nullptr;
    }
    for (std::size_t index = 2; index < payload.items.size(); ++index) {
        const auto& candidate = payload.items[index];
        if (is_materialized_form_property_block(candidate)) {
            if (root_index != nullptr) {
                *root_index = index;
            }
            return &candidate;
        }
    }
    return nullptr;
}

oof::platform::stream::ListValue* find_materialized_form_property_block_mut(
    oof::platform::stream::ListValue& payload,
    std::size_t* root_index = nullptr
) {
    if (!payload.is_list || payload.items.size() <= 2) {
        return nullptr;
    }
    for (std::size_t index = 2; index < payload.items.size(); ++index) {
        auto& candidate = payload.items[index];
        if (is_materialized_form_property_block(candidate)) {
            if (root_index != nullptr) {
                *root_index = index;
            }
            return &candidate;
        }
    }
    return nullptr;
}

std::vector<MaterializedFormAttribute> collect_materialized_form_attributes(
    const oof::platform::stream::ListValue& payload
) {
    std::vector<MaterializedFormAttribute> attributes;
    std::size_t property_root_index = 0;
    const auto* property_block = find_materialized_form_property_block(payload, &property_root_index);
    if (property_block == nullptr) {
        return attributes;
    }
    const auto& counted_properties = property_block->items[2];
    for (std::size_t index = 1; index < counted_properties.items.size(); ++index) {
        const auto& record = counted_properties.items[index];
        if (!record.is_list || record.items.size() < 5 || !record.items[0].is_list) {
            continue;
        }

        MaterializedFormAttribute attribute;
        attribute.id = oof::platform::stream::dump_compact(record.items[0]);
        attribute.object_id = "attribute:" + attribute.id;
        attribute.path = "$/" + std::to_string(property_root_index) + "/2/" + std::to_string(index);
        attribute.main = record.items.size() > 1 && !record.items[1].is_list ? record.items[1].atom : "";
        attribute.stored_data = record.items.size() > 2 && !record.items[2].is_list ? record.items[2].atom : "";
        attribute.name = record.items.size() > 4 && !record.items[4].is_list ? record.items[4].atom : "";
        if (record.items.size() > 5) {
            attribute.type_pattern = oof::platform::stream::dump_compact(record.items[5]);
        }
        attributes.push_back(std::move(attribute));
    }
    return attributes;
}

bool looks_like_materialized_form_command_record(const oof::platform::stream::ListValue& value) {
    if (!value.is_list || value.items.size() < 2 || value.items[0].is_list) {
        return false;
    }
    if (value.items.size() == 3 && value.items[2].is_list) {
        return false;
    }
    return value.items[0].atom == "command" ||
           value.items[0].atom == "Command" ||
           value.items[0].atom == "cmd" ||
           value.items[0].atom == "cmdi";
}

std::string command_record_id_value(const oof::platform::stream::ListValue& record) {
    if (!record.is_list || record.items.empty()) {
        return {};
    }
    if (looks_like_materialized_form_command_record(record) && record.items.size() > 1) {
        return oof::platform::stream::dump_compact(record.items[1]);
    }
    return oof::platform::stream::dump_compact(record.items[0]);
}

std::string command_record_scalar_value(
    const oof::platform::stream::ListValue& record,
    std::size_t tagged_index,
    std::size_t plain_index
) {
    if (!record.is_list) {
        return {};
    }
    const std::size_t index = looks_like_materialized_form_command_record(record) ? tagged_index : plain_index;
    if (record.items.size() > index && !record.items[index].is_list) {
        return record.items[index].atom;
    }
    return {};
}

void collect_materialized_form_commands_from_block(
    const oof::platform::stream::ListValue& block,
    std::string_view path,
    std::vector<MaterializedFormCommand>& commands
) {
    if (!block.is_list || is_materialized_form_property_block(block)) {
        return;
    }

    if (looks_like_materialized_form_command_record(block) ||
        (block.items.size() >= 4 && (block.items[0].is_list || !block.items[0].atom.empty()))) {
        MaterializedFormCommand command;
        command.id = command_record_id_value(block);
        command.object_id = "command:" + command.id;
        command.name = command_record_scalar_value(block, 2, 1);
        command.handler = command_record_scalar_value(block, 3, 2);
        command.modifies_data = command_record_scalar_value(block, 4, 3);
        command.path = std::string(path);
        if (!command.id.empty() && (!command.name.empty() || !command.handler.empty() || !command.modifies_data.empty())) {
            commands.push_back(std::move(command));
            return;
        }
    }

    for (std::size_t index = 0; index < block.items.size(); ++index) {
        collect_materialized_form_commands_from_block(
            block.items[index],
            child_path(path, index),
            commands);
    }
}

std::vector<MaterializedFormCommand> collect_materialized_form_commands(
    const oof::platform::stream::ListValue& payload
) {
    std::vector<MaterializedFormCommand> commands;
    if (!payload.is_list || payload.items.size() <= 2) {
        return commands;
    }
    for (std::size_t index = 2; index < payload.items.size(); ++index) {
        const auto& root_child = payload.items[index];
        if (is_materialized_form_property_block(root_child)) {
            continue;
        }
        collect_materialized_form_commands_from_block(
            root_child,
            "$/" + std::to_string(index),
            commands);
    }
    return commands;
}

void print_form_payload_structure_json(
    const std::vector<std::uint8_t>& form_payload,
    std::string_view source_label
) {
    const std::string text = decode_form_payload_text(form_payload);
    const auto root = oof::platform::stream::parse(text);
    if (!root.is_list) {
        throw std::runtime_error("form payload root is not a list");
    }

    PayloadScanStats stats;
    collect_payload_scan(root, stats);

    std::vector<GuidNodeInfo> guid_nodes;
    std::vector<FormatAtomInfo> format_atoms;
    collect_form_payload_structure(root, "$", guid_nodes, format_atoms);

    std::map<std::string, std::size_t> guid_frequency;
    std::map<std::string, std::size_t> descriptor_status_frequency;
    std::size_t candidate_object_nodes = 0;
    std::size_t known_descriptor_candidate_nodes = 0;
    for (const auto& node : guid_nodes) {
        ++guid_frequency[node.guid];
        ++descriptor_status_frequency[
            node.descriptor_binding == nullptr ? "unbound" : std::string(node.descriptor_binding->status)
        ];
        if ((node.arity == 6 || node.arity == 7) && !node.slot1.empty()) {
            ++candidate_object_nodes;
            if (node.known_descriptor_pool_member) {
                ++known_descriptor_candidate_nodes;
            }
        }
    }

    std::cout << "{";
    std::cout << "\"source\":";
    print_json_string(source_label);
    std::cout << ",\"payloadSize\":" << form_payload.size();
    std::cout << ",\"rootArity\":" << root.items.size();
    if (!root.items.empty() && !root.items[0].is_list) {
        std::cout << ",\"rootVersion\":";
        print_json_string(root.items[0].atom);
    }
    if (root.items.size() > 1 && root.items[1].is_list && !root.items[1].items.empty() && !root.items[1].items[0].is_list) {
        std::cout << ",\"formSectionVersion\":";
        print_json_string(root.items[1].items[0].atom);
    }
    std::cout << ",\"lists\":" << stats.lists;
    std::cout << ",\"atoms\":" << stats.atoms;
    std::cout << ",\"guidHeadNodes\":" << guid_nodes.size();
    std::cout << ",\"candidateObjectNodes\":" << candidate_object_nodes;
    std::cout << ",\"knownDescriptorCandidateNodes\":" << known_descriptor_candidate_nodes;
    std::cout << ",\"guidNodesTotal\":" << guid_nodes.size();
    constexpr std::size_t max_guid_nodes_to_print = 64;
    const std::size_t guid_nodes_to_print = std::min(guid_nodes.size(), max_guid_nodes_to_print);
    std::cout << ",\"guidNodesTruncated\":" << (guid_nodes_to_print < guid_nodes.size() ? "true" : "false");
    std::cout << ",\"descriptorStatusFrequency\":[";
    std::size_t status_index = 0;
    for (const auto& [status, count] : descriptor_status_frequency) {
        if (status_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"status\":";
        print_json_string(status);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "]";
    std::cout << ",\"guidFrequency\":[";
    std::size_t frequency_index = 0;
    for (const auto& [guid, count] : guid_frequency) {
        if (frequency_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"guid\":";
        print_json_string(guid);
        std::cout << ",\"count\":" << count;
        std::cout << ",\"knownDescriptorPoolMember\":"
                  << (is_known_descriptor_pool_guid(guid) ? "true" : "false");
        if (const auto* binding = oof::platform::form_descriptor::binding_for_guid(guid)) {
            std::cout << ",\"descriptorBinding\":{\"status\":";
            print_json_string(binding->status);
            std::cout << ",\"role\":";
            print_json_string(binding->role);
            std::cout << ",\"platformType\":";
            print_json_string(binding->platform_type);
            std::cout << ",\"streamElement\":";
            print_json_string(binding->stream_element);
            std::cout << ",\"evidence\":";
            print_json_string(binding->evidence);
            std::cout << "}";
        }
        std::cout << "}";
    }
    std::cout << "],\"guidNodes\":[";
    for (std::size_t index = 0; index < guid_nodes_to_print; ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& node = guid_nodes[index];
        std::cout << "{\"path\":";
        print_json_string(node.path);
        std::cout << ",\"guid\":";
        print_json_string(node.guid);
        std::cout << ",\"arity\":" << node.arity;
        if (!node.slot1.empty()) {
            std::cout << ",\"slot1\":";
            print_json_string(node.slot1);
        }
        std::cout << ",\"scalarChildren\":" << node.scalar_children;
        std::cout << ",\"listChildren\":" << node.list_children;
        std::cout << ",\"knownDescriptorPoolMember\":"
                  << (node.known_descriptor_pool_member ? "true" : "false");
        if (node.descriptor_binding != nullptr) {
            std::cout << ",\"descriptorBinding\":{\"status\":";
            print_json_string(node.descriptor_binding->status);
            std::cout << ",\"role\":";
            print_json_string(node.descriptor_binding->role);
            std::cout << ",\"platformType\":";
            print_json_string(node.descriptor_binding->platform_type);
            std::cout << ",\"streamElement\":";
            print_json_string(node.descriptor_binding->stream_element);
            std::cout << ",\"evidence\":";
            print_json_string(node.descriptor_binding->evidence);
            std::cout << "}";
        }
        std::cout << "}";
    }
    std::cout << "],\"formatIdAtoms\":[";
    for (std::size_t index = 0; index < format_atoms.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& atom = format_atoms[index];
        std::cout << "{\"path\":";
        print_json_string(atom.path);
        std::cout << ",\"formatId\":" << atom.format_id;
        std::cout << ",\"symbol\":";
        print_json_string(atom.symbol);
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void print_form_payload_structure(const std::string& path) {
    const std::vector<std::uint8_t> data = read_file_bytes(path);
    const auto container = oof::platform::formbin::parse_container(data);
    const auto& form_file = find_container_file(container, "form");
    print_form_payload_structure_json(form_file.payload, "Form.bin:form");
}

void print_form_object_graph_json(
    const std::vector<std::uint8_t>& form_payload,
    std::string_view source_label
) {
    const std::string text = decode_form_payload_text(form_payload);
    const auto root = oof::platform::stream::parse(text);
    if (!root.is_list) {
        throw std::runtime_error("form payload root is not a list");
    }

    std::vector<MaterializedFormItem> items;
    std::size_t guid_head_nodes = 0;
    std::size_t nested_unbound_guid_nodes = 0;
    collect_materialized_form_items(root, "$", "", items, guid_head_nodes, nested_unbound_guid_nodes);

    std::map<std::string_view, std::size_t> type_frequency;
    std::map<std::string_view, std::size_t> status_frequency;
    std::size_t named_items = 0;
    std::size_t schema_backed = 0;
    for (const auto& item : items) {
        ++status_frequency[item.descriptor_binding->status];
        ++type_frequency[item.descriptor_binding->platform_type];
        if (!item.name.empty()) {
            ++named_items;
        }
        if (oof::platform::form_descriptor::schema_for_binding(*item.descriptor_binding) != nullptr) {
            ++schema_backed;
        }
    }

    std::cout << "{\"source\":";
    print_json_string(source_label);
    std::cout << ",\"payloadSize\":" << form_payload.size();
    std::cout << ",\"rootArity\":" << root.items.size();
    if (!root.items.empty() && !root.items[0].is_list) {
        std::cout << ",\"rootVersion\":";
        print_json_string(root.items[0].atom);
    }
    if (root.items.size() > 1 && root.items[1].is_list && !root.items[1].items.empty() && !root.items[1].items[0].is_list) {
        std::cout << ",\"formSectionVersion\":";
        print_json_string(root.items[1].items[0].atom);
    }
    std::cout << ",\"guidHeadNodes\":" << guid_head_nodes;
    std::cout << ",\"materializedItems\":" << items.size();
    std::cout << ",\"namedItems\":" << named_items;
    std::cout << ",\"schemaBackedItems\":" << schema_backed;
    std::cout << ",\"nestedUnboundGuidNodes\":" << nested_unbound_guid_nodes;
    std::cout << ",\"statusFrequency\":[";
    std::size_t index = 0;
    for (const auto& [status, count] : status_frequency) {
        if (index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"status\":";
        print_json_string(status);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"typeFrequency\":[";
    index = 0;
    for (const auto& [type, count] : type_frequency) {
        if (index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"type\":";
        print_json_string(type);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"items\":[";
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
        if (item_index != 0) {
            std::cout << ",";
        }
        const auto& item = items[item_index];
        const auto* schema = oof::platform::form_descriptor::schema_for_binding(*item.descriptor_binding);
        std::cout << "{\"objectId\":";
        print_json_string(item.object_id);
        std::cout << ",\"name\":";
        print_json_string(item.name);
        std::cout << ",\"parentObjectId\":";
        print_json_string(item.parent_object_id);
        std::cout << ",\"path\":";
        print_json_string(item.path);
        std::cout << ",\"guid\":";
        print_json_string(item.guid);
        std::cout << ",\"arity\":" << item.arity;
        std::cout << ",\"platformType\":";
        print_json_string(item.descriptor_binding->platform_type);
        std::cout << ",\"streamElement\":";
        print_json_string(item.descriptor_binding->stream_element);
        std::cout << ",\"status\":";
        print_json_string(item.descriptor_binding->status);
        std::cout << ",\"schemaBacked\":"
                  << (schema != nullptr ? "true" : "false");
        if (schema != nullptr) {
            std::cout << ",\"schemaAttributes\":";
            print_json_string(schema->attributes);
            std::cout << ",\"schemaChildElements\":";
            print_json_string(schema->child_elements);
        }
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void print_form_object_graph(const std::string& path) {
    const std::vector<std::uint8_t> data = read_file_bytes(path);
    const auto container = oof::platform::formbin::parse_container(data);
    const auto& form_file = find_container_file(container, "form");
    print_form_object_graph_json(form_file.payload, "Form.bin:form");
}

struct RuntimeFormEnvelope {
    std::string marker;
    std::string runtime_uuid;
    oof::platform::stream::ListValue payload;
};

RuntimeFormEnvelope parse_runtime_form_envelope(const std::string& text) {
    auto root = oof::platform::stream::parse(text);
    if (!root.is_list || root.items.size() != 3) {
        throw std::runtime_error("runtime form stream must be {\"#\",runtime-guid,form-payload}");
    }
    if (root.items[0].is_list || root.items[1].is_list || !root.items[2].is_list) {
        throw std::runtime_error("runtime form stream envelope has unexpected slot types");
    }
    if (root.items[0].atom != "#") {
        throw std::runtime_error("runtime form stream envelope marker is not #");
    }
    if (!is_guid_text(root.items[1].atom)) {
        throw std::runtime_error("runtime form stream envelope uuid is not a GUID atom");
    }
    if (root.items[2].items.empty() || root.items[2].items[0].is_list) {
        throw std::runtime_error("runtime form payload root is missing version atom");
    }

    RuntimeFormEnvelope envelope;
    envelope.marker = root.items[0].atom;
    envelope.runtime_uuid = root.items[1].atom;
    envelope.payload = std::move(root.items[2]);
    return envelope;
}

RuntimeFormEnvelope runtime_envelope_from_form_payload(const std::vector<std::uint8_t>& form_payload) {
    RuntimeFormEnvelope envelope;
    envelope.marker = "#";
    envelope.runtime_uuid = "00000000-0000-0000-0000-000000000000";
    envelope.payload = oof::platform::stream::parse(decode_form_payload_text(form_payload));
    if (!envelope.payload.is_list) {
        throw std::runtime_error("Form.bin form payload does not look like an ordinary form list stream");
    }
    return envelope;
}

oof::platform::stream::ListValue build_runtime_form_envelope_value(const RuntimeFormEnvelope& envelope) {
    return oof::platform::stream::ListValue::list({
        oof::platform::stream::ListValue::string_atom(envelope.marker),
        oof::platform::stream::ListValue::raw_atom(envelope.runtime_uuid),
        envelope.payload
    });
}

std::string dump_runtime_form_envelope(const RuntimeFormEnvelope& envelope) {
    return oof::platform::stream::dump_compact(build_runtime_form_envelope_value(envelope));
}

struct MaterializedGraphSummary {
    std::vector<MaterializedFormItem> items;
    std::vector<MaterializedFormAttribute> attributes;
    std::vector<MaterializedFormCommand> commands;
    std::vector<MaterializedFormEvent> events;
    std::size_t guid_head_nodes = 0;
    std::size_t nested_unbound_guid_nodes = 0;
    std::size_t named_items = 0;
    std::size_t schema_backed_items = 0;
    std::map<std::string_view, std::size_t> type_frequency;
    std::map<std::string_view, std::size_t> status_frequency;
};

RuntimeFormEnvelope read_runtime_form_envelope_file(const std::string& path, std::string& canonical_text);

MaterializedGraphSummary summarize_materialized_graph(const oof::platform::stream::ListValue& payload) {
    MaterializedGraphSummary summary;
    collect_materialized_form_items(
        payload,
        "$",
        "",
        summary.items,
        summary.guid_head_nodes,
        summary.nested_unbound_guid_nodes);
    summary.attributes = collect_materialized_form_attributes(payload);
    summary.commands = collect_materialized_form_commands(payload);

    for (const auto& item : summary.items) {
        ++summary.status_frequency[item.descriptor_binding->status];
        ++summary.type_frequency[item.descriptor_binding->platform_type];
        if (!item.name.empty()) {
            ++summary.named_items;
        }
        summary.events.insert(summary.events.end(), item.events.begin(), item.events.end());
        if (oof::platform::form_descriptor::schema_for_binding(*item.descriptor_binding) != nullptr) {
            ++summary.schema_backed_items;
        }
    }
    return summary;
}

std::string xml_escape(std::string_view value);

std::string layout_dimension_name(std::string_view code) {
    if (code == "0") return "top";
    if (code == "1") return "bottom";
    if (code == "2") return "left";
    if (code == "3") return "right";
    return std::string("dimension") + std::string(code);
}

std::string layout_dimension_code(std::string_view name) {
    if (name == "top") return "0";
    if (name == "bottom") return "1";
    if (name == "left") return "2";
    if (name == "right") return "3";
    if (name.rfind("dimension", 0) == 0) {
        return std::string(name.substr(std::string_view("dimension").size()));
    }
    return "0";
}

bool layout_count_at(const oof::platform::stream::ListValue& info, std::size_t cursor) {
    if (!info.is_list || cursor >= info.items.size() || info.items[cursor].is_list) {
        return false;
    }
    std::size_t count = 0;
    try {
        count = static_cast<std::size_t>(std::stoll(info.items[cursor].atom));
    } catch (...) {
        return false;
    }
    if (count == 0 || info.items.size() < cursor + 1 + count) {
        return false;
    }
    for (std::size_t index = cursor + 1; index < cursor + 1 + count; ++index) {
        if (!info.items[index].is_list) {
            return false;
        }
    }
    return true;
}

bool is_page_style_group_value(const oof::platform::stream::ListValue& value) {
    return value.is_list &&
           value.items.size() >= 5 &&
           !value.items[0].is_list &&
           (value.items[0].atom == "8" || value.items[0].atom == "10");
}

bool is_page_state_value(const oof::platform::stream::ListValue& value) {
    return value.is_list &&
           !value.items.empty() &&
           !value.items[0].is_list &&
           (value.items[0].atom == "3" || value.items[0].atom == "5" || value.items[0].atom == "6");
}

std::pair<std::size_t, std::size_t> root_layout_dependency_cursor(const oof::platform::stream::ListValue& info) {
    std::size_t cursor = 2;
    while (cursor < info.items.size()) {
        if (layout_count_at(info, cursor)) {
            break;
        }
        if (info.items[cursor].is_list) {
            return {2, cursor};
        }
        ++cursor;
    }
    std::size_t end = cursor;
    while (end < info.items.size()) {
        if (!layout_count_at(info, end)) {
            break;
        }
        const std::size_t count = static_cast<std::size_t>(std::stoll(info.items[end].atom));
        end += 1 + count;
    }
    return {cursor, end};
}

std::pair<std::string, std::string> root_layout_page_scalars(
    const oof::platform::stream::ListValue& info,
    std::size_t cursor
) {
    while (cursor < info.items.size() && !is_page_style_group_value(info.items[cursor])) {
        ++cursor;
    }
    if (cursor + 2 >= info.items.size()) {
        return {"", ""};
    }
    return {
        info.items[cursor + 1].is_list ? "" : info.items[cursor + 1].atom,
        info.items[cursor + 2].is_list ? "" : info.items[cursor + 2].atom,
    };
}

std::vector<std::map<std::string, std::string>> root_layout_page_layouts(
    const oof::platform::stream::ListValue& info,
    std::size_t cursor
) {
    while (cursor < info.items.size() && !is_page_style_group_value(info.items[cursor])) {
        ++cursor;
    }
    if (cursor >= info.items.size()) {
        return {};
    }
    ++cursor;
    while (cursor < info.items.size() &&
           !(info.items[cursor].is_list &&
             !info.items[cursor].items.empty() &&
             !info.items[cursor].items[0].is_list &&
             info.items[cursor].items[0].atom == "1")) {
        ++cursor;
    }
    if (cursor + 4 >= info.items.size()) {
        return {};
    }
    cursor += 4;
    if (cursor >= info.items.size() || info.items[cursor].is_list) {
        return {};
    }
    std::size_t record_count = 0;
    try {
        record_count = static_cast<std::size_t>(std::stoll(info.items[cursor].atom));
    } catch (...) {
        return {};
    }
    ++cursor;
    std::vector<std::map<std::string, std::string>> layouts;
    for (std::size_t offset = 0; offset + 3 < record_count; offset += 4) {
        if (cursor + offset + 3 >= info.items.size()) {
            break;
        }
        const auto& left = info.items[cursor + offset];
        const auto& top = info.items[cursor + offset + 1];
        const auto& width = info.items[cursor + offset + 2];
        const auto& height = info.items[cursor + offset + 3];
        if (!left.is_list || !top.is_list || !width.is_list || !height.is_list ||
            left.items.size() < 9 || top.items.size() < 9 || width.items.size() < 9 || height.items.size() < 9) {
            break;
        }
        layouts.push_back({
            {"page", left.items[5].atom},
            {"left", left.items[1].atom},
            {"top", top.items[1].atom},
            {"width", width.items[1].atom},
            {"height", height.items[1].atom},
            {"horizontalMode", width.items[7].atom},
            {"verticalMode", height.items[7].atom},
        });
    }
    return layouts;
}

std::string root_panel_layout_xml_from_payload(const oof::platform::stream::ListValue& payload) {
    if (!payload.is_list || payload.items.size() <= 1) {
        return {};
    }
    const auto& root_record = payload.items[1];
    if (!root_record.is_list || root_record.items.size() <= 2) {
        return {};
    }
    const auto& root_panel = root_record.items[2];
    if (!root_panel.is_list ||
        root_panel.items.size() <= 1 ||
        !root_panel.items[1].is_list ||
        root_panel.items[1].items.size() <= 1 ||
        !root_panel.items[1].items[1].is_list) {
        return {};
    }
    const auto& info = root_panel.items[1].items[1];
    if (info.items.size() < 2 || info.items[1].is_list || info.items[1].atom != "26") {
        return {};
    }
    const auto [dependency_start, dependency_end] = root_layout_dependency_cursor(info);
    const auto [page_state_flag, current_page_index] = root_layout_page_scalars(info, dependency_end);
    const auto layouts = root_layout_page_layouts(info, dependency_end);

    std::string out = "<RootPanelLayout";
    if (!page_state_flag.empty()) {
        out += " pageStateFlag=\"" + xml_escape(page_state_flag) + "\"";
    }
    if (!current_page_index.empty()) {
        out += " currentPageIndex=\"" + xml_escape(current_page_index) + "\"";
    }
    out += ">\n";
    std::size_t group_order = 1;
    for (std::size_t cursor = dependency_start; cursor < dependency_end;) {
        if (!layout_count_at(info, cursor)) {
            break;
        }
        const std::size_t count = static_cast<std::size_t>(std::stoll(info.items[cursor].atom));
        out += "  <LayoutDependencyGroup order=\"" + std::to_string(group_order++) + "\">\n";
        for (std::size_t index = 0; index < count; ++index) {
            const auto& record = info.items[cursor + 1 + index];
            if (record.is_list && record.items.size() >= 3 && !record.items[1].is_list && !record.items[2].is_list) {
                out += "    <LayoutDependency targetId=\"" + xml_escape(record.items[1].atom) +
                       "\" dimension=\"" + xml_escape(layout_dimension_name(record.items[2].atom)) + "\"/>\n";
            }
        }
        out += "  </LayoutDependencyGroup>\n";
        cursor += 1 + count;
    }
    for (const auto& item : info.items) {
        if (!item.is_list || item.items.size() < 3 || item.items[0].is_list || item.items[0].atom != "1") {
            continue;
        }
        for (std::size_t index = 2; index < item.items.size(); ++index) {
            const auto& state = item.items[index];
            if (!is_page_state_value(state) || state.items.size() <= 6 || state.items[6].is_list) {
                continue;
            }
            std::string title;
            if (state.items.size() > 1) {
                find_first_localized_text(state.items[1], title);
            }
            std::string style_mode;
            if (state.items.size() > 2 && state.items[2].is_list && state.items[2].items.size() > 6 && !state.items[2].items[6].is_list) {
                style_mode = state.items[2].items[6].atom;
            }
            out += "  <PageState name=\"" + xml_escape(state.items[6].atom) + "\"";
            if (!style_mode.empty()) {
                out += " styleMode=\"" + xml_escape(style_mode) + "\"";
            }
            if (title.empty()) {
                out += "/>\n";
            } else {
                out += ">\n    <Title>" + xml_escape(title) + "</Title>\n  </PageState>\n";
            }
        }
        break;
    }
    for (const auto& layout : layouts) {
        out += "  <PageLayout page=\"" + xml_escape(layout.at("page")) +
               "\" left=\"" + xml_escape(layout.at("left")) +
               "\" top=\"" + xml_escape(layout.at("top")) +
               "\" width=\"" + xml_escape(layout.at("width")) +
               "\" height=\"" + xml_escape(layout.at("height")) +
               "\" horizontalMode=\"" + xml_escape(layout.at("horizontalMode")) +
               "\" verticalMode=\"" + xml_escape(layout.at("verticalMode")) + "\"/>\n";
    }
    out += "</RootPanelLayout>";
    return out;
}

void print_materialized_graph_json(const MaterializedGraphSummary& summary) {
    std::cout << "\"guidHeadNodes\":" << summary.guid_head_nodes;
    std::cout << ",\"materializedItems\":" << summary.items.size();
    std::cout << ",\"namedItems\":" << summary.named_items;
    std::cout << ",\"schemaBackedItems\":" << summary.schema_backed_items;
    std::cout << ",\"nestedUnboundGuidNodes\":" << summary.nested_unbound_guid_nodes;
    std::cout << ",\"statusFrequency\":[";
    std::size_t index = 0;
    for (const auto& [status, count] : summary.status_frequency) {
        if (index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"status\":";
        print_json_string(status);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"typeFrequency\":[";
    index = 0;
    for (const auto& [type, count] : summary.type_frequency) {
        if (index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"type\":";
        print_json_string(type);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"items\":[";
    for (std::size_t item_index = 0; item_index < summary.items.size(); ++item_index) {
        if (item_index != 0) {
            std::cout << ",";
        }
        const auto& item = summary.items[item_index];
        const auto* schema = oof::platform::form_descriptor::schema_for_binding(*item.descriptor_binding);
        std::cout << "{\"objectId\":";
        print_json_string(item.object_id);
        std::cout << ",\"name\":";
        print_json_string(item.name);
        std::cout << ",\"parentObjectId\":";
        print_json_string(item.parent_object_id);
        std::cout << ",\"path\":";
        print_json_string(item.path);
        std::cout << ",\"guid\":";
        print_json_string(item.guid);
        std::cout << ",\"arity\":" << item.arity;
        std::cout << ",\"platformType\":";
        print_json_string(item.descriptor_binding->platform_type);
        std::cout << ",\"streamElement\":";
        print_json_string(item.descriptor_binding->stream_element);
        std::cout << ",\"status\":";
        print_json_string(item.descriptor_binding->status);
        std::cout << ",\"schemaBacked\":"
                  << (schema != nullptr ? "true" : "false");
        if (schema != nullptr) {
            std::cout << ",\"schemaAttributes\":";
            print_json_string(schema->attributes);
            std::cout << ",\"schemaChildElements\":";
            print_json_string(schema->child_elements);
        }
        std::cout << "}";
    }
    std::cout << "]";
}

std::vector<std::string> split_csv_list(std::string_view value) {
    std::vector<std::string> out;
    while (!value.empty()) {
        const std::size_t comma = value.find(',');
        std::string_view item = value.substr(0, comma);
        while (!item.empty() && item.front() == ' ') {
            item.remove_prefix(1);
        }
        while (!item.empty() && item.back() == ' ') {
            item.remove_suffix(1);
        }
        if (!item.empty()) {
            out.emplace_back(item);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        value.remove_prefix(comma + 1);
    }
    return out;
}

std::string localized_property_name(std::string_view name) {
    if (const auto* descriptor = oof::platform::property_registry::find_descriptor(name)) {
        return std::string(descriptor->localized_name);
    }
    return {};
}

bool property_value_type_is(std::string_view value_type, std::string_view type_name) {
    return value_type == type_name || value_type == ("ui:" + std::string(type_name));
}

std::string platform_value_object_storage(
    std::string_view object_class,
    std::string_view value
) {
    if (object_class == "Picture") {
        if (value.empty() || value == "V8Picture()") {
            return "empty";
        }
        if (value.rfind("#base64:", 0) == 0) {
            return "inline-base64";
        }
        if (value.rfind("file:", 0) == 0 || value.rfind("relative:", 0) == 0) {
            return "relative";
        }
        if (value.rfind("ref:", 0) == 0 || value.find("PictureRef") != std::string::npos) {
            return "reference";
        }
        return "persistent";
    }
    if (object_class == "Color") {
        if (value.empty() || value.find("eAutoColor") != std::string::npos || value == "auto") {
            return "auto";
        }
        if (value.rfind("#", 0) == 0) {
            return "absolute-rgb";
        }
        if (value.rfind("style:", 0) == 0 || value.find("IV8Style") != std::string::npos ||
            value.find("kLogForm") != std::string::npos) {
            return "style";
        }
        return "value";
    }
    if (object_class == "Font") {
        if (value.empty() || value.find("eAutoFont") != std::string::npos || value == "auto") {
            return "auto";
        }
        if (value.rfind("style:", 0) == 0 || value.find("kLogForm") != std::string::npos ||
            value.rfind("sys:", 0) == 0) {
            return "style";
        }
        return "value";
    }
    return {};
}

int hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    return -1;
}

bool parse_hex_rgb(std::string_view value, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue) {
    if (value.size() != 7 || value[0] != '#') {
        return false;
    }
    const int r1 = hex_digit_value(value[1]);
    const int r2 = hex_digit_value(value[2]);
    const int g1 = hex_digit_value(value[3]);
    const int g2 = hex_digit_value(value[4]);
    const int b1 = hex_digit_value(value[5]);
    const int b2 = hex_digit_value(value[6]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
        return false;
    }
    red = static_cast<std::uint8_t>((r1 << 4) | r2);
    green = static_cast<std::uint8_t>((g1 << 4) | g2);
    blue = static_cast<std::uint8_t>((b1 << 4) | b2);
    return true;
}

std::string style_ref_name_from_platform_literal(std::string_view value) {
    const std::string text(value);
    const std::string marker = "IV8Style::e";
    const auto marker_pos = text.find(marker);
    if (marker_pos != std::string::npos) {
        const auto start = marker_pos + marker.size();
        const auto end = text.find_first_of("), ", start);
        return "style:" + text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    if (value.rfind("style:", 0) == 0) {
        return std::string(value);
    }
    if (text.find("kLogFormButtonDefTextColor") != std::string::npos) {
        return "style:ButtonTextColor";
    }
    if (text.find("kLogFormButtonDefBackColor") != std::string::npos) {
        return "style:ButtonBkgrndColor";
    }
    if (text.find("BorderColor") != std::string::npos) {
        return "style:BorderColor";
    }
    if (text.find("BackColor") != std::string::npos || text.find("BkClr") != std::string::npos) {
        return "style:BackColor";
    }
    if (text.find("TextColor") != std::string::npos || text.find("TxtClr") != std::string::npos) {
        return "style:TextColor";
    }
    return {};
}

std::string font_ref_name_from_platform_literal(std::string_view value) {
    if (value.rfind("style:", 0) == 0 || value.rfind("sys:", 0) == 0) {
        return std::string(value);
    }
    if (value.find("kLogForm") != std::string::npos && value.find("Font") != std::string::npos) {
        return "sys:DefaultGUIFont";
    }
    return {};
}

std::string platform_value_object_evidence(std::string_view object_class) {
    if (object_class == "Picture") {
        return "xdto_root.res:data_ui.xsd Picture/PictureRef + core::V8Picture::to_storage/from_storage";
    }
    if (object_class == "Color") {
        return "xdto_root.res:data_ui.xsd Color + core::Color::serialize/deserialize";
    }
    if (object_class == "Font") {
        return "xdto_root.res:data_ui.xsd Font + core::Font::serialize/deserialize";
    }
    if (object_class == "Border") {
        return "xdto_root.res:data_ui.xsd Border + core::V8Border::serialize/deserialize";
    }
    return {};
}

void project_platform_value_object(
    oof::platform::object_model::PlatformObjectProperty& property
) {
    property.value_object_owner_member = property.platform_member;
    property.value_object_evidence = platform_value_object_evidence(property.value_object_class);

    if (property.value_object_class == "Picture") {
        oof::platform::value::V8Picture picture;
        if (property.value != "V8Picture()" && !property.value.empty()) {
            picture.storage_id = property.value;
            property.value_object_schema_value = property.value;
        }
        property.value_object_list_stream = picture.serialize_list_stream();
        return;
    }

    if (property.value_object_class == "Color") {
        std::uint8_t red = 0;
        std::uint8_t green = 0;
        std::uint8_t blue = 0;
        oof::platform::value::Color color = oof::platform::value::Color::auto_color();
        if (parse_hex_rgb(property.value, red, green, blue)) {
            color = oof::platform::value::Color::absolute_rgb(red, green, blue);
        } else {
            const std::string style_name = style_ref_name_from_platform_literal(property.value);
            if (!style_name.empty()) {
                color = oof::platform::value::Color::style(
                    oof::platform::value::AbstractRef::named(style_name));
            }
        }
        property.value_object_schema_value = color.schema_value();
        property.value_object_list_stream = color.serialize_list_stream();
        return;
    }

    if (property.value_object_class == "Font") {
        oof::platform::value::Font font;
        const std::string font_ref_name = font_ref_name_from_platform_literal(property.value);
        if (!font_ref_name.empty()) {
            font.kind = oof::platform::value::FontKind::style_item;
            font.ref = oof::platform::value::AbstractRef::named(font_ref_name);
            property.value_object_schema_value = font_ref_name;
        } else if (property.value == "auto" || property.value.find("eAutoFont") != std::string::npos) {
            font.kind = oof::platform::value::FontKind::auto_font;
            property.value_object_schema_value = "AutoFont";
        } else {
            font.kind = oof::platform::value::FontKind::absolute;
            font.face_name = std::string(property.value);
            property.value_object_schema_value = property.value;
        }
        property.value_object_list_stream = font.serialize_list_stream();
        return;
    }

    if (property.value_object_class == "Border") {
        oof::platform::value::V8Border border;
        if (property.value == "Single") {
            border.style = oof::platform::value::BorderType::single;
            border.width = 1;
            property.value_object_schema_value = "Single";
        } else {
            property.value_object_schema_value = "WithoutBorder";
        }
        property.value_object_list_stream = border.serialize_list_stream();
    }
}

void enrich_platform_value_object(
    oof::platform::object_model::PlatformObjectProperty& property
) {
    if (property_value_type_is(property.value_type, "Picture")) {
        property.value_object_class = "Picture";
        property.value_object_constructor = "New Picture";
    } else if (property_value_type_is(property.value_type, "Color")) {
        property.value_object_class = "Color";
        property.value_object_constructor = "New Color";
    } else if (property_value_type_is(property.value_type, "Font")) {
        property.value_object_class = "Font";
        property.value_object_constructor = "New Font";
    } else if (property_value_type_is(property.value_type, "Border")) {
        property.value_object_class = "Border";
        property.value_object_constructor = "New Border";
    }
    if (!property.value_object_class.empty()) {
        property.value_object_storage = platform_value_object_storage(
            property.value_object_class,
            property.value);
        property.value_object_literal = property.value;
        project_platform_value_object(property);
    }
}

oof::platform::object_model::PlatformObjectProperty make_described_property(
    std::string_view name,
    std::string value
) {
    const auto* descriptor = oof::platform::property_registry::find_descriptor(name);
    if (descriptor == nullptr) {
        throw std::runtime_error("platform property descriptor coverage gap: " + std::string(name));
    }
    auto property = oof::platform::object_model::make_property(
        std::string(descriptor->name),
        std::string(descriptor->localized_name),
        std::move(value),
        std::string(descriptor->source),
        std::string(descriptor->value_type),
        std::string(descriptor->slot_binding),
        descriptor->writable,
        std::string(oof::platform::property_registry::slot_codec_name(descriptor->slot_codec)));
    enrich_platform_value_object(property);
    return property;
}

oof::platform::object_model::PlatformObjectProperty make_platform_object_property(
    std::string name,
    std::string localized_name,
    std::string value,
    std::string value_type,
    std::string source,
    std::string default_value = {},
    std::string write_policy = {},
    std::string value_origin = "stream",
    std::string slot_binding = {},
    std::string slot_codec = {},
    bool writable = false,
    std::string platform_member = {},
    std::string platform_default = {}
) {
    auto property = oof::platform::object_model::make_property(
        std::move(name),
        std::move(localized_name),
        std::move(value),
        std::move(source),
        std::move(value_type),
        std::move(slot_binding),
        writable,
        std::move(slot_codec),
        std::move(default_value),
        std::move(write_policy),
        std::move(value_origin),
        std::move(platform_member),
        std::move(platform_default));
    enrich_platform_value_object(property);
    return property;
}

oof::platform::object_model::PlatformObjectCollectionDescriptor make_described_collection(
    std::string_view name,
    std::size_t count
) {
    const auto* descriptor = oof::platform::property_registry::find_descriptor(name);
    if (descriptor == nullptr) {
        throw std::runtime_error("platform collection descriptor coverage gap: " + std::string(name));
    }
    return oof::platform::object_model::make_collection_descriptor(
        std::string(descriptor->name),
        std::string(descriptor->localized_name),
        std::string(descriptor->value_type),
        count,
        std::string(descriptor->source),
        std::string(descriptor->slot_binding),
        descriptor->writable,
        std::string(oof::platform::property_registry::slot_codec_name(descriptor->slot_codec)));
}

void add_api_surface(
    oof::platform::object_model::PlatformObject& object,
    const oof::platform::runtime_binding::PlatformApiObject* api
) {
    if (api == nullptr) {
        return;
    }
    for (const auto& name : split_csv_list(api->sample_properties)) {
        if (object.property(name) == nullptr) {
            if (oof::platform::property_registry::find_descriptor(name) != nullptr) {
                object.properties.push_back(make_described_property(name, ""));
            } else {
                object.coverage_gaps.push_back(oof::platform::object_model::make_coverage_gap(
                    name,
                    std::string(api->api_source),
                    "api property has no PlatformPropertyDescriptor binding"));
            }
        }
    }
    for (const auto& name : split_csv_list(api->sample_methods)) {
        object.methods.push_back(oof::platform::object_model::make_method(name));
    }
    for (const auto& name : split_csv_list(api->sample_events)) {
        object.events.push_back(oof::platform::object_model::make_event(name));
    }
}

void add_platform_object_schema_surface(
    oof::platform::object_model::PlatformObject& object,
    const oof::platform::object_schema::PlatformObjectSchema& schema
) {
    for (const auto& member : schema.xsd_members) {
        if (object.property(member.name) == nullptr) {
            object.properties.push_back(make_platform_object_property(
                member.name,
                localized_property_name(member.name),
                member.default_value,
                member.value_type,
                member.source,
                member.default_value,
                member.write_policy,
                "schema-default",
                member.slot_binding,
                member.slot_codec,
                member.writable,
                member.platform_member,
                member.platform_default));
        }
    }
    for (const auto& name : schema.api_properties) {
        if (object.property(name) == nullptr) {
            if (oof::platform::property_registry::find_descriptor(name) != nullptr) {
                object.properties.push_back(make_described_property(name, ""));
            } else {
                object.coverage_gaps.push_back(oof::platform::object_model::make_coverage_gap(
                    name,
                    schema.schema_source.empty() ? schema.api_source : schema.schema_source,
                    "schema api property has no PlatformPropertyDescriptor binding"));
            }
        }
    }
    for (const auto& name : schema.api_methods) {
        if (!object.has_method(name)) {
            object.methods.push_back(oof::platform::object_model::make_method(name));
        }
    }
    for (const auto& name : schema.api_events) {
        bool exists = false;
        for (const auto& event : object.events) {
            if (event.name == name || event.localized_name == name) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            object.events.push_back(oof::platform::object_model::make_event(name));
        }
    }
}

const oof::platform::runtime_binding::PlatformApiObject* api_object_for_type(std::string_view type_name) {
    for (const auto& api : oof::platform::runtime_binding::api_objects) {
        if (api.name == type_name) {
            return &api;
        }
    }
    return nullptr;
}

class PlatformObjectTypeHandler {
public:
    virtual ~PlatformObjectTypeHandler() = default;
    virtual std::string_view platform_type() const = 0;
    virtual oof::platform::object_model::PlatformFormObject materialize_form(
        const RuntimeFormEnvelope& envelope
    ) const = 0;
};

class FormPlatformObjectTypeHandler final : public PlatformObjectTypeHandler {
public:
    std::string_view platform_type() const override {
        return "Form";
    }

    oof::platform::object_model::PlatformFormObject materialize_form(
        const RuntimeFormEnvelope& envelope
    ) const override {
        const auto summary = summarize_materialized_graph(envelope.payload);

        oof::platform::object_model::PlatformFormObject form_object;
        materialize_form_root(form_object, envelope, summary);
        materialize_attributes(form_object, summary);
        materialize_commands(form_object, summary);
        materialize_items(form_object, summary);
        materialize_events(form_object, summary);
        link_item_children(form_object);
        return form_object;
    }

private:
    static void materialize_form_root(
        oof::platform::object_model::PlatformFormObject& form_object,
        const RuntimeFormEnvelope& envelope,
        const MaterializedGraphSummary& summary
    ) {
        form_object.form.object_id = "0";
        form_object.form.name = "Form";
        form_object.form.platform_type = "Form";
        form_object.form.type_category = "core::kLogFormTypeInfoCategory";
        form_object.form.type_source = "PlatformObjectTypeHandler<Form> + core85 ContextCore + mngbase RTLogForm";
        form_object.form.path = "$";
        form_object.form.properties.push_back(make_described_property("Type", "Form"));
        form_object.form.properties.push_back(make_described_property("RuntimeUUID", envelope.runtime_uuid));
        if (envelope.payload.is_list && envelope.payload.items.size() > 1) {
            const auto& root_record = envelope.payload.items[1];
            if (root_record.is_list && root_record.items.size() > 1) {
                std::string title;
                if (find_first_localized_text(root_record.items[1], title)) {
                    form_object.form.properties.push_back(make_platform_object_property(
                        "Title", "Заголовок", std::move(title), "LocalizedText",
                        "ordinary form root title record", {}, {}, "stream"));
                }
                if (!root_record.items.empty() &&
                    !root_record.items[0].is_list &&
                    root_record.items[0].atom == "18" &&
                    root_record.items.size() >= 14) {
                    form_object.form.properties.push_back(make_platform_object_property(
                        "SerializationCounter", "", root_record.items[10].atom, "xs:nonNegativeInteger",
                        "ordinary form extended root record", {}, {}, "stream"));
                    form_object.form.properties.push_back(make_platform_object_property(
                        "Width", "", root_record.items[11].atom, "xs:integer",
                        "ordinary form extended root record", {}, {}, "stream"));
                    form_object.form.properties.push_back(make_platform_object_property(
                        "Height", "", root_record.items[12].atom, "xs:integer",
                        "ordinary form extended root record", {}, {}, "stream"));
                }
            }
        }
        const std::string root_layout_xml = root_panel_layout_xml_from_payload(envelope.payload);
        if (!root_layout_xml.empty()) {
            form_object.form.properties.push_back(make_platform_object_property(
                "RootPanelLayoutXml", "", root_layout_xml, "RootPanelLayout",
                "OrdinaryFormPalette.xsd RootPanelLayout + ordinary root panel info record", {}, {}, "stream"));
        }
        if (envelope.payload.is_list && envelope.payload.items.size() > 3) {
            const auto& info = envelope.payload.items[3];
            if (info.is_list && info.items.size() >= 2 && !info.items[0].is_list && !info.items[1].is_list) {
                form_object.form.properties.push_back(make_platform_object_property(
                    "FormObjectUuid", "", info.items[0].atom, "UUID",
                    "ordinary form object info record", {}, {}, "stream"));
                form_object.form.properties.push_back(make_platform_object_property(
                    "FormObjectKind", "", info.items[1].atom, "xs:string",
                    "ordinary form object info record", {}, {}, "stream"));
                if (info.items.size() > 2 && info.items[2].is_list && info.items[2].items.size() >= 5) {
                    const auto& state = info.items[2];
                    if (!state.items[0].is_list) {
                        form_object.form.properties.push_back(make_platform_object_property(
                            "FormObjectStateKind", "", state.items[0].atom, "xs:string",
                            "ordinary form object state record", {}, {}, "stream"));
                    }
                    if (!state.items[1].is_list) {
                        form_object.form.properties.push_back(make_platform_object_property(
                            "FormObjectStateMode", "", state.items[1].atom, "xs:string",
                            "ordinary form object state record", {}, {}, "stream"));
                    }
                    if (!state.items[4].is_list) {
                        form_object.form.properties.push_back(make_platform_object_property(
                            "FormObjectStateFlag", "", state.items[4].atom, "xs:string",
                            "ordinary form object state record", {}, {}, "stream"));
                    }
                }
            }
        }
        form_object.form.properties.push_back(make_described_property("Items", std::to_string(summary.items.size())));
        form_object.form.properties.push_back(make_described_property("Attributes", std::to_string(summary.attributes.size())));
        form_object.form.properties.push_back(make_described_property("Commands", std::to_string(summary.commands.size())));
        form_object.form.properties.push_back(make_described_property("Events", std::to_string(summary.events.size())));
        form_object.form.collections.push_back(make_described_collection("Items", summary.items.size()));
        form_object.form.collections.push_back(make_described_collection("Attributes", summary.attributes.size()));
        form_object.form.collections.push_back(make_described_collection("Commands", summary.commands.size()));
        form_object.form.collections.push_back(make_described_collection("Events", summary.events.size()));
        add_platform_object_schema_surface(
            form_object.form,
            oof::platform::object_schema::build_schema_for_root_form());
        add_api_surface(form_object.form, api_object_for_type("Form"));
    }

    static void materialize_attributes(
        oof::platform::object_model::PlatformFormObject& form_object,
        const MaterializedGraphSummary& summary
    ) {
        for (const auto& attribute : summary.attributes) {
            oof::platform::object_model::PlatformObject object;
            object.object_id = attribute.object_id;
            object.name = attribute.name;
            object.platform_type = "FormAttribute";
            object.type_category = "core::kLogFormTypeInfoCategory";
            object.type_source = "PlatformObjectTypeHandler<Form>.Attributes + mngcore logform.xsd Property";
            object.path = attribute.path;
            object.parent_object_id = "0";
            object.properties.push_back(make_platform_object_property(
                "ID", "Идентификатор", attribute.id, "CompositeID", "logform.xsd:Property@id"));
            object.properties.push_back(make_platform_object_property(
                "Name", "Имя", attribute.name, "String", "runtime property block"));
            object.properties.push_back(make_platform_object_property(
                "Main", "Основной", attribute.main, "Boolean", "logform.xsd:Property@main"));
            object.properties.push_back(make_platform_object_property(
                "StoredData", "СохраняемыеДанные", attribute.stored_data, "Boolean", "logform.xsd:Property@storedData"));
            object.properties.push_back(make_platform_object_property(
                "Type", "Тип", attribute.type_pattern, "TypeDomainPattern", "runtime property block TypeDomainPattern"));
            add_api_surface(object, api_object_for_type("FormAttribute"));
            form_object.attributes.add(std::move(object));
        }
    }

    static void materialize_commands(
        oof::platform::object_model::PlatformFormObject& form_object,
        const MaterializedGraphSummary& summary
    ) {
        for (const auto& command : summary.commands) {
            oof::platform::object_model::PlatformObject object;
            object.object_id = command.object_id;
            object.name = command.name;
            object.platform_type = "FormCommand";
            object.type_category = "core::kLogFormTypeInfoCategory";
            object.type_source = "PlatformObjectTypeHandler<Form>.Commands + mngcore logform.xsd Command + cmi.xsd CommandInfo";
            object.path = command.path;
            object.parent_object_id = "0";
            object.properties.push_back(make_platform_object_property(
                "ID", "Идентификатор", command.id, "CompositeID", "logform.xsd:Command/id"));
            object.properties.push_back(make_platform_object_property(
                "Name", "Имя", command.name, "String", "logform.xsd:Command@name + cmi.xsd CommandInfo@name"));
            object.properties.push_back(make_platform_object_property(
                "Handler", "Обработчик", command.handler, "String", "logform.xsd:Command@handler + cmi.xsd HandlerInfo/name"));
            object.properties.push_back(make_platform_object_property(
                "ModifiesData", "ИзменяетДанные", command.modifies_data, "Boolean", "logform.xsd:Command@modifiesData + cmi.xsd HandlerInfo/modifiesData"));
            add_api_surface(object, api_object_for_type("FormCommand"));
            form_object.commands.add(std::move(object));
        }
    }

    static void materialize_items(
        oof::platform::object_model::PlatformFormObject& form_object,
        const MaterializedGraphSummary& summary
    ) {
        for (const auto& item : summary.items) {
            oof::platform::object_model::PlatformObject object;
            object.object_id = item.object_id;
            object.name = item.name;
            object.platform_type = std::string(item.descriptor_binding->platform_type);
            object.type_category = "core::kLogFormTypeInfoCategory";
            object.type_source = "PlatformObjectTypeHandler<Form>.Items + " + std::string(item.descriptor_binding->evidence);
            object.path = item.path;
            object.parent_object_id = item.parent_object_id;
            object.properties.push_back(make_described_property("ObjectID", item.object_id));
            object.properties.push_back(make_described_property("Name", item.name));
            object.properties.push_back(make_described_property("Type", object.platform_type));
            object.properties.push_back(make_described_property("Parent", item.parent_object_id));
            object.properties.push_back(make_described_property("Path", item.path));
            if (!item.title.empty()) {
                object.properties.push_back(make_described_property("Title", item.title));
            }
            if (!item.visible.empty()) {
                object.properties.push_back(make_described_property("Visible", item.visible));
            }
            if (!item.enabled.empty()) {
                object.properties.push_back(make_described_property("Enabled", item.enabled));
            }
            object.properties.push_back(make_described_property("Events", std::to_string(item.events.size())));
            object.collections.push_back(make_described_collection("Events", item.events.size()));
            if (!item.left.empty()) {
                object.properties.push_back(make_described_property("Left", item.left));
                object.properties.push_back(make_described_property("Top", item.top));
                object.properties.push_back(make_described_property("Width", std::to_string(std::stoll(item.right) - std::stoll(item.left))));
                object.properties.push_back(make_described_property("Height", std::to_string(std::stoll(item.bottom) - std::stoll(item.top))));
                object.properties.push_back(make_described_property("Right", item.right));
                object.properties.push_back(make_described_property("Bottom", item.bottom));
                for (const auto& binding : item.bindings) {
                    object.properties.push_back(make_described_property("Binding." + binding.name, oof::platform::stream::dump_compact(binding.value)));
                }
                for (const auto& binding : item.dimension_bindings) {
                    object.properties.push_back(make_described_property("DimensionBinding." + binding.name, oof::platform::stream::dump_compact(binding.value)));
                }
            }
            if (const auto* schema = oof::platform::form_schema::control_by_type_name(object.platform_type)) {
                add_platform_object_schema_surface(
                    object,
                    oof::platform::object_schema::build_schema_for_control(*schema));
            } else {
                add_api_surface(object, api_object_for_type(object.platform_type));
            }
            if (!item.picture_payload.empty()) {
                if (auto* picture = object.property("Picture")) {
                    picture->value = item.picture_payload;
                    picture->value_origin = "stream";
                    enrich_platform_value_object(*picture);
                } else {
                    object.properties.push_back(make_described_property("Picture", item.picture_payload));
                }
            }
            form_object.items.add(std::move(object));
        }
    }

    static void materialize_events(
        oof::platform::object_model::PlatformFormObject& form_object,
        const MaterializedGraphSummary& summary
    ) {
        for (const auto& event : summary.events) {
            oof::platform::object_model::PlatformObject object;
            object.object_id = event.object_id;
            object.name = event.handler;
            object.platform_type = "FormEvent";
            object.type_category = "core::kLogFormTypeInfoCategory";
            object.type_source = "PlatformObjectTypeHandler<Form>.Items.Events + mngcore logform.xsd Event";
            object.path = event.path;
            object.parent_object_id = event.owner_object_id;
            object.properties.push_back(make_platform_object_property(
                "ID", "Идентификатор", event.id, "UUID", "logform.xsd:Event/id"));
            object.properties.push_back(make_platform_object_property(
                "Handler", "Обработчик", event.handler, "String", "logform.xsd:Event@handler"));
            object.properties.push_back(make_platform_object_property(
                "Parent", "Родитель", event.owner_object_id, "FormItem", "m_elementEvents owner"));
            add_api_surface(object, api_object_for_type("FormEvent"));
            form_object.events.add(std::move(object));
        }
    }

    static void link_item_children(oof::platform::object_model::PlatformFormObject& form_object) {
        for (std::size_t index = 0; index < form_object.items.objects().size(); ++index) {
            const auto& object = form_object.items.objects()[index];
            if (object.parent_object_id.empty()) {
                form_object.form.children.push_back(index);
                continue;
            }
            for (auto& maybe_parent : form_object.items.mutable_objects()) {
                if (maybe_parent.object_id == object.parent_object_id) {
                    maybe_parent.children.push_back(index);
                    break;
                }
            }
        }
    }
};

const PlatformObjectTypeHandler& platform_object_type_handler_for(std::string_view platform_type) {
    static const FormPlatformObjectTypeHandler form_handler;
    if (platform_type == form_handler.platform_type()) {
        return form_handler;
    }
    throw std::runtime_error("platform object type handler is not registered: " + std::string(platform_type));
}

oof::platform::object_model::PlatformFormObject materialize_platform_form_object(
    const RuntimeFormEnvelope& envelope
) {
    return platform_object_type_handler_for("Form").materialize_form(envelope);
}

void print_platform_object_json(const oof::platform::object_model::PlatformObject& object) {
    std::cout << "{\"objectId\":";
    print_json_string(object.object_id);
    std::cout << ",\"name\":";
    print_json_string(object.name);
    std::cout << ",\"platformType\":";
    print_json_string(object.platform_type);
    std::cout << ",\"typeCategory\":";
    print_json_string(object.type_category);
    std::cout << ",\"typeSource\":";
    print_json_string(object.type_source);
    std::cout << ",\"path\":";
    print_json_string(object.path);
    std::cout << ",\"parentObjectId\":";
    print_json_string(object.parent_object_id);
    std::cout << ",\"properties\":[";
    for (std::size_t index = 0; index < object.properties.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& prop = object.properties[index];
        std::cout << "{\"name\":";
        print_json_string(prop.name);
        std::cout << ",\"localizedName\":";
        print_json_string(prop.localized_name);
        std::cout << ",\"valueType\":";
        print_json_string(prop.value_type);
        std::cout << ",\"value\":";
        print_json_string(prop.value);
        std::cout << ",\"defaultValue\":";
        print_json_string(prop.default_value);
        std::cout << ",\"writePolicy\":";
        print_json_string(prop.write_policy);
        std::cout << ",\"valueOrigin\":";
        print_json_string(prop.value_origin);
        std::cout << ",\"source\":";
        print_json_string(prop.source);
        std::cout << ",\"platformMember\":";
        print_json_string(prop.platform_member);
        std::cout << ",\"platformDefault\":";
        print_json_string(prop.platform_default);
        std::cout << ",\"slotBinding\":";
        print_json_string(prop.slot_binding);
        std::cout << ",\"slotCodec\":";
        print_json_string(prop.slot_codec);
        if (!prop.value_object_class.empty()) {
            std::cout << ",\"valueObject\":{\"class\":";
            print_json_string(prop.value_object_class);
            std::cout << ",\"constructor\":";
            print_json_string(prop.value_object_constructor);
            std::cout << ",\"storage\":";
            print_json_string(prop.value_object_storage);
            std::cout << ",\"literal\":";
            print_json_string(prop.value_object_literal);
            std::cout << ",\"schemaValue\":";
            print_json_string(prop.value_object_schema_value);
            std::cout << ",\"listStream\":";
            print_json_string(prop.value_object_list_stream);
            std::cout << ",\"ownerMember\":";
            print_json_string(prop.value_object_owner_member);
            std::cout << ",\"evidence\":";
            print_json_string(prop.value_object_evidence);
            std::cout << "}";
        }
        std::cout << ",\"readable\":"
                  << (prop.readable ? "true" : "false");
        std::cout << ",\"writable\":"
                  << (prop.writable ? "true" : "false");
        std::cout << "}";
    }
    std::cout << "],\"collections\":[";
    for (std::size_t index = 0; index < object.collections.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& collection = object.collections[index];
        std::cout << "{\"name\":";
        print_json_string(collection.name);
        std::cout << ",\"localizedName\":";
        print_json_string(collection.localized_name);
        std::cout << ",\"valueType\":";
        print_json_string(collection.value_type);
        std::cout << ",\"count\":" << collection.count;
        std::cout << ",\"source\":";
        print_json_string(collection.source);
        std::cout << ",\"slotBinding\":";
        print_json_string(collection.slot_binding);
        std::cout << ",\"slotCodec\":";
        print_json_string(collection.slot_codec);
        std::cout << ",\"readable\":"
                  << (collection.readable ? "true" : "false");
        std::cout << ",\"writable\":"
                  << (collection.writable ? "true" : "false");
        std::cout << "}";
    }
    std::cout << "],\"methods\":[";
    for (std::size_t index = 0; index < object.methods.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        std::cout << "{\"name\":";
        print_json_string(object.methods[index].name);
        std::cout << ",\"localizedName\":";
        print_json_string(object.methods[index].localized_name);
        std::cout << "}";
    }
    std::cout << "],\"events\":[";
    for (std::size_t index = 0; index < object.events.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        std::cout << "{\"name\":";
        print_json_string(object.events[index].name);
        std::cout << ",\"localizedName\":";
        print_json_string(object.events[index].localized_name);
        std::cout << "}";
    }
    std::cout << "],\"descriptorCoverageGaps\":[";
    for (std::size_t index = 0; index < object.coverage_gaps.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& gap = object.coverage_gaps[index];
        std::cout << "{\"name\":";
        print_json_string(gap.name);
        std::cout << ",\"source\":";
        print_json_string(gap.source);
        std::cout << ",\"reason\":";
        print_json_string(gap.reason);
        std::cout << "}";
    }
    std::cout << "],\"children\":[";
    for (std::size_t index = 0; index < object.children.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        std::cout << object.children[index];
    }
    std::cout << "]}";
}

void print_platform_object_collection_json(
    const oof::platform::object_model::PlatformObjectCollection& collection,
    const oof::platform::object_model::PlatformObjectCollectionDescriptor* descriptor = nullptr
) {
    std::cout << "{";
    if (descriptor != nullptr) {
        std::cout << "\"name\":";
        print_json_string(descriptor->name);
        std::cout << ",\"localizedName\":";
        print_json_string(descriptor->localized_name);
        std::cout << ",\"valueType\":";
        print_json_string(descriptor->value_type);
        std::cout << ",\"source\":";
        print_json_string(descriptor->source);
        std::cout << ",\"slotBinding\":";
        print_json_string(descriptor->slot_binding);
        std::cout << ",\"slotCodec\":";
        print_json_string(descriptor->slot_codec);
        std::cout << ",\"readable\":" << (descriptor->readable ? "true" : "false");
        std::cout << ",\"writable\":" << (descriptor->writable ? "true" : "false");
        std::cout << ",";
    }
    std::cout << "\"count\":" << collection.count();
    std::cout << ",\"methods\":[\"Count\",\"Find\",\"Get\",\"IndexOf\"]";
    std::cout << ",\"objects\":[";
    for (std::size_t index = 0; index < collection.count(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_platform_object_json(collection.get(index));
    }
    std::cout << "]}";
}

void print_platform_form_object_document(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view source,
    std::string_view runtime_uuid
) {
    const auto* first = form_object.items.count() == 0 ? nullptr : &form_object.items.get(0);
    const auto* found = first == nullptr ? nullptr : form_object.items.find(first->name);

    std::cout << "{\"source\":";
    print_json_string(source);
    if (!runtime_uuid.empty()) {
        std::cout << ",\"runtimeUuid\":";
        print_json_string(runtime_uuid);
    }
    std::cout << ",\"contextContract\":{\"platformEvidence\":\"core85 exports IContextDef/GroupContext/IContextExtImplBase getNProps,getPropName,findProp,isPropReadable,isPropWritable,getPropVal,setPropVal,call; mngcore logform.xsd declares Form/elements/command/property and element event/commands/autoCommandBar; cmi.xsd declares CommandInfo/Command/HandlerInfo\",\"model\":\"typeDescriptor + property/method/event/collection descriptors + slot-backed values\",\"descriptorRegistry\":\"PlatformPropertyDescriptor + PropertySlotBinding\",\"implementedWritableSlotCodecs\":[\"name-record\",\"scalar-flag\",\"position-record\",\"binding-record\",\"attribute-record\",\"command-record\",\"event-action-record\"]}";
    std::cout << ",\"form\":";
    print_platform_object_json(form_object.form);
    std::cout << ",\"items\":{\"count\":" << form_object.collection("Items").count();
    std::cout << ",\"methods\":[\"Count\",\"Find\",\"Get\",\"IndexOf\"]";
    if (first != nullptr) {
        std::cout << ",\"get0\":{\"name\":";
        print_json_string(first->name);
        std::cout << ",\"platformType\":";
        print_json_string(first->platform_type);
        std::cout << "}";
        std::cout << ",\"findFirstName\":"
                  << (found != nullptr ? "true" : "false");
    }
    std::cout << ",\"objects\":[";
    for (std::size_t index = 0; index < form_object.items.count(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_platform_object_json(form_object.items.get(index));
    }
    std::cout << "]}";
    std::cout << ",\"attributes\":";
    print_platform_object_collection_json(
        form_object.collection("Attributes"),
        form_object.form.collection_descriptor("Attributes"));
    std::cout << ",\"commands\":";
    print_platform_object_collection_json(
        form_object.collection("Commands"),
        form_object.form.collection_descriptor("Commands"));
    std::cout << ",\"events\":";
    print_platform_object_collection_json(
        form_object.collection("Events"),
        form_object.form.collection_descriptor("Events"));
    std::cout << "}\n";
}

void print_runtime_platform_object(const std::string& path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(path, canonical_text);
    print_platform_form_object_document(
        materialize_platform_form_object(envelope),
        "RuntimeForm:PlatformObject",
        envelope.runtime_uuid);
}

void print_platform_object_get_json(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view source,
    std::string_view object_id,
    std::string_view property_name
) {
    const auto* object = form_object.find_object_by_id(object_id);
    if (object == nullptr) {
        throw std::runtime_error("platform object is not found: " + std::string(object_id));
    }
    const auto* property = object->property(property_name);
    if (property == nullptr) {
        throw std::runtime_error("platform object property is not found: " + std::string(property_name));
    }

    std::cout << "{\"operation\":\"getPropVal\"";
    std::cout << ",\"source\":";
    print_json_string(source);
    std::cout << ",\"objectId\":";
    print_json_string(object_id);
    std::cout << ",\"objectName\":";
    print_json_string(object->name);
    std::cout << ",\"platformType\":";
    print_json_string(object->platform_type);
    std::cout << ",\"property\":";
    print_json_string(property_name);
    std::cout << ",\"descriptorName\":";
    print_json_string(property->name);
    std::cout << ",\"localizedName\":";
    print_json_string(property->localized_name);
    std::cout << ",\"valueType\":";
    print_json_string(property->value_type);
    std::cout << ",\"value\":";
    print_json_string(form_object.get_prop_val(object_id, property_name));
    std::cout << ",\"defaultValue\":";
    print_json_string(property->default_value);
    std::cout << ",\"writePolicy\":";
    print_json_string(property->write_policy);
    std::cout << ",\"valueOrigin\":";
    print_json_string(property->value_origin);
    std::cout << ",\"platformMember\":";
    print_json_string(property->platform_member);
    std::cout << ",\"platformDefault\":";
    print_json_string(property->platform_default);
    std::cout << ",\"readable\":" << (property->readable ? "true" : "false");
    std::cout << ",\"writable\":" << (property->writable ? "true" : "false");
    std::cout << ",\"slotBinding\":";
    print_json_string(property->slot_binding);
    std::cout << ",\"slotCodec\":";
    print_json_string(property->slot_codec);
    if (!property->value_object_class.empty()) {
        std::cout << ",\"valueObject\":{\"class\":";
        print_json_string(property->value_object_class);
        std::cout << ",\"constructor\":";
        print_json_string(property->value_object_constructor);
        std::cout << ",\"storage\":";
        print_json_string(property->value_object_storage);
        std::cout << ",\"literal\":";
        print_json_string(property->value_object_literal);
        std::cout << ",\"schemaValue\":";
        print_json_string(property->value_object_schema_value);
        std::cout << ",\"listStream\":";
        print_json_string(property->value_object_list_stream);
        std::cout << ",\"ownerMember\":";
        print_json_string(property->value_object_owner_member);
        std::cout << ",\"evidence\":";
        print_json_string(property->value_object_evidence);
        std::cout << "}";
    }
    std::cout << "}\n";
}

void print_runtime_platform_object_get(
    const std::string& path,
    std::string_view object_id,
    std::string_view property_name
) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(path, canonical_text);
    print_platform_object_get_json(
        materialize_platform_form_object(envelope),
        "RuntimeForm:PlatformObject",
        object_id,
        property_name);
}

std::string xml_escape(std::string_view value) {
    std::string out;
    for (const char ch : value) {
        switch (ch) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    return out;
}

std::string xml_unescape(std::string value) {
    const std::vector<std::pair<std::string, std::string>> entities{
        {"&quot;", "\""},
        {"&apos;", "'"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&amp;", "&"},
    };
    for (const auto& [entity, replacement] : entities) {
        std::size_t pos = 0;
        while ((pos = value.find(entity, pos)) != std::string::npos) {
            value.replace(pos, entity.size(), replacement);
            pos += replacement.size();
        }
    }
    return value;
}

const char* base64_alphabet() {
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64_encode(const std::vector<std::uint8_t>& data) {
    std::string out;
    const char* alphabet = base64_alphabet();
    std::size_t index = 0;
    while (index + 2 < data.size()) {
        const std::uint32_t triple =
            (static_cast<std::uint32_t>(data[index]) << 16) |
            (static_cast<std::uint32_t>(data[index + 1]) << 8) |
            static_cast<std::uint32_t>(data[index + 2]);
        out.push_back(alphabet[(triple >> 18) & 0x3f]);
        out.push_back(alphabet[(triple >> 12) & 0x3f]);
        out.push_back(alphabet[(triple >> 6) & 0x3f]);
        out.push_back(alphabet[triple & 0x3f]);
        index += 3;
    }
    if (index < data.size()) {
        std::uint32_t triple = static_cast<std::uint32_t>(data[index]) << 16;
        out.push_back(alphabet[(triple >> 18) & 0x3f]);
        if (index + 1 < data.size()) {
            triple |= static_cast<std::uint32_t>(data[index + 1]) << 8;
            out.push_back(alphabet[(triple >> 12) & 0x3f]);
            out.push_back(alphabet[(triple >> 6) & 0x3f]);
            out.push_back('=');
        } else {
            out.push_back(alphabet[(triple >> 12) & 0x3f]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

int base64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return 26 + ch - 'a';
    }
    if (ch >= '0' && ch <= '9') {
        return 52 + ch - '0';
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

std::vector<std::uint8_t> base64_decode(std::string_view text) {
    std::vector<std::uint8_t> out;
    int buffer = 0;
    int bits = -8;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        if (ch == '=') {
            break;
        }
        const int value = base64_value(ch);
        if (value < 0) {
            throw std::runtime_error("invalid base64 picture payload");
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

std::string base64_payload_body(std::string_view payload) {
    std::string body;
    if (payload.rfind("#base64:", 0) == 0) {
        payload.remove_prefix(std::string_view("#base64:").size());
    }
    for (const char ch : payload) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            body.push_back(ch);
        }
    }
    return body;
}

std::vector<std::uint8_t> decode_picture_payload(std::string_view payload) {
    return base64_decode(base64_payload_body(payload));
}

std::string wrap_base64_picture_payload(const std::vector<std::uint8_t>& data) {
    const std::string encoded = base64_encode(data);
    std::string out = "#base64:";
    for (std::size_t index = 0; index < encoded.size(); index += 64) {
        if (index != 0) {
            out += "\r\r\n";
        }
        out += encoded.substr(index, 64);
    }
    return out;
}

std::string picture_extension_for_bytes(const std::vector<std::uint8_t>& data) {
    if (data.size() >= 6 &&
        data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
        data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a') {
        return "gif";
    }
    if (data.size() >= 8 &&
        data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G' &&
        data[4] == '\r' && data[5] == '\n' && data[6] == 0x1a && data[7] == '\n') {
        return "png";
    }
    if (data.size() >= 2 && data[0] == 0xff && data[1] == 0xd8) {
        return "jpg";
    }
    return "bin";
}

bool set_materialized_object_picture_payload(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    std::string_view payload
);

std::string public_xml_tag_for_platform_type(std::string_view platform_type) {
    if (platform_type == "TextBox") {
        return "InputField";
    }
    if (platform_type == "TableBox") {
        return "Table";
    }
    if (platform_type == "Image") {
        return "PictureDecoration";
    }
    if (platform_type == "Label") {
        return "LabelDecoration";
    }
    if (platform_type == "GroupBox") {
        return "UsualGroup";
    }
    if (platform_type == "Spreadsheet") {
        return "SpreadsheetDocumentField";
    }
    if (platform_type == "TextDocument") {
        return "TextDocumentField";
    }
    if (platform_type == "FormattedDocument") {
        return "FormattedDocumentField";
    }
    if (platform_type == "Calendar") {
        return "CalendarField";
    }
    if (platform_type == "TrackBar") {
        return "TrackBar";
    }
    if (platform_type == "PanelPage") {
        return "Page";
    }
    return std::string(platform_type);
}

std::string platform_type_for_public_xml_tag(std::string_view tag) {
    if (tag == "InputField") {
        return "TextBox";
    }
    if (tag == "Table") {
        return "TableBox";
    }
    if (tag == "PictureDecoration") {
        return "Image";
    }
    if (tag == "LabelDecoration") {
        return "Label";
    }
    if (tag == "UsualGroup") {
        return "GroupBox";
    }
    if (tag == "SpreadsheetDocumentField") {
        return "Spreadsheet";
    }
    if (tag == "TextDocumentField") {
        return "TextDocument";
    }
    if (tag == "FormattedDocumentField") {
        return "FormattedDocument";
    }
    if (tag == "CalendarField") {
        return "Calendar";
    }
    if (tag == "Separator") {
        return "Splitter";
    }
    if (tag == "GeographicalMap") {
        return "GeographicalSchemaField";
    }
    if (tag == "Flowchart") {
        return "GraphicalSchemaField";
    }
    if (tag == "Page") {
        return "PanelPage";
    }
    return std::string(tag);
}

struct PublicXmlControlEdit {
    std::string tag;
    std::string object_id;
    std::string name;
    bool has_name = false;
    std::string title;
    bool has_title = false;
    std::string visible;
    bool has_visible = false;
    std::string enabled;
    bool has_enabled = false;
    std::string left;
    std::string top;
    std::string right;
    std::string bottom;
    bool has_position = false;
    std::vector<oof::platform::object_model::PlatformObjectPropertyEdit> schema_properties;
    std::vector<GeometryBindingRecord> bindings;
    std::vector<GeometryBindingRecord> dimension_bindings;
};

struct PublicXmlApplyResult {
    std::size_t controls = 0;
    std::size_t name_edits = 0;
    std::size_t title_edits = 0;
    std::size_t scalar_flag_edits = 0;
    std::size_t position_edits = 0;
    std::size_t binding_edits = 0;
    std::size_t dimension_binding_edits = 0;
    std::size_t attribute_edits = 0;
    std::size_t command_edits = 0;
    std::size_t event_edits = 0;
    std::size_t deleted_controls = 0;
};

PublicXmlApplyResult apply_public_xml_edits(
    RuntimeFormEnvelope& envelope,
    const std::vector<PublicXmlControlEdit>& edits
);

oof::platform::object_model::PlatformFormObjectEdit public_xml_edits_to_platform_object_edits(
    const std::vector<PublicXmlControlEdit>& edits
);

oof::platform::object_model::PlatformFormObjectEdit parse_public_xml_collection_edits(
    const std::string& xml
);

oof::platform::object_model::PlatformFormObjectEdit parse_public_xml_platform_object_edits(
    const std::string& xml
);

oof::platform::object_model::PlatformFormObjectEdit keep_changed_platform_object_edits(
    const oof::platform::object_model::PlatformFormObjectEdit& requested,
    const oof::platform::object_model::PlatformFormObject& baseline
);

PublicXmlApplyResult apply_platform_object_edits(
    RuntimeFormEnvelope& envelope,
    const oof::platform::object_model::PlatformFormObjectEdit& object_edit
);

bool set_property_slot_value(
    oof::platform::stream::ListValue& payload,
    std::string_view object_id,
    const oof::platform::property_registry::PlatformPropertyDescriptor& descriptor,
    std::string_view new_value
);

std::set<std::string> requested_public_control_ids(const std::string& xml);

std::size_t delete_controls_missing_from_public_xml(
    oof::platform::stream::ListValue& payload,
    const std::set<std::string>& requested_ids
);

bool string_view_starts_with(std::string_view value, std::string_view prefix);

const oof::platform::property_registry::PlatformPropertyDescriptor& require_property_descriptor(
    std::string_view property_name
);

std::string xml_attr_value(std::string_view attrs, std::string_view name) {
    const std::regex attr_pattern(std::string(name) + "\\s*=\\s*\"([^\"]*)\"");
    std::cmatch match;
    const std::string attr_text(attrs);
    if (!std::regex_search(attr_text.c_str(), match, attr_pattern)) {
        return {};
    }
    return xml_unescape(match[1].str());
}

struct XmlElementSlice {
    std::string attrs;
    std::string body;
    bool self_closing = false;
};

std::vector<XmlElementSlice> find_xml_elements(std::string_view text, std::string_view tag) {
    std::vector<XmlElementSlice> elements;
    std::size_t cursor = 0;
    const std::string open = "<" + std::string(tag);
    const std::string close = "</" + std::string(tag) + ">";
    while (cursor < text.size()) {
        const std::size_t start = text.find(open, cursor);
        if (start == std::string::npos) {
            break;
        }
        const std::size_t name_end = start + open.size();
        if (name_end < text.size()) {
            const char after_name = text[name_end];
            if (std::isalnum(static_cast<unsigned char>(after_name)) || after_name == '_' || after_name == '-') {
                cursor = name_end;
                continue;
            }
        }
        const std::size_t tag_end = text.find(">", start);
        if (tag_end == std::string::npos) {
            break;
        }
        XmlElementSlice element;
        element.attrs = std::string(text.substr(start + open.size(), tag_end - start - open.size()));
        std::size_t attr_end = element.attrs.find_last_not_of(" \t\r\n");
        element.self_closing = attr_end != std::string::npos && element.attrs[attr_end] == '/';
        if (element.self_closing) {
            element.attrs = element.attrs.substr(0, attr_end);
            cursor = tag_end + 1;
        } else {
            const std::size_t close_start = text.find(close, tag_end + 1);
            if (close_start == std::string::npos) {
                break;
            }
            element.body = std::string(text.substr(tag_end + 1, close_start - tag_end - 1));
            cursor = close_start + close.size();
        }
        elements.push_back(std::move(element));
    }
    return elements;
}

std::string public_value_object_xml_literal(std::string_view property_body) {
    for (const std::string& value_tag : {"PictureValue", "ColorValue", "FontValue", "BorderValue"}) {
        for (const auto& value_xml : find_xml_elements(property_body, value_tag)) {
            if (!value_xml.self_closing) {
                return xml_unescape(value_xml.body);
            }
            const std::string literal = xml_attr_value(value_xml.attrs, "literal");
            if (!literal.empty()) {
                return literal;
            }
            const std::string value = xml_attr_value(value_xml.attrs, "value");
            if (!value.empty()) {
                return value;
            }
        }
    }
    return xml_unescape(std::string(property_body));
}

const std::set<std::string>& public_schema_property_names() {
    static const std::set<std::string> names = [] {
        std::set<std::string> result;
        static const std::set<std::string> structural_names{
            "Title", "Visible", "Enabled", "Position", "Events", "ChildItems",
            "Attributes", "Commands", "Name", "Type", "Parent", "Path", "ObjectID",
        };
        for (const auto& schema : oof::platform::object_schema::build_platform_object_schemas()) {
            for (const auto& member : schema.xsd_members) {
                if (member.value_type == "ordinary form child controls" ||
                    member.value_type == "Page" ||
                    member.value_type == "Command" ||
                    member.value_type == "Submenu" ||
                    member.value_type == "MenuSeparator" ||
                    member.value_type == "TableColumn" ||
                    member.value_type == "TableColumnsGroup") {
                    continue;
                }
                if (!member.name.empty() &&
                    member.name.find('.') == std::string::npos &&
                    structural_names.count(member.name) == 0) {
                    result.insert(member.name);
                }
            }
        }
        result.insert("Picture");
        result.insert("TextColor");
        result.insert("BackColor");
        result.insert("BorderColor");
        result.insert("Font");
        return result;
    }();
    return names;
}

std::string anchor_target_id_from_attrs(std::string_view attrs) {
    const std::string target = xml_attr_value(attrs, "target");
    const std::string target_id = xml_attr_value(attrs, "targetId");
    if (target == "none") {
        return "-1";
    }
    if (target == "parent") {
        return "0";
    }
    if (target == "self") {
        return target_id.empty() ? "0" : target_id;
    }
    return target_id.empty() ? "-1" : target_id;
}

oof::platform::stream::ListValue anchor_record_from_xml_attrs(std::string_view attrs) {
    const std::string value = xml_attr_value(attrs, "value");
    if (!value.empty()) {
        return oof::platform::stream::ListValue::raw_atom(value);
    }
    std::vector<oof::platform::stream::ListValue> items;
    items.push_back(oof::platform::stream::ListValue::raw_atom(anchor_relation_code(xml_attr_value(attrs, "relation"))));
    items.push_back(oof::platform::stream::ListValue::raw_atom(anchor_target_id_from_attrs(attrs)));
    items.push_back(oof::platform::stream::ListValue::raw_atom(anchor_side_code(xml_attr_value(attrs, "side"))));
    const std::string offset = xml_attr_value(attrs, "offset");
    items.push_back(oof::platform::stream::ListValue::raw_atom(offset.empty() ? "0" : offset));
    return oof::platform::stream::ListValue::list(std::move(items));
}

std::vector<oof::platform::stream::ListValue> anchor_children_from_xml_body(std::string_view body) {
    struct FoundAnchor {
        std::size_t pos = 0;
        std::string attrs;
    };
    std::vector<FoundAnchor> found;
    for (const std::string& tag : {"From", "To", "Extra"}) {
        std::size_t cursor = 0;
        const std::string open = "<" + tag;
        while (cursor < body.size()) {
            const std::size_t start = body.find(open, cursor);
            if (start == std::string::npos) {
                break;
            }
            const std::size_t end = body.find("/>", start);
            if (end == std::string::npos) {
                break;
            }
            found.push_back({start, std::string(body.substr(start + open.size(), end - start - open.size()))});
            cursor = end + 2;
        }
    }
    std::sort(found.begin(), found.end(), [](const FoundAnchor& lhs, const FoundAnchor& rhs) {
        return lhs.pos < rhs.pos;
    });
    std::vector<oof::platform::stream::ListValue> anchors;
    for (const auto& item : found) {
        anchors.push_back(anchor_record_from_xml_attrs(item.attrs));
    }
    return anchors;
}

std::vector<PublicXmlControlEdit> parse_public_xml_control_edits(const std::string& xml) {
    if (xml.find("<ListStream") != std::string::npos || xml.find("<RawBracket") != std::string::npos ||
        xml.find("<PlatformRecords") != std::string::npos || xml.find("<FormBin") != std::string::npos) {
        throw std::runtime_error("OrdinaryForm XML must not contain raw/list-stream fallback nodes");
    }
    if (xml.find("ordinaryFormVersion=\"2.") == std::string::npos) {
        throw std::runtime_error("expected OrdinaryForm XML with ordinaryFormVersion=\"2.*\"");
    }

    const std::set<std::string> section_tags{
        "Form", "Events", "Event", "ChildItems", "Attributes", "Attribute", "Commands", "Command",
        "Title", "Position", "Pages"
    };
    const std::regex start_tag_pattern(R"(<([A-Za-z][A-Za-z0-9]*)\b([^>]*)>)");
    std::vector<PublicXmlControlEdit> edits;
    for (std::sregex_iterator it(xml.begin(), xml.end(), start_tag_pattern), end; it != end; ++it) {
        const std::string tag = (*it)[1].str();
        if (section_tags.count(tag) != 0) {
            continue;
        }
        const std::string attrs = (*it)[2].str();
        const std::string object_id = xml_attr_value(attrs, "id");
        if (object_id.empty()) {
            continue;
        }
        PublicXmlControlEdit edit;
        edit.tag = tag;
        edit.object_id = object_id;
        edit.name = xml_attr_value(attrs, "name");
        edit.has_name = attrs.find("name") != std::string::npos;

        const std::size_t body_start = static_cast<std::size_t>(it->position() + it->length());
        const std::string close_tag = "</" + tag + ">";
        const std::size_t body_end = xml.find(close_tag, body_start);
        if (body_end != std::string::npos) {
            const std::string body = xml.substr(body_start, body_end - body_start);
            const std::size_t title_start = body.find("<Title>");
            if (title_start != std::string::npos) {
                const std::size_t title_value_start = title_start + std::string("<Title>").size();
                const std::size_t title_end = body.find("</Title>", title_value_start);
                if (title_end != std::string::npos) {
                    edit.title = xml_unescape(body.substr(title_value_start, title_end - title_value_start));
                    edit.has_title = true;
                }
            }
            const std::size_t visible_start = body.find("<Visible>");
            if (visible_start != std::string::npos) {
                const std::size_t visible_value_start = visible_start + std::string("<Visible>").size();
                const std::size_t visible_end = body.find("</Visible>", visible_value_start);
                if (visible_end != std::string::npos) {
                    edit.visible = xml_unescape(body.substr(visible_value_start, visible_end - visible_value_start));
                    edit.has_visible = true;
                }
            }
            const std::size_t enabled_start = body.find("<Enabled>");
            if (enabled_start != std::string::npos) {
                const std::size_t enabled_value_start = enabled_start + std::string("<Enabled>").size();
                const std::size_t enabled_end = body.find("</Enabled>", enabled_value_start);
                if (enabled_end != std::string::npos) {
                    edit.enabled = xml_unescape(body.substr(enabled_value_start, enabled_end - enabled_value_start));
                    edit.has_enabled = true;
                }
            }
            for (const auto& property_name : public_schema_property_names()) {
                for (const auto& property_xml : find_xml_elements(body, property_name)) {
                    if (property_xml.self_closing) {
                        continue;
                    }
                    edit.schema_properties.push_back({
                        property_name,
                        public_value_object_xml_literal(property_xml.body),
                    });
                }
            }
            const std::size_t position_start = body.find("<Position");
            if (position_start != std::string::npos) {
                const std::size_t position_end = body.find(">", position_start);
                if (position_end != std::string::npos) {
                    const std::string position_tag = body.substr(position_start, position_end - position_start + 1);
                    edit.left = xml_attr_value(position_tag, "left");
                    edit.top = xml_attr_value(position_tag, "top");
                    edit.right = xml_attr_value(position_tag, "right");
                    edit.bottom = xml_attr_value(position_tag, "bottom");
                    edit.has_position = !edit.left.empty() &&
                                        !edit.top.empty() &&
                                        !edit.right.empty() &&
                                        !edit.bottom.empty();
                    const std::size_t position_close = body.find("</Position>", position_end);
                    if (position_close != std::string::npos) {
                        const std::string position_body = body.substr(position_end + 1, position_close - position_end - 1);
                        for (const auto& binding_xml : find_xml_elements(position_body, "Binding")) {
                            const std::string coordinate = xml_attr_value(binding_xml.attrs, "coordinate");
                            const std::string value = xml_attr_value(binding_xml.attrs, "value");
                            if (coordinate.empty()) {
                                continue;
                            }
                            if (!value.empty()) {
                                edit.bindings.push_back({coordinate, oof::platform::stream::ListValue::raw_atom(value)});
                                continue;
                            }
                            std::vector<oof::platform::stream::ListValue> record_items;
                            const std::string mode = xml_attr_value(binding_xml.attrs, "mode");
                            record_items.push_back(oof::platform::stream::ListValue::raw_atom(mode.empty() ? "0" : mode));
                            auto anchors = anchor_children_from_xml_body(binding_xml.body);
                            record_items.insert(record_items.end(), std::make_move_iterator(anchors.begin()), std::make_move_iterator(anchors.end()));
                            edit.bindings.push_back({coordinate, oof::platform::stream::ListValue::list(std::move(record_items))});
                        }
                        for (const auto& binding_xml : find_xml_elements(position_body, "DimensionBinding")) {
                            const std::string dimension = xml_attr_value(binding_xml.attrs, "dimension");
                            const std::string value = xml_attr_value(binding_xml.attrs, "value");
                            if (dimension.empty()) {
                                continue;
                            }
                            if (!value.empty()) {
                                edit.dimension_bindings.push_back({dimension, oof::platform::stream::ListValue::raw_atom(value)});
                                continue;
                            }
                            std::vector<oof::platform::stream::ListValue> record_items;
                            const std::string mode = xml_attr_value(binding_xml.attrs, "mode");
                            record_items.push_back(oof::platform::stream::ListValue::raw_atom(mode.empty() ? "0" : mode));
                            record_items.push_back(oof::platform::stream::ListValue::raw_atom(anchor_target_id_from_attrs(binding_xml.attrs)));
                            record_items.push_back(oof::platform::stream::ListValue::raw_atom(anchor_side_code(xml_attr_value(binding_xml.attrs, "side"))));
                            auto anchors = anchor_children_from_xml_body(binding_xml.body);
                            record_items.insert(record_items.end(), std::make_move_iterator(anchors.begin()), std::make_move_iterator(anchors.end()));
                            edit.dimension_bindings.push_back({dimension, oof::platform::stream::ListValue::list(std::move(record_items))});
                        }
                    }
                }
            }
        }
        edits.push_back(std::move(edit));
    }
    return edits;
}

const std::set<std::string>& public_xml_section_tags() {
    static const std::set<std::string> tags{
        "Form", "Events", "Event", "ChildItems", "Attributes", "Attribute", "Commands", "Command",
        "Title", "Position", "Pages", "Picture", "PictureValue", "Binding", "Bindings", "DimensionBinding",
        "From", "To", "Extra", "Item"
    };
    return tags;
}

std::set<std::string> requested_public_control_ids(const std::string& xml) {
    std::set<std::string> ids;
    const std::regex start_tag_pattern(R"(<([A-Za-z][A-Za-z0-9]*)\b([^>]*)>)");
    for (std::sregex_iterator it(xml.begin(), xml.end(), start_tag_pattern), end; it != end; ++it) {
        const std::string tag = (*it)[1].str();
        if (public_xml_section_tags().count(tag) != 0) {
            continue;
        }
        const std::string id = xml_attr_value((*it)[2].str(), "id");
        if (!id.empty()) {
            ids.insert(id);
        }
    }
    return ids;
}

bool has_nested_materializable_object(const oof::platform::stream::ListValue& value) {
    if (!value.is_list) {
        return false;
    }
    for (const auto& item : value.items) {
        if (is_materializable_object_candidate(item) || has_nested_materializable_object(item)) {
            return true;
        }
    }
    return false;
}

bool counted_child_container(const oof::platform::stream::ListValue& value) {
    if (!value.is_list || value.items.empty() || value.items[0].is_list || !is_int_atom(value.items[0])) {
        return false;
    }
    std::size_t materialized_children = 0;
    for (std::size_t index = 1; index < value.items.size(); ++index) {
        if (is_materializable_object_candidate(value.items[index])) {
            ++materialized_children;
        }
    }
    return materialized_children != 0 &&
           value.items[0].atom == std::to_string(value.items.size() - 1);
}

std::size_t delete_controls_missing_from_public_xml(
    oof::platform::stream::ListValue& payload,
    const std::set<std::string>& requested_ids
) {
    if (!payload.is_list) {
        return 0;
    }

    std::size_t deleted = 0;
    if (counted_child_container(payload)) {
        for (std::size_t index = 1; index < payload.items.size();) {
            auto& item = payload.items[index];
            if (is_materializable_object_candidate(item) &&
                !item.items[1].is_list &&
                requested_ids.count(item.items[1].atom) == 0) {
                if (has_nested_materializable_object(item)) {
                    throw std::runtime_error("cannot delete non-leaf control from public XML yet: object=" +
                                             item.items[1].atom);
                }
                payload.items.erase(payload.items.begin() + static_cast<std::ptrdiff_t>(index));
                payload.items[0].atom = std::to_string(payload.items.size() - 1);
                payload.items[0].atom_kind = oof::platform::stream::ListValue::AtomKind::raw;
                ++deleted;
                continue;
            }
            ++index;
        }
    }

    for (auto& item : payload.items) {
        deleted += delete_controls_missing_from_public_xml(item, requested_ids);
    }
    return deleted;
}

oof::platform::object_model::PlatformFormObjectEdit parse_public_xml_collection_edits(
    const std::string& xml
) {
    oof::platform::object_model::PlatformFormObjectEdit edits;

    for (const auto& attribute_xml : find_xml_elements(xml, "Attribute")) {
        const std::string object_id = xml_attr_value(attribute_xml.attrs, "objectId");
        if (object_id.empty()) {
            continue;
        }
        auto& object = edits.object(object_id, "FormAttribute");
        const std::string name = xml_attr_value(attribute_xml.attrs, "name");
        if (!name.empty()) {
            object.set_property("Name", name);
        }
    }

    for (const auto& command_xml : find_xml_elements(xml, "Command")) {
        const std::string object_id = xml_attr_value(command_xml.attrs, "objectId");
        if (object_id.empty()) {
            continue;
        }
        auto& object = edits.object(object_id, "FormCommand");
        const std::string name = xml_attr_value(command_xml.attrs, "name");
        if (!name.empty()) {
            object.set_property("Name", name);
        }
        const std::string handler = xml_attr_value(command_xml.attrs, "handler");
        if (!handler.empty()) {
            object.set_property("Handler", handler);
        }
        const std::string modifies_data = xml_attr_value(command_xml.attrs, "modifiesData");
        if (!modifies_data.empty()) {
            object.set_property("ModifiesData", modifies_data);
        }
    }

    for (const auto& event_xml : find_xml_elements(xml, "Event")) {
        const std::string object_id = xml_attr_value(event_xml.attrs, "objectId");
        if (object_id.empty()) {
            continue;
        }
        auto& object = edits.object(object_id, "FormEvent");
        const std::string handler = xml_attr_value(event_xml.attrs, "handler");
        if (!handler.empty()) {
            object.set_property("Handler", handler);
        }
    }

    return edits;
}

std::size_t find_matching_xml_close(
    std::string_view text,
    std::string_view tag,
    std::size_t open_tag_end
) {
    const std::string open = "<" + std::string(tag);
    const std::string close = "</" + std::string(tag) + ">";
    std::size_t cursor = open_tag_end;
    std::size_t depth = 1;
    while (cursor < text.size()) {
        const std::size_t next_open = text.find(open, cursor);
        const std::size_t next_close = text.find(close, cursor);
        if (next_close == std::string::npos) {
            return std::string::npos;
        }
        if (next_open != std::string::npos && next_open < next_close) {
            const std::size_t name_end = next_open + open.size();
            if (name_end < text.size()) {
                const char after_name = text[name_end];
                if (std::isalnum(static_cast<unsigned char>(after_name)) || after_name == '_' || after_name == '-') {
                    cursor = name_end;
                    continue;
                }
            }
            const std::size_t nested_tag_end = text.find('>', next_open);
            if (nested_tag_end == std::string::npos) {
                return std::string::npos;
            }
            const std::string nested_attrs(text.substr(next_open + open.size(), nested_tag_end - next_open - open.size()));
            const std::size_t attr_end = nested_attrs.find_last_not_of(" \t\r\n");
            const bool nested_self_closing =
                attr_end != std::string::npos && nested_attrs[attr_end] == '/';
            if (!nested_self_closing) {
                ++depth;
            }
            cursor = nested_tag_end + 1;
            continue;
        }
        --depth;
        if (depth == 0) {
            return next_close;
        }
        cursor = next_close + close.size();
    }
    return std::string::npos;
}

XmlElementSlice first_xml_element(std::string_view text, std::string_view tag) {
    const std::string open = "<" + std::string(tag);
    const std::size_t start = text.find(open);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t name_end = start + open.size();
    if (name_end < text.size()) {
        const char after_name = text[name_end];
        if (std::isalnum(static_cast<unsigned char>(after_name)) || after_name == '_' || after_name == '-') {
            return {};
        }
    }
    const std::size_t tag_end = text.find('>', start);
    if (tag_end == std::string::npos) {
        return {};
    }
    XmlElementSlice element;
    element.attrs = std::string(text.substr(name_end, tag_end - name_end));
    std::size_t attr_end = element.attrs.find_last_not_of(" \t\r\n");
    element.self_closing = attr_end != std::string::npos && element.attrs[attr_end] == '/';
    if (element.self_closing) {
        element.attrs = element.attrs.substr(0, attr_end);
        return element;
    }
    const std::size_t close_start = find_matching_xml_close(text, tag, tag_end + 1);
    if (close_start == std::string::npos) {
        return {};
    }
    element.body = std::string(text.substr(tag_end + 1, close_start - tag_end - 1));
    return element;
}

std::string public_xml_text_content(std::string_view body);

void set_or_add_described_property(
    oof::platform::object_model::PlatformObject& object,
    std::string_view name,
    std::string value
) {
    if (auto* property = object.property(name)) {
        property->value = std::move(value);
        property->value_origin = "public-xml";
        enrich_platform_value_object(*property);
        return;
    }
    auto property = make_described_property(name, std::move(value));
    property.value_origin = "public-xml";
    enrich_platform_value_object(property);
    object.properties.push_back(std::move(property));
}

void add_public_xml_collection_objects(
    oof::platform::object_model::PlatformFormObject& form_object,
    const std::string& xml
) {
    for (const auto& attribute_xml : find_xml_elements(xml, "Attribute")) {
        const std::string object_id = xml_attr_value(attribute_xml.attrs, "objectId");
        if (object_id.empty()) {
            continue;
        }
        oof::platform::object_model::PlatformObject object;
        object.object_id = object_id;
        object.name = xml_attr_value(attribute_xml.attrs, "name");
        object.platform_type = "FormAttribute";
        object.type_category = "core::kLogFormTypeInfoCategory";
        object.type_source = "PublicOrdinaryFormXml.Attributes";
        object.parent_object_id = "0";
        object.properties.push_back(make_platform_object_property("ID", "Идентификатор", xml_attr_value(attribute_xml.attrs, "id"), "CompositeID", "OrdinaryForm.xml Attribute@id", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Name", "Имя", object.name, "String", "OrdinaryForm.xml Attribute@name", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Main", "Основной", xml_attr_value(attribute_xml.attrs, "main"), "Boolean", "OrdinaryForm.xml Attribute@main", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("StoredData", "СохраняемыеДанные", xml_attr_value(attribute_xml.attrs, "storedData"), "Boolean", "OrdinaryForm.xml Attribute@storedData", {}, {}, "public-xml"));
        const auto type_xml = first_xml_element(attribute_xml.body, "Type");
        object.properties.push_back(make_platform_object_property("Type", "Тип", xml_unescape(type_xml.body), "TypeDomainPattern", "OrdinaryForm.xml Attribute/Type", {}, {}, "public-xml"));
        add_api_surface(object, api_object_for_type("FormAttribute"));
        form_object.attributes.add(std::move(object));
    }

    for (const auto& command_xml : find_xml_elements(xml, "Command")) {
        const std::string object_id = xml_attr_value(command_xml.attrs, "objectId");
        if (object_id.empty()) {
            continue;
        }
        oof::platform::object_model::PlatformObject object;
        object.object_id = object_id;
        object.name = xml_attr_value(command_xml.attrs, "name");
        object.platform_type = "FormCommand";
        object.type_category = "core::kLogFormTypeInfoCategory";
        object.type_source = "PublicOrdinaryFormXml.Commands";
        object.parent_object_id = "0";
        object.properties.push_back(make_platform_object_property("ID", "Идентификатор", xml_attr_value(command_xml.attrs, "id"), "CompositeID", "OrdinaryForm.xml Command@id", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Name", "Имя", object.name, "String", "OrdinaryForm.xml Command@name", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Handler", "Обработчик", xml_attr_value(command_xml.attrs, "handler"), "String", "OrdinaryForm.xml Command@handler", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("ModifiesData", "ИзменяетДанные", xml_attr_value(command_xml.attrs, "modifiesData"), "Boolean", "OrdinaryForm.xml Command@modifiesData", {}, {}, "public-xml"));
        add_api_surface(object, api_object_for_type("FormCommand"));
        form_object.commands.add(std::move(object));
    }
}

void add_public_xml_events(
    oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view owner_object_id,
    const std::string& body
) {
    const auto events_xml = first_xml_element(body, "Events");
    if (events_xml.self_closing && events_xml.attrs.empty() && events_xml.body.empty()) {
        return;
    }
    for (const auto& event_xml : find_xml_elements(events_xml.body, "Event")) {
        oof::platform::object_model::PlatformObject object;
        object.object_id = xml_attr_value(event_xml.attrs, "objectId");
        if (object.object_id.empty()) {
            const std::string id = xml_attr_value(event_xml.attrs, "id");
            object.object_id = "event:" + std::string(owner_object_id) + ":" + id;
        }
        object.name = xml_attr_value(event_xml.attrs, "handler");
        object.platform_type = "FormEvent";
        object.type_category = "core::kLogFormTypeInfoCategory";
        object.type_source = "PublicOrdinaryFormXml.Events";
        object.parent_object_id = std::string(owner_object_id);
        object.properties.push_back(make_platform_object_property("ID", "Идентификатор", xml_attr_value(event_xml.attrs, "id"), "UUID", "OrdinaryForm.xml Event@id", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Handler", "Обработчик", object.name, "String", "OrdinaryForm.xml Event@handler", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Parent", "Родитель", std::string(owner_object_id), "FormItem", "OrdinaryForm.xml Event@ownerId", {}, {}, "public-xml"));
        add_api_surface(object, api_object_for_type("FormEvent"));
        form_object.events.add(std::move(object));
    }
}

void collect_public_xml_controls(
    oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view child_items_body,
    std::string_view parent_object_id,
    const std::vector<PublicXmlControlEdit>& control_edits
) {
    const std::regex start_tag_pattern(R"(<([A-Za-z][A-Za-z0-9]*)\b([^>]*)>)");
    std::size_t cursor = 0;
    while (cursor < child_items_body.size()) {
        const std::string remaining(child_items_body.substr(cursor));
        std::smatch match;
        if (!std::regex_search(remaining, match, start_tag_pattern)) {
            break;
        }
        const std::size_t start = cursor + static_cast<std::size_t>(match.position());
        const std::string tag = match[1].str();
        if (public_xml_section_tags().count(tag) != 0) {
            cursor = start + static_cast<std::size_t>(match.length());
            continue;
        }
        const std::size_t tag_end = child_items_body.find('>', start);
        if (tag_end == std::string::npos) {
            break;
        }
        const std::string attrs = match[2].str();
        const std::size_t attr_end = attrs.find_last_not_of(" \t\r\n");
        const bool self_closing = attr_end != std::string::npos && attrs[attr_end] == '/';
        std::string body;
        std::size_t element_end = tag_end + 1;
        if (!self_closing) {
            const std::size_t close_start = find_matching_xml_close(child_items_body, tag, tag_end + 1);
            if (close_start == std::string::npos) {
                throw std::runtime_error("OrdinaryForm XML control element is not closed: " + tag);
            }
            body = std::string(child_items_body.substr(tag_end + 1, close_start - tag_end - 1));
            element_end = close_start + std::string("</" + tag + ">").size();
        }

        const std::string object_id = xml_attr_value(attrs, "id");
        if (object_id.empty()) {
            cursor = element_end;
            continue;
        }
        oof::platform::object_model::PlatformObject object;
        object.object_id = object_id;
        object.name = xml_attr_value(attrs, "name");
        object.platform_type = platform_type_for_public_xml_tag(tag);
        object.type_category = "core::kLogFormTypeInfoCategory";
        object.type_source = "PublicOrdinaryFormXml.ChildItems + platform schema palette";
        object.parent_object_id = std::string(parent_object_id);
        object.path = object.parent_object_id.empty() ? "$/items/" + object.object_id : "$/items/" + object.parent_object_id + "/" + object.object_id;
        object.properties.push_back(make_described_property("ObjectID", object.object_id));
        object.properties.push_back(make_described_property("Name", object.name));
        object.properties.push_back(make_described_property("Type", object.platform_type));
        object.properties.push_back(make_described_property("Parent", object.parent_object_id));
        object.properties.push_back(make_described_property("Path", object.path));
        if (const auto* schema = oof::platform::form_schema::control_by_type_name(object.platform_type)) {
            add_platform_object_schema_surface(object, oof::platform::object_schema::build_schema_for_control(*schema));
        } else {
            add_api_surface(object, api_object_for_type(object.platform_type));
        }

        for (const auto& edit : control_edits) {
            if (edit.object_id != object_id) {
                continue;
            }
            if (edit.has_title) {
                set_or_add_described_property(object, "Title", edit.title);
            }
            if (edit.has_visible) {
                set_or_add_described_property(object, "Visible", edit.visible);
            }
            if (edit.has_enabled) {
                set_or_add_described_property(object, "Enabled", edit.enabled);
            }
            if (edit.has_position) {
                set_or_add_described_property(object, "Left", edit.left);
                set_or_add_described_property(object, "Top", edit.top);
                set_or_add_described_property(object, "Right", edit.right);
                set_or_add_described_property(object, "Bottom", edit.bottom);
                set_or_add_described_property(object, "Width", std::to_string(std::stoll(edit.right) - std::stoll(edit.left)));
                set_or_add_described_property(object, "Height", std::to_string(std::stoll(edit.bottom) - std::stoll(edit.top)));
                for (const auto& binding : edit.bindings) {
                    set_or_add_described_property(object, "Binding." + binding.name, oof::platform::stream::dump_compact(binding.value));
                }
                for (const auto& binding : edit.dimension_bindings) {
                    set_or_add_described_property(object, "DimensionBinding." + binding.name, oof::platform::stream::dump_compact(binding.value));
                }
            }
            for (const auto& property : edit.schema_properties) {
                set_or_add_described_property(object, property.name, property.value);
            }
            break;
        }

        const std::size_t new_index = form_object.items.count();
        form_object.items.add(std::move(object));
        if (parent_object_id.empty()) {
            form_object.form.children.push_back(new_index);
        } else if (auto* parent = form_object.find_object_by_id(parent_object_id)) {
            parent->children.push_back(new_index);
        }
        add_public_xml_events(form_object, object_id, body);
        const auto nested_child_items = first_xml_element(body, "ChildItems");
        if (!nested_child_items.self_closing && !nested_child_items.body.empty()) {
            collect_public_xml_controls(form_object, nested_child_items.body, object_id, control_edits);
        }
        cursor = element_end;
    }
}

oof::platform::object_model::PlatformFormObject platform_form_object_from_public_xml(
    const std::string& xml
) {
    const auto form_xml = first_xml_element(xml, "Form");
    const auto control_edits = parse_public_xml_control_edits(xml);
    oof::platform::object_model::PlatformFormObject form_object;
    form_object.form.object_id = "0";
    form_object.form.name = "Form";
    form_object.form.platform_type = "Form";
    form_object.form.type_category = "core::kLogFormTypeInfoCategory";
    form_object.form.type_source = "PublicOrdinaryFormXml -> PlatformFormObject";
    form_object.form.path = "$";
    form_object.form.properties.push_back(make_described_property("Type", "Form"));
    auto add_public_root_property = [&](std::string name, std::string value, std::string value_type, std::string source) {
        if (!value.empty()) {
            form_object.form.properties.push_back(make_platform_object_property(
                std::move(name), "", std::move(value), std::move(value_type), std::move(source),
                {}, {}, "public-xml"));
        }
    };
    if (!form_xml.attrs.empty()) {
        add_public_root_property("FormObjectUuid", xml_attr_value(form_xml.attrs, "formObjectUuid"), "UUID", "OrdinaryForm.xml Form@formObjectUuid");
        add_public_root_property("FormObjectKind", xml_attr_value(form_xml.attrs, "formObjectKind"), "xs:string", "OrdinaryForm.xml Form@formObjectKind");
        add_public_root_property("FormObjectStateKind", xml_attr_value(form_xml.attrs, "formObjectStateKind"), "xs:string", "OrdinaryForm.xml Form@formObjectStateKind");
        add_public_root_property("FormObjectStateMode", xml_attr_value(form_xml.attrs, "formObjectStateMode"), "xs:string", "OrdinaryForm.xml Form@formObjectStateMode");
        add_public_root_property("FormObjectStateFlag", xml_attr_value(form_xml.attrs, "formObjectStateFlag"), "xs:string", "OrdinaryForm.xml Form@formObjectStateFlag");
    }
    const auto title_xml = first_xml_element(xml, "Title");
    if (!title_xml.self_closing && !title_xml.body.empty()) {
        add_public_root_property("Title", public_xml_text_content(title_xml.body), "LocalizedText", "OrdinaryForm.xml Form/Title");
    }
    for (const auto& element_name : {"Width", "Height", "SerializationCounter"}) {
        const auto element = first_xml_element(xml, element_name);
        if (!element.self_closing && !element.body.empty()) {
            add_public_root_property(element_name, public_xml_text_content(element.body), "xs:integer",
                                     "OrdinaryForm.xml Form/" + std::string(element_name));
        }
    }
    const auto root_layout_xml = first_xml_element(xml, "RootPanelLayout");
    if (!root_layout_xml.self_closing && !root_layout_xml.body.empty()) {
        std::string fragment = "<RootPanelLayout";
        fragment += root_layout_xml.attrs;
        fragment += ">";
        fragment += root_layout_xml.body;
        fragment += "</RootPanelLayout>";
        add_public_root_property("RootPanelLayoutXml", fragment, "RootPanelLayout", "OrdinaryForm.xml Form/RootPanelLayout");
    }
    add_platform_object_schema_surface(form_object.form, oof::platform::object_schema::build_schema_for_root_form());
    add_api_surface(form_object.form, api_object_for_type("Form"));

    add_public_xml_events(form_object, "0", xml);
    const auto child_items = first_xml_element(xml, "ChildItems");
    if (!child_items.self_closing && !child_items.body.empty()) {
        collect_public_xml_controls(form_object, child_items.body, "", control_edits);
    }
    add_public_xml_collection_objects(form_object, xml);
    form_object.form.properties.push_back(make_described_property("Items", std::to_string(form_object.items.count())));
    form_object.form.properties.push_back(make_described_property("Attributes", std::to_string(form_object.attributes.count())));
    form_object.form.properties.push_back(make_described_property("Commands", std::to_string(form_object.commands.count())));
    form_object.form.properties.push_back(make_described_property("Events", std::to_string(form_object.events.count())));
    form_object.form.collections.push_back(make_described_collection("Items", form_object.items.count()));
    form_object.form.collections.push_back(make_described_collection("Attributes", form_object.attributes.count()));
    form_object.form.collections.push_back(make_described_collection("Commands", form_object.commands.count()));
    form_object.form.collections.push_back(make_described_collection("Events", form_object.events.count()));
    return form_object;
}

void append_indent(std::string& out, int indent) {
    out.append(static_cast<std::size_t>(indent), ' ');
}

const oof::platform::object_model::PlatformObjectProperty* find_object_property(
    const oof::platform::object_model::PlatformObject& object,
    std::string_view name
) {
    return object.property(name);
}

std::string object_property_value(
    const oof::platform::object_model::PlatformObject& object,
    std::string_view name
) {
    if (const auto* property = find_object_property(object, name)) {
        return property->value;
    }
    return {};
}

bool property_is_explicit_for_xml(const oof::platform::object_model::PlatformObjectProperty& property) {
    if (property.value.empty()) {
        return false;
    }
    if (property.value_origin == "schema-default") {
        return false;
    }
    if (property.write_policy == "omit-when-default" &&
        !property.default_value.empty() &&
        property.value == property.default_value) {
        return false;
    }
    return true;
}

bool has_explicit_property_for_xml(
    const oof::platform::object_model::PlatformObject& object,
    std::string_view name
) {
    const auto* property = find_object_property(object, name);
    return property != nullptr && property_is_explicit_for_xml(*property);
}

bool is_public_schema_property_xml(
    const oof::platform::object_model::PlatformObjectProperty& property
) {
    if (property.source.find("managed-application/logform") == std::string::npos) {
        return false;
    }
    if (property.name.empty() || property.name.find('.') != std::string::npos) {
        return false;
    }
    static const std::set<std::string> structural_names{
        "Title", "Visible", "Enabled", "Position", "Events", "ChildItems",
        "Attributes", "Commands", "Name", "Type", "Parent", "Path", "ObjectID",
    };
    return structural_names.count(property.name) == 0;
}

bool has_explicit_schema_properties_for_xml(
    const oof::platform::object_model::PlatformObject& object
) {
    for (const auto& property : object.properties) {
        if (is_public_schema_property_xml(property) && property_is_explicit_for_xml(property)) {
            return true;
        }
    }
    return false;
}

void append_named_text_property_xml(
    std::string& out,
    const oof::platform::object_model::PlatformObject& object,
    std::string_view property_name,
    int indent
) {
    const auto* property = find_object_property(object, property_name);
    if (property == nullptr || !property_is_explicit_for_xml(*property)) {
        return;
    }
    append_indent(out, indent);
    out += "<";
    out += property_name;
    out += ">";
    out += xml_escape(property->value);
    out += "</";
    out += property_name;
    out += ">\n";
}

void append_form_scalar_property_xml(
    std::string& out,
    const oof::platform::object_model::PlatformObject& object,
    std::string_view property_name,
    int indent
) {
    const auto* property = find_object_property(object, property_name);
    if (property == nullptr || !property_is_explicit_for_xml(*property)) {
        return;
    }
    append_indent(out, indent);
    out += "<";
    out += property_name;
    out += ">";
    out += xml_escape(property->value);
    out += "</";
    out += property_name;
    out += ">\n";
}

struct PublicXmlPackageSidecarSink {
    std::filesystem::path package_root;
    std::size_t picture_count = 0;
};

std::string package_item_name(const oof::platform::object_model::PlatformObject& object) {
    if (!object.name.empty()) {
        return safe_container_file_name(object.name);
    }
    if (!object.object_id.empty()) {
        return safe_container_file_name(object.object_id);
    }
    return "Item";
}

void append_schema_properties_xml(
    std::string& out,
    const oof::platform::object_model::PlatformObject& object,
    int indent,
    PublicXmlPackageSidecarSink* sidecar_sink = nullptr
) {
    for (const auto& property : object.properties) {
        if (!is_public_schema_property_xml(property) || !property_is_explicit_for_xml(property)) {
            continue;
        }
        if (sidecar_sink != nullptr &&
            property.name == "Picture" &&
            property.value_object_storage == "inline-base64") {
            const auto picture_bytes = decode_picture_payload(property.value);
            const std::string relative_path =
                "Items/" + package_item_name(object) + "/Picture." + picture_extension_for_bytes(picture_bytes);
            write_file_bytes(sidecar_sink->package_root / relative_path, picture_bytes);
            ++sidecar_sink->picture_count;
            append_indent(out, indent);
            out += "<Picture file=\"";
            out += xml_escape(relative_path);
            out += "\"/>\n";
            continue;
        }
        append_indent(out, indent);
        out += "<";
        out += property.name;
        if (!property.value_object_class.empty()) {
            out += ">\n";
            append_indent(out, indent + 2);
            const std::string value_text = property.value_object_schema_value.empty()
                ? property.value_object_literal
                : property.value_object_schema_value;
            out += "<";
            out += property.value_object_class;
            out += "Value constructor=\"";
            out += xml_escape(property.value_object_constructor);
            out += "\" storage=\"";
            out += xml_escape(property.value_object_storage);
            out += "\">";
            out += xml_escape(value_text);
            out += "</";
            out += property.value_object_class;
            out += "Value>\n";
            append_indent(out, indent);
            out += "</";
            out += property.name;
            out += ">\n";
        } else {
            out += ">";
            out += xml_escape(property.value);
            out += "</";
            out += property.name;
            out += ">\n";
        }
    }
}

bool has_position_properties(const oof::platform::object_model::PlatformObject& object) {
    return object.property("Left") != nullptr &&
           object.property("Top") != nullptr &&
           object.property("Right") != nullptr &&
           object.property("Bottom") != nullptr;
}

std::vector<GeometryBindingRecord> binding_properties(
    const oof::platform::object_model::PlatformObject& object
) {
    std::vector<GeometryBindingRecord> bindings;
    for (const std::string& coordinate : {
             "top",
             "bottom",
             "left",
             "right",
             "verticalCenter",
             "horizontalCenter",
    }) {
        const auto* prop = object.property("Binding." + coordinate);
        if (prop != nullptr) {
            bindings.push_back({coordinate, oof::platform::stream::parse(prop->value)});
        }
    }
    return bindings;
}

std::vector<GeometryBindingRecord> dimension_binding_properties(
    const oof::platform::object_model::PlatformObject& object
) {
    std::vector<GeometryBindingRecord> bindings;
    for (const std::string& dimension : {
             "height",
             "minHeight",
             "stretch",
             "width",
    }) {
        const auto* prop = object.property("DimensionBinding." + dimension);
        if (prop != nullptr) {
            bindings.push_back({dimension, oof::platform::stream::parse(prop->value)});
        }
    }
    return bindings;
}

std::vector<const oof::platform::object_model::PlatformObject*> event_objects_for_parent(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view parent_object_id
) {
    std::vector<const oof::platform::object_model::PlatformObject*> events;
    for (const auto& event : form_object.events.objects()) {
        if (event.parent_object_id == parent_object_id) {
            events.push_back(&event);
        }
    }
    return events;
}

void append_event_xml(
    std::string& out,
    const oof::platform::object_model::PlatformObject& event,
    int indent
) {
    append_indent(out, indent);
    out += "<Event";
    const std::string id = object_property_value(event, "ID");
    if (!id.empty()) {
        out += " id=\"";
        out += xml_escape(id);
        out += "\"";
    }
    const std::string handler = object_property_value(event, "Handler");
    if (!handler.empty()) {
        out += " handler=\"";
        out += xml_escape(handler);
        out += "\"";
    }
    if (!event.object_id.empty()) {
        out += " objectId=\"";
        out += xml_escape(event.object_id);
        out += "\"";
    }
    if (!event.parent_object_id.empty()) {
        out += " ownerId=\"";
        out += xml_escape(event.parent_object_id);
        out += "\"";
    }
    out += "/>\n";
}

void append_events_xml(
    std::string& out,
    const std::vector<const oof::platform::object_model::PlatformObject*>& events,
    int indent
) {
    append_indent(out, indent);
    if (events.empty()) {
        out += "<Events/>\n";
        return;
    }
    out += "<Events>\n";
    for (const auto* event : events) {
        append_event_xml(out, *event, indent + 2);
    }
    append_indent(out, indent);
    out += "</Events>\n";
}

void append_attributes_xml(
    std::string& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    int indent
) {
    append_indent(out, indent);
    if (form_object.attributes.count() == 0) {
        out += "<Attributes/>\n";
        return;
    }
    out += "<Attributes>\n";
    for (const auto& attribute : form_object.attributes.objects()) {
        append_indent(out, indent + 2);
        out += "<Attribute";
        const std::string name = object_property_value(attribute, "Name");
        if (!name.empty()) {
            out += " name=\"";
            out += xml_escape(name);
            out += "\"";
        }
        const std::string id = object_property_value(attribute, "ID");
        if (!id.empty()) {
            out += " id=\"";
            out += xml_escape(id);
            out += "\"";
        }
        if (!attribute.object_id.empty()) {
            out += " objectId=\"";
            out += xml_escape(attribute.object_id);
            out += "\"";
        }
        const std::string main = object_property_value(attribute, "Main");
        if (!main.empty()) {
            out += " main=\"";
            out += xml_escape(main);
            out += "\"";
        }
        const std::string stored_data = object_property_value(attribute, "StoredData");
        if (!stored_data.empty()) {
            out += " storedData=\"";
            out += xml_escape(stored_data);
            out += "\"";
        }
        const std::string type = object_property_value(attribute, "Type");
        if (type.empty()) {
            out += "/>\n";
            continue;
        }
        out += ">\n";
        append_indent(out, indent + 4);
        out += "<Type>";
        out += xml_escape(type);
        out += "</Type>\n";
        append_indent(out, indent + 2);
        out += "</Attribute>\n";
    }
    append_indent(out, indent);
    out += "</Attributes>\n";
}

void append_commands_xml(
    std::string& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    int indent
) {
    append_indent(out, indent);
    if (form_object.commands.count() == 0) {
        out += "<Commands/>\n";
        return;
    }
    out += "<Commands>\n";
    for (const auto& command : form_object.commands.objects()) {
        append_indent(out, indent + 2);
        out += "<Command";
        const std::string id = object_property_value(command, "ID");
        if (!id.empty()) {
            out += " id=\"";
            out += xml_escape(id);
            out += "\"";
        }
        const std::string name = object_property_value(command, "Name");
        if (!name.empty()) {
            out += " name=\"";
            out += xml_escape(name);
            out += "\"";
        }
        const std::string handler = object_property_value(command, "Handler");
        if (!handler.empty()) {
            out += " handler=\"";
            out += xml_escape(handler);
            out += "\"";
        }
        const std::string modifies_data = object_property_value(command, "ModifiesData");
        if (!modifies_data.empty()) {
            out += " modifiesData=\"";
            out += xml_escape(modifies_data);
            out += "\"";
        }
        if (!command.object_id.empty()) {
            out += " objectId=\"";
            out += xml_escape(command.object_id);
            out += "\"";
        }
        out += "/>\n";
    }
    append_indent(out, indent);
    out += "</Commands>\n";
}

void append_anchor_xml(
    std::string& out,
    std::string_view tag,
    const oof::platform::stream::ListValue& value,
    const oof::platform::object_model::PlatformFormObject& form_object,
    int indent
) {
    append_indent(out, indent);
    out += "<";
    out += tag;
    if (!value.is_list) {
        out += " value=\"";
        out += xml_escape(value.atom);
        out += "\"/>\n";
        return;
    }
    if (!is_simple_platform_anchor(value)) {
        throw std::runtime_error("unsupported platform Binding anchor shape; public XML cannot expose raw list fallback");
    }
    out += " relation=\"";
    out += xml_escape(anchor_relation_name(value.items[0].atom));
    out += "\" target=\"";
    out += xml_escape(anchor_target_name(value.items[1].atom));
    out += "\"";
    if (anchor_target_name(value.items[1].atom) == "element") {
        out += " targetId=\"";
        out += xml_escape(value.items[1].atom);
        out += "\"";
    }
    if (const auto* target = find_object_by_id(form_object, value.items[1].atom)) {
        out += " targetName=\"";
        out += xml_escape(target->name);
        out += "\"";
    }
    out += " side=\"";
    out += xml_escape(anchor_side_name(value.items[2].atom));
    out += "\" offset=\"";
    out += xml_escape(value.items[3].atom);
    out += "\"/>\n";
}

void append_binding_xml(
    std::string& out,
    const GeometryBindingRecord& binding,
    const oof::platform::object_model::PlatformFormObject& form_object,
    int indent
) {
    append_indent(out, indent);
    out += "<Binding coordinate=\"";
    out += xml_escape(binding.name);
    out += "\"";
    if (!binding.value.is_list) {
        out += " value=\"";
        out += xml_escape(binding.value.atom);
        out += "\"/>\n";
        return;
    }
    if (binding.value.items.empty() || binding.value.items[0].is_list) {
        throw std::runtime_error("unsupported platform Binding record shape; missing scalar mode");
    }
    out += " mode=\"";
    out += xml_escape(binding.value.items[0].atom);
    out += "\">\n";
    if (binding.value.items.size() > 1) {
        append_anchor_xml(out, "From", binding.value.items[1], form_object, indent + 2);
    }
    if (binding.value.items.size() > 2) {
        append_anchor_xml(out, "To", binding.value.items[2], form_object, indent + 2);
    }
    for (std::size_t index = 3; index < binding.value.items.size(); ++index) {
        append_anchor_xml(out, "Extra", binding.value.items[index], form_object, indent + 2);
    }
    append_indent(out, indent);
    out += "</Binding>\n";
}

void append_dimension_binding_xml(
    std::string& out,
    const GeometryBindingRecord& binding,
    const oof::platform::object_model::PlatformFormObject& form_object,
    int indent
) {
    append_indent(out, indent);
    out += "<DimensionBinding dimension=\"";
    out += xml_escape(binding.name);
    out += "\"";
    if (!binding.value.is_list) {
        out += " value=\"";
        out += xml_escape(binding.value.atom);
        out += "\"/>\n";
        return;
    }
    if (binding.value.items.size() < 3 ||
        binding.value.items[0].is_list ||
        binding.value.items[1].is_list ||
        binding.value.items[2].is_list) {
        throw std::runtime_error("unsupported platform DimensionBinding record shape; public XML cannot expose raw list fallback");
    }
    out += " mode=\"";
    out += xml_escape(binding.value.items[0].atom);
    out += "\" target=\"";
    out += xml_escape(anchor_target_name(binding.value.items[1].atom));
    out += "\"";
    if (anchor_target_name(binding.value.items[1].atom) == "element") {
        out += " targetId=\"";
        out += xml_escape(binding.value.items[1].atom);
        out += "\"";
    }
    if (const auto* target = find_object_by_id(form_object, binding.value.items[1].atom)) {
        out += " targetName=\"";
        out += xml_escape(target->name);
        out += "\"";
    }
    out += " side=\"";
    out += xml_escape(anchor_side_name(binding.value.items[2].atom));
    out += "\"";
    if (binding.value.items.size() == 3) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    for (std::size_t index = 3; index < binding.value.items.size(); ++index) {
        append_anchor_xml(out, "Extra", binding.value.items[index], form_object, indent + 2);
    }
    append_indent(out, indent);
    out += "</DimensionBinding>\n";
}

void append_position_xml(
    std::string& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object,
    int indent
) {
    const auto* left = object.property("Left");
    const auto* top = object.property("Top");
    const auto* right = object.property("Right");
    const auto* bottom = object.property("Bottom");
    if (left == nullptr || top == nullptr || right == nullptr || bottom == nullptr) {
        return;
    }
    const auto bindings = binding_properties(object);
    const auto dimension_bindings = dimension_binding_properties(object);
    append_indent(out, indent);
    out += "<Position left=\"";
    out += xml_escape(left->value);
    out += "\" top=\"";
    out += xml_escape(top->value);
    out += "\" right=\"";
    out += xml_escape(right->value);
    out += "\" bottom=\"";
    out += xml_escape(bottom->value);
    out += "\"";
    if (bindings.empty() && dimension_bindings.empty()) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    append_indent(out, indent + 2);
    out += "<Bindings>\n";
    for (const auto& binding : bindings) {
        append_binding_xml(out, binding, form_object, indent + 4);
    }
    for (const auto& binding : dimension_bindings) {
        append_dimension_binding_xml(out, binding, form_object, indent + 4);
    }
    append_indent(out, indent + 2);
    out += "</Bindings>\n";
    append_indent(out, indent);
    out += "</Position>\n";
}

void append_control_xml(
    std::string& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::size_t object_index,
    int indent,
    PublicXmlPackageSidecarSink* sidecar_sink = nullptr
) {
    const auto& object = form_object.items.get(object_index);
    const std::string tag = public_xml_tag_for_platform_type(object.platform_type);
    append_indent(out, indent);
    out += "<";
    out += tag;
    if (!object.name.empty()) {
        out += " name=\"";
        out += xml_escape(object.name);
        out += "\"";
    }
    if (!object.object_id.empty()) {
        out += " id=\"";
        out += xml_escape(object.object_id);
        out += "\"";
    }
    const auto object_events = event_objects_for_parent(form_object, object.object_id);
    if (object.children.empty() &&
        !has_explicit_property_for_xml(object, "Title") &&
        !has_explicit_property_for_xml(object, "Visible") &&
        !has_explicit_property_for_xml(object, "Enabled") &&
        !has_explicit_schema_properties_for_xml(object) &&
        !has_position_properties(object) &&
        object_events.empty()) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    append_named_text_property_xml(out, object, "Title", indent + 2);
    append_named_text_property_xml(out, object, "Visible", indent + 2);
    append_named_text_property_xml(out, object, "Enabled", indent + 2);
    append_schema_properties_xml(out, object, indent + 2, sidecar_sink);
    append_position_xml(out, form_object, object, indent + 2);
    if (!object_events.empty()) {
        append_events_xml(out, object_events, indent + 2);
    }
    if (!object.children.empty()) {
        append_indent(out, indent + 2);
        out += "<ChildItems>\n";
        for (const std::size_t child_index : object.children) {
            append_control_xml(out, form_object, child_index, indent + 4, sidecar_sink);
        }
        append_indent(out, indent + 2);
        out += "</ChildItems>\n";
    }
    append_indent(out, indent);
    out += "</";
    out += tag;
    out += ">\n";
}

std::string form_object_to_public_xml(
    const oof::platform::object_model::PlatformFormObject& form_object,
    PublicXmlPackageSidecarSink* sidecar_sink = nullptr
) {
    std::string out;
    out += "<?xml version='1.0' encoding='utf-8'?>\n";
    out += "<Form xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" ordinaryFormVersion=\"2.0\" xsi:noNamespaceSchemaLocation=\"OrdinaryForm.xsd\"";
    const auto append_root_attr = [&](std::string_view property_name, std::string_view attr_name) {
        const auto* property = find_object_property(form_object.form, property_name);
        if (property == nullptr || !property_is_explicit_for_xml(*property)) {
            return;
        }
        out += " ";
        out += attr_name;
        out += "=\"";
        out += xml_escape(property->value);
        out += "\"";
    };
    append_root_attr("FormObjectUuid", "formObjectUuid");
    append_root_attr("FormObjectKind", "formObjectKind");
    append_root_attr("FormObjectStateKind", "formObjectStateKind");
    append_root_attr("FormObjectStateMode", "formObjectStateMode");
    append_root_attr("FormObjectStateFlag", "formObjectStateFlag");
    out += ">\n";
    append_named_text_property_xml(out, form_object.form, "Title", 2);
    append_form_scalar_property_xml(out, form_object.form, "Width", 2);
    append_form_scalar_property_xml(out, form_object.form, "Height", 2);
    append_form_scalar_property_xml(out, form_object.form, "SerializationCounter", 2);
    if (const auto* root_layout = find_object_property(form_object.form, "RootPanelLayoutXml")) {
        if (property_is_explicit_for_xml(*root_layout)) {
            append_indent(out, 2);
            out += root_layout->value;
            out += "\n";
        }
    }
    append_events_xml(out, event_objects_for_parent(form_object, "0"), 2);
    out += "  <ChildItems>\n";
    for (const std::size_t child_index : form_object.form.children) {
        append_control_xml(out, form_object, child_index, 4, sidecar_sink);
    }
    out += "  </ChildItems>\n";
    append_attributes_xml(out, form_object, 2);
    append_commands_xml(out, form_object, 2);
    out += "</Form>\n";
    return out;
}

void write_runtime_form_xml(const std::string& input_path, const std::string& output_path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    const auto form_object = materialize_platform_form_object(envelope);
    const std::string xml = form_object_to_public_xml(form_object);
    write_file_bytes(output_path, std::vector<std::uint8_t>(xml.begin(), xml.end()));
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"bytes\":" << xml.size();
    std::cout << ",\"source\":\"RuntimeForm:PlatformObject\"";
    std::cout << ",\"controlCount\":" << form_object.items.count();
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << "}\n";
}

RuntimeFormEnvelope read_formbin_runtime_envelope(const std::string& input_path) {
    const std::vector<std::uint8_t> data = read_file_bytes(input_path);
    const auto container = oof::platform::formbin::parse_container(data);
    const auto& form_file = find_container_file(container, "form");
    return runtime_envelope_from_form_payload(form_file.payload);
}

std::filesystem::path form_package_root_for_xml(const std::filesystem::path& xml_path) {
    auto package_root = xml_path;
    package_root.replace_extension("");
    return package_root;
}

std::filesystem::path form_package_module_path(const std::filesystem::path& xml_path) {
    return form_package_root_for_xml(xml_path) / "Module.bsl";
}

std::filesystem::path checked_package_relative_path(
    const std::filesystem::path& package_root,
    std::string_view relative
) {
    const std::filesystem::path rel{std::string(relative)};
    if (rel.is_absolute()) {
        throw std::runtime_error("Picture file path must be relative to the Form package");
    }
    for (const auto& part : rel) {
        if (part == "..") {
            throw std::runtime_error("Picture file path must not escape the Form package");
        }
    }
    return package_root / rel;
}

std::string inline_picture_sidecars_for_build(
    const std::string& xml,
    const std::filesystem::path& xml_path,
    std::size_t& picture_sidecars_read
) {
    const auto package_root = form_package_root_for_xml(xml_path);
    std::string out;
    std::size_t cursor = 0;
    const std::string open = "<Picture";
    while (cursor < xml.size()) {
        const std::size_t start = xml.find(open, cursor);
        if (start == std::string::npos) {
            out += xml.substr(cursor);
            break;
        }
        const std::size_t name_end = start + open.size();
        if (name_end < xml.size()) {
            const char after_name = xml[name_end];
            if (std::isalnum(static_cast<unsigned char>(after_name)) || after_name == '_' || after_name == '-') {
                out += xml.substr(cursor, name_end - cursor);
                cursor = name_end;
                continue;
            }
        }
        const std::size_t tag_end = xml.find('>', start);
        if (tag_end == std::string::npos) {
            out += xml.substr(cursor);
            break;
        }
        const std::string attrs = xml.substr(name_end, tag_end - name_end);
        const std::string file = xml_attr_value(attrs, "file");
        if (file.empty()) {
            out += xml.substr(cursor, tag_end + 1 - cursor);
            cursor = tag_end + 1;
            continue;
        }

        std::size_t replace_end = tag_end + 1;
        const std::size_t attr_end = attrs.find_last_not_of(" \t\r\n/");
        const bool self_closing = attr_end == std::string::npos ||
                                  attrs.find('/', attr_end + 1) != std::string::npos;
        if (!self_closing) {
            const std::size_t close_start = xml.find("</Picture>", tag_end + 1);
            if (close_start == std::string::npos) {
                throw std::runtime_error("Picture file element is not closed");
            }
            replace_end = close_start + std::string("</Picture>").size();
        }

        const auto picture_bytes = read_file_bytes(checked_package_relative_path(package_root, file).string());
        const std::string payload = wrap_base64_picture_payload(picture_bytes);
        out += xml.substr(cursor, start - cursor);
        out += "<Picture><PictureValue constructor=\"New Picture\" storage=\"inline-base64\">";
        out += xml_escape(payload);
        out += "</PictureValue></Picture>";
        ++picture_sidecars_read;
        cursor = replace_end;
    }
    return out;
}

std::size_t apply_picture_sidecar_edits(
    oof::platform::stream::ListValue& payload,
    const std::string& xml,
    const std::filesystem::path& xml_path
) {
    const auto package_root = form_package_root_for_xml(xml_path);
    const std::set<std::string> section_tags{
        "Form", "Events", "Event", "ChildItems", "Attributes", "Attribute", "Commands", "Command",
        "Title", "Position", "Pages", "Picture", "PictureValue"
    };
    const std::regex start_tag_pattern(R"(<([A-Za-z][A-Za-z0-9]*)\b([^>]*)>)");
    std::size_t edits = 0;
    for (std::sregex_iterator it(xml.begin(), xml.end(), start_tag_pattern), end; it != end; ++it) {
        const std::string tag = (*it)[1].str();
        if (section_tags.count(tag) != 0) {
            continue;
        }
        const std::string attrs = (*it)[2].str();
        const std::string object_id = xml_attr_value(attrs, "id");
        if (object_id.empty()) {
            continue;
        }
        const std::size_t body_start = static_cast<std::size_t>(it->position() + it->length());
        const std::string close_tag = "</" + tag + ">";
        const std::size_t body_end = xml.find(close_tag, body_start);
        if (body_end == std::string::npos) {
            continue;
        }
        std::string body = xml.substr(body_start, body_end - body_start);
        const std::size_t child_items_pos = body.find("<ChildItems>");
        if (child_items_pos != std::string::npos) {
            body.resize(child_items_pos);
        }
        for (const auto& picture_xml : find_xml_elements(body, "Picture")) {
            const std::string file = xml_attr_value(picture_xml.attrs, "file");
            if (file.empty()) {
                continue;
            }
            const auto picture_bytes = read_file_bytes(checked_package_relative_path(package_root, file).string());
            const std::string picture_payload = wrap_base64_picture_payload(picture_bytes);
            if (!set_materialized_object_picture_payload(payload, object_id, picture_payload)) {
                throw std::runtime_error("cannot apply Picture sidecar: baseline object has no writable picture payload slot: object=" +
                                         object_id);
            }
            ++edits;
        }
    }
    return edits;
}

void write_formbin_package(const std::string& input_path, const std::string& output_path) {
    const std::vector<std::uint8_t> data = read_file_bytes(input_path);
    const auto container = oof::platform::formbin::parse_container(data);
    const auto& form_file = find_container_file(container, "form");
    const RuntimeFormEnvelope envelope = runtime_envelope_from_form_payload(form_file.payload);
    const auto form_object = materialize_platform_form_object(envelope);
    const std::filesystem::path xml_path(output_path);
    PublicXmlPackageSidecarSink sidecar_sink{form_package_root_for_xml(xml_path)};
    const std::string xml = form_object_to_public_xml(form_object, &sidecar_sink);
    write_file_bytes(xml_path, std::vector<std::uint8_t>(xml.begin(), xml.end()));

    bool module_written = false;
    std::size_t module_bytes = 0;
    for (const auto& file : container.files) {
        if (file.name == "module") {
            const auto module_path = form_package_module_path(xml_path);
            write_file_bytes(module_path, file.payload);
            module_written = true;
            module_bytes = file.payload.size();
            break;
        }
    }

    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"packageRoot\":";
    print_json_string(form_package_root_for_xml(xml_path).string());
    std::cout << ",\"operation\":\"formbin-dump-package\"";
    std::cout << ",\"bytes\":" << xml.size();
    std::cout << ",\"source\":\"Form.bin\"";
    std::cout << ",\"controlCount\":" << form_object.items.count();
    std::cout << ",\"moduleWritten\":" << (module_written ? "true" : "false");
    std::cout << ",\"moduleBytes\":" << module_bytes;
    std::cout << ",\"pictureSidecars\":" << sidecar_sink.picture_count;
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << "}\n";
}

void print_formbin_platform_object(const std::string& input_path) {
    RuntimeFormEnvelope envelope = read_formbin_runtime_envelope(input_path);
    print_platform_form_object_document(
        materialize_platform_form_object(envelope),
        "Form.bin:PlatformObject",
        "");
}

void print_formbin_platform_object_get(
    const std::string& input_path,
    std::string_view object_id,
    std::string_view property_name
) {
    RuntimeFormEnvelope envelope = read_formbin_runtime_envelope(input_path);
    print_platform_object_get_json(
        materialize_platform_form_object(envelope),
        "Form.bin:PlatformObject",
        object_id,
        property_name);
}

std::vector<std::uint8_t> encode_form_payload_text(
    const std::vector<std::uint8_t>& original_payload,
    const oof::platform::stream::ListValue& payload
) {
    const std::string text = oof::platform::stream::dump_compact(payload);
    std::vector<std::uint8_t> out;
    if (original_payload.size() >= 3 &&
        original_payload[0] == 0xef &&
        original_payload[1] == 0xbb &&
        original_payload[2] == 0xbf) {
        out.push_back(0xef);
        out.push_back(0xbb);
        out.push_back(0xbf);
    }
    out.insert(out.end(), text.begin(), text.end());
    return out;
}

void write_runtime_form_from_xml(
    const std::string& input_path,
    const std::string& xml_path,
    const std::string& output_path
) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    const auto baseline = materialize_platform_form_object(envelope);
    const auto requested_edits = parse_public_xml_platform_object_edits(read_file_text_lossy(xml_path));
    const auto object_edits = keep_changed_platform_object_edits(requested_edits, baseline);
    const auto result = apply_platform_object_edits(envelope, object_edits);
    const std::size_t deleted_controls =
        delete_controls_missing_from_public_xml(envelope.payload, requested_public_control_ids(read_file_text_lossy(xml_path)));
    const std::string rebuilt_text = dump_runtime_form_envelope(envelope);
    write_file_bytes(output_path, std::vector<std::uint8_t>(rebuilt_text.begin(), rebuilt_text.end()));
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"runtime-form-build-xml\"";
    std::cout << ",\"bytes\":" << rebuilt_text.size();
    std::cout << ",\"controls\":" << result.controls;
    std::cout << ",\"nameEdits\":" << result.name_edits;
    std::cout << ",\"titleEdits\":" << result.title_edits;
    std::cout << ",\"scalarFlagEdits\":" << result.scalar_flag_edits;
    std::cout << ",\"positionEdits\":" << result.position_edits;
    std::cout << ",\"bindingEdits\":" << result.binding_edits;
    std::cout << ",\"dimensionBindingEdits\":" << result.dimension_binding_edits;
    std::cout << ",\"attributeEdits\":" << result.attribute_edits;
    std::cout << ",\"commandEdits\":" << result.command_edits;
    std::cout << ",\"eventEdits\":" << result.event_edits;
    std::cout << ",\"deletedControls\":" << deleted_controls;
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << "}\n";
}

void write_formbin_from_package(
    const std::string& input_path,
    const std::string& xml_path,
    const std::string& output_path
) {
    const std::vector<std::uint8_t> data = read_file_bytes(input_path);
    auto container = oof::platform::formbin::parse_container(data);
    auto& form_file = find_container_file_mut(container, "form");

    RuntimeFormEnvelope envelope = runtime_envelope_from_form_payload(form_file.payload);
    const auto baseline = materialize_platform_form_object(envelope);
    std::size_t picture_sidecars_read = 0;
    const auto xml_package_path = std::filesystem::path(xml_path);
    const std::string source_xml = read_file_text_lossy(xml_path);
    const std::string package_xml = inline_picture_sidecars_for_build(
        source_xml,
        xml_package_path,
        picture_sidecars_read);
    const auto requested_edits = parse_public_xml_platform_object_edits(package_xml);
    const auto object_edits = keep_changed_platform_object_edits(requested_edits, baseline);
    const auto result = apply_platform_object_edits(envelope, object_edits);
    const std::size_t picture_sidecar_edits =
        apply_picture_sidecar_edits(envelope.payload, source_xml, xml_package_path);
    const std::size_t deleted_controls =
        delete_controls_missing_from_public_xml(envelope.payload, requested_public_control_ids(source_xml));
    form_file.payload = encode_form_payload_text(form_file.payload, envelope.payload);

    const auto module_path = form_package_module_path(xml_package_path);
    bool module_sidecar_used = false;
    std::size_t module_bytes = 0;
    if (std::filesystem::is_regular_file(module_path)) {
        auto& module_file = find_container_file_mut(container, "module");
        module_file.payload = read_file_bytes(module_path.string());
        module_sidecar_used = true;
        module_bytes = module_file.payload.size();
    }

    const auto rebuilt = oof::platform::formbin::serialize_container(container);
    write_file_bytes(output_path, rebuilt);
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"formbin-build-package\"";
    std::cout << ",\"bytes\":" << rebuilt.size();
    std::cout << ",\"controls\":" << result.controls;
    std::cout << ",\"nameEdits\":" << result.name_edits;
    std::cout << ",\"titleEdits\":" << result.title_edits;
    std::cout << ",\"scalarFlagEdits\":" << result.scalar_flag_edits;
    std::cout << ",\"positionEdits\":" << result.position_edits;
    std::cout << ",\"bindingEdits\":" << result.binding_edits;
    std::cout << ",\"dimensionBindingEdits\":" << result.dimension_binding_edits;
    std::cout << ",\"attributeEdits\":" << result.attribute_edits;
    std::cout << ",\"commandEdits\":" << result.command_edits;
    std::cout << ",\"eventEdits\":" << result.event_edits;
    std::cout << ",\"deletedControls\":" << deleted_controls;
    std::cout << ",\"moduleSource\":";
    print_json_string(module_sidecar_used ? "sidecar" : "baseline");
    std::cout << ",\"moduleBytes\":" << module_bytes;
    std::cout << ",\"pictureSidecarsRead\":" << picture_sidecars_read;
    std::cout << ",\"pictureSidecarEdits\":" << picture_sidecar_edits;
    std::cout << ",\"preservedContainerFiles\":" << container.files.size();
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << "}\n";
}

using LV = oof::platform::stream::ListValue;

LV raw(std::string value) {
    return LV::raw_atom(std::move(value));
}

LV str_atom(std::string value) {
    return LV::string_atom(std::move(value));
}

LV list(std::vector<LV> value) {
    return LV::list(std::move(value));
}

LV localized_text_record(std::string_view text) {
    return list({
        raw("1"),
        raw("1"),
        list({str_atom("ru"), str_atom(std::string(text))})
    });
}

std::string public_xml_text_content(std::string_view body) {
    const auto item = first_xml_element(body, "Item");
    if (!item.self_closing && !item.body.empty()) {
        return xml_unescape(item.body);
    }
    return xml_unescape(std::string(body));
}

std::string public_form_title_from_xml(const std::string& xml) {
    std::size_t root_header_end = xml.size();
    for (const std::string& tag : {"<Events", "<ChildItems", "<Attributes", "<Commands"}) {
        const std::size_t pos = xml.find(tag);
        if (pos != std::string::npos) {
            root_header_end = std::min(root_header_end, pos);
        }
    }
    const auto title = first_xml_element(std::string_view(xml).substr(0, root_header_end), "Title");
    if (title.self_closing || title.body.empty()) {
        return "";
    }
    const std::string text = public_xml_text_content(title.body);
    return text;
}

std::string source_writer_control_guid(std::string_view platform_type) {
    static const std::map<std::string_view, std::string_view> guids{
        {"Panel", "09ccdc77-ea1a-4a6d-ab1c-3435eada2433"},
        {"PanelPage", "09ccdc77-ea1a-4a6d-ab1c-3435eada2433"},
        {"ActiveXControl", "621e95f1-064f-11d4-9400-008048da11f9"},
        {"Label", "0fc7e20d-f241-460c-bdf4-5ad88e5474a5"},
        {"Image", "151ef23e-6bb2-4681-83d0-35bc2217230c"},
        {"Button", "6ff79819-710e-4145-97cd-1618da79e3e2"},
        {"TextBox", "381ed624-9217-4e63-85db-c4c3cb87daae"},
        {"InputField", "381ed624-9217-4e63-85db-c4c3cb87daae"},
        {"CommandBar", "e69bf21d-97b2-4f37-86db-675aea9ec2cb"},
        {"CheckBox", "35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26"},
        {"TableBox", "ea83fe3a-ac3c-4cce-8045-3dddf35b28b1"},
        {"Table", "ea83fe3a-ac3c-4cce-8045-3dddf35b28b1"},
        {"ChoiceField", "64483e7f-3833-48e2-8c75-2c31aac49f6e"},
        {"Spreadsheet", "236a17b3-7f44-46d9-a907-75f9cdc61ab5"},
        {"SpreadsheetDocumentField", "236a17b3-7f44-46d9-a907-75f9cdc61ab5"},
        {"GroupBox", "90db814a-c75f-4b54-bc96-df62e554d67d"},
        {"RadioButton", "782e569a-79a7-4a4f-a936-b48d013936ec"},
        {"Splitter", "36e52348-5d60-4770-8e89-a16ed50a2006"},
        {"Chart", "a8b97779-1a4b-4059-b09c-807f86d2a461"},
        {"ListBox", "19f8b798-314e-4b4e-8121-905b2a7a03f5"},
        {"HTMLDocumentField", "d92a805c-98ae-4750-9158-d9ce7cec2f20"},
        {"TrackBar", "6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef"},
        {"Calendar", "e3c063d8-ef92-41be-9c89-b70290b5368b"},
        {"CalendarField", "e3c063d8-ef92-41be-9c89-b70290b5368b"},
        {"TextDocument", "14c4a229-bfc3-42fe-9ce1-2da049fd0109"},
        {"TextDocumentField", "14c4a229-bfc3-42fe-9ce1-2da049fd0109"},
        {"PivotChart", "a26da99e-184a-4823-b0d6-62816d38dc4e"},
        {"GeographicalSchemaField", "ad37194e-555e-4305-b718-5dca84baf145"},
        {"ProgressBar", "b1db1f86-abbb-4cf0-8852-fe6ae21650c2"},
        {"GraphicalSchemaField", "42248403-7748-49da-b782-e4438fd7bff3"},
        {"GanttChart", "e5fdc112-5c84-4a16-9728-72b85692b6e2"},
        {"Dendrogram", "984981b1-622d-4ebc-94f7-885f0cdfb59a"},
    };
    const auto it = guids.find(platform_type);
    if (it == guids.end()) {
        throw std::runtime_error("native source writer does not know ordinary control GUID for type: " +
                                 std::string(platform_type));
    }
    return std::string(it->second);
}

std::string object_prop_or_default(
    const oof::platform::object_model::PlatformObject& object,
    std::string_view name,
    std::string fallback
) {
    if (const auto* property = object.property(name)) {
        if (!property->value.empty()) {
            return property->value;
        }
    }
    return fallback;
}

LV source_writer_geometry(const oof::platform::object_model::PlatformObject& object) {
    std::vector<LV> items;
    items.push_back(raw("8"));
    items.push_back(raw(object_prop_or_default(object, "Left", "0")));
    items.push_back(raw(object_prop_or_default(object, "Top", "0")));
    items.push_back(raw(object_prop_or_default(object, "Right", "0")));
    items.push_back(raw(object_prop_or_default(object, "Bottom", "0")));
    items.push_back(raw("0"));
    for (std::string_view name : {
             "Binding.top", "Binding.bottom", "Binding.left",
             "Binding.right", "Binding.verticalCenter", "Binding.horizontalCenter",
         }) {
        const auto* property = object.property(name);
        items.push_back(property == nullptr || property->value.empty()
                            ? raw("0")
                            : oof::platform::stream::parse(property->value));
    }
    items.push_back(raw("0"));
    for (std::string_view name : {
             "DimensionBinding.height", "DimensionBinding.minHeight",
             "DimensionBinding.stretch", "DimensionBinding.width",
         }) {
        const auto* property = object.property(name);
        items.push_back(property == nullptr || property->value.empty()
                            ? raw("0")
                            : oof::platform::stream::parse(property->value));
    }
    items.push_back(raw("0"));
    items.push_back(raw("0"));
    return list(std::move(items));
}

LV source_writer_metadata(const oof::platform::object_model::PlatformObject& object) {
    return list({
        raw("14"),
        str_atom(object.name.empty() ? object.object_id : object.name),
        raw("4294967295"),
        raw("0"),
        raw("0"),
        raw("0"),
    });
}

std::vector<const oof::platform::object_model::PlatformObject*> source_writer_children(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent
) {
    std::vector<const oof::platform::object_model::PlatformObject*> children;
    for (const auto& index : parent.children) {
        if (index < form_object.items.count()) {
            children.push_back(&form_object.items.get(index));
        }
    }
    return children;
}

LV source_writer_child_table(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent
);

LV source_writer_control_record(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    return list({
        raw(source_writer_control_guid(object.platform_type)),
        raw(object.object_id.empty() ? "0" : object.object_id),
        list({raw("1"), localized_text_record(title)}),
        source_writer_geometry(object),
        source_writer_metadata(object),
        source_writer_child_table(form_object, object),
    });
}

LV source_writer_child_table(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent
) {
    std::vector<LV> items;
    const auto children = source_writer_children(form_object, parent);
    items.push_back(raw(std::to_string(children.size())));
    for (const auto* child : children) {
        items.push_back(source_writer_control_record(form_object, *child));
    }
    return list(std::move(items));
}

std::int64_t source_writer_slot_number(std::string_view composite_id) {
    std::string digits;
    for (const char ch : composite_id) {
        if (ch >= '0' && ch <= '9') {
            digits.push_back(ch);
        }
    }
    if (digits.empty()) {
        return 0;
    }
    return std::stoll(digits);
}

LV source_writer_composite_id_value(const std::string& composite_id) {
    if (!composite_id.empty() && composite_id.front() == '{') {
        return oof::platform::stream::parse(composite_id);
    }
    return raw(composite_id.empty() ? "0" : composite_id);
}

LV source_writer_attribute_record(const oof::platform::object_model::PlatformObject& attribute) {
    const std::string id = object_prop_or_default(attribute, "ID", "0");
    const std::string name = object_prop_or_default(attribute, "Name", attribute.name);
    const std::string main = object_prop_or_default(attribute, "Main", "1");
    const std::string type = object_prop_or_default(attribute, "Type", "{\"Pattern\"}");
    return list({
        source_writer_composite_id_value(id),
        raw(main == "0" || main == "false" ? "0" : "1"),
        raw("0"),
        raw("1"),
        str_atom(name),
        oof::platform::stream::parse(type),
    });
}

void collect_source_writer_attribute_links(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent,
    const std::map<std::string, std::string>& attribute_slots,
    std::vector<LV>& links
) {
    for (const auto* child : source_writer_children(form_object, parent)) {
        const auto found = attribute_slots.find(child->name);
        if (found != attribute_slots.end()) {
            links.push_back(list({
                raw(child->object_id),
                list({raw("1"), source_writer_composite_id_value(found->second)}),
            }));
        }
        collect_source_writer_attribute_links(form_object, *child, attribute_slots, links);
    }
}

LV source_writer_attributes_table(const oof::platform::object_model::PlatformFormObject& form_object) {
    std::vector<LV> records;
    std::map<std::string, std::string> attribute_slots_by_name;
    std::int64_t max_slot = 0;
    for (const auto& attribute : form_object.attributes.objects()) {
        const std::string id = object_prop_or_default(attribute, "ID", "0");
        const std::string name = object_prop_or_default(attribute, "Name", attribute.name);
        max_slot = std::max(max_slot, source_writer_slot_number(id));
        if (!name.empty()) {
            attribute_slots_by_name[name] = id;
        }
        records.push_back(source_writer_attribute_record(attribute));
    }

    std::vector<LV> record_table;
    record_table.push_back(raw(std::to_string(records.size())));
    record_table.insert(record_table.end(), std::make_move_iterator(records.begin()), std::make_move_iterator(records.end()));

    std::vector<LV> links;
    collect_source_writer_attribute_links(form_object, form_object.form, attribute_slots_by_name, links);
    std::vector<LV> link_table;
    link_table.push_back(raw(std::to_string(links.size())));
    link_table.insert(link_table.end(), std::make_move_iterator(links.begin()), std::make_move_iterator(links.end()));

    return list({
        list({raw("1")}),
        raw(form_object.attributes.count() == 0 ? "0" : std::to_string(max_slot + 1)),
        list(std::move(record_table)),
        list(std::move(link_table)),
    });
}

LV source_writer_root_panel_info_from_layout_xml(const std::string& xml);

LV source_writer_root_record(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view title
) {
    std::vector<LV> root_children;
    for (const auto* child : source_writer_children(form_object, form_object.form)) {
        root_children.push_back(source_writer_control_record(form_object, *child));
    }
    std::vector<LV> child_table;
    child_table.push_back(raw(std::to_string(root_children.size())));
    child_table.insert(child_table.end(), std::make_move_iterator(root_children.begin()), std::make_move_iterator(root_children.end()));

    LV root_panel_info = list({raw("1"), localized_text_record(title)});
    if (const auto* root_layout = find_object_property(form_object.form, "RootPanelLayoutXml")) {
        LV layout_info = source_writer_root_panel_info_from_layout_xml(root_layout->value);
        if (layout_info.is_list) {
            root_panel_info = std::move(layout_info);
        }
    }
    const LV root_panel = list({
        raw(source_writer_control_guid("Panel")),
        std::move(root_panel_info),
        list(std::move(child_table)),
    });
    std::vector<LV> root_items{
        raw("16"),
        list({localized_text_record(title), raw("52"), raw("4294967295")}),
        root_panel,
        raw("885"),
        raw("244"),
        raw("1"),
        raw("0"),
        raw("1"),
        raw("4"),
        raw("4"),
        raw("6"),
    };
    const std::string width = object_property_value(form_object.form, "Width");
    const std::string height = object_property_value(form_object.form, "Height");
    const std::string counter = object_property_value(form_object.form, "SerializationCounter");
    if (!width.empty() || !height.empty() || !counter.empty()) {
        root_items[0] = raw("18");
        root_items[10] = raw(counter.empty() ? "3" : counter);
        root_items.push_back(raw(width.empty() ? "0" : width));
        root_items.push_back(raw(height.empty() ? "0" : height));
        root_items.push_back(raw("96"));
    }
    return list(std::move(root_items));
}

LV source_writer_form_object_info(
    const oof::platform::object_model::PlatformFormObject& form_object
) {
    const std::string uuid = object_property_value(form_object.form, "FormObjectUuid");
    const std::string kind = object_property_value(form_object.form, "FormObjectKind");
    const std::string state_kind = object_property_value(form_object.form, "FormObjectStateKind");
    const std::string state_mode = object_property_value(form_object.form, "FormObjectStateMode");
    const std::string state_flag = object_property_value(form_object.form, "FormObjectStateFlag");
    if (!state_kind.empty() || !state_mode.empty() || !state_flag.empty()) {
        return list({
            raw(uuid.empty() ? "00000000-0000-0000-0000-000000000000" : uuid),
            raw(kind.empty() ? "0" : kind),
            list({
                raw(state_kind.empty() ? "0" : state_kind),
                raw(state_mode.empty() ? "0" : state_mode),
                list({raw("0"), raw("0")}),
                list({raw("0")}),
                raw(state_flag.empty() ? "0" : state_flag),
            }),
        });
    }
    return list({
        raw(uuid.empty() ? "00000000-0000-0000-0000-000000000000" : uuid),
        raw(kind.empty() ? "0" : kind),
    });
}

LV source_writer_default_color_record() {
    return list({raw("4"), raw("4"), list({raw("0")}), raw("4")});
}

LV source_writer_empty_page_style_record() {
    return list({raw("4"), raw("0"), list({raw("0")}), str_atom(""), raw("-1"), raw("-1"), raw("1"), raw("0"), str_atom("")});
}

LV source_writer_page_style_group_record(std::string_view active) {
    return list({
        raw("10"), raw(std::string(active)),
        source_writer_empty_page_style_record(),
        source_writer_empty_page_style_record(),
        source_writer_empty_page_style_record(),
        raw("100"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
    });
}

LV source_writer_root_page_state_style_group_record(std::string_view style_mode) {
    auto record = source_writer_page_style_group_record("0");
    record.items[6] = raw(std::string(style_mode.empty() ? "0" : style_mode));
    return record;
}

LV source_writer_root_panel_base_info_record() {
    return list({
        raw("19"),
        raw("1"),
        source_writer_default_color_record(),
        source_writer_default_color_record(),
        list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
        raw("0"),
        list({raw("4"), raw("3"), list({raw("-22")}), raw("3")}),
        source_writer_default_color_record(),
        source_writer_default_color_record(),
        list({raw("4"), raw("3"), list({raw("-7")}), raw("3")}),
        list({raw("4"), raw("3"), list({raw("-21")}), raw("3")}),
        list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("0"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}),
        list({raw("1"), raw("0")}),
        raw("0"),
        raw("0"),
        raw("100"),
        raw("2"),
        raw("2"),
        raw("1"),
        raw("2"),
        source_writer_default_color_record(),
    });
}

LV source_writer_root_page_state_record(const XmlElementSlice& page_state) {
    const std::string name = xml_attr_value(page_state.attrs, "name").empty()
        ? "Страница1"
        : xml_attr_value(page_state.attrs, "name");
    const std::string style_mode = xml_attr_value(page_state.attrs, "styleMode").empty()
        ? "0"
        : xml_attr_value(page_state.attrs, "styleMode");
    const auto title = first_xml_element(page_state.body, "Title");
    const std::string title_text = (!title.self_closing && !title.body.empty())
        ? public_xml_text_content(title.body)
        : name;
    return list({
        raw("6"),
        localized_text_record(title_text),
        source_writer_root_page_state_style_group_record(style_mode),
        raw("-1"),
        raw("1"),
        raw("1"),
        str_atom(name),
        raw("1"),
        source_writer_default_color_record(),
        source_writer_default_color_record(),
        list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
        raw("1"),
    });
}

std::vector<LV> source_writer_root_page_state_records(const XmlElementSlice& layout) {
    std::vector<LV> states;
    for (const auto& page_state : find_xml_elements(layout.body, "PageState")) {
        states.push_back(source_writer_root_page_state_record(page_state));
    }
    if (states.empty()) {
        XmlElementSlice fallback;
        fallback.attrs = " name=\"Страница1\"";
        states.push_back(source_writer_root_page_state_record(fallback));
    }
    return states;
}

std::vector<LV> source_writer_layout_dependency_sequence(const XmlElementSlice& layout) {
    std::vector<LV> sequence;
    for (const auto& group : find_xml_elements(layout.body, "LayoutDependencyGroup")) {
        std::vector<LV> dependencies;
        for (const auto& dependency : find_xml_elements(group.body, "LayoutDependency")) {
            dependencies.push_back(list({
                raw("0"),
                raw(xml_attr_value(dependency.attrs, "targetId").empty() ? "0" : xml_attr_value(dependency.attrs, "targetId")),
                raw(layout_dimension_code(xml_attr_value(dependency.attrs, "dimension"))),
            }));
        }
        sequence.push_back(raw(std::to_string(dependencies.size())));
        sequence.insert(sequence.end(), std::make_move_iterator(dependencies.begin()), std::make_move_iterator(dependencies.end()));
    }
    if (sequence.empty()) {
        sequence.push_back(raw("0"));
    }
    sequence.push_back(raw("0"));
    sequence.push_back(raw("0"));
    return sequence;
}

std::vector<LV> source_writer_root_page_layout_records(const XmlElementSlice& layout) {
    std::vector<LV> records;
    for (const auto& page_layout : find_xml_elements(layout.body, "PageLayout")) {
        const std::string page = xml_attr_value(page_layout.attrs, "page").empty() ? "0" : xml_attr_value(page_layout.attrs, "page");
        const std::string left = xml_attr_value(page_layout.attrs, "left").empty() ? "8" : xml_attr_value(page_layout.attrs, "left");
        const std::string top = xml_attr_value(page_layout.attrs, "top").empty() ? "33" : xml_attr_value(page_layout.attrs, "top");
        const std::string width = xml_attr_value(page_layout.attrs, "width").empty() ? "0" : xml_attr_value(page_layout.attrs, "width");
        const std::string height = xml_attr_value(page_layout.attrs, "height").empty() ? "0" : xml_attr_value(page_layout.attrs, "height");
        const std::string horizontal_mode = xml_attr_value(page_layout.attrs, "horizontalMode").empty() ? "0" : xml_attr_value(page_layout.attrs, "horizontalMode");
        const std::string vertical_mode = xml_attr_value(page_layout.attrs, "verticalMode").empty() ? "0" : xml_attr_value(page_layout.attrs, "verticalMode");
        records.push_back(list({raw("2"), raw(left), raw("1"), raw("1"), raw("1"), raw(page), raw("0"), raw("0"), raw("0")}));
        records.push_back(list({raw("2"), raw(top), raw("0"), raw("1"), raw("2"), raw(page), raw("0"), raw("0"), raw("0")}));
        records.push_back(list({raw("2"), raw(width), raw("1"), raw("1"), raw("3"), raw(page), raw("0"), raw(horizontal_mode), raw("0")}));
        records.push_back(list({raw("2"), raw(height), raw("0"), raw("1"), raw("4"), raw(page), raw("0"), raw(vertical_mode), raw("0")}));
    }
    if (records.empty()) {
        records.push_back(list({raw("2"), raw("8"), raw("1"), raw("1"), raw("1"), raw("0"), raw("0"), raw("0"), raw("0")}));
        records.push_back(list({raw("2"), raw("33"), raw("0"), raw("1"), raw("2"), raw("0"), raw("0"), raw("0"), raw("0")}));
        records.push_back(list({raw("2"), raw("0"), raw("1"), raw("1"), raw("3"), raw("0"), raw("0"), raw("0"), raw("0")}));
        records.push_back(list({raw("2"), raw("0"), raw("0"), raw("1"), raw("4"), raw("0"), raw("0"), raw("0"), raw("0")}));
    }
    return records;
}

LV source_writer_root_panel_info_from_layout_xml(const std::string& xml) {
    const auto layout_xml = first_xml_element(xml, "RootPanelLayout");
    if (layout_xml.self_closing && layout_xml.body.empty()) {
        return {};
    }
    std::vector<LV> states = source_writer_root_page_state_records(layout_xml);
    std::vector<LV> state_table;
    state_table.push_back(raw("1"));
    state_table.push_back(raw(std::to_string(states.size())));
    state_table.insert(state_table.end(), std::make_move_iterator(states.begin()), std::make_move_iterator(states.end()));

    std::vector<LV> position_records = source_writer_root_page_layout_records(layout_xml);
    std::vector<LV> body{
        source_writer_root_panel_base_info_record(),
        raw("26"),
    };
    auto dependency_sequence = source_writer_layout_dependency_sequence(layout_xml);
    body.insert(body.end(), std::make_move_iterator(dependency_sequence.begin()), std::make_move_iterator(dependency_sequence.end()));
    body.push_back(source_writer_page_style_group_record("1"));
    body.push_back(raw(xml_attr_value(layout_xml.attrs, "pageStateFlag").empty() ? "0" : xml_attr_value(layout_xml.attrs, "pageStateFlag")));
    body.push_back(raw(xml_attr_value(layout_xml.attrs, "currentPageIndex").empty() ? "1" : xml_attr_value(layout_xml.attrs, "currentPageIndex")));
    body.push_back(list(std::move(state_table)));
    body.push_back(raw("1"));
    body.push_back(raw("1"));
    body.push_back(raw("0"));
    body.push_back(raw(std::to_string(position_records.size())));
    body.insert(body.end(), std::make_move_iterator(position_records.begin()), std::make_move_iterator(position_records.end()));
    body.push_back(raw("0"));
    body.push_back(raw("4294967295"));
    body.push_back(raw("5"));
    body.push_back(raw("64"));
    body.push_back(raw("0"));
    body.push_back(source_writer_default_color_record());
    body.push_back(raw("0"));
    body.push_back(raw("0"));
    body.push_back(raw("57"));
    body.push_back(raw("0"));
    body.push_back(raw("0"));
    return list({raw("1"), list(std::move(body)), list({raw("0")})});
}

LV source_writer_form_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view title
) {
    return list({
        raw("27"),
        source_writer_root_record(form_object, title),
        source_writer_attributes_table(form_object),
        source_writer_form_object_info(form_object),
        list({raw("0")}),
        raw("1"),
        raw("4"),
        raw("1"),
        raw("0"),
        raw("0"),
        raw("0"),
        list({raw("0")}),
        list({raw("0")}),
        list({
            raw("10"), raw("0"),
            list({raw("4"), raw("0"), list({raw("0")}), str_atom(""), raw("-1"), raw("-1"), raw("1"), raw("0"), str_atom("")}),
            list({raw("4"), raw("0"), list({raw("0")}), str_atom(""), raw("-1"), raw("-1"), raw("1"), raw("0"), str_atom("")}),
            list({raw("4"), raw("0"), list({raw("0")}), str_atom(""), raw("-1"), raw("-1"), raw("1"), raw("0"), str_atom("")}),
            raw("100"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
        }),
        raw("1"),
        raw("2"),
        raw("0"),
        raw("0"),
        raw("1"),
        raw("1"),
    });
}

std::vector<std::uint8_t> source_writer_form_payload_bytes(const LV& payload) {
    const std::string text = oof::platform::stream::dump_compact(payload);
    std::vector<std::uint8_t> out{0xef, 0xbb, 0xbf};
    out.insert(out.end(), text.begin(), text.end());
    return out;
}

void write_formbin_from_source_package(
    const std::string& xml_path,
    const std::string& output_path
) {
    const auto xml_package_path = std::filesystem::path(xml_path);
    std::size_t picture_sidecars_read = 0;
    const std::string source_xml = read_file_text_lossy(xml_path);
    const std::string package_xml = inline_picture_sidecars_for_build(
        source_xml,
        xml_package_path,
        picture_sidecars_read);
    const auto form_object = platform_form_object_from_public_xml(package_xml);
    const std::string title = public_form_title_from_xml(package_xml);

    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    container.files.push_back({
        "form",
        0,
        0,
        source_writer_form_payload_bytes(source_writer_form_payload(form_object, title)),
    });

    const auto module_path = form_package_module_path(xml_package_path);
    bool module_sidecar_used = false;
    std::size_t module_bytes = 0;
    std::vector<std::uint8_t> module_payload;
    if (std::filesystem::is_regular_file(module_path)) {
        module_payload = read_file_bytes(module_path.string());
        module_sidecar_used = true;
        module_bytes = module_payload.size();
    }
    container.files.push_back({"module", 0, 0, std::move(module_payload)});

    const auto rebuilt = oof::platform::formbin::serialize_container(container);
    write_file_bytes(output_path, rebuilt);
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"formbin-build-source-package\"";
    std::cout << ",\"bytes\":" << rebuilt.size();
    std::cout << ",\"source\":\"Form.xml\"";
    std::cout << ",\"controls\":" << form_object.items.count();
    std::cout << ",\"moduleSource\":";
    print_json_string(module_sidecar_used ? "sidecar" : "empty");
    std::cout << ",\"moduleBytes\":" << module_bytes;
    std::cout << ",\"pictureSidecarsRead\":" << picture_sidecars_read;
    std::cout << ",\"usesBaseBin\":false";
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << "}\n";
}

void write_formbin_platform_object_set(
    const std::string& input_path,
    const std::string& output_path,
    std::string_view object_id,
    std::string_view property_name,
    std::string_view new_value
) {
    const auto& descriptor = require_property_descriptor(property_name);
    if (!oof::platform::property_registry::can_set_with_current_codec(descriptor)) {
        throw std::runtime_error("property is registered but its slot codec is not writable yet through native Form.bin setPropVal: " +
                                 std::string(property_name) + " codec=" +
                                 std::string(oof::platform::property_registry::slot_codec_name(descriptor.slot_codec)));
    }

    const std::vector<std::uint8_t> data = read_file_bytes(input_path);
    auto container = oof::platform::formbin::parse_container(data);
    auto file_it = container.files.end();
    for (auto it = container.files.begin(); it != container.files.end(); ++it) {
        if (it->name == "form") {
            file_it = it;
            break;
        }
    }
    if (file_it == container.files.end()) {
        throw std::runtime_error("Form.bin does not contain required logical file");
    }

    RuntimeFormEnvelope envelope = runtime_envelope_from_form_payload(file_it->payload);
    oof::platform::object_model::PlatformFormObjectEdit object_edit;
    object_edit.object(std::string(object_id)).set_property(std::string(descriptor.name), std::string(new_value));
    apply_platform_object_edits(envelope, object_edit);
    file_it->payload = encode_form_payload_text(file_it->payload, envelope.payload);

    const auto rebuilt = oof::platform::formbin::serialize_container(container);
    write_file_bytes(output_path, rebuilt);
    const auto form_object = materialize_platform_form_object(envelope);
    const auto* changed = form_object.find_object_by_id(object_id);

    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"bytes\":" << rebuilt.size();
    std::cout << ",\"operation\":\"setPropVal\"";
    std::cout << ",\"source\":\"Form.bin:PlatformObject\"";
    std::cout << ",\"objectId\":";
    print_json_string(object_id);
    std::cout << ",\"property\":";
    print_json_string(property_name);
    std::cout << ",\"descriptorName\":";
    print_json_string(descriptor.name);
    std::cout << ",\"value\":";
    print_json_string(new_value);
    std::cout << ",\"slotBinding\":";
    print_json_string(descriptor.slot_binding);
    std::cout << ",\"slotCodec\":";
    print_json_string(oof::platform::property_registry::slot_codec_name(descriptor.slot_codec));
    std::cout << ",\"preservedContainerFiles\":" << container.files.size();
    std::cout << ",\"changedObject\":";
    if (changed != nullptr) {
        print_platform_object_json(*changed);
    } else {
        std::cout << "null";
    }
    std::cout << "}\n";
}

void print_formbin_xml_coverage(const std::string& input_path) {
    RuntimeFormEnvelope envelope = read_formbin_runtime_envelope(input_path);
    const auto summary = summarize_materialized_graph(envelope.payload);
    std::cout << "{\"source\":\"Form.bin:form\"";
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << ",\"nativeXmlProjection\":true";
    std::cout << ",\"nativeXmlWriter\":true";
    std::cout << ",\"supportedEditCodecs\":[\"Name\",\"Title\",\"Visible\",\"Enabled\",\"Position\",\"Binding:value\",\"Binding:anchor-list\",\"DimensionBinding:value\",\"DimensionBinding:record\",\"Attribute.Name\",\"Command.Name\",\"Command.Handler\",\"Command.ModifiesData\",\"Event.Handler\",\"DeleteLeafControl\",\"Form.bin.PlatformObject.getPropVal\",\"Form.bin.PlatformObject.setPropVal\"]";
    std::cout << ",\"materializedItems\":" << summary.items.size();
    std::cout << ",\"namedItems\":" << summary.named_items;
    std::cout << ",\"schemaBackedItems\":" << summary.schema_backed_items;
    std::cout << ",\"missingCodecs\":[\"cf_form_controls8 color/font/picture/control-specific typed payload fields\"]";
    std::cout << "}\n";
}

void replace_all(std::string& value, std::string_view needle, std::string_view replacement) {
    std::size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
        value.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
}

void print_formbin_package_selftest() {
    const std::string form_text =
        "{{\"MainCaption\",1,1,{\"ru\",\"Main\"}},"
        "{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{1,1,{\"ru\",\"Run\"}}},"
        "{8,1,2,101,22,0,0,0,0,0,0,0,0,0,0,0,0},{14,\"Button1\",4294967295,0,0,0},{0}}}";
    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    container.files.push_back({"form", 11, 22, std::vector<std::uint8_t>(form_text.begin(), form_text.end())});
    container.files.push_back({"module", 33, 44, {'m', 'o', 'd'}});

    RuntimeFormEnvelope envelope = runtime_envelope_from_form_payload(container.files[0].payload);
    std::string xml = form_object_to_public_xml(materialize_platform_form_object(envelope));
    replace_all(xml, "name=\"Button1\"", "name=\"ButtonXmlEdited\"");
    replace_all(xml, "<Title>Run</Title>", "<Title>RunXmlEdited</Title>");
    replace_all(xml, "left=\"1\"", "left=\"9\"");
    replace_all(xml, "coordinate=\"left\" value=\"0\"", "coordinate=\"left\" value=\"21\"");
    replace_all(xml, "dimension=\"width\" value=\"0\"", "dimension=\"width\" value=\"2\"");
    const auto xml_form_object = platform_form_object_from_public_xml(xml);
    const std::string object_redump_xml = form_object_to_public_xml(xml_form_object);

    const auto edits = parse_public_xml_control_edits(xml);
    const auto result = apply_public_xml_edits(envelope, edits);
    container.files[0].payload = encode_form_payload_text(container.files[0].payload, envelope.payload);
    const auto rebuilt = oof::platform::formbin::serialize_container(container);
    const auto reparsed = oof::platform::formbin::parse_container(rebuilt);
    const auto redump_envelope = runtime_envelope_from_form_payload(find_container_file(reparsed, "form").payload);
    const std::string redump_xml = form_object_to_public_xml(materialize_platform_form_object(redump_envelope));
    const auto& module = find_container_file(reparsed, "module");

    const std::string anchor_form_text =
        "{{\"MainCaption\",1,1,{\"ru\",\"Main\"}},"
        "{6ff79819-710e-4145-97cd-1618da79e3e2,4,{1,{1,1,{\"ru\",\"Panel\"}}},"
        "{8,0,0,200,100,0,0,0,0,0,0,0,0,0,0,0,0},{14,\"PanelHost\",4294967295,0,0,0},{0}},"
        "{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{1,1,{\"ru\",\"Run\"}}},"
        "{8,1,2,101,22,0,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},"
        "{0,{2,4,2,0},{2,-1,6,0}},0,0,0,0,{0,4,1},0,0,0},{14,\"Button1\",4294967295,0,0,0},{0}}}";
    RuntimeFormEnvelope anchor_envelope = runtime_envelope_from_form_payload(
        std::vector<std::uint8_t>(anchor_form_text.begin(), anchor_form_text.end()));
    std::string anchor_xml = form_object_to_public_xml(materialize_platform_form_object(anchor_envelope));
    const bool anchor_name_visible = anchor_xml.find("targetName=\"PanelHost\"") != std::string::npos;
    replace_all(anchor_xml, "targetId=\"4\" targetName=\"PanelHost\" side=\"left\" offset=\"0\"",
                "targetId=\"4\" targetName=\"PanelHost\" side=\"left\" offset=\"7\"");
    const auto anchor_xml_form_object = platform_form_object_from_public_xml(anchor_xml);
    const std::string anchor_object_redump_xml = form_object_to_public_xml(anchor_xml_form_object);
    const auto anchor_edits = parse_public_xml_control_edits(anchor_xml);
    const auto anchor_result = apply_public_xml_edits(anchor_envelope, anchor_edits);
    const std::string anchor_redump_xml = form_object_to_public_xml(materialize_platform_form_object(anchor_envelope));

    std::cout << "{\"operation\":\"formbin-package-selftest\"";
    std::cout << ",\"nameEdits\":" << result.name_edits;
    std::cout << ",\"titleEdits\":" << result.title_edits;
    std::cout << ",\"positionEdits\":" << result.position_edits;
    std::cout << ",\"bindingEdits\":" << result.binding_edits;
    std::cout << ",\"dimensionBindingEdits\":" << result.dimension_binding_edits;
    std::cout << ",\"anchorBindingEdits\":" << anchor_result.binding_edits;
    std::cout << ",\"anchorTargetNameVisible\":" << (anchor_name_visible ? "true" : "false");
    std::cout << ",\"xmlToPlatformFormObject\":true";
    std::cout << ",\"xmlObjectItems\":" << xml_form_object.items.count();
    std::cout << ",\"xmlObjectNestedItems\":" << anchor_xml_form_object.items.count();
    std::cout << ",\"xmlObjectNameRoundtrip\":"
              << (object_redump_xml.find("ButtonXmlEdited") != std::string::npos ? "true" : "false");
    std::cout << ",\"xmlObjectTitleRoundtrip\":"
              << (object_redump_xml.find("RunXmlEdited") != std::string::npos ? "true" : "false");
    std::cout << ",\"xmlObjectPositionRoundtrip\":"
              << (object_redump_xml.find("<Position left=\"9\" top=\"2\" right=\"101\" bottom=\"22\"") != std::string::npos ? "true" : "false");
    std::cout << ",\"xmlObjectBindingRoundtrip\":"
              << (object_redump_xml.find("<Binding coordinate=\"left\" value=\"21\"/>") != std::string::npos ? "true" : "false");
    std::cout << ",\"xmlObjectDimensionBindingRoundtrip\":"
              << (object_redump_xml.find("<DimensionBinding dimension=\"width\" value=\"2\"/>") != std::string::npos ? "true" : "false");
    std::cout << ",\"xmlObjectChildItemsRoundtrip\":"
              << (anchor_object_redump_xml.find("name=\"PanelHost\" id=\"4\"") != std::string::npos &&
                  anchor_object_redump_xml.find("name=\"Button1\" id=\"5\"") != std::string::npos ? "true" : "false");
    std::cout << ",\"xmlObjectNoRawXml\":"
              << (object_redump_xml.find("<ListStream") == std::string::npos &&
                  object_redump_xml.find("<RawBracket") == std::string::npos &&
                  object_redump_xml.find("<PlatformRecords") == std::string::npos &&
                  object_redump_xml.find("<FormBin") == std::string::npos ? "true" : "false");
    std::cout << ",\"anchorBindingRoundtrip\":"
              << (anchor_redump_xml.find("targetId=\"4\" targetName=\"PanelHost\" side=\"left\" offset=\"7\"") != std::string::npos ? "true" : "false");
    std::cout << ",\"dimensionRecordRoundtrip\":"
              << (anchor_redump_xml.find("<DimensionBinding dimension=\"height\" mode=\"0\" target=\"element\" targetId=\"4\" targetName=\"PanelHost\" side=\"bottom\"/>") != std::string::npos ? "true" : "false");
    std::cout << ",\"nameRoundtrip\":"
              << (redump_xml.find("ButtonXmlEdited") != std::string::npos ? "true" : "false");
    std::cout << ",\"titleRoundtrip\":"
              << (redump_xml.find("RunXmlEdited") != std::string::npos ? "true" : "false");
    std::cout << ",\"positionRoundtrip\":"
              << (redump_xml.find("<Position left=\"9\" top=\"2\" right=\"101\" bottom=\"22\"") != std::string::npos ? "true" : "false");
    std::cout << ",\"bindingRoundtrip\":"
              << (redump_xml.find("<Binding coordinate=\"left\" value=\"21\"/>") != std::string::npos ? "true" : "false");
    std::cout << ",\"dimensionBindingRoundtrip\":"
              << (redump_xml.find("<DimensionBinding dimension=\"width\" value=\"2\"/>") != std::string::npos ? "true" : "false");
    std::cout << ",\"modulePreserved\":"
              << (module.payload == std::vector<std::uint8_t>({'m', 'o', 'd'}) ? "true" : "false");
    std::cout << ",\"noRawXml\":"
              << (redump_xml.find("ListStream") == std::string::npos ? "true" : "false");
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << "}\n";
}

void print_formbin_platform_object_selftest() {
    const std::string form_text =
        "{{\"MainCaption\",1,1,{\"ru\",\"Main\"}},"
        "{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{1,1,{\"ru\",\"Run\"}}},"
        "{8,1,2,101,22,0,0,0,0,0,0,0,0,0,0,0,0},{14,\"Button1\",4294967295,0,0,0},{0}}}";
    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    container.files.push_back({"form", 11, 22, std::vector<std::uint8_t>(form_text.begin(), form_text.end())});
    container.files.push_back({"module", 33, 44, {'m', 'o', 'd'}});

    const auto encoded = oof::platform::formbin::serialize_container(container);
    auto parsed = oof::platform::formbin::parse_container(encoded);
    auto form_it = parsed.files.end();
    for (auto it = parsed.files.begin(); it != parsed.files.end(); ++it) {
        if (it->name == "form") {
            form_it = it;
            break;
        }
    }
    if (form_it == parsed.files.end()) {
        throw std::runtime_error("Form.bin selftest container lost form file");
    }

    RuntimeFormEnvelope envelope = runtime_envelope_from_form_payload(form_it->payload);
    auto form_object = materialize_platform_form_object(envelope);
    auto* button = form_object.find_object_by_id("5");
    auto* picture = button == nullptr ? nullptr : button->property("Picture");
    const std::string before_name = button == nullptr ? "" : form_object.get_prop_val("5", "Name");
    const std::string picture_value = picture == nullptr ? "" : picture->value;
    const std::string picture_member = picture == nullptr ? "" : picture->platform_member;
    const std::string picture_object_class = picture == nullptr ? "" : picture->value_object_class;
    const std::string picture_constructor = picture == nullptr ? "" : picture->value_object_constructor;
    const std::string picture_storage = picture == nullptr ? "" : picture->value_object_storage;
    bool explicit_picture_xml = false;
    bool explicit_color_xml = false;
    bool explicit_font_xml = false;
    if (picture != nullptr) {
        picture->value = "#base64:R0lGODlh";
        picture->value_origin = "stream";
        enrich_platform_value_object(*picture);
    }
    if (button != nullptr) {
        if (auto* text_color = button->property("TextColor")) {
            text_color->value = "#123456";
            text_color->value_origin = "stream";
            enrich_platform_value_object(*text_color);
        }
        if (auto* font = button->property("Font")) {
            font->value = "sys:DefaultGUIFont";
            font->value_origin = "stream";
            enrich_platform_value_object(*font);
        }
        const std::string value_xml = form_object_to_public_xml(form_object);
        explicit_picture_xml =
            value_xml.find("<Picture>") != std::string::npos &&
            value_xml.find("<PictureValue constructor=\"New Picture\" storage=\"inline-base64\">#base64:R0lGODlh</PictureValue>") != std::string::npos;
        explicit_color_xml =
            value_xml.find("<TextColor>") != std::string::npos &&
            value_xml.find("<ColorValue constructor=\"New Color\" storage=\"absolute-rgb\">#123456</ColorValue>") != std::string::npos;
        explicit_font_xml =
            value_xml.find("<Font>") != std::string::npos &&
            value_xml.find("<FontValue constructor=\"New Font\" storage=\"style\">sys:DefaultGUIFont</FontValue>") != std::string::npos;
    }

    oof::platform::object_model::PlatformFormObjectEdit object_edit;
    object_edit.object("5").set_property("Title", "ButtonFromFormBinObject");
    const auto result = apply_platform_object_edits(envelope, object_edit);
    form_it->payload = encode_form_payload_text(form_it->payload, envelope.payload);

    const auto rebuilt = oof::platform::formbin::serialize_container(parsed);
    const auto reparsed = oof::platform::formbin::parse_container(rebuilt);
    const RuntimeFormEnvelope redump_envelope = runtime_envelope_from_form_payload(find_container_file(reparsed, "form").payload);
    const auto redump_object = materialize_platform_form_object(redump_envelope);
    const auto& module = find_container_file(reparsed, "module");

    std::cout << "{\"operation\":\"formbin-platform-object-selftest\"";
    std::cout << ",\"source\":\"Form.bin:PlatformObject\"";
    std::cout << ",\"beforeName\":";
    print_json_string(before_name);
    std::cout << ",\"pictureValue\":";
    print_json_string(picture_value);
    std::cout << ",\"picturePlatformMember\":";
    print_json_string(picture_member);
    std::cout << ",\"pictureObjectClass\":";
    print_json_string(picture_object_class);
    std::cout << ",\"pictureConstructor\":";
    print_json_string(picture_constructor);
    std::cout << ",\"pictureStorage\":";
    print_json_string(picture_storage);
    std::cout << ",\"explicitPictureXml\":"
              << (explicit_picture_xml ? "true" : "false");
    std::cout << ",\"explicitColorXml\":"
              << (explicit_color_xml ? "true" : "false");
    std::cout << ",\"explicitFontXml\":"
              << (explicit_font_xml ? "true" : "false");
    std::cout << ",\"titleEdits\":" << result.title_edits;
    std::cout << ",\"titleRoundtrip\":"
              << (redump_object.get_prop_val("5", "Title") == "ButtonFromFormBinObject" ? "true" : "false");
    std::cout << ",\"modulePreserved\":"
              << (module.payload == std::vector<std::uint8_t>({'m', 'o', 'd'}) ? "true" : "false");
    std::cout << ",\"publicContract\":\"PlatformObject\"";
    std::cout << "}\n";
}

RuntimeFormEnvelope read_runtime_form_envelope_file(const std::string& path, std::string& canonical_text) {
    const auto bytes = read_file_bytes(path);
    const std::string text = decode_text_file_bytes(bytes);
    RuntimeFormEnvelope envelope = parse_runtime_form_envelope(text);
    canonical_text = dump_runtime_form_envelope(envelope);
    return envelope;
}

void print_runtime_form_object_graph(const std::string& path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(path, canonical_text);
    const std::string payload_text = oof::platform::stream::dump_compact(envelope.payload);
    const auto summary = summarize_materialized_graph(envelope.payload);

    std::cout << "{\"source\":\"RuntimeForm:payload\"";
    std::cout << ",\"runtimeEnvelope\":{\"marker\":";
    print_json_string(envelope.marker);
    std::cout << ",\"runtimeUuid\":";
    print_json_string(envelope.runtime_uuid);
    std::cout << ",\"canonicalBytes\":" << canonical_text.size() << "}";
    std::cout << ",\"payloadSize\":" << payload_text.size();
    std::cout << ",\"rootArity\":" << envelope.payload.items.size();
    std::cout << ",\"rootVersion\":";
    print_json_string(envelope.payload.items[0].atom);
    if (envelope.payload.items.size() > 1 && envelope.payload.items[1].is_list && !envelope.payload.items[1].items.empty() && !envelope.payload.items[1].items[0].is_list) {
        std::cout << ",\"formSectionVersion\":";
        print_json_string(envelope.payload.items[1].items[0].atom);
    }
    std::cout << ",";
    print_materialized_graph_json(summary);
    std::cout << "}\n";
}

void print_runtime_form_roundtrip(const std::string& path) {
    const auto input_bytes = read_file_bytes(path);
    const std::string input_text = decode_text_file_bytes(input_bytes);
    RuntimeFormEnvelope envelope = parse_runtime_form_envelope(input_text);
    const std::string canonical_text = dump_runtime_form_envelope(envelope);
    RuntimeFormEnvelope reparsed = parse_runtime_form_envelope(canonical_text);
    const std::string rebuilt_text = dump_runtime_form_envelope(reparsed);
    const std::string payload_text = oof::platform::stream::dump_compact(envelope.payload);
    const std::string rebuilt_payload_text = oof::platform::stream::dump_compact(reparsed.payload);
    const auto summary = summarize_materialized_graph(reparsed.payload);

    std::cout << "{\"inputBytes\":" << input_bytes.size();
    std::cout << ",\"canonicalBytes\":" << canonical_text.size();
    std::cout << ",\"runtimeUuid\":";
    print_json_string(envelope.runtime_uuid);
    std::cout << ",\"payloadRootVersion\":";
    print_json_string(envelope.payload.items[0].atom);
    std::cout << ",\"canonicalRoundtripEqual\":"
              << (canonical_text == rebuilt_text ? "true" : "false");
    std::cout << ",\"payloadRoundtripEqual\":"
              << (payload_text == rebuilt_payload_text ? "true" : "false");
    std::cout << ",\"materializedItems\":" << summary.items.size();
    std::cout << ",\"namedItems\":" << summary.named_items;
    std::cout << ",\"schemaBackedItems\":" << summary.schema_backed_items;
    std::cout << ",\"nestedUnboundGuidNodes\":" << summary.nested_unbound_guid_nodes;
    std::cout << "}\n";
}

void write_runtime_form_rebuild(const std::string& input_path, const std::string& output_path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    const std::vector<std::uint8_t> output(canonical_text.begin(), canonical_text.end());
    write_file_bytes(output_path, output);
    const auto summary = summarize_materialized_graph(envelope.payload);

    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"bytes\":" << output.size();
    std::cout << ",\"runtimeUuid\":";
    print_json_string(envelope.runtime_uuid);
    std::cout << ",\"payloadRootVersion\":";
    print_json_string(envelope.payload.items[0].atom);
    std::cout << ",\"materializedItems\":" << summary.items.size();
    std::cout << ",\"namedItems\":" << summary.named_items;
    std::cout << ",\"schemaBackedItems\":" << summary.schema_backed_items;
    std::cout << "}\n";
}

bool rename_platform_name_record(oof::platform::stream::ListValue& value, std::string_view new_name) {
    if (!value.is_list) {
        return false;
    }
    if (is_platform_name_record(value)) {
        value.items[1].atom = std::string(new_name);
        value.items[1].atom_kind = oof::platform::stream::ListValue::AtomKind::string;
        return true;
    }
    for (auto& item : value.items) {
        if (rename_platform_name_record(item, new_name)) {
            return true;
        }
    }
    return false;
}

bool rename_materialized_object(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    std::string_view new_name
) {
    if (!value.is_list) {
        return false;
    }
    if (is_materializable_object_candidate(value) &&
        !value.items[1].is_list &&
        value.items[1].atom == object_id) {
        return rename_platform_name_record(value, new_name);
    }
    for (auto& item : value.items) {
        if (rename_materialized_object(item, object_id, new_name)) {
            return true;
        }
    }
    return false;
}

bool set_first_localized_text(oof::platform::stream::ListValue& value, std::string_view text) {
    if (!value.is_list) {
        return false;
    }
    if (value.items.size() >= 2 &&
        !value.items[0].is_list &&
        !value.items[1].is_list &&
        value.items[0].atom_kind == oof::platform::stream::ListValue::AtomKind::string &&
        value.items[1].atom_kind == oof::platform::stream::ListValue::AtomKind::string) {
        value.items[1].atom = std::string(text);
        return true;
    }
    for (auto& item : value.items) {
        if (set_first_localized_text(item, text)) {
            return true;
        }
    }
    return false;
}

bool set_materialized_object_title(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    std::string_view title
) {
    if (!value.is_list) {
        return false;
    }
    if (is_materializable_object_candidate(value) &&
        !value.items[1].is_list &&
        value.items[1].atom == object_id) {
        if (value.items.size() > 2 && value.items[2].is_list) {
            return set_first_localized_text(value.items[2], title);
        }
        return false;
    }
    for (auto& item : value.items) {
        if (set_materialized_object_title(item, object_id, title)) {
            return true;
        }
    }
    return false;
}

bool set_geometry_atom(
    oof::platform::stream::ListValue& geometry,
    std::size_t index,
    std::string_view value
) {
    if (!geometry.is_list || geometry.items.size() <= index || !is_int_atom(geometry.items[index])) {
        return false;
    }
    geometry.items[index].atom = std::string(value);
    geometry.items[index].atom_kind = oof::platform::stream::ListValue::AtomKind::raw;
    return true;
}

oof::platform::stream::ListValue* find_immediate_geometry_record_mut(
    oof::platform::stream::ListValue& value
) {
    if (!value.is_list) {
        return nullptr;
    }
    for (auto& item : value.items) {
        if (is_geometry_record(item)) {
            return &item;
        }
    }
    return nullptr;
}

oof::platform::stream::ListValue* find_base_info_record_mut(
    oof::platform::stream::ListValue& value
) {
    if (!value.is_list) {
        return nullptr;
    }
    if (is_base_info_record(value)) {
        return &value;
    }
    for (auto& item : value.items) {
        if (auto* found = find_base_info_record_mut(item)) {
            return found;
        }
    }
    return nullptr;
}

std::int64_t parse_required_int(std::string_view value, std::string_view field_name) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoll(std::string(value), &consumed, 10);
        if (consumed != value.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error("expected integer value for " + std::string(field_name) + ": " + std::string(value));
    }
}

bool set_materialized_object_position_property(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    std::string_view property_name,
    std::string_view new_value
) {
    if (!value.is_list) {
        return false;
    }
    if (is_materializable_object_candidate(value) &&
        !value.items[1].is_list &&
        value.items[1].atom == object_id) {
        auto* geometry = find_immediate_geometry_record_mut(value);
        if (geometry == nullptr) {
            return false;
        }
        const auto left = parse_required_int(geometry->items[1].atom, "Left");
        const auto top = parse_required_int(geometry->items[2].atom, "Top");
        const auto right = parse_required_int(geometry->items[3].atom, "Right");
        const auto bottom = parse_required_int(geometry->items[4].atom, "Bottom");
        const auto numeric_value = parse_required_int(new_value, property_name);
        if (property_name == "Left" || property_name == "Лево") {
            return set_geometry_atom(*geometry, 1, std::to_string(numeric_value));
        }
        if (property_name == "Top" || property_name == "Верх") {
            return set_geometry_atom(*geometry, 2, std::to_string(numeric_value));
        }
        if (property_name == "Width" || property_name == "Ширина") {
            return set_geometry_atom(*geometry, 3, std::to_string(left + numeric_value));
        }
        if (property_name == "Height" || property_name == "Высота") {
            return set_geometry_atom(*geometry, 4, std::to_string(top + numeric_value));
        }
        if (property_name == "Right") {
            return set_geometry_atom(*geometry, 3, std::to_string(numeric_value));
        }
        if (property_name == "Bottom") {
            return set_geometry_atom(*geometry, 4, std::to_string(numeric_value));
        }
        (void)right;
        (void)bottom;
        return false;
    }
    for (auto& item : value.items) {
        if (set_materialized_object_position_property(item, object_id, property_name, new_value)) {
            return true;
        }
    }
    return false;
}

bool set_base_info_bool_atom(
    oof::platform::stream::ListValue& base_info,
    std::size_t slot,
    std::string_view value
) {
    if (!base_info.is_list || base_info.items.size() <= slot || base_info.items[slot].is_list) {
        return false;
    }
    base_info.items[slot].atom = platform_bool_atom(value);
    base_info.items[slot].atom_kind = oof::platform::stream::ListValue::AtomKind::raw;
    return true;
}

bool set_materialized_object_scalar_flag(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    std::string_view property_name,
    std::string_view new_value
) {
    if (!value.is_list) {
        return false;
    }
    if (is_materializable_object_candidate(value) &&
        !value.items[1].is_list &&
        value.items[1].atom == object_id) {
        auto* base_info = find_base_info_record_mut(value);
        if (base_info == nullptr) {
            return false;
        }
        if (property_name == "Visible" || property_name == "Видимость") {
            return set_base_info_bool_atom(*base_info, 1, new_value);
        }
        if (property_name == "Enabled" || property_name == "Доступность") {
            return set_base_info_bool_atom(*base_info, 5, new_value);
        }
        return false;
    }
    for (auto& item : value.items) {
        if (set_materialized_object_scalar_flag(item, object_id, property_name, new_value)) {
            return true;
        }
    }
    return false;
}

bool set_materialized_object_bindings(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    const std::vector<GeometryBindingRecord>& bindings
) {
    if (!value.is_list) {
        return false;
    }
    if (is_materializable_object_candidate(value) &&
        !value.items[1].is_list &&
        value.items[1].atom == object_id) {
        auto* geometry = find_immediate_geometry_record_mut(value);
        if (geometry == nullptr) {
            return false;
        }
        bool changed = false;
        for (const auto& binding : bindings) {
            const std::size_t slot = binding_coordinate_slot(binding.name);
            if (slot == 0 || geometry->items.size() <= slot) {
                throw std::runtime_error("unsupported Binding coordinate: " + binding.name);
            }
            geometry->items[slot] = binding.value;
            changed = true;
        }
        return changed;
    }
    for (auto& item : value.items) {
        if (set_materialized_object_bindings(item, object_id, bindings)) {
            return true;
        }
    }
    return false;
}

bool set_materialized_object_dimension_bindings(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    const std::vector<GeometryBindingRecord>& bindings
) {
    if (!value.is_list) {
        return false;
    }
    if (is_materializable_object_candidate(value) &&
        !value.items[1].is_list &&
        value.items[1].atom == object_id) {
        auto* geometry = find_immediate_geometry_record_mut(value);
        if (geometry == nullptr) {
            return false;
        }
        bool changed = false;
        for (const auto& binding : bindings) {
            const std::size_t slot = dimension_binding_slot(binding.name);
            if (slot == 0 || geometry->items.size() <= slot) {
                throw std::runtime_error("unsupported DimensionBinding dimension: " + binding.name);
            }
            geometry->items[slot] = binding.value;
            changed = true;
        }
        return changed;
    }
    for (auto& item : value.items) {
        if (set_materialized_object_dimension_bindings(item, object_id, bindings)) {
            return true;
        }
    }
    return false;
}

bool replace_first_base64_payload(oof::platform::stream::ListValue& value, std::string_view payload) {
    if (!value.is_list) {
        if (value.atom.rfind("#base64:", 0) == 0) {
            value.atom = std::string(payload);
            value.atom_kind = oof::platform::stream::ListValue::AtomKind::raw;
            return true;
        }
        return false;
    }
    for (auto& item : value.items) {
        if (replace_first_base64_payload(item, payload)) {
            return true;
        }
    }
    return false;
}

bool set_materialized_object_picture_payload(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    std::string_view payload
) {
    if (!value.is_list) {
        return false;
    }
    if (is_materializable_object_candidate(value) &&
        !value.items[1].is_list &&
        value.items[1].atom == object_id) {
        return replace_first_base64_payload(value, payload);
    }
    for (auto& item : value.items) {
        if (set_materialized_object_picture_payload(item, object_id, payload)) {
            return true;
        }
    }
    return false;
}

bool set_materialized_attribute_property(
    oof::platform::stream::ListValue& payload,
    std::string_view object_id,
    std::string_view property_name,
    std::string_view new_value
) {
    constexpr std::string_view prefix = "attribute:";
    if (object_id.size() <= prefix.size() || object_id.substr(0, prefix.size()) != prefix) {
        return false;
    }
    if (property_name != "Name" && property_name != "Имя") {
        return false;
    }
    const std::string wanted_id(object_id.substr(prefix.size()));
    auto* property_block = find_materialized_form_property_block_mut(payload);
    if (property_block == nullptr || property_block->items.size() < 3 || !property_block->items[2].is_list) {
        return false;
    }
    auto& counted_properties = property_block->items[2];
    for (std::size_t index = 1; index < counted_properties.items.size(); ++index) {
        auto& record = counted_properties.items[index];
        if (!record.is_list || record.items.size() <= 4 || !record.items[0].is_list) {
            continue;
        }
        if (oof::platform::stream::dump_compact(record.items[0]) == wanted_id && !record.items[4].is_list) {
            record.items[4].atom = std::string(new_value);
            record.items[4].atom_kind = oof::platform::stream::ListValue::AtomKind::string;
            return true;
        }
    }
    return false;
}

bool set_materialized_command_property_in_block(
    oof::platform::stream::ListValue& block,
    std::string_view wanted_id,
    std::string_view property_name,
    std::string_view new_value
) {
    if (!block.is_list || is_materialized_form_property_block(block)) {
        return false;
    }
    if (looks_like_materialized_form_command_record(block) ||
        (block.items.size() >= 4 && (block.items[0].is_list || !block.items[0].atom.empty()))) {
        if (command_record_id_value(block) == wanted_id) {
            const bool tagged = looks_like_materialized_form_command_record(block);
            std::size_t slot = 0;
            if (property_name == "Name" || property_name == "Имя") {
                slot = tagged ? 2 : 1;
            } else if (property_name == "Handler" || property_name == "Обработчик") {
                slot = tagged ? 3 : 2;
            } else if (property_name == "ModifiesData" || property_name == "ИзменяетДанные") {
                slot = tagged ? 4 : 3;
            }
            if (slot != 0 && block.items.size() > slot && !block.items[slot].is_list) {
                block.items[slot].atom = std::string(new_value);
                block.items[slot].atom_kind = (property_name == "ModifiesData" || property_name == "ИзменяетДанные")
                    ? oof::platform::stream::ListValue::AtomKind::raw
                    : oof::platform::stream::ListValue::AtomKind::string;
                return true;
            }
        }
    }
    for (auto& item : block.items) {
        if (set_materialized_command_property_in_block(item, wanted_id, property_name, new_value)) {
            return true;
        }
    }
    return false;
}

bool set_materialized_command_property(
    oof::platform::stream::ListValue& payload,
    std::string_view object_id,
    std::string_view property_name,
    std::string_view new_value
) {
    constexpr std::string_view prefix = "command:";
    if (object_id.size() <= prefix.size() || object_id.substr(0, prefix.size()) != prefix) {
        return false;
    }
    const std::string wanted_id(object_id.substr(prefix.size()));
    if (!payload.is_list || payload.items.size() <= 2) {
        return false;
    }
    for (std::size_t index = 2; index < payload.items.size(); ++index) {
        if (set_materialized_command_property_in_block(payload.items[index], wanted_id, property_name, new_value)) {
            return true;
        }
    }
    return false;
}

bool set_materialized_event_property(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    std::string_view property_name,
    std::string_view new_value
) {
    constexpr std::string_view prefix = "event:";
    if (object_id.size() <= prefix.size() || object_id.substr(0, prefix.size()) != prefix) {
        return false;
    }
    if (property_name != "Handler" && property_name != "Обработчик") {
        return false;
    }
    if (!value.is_list) {
        return false;
    }
    if (looks_like_materialized_form_event_record(value)) {
        const std::string event_id = oof::platform::stream::dump_compact(value.items[1]);
        if (object_id.size() >= prefix.size() + event_id.size() &&
            object_id.substr(object_id.size() - event_id.size()) == event_id &&
            !value.items[2].is_list) {
            value.items[2].atom = std::string(new_value);
            value.items[2].atom_kind = oof::platform::stream::ListValue::AtomKind::string;
            return true;
        }
    }
    for (auto& item : value.items) {
        if (set_materialized_event_property(item, object_id, property_name, new_value)) {
            return true;
        }
    }
    return false;
}

PublicXmlApplyResult apply_public_xml_edits(
    RuntimeFormEnvelope& envelope,
    const std::vector<PublicXmlControlEdit>& edits
) {
    return apply_platform_object_edits(envelope, public_xml_edits_to_platform_object_edits(edits));
}

oof::platform::object_model::PlatformFormObjectEdit public_xml_edits_to_platform_object_edits(
    const std::vector<PublicXmlControlEdit>& edits
) {
    oof::platform::object_model::PlatformFormObjectEdit object_edit;
    for (const auto& edit : edits) {
        auto& object = object_edit.object(edit.object_id, edit.tag);
        if (edit.has_name) {
            object.set_property("Name", edit.name);
        }
        if (edit.has_title) {
            object.set_property("Title", edit.title);
        }
        if (edit.has_visible) {
            object.set_property("Visible", edit.visible);
        }
        if (edit.has_enabled) {
            object.set_property("Enabled", edit.enabled);
        }
        for (const auto& property : edit.schema_properties) {
            object.set_property(property.name, property.value);
        }
        if (edit.has_position) {
            object.set_property("Left", edit.left);
            object.set_property("Top", edit.top);
            object.set_property("Right", edit.right);
            object.set_property("Bottom", edit.bottom);
        }
        for (const auto& binding : edit.bindings) {
            object.set_property("Binding." + binding.name, oof::platform::stream::dump_compact(binding.value));
        }
        for (const auto& binding : edit.dimension_bindings) {
            object.set_property("DimensionBinding." + binding.name, oof::platform::stream::dump_compact(binding.value));
        }
    }
    return object_edit;
}

void merge_platform_form_object_edits(
    oof::platform::object_model::PlatformFormObjectEdit& target,
    const oof::platform::object_model::PlatformFormObjectEdit& source
) {
    for (const auto& source_object : source.objects) {
        auto& target_object = target.object(source_object.object_id, source_object.platform_type);
        for (const auto& property : source_object.properties) {
            target_object.set_property(property.name, property.value);
        }
    }
}

oof::platform::object_model::PlatformFormObjectEdit parse_public_xml_platform_object_edits(
    const std::string& xml
) {
    auto edits = public_xml_edits_to_platform_object_edits(parse_public_xml_control_edits(xml));
    merge_platform_form_object_edits(edits, parse_public_xml_collection_edits(xml));
    return edits;
}

oof::platform::object_model::PlatformFormObjectEdit keep_changed_platform_object_edits(
    const oof::platform::object_model::PlatformFormObjectEdit& requested,
    const oof::platform::object_model::PlatformFormObject& baseline
) {
    oof::platform::object_model::PlatformFormObjectEdit changed;
    for (const auto& requested_object : requested.objects) {
        const auto* current_object = baseline.find_object_by_id(requested_object.object_id);
        auto& changed_object = changed.object(requested_object.object_id, requested_object.platform_type);
        for (const auto& property : requested_object.properties) {
            const auto* current_property = current_object != nullptr ? current_object->property(property.name) : nullptr;
            if (current_property != nullptr && current_property->value == property.value) {
                continue;
            }
            changed_object.set_property(property.name, property.value);
        }
    }
    return changed;
}

PublicXmlApplyResult apply_platform_object_edits(
    RuntimeFormEnvelope& envelope,
    const oof::platform::object_model::PlatformFormObjectEdit& object_edit
) {
    PublicXmlApplyResult result;
    for (const auto& object : object_edit.objects) {
        if (object.properties.empty()) {
            continue;
        }
        const bool attribute_object = string_view_starts_with(object.object_id, "attribute:");
        const bool command_object = string_view_starts_with(object.object_id, "command:");
        const bool event_object = string_view_starts_with(object.object_id, "event:");
        if (!attribute_object && !command_object && !event_object) {
            ++result.controls;
        }
        for (const auto& property : object.properties) {
            if (attribute_object) {
                if (!set_materialized_attribute_property(envelope.payload, object.object_id, property.name, property.value)) {
                    throw std::runtime_error("FormAttribute property has no writable platform slot: object=" +
                                             object.object_id + " property=" + property.name);
                }
                ++result.attribute_edits;
                continue;
            }
            if (command_object) {
                if (!set_materialized_command_property(envelope.payload, object.object_id, property.name, property.value)) {
                    throw std::runtime_error("FormCommand property has no writable platform slot: object=" +
                                             object.object_id + " property=" + property.name);
                }
                ++result.command_edits;
                continue;
            }
            if (event_object) {
                if (!set_materialized_event_property(envelope.payload, object.object_id, property.name, property.value)) {
                    throw std::runtime_error("FormEvent property has no writable platform slot: object=" +
                                             object.object_id + " property=" + property.name);
                }
                ++result.event_edits;
                continue;
            }
            const auto& descriptor = require_property_descriptor(property.name);
            if (!set_property_slot_value(envelope.payload, object.object_id, descriptor, property.value)) {
                throw std::runtime_error("PlatformObject property has no writable platform slot: object=" +
                                         object.object_id + " property=" + property.name);
            }
            if (property.name == "Name") {
                ++result.name_edits;
            } else if (property.name == "Title") {
                ++result.title_edits;
            } else if (property.name == "Left") {
                ++result.position_edits;
            } else if (property.name.rfind("DimensionBinding.", 0) == 0) {
                ++result.dimension_binding_edits;
            } else if (property.name.rfind("Binding.", 0) == 0) {
                ++result.binding_edits;
            } else if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::scalar_flag) {
                ++result.scalar_flag_edits;
            }
        }
    }
    return result;
}

const oof::platform::property_registry::PlatformPropertyDescriptor& require_property_descriptor(
    std::string_view property_name
) {
    const auto* descriptor = oof::platform::property_registry::find_descriptor(property_name);
    if (descriptor == nullptr) {
        throw std::runtime_error("platform property descriptor is not registered yet: " + std::string(property_name));
    }
    return *descriptor;
}

bool string_view_starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

oof::platform::stream::ListValue parse_slot_value(std::string_view new_value) {
    if (!new_value.empty() && new_value.front() == '{') {
        return oof::platform::stream::parse(std::string(new_value));
    }
    return oof::platform::stream::ListValue::raw_atom(std::string(new_value));
}

bool set_property_slot_value(
    oof::platform::stream::ListValue& payload,
    std::string_view object_id,
    const oof::platform::property_registry::PlatformPropertyDescriptor& descriptor,
    std::string_view new_value
) {
    if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::name_record) {
        if (descriptor.name == "Title" || descriptor.name == "Caption") {
            if (set_materialized_object_title(payload, object_id, new_value)) {
                return true;
            }
            return rename_materialized_object(payload, object_id, new_value);
        }
        return rename_materialized_object(payload, object_id, new_value);
    }
    if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::position_record) {
        return set_materialized_object_position_property(payload, object_id, descriptor.name, new_value);
    }
    if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::scalar_flag) {
        return set_materialized_object_scalar_flag(payload, object_id, descriptor.name, new_value);
    }
    if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::binding_record) {
        constexpr std::string_view binding_prefix = "Binding.";
        constexpr std::string_view dimension_prefix = "DimensionBinding.";
        if (string_view_starts_with(descriptor.name, binding_prefix)) {
            const std::string coordinate(descriptor.name.substr(binding_prefix.size()));
            return set_materialized_object_bindings(payload, object_id, {{coordinate, parse_slot_value(new_value)}});
        }
        if (string_view_starts_with(descriptor.name, dimension_prefix)) {
            const std::string dimension(descriptor.name.substr(dimension_prefix.size()));
            return set_materialized_object_dimension_bindings(payload, object_id, {{dimension, parse_slot_value(new_value)}});
        }
    }
    if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::picture_record) {
        return set_materialized_object_picture_payload(payload, object_id, new_value);
    }
    throw std::runtime_error("slot codec is registered but not implemented for setPropVal yet: " +
                             std::string(oof::platform::property_registry::slot_codec_name(descriptor.slot_codec)));
}

void write_runtime_platform_object_set(
    const std::string& input_path,
    const std::string& output_path,
    std::string_view object_id,
    std::string_view property_name,
    std::string_view new_value
) {
    const auto& descriptor = require_property_descriptor(property_name);
    if (!oof::platform::property_registry::can_set_with_current_codec(descriptor)) {
        throw std::runtime_error("property is registered but its slot codec is not writable yet through native setPropVal: " +
                                 std::string(property_name) + " codec=" +
                                 std::string(oof::platform::property_registry::slot_codec_name(descriptor.slot_codec)));
    }

    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    oof::platform::object_model::PlatformFormObjectEdit object_edit;
    object_edit.object(std::string(object_id)).set_property(std::string(descriptor.name), std::string(new_value));
    apply_platform_object_edits(envelope, object_edit);

    const std::string rebuilt_text = dump_runtime_form_envelope(envelope);
    const std::vector<std::uint8_t> output(rebuilt_text.begin(), rebuilt_text.end());
    write_file_bytes(output_path, output);
    const auto form_object = materialize_platform_form_object(envelope);
    const auto* changed = form_object.find_object_by_id(object_id);

    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"bytes\":" << output.size();
    std::cout << ",\"operation\":\"setPropVal\"";
    std::cout << ",\"objectId\":";
    print_json_string(object_id);
    std::cout << ",\"property\":";
    print_json_string(property_name);
    std::cout << ",\"descriptorName\":";
    print_json_string(descriptor.name);
    std::cout << ",\"value\":";
    print_json_string(new_value);
    std::cout << ",\"slotBinding\":";
    print_json_string(descriptor.slot_binding);
    std::cout << ",\"slotCodec\":";
    print_json_string(oof::platform::property_registry::slot_codec_name(descriptor.slot_codec));
    std::cout << ",\"runtimeUuid\":";
    print_json_string(envelope.runtime_uuid);
    std::cout << ",\"changedObject\":";
    if (changed != nullptr) {
        print_platform_object_json(*changed);
    } else {
        std::cout << "null";
    }
    std::cout << "}\n";
}

void print_platform_property_registry() {
    std::map<std::string, std::size_t> codec_counts;
    std::size_t writable = 0;
    const auto& base_descriptors = oof::platform::property_registry::descriptors;
    const auto& generated_descriptors = oof::platform::property_registry::generated_api_descriptors();
    const std::size_t generated_schema_count = oof::platform::property_registry::generated_schema_descriptor_count();
    const std::size_t generated_api_count = oof::platform::property_registry::generated_api_descriptor_count();
    const std::size_t descriptor_count = base_descriptors.size() + generated_descriptors.size();
    std::cout << "{\"source\":\"PlatformPropertyDescriptorRegistry\"";
    std::cout << ",\"descriptorCount\":" << descriptor_count;
    std::cout << ",\"baseDescriptorCount\":" << base_descriptors.size();
    std::cout << ",\"generatedDescriptorCount\":" << generated_descriptors.size();
    std::cout << ",\"generatedSchemaDescriptorCount\":" << generated_schema_count;
    std::cout << ",\"generatedApiDescriptorCount\":" << generated_api_count;
    std::cout << ",\"descriptors\":[";
    std::size_t printed = 0;
    const auto print_descriptor = [&](const oof::platform::property_registry::PlatformPropertyDescriptor& descriptor,
                                      std::string_view origin) {
        if (printed != 0) {
            std::cout << ",";
        }
        ++printed;
        const std::string codec(oof::platform::property_registry::slot_codec_name(descriptor.slot_codec));
        codec_counts[codec] += 1;
        if (descriptor.writable) {
            ++writable;
        }
        std::cout << "{\"name\":";
        print_json_string(descriptor.name);
        std::cout << ",\"localizedName\":";
        print_json_string(descriptor.localized_name);
        std::cout << ",\"valueType\":";
        print_json_string(descriptor.value_type);
        std::cout << ",\"slotCodec\":";
        print_json_string(codec);
        std::cout << ",\"slotBinding\":";
        print_json_string(descriptor.slot_binding);
        std::cout << ",\"readable\":" << (descriptor.readable ? "true" : "false");
        std::cout << ",\"writable\":" << (descriptor.writable ? "true" : "false");
        std::cout << ",\"source\":";
        print_json_string(descriptor.source);
        std::cout << ",\"origin\":";
        print_json_string(origin);
        std::cout << "}";
    };
    for (const auto& descriptor : base_descriptors) {
        print_descriptor(descriptor, "static-platform-descriptor");
    }
    for (std::size_t index = 0; index < generated_descriptors.size(); ++index) {
        print_descriptor(
            generated_descriptors[index],
            index < generated_schema_count ? "generated-platform-schema-catalog" : "generated-platform-api-catalog");
    }
    std::cout << "],\"writableCount\":" << writable;
    std::cout << ",\"slotCodecCounts\":[";
    bool first = true;
    for (const auto& [codec, count] : codec_counts) {
        if (!first) {
            std::cout << ",";
        }
        first = false;
        std::cout << "{\"slotCodec\":";
        print_json_string(codec);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "]}\n";
}

void write_runtime_form_rename(
    const std::string& input_path,
    const std::string& output_path,
    std::string_view object_id,
    std::string_view new_name
) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    if (!rename_materialized_object(envelope.payload, object_id, new_name)) {
        throw std::runtime_error("runtime form object id was not found or has no platform name record: " + std::string(object_id));
    }
    const std::string rebuilt_text = dump_runtime_form_envelope(envelope);
    const std::vector<std::uint8_t> output(rebuilt_text.begin(), rebuilt_text.end());
    write_file_bytes(output_path, output);
    const auto summary = summarize_materialized_graph(envelope.payload);

    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"bytes\":" << output.size();
    std::cout << ",\"objectId\":";
    print_json_string(object_id);
    std::cout << ",\"newName\":";
    print_json_string(new_name);
    std::cout << ",\"payloadRootVersion\":";
    print_json_string(envelope.payload.items[0].atom);
    std::cout << ",\"materializedItems\":" << summary.items.size();
    std::cout << ",\"namedItems\":" << summary.named_items;
    std::cout << ",\"schemaBackedItems\":" << summary.schema_backed_items;
    std::cout << "}\n";
}

struct RuntimeSemanticDiffStats {
    std::size_t structural_diffs = 0;
    std::size_t semantic_diffs = 0;
    std::size_t volatile_guid_diffs = 0;
    std::size_t volatile_counter_diffs = 0;
    std::vector<std::string> semantic_paths;
};

bool parse_int64_atom(std::string_view value, std::int64_t& parsed) {
    if (value.empty()) {
        return false;
    }
    std::size_t offset = 0;
    bool negative = false;
    if (value[0] == '-') {
        negative = true;
        offset = 1;
    }
    if (offset == value.size()) {
        return false;
    }
    std::int64_t result = 0;
    for (; offset < value.size(); ++offset) {
        const char ch = value[offset];
        if (ch < '0' || ch > '9') {
            return false;
        }
        result = result * 10 + (ch - '0');
    }
    parsed = negative ? -result : result;
    return true;
}

std::string node_type_scope(const oof::platform::stream::ListValue& value, std::string_view current_scope) {
    if (is_materializable_object_candidate(value)) {
        if (const auto* binding = oof::platform::form_descriptor::binding_for_guid(value.items[0].atom)) {
            return std::string(binding->platform_type);
        }
    }
    return std::string(current_scope);
}

void collect_runtime_semantic_diff(
    const oof::platform::stream::ListValue& left,
    const oof::platform::stream::ListValue& right,
    std::string_view path,
    std::string_view type_scope,
    RuntimeSemanticDiffStats& stats
) {
    if (left.is_list != right.is_list) {
        ++stats.structural_diffs;
        stats.semantic_paths.push_back(std::string(path));
        return;
    }
    if (!left.is_list) {
        if (left.atom_kind == right.atom_kind && left.atom == right.atom) {
            return;
        }
        std::int64_t left_int = 0;
        std::int64_t right_int = 0;
        if (path == "$/2/1/10" &&
            left.atom_kind == oof::platform::stream::ListValue::AtomKind::raw &&
            right.atom_kind == oof::platform::stream::ListValue::AtomKind::raw &&
            parse_int64_atom(left.atom, left_int) &&
            parse_int64_atom(right.atom, right_int) &&
            right_int == left_int + 1) {
            ++stats.volatile_counter_diffs;
            return;
        }
        if (type_scope == "CommandBar" &&
            left.atom_kind == oof::platform::stream::ListValue::AtomKind::raw &&
            right.atom_kind == oof::platform::stream::ListValue::AtomKind::raw &&
            is_guid_text(left.atom) &&
            is_guid_text(right.atom)) {
            ++stats.volatile_guid_diffs;
            return;
        }
        ++stats.semantic_diffs;
        if (stats.semantic_paths.size() < 16) {
            stats.semantic_paths.push_back(std::string(path));
        }
        return;
    }
    if (left.items.size() != right.items.size()) {
        ++stats.structural_diffs;
        stats.semantic_paths.push_back(std::string(path));
        return;
    }

    const std::string child_scope = node_type_scope(left, type_scope);
    for (std::size_t index = 0; index < left.items.size(); ++index) {
        collect_runtime_semantic_diff(
            left.items[index],
            right.items[index],
            child_path(path, index),
            child_scope,
            stats);
    }
}

void print_runtime_form_semantic_diff(const std::string& left_path, const std::string& right_path) {
    const auto left_bytes = read_file_bytes(left_path);
    const auto right_bytes = read_file_bytes(right_path);
    const auto left = oof::platform::stream::parse(decode_text_file_bytes(left_bytes));
    const auto right = oof::platform::stream::parse(decode_text_file_bytes(right_bytes));

    RuntimeSemanticDiffStats stats;
    collect_runtime_semantic_diff(left, right, "$", "", stats);
    const bool normalized_equal =
        stats.structural_diffs == 0 &&
        stats.semantic_diffs == 0;

    std::cout << "{\"leftBytes\":" << left_bytes.size();
    std::cout << ",\"rightBytes\":" << right_bytes.size();
    std::cout << ",\"normalizedEqual\":" << (normalized_equal ? "true" : "false");
    std::cout << ",\"structuralDiffs\":" << stats.structural_diffs;
    std::cout << ",\"semanticDiffs\":" << stats.semantic_diffs;
    std::cout << ",\"volatileGuidDiffs\":" << stats.volatile_guid_diffs;
    std::cout << ",\"volatileCounterDiffs\":" << stats.volatile_counter_diffs;
    std::cout << ",\"semanticPaths\":[";
    for (std::size_t index = 0; index < stats.semantic_paths.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_json_string(stats.semantic_paths[index]);
    }
    std::cout << "]}\n";
}

void print_transfer_descriptor_json(const oof::platform::ordinary::TransferDescriptor& descriptor) {
    std::cout << "{\"symbol\":";
    print_json_string(descriptor.symbol);
    std::cout << ",\"facet\":";
    print_json_string(oof::platform::ordinary::facet_name(descriptor.facet));
    std::cout << ",\"formatId\":" << descriptor.format_id;
    std::cout << ",\"recordSize\":" << descriptor.record_size;
    std::cout << ",\"countSemantics\":";
    print_json_string(descriptor.count_semantics);
    std::cout << ",\"boundary\":";
    print_json_string(descriptor.boundary);
    std::cout << ",\"platformRole\":";
    print_json_string(descriptor.platform_role);
    std::cout << ",\"nativeRole\":";
    print_json_string(descriptor.native_role);
    std::cout << "}";
}

void print_form_transfer_linkage_json(
    const std::vector<std::uint8_t>& form_payload,
    std::string_view source_label
) {
    const std::string text = decode_form_payload_text(form_payload);
    const auto root = oof::platform::stream::parse(text);
    if (!root.is_list) {
        throw std::runtime_error("form payload root is not a list");
    }

    std::vector<MaterializedFormItem> items;
    std::size_t guid_head_nodes = 0;
    std::size_t nested_unbound_guid_nodes = 0;
    collect_materialized_form_items(root, "$", "", items, guid_head_nodes, nested_unbound_guid_nodes);

    std::vector<GuidNodeInfo> guid_nodes;
    std::vector<FormatAtomInfo> format_atoms;
    collect_form_payload_structure(root, "$", guid_nodes, format_atoms);

    std::set<std::string> object_ids;
    std::size_t schema_backed = 0;
    for (const auto& item : items) {
        object_ids.insert(item.object_id);
        if (oof::platform::form_descriptor::schema_for_binding(*item.descriptor_binding) != nullptr) {
            ++schema_backed;
        }
    }

    std::map<std::string, std::size_t> format_symbol_frequency;
    for (const auto& atom : format_atoms) {
        ++format_symbol_frequency[atom.symbol];
    }

    std::cout << "{\"source\":";
    print_json_string(source_label);
    std::cout << ",\"payloadSize\":" << form_payload.size();
    std::cout << ",\"objectGraph\":{\"materializedItems\":" << items.size();
    std::cout << ",\"schemaBackedItems\":" << schema_backed;
    std::cout << ",\"uniqueObjectIds\":" << object_ids.size();
    std::cout << ",\"nestedUnboundGuidNodes\":" << nested_unbound_guid_nodes;
    std::cout << "},\"platformTransferContract\":{\"evidence\":\"dsgnfrm exports wbase::cf_form_controls8/cf_form_controls_position8/cf_form_controls_info8; decompile maps GetData order position8, controls8, info8\"";
    std::cout << ",\"formatEntrySize\":" << oof::platform::format_entry_record_size;
    std::cout << ",\"descriptors\":[";
    for (std::size_t index = 0; index < oof::platform::ordinary::transfer_registry.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_transfer_descriptor_json(oof::platform::ordinary::transfer_registry[index]);
    }
    std::cout << "]},\"payloadFormatAtoms\":{\"count\":" << format_atoms.size();
    std::cout << ",\"frequency\":[";
    std::size_t index = 0;
    for (const auto& [symbol, count] : format_symbol_frequency) {
        if (index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"symbol\":";
        print_json_string(symbol);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"atoms\":[";
    for (std::size_t atom_index = 0; atom_index < format_atoms.size(); ++atom_index) {
        if (atom_index != 0) {
            std::cout << ",";
        }
        const auto& atom = format_atoms[atom_index];
        std::cout << "{\"path\":";
        print_json_string(atom.path);
        std::cout << ",\"formatId\":" << atom.format_id;
        std::cout << ",\"symbol\":";
        print_json_string(atom.symbol);
        std::cout << "}";
    }
    const bool transfer_format_atoms_embedded = !format_atoms.empty();
    std::cout << "]},\"linkageStatus\":{\"objectGraphReady\":true";
    std::cout << ",\"transferFormatAtomsEmbedded\":"
              << (transfer_format_atoms_embedded ? "true" : "false");
    std::cout << ",\"directFacetLinkageRequiredForFormBinRebuild\":"
              << (transfer_format_atoms_embedded ? "true" : "false");
    std::cout << ",\"controls8SectionDecoded\":false";
    std::cout << ",\"position8RecordsDecoded\":false";
    std::cout << ",\"info8RecordLayoutDecoded\":true";
    std::cout << ",\"info8ObjectSemanticsDecoded\":false";
    std::cout << ",\"info8LayoutStatus\":";
    print_json_string(oof::platform::ordinary::ControlInfoRecord::layout_status());
    std::cout << ",\"info8SemanticsStatus\":";
    print_json_string(oof::platform::ordinary::ControlInfoRecord::semantics_status());
    std::cout << ",\"reason\":";
    if (transfer_format_atoms_embedded) {
        print_json_string(
            "payload contains transfer format ids; inspect as a platform data-exchange transfer set before rebuild");
    } else {
        print_json_string(
            "persisted ordinary Form.bin payload is a list-stream object graph and does not embed cf_form_controls* transfer format ids; transfer registry describes platform GetData/data-exchange facets, not direct Form.bin sections");
    }
    std::cout << ",\"next\":";
    if (transfer_format_atoms_embedded) {
        print_json_string("decode transfer sections and correlate records by objectId");
    } else {
        print_json_string("decode named properties directly from the materialized list-stream object graph and use cf_form_controls* registry only for data-exchange/export compatibility");
    }
    std::cout << "},\"items\":[";
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
        if (item_index != 0) {
            std::cout << ",";
        }
        const auto& item = items[item_index];
        std::cout << "{\"objectId\":";
        print_json_string(item.object_id);
        std::cout << ",\"name\":";
        print_json_string(item.name);
        std::cout << ",\"platformType\":";
        print_json_string(item.descriptor_binding->platform_type);
        std::cout << ",\"status\":";
        print_json_string(item.descriptor_binding->status);
        std::cout << ",\"expectedFacets\":[\"cf_form_controls8\",\"cf_form_controls_position8\",\"cf_form_controls_info8\"]";
        std::cout << ",\"facetLinkage\":";
        print_json_string(
            transfer_format_atoms_embedded
                ? "pending-transfer-section-decode"
                : "not-embedded-in-persisted-formbin");
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void print_form_transfer_linkage(const std::string& path) {
    const std::vector<std::uint8_t> data = read_file_bytes(path);
    const auto container = oof::platform::formbin::parse_container(data);
    const auto& form_file = find_container_file(container, "form");
    print_form_transfer_linkage_json(form_file.payload, "Form.bin:form");
}

void print_form_payload_structure_selftest() {
    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    const std::string form_text =
        "{27,{18,{09ccdc77-ea1a-4a6d-ab1c-3435eada2433,{1}},"
        "{e69bf21d-97b2-4f37-86db-675aea9ec2cb,2,{9472,21760,40192},{},{},{}}}}";
    container.files.push_back({"form", 1, 2, std::vector<std::uint8_t>(form_text.begin(), form_text.end())});
    container.files.push_back({"module", 3, 4, {'/', '/', 'm'}});

    const auto bytes = oof::platform::formbin::serialize_container(container);
    const auto reparsed = oof::platform::formbin::parse_container(bytes);
    const auto& form_file = find_container_file(reparsed, "form");
    print_form_payload_structure_json(form_file.payload, "selftest");
}

void print_form_object_graph_selftest() {
    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    const std::string form_text =
        "{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{14,\"Button1\",4294967295,0,0,0},{},{},{}},"
        "{35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26,6,{14,\"Check1\",4294967295,0,0,0},{},{},{}}}}";
    container.files.push_back({"form", 1, 2, std::vector<std::uint8_t>(form_text.begin(), form_text.end())});
    container.files.push_back({"module", 3, 4, {'/', '/', 'm'}});

    const auto bytes = oof::platform::formbin::serialize_container(container);
    const auto reparsed = oof::platform::formbin::parse_container(bytes);
    const auto& form_file = find_container_file(reparsed, "form");
    print_form_object_graph_json(form_file.payload, "selftest");
}

void print_form_transfer_linkage_selftest() {
    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    const std::string form_text =
        "{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{14,\"Button1\",4294967295,0,0,0},"
        "{9472,21760,40192},{},{}}}}";
    container.files.push_back({"form", 1, 2, std::vector<std::uint8_t>(form_text.begin(), form_text.end())});
    container.files.push_back({"module", 3, 4, {'/', '/', 'm'}});

    const auto bytes = oof::platform::formbin::serialize_container(container);
    const auto reparsed = oof::platform::formbin::parse_container(bytes);
    const auto& form_file = find_container_file(reparsed, "form");
    print_form_transfer_linkage_json(form_file.payload, "selftest");
}

void print_platform_guid_scan(const std::string& path) {
    const std::vector<std::uint8_t> data = read_file_bytes(path);
    const auto scan = oof::platform::guid_registry::scan_dsgnfrm_guid_registry(data);

    std::cout << "{";
    std::cout << "\"seedHits\":[";
    for (std::size_t index = 0; index < scan.seed_hits.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& hit = scan.seed_hits[index];
        std::cout << "{\"offset\":\"0x" << std::hex << hit.offset << std::dec << "\",\"guid\":";
        print_json_string(hit.guid);
        std::cout << "}";
    }
    std::cout << "],\"repeatedGuidBlocks\":[";
    for (std::size_t index = 0; index < scan.repeated_blocks.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        std::cout << "\"0x" << std::hex << scan.repeated_blocks[index].offset << std::dec << "\"";
    }
    std::cout << "],\"codeRefs\":[";
    for (std::size_t index = 0; index < scan.code_refs.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& ref = scan.code_refs[index];
        std::cout << "{\"offset\":\"0x" << std::hex << ref.offset << "\",\"target\":\"0x" << ref.target
                  << std::dec << "\"}";
    }
    std::cout << "],\"firstBlockGuids\":[";
    if (!scan.repeated_blocks.empty()) {
        const std::size_t block = scan.repeated_blocks.front().offset;
        for (std::size_t index = 0; index < 24 && block + (index + 1) * 16 <= data.size(); ++index) {
            if (index != 0) {
                std::cout << ",";
            }
            std::array<std::uint8_t, 16> bytes{};
            for (std::size_t byte_index = 0; byte_index < bytes.size(); ++byte_index) {
                bytes[byte_index] = data[block + index * 16 + byte_index];
            }
            print_json_string(oof::platform::guid_registry::guid_to_text_le(bytes));
        }
    }
    std::cout << "]}\n";
}

struct ResourceDescriptorHit {
    std::string file;
    std::string guid;
    std::string id;
    std::string nearest_name;
    const oof::platform::descriptor::DescriptorGuidBinding* binding = nullptr;
};

std::string parse_decimal_after_guid(std::string_view text, std::size_t pos, std::size_t guid_size) {
    std::size_t cursor = pos + guid_size;
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    if (cursor >= text.size() || text[cursor] != ',') {
        return {};
    }
    ++cursor;
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    const std::size_t start = cursor;
    while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    return std::string(text.substr(start, cursor - start));
}

std::string find_nearest_resource_name(std::string_view text, std::size_t pos) {
    constexpr std::size_t scan_window = 20000;
    const std::size_t end = std::min(text.size(), pos + scan_window);
    const std::string_view window = text.substr(pos, end - pos);
    const std::string marker = "{14,\"";
    const std::size_t marker_pos = window.find(marker);
    if (marker_pos == std::string_view::npos) {
        return {};
    }
    const std::size_t name_start = marker_pos + marker.size();
    const std::size_t name_end = window.find('"', name_start);
    if (name_end == std::string_view::npos) {
        return {};
    }
    return std::string(window.substr(name_start, name_end - name_start));
}

std::vector<ResourceDescriptorHit> scan_resource_descriptor_hits(const std::string& path) {
    const std::string text = read_file_text_lossy(path);
    const std::string lowered = ascii_lower(text);
    std::vector<ResourceDescriptorHit> hits;
    for (const auto& binding : oof::platform::descriptor::ordinary_descriptor_guid_bindings) {
        const std::string guid = ascii_lower(binding.guid);
        std::size_t pos = 0;
        while ((pos = lowered.find(guid, pos)) != std::string::npos) {
            ResourceDescriptorHit hit;
            hit.file = path;
            hit.guid = guid;
            hit.id = parse_decimal_after_guid(text, pos, guid.size());
            hit.nearest_name = find_nearest_resource_name(text, pos);
            hit.binding = &binding;
            hits.push_back(std::move(hit));
            pos += guid.size();
        }
    }
    return hits;
}

std::vector<ResourceDescriptorHit> scan_resource_unknown_guid_candidates(const std::string& path) {
    const std::string text = read_file_text_lossy(path);
    const std::regex guid_entry(
        R"(\{([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\s*,\s*([0-9]+))");
    std::vector<ResourceDescriptorHit> hits;
    std::set<std::string> seen;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), guid_entry); it != std::sregex_iterator(); ++it) {
        const auto& match = *it;
        const std::string guid = ascii_lower(match[1].str());
        if (oof::platform::descriptor::is_bound_descriptor_guid(guid)) {
            continue;
        }
        const std::size_t pos = static_cast<std::size_t>(match.position());
        const std::string nearest_name = find_nearest_resource_name(text, pos);
        if (nearest_name.empty()) {
            continue;
        }
        const std::string key = guid + ":" + nearest_name;
        if (!seen.insert(key).second) {
            continue;
        }
        ResourceDescriptorHit hit;
        hit.file = path;
        hit.guid = guid;
        hit.id = match[2].str();
        hit.nearest_name = nearest_name;
        hits.push_back(std::move(hit));
    }
    return hits;
}

std::string regex_first_group(const std::string& text, const std::regex& pattern) {
    std::smatch match;
    if (std::regex_search(text, match, pattern) && match.size() > 1) {
        return match[1].str();
    }
    return {};
}

std::vector<std::string> regex_all_group(const std::string& text, const std::regex& pattern) {
    std::vector<std::string> values;
    std::set<std::string> seen;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
        const auto& match = *it;
        if (match.size() <= 1) {
            continue;
        }
        const std::string value = match[1].str();
        if (seen.insert(value).second) {
            values.push_back(value);
        }
    }
    return values;
}

bool is_form_related_schema_name(std::string_view name) {
    const std::string lower = ascii_lower(name);
    return lower.find("form") != std::string::npos ||
           lower.find("element") != std::string::npos ||
           lower.find("control") != std::string::npos ||
           lower.find("uobject") != std::string::npos ||
           lower.find("ui") != std::string::npos ||
           lower.find("layout") != std::string::npos ||
           lower.find("button") != std::string::npos;
}

void print_json_string_array(const std::vector<std::string>& values, std::size_t max_items = 64) {
    std::cout << "[";
    const std::size_t count = std::min(values.size(), max_items);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_json_string(values[index]);
    }
    std::cout << "]";
}

void print_platform_form_schema() {
    std::map<std::string_view, std::size_t> source_frequency;
    std::map<std::string_view, std::size_t> value_type_frequency;
    for (const auto& control : oof::platform::form_schema::logform_layouter_controls) {
        ++source_frequency[control.schema_source];
        std::string_view values = control.value_types;
        while (!values.empty()) {
            const std::size_t comma = values.find(',');
            const std::string_view value = values.substr(0, comma);
            if (!value.empty()) {
                ++value_type_frequency[value];
            }
            if (comma == std::string_view::npos) {
                break;
            }
            values.remove_prefix(comma + 1);
        }
    }

    std::cout << "{\"source\":";
    print_json_string(oof::platform::form_schema::logform_layouter_schema);
    std::cout << ",\"controlCount\":" << oof::platform::form_schema::logform_layouter_controls.size();
    std::cout << ",\"directGuidBindings\":0";
    std::cout << ",\"guidBindingStatus\":\"not-present-in-xsd; use resource/binary evidence\"";
    std::cout << ",\"controls\":[";
    for (std::size_t index = 0; index < oof::platform::form_schema::logform_layouter_controls.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& control = oof::platform::form_schema::logform_layouter_controls[index];
        std::cout << "{\"typeName\":";
        print_json_string(control.type_name);
        std::cout << ",\"streamElement\":";
        print_json_string(control.stream_element);
        std::cout << ",\"schemaSource\":";
        print_json_string(control.schema_source);
        std::cout << ",\"baseType\":";
        print_json_string(control.base_type);
        std::cout << ",\"childElements\":";
        print_json_string(control.child_elements);
        std::cout << ",\"attributes\":";
        print_json_string(control.attributes);
        std::cout << ",\"valueTypes\":";
        print_json_string(control.value_types);
        std::cout << ",\"rootComplexType\":";
        print_json_string(control.root_complex_type);
        std::cout << ",\"variantElement\":";
        print_json_string(control.variant_element);
        std::cout << ",\"variantComplexType\":";
        print_json_string(control.variant_complex_type);
        std::cout << ",\"rootSequence\":";
        print_json_string(control.root_sequence);
        std::cout << ",\"variantSequence\":";
        print_json_string(control.variant_sequence);
        std::cout << ",\"rootAttributes\":";
        print_json_string(control.root_attributes);
        std::cout << ",\"variantAttributes\":";
        print_json_string(control.variant_attributes);
        std::cout << ",\"platformMembers\":";
        print_json_string(control.platform_members);
        std::cout << ",\"defaultContract\":";
        print_json_string(control.default_contract);
        std::cout << ",\"evidence\":";
        print_json_string(control.evidence);
        std::cout << "}";
    }
    std::cout << "],\"valueTypeFrequency\":[";
    std::size_t vt_index = 0;
    for (const auto& [value_type, count] : value_type_frequency) {
        if (vt_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"type\":";
        print_json_string(value_type);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "]}\n";
}

void print_platform_object_schema() {
    const auto schemas = oof::platform::object_schema::build_platform_object_schemas();
    std::size_t api_backed = 0;
    std::size_t member_count = 0;
    for (const auto& schema : schemas) {
        if (!schema.api_source.empty()) {
            ++api_backed;
        }
        member_count += schema.xsd_members.size();
    }

    std::cout << "{\"source\":\"platform object schema join: 8.5 mngcore logform XSD resource + shcntx API catalog + localization resource evidence\"";
    std::cout << ",\"schemaCount\":" << schemas.size();
    std::cout << ",\"apiBackedCount\":" << api_backed;
    std::cout << ",\"xsdMemberCount\":" << member_count;
    std::cout << ",\"publicXmlRole\":\"source-of-truth object schema for PlatformObject to Form.xml projection; not a raw list-stream dump\"";
    std::cout << ",\"schemas\":[";
    for (std::size_t index = 0; index < schemas.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& schema = schemas[index];
        std::cout << "{\"typeName\":";
        print_json_string(schema.type_name);
        std::cout << ",\"streamElement\":";
        print_json_string(schema.stream_element);
        std::cout << ",\"schemaSource\":";
        print_json_string(schema.schema_source);
        std::cout << ",\"rootComplexType\":";
        print_json_string(schema.root_complex_type);
        std::cout << ",\"variantElement\":";
        print_json_string(schema.variant_element);
        std::cout << ",\"variantComplexType\":";
        print_json_string(schema.variant_complex_type);
        std::cout << ",\"rootSequence\":";
        print_json_string(schema.root_sequence);
        std::cout << ",\"variantSequence\":";
        print_json_string(schema.variant_sequence);
        std::cout << ",\"rootAttributes\":";
        print_json_string(schema.root_attributes);
        std::cout << ",\"variantAttributes\":";
        print_json_string(schema.variant_attributes);
        std::cout << ",\"platformMembers\":";
        print_json_string(schema.platform_members);
        std::cout << ",\"defaultContract\":";
        print_json_string(schema.default_contract);
        std::cout << ",\"apiSource\":";
        print_json_string(schema.api_source);
        std::cout << ",\"runtimeSource\":";
        print_json_string(schema.runtime_source);
        std::cout << ",\"persistenceSource\":";
        print_json_string(schema.persistence_source);
        std::cout << ",\"localizationSource\":";
        print_json_string(schema.localization_source);
        std::cout << ",\"xsdMembers\":[";
        for (std::size_t member_index = 0; member_index < schema.xsd_members.size(); ++member_index) {
            if (member_index != 0) {
                std::cout << ",";
            }
            const auto& member = schema.xsd_members[member_index];
            std::cout << "{\"name\":";
            print_json_string(member.name);
            std::cout << ",\"streamName\":";
            print_json_string(member.stream_name);
            std::cout << ",\"valueType\":";
            print_json_string(member.value_type);
            std::cout << ",\"defaultValue\":";
            print_json_string(member.default_value);
            std::cout << ",\"writePolicy\":";
            print_json_string(member.write_policy);
            std::cout << ",\"platformMember\":";
            print_json_string(member.platform_member);
            std::cout << ",\"platformDefault\":";
            print_json_string(member.platform_default);
            std::cout << ",\"slotBinding\":";
            print_json_string(member.slot_binding);
            std::cout << ",\"slotCodec\":";
            print_json_string(member.slot_codec);
            std::cout << ",\"codecStatus\":";
            print_json_string(member.codec_status);
            std::cout << ",\"writable\":" << (member.writable ? "true" : "false");
            std::cout << ",\"source\":";
            print_json_string(member.source);
            std::cout << "}";
        }
        std::cout << "],\"apiProperties\":";
        print_json_string_array(schema.api_properties);
        std::cout << ",\"apiMethods\":";
        print_json_string_array(schema.api_methods);
        std::cout << ",\"apiEvents\":";
        print_json_string_array(schema.api_events);
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void print_descriptor_schema_binding_json(
    const oof::platform::form_descriptor::DescriptorSchemaBinding& binding
) {
    const auto* schema = oof::platform::form_descriptor::schema_for_binding(binding);
    const auto* direct = oof::platform::descriptor::binding_for_guid(binding.guid);

    std::cout << "{\"guid\":";
    print_json_string(binding.guid);
    std::cout << ",\"status\":";
    print_json_string(binding.status);
    std::cout << ",\"platformType\":";
    print_json_string(binding.platform_type);
    std::cout << ",\"streamElement\":";
    print_json_string(binding.stream_element);
    std::cout << ",\"role\":";
    print_json_string(binding.role);
    std::cout << ",\"fieldKindSymbol\":";
    print_json_string(binding.field_kind_symbol);
    std::cout << ",\"typePresentationSymbol\":";
    print_json_string(binding.type_presentation_symbol);
    std::cout << ",\"corpusObjectNames\":";
    print_json_string(binding.corpus_object_names);
    std::cout << ",\"evidence\":";
    print_json_string(binding.evidence);
    std::cout << ",\"directDescriptorRegistry\":"
              << (direct != nullptr ? "true" : "false");
    std::cout << ",\"schemaBacked\":"
              << (schema != nullptr ? "true" : "false");
    if (schema != nullptr) {
        std::cout << ",\"schema\":{\"source\":";
        print_json_string(schema->schema_source);
        std::cout << ",\"typeName\":";
        print_json_string(schema->type_name);
        std::cout << ",\"streamElement\":";
        print_json_string(schema->stream_element);
        std::cout << ",\"baseType\":";
        print_json_string(schema->base_type);
        std::cout << ",\"childElements\":";
        print_json_string(schema->child_elements);
        std::cout << ",\"attributes\":";
        print_json_string(schema->attributes);
        std::cout << ",\"valueTypes\":";
        print_json_string(schema->value_types);
        std::cout << "}";
    }
    std::cout << "}";
}

void print_platform_descriptor_join() {
    std::map<std::string_view, std::size_t> status_frequency;
    std::size_t schema_backed = 0;
    std::size_t direct_registry = 0;
    for (const auto& binding : oof::platform::form_descriptor::descriptor_schema_bindings) {
        ++status_frequency[binding.status];
        if (oof::platform::form_descriptor::schema_for_binding(binding) != nullptr) {
            ++schema_backed;
        }
        if (oof::platform::descriptor::binding_for_guid(binding.guid) != nullptr) {
            ++direct_registry;
        }
    }

    std::cout << "{\"source\":\"platform resources + binary/resource descriptor evidence + all-controls Form.bin corpus\"";
    std::cout << ",\"bindingCount\":"
              << oof::platform::form_descriptor::descriptor_schema_bindings.size();
    std::cout << ",\"schemaBackedCount\":" << schema_backed;
    std::cout << ",\"directRegistryCount\":" << direct_registry;
    std::cout << ",\"statusFrequency\":[";
    std::size_t status_index = 0;
    for (const auto& [status, count] : status_frequency) {
        if (status_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"status\":";
        print_json_string(status);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"bindings\":[";
    for (std::size_t index = 0; index < oof::platform::form_descriptor::descriptor_schema_bindings.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_descriptor_schema_binding_json(oof::platform::form_descriptor::descriptor_schema_bindings[index]);
    }
    std::cout << "]}\n";
}

void print_platform_runtime_bindings() {
    std::cout << "{\"source\":\"platform help/resource/binary runtime binding evidence\"";
    std::cout << ",\"layerCount\":"
              << oof::platform::runtime_binding::runtime_layers.size();
    std::cout << ",\"layers\":[";
    for (std::size_t index = 0; index < oof::platform::runtime_binding::runtime_layers.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& layer = oof::platform::runtime_binding::runtime_layers[index];
        std::cout << "{\"name\":";
        print_json_string(layer.name);
        std::cout << ",\"source\":";
        print_json_string(layer.source);
        std::cout << ",\"role\":";
        print_json_string(layer.role);
        std::cout << ",\"evidence\":";
        print_json_string(layer.evidence);
        std::cout << "}";
    }
    std::cout << "],\"objectCount\":"
              << oof::platform::runtime_binding::api_objects.size();
    std::cout << ",\"objects\":[";
    for (std::size_t index = 0; index < oof::platform::runtime_binding::api_objects.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& object = oof::platform::runtime_binding::api_objects[index];
        std::cout << "{\"name\":";
        print_json_string(object.name);
        std::cout << ",\"apiSource\":";
        print_json_string(object.api_source);
        std::cout << ",\"runtimeSource\":";
        print_json_string(object.runtime_source);
        std::cout << ",\"persistenceSource\":";
        print_json_string(object.persistence_source);
        std::cout << ",\"xdtoSource\":";
        print_json_string(object.xdto_source);
        std::cout << ",\"localizationSource\":";
        print_json_string(object.localization_source);
        std::cout << ",\"propertyCount\":" << object.property_count;
        std::cout << ",\"methodCount\":" << object.method_count;
        std::cout << ",\"eventCount\":" << object.event_count;
        std::cout << ",\"sampleProperties\":";
        print_json_string(object.sample_properties);
        std::cout << ",\"sampleMethods\":";
        print_json_string(object.sample_methods);
        std::cout << ",\"sampleEvents\":";
        print_json_string(object.sample_events);
        std::cout << ",\"evidence\":";
        print_json_string(object.evidence);
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void print_platform_xsd_inventory(int argc, char** argv) {
    const std::regex target_namespace_pattern("targetNamespace\\s*=\\s*\"([^\"]+)\"");
    const std::regex import_namespace_pattern("<xs:import[^>]*namespace\\s*=\\s*\"([^\"]+)\"");
    const std::regex include_location_pattern("<xs:include[^>]*schemaLocation\\s*=\\s*\"([^\"]+)\"");
    const std::regex complex_type_pattern("<xs:complexType[^>]*name\\s*=\\s*\"([^\"]+)\"");
    const std::regex simple_type_pattern("<xs:simpleType[^>]*name\\s*=\\s*\"([^\"]+)\"");
    const std::regex element_pattern("<xs:element[^>]*name\\s*=\\s*\"([^\"]+)\"");

    std::cout << "{\"files\":" << (argc - 2) << ",\"schemas\":[";
    std::map<std::string, std::size_t> namespace_frequency;
    for (int index = 2; index < argc; ++index) {
        if (index != 2) {
            std::cout << ",";
        }
        const std::string path = argv[index];
        const std::string text = read_file_text_lossy(path);
        const std::string target_namespace = regex_first_group(text, target_namespace_pattern);
        if (!target_namespace.empty()) {
            ++namespace_frequency[target_namespace];
        }
        const auto imports = regex_all_group(text, import_namespace_pattern);
        const auto includes = regex_all_group(text, include_location_pattern);
        const auto complex_types = regex_all_group(text, complex_type_pattern);
        const auto simple_types = regex_all_group(text, simple_type_pattern);
        const auto elements = regex_all_group(text, element_pattern);

        std::vector<std::string> form_related_types;
        for (const auto& name : complex_types) {
            if (is_form_related_schema_name(name)) {
                form_related_types.push_back(name);
            }
        }
        for (const auto& name : simple_types) {
            if (is_form_related_schema_name(name)) {
                form_related_types.push_back(name);
            }
        }
        std::vector<std::string> form_related_elements;
        for (const auto& name : elements) {
            if (is_form_related_schema_name(name)) {
                form_related_elements.push_back(name);
            }
        }

        std::cout << "{\"file\":";
        print_json_string(path);
        std::cout << ",\"targetNamespace\":";
        print_json_string(target_namespace);
        std::cout << ",\"imports\":";
        print_json_string_array(imports);
        std::cout << ",\"includes\":";
        print_json_string_array(includes);
        std::cout << ",\"complexTypes\":" << complex_types.size();
        std::cout << ",\"simpleTypes\":" << simple_types.size();
        std::cout << ",\"elements\":" << elements.size();
        std::cout << ",\"formRelatedTypes\":";
        print_json_string_array(form_related_types);
        std::cout << ",\"formRelatedTypesTruncated\":"
                  << (form_related_types.size() > 64 ? "true" : "false");
        std::cout << ",\"formRelatedElements\":";
        print_json_string_array(form_related_elements);
        std::cout << ",\"formRelatedElementsTruncated\":"
                  << (form_related_elements.size() > 64 ? "true" : "false");
        std::cout << "}";
    }
    std::cout << "],\"namespaceFrequency\":[";
    std::size_t ns_index = 0;
    for (const auto& [ns, count] : namespace_frequency) {
        if (ns_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"namespace\":";
        print_json_string(ns);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "]}\n";
}

void print_resource_hit_json(const ResourceDescriptorHit& hit) {
    std::cout << "{\"file\":";
    print_json_string(hit.file);
    std::cout << ",\"guid\":";
    print_json_string(hit.guid);
    std::cout << ",\"id\":";
    print_json_string(hit.id);
    std::cout << ",\"nearestName\":";
    print_json_string(hit.nearest_name);
    if (hit.binding != nullptr) {
        std::cout << ",\"descriptorBinding\":{\"status\":";
        print_json_string(hit.binding->status);
        std::cout << ",\"role\":";
        print_json_string(hit.binding->role);
        std::cout << ",\"evidence\":";
        print_json_string(hit.binding->evidence);
        std::cout << "}";
    }
    std::cout << "}";
}

void print_platform_resource_descriptor_scan(int argc, char** argv) {
    std::vector<ResourceDescriptorHit> known_hits;
    std::vector<ResourceDescriptorHit> unknown_hits;
    std::map<std::string, std::size_t> known_guid_frequency;
    std::map<std::string, std::size_t> unknown_guid_frequency;

    for (int index = 2; index < argc; ++index) {
        auto file_known_hits = scan_resource_descriptor_hits(argv[index]);
        for (const auto& hit : file_known_hits) {
            ++known_guid_frequency[hit.guid];
        }
        known_hits.insert(known_hits.end(), file_known_hits.begin(), file_known_hits.end());

        auto file_unknown_hits = scan_resource_unknown_guid_candidates(argv[index]);
        for (const auto& hit : file_unknown_hits) {
            ++unknown_guid_frequency[hit.guid];
        }
        unknown_hits.insert(unknown_hits.end(), file_unknown_hits.begin(), file_unknown_hits.end());
    }

    constexpr std::size_t max_hits_to_print = 96;
    std::cout << "{\"files\":" << (argc - 2);
    std::cout << ",\"knownDescriptorHitsTotal\":" << known_hits.size();
    std::cout << ",\"unknownGuidCandidateHitsTotal\":" << unknown_hits.size();
    std::cout << ",\"knownGuidFrequency\":[";
    std::size_t item_index = 0;
    for (const auto& [guid, count] : known_guid_frequency) {
        if (item_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"guid\":";
        print_json_string(guid);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"unknownGuidFrequency\":[";
    item_index = 0;
    for (const auto& [guid, count] : unknown_guid_frequency) {
        if (item_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"guid\":";
        print_json_string(guid);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"knownDescriptorHits\":[";
    for (std::size_t index = 0; index < std::min(known_hits.size(), max_hits_to_print); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_resource_hit_json(known_hits[index]);
    }
    std::cout << "],\"knownDescriptorHitsTruncated\":"
              << (known_hits.size() > max_hits_to_print ? "true" : "false");
    std::cout << ",\"unknownGuidCandidateHits\":[";
    for (std::size_t index = 0; index < std::min(unknown_hits.size(), max_hits_to_print); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_resource_hit_json(unknown_hits[index]);
    }
    std::cout << "],\"unknownGuidCandidateHitsTruncated\":"
              << (unknown_hits.size() > max_hits_to_print ? "true" : "false");
    std::cout << "}\n";
}

void print_formbin_selftest() {
    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    container.files.push_back({"form", 1, 2, {'{', '1', '}'}});
    container.files.push_back({"module", 3, 4, {'/', '/', 'm'}});

    const std::vector<std::uint8_t> bytes = oof::platform::formbin::serialize_container(container);
    const auto reparsed = oof::platform::formbin::parse_container(bytes);
    const std::vector<std::uint8_t> rebuilt = oof::platform::formbin::serialize_container(reparsed);

    std::cout << "{";
    std::cout << "\"bytes\":" << bytes.size();
    std::cout << ",\"byteEqual\":" << (bytes == rebuilt ? "true" : "false");
    std::cout << ",\"fileCount\":" << reparsed.files.size();
    std::cout << ",\"firstName\":";
    print_json_string(reparsed.files.at(0).name);
    std::cout << ",\"secondName\":";
    print_json_string(reparsed.files.at(1).name);
    std::cout << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }

    try {
        const std::string command = argv[1];
        if (command == "mechanism") {
            print_mechanism();
            return 0;
        }
        if (command == "value-roundtrip") {
            print_value_roundtrip();
            return 0;
        }
        if (command == "controls-codec") {
            print_controls_codec();
            return 0;
        }
        if (command == "info8-codec") {
            print_info8_codec();
            return 0;
        }
        if (command == "graph-codec") {
            print_graph_codec();
            return 0;
        }
        if (command == "transfer-roundtrip") {
            print_transfer_roundtrip();
            return 0;
        }
        if (command == "transfer-sections") {
            print_transfer_sections();
            return 0;
        }
        if (command == "formbin-selftest") {
            print_formbin_selftest();
            return 0;
        }
        if (command == "formbin-package-selftest") {
            print_formbin_package_selftest();
            return 0;
        }
        if (command == "formbin-platform-object-selftest") {
            print_formbin_platform_object_selftest();
            return 0;
        }
        if (command == "form-payload-structure-selftest") {
            print_form_payload_structure_selftest();
            return 0;
        }
        if (command == "form-object-graph-selftest") {
            print_form_object_graph_selftest();
            return 0;
        }
        if (command == "form-transfer-linkage-selftest") {
            print_form_transfer_linkage_selftest();
            return 0;
        }
        if (command == "raw-deflate-selftest") {
            print_raw_deflate_selftest();
            return 0;
        }
        if (command == "formbin-info" && argc == 3) {
            print_formbin_info(argv[2]);
            return 0;
        }
        if (command == "formbin-roundtrip" && argc == 3) {
            print_formbin_roundtrip(argv[2]);
            return 0;
        }
        if (command == "container-extract" && argc == 4) {
            extract_container_files(argv[2], argv[3], false);
            return 0;
        }
        if (command == "container-extract-inflate" && argc == 4) {
            extract_container_files(argv[2], argv[3], true);
            return 0;
        }
        if (command == "platform-form-schema") {
            print_platform_form_schema();
            return 0;
        }
        if (command == "platform-object-schema") {
            print_platform_object_schema();
            return 0;
        }
        if (command == "platform-descriptor-join") {
            print_platform_descriptor_join();
            return 0;
        }
        if (command == "platform-runtime-bindings") {
            print_platform_runtime_bindings();
            return 0;
        }
        if (command == "platform-property-registry") {
            print_platform_property_registry();
            return 0;
        }
        if (command == "form-payload-info" && argc == 3) {
            print_form_payload_info(argv[2]);
            return 0;
        }
        if (command == "form-payload-structure" && argc == 3) {
            print_form_payload_structure(argv[2]);
            return 0;
        }
        if (command == "form-object-graph" && argc == 3) {
            print_form_object_graph(argv[2]);
            return 0;
        }
        if (command == "formbin-dump-package" && argc == 4) {
            write_formbin_package(argv[2], argv[3]);
            return 0;
        }
        if (command == "formbin-build-package" && argc == 5) {
            write_formbin_from_package(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (command == "formbin-build-source-package" && argc == 4) {
            write_formbin_from_source_package(argv[2], argv[3]);
            return 0;
        }
        if (command == "formbin-xml-coverage" && argc == 3) {
            print_formbin_xml_coverage(argv[2]);
            return 0;
        }
        if (command == "formbin-platform-object" && argc == 3) {
            print_formbin_platform_object(argv[2]);
            return 0;
        }
        if (command == "formbin-platform-object-get" && argc == 5) {
            print_formbin_platform_object_get(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (command == "formbin-platform-object-set" && argc == 7) {
            write_formbin_platform_object_set(argv[2], argv[3], argv[4], argv[5], argv[6]);
            return 0;
        }
        if (command == "runtime-form-dump-xml" && argc == 4) {
            write_runtime_form_xml(argv[2], argv[3]);
            return 0;
        }
        if (command == "runtime-form-build-xml" && argc == 5) {
            write_runtime_form_from_xml(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (command == "runtime-form-object-graph" && argc == 3) {
            print_runtime_form_object_graph(argv[2]);
            return 0;
        }
        if (command == "runtime-form-roundtrip" && argc == 3) {
            print_runtime_form_roundtrip(argv[2]);
            return 0;
        }
        if (command == "runtime-platform-object" && argc == 3) {
            print_runtime_platform_object(argv[2]);
            return 0;
        }
        if (command == "runtime-platform-object-get" && argc == 5) {
            print_runtime_platform_object_get(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (command == "runtime-form-semantic-diff" && argc == 4) {
            print_runtime_form_semantic_diff(argv[2], argv[3]);
            return 0;
        }
        if (command == "runtime-form-rebuild" && argc == 4) {
            write_runtime_form_rebuild(argv[2], argv[3]);
            return 0;
        }
        if (command == "runtime-form-rename" && argc == 6) {
            write_runtime_form_rename(argv[2], argv[3], argv[4], argv[5]);
            return 0;
        }
        if (command == "runtime-platform-object-set" && argc == 7) {
            write_runtime_platform_object_set(argv[2], argv[3], argv[4], argv[5], argv[6]);
            return 0;
        }
        if (command == "form-transfer-linkage" && argc == 3) {
            print_form_transfer_linkage(argv[2]);
            return 0;
        }
        if (command == "platform-guid-scan" && argc == 3) {
            print_platform_guid_scan(argv[2]);
            return 0;
        }
        if (command == "platform-resource-descriptor-scan" && argc >= 3) {
            print_platform_resource_descriptor_scan(argc, argv);
            return 0;
        }
        if (command == "platform-xsd-inventory" && argc >= 3) {
            print_platform_xsd_inventory(argc, argv);
            return 0;
        }

        if (argc != 2) {
            usage();
            return 2;
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

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
