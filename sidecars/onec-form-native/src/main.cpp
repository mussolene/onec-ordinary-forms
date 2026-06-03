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

struct GeometryBindingRecord {
    std::string name;
    oof::platform::stream::ListValue value;
};

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
                    item.bindings.push_back({binding_coordinate_name(index), geometry->items[index]});
                }
                for (std::size_t index = 13; index <= 16 && index < geometry->items.size(); ++index) {
                    item.dimension_bindings.push_back({dimension_binding_name(index), geometry->items[index]});
                }
            }
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
    bool writable = false
) {
    return oof::platform::object_model::make_property(
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
        std::move(value_origin));
}

oof::platform::object_model::PlatformObjectCollectionDescriptor make_described_collection(
    std::string_view name,
    std::size_t count
) {
    const auto* descriptor = oof::platform::property_registry::find_descriptor(name);
    if (descriptor == nullptr) {
        return oof::platform::object_model::make_collection_descriptor(
            std::string(name),
            {},
            "ObjectCollection",
            count,
            "platform-api-catalog");
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
                member.writable));
        }
    }
    for (const auto& name : schema.api_properties) {
        if (object.property(name) == nullptr) {
            object.properties.push_back(make_described_property(name, ""));
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
        form_object.form.properties.push_back(make_described_property("Items", std::to_string(summary.items.size())));
        form_object.form.properties.push_back(make_described_property("Attributes", std::to_string(summary.attributes.size())));
        form_object.form.properties.push_back(make_described_property("Commands", std::to_string(summary.commands.size())));
        form_object.form.properties.push_back(make_described_property("Events", std::to_string(summary.events.size())));
        form_object.form.collections.push_back(make_described_collection("Items", summary.items.size()));
        form_object.form.collections.push_back(make_described_collection("Attributes", summary.attributes.size()));
        form_object.form.collections.push_back(make_described_collection("Commands", summary.commands.size()));
        form_object.form.collections.push_back(make_described_collection("Events", summary.events.size()));
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

void print_runtime_platform_object(const std::string& path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(path, canonical_text);
    const auto form_object = materialize_platform_form_object(envelope);
    const auto* first = form_object.items.count() == 0 ? nullptr : &form_object.items.get(0);
    const auto* found = first == nullptr ? nullptr : form_object.items.find(first->name);

    std::cout << "{\"source\":\"RuntimeForm:PlatformObject\"";
    std::cout << ",\"runtimeUuid\":";
    print_json_string(envelope.runtime_uuid);
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

void print_runtime_platform_object_get(
    const std::string& path,
    std::string_view object_id,
    std::string_view property_name
) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(path, canonical_text);
    const auto form_object = materialize_platform_form_object(envelope);
    const auto* object = form_object.find_object_by_id(object_id);
    if (object == nullptr) {
        throw std::runtime_error("platform object is not found: " + std::string(object_id));
    }
    const auto* property = object->property(property_name);
    if (property == nullptr) {
        throw std::runtime_error("platform object property is not found: " + std::string(property_name));
    }

    std::cout << "{\"operation\":\"getPropVal\"";
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
    std::cout << ",\"readable\":" << (property->readable ? "true" : "false");
    std::cout << ",\"writable\":" << (property->writable ? "true" : "false");
    std::cout << ",\"slotBinding\":";
    print_json_string(property->slot_binding);
    std::cout << ",\"slotCodec\":";
    print_json_string(property->slot_codec);
    std::cout << "}\n";
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
        std::size_t attr_end = element.attrs.find_last_not_of(" \t\r\n/");
        element.self_closing = attr_end == std::string::npos || element.attrs.find('/', attr_end + 1) != std::string::npos;
        if (element.self_closing) {
            element.attrs = element.attrs.substr(0, attr_end == std::string::npos ? 0 : attr_end + 1);
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
                        xml_unescape(property_xml.body),
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
    if (property.source.find("managed-application/logform/layouter") == std::string::npos) {
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

void append_schema_properties_xml(
    std::string& out,
    const oof::platform::object_model::PlatformObject& object,
    int indent
) {
    for (const auto& property : object.properties) {
        if (!is_public_schema_property_xml(property) || !property_is_explicit_for_xml(property)) {
            continue;
        }
        append_indent(out, indent);
        out += "<";
        out += property.name;
        out += ">";
        out += xml_escape(property.value);
        out += "</";
        out += property.name;
        out += ">\n";
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
    append_schema_properties_xml(out, object, indent + 2);
    append_position_xml(out, form_object, object, indent + 2);
    if (!object_events.empty()) {
        append_events_xml(out, object_events, indent + 2);
    }
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
    append_events_xml(out, event_objects_for_parent(form_object, "0"), 2);
    out += "  <ChildItems>\n";
    for (const std::size_t child_index : form_object.form.children) {
        append_control_xml(out, form_object, child_index, 4);
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
    const auto object_edits = parse_public_xml_platform_object_edits(read_file_text_lossy(xml_path));
    const auto result = apply_platform_object_edits(envelope, object_edits);
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
    const auto object_edits = parse_public_xml_platform_object_edits(read_file_text_lossy(xml_path));
    const auto result = apply_platform_object_edits(envelope, object_edits);
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
    std::cout << ",\"scalarFlagEdits\":" << result.scalar_flag_edits;
    std::cout << ",\"positionEdits\":" << result.position_edits;
    std::cout << ",\"bindingEdits\":" << result.binding_edits;
    std::cout << ",\"dimensionBindingEdits\":" << result.dimension_binding_edits;
    std::cout << ",\"attributeEdits\":" << result.attribute_edits;
    std::cout << ",\"commandEdits\":" << result.command_edits;
    std::cout << ",\"eventEdits\":" << result.event_edits;
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
    std::cout << ",\"supportedEditCodecs\":[\"Name\",\"Title\",\"Visible\",\"Enabled\",\"Position\",\"Binding:value\",\"Binding:anchor-list\",\"DimensionBinding:value\",\"DimensionBinding:record\",\"Attribute.Name\",\"Command.Name\",\"Command.Handler\",\"Command.ModifiesData\",\"Event.Handler\"]";
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
    replace_all(xml, "dimension=\"width\" value=\"0\"", "dimension=\"width\" value=\"2\"");

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
    const auto anchor_edits = parse_public_xml_control_edits(anchor_xml);
    const auto anchor_result = apply_public_xml_edits(anchor_envelope, anchor_edits);
    const std::string anchor_redump_xml = form_object_to_public_xml(materialize_platform_form_object(anchor_envelope));

    std::cout << "{\"operation\":\"formbin-xml-build-selftest\"";
    std::cout << ",\"nameEdits\":" << result.name_edits;
    std::cout << ",\"titleEdits\":" << result.title_edits;
    std::cout << ",\"positionEdits\":" << result.position_edits;
    std::cout << ",\"bindingEdits\":" << result.binding_edits;
    std::cout << ",\"dimensionBindingEdits\":" << result.dimension_binding_edits;
    std::cout << ",\"anchorBindingEdits\":" << anchor_result.binding_edits;
    std::cout << ",\"anchorTargetNameVisible\":" << (anchor_name_visible ? "true" : "false");
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

    std::cout << "{\"source\":\"platform object schema join: mngcore logform_layouter XSD + shcntx API catalog + localization resource evidence\"";
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
