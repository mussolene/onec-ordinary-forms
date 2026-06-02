#include <cctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "form_bin_container.hpp"
#include "ordinary_controls.hpp"
#include "ordinary_form_graph.hpp"
#include "platform_guid_registry.hpp"
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
    std::cerr << "Usage: oof-native <compact|listout|stats|mechanism|value-roundtrip|controls-codec|graph-codec|transfer-roundtrip|transfer-sections|formbin-selftest> < stream.txt\n"
              << "       oof-native <formbin-info|formbin-roundtrip|form-payload-info> Form.bin\n"
              << "       oof-native platform-guid-scan dsgnfrm.so\n";
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
    if (argc != 2 && argc != 3) {
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
        if (command == "formbin-info" && argc == 3) {
            print_formbin_info(argv[2]);
            return 0;
        }
        if (command == "formbin-roundtrip" && argc == 3) {
            print_formbin_roundtrip(argv[2]);
            return 0;
        }
        if (command == "form-payload-info" && argc == 3) {
            print_form_payload_info(argv[2]);
            return 0;
        }
        if (command == "platform-guid-scan" && argc == 3) {
            print_platform_guid_scan(argv[2]);
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

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
