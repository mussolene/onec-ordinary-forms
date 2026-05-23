"""Ordinary form control vocabulary.

Names in this module are the public XML vocabulary for ordinary forms. The
platform names match the 1C ordinary-form control palette/API terminology.
"""

from __future__ import annotations

import importlib.resources
import xml.etree.ElementTree as ET
from dataclasses import dataclass


@dataclass(frozen=True)
class PlatformMember:
    name: str
    type: str = ""


@dataclass(frozen=True)
class OrdinaryControlDescriptor:
    xml_tag: str
    platform_name: str
    managed_equivalent: str
    properties: tuple[str, ...] = ()
    platform_properties: tuple[PlatformMember, ...] = ()
    platform_events: tuple[PlatformMember, ...] = ()


def load_platform_palette() -> dict[str, dict[str, object]]:
    path = importlib.resources.files("onec_ordinary_forms") / "schemas" / "ordinary-form.xsd"
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    palette = root.find(".//PlatformPalette")
    if palette is None:
        return {}
    result: dict[str, dict[str, object]] = {}
    for control in palette.findall("Control"):
        name = control.get("name")
        if not name:
            continue
        properties = [
            {"name": prop.get("platformName", ""), "type": prop.get("platformType", "")}
            for prop in control.findall("./Properties/Property")
            if prop.get("platformName")
        ]
        events = [
            {"name": event.get("platformName", "")}
            for event in control.findall("./Events/Event")
            if event.get("platformName")
        ]
        result[name] = {
            "platformName": control.get("platformName", ""),
            "properties": properties,
            "events": events,
        }
    return result


def platform_members(control: str, key: str) -> tuple[PlatformMember, ...]:
    raw_palette = PLATFORM_PALETTE.get(control, {})
    raw_members = raw_palette.get(key, [])
    if not isinstance(raw_members, list):
        return ()
    result: list[PlatformMember] = []
    for item in raw_members:
        if not isinstance(item, dict) or not item.get("name"):
            continue
        result.append(PlatformMember(name=str(item["name"]), type=str(item.get("type", ""))))
    return tuple(result)


PLATFORM_PALETTE = load_platform_palette()


COMMON_CONTROL_PROPERTIES = (
    "Title",
    "ToolTip",
    "Position",
    "Visible",
    "Enabled",
    "ReadOnly",
    "SkipOnInput",
    "DataPath",
    "Font",
    "TextColor",
    "BackColor",
    "BorderColor",
    "ContextMenu",
    "AutoContextMenu",
    "ExtendedTooltip",
    "Events",
)

TEXT_CONTROL_PROPERTIES = (
    "HorizontalAlign",
    "VerticalAlign",
    "TextPosition",
    "PicturePosition",
    "AutoMaxWidth",
    "HorizontalStretch",
    "Hyperlink",
)

INPUT_CONTROL_PROPERTIES = (
    "ValueType",
    "EditMode",
    "ChoiceMode",
    "MaxLength",
    "MultiLine",
    "PasswordMode",
    "ChoiceButton",
    "ClearButton",
    "OpenButton",
    "CreateButton",
    "EditButton",
    "RegulationButton",
)


