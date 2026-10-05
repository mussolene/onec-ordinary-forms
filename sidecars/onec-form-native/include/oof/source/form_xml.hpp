#pragma once

#include <string>
#include <string_view>

#include "oof/model/ordinary_form.hpp"
#include "oof/result.hpp"

namespace oof::source {

inline constexpr std::string_view ordinary_form_xml_version = "2.1";

[[nodiscard]] Result<model::OrdinaryFormDocument> parse_form_xml(
    std::string_view xml);
[[nodiscard]] Result<std::string> serialize_form_xml(
    const model::OrdinaryFormDocument& document);

}  // namespace oof::source
