"""Internal ordinary-form ListOutStream codec.

This module is the boundary between public object-model ``Form.xml`` and the
platform list-stream payload stored as the ``form`` file inside ``Form.bin``.
Public XML must never expose this stream directly.
"""

from __future__ import annotations

import base64
import copy
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

from onec_ordinary_forms.liststream import dumps_list_out_stream
from onec_ordinary_forms.ordinary_platform import (
    CF_FORM_CONTROLS8_FORMAT_ID,
    CF_FORM_CONTROLS_INFO8_FORMAT_ID,
    CF_FORM_CONTROLS_POSITION8_FORMAT_ID,
    ORDINARY_CONTROL_GUID_BY_TYPE,
)
from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS
from onec_ordinary_forms.value_codec import is_integer_atom, localized_text_record, quote_atom


PLATFORM_CONTROL_FORMAT_IDS = {
    "controls": CF_FORM_CONTROLS8_FORMAT_ID,
    "position": CF_FORM_CONTROLS_POSITION8_FORMAT_ID,
    "info": CF_FORM_CONTROLS_INFO8_FORMAT_ID,
}

DEFAULT_CONTROL_EVENT_UUID = "e1692cc2-605b-4535-84dd-28440238746c"


@dataclass(frozen=True)
class InfoSlotDescriptor:
    name: str
    index: int


@dataclass(frozen=True)
class ControlInfoDescriptor:
    control_type: str
    info_kind: str
    slots: tuple[InfoSlotDescriptor, ...] = ()

    def slot_index(self, name: str) -> int:
        for slot in self.slots:
            if slot.name == name:
                return slot.index
        raise KeyError(f"Unknown {self.control_type} info slot: {name}")


@dataclass(frozen=True)
class PlatformListRecordDescriptor:
    name: str
    length: int
    defaults: tuple[tuple[int, object], ...] = ()

    def build(self, overrides: dict[int, object] | None = None) -> list[object]:
        record: list[object] = ["0"] * self.length
        for index, value in self.defaults:
            record[index] = copy.deepcopy(value)
        for index, value in (overrides or {}).items():
            if index < 0 or index >= self.length:
                raise IndexError(f"{self.name} slot index out of range: {index}")
            record[index] = value
        return record


CORE_CONTROL_INFO_DESCRIPTORS = {
    "FormRootPanel": ControlInfoDescriptor(
        control_type="FormRootPanel",
        info_kind="1",
        slots=(
            InfoSlotDescriptor("BaseInfo", 0),
            InfoSlotDescriptor("PageStates", 17),
            InfoSlotDescriptor("PagePositions", 21),
        ),
    ),
    "Panel": ControlInfoDescriptor(
        control_type="Panel",
        info_kind="1",
        slots=(
            InfoSlotDescriptor("BaseInfo", 0),
            InfoSlotDescriptor("PageStates", 42),
            InfoSlotDescriptor("PagePositions", 46),
        ),
    ),
    "CommandBar": ControlInfoDescriptor(
        control_type="CommandBar",
        info_kind="2",
        slots=(
            InfoSlotDescriptor("BaseInfo", 0),
            InfoSlotDescriptor("Autofill", 6),
        ),
    ),
    "InputField": ControlInfoDescriptor(
        control_type="InputField",
        info_kind="9",
        slots=(
            InfoSlotDescriptor("BaseInfo", 0),
            InfoSlotDescriptor("ReadOnly", 12),
        ),
    ),
    "Table": ControlInfoDescriptor(
        control_type="Table",
        info_kind="5",
        slots=(
            InfoSlotDescriptor("BaseInfo", 0),
            InfoSlotDescriptor("View", 1),
            InfoSlotDescriptor("RowsCount", 20),
            InfoSlotDescriptor("ColumnsCount", 21),
            InfoSlotDescriptor("AutoMarkIncomplete", 22),
        ),
    ),
}

DIAGRAM_BODY_DESCRIPTOR = PlatformListRecordDescriptor(
    name="DiagramBody",
    length=3,
    defaults=(
        (0, "0"),
        (1, ["11"]),
    ),
)

PIVOT_CHART_INFO_DESCRIPTOR = PlatformListRecordDescriptor(
    name="PivotChartInfo",
    length=13,
    defaults=(
        (0, "3"),
        (2, ["0"]),
        (3, "1"),
        (4, "6"),
        (5, "12"),
        (6, "1"),
        (7, "2"),
        (8, "1"),
        (9, "0"),
        (10, ["4", "3", ["-7"], "3"]),
        (11, ["4", "3", ["-3"], "3"]),
        (12, "1"),
    ),
)

TABLE_COLUMN_VALUE_PAYLOAD_BY_PATTERN = {
    ('"S"',): "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVFLCgIxDBWXwiy9QNYpJG3HNrcQxAOMOlsX4k56\r\r\nMhceySvYNqOOOmBTSF8+Ly90OZ/Vc7/eLoLN4gLr7nzuT0eoYAOpWaTy1MuCXJBH\r\r\nXxwl9GkKR3RIuZQph0grXHHG2oRusucXa0d4NwwU/Iw4VWM4linZapSRFObp+LSv\r\r\nAWUrx1qFNLIx8toHW0gvD/BRVGlpoM05w+WWPED6k30xTEgfCVqFECy3aGvV8B11\r\r\nb+nCyruDNSy9GN/21sQozthIu72wtJ0E1fC9BekelW5grINZBamM9AA=",
    ('"N"', "10", "0", "0"): "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVExDsIwDESMSDyBxbMjxUlK4k8gFh5QoCsDYkN5\r\r\nGQNP4gvEcSlVqUQcyTo7dz4rm+WintfjeWdcr+6wb2+37nqBCnaAZLFEXq+yVPQS\r\r\nIwkKGCTZjCHP4YS+kKlolJLVF16ScS6jn+X8YmXEL6GXoE/FqxtDSaaIW4GEVmGZ\r\r\njp+YDJCtPKH9CeqZNQUlwgHykAGGV4Ou7XVLz5Bc6QPkP91BYcb7yNE2xuioQTf+\r\r\nj7o4t3Eb/NkZ4o5NaDpnUmJvXLLHExM3LUf1MN3C6h5Vrlesg0kNqY38Bg==",
    ('"D"', '"D"'): "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVE7DsIwDEWMSD0Aq2dHipOUxDsHYOEABboyIDaU\r\r\nkzFwJK5AHJdfqUScyHr+PD8ry/msnvv1dmFsFhfYdOdzfzpCBWvA8nKzyAL1EiMJ\r\r\nChjE2YwhT+GEHm0pJVtCViu8OONcRj/Z84u1I74bBgp6RryqMZRkSrEaJbQKy3R8\r\r\n2miAbOUJ33kaebGgfbCF/PIAX0WV1g60JWdIruQB8p/si2FC+oewVYzRUYuuVg3f\r\r\nUffmLq6CPzhD3LMJbe9MSuyNS3a3Z+K246gaxltY3aPSDYx1MKkglZEf",
    ('"B"',): "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVE7DsIwDEWMSB25gGdHipOUxCsnYOEABboyIDaU\r\r\nkzFwJK5AHBfKpxJxVOfZz8+2upzP6rlfbxfGZnGBTXc+96cjVLCG3CyyPPUSIwkK\r\r\nGMTZjCFP4YQebaGSLSGrDC/OOJfRT9b8Yq2IY8EgQc+I12kMJelSrEYJrcLSHZ/2\r\r\n1UC28qSst+/oxYLWwRbyywN8kKqsHWRLzpBcyQPkP9mXwsToo9EqxuioRVdZw++o\r\r\ne3MXV8EfnCHu2YS2dyYl9sYlu9szcdtx1Bm+t7C6R5UbFGtj0oF0jPwA",
    ('"#"', "4772b3b4-f4a3-49c0-a1a5-8cb5961511a3"): "#base64:AgFTS2/0iI3BTqDV67a9oKcNhVExbsMwDCw6Bsgn1JUERJGypV90yQNkQx07FNkC\r\r\nvaxDn9QvVBIVJ2kN1CRMHHk8UiA9P/Xv+/PrEuF4uJjXdD7nj3fTwYsBmWe38CL4\r\r\nJolR4moxUfIY1sXHiTxR4nI8lMZXpwjUkIC0YAtI2cMBGGylkq0pqwxuAZ0rwLs9\r\r\nf7F2zLeGIUHXDOs2SKFNqdazBFZhnQ5X+zWgvYpJWeN/j9RE+8zJlC0a80DqsnbI\r\r\n1hpS81Y3pvxT3RR2Vr8ZTXM9E3lwnTXO0d8dOLnonUU75YDCEjCGuGJa8hRzyMzZ\r\r\nqf799ZuE+lhM37NNLz8=",
}


BINDING_COORDINATE_SLOT = {
    "top": 1,
    "bottom": 2,
    "left": 3,
    "right": 4,
    "verticalCenter": 5,
    "horizontalCenter": 6,
}

DIMENSION_NAME_SLOT = {
    "height": 1,
    "minHeight": 2,
    "stretch": 3,
    "width": 4,
}

