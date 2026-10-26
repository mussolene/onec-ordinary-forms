#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "oof/diagnostic.hpp"
#include "oof/form_bin.hpp"
#include "oof/model/metamodel.hpp"
#include "oof/source/form_xml.hpp"

namespace {

constexpr std::string_view version = "1.0.0-dev";
constexpr std::uintmax_t max_input_bytes = 64U * 1024U * 1024U;

void print_usage(std::ostream& output) {
    output
        << "Usage: oof dump <Form.bin> <Form.xml> [--json]\n"
        << "       oof build <Form.xml> <Form.bin> [--json] (requires <XML-stem>/Module.bsl)\n"
        << "       oof --coverage [--json]\n"
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

void print_success(
    std::string_view command,
    const std::filesystem::path& output,
    const oof::Diagnostics& diagnostics,
    bool json) {
    if (!json) {
        for (const auto& item : diagnostics) {
            if (item.severity != oof::DiagnosticSeverity::warning) continue;
            std::cerr << "warning " << item.code << ": " << item.message;
            if (!item.path.empty()) std::cerr << " [" << item.path << ']';
            std::cerr << '\n';
        }
        return;
    }
    std::cout << "{\"ok\":true,\"command\":";
    print_json_string(std::cout, command);
    std::cout << ",\"output\":";
    print_json_string(std::cout, output.string());
    std::cout << ",\"diagnostics\":[";
    bool first = true;
    for (const auto& item : diagnostics) {
        if (!first) std::cout << ',';
        first = false;
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

void print_coverage(bool json) {
    const auto& coverage = oof::model::metamodel::metamodel_coverage();
    if (!json) {
        std::cout << "Descriptor coverage for target platform " << coverage.target_platform_version
                  << ": " << coverage.target_control_count << '/' << coverage.control_count
                  << " controls (ActiveXControl excluded; " << coverage.excluded_control_property_occurrences
                  << " property occurrences excluded). Remaining classifications: "
                  << coverage.unclassified_properties << " properties, "
                  << coverage.unclassified_events << " events, "
                  << coverage.unknown_defaults << " unknown defaults, "
                  << coverage.missing_storage_codecs << " missing storage codecs, "
                  << coverage.unknown_value_kinds << " unknown value kinds, "
                  << coverage.unclassified_value_codecs << " unclassified value codecs.\n";
        return;
    }
    std::cout << "{\"ok\":true,\"targetPlatformVersion\":";
    print_json_string(std::cout, coverage.target_platform_version);
    std::cout << ",\"catalogControlCount\":" << coverage.control_count
              << ",\"targetControlCount\":" << coverage.target_control_count
              << ",\"excludedControls\":[\"ActiveXControl\"]"
              << ",\"catalogControlPropertyOccurrences\":" << coverage.control_property_occurrences
              << ",\"excludedControlPropertyOccurrences\":" << coverage.excluded_control_property_occurrences
              << ",\"descriptorCoverageComplete\":" << (coverage.release_ready ? "true" : "false")
              << ",\"remaining\":{\"properties\":" << coverage.unclassified_properties
              << ",\"events\":" << coverage.unclassified_events
              << ",\"unknownDefaults\":" << coverage.unknown_defaults
              << ",\"missingStorageCodecs\":" << coverage.missing_storage_codecs
              << ",\"unknownValueKinds\":" << coverage.unknown_value_kinds
              << ",\"unclassifiedValueCodecs\":" << coverage.unclassified_value_codecs
              << "}}\n";
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

std::vector<std::uint8_t> read_bytes(
    const std::filesystem::path& path, std::uintmax_t limit = max_input_bytes) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("input must be a regular file: " + path.string());
    }
    const auto size = std::filesystem::file_size(path);
    if (size > limit) {
        throw std::runtime_error("input exceeds the 64 MiB file or remaining picture package limit: " + path.string());
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open input file: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size != 0) file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!file || file.peek() != std::char_traits<char>::eof() || file.bad()) {
        throw std::runtime_error("input changed or could not be read completely: " + path.string());
    }
    return bytes;
}

std::string read_text(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path);
    const bool has_bom = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf;
    return std::string(bytes.begin() + (has_bom ? 3 : 0), bytes.end());
}

void check_output_file(const std::filesystem::path& path) {
    const auto status = std::filesystem::symlink_status(path);
    if (std::filesystem::is_symlink(status) ||
        (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))) {
        throw std::runtime_error("output must be a regular file, not a symbolic link: " + path.string());
    }
    if (std::filesystem::is_regular_file(status)) {
        const auto permissions = status.permissions();
        constexpr auto writable = std::filesystem::perms::owner_write |
            std::filesystem::perms::group_write | std::filesystem::perms::others_write;
        if ((permissions & writable) == std::filesystem::perms::none) {
            throw std::runtime_error("output file is read-only: " + path.string());
        }
    }
    if (std::filesystem::is_regular_file(status) && std::filesystem::hard_link_count(path) != 1) {
        throw std::runtime_error("output cannot replace a file with multiple hard links: " + path.string());
    }
}

