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
    std::string root_complex_type;
    std::string variant_element;
    std::string variant_complex_type;
    std::string root_sequence;
    std::string variant_sequence;
    std::string root_attributes;
    std::string variant_attributes;
    std::string platform_members;
    std::string default_contract;
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

inline std::string entry_name(std::string_view entry, char delimiter) {
    if (delimiter == '>') {
        const std::size_t arrow = entry.find("->");
        if (arrow == std::string_view::npos) {
            return std::string(entry);
        }
        return std::string(entry.substr(0, arrow));
    }
    const std::size_t pos = entry.find(delimiter);
    if (pos == std::string_view::npos) {
        return std::string(entry);
    }
    return std::string(entry.substr(0, pos));
}

inline std::string entry_value(std::string_view entry, char delimiter) {
    if (delimiter == '>') {
        const std::size_t arrow = entry.find("->");
        if (arrow == std::string_view::npos) {
            return {};
        }
        return std::string(entry.substr(arrow + 2));
    }
    const std::size_t pos = entry.find(delimiter);
    if (pos == std::string_view::npos) {
        return {};
    }
    return std::string(entry.substr(pos + 1));
}

inline std::string lookup_entry_value(std::string_view entries, std::string_view name, char delimiter) {
    while (!entries.empty()) {
        const std::size_t pos = entries.find(delimiter == '=' ? ';' : ',');
        std::string_view entry = entries.substr(0, pos);
        while (!entry.empty() && entry.front() == ' ') {
            entry.remove_prefix(1);
        }
        while (!entry.empty() && entry.back() == ' ') {
            entry.remove_suffix(1);
        }
        if (!entry.empty() && entry_name(entry, delimiter) == name) {
            return entry_value(entry, delimiter);
        }
        if (pos == std::string_view::npos) {
            break;
        }
        entries.remove_prefix(pos + 1);
    }
    return {};
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
    if (stream_name == "realHeight") {
        return "RealHeight";
    }
    if (stream_name == "enter") {
        return "EnterKeyBehavior";
    }
    if (stream_name == "verScroll") {
        return "VerticalScrolling";
    }
    if (stream_name == "convRepr") {
        return "ConversationsRepresentation";
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

inline bool writable_for_schema_value_type(std::string_view value_type) {
    // Picture/Color/Font/Border live in the cf_form_controls_info8 value layer.
    // Their info8 object-property slot semantics are not yet proven by the
    // runtime differential oracle, so they are readable coverage gaps, never
    // writable. Writability is promoted only by a proven slot binding, not by
    // schema existence. See docs/ordinary-form-pattern-audit.md and
    // oof-native object-model-gate.
    (void)value_type;
    return false;
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
    schema.root_complex_type = std::string(control.root_complex_type);
    schema.variant_element = std::string(control.variant_element);
    schema.variant_complex_type = std::string(control.variant_complex_type);
    schema.root_sequence = std::string(control.root_sequence);
    schema.variant_sequence = std::string(control.variant_sequence);
    schema.root_attributes = std::string(control.root_attributes);
    schema.variant_attributes = std::string(control.variant_attributes);
    schema.platform_members = std::string(control.platform_members);
    schema.default_contract = std::string(control.default_contract);
    if (const auto* api = api_object_by_name(control.type_name)) {
        schema.api_source = std::string(api->api_source);
        schema.runtime_source = std::string(api->runtime_source);
        schema.persistence_source = std::string(api->persistence_source);
        schema.localization_source = std::string(api->localization_source);
        schema.api_properties = split_csv(api->sample_properties);
        schema.api_methods = split_csv(api->sample_methods);
        schema.api_events = split_csv(api->sample_events);
    }
    const std::string typed_elements =
        std::string(control.root_sequence) + "," + std::string(control.variant_sequence);
    const std::string typed_attributes =
        std::string(control.root_attributes) + "," + std::string(control.variant_attributes);
    for (const auto& typed_member : split_csv(typed_elements)) {
        const std::string member = entry_name(typed_member, ':');
        std::string value_type = entry_value(typed_member, ':');
        if (value_type.empty()) {
            value_type = xsd_value_type_for_stream_member(control, member);
        }
        const std::string platform_default = lookup_entry_value(control.default_contract, member, '=');
        const std::string default_value =
            platform_default.empty() ? xsd_default_for_stream_member(control, member, value_type) : platform_default;
        const std::string slot_codec = slot_codec_for_schema_value_type(value_type);
        schema.xsd_members.push_back({
            public_member_name(member),
            member,
            value_type,
            default_value,
            write_policy_for_schema_default(default_value),
            lookup_entry_value(control.platform_members, member, '>'),
            platform_default,
            slot_binding_for_schema_member(member, value_type),
            slot_codec,
            codec_status_for_schema_value_type(value_type),
            writable_for_schema_value_type(value_type),
            std::string(control.schema_source) + ":" + std::string(control.type_name) + "/" + member,
        });
    }
    for (const auto& typed_attribute : split_csv(typed_attributes)) {
        const std::string attribute = entry_name(typed_attribute, ':');
        std::string value_type = entry_value(typed_attribute, ':');
        if (value_type.empty()) {
            value_type = "attribute";
        }
        const std::string platform_default = lookup_entry_value(control.default_contract, attribute, '=');
        const std::string default_value =
            platform_default.empty() ? xsd_default_for_attribute(control, attribute) : platform_default;
        schema.xsd_members.push_back({
            public_member_name(attribute),
            attribute,
            value_type,
            default_value,
            write_policy_for_schema_default(default_value),
            lookup_entry_value(control.platform_members, attribute, '>'),
            platform_default,
            {},
            {},
            {},
            false,
            std::string(control.schema_source) + ":" + std::string(control.type_name) + "@" + attribute,
        });
    }
    return schema;
}

inline PlatformObjectSchema build_schema_for_root_form() {
    PlatformObjectSchema schema;
    schema.type_name = "Form";
    schema.stream_element = "Form";
    schema.schema_source =
        "platform-resource:mngcore_root-170-http_v8.1c.ru_8.2_managed-application_logform_layouter.xsd:"
        "http://v8.1c.ru/8.2/managed-application/logform/layouter";
    schema.root_complex_type = "Form";
    schema.root_attributes =
        "realHeight:xs:decimal,enter:tns:FormEnterKeyBehavior,verScroll:xs:boolean,convRepr:lf:LogFormShowConversations";
    schema.platform_members =
        "realHeight->m_realHeight,enter->m_enterKeyBehavior,verScroll->m_verticalScrolling,convRepr->m_showECSButton";
    schema.default_contract =
        "realHeight=0;enter=ControlNavigation;verScroll=false;convRepr=eLFSECShow";
    schema.runtime_source = "mngbase RTLogForm / core85 ordinary form materialization";
    schema.localization_source = "mngcore logform_layouter.xsd comments";

    for (const auto& typed_attribute : split_csv(schema.root_attributes)) {
        const std::string attribute = entry_name(typed_attribute, ':');
        const std::string value_type = entry_value(typed_attribute, ':');
        const std::string default_value = lookup_entry_value(schema.default_contract, attribute, '=');
        schema.xsd_members.push_back({
            public_member_name(attribute),
            attribute,
            value_type,
            default_value,
            write_policy_for_schema_default(default_value),
            lookup_entry_value(schema.platform_members, attribute, '>'),
            default_value,
            std::string("logform_layouter.xsd:Form@") + attribute,
            "schema-attribute",
            "xsd-root-form-attribute",
            false,
            schema.schema_source + ":Form@" + attribute,
        });
    }
    return schema;
}

inline std::vector<PlatformObjectSchema> build_platform_object_schemas() {
    std::vector<PlatformObjectSchema> schemas;
    const auto controls = form_schema::all_controls();
    schemas.reserve(controls.size() + 1);
    schemas.push_back(build_schema_for_root_form());
    for (const auto* control : controls) {
        schemas.push_back(build_schema_for_control(*control));
    }
    return schemas;
}

}  // namespace oof::platform::object_schema