ANCHOR_KIND_MAP = {
    "0": "none",
    "1": "absolute",
    "2": "targetEdgeOffset",
    "3": "targetCenterOffset",
    "4": "expression",
    "5": "relative",
    "6": "group",
}

EDGE_NAME_MAP = {
    "-1": "unknown",
    "0": "top",
    "1": "bottom",
    "2": "left",
    "3": "right",
    "4": "width",
    "5": "height",
    "6": "none",
}

CONTROL_METADATA_SCOPE = {
    "default": "4294967295",
    "CommandBar": "0",
}

# Platform-shaped geometry trailers by control family.
# The serializer writes a common header and then appends a trailer profile.
GEOMETRY_TRAILER_PROFILE = {
    "default": ["0", "0", "0", "1", "0", "0"],
    "CommandBar": ["0", "0", "0", "0", "1", "1", "0"],
    "Label": ["0", "0", "0", "1", "0", "1", "0", "0"],
    "Panel": ["0", "0", "2", "2", "0", "0"],
    "paged": ["0", "0", "0"],
}


def form_stream_from_object_xml(root: ET.Element, asset_root: Path | None = None) -> bytes:
    """Serialize public ordinary ``Form.xml`` into platform list-stream bytes."""

    if root.tag != "Form":
        raise ValueError("Expected public ordinary form XML root <Form>")

    title = form_title_from_xml(root)
    if not title:
        pages = top_level_pages(root)
        first_page = pages[0] if pages else None
        title = get_multilang_text(first_page, "Title") or (first_page.get("name") if first_page is not None else "Main")

    attributes = []
    attribute_type_patterns: dict[str, list[object]] = {}
    attribute_slots: dict[str, str] = {}
    for attribute in root.findall("./Attributes/Attribute"):
        name = attribute.get("name", "")
        if not name:
            continue
        type_pattern = type_pattern_from_xml(attribute)
        attribute_type_patterns[name] = type_pattern
        if attribute.get("slot"):
            attribute_slots[name] = attribute.get("slot", "")
        attributes.append(attribute_record_from_xml(attribute))

    controls: list[object] = []
    for page in top_level_pages(root):
        for child in page:
            control = control_stream_from_xml(child, asset_root, attribute_type_patterns, attribute_slots)
            if control:
                controls.append(control)

    stream = ordinary_form_stream(
        title or "Main",
        attributes,
        controls,
        events_from_xml(root),
        form_size_from_xml(root),
    )
    return ("\ufeff" + dumps_list_out_stream(stream)).encode("utf-8")


def ordinary_form_stream(
    title: str,
    attributes: list[object],
    controls: list[object],
    events: list[object],
    form_size: tuple[str, str, bool],
) -> list[object]:
    return [
        "27",
        form_root_record(title, controls, form_size),
        attributes_table(attributes),
        form_object_info_record(),
        ["1", *events] if events else ["0"],
        "1",
        "4",
        "1",
        "0",
        "0",
        "0",
        ["0"],
        ["0"],
        ["3", "0", ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]],
        "1",
        "2",
        "0",
        "0",
        "1",
        "1",
    ]


def form_root_record(title: str, controls: list[object], form_size: tuple[str, str, bool]) -> list[object]:
    width, height, explicit_size = form_size
    root_panel = [
        ORDINARY_CONTROL_GUID_BY_TYPE["Panel"],
        root_panel_info(title, width, height),
        ["1", *controls] if len(controls) == 1 else [str(len(controls)), *controls],
    ]
    record = [
        "16",
        [localized_text_record(title), "52", "4294967295"],
        root_panel,
        width,
        height,
        "1",
        "0",
        "1",
        "4",
        "4",
        "6",
    ]
    if explicit_size:
        record[0] = "18"
        record[1][1] = "41"
        record[1][2] = "3"
        record[-1] = "3"
        record.extend([width, height, "96"])
    return record


def root_panel_info(title: str, form_width: str, form_height: str) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["FormRootPanel"]
    margin_left = "8"
    margin_top = "33"
    page_width = panel_extent_value(form_width, 8)
    page_height = panel_extent_value(form_height, 33)
    page_title = localized_text_record("Страница1")
    position_records = [
        ["2", margin_left, "1", "1", "1", "0", "0", "0", "0"],
        ["2", margin_top, "0", "1", "2", "0", "0", "0", "0"],
        ["2", page_width, "1", "1", "3", "0", "0", margin_left, "0"],
        ["2", page_height, "0", "1", "4", "0", "0", margin_top, "0"],
    ]
    return [
        descriptor.info_kind,
        [
            root_panel_base_info_record(),
            "26",
            "0",
            "2",
            ["0", "3", "1"],
            ["0", "4", "1"],
            "2",
            ["0", "2", "2"],
            ["0", "3", "2"],
            "3",
            ["0", "2", "3"],
            ["0", "3", "3"],
            ["0", "4", "3"],
            "0",
            "0",
            page_style_group_record("1"),
            "0",
            "1",
            ["1", "1", ["6", page_title, page_style_group_record("0"), "-1", "1", "1", quoted_atom("Страница1"), "1", default_color_record(), default_color_record(), ["8", "3", "0", "1", "100"], "1"]],
            "1",
            "1",
            "0",
            "4",
            *position_records,
            "0",
            "4294967295",
            "5",
            "64",
            "0",
            default_color_record(),
            "0",
            "0",
            "57",
            "0",
            "0",
        ],
        ["0"],
    ]


def panel_base_info_record() -> list[object]:
    return base_info_record_from_xml(None)


def root_panel_base_info_record() -> list[object]:
    return [
        "19",
        "1",
        default_color_record(),
        default_color_record(),
        ["8", "3", "0", "1", "100"],
        "0",
        ["4", "3", ["-22"], "3"],
        default_color_record(),
        default_color_record(),
        ["4", "3", ["-7"], "3"],
        ["4", "3", ["-21"], "3"],
        ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        ["1", "0"],
        "0",
        "0",
        "100",
        "2",
        "2",
        "1",
        "2",
        default_color_record(),
    ]


def default_color_record() -> list[object]:
    return ["4", "4", ["0"], "4"]


def empty_page_style_record() -> list[object]:
    return ["4", "0", ["0"], '""', "-1", "-1", "1", "0", '""']


def page_style_group_record(active: str) -> list[object]:
    return ["10", active, empty_page_style_record(), empty_page_style_record(), empty_page_style_record(), "100", "0", "0", "0", "0", "0"]


def panel_extent_value(value: str, offset: int) -> str:
    if value.isdigit():
        return str(max(int(value) - offset, 0))
    return value


def base_info_record_from_xml(element: ET.Element | None) -> list[object]:
    return [
        "10",
        visible_record_from_xml(element),
        color_record_from_xml(element, "TextColor"),
        color_record_from_xml(element, "BackColor"),
        font_record_from_xml(element.find("Font") if element is not None else None),
        "0",
        color_record_from_xml(element, "BorderColor"),
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["3", "3", ["-7"]],
        ["3", "3", ["-21"]],
        ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        tooltip_record_from_xml(element),
    ]


def visible_record_from_xml(element: ET.Element | None) -> str:
    if element is None:
        return "1"
    value = (element.findtext("Visible") or "").strip().lower()
    return "0" if value in {"false", "0"} else "1"


def tooltip_record_from_xml(element: ET.Element | None) -> list[object]:
    text = get_multilang_text(element, "ToolTip")
    return localized_text_record(text) if text else ["1", "0"]


def color_record_from_xml(element: ET.Element | None, tag: str) -> list[object]:
    if element is None:
        return ["3", "4", ["0"]]
    node = element.find(tag)
    if node is None or not node.text:
        return ["3", "4", ["0"]]
    return ["3", "3", [node.text.strip()]]


def font_record_from_xml(font: ET.Element | None) -> list[object]:
    if font is None:
        return ["6", "3", "0", "1"]
    result: list[object] = [font.get("kind", "6"), font.get("family", "3"), font.get("style", "0")]
    deltas = [delta.text or "0" for delta in font.findall("Delta")]
    result.append(deltas if deltas else ["0"])
    for value in font.findall("Value"):
        result.append(value.text or "0")
    return result


def attributes_table(attributes: list[object]) -> list[object]:
    max_slot = 0
    for attribute in attributes:
        if isinstance(attribute, list) and attribute and isinstance(attribute[0], list) and attribute[0]:
            try:
                max_slot = max(max_slot, int(str(attribute[0][0])))
            except ValueError:
                pass
    return [["1"], str(max_slot + 1 if attributes else 0), ["3", *attributes], ["0"]]


def attribute_record_from_xml(attribute: ET.Element) -> list[object]:
    object_id = attribute.get("id", "0")
    visible_id = attribute.get("slot") or object_id
    pattern = type_pattern_from_xml(attribute)
    type_record: list[object] = [quoted_atom("Pattern")]
    if pattern:
        type_record.append(pattern)
    return [
        [visible_id],
        "0",
        "0",
        "1",
        quoted_atom(attribute.get("name", "")),
        type_record,
    ]


def form_object_info_record() -> list[object]:
    return ["59d6c227-97d3-46f6-84a0-584c5a2807e1", "1", ["2", "0", ["0", "0"], ["0"], "1"]]


