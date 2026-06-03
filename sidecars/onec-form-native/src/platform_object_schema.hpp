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
        schema.xsd_members.push_back({
            public_member_name(member),
            member,
            xsd_value_type_for_stream_member(control, member),
            std::string(control.schema_source) + ":" + std::string(control.type_name) + "/" + member,
        });
    }
    for (const auto& attribute : split_csv(control.attributes)) {
        schema.xsd_members.push_back({
            public_member_name(attribute),
            attribute,
            "attribute",
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
