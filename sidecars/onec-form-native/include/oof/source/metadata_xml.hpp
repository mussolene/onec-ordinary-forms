#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <string>
#include <utility>

#include "oof/metadata_context.hpp"

namespace oof::source {

inline constexpr std::size_t metadata_xml_max_documents = 16384;
inline constexpr std::size_t metadata_xml_max_document_bytes = 16U * 1024U * 1024U;
inline constexpr std::size_t metadata_xml_max_total_bytes = 128U * 1024U * 1024U;

// A declaration index only. Imports are never loaded, and unannotated types have no UUID.
class ConfigurationTypeSchema {
public:
    static constexpr std::size_t max_declarations = 100000;

    [[nodiscard]] std::optional<std::string_view> name_for(const model::UuidValue& type_id) const;
    [[nodiscard]] bool declares(std::string_view name) const;
    [[nodiscard]] std::size_t declaration_count() const noexcept { return declarations_.size(); }
    [[nodiscard]] std::size_t identity_count() const noexcept { return identities_.size(); }

private:
    ConfigurationTypeSchema(std::set<std::string, std::less<>> declarations,
        std::map<std::string, std::string, std::less<>> identities)
        : declarations_(std::move(declarations)), identities_(std::move(identities)) {}

    std::set<std::string, std::less<>> declarations_;
    std::map<std::string, std::string, std::less<>> identities_;

    friend Result<ConfigurationTypeSchema> parse_configuration_type_schema(std::string_view xsd);
};

[[nodiscard]] Result<ConfigurationTypeSchema> parse_configuration_type_schema(std::string_view xsd);

[[nodiscard]] Result<MetadataContext> parse_metadata_context(
    std::span<const std::string> xml_documents);

}  // namespace oof::source
