#include "oof/form_bin.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oof/diagnostic.hpp"
#include "oof/storage/form_bin_container.hpp"
#include "oof/storage/form_stream.hpp"
#include "oof/storage/list_stream.hpp"

namespace oof {
namespace {

constexpr std::array<std::uint8_t, 3> utf8_bom{0xef, 0xbb, 0xbf};

Diagnostic diagnostic(
    std::string code,
    std::string path,
    std::string expected,
    std::string actual,
    std::string message) {
    return Diagnostic{
        std::move(code),
        DiagnosticSeverity::error,
        {},
        std::move(path),
        {},
        std::move(expected),
        std::move(actual),
        std::move(message),
    };
}

std::string utf8_payload(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    if (bytes.size() >= utf8_bom.size() &&
        std::equal(utf8_bom.begin(), utf8_bom.end(), bytes.begin())) {
        offset = utf8_bom.size();
    }
    return std::string(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.end());
}

std::vector<std::uint8_t> bom_payload(std::string_view text) {
    std::vector<std::uint8_t> bytes(utf8_bom.begin(), utf8_bom.end());
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

std::string canonical_module_newlines(std::string_view text, bool platform_crlf) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
            normalized.append(platform_crlf ? "\r\n" : "\n");
            ++index;
        } else if (character == '\n') {
            normalized.append(platform_crlf ? "\r\n" : "\n");
        } else {
            normalized.push_back(character);
        }
    }
    return normalized;
}

}  // namespace

Result<model::OrdinaryFormDocument> load_form_bin(
    std::span<const std::uint8_t> bytes,
    std::string_view form_name) {
    try {
        const storage::formbin::OneCContainer container =
            storage::formbin::parse_container({bytes.begin(), bytes.end()});
        const storage::formbin::OneCContainerFile* form_file = nullptr;
        const storage::formbin::OneCContainerFile* module_file = nullptr;
        for (const auto& file : container.files) {
            const auto** target = file.name == "form"
                                      ? &form_file
                                      : file.name == "module" ? &module_file : nullptr;
            if (target == nullptr) {
                return Result<model::OrdinaryFormDocument>::failure({diagnostic(
                    "OOF1202",
                    "Form.bin/" + file.name,
                    "logical file form or module",
                    file.name,
                    "Form.bin contains an unsupported logical file")});
            }
            if (*target != nullptr) {
                return Result<model::OrdinaryFormDocument>::failure({diagnostic(
                    "OOF1202",
                    "Form.bin/" + file.name,
                    "one logical file",
                    "duplicate",
                    "Form.bin contains a duplicate logical file")});
            }
            *target = &file;
        }
        if (form_file == nullptr || module_file == nullptr || container.files.size() != 2) {
            return Result<model::OrdinaryFormDocument>::failure({diagnostic(
                "OOF1201",
                "Form.bin",
                "exactly form and module logical files",
                std::to_string(container.files.size()) + " files",
                "Form.bin does not contain the product storage pair")});
        }

        storage::list_stream::ListValue payload;
        try {
            payload = storage::list_stream::parse(utf8_payload(form_file->payload));
        } catch (const std::exception& error) {
            return Result<model::OrdinaryFormDocument>::failure({diagnostic(
                "OOF1203",
                "Form.bin/form",
                "ordinary-form list stream",
                {},
                error.what())});
        }
        auto decoded = storage::form_stream::decode_document(payload, form_name);
        if (!decoded) {
            return Result<model::OrdinaryFormDocument>::failure(decoded.diagnostics());
        }
        auto document = decoded.take_value();
        document.set_module(model::FormModule{
            canonical_module_newlines(utf8_payload(module_file->payload), false)});
        return Result<model::OrdinaryFormDocument>::success(std::move(document));
    } catch (const std::exception& error) {
        return Result<model::OrdinaryFormDocument>::failure({diagnostic(
            "OOF1200",
            "Form.bin",
            "valid 1C container",
            {},
            error.what())});
    }
}

Result<std::vector<std::uint8_t>> save_form_bin(
    const model::OrdinaryFormDocument& document) {
    try {
        auto encoded = storage::form_stream::encode_document(document);
        if (!encoded) {
            return Result<std::vector<std::uint8_t>>::failure(encoded.diagnostics());
        }
        const std::string form_text = storage::list_stream::dump_listout(encoded.value());
        storage::formbin::OneCContainer container;
        container.block_size = storage::formbin::container_block_size;
        container.files.push_back({"form", 0, 0, bom_payload(form_text)});
        const std::string module_text = canonical_module_newlines(document.module().text, true);
        container.files.push_back({"module", 0, 0, bom_payload(module_text)});
        auto bytes = storage::formbin::serialize_container(container);

        const auto verified = load_form_bin(bytes, document.form().name);
        if (!verified) {
            return Result<std::vector<std::uint8_t>>::failure(verified.diagnostics());
        }
        return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
    } catch (const std::exception& error) {
        return Result<std::vector<std::uint8_t>>::failure({diagnostic(
            "OOF1200",
            "Form.bin",
            "encodable ordinary-form document",
            {},
            error.what())});
    }
}

}  // namespace oof