std::filesystem::path module_path_for(const std::filesystem::path& xml_path) {
    auto root = xml_path;
    root.replace_extension();
    return root / "Module.bsl";
}

std::filesystem::path package_root_for(const std::filesystem::path& xml_path) {
    auto root = xml_path;
    root.replace_extension();
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(root))) {
        throw std::runtime_error("source package directory cannot be a symbolic link: " + root.string());
    }
    return root;
}

bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != ".." && !relative.is_absolute();
}

struct PendingWrite {
    PendingWrite(std::filesystem::path target, const std::vector<std::uint8_t>* contents)
        : output(std::move(target)), bytes(contents) {}

    std::filesystem::path output;
    const std::vector<std::uint8_t>* bytes{};
    std::filesystem::path staged;
    std::filesystem::path backup;
    bool backed_up{};
    bool installed{};
};

std::filesystem::perms default_output_permissions() {
#ifdef _WIN32
    return std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
#else
    const auto mask = ::umask(0);
    ::umask(mask);
    return static_cast<std::filesystem::perms>(0666 & ~mask);
#endif
}

std::filesystem::path write_staged_file(
    const std::filesystem::path& output, const std::vector<std::uint8_t>& bytes,
    std::string_view role = "stage") {
    const auto parent = output.parent_path().empty() ? std::filesystem::path(".") : output.parent_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    static std::atomic<std::uint64_t> sequence{};
    std::filesystem::path staged;
    int descriptor = -1;
#ifdef _WIN32
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
        staged = parent / (".oof-" + std::string(role) + "-" + std::to_string(stamp) + "-" + std::to_string(id));
        descriptor = ::_wopen(staged.c_str(), _O_CREAT | _O_EXCL | _O_BINARY | _O_WRONLY, _S_IREAD | _S_IWRITE);
        if (descriptor >= 0) break;
        if (errno != EEXIST) throw std::runtime_error("cannot create staged output file: " + staged.string() + ": " + std::strerror(errno));
    }
#else
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
        std::string pattern = (parent / (".oof-" + std::string(role) + "-" + std::to_string(stamp) + "-" + std::to_string(id) + "-XXXXXX")).string();
        descriptor = ::mkstemp(pattern.data());
        if (descriptor >= 0) {
            staged = pattern;
            break;
        }
        if (errno != EEXIST) throw std::runtime_error("cannot create staged output file: " + pattern + ": " + std::strerror(errno));
    }
#endif
    if (descriptor < 0) throw std::runtime_error("cannot allocate exclusive staged output beside: " + output.string());

    try {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
#ifdef _WIN32
            const auto chunk = static_cast<unsigned>(std::min<std::size_t>(bytes.size() - offset, 1024U * 1024U));
            const int written = ::_write(descriptor, bytes.data() + offset, chunk);
#else
            const auto written = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
#endif
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::runtime_error("cannot write staged output file: " + staged.string() + ": " + std::strerror(errno));
            offset += static_cast<std::size_t>(written);
        }
