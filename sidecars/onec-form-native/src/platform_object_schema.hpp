#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "platform_form_schema.hpp"
#include "platform_runtime_binding.hpp"

namespace oof::platform::object_schema {

struct PlatformObjectSchemaMember {
    std::string name;
    std::string stream_name;
    std::string value_type;
    std::string default_value;
    std::string write_policy;
    std::string platform_member;
    std::string platform_default;
    std::string slot_binding;
    std::string slot_codec;
    std::string codec_status;
    bool writable = false;
    std::string source;
};

struct PlatformObjectSchema {
    std::string type_name;
    std::string stream_element;
    std::string schema_source;
    std::string api_source;
    std::string runtime_source;
    std::string persistence_source;
    std::string localization_source;
    std::vector<PlatformObjectSchemaMember> xsd_members;
    std::vector<std::string> api_properties;
    std::vector<std::string> api_methods;
    std::vector<std::string> api_events;
};

inline std::vector<std::string> split_csv(std::string_view text) {
    std::vector<std::string> result;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        std::string_view value = text.substr(0, comma);
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        while (!value.empty() && value.back() == ' ') {
            value.remove_suffix(1);
        }
        if (!value.empty()) {
            result.emplace_back(value);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return result;
}

inline std::string public_member_name(std::string_view stream_name) {
    if (stream_name == "tooltip") {
        return "ToolTip";
    }
    if (stream_name == "shortCut" || stream_name == "stcut") {
        return "Shortcut";
    }
    if (stream_name == "pic" || stream_name == "picVal") {
        return "Picture";
    }
    if (stream_name == "txtClr") {
        return "TextColor";
    }
    if (stream_name == "bkClr") {
        return "BackColor";
    }
    if (stream_name == "brdClr") {
        return "BorderColor";
    }
    if (stream_name == "fnt") {
        return "Font";
    }
    if (stream_name == "brd") {
        return "Border";
    }
    if (stream_name == "cntm") {
        return "ContextMenu";
    }
    if (stream_name == "cmd") {
        return "Command";
    }
    if (stream_name == "msep") {
        return "MenuSeparator";
    }
    if (stream_name == "page") {
        return "Page";
    }
    if (stream_name == "lbl") {
        return "Label";
    }
    if (stream_name == "txt") {
        return "TextBox";
    }
    if (stream_name == "chk") {
        return "CheckBox";
    }
    if (stream_name == "btn") {
        return "Button";
    }
    if (stream_name == "rbtn") {
        return "RadioButton";
    }
    if (stream_name == "cmdb") {
        return "CommandBar";
    }
    if (stream_name == "tbl") {
        return "TableBox";
    }
    if (stream_name == "grpb") {
        return "GroupBox";
    }
    if (stream_name == "pnl") {
        return "Panel";
    }
    if (stream_name == "img") {
        return "Image";
    }
    if (stream_name == "sep") {
        return "Separator";
    }
    return std::string(stream_name);
}

inline std::string xsd_value_type_for_stream_member(
    const form_schema::PlatformFormSchemaControl& control,
    std::string_view stream_name
) {
    if (stream_name == "pic" || stream_name == "picVal" || stream_name == "choiceBtnPic" ||
        stream_name == "rowsPic" || stream_name == "headerPic" || stream_name == "footerPic") {
        return "ui:Picture";
    }
    if (stream_name == "txtClr" || stream_name == "bkClr" || stream_name == "brdClr") {
        return "ui:Color";
    }
    if (stream_name == "fnt") {
        return "ui:Font";
    }
    if (stream_name == "brd") {
        return "ui:Border";
    }
    if (stream_name == "shortCut" || stream_name == "stcut") {
        return "ui:ShortCutType";
    }
    if (stream_name == "cntm") {
        return "ContextMenu";
    }
    if (stream_name == "page") {
        return "Page";
    }
    if (stream_name == "cmd") {
        return "Command";
    }
    if (stream_name == "menu") {
        return "Submenu";
    }
    if (stream_name == "msep") {
        return "MenuSeparator";
    }
    if (stream_name == "tooltip") {
        return "xs:string";
    }
    (void)control;
    return "Value";
}

inline std::string xsd_default_for_stream_member(
    const form_schema::PlatformFormSchemaControl& control,
    std::string_view stream_name,
    std::string_view value_type
) {
    if (value_type == "ui:Picture") {
        return "V8Picture()";
    }
    if (value_type == "ui:Font") {
        return "Font(Font::eAutoFont, 0)";
    }
    if (value_type == "ui:Color") {
        if (stream_name == "brdClr") {
            if (control.type_name == "TextBox" || control.type_name == "CheckBox" ||
                control.type_name == "RadioButton" || control.type_name == "SpreadSheet" ||
                control.type_name == "TextDocument" || control.type_name == "FormattedDocument" ||
                control.type_name == "Calendar" || control.type_name == "ProgressBar" ||
                control.type_name == "TrackBar" || control.type_name == "Chart" ||
                control.type_name == "GeographicalSchema" ||
                control.type_name == "Dendrogram" || control.type_name == "Flowchart" ||
                control.type_name == "HTMLDocument" || control.type_name == "GraphicalSchema") {
                return "Color(Color::eV8Color, IV8Style::eBorderColor)";
            }
        }
        if (control.type_name == "Button" && stream_name == "txtClr") {
            return "Color(Color::eV8Color, IV8Style::eButtonTextColor)";
        }
        if (control.type_name == "Button" && stream_name == "bkClr") {
            return "Color(Color::eV8Color, IV8Style::eButtonBkgrndColor)";
        }
        if (control.type_name == "Button" && stream_name == "brdClr") {
            return "Color(Color::eV8Color, IV8Style::eBorderColor)";
        }
        return "Color(Color::eAutoColor, 0)";
    }
    if (value_type == "ui:Border") {
        return "Border()";
    }
    if (stream_name == "tooltip" || stream_name == "shortCut" || stream_name == "stcut" ||
        stream_name == "cntm" || stream_name == "cmd" || stream_name == "menu" ||
        stream_name == "msep") {
        return "empty";
    }
    return {};
}

inline std::string xsd_default_for_attribute(
    const form_schema::PlatformFormSchemaControl& control,
    std::string_view attribute
) {
    if (attribute == "defBtn" || attribute == "depBtn" || attribute == "threeState" ||
        attribute == "hyper" || attribute == "listChoiceMode" || attribute == "secCB") {
        return "false";
    }
    if (attribute == "wrap" || attribute == "choice" || attribute == "textEdit") {
        return "true";
    }
    if (attribute == "hAlign") {
        return "Left";
    }
    if (attribute == "vAlign") {
        return "Center";
    }
    if (attribute == "pwd" || attribute == "markNegatives" || attribute == "choiceBtn" ||
        attribute == "clearBtn" || attribute == "spinBtn" || attribute == "openBtn" ||
        attribute == "multiLine") {
        return "auto";
    }
    (void)control;
    return {};
}

inline std::string write_policy_for_schema_default(std::string_view default_value) {
    if (default_value.empty()) {
        return "explicit";
    }
    return "omit-when-default";
}

inline std::string platform_member_for_stream_member(std::string_view stream_name) {
    if (stream_name == "tooltip") {
        return "m_tooltip";
    }
    if (stream_name == "shortCut" || stream_name == "stcut") {
        return "m_shortCut";
    }
    if (stream_name == "pic") {
        return "m_picture";
    }
    if (stream_name == "picVal" || stream_name == "picValues") {
        return "m_pictureValues";
    }
    if (stream_name == "choiceBtnPic") {
        return "m_choiceButtonPicture";
    }
    if (stream_name == "rowsPic") {
        return "m_rowsPicture";
    }
    if (stream_name == "headerPic") {
        return "m_headerPicture";
    }
    if (stream_name == "footerPic") {
        return "m_footerPicture";
    }
    if (stream_name == "txtClr") {
        return "m_textColor";
    }
    if (stream_name == "bkClr") {
        return "m_backColor";
    }
    if (stream_name == "brdClr") {
        return "m_borderColor";
    }
    if (stream_name == "fnt") {
        return "m_font";
    }
    if (stream_name == "brd") {
        return "m_border";
    }
    if (stream_name == "cntm") {
        return "m_contextMenu";
    }
    return {};
}

inline std::string slot_codec_for_schema_value_type(std::string_view value_type) {
    if (value_type == "ui:Picture") {
        return "picture-record";
    }
    if (value_type == "ui:Font") {
        return "font-record";
    }
    if (value_type == "ui:Color") {
        return "color-record";
    }
    if (value_type == "ui:Border") {
        return "border-record";
    }
    return {};
}

inline std::string slot_binding_for_schema_member(
    std::string_view stream_name,
    std::string_view value_type
) {
    if (slot_codec_for_schema_value_type(value_type).empty()) {
        return {};
    }
    return "cf_form_controls_info8:" + std::string(stream_name);
}

inline std::string codec_status_for_schema_value_type(std::string_view value_type) {
    if (slot_codec_for_schema_value_type(value_type).empty()) {
        return {};
    }
    return "pending-info8-codec";
}

inline const runtime_binding::PlatformApiObject* api_object_by_name(std::string_view name) {
    for (const auto& object : runtime_binding::api_objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

inline PlatformObjectSchema build_schema_for_control(const form_schema::PlatformFormSchemaControl& control) {
    PlatformObjectSchema schema;
    schema.type_name = std::string(control.type_name);
    schema.stream_element = std::string(control.stream_element);
    schema.schema_source = std::string(control.schema_source);
    if (const auto* api = api_object_by_name(control.type_name)) {
        schema.api_source = std::string(api->api_source);
        schema.runtime_source = std::string(api->runtime_source);
        schema.persistence_source = std::string(api->persistence_source);
        schema.localization_source = std::string(api->localization_source);
        schema.api_properties = split_csv(api->sample_properties);
        schema.api_methods = split_csv(api->sample_methods);
        schema.api_events = split_csv(api->sample_events);
    }
    for (const auto& member : split_csv(control.child_elements)) {
        const std::string value_type = xsd_value_type_for_stream_member(control, member);
        const std::string default_value = xsd_default_for_stream_member(control, member, value_type);
        const std::string slot_codec = slot_codec_for_schema_value_type(value_type);
        schema.xsd_members.push_back({
            public_member_name(member),
            member,
            value_type,
            default_value,
            write_policy_for_schema_default(default_value),
            platform_member_for_stream_member(member),
            default_value,
            slot_binding_for_schema_member(member, value_type),
            slot_codec,
            codec_status_for_schema_value_type(value_type),
            false,
            std::string(control.schema_source) + ":" + std::string(control.type_name) + "/" + member,
        });
    }
    for (const auto& attribute : split_csv(control.attributes)) {
        const std::string default_value = xsd_default_for_attribute(control, attribute);
        schema.xsd_members.push_back({
            public_member_name(attribute),
            attribute,
            "attribute",
            default_value,
            write_policy_for_schema_default(default_value),
            {},
            default_value,
            {},
            {},
            {},
            false,
            std::string(control.schema_source) + ":" + std::string(control.type_name) + "@" + attribute,
        });
    }
    return schema;
}

inline std::vector<PlatformObjectSchema> build_platform_object_schemas() {
    std::vector<PlatformObjectSchema> schemas;
    schemas.reserve(form_schema::logform_layouter_controls.size());
    for (const auto& control : form_schema::logform_layouter_controls) {
        schemas.push_back(build_schema_for_control(control));
    }
    return schemas;
}

}  // namespace oof::platform::object_schema