def events_from_xml(root: ET.Element) -> list[object]:
    result: list[object] = []
    for event in root.findall("./Events/Event"):
        name = event.get("name")
        uuid = event.get("uuid")
        if not name or not uuid:
            continue
        result.append(["70001", uuid, ["3", quoted_atom(name), event_descriptor(name)]])
    return result


def event_descriptor(name: str, title: str | None = None) -> list[object]:
    title = title or name.replace("ПриОткрытии", "При открытии")
    return [
        "1",
        quoted_atom(name),
        localized_text_record(title),
        localized_text_record(title),
        localized_text_record(title),
        empty_page_style_record(),
        ["0", "0", "0"],
    ]


def form_title_from_xml(root: ET.Element) -> str:
    return get_multilang_text(root, "Title")


def form_size_from_xml(root: ET.Element) -> tuple[str, str, bool]:
    width = (root.findtext("Width") or "").strip()
    height = (root.findtext("Height") or "").strip()
    return width or "885", height or "244", bool(width and height)


def top_level_pages(root: ET.Element) -> list[ET.Element]:
    return root.findall("./Pages/Page")


def get_multilang_text(parent: ET.Element | None, tag: str) -> str:
    if parent is None:
        return ""
    node = parent.find(tag)
    if node is None:
        return ""
    for item in node.findall("Item"):
        if item.get("lang") in (None, "ru"):
            return item.text or ""
    first = node.find("Item")
    return "" if first is None else (first.text or "")


def type_pattern_from_xml(attribute: ET.Element) -> list[object]:
    pattern = attribute.find("./Type/Pattern")
    if pattern is None:
        return []
    result: list[object] = []
    for item in pattern.findall("PatternItem"):
        code = item.get("code", "")
        if not code:
            continue
        result.append(quote_atom(code) if not is_integer_atom(code) else code)
        uuid = item.get("uuid")
        if code == "#" and uuid:
            result.append(uuid)
    return result


def control_stream_from_xml(
    element: ET.Element,
    asset_root: Path | None,
    attribute_type_patterns: dict[str, list[object]] | None = None,
    attribute_slots: dict[str, str] | None = None,
) -> list[object] | None:
    return control_stream_from_xml_with_page(element, asset_root, attribute_type_patterns or {}, attribute_slots or {}, None, None)


def control_stream_from_xml_with_page(
    element: ET.Element,
    asset_root: Path | None,
    attribute_type_patterns: dict[str, list[object]],
    attribute_slots: dict[str, str],
    page_index: int | None,
    page_order: int | None,
) -> list[object] | None:
    control_type = control_type_from_xml_tag(element.tag)
    if not control_type:
        return None
    class_id = ORDINARY_CONTROL_GUID_BY_TYPE.get(control_type)
    if class_id is None:
        raise ValueError(f"Unsupported ordinary form control type for stream writer: {element.tag}")
    object_id = required_control_id(element)
    name = required_control_name(element)
    info = control_info_from_xml(element, name, control_type, asset_root, attribute_type_patterns)
    data_path = data_path_from_xml(element) if control_type in DATA_BOUND_CONTROL_TYPES else ""
    data_slot = attribute_slots.get(data_path, "")
    if not data_slot and control_type == "RadioButton":
        data_slot = attribute_slots.get(radio_group_data_path(data_path), "")
    radio_ordinal = radio_group_ordinal(data_path) if control_type == "RadioButton" else 0
    geometry = geometry_stream_from_xml(
        control_type,
        element.find("Position"),
        page_index,
        page_order,
        data_slot,
        radio_ordinal,
    )
    metadata_name = data_path if control_type in DATA_BOUND_CONTROL_TYPES else name
    if control_type == "CommandBar" and name not in {"КоманднаяПанель1", "КоманднаяПанель3", "ОсновныеДействияФормы"}:
        metadata_scope = "8"
    elif control_type == "CommandBar" and name == "КоманднаяПанель3":
        metadata_scope = CONTROL_METADATA_SCOPE["default"]
    else:
        metadata_scope = CONTROL_METADATA_SCOPE.get(control_type, CONTROL_METADATA_SCOPE["default"])
    metadata = ["14", quoted_atom(metadata_name), metadata_scope, "0", "0", "0"]
    if control_type == "RadioButton":
        metadata[5] = bool_record_from_xml(element, "FirstInGroup", default=False)
    children: list[object] = []
    for child in element:
        child_stream = control_stream_from_xml_with_page(child, asset_root, attribute_type_patterns, attribute_slots, None, None)
        if child_stream:
            children.append(child_stream)
    pages = element.find("Pages")
    if pages is not None:
        page_children: list[tuple[int, list[object]]] = []
        for page_number, page in enumerate(pages.findall("Page")):
            page_child_order = 0
            for child in page:
                if not control_type_from_xml_tag(child.tag):
                    continue
                child_stream = control_stream_from_xml_with_page(child, asset_root, attribute_type_patterns, attribute_slots, page_number, page_child_order)
                if child_stream:
                    page_children.append((int(child_stream[1]), child_stream))
                    page_child_order += 1
        children.extend(child for _, child in sorted(page_children, key=lambda item: item[0]))
    child_table: list[object] = [str(len(children)), *children]
    return [class_id, object_id, info, geometry, metadata, child_table]


DATA_BOUND_CONTROL_TYPES = {
    "InputField",
    "CheckBox",
    "Table",
    "ChoiceField",
    "SpreadsheetDocumentField",
    "RadioButton",
    "Chart",
    "PivotChart",
    "GeographicalSchemaField",
    "GraphicalSchemaField",
    "ListBox",
    "HTMLDocumentField",
    "ProgressBar",
    "TrackBar",
    "CalendarField",
    "PeriodChooser",
    "TextDocumentField",
    "GanttChart",
    "Dendrogram",
}


def data_path_from_xml(element: ET.Element) -> str:
    data_path = element.findtext("DataPath")
    if data_path and data_path.strip():
        return data_path.strip()
    return required_control_name(element)


def control_type_from_xml_tag(tag: str) -> str:
    for control_type, descriptor in ORDINARY_CONTROL_DESCRIPTORS.items():
        if descriptor.xml_tag == tag:
            return control_type
    return tag if tag in ORDINARY_CONTROL_DESCRIPTORS else ""


def required_control_id(element: ET.Element) -> str:
    object_id = element.get("id")
    if not object_id:
        raise ValueError(f"Ordinary form control <{element.tag}> must have an explicit id")
    try:
        int(object_id)
    except ValueError as exc:
        raise ValueError(f"Ordinary form control <{element.tag}> id must be an integer: {object_id}") from exc
    return object_id


def required_control_name(element: ET.Element) -> str:
    name = element.get("name")
    if not name:
        raise ValueError(f"Ordinary form control <{element.tag}> must have an explicit name")
    return name


def radio_group_data_path(data_path: str) -> str:
    return re.sub(r"\d+$", "1", data_path)


def radio_group_ordinal(data_path: str) -> int:
    match = re.search(r"(\d+)$", data_path)
    if not match:
        return 0
    return max(int(match.group(1)) - 1, 0)


def control_info_from_xml(
    element: ET.Element,
    name: str,
    control_type: str,
    asset_root: Path | None,
    attribute_type_patterns: dict[str, list[object]],
) -> list[object]:
    title = get_multilang_text(element, "Title")
    title_record = localized_text_record(title or name)
    actions = control_actions_from_xml(element)
    if control_type == "Panel":
        return panel_control_info_from_xml(element, title_record)
    if control_type == "Button":
        return button_control_info(element, title_record, actions)
    if control_type == "Image":
        picture_payload = picture_payload_from_xml(element.find("Picture"), asset_root)
        return image_control_info(element, title_record, picture_payload)
    if control_type == "CheckBox":
        return checkbox_control_info(element, title_record, actions)
    if control_type == "ChoiceField":
        return choice_field_control_info(element, actions)
    if control_type == "RadioButton":
        data_path = data_path_from_xml(element)
        return radio_button_control_info(
            element,
            title_record,
            actions,
            attribute_type_patterns.get(data_path, []) or attribute_type_patterns.get(radio_group_data_path(data_path), []),
        )
    if control_type == "InputField":
        data_path = data_path_from_xml(element)
        return input_field_control_info(element, actions, attribute_type_patterns.get(data_path, []))
    if control_type == "GroupBox":
        return group_box_control_info(element, title_record)
    if control_type == "Splitter":
        return splitter_control_info(element)
    if control_type == "Chart":
        return chart_control_info()
    if control_type == "PivotChart":
        return pivot_chart_control_info(element, title_record)
    if control_type == "GanttChart":
        return gantt_chart_control_info(element, title_record)
    if control_type == "Dendrogram":
        return dendrogram_control_info(element, title_record)
    if control_type == "HTMLDocumentField":
        return html_document_field_control_info()
    if control_type == "ListBox":
        return list_box_control_info(element)
    if control_type == "ProgressBar":
        return progress_bar_control_info(element)
    if control_type == "TrackBar":
        return track_bar_control_info(element)
    if control_type == "CalendarField":
        return calendar_field_control_info(element)
    if control_type == "TextDocumentField":
        return text_document_field_control_info(element)
    if control_type == "GeographicalSchemaField":
        return geographical_schema_field_control_info(element)
    if control_type == "GraphicalSchemaField":
        return graphical_schema_field_control_info(element)
    if control_type == "CommandBar":
        return command_bar_control_info(element)
    if control_type == "Table":
        data_path = data_path_from_xml(element)
        return table_control_info(element, actions, attribute_type_patterns.get(data_path, []))
    if control_type == "SpreadsheetDocumentField":
        return spreadsheet_document_field_control_info(element, actions)
    if control_type == "Label":
        return label_control_info(element, title_record, actions)
    raise ValueError(f"Unsupported ordinary form control type for stream writer: {control_type}")