ORDINARY_CONTROL_DESCRIPTORS: dict[str, OrdinaryControlDescriptor] = {
    "Panel": OrdinaryControlDescriptor("Panel", "Панель", "Pages", COMMON_CONTROL_PROPERTIES + ("Pages", "PagesRepresentation")),
    "Label": OrdinaryControlDescriptor(
        "LabelDecoration",
        "Надпись",
        "LabelDecoration",
        COMMON_CONTROL_PROPERTIES + TEXT_CONTROL_PROPERTIES,
    ),
    "Image": OrdinaryControlDescriptor(
        "PictureDecoration",
        "ПолеКартинки",
        "PictureDecoration",
        COMMON_CONTROL_PROPERTIES + ("Picture", "PicturePosition", "Border"),
    ),
    "Button": OrdinaryControlDescriptor(
        "Button",
        "Кнопка",
        "Button",
        COMMON_CONTROL_PROPERTIES + ("ButtonType", "Action", "CommandName", "DefaultButton", "CancelButton", "Picture", "PicturePosition"),
    ),
    "InputField": OrdinaryControlDescriptor(
        "InputField",
        "ПолеВвода",
        "InputField",
        COMMON_CONTROL_PROPERTIES + INPUT_CONTROL_PROPERTIES,
    ),
    "CommandBar": OrdinaryControlDescriptor("CommandBar", "КоманднаяПанель", "CommandBar", COMMON_CONTROL_PROPERTIES + ("Autofill",)),
    "CheckBox": OrdinaryControlDescriptor("CheckBox", "Флажок", "CheckBoxField", COMMON_CONTROL_PROPERTIES + ("ValueType", "TextPosition")),
    "Table": OrdinaryControlDescriptor(
        "Table",
        "ТабличноеПоле",
        "Table",
        COMMON_CONTROL_PROPERTIES + ("Columns", "Rows", "ColumnsCount", "RowsCount", "AutoMarkIncomplete"),
    ),
    "ChoiceField": OrdinaryControlDescriptor(
        "ChoiceField",
        "ПолеВыбора",
        "ChoiceField",
        COMMON_CONTROL_PROPERTIES + INPUT_CONTROL_PROPERTIES + ("ChoiceList",),
    ),
    "SpreadsheetDocumentField": OrdinaryControlDescriptor(
        "SpreadsheetDocumentField",
        "ПолеТабличногоДокумента",
        "SpreadsheetDocumentField",
        COMMON_CONTROL_PROPERTIES,
    ),
    "GroupBox": OrdinaryControlDescriptor("GroupBox", "РамкаГруппы", "UsualGroup", COMMON_CONTROL_PROPERTIES + ("ShowTitle", "Representation")),
    "RadioButton": OrdinaryControlDescriptor("RadioButton", "Переключатель", "RadioButton", COMMON_CONTROL_PROPERTIES + ("ValueType", "Items")),
    "Splitter": OrdinaryControlDescriptor("Splitter", "Разделитель", "Splitter", COMMON_CONTROL_PROPERTIES),
    "Chart": OrdinaryControlDescriptor("Chart", "Диаграмма", "ChartField", COMMON_CONTROL_PROPERTIES + ("ValueType",)),
    "ListBox": OrdinaryControlDescriptor("ListBox", "Список", "ListBox", COMMON_CONTROL_PROPERTIES + ("ValueType", "Items", "MultiLine")),
    "HTMLDocumentField": OrdinaryControlDescriptor(
        "HTMLDocumentField",
        "ПолеHTMLДокумента",
        "HTMLDocumentField",
        COMMON_CONTROL_PROPERTIES,
    ),
    "ProgressBar": OrdinaryControlDescriptor("ProgressBar", "Индикатор", "ProgressBar", COMMON_CONTROL_PROPERTIES + ("MinWidth", "MaxWidth")),
    "TrackBar": OrdinaryControlDescriptor(
        "TrackBar",
        "ПолосаРегулирования",
        "TrackBar",
        COMMON_CONTROL_PROPERTIES + ("Orientation", "MinWidth", "MaxWidth"),
    ),
    "CalendarField": OrdinaryControlDescriptor("CalendarField", "ПолеКалендаря", "CalendarField", COMMON_CONTROL_PROPERTIES + ("ValueType",)),
    "TextDocumentField": OrdinaryControlDescriptor(
        "TextDocumentField",
        "ПолеТекстовогоДокумента",
        "TextDocumentField",
        COMMON_CONTROL_PROPERTIES + ("Border", "HorizontalStretch", "VerticalStretch"),
    ),
    "CommandBarButton": OrdinaryControlDescriptor(
        "CommandBarButton",
        "КнопкаКоманднойПанели",
        "CommandBarButton",
        ("Title", "ToolTip", "Picture", "Action", "CommandName"),
    ),
}


def control_descriptor(control_type: object) -> OrdinaryControlDescriptor | None:
    return ORDINARY_CONTROL_DESCRIPTORS.get(str(control_type))


ORDINARY_CONTROL_DESCRIPTORS = {
    key: OrdinaryControlDescriptor(
        xml_tag=value.xml_tag,
        platform_name=value.platform_name,
        managed_equivalent=value.managed_equivalent,
        properties=value.properties,
        platform_properties=platform_members(value.xml_tag, "properties"),
        platform_events=platform_members(value.xml_tag, "events"),
    )
    for key, value in ORDINARY_CONTROL_DESCRIPTORS.items()
}
