#pragma once

#include <string>

#include "oof/model/metamodel.hpp"

namespace oof::source {

struct GeneratedSchemas {
    std::string ordinary_form_xsd;
    std::string ordinary_form_palette_xsd;
};

[[nodiscard]] GeneratedSchemas generate_schemas(
    const model::metamodel::Metamodel& metamodel);

}  // namespace oof::source