#ifdef _WIN32
        if (::_close(descriptor) != 0) {
#else
        if (::close(descriptor) != 0) {
#endif
            descriptor = -1;
            throw std::runtime_error("cannot close staged output file: " + staged.string() + ": " + std::strerror(errno));
        }
        descriptor = -1;
    } catch (...) {
        if (descriptor >= 0) {
#ifdef _WIN32
            ::_close(descriptor);
#else
            ::close(descriptor);
#endif
        }
        std::error_code ignored;
        std::filesystem::remove(staged, ignored);
        throw;
    }
    return staged;
}

void write_file_set(std::vector<PendingWrite> writes) {
    std::set<std::filesystem::path> outputs;
    for (auto& write : writes) {
        check_output_file(write.output);
        const auto normalized = std::filesystem::weakly_canonical(write.output);
        if (!outputs.insert(normalized).second) {
            throw std::runtime_error("multiple package outputs resolve to the same file: " + write.output.string());
        }
    }
    const auto cleanup = [&writes](bool preserve_backups = false) {
        std::string failure;
        for (auto& write : writes) {
            std::error_code error;
            if (!write.staged.empty()) {
                std::filesystem::remove(write.staged, error);
                if (error && failure.empty()) failure = "cannot remove staged file " + write.staged.string() + ": " + error.message();
            }
            if (!preserve_backups && !write.backup.empty()) {
                error.clear();
                std::filesystem::remove(write.backup, error);
                if (error && failure.empty()) failure = "cannot remove backup file " + write.backup.string() + ": " + error.message();
            }
        }
        return failure;
    };
    try {
        for (auto& write : writes) {
            const auto parent = write.output.parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            write.staged = write_staged_file(write.output, *write.bytes);
        }
        for (auto& write : writes) {
            const auto status = std::filesystem::symlink_status(write.output);
            const bool output_existed = std::filesystem::exists(status);
            const auto output_permissions = output_existed ? status.permissions() : default_output_permissions();
            if (output_existed) {
                write.backup = write_staged_file(write.output, {}, "backup");
                std::filesystem::rename(write.output, write.backup);
                write.backed_up = true;
            }
            std::filesystem::rename(write.staged, write.output);
            write.staged.clear();
            write.installed = true;
            std::filesystem::permissions(write.output, output_permissions, std::filesystem::perm_options::replace);
        }
    } catch (...) {
        const auto failure = std::current_exception();
        std::string rollback_failure;
        for (auto it = writes.rbegin(); it != writes.rend(); ++it) {
            std::error_code error;
            if (it->installed) {
                std::filesystem::remove(it->output, error);
                if (error && rollback_failure.empty()) rollback_failure = "cannot remove partially installed output: " + error.message();
            }
            if (it->backed_up) {
                error.clear();
                std::filesystem::rename(it->backup, it->output, error);
                if (!error) it->backup.clear();
                else if (rollback_failure.empty()) rollback_failure = "cannot restore previous output: " + error.message();
            }
        }
        const auto cleanup_failure = cleanup(!rollback_failure.empty());
        if (!cleanup_failure.empty() && rollback_failure.empty()) {
            throw std::runtime_error("output transaction failed and temporary cleanup was incomplete: " + cleanup_failure);
        }
        if (!rollback_failure.empty()) {
            if (!cleanup_failure.empty()) rollback_failure += "; " + cleanup_failure;
            throw std::runtime_error("output installation failed and rollback was incomplete: " + rollback_failure);
        }
        std::rethrow_exception(failure);
    }
    const auto cleanup_failure = cleanup();
    if (!cleanup_failure.empty()) {
        throw std::runtime_error("outputs were installed, but temporary cleanup failed: " + cleanup_failure);
    }
}