def control_actions_from_xml(element: ET.Element) -> list[object]:
    actions = action_table_from_xml(element.find("Action"))
    if actions:
        return actions
    return event_table_from_xml(element.find("Events"))


def panel_control_info_from_xml(element: ET.Element, title_record: list[object]) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["Panel"]
    pages = element.find("Pages")
    page_nodes = pages.findall("Page") if pages is not None else []
    page_count = len(page_nodes) or 1
    position = element.find("Position")
    raw_width = position.get("width", "1228") if position is not None else "1228"
    raw_height = position.get("height", "1054") if position is not None else "1054"
    width = panel_extent_value(raw_width, 8)
    height = panel_extent_value(raw_height, 26)
    state_table = panel_state_table(element, title_record, extended=True)
    position_records = panel_position_records(page_count, width, height, mode="6")
    return [
        descriptor.info_kind,
        [
            root_panel_base_info_record(),
            "26",
            "0",
            *panel_control_slot_profile(),
            "0",
            "0",
            page_style_group_record("1"),
            "1",
            "1",
            state_table,
            "1",
            "1",
            "0",
            str(len(position_records)),
            *position_records,
            "0",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "4294967295",
            "5",
            "64",
            "0",
            default_color_record(),
            "0",
            "0",
            "57",
            "0",
            "0",
        ],
        ["0"],
    ]


def panel_control_slot_profile() -> list[object]:
    return [
        "12",
        ["0", "8", "1"],
        ["0", "19", "1"],
        ["0", "25", "1"],
        ["0", "26", "1"],
        ["0", "28", "1"],
        ["0", "29", "1"],
        ["0", "30", "1"],
        ["0", "31", "1"],
        ["0", "33", "1"],
        ["0", "39", "1"],
        ["0", "40", "1"],
        ["0", "41", "1"],
        "1",
        ["0", "25", "3"],
        "18",
        ["0", "7", "3"],
        ["0", "8", "3"],
        ["0", "10", "3"],
        ["0", "19", "3"],
        ["0", "21", "3"],
        ["0", "23", "3"],
        ["0", "25", "3"],
        ["0", "26", "3"],
        ["0", "27", "3"],
        ["0", "28", "3"],
        ["0", "29", "3"],
        ["0", "30", "3"],
        ["0", "31", "3"],
        ["0", "32", "3"],
        ["0", "33", "3"],
        ["0", "39", "3"],
        ["0", "40", "3"],
        ["0", "41", "3"],
    ]


def panel_state_table(element: ET.Element, fallback_title: list[object], *, extended: bool = False) -> list[object]:
    pages = element.find("Pages")
    page_nodes = pages.findall("Page") if pages is not None else []
    if not page_nodes:
        name = element.get("name", "Страница1")
        if extended:
            return ["1", "1", panel_state_record("6", fallback_title, name)]
        return ["1", "1", panel_state_record("3", fallback_title, name)]
    states: list[object] = []
    for page in page_nodes:
        name = page.get("name", "Страница1")
        title = get_multilang_text(page, "Title") or name
        states.append(panel_state_record("6" if extended else "3", localized_text_record(title), name))
    return ["1", str(len(states)), *states]


def panel_state_record(record_kind: str, title: list[object], name: str) -> list[object]:
    record = [
        record_kind,
        title,
        ["3", "0", ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]],
        "-1",
        "1",
        "1",
        quoted_atom(name),
        "1",
    ]
    if record_kind == "6":
        record[2] = page_style_group_record("0")
        record.extend([default_color_record(), default_color_record(), ["8", "3", "0", "1", "100"], "1"])
    return record


def panel_position_records(page_count: int, width: str, height: str, *, mode: str = "2") -> list[list[object]]:
    records: list[list[object]] = []
    for page_index in range(page_count):
        page_width = width
        page_height = height
        horizontal_mode = mode
        vertical_mode = mode
        if mode == "2" and page_count > 1 and page_index == page_count - 1 and width.isdigit() and height.isdigit():
            page_width = str(max(int(width) - 2, 0))
            page_height = str(max(int(height) - 2, 0))
            horizontal_mode = "4"
            vertical_mode = "4"
        records.extend(
            [
                ["2", "6", "1", "1", "1", str(page_index), "0", "0", "0"],
                ["2", "6", "0", "1", "2", str(page_index), "0", "0", "0"],
                ["2", page_width, "1", "1", "3", str(page_index), "0", horizontal_mode, "0"],
                ["2", page_height, "0", "1", "4", str(page_index), "0", vertical_mode, "0"],
            ]
        )
    return records


def label_control_info(element: ET.Element, title_record: list[object], actions: list[object]) -> list[object]:
    title = get_multilang_text(element, "Title") or element.get("name", "")
    label_mode = "0" if title.endswith(":") else "4"
    return [
        "3",
        [
            extended_base_info_record_from_xml(element),
            "11",
            title_record,
            label_mode,
            "1",
            "1" if actions else "0",
            "0",
            "0",
            ["0", "0", "0"],
            "0",
            ["1", "0"],
            "1",
            ["10", label_mode, empty_page_style_record(), empty_page_style_record(), empty_page_style_record(), "100", "2", "0", "0", "1", "2"],
            "4",
            "0",
            "0",
            "0",
            "0",
            "0",
            "0",
            "0",
        ],
        ["1", *actions] if actions else ["0"],
    ]


def input_field_control_info(element: ET.Element, actions: list[object], type_pattern: list[object] | None = None) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["InputField"]
    pattern = type_pattern or [quoted_atom("S")]
    return [
        descriptor.info_kind,
        [quoted_atom("Pattern"), pattern],
        [input_field_info_record_from_xml(element)],
        input_field_data_source_record(element),
        ["1", *actions] if actions else ["0"],
        "0",
        "1",
        "0",
        ["1", "0"],
        "0",
    ]


def input_field_data_source_record(element: ET.Element) -> list[object]:
    if element.findtext("DataPath"):
        return [
            "1",
            [
                "9a7643d2-19e9-45e2-8893-280bc9195a97",
                ["4", [quoted_atom("U")], [quoted_atom("U")], "0", '""', "0", "0"],
            ],
        ]
    return ["0"]


