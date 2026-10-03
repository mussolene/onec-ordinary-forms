#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "oof/diagnostic.hpp"
#include "oof/form_bin.hpp"
#include "oof/source/form_xml.hpp"

namespace {

constexpr std::string_view version = "1.0.0-dev";

void print_usage(std::ostream& output) {
    output
        << "Usage: oof dump <Form.bin> <Form.xml> [--json]\n"
        << "       oof build <Form.xml> <Form.bin> [--json] (requires <XML-stem>/Module.bsl)\n"
        << "       oof --version\n";
}

bool requests_json(int argc, char** argv) {
    return std::any_of(argv + 1, argv + argc, [](const char* argument) {
        return std::string_view(argument) == "--json";
    });
}

std::vector<std::string_view> positional_arguments(int argc, char** argv) {
    std::vector<std::string_view> result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument != "--json") {
            result.push_back(argument);
        }
    }
    return result;
}

void print_json_string(std::ostream& output, std::string_view value) {
    output << '"';
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (byte < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    output << "\\u00" << hex[byte >> 4] << hex[byte & 0x0f];
                } else {
                    output << static_cast<char>(byte);
                }
        }
    }
    output << '"';
}

void print_diagnostics(const oof::Diagnostics& diagnostics, bool json) {
    if (!json) {
        for (const auto& item : diagnostics) {
            std::cerr << item.code << ": " << item.message;
            if (!item.path.empty()) {
                std::cerr << " [" << item.path << ']';
            }
            std::cerr << '\n';
        }
        return;
    }
    std::cout << "{\"ok\":false,\"diagnostics\":[";
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        const auto& item = diagnostics[index];
        std::cout << "{\"code\":";
        print_json_string(std::cout, item.code);
        std::cout << ",\"severity\":";
        print_json_string(std::cout, oof::to_string(item.severity));
        std::cout << ",\"objectId\":";
        print_json_string(std::cout, item.object_id);
        std::cout << ",\"path\":";
        print_json_string(std::cout, item.path);
        std::cout << ",\"property\":";
        print_json_string(std::cout, item.property);
        std::cout << ",\"expected\":";
        print_json_string(std::cout, item.expected);
        std::cout << ",\"actual\":";
        print_json_string(std::cout, item.actual);
        std::cout << ",\"message\":";
        print_json_string(std::cout, item.message);
        std::cout << '}';
    }
    std::cout << "]}\n";
}

oof::Diagnostics cli_failure(
    std::string code,
    std::string path,
    std::string expected,
    std::string actual,
    std::string message) {
    return {oof::Diagnostic{
        std::move(code),
        oof::DiagnosticSeverity::error,
        {},
        std::move(path),
        {},
        std::move(expected),
        std::move(actual),
        std::move(message),
    }};
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open input file: " + path.string());
    }
    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>(),
    };
}

std::string read_text(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

void write_bytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot open output file: " + path.string());
    }
    file.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("cannot write output file: " + path.string());
    }
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    write_bytes(path, {text.begin(), text.end()});
}

std::filesystem::path module_path_for(const std::filesystem::path& xml_path) {
    auto root = xml_path;
    root.replace_extension();
    return root / "Module.bsl";
}

std::filesystem::path package_root_for(const std::filesystem::path& xml_path) {
    auto root = xml_path;
    root.replace_extension();
    return root;
}

bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != ".." && !relative.is_absolute();
}

std::filesystem::path checked_asset_path(
    const std::filesystem::path& package_root,
    std::string_view relative_path,
    bool must_exist) {
    const auto root = std::filesystem::canonical(package_root);
    const auto candidate = root / std::filesystem::path(relative_path);
    if (must_exist) {
        const auto resolved = std::filesystem::canonical(candidate);
        if (!path_is_within(root, resolved) || !std::filesystem::is_regular_file(resolved)) {
            throw std::runtime_error("picture asset path escapes the source package or is not a file: " + std::string(relative_path));
        }
        return resolved;
    }
    const auto proposed_parent = std::filesystem::weakly_canonical(candidate.parent_path());
    const auto proposed = std::filesystem::weakly_canonical(candidate);
    if (!path_is_within(root, proposed_parent) || !path_is_within(root, proposed)) {
        throw std::runtime_error("picture asset output path escapes the source package: " + std::string(relative_path));
    }
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(candidate))) {
        throw std::runtime_error("picture asset output cannot replace a symbolic link: " + std::string(relative_path));
    }
    std::filesystem::create_directories(candidate.parent_path());
    const auto parent = std::filesystem::canonical(candidate.parent_path());
    const auto resolved = std::filesystem::weakly_canonical(candidate);
    if (!path_is_within(root, parent) || !path_is_within(root, resolved)) {
        throw std::runtime_error("picture asset output path escapes the source package: " + std::string(relative_path));
    }
    return resolved;
}

