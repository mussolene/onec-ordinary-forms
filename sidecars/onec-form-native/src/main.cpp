#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
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
#include <optional>
#include <utility>
#include <vector>

#include <zlib.h>

#include "control_info_descriptor_registry.hpp"
#include "form_bin_container.hpp"
#include "ordinary_control_type_registry.hpp"
#include "ordinary_controls.hpp"
#include "ordinary_form_concept_registry.hpp"
#include "ordinary_form_graph.hpp"
#include "ordinary_form_object.hpp"
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

void usage() {
    const bool show_diagnostics = std::getenv("OOF_NATIVE_SHOW_DIAGNOSTICS") != nullptr;
    std::cerr << "Usage: oof-native <product-command> [args]\n"
              << "\n"
              << "Product commands:\n"
              << "  oof-native formbin-dump-package Form.bin Form.xml\n"
              << "  oof-native formbin-build-source-package Form.xml rebuilt-Form.bin\n"
              << "  oof-native formbin-xml-coverage Form.bin\n"
              << "  oof-native object-model-gate\n"
              << "\n"
              << "Product XML is the public OrdinaryForm source package surface. Runtime streams,\n"
              << "platform-XSD XML, container surgery, platform registries, and selftests are\n"
              << "diagnostic commands, not user-facing build/dump equivalents.\n";
    if (!show_diagnostics) {
        std::cerr << "\nSet OOF_NATIVE_SHOW_DIAGNOSTICS=1 to list diagnostic commands.\n";
        return;
    }
    std::cerr << "\nDiagnostic commands:\n"
              << "  oof-native <mechanism|value-roundtrip|formbin-selftest|formbin-source-package-selftest|formbin-package-selftest|formbin-platform-object-selftest|form-object-graph-selftest|object-graph-concept-selftest|raw-deflate-selftest> < stream.txt\n"
              << "  oof-native ordinary-form-object-selftest\n"
              << "  oof-native empty-form-object-roundtrip [title]\n"
              << "  oof-native <formbin-info|formbin-roundtrip|formbin-object-roundtrip|formbin-object-roundtrip-diff|formbin-xsd-order-object-roundtrip|form-object-graph> Form.bin\n"
              << "  oof-native formbin-dump-platform-xsd-xml Form.bin PlatformForm.xml\n"
              << "  oof-native <formbin-platform-object|formbin-platform-object-get|formbin-xsd-order-object-gate> Form.bin [objectId property]\n"
              << "  oof-native formbin-platform-object-set input-Form.bin rebuilt-Form.bin objectId property value\n"
              << "  oof-native runtime-form-dump-xml runtime-form-stream.txt Form.xml\n"
              << "  oof-native runtime-form-dump-platform-xsd-xml runtime-form-stream.txt PlatformForm.xml\n"
              << "  oof-native platform-xsd-xml-object PlatformForm.xml\n"
              << "  oof-native platform-xsd-xml-roundtrip PlatformForm.xml rebuilt-PlatformForm.xml\n"
              << "  oof-native platform-xsd-xml-build-runtime PlatformForm.xml runtime-form-stream.txt\n"
              << "  oof-native platform-xsd-xml-build-formbin PlatformForm.xml Form.bin\n"
              << "  oof-native <runtime-form-object-graph|runtime-form-roundtrip|runtime-form-object-roundtrip|runtime-form-object-roundtrip-diff|runtime-xsd-order-object-roundtrip|runtime-platform-object|runtime-xsd-order-object-gate> runtime-form-stream.txt\n"
              << "  oof-native runtime-form-semantic-diff left-runtime-stream.txt right-runtime-stream.txt\n"
              << "  oof-native runtime-form-node runtime-form-stream.txt node-path\n"
              << "  oof-native runtime-form-rebuild runtime-form-stream.txt rebuilt-stream.txt\n"
              << "  oof-native runtime-form-rename runtime-form-stream.txt rebuilt-stream.txt objectId newName\n"
              << "  oof-native runtime-platform-object-get runtime-form-stream.txt objectId property\n"
              << "  oof-native runtime-platform-object-set runtime-form-stream.txt rebuilt-stream.txt objectId property value\n"
              << "  oof-native container-extract <1c-container> <out-dir>\n"
              << "  oof-native container-extract-inflate <1c-container> <out-dir>\n"
              << "  oof-native container-replace <1c-container> <file-name> <replacement-file> <out-container> [--raw-deflate]\n"
              << "  oof-native <platform-form-schema|platform-object-schema|platform-descriptor-join|platform-runtime-bindings|platform-property-registry|platform-control-info-descriptors>\n"
              << "  oof-native ordinary-form-concepts\n"
              << "  oof-native <xsd-order-object-gate|xsd-order-object-roundtrip>\n"
              << "  oof-native platform-guid-scan dsgnfrm.so\n"
              << "  oof-native platform-resource-descriptor-scan file.res [file.res ...]\n"
              << "  oof-native platform-xsd-inventory file.xsd [file.xsd ...]\n";
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
        std::cout << ",\"createdTicks\":" << file.created;
        std::cout << ",\"modifiedTicks\":" << file.modified;
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

std::vector<std::uint8_t> deflate_raw_deflate(const std::vector<std::uint8_t>& data) {
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    const int init_code = deflateInit2(
        &stream,
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        -MAX_WBITS,
        8,
        Z_DEFAULT_STRATEGY);
    if (init_code != Z_OK) {
        throw std::runtime_error("cannot initialize raw deflate compressor");
    }

    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, 16384> buffer{};
    int code = Z_OK;
    while (code == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        code = deflate(&stream, Z_FINISH);
        const std::size_t produced = buffer.size() - stream.avail_out;
        output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    }
    deflateEnd(&stream);
    if (code != Z_STREAM_END) {
        throw std::runtime_error("raw deflate payload did not compress cleanly");
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

void replace_container_file(
    const std::string& input_path,
    const std::string& file_name,
    const std::string& replacement_path,
    const std::string& output_path,
    bool raw_deflate
) {
    const std::vector<std::uint8_t> data = read_file_bytes(input_path);
    auto container = oof::platform::formbin::parse_container(data);
    auto replacement = read_file_bytes(replacement_path);
    const std::size_t replacement_size = replacement.size();
    if (raw_deflate) {
        replacement = deflate_raw_deflate(replacement);
    }

    bool replaced = false;
    std::size_t old_size = 0;
    for (auto& file : container.files) {
        if (file.name == file_name) {
            old_size = file.payload.size();
            file.payload = std::move(replacement);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        throw std::runtime_error("container file not found: " + file_name);
    }

    const auto rebuilt = oof::platform::formbin::serialize_container(container);
    write_file_bytes(output_path, rebuilt);
    std::cout << "{";
    std::cout << "\"operation\":\"container-replace\"";
    std::cout << ",\"output\":";
    print_json_string(output_path);
    std::cout << ",\"fileName\":";
    print_json_string(file_name);
    std::cout << ",\"fileCount\":" << container.files.size();
    std::cout << ",\"oldPayloadSize\":" << old_size;
    std::cout << ",\"replacementSize\":" << replacement_size;
    std::cout << ",\"storedPayloadSize\":";
    for (const auto& file : container.files) {
        if (file.name == file_name) {
            std::cout << file.payload.size();
            break;
        }
    }
    std::cout << ",\"rawDeflate\":" << (raw_deflate ? "true" : "false");
    std::cout << ",\"bytes\":" << rebuilt.size();
    std::cout << "}\n";
}

void print_raw_deflate_selftest() {
    const std::vector<std::uint8_t> compressed{0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00};
    const auto inflated = inflate_raw_deflate(compressed);
    const auto recompressed = deflate_raw_deflate(inflated);
    const auto reinflated = inflate_raw_deflate(recompressed);
    const std::string text(inflated.begin(), inflated.end());
    std::cout << "{";
    std::cout << "\"inflated\":";
    print_json_string(text);
    std::cout << ",\"byteEqual\":" << (text == "hello" ? "true" : "false");
    std::cout << ",\"reinflateEqual\":" << (reinflated == inflated ? "true" : "false");
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

std::string child_path(std::string_view path, std::size_t index) {
    std::string out(path);
    out += "/";
    out += std::to_string(index);
    return out;
}

struct GeometryBindingRecord {
    std::string name;
    oof::platform::stream::ListValue value;
};

struct ControlInfoSlotProperty {
    std::string name;
    std::string value;
};

const oof::platform::stream::ListValue* control_info_slot_body(
    const oof::platform::stream::ListValue& object_value
) {
    if (!object_value.is_list || object_value.items.size() <= 2 || !object_value.items[2].is_list) {
        return nullptr;
    }
    const auto& info_record = object_value.items[2];
    if (info_record.items.size() > 2 && info_record.items[2].is_list) {
        const auto& nested = info_record.items[2];
        if (nested.items.size() == 1 && nested.items[0].is_list) {
            return &nested.items[0];
        }
        return &nested;
    }
    return &info_record;
}

oof::platform::stream::ListValue* mutable_control_info_slot_body(
    oof::platform::stream::ListValue& object_value
) {
    if (!object_value.is_list || object_value.items.size() <= 2 || !object_value.items[2].is_list) {
        return nullptr;
    }
    auto& info_record = object_value.items[2];
    if (info_record.items.size() > 2 && info_record.items[2].is_list) {
        auto& nested = info_record.items[2];
        if (nested.items.size() == 1 && nested.items[0].is_list) {
            return &nested.items[0];
        }
        return &nested;
    }
    return &info_record;
}

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

std::string public_control_info_slot_value(const oof::platform::stream::ListValue& value) {
    if (!value.is_list) {
        return value.atom;
    }
    if (!value.items.empty() &&
        !value.items[0].is_list &&
        value.items[0].atom.rfind("#base64:", 0) == 0) {
        std::string payload;
        for (const auto& item : value.items) {
            if (item.is_list) {
                break;
            }
            payload += item.atom;
        }
        return payload;
    }
    return oof::platform::stream::dump_compact(value);
}

bool find_first_localized_text(const oof::platform::stream::ListValue& value, std::string& text);
std::string xml_escape(std::string_view value);
void append_type_domain_pattern_xml(std::string& out, std::string_view type_text, int indent);

const oof::platform::stream::ListValue* first_type_domain_pattern_value(
    const oof::platform::stream::ListValue& value
) {
    if (!value.is_list || value.items.size() < 2) {
        return nullptr;
    }
    if (!value.items[0].is_list && value.items[0].atom == "Pattern" && value.items[1].is_list) {
        return &value;
    }
    for (const auto& item : value.items) {
        if (const auto* found = first_type_domain_pattern_value(item)) {
            return found;
        }
    }
    return nullptr;
}

std::string table_columns_xml_from_payload(const oof::platform::stream::ListValue& object_value) {
    if (!object_value.is_list || object_value.items.size() <= 2 || !object_value.items[2].is_list) {
        return {};
    }
    const auto& payload = object_value.items[2];
    if (payload.items.size() <= 2 || !payload.items[2].is_list || payload.items[2].items.size() <= 1) {
        return {};
    }
    const auto& view = payload.items[2].items[1];
    if (!view.is_list || view.items.size() <= 23 || !view.items[23].is_list) {
        return {};
    }
    const auto& columns = view.items[23];
    if (columns.items.size() <= 1) {
        return {};
    }
    std::string xml;
    xml += "<Columns>\n";
    for (std::size_t index = 1; index < columns.items.size(); ++index) {
        const auto& column = columns.items[index];
        std::string title = "Column" + std::to_string(index);
        find_first_localized_text(column, title);
        std::string type = "{\"Pattern\",{\"S\"}}";
        if (const auto* pattern = first_type_domain_pattern_value(column)) {
            type = oof::platform::stream::dump_compact(*pattern);
        }
        xml += "  <Column title=\"";
        xml += xml_escape(title);
        xml += "\">\n";
        append_type_domain_pattern_xml(xml, type, 4);
        xml += "  </Column>\n";
    }
    xml += "</Columns>";
    return xml;
}

std::vector<ControlInfoSlotProperty> control_info_slot_properties(
    const oof::platform::stream::ListValue& object_value,
    std::string_view platform_type
) {
    std::vector<ControlInfoSlotProperty> properties;
    const auto* info = control_info_slot_body(object_value);
    if (info == nullptr) {
        return properties;
    }
    const auto* descriptor = oof::platform::control_info::descriptor_for_control_type(platform_type);
    if (descriptor == nullptr) {
        return properties;
    }
    for (std::size_t index = 0; index < descriptor->slot_count; ++index) {
        const auto& slot = descriptor->slots[index];
        const auto* property_descriptor = oof::platform::property_registry::find_descriptor(slot.name);
        if (property_descriptor == nullptr ||
            property_descriptor->slot_codec != oof::platform::property_registry::SlotCodec::control_info_slot) {
            continue;
        }
        if (info->items.size() <= slot.index) {
            continue;
        }
        const std::string value = public_control_info_slot_value(info->items[slot.index]);
        if (!value.empty()) {
            properties.push_back({std::string(slot.name), value});
        }
    }
    return properties;
}

struct MaterializedFormEvent {
    std::string object_id;
    std::string owner_object_id;
    std::string id;
    std::string handler;
    std::string title;
    std::string path;
    std::size_t ordinal = 0;
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
    std::string table_columns_xml;
    std::vector<ControlInfoSlotProperty> control_info_properties;
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
    std::size_t ordinal = 0;
};

std::string materialized_command_object_id(std::string_view command_id, std::size_t ordinal) {
    return "command:" + std::string(command_id) + ":" + std::to_string(ordinal);
}

void assign_materialized_command_object_ids(std::vector<MaterializedFormCommand>& commands) {
    std::map<std::string, std::size_t> ordinals;
    for (auto& command : commands) {
        command.ordinal = ordinals[command.id]++;
        command.object_id = materialized_command_object_id(command.id, command.ordinal);
    }
}

std::string materialized_event_object_id(
    std::string_view owner_object_id,
    std::string_view event_id,
    std::size_t ordinal
) {
    return "event:" + std::string(owner_object_id) + ":" + std::string(event_id) + ":" + std::to_string(ordinal);
}

void assign_materialized_event_object_ids(std::vector<MaterializedFormEvent>& events) {
    std::map<std::pair<std::string, std::string>, std::size_t> ordinals;
    for (auto& event : events) {
        auto key = std::make_pair(event.owner_object_id, event.id);
        event.ordinal = ordinals[key]++;
        event.object_id = materialized_event_object_id(event.owner_object_id, event.id, event.ordinal);
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
           !value.items[0].is_list &&
           value.items[0].atom == "8" &&
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

bool looks_like_platform_event_action_record(const oof::platform::stream::ListValue& value) {
    return value.is_list &&
           value.items.size() >= 2 &&
           !value.items[0].is_list &&
           value.items[0].atom == "3" &&
           !value.items[1].is_list &&
           !value.items[1].atom.empty();
}

bool looks_like_platform_element_event_record(const oof::platform::stream::ListValue& value) {
    return value.is_list &&
           value.items.size() >= 3 &&
           !value.items[0].is_list &&
           !value.items[1].is_list &&
           is_guid_text(value.items[1].atom) &&
           looks_like_platform_event_action_record(value.items[2]);
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
        event.path = std::string(path);
        if (!event.id.empty() && !event.handler.empty()) {
            events.push_back(std::move(event));
        }
        return events;
    }
    if (looks_like_platform_element_event_record(value)) {
        MaterializedFormEvent event;
        event.owner_object_id = std::string(owner_object_id);
        event.id = value.items[1].atom;
        event.handler = value.items[2].items[1].atom;
        if (!find_first_localized_text(value.items[2], event.title)) {
            event.title = event.handler;
        }
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
            if (item.descriptor_binding->platform_type != "ActiveXControl" &&
                item.descriptor_binding->platform_type != "TableBox") {
                item.picture_payload = first_base64_picture_payload(value);
            }
            if (item.descriptor_binding->platform_type == "TableBox") {
                item.table_columns_xml = table_columns_xml_from_payload(value);
            }
            if (value.items.size() > 2 && value.items[2].is_list) {
                item.events = collect_materialized_form_events(value.items[2], item.object_id, child_path(path, 2));
            }
            if (value.items.size() > 3 && looks_like_materialized_form_event_record(value.items[3])) {
                auto legacy_events = collect_materialized_form_events(value.items[3], item.object_id, child_path(path, 3));
                item.events.insert(
                    item.events.end(),
                    std::make_move_iterator(legacy_events.begin()),
                    std::make_move_iterator(legacy_events.end()));
            }
            assign_materialized_event_object_ids(item.events);
            const auto* public_control_binding = oof::ordinary::control_type::binding_for_guid(item.guid);
            item.control_info_properties = control_info_slot_properties(
                value,
                public_control_binding != nullptr
                    ? public_control_binding->writer_control_type
                    : item.descriptor_binding->platform_type);
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

    if (looks_like_materialized_form_command_record(block)) {
        MaterializedFormCommand command;
        command.id = command_record_id_value(block);
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
    assign_materialized_command_object_ids(commands);
    return commands;
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

RuntimeFormEnvelope read_formbin_runtime_envelope(const std::string& input_path);
oof::platform::stream::ListValue platform_form_listout_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view title
);
oof::platform::stream::ListValue platform_form_listout_payload(
    const oof::ordinary::object::OrdinaryForm& form,
    std::string_view title
);

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
        form_object.form.identity = oof::platform::object_model::make_identity(
            "Form",
            "0",
            "platform-root-fixed-id; UUID stored as FormObjectUuid when present",
            {},
            {},
            {},
            "core85 ContextCore + mngbase RTLogForm",
            "Form",
            "mngcore_root.res:logform.xsd + mngcore_root.res:logform_layouter.xsd");
        form_object.form.properties.push_back(make_described_property("Type", "Form"));
        form_object.form.properties.push_back(make_described_property("RuntimeUUID", envelope.runtime_uuid));
        bool has_root_title = false;
        if (envelope.payload.is_list && envelope.payload.items.size() > 1) {
            const auto& root_record = envelope.payload.items[1];
            if (root_record.is_list && root_record.items.size() > 1) {
                std::string title;
                if (find_first_localized_text(root_record.items[1], title)) {
                    has_root_title = true;
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
        if (!has_root_title) {
            form_object.form.properties.push_back(make_platform_object_property(
                "Title", "Заголовок", {}, "LocalizedText",
                "ordinary form root title record", {}, {}, "stream"));
        }
        const std::string root_layout_xml = root_panel_layout_xml_from_payload(envelope.payload);
        if (!root_layout_xml.empty()) {
            form_object.form.properties.push_back(make_platform_object_property(
                "RootPanelLayoutXml", "", root_layout_xml, "RootPanelLayout",
                "OrdinaryFormPalette.xsd RootPanelLayout + ordinary root panel info record", {}, {}, "stream"));
        }
        bool has_form_object_info = false;
        if (envelope.payload.is_list && envelope.payload.items.size() > 3) {
            const auto& info = envelope.payload.items[3];
            if (info.is_list && info.items.size() >= 2 && !info.items[0].is_list && !info.items[1].is_list) {
                has_form_object_info = true;
                form_object.form.properties.push_back(make_platform_object_property(
                    "FormObjectUuid", "", info.items[0].atom, "UUID",
                    "ordinary form object info record", {}, {}, "stream"));
                form_object.form.identity.uuid = info.items[0].atom;
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
        if (!has_form_object_info) {
            form_object.form.identity.uuid = "00000000-0000-0000-0000-000000000000";
            form_object.form.properties.push_back(make_platform_object_property(
                "FormObjectUuid", "", "00000000-0000-0000-0000-000000000000", "UUID",
                "ordinary form object info record", {}, {}, "stream"));
            form_object.form.properties.push_back(make_platform_object_property(
                "FormObjectKind", "", "0", "xs:string",
                "ordinary form object info record", {}, {}, "stream"));
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
            object.identity = oof::platform::object_model::make_identity(
                attribute.object_id,
                {},
                "CompositeID preserved from logform.xsd Property@id; new ids require allocator",
                attribute.id,
                {},
                {},
                "mngcore logform.xsd Property",
                "Property",
                "mngcore_root.res:logform.xsd");
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
            form_object.add_edge(oof::platform::object_model::make_edge(
                "collection-member",
                "0",
                object.object_id,
                "Attributes",
                "PlatformObjectTypeHandler<Form>.Attributes + mngcore logform.xsd Property",
                "mngcore_root.res:logform.xsd",
                "Property@id",
                "CompositeID",
                object.name));
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
            object.identity = oof::platform::object_model::make_identity(
                command.object_id,
                {},
                "CompositeID preserved from logform.xsd Command/id; new ids require allocator",
                command.id,
                {},
                {},
                "mngcore logform.xsd Command + cmi.xsd CommandInfo",
                "Command",
                "mngcore_root.res:logform.xsd + mngcore_root.res:cmi.xsd");
            object.properties.push_back(make_platform_object_property(
                "ID", "Идентификатор", command.id, "CompositeID", "logform.xsd:Command/id"));
            object.properties.push_back(make_platform_object_property(
                "Name", "Имя", command.name, "String", "logform.xsd:Command@name + cmi.xsd CommandInfo@name"));
            object.properties.push_back(make_platform_object_property(
                "Handler", "Обработчик", command.handler, "String", "logform.xsd:Command@handler + cmi.xsd HandlerInfo/name"));
            object.properties.push_back(make_platform_object_property(
                "ModifiesData", "ИзменяетДанные", command.modifies_data, "Boolean", "logform.xsd:Command@modifiesData + cmi.xsd HandlerInfo/modifiesData"));
            add_api_surface(object, api_object_for_type("FormCommand"));
            form_object.add_edge(oof::platform::object_model::make_edge(
                "collection-member",
                "0",
                object.object_id,
                "Commands",
                "PlatformObjectTypeHandler<Form>.Commands + mngcore logform.xsd Command + cmi.xsd CommandInfo",
                "mngcore_root.res:logform.xsd + mngcore_root.res:cmi.xsd",
                "Command/id",
                "CompositeID",
                object.name));
            form_object.commands.add(std::move(object));
        }
    }

    static void materialize_items(
        oof::platform::object_model::PlatformFormObject& form_object,
        const MaterializedGraphSummary& summary
    ) {
        for (const auto& item : summary.items) {
            oof::platform::object_model::PlatformObject object;
            const auto* control_binding = oof::ordinary::control_type::binding_for_guid(item.guid);
            const std::string writer_control_type = control_binding != nullptr
                ? std::string(control_binding->writer_control_type)
                : std::string(item.descriptor_binding->platform_type);
            const std::string platform_schema_type = control_binding != nullptr
                ? std::string(control_binding->platform_type)
                : std::string(item.descriptor_binding->platform_type);
            const std::string stream_element = control_binding != nullptr
                ? std::string(control_binding->stream_element)
                : std::string(item.descriptor_binding->stream_element);
            object.object_id = item.object_id;
            object.name = item.name;
            object.platform_type = writer_control_type;
            object.type_category = "core::kLogFormTypeInfoCategory";
            object.type_source = "PlatformObjectTypeHandler<Form>.Items + " + std::string(item.descriptor_binding->evidence);
            object.path = item.parent_object_id.empty()
                ? "$/Items/" + item.object_id
                : "$/Items/" + item.parent_object_id + "/" + item.object_id;
            object.parent_object_id = item.parent_object_id;
            const auto* schema = oof::platform::form_schema::control_by_type_name(platform_schema_type);
            object.identity = oof::platform::object_model::make_identity(
                item.object_id,
                item.object_id,
                "platform object id preserved from cf_form_controls8 item record; name is mutable presentation, not identity",
                {},
                {},
                item.guid,
                std::string(item.descriptor_binding->evidence),
                stream_element,
                schema != nullptr ? std::string(schema->schema_source) : std::string{});
            object.properties.push_back(make_described_property("ObjectID", item.object_id));
            object.properties.push_back(make_described_property("Name", item.name));
            object.properties.push_back(make_described_property("Type", object.platform_type));
            object.properties.push_back(make_described_property("Parent", item.parent_object_id));
            object.properties.push_back(make_described_property("Path", object.path));
            object.properties.push_back(make_described_property("Title", item.title.empty() ? item.name : item.title));
            object.properties.push_back(make_described_property("Visible", item.visible.empty() ? "true" : item.visible));
            object.properties.push_back(make_described_property("Enabled", item.enabled.empty() ? "true" : item.enabled));
            object.properties.push_back(make_described_property("Events", std::to_string(item.events.size())));
            object.collections.push_back(make_described_collection("Events", item.events.size()));
            const std::string left = item.left.empty() ? "0" : item.left;
            const std::string top = item.top.empty() ? "0" : item.top;
            const std::string right = item.right.empty() ? "0" : item.right;
            const std::string bottom = item.bottom.empty() ? "0" : item.bottom;
            object.properties.push_back(make_described_property("Left", left));
            object.properties.push_back(make_described_property("Top", top));
            object.properties.push_back(make_described_property("Width", std::to_string(std::stoll(right) - std::stoll(left))));
            object.properties.push_back(make_described_property("Height", std::to_string(std::stoll(bottom) - std::stoll(top))));
            object.properties.push_back(make_described_property("Right", right));
            object.properties.push_back(make_described_property("Bottom", bottom));
            for (std::string_view binding_name : {"top", "bottom", "left", "right", "verticalCenter", "horizontalCenter"}) {
                std::string value = "0";
                for (const auto& binding : item.bindings) {
                    if (binding.name == binding_name) {
                        value = oof::platform::stream::dump_compact(binding.value);
                        break;
                    }
                }
                object.properties.push_back(make_described_property("Binding." + std::string(binding_name), std::move(value)));
            }
            for (std::string_view binding_name : {"height", "minHeight", "stretch", "width"}) {
                std::string value = "0";
                for (const auto& binding : item.dimension_bindings) {
                    if (binding.name == binding_name) {
                        value = oof::platform::stream::dump_compact(binding.value);
                        break;
                    }
                }
                object.properties.push_back(make_described_property("DimensionBinding." + std::string(binding_name), std::move(value)));
            }
            if (const auto* control_schema = oof::platform::form_schema::control_by_type_name(platform_schema_type)) {
                add_platform_object_schema_surface(
                    object,
                    oof::platform::object_schema::build_schema_for_control(*control_schema));
            } else {
                add_api_surface(object, api_object_for_type(object.platform_type));
            }
            for (const auto& property : item.control_info_properties) {
                if (object.property(property.name) == nullptr) {
                    object.properties.push_back(make_described_property(property.name, property.value));
                } else if (auto* existing = object.property(property.name)) {
                    existing->value = property.value;
                    existing->value_origin = "stream";
                }
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
            if (!item.table_columns_xml.empty()) {
                object.properties.push_back(make_platform_object_property(
                    "TableColumnsXml",
                    "Колонки",
                    item.table_columns_xml,
                    "TableColumns",
                    "cf_form_controls_info8:Table:View:Columns",
                    {},
                    {},
                    "stream",
                    "cf_form_controls_info8:Table:View:Columns",
                    "table-column-record",
                    true,
                    "TableBox::m_columns",
                    {}));
            }
            form_object.add_edge(oof::platform::object_model::make_edge(
                "uses-schema",
                object.object_id,
                object.object_id,
                object.platform_type,
                object.type_source,
                object.identity.schema_source,
                object.identity.stream_element,
                "ordinary-form-control",
                object.name,
                schema != nullptr));
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
            object.path = "$/Events/" + event.owner_object_id + "/" + event.id + "/" + std::to_string(event.ordinal);
            object.parent_object_id = event.owner_object_id;
            object.identity = oof::platform::object_model::make_identity(
                event.object_id,
                {},
                "UUID preserved from logform.xsd Event/id; new event ids use platform UUID policy",
                {},
                event.id,
                {},
                "mngcore logform.xsd Event",
                "Event",
                "mngcore_root.res:logform.xsd");
            object.properties.push_back(make_platform_object_property(
                "ID", "Идентификатор", event.id, "UUID", "logform.xsd:Event/id"));
            object.properties.push_back(make_platform_object_property(
                "Handler", "Обработчик", event.handler, "String", "logform.xsd:Event@handler"));
            object.properties.push_back(make_platform_object_property(
                "Title", "Представление", event.title, "String", "cf_form_controls8 Event action presentation"));
            object.properties.push_back(make_platform_object_property(
                "Parent", "Родитель", event.owner_object_id, "FormItem", "m_elementEvents owner"));
            add_api_surface(object, api_object_for_type("FormEvent"));
            form_object.add_edge(oof::platform::object_model::make_edge(
                "owns-event",
                event.owner_object_id,
                object.object_id,
                "Events",
                "m_elementEvents owner + mngcore logform.xsd Event",
                "mngcore_root.res:logform.xsd",
                "Event/id",
                "UUID",
                event.handler));
            form_object.events.add(std::move(object));
        }
    }

    static void link_item_children(oof::platform::object_model::PlatformFormObject& form_object) {
        for (std::size_t index = 0; index < form_object.items.objects().size(); ++index) {
            const auto& object = form_object.items.objects()[index];
            if (object.parent_object_id.empty()) {
                form_object.form.children.push_back(index);
                form_object.add_edge(oof::platform::object_model::make_edge(
                    "contains",
                    "0",
                    object.object_id,
                    "ChildItems",
                    "cf_form_controls8 containment + managed-form-style ChildItems projection",
                    object.identity.schema_source,
                    object.identity.stream_element,
                    object.platform_type,
                    object.name));
                continue;
            }
            for (auto& maybe_parent : form_object.items.mutable_objects()) {
                if (maybe_parent.object_id == object.parent_object_id) {
                    maybe_parent.children.push_back(index);
                    form_object.add_edge(oof::platform::object_model::make_edge(
                        "contains",
                        maybe_parent.object_id,
                        object.object_id,
                        "ChildItems",
                        "cf_form_controls8 containment + managed-form-style ChildItems projection",
                        object.identity.schema_source,
                        object.identity.stream_element,
                        object.platform_type,
                        object.name));
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

oof::platform::object_model::PlatformFormObject make_empty_platform_form_object(
    std::string title = {}
) {
    oof::platform::object_model::PlatformFormObject form_object;
    form_object.form.object_id = "0";
    form_object.form.name = "Form";
    form_object.form.platform_type = "Form";
    form_object.form.type_category = "core::kLogFormTypeInfoCategory";
    form_object.form.type_source = "EmptyOrdinaryFormObject.default-contract";
    form_object.form.path = "$";
    form_object.form.identity = oof::platform::object_model::make_identity(
        "Form",
        "0",
        "platform-root-fixed-id; empty ordinary form starts from default object contract",
        {},
        {},
        {},
        "EmptyOrdinaryFormObject + mngbase RTLogForm",
        "Form",
        "mngcore_root.res:logform.xsd + mngcore_root.res:logform_layouter.xsd");
    form_object.form.properties.push_back(make_described_property("Type", "Form"));
    if (!title.empty()) {
        form_object.form.properties.push_back(make_platform_object_property(
            "Title",
            "Заголовок",
            std::move(title),
            "LocalizedText",
            "EmptyOrdinaryFormObject.Title",
            {},
            {},
            "object-explicit"));
    }
    add_platform_object_schema_surface(
        form_object.form,
        oof::platform::object_schema::build_schema_for_root_form());
    add_api_surface(form_object.form, api_object_for_type("Form"));
    form_object.form.properties.push_back(make_described_property("Items", "0"));
    form_object.form.properties.push_back(make_described_property("Attributes", "0"));
    form_object.form.properties.push_back(make_described_property("Commands", "0"));
    form_object.form.properties.push_back(make_described_property("Events", "0"));
    form_object.form.collections.push_back(make_described_collection("Items", 0));
    form_object.form.collections.push_back(make_described_collection("Attributes", 0));
    form_object.form.collections.push_back(make_described_collection("Commands", 0));
    form_object.form.collections.push_back(make_described_collection("Events", 0));
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
    std::cout << ",\"identity\":{\"publicId\":";
    print_json_string(object.identity.public_id);
    std::cout << ",\"platformObjectId\":";
    print_json_string(object.identity.platform_object_id);
    std::cout << ",\"compositeId\":";
    print_json_string(object.identity.composite_id);
    std::cout << ",\"uuid\":";
    print_json_string(object.identity.uuid);
    std::cout << ",\"classGuid\":";
    print_json_string(object.identity.class_guid);
    std::cout << ",\"classGuidSource\":";
    print_json_string(object.identity.class_guid_source);
    std::cout << ",\"streamElement\":";
    print_json_string(object.identity.stream_element);
    std::cout << ",\"schemaSource\":";
    print_json_string(object.identity.schema_source);
    std::cout << ",\"identityPolicy\":";
    print_json_string(object.identity.identity_policy);
    std::cout << "},\"properties\":[";
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
    std::cout << ",\"edges\":[";
    for (std::size_t index = 0; index < form_object.edges.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& edge = form_object.edges[index];
        std::cout << "{\"kind\":";
        print_json_string(edge.kind);
        std::cout << ",\"fromObjectId\":";
        print_json_string(edge.from_object_id);
        std::cout << ",\"toObjectId\":";
        print_json_string(edge.to_object_id);
        std::cout << ",\"role\":";
        print_json_string(edge.role);
        std::cout << ",\"name\":";
        print_json_string(edge.name);
        std::cout << ",\"source\":";
        print_json_string(edge.source);
        std::cout << ",\"schemaSource\":";
        print_json_string(edge.schema_source);
        std::cout << ",\"slotBinding\":";
        print_json_string(edge.slot_binding);
        std::cout << ",\"valueType\":";
        print_json_string(edge.value_type);
        std::cout << ",\"required\":" << (edge.required ? "true" : "false");
        std::cout << "}";
    }
    std::cout << "]";
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

std::string public_xml_tag_for_platform_type(std::string_view platform_type, std::string_view class_guid = {}) {
    if (!class_guid.empty()) {
        if (const auto* binding = oof::ordinary::control_type::binding_for_guid(class_guid)) {
            return std::string(binding->public_xml_tag);
        }
    }
    if (const auto* binding = oof::ordinary::control_type::binding_for_writer_control_type(platform_type)) {
        return std::string(binding->public_xml_tag);
    }
    if (const auto* binding = oof::ordinary::control_type::unambiguous_binding_for_platform_type(platform_type)) {
        return std::string(binding->public_xml_tag);
    }
    if (platform_type == "FormattedDocument") {
        return "FormattedDocumentField";
    }
    if (platform_type == "PanelPage") {
        return "Page";
    }
    return std::string(platform_type);
}

std::string platform_type_for_public_xml_tag(std::string_view tag) {
    if (const auto* binding = oof::ordinary::control_type::binding_for_public_xml_tag(tag)) {
        return std::string(binding->writer_control_type);
    }
    if (tag == "FormattedDocumentField") {
        return "FormattedDocument";
    }
    if (tag == "Page") {
        return "PanelPage";
    }
    return std::string(tag);
}

std::string platform_schema_type_for_public_xml_tag(std::string_view tag) {
    if (const auto* binding = oof::ordinary::control_type::binding_for_public_xml_tag(tag)) {
        return std::string(binding->platform_type);
    }
    return platform_type_for_public_xml_tag(tag);
}

std::string stream_element_for_public_xml_tag(std::string_view tag) {
    if (const auto* binding = oof::ordinary::control_type::binding_for_public_xml_tag(tag)) {
        return std::string(binding->stream_element);
    }
    return {};
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
    std::string read_only;
    bool has_read_only = false;
    std::string left;
    std::string top;
    std::string right;
    std::string bottom;
    bool has_position = false;
    std::string table_columns_xml;
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

oof::platform::object_model::PlatformFormObjectEdit public_xml_edits_to_platform_object_edits(
    const std::vector<PublicXmlControlEdit>& edits
);

PublicXmlApplyResult apply_platform_object_edits(
    RuntimeFormEnvelope& envelope,
    const oof::platform::object_model::PlatformFormObjectEdit& object_edit
);

PublicXmlApplyResult apply_platform_object_edits_to_object(
    oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformFormObjectEdit& object_edit
);

bool set_property_slot_value(
    oof::platform::stream::ListValue& payload,
    std::string_view object_id,
    const oof::platform::property_registry::PlatformPropertyDescriptor& descriptor,
    std::string_view new_value
);

void append_indent(std::string& out, int indent);

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

XmlElementSlice first_xml_element(std::string_view text, std::string_view tag);

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
        result.insert("Clsid");
        result.insert("State1");
        result.insert("State2");
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

std::string xml_local_name(std::string_view name) {
    const std::size_t colon = name.rfind(':');
    if (colon == std::string_view::npos) {
        return std::string(name);
    }
    return std::string(name.substr(colon + 1));
}

bool public_xml_forbidden_raw_name(std::string_view name) {
    const std::string local = xml_local_name(name);
    const std::string lower = ascii_lower(local);
    static const std::set<std::string> forbidden{
        "actionprofile",
        "bracketstream",
        "datasourceprofile",
        "dimensionprofile",
        "formbin",
        "linkmodeshape",
        "liststream",
        "logicalstream",
        "objectmodel",
        "pages",
        "platformrecords",
        "profileuuid",
        "rawbracket",
        "rootrecord",
        "serializationprofile",
        "dimensionsegments",
        "layoutflag1",
        "layoutflag2",
        "layoutgroup",
        "layoutmode",
        "layoutnextorder",
        "layoutorder",
        "secondarydimensionmarker",
        "stateblob",
        "toplevel",
        "unit",
        "valuedescriptor",
        "viewprofile",
    };
    if (forbidden.count(lower) != 0) {
        return true;
    }
    if (lower.find("profile") != std::string::npos) {
        return true;
    }
    if (lower.find("linkmodeshape") != std::string::npos) {
        return true;
    }
    if (lower.rfind("raw", 0) == 0) {
        return true;
    }
    static const std::regex slotn(R"(^slot[0-9]+$)", std::regex_constants::icase);
    return std::regex_match(local, slotn);
}

void validate_public_xml_has_no_raw_vocabulary(const std::string& xml) {
    const std::regex tag_pattern(R"(<\s*/?\s*([A-Za-z_][A-Za-z0-9_:.-]*)\b([^<>]*)>)");
    const std::regex attr_pattern(R"(([A-Za-z_][A-Za-z0-9_:.-]*)\s*=)");
    for (std::sregex_iterator it(xml.begin(), xml.end(), tag_pattern), end; it != end; ++it) {
        const std::string tag = (*it)[1].str();
        if (public_xml_forbidden_raw_name(tag)) {
            throw std::runtime_error("OrdinaryForm XML must not contain raw/profile/indexed public element: " + tag);
        }
        const std::string attrs = (*it)[2].str();
        for (std::sregex_iterator attr_it(attrs.begin(), attrs.end(), attr_pattern), attr_end; attr_it != attr_end; ++attr_it) {
            const std::string attr = (*attr_it)[1].str();
            if (public_xml_forbidden_raw_name(attr)) {
                throw std::runtime_error("OrdinaryForm XML must not contain raw/profile/indexed public attribute: " + attr);
            }
        }
    }
}

std::vector<PublicXmlControlEdit> parse_public_xml_control_edits(const std::string& xml) {
    validate_public_xml_has_no_raw_vocabulary(xml);
    if (xml.find("ordinaryFormVersion=\"2.") == std::string::npos) {
        throw std::runtime_error("expected OrdinaryForm XML with ordinaryFormVersion=\"2.*\"");
    }

    const std::set<std::string> section_tags{
        "Form", "Events", "Event", "ChildItems", "Attributes", "Attribute", "Commands", "Command",
        "Title", "Position", "Pages", "Columns", "Column"
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
            std::string own_body = body;
            const std::size_t child_items_start = own_body.find("<ChildItems>");
            if (child_items_start != std::string::npos) {
                own_body.resize(child_items_start);
            }
            const std::size_t title_start = own_body.find("<Title>");
            if (title_start != std::string::npos) {
                const std::size_t title_value_start = title_start + std::string("<Title>").size();
                const std::size_t title_end = own_body.find("</Title>", title_value_start);
                if (title_end != std::string::npos) {
                    edit.title = xml_unescape(own_body.substr(title_value_start, title_end - title_value_start));
                    edit.has_title = true;
                }
            }
            const std::size_t visible_start = own_body.find("<Visible>");
            if (visible_start != std::string::npos) {
                const std::size_t visible_value_start = visible_start + std::string("<Visible>").size();
                const std::size_t visible_end = own_body.find("</Visible>", visible_value_start);
                if (visible_end != std::string::npos) {
                    edit.visible = xml_unescape(own_body.substr(visible_value_start, visible_end - visible_value_start));
                    edit.has_visible = true;
                }
            }
            const std::size_t enabled_start = own_body.find("<Enabled>");
            if (enabled_start != std::string::npos) {
                const std::size_t enabled_value_start = enabled_start + std::string("<Enabled>").size();
                const std::size_t enabled_end = own_body.find("</Enabled>", enabled_value_start);
                if (enabled_end != std::string::npos) {
                    edit.enabled = xml_unescape(own_body.substr(enabled_value_start, enabled_end - enabled_value_start));
                    edit.has_enabled = true;
                }
            }
            const std::size_t read_only_start = own_body.find("<ReadOnly>");
            if (read_only_start != std::string::npos) {
                const std::size_t read_only_value_start = read_only_start + std::string("<ReadOnly>").size();
                const std::size_t read_only_end = own_body.find("</ReadOnly>", read_only_value_start);
                if (read_only_end != std::string::npos) {
                    edit.read_only = xml_unescape(own_body.substr(read_only_value_start, read_only_end - read_only_value_start));
                    edit.has_read_only = true;
                }
            }
            for (const auto& property_name : public_schema_property_names()) {
                for (const auto& property_xml : find_xml_elements(own_body, property_name)) {
                    if (property_xml.self_closing) {
                        continue;
                    }
                    edit.schema_properties.push_back({
                        property_name,
                        public_value_object_xml_literal(property_xml.body),
                    });
                }
            }
            if (tag == "Table") {
                for (const auto& columns_xml : find_xml_elements(own_body, "Columns")) {
                    std::string columns_value;
                    columns_value += "<Columns>\n";
                    for (const auto& column_xml : find_xml_elements(columns_xml.body, "Column")) {
                        columns_value += "  <Column title=\"";
                        columns_value += xml_escape(xml_attr_value(column_xml.attrs, "title"));
                        columns_value += "\">\n";
                        if (const auto type_xml = first_xml_element(column_xml.body, "Type");
                            !type_xml.body.empty() || type_xml.self_closing) {
                            columns_value += "    <Type>";
                            columns_value += type_xml.body;
                            columns_value += "</Type>\n";
                        }
                        columns_value += "  </Column>\n";
                    }
                    columns_value += "</Columns>";
                    edit.table_columns_xml = std::move(columns_value);
                    break;
                }
            }
            const std::size_t position_start = own_body.find("<Position");
            if (position_start != std::string::npos) {
                const std::size_t position_end = own_body.find(">", position_start);
                if (position_end != std::string::npos) {
                    const std::string position_tag = own_body.substr(position_start, position_end - position_start + 1);
                    edit.left = xml_attr_value(position_tag, "left");
                    edit.top = xml_attr_value(position_tag, "top");
                    edit.right = xml_attr_value(position_tag, "right");
                    edit.bottom = xml_attr_value(position_tag, "bottom");
                    edit.has_position = !edit.left.empty() &&
                                        !edit.top.empty() &&
                                        !edit.right.empty() &&
                                        !edit.bottom.empty();
                    const std::size_t position_close = own_body.find("</Position>", position_end);
                    if (position_close != std::string::npos) {
                        const std::string position_body = own_body.substr(position_end + 1, position_close - position_end - 1);
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
        "From", "To", "Extra", "Item", "Type", "TypePattern", "Any", "ObjectType", "TypeValue",
        "Reference", "List", "String", "Binary", "Date", "Number", "Columns", "Column"
    };
    return tags;
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
    std::size_t start = text.find(open);
    while (start != std::string::npos) {
        const std::size_t name_end = start + open.size();
        if (name_end >= text.size()) {
            return {};
        }
        const char after_name = text[name_end];
        if (!std::isalnum(static_cast<unsigned char>(after_name)) && after_name != '_' && after_name != '-') {
            break;
        }
        start = text.find(open, name_end);
    }
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t name_end = start + open.size();
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

std::string bool_xml_attr(bool value) {
    return value ? "true" : "false";
}

bool xml_bool_attr_value(std::string_view attrs, std::string_view name, bool default_value) {
    const std::string value = xml_attr_value(attrs, name);
    if (value.empty()) {
        return default_value;
    }
    return value == "1" || value == "true" || value == "True";
}

std::uint32_t xml_uint_attr_value(std::string_view attrs, std::string_view name, std::uint32_t default_value = 0) {
    const std::string value = xml_attr_value(attrs, name);
    if (value.empty()) {
        return default_value;
    }
    return static_cast<std::uint32_t>(std::stoul(value));
}

oof::platform::value::TypeDomainPattern parse_type_domain_pattern_text(std::string_view text) {
    oof::platform::stream::ListInStream in(text);
    return oof::platform::value::TypeDomainPattern::deserialize(in);
}

const char* public_type_domain_entry_tag(oof::platform::value::TypeDomainTerm term, const std::string& type_guid) {
    using oof::platform::value::TypeDomainTerm;
    switch (term) {
        case TypeDomainTerm::list:
            return "List";
        case TypeDomainTerm::binary:
            return "Binary";
        case TypeDomainTerm::date:
            return "Date";
        case TypeDomainTerm::numeric:
            return "Number";
        case TypeDomainTerm::reference:
            return "Reference";
        case TypeDomainTerm::string:
            return "String";
        case TypeDomainTerm::type:
            return "TypeValue";
        case TypeDomainTerm::unknown:
            return type_guid.empty() ? "Any" : "ObjectType";
    }
    return "Any";
}

void append_type_domain_pattern_xml(std::string& out, std::string_view type_text, int indent) {
    const auto pattern = parse_type_domain_pattern_text(type_text);
    append_indent(out, indent);
    out += "<Type>\n";
    append_indent(out, indent + 2);
    out += "<TypePattern>\n";
    for (const auto& entry : pattern.entries) {
        append_indent(out, indent + 4);
        const std::string tag = public_type_domain_entry_tag(entry.term, entry.type_guid);
        out += "<";
        out += tag;
        switch (entry.term) {
            case oof::platform::value::TypeDomainTerm::numeric:
                if (entry.numeric.length != 0 || entry.numeric.precision != 0 || entry.numeric.non_negative) {
                    out += " length=\"";
                    out += std::to_string(entry.numeric.length);
                    out += "\" precision=\"";
                    out += std::to_string(entry.numeric.precision);
                    out += "\" nonNegative=\"";
                    out += bool_xml_attr(entry.numeric.non_negative);
                    out += "\"";
                }
                break;
            case oof::platform::value::TypeDomainTerm::string:
                if (entry.string.length != 0) {
                    out += " length=\"";
                    out += std::to_string(entry.string.length);
                    out += "\" variable=\"";
                    out += bool_xml_attr(entry.string.variable);
                    out += "\"";
                }
                break;
            case oof::platform::value::TypeDomainTerm::binary:
                if (entry.binary.length != 0) {
                    out += " length=\"";
                    out += std::to_string(entry.binary.length);
                    out += "\" variable=\"";
                    out += bool_xml_attr(entry.binary.variable);
                    out += "\"";
                }
                break;
            case oof::platform::value::TypeDomainTerm::date:
                out += " date=\"";
                out += bool_xml_attr(entry.date.date);
                out += "\" time=\"";
                out += bool_xml_attr(entry.date.time);
                out += "\"";
                break;
            case oof::platform::value::TypeDomainTerm::type:
            case oof::platform::value::TypeDomainTerm::reference:
            case oof::platform::value::TypeDomainTerm::list:
            case oof::platform::value::TypeDomainTerm::unknown:
                if (!entry.type_guid.empty()) {
                    out += " guid=\"";
                    out += xml_escape(entry.type_guid);
                    out += "\"";
                }
                break;
        }
        out += "/>\n";
    }
    append_indent(out, indent + 2);
    out += "</TypePattern>\n";
    append_indent(out, indent);
    out += "</Type>\n";
}

std::optional<oof::platform::value::TypeDomainEntry> type_domain_entry_from_public_xml(
    std::string_view tag,
    const XmlElementSlice& xml
) {
    oof::platform::value::TypeDomainEntry entry;
    if (tag == "Any") {
        entry.term = oof::platform::value::TypeDomainTerm::unknown;
    } else if (tag == "ObjectType") {
        entry.term = oof::platform::value::TypeDomainTerm::unknown;
        entry.type_guid = xml_attr_value(xml.attrs, "guid");
    } else if (tag == "TypeValue") {
        entry.term = oof::platform::value::TypeDomainTerm::type;
        entry.type_guid = xml_attr_value(xml.attrs, "guid");
    } else if (tag == "Reference") {
        entry.term = oof::platform::value::TypeDomainTerm::reference;
        entry.type_guid = xml_attr_value(xml.attrs, "guid");
    } else if (tag == "List") {
        entry.term = oof::platform::value::TypeDomainTerm::list;
        entry.type_guid = xml_attr_value(xml.attrs, "guid");
    } else if (tag == "String") {
        entry.term = oof::platform::value::TypeDomainTerm::string;
        entry.string.length = xml_uint_attr_value(xml.attrs, "length");
        entry.string.variable = xml_bool_attr_value(xml.attrs, "variable", true);
    } else if (tag == "Binary") {
        entry.term = oof::platform::value::TypeDomainTerm::binary;
        entry.binary.length = xml_uint_attr_value(xml.attrs, "length");
        entry.binary.variable = xml_bool_attr_value(xml.attrs, "variable", true);
    } else if (tag == "Date") {
        entry.term = oof::platform::value::TypeDomainTerm::date;
        entry.date.date = xml_bool_attr_value(xml.attrs, "date", true);
        entry.date.time = xml_bool_attr_value(xml.attrs, "time", true);
    } else if (tag == "Number") {
        entry.term = oof::platform::value::TypeDomainTerm::numeric;
        entry.numeric.length = xml_uint_attr_value(xml.attrs, "length");
        entry.numeric.precision = xml_uint_attr_value(xml.attrs, "precision");
        entry.numeric.non_negative = xml_bool_attr_value(xml.attrs, "nonNegative", false);
    } else {
        return std::nullopt;
    }
    return entry;
}

std::string type_domain_pattern_text_from_public_type_xml(const XmlElementSlice& type_xml) {
    const auto pattern_xml = first_xml_element(type_xml.body, "TypePattern");
    if (pattern_xml.body.empty() && !pattern_xml.self_closing) {
        return xml_unescape(type_xml.body);
    }
    oof::platform::value::TypeDomainPattern pattern;
    const std::regex entry_tag_pattern(R"(<(Any|ObjectType|TypeValue|Reference|List|String|Binary|Date|Number)\b([^>]*)/>)");
    for (std::sregex_iterator it(pattern_xml.body.begin(), pattern_xml.body.end(), entry_tag_pattern), end; it != end; ++it) {
        XmlElementSlice entry_xml;
        entry_xml.attrs = (*it)[2].str();
        entry_xml.self_closing = true;
        auto entry = type_domain_entry_from_public_xml((*it)[1].str(), entry_xml);
        if (entry.has_value()) {
            pattern.entries.push_back(std::move(*entry));
        }
    }
    if (pattern.entries.empty()) {
        const std::regex open_entry_tag_pattern(R"(<(Any|ObjectType|TypeValue|Reference|List|String|Binary|Date|Number)\b([^>]*)>)");
        for (std::sregex_iterator it(pattern_xml.body.begin(), pattern_xml.body.end(), open_entry_tag_pattern), end; it != end; ++it) {
            const std::string full_tag = (*it)[0].str();
            if (full_tag.size() >= 2 && full_tag[full_tag.size() - 2] == '/') {
                continue;
            }
            XmlElementSlice entry_xml;
            entry_xml.attrs = (*it)[2].str();
            auto entry = type_domain_entry_from_public_xml((*it)[1].str(), entry_xml);
            if (entry.has_value()) {
                pattern.entries.push_back(std::move(*entry));
            }
        }
    }
    return pattern.serialize_list_stream();
}

std::string ordinary_form_listout_control_guid(std::string_view platform_type);

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
        object.identity = oof::platform::object_model::make_identity(
            object.object_id,
            {},
            "CompositeID preserved from public XML Attribute@id; new ids require allocator",
            xml_attr_value(attribute_xml.attrs, "id"),
            {},
            {},
            "mngcore logform.xsd Property",
            "Property",
            "mngcore_root.res:logform.xsd");
        object.properties.push_back(make_platform_object_property("ID", "Идентификатор", xml_attr_value(attribute_xml.attrs, "id"), "CompositeID", "OrdinaryForm.xml Attribute@id", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Name", "Имя", object.name, "String", "OrdinaryForm.xml Attribute@name", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Main", "Основной", xml_attr_value(attribute_xml.attrs, "main"), "Boolean", "OrdinaryForm.xml Attribute@main", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("StoredData", "СохраняемыеДанные", xml_attr_value(attribute_xml.attrs, "storedData"), "Boolean", "OrdinaryForm.xml Attribute@storedData", {}, {}, "public-xml"));
        const auto type_xml = first_xml_element(attribute_xml.body, "Type");
        object.properties.push_back(make_platform_object_property("Type", "Тип", type_domain_pattern_text_from_public_type_xml(type_xml), "TypeDomainPattern", "OrdinaryForm.xml Attribute/Type", {}, {}, "public-xml"));
        add_api_surface(object, api_object_for_type("FormAttribute"));
        form_object.add_edge(oof::platform::object_model::make_edge(
            "collection-member",
            "0",
            object.object_id,
            "Attributes",
            "PublicOrdinaryFormXml.Attributes + mngcore logform.xsd Property",
            "mngcore_root.res:logform.xsd",
            "Property@id",
            "CompositeID",
            object.name));
        form_object.attributes.add(std::move(object));
    }

    std::map<std::string, std::size_t> command_ordinals;
    for (const auto& command_xml : find_xml_elements(xml, "Command")) {
        const std::string id = xml_attr_value(command_xml.attrs, "id");
        const std::size_t ordinal = command_ordinals[id]++;
        std::string object_id = xml_attr_value(command_xml.attrs, "objectId");
        if (object_id.empty()) {
            object_id = materialized_command_object_id(id, ordinal);
        }
        oof::platform::object_model::PlatformObject object;
        object.object_id = object_id;
        object.name = xml_attr_value(command_xml.attrs, "name");
        object.platform_type = "FormCommand";
        object.type_category = "core::kLogFormTypeInfoCategory";
        object.type_source = "PublicOrdinaryFormXml.Commands";
        object.parent_object_id = "0";
        object.identity = oof::platform::object_model::make_identity(
            object.object_id,
            {},
            "CompositeID preserved from public XML Command@id; new ids require allocator",
            id,
            {},
            {},
            "mngcore logform.xsd Command + cmi.xsd CommandInfo",
            "Command",
            "mngcore_root.res:logform.xsd + mngcore_root.res:cmi.xsd");
        object.properties.push_back(make_platform_object_property("ID", "Идентификатор", id, "CompositeID", "OrdinaryForm.xml Command@id", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Name", "Имя", object.name, "String", "OrdinaryForm.xml Command@name", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Handler", "Обработчик", xml_attr_value(command_xml.attrs, "handler"), "String", "OrdinaryForm.xml Command@handler", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("ModifiesData", "ИзменяетДанные", xml_attr_value(command_xml.attrs, "modifiesData"), "Boolean", "OrdinaryForm.xml Command@modifiesData", {}, {}, "public-xml"));
        add_api_surface(object, api_object_for_type("FormCommand"));
        form_object.add_edge(oof::platform::object_model::make_edge(
            "collection-member",
            "0",
            object.object_id,
            "Commands",
            "PublicOrdinaryFormXml.Commands + mngcore logform.xsd Command + cmi.xsd CommandInfo",
            "mngcore_root.res:logform.xsd + mngcore_root.res:cmi.xsd",
            "Command/id",
            "CompositeID",
            object.name));
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
    std::map<std::string, std::size_t> event_ordinals;
    for (const auto& event_xml : find_xml_elements(events_xml.body, "Event")) {
        oof::platform::object_model::PlatformObject object;
        const std::string id = xml_attr_value(event_xml.attrs, "id");
        const std::size_t ordinal = event_ordinals[id]++;
        object.object_id = xml_attr_value(event_xml.attrs, "objectId");
        if (object.object_id.empty()) {
            object.object_id = materialized_event_object_id(owner_object_id, id, ordinal);
        }
        object.name = xml_attr_value(event_xml.attrs, "handler");
        object.platform_type = "FormEvent";
        object.type_category = "core::kLogFormTypeInfoCategory";
        object.type_source = "PublicOrdinaryFormXml.Events";
        object.parent_object_id = std::string(owner_object_id);
        object.identity = oof::platform::object_model::make_identity(
            object.object_id,
            {},
            "UUID preserved from public XML Event@id; new event ids use platform UUID policy",
                {},
                id,
                {},
                "mngcore logform.xsd Event",
                "Event",
                "mngcore_root.res:logform.xsd");
        object.properties.push_back(make_platform_object_property("ID", "Идентификатор", id, "UUID", "OrdinaryForm.xml Event@id", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Handler", "Обработчик", object.name, "String", "OrdinaryForm.xml Event@handler", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Title", "Представление", xml_attr_value(event_xml.attrs, "title"), "String", "OrdinaryForm.xml Event@title", {}, {}, "public-xml"));
        object.properties.push_back(make_platform_object_property("Parent", "Родитель", std::string(owner_object_id), "FormItem", "OrdinaryForm.xml Event@ownerId", {}, {}, "public-xml"));
        add_api_surface(object, api_object_for_type("FormEvent"));
        form_object.add_edge(oof::platform::object_model::make_edge(
            "owns-event",
            std::string(owner_object_id),
            object.object_id,
            "Events",
            "PublicOrdinaryFormXml.Events + mngcore logform.xsd Event",
            "mngcore_root.res:logform.xsd",
            "Event/id",
            "UUID",
            object.name));
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
        std::string schema_source;
        std::string stream_element = stream_element_for_public_xml_tag(tag);
        const std::string platform_schema_type = platform_schema_type_for_public_xml_tag(tag);
        object.properties.push_back(make_described_property("ObjectID", object.object_id));
        object.properties.push_back(make_described_property("Name", object.name));
        object.properties.push_back(make_described_property("Type", object.platform_type));
        object.properties.push_back(make_described_property("Parent", object.parent_object_id));
        object.properties.push_back(make_described_property("Path", object.path));
        if (const auto* schema = oof::platform::form_schema::control_by_type_name(platform_schema_type)) {
            schema_source = schema->schema_source;
            add_platform_object_schema_surface(object, oof::platform::object_schema::build_schema_for_control(*schema));
        } else {
            add_api_surface(object, api_object_for_type(object.platform_type));
        }
        const std::string class_guid = ordinary_form_listout_control_guid(object.platform_type);
        object.identity = oof::platform::object_model::make_identity(
            object.object_id,
            object.object_id,
            "platform object id preserved from public XML control id; name is mutable presentation, not identity",
            {},
            {},
            class_guid,
            "PublicOrdinaryFormXml.ChildItems + OrdinaryFormPalette.xsd",
            stream_element,
            schema_source);

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
            if (edit.has_read_only) {
                set_or_add_described_property(object, "ReadOnly", edit.read_only);
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
            if (!edit.table_columns_xml.empty()) {
                set_or_add_described_property(object, "TableColumnsXml", edit.table_columns_xml);
            }
            break;
        }

        const std::size_t new_index = form_object.items.count();
        form_object.items.add(std::move(object));
        if (parent_object_id.empty()) {
            form_object.form.children.push_back(new_index);
            form_object.add_edge(oof::platform::object_model::make_edge(
                "contains",
                "0",
                object_id,
                "ChildItems",
                "PublicOrdinaryFormXml.ChildItems + managed-form-style ChildItems projection",
                schema_source,
                stream_element,
                object.platform_type,
                xml_attr_value(attrs, "name")));
        } else if (auto* parent = form_object.find_object_by_id(parent_object_id)) {
            parent->children.push_back(new_index);
            form_object.add_edge(oof::platform::object_model::make_edge(
                "contains",
                std::string(parent_object_id),
                object_id,
                "ChildItems",
                "PublicOrdinaryFormXml.ChildItems + managed-form-style ChildItems projection",
                schema_source,
                stream_element,
                object.platform_type,
                xml_attr_value(attrs, "name")));
        }
        form_object.add_edge(oof::platform::object_model::make_edge(
            "uses-schema",
            object_id,
            object_id,
            object.platform_type,
            "PublicOrdinaryFormXml.ChildItems + OrdinaryFormPalette.xsd",
            schema_source,
            stream_element,
            "ordinary-form-control",
            xml_attr_value(attrs, "name"),
            !schema_source.empty()));
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
    form_object.form.identity = oof::platform::object_model::make_identity(
        "Form",
        "0",
        "platform-root-fixed-id; UUID stored as FormObjectUuid when present",
        {},
        xml_attr_value(form_xml.attrs, "formObjectUuid"),
        {},
        "PublicOrdinaryFormXml + mngbase RTLogForm",
        "Form",
        "mngcore_root.res:logform.xsd + mngcore_root.res:logform_layouter.xsd");
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

void append_platform_object_signature(
    std::ostringstream& out,
    const oof::platform::object_model::PlatformObject& object
) {
    out << "object{"
        << "id=" << object.object_id
        << ";name=" << object.name
        << ";type=" << object.platform_type
        << ";category=" << object.type_category
        << ";path=" << object.path
        << ";parent=" << object.parent_object_id
        << ";publicId=" << object.identity.public_id
        << ";platformObjectId=" << object.identity.platform_object_id
        << ";compositeId=" << object.identity.composite_id
        << ";uuid=" << object.identity.uuid
        << ";classGuid=" << object.identity.class_guid
        << ";streamElement=" << object.identity.stream_element
        << ";children=";
    for (const auto child : object.children) {
        out << child << ",";
    }
    out << ";properties=[";
    for (const auto& property : object.properties) {
        out << property.name
            << "/" << property.localized_name
            << "=" << property.value
            << "<" << property.value_type
            << "|default=" << property.default_value
            << "|origin=" << property.value_origin
            << "|member=" << property.platform_member
            << "|slot=" << property.slot_binding
            << "|codec=" << property.slot_codec
            << "|valueObject=" << property.value_object_class
            << "|literal=" << property.value_object_literal
            << "|schema=" << property.value_object_schema_value
            << "|stream=" << property.value_object_list_stream
            << ">;";
    }
    out << "];events=[";
    for (const auto& event : object.events) {
        out << event.name
            << "/" << event.localized_name
            << ";";
    }
    out << "]}";
}

std::string platform_form_object_signature(
    const oof::platform::object_model::PlatformFormObject& form_object
) {
    std::ostringstream out;
    out << "PlatformFormObject{form=";
    append_platform_object_signature(out, form_object.form);
    out << ";items=[";
    for (const auto& object : form_object.items.objects()) {
        append_platform_object_signature(out, object);
        out << ";";
    }
    out << "];attributes=[";
    for (const auto& object : form_object.attributes.objects()) {
        append_platform_object_signature(out, object);
        out << ";";
    }
    out << "];commands=[";
    for (const auto& object : form_object.commands.objects()) {
        append_platform_object_signature(out, object);
        out << ";";
    }
    out << "];events=[";
    for (const auto& object : form_object.events.objects()) {
        append_platform_object_signature(out, object);
        out << ";";
    }
    out << "];edges=[";
    for (const auto& edge : form_object.edges) {
        out << edge.kind
            << ":" << edge.from_object_id
            << "->" << edge.to_object_id
            << "/" << edge.role
            << "/" << edge.name
            << "/" << edge.slot_binding
            << ";";
    }
    out << "]}";
    return out.str();
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
    if (property.source.find("managed-application/logform") == std::string::npos &&
        property.source.find("cf_form_controls_info8") == std::string::npos) {
        return false;
    }
    if (property.name.empty() || property.name.find('.') != std::string::npos) {
        return false;
    }
    if (property.name == "TableColumnsXml") {
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

struct PublicXmlPackageFileSink {
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
    PublicXmlPackageFileSink* package_sink = nullptr
) {
    for (const auto& property : object.properties) {
        if (!is_public_schema_property_xml(property) || !property_is_explicit_for_xml(property)) {
            continue;
        }
        if (package_sink != nullptr &&
            property.name == "Picture" &&
            property.value_object_storage == "inline-base64") {
            const auto picture_bytes = decode_picture_payload(property.value);
            const std::string relative_path =
                "Items/" + package_item_name(object) + "/Picture." + picture_extension_for_bytes(picture_bytes);
            write_file_bytes(package_sink->package_root / relative_path, picture_bytes);
            ++package_sink->picture_count;
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
    for (const auto& edge : form_object.edges) {
        if (edge.kind != "owns-event" || edge.from_object_id != parent_object_id) {
            continue;
        }
        if (const auto* event = form_object.events.find(edge.to_object_id)) {
            events.push_back(event);
        }
    }
    if (!events.empty()) {
        return events;
    }
    for (const auto& event : form_object.events.objects()) {
        if (event.parent_object_id == parent_object_id) {
            events.push_back(&event);
        }
    }
    return events;
}

std::vector<std::size_t> child_indices_for_parent(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view parent_object_id
) {
    std::vector<std::size_t> children;
    for (const auto& edge : form_object.edges) {
        if (edge.kind != "contains" || edge.from_object_id != parent_object_id) {
            continue;
        }
        const auto index = form_object.items.index_of(edge.to_object_id);
        if (index >= 0) {
            children.push_back(static_cast<std::size_t>(index));
        }
    }
    if (!children.empty()) {
        return children;
    }
    if (parent_object_id == "0" || parent_object_id.empty()) {
        for (const auto index : form_object.form.children) {
            if (index < form_object.items.count()) {
                children.push_back(index);
            }
        }
        return children;
    }
    if (const auto* parent = form_object.find_object_by_id(parent_object_id)) {
        for (const auto index : parent->children) {
            if (index < form_object.items.count()) {
                children.push_back(index);
            }
        }
    }
    return children;
}

std::vector<const oof::platform::object_model::PlatformObject*> child_objects_for_parent(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view parent_object_id
) {
    std::vector<const oof::platform::object_model::PlatformObject*> children;
    for (const auto index : child_indices_for_parent(form_object, parent_object_id)) {
        children.push_back(&form_object.items.get(index));
    }
    return children;
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
    const std::string title = object_property_value(event, "Title");
    if (!title.empty()) {
        out += " title=\"";
        out += xml_escape(title);
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
        append_type_domain_pattern_xml(out, type, indent + 4);
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

void append_table_columns_xml(
    std::string& out,
    const oof::platform::object_model::PlatformObject& object,
    int indent
) {
    const auto* columns = object.property("TableColumnsXml");
    if (columns == nullptr || columns->value.empty()) {
        return;
    }
    for (const auto& columns_xml : find_xml_elements(columns->value, "Columns")) {
        append_indent(out, indent);
        out += "<Columns>\n";
        for (const auto& column_xml : find_xml_elements(columns_xml.body, "Column")) {
            append_indent(out, indent + 2);
            out += "<Column title=\"";
            out += xml_escape(xml_attr_value(column_xml.attrs, "title"));
            out += "\">\n";
            if (const auto type_xml = first_xml_element(column_xml.body, "Type");
                !type_xml.body.empty() || type_xml.self_closing) {
                append_indent(out, indent + 4);
                out += "<Type>";
                out += type_xml.body;
                out += "</Type>\n";
            }
            append_indent(out, indent + 2);
            out += "</Column>\n";
        }
        append_indent(out, indent);
        out += "</Columns>\n";
        return;
    }
}

void append_control_xml(
    std::string& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::size_t object_index,
    int indent,
    PublicXmlPackageFileSink* package_sink = nullptr
) {
    const auto& object = form_object.items.get(object_index);
    const std::string tag = public_xml_tag_for_platform_type(object.platform_type, object.identity.class_guid);
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
    const auto child_indices = child_indices_for_parent(form_object, object.object_id);
    if (child_indices.empty() &&
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
    append_schema_properties_xml(out, object, indent + 2, package_sink);
    append_table_columns_xml(out, object, indent + 2);
    append_position_xml(out, form_object, object, indent + 2);
    if (!object_events.empty()) {
        append_events_xml(out, object_events, indent + 2);
    }
    if (!child_indices.empty()) {
        append_indent(out, indent + 2);
        out += "<ChildItems>\n";
        for (const std::size_t child_index : child_indices) {
            append_control_xml(out, form_object, child_index, indent + 4, package_sink);
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
    PublicXmlPackageFileSink* package_sink = nullptr
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
    for (const std::size_t child_index : child_indices_for_parent(form_object, "0")) {
        append_control_xml(out, form_object, child_index, 4, package_sink);
    }
    out += "  </ChildItems>\n";
    append_attributes_xml(out, form_object, 2);
    append_commands_xml(out, form_object, 2);
    out += "</Form>\n";
    return out;
}

std::string form_object_to_public_xml(
    const oof::ordinary::object::OrdinaryForm& form,
    PublicXmlPackageFileSink* package_sink = nullptr
) {
    return form_object_to_public_xml(form.platform_object(), package_sink);
}

std::string platform_xsd_element_name(
    const oof::platform::object_model::PlatformObject& object,
    const oof::platform::object_schema::PlatformObjectSchema& schema
) {
    if (object.platform_type == "FormAttribute") {
        return "property";
    }
    if (object.platform_type == "FormCommand") {
        return "command";
    }
    if (object.platform_type == "FormEvent") {
        return "event";
    }
    return schema.stream_element.empty() ? schema.type_name : schema.stream_element;
}

bool platform_xsd_member_is_attribute(
    const oof::platform::object_schema::PlatformObjectSchemaMember& member
) {
    return member.source.find('@') != std::string::npos ||
        member.slot_binding.find('@') != std::string::npos;
}

std::string platform_xsd_property_text(
    const oof::platform::object_model::PlatformObjectProperty& property
) {
    if (!property.value_object_schema_value.empty()) {
        return property.value_object_schema_value;
    }
    if (!property.value_object_literal.empty()) {
        return property.value_object_literal;
    }
    return property.value;
}

std::string platform_xsd_member_text(
    const oof::platform::object_model::PlatformObject& object,
    const oof::platform::object_schema::PlatformObjectSchemaMember& member,
    const oof::platform::object_model::PlatformObjectProperty& property
) {
    if (member.stream_name == "id") {
        return object.identity.public_id.empty() ? object.object_id : object.identity.public_id;
    }
    if (member.stream_name == "name") {
        return object.name;
    }
    return platform_xsd_property_text(property);
}

struct PlatformXdtoProperty {
    std::string name;
    std::string stream_name;
    std::string value;
    bool attribute = false;
};

struct PlatformXdtoObject {
    std::string element_name;
    std::string xsd_namespace;
    std::string platform_type;
    std::string object_id;
    std::string name;
    std::string schema_source;
    std::vector<PlatformXdtoProperty> properties;
    std::vector<PlatformXdtoObject> children;

    const PlatformXdtoProperty* property(std::string_view property_name) const {
        for (const auto& property : properties) {
            if (property.name == property_name || property.stream_name == property_name) {
                return &property;
            }
        }
        return nullptr;
    }

    std::string get_prop_val(std::string_view property_name) const {
        const auto* found = property(property_name);
        if (found == nullptr) {
            throw std::runtime_error("XDTO object property is not found: " + std::string(property_name));
        }
        return found->value;
    }

    void set_prop_val(std::string_view property_name, std::string value) {
        for (auto& property : properties) {
            if (property.name == property_name || property.stream_name == property_name) {
                property.value = std::move(value);
                return;
            }
        }
        throw std::runtime_error("XDTO object property is not found: " + std::string(property_name));
    }
};

void add_xdto_property(
    PlatformXdtoObject& object,
    std::string name,
    std::string stream_name,
    std::string value,
    bool attribute
) {
    object.properties.push_back(PlatformXdtoProperty{
        std::move(name),
        std::move(stream_name),
        std::move(value),
        attribute});
}

PlatformXdtoObject platform_object_to_xdto_object(
    const oof::platform::object_model::PlatformObject& object
) {
    const auto schema = oof::platform::object_schema::schema_for_platform_type(object.platform_type);
    if (!schema.has_value()) {
        throw std::runtime_error("platform XDTO schema is not found for object type: " + object.platform_type);
    }
    PlatformXdtoObject xdto;
    xdto.element_name = platform_xsd_element_name(object, *schema);
    xdto.xsd_namespace = schema->xsd_namespace;
    xdto.platform_type = object.platform_type;
    xdto.object_id = object.object_id;
    xdto.name = object.name;
    xdto.schema_source = schema->schema_source;
    for (const auto& member : schema->xsd_members) {
        if (member.stream_name.empty() || member.stream_name.find('|') != std::string::npos) {
            continue;
        }
        const auto* property = object.property(member.name);
        if (property == nullptr) {
            continue;
        }
        const std::string text = platform_xsd_member_text(object, member, *property);
        if (text.empty() && !platform_xsd_member_is_attribute(member)) {
            continue;
        }
        add_xdto_property(
            xdto,
            member.name,
            member.stream_name,
            text,
            platform_xsd_member_is_attribute(member));
    }
    return xdto;
}

void append_platform_xdto_xml_object(std::string& out, const PlatformXdtoObject& object, int indent) {
    append_indent(out, indent);
    out += "<";
    out += object.element_name;
    if (object.platform_type == "Form") {
        out += " xmlns=\"";
        out += xml_escape(object.xsd_namespace);
        out += "\"";
        out += " xmlns:lf=\"http://v8.1c.ru/8.2/managed-application/logform\"";
        out += " xmlns:chart=\"http://v8.1c.ru/8.2/data/chart\"";
    } else if (!object.xsd_namespace.empty()) {
        out += " xmlns=\"";
        out += xml_escape(object.xsd_namespace);
        out += "\"";
    }
    for (const auto& property : object.properties) {
        if (!property.attribute) {
            continue;
        }
        out += " ";
        out += property.stream_name;
        out += "=\"";
        out += xml_escape(property.value);
        out += "\"";
    }

    std::string body;
    for (const auto& property : object.properties) {
        if (property.attribute) {
            continue;
        }
        append_indent(body, indent + 2);
        body += "<";
        body += property.stream_name;
        body += ">";
        body += xml_escape(property.value);
        body += "</";
        body += property.stream_name;
        body += ">\n";
    }
    for (const auto& child : object.children) {
        append_platform_xdto_xml_object(body, child, indent + 2);
    }
    if (body.empty()) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    out += body;
    append_indent(out, indent);
    out += "</";
    out += object.element_name;
    out += ">\n";
}

PlatformXdtoObject platform_form_object_to_xdto_object(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    PlatformXdtoObject xdto = platform_object_to_xdto_object(object);
    if (object.platform_type == "Form") {
        for (const auto& attribute : form_object.attributes.objects()) {
            xdto.children.push_back(platform_form_object_to_xdto_object(form_object, attribute));
        }
        for (const auto& command : form_object.commands.objects()) {
            xdto.children.push_back(platform_form_object_to_xdto_object(form_object, command));
        }
        for (const auto* event : event_objects_for_parent(form_object, "0")) {
            xdto.children.push_back(platform_form_object_to_xdto_object(form_object, *event));
        }
    } else {
        for (const auto* event : event_objects_for_parent(form_object, object.object_id)) {
            xdto.children.push_back(platform_form_object_to_xdto_object(form_object, *event));
        }
    }
    for (const auto* child : child_objects_for_parent(form_object, object.object_id)) {
        xdto.children.push_back(platform_form_object_to_xdto_object(form_object, *child));
    }
    return xdto;
}

std::string form_object_to_platform_xsd_xml(
    const oof::platform::object_model::PlatformFormObject& form_object
) {
    std::string out;
    out += "<?xml version='1.0' encoding='utf-8'?>\n";
    const PlatformXdtoObject xdto = platform_form_object_to_xdto_object(form_object, form_object.form);
    append_platform_xdto_xml_object(out, xdto, 0);
    return out;
}

std::optional<oof::platform::object_schema::PlatformObjectSchema> schema_for_platform_xsd_element(
    std::string_view tag,
    std::string_view xsd_namespace
) {
    for (const auto& schema : oof::platform::object_schema::build_platform_object_schemas()) {
        if (!xsd_namespace.empty() && !schema.xsd_namespace.empty() && schema.xsd_namespace != xsd_namespace) {
            continue;
        }
        oof::platform::object_model::PlatformObject schema_object;
        schema_object.platform_type = schema.type_name;
        if (tag == platform_xsd_element_name(schema_object, schema)) {
            return schema;
        }
        if (tag == schema.stream_element || tag == schema.type_name) {
            return schema;
        }
    }
    return std::nullopt;
}

std::string platform_xsd_object_id_from_attrs(std::string_view attrs, std::string_view tag) {
    std::string id = xml_attr_value(attrs, "id");
    if (!id.empty()) {
        return id;
    }
    id = xml_attr_value(attrs, "objectId");
    if (!id.empty()) {
        return id;
    }
    id = xml_attr_value(attrs, "name");
    if (!id.empty()) {
        return id;
    }
    return std::string(tag);
}

void set_or_add_platform_xsd_member_property(
    oof::platform::object_model::PlatformObject& object,
    const oof::platform::object_schema::PlatformObjectSchemaMember& member,
    std::string value
) {
    if (auto* property = object.property(member.name)) {
        property->value = std::move(value);
        property->value_origin = "platform-xsd-xml";
        enrich_platform_value_object(*property);
        if (!property->value.empty() && property->value_object_schema_value.empty()) {
            property->value_object_literal.clear();
        }
        return;
    }
    auto property = make_platform_object_property(
        member.name,
        localized_property_name(member.name),
        std::move(value),
        member.value_type,
        member.source,
        member.default_value,
        member.write_policy,
        "platform-xsd-xml",
        member.slot_binding,
        member.slot_codec,
        member.writable,
        member.platform_member,
        member.platform_default);
    object.properties.push_back(std::move(property));
}

bool platform_xsd_element_is_member_of_parent(
    const oof::platform::object_schema::PlatformObjectSchema& parent_schema,
    std::string_view tag
) {
    for (const auto& member : parent_schema.xsd_members) {
        if (member.stream_name == tag) {
            return true;
        }
    }
    return false;
}

std::optional<std::size_t> platform_xsd_member_child_index(
    const oof::platform::object_schema::PlatformObjectSchema& parent_schema,
    std::string_view tag
) {
    for (std::size_t index = 0; index < parent_schema.xsd_members.size(); ++index) {
        const auto& member = parent_schema.xsd_members[index];
        if (platform_xsd_member_is_attribute(member)) {
            continue;
        }
        if (member.stream_name == tag) {
            return index;
        }
    }
    return std::nullopt;
}

void validate_platform_xdto_child_order(
    const oof::platform::object_schema::PlatformObjectSchema& schema,
    std::string_view body
) {
    const std::regex start_tag_pattern(R"(<([A-Za-z][A-Za-z0-9]*)\b([^>]*)>)");
    std::size_t cursor = 0;
    std::size_t last_member_index = 0;
    bool have_member = false;
    bool object_child_seen = false;
    while (cursor < body.size()) {
        const std::string remaining(body.substr(cursor));
        std::smatch match;
        if (!std::regex_search(remaining, match, start_tag_pattern)) {
            break;
        }
        const std::size_t start = cursor + static_cast<std::size_t>(match.position());
        const std::string tag = match[1].str();
        const std::size_t tag_end = body.find('>', start);
        if (tag_end == std::string::npos) {
            throw std::runtime_error("platform XDTO XML start tag is not closed: " + tag);
        }
        const std::string attrs = match[2].str();
        const std::size_t attr_end = attrs.find_last_not_of(" \t\r\n");
        const bool self_closing = attr_end != std::string::npos && attrs[attr_end] == '/';
        std::size_t element_end = tag_end + 1;
        if (!self_closing) {
            const std::size_t close_start = find_matching_xml_close(body, tag, tag_end + 1);
            if (close_start == std::string::npos) {
                throw std::runtime_error("platform XDTO XML element is not closed: " + tag);
            }
            element_end = close_start + std::string("</" + tag + ">").size();
        }

        const std::string xsd_namespace = xml_attr_value(attrs, "xmlns");
        const bool explicit_object_child =
            !xsd_namespace.empty() && schema_for_platform_xsd_element(tag, xsd_namespace).has_value();
        if (!explicit_object_child && platform_xsd_member_child_index(schema, tag).has_value()) {
            const auto member_index = platform_xsd_member_child_index(schema, tag).value();
            if (object_child_seen) {
                throw std::runtime_error("platform XDTO member appears after object child: " + tag);
            }
            if (have_member && member_index < last_member_index) {
                throw std::runtime_error("platform XDTO member order violates schema sequence: " + tag);
            }
            last_member_index = member_index;
            have_member = true;
        } else {
            if (!schema_for_platform_xsd_element(tag, xsd_namespace).has_value()) {
                throw std::runtime_error("platform XDTO child is neither schema member nor known object: " + tag);
            }
            object_child_seen = true;
        }
        cursor = element_end;
    }
}

void add_platform_xdto_member_values(
    oof::platform::object_model::PlatformObject& object,
    const oof::platform::object_schema::PlatformObjectSchema& schema,
    const PlatformXdtoObject& xdto
) {
    for (const auto& member : schema.xsd_members) {
        if (member.stream_name.empty() || member.stream_name.find('|') != std::string::npos) {
            continue;
        }
        if (const auto* property = xdto.property(member.stream_name)) {
            set_or_add_platform_xsd_member_property(object, member, property->value);
        }
    }
}

PlatformXdtoObject platform_xdto_object_from_xml_element(
    std::string_view tag,
    std::string_view attrs,
    std::string_view body
) {
    const std::string xsd_namespace = xml_attr_value(attrs, "xmlns");
    const auto schema = schema_for_platform_xsd_element(tag, xsd_namespace);
    if (!schema.has_value()) {
        throw std::runtime_error("platform XDTO schema is not found for element: " + std::string(tag));
    }
    PlatformXdtoObject xdto;
    xdto.element_name = std::string(tag);
    xdto.xsd_namespace = schema->xsd_namespace;
    xdto.platform_type = schema->type_name;
    xdto.object_id = platform_xsd_object_id_from_attrs(attrs, tag);
    xdto.name = xml_attr_value(attrs, "name");
    if (xdto.name.empty()) {
        xdto.name = xdto.object_id;
    }
    xdto.schema_source = schema->schema_source;
    validate_platform_xdto_child_order(*schema, body);
    for (const auto& member : schema->xsd_members) {
        if (member.stream_name.empty() || member.stream_name.find('|') != std::string::npos) {
            continue;
        }
        if (platform_xsd_member_is_attribute(member)) {
            const std::string value = xml_attr_value(attrs, member.stream_name);
            if (!value.empty() || member.stream_name == "id" || member.stream_name == "name") {
                add_xdto_property(xdto, member.name, member.stream_name, value, true);
            }
            continue;
        }
        if (member.stream_name == "event") {
            continue;
        }
        const auto child = first_xml_element(body, member.stream_name);
        if (!child.self_closing || !child.body.empty()) {
            add_xdto_property(xdto, member.name, member.stream_name, xml_unescape(child.body), false);
        }
    }

    const std::regex start_tag_pattern(R"(<([A-Za-z][A-Za-z0-9]*)\b([^>]*)>)");
    std::size_t cursor = 0;
    while (cursor < body.size()) {
        const std::string remaining(body.substr(cursor));
        std::smatch match;
        if (!std::regex_search(remaining, match, start_tag_pattern)) {
            break;
        }
        const std::size_t start = cursor + static_cast<std::size_t>(match.position());
        const std::string child_tag = match[1].str();
        const std::size_t tag_end = body.find('>', start);
        if (tag_end == std::string::npos) {
            break;
        }
        const std::string child_attrs = match[2].str();
        const std::size_t attr_end = child_attrs.find_last_not_of(" \t\r\n");
        const bool self_closing = attr_end != std::string::npos && child_attrs[attr_end] == '/';
        std::string child_body;
        std::size_t element_end = tag_end + 1;
        if (!self_closing) {
            const std::size_t close_start = find_matching_xml_close(body, child_tag, tag_end + 1);
            if (close_start == std::string::npos) {
                throw std::runtime_error("platform XDTO XML element is not closed: " + child_tag);
            }
            child_body = std::string(body.substr(tag_end + 1, close_start - tag_end - 1));
            element_end = close_start + std::string("</" + child_tag + ">").size();
        }
        const std::string child_namespace = xml_attr_value(child_attrs, "xmlns");
        const bool explicit_object_child =
            !child_namespace.empty() && schema_for_platform_xsd_element(child_tag, child_namespace).has_value();
        if (explicit_object_child || !platform_xsd_element_is_member_of_parent(*schema, child_tag)) {
            xdto.children.push_back(platform_xdto_object_from_xml_element(child_tag, child_attrs, child_body));
        }
        cursor = element_end;
    }
    return xdto;
}

PlatformXdtoObject platform_xdto_object_from_xml(const std::string& xml) {
    const auto root = first_xml_element(xml, "Form");
    if (root.self_closing && root.body.empty()) {
        throw std::runtime_error("platform XDTO XML must contain Form root element");
    }
    return platform_xdto_object_from_xml_element("Form", root.attrs, root.body);
}

void add_platform_xdto_child_object(
    oof::platform::object_model::PlatformFormObject& form_object,
    const PlatformXdtoObject& xdto,
    std::string_view parent_object_id
) {
    const auto schema = schema_for_platform_xsd_element(xdto.element_name, xdto.xsd_namespace);
    if (!schema.has_value()) {
        throw std::runtime_error("platform XDTO schema is not found for object element: " + xdto.element_name);
    }
    oof::platform::object_model::PlatformObject object;
    object.object_id = xdto.object_id;
    object.name = xdto.name;
    object.platform_type = xdto.platform_type;
    object.type_category = "core::kLogFormTypeInfoCategory";
    object.type_source = "platform XDTO IObject + PlatformObjectSchema";
    object.parent_object_id = std::string(parent_object_id);
    object.path = object.parent_object_id.empty()
        ? "$/items/" + object.object_id
        : "$/items/" + object.parent_object_id + "/" + object.object_id;
    add_platform_object_schema_surface(object, *schema);
    object.properties.push_back(make_described_property("ObjectID", object.object_id));
    object.properties.push_back(make_described_property("Name", object.name));
    object.properties.push_back(make_described_property("Type", object.platform_type));
    object.properties.push_back(make_described_property("Parent", object.parent_object_id));
    object.properties.push_back(make_described_property("Path", object.path));
    add_platform_xdto_member_values(object, *schema, xdto);
    object.identity = oof::platform::object_model::make_identity(
        object.object_id,
        object.object_id,
        "platform XDTO IObject id/name; schema stream element resolves object type",
        {},
        {},
        object.platform_type == "FormAttribute" || object.platform_type == "FormCommand" || object.platform_type == "FormEvent"
            ? ""
            : ordinary_form_listout_control_guid(object.platform_type),
        "platform XDTO IObject + ordinary control type registry",
        schema->stream_element,
        schema->schema_source);

    if (object.platform_type == "FormAttribute") {
        form_object.attributes.add(std::move(object));
    } else if (object.platform_type == "FormCommand") {
        form_object.commands.add(std::move(object));
    } else if (object.platform_type == "FormEvent") {
        form_object.events.add(std::move(object));
    } else {
        const std::size_t new_index = form_object.items.count();
        const std::string object_id = object.object_id;
        const std::string object_name = object.name;
        const std::string object_type = object.platform_type;
        form_object.items.add(std::move(object));
        if (parent_object_id.empty() || parent_object_id == "0") {
            form_object.form.children.push_back(new_index);
        } else if (auto* parent = form_object.find_object_by_id(parent_object_id)) {
            parent->children.push_back(new_index);
        }
        form_object.add_edge(oof::platform::object_model::make_edge(
            "contains",
            parent_object_id.empty() ? "0" : std::string(parent_object_id),
            object_id,
            "ChildItems",
            "platform XDTO IObject tree order",
            schema->schema_source,
            schema->stream_element,
            object_type,
            object_name));
    }
    for (const auto& child : xdto.children) {
        add_platform_xdto_child_object(form_object, child, xdto.object_id);
    }
}

oof::platform::object_model::PlatformFormObject platform_form_object_from_xdto_object(
    const PlatformXdtoObject& xdto
) {
    if (xdto.platform_type != "Form") {
        throw std::runtime_error("root platform XDTO object must be Form");
    }
    const auto schema = schema_for_platform_xsd_element(xdto.element_name, xdto.xsd_namespace);
    if (!schema.has_value()) {
        throw std::runtime_error("platform XDTO root Form schema is not found");
    }
    oof::platform::object_model::PlatformFormObject form_object;
    form_object.form.object_id = "0";
    form_object.form.name = "Form";
    form_object.form.platform_type = "Form";
    form_object.form.type_category = "core::kLogFormTypeInfoCategory";
    form_object.form.type_source = "platform XDTO IObject + PlatformObjectSchema";
    form_object.form.path = "$";
    form_object.form.identity = oof::platform::object_model::make_identity(
        "0",
        "0",
        "root Form object from platform XDTO IObject",
        {},
        {},
        "5c83cba4-7a20-4102-a5be-add0ee74f6a1",
        "RTLogFormClass platform mechanism",
        schema->stream_element,
        schema->schema_source);
    add_platform_object_schema_surface(form_object.form, *schema);
    form_object.form.properties.push_back(make_described_property("Type", "Form"));
    form_object.form.properties.push_back(make_described_property("RuntimeUUID", ""));
    add_platform_xdto_member_values(form_object.form, *schema, xdto);
    for (const auto& child : xdto.children) {
        add_platform_xdto_child_object(form_object, child, "0");
    }
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

oof::platform::object_model::PlatformFormObject platform_form_object_from_platform_xsd_xml(
    const std::string& xml
) {
    return platform_form_object_from_xdto_object(platform_xdto_object_from_xml(xml));
}

bool platform_xdto_property_is_schema_member(
    const oof::platform::object_schema::PlatformObjectSchema& schema,
    const PlatformXdtoProperty& property
) {
    for (const auto& member : schema.xsd_members) {
        if (member.name == property.name || member.stream_name == property.stream_name) {
            return true;
        }
    }
    return false;
}

void validate_platform_xdto_object_for_schema_order(
    const PlatformXdtoObject& xdto,
    std::size_t& schema_objects,
    std::size_t& schema_members
) {
    const auto schema = schema_for_platform_xsd_element(xdto.element_name, xdto.xsd_namespace);
    if (!schema.has_value()) {
        throw std::runtime_error("platform XDTO schema is not found for writer object: " + xdto.element_name);
    }
    ++schema_objects;
    std::set<std::string> seen_properties;
    for (const auto& property : xdto.properties) {
        if (!platform_xdto_property_is_schema_member(*schema, property)) {
            throw std::runtime_error(
                "platform XDTO writer property is not declared by schema: " +
                xdto.element_name + "." + property.stream_name);
        }
        if (!seen_properties.insert(property.stream_name).second) {
            throw std::runtime_error(
                "platform XDTO writer duplicate property: " +
                xdto.element_name + "." + property.stream_name);
        }
        ++schema_members;
    }
    for (const auto& child : xdto.children) {
        if (platform_xsd_element_is_member_of_parent(*schema, child.element_name)) {
            throw std::runtime_error(
                "platform XDTO writer object child collides with schema member: " +
                xdto.element_name + "/" + child.element_name);
        }
        if (!schema_for_platform_xsd_element(child.element_name, child.xsd_namespace).has_value()) {
            throw std::runtime_error("platform XDTO writer child object is not declared by platform schemas: " + child.element_name);
        }
        validate_platform_xdto_object_for_schema_order(child, schema_objects, schema_members);
    }
}

struct PlatformXdtoListStreamWriteResult {
    PlatformXdtoObject xdto;
    oof::platform::object_model::PlatformFormObject form_object;
    oof::platform::stream::ListValue payload;
    std::size_t schema_objects = 0;
    std::size_t schema_members = 0;
};

PlatformXdtoListStreamWriteResult platform_xdto_schema_order_list_stream_payload(
    PlatformXdtoObject xdto
) {
    PlatformXdtoListStreamWriteResult result;
    result.xdto = std::move(xdto);
    validate_platform_xdto_object_for_schema_order(result.xdto, result.schema_objects, result.schema_members);
    result.form_object = platform_form_object_from_xdto_object(result.xdto);
    result.payload = platform_form_listout_payload(
        result.form_object,
        object_property_value(result.form_object.form, "Title"));
    return result;
}

void write_runtime_form_xml(const std::string& input_path, const std::string& output_path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    const oof::ordinary::object::OrdinaryForm form(materialize_platform_form_object(envelope));
    const std::string xml = form_object_to_public_xml(form);
    write_file_bytes(output_path, std::vector<std::uint8_t>(xml.begin(), xml.end()));
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"bytes\":" << xml.size();
    std::cout << ",\"source\":\"RuntimeForm:OrdinaryForm\"";
    std::cout << ",\"controlCount\":" << form.items().count();
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << "}\n";
}

void write_runtime_form_platform_xsd_xml(const std::string& input_path, const std::string& output_path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    const auto form_object = materialize_platform_form_object(envelope);
    const std::string xml = form_object_to_platform_xsd_xml(form_object);
    write_file_bytes(output_path, std::vector<std::uint8_t>(xml.begin(), xml.end()));
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"runtime-form-dump-platform-xsd-xml\"";
    std::cout << ",\"source\":\"PlatformFormObject\"";
    std::cout << ",\"publicOrdinaryFormXsdUsed\":false";
    std::cout << ",\"xmlVocabulary\":\"platform-xsd-stream-elements\"";
    std::cout << ",\"objects\":{\"items\":" << form_object.items.count()
              << ",\"attributes\":" << form_object.attributes.count()
              << ",\"commands\":" << form_object.commands.count()
              << ",\"events\":" << form_object.events.count()
              << ",\"edges\":" << form_object.edges.size() << "}";
    std::cout << ",\"bytes\":" << xml.size();
    std::cout << "}\n";
}

void write_formbin_platform_xsd_xml(const std::string& input_path, const std::string& output_path) {
    RuntimeFormEnvelope envelope = read_formbin_runtime_envelope(input_path);
    const auto form_object = materialize_platform_form_object(envelope);
    const std::string xml = form_object_to_platform_xsd_xml(form_object);
    write_file_bytes(output_path, std::vector<std::uint8_t>(xml.begin(), xml.end()));
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"formbin-dump-platform-xsd-xml\"";
    std::cout << ",\"source\":\"Form.bin:PlatformFormObject\"";
    std::cout << ",\"publicOrdinaryFormXsdUsed\":false";
    std::cout << ",\"xmlVocabulary\":\"platform-xsd-stream-elements\"";
    std::cout << ",\"objects\":{\"items\":" << form_object.items.count()
              << ",\"attributes\":" << form_object.attributes.count()
              << ",\"commands\":" << form_object.commands.count()
              << ",\"events\":" << form_object.events.count()
              << ",\"edges\":" << form_object.edges.size() << "}";
    std::cout << ",\"bytes\":" << xml.size();
    std::cout << "}\n";
}

void print_platform_xsd_xml_object(const std::string& input_path) {
    const auto form_object = platform_form_object_from_platform_xsd_xml(read_file_text_lossy(input_path));
    print_platform_form_object_document(form_object, "platform-XSD-XML", "");
}

void write_platform_xsd_xml_roundtrip(const std::string& input_path, const std::string& output_path) {
    const auto form_object = platform_form_object_from_platform_xsd_xml(read_file_text_lossy(input_path));
    const std::string xml = form_object_to_platform_xsd_xml(form_object);
    write_file_bytes(output_path, std::vector<std::uint8_t>(xml.begin(), xml.end()));
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"platform-xsd-xml-roundtrip\"";
    std::cout << ",\"path\":\"platform-XSD-XML -> PlatformXdtoObject -> PlatformFormObject -> PlatformXdtoObject -> platform-XSD-XML\"";
    std::cout << ",\"publicOrdinaryFormXsdUsed\":false";
    std::cout << ",\"objects\":{\"items\":" << form_object.items.count()
              << ",\"attributes\":" << form_object.attributes.count()
              << ",\"commands\":" << form_object.commands.count()
              << ",\"events\":" << form_object.events.count()
              << ",\"edges\":" << form_object.edges.size() << "}";
    std::cout << ",\"bytes\":" << xml.size();
    std::cout << "}\n";
}

void write_platform_xsd_xml_runtime_form(const std::string& input_path, const std::string& output_path) {
    auto result = platform_xdto_schema_order_list_stream_payload(
        platform_xdto_object_from_xml(read_file_text_lossy(input_path)));
    const auto& form_object = result.form_object;
    RuntimeFormEnvelope envelope;
    envelope.marker = "#";
    envelope.runtime_uuid = "5c83cba4-7a20-4102-a5be-add0ee74f6a1";
    envelope.payload = result.payload;
    const std::string output = dump_runtime_form_envelope(envelope);
    write_file_bytes(output_path, std::vector<std::uint8_t>(output.begin(), output.end()));
    const auto redump_object = materialize_platform_form_object(envelope);
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"platform-xsd-xml-build-runtime\"";
    std::cout << ",\"path\":\"platform-XSD-XML -> PlatformXdtoObject -> XSD schema-order ListOutStream -> bracket\"";
    std::cout << ",\"schemaOrderWriter\":\"PlatformXdtoObject\"";
    std::cout << ",\"publicOrdinaryFormXsdUsed\":false";
    std::cout << ",\"schemaObjects\":" << result.schema_objects;
    std::cout << ",\"schemaMembers\":" << result.schema_members;
    std::cout << ",\"objects\":{\"items\":" << form_object.items.count()
              << ",\"attributes\":" << form_object.attributes.count()
              << ",\"commands\":" << form_object.commands.count()
              << ",\"events\":" << form_object.events.count()
              << ",\"edges\":" << form_object.edges.size() << "}";
    std::cout << ",\"redumpObjects\":{\"items\":" << redump_object.items.count()
              << ",\"attributes\":" << redump_object.attributes.count()
              << ",\"commands\":" << redump_object.commands.count()
              << ",\"events\":" << redump_object.events.count()
              << ",\"edges\":" << redump_object.edges.size() << "}";
    std::cout << ",\"bytes\":" << output.size();
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

std::string inline_picture_package_files_for_build(
    const std::string& xml,
    const std::filesystem::path& xml_path,
    std::size_t& picture_files_read
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
        ++picture_files_read;
        cursor = replace_end;
    }
    return out;
}

void write_formbin_package(const std::string& input_path, const std::string& output_path) {
    const std::vector<std::uint8_t> data = read_file_bytes(input_path);
    const auto container = oof::platform::formbin::parse_container(data);
    const auto& form_file = find_container_file(container, "form");
    const RuntimeFormEnvelope envelope = runtime_envelope_from_form_payload(form_file.payload);
    const oof::ordinary::object::OrdinaryForm form(materialize_platform_form_object(envelope));
    const std::filesystem::path xml_path(output_path);
    PublicXmlPackageFileSink package_sink{form_package_root_for_xml(xml_path)};
    const std::string xml = form_object_to_public_xml(form, &package_sink);
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
    std::cout << ",\"source\":\"Form.bin:OrdinaryForm\"";
    std::cout << ",\"controlCount\":" << form.items().count();
    std::cout << ",\"moduleWritten\":" << (module_written ? "true" : "false");
    std::cout << ",\"moduleBytes\":" << module_bytes;
    std::cout << ",\"picturePackageFiles\":" << package_sink.picture_count;
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
    const std::string text = oof::platform::stream::dump_listout(payload);
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

void replace_panel_page_title_atoms(LV& value, std::string_view title) {
    if (title.empty()) {
        return;
    }
    if (!value.is_list) {
        if (value.atom_kind == LV::AtomKind::string && value.atom == "Страница1") {
            value.atom = std::string(title);
        }
        return;
    }
    for (auto& item : value.items) {
        replace_panel_page_title_atoms(item, title);
    }
}

LV ordinary_form_listout_default_color_record();
LV ordinary_form_listout_empty_page_style_record();
LV ordinary_form_listout_root_panel_base_info_record();
LV ordinary_form_listout_event_table(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view parent_object_id
);

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

std::string ordinary_form_listout_control_guid(std::string_view platform_type) {
    if (const auto* binding = oof::ordinary::control_type::binding_for_writer_control_type(platform_type)) {
        return std::string(binding->guid);
    }
    if (const auto* binding = oof::ordinary::control_type::binding_for_public_xml_tag(platform_type)) {
        return std::string(binding->guid);
    }
    if (const auto* binding = oof::ordinary::control_type::unambiguous_binding_for_platform_type(platform_type)) {
        return std::string(binding->guid);
    }
    static const std::map<std::string_view, std::string_view> guids{
        {"Panel", "09ccdc77-ea1a-4a6d-ab1c-3435eada2433"},
        {"PanelPage", "09ccdc77-ea1a-4a6d-ab1c-3435eada2433"},
    };
    const auto it = guids.find(platform_type);
    if (it == guids.end()) {
        throw std::runtime_error("OrdinaryForm ListOut writer does not know ordinary control GUID for type: " +
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

std::string ordinary_form_listout_bool_prop(
    const oof::platform::object_model::PlatformObject& object,
    std::string_view name,
    std::string fallback
) {
    std::string value = object_prop_or_default(object, name, std::move(fallback));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "true") {
        return "1";
    }
    if (value == "false") {
        return "0";
    }
    return value;
}

bool ordinary_form_listout_top_command_bar(const oof::platform::object_model::PlatformObject& object) {
    return object.platform_type == "CommandBar" && object.parent_object_id.empty();
}

LV ordinary_form_listout_geometry(const oof::platform::object_model::PlatformObject& object) {
    const bool top_command_bar = ordinary_form_listout_top_command_bar(object);
    const bool panel = object.platform_type == "Panel";
    const bool table = object.platform_type == "Table";
    const bool radio_button = object.platform_type == "RadioButton";
    const bool list_box = object.platform_type == "ListBox";
    const bool choice_field = object.platform_type == "ChoiceField";
    const bool group_box = object.platform_type == "GroupBox";
    const bool splitter = object.platform_type == "Splitter";
    const bool spreadsheet = object.platform_type == "SpreadsheetDocumentField";
    const bool track_bar = object.platform_type == "TrackBar";
    const bool calendar_field = object.platform_type == "CalendarField";
    const bool text_document_field = object.platform_type == "TextDocumentField";
    const bool pivot_chart = object.platform_type == "PivotChart";
    const bool geographical_schema = object.platform_type == "GeographicalSchemaField";
    const bool progress_bar = object.platform_type == "ProgressBar";
    const bool graphical_schema = object.platform_type == "GraphicalSchemaField";
    const bool chart = object.platform_type == "Chart";
    const bool gantt_chart = object.platform_type == "GanttChart";
    const bool dendrogram = object.platform_type == "Dendrogram";
    bool dimension_bound = false;
    for (std::string_view name : {
             "DimensionBinding.height", "DimensionBinding.minHeight",
             "DimensionBinding.stretch", "DimensionBinding.width",
         }) {
        const auto* property = object.property(name);
        if (property != nullptr && !property->value.empty() && property->value.front() == '{') {
            dimension_bound = true;
            break;
        }
    }
    std::vector<LV> items;
    items.push_back(raw("8"));
    items.push_back(raw(object_prop_or_default(object, "Left", "0")));
    items.push_back(raw(object_prop_or_default(object, "Top", "0")));
    items.push_back(raw(object_prop_or_default(object, "Right", "0")));
    items.push_back(raw(object_prop_or_default(object, "Bottom", "0")));
    items.push_back(raw((top_command_bar || panel || table || list_box || splitter || spreadsheet || calendar_field || text_document_field || pivot_chart || geographical_schema || progress_bar || graphical_schema || chart || gantt_chart || dendrogram || dimension_bound) ? "1" : "0"));
    for (std::string_view name : {
             "Binding.top", "Binding.bottom", "Binding.left",
             "Binding.right", "Binding.verticalCenter", "Binding.horizontalCenter",
         }) {
        const auto* property = object.property(name);
        items.push_back(property == nullptr || property->value.empty()
                            ? raw("0")
                            : oof::platform::stream::parse(property->value));
    }
    items.push_back(raw((top_command_bar || (dimension_bound && !splitter)) ? "1" : "0"));
    for (std::string_view name : {
             "DimensionBinding.height", "DimensionBinding.minHeight",
             "DimensionBinding.stretch", "DimensionBinding.width",
         }) {
        const auto* property = object.property(name);
        items.push_back(property == nullptr || property->value.empty()
                            ? raw("0")
                            : oof::platform::stream::parse(property->value));
    }
    if (top_command_bar) {
        items.push_back(raw("0"));
        items.push_back(raw("0"));
        items.push_back(raw("0"));
        items.push_back(raw("0"));
        items.push_back(raw("1"));
        if (!object_prop_or_default(object, "Title", "").empty()) {
            items.push_back(raw(object.object_id.empty() ? "0" : object.object_id));
        }
        items.push_back(raw("1"));
        items.push_back(raw(object_prop_or_default(object, "Title", "").empty() ? "0" : "1"));
    } else {
        items.push_back(raw("0"));
        if (!table && !splitter && !calendar_field && !text_document_field && !pivot_chart && !geographical_schema && !graphical_schema && !chart && !gantt_chart && !dendrogram) {
            items.push_back(raw("0"));
        }
        if (dimension_bound) {
            if (splitter) {
                items.push_back(raw("0"));
                items.push_back(raw("11"));
                items.push_back(raw("0"));
                items.push_back(raw("1"));
                items.push_back(raw("0"));
                items.push_back(raw("0"));
            } else {
                items.push_back(raw("0"));
                items.push_back(raw(group_box ? "8" : (choice_field ? "7" : (radio_button ? "5" : (track_bar ? "13" : (progress_bar ? "18" : "0"))))));
                items.push_back(raw(choice_field ? "1" : "0"));
                items.push_back(raw(choice_field ? "2" : "1"));
                items.push_back(raw("0"));
                items.push_back(raw("0"));
            }
        }
        if (panel) {
            items.push_back(raw("2"));
            items.push_back(raw("2"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (table) {
            items.push_back(raw("2"));
            items.push_back(raw("1"));
            items.push_back(raw("2"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (list_box) {
            items.push_back(raw("6"));
            items.push_back(raw("1"));
            items.push_back(raw("2"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (spreadsheet) {
            items.push_back(raw("12"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (calendar_field) {
            items.push_back(raw("14"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (text_document_field) {
            items.push_back(raw("15"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (pivot_chart) {
            items.push_back(raw("16"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (geographical_schema) {
            items.push_back(raw("17"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (graphical_schema) {
            items.push_back(raw("19"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (chart) {
            items.push_back(raw("21"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (gantt_chart) {
            items.push_back(raw("22"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        } else if (dendrogram) {
            items.push_back(raw("23"));
            items.push_back(raw("0"));
            items.push_back(raw("1"));
            items.push_back(raw("0"));
            items.push_back(raw("0"));
        }
    }
    return list(std::move(items));
}

LV ordinary_form_listout_metadata(const oof::platform::object_model::PlatformObject& object) {
    return list({
        raw("14"),
        str_atom(object.name.empty() ? object.object_id : object.name),
        raw(ordinary_form_listout_top_command_bar(object) ? "0" : "4294967295"),
        raw("0"),
        raw("0"),
        raw(object.platform_type == "RadioButton" ? "1" : "0"),
    });
}

LV ordinary_form_listout_font_placeholder() {
    return list({
        raw("3"),
        raw("0"),
        list({raw("0")}),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("48312c09-257f-4b29-b280-284dd89efc1e"),
    });
}

LV ordinary_form_listout_command_bar_font_placeholder() {
    return list({
        raw("3"),
        raw("0"),
        list({raw("0")}),
        raw("4"),
        raw("1"),
        raw("0"),
        raw("00000000-0000-0000-0000-000000000000"),
    });
}

LV ordinary_form_listout_empty_picture_value() {
    return list({
        raw("4"),
        raw("0"),
        list({raw("0")}),
        str_atom(""),
        raw("-1"),
        raw("-1"),
        raw("1"),
        raw("0"),
        str_atom(""),
    });
}

LV ordinary_form_listout_color_value(std::string value) {
    return list({
        raw("4"),
        raw("3"),
        list({raw(std::move(value))}),
        raw("3"),
    });
}

LV ordinary_form_listout_auto_color_value() {
    return list({
        raw("4"),
        raw("4"),
        list({raw("0")}),
        raw("4"),
    });
}

LV ordinary_form_listout_extended_base_info(const oof::platform::object_model::PlatformObject& object) {
    auto base = ordinary_form_listout_root_panel_base_info_record();
    base.items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
    base.items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "1"));
    base.items[6] = ordinary_form_listout_default_color_record();
    base.items[17] = raw("1");
    return base;
}

LV ordinary_form_listout_type_domain_pattern_record(const oof::platform::object_model::PlatformObject& object) {
    const std::string type = object_prop_or_default(object, "Type", "");
    if (!type.empty() && type.front() == '{') {
        return oof::platform::stream::parse(type);
    }
    return list({str_atom("Pattern"), list({str_atom("S")})});
}

LV ordinary_form_listout_attribute_type_domain_pattern_record(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    for (const auto& attribute : form_object.attributes.objects()) {
        if (object_property_value(attribute, "Name") == object.name) {
            const std::string type = object_property_value(attribute, "Type");
            if (!type.empty() && type.front() == '{') {
                return oof::platform::stream::parse(type);
            }
        }
    }
    return ordinary_form_listout_type_domain_pattern_record(object);
}

LV ordinary_form_listout_button_picture_record(const oof::platform::object_model::PlatformObject& object) {
    const std::string picture = object_prop_or_default(object, "Picture", "");
    if (picture.empty() || picture == "V8Picture()") {
        return ordinary_form_listout_empty_page_style_record();
    }
    return list({
        raw("4"), raw("3"), list({raw("0")}), str_atom(""), raw("-1"), raw("-1"),
        raw("0"), list({list({raw(picture)})}), raw("0"), str_atom(""),
    });
}

LV ordinary_form_listout_button_base_info(const oof::platform::object_model::PlatformObject& object) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    return list({
        list({
            raw("19"),
            raw(ordinary_form_listout_bool_prop(object, "Visible", "1")),
            ordinary_form_listout_auto_color_value(),
            ordinary_form_listout_auto_color_value(),
            list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
            raw(ordinary_form_listout_bool_prop(object, "Enabled", "1")),
            ordinary_form_listout_color_value("-22"),
            ordinary_form_listout_auto_color_value(),
            ordinary_form_listout_auto_color_value(),
            ordinary_form_listout_color_value("-7"),
            ordinary_form_listout_color_value("-21"),
            ordinary_form_listout_font_placeholder(),
            list({raw("1"), raw("0")}),
            raw("0"),
            raw("0"),
            raw("100"),
            raw("2"),
            raw("1"),
            raw(ordinary_form_listout_bool_prop(object, "Enabled", "1")),
            raw("2"),
            ordinary_form_listout_auto_color_value(),
        }),
        raw("14"),
        localized_text_record(title),
        raw("1"),
        raw("1"),
        raw("0"),
        raw("0"),
        raw("0"),
        ordinary_form_listout_button_picture_record(object),
        list({raw("0"), raw("0"), raw("0")}),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("2"),
    });
}

LV ordinary_form_listout_label_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    const std::string horizontal_align = object_prop_or_default(object, "HorizontalAlign", title.size() > 0 && title.back() == ':' ? "0" : "4");
    const std::string vertical_align = object_prop_or_default(object, "VerticalAlign", "1");
    const std::string picture_size = object_prop_or_default(object, "PictureSize", "1");
    const std::string picture_position = object_prop_or_default(object, "PicturePosition", title.size() > 0 && title.back() == ':' ? "0" : horizontal_align);
    const std::string text_position = object_prop_or_default(object, "TextPosition", picture_position);
    const LV picture_style = list({
        raw("10"),
        raw(picture_position),
        ordinary_form_listout_button_picture_record(object),
        ordinary_form_listout_empty_page_style_record(),
        ordinary_form_listout_empty_page_style_record(),
        raw("100"), raw("2"), raw("0"), raw("0"), raw("1"), raw("2"),
    });
    return list({
        raw("3"),
        list({
            ordinary_form_listout_extended_base_info(object),
            raw("11"),
            localized_text_record(title),
            raw(horizontal_align),
            raw(vertical_align),
            raw(ordinary_form_listout_bool_prop(object, "Hyperlink", event_objects_for_parent(form_object, object.object_id).empty() ? "0" : "1")),
            raw("0"),
            raw("0"),
            list({raw("0"), raw("0"), raw("0")}),
            raw("0"),
            list({raw("1"), raw("0")}),
            raw(picture_size),
            picture_style,
            raw(text_position),
            raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
        }),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_image_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    const std::string display_mode = object_prop_or_default(object, "DisplayMode", "0");
    const std::string display_state = object_prop_or_default(object, "DisplayState", "0");
    const LV picture_style = list({
        raw("10"),
        raw("0"),
        ordinary_form_listout_button_picture_record(object),
        ordinary_form_listout_empty_page_style_record(),
        ordinary_form_listout_empty_page_style_record(),
        raw("100"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
    });
    return list({
        raw("1"),
        list({
            ordinary_form_listout_extended_base_info(object),
            raw("20"),
            raw(display_mode),
            raw(display_state),
            picture_style,
            list({raw("0"), raw("0"), raw("0")}),
            raw("1"), raw("1"), raw("0"), raw("0"),
            list({raw("1"), raw("0")}),
            raw("0"), raw("1"), raw("0"), raw("1"),
        }),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_checkbox_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    return list({
        raw("1"),
        list({
            list({
                ordinary_form_listout_extended_base_info(object),
                raw("7"),
                localized_text_record(title),
                raw("1"), raw("0"), raw("1"), raw("0"), raw("100"), raw("1"),
            }),
            raw("4"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
        }),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_radiobutton_inner_info(const oof::platform::object_model::PlatformObject& object) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    return list({
        list({
            ordinary_form_listout_button_base_info(object).items[0],
            raw("7"),
            localized_text_record(title),
            raw("1"),
            raw("0"),
            raw("1"),
            raw("0"),
            raw("100"),
            raw("1"),
        }),
        raw("4"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
    });
}

LV ordinary_form_listout_radiobutton_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    return list({
        raw("4"),
        ordinary_form_listout_attribute_type_domain_pattern_record(form_object, object),
        ordinary_form_listout_radiobutton_inner_info(object),
        raw("0"),
        list({str_atom("N"), raw("0")}),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_input_field_info_record(const oof::platform::object_model::PlatformObject& object) {
    auto record = list({
        ordinary_form_listout_extended_base_info(object),
        raw("31"),
        raw("0"),
        raw(object_prop_or_default(object, "EditMode", "0")),
        raw(object_prop_or_default(object, "ChoiceMode", "1")),
        raw(ordinary_form_listout_bool_prop(object, "PasswordMode", "0")),
        raw("0"),
        raw(ordinary_form_listout_bool_prop(object, "ExtendedEdit", "0")),
        raw("0"), raw("0"), raw("0"), raw("1"),
        raw(ordinary_form_listout_bool_prop(object, "ReadOnly", "0")),
        raw("0"), raw(object_prop_or_default(object, "MaxLength", "0")),
        raw("0"), raw("0"), raw("4"), raw("0"),
        list({str_atom("U")}),
        list({str_atom("U")}),
        str_atom(object_prop_or_default(object, "Mask", "")),
        raw("0"), raw("1"), raw("0"), raw("0"),
        raw(ordinary_form_listout_bool_prop(object, "MultiLine", "0")),
        raw("0"),
        ordinary_form_listout_empty_page_style_record(),
        ordinary_form_listout_empty_page_style_record(),
        raw("0"), raw("0"), raw("0"),
        list({raw("0"), raw("0"), raw("0")}),
        list({raw("1"), raw("0")}),
        raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
        raw("16777215"), raw("2"), raw("0"), raw("0"),
    });
    const std::string max_length = object_prop_or_default(object, "MaxLength", "0");
    if (max_length != "0") {
        record.items[13] = raw("1");
        record.items[14] = raw(max_length);
    }
    return record;
}

LV ordinary_form_listout_input_field_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    return list({
        raw("9"),
        ordinary_form_listout_type_domain_pattern_record(object),
        list({ordinary_form_listout_input_field_info_record(object)}),
        list({raw("0")}),
        ordinary_form_listout_event_table(form_object, object.object_id),
        raw("0"),
        raw("1"),
        raw("0"),
        list({raw("1"), raw("0")}),
        raw("0"),
    });
}

LV ordinary_form_listout_choice_field_info_record(const oof::platform::object_model::PlatformObject& object) {
    auto base = ordinary_form_listout_root_panel_base_info_record();
    base.items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
    base.items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "0"));
    base.items[6] = ordinary_form_listout_color_value("-22");
    base.items[9] = ordinary_form_listout_color_value("-7");
    base.items[10] = ordinary_form_listout_color_value("-21");
    base.items[11] = list({
        raw("3"),
        raw("1"),
        list({raw("-18")}),
        raw("0"),
        raw("0"),
        raw("0"),
    });
    return list({
        base,
        raw("31"),
        raw("0"),
        raw("0"),
        raw("1"),
        raw("0"),
        raw("1"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("1"),
        raw("0"),
        raw("0"),
        raw("255"),
        raw("0"),
        raw("0"),
        raw("4"),
        raw("0"),
        list({str_atom("U")}),
        list({str_atom("U")}),
        str_atom(""),
        raw("0"),
        raw("1"),
        raw("1"),
        raw("0"),
        raw("0"),
        raw("0"),
        ordinary_form_listout_empty_picture_value(),
        ordinary_form_listout_empty_picture_value(),
        raw("0"),
        raw("0"),
        raw("0"),
        list({raw("0"), raw("0"), raw("0")}),
        list({raw("1"), raw("0")}),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("16777215"),
        raw("2"),
        raw("0"),
        raw("0"),
    });
}

LV ordinary_form_listout_choice_field_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    return list({
        raw("2"),
        ordinary_form_listout_choice_field_info_record(object),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_command_bar_base_info(const oof::platform::object_model::PlatformObject& object) {
    return list({
        raw("19"),
        raw(ordinary_form_listout_bool_prop(object, "Visible", "1")),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
        raw(ordinary_form_listout_bool_prop(object, "Enabled", "0")),
        ordinary_form_listout_color_value("-22"),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_color_value("-21"),
        ordinary_form_listout_command_bar_font_placeholder(),
        list({raw("1"), raw("0")}),
        raw("0"),
        raw("0"),
        raw("100"),
        raw("2"),
        raw("1"),
        raw("1"),
        raw("2"),
        ordinary_form_listout_auto_color_value(),
    });
}

LV ordinary_form_listout_command_bar_title_action_record(const oof::platform::object_model::PlatformObject& object) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    const std::string handler = object.name.empty() ? "CommandBarAction" : object.name;
    return list({
        raw("8"),
        raw("d3fc7006-edf1-4b73-9867-1648ab142485"),
        raw("1"),
        raw("e1692cc2-605b-4535-84dd-28440238746c"),
        list({
            raw("3"),
            str_atom(handler),
            list({
                raw("1"),
                str_atom(handler),
                localized_text_record(title),
                localized_text_record(title),
                localized_text_record(title),
                ordinary_form_listout_empty_picture_value(),
                list({raw("0"), raw("0"), raw("0")}),
            }),
        }),
        raw("0"),
        raw("1"),
        raw("1"),
    });
}

LV ordinary_form_listout_command_bar_actions_payload(const oof::platform::object_model::PlatformObject& object) {
    const std::string title = object_prop_or_default(object, "Title", "");
    if (title.empty()) {
        return list({
            raw("5"),
            raw("6013c551-1c48-4ef4-a466-6fbe824675ca"),
            raw("12"),
            raw("1"),
            raw("0"),
            raw("1"),
            list({
                raw("5"),
                raw("b78f2e80-ec68-11d4-9dcf-0050bae2bc79"),
                raw("4"),
                raw("0"),
                raw("0"),
                list({raw("0"), raw("0"), list({raw("0")})}),
            }),
        });
    }
    return list({
        raw("5"),
        raw("87a7828f-3ea2-4ed5-9afd-292b4728c926"),
        raw("3"),
        raw("1"),
        raw("1"),
        ordinary_form_listout_command_bar_title_action_record(object),
        raw("1"),
        list({
            raw("5"),
            raw("b78f2e80-ec68-11d4-9dcf-0050bae2bc79"),
            raw("4"),
            raw("0"),
            raw("0"),
            list({raw("0"), raw("0"), list({raw("0")})}),
        }),
    });
}

LV ordinary_form_listout_command_bar_payload(const oof::platform::object_model::PlatformObject& object) {
    const bool has_title = !object_prop_or_default(object, "Title", "").empty();
    return list({
        raw("2"),
        list({
            ordinary_form_listout_command_bar_base_info(object),
            raw("9"),
            raw("2"),
            raw(has_title ? "0" : "1"),
            raw("0"),
            raw("1"),
            raw("1"),
            ordinary_form_listout_command_bar_actions_payload(object),
            raw("b78f2e80-ec68-11d4-9dcf-0050bae2bc79"),
            raw("4"),
            raw(has_title ? "2601b204-97f7-4060-b50c-9b68f10d8acd" : "9d0a2e40-b978-11d4-84b6-008048da06df"),
            raw(has_title ? "1" : "0"),
            raw("0"),
            raw("0"),
            ordinary_form_listout_button_picture_record(object),
        }),
    });
}

LV ordinary_form_listout_table_base_info(const oof::platform::object_model::PlatformObject& object) {
    auto base = ordinary_form_listout_root_panel_base_info_record();
    base.items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
    base.items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "0"));
    base.items[6] = ordinary_form_listout_color_value("-22");
    base.items[9] = ordinary_form_listout_color_value("-7");
    base.items[10] = ordinary_form_listout_color_value("-21");
    base.items[11] = list({
        raw("3"),
        raw("1"),
        list({raw("-18")}),
        raw("0"),
        raw("0"),
        raw("0"),
    });
    return base;
}

std::string ordinary_form_table_column_type_state_blob(std::string_view type_pattern) {
    const std::string compact(type_pattern);
    if (compact == "{\"Pattern\",{\"N\",10,0,0}}") {
        return "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVFLDgIhDDUuTeYIbrouCQVG6CWMGw8w6mxdGHeGk7nwSF5BSmf8TiIQmtfyXl/Dcj6r6369XRibxQU23fncn45QwRqQLJadm0WWjB5iJEEBgwSbMeQpnNAXMhWNkrL6wkswzmX0k5xfrIz4IgwSNGa8ujGUpIu4FUhoFZbu6MRFub8ayFSelPSxaWDWENQ7bCE/I0BVHN9XXTvolpohOVIHyH+qT4UJ72+OVjFGR20Z4u0/6uDcxVXwB2eIezah7Z1Jib1xye72TNx2HNXD9xRiYJQbFGtjUkNqIz8A";
    }
    if (compact == "{\"Pattern\",{\"D\",\"D\"}}") {
        return "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVE7DsIwDEWMSD0Aq2dHipOUxDsHYOEABboyIDaUkzFwJK5AHLeASiWatNbz571ndb1c1Od5f9wYm9UNdt312l/OUMEWsLy5WWWBeomRBAUMEmzGkOdwQo+2tJItKasdXoJxLqOfnfnFOhE/AwMFjRmvbgwlUSmnZgmtwqKOTlyU70RAtvKkQ9o7iXKCWoc95HcEqJRjS6W1A22pGZIrdYD8p/pmmLH+ZWwTY3TUFlnpGn5H3Zu7uAn+5Axxzya0vTMpsTcu2cORiduOo3qYbiEGRrqBsQqTGlIb+QU=";
    }
    if (compact == "{\"Pattern\",{\"B\"}}") {
        return "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVE7DsIwDEWMSB25gGdHipOUxCsnYOEABboyIDaUkzFwJK5AHBfKpxJ1FNe/5/eU5XxWv/v1dmFsFhfYdOdzfzpCDdaQm0WWXz3ESBIFDOJsxpCn4oQebWklW1JWO7w441xGPznzG+tEHAcGCHpmvLIxlGRLsZoltBqW7eiERbm/FogqT9r1do9eLCh12EJ+eYCPpgprB9hSMyRH6gD5T/WFMEF9NFrFGB21RYN0Dc9RdXMXV8EfnCHu2YS2dyYl9sYlu9szcdtxVA7fKqzqqHADYl1MSkhp5Ac=";
    }
    if (compact == "{\"Pattern\",{\"#\",4772b3b4-f4a3-49c0-a1a5-8cb5961511a3}}") {
        return "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVE7bsMwDC06Bsgl1JUERJG2pVt06QFkQx07FNkCnaxDjtQrVDQVJ2kNVCRE8Pf4CNLz0/q+vy7nBMfD2b3m06l8frjVeXEg0xRmngXfJTNKWjxmygPGZR7SSANR5no8VK03pQSknoCo8RWk7vkRGHwrJd9C3ipYDYZQgXd7/vrWMd0aOgRdI2xskKJOabJGCby5bToEZdH+XwN0Kyar6v+jVRGj7t5c3axzW4u/buY7bMshqWreufpPdkPYoX4TGqd2JhraDlrVz7HuHTmHNASPfiwRhSViimnBPJcxlViYSzD8++srhGknZvts0+sP";
    }
    return "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVE7DsIwDEWMSIxcwLMjxUlK4lsgIQ5QoCsDYkM5GQNH4grEcfhXoonqPvv5+VldTCf1uV2uZ8b57Ayr/nQajgeoYA15PsvyqZcYSVDAIMFmDHkMJ/RoC5VsSVlleAnGuYx+tOcXa0d8NTQJemS8ujGUZEo5NUtoFZbp6MRFeX8NkK08KUtb23lHQa3DBvIzAnyQqqxtsqVmSK7UAfKf6lNhxPqboWWM0VFXdhBW+x11b+7jMvi9M8QDm9ANzqTE3rhktzsm7nqO6uF7C6t7VLmmWAeTGlIb+Q4=";
}

LV ordinary_form_listout_table_column_type_state_record(std::string_view type_pattern) {
    return list({
        list({raw(ordinary_form_table_column_type_state_blob(type_pattern))}),
        raw("0"),
    });
}

struct OrdinaryFormTableColumnSpec {
    std::string title;
    std::string type_pattern;
};

std::vector<OrdinaryFormTableColumnSpec> ordinary_form_table_column_specs(
    const oof::platform::object_model::PlatformObject& object
) {
    std::vector<OrdinaryFormTableColumnSpec> specs;
    if (const auto* columns_property = object.property("TableColumnsXml")) {
        for (const auto& columns_xml : find_xml_elements(columns_property->value, "Columns")) {
            for (const auto& column_xml : find_xml_elements(columns_xml.body, "Column")) {
                OrdinaryFormTableColumnSpec spec;
                spec.title = xml_attr_value(column_xml.attrs, "title");
                if (spec.title.empty()) {
                    spec.title = "Column" + std::to_string(specs.size() + 1);
                }
                if (const auto type_xml = first_xml_element(column_xml.body, "Type");
                    !type_xml.body.empty() || type_xml.self_closing) {
                    spec.type_pattern = type_domain_pattern_text_from_public_type_xml(type_xml);
                }
                if (spec.type_pattern.empty()) {
                    spec.type_pattern = "{\"Pattern\",{\"S\"}}";
                }
                specs.push_back(std::move(spec));
            }
        }
    }
    if (specs.empty()) {
        specs.push_back({
            object_prop_or_default(object, "Title", object.name.empty() ? "Column1" : object.name),
            "{\"Pattern\",{\"S\"}}",
        });
    }
    return specs;
}

LV ordinary_form_listout_table_column_record(const OrdinaryFormTableColumnSpec& spec, std::size_t ordinal) {
    const std::string& title = spec.title;
    return list({
        raw("737535a4-21e6-4971-8513-3e3173a9fedd"),
        list({
            raw("8"),
            list({
                raw("8"),
                list({
                    raw("23"),
                    localized_text_record(title),
                    list({raw("1"), raw("0")}),
                    list({raw("1"), raw("0")}),
                    raw("1e2"),
                    raw(std::to_string(ordinal)),
                    raw("-1"),
                    raw("-1"),
                    raw("-1"),
                    raw("12590592"),
                    ordinary_form_listout_empty_picture_value(),
                    ordinary_form_listout_empty_picture_value(),
                    ordinary_form_listout_empty_picture_value(),
                    raw("16"),
                    raw("16"),
                    raw("d2314b5d-8da4-4e0f-822b-45e7500eae09"),
                    ordinary_form_listout_auto_color_value(),
                    ordinary_form_listout_auto_color_value(),
                    ordinary_form_listout_auto_color_value(),
                    ordinary_form_listout_auto_color_value(),
                    ordinary_form_listout_auto_color_value(),
                    ordinary_form_listout_auto_color_value(),
                    list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
                    list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
                    list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
                    raw("1"),
                    raw("0"),
                    raw("0"),
                    raw("4"),
                    raw("0"),
                    str_atom(title),
                    list({}),
                    raw("15"),
                    raw("0"),
                    list({raw("1"), raw("0")}),
                    oof::platform::stream::parse(spec.type_pattern),
                    raw("0"),
                    raw("1"),
                    raw("381ed624-9217-4e63-85db-c4c3cb87daae"),
                    ordinary_form_listout_table_column_type_state_record(spec.type_pattern),
                    raw("0"),
                    raw("0"),
                    raw("0"),
                    raw("0"),
                    raw("0"),
                    raw("1e2"),
                    raw("0"),
                    raw("1"),
                    raw("0"),
                    raw("0"),
                    raw("2"),
                    raw("0"),
                }),
                list({raw("-1")}),
                list({raw("-1")}),
                list({raw("-1")}),
            }),
            str_atom(title),
            str_atom(""),
            str_atom(""),
            raw("0"),
        }),
    });
}

LV ordinary_form_listout_table_column_collection_record(const oof::platform::object_model::PlatformObject& object) {
    std::vector<LV> items;
    items.push_back(raw("5"));
    const auto specs = ordinary_form_table_column_specs(object);
    for (std::size_t index = 0; index < specs.size(); ++index) {
        items.push_back(ordinary_form_listout_table_column_record(specs[index], index));
    }
    return list(std::move(items));
}

LV ordinary_form_listout_table_view_record(const oof::platform::object_model::PlatformObject& object) {
    const std::string rows_count = object_prop_or_default(object, "RowsCount", "100");
    const auto column_specs = ordinary_form_table_column_specs(object);
    const std::string columns_count = object_prop_or_default(object, "ColumnsCount", std::to_string(column_specs.size()));
    const std::string auto_mark = object_prop_or_default(object, "AutoMarkIncomplete", "2");
    return list({
        raw("23"),
        raw("117644301"),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_color_value("-14"),
        ordinary_form_listout_color_value("-15"),
        ordinary_form_listout_color_value("-13"),
        raw("2"),
        raw("2"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("1"),
        raw("1"),
        list({raw("8"), raw("2"), raw("0"), list({raw("-20")}), raw("1"), raw("100")}),
        list({raw("8"), raw("2"), raw("0"), list({raw("-20")}), raw("1"), raw("100")}),
        raw("2"),
        raw("0"),
        raw("1"),
        ordinary_form_listout_table_column_collection_record(object),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw(rows_count),
        raw("1"),
        raw("2"),
        raw("1"),
        raw("1"),
        raw("0"),
        raw(columns_count),
        raw(auto_mark),
    });
}

LV ordinary_form_listout_table_payload(const oof::platform::object_model::PlatformFormObject& form_object,
                                       const oof::platform::object_model::PlatformObject& object) {
    return list({
        raw("5"),
        ordinary_form_listout_attribute_type_domain_pattern_record(form_object, object),
        list({
            ordinary_form_listout_table_base_info(object),
            ordinary_form_listout_table_view_record(object),
        }),
        list({
            raw("342cf854-134c-42bb-8af9-a2103d5d9723"),
            list({raw("5"), raw("0"), raw("0"), raw("1")}),
        }),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_listbox_base_info(const oof::platform::object_model::PlatformObject& object) {
    return ordinary_form_listout_table_base_info(object);
}

LV ordinary_form_listout_listbox_view_record() {
    return list({
        raw("23"),
        raw("100743712"),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_auto_color_value(),
        ordinary_form_listout_color_value("-14"),
        ordinary_form_listout_color_value("-15"),
        ordinary_form_listout_color_value("-13"),
        raw("2"),
        raw("2"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("1"),
        raw("0"),
        raw("1"),
        raw("1"),
        list({raw("8"), raw("2"), raw("0"), list({raw("-20")}), raw("1"), raw("100")}),
        list({raw("8"), raw("2"), raw("0"), list({raw("-20")}), raw("1"), raw("100")}),
        raw("2"),
        raw("0"),
        raw("1"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("100"),
        raw("1"),
        raw("2"),
        raw("2"),
        raw("2"),
        raw("0"),
        raw("0"),
        raw("2"),
    });
}

LV ordinary_form_listout_listbox_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    return list({
        raw("1"),
        list({
            ordinary_form_listout_listbox_base_info(object),
            ordinary_form_listout_listbox_view_record(),
            raw("6"),
            raw("0"),
            raw("0"),
            raw("1"),
            raw("0"),
        }),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_groupbox_payload(const oof::platform::object_model::PlatformObject& object) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    auto base = ordinary_form_listout_root_panel_base_info_record();
    base.items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
    base.items[4] = list({raw("8"), raw("3"), raw("4"), raw("700"), raw("1"), raw("100")});
    base.items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "0"));
    base.items[6] = ordinary_form_listout_color_value("-22");
    base.items[9] = ordinary_form_listout_color_value("-7");
    base.items[10] = ordinary_form_listout_color_value("-21");
    return list({
        raw("0"),
        list({
            base,
            raw("8"),
            localized_text_record(title),
            list({
                raw("3"),
                raw("0"),
                list({raw("0")}),
                raw("6"),
                raw("1"),
                raw("0"),
                raw("cf48d3ca-5bd4-45b9-bb8f-a0922a8335f2"),
            }),
            raw("0"),
        }),
    });
}

LV ordinary_form_listout_splitter_payload(const oof::platform::object_model::PlatformObject& object) {
    auto base = ordinary_form_listout_root_panel_base_info_record();
    base.items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
    base.items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "1"));
    base.items[6] = ordinary_form_listout_color_value("-22");
    base.items[9] = ordinary_form_listout_color_value("-7");
    base.items[10] = ordinary_form_listout_color_value("-21");
    base.items[11] = list({
        raw("3"),
        raw("0"),
        list({raw("-18")}),
        raw("0"),
        raw("0"),
        raw("0"),
        raw("48312c09-257f-4b29-b280-284dd89efc1e"),
    });
    return list({
        raw("0"),
        list({
            base,
            raw("2"),
            raw("2"),
            raw("0"),
        }),
    });
}

LV ordinary_form_listout_spreadsheet_payload(const oof::platform::object_model::PlatformObject& object) {
    const std::string left = object_prop_or_default(object, "Left", "0");
    const std::string top = object_prop_or_default(object, "Top", "0");
    const std::string right = object_prop_or_default(object, "Right", "0");
    const std::string bottom = object_prop_or_default(object, "Bottom", "0");
    const std::string title = object_prop_or_default(object, "Title", "ru");
    return list({
        raw("18"),
        raw(left),
        raw(top),
        raw(right),
        raw(bottom),
        raw("5"),
        raw("5"),
        raw("0"),
        raw("1"),
        ordinary_form_listout_color_value("-22"),
        list({
            raw("3"), raw("1"), list({raw("-18")}),
            raw("0"), raw("0"), raw("0"),
        }),
        list({
            raw("8"), raw("1"), raw("12"),
            list({str_atom("ru"), str_atom(title), raw("1"), raw("1"), str_atom("ru"), str_atom("Русский"), str_atom("Русский"), raw("1")}),
            list({raw("128"), raw("72")}),
            list({raw("0")}),
            raw("0"),
            list({raw("0"), raw("0")}),
            list({raw("0"), raw("0")}),
            list({raw("0"), raw("0")}),
            list({raw("0"), raw("0")}),
            list({raw("0"), raw("0")}),
            list({raw("0"), raw("0")}),
            raw("0"),
            raw("2"),
            raw("0"),
            list({raw("0"), raw("0"), raw("00000000-0000-0000-0000-000000000000"), raw("0")}),
            raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
            list({raw("0")}),
            list({raw("0")}),
            list({raw("0")}),
            list({raw("0")}),
            str_atom(""),
            list({
                list({
                    raw("0"), raw("6"),
                    raw("6"), list({str_atom("N"), raw("1000")}),
                    raw("7"), list({str_atom("N"), raw("1000")}),
                    raw("8"), list({str_atom("N"), raw("1000")}),
                    raw("9"), list({str_atom("N"), raw("1000")}),
                    raw("10"), list({str_atom("N"), raw("1000")}),
                    raw("11"), list({str_atom("N"), raw("1000")}),
                }),
            }),
            list({raw("0"), raw("-1"), raw("-1"), raw("-1"), raw("-1"), raw("00000000-0000-0000-0000-000000000000")}),
            raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
            raw("1"), raw("0"), raw("1"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("2"),
            ordinary_form_listout_color_value("-1"),
            ordinary_form_listout_color_value("-3"),
            raw("0"), raw("0"), raw("0"), str_atom(""), raw("0"),
            list({
                raw("3"), raw("0"), raw("0"), raw("100"), raw("1"), raw("1"), raw("0"), raw("1"), raw("1"),
                raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
                raw("0"), raw("0"), raw("0"), str_atom(""), raw("0"), raw("0"), raw("0"), raw("0"),
                raw("0"), raw("0"), raw("0"),
            }),
            list({raw("0")}),
            raw("0"), raw("0"), raw("0"), raw("1"), raw("0"), raw("0"), raw("0"),
        }),
        raw("0"),
        raw("1"),
        list({
            raw("3"), raw("0"), raw("0"), raw("100"), raw("0"), raw("0"), raw("0"), raw("1"), raw("1"),
            raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
            raw("0"), raw("0"), raw("0"), str_atom(title), raw("0"), raw("1"),
            list({raw("3"), raw("0"), raw("0"), raw("0"), raw("0"), raw("00000000-0000-0000-0000-000000000000")}),
            raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
        }),
        raw("1"), raw("1"),
        list({raw("0")}),
        raw("0"), raw("0"), raw("0"), raw("0"), raw("0"), raw("1"), raw("0"), raw("1"), raw("1"), raw("0"), raw("0"), raw("0"), raw("0"), raw("1"), raw("1"),
    });
}

LV ordinary_form_listout_trackbar_base_info(const oof::platform::object_model::PlatformObject& object) {
    auto base = ordinary_form_listout_root_panel_base_info_record();
    base.items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
    base.items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "1"));
    return base;
}

LV ordinary_form_listout_trackbar_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    return list({
        raw("1"),
        list({
            ordinary_form_listout_trackbar_base_info(object),
            raw("5"),
            raw(object_prop_or_default(object, "MinimumValue", "0")),
            raw(object_prop_or_default(object, "MaximumValue", "100")),
            raw(object_prop_or_default(object, "Step", "1")),
            raw(object_prop_or_default(object, "BigStep", "10")),
            raw(object_prop_or_default(object, "Orientation", "2")),
            raw(object_prop_or_default(object, "Marking", "2")),
            raw(object_prop_or_default(object, "MarkStep", "5")),
            raw(object_prop_or_default(object, "CurrentValue", "100")),
        }),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_progressbar_payload(const oof::platform::object_model::PlatformObject& object) {
    return list({
        raw("0"),
        list({
            ordinary_form_listout_table_base_info(object),
            raw(object_prop_or_default(object, "Orientation", "3")),
            raw(object_prop_or_default(object, "MinimumValue", "0")),
            raw(object_prop_or_default(object, "MaximumValue", "100")),
            raw(object_prop_or_default(object, "Step", "1")),
            raw(object_prop_or_default(object, "BigStep", "1")),
            raw(object_prop_or_default(object, "ShowPercent", "0")),
            raw(object_prop_or_default(object, "DisplayStyle", "2")),
        }),
    });
}

LV ordinary_form_listout_calendar_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    return list({
        raw("1"),
        list({
            ordinary_form_listout_table_base_info(object),
            raw("9"),
            ordinary_form_listout_color_value("-16"),
            ordinary_form_listout_color_value("-14"),
            ordinary_form_listout_color_value("-15"),
            raw(object_prop_or_default(object, "PeriodStart", "00010101000000")),
            raw(object_prop_or_default(object, "PeriodEnd", "00010101000000")),
            raw("1"),
            raw("1"),
            raw("0"),
            raw("0"),
            raw("0"),
            raw("0"),
            raw("1"),
        }),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_text_document_payload(const oof::platform::object_model::PlatformObject& object) {
    auto base = ordinary_form_listout_root_panel_base_info_record();
    base.items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
    base.items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "0"));
    return list({
        base,
        raw("6"),
        raw("1"),
        raw(object_prop_or_default(object, "Uuid", "00000000-0000-0000-0000-000000000000")),
        list({raw("0")}),
        raw("0"),
        raw("0"),
    });
}


LV ordinary_form_listout_pivot_chart_payload(const oof::platform::object_model::PlatformObject& object) {
    LV payload = oof::platform::stream::parse(R"OOF_PIVOT({3,
{0,
{11},
{75,5,4,1,4,
{4,0,
{10053120},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,
{1,1,
{"ru","<Элемент 2>"}
},1,0,0,2,
{"U"},
{"U"},0,
{4,0,
{13434624},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},1,
{1,1,
{"ru","<Элемент 3>"}
},1,0,0,3,
{"U"},
{"U"},0,
{4,0,
{10053120},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},2,
{1,1,
{"ru","<Элемент 5>"}
},1,0,0,4,
{"U"},
{"U"},0,
{4,0,
{13434624},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,
{1,1,
{"ru","<Элемент 6>"}
},1,0,0,5,
{"U"},
{"U"},0,
{4,0,
{1644953},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,
{1,1,
{"ru","Сводная"}
},0,0,0,1,
{"U"},
{"U"},0,1,4,
{1,1,
{"ru","<Элемент 2>"}
},1,1,
{4,0,
{3484368},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,0,0,
{"U"},
{"U"},0,
{1,1,
{"ru","<Элемент 3>"}
},1,2,
{4,0,
{123390},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},1,0,0,
{"U"},
{"U"},0,
{1,1,
{"ru","<Элемент 5>"}
},1,3,
{4,0,
{1690649},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},2,0,0,
{"U"},
{"U"},0,
{1,1,
{"ru","<Элемент 6>"}
},1,4,
{4,0,
{15053337},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,0,0,
{"U"},
{"U"},0,3,3,6,0,", ",0,
{1,0},
{1,0},
{4,3,
{-3},3},0,0,
{1,1,
{"ru","СводнаяДиаграмма1"}
},1,1,
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},0,
{4,3,
{-1},3},1,
{4,3,
{-1},3},1,
{4,3,
{-1},3},0,
{4,0,
{16777215},0},
{4,3,
{-3},3},
{4,3,
{-3},3},
{4,3,
{-3},3},
{8,2,0,
{-20},1,100},
{8,2,0,
{-20},1,100},
{8,2,0,
{-20},1,100},1,1,1,1,1,
{1,0},0,
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,4,
{0},4},1,1,0,4,30,1,0,1,0,0,1,0,0,1,0,1,1,2,
{1,0},1,0,0,1,
{4,0,
{169},0},0,0,
{1,0,0,0},0,180,5,1,0,4,
{4,0,
{11119017},0},1,0,1,0,1,0,0,1.666666666666667e-1,0,8.333333333333334e-1,5.277777777777777e-2,0,0,8.333333333333334e-1,0,0,9.472222222222222e-1,0,
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"",0,0,
{"N",2},
{"U"},"<Элемент 1> <Элемент 2>
<Элемент 1> <Элемент 2>
2",
{"N",3},
{"U"},"<Элемент 1> <Элемент 2>
<Элемент 1> <Элемент 3>
3",
{"N",3},
{"U"},"<Элемент 1> <Элемент 2>
<Элемент 4> <Элемент 5>
3",
{"N",1},
{"U"},"<Элемент 1> <Элемент 2>
<Элемент 4> <Элемент 6>
1",
{"N",2},
{"U"},"<Элемент 1> <Элемент 3>
<Элемент 1> <Элемент 2>
2",
{"N",4},
{"U"},"<Элемент 1> <Элемент 3>
<Элемент 1> <Элемент 3>
4",
{"N",2},
{"U"},"<Элемент 1> <Элемент 3>
<Элемент 4> <Элемент 5>
2",
{"N",3},
{"U"},"<Элемент 1> <Элемент 3>
<Элемент 4> <Элемент 6>
3",
{"N",2},
{"U"},"<Элемент 4> <Элемент 5>
<Элемент 1> <Элемент 2>
2",
{"N",4},
{"U"},"<Элемент 4> <Элемент 5>
<Элемент 1> <Элемент 3>
4",
{"N",4},
{"U"},"<Элемент 4> <Элемент 5>
<Элемент 4> <Элемент 5>
4",
{"N",3},
{"U"},"<Элемент 4> <Элемент 5>
<Элемент 4> <Элемент 6>
3",
{"N",3},
{"U"},"<Элемент 4> <Элемент 6>
<Элемент 1> <Элемент 2>
3",
{"N",2},
{"U"},"<Элемент 4> <Элемент 6>
<Элемент 1> <Элемент 3>
2",
{"N",5},
{"U"},"<Элемент 4> <Элемент 6>
<Элемент 4> <Элемент 5>
5",
{"N",4},
{"U"},"<Элемент 4> <Элемент 6>
<Элемент 4> <Элемент 6>
4",14,2,
{8,3,0,1,100},1,
{4,4,
{0},4},
{3,0,
{0},1,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},1,1,1,0,0,95,1e-1,1e-1,3e-2,
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,0,
{0},0},2,255,0,8095515,00000000-0000-0000-0000-000000000000,0,
{0,0},
{0,0},
{0,0},
{0,0},
{0,0},0,
{0,0,
{0,1,0,1,0},0,0},
{0,0,
{0,1,0,1,0},0,0},0,0,2,-2,1,10,1,20,0,0,
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},0,0,
{4,4,
{0},4},
{4,4,
{0},4},0,
{
{4,0,
{3484368},0}
},
{
{4,0,
{123390},0}
},
{
{4,0,
{1690649},0}
},
{
{4,0,
{15053337},0}
},
{
{4,0,
{10053120},0},3,0,0,0,"",
{1,0},
{1,0},
{1,0},0},
{
{4,0,
{13434624},0},1,0,0,0,"",
{1,0},
{1,0},
{1,0},0},
{
{4,0,
{10053120},0},2,0,0,0,"",
{1,0},
{1,0},
{1,0},0},
{
{4,0,
{13434624},0},3,0,0,0,"",
{1,0},
{1,0},
{1,0},0},
{
{4,0,
{1644953},0},3,0,0,0,"",
{1,0},
{1,0},
{1,0},0},0,0,0.166666666666666666666666667,0,0.833333333333333333333333333,0.052777777777777777777777778,0,0,0.833333333333333333333333333,0,0,0.947222222222222222222222222,1,5,1,0,0,0.167330677290837,0,0.832669322709163,0.0535211267605633,0,0,0.832669322709163,0,0,0.946478873239436,
{0,0},
{0,0},
{0,0},
{0,0},
{0,14,
{4,4,
{0},4},
{4,4,
{0},4},0,0},
{0,14,
{4,4,
{0},4},
{4,4,
{0},4},0,0},0,0,
{0,0,0,0,0},
{0,0,0,0},0,
{
{1,
{1,1,
{"#","<Элемент 1> <Элемент 2>
<Элемент 1> <Элемент 2>
2"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 1> <Элемент 2>
<Элемент 1> <Элемент 3>
3"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 1> <Элемент 2>
<Элемент 4> <Элемент 5>
3"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 1> <Элемент 2>
<Элемент 4> <Элемент 6>
1"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 1> <Элемент 3>
<Элемент 1> <Элемент 2>
2"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 1> <Элемент 3>
<Элемент 1> <Элемент 3>
4"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 1> <Элемент 3>
<Элемент 4> <Элемент 5>
2"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 1> <Элемент 3>
<Элемент 4> <Элемент 6>
3"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 4> <Элемент 5>
<Элемент 1> <Элемент 2>
2"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 4> <Элемент 5>
<Элемент 1> <Элемент 3>
4"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 4> <Элемент 5>
<Элемент 4> <Элемент 5>
4"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 4> <Элемент 5>
<Элемент 4> <Элемент 6>
3"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 4> <Элемент 6>
<Элемент 1> <Элемент 2>
3"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 4> <Элемент 6>
<Элемент 1> <Элемент 3>
2"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 4> <Элемент 6>
<Элемент 4> <Элемент 5>
5"}
},0},0},
{
{1,
{1,1,
{"#","<Элемент 4> <Элемент 6>
<Элемент 4> <Элемент 6>
4"}
},0},0},,60,
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{0,0,
{0,1,0,1,0},0,0},0,0,0,0,0,0,0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4}
}
},
{0,
{0,
{3,0,1,0,
{1,
{8,0,0,0,0,0,
{"U"},
{1,0},
{"U"},0,4294967281},4294967295},
{0,1,
{0,
{4,0,
{0},0},
{4,0,
{0},0}
}
},1,0}
},
{0,
{3,0,1,0,
{1,
{8,0,0,0,0,0,
{"U"},
{1,0},
{"U"},0,4294911569},232515672},
{0,1,
{0,
{4,0,
{0},0},
{4,0,
{0},0}
}
},1,0}
},
{0,0},1,1},1,6,12,1,2,1,0,
{4,3,
{-7},3},
{4,3,
{-3},3},1})OOF_PIVOT");
    const std::string title = object_prop_or_default(object, "Title", object.name);
    if (payload.is_list && payload.items.size() > 12) {
        payload.items[3] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
        payload.items[12] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "1"));
    }
    (void)title;
    return payload;
}


LV ordinary_form_listout_geographical_schema_payload(const oof::platform::object_model::PlatformObject& object) {
    LV payload = oof::platform::stream::parse(R"OOF_GEO({19,1,
{4,3,
{-10},3},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,3,
{-22},3},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{0},0,1,3,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,1,1,2,
{4,4,
{0},4}
})OOF_GEO");
    if (payload.is_list) {
        payload.items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
        payload.items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "0"));
    }
    return payload;
}

LV ordinary_form_listout_geographical_schema_output_payload() {
    return oof::platform::stream::parse(R"OOF_GEO_EXTRA({2,2,
{
{1,0,0,0},
{0,0,0,0,0,
{}
},
{1,
{1,0},
{8,2,0,
{-20},1,100},
{4,3,
{-3},3},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},1,
{4,3,
{-10},3},0,0,0,95},
{1,
{8,2,0,
{-20},1,100},
{4,3,
{-3},3},
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},1,
{4,3,
{-10},3},0,
{},75,0,5,0,1},
{
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},1,
{4,3,
{-10},3},0,25,5,0},
{0,
{}
},0,1,0,0,0,0},
{0},0})OOF_GEO_EXTRA");
}

LV ordinary_form_listout_graphical_schema_payload(const oof::platform::object_model::PlatformObject& object) {
    LV payload = oof::platform::stream::parse(R"OOF_GRAPHICAL({
{19,1,
{4,3,
{-10},3},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,3,
{-22},3},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,1,1,2,
{4,4,
{0},4}
},5,
{
{5,
{
{1,
{4,3,
{-10},3},1,20,20,3,6,6,
{"N",10},7,
{"N",10},8,
{"N",10},9,
{"N",10},13,
{"N",0},16,
{"N",0}
}
},0,0}
},
{0},0,0})OOF_GRAPHICAL");
    if (payload.is_list && !payload.items.empty() && payload.items[0].is_list) {
        payload.items[0].items[1] = raw(ordinary_form_listout_bool_prop(object, "Visible", "1"));
        payload.items[0].items[5] = raw(ordinary_form_listout_bool_prop(object, "Enabled", "0"));
    }
    return payload;
}

LV ordinary_form_listout_chart_kind_payload() {
    return oof::platform::stream::parse(R"OOF_CHART_KIND({11})OOF_CHART_KIND");
}

LV ordinary_form_listout_chart_body_payload() {
    return oof::platform::stream::parse(R"OOF_CHART_BODY({75,1,0,1,0,
{4,0,
{1644953},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,
{1,1,
{"ru","Сводная"}
},0,0,0,1,
{"U"},
{"U"},0,1,0,-1,0,6,0,", ",0,
{1,0},
{1,0},
{4,3,
{-3},3},0,0,
{1,1,
{"ru","Диаграмма1"}
},1,1,
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},0,
{4,3,
{-1},3},1,
{4,3,
{-1},3},1,
{4,3,
{-1},3},0,
{4,0,
{16777215},0},
{4,3,
{-3},3},
{4,3,
{-3},3},
{4,3,
{-3},3},
{8,2,0,
{-20},1,100},
{8,2,0,
{-20},1,100},
{8,2,0,
{-20},1,100},1,1,1,1,1,
{1,0},0,
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,4,
{0},4},1,1,0,4,30,1,0,1,0,0,1,0,0,0,0,1,1,2,
{1,0},1,0,0,0,
{4,0,
{169},0},0,0,
{1,0,0,0},0,180,5,1,0,4,
{4,0,
{11119017},0},1,0,1,0,1,0,0,1.6875e-1,0,8.3125e-1,6.388888888888888e-2,0,0,8.3125e-1,0,0,9.361111111111111e-1,0,
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"",0,1,14,2,
{8,3,0,1,100},1,
{4,4,
{0},4},
{3,0,
{0},1,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},1,1,1,0,0,95,1e-1,1e-1,3e-2,
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,0,
{0},0},2,255,0,0,00000000-0000-0000-0000-000000000000,0,
{0,0},0,
{0,0,
{0,1,0,1,0},0,0},
{0,0,
{0,1,0,1,0},0,0},0,0,2,-2,1,10,1,20,0,0,
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},0,0,
{4,4,
{0},4},
{4,4,
{0},4},0,
{
{4,0,
{1644953},0},3,0,0,0,"",
{1,0},
{1,0},
{1,0},0},0,0,0.16875,0,0.83125,0.063888888888888888888888889,0,0,0.83125,0,0,0.936111111111111111111111111,1,5,1,0,0,0.168032786885246,0,0.831967213114754,0.0637583892617449,0,0,0.831967213114754,0,0,0.936241610738255,
{0,0},
{0,0},
{0,0},
{0,0},
{0,14,
{4,4,
{0},4},
{4,4,
{0},4},0,0},
{0,14,
{4,4,
{0},4},
{4,4,
{0},4},0,0},0,0,
{0,0,0,0,0},
{0,0,0,0},0,,60,
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{0,0,
{0,1,0,1,0},0,0},0,0,0,0,0,0,0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4}
})OOF_CHART_BODY");
}

LV ordinary_form_listout_gantt_chart_payload() {
    return oof::platform::stream::parse(R"OOF_GANTT({19,
{0,
{11},
{75,1,0,1,0,
{4,0,
{1644953},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,
{1,1,
{"ru","Сводная"}
},0,0,0,1,
{"U"},
{"U"},0,1,0,-1,0,6,0,", ",0,
{1,0},
{1,0},
{4,3,
{-3},3},0,0,
{1,1,
{"ru","ДиаграммаГанта1"}
},1,1,
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},
{3,0,
{0},1,1,0,00000000-0000-0000-0000-000000000000},
{4,3,
{-22},3},0,
{4,3,
{-1},3},1,
{4,3,
{-1},3},1,
{4,3,
{-1},3},0,
{4,0,
{16777215},0},
{4,3,
{-3},3},
{4,3,
{-3},3},
{4,3,
{-3},3},
{8,2,0,
{-20},1,100},
{8,2,0,
{-20},1,100},
{8,2,0,
{-20},1,100},1,1,1,1,1,
{1,0},0,
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,4,
{0},4},1,1,0,4,30,1,0,0,0,0,1,0,0,0,0,1,1,2,
{1,0},1,0,0,1,
{4,0,
{169},0},0,0,
{1,0,0,0},0,180,5,1,0,4,
{4,0,
{11119017},0},1,0,1,0,1,0,0,1.6875e-1,0,8.3125e-1,3.888888888888888e-2,0,0,8.3125e-1,0,0,9.611111111111111e-1,0,
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"",0,1,14,2,
{8,3,0,1,100},1,
{4,4,
{0},4},
{3,0,
{0},1,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},1,1,1,0,0,95,1e-1,1e-1,3e-2,
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,0,
{0},0},2,255,0,0,00000000-0000-0000-0000-000000000000,0,
{0,0},0,
{0,0,
{0,1,0,1,0},0,0},
{0,0,
{0,1,0,1,0},0,0},0,0,2,-2,1,10,1,20,0,0,
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},0,0,
{4,4,
{0},4},
{4,4,
{0},4},0,
{
{4,0,
{1644953},0},3,0,0,0,"",
{1,0},
{1,0},
{1,0},0},0,0,0.16875,0,0.83125,0.038888888888888888888888889,0,0,0.83125,0,0,0.961111111111111111111111111,1,5,1,0,0,0.168085106382979,0,0.831914893617021,0.0377733598409543,0,0,0.831914893617021,0,0,0.962226640159045,
{0,0},
{0,0},
{0,0},
{0,0},
{0,14,
{4,4,
{0},4},
{4,4,
{0},4},0,0},
{0,14,
{4,4,
{0},4},
{4,4,
{0},4},0,0},0,0,
{0,0,0,0,0},
{0,0,0,0},0,,60,
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{0,0,
{0,1,0,1,0},0,0},0,0,0,0,0,0,0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4}
}
},
{1,
{3,0,1,0,
{2,
{8,0,0,0,0,0,
{"U"},
{1,0},
{"U"},0,4294949825},
{4,0,
{0},"",-1,-1,1,0,""},
{8,3,0,1,100}
},
{0,1,
{0,
{0,
{4,0,
{0},0},
{4,0,
{0},0}
},
{4,4,
{0},4},
{4,4,
{0},4}
}
},1,0}
},
{0,
{3,0,1,0,
{3,
{8,0,0,0,0,0,
{"U"},
{1,0},
{"U"},0,4294902785}
},
{0,1,
{0,
{0,
{4,0,
{0},0},
{4,0,
{0},0}
},
{4,0,
{0},0}
}
},1,0}
},0,0,1,
{3,0,1,
{8,30,1,1,
{4,0,
{0},2,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,0,
{12632256},0},3,
{1,0},
{0,
{1,0,0}
},
{4,4,
{0},4},
{4,4,
{0},4},1},0,
{4,3,
{-10},3},
{4,3,
{-3},3},0},2,50,1,1,20260524000000,20260604235959,20260524000000,3,3,30,0,1,0,
{1,0},
{4,0,
{16777215},0},
{3,
{0,
{1,0,0},0},
{0,0}
},0,
{4,0,
{8388608},0},
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{0,0,0},0,0,1,0,0})OOF_GANTT");
}

LV ordinary_form_listout_dendrogram_payload() {
    return oof::platform::stream::parse(R"OOF_DENDRO({0,
{0,
{11},
{75,1,0,1,0,
{4,0,
{1644953},0},
{4,0,
{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},3,
{1,1,
{"ru","Сводная"}
},0,0,0,1,
{"U"},
{"U"},0,1,0,-1,0,6,0,", ",0,
{1,0},
{1,0},
{4,3,
{-3},3},0,0,
{1,1,
{"ru","Дендрограмма1"}
},1,0,
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,3,
{-22},3},0,
{4,3,
{-1},3},1,
{4,3,
{-1},3},1,
{4,3,
{-1},3},0,
{4,0,
{16777215},0},
{4,3,
{-3},3},
{4,3,
{-3},3},
{4,3,
{-3},3},
{8,2,0,
{-20},1,100},
{8,2,0,
{-20},1,100},
{8,2,0,
{-20},1,100},1,1,1,1,1,
{1,0},0,
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,4,
{0},4},1,1,0,4,30,1,0,1,0,0,1,0,0,0,0,1,1,2,
{1,0},1,0,0,0,
{4,0,
{169},0},0,0,
{1,0,0,0},0,180,5,1,0,4,
{4,0,
{11119017},0},1,0,1,0,1,0,4.166666666666666e-2,0,0,0,0,1,1,0,0,0,9.583333333333334e-1,0,
{4,3,
{-22},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"",0,1,14,2,
{8,3,0,1,100},1,
{4,4,
{0},4},
{3,0,
{0},1,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},1,1,1,0,0,95,1e-1,1e-1,3e-2,
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},
{4,0,
{0},0},2,255,0,0,00000000-0000-0000-0000-000000000000,0,
{0,0},0,
{0,0,
{0,1,0,1,0},0,0},
{0,0,
{0,1,0,1,0},0,0},0,0,2,-2,1,10,1,20,0,0,
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},0,0,
{4,4,
{0},4},
{4,4,
{0},4},0,
{
{4,0,
{1644953},0},3,0,0,0,"",
{1,0},
{1,0},
{1,0},0},0,0.041666666666666666666666667,0,0,0,0,1,1,0,0,0,0.958333333333333333333333333,1,6,1,0,0.0425055928411633,0,0,1,0.0425055928411633,0,0,0,0,0,0.957494407158836,
{0,0},
{0,0},
{0,0},
{0,0},
{0,14,
{4,4,
{0},4},
{4,4,
{0},4},0,0},
{0,14,
{4,4,
{0},4},
{4,4,
{0},4},0,0},0,0,
{0,0,0,0,0},
{0,0,0,0},0,,60,
{2,0,0,2,
{1,0},
{1,4,0.5,0.5,
{8,3,0,1,100},
{4,4,
{0},4},
{4,4,
{0},4},1,
{3,0,
{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},
{4,4,
{0},4},4,2,0},2,0,0,
{4,4,
{0},4},
{8,3,0,1,100},
{4,4,
{0},4},2,
{1,0},0,
{4,4,
{0},4},0,0,0,0,0,0},
{0,0,
{0,1,0,1,0},0,0},0,0,0,0,0,0,0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4}
}
},
{0,
{3,0,1,0,
{0,
{8,0,0,0,0,0,
{"U"},
{1,0},
{"U"},0,4294901793}
},
{0,1,
{0,
{4,0,
{0},0},
{4,0,
{0},0}
}
},1,0}
},
{0,
{3,0,1,0,
{0,
{8,0,0,0,0,0,
{"U"},
{1,0},
{"U"},0,4294901761},0,0,0},
{0,1,
{0,
{4,0,
{0},0},
{4,0,
{0},0}
}
},1,0}
},0,1,6,12,
{4,0,
{8388608},0},
{4,0,
{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},0})OOF_DENDRO");
}

LV ordinary_form_listout_generic_object_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    return list({
        raw("1"),
        ordinary_form_listout_extended_base_info(object),
        localized_text_record(title),
        ordinary_form_listout_button_picture_record(object),
        ordinary_form_listout_event_table(form_object, object.object_id),
    });
}

LV ordinary_form_listout_event_action_record(const oof::platform::object_model::PlatformObject& event) {
    const std::string handler = object_property_value(event, "Handler");
    std::string title = object_property_value(event, "Title");
    if (title.empty()) {
        title = handler;
    }
    return list({
        raw("3"),
        str_atom(handler),
        list({
            raw("1"),
            str_atom(handler),
            localized_text_record(title),
            localized_text_record(title),
            localized_text_record(title),
            ordinary_form_listout_empty_picture_value(),
            list({raw("0"), raw("0"), raw("0")}),
        }),
    });
}

LV ordinary_form_listout_event_record(const oof::platform::object_model::PlatformObject& event) {
    return list({
        raw("0"),
        raw(object_property_value(event, "ID")),
        ordinary_form_listout_event_action_record(event),
    });
}

LV ordinary_form_listout_event_table(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view parent_object_id
) {
    std::vector<LV> items;
    const auto events = event_objects_for_parent(form_object, parent_object_id);
    for (const auto* event : events) {
        const std::string id = object_property_value(*event, "ID");
        const std::string handler = object_property_value(*event, "Handler");
        if (!id.empty() && !handler.empty()) {
            items.push_back(ordinary_form_listout_event_record(*event));
        }
    }
    std::vector<LV> table;
    table.push_back(raw(std::to_string(items.size())));
    table.insert(table.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    return list(std::move(table));
}

LV ordinary_form_listout_root_panel_info_from_layout_xml(const std::string& xml);

LV ordinary_form_listout_control_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    const std::string title = object_prop_or_default(object, "Title", object.name);
    if (object.platform_type == "Label") {
        return ordinary_form_listout_label_payload(form_object, object);
    }
    if (object.platform_type == "Image") {
        return ordinary_form_listout_image_payload(form_object, object);
    }
    if (object.platform_type == "CheckBox") {
        return ordinary_form_listout_checkbox_payload(form_object, object);
    }
    if (object.platform_type == "RadioButton") {
        return ordinary_form_listout_radiobutton_payload(form_object, object);
    }
    if (object.platform_type == "TextBox" || object.platform_type == "InputField") {
        return ordinary_form_listout_input_field_payload(form_object, object);
    }
    if (object.platform_type == "ChoiceField") {
        return ordinary_form_listout_choice_field_payload(form_object, object);
    }
    if (object.platform_type == "Button") {
        return list({
            raw("1"),
            ordinary_form_listout_button_base_info(object),
            ordinary_form_listout_event_table(form_object, object.object_id),
        });
    }
    if (object.platform_type == "CommandBar") {
        return ordinary_form_listout_command_bar_payload(object);
    }
    if (object.platform_type == "Table") {
        return ordinary_form_listout_table_payload(form_object, object);
    }
    if (object.platform_type == "ListBox") {
        return ordinary_form_listout_listbox_payload(form_object, object);
    }
    if (object.platform_type == "GroupBox") {
        return ordinary_form_listout_groupbox_payload(object);
    }
    if (object.platform_type == "Splitter") {
        return ordinary_form_listout_splitter_payload(object);
    }
    if (object.platform_type == "SpreadsheetDocumentField") {
        return ordinary_form_listout_spreadsheet_payload(object);
    }
    if (object.platform_type == "TrackBar") {
        return ordinary_form_listout_trackbar_payload(form_object, object);
    }
    if (object.platform_type == "ProgressBar") {
        return ordinary_form_listout_progressbar_payload(object);
    }
    if (object.platform_type == "CalendarField") {
        return ordinary_form_listout_calendar_payload(form_object, object);
    }
    if (object.platform_type == "TextDocumentField") {
        return ordinary_form_listout_text_document_payload(object);
    }
    if (object.platform_type == "PivotChart") {
        return ordinary_form_listout_pivot_chart_payload(object);
    }
    if (object.platform_type == "GeographicalSchemaField") {
        return ordinary_form_listout_geographical_schema_payload(object);
    }
    if (object.platform_type == "GraphicalSchemaField") {
        return ordinary_form_listout_graphical_schema_payload(object);
    }
    if (object.platform_type == "GanttChart") {
        return ordinary_form_listout_gantt_chart_payload();
    }
    if (object.platform_type == "Dendrogram") {
        return ordinary_form_listout_dendrogram_payload();
    }
    if (object.platform_type == "Panel") {
        if (const auto* root_layout = find_object_property(form_object.form, "RootPanelLayoutXml")) {
            LV layout_info = ordinary_form_listout_root_panel_info_from_layout_xml(root_layout->value);
            if (layout_info.is_list) {
                replace_panel_page_title_atoms(layout_info, title);
                return layout_info;
            }
        }
    }
    return ordinary_form_listout_generic_object_payload(form_object, object);
}

bool ordinary_form_listout_has_typed_payload(
    const oof::platform::object_model::PlatformObject& object
) {
    if (object.platform_type == "Label" ||
        object.platform_type == "Image" ||
        object.platform_type == "CheckBox" ||
        object.platform_type == "TextBox" ||
        object.platform_type == "InputField" ||
        object.platform_type == "Button") {
        return true;
    }
    return object.platform_type == "CommandBar" ||
           object.platform_type == "ChoiceField" ||
           object.platform_type == "CalendarField" ||
           object.platform_type == "Chart" ||
           object.platform_type == "GeographicalSchemaField" ||
           object.platform_type == "Dendrogram" ||
           object.platform_type == "GanttChart" ||
           object.platform_type == "GraphicalSchemaField" ||
           object.platform_type == "GroupBox" ||
           object.platform_type == "ListBox" ||
           object.platform_type == "Panel" ||
           object.platform_type == "PivotChart" ||
           object.platform_type == "ProgressBar" ||
           object.platform_type == "RadioButton" ||
           object.platform_type == "SpreadsheetDocumentField" ||
           object.platform_type == "Splitter" ||
           object.platform_type == "Table" ||
           object.platform_type == "TextDocumentField" ||
           object.platform_type == "TrackBar";
}

struct OrdinaryFormListOutCoverageSummary {
    std::size_t typed_payload_controls = 0;
    std::size_t minimal_payload_controls = 0;
    std::map<std::string, std::size_t> typed_payload_types;
    std::map<std::string, std::size_t> minimal_payload_types;
};

OrdinaryFormListOutCoverageSummary ordinary_form_listout_coverage_summary(
    const oof::platform::object_model::PlatformFormObject& form_object
) {
    OrdinaryFormListOutCoverageSummary summary;
    for (const auto& object : form_object.items.objects()) {
        if (ordinary_form_listout_has_typed_payload(object)) {
            ++summary.typed_payload_controls;
            ++summary.typed_payload_types[object.platform_type];
            continue;
        }
        ++summary.minimal_payload_controls;
        ++summary.minimal_payload_types[object.platform_type];
    }
    return summary;
}

void print_ordinary_form_listout_coverage_json(const OrdinaryFormListOutCoverageSummary& summary) {
    (void)summary;
}

std::vector<const oof::platform::object_model::PlatformObject*> ordinary_form_listout_children(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent
) {
    return child_objects_for_parent(form_object, parent.object_id);
}

LV ordinary_form_listout_child_table(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent
);

LV ordinary_form_listout_control_record(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    if (object.platform_type == "GeographicalSchemaField") {
        return list({
            raw(ordinary_form_listout_control_guid(object.platform_type)),
            raw(object.object_id.empty() ? "0" : object.object_id),
            ordinary_form_listout_geographical_schema_payload(object),
            ordinary_form_listout_geographical_schema_output_payload(),
            ordinary_form_listout_geometry(object),
            ordinary_form_listout_metadata(object),
            ordinary_form_listout_child_table(form_object, object),
        });
    }
    if (object.platform_type == "Chart") {
        return list({
            raw(ordinary_form_listout_control_guid(object.platform_type)),
            raw(object.object_id.empty() ? "0" : object.object_id),
            ordinary_form_listout_chart_kind_payload(),
            ordinary_form_listout_chart_body_payload(),
            ordinary_form_listout_geometry(object),
            ordinary_form_listout_metadata(object),
            ordinary_form_listout_child_table(form_object, object),
        });
    }
    return list({
        raw(ordinary_form_listout_control_guid(object.platform_type)),
        raw(object.object_id.empty() ? "0" : object.object_id),
        ordinary_form_listout_control_payload(form_object, object),
        ordinary_form_listout_geometry(object),
        ordinary_form_listout_metadata(object),
        ordinary_form_listout_child_table(form_object, object),
    });
}

LV ordinary_form_listout_child_table(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent
) {
    std::vector<LV> items;
    const auto children = ordinary_form_listout_children(form_object, parent);
    items.push_back(raw(std::to_string(children.size())));
    for (const auto* child : children) {
        items.push_back(ordinary_form_listout_control_record(form_object, *child));
    }
    return list(std::move(items));
}

std::int64_t ordinary_form_listout_slot_number(std::string_view composite_id) {
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

LV ordinary_form_listout_composite_id_value(const std::string& composite_id) {
    if (!composite_id.empty() && composite_id.front() == '{') {
        return oof::platform::stream::parse(composite_id);
    }
    return raw(composite_id.empty() ? "0" : composite_id);
}

LV ordinary_form_listout_attribute_record(const oof::platform::object_model::PlatformObject& attribute) {
    const std::string id = object_prop_or_default(attribute, "ID", "0");
    const std::string name = object_prop_or_default(attribute, "Name", attribute.name);
    const std::string main = object_prop_or_default(attribute, "Main", "1");
    const std::string type = object_prop_or_default(attribute, "Type", "{\"Pattern\"}");
    return list({
        ordinary_form_listout_composite_id_value(id),
        raw(main == "0" || main == "false" ? "0" : "1"),
        raw("0"),
        raw("1"),
        str_atom(name),
        oof::platform::stream::parse(type),
    });
}

void collect_ordinary_form_listout_attribute_links(
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent,
    const std::map<std::string, std::string>& attribute_slots,
    std::vector<LV>& links
) {
    for (const auto* child : ordinary_form_listout_children(form_object, parent)) {
        const auto found = attribute_slots.find(child->name);
        if (found != attribute_slots.end()) {
            links.push_back(list({
                raw(child->object_id),
                list({raw("1"), ordinary_form_listout_composite_id_value(found->second)}),
            }));
        }
        collect_ordinary_form_listout_attribute_links(form_object, *child, attribute_slots, links);
    }
}

LV ordinary_form_listout_attributes_table(const oof::platform::object_model::PlatformFormObject& form_object) {
    std::vector<LV> records;
    std::map<std::string, std::string> attribute_slots_by_name;
    std::int64_t max_slot = 0;
    for (const auto& attribute : form_object.attributes.objects()) {
        const std::string id = object_prop_or_default(attribute, "ID", "0");
        const std::string name = object_prop_or_default(attribute, "Name", attribute.name);
        max_slot = std::max(max_slot, ordinary_form_listout_slot_number(id));
        if (!name.empty()) {
            attribute_slots_by_name[name] = id;
        }
        records.push_back(ordinary_form_listout_attribute_record(attribute));
    }

    std::vector<LV> record_table;
    record_table.push_back(raw(std::to_string(records.size())));
    record_table.insert(record_table.end(), std::make_move_iterator(records.begin()), std::make_move_iterator(records.end()));

    std::vector<LV> links;
    collect_ordinary_form_listout_attribute_links(form_object, form_object.form, attribute_slots_by_name, links);
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

LV ordinary_form_listout_root_panel_info_from_layout_xml(const std::string& xml);

LV ordinary_form_listout_form_object_info(
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

LV ordinary_form_listout_default_color_record() {
    return list({raw("4"), raw("4"), list({raw("0")}), raw("4")});
}

LV ordinary_form_listout_empty_page_style_record() {
    return list({raw("4"), raw("0"), list({raw("0")}), str_atom(""), raw("-1"), raw("-1"), raw("1"), raw("0"), str_atom("")});
}

LV ordinary_form_listout_page_style_group_record(std::string_view active) {
    return list({
        raw("10"), raw(std::string(active)),
        ordinary_form_listout_empty_page_style_record(),
        ordinary_form_listout_empty_page_style_record(),
        ordinary_form_listout_empty_page_style_record(),
        raw("100"), raw("0"), raw("0"), raw("0"), raw("0"), raw("0"),
    });
}

LV ordinary_form_listout_root_page_state_style_group_record(std::string_view style_mode) {
    auto record = ordinary_form_listout_page_style_group_record("0");
    record.items[6] = raw(std::string(style_mode.empty() ? "0" : style_mode));
    return record;
}

LV ordinary_form_listout_root_panel_base_info_record() {
    return list({
        raw("19"),
        raw("1"),
        ordinary_form_listout_default_color_record(),
        ordinary_form_listout_default_color_record(),
        list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
        raw("0"),
        list({raw("4"), raw("3"), list({raw("-22")}), raw("3")}),
        ordinary_form_listout_default_color_record(),
        ordinary_form_listout_default_color_record(),
        list({raw("4"), raw("3"), list({raw("-7")}), raw("3")}),
        list({raw("4"), raw("3"), list({raw("-21")}), raw("3")}),
        list({raw("3"), raw("0"), list({raw("0")}), raw("0"), raw("0"), raw("0"), raw("48312c09-257f-4b29-b280-284dd89efc1e")}),
        list({raw("1"), raw("0")}),
        raw("0"),
        raw("0"),
        raw("100"),
        raw("2"),
        raw("1"),
        raw("1"),
        raw("2"),
        ordinary_form_listout_default_color_record(),
    });
}

LV ordinary_form_listout_root_page_state_record(const XmlElementSlice& page_state) {
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
        ordinary_form_listout_root_page_state_style_group_record(style_mode),
        raw("-1"),
        raw("1"),
        raw("1"),
        str_atom(name),
        raw("1"),
        ordinary_form_listout_default_color_record(),
        ordinary_form_listout_default_color_record(),
        list({raw("8"), raw("3"), raw("0"), raw("1"), raw("100")}),
        raw("1"),
    });
}

std::vector<LV> ordinary_form_listout_root_page_state_records(const XmlElementSlice& layout) {
    std::vector<LV> states;
    for (const auto& page_state : find_xml_elements(layout.body, "PageState")) {
        states.push_back(ordinary_form_listout_root_page_state_record(page_state));
    }
    if (states.empty()) {
        XmlElementSlice fallback;
        fallback.attrs = " name=\"Страница1\"";
        states.push_back(ordinary_form_listout_root_page_state_record(fallback));
    }
    return states;
}

std::vector<LV> ordinary_form_listout_layout_dependency_sequence(const XmlElementSlice& layout) {
    std::vector<LV> sequence;
    const auto groups = find_xml_elements(layout.body, "LayoutDependencyGroup");
    if (!groups.empty()) {
        sequence.push_back(raw("0"));
    }
    for (const auto& group : groups) {
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

std::vector<LV> ordinary_form_listout_root_page_layout_records(const XmlElementSlice& layout) {
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

LV ordinary_form_listout_root_panel_info_from_layout_xml(const std::string& xml) {
    const auto layout_xml = first_xml_element(xml, "RootPanelLayout");
    if (layout_xml.self_closing && layout_xml.body.empty()) {
        return {};
    }
    std::vector<LV> states = ordinary_form_listout_root_page_state_records(layout_xml);
    std::vector<LV> state_table;
    state_table.push_back(raw("1"));
    state_table.push_back(raw(std::to_string(states.size())));
    state_table.insert(state_table.end(), std::make_move_iterator(states.begin()), std::make_move_iterator(states.end()));

    std::vector<LV> position_records = ordinary_form_listout_root_page_layout_records(layout_xml);
    std::vector<LV> body{
        ordinary_form_listout_root_panel_base_info_record(),
        raw("26"),
    };
    auto dependency_sequence = ordinary_form_listout_layout_dependency_sequence(layout_xml);
    body.insert(body.end(), std::make_move_iterator(dependency_sequence.begin()), std::make_move_iterator(dependency_sequence.end()));
    body.push_back(ordinary_form_listout_page_style_group_record("1"));
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
    body.push_back(ordinary_form_listout_default_color_record());
    body.push_back(raw("0"));
    body.push_back(raw("0"));
    body.push_back(raw("57"));
    body.push_back(raw("0"));
    body.push_back(raw("0"));
    return list({raw("1"), list(std::move(body)), list({raw("0")})});
}

void listout_write_value(oof::platform::stream::ListOutStream& out, const LV& value) {
    if (value.is_list) {
        out.begin_list();
        for (const auto& item : value.items) {
            listout_write_value(out, item);
        }
        out.end_list();
        return;
    }
    if (value.atom_kind == LV::AtomKind::string) {
        out.write_string(value.atom);
        return;
    }
    out.write_raw_atom(value.atom);
}

void platform_form_listout_write_control_record(
    oof::platform::stream::ListOutStream& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
);

void platform_form_listout_write_child_table(
    oof::platform::stream::ListOutStream& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& parent
) {
    const auto children = ordinary_form_listout_children(form_object, parent);
    out.begin_list();
    out.write_raw_atom(std::to_string(children.size()));
    for (const auto* child : children) {
        platform_form_listout_write_control_record(out, form_object, *child);
    }
    out.end_list();
}

void platform_form_listout_write_control_record(
    oof::platform::stream::ListOutStream& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformObject& object
) {
    out.begin_list();
    out.write_raw_atom(ordinary_form_listout_control_guid(object.platform_type));
    out.write_raw_atom(object.object_id.empty() ? "0" : object.object_id);
    if (object.platform_type == "GeographicalSchemaField") {
        listout_write_value(out, ordinary_form_listout_geographical_schema_payload(object));
        listout_write_value(out, ordinary_form_listout_geographical_schema_output_payload());
    } else if (object.platform_type == "Chart") {
        listout_write_value(out, ordinary_form_listout_chart_kind_payload());
        listout_write_value(out, ordinary_form_listout_chart_body_payload());
    } else {
        listout_write_value(out, ordinary_form_listout_control_payload(form_object, object));
    }
    listout_write_value(out, ordinary_form_listout_geometry(object));
    listout_write_value(out, ordinary_form_listout_metadata(object));
    platform_form_listout_write_child_table(out, form_object, object);
    out.end_list();
}

void platform_form_listout_write_root_child_table(
    oof::platform::stream::ListOutStream& out,
    const oof::platform::object_model::PlatformFormObject& form_object
) {
    const auto children = ordinary_form_listout_children(form_object, form_object.form);
    out.begin_list();
    out.write_raw_atom(std::to_string(children.size()));
    for (const auto* child : children) {
        platform_form_listout_write_control_record(out, form_object, *child);
    }
    out.end_list();
}

void platform_form_listout_write_root_record(
    oof::platform::stream::ListOutStream& out,
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view title
) {
    LV root_panel_info = list({raw("1"), localized_text_record(title)});
    if (const auto* root_layout = find_object_property(form_object.form, "RootPanelLayoutXml")) {
        LV layout_info = ordinary_form_listout_root_panel_info_from_layout_xml(root_layout->value);
        if (layout_info.is_list) {
            root_panel_info = std::move(layout_info);
        }
    }

    const std::string width = object_property_value(form_object.form, "Width");
    const std::string height = object_property_value(form_object.form, "Height");
    const std::string counter = object_property_value(form_object.form, "SerializationCounter");
    const bool extended_root = !width.empty() || !height.empty() || !counter.empty();

    out.begin_list();
    out.write_raw_atom(extended_root ? "18" : "16");
    listout_write_value(out, list({localized_text_record(title), raw("42"), raw("3")}));
    out.begin_list();
    out.write_raw_atom(ordinary_form_listout_control_guid("Panel"));
    listout_write_value(out, root_panel_info);
    platform_form_listout_write_root_child_table(out, form_object);
    out.end_list();
    out.write_raw_atom("885");
    out.write_raw_atom("244");
    out.write_raw_atom("1");
    out.write_raw_atom("0");
    out.write_raw_atom("1");
    out.write_raw_atom("4");
    out.write_raw_atom("4");
    out.write_raw_atom(extended_root ? (counter.empty() ? "3" : counter) : "6");
    if (extended_root) {
        out.write_raw_atom(width.empty() ? "0" : width);
        out.write_raw_atom(height.empty() ? "0" : height);
        out.write_raw_atom("96");
    }
    out.end_list();
}

LV platform_form_listout_payload(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view title
) {
    oof::platform::stream::ListOutStream out;
    out.begin_list();
    out.write_raw_atom("27");
    platform_form_listout_write_root_record(out, form_object, title);
    listout_write_value(out, ordinary_form_listout_attributes_table(form_object));
    listout_write_value(out, ordinary_form_listout_form_object_info(form_object));
    out.begin_list();
    out.write_raw_atom("0");
    out.end_list();
    out.write_raw_atom("1");
    out.write_raw_atom("4");
    out.write_raw_atom("1");
    out.write_raw_atom("0");
    out.write_raw_atom("0");
    out.write_raw_atom("0");
    out.begin_list();
    out.write_raw_atom("0");
    out.end_list();
    out.begin_list();
    out.write_raw_atom("0");
    out.end_list();
    out.begin_list();
    out.write_raw_atom("10");
    out.write_raw_atom("0");
    listout_write_value(out, ordinary_form_listout_empty_page_style_record());
    listout_write_value(out, ordinary_form_listout_empty_page_style_record());
    listout_write_value(out, ordinary_form_listout_empty_page_style_record());
    out.write_raw_atom("100");
    out.write_raw_atom("0");
    out.write_raw_atom("0");
    out.write_raw_atom("0");
    out.write_raw_atom("0");
    out.write_raw_atom("0");
    out.end_list();
    out.write_raw_atom("1");
    out.write_raw_atom("2");
    out.write_raw_atom("0");
    out.write_raw_atom("0");
    out.write_raw_atom("1");
    out.write_raw_atom("1");
    out.end_list();
    return out.root();
}

LV platform_form_listout_payload(
    const oof::ordinary::object::OrdinaryForm& form,
    std::string_view title
) {
    return platform_form_listout_payload(form.platform_object(), title);
}

std::vector<std::uint8_t> platform_form_listout_payload_bytes(const LV& payload) {
    const std::string text = oof::platform::stream::dump_listout(payload);
    std::vector<std::uint8_t> out{0xef, 0xbb, 0xbf};
    out.insert(out.end(), text.begin(), text.end());
    return out;
}

constexpr std::uint64_t default_source_container_ticks = 630822816000000000ULL;
constexpr std::uint64_t unix_epoch_container_ticks = 621355968000000000ULL;

std::optional<std::uint64_t> parse_optional_u64(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(std::string(value), &consumed, 10);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t> filesystem_mtime_container_ticks(const std::filesystem::path& path) {
    try {
        if (!std::filesystem::is_regular_file(path)) {
            return std::nullopt;
        }
        const auto file_time = std::filesystem::last_write_time(path);
        const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        using container_tick_duration = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;
        const auto ticks_since_unix_epoch =
            std::chrono::duration_cast<container_tick_duration>(system_time.time_since_epoch()).count();
        if (ticks_since_unix_epoch < 0 &&
            static_cast<std::uint64_t>(-ticks_since_unix_epoch) > unix_epoch_container_ticks) {
            return std::nullopt;
        }
        return unix_epoch_container_ticks + ticks_since_unix_epoch;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

struct SourcePackageContainerTimes {
    std::uint64_t form_created = default_source_container_ticks;
    std::uint64_t form_modified = default_source_container_ticks;
    std::uint64_t module_created = default_source_container_ticks;
    std::uint64_t module_modified = default_source_container_ticks;
    std::string source = "default";
};

SourcePackageContainerTimes source_package_container_times(
    const std::filesystem::path& xml_path,
    const std::filesystem::path& module_path,
    const std::string& source_xml
) {
    SourcePackageContainerTimes times;
    const auto form_xml = first_xml_element(source_xml, "Form");
    const auto legacy_created = parse_optional_u64(xml_attr_value(form_xml.attrs, "containerCreatedTicks"));
    const auto legacy_modified = parse_optional_u64(xml_attr_value(form_xml.attrs, "containerModifiedTicks"));
    if (legacy_created.has_value() || legacy_modified.has_value()) {
        times.form_created = legacy_created.value_or(legacy_modified.value_or(default_source_container_ticks));
        times.form_modified = legacy_modified.value_or(times.form_created);
        times.module_created = times.form_created;
        times.module_modified = times.form_modified;
        times.source = "legacy-xml-container-ticks";
        return times;
    }

    const auto form_mtime = filesystem_mtime_container_ticks(xml_path);
    const auto module_mtime = filesystem_mtime_container_ticks(module_path);
    if (form_mtime.has_value() || module_mtime.has_value()) {
        times.form_modified = form_mtime.value_or(module_mtime.value_or(default_source_container_ticks));
        times.form_created = times.form_modified;
        times.module_modified = module_mtime.value_or(times.form_modified);
        times.module_created = times.module_modified;
        times.source = module_mtime.has_value() ? "file-mtime" : "form-xml-mtime";
    }
    return times;
}

struct SourcePackageBuildResult {
    std::vector<std::uint8_t> bytes;
    std::size_t control_count = 0;
    bool module_package_file_used = false;
    std::size_t module_bytes = 0;
    std::size_t picture_files_read = 0;
    OrdinaryFormListOutCoverageSummary listout_coverage;
    SourcePackageContainerTimes container_times;
};

SourcePackageBuildResult build_formbin_source_package(const std::string& xml_path) {
    const auto xml_package_path = std::filesystem::path(xml_path);
    SourcePackageBuildResult result;
    const std::string source_xml = read_file_text_lossy(xml_path);
    const std::string package_xml = inline_picture_package_files_for_build(
        source_xml,
        xml_package_path,
        result.picture_files_read);
    const oof::ordinary::object::OrdinaryForm form(platform_form_object_from_public_xml(package_xml));
    const std::string title = public_form_title_from_xml(package_xml);
    result.control_count = form.items().count();
    result.listout_coverage = ordinary_form_listout_coverage_summary(form.platform_object());

    const auto module_path = form_package_module_path(xml_package_path);
    result.container_times = source_package_container_times(xml_package_path, module_path, source_xml);

    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    container.files.push_back({
        "form",
        result.container_times.form_created,
        result.container_times.form_modified,
        platform_form_listout_payload_bytes(platform_form_listout_payload(form, title)),
    });

    std::vector<std::uint8_t> module_payload;
    if (std::filesystem::is_regular_file(module_path)) {
        module_payload = read_file_bytes(module_path.string());
        result.module_package_file_used = true;
        result.module_bytes = module_payload.size();
    }
    container.files.push_back({
        "module",
        result.container_times.module_created,
        result.container_times.module_modified,
        std::move(module_payload),
    });

    result.bytes = oof::platform::formbin::serialize_container(container);
    return result;
}

void write_formbin_from_source_package(
    const std::string& xml_path,
    const std::string& output_path
) {
    const auto result = build_formbin_source_package(xml_path);

    write_file_bytes(output_path, result.bytes);
    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"formbin-build-source-package\"";
    std::cout << ",\"productPath\":true";
    std::cout << ",\"architecture\":\"Form.xml -> OrdinaryForm -> ListOutStream -> Form.bin\"";
    std::cout << ",\"bytes\":" << result.bytes.size();
    std::cout << ",\"source\":\"Form.xml\"";
    std::cout << ",\"controls\":" << result.control_count;
    std::cout << ",\"moduleSource\":";
    print_json_string(result.module_package_file_used ? "package-file" : "empty");
    std::cout << ",\"moduleBytes\":" << result.module_bytes;
    std::cout << ",\"picturePackageFilesRead\":" << result.picture_files_read;
    std::cout << ",\"containerTicksSource\":";
    print_json_string(result.container_times.source);
    std::cout << ",\"formCreatedTicks\":" << result.container_times.form_created;
    std::cout << ",\"formModifiedTicks\":" << result.container_times.form_modified;
    std::cout << ",\"moduleCreatedTicks\":" << result.container_times.module_created;
    std::cout << ",\"moduleModifiedTicks\":" << result.container_times.module_modified;
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    print_ordinary_form_listout_coverage_json(result.listout_coverage);
    std::cout << "}\n";
}

void write_formbin_from_platform_xsd_xml(
    const std::string& xml_path,
    const std::string& output_path
) {
    const std::filesystem::path xml_package_path(xml_path);
    const std::string source_xml = read_file_text_lossy(xml_path);
    auto result = platform_xdto_schema_order_list_stream_payload(
        platform_xdto_object_from_xml(source_xml));
    const auto& form_object = result.form_object;
    const auto container_times = source_package_container_times(
        xml_package_path,
        form_package_module_path(xml_package_path),
        source_xml);

    oof::platform::formbin::OneCContainer container;
    container.block_size = oof::platform::formbin::container_block_size;
    container.files.push_back({
        "form",
        container_times.form_created,
        container_times.form_modified,
        platform_form_listout_payload_bytes(result.payload),
    });
    container.files.push_back({
        "module",
        container_times.module_created,
        container_times.module_modified,
        {},
    });

    const std::vector<std::uint8_t> bytes = oof::platform::formbin::serialize_container(container);
    write_file_bytes(output_path, bytes);
    const auto reparsed = oof::platform::formbin::parse_container(bytes);
    const RuntimeFormEnvelope redump_envelope = runtime_envelope_from_form_payload(find_container_file(reparsed, "form").payload);
    const auto redump_object = materialize_platform_form_object(redump_envelope);

    std::cout << "{\"output\":";
    print_json_string(output_path);
    std::cout << ",\"operation\":\"platform-xsd-xml-build-formbin\"";
    std::cout << ",\"path\":\"platform-XSD-XML -> PlatformXdtoObject -> XSD schema-order ListOutStream -> Form.bin\"";
    std::cout << ",\"schemaOrderWriter\":\"PlatformXdtoObject\"";
    std::cout << ",\"publicOrdinaryFormXsdUsed\":false";
    std::cout << ",\"schemaObjects\":" << result.schema_objects;
    std::cout << ",\"schemaMembers\":" << result.schema_members;
    std::cout << ",\"bytes\":" << bytes.size();
    std::cout << ",\"containerTicksSource\":";
    print_json_string(container_times.source);
    std::cout << ",\"objects\":{\"items\":" << form_object.items.count()
              << ",\"attributes\":" << form_object.attributes.count()
              << ",\"commands\":" << form_object.commands.count()
              << ",\"events\":" << form_object.events.count()
              << ",\"edges\":" << form_object.edges.size() << "}";
    std::cout << ",\"redumpObjects\":{\"items\":" << redump_object.items.count()
              << ",\"attributes\":" << redump_object.attributes.count()
              << ",\"commands\":" << redump_object.commands.count()
              << ",\"events\":" << redump_object.events.count()
              << ",\"edges\":" << redump_object.edges.size() << "}";
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
    oof::ordinary::object::OrdinaryForm ordinary_form(materialize_platform_form_object(envelope));
    ordinary_form.set_prop_val(object_id, descriptor.name, std::string(new_value));
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
    const auto form_object = materialize_platform_form_object(envelope);
    const auto listout_coverage = ordinary_form_listout_coverage_summary(form_object);
    std::cout << "{\"source\":\"Form.bin:form\"";
    std::cout << ",\"publicContract\":\"OrdinaryForm\"";
    std::cout << ",\"objectModel\":\"PlatformFormObject\"";
    std::cout << ",\"nativeXmlProjection\":true";
    std::cout << ",\"nativeXmlWriter\":true";
    std::cout << ",\"supportedEditCodecs\":[\"Name\",\"Title\",\"Visible\",\"Enabled\",\"Position\",\"Binding:value\",\"Binding:anchor-list\",\"DimensionBinding:value\",\"DimensionBinding:record\",\"Attribute.Name\",\"Command.Name\",\"Command.Handler\",\"Command.ModifiesData\",\"Event.Handler\",\"DeleteLeafControl\",\"Form.bin.PlatformObject.getPropVal\",\"Form.bin.PlatformObject.setPropVal\"]";
    std::cout << ",\"materializedItems\":" << summary.items.size();
    std::cout << ",\"namedItems\":" << summary.named_items;
    std::cout << ",\"schemaBackedItems\":" << summary.schema_backed_items;
    print_ordinary_form_listout_coverage_json(listout_coverage);
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
    auto edited_form_object = materialize_platform_form_object(envelope);
    const auto result = apply_platform_object_edits_to_object(
        edited_form_object,
        public_xml_edits_to_platform_object_edits(edits));
    envelope.payload = platform_form_listout_payload(
        edited_form_object,
        object_property_value(edited_form_object.form, "Title"));
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
    auto anchor_edited_form_object = materialize_platform_form_object(anchor_envelope);
    const auto anchor_result = apply_platform_object_edits_to_object(
        anchor_edited_form_object,
        public_xml_edits_to_platform_object_edits(anchor_edits));
    anchor_envelope.payload = platform_form_listout_payload(
        anchor_edited_form_object,
        object_property_value(anchor_edited_form_object.form, "Title"));
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

struct ObjectBracketRoundtripResult {
    oof::platform::object_model::PlatformFormObject object1;
    oof::platform::object_model::PlatformFormObject object2;
    oof::platform::object_model::PlatformFormObject object3;
    std::string payload1_text;
    std::string payload2_text;
    std::string payload3_text;
    std::string signature1;
    std::string signature2;
    std::string signature3;
    OrdinaryFormListOutCoverageSummary listout_coverage;
};

ObjectBracketRoundtripResult object_bracket_roundtrip(const RuntimeFormEnvelope& envelope) {
    ObjectBracketRoundtripResult result;
    result.object1 = materialize_platform_form_object(envelope);
    result.payload1_text = oof::platform::stream::dump_compact(envelope.payload);
    result.signature1 = platform_form_object_signature(result.object1);

    RuntimeFormEnvelope envelope2 = envelope;
    envelope2.payload = platform_form_listout_payload(
        result.object1,
        object_property_value(result.object1.form, "Title"));
    result.payload2_text = oof::platform::stream::dump_compact(envelope2.payload);
    result.object2 = materialize_platform_form_object(envelope2);
    result.signature2 = platform_form_object_signature(result.object2);

    RuntimeFormEnvelope envelope3 = envelope2;
    envelope3.payload = platform_form_listout_payload(
        result.object2,
        object_property_value(result.object2.form, "Title"));
    result.payload3_text = oof::platform::stream::dump_compact(envelope3.payload);
    result.object3 = materialize_platform_form_object(envelope3);
    result.signature3 = platform_form_object_signature(result.object3);
    result.listout_coverage = ordinary_form_listout_coverage_summary(result.object1);
    return result;
}

ObjectBracketRoundtripResult empty_object_bracket_roundtrip(std::string title) {
    ObjectBracketRoundtripResult result;
    result.object1 = make_empty_platform_form_object(std::move(title));
    result.signature1 = platform_form_object_signature(result.object1);
    result.payload1_text = "";

    RuntimeFormEnvelope envelope2;
    envelope2.payload = platform_form_listout_payload(
        result.object1,
        object_property_value(result.object1.form, "Title"));
    result.payload2_text = oof::platform::stream::dump_compact(envelope2.payload);
    result.object2 = materialize_platform_form_object(envelope2);
    result.signature2 = platform_form_object_signature(result.object2);

    RuntimeFormEnvelope envelope3 = envelope2;
    envelope3.payload = platform_form_listout_payload(
        result.object2,
        object_property_value(result.object2.form, "Title"));
    result.payload3_text = oof::platform::stream::dump_compact(envelope3.payload);
    result.object3 = materialize_platform_form_object(envelope3);
    result.signature3 = platform_form_object_signature(result.object3);
    result.listout_coverage = ordinary_form_listout_coverage_summary(result.object2);
    return result;
}

void print_empty_form_object_roundtrip(std::string title) {
    const auto result = empty_object_bracket_roundtrip(std::move(title));
    const bool object_equal_after_first_write = result.signature1 == result.signature2;
    const bool object_stable_after_second_write = result.signature2 == result.signature3;
    const bool payload_stable_after_second_write = result.payload2_text == result.payload3_text;

    std::cout << "{\"source\":\"EmptyOrdinaryFormObject\"";
    std::cout << ",\"publicContract\":\"PlatformFormObject\"";
    std::cout << ",\"path\":\"PlatformFormObject -> bracket -> PlatformFormObject -> bracket\"";
    std::cout << ",\"objectOrigin\":\"empty-default-object\"";
    std::cout << ",\"objectCounts\":{\"items\":" << result.object1.items.count()
              << ",\"attributes\":" << result.object1.attributes.count()
              << ",\"commands\":" << result.object1.commands.count()
              << ",\"events\":" << result.object1.events.count()
              << ",\"edges\":" << result.object1.edges.size() << "}";
    std::cout << ",\"writtenObjectCounts\":{\"items\":" << result.object2.items.count()
              << ",\"attributes\":" << result.object2.attributes.count()
              << ",\"commands\":" << result.object2.commands.count()
              << ",\"events\":" << result.object2.events.count()
              << ",\"edges\":" << result.object2.edges.size() << "}";
    std::cout << ",\"firstWrittenPayloadBytes\":" << result.payload2_text.size();
    std::cout << ",\"secondWrittenPayloadBytes\":" << result.payload3_text.size();
    std::cout << ",\"objectSignatureEqualAfterFirstWrite\":"
              << (object_equal_after_first_write ? "true" : "false");
    std::cout << ",\"objectSignatureStableAfterSecondWrite\":"
              << (object_stable_after_second_write ? "true" : "false");
    std::cout << ",\"payloadStableAfterSecondWrite\":"
              << (payload_stable_after_second_write ? "true" : "false");
    print_ordinary_form_listout_coverage_json(result.listout_coverage);
    std::cout << "}\n";
}

void print_object_bracket_roundtrip_json(
    const RuntimeFormEnvelope& envelope,
    std::string_view source,
    std::size_t input_bytes
) {
    const auto result = object_bracket_roundtrip(envelope);
    const bool input_payload_equal = result.payload1_text == result.payload2_text;
    const bool object_equal_after_first_write = result.signature1 == result.signature2;
    const bool object_stable_after_second_write = result.signature2 == result.signature3;
    const bool payload_stable_after_second_write = result.payload2_text == result.payload3_text;

    std::cout << "{\"source\":";
    print_json_string(source);
    std::cout << ",\"publicContract\":\"PlatformFormObject\"";
    std::cout << ",\"path\":\"bracket -> PlatformFormObject -> bracket -> PlatformFormObject -> bracket\"";
    std::cout << ",\"inputBytes\":" << input_bytes;
    std::cout << ",\"runtimeUuid\":";
    print_json_string(envelope.runtime_uuid);
    std::cout << ",\"payloadRootVersion\":";
    print_json_string(envelope.payload.items.empty() ? "" : envelope.payload.items[0].atom);
    std::cout << ",\"objectCounts\":{\"items\":" << result.object1.items.count()
              << ",\"attributes\":" << result.object1.attributes.count()
              << ",\"commands\":" << result.object1.commands.count()
              << ",\"events\":" << result.object1.events.count()
              << ",\"edges\":" << result.object1.edges.size() << "}";
    std::cout << ",\"writtenObjectCounts\":{\"items\":" << result.object2.items.count()
              << ",\"attributes\":" << result.object2.attributes.count()
              << ",\"commands\":" << result.object2.commands.count()
              << ",\"events\":" << result.object2.events.count()
              << ",\"edges\":" << result.object2.edges.size() << "}";
    std::cout << ",\"inputPayloadBytes\":" << result.payload1_text.size();
    std::cout << ",\"firstWrittenPayloadBytes\":" << result.payload2_text.size();
    std::cout << ",\"secondWrittenPayloadBytes\":" << result.payload3_text.size();
    std::cout << ",\"inputPayloadEqualAfterObjectWrite\":"
              << (input_payload_equal ? "true" : "false");
    std::cout << ",\"objectSignatureEqualAfterFirstWrite\":"
              << (object_equal_after_first_write ? "true" : "false");
    std::cout << ",\"objectSignatureStableAfterSecondWrite\":"
              << (object_stable_after_second_write ? "true" : "false");
    std::cout << ",\"payloadStableAfterSecondWrite\":"
              << (payload_stable_after_second_write ? "true" : "false");
    print_ordinary_form_listout_coverage_json(result.listout_coverage);
    std::cout << "}\n";
}

void print_runtime_form_object_roundtrip(const std::string& path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(path, canonical_text);
    print_object_bracket_roundtrip_json(envelope, "RuntimeForm:payload", canonical_text.size());
}

void print_formbin_object_roundtrip(const std::string& path) {
    const auto input_bytes = read_file_bytes(path);
    const auto container = oof::platform::formbin::parse_container(input_bytes);
    const auto& form_file = find_container_file(container, "form");
    RuntimeFormEnvelope envelope = runtime_envelope_from_form_payload(form_file.payload);
    print_object_bracket_roundtrip_json(envelope, "Form.bin:form", form_file.payload.size());
}

struct ObjectRoundtripDiffDetail {
    std::string kind;
    std::string object_id;
    std::string object_name;
    std::string platform_type;
    std::string property;
    std::string left;
    std::string right;
};

struct ObjectRoundtripDiffSummary {
    std::size_t missing_objects = 0;
    std::size_t added_objects = 0;
    std::size_t changed_identity = 0;
    std::size_t missing_properties = 0;
    std::size_t added_properties = 0;
    std::size_t changed_properties = 0;
    std::size_t changed_collections = 0;
    std::size_t changed_children = 0;
    std::size_t missing_edges = 0;
    std::size_t added_edges = 0;
    std::map<std::string, std::size_t> by_kind;
    std::map<std::string, std::size_t> by_platform_type;
    std::map<std::string, std::size_t> by_property;
    std::vector<ObjectRoundtripDiffDetail> details;
};

std::string property_compare_value(
    const oof::platform::object_model::PlatformObjectProperty& property
) {
    std::ostringstream out;
    out << property.value
        << "|type=" << property.value_type
        << "|default=" << property.default_value
        << "|origin=" << property.value_origin
        << "|member=" << property.platform_member
        << "|slot=" << property.slot_binding
        << "|codec=" << property.slot_codec
        << "|valueObject=" << property.value_object_class
        << "|literal=" << property.value_object_literal
        << "|schema=" << property.value_object_schema_value
        << "|stream=" << property.value_object_list_stream;
    return out.str();
}

std::string object_identity_compare_value(
    const oof::platform::object_model::PlatformObject& object
) {
    std::ostringstream out;
    out << "name=" << object.name
        << "|type=" << object.platform_type
        << "|parent=" << object.parent_object_id
        << "|path=" << object.path
        << "|publicId=" << object.identity.public_id
        << "|platformObjectId=" << object.identity.platform_object_id
        << "|compositeId=" << object.identity.composite_id
        << "|uuid=" << object.identity.uuid
        << "|classGuid=" << object.identity.class_guid
        << "|streamElement=" << object.identity.stream_element;
    return out.str();
}

std::string object_children_compare_value(
    const oof::platform::object_model::PlatformObject& object
) {
    std::ostringstream out;
    for (const auto child : object.children) {
        out << child << ",";
    }
    return out.str();
}

std::map<std::string, std::string> object_property_map(
    const oof::platform::object_model::PlatformObject& object
) {
    std::map<std::string, std::string> values;
    for (const auto& property : object.properties) {
        values[property.name.empty() ? property.localized_name : property.name] = property_compare_value(property);
    }
    return values;
}

std::map<std::string, std::string> object_collection_map(
    const oof::platform::object_model::PlatformObject& object
) {
    std::map<std::string, std::string> values;
    for (const auto& collection : object.collections) {
        values[collection.name] =
            collection.localized_name + "|" +
            collection.value_type + "|" +
            std::to_string(collection.count) + "|" +
            collection.slot_binding + "|" +
            collection.slot_codec;
    }
    return values;
}

void add_object_diff_detail(
    ObjectRoundtripDiffSummary& summary,
    std::string kind,
    const oof::platform::object_model::PlatformObject* object,
    std::string property,
    std::string left,
    std::string right
) {
    ++summary.by_kind[kind];
    if (object != nullptr) {
        ++summary.by_platform_type[object->platform_type.empty() ? "<none>" : object->platform_type];
    }
    if (!property.empty()) {
        ++summary.by_property[property];
    }
    if (summary.details.size() >= 80) {
        return;
    }
    ObjectRoundtripDiffDetail detail;
    detail.kind = std::move(kind);
    if (object != nullptr) {
        detail.object_id = object->object_id;
        detail.object_name = object->name;
        detail.platform_type = object->platform_type;
    }
    detail.property = std::move(property);
    detail.left = std::move(left);
    detail.right = std::move(right);
    summary.details.push_back(std::move(detail));
}

std::map<std::string, const oof::platform::object_model::PlatformObject*> platform_object_index(
    const oof::platform::object_model::PlatformFormObject& form_object
) {
    std::map<std::string, const oof::platform::object_model::PlatformObject*> objects;
    objects["form:0"] = &form_object.form;
    for (const auto& object : form_object.items.objects()) {
        objects["item:" + object.object_id] = &object;
    }
    for (const auto& object : form_object.attributes.objects()) {
        objects["attribute:" + object.object_id] = &object;
    }
    for (const auto& object : form_object.commands.objects()) {
        objects["command:" + object.object_id] = &object;
    }
    for (const auto& object : form_object.events.objects()) {
        objects["event:" + object.object_id] = &object;
    }
    return objects;
}

std::set<std::string> platform_edge_set(
    const oof::platform::object_model::PlatformFormObject& form_object
) {
    std::set<std::string> edges;
    for (const auto& edge : form_object.edges) {
        edges.insert(edge.kind + "|" + edge.from_object_id + "|" + edge.to_object_id + "|" + edge.role + "|" + edge.name);
    }
    return edges;
}

void compare_named_value_maps(
    ObjectRoundtripDiffSummary& summary,
    const oof::platform::object_model::PlatformObject& left_object,
    const std::map<std::string, std::string>& left,
    const std::map<std::string, std::string>& right,
    std::string_view missing_kind,
    std::string_view added_kind,
    std::string_view changed_kind,
    std::size_t& missing_count,
    std::size_t& added_count,
    std::size_t& changed_count
) {
    for (const auto& [name, left_value] : left) {
        const auto right_it = right.find(name);
        if (right_it == right.end()) {
            ++missing_count;
            add_object_diff_detail(summary, std::string(missing_kind), &left_object, name, left_value, "");
            continue;
        }
        if (left_value != right_it->second) {
            ++changed_count;
            add_object_diff_detail(summary, std::string(changed_kind), &left_object, name, left_value, right_it->second);
        }
    }
    for (const auto& [name, right_value] : right) {
        if (left.find(name) == left.end()) {
            ++added_count;
            add_object_diff_detail(summary, std::string(added_kind), &left_object, name, "", right_value);
        }
    }
}

ObjectRoundtripDiffSummary diff_platform_form_objects(
    const oof::platform::object_model::PlatformFormObject& left,
    const oof::platform::object_model::PlatformFormObject& right
) {
    ObjectRoundtripDiffSummary summary;
    const auto left_objects = platform_object_index(left);
    const auto right_objects = platform_object_index(right);
    for (const auto& [key, left_object] : left_objects) {
        const auto right_it = right_objects.find(key);
        if (right_it == right_objects.end()) {
            ++summary.missing_objects;
            add_object_diff_detail(summary, "missing-object", left_object, key, object_identity_compare_value(*left_object), "");
            continue;
        }
        const auto* right_object = right_it->second;
        const std::string left_identity = object_identity_compare_value(*left_object);
        const std::string right_identity = object_identity_compare_value(*right_object);
        if (left_identity != right_identity) {
            ++summary.changed_identity;
            add_object_diff_detail(summary, "changed-identity", left_object, "", left_identity, right_identity);
        }
        const std::string left_children = object_children_compare_value(*left_object);
        const std::string right_children = object_children_compare_value(*right_object);
        if (left_children != right_children) {
            ++summary.changed_children;
            add_object_diff_detail(summary, "changed-children", left_object, "ChildItems", left_children, right_children);
        }
        compare_named_value_maps(
            summary,
            *left_object,
            object_property_map(*left_object),
            object_property_map(*right_object),
            "missing-property",
            "added-property",
            "changed-property",
            summary.missing_properties,
            summary.added_properties,
            summary.changed_properties);
        std::size_t missing_collections = 0;
        std::size_t added_collections = 0;
        compare_named_value_maps(
            summary,
            *left_object,
            object_collection_map(*left_object),
            object_collection_map(*right_object),
            "missing-collection",
            "added-collection",
            "changed-collection",
            missing_collections,
            added_collections,
            summary.changed_collections);
        summary.changed_collections += missing_collections + added_collections;
    }
    for (const auto& [key, right_object] : right_objects) {
        if (left_objects.find(key) == left_objects.end()) {
            ++summary.added_objects;
            add_object_diff_detail(summary, "added-object", right_object, key, "", object_identity_compare_value(*right_object));
        }
    }
    const auto left_edges = platform_edge_set(left);
    const auto right_edges = platform_edge_set(right);
    for (const auto& edge : left_edges) {
        if (right_edges.find(edge) == right_edges.end()) {
            ++summary.missing_edges;
            add_object_diff_detail(summary, "missing-edge", nullptr, "Edges", edge, "");
        }
    }
    for (const auto& edge : right_edges) {
        if (left_edges.find(edge) == left_edges.end()) {
            ++summary.added_edges;
            add_object_diff_detail(summary, "added-edge", nullptr, "Edges", "", edge);
        }
    }
    return summary;
}

void print_frequency_json(const std::map<std::string, std::size_t>& values) {
    std::cout << "[";
    std::size_t index = 0;
    for (const auto& [name, count] : values) {
        if (index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"name\":";
        print_json_string(name);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "]";
}

void print_object_roundtrip_diff_json(
    const RuntimeFormEnvelope& envelope,
    std::string_view source
) {
    const auto result = object_bracket_roundtrip(envelope);
    const auto first_diff = diff_platform_form_objects(result.object1, result.object2);
    const auto stability_diff = diff_platform_form_objects(result.object2, result.object3);
    std::cout << "{\"source\":";
    print_json_string(source);
    std::cout << ",\"path\":\"bracket -> PlatformFormObject -> bracket -> PlatformFormObject\"";
    std::cout << ",\"objectSignatureEqualAfterFirstWrite\":"
              << (result.signature1 == result.signature2 ? "true" : "false");
    std::cout << ",\"objectSignatureStableAfterSecondWrite\":"
              << (result.signature2 == result.signature3 ? "true" : "false");
    std::cout << ",\"payloadStableAfterSecondWrite\":"
              << (result.payload2_text == result.payload3_text ? "true" : "false");
    print_ordinary_form_listout_coverage_json(result.listout_coverage);
    std::cout << ",\"firstWriteDiff\":{\"missingObjects\":" << first_diff.missing_objects
              << ",\"addedObjects\":" << first_diff.added_objects
              << ",\"changedIdentity\":" << first_diff.changed_identity
              << ",\"missingProperties\":" << first_diff.missing_properties
              << ",\"addedProperties\":" << first_diff.added_properties
              << ",\"changedProperties\":" << first_diff.changed_properties
              << ",\"changedCollections\":" << first_diff.changed_collections
              << ",\"changedChildren\":" << first_diff.changed_children
              << ",\"missingEdges\":" << first_diff.missing_edges
              << ",\"addedEdges\":" << first_diff.added_edges;
    std::cout << ",\"byKind\":";
    print_frequency_json(first_diff.by_kind);
    std::cout << ",\"byPlatformType\":";
    print_frequency_json(first_diff.by_platform_type);
    std::cout << ",\"byProperty\":";
    print_frequency_json(first_diff.by_property);
    std::cout << ",\"details\":[";
    for (std::size_t index = 0; index < first_diff.details.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& detail = first_diff.details[index];
        std::cout << "{\"kind\":";
        print_json_string(detail.kind);
        std::cout << ",\"objectId\":";
        print_json_string(detail.object_id);
        std::cout << ",\"objectName\":";
        print_json_string(detail.object_name);
        std::cout << ",\"platformType\":";
        print_json_string(detail.platform_type);
        std::cout << ",\"property\":";
        print_json_string(detail.property);
        std::cout << ",\"left\":";
        print_json_string(detail.left);
        std::cout << ",\"right\":";
        print_json_string(detail.right);
        std::cout << "}";
    }
    std::cout << "]}";
    std::cout << ",\"secondWriteDiffCounts\":{\"missingObjects\":" << stability_diff.missing_objects
              << ",\"addedObjects\":" << stability_diff.added_objects
              << ",\"changedIdentity\":" << stability_diff.changed_identity
              << ",\"missingProperties\":" << stability_diff.missing_properties
              << ",\"addedProperties\":" << stability_diff.added_properties
              << ",\"changedProperties\":" << stability_diff.changed_properties
              << ",\"changedCollections\":" << stability_diff.changed_collections
              << ",\"changedChildren\":" << stability_diff.changed_children
              << ",\"missingEdges\":" << stability_diff.missing_edges
              << ",\"addedEdges\":" << stability_diff.added_edges << "}";
    std::cout << "}\n";
}

void print_runtime_form_object_roundtrip_diff(const std::string& path) {
    std::string canonical_text;
    RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(path, canonical_text);
    print_object_roundtrip_diff_json(envelope, "RuntimeForm:payload");
}

void print_formbin_object_roundtrip_diff(const std::string& path) {
    RuntimeFormEnvelope envelope = read_formbin_runtime_envelope(path);
    print_object_roundtrip_diff_json(envelope, "Form.bin:form");
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
    for (std::size_t index = 0; index < value.items.size(); ++index) {
        auto& item = value.items[index];
        if (!item.is_list && item.atom.rfind("#base64:", 0) == 0) {
            item.atom = std::string(payload);
            item.atom_kind = oof::platform::stream::ListValue::AtomKind::raw;
            std::size_t erase_end = index + 1;
            while (erase_end < value.items.size() && !value.items[erase_end].is_list) {
                ++erase_end;
            }
            value.items.erase(value.items.begin() + static_cast<std::ptrdiff_t>(index + 1),
                              value.items.begin() + static_cast<std::ptrdiff_t>(erase_end));
            return true;
        }
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

oof::platform::stream::ListValue control_info_slot_value_from_public_xml(
    std::string_view property_name,
    std::string_view value
) {
    if (property_name == "State1" || property_name == "State2") {
        std::string payload(value);
        if (payload.rfind("#base64:", 0) != 0 && payload.find('{') == std::string::npos) {
            payload = "#base64:" + payload;
        }
        if (!payload.empty() && payload.front() == '{') {
            return oof::platform::stream::parse(payload);
        }
        return oof::platform::stream::ListValue::list({
            oof::platform::stream::ListValue::raw_atom(std::move(payload)),
        });
    }
    if (!value.empty() && value.front() == '{') {
        return oof::platform::stream::parse(std::string(value));
    }
    const auto* property_descriptor = oof::platform::property_registry::find_descriptor(property_name);
    if (property_descriptor != nullptr && property_descriptor->value_type == "Boolean") {
        return oof::platform::stream::ListValue::raw_atom(platform_bool_atom(value));
    }
    return oof::platform::stream::ListValue::raw_atom(std::string(value));
}

bool set_materialized_object_control_info_slot(
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
        value.items[1].atom == object_id &&
        value.items.size() > 2 &&
        value.items[2].is_list) {
        const auto* binding = oof::ordinary::control_type::binding_for_guid(value.items[0].atom);
        if (binding == nullptr) {
            return false;
        }
        const auto* descriptor = oof::platform::control_info::descriptor_for_control_type(binding->writer_control_type);
        if (descriptor == nullptr) {
            return false;
        }
        const auto slot = oof::platform::control_info::slot_index(*descriptor, property_name);
        auto* info = mutable_control_info_slot_body(value);
        if (info == nullptr || !slot.has_value() || info->items.size() <= *slot) {
            return false;
        }
        info->items[*slot] = control_info_slot_value_from_public_xml(property_name, new_value);
        return true;
    }
    for (auto& item : value.items) {
        if (set_materialized_object_control_info_slot(item, object_id, property_name, new_value)) {
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

struct ParsedCommandObjectId {
    std::string command_id;
    std::optional<std::size_t> ordinal;
};

std::optional<ParsedCommandObjectId> parse_command_object_id(std::string_view object_id) {
    constexpr std::string_view prefix = "command:";
    if (object_id.size() <= prefix.size() || object_id.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }
    std::string rest(object_id.substr(prefix.size()));
    ParsedCommandObjectId parsed;
    const std::size_t last_separator = rest.rfind(':');
    if (last_separator != std::string::npos && last_separator + 1 < rest.size()) {
        const std::string ordinal_text = rest.substr(last_separator + 1);
        const bool digits_only = std::all_of(
            ordinal_text.begin(),
            ordinal_text.end(),
            [](unsigned char ch) { return std::isdigit(ch) != 0; });
        if (digits_only) {
            parsed.command_id = rest.substr(0, last_separator);
            parsed.ordinal = static_cast<std::size_t>(std::stoull(ordinal_text));
        }
    }
    if (parsed.command_id.empty()) {
        parsed.command_id = std::move(rest);
    }
    return parsed;
}

bool set_materialized_command_property_in_block(
    oof::platform::stream::ListValue& block,
    const ParsedCommandObjectId& wanted,
    std::string_view property_name,
    std::string_view new_value,
    std::map<std::string, std::size_t>& ordinals
) {
    if (!block.is_list || is_materialized_form_property_block(block)) {
        return false;
    }
    if (looks_like_materialized_form_command_record(block)) {
        const std::string command_id = command_record_id_value(block);
        const bool visible_command = !command_id.empty() &&
            (!command_record_scalar_value(block, 2, 1).empty() ||
             !command_record_scalar_value(block, 3, 2).empty() ||
             !command_record_scalar_value(block, 4, 3).empty());
        if (visible_command) {
            const std::size_t ordinal = ordinals[command_id]++;
            if (command_id == wanted.command_id && (!wanted.ordinal.has_value() || *wanted.ordinal == ordinal)) {
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
    }
    for (auto& item : block.items) {
        if (set_materialized_command_property_in_block(item, wanted, property_name, new_value, ordinals)) {
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
    const auto wanted = parse_command_object_id(object_id);
    if (!wanted.has_value()) {
        return false;
    }
    if (!payload.is_list || payload.items.size() <= 2) {
        return false;
    }
    std::map<std::string, std::size_t> ordinals;
    for (std::size_t index = 2; index < payload.items.size(); ++index) {
        if (set_materialized_command_property_in_block(payload.items[index], *wanted, property_name, new_value, ordinals)) {
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
    struct ParsedEventObjectId {
        std::string owner_object_id;
        std::string event_id;
        std::optional<std::size_t> ordinal;
    };
    auto parse_event_object_id = [](std::string_view value) -> std::optional<ParsedEventObjectId> {
        constexpr std::string_view prefix = "event:";
        if (value.size() <= prefix.size() || value.substr(0, prefix.size()) != prefix) {
            return std::nullopt;
        }
        std::string rest(value.substr(prefix.size()));
        const std::size_t first_separator = rest.find(':');
        if (first_separator == std::string::npos || first_separator == 0 || first_separator + 1 >= rest.size()) {
            return std::nullopt;
        }
        ParsedEventObjectId parsed;
        parsed.owner_object_id = rest.substr(0, first_separator);
        std::string event_and_ordinal = rest.substr(first_separator + 1);
        const std::size_t last_separator = event_and_ordinal.rfind(':');
        if (last_separator != std::string::npos && last_separator + 1 < event_and_ordinal.size()) {
            const std::string ordinal_text = event_and_ordinal.substr(last_separator + 1);
            const bool digits_only = std::all_of(
                ordinal_text.begin(),
                ordinal_text.end(),
                [](unsigned char ch) { return std::isdigit(ch) != 0; });
            if (digits_only) {
                parsed.event_id = event_and_ordinal.substr(0, last_separator);
                parsed.ordinal = static_cast<std::size_t>(std::stoull(ordinal_text));
            }
        }
        if (parsed.event_id.empty()) {
            parsed.event_id = std::move(event_and_ordinal);
        }
        return parsed;
    };
    auto set_event_record = [](
        oof::platform::stream::ListValue& record,
        const ParsedEventObjectId& parsed,
        std::string_view replacement,
        std::map<std::string, std::size_t>& ordinals,
        const auto& self
    ) -> bool {
        if (!record.is_list) {
            return false;
        }
        if (looks_like_materialized_form_event_record(record)) {
            const std::string event_id = oof::platform::stream::dump_compact(record.items[1]);
            const std::string handler = !record.items[2].is_list ? record.items[2].atom : "";
            if (!event_id.empty() && !handler.empty()) {
                const std::size_t ordinal = ordinals[event_id]++;
                if (event_id == parsed.event_id && (!parsed.ordinal.has_value() || *parsed.ordinal == ordinal)) {
                    record.items[2].atom = std::string(replacement);
                    record.items[2].atom_kind = oof::platform::stream::ListValue::AtomKind::string;
                    return true;
                }
            }
            return false;
        }
        if (looks_like_platform_element_event_record(record)) {
            const std::string& event_id = record.items[1].atom;
            const std::string handler = record.items[2].items[1].atom;
            if (!event_id.empty() && !handler.empty()) {
                const std::size_t ordinal = ordinals[event_id]++;
                if (event_id == parsed.event_id && (!parsed.ordinal.has_value() || *parsed.ordinal == ordinal)) {
                    record.items[2].items[1].atom = std::string(replacement);
                    record.items[2].items[1].atom_kind = oof::platform::stream::ListValue::AtomKind::string;
                    return true;
                }
            }
            return false;
        }
        for (auto& item : record.items) {
            if (self(item, parsed, replacement, ordinals, self)) {
                return true;
            }
        }
        return false;
    };
    auto set_event_in_owner = [&](oof::platform::stream::ListValue& owner, const ParsedEventObjectId& parsed) -> bool {
        std::map<std::string, std::size_t> ordinals;
        if (owner.items.size() > 2 && set_event_record(owner.items[2], parsed, new_value, ordinals, set_event_record)) {
            return true;
        }
        if (owner.items.size() > 3 && set_event_record(owner.items[3], parsed, new_value, ordinals, set_event_record)) {
            return true;
        }
        return false;
    };
    auto find_owner_and_set = [&](oof::platform::stream::ListValue& node, const ParsedEventObjectId& parsed, const auto& self) -> bool {
        if (!node.is_list) {
            return false;
        }
        if (is_materializable_object_candidate(node) &&
            node.items.size() > 1 &&
            !node.items[1].is_list &&
            node.items[1].atom == parsed.owner_object_id) {
            return set_event_in_owner(node, parsed);
        }
        for (auto& item : node.items) {
            if (self(item, parsed, self)) {
                return true;
            }
        }
        return false;
    };
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
    const auto parsed = parse_event_object_id(object_id);
    if (!parsed.has_value()) {
        return false;
    }
    if (parsed->owner_object_id == "0") {
        std::map<std::string, std::size_t> ordinals;
        return set_event_record(value, *parsed, new_value, ordinals, set_event_record);
    }
    return find_owner_and_set(value, *parsed, find_owner_and_set);
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
        if (!edit.table_columns_xml.empty()) {
            object.set_property("TableColumnsXml", edit.table_columns_xml);
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

void set_existing_platform_object_property_value(
    oof::platform::object_model::PlatformObject& object,
    const oof::platform::object_model::PlatformObjectPropertyEdit& edit_property
) {
    auto* property = object.property(edit_property.name);
    if (property == nullptr) {
        throw std::runtime_error("PlatformObject property is not materialized: object=" +
                                 object.object_id + " property=" + edit_property.name);
    }
    property->value = edit_property.value;
    property->value_origin = "public-xml";
    enrich_platform_value_object(*property);
    if (property->name == "Name") {
        object.name = property->value;
    }
}

PublicXmlApplyResult apply_platform_object_edits_to_object(
    oof::platform::object_model::PlatformFormObject& form_object,
    const oof::platform::object_model::PlatformFormObjectEdit& object_edit
) {
    PublicXmlApplyResult result;
    for (const auto& edit_object : object_edit.objects) {
        if (edit_object.properties.empty()) {
            continue;
        }
        auto* object = form_object.find_object_by_id(edit_object.object_id);
        if (object == nullptr) {
            throw std::runtime_error("PlatformObject edit target is not materialized: object=" +
                                     edit_object.object_id);
        }
        const bool attribute_object = string_view_starts_with(edit_object.object_id, "attribute:");
        const bool command_object = string_view_starts_with(edit_object.object_id, "command:");
        const bool event_object = string_view_starts_with(edit_object.object_id, "event:");
        if (!attribute_object && !command_object && !event_object) {
            ++result.controls;
        }
        for (const auto& property : edit_object.properties) {
            set_existing_platform_object_property_value(*object, property);
            if (attribute_object) {
                ++result.attribute_edits;
            } else if (command_object) {
                ++result.command_edits;
            } else if (event_object) {
                ++result.event_edits;
            } else if (property.name == "Name") {
                ++result.name_edits;
            } else if (property.name == "Title") {
                ++result.title_edits;
            } else if (property.name == "Left") {
                ++result.position_edits;
            } else if (property.name.rfind("DimensionBinding.", 0) == 0) {
                ++result.dimension_binding_edits;
            } else if (property.name.rfind("Binding.", 0) == 0) {
                ++result.binding_edits;
            } else {
                const auto& descriptor = require_property_descriptor(property.name);
                if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::scalar_flag) {
                    ++result.scalar_flag_edits;
                }
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
    if (descriptor.slot_codec == oof::platform::property_registry::SlotCodec::control_info_slot) {
        return set_materialized_object_control_info_slot(payload, object_id, descriptor.name, new_value);
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
    oof::ordinary::object::OrdinaryForm ordinary_form(materialize_platform_form_object(envelope));
    ordinary_form.set_prop_val(object_id, descriptor.name, std::string(new_value));
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

void print_ordinary_form_concepts() {
    const auto concepts = oof::ordinary::concept_registry::build_concepts();
    const auto stats = oof::ordinary::concept_registry::stats_for(concepts);
    std::cout << "{\"source\":\"OrdinaryFormConceptRegistry\"";
    std::cout << ",\"owner\":\"OrdinaryForm object model\"";
    std::cout << ",\"decisionBoundary\":\"sources are evidence adapters; accepted concepts own public XML and serializer admission\"";
    std::cout << ",\"conceptCount\":" << stats.total;
    std::cout << ",\"controlConceptCount\":" << stats.controls;
    std::cout << ",\"propertyConceptCount\":" << stats.properties;
    std::cout << ",\"acceptedCount\":" << stats.accepted;
    std::cout << ",\"proposedCount\":" << stats.proposed;
    std::cout << ",\"diagnosticCount\":" << stats.diagnostic;
    std::cout << ",\"rejectedCount\":" << stats.rejected;
    std::cout << ",\"concepts\":[";
    for (std::size_t index = 0; index < concepts.size(); ++index) {
        const auto& concept = concepts[index];
        if (index != 0) {
            std::cout << ",";
        }
        std::cout << "{\"kind\":";
        print_json_string(oof::ordinary::concept_registry::concept_kind_name(concept.kind));
        std::cout << ",\"status\":";
        print_json_string(oof::ordinary::concept_registry::concept_status_name(concept.status));
        std::cout << ",\"publicName\":";
        print_json_string(concept.public_name);
        std::cout << ",\"apiName\":";
        print_json_string(concept.api_name);
        std::cout << ",\"runtimeIdentity\":";
        print_json_string(concept.runtime_identity);
        std::cout << ",\"storageBinding\":";
        print_json_string(concept.storage_binding);
        std::cout << ",\"codec\":";
        print_json_string(concept.codec);
        std::cout << ",\"readable\":" << (concept.readable ? "true" : "false");
        std::cout << ",\"writable\":" << (concept.writable ? "true" : "false");
        std::cout << ",\"proof\":";
        print_json_string(concept.proof);
        std::cout << "}";
    }
    std::cout << "]}\n";
}

bool object_model_gate_is_unproven_value_codec(std::string_view codec) {
    return codec == "color-record" ||
           codec == "font-record" ||
           codec == "picture-record" ||
           codec == "border-record";
}

bool object_model_gate_is_pending_status(std::string_view status) {
    return status.find("pending") != std::string_view::npos ||
           status.find("unproven") != std::string_view::npos ||
           status.find("unknown-residue") != std::string_view::npos;
}

bool object_model_gate_has_raw_public_name(std::string_view name) {
    static constexpr std::array<std::string_view, 8> forbidden{
        "SerializationProfile",
        "RawBracket",
        "BracketStream",
        "ListStream",
        "FormBin",
        "ObjectModel",
        "LogicalStream",
        "PlatformRecords",
    };
    for (const auto token : forbidden) {
        if (name.find(token) != std::string_view::npos) {
            return true;
        }
    }
    const std::regex slotn(R"(\bslot\d+\b)", std::regex_constants::icase);
    return std::regex_search(std::string(name), slotn);
}

bool object_model_gate_can_write_codec(std::string_view codec) {
    return codec == "name-record" ||
           codec == "scalar-flag" ||
           codec == "position-record" ||
           codec == "binding-record" ||
           codec == "control-info-slot" ||
           codec == "attribute-record" ||
           codec == "command-record" ||
           codec == "event-action-record";
}

void object_model_gate_add_violation(
    std::vector<std::string>& violations,
    std::string_view label,
    std::string_view name,
    std::string_view message
) {
    violations.push_back(std::string(label) + " " + std::string(name) + ": " + std::string(message));
}

void object_model_gate_check_member(
    std::vector<std::string>& violations,
    std::string_view label,
    std::string_view name,
    std::string_view codec,
    std::string_view codec_status,
    bool writable
) {
    if (object_model_gate_has_raw_public_name(name)) {
        object_model_gate_add_violation(violations, label, name, "raw-shape vocabulary in public name");
    }
    if (!writable) {
        return;
    }
    if (object_model_gate_is_unproven_value_codec(codec)) {
        object_model_gate_add_violation(violations, label, name, "writable unproven value codec " + std::string(codec));
    } else if (!object_model_gate_can_write_codec(codec)) {
        object_model_gate_add_violation(violations, label, name, "writable codec is outside current native setPropVal surface: " + std::string(codec));
    }
    if (object_model_gate_is_pending_status(codec_status)) {
        object_model_gate_add_violation(violations, label, name, "writable codec status is not proven: " + std::string(codec_status));
    }
}

std::size_t count_substring(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return 0;
    }
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

struct PublicSchemaEscapeGate {
    bool inspected = false;
    std::size_t any_object_type = 0;
    std::size_t xs_any = 0;
    std::size_t xs_any_attribute = 0;
    std::size_t stable_object_types = 0;
    std::size_t stable_object_type_requirements = 0;
    std::size_t input_field_property_types = 0;
    std::size_t input_field_property_type_requirements = 0;

    std::size_t total() const {
        return any_object_type + xs_any + xs_any_attribute;
    }
};

PublicSchemaEscapeGate inspect_public_schema_escape_gate() {
    PublicSchemaEscapeGate gate;
    const std::array<std::filesystem::path, 3> candidates{{
        "schemas/OrdinaryForm.xsd",
        "../../schemas/OrdinaryForm.xsd",
        "../../../schemas/OrdinaryForm.xsd",
    }};
    for (const auto& path : candidates) {
        if (!std::filesystem::is_regular_file(path)) {
            continue;
        }
        const std::string text = read_file_text_lossy(path.string());
        gate.inspected = true;
        gate.any_object_type = count_substring(text, "AnyObjectType");
        gate.xs_any = count_substring(text, "<xs:any ");
        gate.xs_any_attribute = count_substring(text, "<xs:anyAttribute");
        const std::array<std::string_view, 8> stable_object_type_anchors{{
            "<xs:element name=\"Position\" type=\"PositionType\"",
            "<xs:element name=\"Bindings\" type=\"BindingsType\"",
            "<xs:element name=\"Attribute\" type=\"AttributeType\"",
            "<xs:element name=\"Command\" type=\"CommandType\"",
            "<xs:element name=\"TypePattern\" type=\"TypePatternType\"",
            "<xs:complexType name=\"PositionType\"",
            "<xs:complexType name=\"BindingsType\"",
            "<xs:complexType name=\"TypePatternType\"",
        }};
        gate.stable_object_type_requirements = stable_object_type_anchors.size();
        for (const auto anchor : stable_object_type_anchors) {
            if (text.find(anchor) != std::string::npos) {
                ++gate.stable_object_types;
            }
        }
        const std::array<std::string_view, 6> input_field_property_type_anchors{{
            "<xs:element name=\"DataPath\" type=\"StringPropertyType\"",
            "<xs:element name=\"Enabled\" type=\"BooleanPropertyType\"",
            "<xs:element name=\"Position\" type=\"PositionType\"",
            "<xs:element name=\"ReadOnly\" type=\"BooleanPropertyType\"",
            "<xs:element name=\"Title\" type=\"StringPropertyType\"",
            "<xs:element name=\"Visible\" type=\"BooleanPropertyType\"",
        }};
        gate.input_field_property_type_requirements = input_field_property_type_anchors.size();
        for (const auto anchor : input_field_property_type_anchors) {
            if (text.find(anchor) != std::string::npos) {
                ++gate.input_field_property_types;
            }
        }
        return gate;
    }
    return gate;
}

void print_object_model_gate() {
    std::vector<std::string> violations;
    std::size_t schema_members_checked = 0;
    std::size_t registry_descriptors_checked = 0;
    std::size_t writable_value_codecs = 0;
    std::size_t readable_value_coverage_gaps = 0;
    std::size_t accepted_concept_incomplete_rows = 0;

    const auto concepts = oof::ordinary::concept_registry::build_concepts();
    const auto concept_stats = oof::ordinary::concept_registry::stats_for(concepts);
    for (const auto& concept : concepts) {
        if (concept.status != oof::ordinary::concept_registry::ConceptStatus::accepted) {
            continue;
        }
        const bool complete =
            !concept.public_name.empty() &&
            !concept.runtime_identity.empty() &&
            !concept.storage_binding.empty() &&
            !concept.codec.empty() &&
            !concept.proof.empty();
        if (!complete) {
            ++accepted_concept_incomplete_rows;
            object_model_gate_add_violation(
                violations,
                "concept",
                concept.public_name,
                "accepted concept has incomplete admission passport");
        }
    }

    const auto schemas = oof::platform::object_schema::build_platform_object_schemas();
    for (const auto& schema : schemas) {
        const std::string label = "schema[" + schema.type_name + "]";
        for (const auto& member : schema.xsd_members) {
            ++schema_members_checked;
            if (object_model_gate_is_unproven_value_codec(member.slot_codec)) {
                if (member.writable) {
                    ++writable_value_codecs;
                } else {
                    ++readable_value_coverage_gaps;
                }
            }
            object_model_gate_check_member(
                violations,
                label,
                member.name,
                member.slot_codec,
                member.codec_status,
                member.writable);
        }
    }

    const auto& generated_descriptors = oof::platform::property_registry::generated_api_descriptors();
    const auto check_descriptor = [&](const oof::platform::property_registry::PlatformPropertyDescriptor& descriptor) {
        ++registry_descriptors_checked;
        object_model_gate_check_member(
            violations,
            "registry",
            descriptor.name,
            oof::platform::property_registry::slot_codec_name(descriptor.slot_codec),
            "",
            descriptor.writable);
    };
    for (const auto& descriptor : oof::platform::property_registry::descriptors) {
        check_descriptor(descriptor);
    }
    for (const auto& descriptor : generated_descriptors) {
        check_descriptor(descriptor);
    }

    const auto public_schema_gate = inspect_public_schema_escape_gate();

    std::cout << "{\"operation\":\"object-model-gate\"";
    std::cout << ",\"conceptOwner\":\"OrdinaryFormConceptRegistry\"";
    std::cout << ",\"conceptCount\":" << concept_stats.total;
    std::cout << ",\"acceptedConcepts\":" << concept_stats.accepted;
    std::cout << ",\"proposedConcepts\":" << concept_stats.proposed;
    std::cout << ",\"diagnosticConcepts\":" << concept_stats.diagnostic;
    std::cout << ",\"rejectedConcepts\":" << concept_stats.rejected;
    std::cout << ",\"acceptedConceptIncompleteRows\":" << accepted_concept_incomplete_rows;
    std::cout << ",\"publicSchemaInspected\":" << (public_schema_gate.inspected ? "true" : "false");
    std::cout << ",\"publicSchemaEscapeHatches\":" << public_schema_gate.total();
    std::cout << ",\"publicSchemaAnyObjectTypeRefs\":" << public_schema_gate.any_object_type;
    std::cout << ",\"publicSchemaXsAnyRefs\":" << public_schema_gate.xs_any;
    std::cout << ",\"publicSchemaXsAnyAttributeRefs\":" << public_schema_gate.xs_any_attribute;
    std::cout << ",\"publicSchemaEscapeStatus\":";
    print_json_string(public_schema_gate.total() == 0 ? "PASS" : "FAIL");
    std::cout << ",\"publicSchemaStableObjectTypes\":" << public_schema_gate.stable_object_types;
    std::cout << ",\"publicSchemaStableObjectTypeRequirements\":"
              << public_schema_gate.stable_object_type_requirements;
    std::cout << ",\"publicSchemaStableObjectTypesStatus\":";
    print_json_string(
        public_schema_gate.inspected &&
                public_schema_gate.stable_object_types == public_schema_gate.stable_object_type_requirements
            ? "PASS"
            : "FAIL");
    std::cout << ",\"publicSchemaInputFieldPropertyTypes\":"
              << public_schema_gate.input_field_property_types;
    std::cout << ",\"publicSchemaInputFieldPropertyTypeRequirements\":"
              << public_schema_gate.input_field_property_type_requirements;
    std::cout << ",\"publicSchemaInputFieldPropertyTypesStatus\":";
    print_json_string(
        public_schema_gate.inspected &&
                public_schema_gate.input_field_property_types ==
                    public_schema_gate.input_field_property_type_requirements
            ? "PASS"
            : "FAIL");
    std::cout << ",\"schemaMembersChecked\":" << schema_members_checked;
    std::cout << ",\"registryDescriptorsChecked\":" << registry_descriptors_checked;
    std::cout << ",\"controlInfoDescriptorCount\":"
              << oof::platform::control_info::descriptor_count();
    std::cout << ",\"controlInfoWriterDescriptorControls\":"
              << oof::platform::control_info::writer_descriptor_count();
    std::cout << ",\"controlInfoWritablePromotions\":"
              << oof::platform::control_info::writable_promotion_count();
    std::cout << ",\"writableValueCodecs\":" << writable_value_codecs;
    std::cout << ",\"readableValueCoverageGaps\":" << readable_value_coverage_gaps;
    std::cout << ",\"releaseReady\":false";
    std::cout << ",\"coverageStatus\":\"PARTIAL\"";
    std::cout << ",\"violations\":" << violations.size();
    std::cout << ",\"status\":";
    print_json_string(violations.empty() ? "PASS" : "FAIL");
    std::cout << ",\"details\":[";
    for (std::size_t index = 0; index < violations.size() && index < 32; ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_json_string(violations[index]);
    }
    std::cout << "]}\n";
}

struct XsdOrderObjectGateStats {
    std::size_t objects_checked = 0;
    std::size_t form_objects_checked = 0;
    std::size_t item_objects_checked = 0;
    std::size_t attribute_objects_checked = 0;
    std::size_t command_objects_checked = 0;
    std::size_t event_objects_checked = 0;
    std::size_t schema_slots_checked = 0;
    std::size_t explicit_slots = 0;
    std::size_t default_slots = 0;
    std::size_t writable_slots = 0;
    std::size_t value_object_slots = 0;
    std::size_t collection_objects_skipped = 0;
    std::map<std::string, std::size_t> type_frequency;
    std::map<std::string, std::size_t> slot_codec_frequency;
    std::vector<std::string> violations;
};

std::optional<oof::platform::object_schema::PlatformObjectSchema> xsd_order_schema_for_object(
    const oof::platform::object_model::PlatformObject& object
) {
    return oof::platform::object_schema::schema_for_platform_type(object.platform_type);
}

void xsd_order_gate_check_object(
    const oof::platform::object_model::PlatformObject& object,
    XsdOrderObjectGateStats& stats,
    std::string_view collection_name
) {
    auto schema = xsd_order_schema_for_object(object);
    if (!schema.has_value()) {
        stats.violations.push_back(
            "object " + object.object_id + " type " + object.platform_type +
            " has no platform XSD storage-order schema");
        return;
    }
    ++stats.objects_checked;
    if (object.platform_type == "Form") {
        ++stats.form_objects_checked;
    } else if (collection_name == "Items") {
        ++stats.item_objects_checked;
    } else if (collection_name == "Attributes") {
        ++stats.attribute_objects_checked;
    } else if (collection_name == "Commands") {
        ++stats.command_objects_checked;
    } else if (collection_name == "Events") {
        ++stats.event_objects_checked;
    }
    ++stats.type_frequency[object.platform_type];
    if (schema->xsd_members.empty()) {
        stats.violations.push_back(
            "object " + object.object_id + " type " + object.platform_type +
            " has empty XSD storage-order member list");
        return;
    }
    for (const auto& member : schema->xsd_members) {
        ++stats.schema_slots_checked;
        if (member.writable) {
            ++stats.writable_slots;
        }
        if (!member.slot_codec.empty()) {
            ++stats.slot_codec_frequency[member.slot_codec];
        }
        if (member.value_type == "ui:Picture" || member.value_type == "ui:Color" || member.value_type == "ui:Font") {
            ++stats.value_object_slots;
        }
        const auto* property = object.property(member.name);
        if (property == nullptr) {
            stats.violations.push_back(
                "object " + object.object_id + " type " + object.platform_type +
                " missing XSD slot property " + member.name + " streamName=" + member.stream_name);
            continue;
        }
        if (property->value_origin == "schema-default") {
            ++stats.default_slots;
        } else {
            ++stats.explicit_slots;
        }
    }
}

void xsd_order_gate_check_form_object(
    const oof::platform::object_model::PlatformFormObject& form_object,
    XsdOrderObjectGateStats& stats
) {
    xsd_order_gate_check_object(form_object.form, stats, "Form");
    for (const auto& object : form_object.items.objects()) {
        xsd_order_gate_check_object(object, stats, "Items");
    }
    for (const auto& object : form_object.attributes.objects()) {
        xsd_order_gate_check_object(object, stats, "Attributes");
    }
    for (const auto& object : form_object.commands.objects()) {
        xsd_order_gate_check_object(object, stats, "Commands");
    }
    for (const auto& object : form_object.events.objects()) {
        xsd_order_gate_check_object(object, stats, "Events");
    }
}

void print_xsd_order_object_gate_json(
    const oof::platform::object_model::PlatformFormObject& form_object,
    std::string_view source_label
) {
    XsdOrderObjectGateStats stats;
    xsd_order_gate_check_form_object(form_object, stats);
    std::cout << "{\"operation\":\"xsd-order-object-gate\"";
    std::cout << ",\"source\":";
    print_json_string(source_label);
    std::cout << ",\"concept\":\"platform XSD sequence/choice/attributes -> descriptor slots -> PlatformFormObject\"";
    std::cout << ",\"objectsChecked\":" << stats.objects_checked;
    std::cout << ",\"formObjectsChecked\":" << stats.form_objects_checked;
    std::cout << ",\"itemObjectsChecked\":" << stats.item_objects_checked;
    std::cout << ",\"attributeObjectsChecked\":" << stats.attribute_objects_checked;
    std::cout << ",\"commandObjectsChecked\":" << stats.command_objects_checked;
    std::cout << ",\"eventObjectsChecked\":" << stats.event_objects_checked;
    std::cout << ",\"collectionObjectsSkipped\":" << stats.collection_objects_skipped;
    std::cout << ",\"schemaSlotsChecked\":" << stats.schema_slots_checked;
    std::cout << ",\"explicitSlots\":" << stats.explicit_slots;
    std::cout << ",\"defaultSlots\":" << stats.default_slots;
    std::cout << ",\"writableSlots\":" << stats.writable_slots;
    std::cout << ",\"valueObjectSlots\":" << stats.value_object_slots;
    std::cout << ",\"typeFrequency\":[";
    std::size_t type_index = 0;
    for (const auto& [type, count] : stats.type_frequency) {
        if (type_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"type\":";
        print_json_string(type);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"slotCodecFrequency\":[";
    std::size_t codec_index = 0;
    for (const auto& [codec, count] : stats.slot_codec_frequency) {
        if (codec_index++ != 0) {
            std::cout << ",";
        }
        std::cout << "{\"codec\":";
        print_json_string(codec);
        std::cout << ",\"count\":" << count << "}";
    }
    std::cout << "],\"violations\":" << stats.violations.size();
    std::cout << ",\"status\":";
    print_json_string(stats.violations.empty() ? "PASS" : "FAIL");
    std::cout << ",\"details\":[";
    for (std::size_t index = 0; index < stats.violations.size() && index < 32; ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_json_string(stats.violations[index]);
    }
    std::cout << "]}\n";
}

void print_xsd_order_object_gate_selftest() {
    constexpr std::string_view fixture =
        "{\"#\",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{14,\"Button1\",4294967295,0,0,0},{},{},{}},{35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26,6,{14,\"Check1\",4294967295,0,0,0},{},{},{}}}}}";
    const auto envelope = parse_runtime_form_envelope(std::string(fixture));
    print_xsd_order_object_gate_json(materialize_platform_form_object(envelope), "selftest:runtime-bracket");
}

void print_runtime_xsd_order_object_gate(const std::string& input_path) {
    std::string canonical_text;
    const auto envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    print_xsd_order_object_gate_json(materialize_platform_form_object(envelope), "RuntimeForm:payload");
}

void print_formbin_xsd_order_object_gate(const std::string& input_path) {
    const auto envelope = read_formbin_runtime_envelope(input_path);
    print_xsd_order_object_gate_json(materialize_platform_form_object(envelope), "Form.bin:form");
}

void print_xsd_order_roundtrip_phase_json(std::string_view phase, const XsdOrderObjectGateStats& stats) {
    std::cout << "{\"phase\":";
    print_json_string(phase);
    std::cout << ",\"status\":";
    print_json_string(stats.violations.empty() ? "PASS" : "FAIL");
    std::cout << ",\"objectsChecked\":" << stats.objects_checked;
    std::cout << ",\"itemObjectsChecked\":" << stats.item_objects_checked;
    std::cout << ",\"attributeObjectsChecked\":" << stats.attribute_objects_checked;
    std::cout << ",\"commandObjectsChecked\":" << stats.command_objects_checked;
    std::cout << ",\"eventObjectsChecked\":" << stats.event_objects_checked;
    std::cout << ",\"collectionObjectsSkipped\":" << stats.collection_objects_skipped;
    std::cout << ",\"schemaSlotsChecked\":" << stats.schema_slots_checked;
    std::cout << ",\"explicitSlots\":" << stats.explicit_slots;
    std::cout << ",\"defaultSlots\":" << stats.default_slots;
    std::cout << ",\"valueObjectSlots\":" << stats.value_object_slots;
    std::cout << ",\"violations\":" << stats.violations.size();
    std::cout << ",\"details\":[";
    for (std::size_t index = 0; index < stats.violations.size() && index < 8; ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        print_json_string(stats.violations[index]);
    }
    std::cout << "]}";
}

void print_xsd_order_object_roundtrip_json(
    const RuntimeFormEnvelope& envelope,
    std::string_view source,
    std::size_t input_bytes
) {
    const auto result = object_bracket_roundtrip(envelope);
    XsdOrderObjectGateStats before_stats;
    XsdOrderObjectGateStats after_first_write_stats;
    XsdOrderObjectGateStats after_second_write_stats;
    xsd_order_gate_check_form_object(result.object1, before_stats);
    xsd_order_gate_check_form_object(result.object2, after_first_write_stats);
    xsd_order_gate_check_form_object(result.object3, after_second_write_stats);

    const bool input_payload_equal = result.payload1_text == result.payload2_text;
    const bool object_equal_after_first_write = result.signature1 == result.signature2;
    const bool object_stable_after_second_write = result.signature2 == result.signature3;
    const bool payload_stable_after_second_write = result.payload2_text == result.payload3_text;
    const bool xsd_order_pass =
        before_stats.violations.empty() &&
        after_first_write_stats.violations.empty() &&
        after_second_write_stats.violations.empty();
    const bool status_pass =
        xsd_order_pass &&
        object_equal_after_first_write &&
        object_stable_after_second_write &&
        payload_stable_after_second_write;

    std::cout << "{\"operation\":\"xsd-order-object-roundtrip\"";
    std::cout << ",\"source\":";
    print_json_string(source);
    std::cout << ",\"hypothesis\":\"platform XSD order descriptors survive bracket -> PlatformFormObject -> bracket roundtrip\"";
    std::cout << ",\"inputBytes\":" << input_bytes;
    std::cout << ",\"payloadRootVersion\":";
    print_json_string(envelope.payload.items.empty() ? "" : envelope.payload.items[0].atom);
    std::cout << ",\"objectCounts\":{\"items\":" << result.object1.items.count()
              << ",\"attributes\":" << result.object1.attributes.count()
              << ",\"commands\":" << result.object1.commands.count()
              << ",\"events\":" << result.object1.events.count()
              << ",\"edges\":" << result.object1.edges.size() << "}";
    std::cout << ",\"writtenObjectCounts\":{\"items\":" << result.object2.items.count()
              << ",\"attributes\":" << result.object2.attributes.count()
              << ",\"commands\":" << result.object2.commands.count()
              << ",\"events\":" << result.object2.events.count()
              << ",\"edges\":" << result.object2.edges.size() << "}";
    std::cout << ",\"inputPayloadEqualAfterObjectWrite\":"
              << (input_payload_equal ? "true" : "false");
    std::cout << ",\"objectSignatureEqualAfterFirstWrite\":"
              << (object_equal_after_first_write ? "true" : "false");
    std::cout << ",\"objectSignatureStableAfterSecondWrite\":"
              << (object_stable_after_second_write ? "true" : "false");
    std::cout << ",\"payloadStableAfterSecondWrite\":"
              << (payload_stable_after_second_write ? "true" : "false");
    std::cout << ",\"xsdOrderPass\":" << (xsd_order_pass ? "true" : "false");
    std::cout << ",\"xsdOrderPhases\":[";
    print_xsd_order_roundtrip_phase_json("beforeWrite", before_stats);
    std::cout << ",";
    print_xsd_order_roundtrip_phase_json("afterFirstWrite", after_first_write_stats);
    std::cout << ",";
    print_xsd_order_roundtrip_phase_json("afterSecondWrite", after_second_write_stats);
    std::cout << "]";
    print_ordinary_form_listout_coverage_json(result.listout_coverage);
    std::cout << ",\"status\":";
    print_json_string(status_pass ? "PASS" : "FAIL");
    std::cout << "}\n";
}

void print_xsd_order_object_roundtrip_selftest() {
    constexpr std::string_view fixture =
        "{\"#\",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{14,\"Button1\",4294967295,0,0,0},{},{},{}},{35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26,6,{14,\"Check1\",4294967295,0,0,0},{},{},{}}}}}";
    const auto envelope = parse_runtime_form_envelope(std::string(fixture));
    print_xsd_order_object_roundtrip_json(envelope, "selftest:runtime-bracket", fixture.size());
}

void print_runtime_xsd_order_object_roundtrip(const std::string& input_path) {
    std::string canonical_text;
    const auto envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    print_xsd_order_object_roundtrip_json(envelope, "RuntimeForm:payload", canonical_text.size());
}

void print_formbin_xsd_order_object_roundtrip(const std::string& input_path) {
    const auto input_bytes = read_file_bytes(input_path);
    const auto container = oof::platform::formbin::parse_container(input_bytes);
    const auto& form_file = find_container_file(container, "form");
    const auto envelope = runtime_envelope_from_form_payload(form_file.payload);
    print_xsd_order_object_roundtrip_json(envelope, "Form.bin:form", form_file.payload.size());
}

void print_object_graph_concept_selftest() {
    constexpr std::string_view fixture =
        "{\"#\",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{14,\"Button1\",4294967295,0,0,0},{},{},{}},{35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26,6,{14,\"Check1\",4294967295,0,0,0},{},{},{}}}}}";
    const auto envelope = parse_runtime_form_envelope(std::string(fixture));
    const auto result = object_bracket_roundtrip(envelope);
    const bool object_equal_after_first_write = result.signature1 == result.signature2;
    const bool object_stable_after_second_write = result.signature2 == result.signature3;
    const bool payload_stable_after_second_write = result.payload2_text == result.payload3_text;
    const bool concept_pass =
        object_equal_after_first_write &&
        object_stable_after_second_write &&
        payload_stable_after_second_write &&
        result.object1.items.count() == result.object2.items.count() &&
        result.object1.edges.size() == result.object2.edges.size();

    std::cout << "{\"operation\":\"object-graph-concept-selftest\"";
    std::cout << ",\"concept\":\"bracket -> PlatformFormObject -> bracket\"";
    std::cout << ",\"sourcePackageUsed\":false";
    std::cout << ",\"platformValidationUsed\":false";
    std::cout << ",\"objectCounts\":{\"items\":" << result.object1.items.count()
              << ",\"attributes\":" << result.object1.attributes.count()
              << ",\"commands\":" << result.object1.commands.count()
              << ",\"events\":" << result.object1.events.count()
              << ",\"edges\":" << result.object1.edges.size() << "}";
    std::cout << ",\"writtenObjectCounts\":{\"items\":" << result.object2.items.count()
              << ",\"attributes\":" << result.object2.attributes.count()
              << ",\"commands\":" << result.object2.commands.count()
              << ",\"events\":" << result.object2.events.count()
              << ",\"edges\":" << result.object2.edges.size() << "}";
    std::cout << ",\"objectSignatureEqualAfterFirstWrite\":"
              << (object_equal_after_first_write ? "true" : "false");
    std::cout << ",\"objectSignatureStableAfterSecondWrite\":"
              << (object_stable_after_second_write ? "true" : "false");
    std::cout << ",\"payloadStableAfterSecondWrite\":"
              << (payload_stable_after_second_write ? "true" : "false");
    std::cout << ",\"status\":";
    print_json_string(concept_pass ? "PASS" : "FAIL");
    std::cout << "}\n";
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
    std::vector<std::map<std::string, std::string>> diff_details;
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

std::string runtime_diff_node_summary(const oof::platform::stream::ListValue& value) {
    if (value.is_list) {
        return "list:" + std::to_string(value.items.size());
    }
    std::string atom = value.atom;
    if (atom.size() > 96) {
        atom.resize(96);
        atom += "...";
    }
    return std::string(value.atom_kind == oof::platform::stream::ListValue::AtomKind::string ? "string:" : "raw:") + atom;
}

void add_runtime_diff_detail(
    RuntimeSemanticDiffStats& stats,
    std::string_view kind,
    std::string_view path,
    const oof::platform::stream::ListValue& left,
    const oof::platform::stream::ListValue& right
) {
    if (stats.diff_details.size() >= 24) {
        return;
    }
    stats.diff_details.push_back({
        {"kind", std::string(kind)},
        {"path", std::string(path)},
        {"left", runtime_diff_node_summary(left)},
        {"right", runtime_diff_node_summary(right)},
    });
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
        add_runtime_diff_detail(stats, "node-kind", path, left, right);
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
        add_runtime_diff_detail(stats, "atom", path, left, right);
        return;
    }
    if (left.items.size() != right.items.size()) {
        ++stats.structural_diffs;
        stats.semantic_paths.push_back(std::string(path));
        add_runtime_diff_detail(stats, "list-size", path, left, right);
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
    std::cout << "],\"diffDetails\":[";
    for (std::size_t index = 0; index < stats.diff_details.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& detail = stats.diff_details[index];
        std::cout << "{\"kind\":";
        print_json_string(detail.at("kind"));
        std::cout << ",\"path\":";
        print_json_string(detail.at("path"));
        std::cout << ",\"left\":";
        print_json_string(detail.at("left"));
        std::cout << ",\"right\":";
        print_json_string(detail.at("right"));
        std::cout << "}";
    }
    std::cout << "]}\n";
}

const oof::platform::stream::ListValue* runtime_node_at_path(
    const oof::platform::stream::ListValue& root,
    std::string_view path
) {
    if (path.empty() || path == "$") {
        return &root;
    }
    if (path.front() != '$') {
        return nullptr;
    }

    const auto* current = &root;
    std::size_t cursor = 1;
    while (cursor < path.size()) {
        if (path[cursor] != '/') {
            return nullptr;
        }
        ++cursor;
        const std::size_t next = path.find('/', cursor);
        const std::string_view token = path.substr(
            cursor,
            next == std::string_view::npos ? std::string_view::npos : next - cursor);
        std::size_t index = 0;
        try {
            index = static_cast<std::size_t>(std::stoull(std::string(token)));
        } catch (...) {
            return nullptr;
        }
        if (!current->is_list || index >= current->items.size()) {
            return nullptr;
        }
        current = &current->items[index];
        if (next == std::string_view::npos) {
            break;
        }
        cursor = next;
    }
    return current;
}

void print_runtime_form_node(const std::string& input_path, std::string_view path) {
    std::string canonical_text;
    const RuntimeFormEnvelope envelope = read_runtime_form_envelope_file(input_path, canonical_text);
    const auto* node = runtime_node_at_path(envelope.payload, path);
    if (node == nullptr) {
        throw std::runtime_error("runtime form node path not found: " + std::string(path));
    }
    std::cout << "{\"operation\":\"runtime-form-node\",\"path\":";
    print_json_string(path);
    std::cout << ",\"summary\":";
    print_json_string(runtime_diff_node_summary(*node));
    std::cout << ",\"node\":";
    print_json_string(oof::platform::stream::dump_compact(*node));
    std::cout << "}\n";
}

void print_ordinary_form_object_selftest() {
    const std::string form_text =
        "{{\"MainCaption\",1,1,{\"ru\",\"Main\"}},"
        "{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{1,1,{\"ru\",\"Run\"}}},"
        "{8,1,2,101,22,0,0,0,0,0,0,0,0,0,0,0,0},{14,\"Button1\",4294967295,0,0,0},{0}}}";
    RuntimeFormEnvelope envelope = runtime_envelope_from_form_payload(
        std::vector<std::uint8_t>(form_text.begin(), form_text.end()));

    oof::ordinary::object::OrdinaryForm form(materialize_platform_form_object(envelope));
    const std::string before_name = form.get_prop_val("5", "Name");
    const std::string before_title = form.get_prop_val("5", "Title");
    form.set_prop_val("5", "Title", "ButtonViaOrdinaryForm");
    form.set_prop_val("5", "Visible", "false");
    const auto* button = form.find_object("5");
    const auto* found_by_collection = form.items().find("Button1");

    bool rejected_diagnostic_property = false;
    try {
        form.set_prop_val("5", "TextColor", "#123456");
    } catch (const std::exception&) {
        rejected_diagnostic_property = true;
    }

    std::cout << "{\"operation\":\"ordinary-form-object-selftest\"";
    std::cout << ",\"object\":\"OrdinaryForm\"";
    std::cout << ",\"backing\":\"PlatformFormObject\"";
    std::cout << ",\"conceptOwner\":\"OrdinaryFormConceptRegistry\"";
    std::cout << ",\"itemsCount\":" << form.items().count();
    std::cout << ",\"attributesCount\":" << form.attributes().count();
    std::cout << ",\"commandsCount\":" << form.commands().count();
    std::cout << ",\"eventsCount\":" << form.events().count();
    std::cout << ",\"beforeName\":";
    print_json_string(before_name);
    std::cout << ",\"beforeTitle\":";
    print_json_string(before_title);
    std::cout << ",\"afterTitle\":";
    print_json_string(form.get_prop_val("5", "Title"));
    std::cout << ",\"afterVisible\":";
    print_json_string(form.get_prop_val("5", "Visible"));
    std::cout << ",\"buttonFound\":" << (button != nullptr ? "true" : "false");
    std::cout << ",\"collectionFindByName\":" << (found_by_collection != nullptr ? "true" : "false");
    std::cout << ",\"diagnosticPropertyRejected\":" << (rejected_diagnostic_property ? "true" : "false");
    std::cout << "}\n";
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
    const auto controls = oof::platform::form_schema::all_controls();
    std::map<std::string_view, std::size_t> source_frequency;
    std::map<std::string_view, std::size_t> value_type_frequency;
    for (const auto* control : controls) {
        ++source_frequency[control->schema_source];
        std::string_view values = control->value_types;
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
    std::cout << ",\"externalSchemaSource\":";
    print_json_string(oof::platform::form_schema::data_chart_schema);
    std::cout << ",\"controlCount\":" << controls.size();
    std::cout << ",\"directGuidBindings\":0";
    std::cout << ",\"guidBindingStatus\":\"not-present-in-xsd; use resource/binary evidence\"";
    std::cout << ",\"controls\":[";
    for (std::size_t index = 0; index < controls.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& control = *controls[index];
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
        std::cout << ",\"xsdNamespace\":";
        print_json_string(schema.xsd_namespace);
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

void print_platform_control_info_descriptors() {
    std::cout << "{\"source\":\"native port of historical no-base writer control info descriptor profiles; platform persistence evidence cf_form_controls_info8\"";
    std::cout << ",\"descriptorCount\":"
              << oof::platform::control_info::descriptor_count();
    std::cout << ",\"writerDescriptorControls\":"
              << oof::platform::control_info::writer_descriptor_count();
    std::cout << ",\"writablePromotions\":"
              << oof::platform::control_info::writable_promotion_count();
    std::cout << ",\"writablePromotion\":\"disabled until info8 slot/value semantic correlation is proven on corpus/platform validation\"";
    std::cout << ",\"status\":\"descriptor-registry-ready\"";
    std::cout << ",\"descriptors\":[";
    for (std::size_t index = 0; index < oof::platform::control_info::descriptors.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        const auto& descriptor = oof::platform::control_info::descriptors[index];
        std::cout << "{\"controlType\":";
        print_json_string(descriptor.control_type);
        std::cout << ",\"infoKind\":";
        print_json_string(descriptor.info_kind);
        std::cout << ",\"writerDescriptor\":"
                  << (oof::platform::control_info::has_writer_descriptor(descriptor.control_type) ? "true" : "false");
        std::cout << ",\"slotCount\":" << descriptor.slot_count;
        std::cout << ",\"slots\":[";
        for (std::size_t slot_index = 0; slot_index < descriptor.slot_count; ++slot_index) {
            if (slot_index != 0) {
                std::cout << ",";
            }
            const auto& slot = descriptor.slots[slot_index];
            std::cout << "{\"name\":";
            print_json_string(slot.name);
            std::cout << ",\"index\":" << slot.index << "}";
        }
        std::cout << "],\"evidence\":";
        print_json_string(descriptor.evidence);
        std::cout << "}";
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

void print_formbin_source_package_selftest() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("oof-native-source-package-selftest-" + std::to_string(unique));
    const auto xml_path = root / "Forms" / "Form" / "Ext" / "Form.xml";
    const auto module_path = form_package_module_path(xml_path);
    const auto out_path = root / "rebuilt-Form.bin";

    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Form ordinaryFormVersion=\"2.0\">\n"
        "  <Title><Item lang=\"ru\">Source title</Item></Title>\n"
        "  <ChildItems>\n"
        "    <Button name=\"SourceButton\" id=\"7\"><Title><Item lang=\"ru\">Run</Item></Title></Button>\n"
        "  </ChildItems>\n"
        "</Form>\n";
    const std::vector<std::uint8_t> xml_bytes(xml.begin(), xml.end());
    const std::vector<std::uint8_t> module_bytes{'s', 'o', 'u', 'r', 'c', 'e', ' ', 'm', 'o', 'd', 'u', 'l', 'e'};

    write_file_bytes(xml_path, xml_bytes);
    write_file_bytes(module_path, module_bytes);
    const auto build = build_formbin_source_package(xml_path.string());
    write_file_bytes(out_path, build.bytes);
    const auto parsed = oof::platform::formbin::parse_container(build.bytes);
    if (parsed.files.size() != 2) {
        throw std::runtime_error("source package selftest expected two logical files");
    }

    const auto& form = parsed.files.at(0);
    const auto& module = parsed.files.at(1);
    const bool form_ticks_match =
        form.created == build.container_times.form_created &&
        form.modified == build.container_times.form_modified;
    const bool module_ticks_match =
        module.created == build.container_times.module_created &&
        module.modified == build.container_times.module_modified;
    const bool module_payload_match = module.payload == module_bytes;
    const bool no_zero_ticks =
        form.created != 0 && form.modified != 0 && module.created != 0 && module.modified != 0;

    const std::vector<std::string> raw_xml_cases{
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Form ordinaryFormVersion=\"2.0\" profileUuid=\"not-public\">\n"
        "</Form>\n",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Form ordinaryFormVersion=\"2.0\">\n"
        "  <ChildItems>\n"
        "    <AnyObjectType name=\"RawCarrier\" id=\"11\" actionProfile=\"1\">\n"
        "      <SerializationProfile linkModeShape=\"slot-map\">\n"
        "        <slot1 value=\"x\" />\n"
        "      </SerializationProfile>\n"
        "    </AnyObjectType>\n"
        "  </ChildItems>\n"
        "</Form>\n",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Form ordinaryFormVersion=\"2.0\">\n"
        "  <ChildItems>\n"
        "    <AnyObjectType name=\"RawCarrier\" id=\"11\"><RawBlob value=\"x\" /></AnyObjectType>\n"
        "  </ChildItems>\n"
        "</Form>\n",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Form ordinaryFormVersion=\"2.0\">\n"
        "  <Pages><Page name=\"Legacy\" /></Pages>\n"
        "</Form>\n",
    };
    std::size_t raw_guard_rejections = 0;
    std::string raw_guard_error;
    for (const auto& raw_xml : raw_xml_cases) {
        write_file_bytes(xml_path, std::vector<std::uint8_t>(raw_xml.begin(), raw_xml.end()));
        try {
            (void)build_formbin_source_package(xml_path.string());
        } catch (const std::exception& ex) {
            ++raw_guard_rejections;
            raw_guard_error = ex.what();
        }
    }
    if (raw_guard_rejections != raw_xml_cases.size()) {
        throw std::runtime_error("source package selftest expected public XML raw guard rejection");
    }

    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);

    std::cout << "{";
    std::cout << "\"bytes\":" << build.bytes.size();
    std::cout << ",\"fileCount\":" << parsed.files.size();
    std::cout << ",\"containerTicksSource\":";
    print_json_string(build.container_times.source);
    std::cout << ",\"formTicksMatch\":" << (form_ticks_match ? "true" : "false");
    std::cout << ",\"moduleTicksMatch\":" << (module_ticks_match ? "true" : "false");
    std::cout << ",\"modulePayloadMatch\":" << (module_payload_match ? "true" : "false");
    std::cout << ",\"noZeroTicks\":" << (no_zero_ticks ? "true" : "false");
    std::cout << ",\"publicXmlRawGuardRejected\":" << (raw_guard_rejections == raw_xml_cases.size() ? "true" : "false");
    std::cout << ",\"publicXmlRawGuardRejections\":" << raw_guard_rejections;
    std::cout << ",\"publicXmlRawGuardError\":";
    print_json_string(raw_guard_error);
    std::cout << ",\"moduleBytes\":" << module.payload.size();
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
        if (command == "formbin-selftest") {
            print_formbin_selftest();
            return 0;
        }
        if (command == "formbin-source-package-selftest") {
            print_formbin_source_package_selftest();
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
        if (command == "ordinary-form-object-selftest") {
            print_ordinary_form_object_selftest();
            return 0;
        }
        if (command == "form-object-graph-selftest") {
            print_form_object_graph_selftest();
            return 0;
        }
        if (command == "object-graph-concept-selftest") {
            print_object_graph_concept_selftest();
            return 0;
        }
        if (command == "raw-deflate-selftest") {
            print_raw_deflate_selftest();
            return 0;
        }
        if (command == "empty-form-object-roundtrip" && (argc == 2 || argc == 3)) {
            print_empty_form_object_roundtrip(argc == 3 ? argv[2] : "");
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
        if (command == "formbin-object-roundtrip" && argc == 3) {
            print_formbin_object_roundtrip(argv[2]);
            return 0;
        }
        if (command == "formbin-object-roundtrip-diff" && argc == 3) {
            print_formbin_object_roundtrip_diff(argv[2]);
            return 0;
        }
        if (command == "formbin-xsd-order-object-roundtrip" && argc == 3) {
            print_formbin_xsd_order_object_roundtrip(argv[2]);
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
        if (command == "container-replace" && (argc == 6 || argc == 7)) {
            const bool raw_deflate = argc == 7 && std::string_view(argv[6]) == "--raw-deflate";
            if (argc == 7 && !raw_deflate) {
                throw std::runtime_error("unknown container-replace option: " + std::string(argv[6]));
            }
            replace_container_file(argv[2], argv[3], argv[4], argv[5], raw_deflate);
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
        if (command == "ordinary-form-concepts") {
            print_ordinary_form_concepts();
            return 0;
        }
        if (command == "platform-control-info-descriptors") {
            print_platform_control_info_descriptors();
            return 0;
        }
        if (command == "object-model-gate") {
            print_object_model_gate();
            return 0;
        }
        if (command == "xsd-order-object-gate") {
            print_xsd_order_object_gate_selftest();
            return 0;
        }
        if (command == "xsd-order-object-roundtrip") {
            print_xsd_order_object_roundtrip_selftest();
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
        if (command == "formbin-build-source-package" && argc == 4) {
            write_formbin_from_source_package(argv[2], argv[3]);
            return 0;
        }
        if (command == "formbin-dump-platform-xsd-xml" && argc == 4) {
            write_formbin_platform_xsd_xml(argv[2], argv[3]);
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
        if (command == "formbin-xsd-order-object-gate" && argc == 3) {
            print_formbin_xsd_order_object_gate(argv[2]);
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
        if (command == "platform-xsd-xml-object" && argc == 3) {
            print_platform_xsd_xml_object(argv[2]);
            return 0;
        }
        if (command == "platform-xsd-xml-roundtrip" && argc == 4) {
            write_platform_xsd_xml_roundtrip(argv[2], argv[3]);
            return 0;
        }
        if (command == "platform-xsd-xml-build-runtime" && argc == 4) {
            write_platform_xsd_xml_runtime_form(argv[2], argv[3]);
            return 0;
        }
        if (command == "platform-xsd-xml-build-formbin" && argc == 4) {
            write_formbin_from_platform_xsd_xml(argv[2], argv[3]);
            return 0;
        }
        if (command == "runtime-form-dump-xml" && argc == 4) {
            write_runtime_form_xml(argv[2], argv[3]);
            return 0;
        }
        if (command == "runtime-form-dump-platform-xsd-xml" && argc == 4) {
            write_runtime_form_platform_xsd_xml(argv[2], argv[3]);
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
        if (command == "runtime-form-object-roundtrip" && argc == 3) {
            print_runtime_form_object_roundtrip(argv[2]);
            return 0;
        }
        if (command == "runtime-form-object-roundtrip-diff" && argc == 3) {
            print_runtime_form_object_roundtrip_diff(argv[2]);
            return 0;
        }
        if (command == "runtime-xsd-order-object-roundtrip" && argc == 3) {
            print_runtime_xsd_order_object_roundtrip(argv[2]);
            return 0;
        }
        if (command == "runtime-platform-object" && argc == 3) {
            print_runtime_platform_object(argv[2]);
            return 0;
        }
        if (command == "runtime-xsd-order-object-gate" && argc == 3) {
            print_runtime_xsd_order_object_gate(argv[2]);
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
        if (command == "runtime-form-node" && argc == 4) {
            print_runtime_form_node(argv[2], argv[3]);
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

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