def input_field_info_record_from_xml(element: ET.Element) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["InputField"]
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    record = [
        base,
        "31",
        "0",
        "0",
        "1",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "1",
        "0",
        "0",
        "0",
        "0",
        "0",
        "4",
        "0",
        [quoted_atom("U")],
        [quoted_atom("U")],
        '""',
        "0",
        "1",
        "0",
        "0",
        "0",
        "0",
        empty_page_style_record(),
        empty_page_style_record(),
        "0",
        "0",
        "0",
        ["0", "0", "0"],
        ["1", "0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "16777215",
        "2",
        "0",
        "0",
    ]
    record[descriptor.slot_index("ReadOnly")] = bool_record_from_xml(element, "ReadOnly", default=False)
    return record


def button_control_info(element: ET.Element, title_record: list[object], actions: list[object]) -> list[object]:
    return [
        "1",
        [
            button_base_info_record(element),
            "14",
            title_record,
            "1",
            "1",
            "0",
            "0",
            "0",
            empty_page_style_record(),
            ["0", "0", "0"],
            "0",
            "0",
            "0",
            "0",
            "0",
            "1",
        ],
        ["1", *actions] if actions else ["0"],
    ]


def checkbox_control_info(element: ET.Element, title_record: list[object], actions: list[object]) -> list[object]:
    return [
        "1",
        [
            [
                extended_base_info_record_from_xml(element),
                "7",
                title_record,
                "1",
                "0",
                "1",
                "0",
                "100",
                "1",
            ],
            "4",
            "0",
            "0",
            "0",
            "0",
            "0",
        ],
        ["1", *actions] if actions else ["0"],
    ]


def choice_field_control_info(element: ET.Element, actions: list[object]) -> list[object]:
    return [
        "2",
        choice_field_info_record_from_xml(element),
        ["1", *actions] if actions else ["0"],
    ]


def choice_field_info_record_from_xml(element: ET.Element) -> list[object]:
    return [
        choice_field_base_info_record_from_xml(element),
        "31",
        "0",
        "0",
        "1",
        "0",
        "1",
        "0",
        "0",
        "0",
        "0",
        "1",
        bool_record_from_xml(element, "ReadOnly", default=False),
        "0",
        "255",
        "0",
        "0",
        "4",
        "0",
        [quoted_atom("U")],
        [quoted_atom("U")],
        '""',
        "0",
        bool_record_from_xml(element, "ChoiceButton", default=True),
        bool_record_from_xml(element, "ClearButton", default=True),
        bool_record_from_xml(element, "OpenButton", default=False),
        bool_record_from_xml(element, "CreateButton", default=False),
        bool_record_from_xml(element, "EditButton", default=False),
        empty_page_style_record(),
        empty_page_style_record(),
        "0",
        "0",
        "0",
        ["0", "0", "0"],
        ["1", "0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "16777215",
        "2",
        "0",
        "0",
    ]


def choice_field_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    return base


def radio_button_control_info(
    element: ET.Element,
    title_record: list[object],
    actions: list[object],
    type_pattern: list[object] | None = None,
) -> list[object]:
    pattern = type_pattern or [quoted_atom("B")]
    return [
        "4",
        [quoted_atom("Pattern"), pattern],
        [
            checkbox_control_inner_info(element, title_record),
            "4",
            "0",
            "0",
            "0",
            "0",
        ],
        "0",
        [pattern[0], "0"] if pattern else [quoted_atom("B"), "0"],
        ["1", *actions] if actions else ["0"],
    ]


def checkbox_control_inner_info(element: ET.Element, title_record: list[object]) -> list[object]:
    return [
        extended_base_info_record_from_xml(element),
        "7",
        title_record,
        "1",
        "0",
        "1",
        "0",
        "100",
        "1",
    ]


def image_control_info(element: ET.Element, title_record: list[object], picture_payload: str) -> list[object]:
    picture_record: list[object]
    if picture_payload:
        picture_record = ["3", "3", ["0"], '""', "-1", "-1", "0", [[picture_payload]], "0"]
    else:
        picture_record = page_style_group_record("0")
    return [
        "1",
        [
            extended_base_info_record_from_xml(element),
            "20",
            "0",
            "0",
            picture_record,
            ["0", "0", "0"],
            "1",
            "1",
            "0",
            "0",
            ["1", "0"],
            "0",
            "1",
            "0",
            "1",
        ],
        ["0"],
    ]


def group_box_control_info(element: ET.Element, title_record: list[object]) -> list[object]:
    return [
        "0",
        [
            group_box_base_info_record_from_xml(element),
            "8",
            title_record,
            ["3", "0", ["0"], "6", "1", "0", "cf48d3ca-5bd4-45b9-bb8f-a0922a8335f2"],
            "0",
        ],
    ]


def group_box_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[4] = ["8", "3", "4", "700", "1", "100"]
    return base


def splitter_control_info(element: ET.Element) -> list[object]:
    return [
        "0",
        [
            splitter_base_info_record_from_xml(element),
            "2",
            "2",
            "0",
        ],
    ]


def splitter_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[5] = "1"
    base[11] = ["3", "0", ["-18"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"]
    return base


def chart_control_info() -> list[object]:
    return ["11"]


def pivot_chart_control_info(element: ET.Element, title_record: list[object]) -> list[object]:
    body = DIAGRAM_BODY_DESCRIPTOR.build({2: diagram_presentation_record(element, title_record, kind="pivot")})
    return PIVOT_CHART_INFO_DESCRIPTOR.build({1: body})


def gantt_chart_control_info(element: ET.Element, title_record: list[object]) -> list[object]:
    return [
        "19",
        [
            "0",
            chart_control_info(),
            diagram_presentation_record(element, title_record, kind="gantt"),
            "0",
            "0",
            "0",
            "0",
            ["0"],
            "0",
            "0",
            "0",
            "0",
        ],
        ["0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
    ]


def dendrogram_control_info(element: ET.Element, title_record: list[object]) -> list[object]:
    return [
        "0",
        [
            "0",
            chart_control_info(),
            diagram_presentation_record(element, title_record, kind="dendrogram"),
            "0",
            "0",
            "0",
            "0",
            ["0"],
            "0",
            "0",
        ],
    ]


def diagram_presentation_record(element: ET.Element, title_record: list[object], *, kind: str) -> list[object]:
    kind_code = (element.findtext("PivotChartKind") or "").strip() if kind == "pivot" else ""
    if not kind_code:
        kind_code = {"pivot": "4", "gantt": "1", "dendrogram": "1"}.get(kind, "1")
    record = [
        "75",
        "1",
        "0",
        "1",
        "0",
        ["4", "0", ["1644953"], "0"],
        ["4", "0", ["0"], "1", "2", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
        "3",
        title_record,
        "0",
        "0",
        "0",
        "1",
        [quoted_atom("U")],
        [quoted_atom("U")],
        "0",
        "1",
        "0",
        "-1",
        "0",
        kind_code,
        "0",
        quoted_atom(", "),
        "0",
        ["1", "0"],
        ["1", "0"],
        ["4", "3", ["-3"], "3"],
        "0",
        "0",
        title_record,
        "1",
        "0",
        ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        ["4", "3", ["-22"], "3"],
        ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        ["4", "3", ["-22"], "3"],
        ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        ["4", "3", ["-22"], "3"],
        "0",
        ["4", "3", ["-1"], "3"],
        "1",
        ["4", "3", ["-1"], "3"],
        "1",
        ["4", "3", ["-1"], "3"],
        "0",
        ["4", "0", ["16777215"], "0"],
        ["4", "3", ["-3"], "3"],
        ["4", "3", ["-3"], "3"],
        ["4", "3", ["-3"], "3"],
        ["8", "2", "0", ["-20"], "1", "100"],
        ["8", "2", "0", ["-20"], "1", "100"],
        ["8", "2", "0", ["-20"], "1", "100"],
        "1",
        "1",
        "1",
        "1",
        "1",
        ["1", "0"],
        "0",
    ]
    if kind == "pivot":
        apply_pivot_chart_fields(record, element, kind_code)
        apply_pivot_chart_source_data(record, element)
    return record


def apply_pivot_chart_fields(record: list[object], element: ET.Element, kind_code: str) -> None:
    fields = sorted(
        element.findall("Fields/Field"),
        key=lambda node: (node.get("role", ""), int(node.get("order", "0") or "0")),
    )
    dimension_fields = [field for field in fields if field.get("role") == "dimension"]
    measure_fields = [field for field in fields if field.get("role") == "measure"]
    if dimension_fields:
        record[1] = str(len(dimension_fields))
        write_pivot_chart_field_records(record, dimension_fields, start_index=2, kind_code=kind_code, dimension=True)
    if measure_fields:
        ensure_list_size(record, 62, "0")
        record[60] = "1"
        record[61] = str(len(measure_fields))
        write_pivot_chart_field_records(record, measure_fields, start_index=62, kind_code=kind_code, dimension=False)


def write_pivot_chart_field_records(
    record: list[object],
    fields: list[ET.Element],
    *,
    start_index: int,
    kind_code: str,
    dimension: bool,
) -> None:
    ensure_list_size(record, start_index + len(fields) * 11, "0")
    for order, field in enumerate(fields):
        offset = start_index + order * 11
        if dimension:
            prefix: list[object] = ["4", "1", kind_code] if order == 0 else [[quoted_atom("U")], [quoted_atom("U")], "0"]
            values = [
                *prefix,
                color_record_from_xml_value(field.get("color", "")),
                ["4", "0", ["0"], "1", "2", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
                field.get("axis", "0") or "0",
                localized_text_record(field.get("title", "")),
                "1",
                "0",
                "0",
                field.get("value", "0") or "0",
            ]
        else:
            values = [
                localized_text_record(field.get("title", "")),
                field.get("value", "0") or "0",
                "1",
                color_record_from_xml_value(field.get("color", "")),
                ["4", "0", ["0"], "1", "2", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
                field.get("axis", "0") or "0",
                "0",
                "0",
                [quoted_atom("U")],
                [quoted_atom("U")],
                "0",
            ]
        record[offset : offset + 11] = values


def apply_pivot_chart_source_data(record: list[object], element: ET.Element) -> None:
    points = element.findall("SourceData/Point")
    if not points:
        return
    ensure_list_size(record, 206, "0")
    del record[206:]
    for point in points:
        record.append([quoted_atom(point.get("valueType", "N") or "N"), point.get("value", "0") or "0"])
        record.append([quoted_atom(point.get("unit", "U") or "U")])
        record.append(quoted_atom(point.text or ""))


def ensure_list_size(record: list[object], size: int, fill: object) -> None:
    while len(record) < size:
        record.append(copy.deepcopy(fill))


def color_record_from_xml_value(value: str | None) -> list[object]:
    return ["4", "0", [value or "0"], "0"]


def html_document_field_control_info() -> list[object]:
    return [
        "5",
        "0",
        ["0"],
        ["3", "3", ["-22"]],
        ["3", "1", ["-18"], "0", "0", "0"],
        "1",
        "0",
    ]


def progress_bar_control_info(element: ET.Element) -> list[object]:
    return [
        "0",
        [
            base_info_record_from_xml(element),
            "3",
            element.findtext("MinimumValue") or "0",
            element.findtext("MaximumValue") or "100",
            element.findtext("Step") or "1",
            element.findtext("BigStep") or "1",
            "0",
            "2",
        ],
    ]


def track_bar_control_info(element: ET.Element) -> list[object]:
    return [
        "1",
        [
            extended_base_info_record_from_xml(element),
            "5",
            element.findtext("MinimumValue") or "0",
            element.findtext("MaximumValue") or "100",
            element.findtext("Step") or "1",
            element.findtext("BigStep") or "10",
            element.findtext("Orientation") or "2",
            "2",
            element.findtext("MarkStep") or "5",
            element.findtext("CurrentValue") or "100",
        ],
        ["0"],
    ]


def calendar_field_control_info(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    return [
        "1",
        [
            base,
            "9",
            ["4", "3", ["-16"], "3"],
            ["4", "3", ["-14"], "3"],
            ["4", "3", ["-15"], "3"],
            element.findtext("PeriodStart") or "00010101000000",
            element.findtext("PeriodEnd") or "00010101000000",
            "1",
            "1",
            "0",
            "0",
            "0",
            "0",
            "1",
        ],
        ["0"],
    ]


def text_document_field_control_info(element: ET.Element) -> list[object]:
    return [
        extended_base_info_record_from_xml(element),
        "6",
        "1",
        "00000000-0000-0000-0000-000000000000",
        ["0"],
        "0",
        "0",
    ]


def geographical_schema_field_control_info(element: ET.Element) -> list[object]:
    return base_info_record_from_xml(element)


def graphical_schema_field_control_info(element: ET.Element) -> list[object]:
    return [
        base_info_record_from_xml(element),
        "5",
        [
            [
                "5",
                [
                    [
                        "1",
                        ["3", "3", ["-10"]],
                        "1",
                        "20",
                        "20",
                        "3",
                        "6",
                        "6",
                        [quoted_atom("N"), "10"],
                    ]
                ],
                "0",
                "0",
            ]
        ],
        ["0"],
        "0",
        "0",
    ]


def list_box_control_info(element: ET.Element) -> list[object]:
    return [
        "1",
        [
            list_box_base_info_record_from_xml(element),
            extended_list_box_view_record_from_xml(element),
            "6",
            "0",
            "0",
            "1",
            "0",
        ],
        ["0"],
    ]


def list_box_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    return base


def command_bar_control_info(element: ET.Element) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["CommandBar"]
    title = get_multilang_text(element, "Title")
    record = [
        command_bar_base_info_record(element),
        "9",
        "2",
        "1",
        "0",
        "0",
        "1",
        command_bar_items_record(element, title),
        "b78f2e80-ec68-11d4-9dcf-0050bae2bc79",
        "4",
        "ccbe7ec8-cb04-45b3-9722-2a58bf1ae4ed"
        if element.get("name") == "ОсновныеДействияФормы" or title
        else "9d0a2e40-b978-11d4-84b6-008048da06df",
        "0",
        "0",
        "0",
    ]
    if element.get("name") == "ОсновныеДействияФормы" or element.find("Title") is not None:
        record[3] = "0"
        record[4] = "2"
        record[11] = "1"
    elif element.get("name") == "КоманднаяПанель3":
        record[3] = "0"
        record[5] = "1"
    else:
        record[5] = "1"
    record[descriptor.slot_index("Autofill")] = bool_record_from_xml(element, "Autofill", default=True)
    return [
        descriptor.info_kind,
        record,
    ]


def command_bar_items_record(element: ET.Element, title: str) -> list[object]:
    if element.get("name") != "ОсновныеДействияФормы" and not title:
        if element.get("name") == "КоманднаяПанель1":
            palette_uuid = "6013c551-1c48-4ef4-a466-6fbe824675ca"
            palette_kind = "9"
            placement = ["0", "0", ["0"]]
        elif element.get("name") == "КоманднаяПанель3":
            palette_uuid = "db776490-56d2-4c30-80a3-9cab0228ae12"
            palette_kind = "0"
            placement = ["-1", "0", ["0"]]
        else:
            palette_uuid = "32be4565-4169-4cb1-bbb8-775fc630a82d"
            palette_kind = "11"
            placement = ["0", "0", ["0"]]
        return [
            "5",
            palette_uuid,
            palette_kind,
            "1",
            "0",
            "1",
            ["5", "b78f2e80-ec68-11d4-9dcf-0050bae2bc79", "4", "0", "0", placement],
        ]
    action_name = compact_identifier(title) or "КнопкаВыполнитьНажатие"
    return [
        "5",
        "b0931d44-c134-43ba-9852-f9690e20b460",
        "3",
        "1",
        "3",
        [
            "8",
            "04ccea6f-37da-4ac5-9cb1-6965c01cd2d3",
            "1",
            "fbe38877-b914-4fd5-8540-07dde06ba2e1",
            ["6", "2", "00000000-0000-0000-0000-000000000000", "142", ["1", "0", "357c6a54-357d-425d-a2bd-22f4f6e86c87", "2147483647", "0"], "0", "1"],
            "0",
            "2",
            "1",
        ],
        ["8", "70d8a1ee-a609-43ad-9f3e-2e573ca60a7a", "1", "abde0c9a-18a6-4e0c-bbaa-af26b911b3e6", ["1", "9d0a2e40-b978-11d4-84b6-008048da06df", "0"], "0", "2", "1"],
        [
            "8",
            "e4ede41c-fdd1-4120-a053-d6044264d385",
            "1",
            DEFAULT_CONTROL_EVENT_UUID,
            ["3", quoted_atom(action_name), command_bar_action_descriptor(action_name, title or action_name)],
            "0",
            "2",
            "1",
        ],
        "1",
        [
            "5",
            "b78f2e80-ec68-11d4-9dcf-0050bae2bc79",
            "4",
            "0",
            "3",
            "e4ede41c-fdd1-4120-a053-d6044264d385",
            ["8", quoted_atom("ОсновныеДействияФормыВыполнить"), "0", "1", localized_text_record("Выполнить"), "1", "b0931d44-c134-43ba-9852-f9690e20b460", "1", "1e2", "0", "1", "1", "0", "1", "0", "0"],
            "70d8a1ee-a609-43ad-9f3e-2e573ca60a7a",
            ["8", quoted_atom("Разделитель"), "0", "1", ["1", "0"], "0", "b0931d44-c134-43ba-9852-f9690e20b460", "2", "1e2", "2", "1", "1", "0", "1", "0", "0"],
            "04ccea6f-37da-4ac5-9cb1-6965c01cd2d3",
            ["8", quoted_atom("ОсновныеДействияФормыЗакрыть"), "0", "1", localized_text_record("Закрыть"), "1", "b0931d44-c134-43ba-9852-f9690e20b460", "3", "1e2", "0", "1", "1", "0", "1", "0", "0"],
            ["-1", "0", ["0"]],
        ],
    ]


def compact_identifier(value: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in value.split())


def command_bar_action_descriptor(name: str, title: str) -> list[object]:
    return [
        "1",
        quoted_atom(name),
        localized_text_record(title),
        localized_text_record(title),
        localized_text_record(title),
        ["4", "0", ["0"], '""', "-1", "-1", "1", "0", '""'],
        ["0", "0", "0"],
    ]


def command_bar_base_info_record(element: ET.Element) -> list[object]:
    tooltip = tooltip_record_from_xml(element)
    if element.get("name") == "ОсновныеДействияФормы" or element.find("Title") is not None:
        command_scope = "7"
    elif element.get("name") == "КоманднаяПанель1":
        command_scope = "4"
    else:
        command_scope = "0"
    return [
        "19",
        visible_record_from_xml(element),
        default_color_record(),
        default_color_record(),
        ["8", "3", "0", "1", "100"],
        "0",
        ["4", "3", ["-22"], "3"],
        default_color_record(),
        default_color_record(),
        default_color_record(),
        ["4", "3", ["-21"], "3"],
        [
            "3",
            "0",
            ["0"],
            command_scope,
            "0" if command_scope == "0" else "1",
            "0",
            "48312c09-257f-4b29-b280-284dd89efc1e" if command_scope == "0" else "00000000-0000-0000-0000-000000000000",
        ],
        tooltip,
        "0",
        "0",
        "100",
        "0",
        "0",
        "0",
        "0",
        default_color_record(),
    ]


def table_control_info(element: ET.Element, actions: list[object], type_pattern: list[object]) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["Table"]
    pattern = type_pattern or [quoted_atom("#"), "00000000-0000-0000-0000-000000000000"]
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    return [
        descriptor.info_kind,
        [quoted_atom("Pattern"), pattern],
        [
            base,
            table_view_record_from_xml(element),
        ],
        table_data_source_record(element),
        ["1", *actions] if actions else ["0"],
    ]


def table_data_source_record(element: ET.Element) -> list[object]:
    if table_columns_from_xml(element):
        return ["342cf854-134c-42bb-8af9-a2103d5d9723", ["5", "0", "0", "1"]]
    return ["00000000-0000-0000-0000-000000000000", ["2", "1", ["0", "1"]]]


def table_view_record_from_xml(element: ET.Element) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["Table"]
    columns_count = element.findtext("ColumnsCount") or "0"
    rows_count = element.findtext("RowsCount") or "0"
    columns = table_columns_from_xml(element)
    if columns:
        return extended_table_view_record(columns)
    record = [
        "12",
        "100801549",
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["3", "3", ["-14"]],
        ["3", "3", ["-15"]],
        ["3", "3", ["-13"]],
        "2",
        "2",
        "0",
        "0",
        "0",
        bool_record_from_xml(element, "ReadOnly", default=False),
        "0",
        "1",
        "1",
        ["6", "2", "0", ["-20"], "1"],
        ["6", "2", "0", ["-20"], "1"],
        "0",
        "0",
        "1",
        ["0"],
    ]
    record[descriptor.slot_index("RowsCount")] = rows_count.strip() or "0"
    record[descriptor.slot_index("ColumnsCount")] = columns_count.strip() or "0"
    record[descriptor.slot_index("AutoMarkIncomplete")] = bool_record_from_xml(
        element,
        "AutoMarkIncomplete",
        default=True,
    )
    return record


def table_columns_from_xml(element: ET.Element) -> list[ET.Element]:
    columns = element.find("Columns")
    if columns is None:
        return []
    return columns.findall("Column")


def extended_table_view_record(columns: list[ET.Element]) -> list[object]:
    return [
        "23",
        "117644301",
        default_color_record(),
        default_color_record(),
        default_color_record(),
        default_color_record(),
        ["4", "3", ["-14"], "3"],
        ["4", "3", ["-15"], "3"],
        ["4", "3", ["-13"], "3"],
        "2",
        "2",
        "0",
        "0",
        "0",
        "0",
        "0",
        "1",
        "1",
        ["8", "2", "0", ["-20"], "1", "100"],
        ["8", "2", "0", ["-20"], "1", "100"],
        "2",
        "0",
        "1",
        ["5", *[table_column_record(column, index) for index, column in enumerate(columns)]],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "100",
        "1",
        "2",
        "1",
        "1",
        "0",
        "0",
        "2",
    ]


def table_column_record(column: ET.Element, index: int) -> list[object]:
    title = get_multilang_text(column, "Title") or column.get("name") or f"Колонка{index + 1}"
    order = column.get("order") or str(index)
    pattern = type_pattern_from_xml(column) or [quoted_atom("S")]
    payload = TABLE_COLUMN_VALUE_PAYLOAD_BY_PATTERN.get(tuple(pattern), TABLE_COLUMN_VALUE_PAYLOAD_BY_PATTERN[(quoted_atom("S"),)])
    body = [
        "23",
        localized_text_record(title),
        ["1", "0"],
        ["1", "0"],
        "1e2",
        order,
        "-1",
        "-1",
        "-1",
        "12590592",
        empty_page_style_record(),
        empty_page_style_record(),
        empty_page_style_record(),
        "16",
        "16",
        "d2314b5d-8da4-4e0f-822b-45e7500eae09",
        default_color_record(),
        default_color_record(),
        default_color_record(),
        default_color_record(),
        default_color_record(),
        default_color_record(),
        ["8", "3", "0", "1", "100"],
        ["8", "3", "0", "1", "100"],
        ["8", "3", "0", "1", "100"],
        "1",
        "0",
        "0",
        "4",
        "0",
        quoted_atom(title),
        [],
        "15",
        "0",
        ["1", "0"],
        [quoted_atom("Pattern"), pattern],
        "0",
        "1",
        ORDINARY_CONTROL_GUID_BY_TYPE["InputField"],
        [[payload], "0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "1e2",
        "0",
        "1",
        "0",
        "0",
        "2",
        "0",
    ]
    return [
        "737535a4-21e6-4971-8513-3e3173a9fedd",
        ["8", ["8", body, ["-1"], ["-1"], ["-1"]], quoted_atom(title), '""', '""', "0"],
    ]


def spreadsheet_document_field_control_info(element: ET.Element, actions: list[object]) -> list[object]:
    position = element.find("Position")
    left = position.get("left", "0") if position is not None else "0"
    top = position.get("top", "0") if position is not None else "0"
    right = position.get("right", "100") if position is not None else "100"
    bottom = position.get("bottom", "100") if position is not None else "100"
    return [
        "18",
        left,
        top,
        right,
        bottom,
        "5",
        "5",
        "0",
        "1",
        spreadsheet_back_color_record_from_xml(element),
        ["3", "1", ["-18"], "0", "0", "0"],
        spreadsheet_settings_record(),
        "0",
        "1",
        ["3", "0", "0", "100", "0", "0", "0", "1", "1", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", quoted_atom("ru"), "0", "1", ["3", "0", "0", "0", "0", "00000000-0000-0000-0000-000000000000"], "0", "0", "0", "0", "0"],
        "1",
        "1",
        ["1", *actions] if actions else ["0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "1",
        "0",
        "1",
        "1",
        "0",
        "0",
        "0",
        "0",
        "1",
        "1",
    ]


def spreadsheet_back_color_record_from_xml(element: ET.Element) -> list[object]:
    node = element.find("BackColor")
    if node is None or not node.text:
        return ["4", "3", ["-22"], "3"]
    return color_record_from_xml(element, "BackColor")


def spreadsheet_settings_record() -> list[object]:
    return [
        "8",
        "1",
        "12",
        [quoted_atom("ru"), quoted_atom("ru"), "1", "1", quoted_atom("ru"), quoted_atom("Русский"), quoted_atom("Русский"), "1"],
        ["128", "72"],
        ["0"],
        "0",
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        "0",
        "2",
        "0",
        ["0", "0", "00000000-0000-0000-0000-000000000000", "0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        ["0"],
        ["0"],
        ["0"],
        ["0"],
        '""',
        [["0", "6", "6", [quoted_atom("N"), "1000"], "7", [quoted_atom("N"), "1000"], "8", [quoted_atom("N"), "1000"], "9", [quoted_atom("N"), "1000"], "10", [quoted_atom("N"), "1000"], "11", [quoted_atom("N"), "1000"]]],
        ["0", "-1", "-1", "-1", "-1", "00000000-0000-0000-0000-000000000000"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "1",
        "0",
        "1",
        "0",
        "0",
        "0",
        "0",
        "0",
        "2",
        ["4", "3", ["-1"], "3"],
        ["4", "3", ["-3"], "3"],
        "0",
        "0",
        "0",
        '""',
        "0",
        ["3", "0", "0", "100", "1", "1", "0", "1", "1", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", '""', "0", "0", "0", "0", "0", "0", "0"],
        ["0"],
        "0",
        "0",
        "0",
        "1",
        "0",
        "0",
        "0",
    ]


def list_box_view_record_from_xml(element: ET.Element) -> list[object]:
    return [
        "12",
        "100743712",
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["3", "3", ["-14"]],
        ["3", "3", ["-15"]],
        ["3", "3", ["-13"]],
        "2",
        "2",
        "0",
        "0",
        "0",
        "1",
        "0",
        "1",
        "1",
        ["6", "2", "0", ["-20"], "1"],
        ["6", "2", "0", ["-20"], "1"],
        "0",
        "0",
        bool_record_from_xml(element, "MultiLine", default=True),
        "0",
    ]


def extended_list_box_view_record_from_xml(element: ET.Element) -> list[object]:
    return [
        "23",
        "100743712",
        default_color_record(),
        default_color_record(),
        default_color_record(),
        default_color_record(),
        ["4", "3", ["-14"], "3"],
        ["4", "3", ["-15"], "3"],
        ["4", "3", ["-13"], "3"],
        "2",
        "2",
        "0",
        "0",
        "0",
        "1",
        "0",
        "1",
        "1",
        ["8", "2", "0", ["-20"], "1", "100"],
        ["8", "2", "0", ["-20"], "1", "100"],
        "2",
        "0",
        bool_record_from_xml(element, "MultiLine", default=True),
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "100",
        "1",
        "2",
        "2",
        "2",
        "0",
        "0",
        "2",
    ]


def button_base_info_record(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[5] = "1"
    return base


def extended_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = root_panel_base_info_record()
    base[1] = visible_record_from_xml(element)
    base[12] = tooltip_record_from_xml(element)
    base[16] = "0"
    base[17] = "0"
    base[18] = "0"
    base[19] = "0"
    return base


def bool_record_from_xml(element: ET.Element | None, tag: str, *, default: bool) -> str:
    if element is None:
        return "1" if default else "0"
    value = (element.findtext(tag) or "").strip().lower()
    if value in {"true", "1"}:
        return "1"
    if value in {"false", "0"}:
        return "0"
    return "1" if default else "0"


def action_table_from_xml(action: ET.Element | None) -> list[object]:
    if action is None or not action.get("name") or not action.get("uuid"):
        return []
    name = action.get("name", "")
    uuid = action.get("uuid", "")
    title = action.get("title") or name
    return [["0", uuid, ["3", quoted_atom(name), event_descriptor(name, title)]]]


def event_table_from_xml(events: ET.Element | None) -> list[object]:
    if events is None:
        return []
    result: list[object] = []
    for event in events.findall("Event"):
        event_name = event.get("name", "")
        handler = (event.text or "").strip() or event_name
        uuid = event.get("uuid") or DEFAULT_CONTROL_EVENT_UUID
        result.append(["2147483647", uuid, ["3", quoted_atom(handler), event_descriptor(handler, event_name)]])
    return result


def picture_payload_from_xml(picture: ET.Element | None, asset_root: Path | None) -> str:
    if picture is None or asset_root is None:
        return ""
    file_name = picture.get("file")
    if not file_name:
        return ""
    image_path = asset_root / file_name
    if not image_path.exists():
        return ""
    return wrap_base64_payload(base64.b64encode(image_path.read_bytes()).decode("ascii"))


def wrap_base64_payload(payload: str) -> str:
    lines = ["#base64:" + payload[:64]]
    lines.extend(payload[index : index + 64] for index in range(64, len(payload), 64))
    return "\r\n\r\n".join(lines)


def geometry_stream_from_xml(
    control_type: str,
    position: ET.Element | None,
    page_index: int | None = None,
    page_order: int | None = None,
    data_slot: str = "",
    radio_ordinal: int = 0,
) -> list[object]:
    left = position.get("left", "0") if position is not None else "0"
    top = position.get("top", "0") if position is not None else "0"
    right = position.get("right", "0") if position is not None else "0"
    bottom = position.get("bottom", "0") if position is not None else "0"
    layout_group = position.get("layoutGroup") if position is not None else None
    layout_order = position.get("layoutOrder") if position is not None else None
    bindings: list[object] = ["0"] * 6
    dimensions: list[object] = ["0"] * 4
    if position is not None:
        binding_container = position.find("Bindings")
        if binding_container is not None:
            for binding in binding_container.findall("Binding"):
                slot = binding.get("slot")
                if not slot and binding.get("coordinate"):
                    mapped = BINDING_COORDINATE_SLOT.get(binding.get("coordinate", ""))
                    slot = str(mapped) if mapped is not None else None
                if slot:
                    try:
                        index = int(slot) - 1
                    except ValueError:
                        index = -1
                    if 0 <= index < len(bindings):
                        bindings[index] = binding_to_raw(binding)
            for binding in binding_container.findall("DimensionBinding"):
                slot = binding.get("slot")
                if not slot and binding.get("dimension"):
                    mapped = DIMENSION_NAME_SLOT.get(binding.get("dimension", ""))
                    slot = str(mapped) if mapped is not None else None
                if slot:
                    try:
                        index = int(slot) - 1
                    except ValueError:
                        index = -1
                    if 0 <= index < len(dimensions):
                        dimensions[index] = dimension_binding_to_raw(binding)
    if control_type == "Splitter":
        default_group = str(page_order) if page_order is not None else "0"
        default_order = str(page_index) if page_index is not None else "0"
        return [
            "8",
            left,
            top,
            right,
            bottom,
            "1",
            *bindings,
            "0",
            *dimensions,
            "0",
            "0",
            *layout_group_tail(layout_group, layout_order, default_group, default_order),
        ]
    if control_type in {"SpreadsheetDocumentField", "TextDocumentField", "PivotChart"} and page_index is not None and page_order is not None:
        return [
            "8",
            left,
            top,
            right,
            bottom,
            "1",
            *bindings,
            *dimensions,
            "0",
            "0",
            *layout_group_tail(layout_group, layout_order, str(page_order), str(page_index)),
        ]
    if page_index is None or page_order is None:
        trailer = GEOMETRY_TRAILER_PROFILE.get(control_type, GEOMETRY_TRAILER_PROFILE["default"])
        if control_type == "CommandBar" and dimensions[2] != "0":
            trailer = ["0", "0", "0", "0", "1", "3", "1", "1"]
        dimension_flag = "1" if any(dimension != "0" for dimension in dimensions) else "0"
        return ["8", left, top, right, bottom, "1", *bindings, dimension_flag, *dimensions, *trailer]
    if control_type == "Table":
        return [
            "8",
            left,
            top,
            right,
            bottom,
            "1",
            *bindings,
            "0",
            "0",
            "0",
            "0",
            "0",
            "0",
            str(page_index),
            "1",
            str(page_index),
            "0",
            "0",
        ]
    if control_type in {"CheckBox", "RadioButton"} and data_slot:
        default_order = str(radio_ordinal) if control_type == "RadioButton" else "0"
        group_tail = layout_group_tail(layout_group, layout_order, data_slot, default_order)
        group_offsets = (
            group_tail[1:3]
            if control_type == "RadioButton"
            else ["0", "1"]
        )
        return [
            "8",
            left,
            top,
            right,
            bottom,
            "1",
            *bindings,
            "1" if any(dimension != "0" for dimension in dimensions) else "0",
            *dimensions,
            "0",
            "0",
            "0",
            group_tail[0],
            *group_offsets,
            "0",
            "0",
        ]
    if data_slot:
        group_tail = layout_group_tail(layout_group, layout_order, data_slot, "1")
        dimension_flag = "1" if dimensions[0] != "0" else "0"
        dimension_part = [dimensions[0]] if dimension_flag == "1" else []
        return [
            "8",
            left,
            top,
            right,
            bottom,
            "1",
            *bindings,
            dimension_flag,
            *dimension_part,
            "0",
            "0",
            "0",
            "0",
            "0",
            *group_tail[:3],
            "0",
            "0",
        ]
    paged_trailer = ["0", "0"] if control_type == "CommandBar" else GEOMETRY_TRAILER_PROFILE["paged"]
    group_tail = layout_group_tail(layout_group, layout_order, str(page_order), str(page_index))
    return [
        "8",
        left,
        top,
        right,
        bottom,
        "1",
        *bindings,
        "1",
        *dimensions,
        *paged_trailer,
        *group_tail[:3],
        "0",
        "0",
    ]


def layout_group_tail(
    layout_group: str | None,
    layout_order: str | None,
    default_group: str,
    default_order: str,
) -> list[str]:
    group = layout_group if layout_group is not None else default_group
    order = layout_order if layout_order is not None else default_order
    try:
        next_order = str(int(order) + 1)
    except ValueError:
        next_order = "0"
    return [group, order, next_order, "0", "0"]


def apply_geometry_bindings_to_raw(geometry: ET.Element, raw_geometry: list[object]) -> None:
    bindings = geometry.find("Bindings")
    if bindings is None:
        return
    for binding in bindings.findall("Binding"):
        slot = binding.get("slot")
        if not slot and binding.get("coordinate"):
            mapped = BINDING_COORDINATE_SLOT.get(binding.get("coordinate", ""))
            slot = str(mapped) if mapped is not None else None
        if not slot:
            continue
        try:
            index = 5 + int(slot)
        except ValueError:
            continue
        if 0 <= index < len(raw_geometry):
            raw_geometry[index] = binding_to_raw(binding)
    for binding in bindings.findall("DimensionBinding"):
        slot = binding.get("slot")
        if not slot and binding.get("dimension"):
            mapped = DIMENSION_NAME_SLOT.get(binding.get("dimension", ""))
            slot = str(mapped) if mapped is not None else None
        if not slot:
            continue
        try:
            index = 12 + int(slot)
        except ValueError:
            continue
        if 0 <= index < len(raw_geometry):
            raw_geometry[index] = dimension_binding_to_raw(binding)


def binding_to_raw(node: ET.Element) -> object:
    if "value" in node.attrib:
        return node.get("value", "")
    result: list[object] = []
    if "mode" in node.attrib:
        result.append(node.get("mode", "0"))
    for tag in ("From", "To"):
        child = node.find(tag)
        if child is not None:
            result.append(anchor_to_raw(child))
    for child in node:
        if child.tag.startswith("Extra"):
            result.append(anchor_to_raw(child))
    return result


def dimension_binding_to_raw(node: ET.Element) -> object:
    if "value" in node.attrib:
        return node.get("value", "")
    result: list[object] = []
    if "mode" in node.attrib:
        result.append(node.get("mode", "0"))
    if "target" in node.attrib or "targetId" in node.attrib:
        result.append(anchor_target_id(node))
    if "side" in node.attrib:
        result.append(anchor_edge_code(node.get("side", "none")))
    for child in node:
        if child.tag.startswith("Extra"):
            result.append(anchor_to_raw(child))
    return result


def anchor_to_raw(node: ET.Element) -> object:
    if "value" in node.attrib:
        return node.get("value", "")
    return [
        anchor_kind_code(node.get("relation") or node.get("kindName", "targetEdgeOffset")),
        anchor_target_id(node),
        anchor_edge_code(node.get("side", "none")),
        node.get("offset", "0"),
    ]


def anchor_kind_code(name: str) -> str:
    for code, value in ANCHOR_KIND_MAP.items():
        if value == name:
            return code
    return "2"


def anchor_edge_code(name: str) -> str:
    for code, value in EDGE_NAME_MAP.items():
        if value == name:
            return code
    return "6"


def anchor_target_id(node: ET.Element) -> str:
    target = node.get("target")
    if target == "none":
        return "-1"
    if target == "parent":
        return "0"
    if target == "self":
        return node.get("targetId", "0")
    return node.get("targetId", "-1")


def quoted_atom(value: str) -> str:
    return quote_atom(value)
