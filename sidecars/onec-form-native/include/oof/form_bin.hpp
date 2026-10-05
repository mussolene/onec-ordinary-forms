#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "oof/model/ordinary_form.hpp"
#include "oof/result.hpp"

namespace oof {

[[nodiscard]] Result<model::OrdinaryFormDocument> load_form_bin(
    std::span<const std::uint8_t> bytes,
    std::string_view form_name = "Form");

[[nodiscard]] Result<std::vector<std::uint8_t>> save_form_bin(
    const model::OrdinaryFormDocument& document);

}  // namespace oof
