#pragma once

#include <array>
#include <string_view>

namespace oof::platform::form_schema {

struct PlatformFormSchemaControl {
    std::string_view type_name;
    std::string_view stream_element;
    std::string_view schema_source;
    std::string_view base_type;
    std::string_view child_elements;
    std::string_view attributes;
    std::string_view value_types;
    std::string_view evidence;
};

constexpr std::string_view logform_layouter_schema =
    "mngcore_root.res:http://v8.1c.ru/8.2/managed-application/logform/layouter";

constexpr std::array<PlatformFormSchemaControl, 27> logform_layouter_controls{{
    {
        "Label",
        "lbl",
        logform_layouter_schema,
        "Item",
        "tooltip,shortCut,pic,txtClr,fnt,cntm",
        "hAlign,vAlign,hyper,format,markNegatives,pwd",
        "ui:ShortCutType,ui:Picture,ui:Color,ui:Font,ContextMenu",
        "Label complexType extends Item; comments define m_tooltip/m_shortCut/m_picture/m_textColor/m_font defaults",
    },
    {
        "TextBox",
        "txt",
        logform_layouter_schema,
        "Item",
        "tooltip,minValue,maxValue,choiceBtnPic,choiceForm,txtClr,bkClr,brdClr,fnt,stcut,cntm",
        "hAlign,wrap,pwd,extended,format,markNegatives,choiceListBtn,choiceBtn,clearBtn,spinBtn,openBtn,mask,listChoiceMode,choiceListHeight,choiceListMinWidth,autoChoice,choiceMode,autoMark,quickChoice,finalQuickChoice,choice,textEdit,multiLine,warningOnEditMode,searchOnInput",
        "Value,ui:Picture,v81:UUID,ui:Color,ui:Font,ui:ShortCutType,ContextMenu",
        "TextBox complexType extends Item; schema comments expose choice/clear/spin/open button flags and format/input value fields",
    },
    {
        "Button",
        "btn",
        logform_layouter_schema,
        "Item",
        "tooltip,shortCut,pic,txtClr,bkClr,brdClr,fnt",
        "defBtn,depBtn",
        "ui:ShortCutType,ui:Picture,ui:Color,ui:Font",
        "Button complexType extends Item; comments define m_defaultButton/m_dependedButton and picture/color/font payloads",
    },
    {
        "CheckBox",
        "chk",
        logform_layouter_schema,
        "Item",
        "tooltip,brdClr,stcut,cntm",
        "threeState,vAlign",
        "ui:Color,ui:ShortCutType,ContextMenu",
        "CheckBox complexType extends Item; schema exposes m_threeState and vertical alignment",
    },
    {
        "RadioButton",
        "rbtn",
        logform_layouter_schema,
        "Item",
        "tooltip,txtClr,fnt,brdClr,stcut,cntm",
        "vAlign",
        "ui:Color,ui:Font,ui:ShortCutType,ContextMenu",
        "RadioButton complexType extends Item; schema exposes text/font/border/shortcut/context-menu fields",
    },
    {
        "CommandBar",
        "cmdb",
        logform_layouter_schema,
        "Item",
        "cmd,menu,msep,stcut",
        "source,hAlign,secCB",
        "Command,Submenu,MenuSeparator,ui:ShortCutType",
        "CommandBar complexType extends Item; schema exposes default command source, secondary command bar flag, and menu children",
    },
    {
        "TableBox",
        "tbl",
        logform_layouter_schema,
        "Item",
        "tooltip,rowsPic,txtClr,bkClr,brdClr,fnt,stcut,cntm,column,group",
        "inplEdit,changeRowSet,changeRowOrder,rowInput,rowSel,sel,showHeader,headerHeight,showFooter,footerHeight,logicRowHeight,realRowHeight,horScroll,verScroll,horLines,verLines,autoColumnWidth,initListView,initTreeView,startDrag,drag",
        "ui:Picture,ui:Color,ui:Font,ui:ShortCutType,ContextMenu,TableColumn,TableColumnsGroup",
        "TableBox complexType extends Item; schema exposes table columns/groups, row modes, scrolling, lines, and drag flags",
    },
    {
        "TableColumn",
        "column",
        logform_layouter_schema,
        "Item",
        "headerPic,footerPic,choiceBtnPic,choiceForm,txtClr,bkClr,brdClr,fnt,stcut,cntm",
        "columnMode,editMode,format,inputFormat,choiceListBtn,choiceBtn,clearBtn,spinBtn,openBtn,mask,choiceListHeight,choiceListMinWidth,autoChoice,choiceMode,autoMark,quickChoice,finalQuickChoice,choice,textEdit,multiLine,searchOnInput,headerFormat",
        "ui:Picture,v81:UUID,ui:Color,ui:Font,ui:ShortCutType,ContextMenu",
        "TableColumn complexType extends Item; schema exposes column input/choice/edit fields",
    },
    {
        "TableColumnsGroup",
        "group",
        logform_layouter_schema,
        "Item",
        "headerPic,column,group",
        "headerFormat",
        "ui:Picture,TableColumn,TableColumnsGroup",
        "TableColumnsGroup complexType extends Item; schema exposes nested table column tree",
    },
    {
        "GroupBox",
        "grpb",
        logform_layouter_schema,
        "Item",
        "tooltip,txtClr,fnt,lbl,txt,chk,btn,rbtn,cmdb,tbl,grpb,pnl,img,sep,sprdsht,txtd,fmtd,clndr,prgb,trckb,chrt,gchrt,dndrgm,flwchrt,html,gm,empt",
        "format,dataLink",
        "ui:Color,ui:Font,ordinary form child controls",
        "GroupBox complexType extends Item; schema exposes ordinary child-control choice and dataLink flag",
    },
    {
        "Separator",
        "sep",
        logform_layouter_schema,
        "Item",
        "",
        "dir",
        "Direction",
        "Separator complexType extends Item; schema exposes direction and Item defaults for secondary layout divider",
    },
    {
        "Panel",
        "pnl",
        logform_layouter_schema,
        "Item",
        "page,stcut",
        "tabs,tabWidth",
        "Page,ui:ShortCutType",
        "Panel complexType extends Item; schema exposes pages and tab representation",
    },
    {
        "Page",
        "page",
        logform_layouter_schema,
        "Item",
        "pic,stcut,lbl,txt,chk,btn,rbtn,cmdb,tbl,grpb,pnl,img,sep,sprdsht,txtd,fmtd,clndr,prgb,trckb,chrt,gchrt,dndrgm,flwchrt,html,gm,empt",
        "",
        "ui:Picture,ui:ShortCutType,ordinary form child controls",
        "Page complexType extends Item; schema exposes page picture, shortcut, and child-control choice",
    },
    {
        "Image",
        "img",
        logform_layouter_schema,
        "Item",
        "tooltip,shortCut,pic,picVal,txtClr,brdClr,fnt,brd,cntm",
        "hyper,pictureSize,zoom,startDrag,drag,picTxt",
        "ui:Picture,ui:Color,ui:Font,ui:Border,ContextMenu",
        "Image complexType extends Item; schema exposes picture value/text and drag flags",
    },
    {
        "Spreadsheet",
        "sprdsht",
        logform_layouter_schema,
        "Item",
        "tooltip,brdClr,stcut,cntm",
        "grid,headers,horScroll,verScroll,blackAndWhite,protect,selShow,output,edit,showGroups,startDrag,drag",
        "ui:Color,ui:ShortCutType,ContextMenu",
        "Spreadsheet complexType extends Item; schema exposes spreadsheet mode flags",
    },
    {
        "TextDocument",
        "txtd",
        logform_layouter_schema,
        "Item",
        "stcut,txtClr,bkClr,brdClr,fnt,cntm",
        "output",
        "ui:ShortCutType,ui:Color,ui:Font,ContextMenu",
        "TextDocument complexType extends Item; schema exposes output flag and text colors/font",
    },
    {
        "FormattedDocument",
        "fmtd",
        logform_layouter_schema,
        "Item",
        "tooltip,stcut,txtClr,bkClr,brdClr,fnt,cntm",
        "output",
        "ui:ShortCutType,ui:Color,ui:Font,ContextMenu",
        "FormattedDocument complexType extends Item; schema exposes output flag and formatted document UI fields",
    },
    {
        "Calendar",
        "clndr",
        logform_layouter_schema,
        "Item",
        "tooltip,fnt,stcut,cntm,brdClr",
        "dateSel,showCurDate,navigation,begPeriod,endPeriod,startDrag,drag",
        "ui:Font,ui:ShortCutType,ContextMenu,ui:Color",
        "Calendar complexType extends Item; schema exposes period/navigation and drag flags",
    },
    {
        "ProgressBar",
        "prgb",
        logform_layouter_schema,
        "Item",
        "tooltip,brdClr,cntm",
        "minValue,maxValue,orientation,represent,showPercent",
        "ui:Color,ContextMenu,Value",
        "ProgressBar complexType extends Item; schema exposes min/max and orientation/representation",
    },
    {
        "TrackBar",
        "trckb",
        logform_layouter_schema,
        "Item",
        "tooltip,brdClr,stcut,cntm",
        "minValue,maxValue,step,page,markingStep,orientation,marking",
        "ui:Color,ui:ShortCutType,ContextMenu,Value",
        "TrackBar complexType extends Item; schema exposes step/page/marking and orientation",
    },
    {
        "Chart",
        "chrt",
        logform_layouter_schema,
        "Item",
        "tooltip,stcut,cntm",
        "",
        "ui:ShortCutType,ContextMenu",
        "Chart complexType extends Item; schema exposes chart base UI fields",
    },
    {
        "GanttChart",
        "gchrt",
        logform_layouter_schema,
        "Item",
        "tooltip,stcut,cntm",
        "",
        "ui:ShortCutType,ContextMenu",
        "GanttChart complexType extends Item; schema exposes chart base UI fields",
    },
    {
        "Dendrogram",
        "dndrgm",
        logform_layouter_schema,
        "Item",
        "tooltip,stcut,cntm",
        "",
        "ui:ShortCutType,ContextMenu",
        "Dendrogram complexType extends Item; schema exposes chart base UI fields",
    },
    {
        "Flowchart",
        "flwchrt",
        logform_layouter_schema,
        "Item",
        "tooltip,brdClr,stcut,cntm",
        "output,edit",
        "ui:Color,ui:ShortCutType,ContextMenu",
        "Flowchart complexType extends Item; schema exposes output/edit flags",
    },
    {
        "HTML",
        "html",
        logform_layouter_schema,
        "Item",
        "tooltip,brdClr,stcut,cntm",
        "output",
        "ui:Color,ui:ShortCutType,ContextMenu",
        "HTML complexType extends Item; schema exposes output flag and HTML UI fields",
    },
    {
        "GeographicalMap",
        "gm",
        logform_layouter_schema,
        "Item",
        "tooltip,brdClr,stcut,cntm",
        "output",
        "ui:Color,ui:ShortCutType,ContextMenu",
        "GeographicalMap complexType extends Item; schema exposes output flag",
    },
    {
        "EmptyElement",
        "empt",
        logform_layouter_schema,
        "Item",
        "",
        "",
        "",
        "EmptyElement complexType extends Item; schema exposes empty ordinary layout placeholder",
    },
}};

inline const PlatformFormSchemaControl* control_by_type_name(std::string_view type_name) {
    for (const auto& control : logform_layouter_controls) {
        if (control.type_name == type_name) {
            return &control;
        }
    }
    return nullptr;
}

}  // namespace oof::platform::form_schema
