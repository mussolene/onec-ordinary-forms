#pragma once

#include <array>
#include <string_view>

namespace oof::platform::descriptor {

struct DescriptorGuidBinding {
    std::string_view guid;
    std::string_view status;
    std::string_view role;
    std::string_view evidence;
};

constexpr std::array<DescriptorGuidBinding, 7> ordinary_descriptor_guid_bindings{{
    {
        "09ccdc77-ea1a-4a6d-ab1c-3435eada2433",
        "platform-resource-backed",
        "ordinary-form panel descriptor",
        "frntend_root.res names MainPanel/Panel1 and dsgnfrm.so repeated GUID block seed hit",
    },
    {
        "e69bf21d-97b2-4f37-86db-675aea9ec2cb",
        "platform-resource-backed",
        "ordinary-form command bar descriptor",
        "mngui_root.res names GroupsCommandBar/OrdersCommandBar and frntend_root.res names Toolbar*",
    },
    {
        "6ff79819-710e-4145-97cd-1618da79e3e2",
        "platform-resource-backed",
        "ordinary-form button descriptor",
        "frntend_root.res names eSelect/eClear/eOKButton/eCancelButton and other button instances",
    },
    {
        "381ed624-9217-4e63-85db-c4c3cb87daae",
        "platform-resource-backed",
        "ordinary-form input field/data editor descriptor",
        "frntend_root.res names InputFld/DataEdt and repeated value-pattern references",
    },
    {
        "ea83fe3a-ac3c-4cce-8045-3dddf35b28b1",
        "platform-resource-backed",
        "ordinary-form grid/table field descriptor",
        "mngui_root.res names Groups/Orders/Filters grids and frntend_root.res names Grid* fields",
    },
    {
        "151ef23e-6bb2-4681-83d0-35bc2217230c",
        "platform-resource-backed",
        "ordinary-form picture descriptor",
        "frntend_root.res names ePicture in picture selection form",
    },
    {
        "0fc7e20d-f241-460c-bdf4-5ad88e5474a5",
        "platform-resource-backed",
        "ordinary-form label/static text descriptor",
        "frntend_root.res names Label*/SplitLogLabel/SeanceIDSt and related static text instances",
    },
}};

inline const DescriptorGuidBinding* binding_for_guid(std::string_view guid) {
    for (const auto& binding : ordinary_descriptor_guid_bindings) {
        if (binding.guid == guid) {
            return &binding;
        }
    }
    return nullptr;
}

inline bool is_bound_descriptor_guid(std::string_view guid) {
    return binding_for_guid(guid) != nullptr;
}

}  // namespace oof::platform::descriptor
