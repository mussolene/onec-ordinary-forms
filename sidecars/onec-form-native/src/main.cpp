#include <array>
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
    std::cerr << "Usage: oof-native <compact|listout|stats|mechanism|value-roundtrip|controls-codec|graph-codec|transfer-roundtrip|transfer-sections|formbin-selftest|formbin-xml-build-selftest|form-payload-structure-selftest|form-object-graph-selftest|form-transfer-linkage-selftest|raw-deflate-selftest> < stream.txt\n"
              << "       oof-native <formbin-info|formbin-roundtrip|form-payload-info|form-payload-structure|form-object-graph|form-transfer-linkage> Form.bin\n"
              << "       oof-native formbin-dump-xml Form.bin Form.xml\n"
              << "       oof-native formbin-build-xml base-Form.bin Form.xml rebuilt-Form.bin\n"
              << "       oof-native formbin-xml-coverage Form.bin\n"
              << "       oof-native runtime-form-dump-xml runtime-form-stream.txt Form.xml\n"
              << "       oof-native runtime-form-build-xml base-runtime-stream.txt Form.xml rebuilt-runtime-stream.txt\n"
              << "       oof-native <runtime-form-object-graph|runtime-form-roundtrip|runtime-platform-object> runtime-form-stream.txt\n"
              << "       oof-native runtime-form-semantic-diff left-runtime-stream.txt right-runtime-stream.txt\n"
              << "       oof-native runtime-form-rebuild runtime-form-stream.txt rebuilt-stream.txt\n"
              << "       oof-native runtime-form-rename runtime-form-stream.txt rebuilt-stream.txt objectId newName\n"
              << "       oof-native runtime-platform-object-set runtime-form-stream.txt rebuilt-stream.txt objectId property value\n"
              << "       oof-native container-extract <1c-container> <out-dir>\n"
              << "       oof-native container-extract-inflate <1c-container> <out-dir>\n"
              << "       oof-native <platform-form-schema|platform-descriptor-join|platform-runtime-bindings|platform-property-registry>\n"
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
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            std::cout << '\\' << ch;
        } else {
            std::cout << ch;
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

struct MaterializedFormItem {
    std::string guid;
    std::string path;
    std::string object_id;
    std::string parent_object_id;
    std::string name;
    std::string title;
    std::string left;
    std::string top;
    std::string right;
    std::string bottom;
    std::vector<std::pair<std::string, std::string>> bindings;
    std::size_t arity = 0;
    const oof::platform::form_descriptor::DescriptorSchemaBinding* descriptor_binding = nullptr;
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
            if (const auto* geometry = find_immediate_geometry_record(value)) {
                item.left = geometry->items[1].atom;
                item.top = geometry->items[2].atom;
                item.right = geometry->items[3].atom;
                item.bottom = geometry->items[4].atom;
                for (std::size_t index = 6; index <= 11 && index < geometry->items.size(); ++index) {
                    if (!geometry->items[index].is_list) {
                        item.bindings.push_back({binding_coordinate_name(index), geometry->items[index].atom});
                    }
                }
            }
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

    for (const auto& item : summary.items) {
        ++summary.status_frequency[item.descriptor_binding->status];
        ++summary.type_frequency[item.descriptor_binding->platform_type];
        if (!item.name.empty()) {
            ++summary.named_items;
        }
        if (oof::platform::form_descriptor::schema_for_binding(*item.descriptor_binding) != nullptr) {
            ++summary.schema_backed_items;
        }
    }
    return summary;
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
    if (name == "Title" || name == "Caption") {
        return "Заголовок";
    }
    if (name == "Name") {
        return "Имя";
    }
    if (name == "Type") {
        return "Тип";
    }
    if (name == "Parent") {
        return "Родитель";
    }
    if (name == "Visible") {
        return "Видимость";
    }
    if (name == "Enabled") {
        return "Доступность";
    }
    return {};
}

oof::platform::object_model::PlatformObjectProperty make_described_property(
    std::string_view name,
    std::string value
) {
    const auto* descriptor = oof::platform::property_registry::find_descriptor(name);
    if (descriptor == nullptr) {
        return oof::platform::object_model::make_property(
            std::string(name),
            localized_property_name(name),
            std::move(value),
            "platform-api-catalog",
            "GenericValue");
    }
    return oof::platform::object_model::make_property(
        std::string(descriptor->name),
        std::string(descriptor->localized_name),
        std::move(value),
        std::string(descriptor->source),
        std::string(descriptor->value_type),
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
            object.properties.push_back(make_described_property(name, ""));
        }
    }
    for (const auto& name : split_csv_list(api->sample_methods)) {
        object.methods.push_back(oof::platform::object_model::make_method(name));
    }
    for (const auto& name : split_csv_list(api->sample_events)) {
        object.events.push_back(oof::platform::object_model::make_event(name));
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

oof::platform::object_model::PlatformFormObject materialize_platform_form_object(
    const RuntimeFormEnvelope& envelope
) {
    const auto summary = summarize_materialized_graph(envelope.payload);

    oof::platform::object_model::PlatformFormObject form_object;
    form_object.form.object_id = "0";
    form_object.form.name = "Form";
    form_object.form.platform_type = "Form";
    form_object.form.type_category = "core::kLogFormTypeInfoCategory";
    form_object.form.type_source = "core85 ContextCore + mngbase RTLogForm";
    form_object.form.path = "$";
    form_object.form.properties.push_back(make_described_property("Type", "Form"));
    form_object.form.properties.push_back(make_described_property("RuntimeUUID", envelope.runtime_uuid));
    form_object.form.properties.push_back(make_described_property("Items", std::to_string(summary.items.size())));
    add_api_surface(form_object.form, api_object_for_type("Form"));

    for (const auto& item : summary.items) {
        oof::platform::object_model::PlatformObject object;
        object.object_id = item.object_id;
        object.name = item.name;
        object.platform_type = std::string(item.descriptor_binding->platform_type);
        object.type_category = "core::kLogFormTypeInfoCategory";
        object.type_source = std::string(item.descriptor_binding->evidence);
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
        if (!item.left.empty()) {
            object.properties.push_back(make_described_property("Left", item.left));
            object.properties.push_back(make_described_property("Top", item.top));
            object.properties.push_back(make_described_property("Width", std::to_string(std::stoll(item.right) - std::stoll(item.left))));
            object.properties.push_back(make_described_property("Height", std::to_string(std::stoll(item.bottom) - std::stoll(item.top))));
            object.properties.push_back(make_described_property("Right", item.right));
            object.properties.push_back(make_described_property("Bottom", item.bottom));
            for (const auto& [coordinate, value] : item.bindings) {
                object.properties.push_back(make_described_property("Binding." + coordinate, value));
            }
        }
        add_api_surface(object, api_object_for_type(object.platform_type));
        form_object.items.add(std::move(object));
    }

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

    return form_object;
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
        std::cout << ",\"source\":";
        print_json_string(prop.source);
        std::cout << ",\"slotBinding\":";
        print_json_string(prop.slot_binding);
        std::cout << ",\"slotCodec\":";
        print_json_string(prop.slot_codec);
        std::cout << ",\"readable\":"
                  << (prop.readable ? "true" : "false");
        std::cout << ",\"writable\":"
                  << (prop.writable ? "true" : "false");
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
    std::cout << "],\"children\":[";
    for (std::size_t index = 0; index < object.children.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        std::cout << object.children[index];
    }
    std::cout << "]}";
}

void print_runtime_platform_object(const std::string& path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(path, canonical_text);
    const auto form_object = materialize_platform_form_object(envelope);
    const auto* first = form_object.items.count() == 0 ? nullptr : &form_object.items.get(0);
    const auto* found = first == nullptr ? nullptr : form_object.items.find(first->name);

    std::cout << "{\"source\":\"RuntimeForm:PlatformObject\"";
    std::cout << ",\"runtimeUuid\":";
    print_json_string(envelope.runtime_uuid);
    std::cout << ",\"contextContract\":{\"platformEvidence\":\"core85 exports IContextDef/GroupContext/IContextExtImplBase getNProps,getPropName,findProp,isPropReadable,isPropWritable,getPropVal,setPropVal,call\",\"model\":\"typeDescriptor + property/method/event descriptors + slot-backed values\",\"descriptorRegistry\":\"PlatformPropertyDescriptor + PropertySlotBinding\",\"implementedWritableSlotCodecs\":[\"name-record\",\"position-record\"]}";
    std::cout << ",\"form\":";
    print_platform_object_json(form_object.form);
    std::cout << ",\"items\":{\"count\":" << form_object.items.count();
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
    std::cout << "]}}\n";
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

struct PublicXmlControlEdit {
    std::string tag;
    std::string object_id;
    std::string name;
    bool has_name = false;
    std::string title;
    bool has_title = false;
    std::string left;
    std::string top;
    std::string right;
    std::string bottom;
    bool has_position = false;
    std::vector<std::pair<std::string, std::string>> bindings;
};

struct PublicXmlApplyResult {
    std::size_t controls = 0;
    std::size_t name_edits = 0;
    std::size_t title_edits = 0;
    std::size_t position_edits = 0;
    std::size_t binding_edits = 0;
};

PublicXmlApplyResult apply_public_xml_edits(
    RuntimeFormEnvelope& envelope,
    const std::vector<PublicXmlControlEdit>& edits
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

std::vector<PublicXmlControlEdit> parse_public_xml_control_edits(const std::string& xml) {
    if (xml.find("<ListStream") != std::string::npos || xml.find("<RawBracket") != std::string::npos ||
        xml.find("<PlatformRecords") != std::string::npos || xml.find("<FormBin") != std::string::npos) {
        throw std::runtime_error("OrdinaryFormV2 XML must not contain raw/list-stream fallback nodes");
    }
    if (xml.find("ordinaryFormVersion=\"2.") == std::string::npos) {
        throw std::runtime_error("expected OrdinaryFormV2 XML with ordinaryFormVersion=\"2.*\"");
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
                        const std::regex binding_pattern(R"(<Binding\b([^>]*)/?>)");
                        for (std::sregex_iterator binding_it(position_body.begin(), position_body.end(), binding_pattern), binding_end;
                             binding_it != binding_end;
                             ++binding_it) {
                            const std::string binding_attrs = (*binding_it)[1].str();
                            const std::string coordinate = xml_attr_value(binding_attrs, "coordinate");
                            const std::string value = xml_attr_value(binding_attrs, "value");
                            if (!coordinate.empty() && !value.empty()) {
                                edit.bindings.push_back({coordinate, value});
                            }
                        }
                    }
                }
            }
        }
        edits.push_back(std::move(edit));
    }
    return edits;
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

void append_named_text_property_xml(
    std::string& out,
    const oof::platform::object_model::PlatformObject& object,
    std::string_view property_name,
    int indent
) {
    const auto* property = find_object_property(object, property_name);
    if (property == nullptr || property->value.empty()) {
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

bool has_position_properties(const oof::platform::object_model::PlatformObject& object) {
    return object.property("Left") != nullptr &&
           object.property("Top") != nullptr &&
           object.property("Right") != nullptr &&
           object.property("Bottom") != nullptr;
}

std::vector<std::pair<std::string, std::string>> simple_binding_properties(
    const oof::platform::object_model::PlatformObject& object
) {
    std::vector<std::pair<std::string, std::string>> bindings;
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
            bindings.push_back({coordinate, prop->value});
        }
    }
    return bindings;
}

void append_position_xml(
    std::string& out,
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
    const auto bindings = simple_binding_properties(object);
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
    if (bindings.empty()) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    append_indent(out, indent + 2);
    out += "<Bindings>\n";
    for (const auto& [coordinate, value] : bindings) {
        append_indent(out, indent + 4);
        out += "<Binding coordinate=\"";
        out += xml_escape(coordinate);
        out += "\" value=\"";
        out += xml_escape(value);
        out += "\"/>\n";
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
    int indent
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
    if (object.children.empty() &&
        find_object_property(object, "Title") == nullptr &&
        !has_position_properties(object)) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    append_named_text_property_xml(out, object, "Title", indent + 2);
    append_position_xml(out, object, indent + 2);
    if (!object.children.empty()) {
        append_indent(out, indent + 2);
        out += "<ChildItems>\n";
        for (const std::size_t child_index : object.children) {
            append_control_xml(out, form_object, child_index, indent + 4);
        }
        append_indent(out, indent + 2);
        out += "</ChildItems>\n";
    }
    append_indent(out, indent);
    out += "</";
    out += tag;
    out += ">\n";
}

std::string form_object_to_public_xml(const oof::platform::object_model::PlatformFormObject& form_object) {
    std::string out;
    out += "<?xml version='1.0' encoding='utf-8'?>\n";
    out += "<Form xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" ordinaryFormVersion=\"2.0-draft\" xsi:noNamespaceSchemaLocation=\"OrdinaryFormV2.xsd\">\n";
    out += "  <Events/>\n";
    out += "  <ChildItems>\n";
    for (const std::size_t child_index : form_object.form.children) {
        append_control_xml(out, form_object, child_index, 4);
    }
    out += "  </ChildItems>\n";
    out += "  <Attributes/>\n";
    out += "  <Commands/>\n";
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
    std::cout << ",\"publicContract\":\"OrdinaryFormV2\"";
    std::cout << "}\n";
}

RuntimeFormEnvelope read_formbin_runtime_envelope(const std::string& input_path) {
    const std::vector<std::uint8_t> data = read_file_bytes(input_path);
    const auto container = oof::platform::formbin::parse_container(data);
    const auto& form_file = find_container_file(container, "form");
    return runtime_envelope_from_form_payload(form_file.payload);
}

void write_formbin_xml(const std::string& input_path, const std::string& output_path) {
    RuntimeFormEnvelope envelope = read_formbin_runtime_envelope(input_path);
    const auto form_object = materialize_platform_form_object(envelope);
    const std::string xml = form_object_to_public_xml(form_object);
    write_file_bytes(output_path, std::vector<std::uint8_t>(xml.begin(), xml.end()));
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"bytes\":" << xml.size();
    std::cout << ",\"source\":\"Form.bin:form\"";
    std::cout << ",\"controlCount\":" << form_object.items.count();
    std::cout << ",\"publicContract\":\"OrdinaryFormV2\"";
    std::cout << "}\n";
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
    const auto edits = parse_public_xml_control_edits(read_file_text_lossy(xml_path));
    const auto result = apply_public_xml_edits(envelope, edits);
    const std::string rebuilt_text = dump_runtime_form_envelope(envelope);
    write_file_bytes(output_path, std::vector<std::uint8_t>(rebuilt_text.begin(), rebuilt_text.end()));
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"runtime-form-build-xml\"";
    std::cout << ",\"bytes\":" << rebuilt_text.size();
    std::cout << ",\"controls\":" << result.controls;
    std::cout << ",\"nameEdits\":" << result.name_edits;
    std::cout << ",\"titleEdits\":" << result.title_edits;
    std::cout << ",\"positionEdits\":" << result.position_edits;
    std::cout << ",\"bindingEdits\":" << result.binding_edits;
    std::cout << ",\"publicContract\":\"OrdinaryFormV2\"";
    std::cout << "}\n";
}

void write_formbin_from_xml(
    const std::string& input_path,
    const std::string& xml_path,
    const std::string& output_path
) {
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
    const auto edits = parse_public_xml_control_edits(read_file_text_lossy(xml_path));
    const auto result = apply_public_xml_edits(envelope, edits);
    file_it->payload = encode_form_payload_text(file_it->payload, envelope.payload);
    const auto rebuilt = oof::platform::formbin::serialize_container(container);
    write_file_bytes(output_path, rebuilt);
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"formbin-build-xml\"";
    std::cout << ",\"bytes\":" << rebuilt.size();
    std::cout << ",\"controls\":" << result.controls;
    std::cout << ",\"nameEdits\":" << result.name_edits;
    std::cout << ",\"titleEdits\":" << result.title_edits;
    std::cout << ",\"positionEdits\":" << result.position_edits;
    std::cout << ",\"bindingEdits\":" << result.binding_edits;
    std::cout << ",\"preservedContainerFiles\":" << container.files.size();
    std::cout << ",\"publicContract\":\"OrdinaryFormV2\"";
    std::cout << "}\n";
}

void print_formbin_xml_coverage(const std::string& input_path) {
    RuntimeFormEnvelope envelope = read_formbin_runtime_envelope(input_path);
    const auto summary = summarize_materialized_graph(envelope.payload);
    std::cout << "{\"source\":\"Form.bin:form\"";
    std::cout << ",\"publicContract\":\"OrdinaryFormV2\"";
    std::cout << ",\"nativeXmlProjection\":true";
    std::cout << ",\"nativeXmlWriter\":true";
    std::cout << ",\"supportedEditCodecs\":[\"Name\",\"Title\",\"Position\",\"Binding:value\"]";
    std::cout << ",\"materializedItems\":" << summary.items.size();
    std::cout << ",\"namedItems\":" << summary.named_items;
    std::cout << ",\"schemaBackedItems\":" << summary.schema_backed_items;
    std::cout << ",\"missingCodecs\":[\"Binding:anchor-list\",\"DimensionBinding\",\"Attributes\",\"Commands\",\"Events\",\"cf_form_controls8 typed payload fields\"]";
    std::cout << "}\n";
}

void replace_all(std::string& value, std::string_view needle, std::string_view replacement) {
    std::size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
        value.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
}

void print_formbin_xml_build_selftest() {
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

    const auto edits = parse_public_xml_control_edits(xml);
    const auto result = apply_public_xml_edits(envelope, edits);
    container.files[0].payload = encode_form_payload_text(container.files[0].payload, envelope.payload);
    const auto rebuilt = oof::platform::formbin::serialize_container(container);
    const auto reparsed = oof::platform::formbin::parse_container(rebuilt);
    const auto redump_envelope = runtime_envelope_from_form_payload(find_container_file(reparsed, "form").payload);
    const std::string redump_xml = form_object_to_public_xml(materialize_platform_form_object(redump_envelope));
    const auto& module = find_container_file(reparsed, "module");

    std::cout << "{\"operation\":\"formbin-xml-build-selftest\"";
    std::cout << ",\"nameEdits\":" << result.name_edits;
    std::cout << ",\"titleEdits\":" << result.title_edits;
    std::cout << ",\"positionEdits\":" << result.position_edits;
    std::cout << ",\"bindingEdits\":" << result.binding_edits;
    std::cout << ",\"nameRoundtrip\":"
              << (redump_xml.find("ButtonXmlEdited") != std::string::npos ? "true" : "false");
    std::cout << ",\"titleRoundtrip\":"
              << (redump_xml.find("RunXmlEdited") != std::string::npos ? "true" : "false");
    std::cout << ",\"positionRoundtrip\":"
              << (redump_xml.find("<Position left=\"9\" top=\"2\" right=\"101\" bottom=\"22\"") != std::string::npos ? "true" : "false");
    std::cout << ",\"bindingRoundtrip\":"
              << (redump_xml.find("<Binding coordinate=\"left\" value=\"21\"/>") != std::string::npos ? "true" : "false");
    std::cout << ",\"modulePreserved\":"
              << (module.payload == std::vector<std::uint8_t>({'m', 'o', 'd'}) ? "true" : "false");
    std::cout << ",\"noRawXml\":"
              << (redump_xml.find("ListStream") == std::string::npos ? "true" : "false");
    std::cout << ",\"publicContract\":\"OrdinaryFormV2\"";
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

bool set_materialized_object_position(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    std::string_view left,
    std::string_view top,
    std::string_view right,
    std::string_view bottom
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
        return set_geometry_atom(*geometry, 1, left) &&
               set_geometry_atom(*geometry, 2, top) &&
               set_geometry_atom(*geometry, 3, right) &&
               set_geometry_atom(*geometry, 4, bottom);
    }
    for (auto& item : value.items) {
        if (set_materialized_object_position(item, object_id, left, top, right, bottom)) {
            return true;
        }
    }
    return false;
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

bool set_materialized_object_bindings(
    oof::platform::stream::ListValue& value,
    std::string_view object_id,
    const std::vector<std::pair<std::string, std::string>>& bindings
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
        for (const auto& [coordinate, binding_value] : bindings) {
            const std::size_t slot = binding_coordinate_slot(coordinate);
            if (slot == 0 || geometry->items.size() <= slot || geometry->items[slot].is_list) {
                throw std::runtime_error("unsupported Binding coordinate or non-scalar binding slot: " + coordinate);
            }
            geometry->items[slot].atom = binding_value;
            geometry->items[slot].atom_kind = oof::platform::stream::ListValue::AtomKind::raw;
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

PublicXmlApplyResult apply_public_xml_edits(
    RuntimeFormEnvelope& envelope,
    const std::vector<PublicXmlControlEdit>& edits
) {
    PublicXmlApplyResult result;
    for (const auto& edit : edits) {
        ++result.controls;
        if (edit.has_name) {
            if (!rename_materialized_object(envelope.payload, edit.object_id, edit.name)) {
                throw std::runtime_error("XML control id has no writable platform name record: " + edit.object_id);
            }
            ++result.name_edits;
        }
        if (edit.has_title) {
            if (!set_materialized_object_title(envelope.payload, edit.object_id, edit.title)) {
                throw std::runtime_error("XML control id has no writable platform title slot: " + edit.object_id);
            }
            ++result.title_edits;
        }
        if (edit.has_position) {
            if (!set_materialized_object_position(
                    envelope.payload,
                    edit.object_id,
                    edit.left,
                    edit.top,
                    edit.right,
                    edit.bottom)) {
                throw std::runtime_error("XML control id has no writable platform position record: " + edit.object_id);
            }
            ++result.position_edits;
        }
        if (!edit.bindings.empty()) {
            if (!set_materialized_object_bindings(envelope.payload, edit.object_id, edit.bindings)) {
                throw std::runtime_error("XML control id has no writable scalar platform binding records: " + edit.object_id);
            }
            result.binding_edits += edit.bindings.size();
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

bool set_property_slot_value(
    oof::platform::stream::ListValue& payload,
    std::string_view object_id,
    const oof::platform::property_registry::PlatformPropertyDescriptor& descriptor,
    std::string_view new_value
) {
    if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::name_record) {
        return rename_materialized_object(payload, object_id, new_value);
    }
    if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::position_record) {
        return set_materialized_object_position_property(payload, object_id, descriptor.name, new_value);
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
    if (!set_property_slot_value(envelope.payload, object_id, descriptor, new_value)) {
        throw std::runtime_error("runtime form object id was not found or has no writable property slot: " + std::string(object_id));
    }

    const std::string rebuilt_text = dump_runtime_form_envelope(envelope);
    const std::vector<std::uint8_t> output(rebuilt_text.begin(), rebuilt_text.end());
    write_file_bytes(output_path, output);
    const auto form_object = materialize_platform_form_object(envelope);
    const oof::platform::object_model::PlatformObject* changed = nullptr;
    for (const auto& object : form_object.items.objects()) {
        if (object.object_id == object_id) {
            changed = &object;
            break;
        }
    }

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
    std::cout << "{\"source\":\"PlatformPropertyDescriptorRegistry\"";
    std::cout << ",\"descriptorCount\":" << oof::platform::property_registry::descriptors.size();
    std::cout << ",\"descriptors\":[";
    for (std::size_t index = 0; index < oof::platform::property_registry::descriptors.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& descriptor = oof::platform::property_registry::descriptors[index];
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
        std::cout << "}";
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
    std::cout << ",\"info8RecordsDecoded\":false";
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
        if (command == "formbin-xml-build-selftest") {
            print_formbin_xml_build_selftest();
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
        if (command == "formbin-dump-xml" && argc == 4) {
            write_formbin_xml(argv[2], argv[3]);
            return 0;
        }
        if (command == "formbin-build-xml" && argc == 5) {
            write_formbin_from_xml(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (command == "formbin-xml-coverage" && argc == 3) {
            print_formbin_xml_coverage(argv[2]);
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
