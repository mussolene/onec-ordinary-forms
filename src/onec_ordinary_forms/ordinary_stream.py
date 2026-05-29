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
from onec_ordinary_forms.ui_values import ordinary_color_code_from_style_ref
from onec_ordinary_forms.value_codec import clean_atom, is_integer_atom, localized_text_record, quote_atom


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
    ('"S"',): "#base64:AgFTS2/0iI3BTqDV67a9oKcNjVE7DsIwDEWMSJ24gWdHipOUxLdAQhygQFcG1A3l\r\r\nZAwciSsQxwXxqQR1KvfZzvN76nI+q8/tcj0zNoszrLth6E9HqGADuVlk+dRDjCQo\r\r\nYJBkM4Y8hRN6tGWUbCnZf258Yy/JxIz+reDoUfGqxVCSHSVqldAqLLvRlaDyfiwQ\r\r\nT550Sq+O8YqCCoct5GcGeBuqtHakLT1DcqQPkH90nwwT0l8ErWKMjtriQabGn1F9\r\r\ncxdXwR+cIe7ZhLZ3JiX2xiW72zNx23FUDZ8urPqodCNjXUwqSGXkOw==",
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
    "CommandBar": ["0", "0", "0", "1", "1", "1", "0"],
    "InputField": ["0", "0", "0", "3", "3", "0", "0"],
    "Label": ["0", "0", "0", "0", "2", "2", "0", "0"],
    "Panel": ["0", "0", "2", "2", "0", "0"],
    "Table": ["0", "0", "0", "4", "0", "0"],
    "paged": ["0", "0", "0"],
}

SHORT_POSITION_PARENT_ANCHORED_TYPES = {
    "CommandBar",
    "PivotChart",
    "SpreadsheetDocumentField",
    "Splitter",
    "TextDocumentField",
}