std::filesystem::path checked_sidecar_path(
    const std::filesystem::path& package_root,
    std::string_view relative_path,
    bool must_exist) {
    const auto root = std::filesystem::canonical(package_root);
    const auto candidate = root / std::filesystem::path(relative_path);
    if (must_exist) {
        const auto resolved = std::filesystem::canonical(candidate);
        if (!path_is_within(root, resolved) || !std::filesystem::is_regular_file(resolved) ||
            std::filesystem::hard_link_count(resolved) != 1) {
            throw std::runtime_error("sidecar path escapes the source package or is not a file: " + std::string(relative_path));
        }
        return resolved;
    }
    const auto proposed_parent = std::filesystem::weakly_canonical(candidate.parent_path());
    const auto proposed = std::filesystem::weakly_canonical(candidate);
    if (!path_is_within(root, proposed_parent) || !path_is_within(root, proposed)) {
        throw std::runtime_error("sidecar output path escapes the source package: " + std::string(relative_path));
    }
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(candidate))) {
        throw std::runtime_error("sidecar output cannot replace a symbolic link: " + std::string(relative_path));
    }
    std::filesystem::create_directories(candidate.parent_path());
    const auto parent = std::filesystem::canonical(candidate.parent_path());
    const auto resolved = std::filesystem::weakly_canonical(candidate);
    if (!path_is_within(root, parent) || !path_is_within(root, resolved)) {
        throw std::runtime_error("sidecar output path escapes the source package: " + std::string(relative_path));
    }
    return resolved;
}

void load_picture_assets(oof::model::OrdinaryFormDocument& document, const std::filesystem::path& xml_path) {
    if (document.assets().empty()) return;
    const auto root = package_root_for(xml_path);
    if (!std::filesystem::is_directory(root)) {
        throw std::runtime_error("picture source package directory is missing: " + root.string());
    }
    std::uintmax_t remaining = max_input_bytes;
    for (const auto& asset : document.assets()) {
        auto bytes = read_bytes(checked_sidecar_path(root, asset.relative_path, true), remaining);
        remaining -= bytes.size();
        document.set_asset_bytes(asset.id, std::move(bytes));
    }
}

std::vector<PendingWrite> picture_asset_writes(
    const oof::model::OrdinaryFormDocument& document, const std::filesystem::path& xml_path) {
    std::vector<PendingWrite> writes;
    if (document.assets().empty()) return writes;
    const auto root = package_root_for(xml_path);
    for (const auto& asset : document.assets()) {
        if (asset.bytes.empty()) throw std::runtime_error("decoded picture asset has no bytes: " + asset.relative_path);
        writes.push_back({checked_sidecar_path(root, asset.relative_path, false), &asset.bytes});
    }
    return writes;
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
    check_output_file(output);
    const auto root = package_root_for(output);
    std::filesystem::create_directories(root);
    const auto module_path = checked_sidecar_path(root, "Module.bsl", false);
    const std::vector<std::uint8_t> xml_bytes(xml.value().begin(), xml.value().end());
    const std::vector<std::uint8_t> module_bytes(
        loaded.value().module().text.begin(), loaded.value().module().text.end());
    std::vector<PendingWrite> writes{{output, &xml_bytes}, {module_path, &module_bytes}};
    auto pictures = picture_asset_writes(loaded.value(), output);
    writes.insert(writes.end(), pictures.begin(), pictures.end());
    write_file_set(std::move(writes));
    auto diagnostics = loaded.diagnostics();
    diagnostics.insert(diagnostics.end(), xml.diagnostics().begin(), xml.diagnostics().end());
    print_success("dump", output, diagnostics, json);
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
    const auto checked_module_path = checked_sidecar_path(package_root_for(input), "Module.bsl", true);
    parsed.value().set_module(oof::model::FormModule{read_text(checked_module_path)});
    const auto encoded = oof::save_form_bin(parsed.value());
    if (!encoded) {
        print_diagnostics(encoded.diagnostics(), json);
        return 1;
    }
    write_file_set({PendingWrite{output, &encoded.value()}});
    auto diagnostics = parsed.diagnostics();
    diagnostics.insert(diagnostics.end(), encoded.diagnostics().begin(), encoded.diagnostics().end());
    print_success("build", output, diagnostics, json);
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
        if (command == "--coverage") {
            if (arguments.size() != 1) {
                print_diagnostics(
                    cli_failure("OOF0003", {}, "--coverage [--json]", "extra operands", "Wrong command arity"),
                    json);
                return 64;
            }
            print_coverage(json);
            return 0;
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
                "--coverage|dump|build",
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
