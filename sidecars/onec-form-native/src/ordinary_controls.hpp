#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "platform_mechanism.hpp"

namespace oof::platform::ordinary {

enum class TransferFacet {
    controls,
    position,
    info,
};

struct TransferDescriptor {
    std::string_view symbol;
    TransferFacet facet;
    std::uint32_t format_id;
    std::uint32_t record_size;
    std::string_view platform_role;
    std::string_view native_role;
};

constexpr std::array<TransferDescriptor, 3> transfer_registry{{
    {
        "cf_form_controls8",
        TransferFacet::controls,
        cf_form_controls8,
        format_entry_record_size,
        "ordinary control payload records",
        "control descriptor payload codec",
    },
    {
        "cf_form_controls_position8",
        TransferFacet::position,
        cf_form_controls_position8,
        position_transfer_record_size,
        "ordinary control geometry/binding position records",
        "position and binding descriptor codec",
    },
    {
        "cf_form_controls_info8",
        TransferFacet::info,
        cf_form_controls_info8,
        info_transfer_record_size,
        "ordinary control shared/base info records",
        "control-info descriptor codec",
    },
}};

struct TripletEntryPoint {
    std::string_view address;
    std::string_view role;
};

constexpr std::array<TripletEntryPoint, 4> triplet_entry_points{{
    {"002709e0", "full control/position/info triplet"},
    {"00270da0", "full control/position/info triplet"},
    {"00270fe0", "full control/position/info triplet"},
    {"002c9430", "info-only entry"},
}};

constexpr std::uint32_t format_id_for(TransferFacet facet) {
    for (const auto& descriptor : transfer_registry) {
        if (descriptor.facet == facet) {
            return descriptor.format_id;
        }
    }
    return 0;
}

constexpr std::uint32_t record_size_for(TransferFacet facet) {
    for (const auto& descriptor : transfer_registry) {
        if (descriptor.facet == facet) {
            return descriptor.record_size;
        }
    }
    return 0;
}

}  // namespace oof::platform::ordinary
