#pragma once

#include <array>
#include <string_view>

namespace oof::platform::runtime_binding {

struct PlatformRuntimeLayer {
    std::string_view name;
    std::string_view source;
    std::string_view role;
    std::string_view evidence;
};

struct PlatformApiObject {
    std::string_view name;
    std::string_view api_source;
    std::string_view runtime_source;
    std::string_view persistence_source;
    std::string_view xdto_source;
    std::string_view localization_source;
    int property_count;
    int method_count;
    int event_count;
    std::string_view sample_properties;
    std::string_view sample_methods;
    std::string_view sample_events;
    std::string_view evidence;
};

constexpr std::array<PlatformRuntimeLayer, 6> runtime_layers{{
    {
        "script-context-dispatch",
        "common_core85.so/platform_core85.so",
        "central object dispatch for properties, methods, constructors and GenericValue arguments",
        "exports core::ContextCore::getPropVal/setPropVal/call, core::GroupContext::getPropVal/setPropVal/call, core::ObjectTypeCore::ctor, core::GenericValue assign/get helpers",
    },
    {
        "logform-type-categories",
        "common_core85.so/platform_core85.so",
        "central type-info categories used by ordinary-form runtime objects",
        "exports core::kLogFormTypeInfoCategory, core::kLogFormControllerTypeInfoCategory, core::kLogFormNestedPropertiesDisabledTypeInfoCategory",
    },
    {
        "ordinary-form-runtime",
        "mngbase.so/mngui.so",
        "runtime form/control classes and customizers bound to the script-context dispatch layer",
        "mngbase imports LogForm type-info categories and owns RTLogFormSrv/LogFormMngrSrv/LogFormElementCommandsInfo; mngui contains LogFormButtonCustomizer, LogFormAdditionCustomizer, ILogFormControl, LogFormChart, LogFormGrid and related SCOM classes",
    },
    {
        "ordinary-form-persistence",
        "dsgnfrm.so",
        "ordinary Form.bin list-stream persistence and cf_form_controls* payload formats",
        "exports wbase::cf_form_controls8/cf_form_controls_position8/cf_form_controls_info8 and imports core::ListInStream/ListOutStream plus CompositeID/TypeDomainPattern serializers",
    },
    {
        "platform-api-catalog",
        "shcntx_root.hbk/shcntx_ru.hbk",
        "object/member catalog visible in configurator/debugger help: objects, properties, methods, events",
        "shcntx_root.hbk exposes about 86k object catalog entries; examples include Form, Button, TableBox, Panel, PivotChart, PivotChartField, PivotChartFieldCollection",
    },
    {
        "xdto-value-schema",
        "chart_root.res/xdto_root.res/mngcore_root.res and extracted XSDs",
        "serialized value-object schemas used under runtime objects",
        "8.5 chart_root XSD exposes http://v8.1c.ru/8.2/data/chart complexType PivotChart and nested Chart fields; logform layouter XSD exposes ordinary form control stream elements",
    },
}};

constexpr std::array<PlatformApiObject, 14> api_objects{{
    {
        "Form",
        "shcntx_root.hbk objects/catalog56/catalog246/Form",
        "core ContextCore + mngbase RTLogFormSrv/LogFormMngrSrv",
        "dsgnfrm.so ListInStream/ListOutStream Form.bin payload",
        "",
        "mngbase_ru.res/frntend_ru.res plus shcntx_ru.hbk help-book",
        38,
        20,
        13,
        "AllowClose,AutoTitle,Caption,CaptionPicture,ChoiceMode,CloseOnChoice,Title",
        "Activate,CheckFilling,ChooseFromList,Close,DoModal,Open,Refresh",
        "BeforeClose,BeforeOpen,ChoiceProcessing,ExternalEvent,OnClose,OnOpen",
        "parsed from shcntx_root.hbk; runtime stream oracle materializes type Форма through ЗначениеИзСтрокиВнутр; core dispatch exports getPropVal/setPropVal/call",
    },
    {
        "FormItems",
        "shcntx_root.hbk objects/catalog1649/catalog1890/FormItems",
        "mngbase/mngui LogForm item collections exposed as ThisForm.Items",
        "ordinary form graph child item nodes under Form.bin payload",
        "",
        "mngbase_ru.res/frntend_ru.res",
        1,
        4,
        0,
        "prop",
        "Count,Find,Get,IndexOf",
        "",
        "parsed from shcntx_root.hbk; platform resource scripts use ThisForm and Items.* property access",
    },
    {
        "FormAttributes",
        "mngcore logform.xsd Form/m_pProperties/property + platform API collection surface",
        "mngbase LogForm property collection exposed through Form.Attributes/Реквизиты",
        "ordinary form property records under m_pProperties",
        "",
        "shcntx_ru.hbk/mngbase_ru.res member names Реквизиты/Реквизит",
        1,
        4,
        0,
        "prop",
        "Count,Find,Get,IndexOf",
        "",
        "platform resources already joined in PlatformPropertyDescriptor: Attributes -> FormAttributes, Attribute.ID/Main/StoredData from logform.xsd Property",
    },
    {
        "FormAttribute",
        "mngcore logform.xsd complexType Property",
        "mngbase LogForm attribute/property object",
        "ordinary form m_pProperties/property element",
        "",
        "shcntx_ru.hbk/mngbase_ru.res member names Реквизит/Основной/СохраняемыеДанные",
        3,
        0,
        0,
        "ID,Main,StoredData",
        "",
        "",
        "PlatformPropertyDescriptor has Attribute.ID/Attribute.Main/Attribute.StoredData bound to logform.xsd Property",
    },
    {
        "FormCommands",
        "mngcore logform.xsd Form/m_pCommands/command + cmi.xsd CommandInfo",
        "mngbase LogForm command collection exposed through Form.Commands/Команды",
        "ordinary form command records under m_pCommands",
        "",
        "shcntx_ru.hbk/mngbase_ru.res member names Команды/Команда",
        1,
        4,
        0,
        "prop",
        "Count,Find,Get,IndexOf",
        "",
        "platform resources already joined in PlatformPropertyDescriptor: Commands -> FormCommands, Command.* from logform.xsd Command + cmi.xsd CommandInfo",
    },
    {
        "FormCommand",
        "mngcore logform.xsd complexType Command + cmi.xsd CommandInfo",
        "mngbase LogForm command/action object",
        "ordinary form m_pCommands/command element",
        "",
        "shcntx_ru.hbk/mngbase_ru.res member names Команда/Обработчик/ИзменяетДанные",
        4,
        0,
        0,
        "ID,Name,Handler,ModifiesData",
        "",
        "",
        "PlatformPropertyDescriptor has Command.ID/Command.Name/Command.Handler/Command.ModifiesData bound to logform.xsd Command",
    },
    {
        "FormEvents",
        "mngcore logform.xsd m_elementEvents/event",
        "mngbase LogForm event collection exposed through Form.Events/События",
        "ordinary form element event records under m_elementEvents",
        "",
        "shcntx_ru.hbk/mngbase_ru.res member names События/Событие",
        1,
        4,
        0,
        "prop",
        "Count,Find,Get,IndexOf",
        "",
        "platform resources already joined in PlatformPropertyDescriptor: Events -> FormEvents, Event.ID/Event.Handler from logform.xsd Event",
    },
    {
        "FormEvent",
        "mngcore logform.xsd complexType Event",
        "mngbase LogForm event object",
        "ordinary form m_elementEvents/event element",
        "",
        "shcntx_ru.hbk/mngbase_ru.res member names Событие/Обработчик",
        2,
        0,
        0,
        "ID,Handler",
        "",
        "",
        "PlatformPropertyDescriptor has Event.ID/Event.Handler bound to logform.xsd Event",
    },
    {
        "Button",
        "shcntx_root.hbk objects/catalog56/catalog86/Button",
        "mngui.so LogFormButtonCustomizer/ILogFormButton/LogFormButtonFieldWnd",
        "dsgnfrm cf_form_controls* descriptor 6ff79819-710e-4145-97cd-1618da79e3e2; logform layouter XSD Button/btn",
        "",
        "mngbase_ru.res/frntend_ru.res",
        16,
        0,
        1,
        "Caption,Enabled,Font,Picture,Shortcut,ToolTip,Visible",
        "",
        "Click",
        "shcntx_root object/member catalog plus mngui LogFormButton symbols and descriptor join evidence",
    },
    {
        "TableBox",
        "shcntx_root.hbk objects/catalog56/catalog86/catalog190/TableBox",
        "mngui.so LogFormGrid and LogFormGrid* controllers",
        "dsgnfrm cf_form_controls* descriptor ea83fe3a-ac3c-4cce-8045-3dddf35b28b1; logform layouter XSD TableBox/tbl",
        "",
        "mngbase_ru.res/frntend_ru.res",
        46,
        12,
        22,
        "Columns,CurrentRow,Enabled,Font,ReadOnly,SelectionMode,Visible",
        "AddRow,ChangeRow,CopyRow,CreateColumns,EndEditRow,RefreshRows",
        "BeforeAddRow,BeforeDeleteRow,ChoiceProcessing,Drag,OnActivateRow",
        "shcntx_root API catalog plus mngui LogFormGrid controller source-path strings and descriptor join evidence",
    },
    {
        "Panel",
        "shcntx_root.hbk objects/catalog56/catalog86/catalog245/Panel",
        "mngui/mngbase LogForm control hierarchy",
        "dsgnfrm cf_form_controls* descriptor 09ccdc77-ea1a-4a6d-ab1c-3435eada2433; logform layouter XSD Panel/pnl",
        "",
        "mngbase_ru.res/frntend_ru.res",
        18,
        0,
        1,
        "AutoTabOrder,BackColor,CurrentPage,Enabled,Pages,ShowTabs,Visible",
        "",
        "OnCurrentPageChange",
        "shcntx_root API catalog plus direct descriptor registry and logform layouter XSD evidence",
    },
    {
        "PanelPage",
        "shcntx_root.hbk objects/catalog56/catalog86/catalog245/PanelPage",
        "mngui/mngbase LogForm control hierarchy",
        "logform layouter XSD Page/page nested in Panel",
        "",
        "mngbase_ru.res/frntend_ru.res",
        7,
        0,
        0,
        "Caption,Enabled,IsOpen,Name,TitlePicture,Value,Visible",
        "",
        "",
        "shcntx_root API catalog; panel pages collection has Add/Clear/Count/Delete/Find/Get/IndexOf/Insert/Move",
    },
    {
        "PivotChart",
        "shcntx_root.hbk objects/catalog63/catalog810/PivotChart",
        "mngui.so/mngbase.so UI/type-domain GUID table plus chart UI resources",
        "ordinary form object GUID a26da99e-184a-4823-b0d6-62816d38dc4e is platform-ui-guid-table-backed; dsgnfrm/logform layouter has no PivotChart complexType",
        "chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd complexType PivotChart",
        "chartui_ru.res/frntend_ru.res",
        33,
        2,
        2,
        "Attributes,BackColor,DataSource,Fields,HorizontalScaleKeeping,PivotChartType,Title",
        "GetPicture,Refresh",
        "DetailProcessing,Selection",
        "shcntx_root API catalog; 8.5 chart_root XDTO PivotChart; mngui/mngbase hardcode PivotChart GUID; chartui_ru contains Сводная диаграмма/Область заголовка/Заголовок strings",
    },
    {
        "PivotChartFieldCollection",
        "shcntx_root.hbk objects/catalog63/catalog810/PivotChartFieldCollection",
        "PivotChart runtime object child collection",
        "nested under PivotChart object model",
        "chart_root data/chart field model",
        "chartui_ru.res/frntend_ru.res",
        1,
        9,
        0,
        "prop",
        "Add,Clear,Count,Delete,Find,Get,IndexOf,Insert,Move",
        "",
        "shcntx_root API catalog; PivotChartField has Attribute/Attributes/Dimension/Name/OpenLevelCount/Resource/ValueType and LevelCount method",
    },
}};

}  // namespace oof::platform::runtime_binding