SHORT_POSITION_FIXED_HEIGHT_TYPES = {
    "CommandBar",
    "Splitter",
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
            control = control_stream_from_xml(child, asset_root, attribute_type_patterns, attribute_slots, {})
            if control:
                controls.append(control)

    stream = ordinary_form_stream(
        title or "Main",
        attributes,
        controls,
        events_from_xml(root),
        form_size_from_xml(root),
        serialization_counter=form_serialization_counter_from_xml(root),
    )
    return ("\ufeff" + dumps_list_out_stream(stream)).encode("utf-8")


def ordinary_form_stream(
    title: str,
    attributes: list[object],
    controls: list[object],
    events: list[object],
    form_size: tuple[str, str, bool],
    root_layout: dict[str, str] | None = None,
    attributes_layout: dict[str, object] | None = None,
    object_info: list[object] | None = None,
    root_style: list[object] | None = None,
    serialization_counter: str = "",
) -> list[object]:
    return [
        "27",
        form_root_record(title, controls, form_size, root_layout, serialization_counter),
        attributes_table(attributes, controls, attributes_layout),
        copy.deepcopy(object_info) if object_info is not None else form_object_info_record(),
        ["1", *events] if events else ["0"],
        "1",
        "4",
        "1",
        "0",
        "0",
        "0",
        ["0"],
        ["0"],
        copy.deepcopy(root_style) if root_style is not None else ["10", "0", empty_page_style_record(), empty_page_style_record(), empty_page_style_record(), "100", "0", "0", "0", "0", "0"],
        "1",
        "2",
        "0",
        "0",
        "1",
        "1",
    ]


def form_root_record(
    title: str,
    controls: list[object],
    form_size: tuple[str, str, bool],
    root_layout: dict[str, str] | None = None,
    serialization_counter: str = "",
) -> list[object]:
    width, height, explicit_size = form_size
    compact_root = root_layout is not None and root_layout.get("recordKind") == "16" and root_layout.get("rootPanelInfoProfile") == "21"
    root_panel_info_template = (root_layout or {}).get("rootPanelInfo")
    if isinstance(root_panel_info_template, list):
        root_panel_info_record = copy.deepcopy(root_panel_info_template)
    else:
        root_panel_info_record = (
            compact_root_panel_info(title, width, height) if compact_root else root_panel_info(title, width, height, controls)
        )
    root_panel = [
        ORDINARY_CONTROL_GUID_BY_TYPE["Panel"],
        root_panel_info_record,
        ["1", *controls] if len(controls) == 1 else [str(len(controls)), *controls],
    ]
    if compact_root:
        return [
            "16",
            [localized_text_record(title), root_layout.get("titleMarker", "2"), root_layout.get("titleScope", "4294967295")],
            root_panel,
            width,
            height,
            root_layout.get("slot5", "1"),
            root_layout.get("slot6", "1"),
            root_layout.get("slot7", "1"),
            root_layout.get("slot8", "4"),
            root_layout.get("slot9", "4"),
            root_layout.get("slot10", "38"),
        ]
    record = [
        (root_layout or {}).get("recordKind", "16"),
        [
            localized_text_record(title),
            (root_layout or {}).get("titleMarker", "52"),
            (root_layout or {}).get("titleScope", "4294967295"),
        ],
        root_panel,
        width,
        height,
        (root_layout or {}).get("slot5", "1"),
        (root_layout or {}).get("slot6", "0"),
        (root_layout or {}).get("slot7", "1"),
        (root_layout or {}).get("slot8", "4"),
        (root_layout or {}).get("slot9", "4"),
        (root_layout or {}).get("slot10", "6"),
    ]
    if explicit_size:
        record[0] = "18"
        record[1][1] = (root_layout or {}).get("titleMarker", "4" if controls else "41")
        record[1][2] = (root_layout or {}).get("titleScope", "4294967295" if controls else "3")
        record[-1] = serialization_counter or (root_layout or {}).get("slot10", "3")
        record.extend([width, height, "96"])
    return record


def compact_root_panel_info(title: str, form_width: str, form_height: str) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["FormRootPanel"]
    margin_left = "8"
    margin_top = "33"
    page_width = panel_extent_value(form_width, 8)
    page_title = localized_text_record("Страница1")
    position_records = [
        ["2", margin_left, "1", "1", "1", "0", "0", "0", "0"],
        ["2", margin_top, "0", "1", "2", "0", "0", "0", "0"],
        ["2", page_width, "1", "1", "3", "0", "0", margin_left, "0"],
        ["2", form_height, "0", "1", "4", "0", "0", "0", "0"],
    ]
    return [
        descriptor.info_kind,
        [
            compact_root_panel_base_info_record(),
            "21",
            "0",
            "1",
            ["0", "1", "1"],
            "1",
            ["0", "2", "2"],
            "2",
            ["0", "1", "3"],
            ["0", "2", "3"],
            "0",
            "0",
            ["3", "1", compact_empty_page_style_record()],
            "0",
            "1",
            ["1", "1", ["3", page_title, ["3", "0", compact_empty_page_style_record()], "-1", "1", "1", quoted_atom("Страница1"), "1"]],
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
        ],
        ["0"],
    ]


def root_panel_info(title: str, form_width: str, form_height: str, controls: list[object] | None = None) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["FormRootPanel"]
    margin_left = "8"
    margin_top = "33"
    max_right, max_bottom = controls_extent(controls or [])
    if max_right and max_bottom:
        page_width = str(max_right + 8)
        page_height = str(max_bottom)
        right_offset = str(max(int_atom(form_width) - max_right - 24, 0))
        return root_panel_info_from_control_extent(page_width, page_height, right_offset)
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


def root_panel_info_from_control_extent(page_width: str, page_height: str, right_offset: str) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["FormRootPanel"]
    page_title = localized_text_record("Страница1")
    position_records = [
        ["2", "8", "1", "1", "1", "0", "0", "0", "0"],
        ["2", "33", "0", "1", "2", "0", "0", "0", "0"],
        ["2", page_width, "1", "1", "3", "0", "0", right_offset, "0"],
        ["2", page_height, "0", "1", "4", "0", "0", "0", "0"],
    ]
    return [
        descriptor.info_kind,
        [
            root_panel_base_info_record(),
            "26",
            "0",
            "1",
            ["0", "1", "1"],
            "1",
            ["0", "2", "2"],
            "3",
            ["0", "1", "3"],
            ["0", "2", "3"],
            ["0", "4", "3"],
            "0",
            "0",
            page_style_group_record("1"),
            "0",
            "1",
            [
                "1",
                "1",
                [
                    "6",
                    page_title,
                    page_style_group_record("0"),
                    "-1",
                    "1",
                    "1",
                    quoted_atom("Страница1"),
                    "1",
                    default_color_record(),
                    default_color_record(),
                    ["8", "3", "0", "1", "100"],
                    "1",
                ],
            ],
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


def controls_extent(controls: list[object]) -> tuple[int, int]:
    max_right = 0
    max_bottom = 0
    for control in controls:
        if not isinstance(control, list) or len(control) <= 3 or not isinstance(control[3], list):
            continue
        geometry = control[3]
        if len(geometry) <= 4:
            continue
        right = int_atom(geometry[3])
        bottom = int_atom(geometry[4])
        max_right = max(max_right, right)
        max_bottom = max(max_bottom, bottom)
    return max_right, max_bottom


def int_atom(value: object) -> int:
    try:
        return int(clean_atom(value))
    except (TypeError, ValueError):
        return 0


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


def regular_panel_base_info_record() -> list[object]:
    record = root_panel_base_info_record()
    record[17] = "1"
    return record


def compact_root_panel_base_info_record() -> list[object]:
    return [
        "10",
        "1",
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["6", "3", "0", "1"],
        "0",
        ["3", "3", ["-22"]],
        ["3", "4", ["0"]],
        ["3", "4", ["0"]],
        ["3", "3", ["-7"]],
        ["3", "3", ["-21"]],
        ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        ["1", "0"],
    ]


def default_color_record() -> list[object]:
    return ["4", "4", ["0"], "4"]


def compact_empty_page_style_record() -> list[object]:
    return ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]


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
    if node is None:
        return ["3", "4", ["0"]]
    value = color_value_from_xml_node(node)
    if not value:
        return ["3", "4", ["0"]]
    return ["3", "3", [value]]


def font_record_from_xml(font: ET.Element | None) -> list[object]:
    if font is None:
        return ["6", "3", "0", "1"]
    platform_kind = font.get("kind", "")
    if platform_kind == "AutoFont":
        height = font.get("height") or font.get("size")
        return ["6", "3", "0", ["0"], height] if height else ["6", "3", "0", "1"]
    result: list[object] = [font.get("kind", "6"), font.get("family", "3"), font.get("style", "0")]
    deltas = [delta.text or "0" for delta in font.findall("Delta")]
    result.append(deltas if deltas else ["0"])
    for value in font.findall("Value"):
        result.append(value.text or "0")
    if len(result) == 4 and font.get("size"):
        result.append(font.get("size") or "0")
    elif len(result) == 4 and font.get("height"):
        result.append(font.get("height") or "0")
    return result


def color_value_from_xml_node(node: ET.Element) -> str:
    for key in ("value", "decimal"):
        value = (node.get(key) or "").strip()
        if value:
            return value
    if (node.get("kind") or "") == "StyleItem":
        style_code = ordinary_color_code_from_style_ref(node.get("name") or node.get("ref") or "")
        if style_code is not None:
            return style_code
    rgb = (node.get("rgb") or "").strip()
    if not rgb and node.text:
        rgb = node.text.strip()
    if rgb.lower() == "auto":
        return ""
    style_code = ordinary_color_code_from_style_ref(rgb)
    if style_code is not None:
        return style_code
    if rgb.startswith("#") and len(rgb) == 7:
        try:
            return str(int(rgb[1:], 16))
        except ValueError:
            return rgb
    return rgb


def attributes_table(
    attributes: list[object],
    controls: list[object] | None = None,
    attributes_layout: dict[str, object] | None = None,
) -> list[object]:
    max_slot = 0
    for attribute in attributes:
        if isinstance(attribute, list) and attribute and isinstance(attribute[0], list) and attribute[0]:
            try:
                max_slot = max(max_slot, int(str(attribute[0][0])))
            except ValueError:
                pass
    marker = str((attributes_layout or {}).get("marker", "1"))
    slot_count = str((attributes_layout or {}).get("slotCount", "")) if attributes_layout else ""
    return [
        [marker],
        slot_count or str(max_slot + 1 if attributes else 0),
        [str(len(attributes)), *attributes],
        copy.deepcopy(attributes_layout["linkTable"])
        if isinstance((attributes_layout or {}).get("linkTable"), list)
        else attribute_link_table(attributes, controls or []),
    ]


def attribute_link_table(attributes: list[object], controls: list[object]) -> list[object]:
    controls_by_name: dict[str, str] = {}
    for control in iter_control_records(controls):
        if not isinstance(control, list) or len(control) < 5:
            continue
        metadata = control[-2]
        if not isinstance(metadata, list) or len(metadata) < 2 or metadata[0] != "14":
            continue
        controls_by_name[str(metadata[1])] = str(control[1])
    links: list[object] = []
    for attribute in attributes:
        if not isinstance(attribute, list) or len(attribute) < 5 or not isinstance(attribute[0], list) or not attribute[0]:
            continue
        slot = str(attribute[0][0])
        control_id = controls_by_name.get(str(attribute[4]))
        if control_id:
            links.append([control_id, ["1", [slot]]])
    return [str(len(links)), *links]


def iter_control_records(controls: list[object]) -> list[list[object]]:
    result: list[list[object]] = []
    for control in controls:
        if not isinstance(control, list):
            continue
        result.append(control)
        child_table = control[-1] if control else None
        if isinstance(child_table, list) and child_table:
            result.extend(iter_control_records(child_table[1:]))
    return result


def attribute_record_from_xml(
    attribute: ET.Element,
    record_flag: str | None = None,
    template: list[object] | None = None,
) -> list[object]:
    object_id = attribute.get("id", "0")
    visible_id = attribute.get("slot") or object_id
    pattern = type_pattern_from_xml(attribute)
    type_record: list[object] = [quoted_atom("Pattern")]
    if pattern:
        type_record.append(pattern)
    if template is not None and len(template) >= 5:
        result = copy.deepcopy(template)
        if attribute.get("slot"):
            result[0] = [visible_id]
        if len(result) > 1:
            result[1] = record_flag if record_flag is not None else result[1]
        type_index = next(
            (
                index
                for index, value in enumerate(result)
                if isinstance(value, list) and value and clean_atom(value[0]) == "Pattern"
            ),
            None,
        )
        if type_index is not None and type_index > 0:
            result[type_index - 1] = quoted_atom(attribute.get("name", ""))
            result[type_index] = type_record
            return result
    return [
        [visible_id],
        record_flag if record_flag is not None else "1",
        "0",
        "1",
        quoted_atom(attribute.get("name", "")),
        type_record,
    ]


def form_object_info_record() -> list[object]:
    return ["00000000-0000-0000-0000-000000000000", "0"]


def events_from_xml(root: ET.Element, style_profile: str = "extended") -> list[object]:
    result: list[object] = []
    for event in root.findall("./Events/Event"):
        name = event.get("name")
        uuid = event.get("uuid")
        if not name or not uuid:
            continue
        result.append(["70001", uuid, ["3", quoted_atom(name), event_descriptor(name, style_profile=style_profile)]])
    return result


def event_descriptor(name: str, title: str | None = None, style_profile: str = "extended") -> list[object]:
    title = title or name.replace("ПриОткрытии", "При открытии")
    style = compact_empty_page_style_record() if style_profile == "compact" else empty_page_style_record()
    return [
        "1",
        quoted_atom(name),
        localized_text_record(title),
        localized_text_record(title),
        localized_text_record(title),
        style,
        ["0", "0", "0"],
    ]


def form_title_from_xml(root: ET.Element) -> str:
    return get_multilang_text(root, "Title")


def form_size_from_xml(root: ET.Element, root_layout: dict[str, str] | None = None) -> tuple[str, str, bool]:
    width = (root.findtext("Width") or "").strip()
    height = (root.findtext("Height") or "").strip()
    if root_layout is not None and not (width and height):
        width = width or root_layout.get("width", "")
        height = height or root_layout.get("height", "")
        return width or "885", height or "244", False
    return width or "885", height or "244", bool(width and height)


def form_serialization_counter_from_xml(root: ET.Element) -> str:
    value = (root.findtext("SerializationCounter") or "").strip()
    return value if value.isdigit() else ""


def replace_first_localized_text_record(value: object, replacement: list[object]) -> bool:
    if not isinstance(value, list):
        return False
    if is_localized_text_record(value):
        value[:] = copy.deepcopy(replacement)
        return True
    for item in value:
        if replace_first_localized_text_record(item, replacement):
            return True
    return False


def is_localized_text_record(value: list[object]) -> bool:
    if len(value) != 3:
        return False
    if clean_atom(value[0]) != "1" or clean_atom(value[1]) != "1":
        return False
    items = value[2]
    return isinstance(items, list) and len(items) >= 2 and clean_atom(items[0]) == "ru" and isinstance(items[1], str)


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
    control_templates: dict[tuple[str, str], dict[str, object]] | None = None,
) -> list[object] | None:
    return control_stream_from_xml_with_page(element, asset_root, attribute_type_patterns or {}, attribute_slots or {}, control_templates or {}, None, None)


def control_stream_from_xml_with_page(
    element: ET.Element,
    asset_root: Path | None,
    attribute_type_patterns: dict[str, list[object]],
    attribute_slots: dict[str, str],
    control_templates: dict[tuple[str, str], dict[str, object]],
    page_index: int | None,
    page_order: int | None,
    parent_size: tuple[str, str] | None = None,
) -> list[object] | None:
    control_type = control_type_from_xml_tag(element.tag)
    if not control_type:
        return None
    class_id = ORDINARY_CONTROL_GUID_BY_TYPE.get(control_type)
    if class_id is None:
        raise ValueError(f"Unsupported ordinary form control type for stream writer: {element.tag}")
    object_id = required_control_id(element)
    name = required_control_name(element)
    control_template = control_templates.get((control_type, object_id)) or control_templates.get((control_type, name))
    title = get_multilang_text(element, "Title")
    title_record = localized_text_record(title or name)
    info = control_info_from_xml(element, name, control_type, asset_root, attribute_type_patterns, control_template)
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
        control_template,
        object_id,
        name,
        parent_size,
    )
    metadata_name = data_path if control_type in DATA_BOUND_CONTROL_TYPES else name
    if control_type == "CommandBar" and name not in {"КоманднаяПанель1", "КоманднаяПанель3", "ОсновныеДействияФормы"}:
        metadata_scope = "8"
    elif control_type == "CommandBar" and name == "КоманднаяПанель3":
        metadata_scope = CONTROL_METADATA_SCOPE["default"]
    else:
        metadata_scope = CONTROL_METADATA_SCOPE.get(control_type, CONTROL_METADATA_SCOPE["default"])
    template_metadata = (control_template or {}).get("metadata")
    metadata = copy.deepcopy(template_metadata) if isinstance(template_metadata, list) else ["14", quoted_atom(metadata_name), metadata_scope, "0", "0", "0"]
    if control_type == "RadioButton":
        metadata[5] = bool_record_from_xml(element, "FirstInGroup", default=False)
    children: list[object] = []
    for child in element:
        child_stream = control_stream_from_xml_with_page(child, asset_root, attribute_type_patterns, attribute_slots, control_templates, None, None)
        if child_stream:
            children.append(child_stream)
    pages = element.find("Pages")
    if pages is not None:
        child_parent_size = position_size(element.find("Position"))
        page_children: list[tuple[int, list[object]]] = []
        for page_number, page in enumerate(pages.findall("Page")):
            page_child_order = 0
            for child in page:
                if not control_type_from_xml_tag(child.tag):
                    continue
                child_stream = control_stream_from_xml_with_page(
                    child,
                    asset_root,
                    attribute_type_patterns,
                    attribute_slots,
                    control_templates,
                    page_number,
                    page_child_order,
                    child_parent_size,
                )
                if child_stream:
                    page_children.append((int(child_stream[1]), child_stream))
                    page_child_order += 1
        children.extend(child for _, child in sorted(page_children, key=lambda item: item[0]))
    child_table: list[object] = [str(len(children)), *children]
    if control_type == "Chart":
        return [class_id, object_id, info, chart_presentation_record(element, title_record, control_actions_from_xml(element)), geometry, metadata, child_table]
    if control_type == "GeographicalSchemaField":
        return [class_id, object_id, info, geographical_schema_settings_record(element), geometry, metadata, child_table]
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


