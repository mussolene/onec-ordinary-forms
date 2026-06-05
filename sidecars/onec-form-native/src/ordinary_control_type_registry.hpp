#pragma once

#include <array>
#include <string_view>

namespace oof::ordinary::control_type {

struct OrdinaryControlTypeBinding {
    std::string_view guid;
    std::string_view status;
    std::string_view platform_type;
    std::string_view stream_element;
    std::string_view public_xml_tag;
    std::string_view writer_control_type;
};

constexpr std::array<OrdinaryControlTypeBinding, 26> bindings{{
    {"09ccdc77-ea1a-4a6d-ab1c-3435eada2433", "platform-resource-backed", "Panel", "pnl", "Panel", "Panel"},
    {"e69bf21d-97b2-4f37-86db-675aea9ec2cb", "platform-resource-backed", "CommandBar", "cmdb", "CommandBar", "CommandBar"},
    {"6ff79819-710e-4145-97cd-1618da79e3e2", "platform-resource-backed", "Button", "btn", "Button", "Button"},
    {"151ef23e-6bb2-4681-83d0-35bc2217230c", "platform-resource-backed", "Image", "img", "PictureDecoration", "Image"},
    {"35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26", "corpus-xsd-resource-correlated", "CheckBox", "chk", "CheckBox", "CheckBox"},
    {"64483e7f-3833-48e2-8c75-2c31aac49f6e", "corpus-xsd-resource-correlated", "TextBox", "txt", "ChoiceField", "ChoiceField"},
    {"782e569a-79a7-4a4f-a936-b48d013936ec", "corpus-xsd-resource-correlated", "RadioButton", "rbtn", "RadioButton", "RadioButton"},
    {"381ed624-9217-4e63-85db-c4c3cb87daae", "platform-resource-backed", "TextBox", "txt", "InputField", "InputField"},
    {"90db814a-c75f-4b54-bc96-df62e554d67d", "corpus-xsd-correlated", "GroupBox", "grpb", "UsualGroup", "GroupBox"},
    {"36e52348-5d60-4770-8e89-a16ed50a2006", "corpus-xsd-correlated", "Separator", "sep", "Splitter", "Splitter"},
    {"a8b97779-1a4b-4059-b09c-807f86d2a461", "corpus-xsd-correlated", "Chart", "chrt", "Chart", "Chart"},
    {"a26da99e-184a-4823-b0d6-62816d38dc4e", "platform-ui-guid-table-backed", "PivotChart", "", "PivotChart", "PivotChart"},
    {"e5fdc112-5c84-4a16-9728-72b85692b6e2", "corpus-xsd-correlated", "GanttChart", "gchrt", "GanttChart", "GanttChart"},
    {"984981b1-622d-4ebc-94f7-885f0cdfb59a", "corpus-xsd-correlated", "Dendrogram", "dndrgm", "Dendrogram", "Dendrogram"},
    {"d92a805c-98ae-4750-9158-d9ce7cec2f20", "platform-resource-backed-windows-oracle-pending", "HTML", "html", "HTMLDocumentField", "HTMLDocumentField"},
    {"19f8b798-314e-4b4e-8121-905b2a7a03f5", "corpus-xsd-resource-correlated", "TextBox", "txt", "ListBox", "ListBox"},
    {"b1db1f86-abbb-4cf0-8852-fe6ae21650c2", "corpus-xsd-resource-correlated", "ProgressBar", "prgb", "ProgressBar", "ProgressBar"},
    {"6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef", "corpus-xsd-resource-correlated", "TrackBar", "trckb", "TrackBar", "TrackBar"},
    {"e3c063d8-ef92-41be-9c89-b70290b5368b", "corpus-xsd-resource-correlated", "Calendar", "clndr", "CalendarField", "CalendarField"},
    {"14c4a229-bfc3-42fe-9ce1-2da049fd0109", "corpus-xsd-correlated", "TextDocument", "txtd", "TextDocumentField", "TextDocumentField"},
    {"ad37194e-555e-4305-b718-5dca84baf145", "corpus-xsd-correlated", "GeographicalMap", "gm", "GeographicalSchemaField", "GeographicalSchemaField"},
    {"42248403-7748-49da-b782-e4438fd7bff3", "corpus-xsd-correlated", "Flowchart", "flwchrt", "GraphicalSchemaField", "GraphicalSchemaField"},
    {"ea83fe3a-ac3c-4cce-8045-3dddf35b28b1", "platform-resource-backed", "TableBox", "tbl", "Table", "Table"},
    {"236a17b3-7f44-46d9-a907-75f9cdc61ab5", "binary-guid-corpus-xsd-correlated", "Spreadsheet", "sprdsht", "SpreadsheetDocumentField", "SpreadsheetDocumentField"},
    {"0fc7e20d-f241-460c-bdf4-5ad88e5474a5", "platform-resource-backed", "Label", "lbl", "LabelDecoration", "Label"},
    {"621e95f1-064f-11d4-9400-008048da11f9", "windows-harness-required", "ActiveXControl", "", "ActiveXControl", "ActiveXControl"},
}};

constexpr const OrdinaryControlTypeBinding* binding_for_guid(std::string_view guid) {
    for (const auto& binding : bindings) {
        if (binding.guid == guid) {
            return &binding;
        }
    }
    return nullptr;
}

constexpr const OrdinaryControlTypeBinding* binding_for_public_xml_tag(std::string_view tag) {
    for (const auto& binding : bindings) {
        if (binding.public_xml_tag == tag) {
            return &binding;
        }
    }
    return nullptr;
}

constexpr const OrdinaryControlTypeBinding* binding_for_writer_control_type(std::string_view control_type) {
    for (const auto& binding : bindings) {
        if (binding.writer_control_type == control_type) {
            return &binding;
        }
    }
    return nullptr;
}

constexpr const OrdinaryControlTypeBinding* unambiguous_binding_for_platform_type(std::string_view platform_type) {
    const OrdinaryControlTypeBinding* found = nullptr;
    for (const auto& binding : bindings) {
        if (binding.platform_type != platform_type) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = &binding;
    }
    return found;
}

}  // namespace oof::ordinary::control_type
