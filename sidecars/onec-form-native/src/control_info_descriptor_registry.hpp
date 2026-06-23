#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace oof::platform::control_info {

struct InfoSlotDescriptor {
    std::string_view name;
    std::uint16_t index;
};

struct ControlInfoDescriptor {
    std::string_view control_type;
    std::string_view info_kind;
    std::array<InfoSlotDescriptor, 16> slots;
    std::size_t slot_count;
    std::string_view evidence;
};

constexpr std::array<ControlInfoDescriptor, 27> descriptors = {{
    {"FormRootPanel", "1", {{{"BaseInfo", 0}, {"PageStates", 17}, {"PagePositions", 21}}}, 3, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"Panel", "1", {{{"BaseInfo", 0}, {"PageStates", 42}, {"PagePositions", 46}}}, 3, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"CommandBar", "2", {{{"BaseInfo", 0}, {"Autofill", 6}}}, 2, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"InputField", "9", {{{"BaseInfo", 0}, {"EditMode", 3}, {"WordWrap", 4}, {"PasswordMode", 5}, {"ChoiceListButton", 6}, {"ChoiceButton", 7}, {"ClearButton", 8}, {"OpenButton", 10}, {"TextEditing", 12}, {"ReadOnly", 13}, {"Mask", 21}, {"MultiLine", 26}, {"Format", 34}, {"AutoMarkIncomplete", 35}, {"ExtendedEdit", 38}}}, 15, "platform oracle inputfield-property-matrix-20260623-171653 + native input_field_info_record writer slots"},
    {"CheckBox", "1", {{{"InnerInfo", 0}, {"BodyKind", 1}}}, 2, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"ChoiceField", "2", {{{"BaseInfo", 0}, {"ReadOnly", 12}, {"ChoiceButton", 23}, {"ClearButton", 24}, {"OpenButton", 25}, {"ChoiceListOrCreateButton", 26}, {"EditButton", 27}}}, 7, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"RadioButton", "4", {{{"TypeDomainPattern", 1}, {"InnerInfo", 2}, {"DataValue", 4}, {"Actions", 5}}}, 4, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"ProgressBar", "0", {{{"BaseInfo", 0}, {"Orientation", 1}, {"MinimumValue", 2}, {"MaximumValue", 3}, {"Step", 4}, {"BigStep", 5}, {"ShowPercent", 6}, {"DisplayStyle", 7}}}, 8, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"TrackBar", "1", {{{"BaseInfo", 0}, {"MinimumValue", 2}, {"MaximumValue", 3}, {"Step", 4}, {"BigStep", 5}, {"Orientation", 6}, {"MarkStep", 8}, {"CurrentValue", 9}}}, 8, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"CalendarField", "1", {{{"BaseInfo", 0}, {"PeriodStart", 5}, {"PeriodEnd", 6}}}, 3, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"ListBox", "1", {{{"BaseInfo", 0}, {"View", 1}, {"ViewKind", 2}}}, 3, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"GroupBox", "0", {{{"BaseInfo", 0}, {"Title", 2}, {"Decoration", 3}}}, 3, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"Splitter", "0", {{{"BaseInfo", 0}, {"Orientation", 2}}}, 2, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"HTMLDocumentField", "5", {{{"Actions", 2}, {"BackColor", 3}, {"Border", 4}}}, 3, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"TextDocumentField", "6", {{{"BaseInfo", 0}, {"Uuid", 3}}}, 2, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"GeographicalSchemaField", "19", {{{"Output", 1}, {"Visible", 11}, {"ToolTip", 12}, {"Scale", 15}, {"BaseStyleMode", 16}, {"BaseStyleState", 17}, {"BaseStyleVisible", 18}, {"BaseStyleDefaultMode", 19}}}, 8, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"GraphicalSchemaField", "5", {{{"BaseInfo", 0}, {"Settings", 2}, {"Actions", 3}, {"HasActions", 4}}}, 4, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"SpreadsheetDocumentField", "18", {{{"Left", 1}, {"Top", 2}, {"Right", 3}, {"Bottom", 4}, {"BackColor", 9}, {"Actions", 17}}}, 6, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"ActiveXControl", "3", {{{"Clsid", 1}, {"State1", 4}, {"State2", 8}}}, 3, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"Chart", "11", {{}}, 0, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"PivotChart", "3", {{{"Body", 1}, {"Secondary", 2}}}, 2, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"GanttChart", "19", {{{"Body", 1}}}, 1, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"Dendrogram", "0", {{{"Body", 1}}}, 1, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"Button", "1", {{{"BaseInfo", 0}, {"Title", 2}, {"Picture", 8}, {"DefaultButton", 15}}}, 4, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"Label", "3", {{{"BaseInfo", 0}, {"Title", 2}, {"Hyperlink", 5}, {"PictureSize", 11}, {"PictureStyleGroup", 12}, {"TextPosition", 13}}}, 6, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"Image", "1", {{{"BaseInfo", 0}, {"DisplayMode", 2}, {"DisplayState", 3}, {"PictureStyleGroup", 4}, {"RenderingProfileFlag", 13}}}, 5, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
    {"Table", "5", {{{"BaseInfo", 0}, {"View", 1}, {"RowsCount", 20}, {"ColumnsCount", 21}, {"AutoMarkIncomplete", 22}}}, 5, "git:15583d8 ordinary_stream.py CONTROL_INFO_SLOT_DESCRIPTORS"},
}};

constexpr std::array<std::string_view, 26> writer_descriptor_controls = {{
    "Panel",
    "ActiveXControl",
    "Button",
    "Image",
    "CheckBox",
    "ChoiceField",
    "RadioButton",
    "InputField",
    "GroupBox",
    "Splitter",
    "Chart",
    "PivotChart",
    "GanttChart",
    "Dendrogram",
    "HTMLDocumentField",
    "ListBox",
    "ProgressBar",
    "TrackBar",
    "CalendarField",
    "TextDocumentField",
    "GeographicalSchemaField",
    "GraphicalSchemaField",
    "CommandBar",
    "Table",
    "SpreadsheetDocumentField",
    "Label",
}};

constexpr const ControlInfoDescriptor* descriptor_for_control_type(std::string_view control_type) {
    for (const auto& descriptor : descriptors) {
        if (descriptor.control_type == control_type) {
            return &descriptor;
        }
    }
    return nullptr;
}

constexpr bool has_writer_descriptor(std::string_view control_type) {
    for (const auto& writer_control : writer_descriptor_controls) {
        if (writer_control == control_type) {
            return true;
        }
    }
    return false;
}

constexpr std::optional<std::uint16_t> slot_index(const ControlInfoDescriptor& descriptor, std::string_view name) {
    for (std::size_t index = 0; index < descriptor.slot_count; ++index) {
        if (descriptor.slots[index].name == name) {
            return descriptor.slots[index].index;
        }
    }
    return std::nullopt;
}

struct PublicXmlOrderDescriptor {
    std::string_view control_type;
    std::array<std::string_view, 24> properties;
    std::size_t property_count;
    std::string_view evidence;
};

constexpr std::array<PublicXmlOrderDescriptor, 2> public_xml_order_descriptors = {{
    {
        "Button",
        {{
            "Picture",
        }},
        1,
        "OrdinaryForm public XML projection order for descriptor-backed Button properties; Title/Visible/Enabled are structural, DefaultButton pending property promotion",
    },
    {
        "InputField",
        {{
            "DataPath",
            "ToolTip",
            "EditMode",
            "WordWrap",
            "PasswordMode",
            "ChoiceListButton",
            "ChoiceButton",
            "ClearButton",
            "OpenButton",
            "TextEditing",
            "ReadOnly",
            "Mask",
            "MultiLine",
            "Format",
            "AutoMarkIncomplete",
            "ExtendedEdit",
        }},
        16,
        "OrdinaryForm public XML projection order for InputField; storage order remains cf_form_controls_info8 slots",
    },
}};

constexpr const PublicXmlOrderDescriptor* public_xml_order_for_control_type(std::string_view control_type) {
    for (const auto& descriptor : public_xml_order_descriptors) {
        if (descriptor.control_type == control_type) {
            return &descriptor;
        }
    }
    return nullptr;
}

constexpr std::size_t descriptor_count() {
    return descriptors.size();
}

constexpr std::size_t writer_descriptor_count() {
    return writer_descriptor_controls.size();
}

constexpr std::size_t writable_promotion_count() {
    return 0;
}

} // namespace oof::platform::control_info
