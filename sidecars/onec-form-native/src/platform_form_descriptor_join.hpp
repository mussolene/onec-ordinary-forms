#pragma once

#include <array>
#include <string_view>

#include "platform_descriptor_registry.hpp"
#include "platform_form_schema.hpp"

namespace oof::platform::form_descriptor {

struct DescriptorSchemaBinding {
    std::string_view guid;
    std::string_view status;
    std::string_view platform_type;
    std::string_view stream_element;
    std::string_view role;
    std::string_view field_kind_symbol;
    std::string_view type_presentation_symbol;
    std::string_view corpus_object_names;
    std::string_view evidence;
};

constexpr std::array<DescriptorSchemaBinding, 25> descriptor_schema_bindings{{
    {
        "09ccdc77-ea1a-4a6d-ab1c-3435eada2433",
        "platform-resource-backed",
        "Panel",
        "pnl",
        "ordinary-form panel descriptor",
        "",
        "",
        "",
        "existing descriptor registry; logform layouter XSD Panel/pnl complexType",
    },
    {
        "e69bf21d-97b2-4f37-86db-675aea9ec2cb",
        "platform-resource-backed",
        "CommandBar",
        "cmdb",
        "ordinary-form command bar descriptor",
        "",
        "",
        "КоманднаяПанель2,КоманднаяПанель3",
        "existing descriptor registry; logform layouter XSD CommandBar/cmdb; all-controls Form.bin names use command-bar prefix",
    },
    {
        "6ff79819-710e-4145-97cd-1618da79e3e2",
        "platform-resource-backed",
        "Button",
        "btn",
        "ordinary-form button descriptor",
        "",
        "",
        "Кнопка1,КнопкаВыбораПериода",
        "existing descriptor registry; logform layouter XSD Button/btn; all-controls Form.bin names use button prefix",
    },
    {
        "381ed624-9217-4e63-85db-c4c3cb87daae",
        "platform-resource-backed",
        "TextBox",
        "txt",
        "ordinary-form input field/data editor descriptor",
        "IDS_FIELDKIND_INPUTFIELD",
        "IDS_TYPE_PRESENTATION_LOGFORMINPUTFIELDDATATYPE",
        "ПолеВвода1,НачПериода,КонПериода",
        "existing descriptor registry; mngcore resource names LogFormInputFieldDataType; logform layouter XSD TextBox/txt",
    },
    {
        "ea83fe3a-ac3c-4cce-8045-3dddf35b28b1",
        "platform-resource-backed",
        "TableBox",
        "tbl",
        "ordinary-form grid/table field descriptor",
        "",
        "",
        "ТабличноеПоле1",
        "existing descriptor registry; logform layouter XSD TableBox/tbl; all-controls Form.bin name uses table-field prefix",
    },
    {
        "151ef23e-6bb2-4681-83d0-35bc2217230c",
        "platform-resource-backed",
        "Image",
        "img",
        "ordinary-form picture descriptor",
        "",
        "",
        "ПолеКартинки1",
        "existing descriptor registry; logform layouter XSD Image/img; all-controls Form.bin name uses picture-field prefix",
    },
    {
        "0fc7e20d-f241-460c-bdf4-5ad88e5474a5",
        "platform-resource-backed",
        "Label",
        "lbl",
        "ordinary-form label/static text descriptor",
        "",
        "",
        "Надпись1,НадписьПолеВвода1,Надпись2,Надпись3,Надпись4,Надпись5",
        "existing descriptor registry; logform layouter XSD Label/lbl; all-controls Form.bin names use label prefix",
    },
    {
        "35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26",
        "corpus-xsd-resource-correlated",
        "CheckBox",
        "chk",
        "ordinary-form checkbox descriptor",
        "IDS_FIELDKIND_CHECKBOX",
        "IDS_TYPE_PRESENTATION_LOGFORMCHECKBOXFIELDDATATYPE",
        "Флажок1",
        "all-controls Form.bin GUID-headed object name; mngcore resources expose checkbox field kind/type presentation; logform layouter XSD CheckBox/chk",
    },
    {
        "782e569a-79a7-4a4f-a936-b48d013936ec",
        "corpus-xsd-resource-correlated",
        "RadioButton",
        "rbtn",
        "ordinary-form radio button descriptor",
        "IDS_FIELDKIND_RADIOBUTTONS",
        "IDS_TYPE_PRESENTATION_LOGFORMRADIOBUTTONSFIELDDATATYPE",
        "Переключатель1,Переключатель2,Переключатель3,Переключатель4",
        "all-controls Form.bin GUID-headed object names; mngcore resources expose radio-buttons field kind/type presentation; logform layouter XSD RadioButton/rbtn",
    },
    {
        "19f8b798-314e-4b4e-8121-905b2a7a03f5",
        "corpus-xsd-resource-correlated",
        "TextBox",
        "txt",
        "ordinary-form list field variant descriptor",
        "",
        "IDS_TYPE_PRESENTATION_LOGFORMINPUTFIELDDATATYPE",
        "ПолеСписка1",
        "all-controls Form.bin GUID-headed object name; object is a field variant represented by logform layouter XSD TextBox/txt",
    },
    {
        "64483e7f-3833-48e2-8c75-2c31aac49f6e",
        "corpus-xsd-resource-correlated",
        "TextBox",
        "txt",
        "ordinary-form choice field variant descriptor",
        "",
        "IDS_TYPE_PRESENTATION_LOGFORMINPUTFIELDDATATYPE",
        "ПолеВыбора1",
        "all-controls Form.bin GUID-headed object name; object is a choice field variant represented by logform layouter XSD TextBox/txt",
    },
    {
        "90db814a-c75f-4b54-bc96-df62e554d67d",
        "corpus-xsd-correlated",
        "GroupBox",
        "grpb",
        "ordinary-form group box descriptor",
        "",
        "",
        "РамкаГруппы1",
        "all-controls Form.bin GUID-headed object name; logform layouter XSD GroupBox/grpb",
    },
    {
        "36e52348-5d60-4770-8e89-a16ed50a2006",
        "corpus-xsd-correlated",
        "Separator",
        "sep",
        "ordinary-form separator descriptor",
        "",
        "",
        "Разделитель1",
        "all-controls Form.bin GUID-headed object name; logform layouter XSD Separator/sep",
    },
    {
        "236a17b3-7f44-46d9-a907-75f9cdc61ab5",
        "binary-guid-corpus-xsd-correlated",
        "Spreadsheet",
        "sprdsht",
        "ordinary-form spreadsheet field descriptor",
        "",
        "IDS_TYPE_PRESENTATION_LOGFORMMOXELFIELDDATATYPE",
        "ПолеТабличногоДокумента1",
        "all-controls Form.bin GUID-headed object name; GUID little-endian hit in dsgnfrm.dll and mngui.dll; mngcore resource names LogFormMoxelFieldDataType; logform layouter XSD Spreadsheet/sprdsht",
    },
    {
        "6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef",
        "corpus-xsd-resource-correlated",
        "TrackBar",
        "trckb",
        "ordinary-form track bar descriptor",
        "IDS_FIELDKIND_TRACKBAR",
        "IDS_TYPE_PRESENTATION_LOGFORMTRACKBARFIELDDATATYPE",
        "ПолосаРегулирования1",
        "all-controls Form.bin GUID-headed object name; mngcore resources expose trackbar field kind/type presentation; logform layouter XSD TrackBar/trckb",
    },
    {
        "e3c063d8-ef92-41be-9c89-b70290b5368b",
        "corpus-xsd-resource-correlated",
        "Calendar",
        "clndr",
        "ordinary-form calendar field descriptor",
        "IDS_FIELDKIND_CALENDAR",
        "IDS_TYPE_PRESENTATION_LOGFORMCALENDARFIELDDATATYPE",
        "ПолеКалендаря1",
        "all-controls Form.bin GUID-headed object name; mngcore resources expose calendar field kind/type presentation; logform layouter XSD Calendar/clndr",
    },
    {
        "14c4a229-bfc3-42fe-9ce1-2da049fd0109",
        "corpus-xsd-correlated",
        "TextDocument",
        "txtd",
        "ordinary-form text document field descriptor",
        "",
        "",
        "ПолеТекстовогоДокумента1",
        "all-controls Form.bin GUID-headed object name; logform layouter XSD TextDocument/txtd",
    },
    {
        "a26da99e-184a-4823-b0d6-62816d38dc4e",
        "corpus-ui-value-correlated",
        "PivotChart",
        "",
        "ordinary-form pivot chart descriptor candidate",
        "",
        "",
        "СводнаяДиаграмма1",
        "all-controls Form.bin GUID-headed object name; platform xdto data/ui schema exposes PivotChartType, but logform layouter has no PivotChart control complexType",
    },
    {
        "ad37194e-555e-4305-b718-5dca84baf145",
        "corpus-xsd-correlated",
        "GeographicalMap",
        "gm",
        "ordinary-form geographical map descriptor",
        "",
        "",
        "ПолеГеографическойСхемы1",
        "all-controls Form.bin GUID-headed object name; logform layouter XSD GeographicalMap/gm",
    },
    {
        "b1db1f86-abbb-4cf0-8852-fe6ae21650c2",
        "corpus-xsd-resource-correlated",
        "ProgressBar",
        "prgb",
        "ordinary-form progress bar descriptor",
        "IDS_FIELDKIND_PROGRESSBAR",
        "IDS_TYPE_PRESENTATION_LOGFORMPROGRESSBARFIELDDATATYPE",
        "Индикатор1",
        "all-controls Form.bin GUID-headed object name; mngcore resources expose progressbar field kind/type presentation; logform layouter XSD ProgressBar/prgb",
    },
    {
        "42248403-7748-49da-b782-e4438fd7bff3",
        "corpus-xsd-correlated",
        "Flowchart",
        "flwchrt",
        "ordinary-form flowchart descriptor",
        "",
        "",
        "ПолеГрафическойСхемы1",
        "all-controls Form.bin GUID-headed object name; logform layouter XSD Flowchart/flwchrt",
    },
    {
        "a8b97779-1a4b-4059-b09c-807f86d2a461",
        "corpus-xsd-correlated",
        "Chart",
        "chrt",
        "ordinary-form chart descriptor",
        "",
        "",
        "Диаграмма1",
        "all-controls Form.bin GUID-headed object name; logform layouter XSD Chart/chrt",
    },
    {
        "e5fdc112-5c84-4a16-9728-72b85692b6e2",
        "corpus-xsd-correlated",
        "GanttChart",
        "gchrt",
        "ordinary-form gantt chart descriptor",
        "",
        "",
        "ДиаграммаГанта1",
        "all-controls Form.bin GUID-headed object name; logform layouter XSD GanttChart/gchrt",
    },
    {
        "984981b1-622d-4ebc-94f7-885f0cdfb59a",
        "corpus-xsd-correlated",
        "Dendrogram",
        "dndrgm",
        "ordinary-form dendrogram descriptor",
        "",
        "",
        "Дендрограмма1",
        "all-controls Form.bin GUID-headed object name; logform layouter XSD Dendrogram/dndrgm",
    },
    {
        "621e95f1-064f-11d4-9400-008048da11f9",
        "corpus-unbound-platform-extension",
        "ActiveXControl",
        "",
        "ordinary-form external control descriptor candidate",
        "",
        "",
        "ЭлементУправления1",
        "all-controls Form.bin GUID-headed object name; no matching logform layouter XSD control complexType found in current platform resource extraction",
    },
}};

inline const DescriptorSchemaBinding* binding_for_guid(std::string_view guid) {
    for (const auto& binding : descriptor_schema_bindings) {
        if (binding.guid == guid) {
            return &binding;
        }
    }
    return nullptr;
}

inline bool is_bound_descriptor_guid(std::string_view guid) {
    return binding_for_guid(guid) != nullptr;
}

inline const form_schema::PlatformFormSchemaControl* schema_for_binding(const DescriptorSchemaBinding& binding) {
    return form_schema::control_by_type_name(binding.platform_type);
}

}  // namespace oof::platform::form_descriptor
