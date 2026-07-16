#include "oof/model/metamodel.hpp"

#include <array>
#include <stdexcept>

namespace oof::model::metamodel {
namespace {

constexpr auto all_versions = VersionMask::all_supported;

constexpr std::array<ControlDescriptor, control_kind_count> descriptors{{
    {ControlKind::panel, "09ccdc77-ea1a-4a6d-ab1c-3435eada2433", "pnl", "Panel", "Panel", u8"Панель", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::ordered_controls},
    {ControlKind::command_bar, "e69bf21d-97b2-4f37-86db-675aea9ec2cb", "cmdb", "CommandBar", "CommandBar", u8"КоманднаяПанель", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::button, "6ff79819-710e-4145-97cd-1618da79e3e2", "btn", "Button", "Button", u8"Кнопка", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::picture_decoration, "151ef23e-6bb2-4681-83d0-35bc2217230c", "img", "PictureDecoration", "Image", u8"ПолеКартинки", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::check_box, "35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26", "chk", "CheckBox", "CheckBox", u8"Флажок", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::choice_field, "64483e7f-3833-48e2-8c75-2c31aac49f6e", "txt", "ChoiceField", "ChoiceField", u8"ПолеВыбора", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::radio_button, "782e569a-79a7-4a4f-a936-b48d013936ec", "rbtn", "RadioButton", "RadioButton", u8"Переключатель", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::input_field, "381ed624-9217-4e63-85db-c4c3cb87daae", "txt", "InputField", "InputField", u8"ПолеВвода", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::usual_group, "90db814a-c75f-4b54-bc96-df62e554d67d", "grpb", "UsualGroup", "GroupBox", u8"РамкаГруппы", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::ordered_controls},
    {ControlKind::splitter, "36e52348-5d60-4770-8e89-a16ed50a2006", "sep", "Splitter", "Splitter", u8"Разделитель", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::chart, "a8b97779-1a4b-4059-b09c-807f86d2a461", "chrt", "Chart", "Chart", u8"Диаграмма", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::pivot_chart, "a26da99e-184a-4823-b0d6-62816d38dc4e", "", "PivotChart", "PivotChart", u8"СводнаяДиаграмма", all_versions, ClassificationStatus::platform_ui_guid_table_backed, ChildPolicy::forbidden},
    {ControlKind::gantt_chart, "e5fdc112-5c84-4a16-9728-72b85692b6e2", "gchrt", "GanttChart", "GanttChart", u8"ДиаграммаГанта", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::dendrogram, "984981b1-622d-4ebc-94f7-885f0cdfb59a", "dndrgm", "Dendrogram", "Dendrogram", u8"Дендрограмма", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::html_document_field, "d92a805c-98ae-4750-9158-d9ce7cec2f20", "html", "HTMLDocumentField", "HTMLDocumentField", u8"ПолеHTMLДокумента", all_versions, ClassificationStatus::platform_resource_backed_windows_oracle_pending, ChildPolicy::forbidden},
    {ControlKind::list_box, "19f8b798-314e-4b4e-8121-905b2a7a03f5", "txt", "ListBox", "ListBox", u8"ПолеСписка", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::progress_bar, "b1db1f86-abbb-4cf0-8852-fe6ae21650c2", "prgb", "ProgressBar", "ProgressBar", u8"Индикатор", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::track_bar, "6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef", "trckb", "TrackBar", "TrackBar", u8"ПолосаРегулирования", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::calendar_field, "e3c063d8-ef92-41be-9c89-b70290b5368b", "clndr", "CalendarField", "CalendarField", u8"ПолеКалендаря", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::text_document_field, "14c4a229-bfc3-42fe-9ce1-2da049fd0109", "txtd", "TextDocumentField", "TextDocumentField", u8"ПолеТекстовогоДокумента", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::geographical_schema_field, "ad37194e-555e-4305-b718-5dca84baf145", "gm", "GeographicalSchemaField", "GeographicalSchemaField", u8"ПолеГеографическойСхемы", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::graphical_schema_field, "42248403-7748-49da-b782-e4438fd7bff3", "flwchrt", "GraphicalSchemaField", "GraphicalSchemaField", u8"ПолеГрафическойСхемы", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::table, "ea83fe3a-ac3c-4cce-8045-3dddf35b28b1", "tbl", "Table", "Table", u8"ТабличноеПоле", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::spreadsheet_document_field, "236a17b3-7f44-46d9-a907-75f9cdc61ab5", "sprdsht", "SpreadsheetDocumentField", "SpreadsheetDocumentField", u8"ПолеТабличногоДокумента", all_versions, ClassificationStatus::binary_guid_corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::label_decoration, "0fc7e20d-f241-460c-bdf4-5ad88e5474a5", "lbl", "LabelDecoration", "Label", u8"Надпись", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::active_x_control, "621e95f1-064f-11d4-9400-008048da11f9", "", "ActiveXControl", "ActiveXControl", u8"ЭлементУправленияActiveX", all_versions, ClassificationStatus::windows_harness_required, ChildPolicy::forbidden},
}};

static_assert(descriptors.size() == 26);

}  // namespace

std::span<const ControlDescriptor> control_descriptors() noexcept {
    return descriptors;
}

const ControlDescriptor& descriptor_for(ControlKind kind) {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= descriptors.size()) {
        throw std::out_of_range("unknown ordinary-form control kind");
    }
    return descriptors[index];
}

const ControlDescriptor* find_by_guid(std::string_view guid) noexcept {
    for (const auto& descriptor : descriptors) {
        if (descriptor.guid == guid) {
            return &descriptor;
        }
    }
    return nullptr;
}

const ControlDescriptor* find_by_public_name(std::string_view name) noexcept {
    for (const auto& descriptor : descriptors) {
        if (descriptor.public_name == name) {
            return &descriptor;
        }
    }
    return nullptr;
}

const ControlDescriptor* find_by_api_name(std::string_view name) noexcept {
    for (const auto& descriptor : descriptors) {
        if (descriptor.api_name == name) {
            return &descriptor;
        }
    }
    return nullptr;
}

const ControlDescriptor* find_by_russian_name(std::u8string_view name) noexcept {
    for (const auto& descriptor : descriptors) {
        if (descriptor.russian_name == name) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::string_view classification_name(ClassificationStatus status) noexcept {
    switch (status) {
        case ClassificationStatus::platform_resource_backed:
            return "platform-resource-backed";
        case ClassificationStatus::corpus_xsd_resource_correlated:
            return "corpus-xsd-resource-correlated";
        case ClassificationStatus::corpus_xsd_correlated:
            return "corpus-xsd-correlated";
        case ClassificationStatus::platform_ui_guid_table_backed:
            return "platform-ui-guid-table-backed";
        case ClassificationStatus::platform_resource_backed_windows_oracle_pending:
            return "platform-resource-backed-windows-oracle-pending";
        case ClassificationStatus::binary_guid_corpus_xsd_correlated:
            return "binary-guid-corpus-xsd-correlated";
        case ClassificationStatus::windows_harness_required:
            return "windows-harness-required";
    }
    return "unknown";
}

}  // namespace oof::model::metamodel