def active_x_state_payload(element: ET.Element, slot: str) -> str:
    for blob in element.findall("./State/StateBlob"):
        if blob.get("slot") == slot:
            payload = "".join((blob.text or "").split())
            return wrap_base64_payload(payload) if payload else ""
    return ""


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
    control_template: dict[str, object] | None = None,
) -> list[object]:
    title = get_multilang_text(element, "Title")
    title_record = localized_text_record(title or name)
    actions = control_actions_from_xml(element)
    template_info = (control_template or {}).get("info")
    if isinstance(template_info, list):
        info = copy.deepcopy(template_info)
        replace_first_localized_text_record(info, title_record)
        return info
    if control_type == "Panel":
        return panel_control_info_from_xml(element, title_record, actions)
    if control_type == "ActiveXControl":
        return active_x_control_info(element)
    if control_type == "Button":
        return button_control_info(element, title_record, actions)
    if control_type == "Image":
        picture_payload = picture_payload_from_xml(element.find("Picture"), asset_root)
        return image_control_info(element, title_record, picture_payload, actions)
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
        return gantt_chart_control_info(element, title_record, actions)
    if control_type == "Dendrogram":
        return dendrogram_control_info(element, title_record)
    if control_type == "HTMLDocumentField":
        return html_document_field_control_info(actions)
    if control_type == "ListBox":
        return list_box_control_info(element, actions)
    if control_type == "ProgressBar":
        return progress_bar_control_info(element)
    if control_type == "TrackBar":
        return track_bar_control_info(element, actions)
    if control_type == "CalendarField":
        return calendar_field_control_info(element, actions)
    if control_type == "TextDocumentField":
        return text_document_field_control_info(element)
    if control_type == "GeographicalSchemaField":
        return geographical_schema_field_control_info(element)
    if control_type == "GraphicalSchemaField":
        return graphical_schema_field_control_info(element, actions)
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


