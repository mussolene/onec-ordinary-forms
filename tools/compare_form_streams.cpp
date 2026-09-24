#include "oof/storage/form_bin_container.hpp"
#include "oof/storage/list_stream.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ls = oof::storage::list_stream;
namespace fb = oof::storage::formbin;

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input Form.bin");
    std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) throw std::runtime_error("failed while reading input Form.bin");
    return bytes;
}

ls::ListValue read_form_tree(const std::string& path) {
    fb::OneCContainer container;
    try {
        container = fb::parse_container(read_bytes(path));
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("invalid or truncated Form.bin container: ") + error.what());
    }
    const fb::OneCContainerFile* form = nullptr;
    for (const auto& file : container.files) {
        if (file.name == "form") {
            if (form != nullptr) throw std::runtime_error("Form.bin container has duplicate logical form files");
            form = &file;
        }
    }
    if (form == nullptr) throw std::runtime_error("no form logical file in Form.bin container");
    std::size_t offset = 0;
    if (form->payload.size() >= 3 && form->payload[0] == 0xef && form->payload[1] == 0xbb && form->payload[2] == 0xbf) offset = 3;
    const std::string text(form->payload.begin() + static_cast<std::ptrdiff_t>(offset), form->payload.end());
    try {
        return ls::parse(text);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("invalid or truncated form list stream: ") + error.what());
    }
}

std::string node_kind(const ls::ListValue& value) {
    if (value.is_list) return "list[" + std::to_string(value.items.size()) + "]";
    return value.atom_kind == ls::ListValue::AtomKind::string ? "string" : "raw";
}

std::string atom_preview(const ls::ListValue& value) {
    if (value.is_list) return node_kind(value);
    std::string out = "\"";
    const std::size_t limit = 96;
    const std::size_t count = value.atom.size() < limit ? value.atom.size() : limit;
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char ch = static_cast<unsigned char>(value.atom[i]);
        if (ch >= 0x20 && ch <= 0x7e && ch != '\\' && ch != '"') out += static_cast<char>(ch);
        else if (ch == '\\' || ch == '"') { out += '\\'; out += static_cast<char>(ch); }
        else {
            std::ostringstream hex;
            hex << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(ch);
            out += hex.str();
        }
    }
    if (value.atom.size() > count) out += "...<truncated, " + std::to_string(value.atom.size()) + " bytes>";
    out += '"';
    return out;
}

void diff(const ls::ListValue& before, const ls::ListValue& after, const std::string& path, std::size_t& changes) {
    if (before.is_list != after.is_list || (!before.is_list && before.atom_kind != after.atom_kind)) {
        std::cout << path << " kind " << node_kind(before) << " -> " << node_kind(after) << '\n';
        ++changes;
        return;
    }
    if (!before.is_list) {
        if (before.atom != after.atom) {
            std::cout << path << " " << node_kind(before) << ' ' << atom_preview(before) << " -> " << atom_preview(after) << '\n';
            ++changes;
        }
        return;
    }
    if (before.items.size() != after.items.size()) {
        std::cout << path << " arity " << before.items.size() << " -> " << after.items.size() << '\n';
        ++changes;
    }
    const std::size_t shared = before.items.size() < after.items.size() ? before.items.size() : after.items.size();
    for (std::size_t i = 0; i < shared; ++i) diff(before.items[i], after.items[i], path + "/" + std::to_string(i), changes);
    for (std::size_t i = shared; i < before.items.size(); ++i) {
        std::cout << path << '/' << i << " removed " << atom_preview(before.items[i]) << '\n';
        ++changes;
    }
    for (std::size_t i = shared; i < after.items.size(); ++i) {
        std::cout << path << '/' << i << " added " << atom_preview(after.items[i]) << '\n';
        ++changes;
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: compare_form_streams <baseline.Form.bin> <variant.Form.bin> [variant.Form.bin ...]\n"
                  << "Recursively compare parsed form list-stream trees. Paths are positional; their meaning is not inferred.\n"
                  << "Shows input atom values; module stream and container metadata are not compared.\n"
                  << "Atom previews are capped at 96 bytes.\n";
        return 0;
    }
    if (argc < 3) {
        std::cerr << "Usage: compare_form_streams <baseline.Form.bin> <variant.Form.bin> [variant.Form.bin ...]\n";
        return 2;
    }
    try {
        const auto baseline = read_form_tree(argv[1]);
        for (int arg = 2; arg < argc; ++arg) {
            const auto variant = read_form_tree(argv[arg]);
            std::cout << "COMPARE base -> variant-" << (arg - 1) << '\n';
            std::size_t changes = 0;
            diff(baseline, variant, "$", changes);
            std::cout << "CHANGE_COUNT " << changes << "\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "compare_form_streams: " << error.what() << '\n';
        return 1;
    }
}
