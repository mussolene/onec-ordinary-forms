#pragma once

#include <string>
#include <string_view>

#include "oof/model/ordinary_form.hpp"
#include "oof/result.hpp"

namespace oof::source {

class ConfigurationTypeSchema;

inline constexpr std::string_view ordinary_form_xml_version = "2.1";

[[nodiscard]] Result<model::OrdinaryFormDocument> parse_form_xml(
    std::string_view xml,
    std::shared_ptr<const MetadataContext> metadata_context = {},
    const ConfigurationTypeSchema* types = nullptr);
[[nodiscard]] Result<std::string> serialize_form_xml(
    const model::OrdinaryFormDocument& document,
    const ConfigurationTypeSchema* types = nullptr);

}  // namespace oof::source
