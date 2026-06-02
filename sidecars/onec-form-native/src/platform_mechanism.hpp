#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace oof::platform {

constexpr std::uint32_t cf_form_controls8 = 0x2500;
constexpr std::uint32_t cf_form_controls_position8 = 0x5500;
constexpr std::uint32_t cf_form_controls_info8 = 0x9d00;

constexpr std::size_t transfer_count_size = 4;
constexpr std::size_t info_transfer_record_size = 0x10;
constexpr std::size_t position_transfer_record_size = 0x20;
constexpr std::size_t format_entry_record_size = 0x28;

struct MechanismEntry {
    std::string_view address;
    std::string_view role;
    std::string_view platform_symbols;
    std::string_view native_target;
};

constexpr std::array<MechanismEntry, 6> mechanism_entries{{
    {
        "00255f70",
        "list-stream-write-entry",
        "core::ListOutStream::ListOutStream",
        "ListOutStream writer and Form.bin writer",
    },
    {
        "00256510",
        "list-stream-read-entry",
        "core::ListInStream::ListInStream",
        "ListInStream reader and Form.bin reader",
    },
    {
        "002709e0",
        "ordinary-control-triplet-entry",
        "cf_form_controls8, cf_form_controls_position8, cf_form_controls_info8",
        "control/position/info descriptor registry",
    },
    {
        "00270da0",
        "ordinary-control-triplet-entry",
        "cf_form_controls8, cf_form_controls_position8, cf_form_controls_info8",
        "control/position/info descriptor registry",
    },
    {
        "00270fe0",
        "ordinary-control-triplet-entry",
        "cf_form_controls8, cf_form_controls_position8, cf_form_controls_info8",
        "control/position/info descriptor registry",
    },
    {
        "002c9430",
        "ordinary-control-info-entry",
        "cf_form_controls_info8",
        "control-info shared/base records",
    },
}};

constexpr std::array<std::string_view, 15> core_value_surface{{
    "ListInStream",
    "ListOutStream",
    "TypeDomainPattern",
    "CompositeID",
    "GenericValue",
    "LocalWString",
    "FormattedString",
    "Color",
    "Font",
    "V8Border",
    "V8Picture",
    "ShortCut",
    "Date",
    "Numeric",
    "PersistenceStorage",
}};

}  // namespace oof::platform