def active_x_control_info(element: ET.Element) -> list[object]:
    clsid = element.findtext("Clsid", "").strip()
    if not clsid:
        raise ValueError("ActiveXControl must contain <Clsid>")
    state_1 = active_x_state_payload(element, "1")
    state_2 = active_x_state_payload(element, "2")
    return [
        "3",
        clsid.lower(),
        ["0"],
        "2",
        [state_1] if state_1 else ["0"],
        "8",
        "16960",
        "11721",
        [state_2] if state_2 else ["0"],
        ["0"],
    ]


def control_actions_from_xml(element: ET.Element) -> list[object]:
    actions = action_table_from_xml(element.find("Action"))
    if actions:
        return actions
    return event_table_from_xml(element.find("Events"))


def panel_control_info_from_xml(element: ET.Element, title_record: list[object], actions: list[object]) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["Panel"]
    pages = element.find("Pages")
    page_nodes = pages.findall("Page") if pages is not None else []
    page_count = len(page_nodes) or 1
    page_capacity = panel_page_capacity(page_count)
    position = element.find("Position")
    raw_width = position.get("width", "1228") if position is not None else "1228"
    raw_height = position.get("height", "1054") if position is not None else "1054"
    width = panel_extent_value(raw_width, 6)
    height = panel_extent_value(raw_height, 24)
    state_table = panel_state_table(element, title_record, extended=True, capacity=page_capacity)
    position_records = panel_position_records(page_capacity, width, height, mode="4")
    return [
        descriptor.info_kind,
        [
            regular_panel_base_info_record(),
            "26",
            "1",
            ["0", str(page_capacity), "1"],
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
            *panel_page_tail_markers(page_capacity),
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
        ["1", *actions] if actions else ["0"],
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
        "0",
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


def panel_page_capacity(page_count: int) -> int:
    return max(page_count, 25)


def panel_page_tail_markers(page_capacity: int) -> list[str]:
    return ["4294967295"] * page_capacity


def panel_state_table(
    element: ET.Element,
    fallback_title: list[object],
    *,
    extended: bool = False,
    capacity: int | None = None,
) -> list[object]:
    pages = element.find("Pages")
    page_nodes = pages.findall("Page") if pages is not None else []
    if not page_nodes:
        name = element.get("name", "Страница1")
        page_nodes = []
        states = [panel_state_record("6" if extended else "3", fallback_title, name)]
    else:
        states = []
        for page in page_nodes:
            name = page.get("name", "Страница1")
            title = get_multilang_text(page, "Title") or name
            states.append(panel_state_record("6" if extended else "3", localized_text_record(title), name))
    if capacity is not None and capacity > len(states):
        for index in range(len(states), capacity):
            name = f"Страница{index + 1}"
            states.append(panel_state_record("6" if extended else "3", localized_text_record(name), name))
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
    label_mode = element.findtext("TextPosition") or ("0" if title.endswith(":") else "4")
    label_style_mode = "0" if title.endswith(":") else label_mode
    base = extended_base_info_record_from_xml(element)
    base[6] = default_color_record()
    return [
        "3",
        [
            base,
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
            ["10", label_style_mode, empty_page_style_record(), empty_page_style_record(), empty_page_style_record(), "100", "2", "0", "0", "1", "2"],
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
        [input_field_info_record_from_xml(element, pattern)],
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


def input_field_info_record_from_xml(element: ET.Element, type_pattern: list[object]) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["InputField"]
    base = extended_base_info_record_from_xml(element)
    base[6] = default_color_record()
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
    if type_pattern == [quoted_atom("D"), quoted_atom("D")]:
        record[4] = "0"
        record[7] = "1"
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


def image_control_info(element: ET.Element, title_record: list[object], picture_payload: str, actions: list[object]) -> list[object]:
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
        ["1", *actions] if actions else ["0"],
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


def chart_presentation_record(element: ET.Element, title_record: list[object], actions: list[object] | None = None) -> list[object]:
    return diagram_presentation_record(element, title_record, kind="chart", actions=actions or [])


def pivot_chart_control_info(element: ET.Element, title_record: list[object]) -> list[object]:
    body = DIAGRAM_BODY_DESCRIPTOR.build({2: diagram_presentation_record(element, title_record, kind="pivot")})
    return PIVOT_CHART_INFO_DESCRIPTOR.build({1: body, 2: pivot_chart_info_secondary_record()})


def pivot_chart_info_secondary_record() -> list[object]:
    return [
        "0",
        [
            "0",
            [
                "3",
                "0",
                "1",
                "0",
                ["1", ["8", "0", "0", "0", "0", "0", [quoted_atom("U")], ["1", "0"], [quoted_atom("U")], "0", "4294967281"], "4294967295"],
                ["0", "1", ["0", ["4", "0", ["0"], "0"], ["4", "0", ["0"], "0"]]],
                "1",
                "0",
            ],
        ],
        [
            "0",
            [
                "3",
                "0",
                "1",
                "0",
                ["1", ["8", "0", "0", "0", "0", "0", [quoted_atom("U")], ["1", "0"], [quoted_atom("U")], "0", "4294911569"], "232515672"],
                ["0", "1", ["0", ["4", "0", ["0"], "0"], ["4", "0", ["0"], "0"]]],
                "1",
                "0",
            ],
        ],
        ["0", "0"],
        "1",
        "1",
    ]


def gantt_chart_control_info(element: ET.Element, title_record: list[object], actions: list[object]) -> list[object]:
    return [
        "19",
        [
            "0",
            chart_control_info(),
            diagram_presentation_record(element, title_record, kind="gantt", actions=actions),
        ],
        *gantt_chart_info_tail(),
    ]


def gantt_chart_info_tail() -> list[object]:
    return [
        [
            "1",
            [
                "3",
                "0",
                "1",
                "0",
                [
                    "2",
                    ["8", "0", "0", "0", "0", "0", [quoted_atom("U")], ["1", "0"], [quoted_atom("U")], "0", "4294949825"],
                    empty_page_style_record(),
                    ["8", "3", "0", "1", "100"],
                ],
                ["0", "1", ["0", ["0", ["4", "0", ["0"], "0"], ["4", "0", ["0"], "0"]], ["4", "4", ["0"], "4"], ["4", "4", ["0"], "4"]]],
                "1",
                "0",
            ],
        ],
        [
            "0",
            [
                "3",
                "0",
                "1",
                "0",
                ["3", ["8", "0", "0", "0", "0", "0", [quoted_atom("U")], ["1", "0"], [quoted_atom("U")], "0", "4294902785"]],
                ["0", "1", ["0", ["0", ["4", "0", ["0"], "0"], ["4", "0", ["0"], "0"]], ["4", "0", ["0"], "0"]]],
                "1",
                "0",
            ],
        ],
        "0",
        "0",
        "1",
        [
            "3",
            "0",
            "1",
            [
                "8",
                "30",
                "1",
                "1",
                ["4", "0", ["0"], "2", "1", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
                ["4", "0", ["12632256"], "0"],
                "3",
                ["1", "0"],
                ["0", ["1", "0", "0"]],
                ["4", "4", ["0"], "4"],
                ["4", "4", ["0"], "4"],
                "1",
            ],
            "0",
            ["4", "3", ["-10"], "3"],
            ["4", "3", ["-3"], "3"],
            "0",
        ],
        "2",
        "50",
        "1",
        "1",
        "20260524000000",
        "20260604235959",
        "20260524000000",
        "3",
        "3",
        "30",
        "0",
        "1",
        "0",
        ["1", "0"],
        ["4", "0", ["16777215"], "0"],
        ["3", ["0", ["1", "0", "0"], "0"], ["0", "0"]],
        "0",
        ["4", "0", ["8388608"], "0"],
        ["4", "0", ["0"], "1", "1", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
        ["0", "0", "0"],
        "0",
        "0",
        "1",
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
        ],
        *dendrogram_info_tail(),
    ]


def dendrogram_info_tail() -> list[object]:
    return [
        [
            "0",
            [
                "3",
                "0",
                "1",
                "0",
                ["0", ["8", "0", "0", "0", "0", "0", [quoted_atom("U")], ["1", "0"], [quoted_atom("U")], "0", "4294901793"]],
                ["0", "1", ["0", ["4", "0", ["0"], "0"], ["4", "0", ["0"], "0"]]],
                "1",
                "0",
            ],
        ],
        [
            "0",
            [
                "3",
                "0",
                "1",
                "0",
                ["0", ["8", "0", "0", "0", "0", "0", [quoted_atom("U")], ["1", "0"], [quoted_atom("U")], "0", "4294901761"], "0", "0", "0"],
                ["0", "1", ["0", ["4", "0", ["0"], "0"], ["4", "0", ["0"], "0"]]],
                "1",
                "0",
            ],
        ],
        "0",
        "1",
        "6",
        "12",
        ["4", "0", ["8388608"], "0"],
        ["4", "0", ["0"], "1", "1", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
        "0",
    ]

def diagram_presentation_record(element: ET.Element, title_record: list[object], *, kind: str, actions: list[object] | None = None) -> list[object]:
    kind_code = (element.findtext("PivotChartKind") or "").strip() if kind == "pivot" else ""
    if kind == "chart":
        kind_code = (element.findtext("ChartKind") or "").strip()
    if not kind_code:
        kind_code = {"pivot": "4", "chart": "6", "gantt": "6", "dendrogram": "6"}.get(kind, "1")
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
    if kind in {"chart", "gantt", "dendrogram"}:
        record[29] = localized_text_record(required_control_name(element))
    if kind in {"chart", "gantt"}:
        record[31] = "1"
    if kind == "gantt":
        record[36] = ["3", "0", ["0"], "1", "1", "0", "00000000-0000-0000-0000-000000000000"]
    if kind in {"chart", "gantt"}:
        record.extend(chart_presentation_tail(kind))
        if actions:
            record[-110] = "1"
            record.insert(-109, ["1", *actions])
    if kind == "dendrogram":
        record.extend(chart_presentation_tail(kind))
    if kind == "pivot":
        apply_pivot_chart_default_presentation_settings(record, title_record)
        apply_pivot_chart_fields(record, element, kind_code)
        apply_pivot_chart_source_data(record, element)
    return record


def chart_presentation_tail(kind: str = "chart") -> list[object]:
    axis = pivot_chart_axis_layout_record
    tail: list[object] = [
        ["4", "0", ["0"], "1", "1", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
        ["4", "4", ["0"], "4"],
        "1",
        "1",
        "0",
        "4",
        "30",
        "1",
        "0",
        "1",
        "0",
        "0",
        "1",
        "0",
        "0",
        "0",
        "0",
        "1",
        "1",
        "2",
        ["1", "0"],
        "1",
        "0",
        "0",
        "0",
        ["4", "0", ["169"], "0"],
        "0",
        "0",
        ["1", "0", "0", "0"],
        "0",
        "180",
        "5",
        "1",
        "0",
        "4",
        ["4", "0", ["11119017"], "0"],
        "1",
        "0",
        "1",
        "0",
        "1",
        "0",
        "0",
        "1.6875e-1",
        "0",
        "8.3125e-1",
        "6.388888888888888e-2",
        "0",
        "0",
        "8.3125e-1",
        "0",
        "0",
        "9.361111111111111e-1",
        "0",
        ["4", "3", ["-22"], "3"],
        ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        '""',
        "0",
        "1",
        "14",
        "2",
        ["8", "3", "0", "1", "100"],
        "1",
        ["4", "4", ["0"], "4"],
        ["3", "0", ["0"], "1", "1", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        ["4", "4", ["0"], "4"],
        "1",
        "1",
        "1",
        "0",
        "0",
        "95",
        "1e-1",
        "1e-1",
        "3e-2",
        ["4", "0", ["0"], "1", "1", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
        ["4", "0", ["0"], "0"],
        "2",
        "255",
        "0",
        "0",
        "00000000-0000-0000-0000-000000000000",
        "0",
        ["0", "0"],
        "0",
        ["0", "0", ["0", "1", "0", "1", "0"], "0", "0"],
        ["0", "0", ["0", "1", "0", "1", "0"], "0", "0"],
        "0",
        "0",
        "2",
        "-2",
        "1",
        "10",
        "1",
        "20",
        "0",
        "0",
        axis(),
        axis(),
        axis(),
        "0",
        "0",
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        "0",
        [["4", "0", ["1644953"], "0"], "3", "0", "0", "0", '""', ["1", "0"], ["1", "0"], ["1", "0"], "0"],
        "0",
        "0",
        "0.16875",
        "0",
        "0.83125",
        "0.063888888888888888888888889",
        "0",
        "0",
        "0.83125",
        "0",
        "0",
        "0.936111111111111111111111111",
        "1",
        "5",
        "1",
        "0",
        "0",
        "0.168032786885246",
        "0",
        "0.831967213114754",
        "0.0637583892617449",
        "0",
        "0",
        "0.831967213114754",
        "0",
        "0",
        "0.936241610738255",
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "14", ["4", "4", ["0"], "4"], ["4", "4", ["0"], "4"], "0", "0"],
        ["0", "14", ["4", "4", ["0"], "4"], ["4", "4", ["0"], "4"], "0", "0"],
        "0",
        "0",
        ["0", "0", "0", "0", "0"],
        ["0", "0", "0", "0"],
        "0",
        "",
        "60",
        axis(),
        ["0", "0", ["0", "1", "0", "1", "0"], "0", "0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
    ]
    if kind == "gantt":
        tail[9] = "0"
        tail[24] = "1"
        tail[46] = "3.888888888888888e-2"
        tail[52] = "9.611111111111111e-1"
        tail[111] = "0.038888888888888888888888889"
        tail[117] = "0.961111111111111111111111111"
        tail[123] = "0.168085106382979"
        tail[125] = "0.831914893617021"
        tail[126] = "0.0377733598409543"
        tail[129] = "0.831914893617021"
        tail[132] = "0.962226640159045"
    if kind == "dendrogram":
        tail[42] = "4.166666666666666e-2"
        tail[43] = "0"
        tail[45] = "0"
        tail[46] = "0"
        tail[47] = "1"
        tail[48] = "1"
        tail[49] = "0"
        tail[52] = "9.583333333333334e-1"
        tail[107] = "0.041666666666666666666666667"
        tail[108] = "0"
        tail[110] = "0"
        tail[111] = "0"
        tail[112] = "1"
        tail[113] = "1"
        tail[114] = "0"
        tail[117] = "0.958333333333333333333333333"
        tail[119] = "6"
        tail[122] = "0.0425055928411633"
        tail[123] = "0"
        tail[125] = "1"
        tail[126] = "0.0425055928411633"
        tail[129] = "0"
        tail[132] = "0.957494407158836"
    return tail


def apply_pivot_chart_default_presentation_settings(record: list[object], title_record: list[object]) -> None:
    ensure_list_size(record, 206, "0")
    record[106:206] = pivot_chart_default_presentation_settings(title_record)


def pivot_chart_default_presentation_settings(title_record: list[object]) -> list[object]:
    return [
        "3",
        "3",
        "6",
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
        "1",
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
        ["4", "0", ["0"], "1", "1", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
        ["4", "4", ["0"], "4"],
        "1",
        "1",
        "0",
        "4",
        "30",
        "1",
        "0",
        "1",
        "0",
        "0",
        "1",
        "0",
        "0",
        "1",
        "0",
        "1",
        "1",
        "2",
        ["1", "0"],
        "1",
        "0",
        "0",
        "1",
        ["4", "0", ["169"], "0"],
        "0",
        "0",
        ["1", "0", "0", "0"],
        "0",
        "180",
        "5",
        "1",
        "0",
        "4",
        ["4", "0", ["11119017"], "0"],
        "1",
        "0",
        "1",
        "0",
        "1",
        "0",
        "0",
        "1.666666666666667e-1",
        "0",
        "8.333333333333334e-1",
        "5.277777777777777e-2",
        "0",
        "0",
        "8.333333333333334e-1",
        "0",
        "0",
        "9.472222222222221e-1",
        "0",
        ["4", "3", ["-22"], "3"],
        ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        '""',
        "0",
        "0",
    ]


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
        after_dimensions = 2 + len(dimension_fields) * 11
        ensure_list_size(record, after_dimensions + 3, "0")
        record[after_dimensions : after_dimensions + 3] = [[quoted_atom("U")], [quoted_atom("U")], "0"]
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
                bool_attribute_record_from_xml(field, "enabled", default=True),
                "0",
                "0",
                field.get("value", "0") or "0",
            ]
        else:
            values = [
                localized_text_record(field.get("title", "")),
                bool_attribute_record_from_xml(field, "enabled", default=True),
                field.get("value", "0") or "0",
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
        record.append(quoted_atom(platform_multiline_text(point.text or "")))
    record.extend(pivot_chart_final_trailer(element))


def platform_multiline_text(value: str) -> str:
    return value.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")


def pivot_chart_final_trailer(element: ET.Element) -> list[object]:
    fields = sorted(
        element.findall("Fields/Field"),
        key=lambda node: (node.get("role", ""), int(node.get("order", "0") or "0")),
    )
    dimension_fields = [field for field in fields if field.get("role") == "dimension"]
    measure_fields = [field for field in fields if field.get("role") == "measure"]
    points = element.findall("SourceData/Point")
    return [
        *pivot_chart_final_trailer_head(),
        *[[color_record_from_xml_value(field.get("color", ""))] for field in measure_fields],
        *[pivot_chart_dimension_style_record(field) for field in dimension_fields],
        *pivot_chart_final_trailer_middle(),
        *[pivot_chart_point_label_record(point) for point in points],
        *pivot_chart_final_trailer_tail(),
    ]


def pivot_chart_dimension_style_record(field: ET.Element) -> list[object]:
    return [
        color_record_from_xml_value(field.get("color", "")),
        field.get("axis", "0") or "0",
        "0",
        "0",
        "0",
        '""',
        ["1", "0"],
        ["1", "0"],
        ["1", "0"],
        "0",
    ]


def pivot_chart_point_label_record(point: ET.Element) -> list[object]:
    return [["1", ["1", "1", [quoted_atom("#"), quoted_atom(platform_multiline_text(point.text or ""))]], "0"], "0"]


def pivot_chart_axis_layout_record() -> list[object]:
    return [
        "2",
        "0",
        "0",
        "2",
        ["1", "0"],
        [
            "1",
            "4",
            "0.5",
            "0.5",
            ["8", "3", "0", "1", "100"],
            ["4", "4", ["0"], "4"],
            ["4", "4", ["0"], "4"],
            "1",
            ["3", "0", ["0"], "0", "1", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
            ["4", "4", ["0"], "4"],
            "4",
            "2",
            "0",
        ],
        "2",
        "0",
        "0",
        ["4", "4", ["0"], "4"],
        ["8", "3", "0", "1", "100"],
        ["4", "4", ["0"], "4"],
        "2",
        ["1", "0"],
        "0",
        ["4", "4", ["0"], "4"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
    ]


def pivot_chart_final_trailer_head() -> list[object]:
    return [
        "14",
        "2",
        ["8", "3", "0", "1", "100"],
        "1",
        ["4", "4", ["0"], "4"],
        ["3", "0", ["0"], "1", "1", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
        ["4", "4", ["0"], "4"],
        "1",
        "1",
        "1",
        "0",
        "0",
        "95",
        "1e-1",
        "1e-1",
        "3e-2",
        ["4", "0", ["0"], "1", "1", "0", "e5cabe59-d992-4d31-8086-3116931aff81", "0"],
        ["4", "0", ["0"], "0"],
        "2",
        "255",
        "0",
        "8392496",
        "00000000-0000-0000-0000-000000000000",
        "0",
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        "0",
        ["0", "0", ["0", "1", "0", "1", "0"], "0", "0"],
        ["0", "0", ["0", "1", "0", "1", "0"], "0", "0"],
        "0",
        "0",
        "2",
        "-2",
        "1",
        "10",
        "1",
        "20",
        "0",
        "0",
        pivot_chart_axis_layout_record(),
        pivot_chart_axis_layout_record(),
        pivot_chart_axis_layout_record(),
        "0",
        "0",
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        "0",
    ]


def pivot_chart_final_trailer_middle() -> list[object]:
    return [
        "0",
        "0",
        "0.166666666666666666666666667",
        "0",
        "0.833333333333333333333333333",
        "0.052777777777777777777777778",
        "0",
        "0",
        "0.833333333333333333333333333",
        "0",
        "0",
        "0.947222222222222222222222222",
        "1",
        "5",
        "1",
        "0",
        "0",
        "0.167330677290837",
        "0",
        "0.832669322709163",
        "0.0535211267605633",
        "0",
        "0",
        "0.832669322709163",
        "0",
        "0",
        "0.946478873239436",
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "0"],
        ["0", "14", ["4", "4", ["0"], "4"], ["4", "4", ["0"], "4"], "0", "0"],
        ["0", "14", ["4", "4", ["0"], "4"], ["4", "4", ["0"], "4"], "0", "0"],
        "0",
        "0",
        ["0", "0", "0", "0", "0"],
        ["0", "0", "0", "0"],
        "0",
    ]


def pivot_chart_final_trailer_tail() -> list[object]:
    return [
        "",
        "60",
        pivot_chart_axis_layout_record(),
        ["0", "0", ["0", "1", "0", "1", "0"], "0", "0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
    ]


def ensure_list_size(record: list[object], size: int, fill: object) -> None:
    while len(record) < size:
        record.append(copy.deepcopy(fill))


def color_record_from_xml_value(value: str | None) -> list[object]:
    return ["4", "0", [value or "0"], "0"]


def bool_attribute_record_from_xml(element: ET.Element, name: str, *, default: bool) -> str:
    value = (element.get(name) or "").strip().lower()
    if value in {"true", "1"}:
        return "1"
    if value in {"false", "0"}:
        return "0"
    return "1" if default else "0"


def html_document_field_control_info(actions: list[object]) -> list[object]:
    return [
        "5",
        "0",
        [str(len(actions)), *actions] if actions else ["0"],
        ["3", "3", ["-22"]],
        ["3", "1", ["-18"], "0", "0", "0"],
        "1",
        "0",
    ]


def progress_bar_control_info(element: ET.Element) -> list[object]:
    return [
        "0",
        [
            progress_bar_base_info_record(element),
            element.findtext("Orientation") or "3",
            element.findtext("MinimumValue") or "0",
            element.findtext("MaximumValue") or "100",
            element.findtext("Step") or "1",
            element.findtext("BigStep") or "1",
            bool_record_from_xml(element, "ShowPercent", default=False),
            element.findtext("DisplayStyle") or "2",
        ],
    ]


def progress_bar_base_info_record(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[0] = "19"
    base[2] = ["4", "4", ["0"], "4"]
    base[3] = ["4", "4", ["0"], "4"]
    base[5] = "0"
    base[6] = ["4", "3", ["-22"], "3"]
    base[7] = ["4", "4", ["0"], "4"]
    base[8] = ["4", "4", ["0"], "4"]
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    return base


def track_bar_control_info(element: ET.Element, actions: list[object]) -> list[object]:
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
        ["1", *actions] if actions else ["0"],
    ]


def calendar_field_control_info(element: ET.Element, actions: list[object]) -> list[object]:
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
        ["1", *actions] if actions else ["0"],
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
    return [
        "19",
        element.findtext("Output") or "1",
        ["4", "3", ["-10"], "3"],
        ["4", "4", ["0"], "4"],
        ["8", "3", "0", "1", "100"],
        "0",
        ["4", "3", ["-22"], "3"],
        ["4", "4", ["0"], "4"],
        ["4", "4", ["0"], "4"],
        ["4", "3", ["-7"], "3"],
        ["4", "3", ["-21"], "3"],
        ["3", "0", ["0"], "0", visible_record_from_xml(element), "3", "48312c09-257f-4b29-b280-284dd89efc1e"],
        tooltip_record_from_xml(element),
        "0",
        "0",
        element.findtext("Scale") or "100",
        "0",
        "0",
        "0",
        "0",
        ["4", "4", ["0"], "4"],
    ]


def geographical_schema_settings_record(element: ET.Element) -> list[object]:
    scale_support = element.findtext("ScaleSupport") or "2"
    return [
        "2",
        scale_support,
        [
            ["1", "0", "0", "0"],
            ["0", "0", "0", "0", "0", []],
            [
                "1",
                ["1", "0"],
                ["8", "2", "0", ["-20"], "1", "100"],
                ["4", "3", ["-3"], "3"],
                "1",
                ["3", "0", ["0"], "0", "1", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
                ["4", "3", ["-22"], "3"],
                "1",
                ["4", "3", ["-10"], "3"],
                "0",
                "0",
                "0",
                "95",
            ],
            [
                "1",
                ["8", "2", "0", ["-20"], "1", "100"],
                ["4", "3", ["-3"], "3"],
                ["3", "0", ["0"], "0", "1", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
                ["4", "3", ["-22"], "3"],
                "1",
                ["4", "3", ["-10"], "3"],
                "0",
                [],
                "75",
                "0",
                "5",
                "0",
                "1",
            ],
            [["3", "0", ["0"], "0", "1", "0", "48312c09-257f-4b29-b280-284dd89efc1e"], ["4", "3", ["-22"], "3"], "1", ["4", "3", ["-10"], "3"], "0", "25", "5", "0"],
            ["0", []],
            "0",
            "1",
            "0",
            "0",
            "0",
            "0",
        ],
        ["0"],
        "0",
    ]


def graphical_schema_field_control_info(element: ET.Element, actions: list[object]) -> list[object]:
    return [
        graphical_schema_base_info_record(element),
        "5",
        [
            [
                "5",
                [
                    [
                        "1",
                        ["4", "3", ["-10"], "3"],
                        "1",
                        "20",
                        "20",
                        "3",
                        "6",
                        "6",
                        [quoted_atom("N"), "10"],
                        "7",
                        [quoted_atom("N"), "10"],
                        "8",
                        [quoted_atom("N"), "10"],
                        "9",
                        [quoted_atom("N"), "10"],
                        "13",
                        [quoted_atom("N"), "0"],
                        "16",
                        [quoted_atom("N"), "0"],
                    ]
                ],
                "0",
                "0",
            ]
        ],
        ["1", *actions] if actions else ["0"],
        "1" if actions else "0",
        "0",
    ]


def graphical_schema_base_info_record(element: ET.Element) -> list[object]:
    base = progress_bar_base_info_record(element)
    base[2] = ["4", "3", ["-10"], "3"]
    return base


def list_box_control_info(element: ET.Element, actions: list[object]) -> list[object]:
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
        ["1", *actions] if actions else ["0"],
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
        "2",
        "2",
        "1",
        "2",
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
        "117644289",
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
        "0",
        "0",
        "1",
        [str(len(columns)), *[table_column_record(column, index) for index, column in enumerate(columns)]],
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
    name = column.get("name") or title
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
        quoted_atom(name),
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
    base[2] = color_record_from_xml(element, "TextColor")
    base[3] = color_record_from_xml(element, "BackColor")
    base[4] = font_record_from_xml(element.find("Font") if element is not None else None)
    base[6] = color_record_from_xml(element, "BorderColor")
    base[12] = tooltip_record_from_xml(element)
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
    control_template: dict[str, object] | None = None,
    object_id: str = "0",
    control_name: str = "",
    parent_size: tuple[str, str] | None = None,
) -> list[object]:
    template_geometry = (control_template or {}).get("geometry")
    if isinstance(template_geometry, list):
        return geometry_stream_from_template(template_geometry, position)
    left = position.get("left", "0") if position is not None else "0"
    top = position.get("top", "0") if position is not None else "0"
    right = position.get("right", "0") if position is not None else "0"
    bottom = position.get("bottom", "0") if position is not None else "0"
    layout_group = position.get("layoutGroup") if position is not None else None
    layout_order = position.get("layoutOrder") if position is not None else None
    bindings: list[object] = ["0"] * 6
    dimensions: list[object] = ["0"] * 4
    has_explicit_geometry_bindings = False
    if position is not None:
        binding_container = position.find("Bindings")
        if binding_container is not None:
            has_explicit_geometry_bindings = True
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
    if page_index is not None and page_order is not None and not has_explicit_geometry_bindings:
        bindings, dimensions = default_paged_geometry_bindings(object_id, right, bottom, left, top)
        if control_type in SHORT_POSITION_PARENT_ANCHORED_TYPES:
            bindings, dimensions = default_parent_anchored_geometry_bindings(
                object_id,
                right,
                bottom,
                left,
                top,
                parent_size,
                fixed_height=control_type in SHORT_POSITION_FIXED_HEIGHT_TYPES,
                fixed_width=False,
            )
            if control_type == "Splitter":
                dimensions = ["1", ["0", object_id, "0"], "0", "0"]
    if compact_scalar_geometry(position, bindings, dimensions):
        compact_order = str(page_order) if page_order is not None else "3"
        return ["3", left, top, right, bottom, compact_order, *bindings, "0", *dimensions[:2]]
    counted_geometry = counted_dimension_geometry_from_xml(position, left, top, right, bottom, bindings)
    if counted_geometry is not None:
        return counted_geometry
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
        if control_type == "CommandBar" and control_name == "КоманднаяПанель1":
            trailer = ["0", "0", "0", "0", "1", "1", "0"]
        if control_type == "CommandBar" and dimensions[2] != "0":
            trailer = ["0", "0", "0", "0", "1", "3", "1", "1"]
        dimension_flag = "1" if any(dimension != "0" for dimension in dimensions) else "0"
        return ["8", left, top, right, bottom, "1", *bindings, dimension_flag, *dimensions, *trailer]
    if control_type == "Table":
        group_tail = layout_group_tail(
            layout_group,
            layout_order,
            str(page_order) if page_order is not None else "0",
            str(page_index) if page_index is not None else "0",
        )
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
            group_tail[0],
            group_tail[1],
            "1",
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
    if control_type in {"GeographicalSchemaField", "GraphicalSchemaField"} and data_slot:
        group_tail = layout_group_tail(layout_group, layout_order, data_slot, "0")
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
            group_tail[0],
            group_tail[1],
            "1",
            "0",
            "0",
        ]
    if data_slot:
        group_tail = layout_group_tail(layout_group, layout_order, data_slot, "1")
        height_dimension_flag = "1" if dimensions[0] != "0" else "0"
        width_dimension_flag = "1" if dimensions[3] != "0" else "0"
        height_dimension_part = [dimensions[0]] if height_dimension_flag == "1" else []
        width_dimension_part = [dimensions[3]] if width_dimension_flag == "1" else []
        return [
            "8",
            left,
            top,
            right,
            bottom,
            "1",
            *bindings,
            height_dimension_flag,
            *height_dimension_part,
            "0",
            width_dimension_flag,
            *width_dimension_part,
            "0",
            "0",
            "0",
            *group_tail[:3],
            "0",
            "0",
        ]
    if control_type in {"Chart", "GanttChart", "Dendrogram", "GeographicalSchemaField", "GraphicalSchemaField"}:
        group_tail = layout_group_tail(
            layout_group,
            layout_order,
            str(page_order) if page_order is not None else "0",
            "0",
        )
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
            group_tail[0],
            group_tail[1],
            "1",
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


def counted_dimension_geometry_from_xml(
    position: ET.Element | None,
    left: str,
    top: str,
    right: str,
    bottom: str,
    bindings: list[object],
) -> list[object] | None:
    if position is None or position.get("dimensionProfile") != "counted":
        return None
    binding_container = position.find("Bindings")
    if binding_container is None:
        return None
    primary: list[object] = []
    secondary: list[object] = []
    for binding in binding_container.findall("DimensionBinding"):
        section = binding.get("section") or "primary"
        if section == "secondary":
            secondary.append(dimension_binding_to_raw(binding))
        elif section == "primary":
            primary.append(dimension_binding_to_raw(binding))
    if not primary:
        return None
    tail = [value for value in (position.get("layoutTail") or "").split(" ") if value != ""]
    return [
        "8",
        left,
        top,
        right,
        bottom,
        "1",
        *bindings,
        position.get("primaryDimensionMarker", "0"),
        str(len(primary)),
        *primary,
        position.get("secondaryDimensionMarker", "0"),
        str(len(secondary)),
        *secondary,
        *tail,
    ]


def compact_scalar_geometry(
    position: ET.Element | None,
    bindings: list[object],
    dimensions: list[object],
) -> bool:
    if position is None or position.get("layoutGroup") is not None or position.get("layoutOrder") is not None:
        return False
    binding_container = position.find("Bindings")
    if binding_container is None:
        return False
    dimension_names = {binding.get("dimension", "") for binding in binding_container.findall("DimensionBinding")}
    if not dimension_names or not dimension_names <= {"height", "minHeight"}:
        return False
    return all(not isinstance(binding, list) for binding in bindings) and all(
        not isinstance(dimension, list) for dimension in dimensions[:2]
    )


def default_paged_geometry_bindings(
    object_id: str,
    right: str,
    bottom: str,
    left: str = "0",
    top: str = "0",
) -> tuple[list[object], list[object]]:
    width = geometry_delta(right, left)
    height = geometry_delta(bottom, top)
    empty_anchor = ["2", "-1", "6", "0"]
    return (
        [
            ["0", empty_anchor, empty_anchor],
            ["0", ["2", object_id, "0", height], empty_anchor],
            ["0", empty_anchor, empty_anchor],
            ["0", ["2", object_id, "2", width], empty_anchor],
            ["0", empty_anchor, empty_anchor],
            ["0", empty_anchor, empty_anchor],
        ],
        [["0", object_id, "1"], "0", "1", ["0", object_id, "3"]],
    )


def default_parent_anchored_geometry_bindings(
    object_id: str,
    right: str,
    bottom: str,
    left: str,
    top: str,
    parent_size: tuple[str, str] | None,
    *,
    fixed_height: bool,
    fixed_width: bool,
) -> tuple[list[object], list[object]]:
    width = geometry_delta(right, left)
    height = geometry_delta(bottom, top)
    parent_width, parent_height = parent_size or ("0", "0")
    empty_anchor = ["2", "-1", "6", "0"]
    bottom_anchor = ["2", object_id, "0", height] if fixed_height else ["2", "0", "1", geometry_delta(bottom, parent_height)]
    right_anchor = ["2", object_id, "2", width] if fixed_width else ["2", "0", "3", geometry_delta(right, parent_width)]
    dimensions: list[object] = []
    dimensions.append(["0", object_id, "1"] if fixed_height else "0")
    dimensions.append("0")
    dimensions.append("1" if fixed_width else "0")
    dimensions.append(["0", object_id, "3"] if fixed_width else "0")
    return (
        [
            ["0", empty_anchor, empty_anchor],
            ["0", bottom_anchor, empty_anchor],
            ["0", empty_anchor, empty_anchor],
            ["0", right_anchor, empty_anchor],
            ["0", empty_anchor, empty_anchor],
            ["0", empty_anchor, empty_anchor],
        ],
        dimensions,
    )


def position_size(position: ET.Element | None) -> tuple[str, str] | None:
    if position is None:
        return None
    width = position.get("width")
    height = position.get("height")
    if not width:
        width = geometry_delta(position.get("right", "0"), position.get("left", "0"))
    if not height:
        height = geometry_delta(position.get("bottom", "0"), position.get("top", "0"))
    return width, height


def geometry_delta(end: str, start: str) -> str:
    try:
        return str(int(end) - int(start))
    except ValueError:
        return "0"


def geometry_stream_from_template(template_geometry: list[object], position: ET.Element | None) -> list[object]:
    result = copy.deepcopy(template_geometry)
    if len(result) < 5 or position is None:
        return result
    for index, key in enumerate(("left", "top", "right", "bottom"), start=1):
        value = position.get(key)
        if value is not None:
            result[index] = value
    binding_container = position.find("Bindings")
    if binding_container is None:
        return result
    for binding in binding_container.findall("Binding"):
        slot = binding.get("slot")
        if not slot and binding.get("coordinate"):
            mapped = BINDING_COORDINATE_SLOT.get(binding.get("coordinate", ""))
            slot = str(mapped) if mapped is not None else None
        if not slot:
            continue
        try:
            index = int(slot) - 1
        except ValueError:
            continue
        target = 6 + index
        if 0 <= index < 6 and target < len(result):
            result[target] = binding_to_raw(binding)
    for binding in binding_container.findall("DimensionBinding"):
        slot = binding.get("slot")
        if not slot and binding.get("dimension"):
            mapped = DIMENSION_NAME_SLOT.get(binding.get("dimension", ""))
            slot = str(mapped) if mapped is not None else None
        if not slot:
            continue
        try:
            index = int(slot) - 1
        except ValueError:
            continue
        target = 13 + index
        if 0 <= index < 4 and target < len(result):
            result[target] = dimension_binding_to_raw(binding)
    return result


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
    if node.get("relation") == "rawList":
        return [raw_value_from_xml(child) for child in node.findall("Value")]
    if "value" in node.attrib:
        return node.get("value", "")
    return [
        anchor_kind_code(node.get("relation") or node.get("kindName", "targetEdgeOffset")),
        anchor_target_id(node),
        anchor_edge_code(node.get("side", "none")),
        node.get("offset", "0"),
    ]


def raw_value_from_xml(node: ET.Element) -> object:
    if node.get("kind") == "list":
        return [raw_value_from_xml(child) for child in node.findall("Value")]
    return node.text or ""


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