void load_picture_assets(oof::model::OrdinaryFormDocument& document, const std::filesystem::path& xml_path) {
    if (document.assets().empty()) return;
    const auto root = package_root_for(xml_path);
    if (!std::filesystem::is_directory(root)) {
        throw std::runtime_error("picture source package directory is missing: " + root.string());
    }
    for (const auto& asset : document.assets()) {
        document.set_asset_bytes(asset.id, read_bytes(checked_asset_path(root, asset.relative_path, true)));
    }
}

void write_picture_assets(const oof::model::OrdinaryFormDocument& document, const std::filesystem::path& xml_path) {
    if (document.assets().empty()) return;
    const auto root = package_root_for(xml_path);
    std::filesystem::create_directories(root);
    for (const auto& asset : document.assets()) {
        if (asset.bytes.empty()) throw std::runtime_error("decoded picture asset has no bytes: " + asset.relative_path);
        write_bytes(checked_asset_path(root, asset.relative_path, false), asset.bytes);
    }
}

int dump_form(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    bool json) {
    const auto loaded = oof::load_form_bin(read_bytes(input));
    if (!loaded) {
        print_diagnostics(loaded.diagnostics(), json);
        return 1;
    }
    const auto xml = oof::source::serialize_form_xml(loaded.value());
    if (!xml) {
        print_diagnostics(xml.diagnostics(), json);
        return 1;
    }
    write_text(output, xml.value());
    write_text(module_path_for(output), loaded.value().module().text);
    write_picture_assets(loaded.value(), output);
    if (json) {
        std::cout << "{\"ok\":true,\"command\":\"dump\",\"output\":";
        print_json_string(std::cout, output.string());
        std::cout << "}\n";
    }
    return 0;
}

int build_form(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    bool json) {
    auto parsed = oof::source::parse_form_xml(read_text(input));
    if (!parsed) {
        print_diagnostics(parsed.diagnostics(), json);
        return 1;
    }
    try {
        load_picture_assets(parsed.value(), input);
    } catch (const std::exception& error) {
        print_diagnostics(cli_failure("OOF0006", input.string(), "accessible picture assets inside source package", {}, error.what()), json);
        return 1;
    }
    const auto module_path = module_path_for(input);
    if (!std::filesystem::exists(module_path)) {
        print_diagnostics(
            cli_failure(
                "OOF0005",
                module_path.string(),
                "existing Module.bsl sidecar (an empty file is allowed)",
                "missing file",
                "Build requires the module sidecar to avoid silently dropping module code"),
            json);
        return 1;
    }
    parsed.value().set_module(oof::model::FormModule{read_text(module_path)});
    const auto encoded = oof::save_form_bin(parsed.value());
    if (!encoded) {
        print_diagnostics(encoded.diagnostics(), json);
        return 1;
    }
    write_bytes(output, encoded.value());
    if (json) {
        std::cout << "{\"ok\":true,\"command\":\"build\",\"output\":";
        print_json_string(std::cout, output.string());
        std::cout << "}\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "oof " << version << '\n';
        return 0;
    }
    if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--help")) {
        print_usage(std::cout);
        return argc == 1 ? 64 : 0;
    }

    const bool json = requests_json(argc, argv);
    const auto arguments = positional_arguments(argc, argv);
    if (arguments.empty()) {
        print_usage(std::cerr);
        return 64;
    }
    const std::string_view command = arguments[0];
    try {
        if ((command == "dump" || command == "build") && arguments.size() != 3) {
            print_diagnostics(
                cli_failure(
                    "OOF0003",
                    {},
                    std::string(command) + " <input> <output>",
                    std::to_string(arguments.size() - 1) + " operands",
                    "Wrong command arity"),
                json);
            return 64;
        }
        if (command == "dump") {
            return dump_form(arguments[1], arguments[2], json);
        }
        if (command == "build") {
            return build_form(arguments[1], arguments[2], json);
        }
        print_diagnostics(
            cli_failure(
                "OOF0002",
                {},
                "dump|build",
                std::string(command),
                "Unknown command"),
            json);
        return 64;
    } catch (const std::exception& error) {
        print_diagnostics(
            cli_failure("OOF0004", {}, "successful file operation", {}, error.what()),
            json);
        return 1;
    }
}
