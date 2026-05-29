"""Internal ordinary-form ListOutStream codec.

This module is the boundary between public object-model ``Form.xml`` and the
platform list-stream payload stored as the ``form`` file inside ``Form.bin``.
Public XML must never expose this stream directly.
"""

from __future__ import annotations

import base64
import copy
import json
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

    root_layout = form_serialization_profile_from_xml(root)
    stream = ordinary_form_stream(
        title or "Main",
        attributes,
        controls,
        events_from_xml(root),
        form_size_from_xml(root, root_layout),
        root_layout=root_layout,
        object_info=form_object_info_from_xml(root),
        serialization_counter=form_serialization_counter_from_xml(root),
    )
    return ("\ufeff" + dumps_list_out_stream(stream)).encode("utf-8")


FormRootLayout = dict[str, object]


def ordinary_form_stream(
    title: str,
    attributes: list[object],
    controls: list[object],
    events: list[object],
    form_size: tuple[str, str, bool],
    root_layout: FormRootLayout | None = None,
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
        action_records(events),
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
    root_layout: FormRootLayout | None = None,
    serialization_counter: str = "",
) -> list[object]:
    width, height, explicit_size = form_size
    compact_root = root_layout is not None and root_layout.get("recordKind") == "16" and root_layout.get("rootPanelInfoProfile") == "21"
    root_panel_info_template = (root_layout or {}).get("rootPanelInfo")
    if isinstance(root_panel_info_template, list):
        root_panel_info_record = copy.deepcopy(root_panel_info_template)
    else:
        root_panel_info_record = (
            compact_root_panel_info(title, width, height) if compact_root else root_panel_info(title, width, height, controls, root_layout)
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
            str(root_layout.get("slot5", "1")),
            str(root_layout.get("slot6", "1")),
            str(root_layout.get("slot7", "1")),
            str(root_layout.get("slot8", "4")),
            str(root_layout.get("slot9", "4")),
            str(root_layout.get("slot10", "38")),
        ]
    record = [
        str((root_layout or {}).get("recordKind", "16")),
        [
            localized_text_record(title),
            str((root_layout or {}).get("titleMarker", "52")),
            str((root_layout or {}).get("titleScope", "4294967295")),
        ],
        root_panel,
        width,
        height,
        str((root_layout or {}).get("slot5", "1")),
        str((root_layout or {}).get("slot6", "0")),
        str((root_layout or {}).get("slot7", "1")),
        str((root_layout or {}).get("slot8", "4")),
        str((root_layout or {}).get("slot9", "4")),
        str((root_layout or {}).get("slot10", "6")),
    ]
    if explicit_size:
        record[0] = "18"
        record[1][1] = str((root_layout or {}).get("titleMarker", "4" if controls else "41"))
        record[1][2] = str((root_layout or {}).get("titleScope", "4294967295" if controls else "3"))
        record[-1] = serialization_counter or str((root_layout or {}).get("slot10", "3"))
        record.extend([width, height, "96"])
    return record


def form_serialization_profile_from_xml(root: ET.Element) -> FormRootLayout | None:
    serialization = root.find("SerializationProfile")
    if serialization is None:
        return None
    result: FormRootLayout = {}
    root_record = serialization.find("RootRecord")
    if root_record is not None:
        for key in ("recordKind", "titleMarker", "titleScope", "slot5", "slot6", "slot7", "slot8", "slot9", "slot10"):
            value = root_record.get(key)
            if value is not None and value != "":
                result[key] = value
    root_panel = serialization.find("RootPanel")
    if root_panel is not None:
        base_style = root_panel.find("BaseStyle")
        if base_style is not None:
            result["rootPanelBaseStyle"] = base_style
        if root_panel.get("pageCapacity"):
            result["rootPanelPageCapacity"] = root_panel.get("pageCapacity", "")
        if root_panel.get("currentPageIndex"):
            result["rootPanelCurrentPageIndex"] = root_panel.get("currentPageIndex", "")
        page_states: list[dict[str, str]] = []
        for page_state in root_panel.findall("PageState"):
            descriptor: dict[str, str] = {}
            if page_state.get("name"):
                descriptor["name"] = page_state.get("name", "")
            title = get_multilang_text(page_state, "Title")
            if title:
                descriptor["title"] = title
            if page_state.get("styleMode"):
                descriptor["styleMode"] = page_state.get("styleMode", "")
            if descriptor:
                page_states.append(descriptor)
        if page_states:
            result["rootPageStates"] = page_states
            result["rootPageName"] = page_states[0].get("name", "")
            result["rootPageTitle"] = page_states[0].get("title", "")
            result["rootPageStyleMode"] = page_states[0].get("styleMode", "")
        page_layouts = [
            {key: page_layout.get(key, "") for key in ("page", "left", "top", "width", "height", "horizontalMode", "verticalMode")}
            for page_layout in root_panel.findall("PageLayout")
        ]
        if page_layouts:
            result["rootPageLayouts"] = page_layouts
            result["rootPageLayout"] = page_layouts[0]
        if root_panel.get("pageLayoutHeader"):
            result["rootPageLayoutHeader"] = [value for value in root_panel.get("pageLayoutHeader", "").split(" ") if value != ""]
        if root_panel.get("dependencyTail"):
            result["rootPanelDependencyTail"] = [value for value in root_panel.get("dependencyTail", "").split(" ") if value != ""]
        if root_panel.get("pageStateFlag"):
            result["rootPanelPageStateFlag"] = root_panel.get("pageStateFlag")
        if root_panel.get("postLayoutTailBeforeColor"):
            result["rootPanelPostLayoutTailBeforeColor"] = [
                value for value in root_panel.get("postLayoutTailBeforeColor", "").split(" ") if value != ""
            ]
        if root_panel.get("postLayoutTailAfterColor"):
            result["rootPanelPostLayoutTailAfterColor"] = [
                value for value in root_panel.get("postLayoutTailAfterColor", "").split(" ") if value != ""
            ]
        dependencies = form_root_panel_dependency_profile_from_xml(root_panel)
        if dependencies is not None:
            result["rootPanelDependencies"] = dependencies
    form_object = serialization.find("FormObject")
    if form_object is not None:
        result["formObject"] = form_object
    return result or None


def form_root_panel_dependency_profile_from_xml(root_panel: ET.Element) -> list[object] | None:
    groups = sorted(root_panel.findall("DependencyGroup"), key=lambda node: int(node.get("order", "0") or "0"))
    if not groups:
        return None
    result: list[object] = []
    for group in groups:
        prefix = [value for value in (group.get("prefix") or "").split(" ") if value != ""]
        result.extend(prefix)
        header = [value for value in (group.get("header") or "").split(" ") if value != ""]
        if header:
            result.append(header)
        records: list[list[object]] = []
        for dependency in group.findall("Dependency"):
            target_id = dependency.get("targetId", "0")
            dimension = dependency.get("dimension", "top")
            dimension_code = PANEL_PROFILE_DIMENSION_CODES.get(dimension)
            if dimension_code is None and dimension.startswith("dimension"):
                dimension_code = dimension.removeprefix("dimension")
            records.append(["0", target_id, dimension_code or "0"])
        result.extend([str(len(records)), *records])
    return result


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


def root_panel_info(
    title: str,
    form_width: str,
    form_height: str,
    controls: list[object] | None = None,
    root_layout: FormRootLayout | None = None,
) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["FormRootPanel"]
    margin_left = "8"
    margin_top = "33"
    page_name = str((root_layout or {}).get("rootPageName", "Страница1"))
    page_title_text = str((root_layout or {}).get("rootPageTitle", "Страница1"))
    page_style_mode = str((root_layout or {}).get("rootPageStyleMode", "0"))
    page_title = localized_text_record(page_title_text)
    page_layout = (root_layout or {}).get("rootPageLayout")
    dependency_profile = (root_layout or {}).get("rootPanelDependencies")
    dependency_tail = [] if isinstance(dependency_profile, list) else ["0", "0"]
    current_page_index = str((root_layout or {}).get("rootPanelCurrentPageIndex", "1"))
    max_right, max_bottom = controls_extent(controls or [])
    if max_right and max_bottom:
        page_width = str(max_right + 8)
        page_height = str(max_bottom)
        right_offset = str(max(int_atom(form_width) - max_right - 24, 0))
        if isinstance(page_layout, dict):
            margin_left = str(page_layout.get("left") or margin_left)
            margin_top = str(page_layout.get("top") or margin_top)
            page_width = str(page_layout.get("width") or page_width)
            page_height = str(page_layout.get("height") or page_height)
            right_offset = str(page_layout.get("horizontalMode") or right_offset)
            bottom_offset = str(page_layout.get("verticalMode") or "0")
        else:
            bottom_offset = "0"
        return root_panel_info_from_control_extent(
            page_width,
            page_height,
            right_offset,
            page_title=page_title,
            page_name=page_name,
            page_style_mode=page_style_mode,
            dependency_profile=dependency_profile if isinstance(dependency_profile, list) else None,
            current_page_index=current_page_index,
            margin_left=margin_left,
            margin_top=margin_top,
            bottom_offset=bottom_offset,
            root_layout=root_layout,
        )
    page_width = panel_extent_value(form_width, 8)
    page_height = panel_extent_value(form_height, 33)
    if isinstance(page_layout, dict):
        margin_left = str(page_layout.get("left") or margin_left)
        margin_top = str(page_layout.get("top") or margin_top)
        page_width = str(page_layout.get("width") or page_width)
        page_height = str(page_layout.get("height") or page_height)
        right_offset = str(page_layout.get("horizontalMode") or margin_left)
        bottom_offset = str(page_layout.get("verticalMode") or margin_top)
    else:
        right_offset = margin_left
        bottom_offset = margin_top
    position_records = [
        ["2", margin_left, "1", "1", "1", "0", "0", "0", "0"],
        ["2", margin_top, "0", "1", "2", "0", "0", "0", "0"],
        ["2", page_width, "1", "1", "3", "0", "0", right_offset, "0"],
        ["2", page_height, "0", "1", "4", "0", "0", bottom_offset, "0"],
    ]
    return [
        descriptor.info_kind,
        [
            root_panel_base_info_record(root_layout),
            "26",
            "0",
            *(dependency_profile if isinstance(dependency_profile, list) else [
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
            ]),
            *dependency_tail,
            page_style_group_record("1"),
            str((root_layout or {}).get("rootPanelPageStateFlag", "0")),
            current_page_index,
            ["1", "1", ["6", page_title, root_page_state_style_group_record(page_style_mode), "-1", "1", "1", quoted_atom(page_name), "1", default_color_record(), default_color_record(), ["8", "3", "0", "1", "100"], "1"]],
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


def root_panel_info_from_control_extent(
    page_width: str,
    page_height: str,
    right_offset: str,
    *,
    page_title: list[object] | None = None,
    page_name: str = "Страница1",
    page_style_mode: str = "0",
    dependency_profile: list[object] | None = None,
    current_page_index: str = "1",
    margin_left: str = "8",
    margin_top: str = "33",
    bottom_offset: str = "0",
    root_layout: FormRootLayout | None = None,
) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["FormRootPanel"]
    page_title = page_title or localized_text_record("Страница1")
    page_states = root_page_state_records(root_layout, page_title, page_name, page_style_mode)
    position_records = root_page_layout_records(
        root_layout,
        margin_left,
        margin_top,
        page_width,
        page_height,
        right_offset,
        bottom_offset,
    )
    layout_header = root_page_layout_header(root_layout)
    body: list[object] = [
        root_panel_base_info_record(root_layout),
        "26",
    ]
    if dependency_profile is None:
        body.append(str((root_layout or {}).get("rootPanelPageCapacity", "0")))
    body.extend(
        dependency_profile or [
                "1",
                ["0", "1", "1"],
                "1",
                ["0", "2", "2"],
                "3",
                ["0", "1", "3"],
                ["0", "2", "3"],
                ["0", "4", "3"],
            ]
    )
    dependency_tail = (root_layout or {}).get("rootPanelDependencyTail")
    body.extend(
        [
            *(dependency_tail if isinstance(dependency_tail, list) else ([] if dependency_profile is not None else ["0", "0"])),
            page_style_group_record("1"),
            str((root_layout or {}).get("rootPanelPageStateFlag", "0")),
            current_page_index,
            ["1", str(len(page_states)), *page_states],
            *layout_header,
            str(len(position_records)),
            *position_records,
            *root_panel_post_layout_tail_before_color(root_layout),
            default_color_record(),
            *root_panel_post_layout_tail_after_color(root_layout),
        ]
    )
    return [
        descriptor.info_kind,
        body,
        ["0"],
    ]


def root_page_state_records(
    root_layout: FormRootLayout | None,
    default_title: list[object],
    default_name: str,
    default_style_mode: str,
) -> list[list[object]]:
    states = (root_layout or {}).get("rootPageStates")
    if not isinstance(states, list) or not states:
        states = [{"name": default_name, "title": default_name, "styleMode": default_style_mode}]
    result: list[list[object]] = []
    for state in states:
        if not isinstance(state, dict):
            continue
        name = str(state.get("name", default_name))
        title = localized_text_record(str(state.get("title", name)))
        style_mode = str(state.get("styleMode", default_style_mode))
        result.append(
            [
                "6",
                title,
                root_page_state_style_group_record(style_mode),
                "-1",
                "1",
                "1",
                quoted_atom(name),
                "1",
                default_color_record(),
                default_color_record(),
                ["8", "3", "0", "1", "100"],
                "1",
            ]
        )
    return result or [["6", default_title, root_page_state_style_group_record(default_style_mode), "-1", "1", "1", quoted_atom(default_name), "1", default_color_record(), default_color_record(), ["8", "3", "0", "1", "100"], "1"]]


def root_page_layout_header(root_layout: FormRootLayout | None) -> list[str]:
    value = (root_layout or {}).get("rootPageLayoutHeader")
    if isinstance(value, list) and value:
        return [str(item) for item in value]
    return ["1", "1", "0"]


def root_page_layout_records(
    root_layout: FormRootLayout | None,
    margin_left: str,
    margin_top: str,
    page_width: str,
    page_height: str,
    right_offset: str,
    bottom_offset: str,
) -> list[list[object]]:
    layouts = (root_layout or {}).get("rootPageLayouts")
    if not isinstance(layouts, list) or not layouts:
        layouts = [
            {
                "page": "0",
                "left": margin_left,
                "top": margin_top,
                "width": page_width,
                "height": page_height,
                "horizontalMode": right_offset,
                "verticalMode": bottom_offset,
            }
        ]
    records: list[list[object]] = []
    for layout in layouts:
        if not isinstance(layout, dict):
            continue
        page = str(layout.get("page", "0"))
        left = str(layout.get("left", margin_left))
        top = str(layout.get("top", margin_top))
        width = str(layout.get("width", page_width))
        height = str(layout.get("height", page_height))
        horizontal_mode = str(layout.get("horizontalMode", right_offset))
        vertical_mode = str(layout.get("verticalMode", bottom_offset))
        records.extend(
            [
                ["2", left, "1", "1", "1", page, "0", "0", "0"],
                ["2", top, "0", "1", "2", page, "0", "0", "0"],
                ["2", width, "1", "1", "3", page, "0", horizontal_mode, "0"],
                ["2", height, "0", "1", "4", page, "0", vertical_mode, "0"],
            ]
        )
    return records


def root_panel_post_layout_tail_before_color(root_layout: FormRootLayout | None) -> list[str]:
    value = (root_layout or {}).get("rootPanelPostLayoutTailBeforeColor")
    if isinstance(value, list) and value:
        return [str(item) for item in value]
    return ["0", "4294967295", "5", "64", "0"]


def root_panel_post_layout_tail_after_color(root_layout: FormRootLayout | None) -> list[str]:
    value = (root_layout or {}).get("rootPanelPostLayoutTailAfterColor")
    if isinstance(value, list) and value:
        return [str(item) for item in value]
    return ["0", "0", "57", "0", "0"]


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


def root_page_state_style_group_record(style_mode: str) -> list[object]:
    record = page_style_group_record("0")
    record[6] = style_mode
    return record


def int_atom(value: object) -> int:
    try:
        return int(clean_atom(value))
    except (TypeError, ValueError):
        return 0


def panel_base_info_record() -> list[object]:
    return base_info_record_from_xml(None)


def root_panel_base_info_record(root_layout: FormRootLayout | None = None) -> list[object]:
    record = [
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
    base_style = (root_layout or {}).get("rootPanelBaseStyle")
    if isinstance(base_style, ET.Element):
        if base_style.find("TextColor") is not None:
            record[2] = color_record_from_xml(base_style, "TextColor")
        if base_style.find("BackColor") is not None:
            record[3] = color_record_from_xml(base_style, "BackColor")
        if base_style.find("Font") is not None:
            record[4] = font_record_from_xml(base_style.find("Font"))
        if base_style.find("BorderColor") is not None:
            record[6] = color_record_from_xml(base_style, "BorderColor")
    return record


def regular_panel_base_info_record() -> list[object]:
    record = root_panel_base_info_record()
    record[17] = "1"
    return record


def panel_base_info_record_for_serialization(serialization: ET.Element | None) -> list[object]:
    record = regular_panel_base_info_record()
    if serialization is not None:
        record[6] = default_color_record()
        record[17] = "2"
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
    if node.get("recordKind") == "4":
        return ["4", node.get("recordSubKind") or "3", [value], node.get("tailKind") or "3"]
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
    result.append(font.get("delta") or (deltas if deltas else ["0"]))
    for value in font.findall("Value"):
        result.append(value.get("atom") or value.text or "0")
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
    slots_by_name: dict[str, str] = {}
    for attribute in attributes:
        if not isinstance(attribute, list) or len(attribute) < 5 or not isinstance(attribute[0], list) or not attribute[0]:
            continue
        slots_by_name[str(attribute[4])] = str(attribute[0][0])
    links: list[object] = []
    for control in iter_control_records(controls):
        if not isinstance(control, list) or len(control) < 5:
            continue
        metadata = control[-2]
        if not isinstance(metadata, list) or len(metadata) < 2 or metadata[0] != "14":
            continue
        slot = slots_by_name.get(str(metadata[1]))
        if slot:
            links.append([str(control[1]), ["1", [slot]]])
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
    if record_flag is None and (attribute.get("controlData") or "").strip().lower() in {"false", "0"}:
        record_flag = "0"
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


def form_object_info_from_xml(root: ET.Element) -> list[object] | None:
    serialization = root.find("SerializationProfile")
    form_object = serialization.find("FormObject") if serialization is not None else None
    if form_object is None:
        return None
    uuid = form_object.get("uuid") or "00000000-0000-0000-0000-000000000000"
    kind = form_object.get("kind") or "0"
    state_kind = form_object.get("stateKind")
    if state_kind is None:
        return [uuid, kind]
    return [
        uuid,
        kind,
        [
            state_kind,
            form_object.get("stateMode") or "0",
            ["0", "0"],
            ["0"],
            form_object.get("stateFlag") or "0",
        ],
    ]


def events_from_xml(root: ET.Element, style_profile: str = "extended") -> list[object]:
    result: list[object] = []
    for event in root.findall("./Events/Event"):
        name = event.get("name")
        uuid = event.get("uuid")
        if not name or not uuid:
            continue
        title = event.get("title") or name
        result.append([event.get("id") or "70001", uuid, ["3", quoted_atom(name), event_descriptor(name, title, style_profile=style_profile)]])
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


def form_size_from_xml(root: ET.Element, root_layout: FormRootLayout | None = None) -> tuple[str, str, bool]:
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
    command_bar_graph = command_bar_action_graph_from_xml(element) if control_type == "CommandBar" else {}
    if command_bar_graph.get("metadataScope"):
        metadata_scope = command_bar_graph["metadataScope"]
    elif control_type == "CommandBar" and name not in {"КоманднаяПанель1", "КоманднаяПанель3", "ОсновныеДействияФормы"}:
        metadata_scope = "8"
    elif control_type == "CommandBar" and name == "КоманднаяПанель3":
        metadata_scope = CONTROL_METADATA_SCOPE["default"]
    else:
        metadata_scope = CONTROL_METADATA_SCOPE.get(control_type, CONTROL_METADATA_SCOPE["default"])
    template_metadata = (control_template or {}).get("metadata")
    metadata = copy.deepcopy(template_metadata) if isinstance(template_metadata, list) else ["14", quoted_atom(metadata_name), metadata_scope, "0", "0", "0"]
    if control_type == "RadioButton":
        metadata[5] = bool_record_from_xml(element, "FirstInGroup", default=False)
    if control_type == "Label" and bool_record_from_xml(element, "DefaultAction", default=False) == "1":
        metadata[4] = "1"
    if control_type == "Button" and bool_record_from_xml(element, "DefaultAction", default=False) == "1":
        metadata[5] = "1"
    children_by_id: list[tuple[int, list[object]]] = []
    for child in element:
        child_stream = control_stream_from_xml_with_page(child, asset_root, attribute_type_patterns, attribute_slots, control_templates, None, None)
        if child_stream:
            children_by_id.append((int(child_stream[1]), child_stream))
    pages = element.find("Pages")
    if pages is not None:
        child_parent_size = position_size(element.find("Position"))
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
                    children_by_id.append((int(child_stream[1]), child_stream))
                    page_child_order += 1
    children = [child for _, child in sorted(children_by_id, key=lambda item: item[0])]
    child_table: list[object] = [str(len(children)), *children]
    if control_type == "Chart":
        return [class_id, object_id, info, chart_presentation_record(element, title_record, control_actions_from_xml(element, control_type)), geometry, metadata, child_table]
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
    actions = control_actions_from_xml(element, control_type)
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
        return button_control_info(element, title_record, actions, asset_root)
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
        return table_control_info(element, actions, attribute_type_patterns.get(data_path, []), asset_root)
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


def control_actions_from_xml(element: ET.Element, control_type: str = "") -> list[object]:
    actions = action_table_from_xml(element.find("Action"))
    events = event_table_from_xml(element.find("Events"), control_type)
    if not actions:
        return events
    seen = {action_record_key(action) for action in actions}
    for event in events:
        key = action_record_key(event)
        if key not in seen:
            actions.append(event)
            seen.add(key)
    return actions


def action_record_key(record: object) -> tuple[str, str, str]:
    if not isinstance(record, list) or len(record) < 3:
        return ("", "", "")
    payload = record[2]
    handler = clean_atom(payload[1]) if isinstance(payload, list) and len(payload) > 1 else ""
    return (clean_atom(record[0]), clean_atom(record[1]), handler)


def panel_control_info_from_xml(element: ET.Element, title_record: list[object], actions: list[object]) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["Panel"]
    pages = element.find("Pages")
    page_nodes = pages.findall("Page") if pages is not None else []
    page_count = len(page_nodes) or 1
    serialization = element.find("SerializationProfile")
    explicit_page_capacity = panel_serialization_page_capacity(serialization)
    page_capacity = explicit_page_capacity or panel_page_capacity(page_count)
    position = element.find("Position")
    raw_width = position.get("width", "1228") if position is not None else "1228"
    raw_height = position.get("height", "1054") if position is not None else "1054"
    width = panel_extent_value(raw_width, 6)
    height = panel_extent_value(raw_height, 24)
    state_table = panel_state_table(element, title_record, extended=True, capacity=page_capacity)
    position_records = panel_page_layout_records(serialization) or panel_position_records(page_capacity, width, height, mode="4")
    dependency_profile = panel_dependency_profile_from_xml(serialization, page_capacity)
    return [
        descriptor.info_kind,
        [
            panel_base_info_record_for_serialization(serialization),
            "26",
            *dependency_profile,
            "0",
            "0",
            page_style_group_record("1"),
            "0" if serialization is not None else "1",
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
        action_records(actions),
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


PANEL_PROFILE_DIMENSION_CODES = {
    "top": "0",
    "bottom": "1",
    "left": "2",
    "right": "3",
}


def panel_serialization_page_capacity(serialization: ET.Element | None) -> int | None:
    if serialization is None:
        return None
    value = serialization.get("pageCapacity")
    if value is None:
        return None
    try:
        page_capacity = int(value)
    except ValueError:
        return None
    return page_capacity if page_capacity > 0 else None


def panel_dependency_profile_from_xml(serialization: ET.Element | None, page_capacity: int) -> list[object]:
    if serialization is None:
        return ["1", ["0", str(page_capacity), "1"], *panel_control_slot_profile()]
    groups = sorted(serialization.findall("DependencyGroup"), key=lambda node: int(node.get("order", "0") or "0"))
    if not groups:
        return ["1", ["0", str(page_capacity), "1"], *panel_control_slot_profile()]
    result: list[object] = []
    for group in groups:
        dependencies: list[list[object]] = []
        for dependency in group.findall("Dependency"):
            target_id = dependency.get("targetId", "0")
            dimension = dependency.get("dimension", "top")
            dimension_code = PANEL_PROFILE_DIMENSION_CODES.get(dimension)
            if dimension_code is None and dimension.startswith("dimension"):
                dimension_code = dimension.removeprefix("dimension")
            dependencies.append(["0", target_id, dimension_code or "0"])
        result.extend([str(len(dependencies)), *dependencies])
    return result


def panel_page_layout_records(serialization: ET.Element | None) -> list[list[object]]:
    if serialization is None:
        return []
    layouts = sorted(serialization.findall("PageLayout"), key=lambda node: int(node.get("page", "0") or "0"))
    records: list[list[object]] = []
    for layout in layouts:
        page = layout.get("page", "0")
        left = layout.get("left", "6")
        top = layout.get("top", "6")
        width = layout.get("width", "0")
        height = layout.get("height", "0")
        horizontal_mode = layout.get("horizontalMode", "4")
        vertical_mode = layout.get("verticalMode", "4")
        records.extend(
            [
                ["2", left, "1", "1", "1", page, "0", "0", "0"],
                ["2", top, "0", "1", "2", page, "0", "0", "0"],
                ["2", width, "1", "1", "3", page, "0", horizontal_mode, "0"],
                ["2", height, "0", "1", "4", page, "0", vertical_mode, "0"],
            ]
        )
    return records


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
            states.append(panel_state_record("6" if extended else "3", localized_text_record(title), name, style_mode=page.get("styleMode")))
    if capacity is not None and capacity > len(states):
        for index in range(len(states), capacity):
            name = f"Страница{index + 1}"
            states.append(panel_state_record("6" if extended else "3", localized_text_record(name), name))
    return ["1", str(len(states)), *states]


def panel_state_record(record_kind: str, title: list[object], name: str, *, style_mode: str | None = None) -> list[object]:
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
        if style_mode is not None and len(record[2]) > 6:
            record[2][6] = style_mode
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
    horizontal_align = text_or_default(element, "HorizontalAlign", element.findtext("TextPosition") or ("0" if title.endswith(":") else "4"))
    vertical_align = text_or_default(element, "VerticalAlign", "1")
    picture_size = text_or_default(element, "PictureSize", "1")
    picture_position = text_or_default(element, "PicturePosition", "0" if title.endswith(":") else horizontal_align)
    hyperlink = bool_record_from_xml(element, "Hyperlink", default=bool(actions))
    base = extended_base_info_record_from_xml(element)
    base[6] = default_color_record()
    return [
        "3",
        [
            base,
            "11",
            title_record,
            horizontal_align,
            vertical_align,
            hyperlink,
            "0",
            "0",
            ["0", "0", "0"],
            "0",
            ["1", "0"],
            picture_size,
            ["10", picture_position, empty_page_style_record(), empty_page_style_record(), empty_page_style_record(), "100", "2", "0", "0", "1", "2"],
            "4",
            "0",
            "0",
            "0",
            "0",
            "0",
            "0",
            "0",
        ],
        action_records(actions),
    ]


def input_field_control_info(element: ET.Element, actions: list[object], type_pattern: list[object] | None = None) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["InputField"]
    pattern = type_pattern or [quoted_atom("S")]
    return [
        descriptor.info_kind,
        [quoted_atom("Pattern"), pattern],
        [input_field_info_record_from_xml(element, pattern)],
        input_field_data_source_record(element),
        action_records(actions),
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
                [
                    "4",
                    [quoted_atom("U")],
                    [quoted_atom("U")],
                    text_or_default(element, "DataBindingMode", "0"),
                    '""',
                    text_or_default(element, "DataBindingFlag", "0"),
                    "0",
                ],
            ],
        ]
    return ["0"]


def input_field_info_record_from_xml(element: ET.Element, type_pattern: list[object]) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["InputField"]
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    record = [
        base,
        "31",
        "0",
        text_or_default(element, "EditMode", "0"),
        text_or_default(element, "ChoiceMode", "1"),
        bool_record_from_xml(element, "PasswordMode", default=False),
        "0",
        "0",
        bool_record_from_xml(element, "ReadOnly", default=False),
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
    record[26] = bool_record_from_xml(element, "MultiLine", default=False)
    return record


def button_control_info(element: ET.Element, title_record: list[object], actions: list[object], asset_root: Path | None) -> list[object]:
    picture_payload = picture_payload_from_xml(element.find("Picture"), asset_root)
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
            button_picture_record(picture_payload) if picture_payload else empty_page_style_record(),
            ["0", "0", "0"],
            "0",
            "0",
            "0",
            "0",
            "0",
            "1",
        ],
        action_records(actions),
    ]


def button_picture_record(picture_payload: str) -> list[object]:
    return ["4", "3", ["0"], '""', "-1", "-1", "0", [[picture_payload]], "0", '""']


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
        action_records(actions),
    ]


def choice_field_control_info(element: ET.Element, actions: list[object]) -> list[object]:
    return [
        "2",
        choice_field_info_record_from_xml(element),
        action_records(actions),
    ]


def choice_field_info_record_from_xml(element: ET.Element) -> list[object]:
    choice_list = choice_list_record_from_xml(element.find("ChoiceList"))
    choice_list_tail: list[object] = (
        [choice_list, "0", "0"]
        if choice_list is not None
        else [
            bool_record_from_xml(element, "CreateButton", default=False),
            bool_record_from_xml(element, "EditButton", default=False),
        ]
    )
    return [
        choice_field_base_info_record_from_xml(element),
        "31",
        text_or_default(element, "LeftFixedColumns", "0"),
        text_or_default(element, "RightFixedColumns", "0"),
        bool_record_from_xml(element, "AutoMarkIncomplete", default=True),
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
        *choice_list_tail,
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


def choice_list_record_from_xml(element: ET.Element | None) -> list[object] | None:
    if element is None:
        return None
    row_records = [choice_list_item_record_from_xml(index, item) for index, item in enumerate(element.findall("Item"))]
    selection_index = element.get("selectionIndex") or str(max(len(row_records) - 1, 0))
    presentation_type = element.get("presentationType") or "87024738-fc2a-4436-ada1-df79d395c424"
    return [
        "9",
        [
            "2",
            ["0", quoted_atom("Value"), [quoted_atom("Pattern")], quoted_atom("Значение"), "10"],
            [
                "1",
                quoted_atom("Presentation"),
                [quoted_atom("Pattern"), [quoted_atom("#"), presentation_type]],
                quoted_atom("Представление"),
                "10",
            ],
        ],
        [
            "2",
            "2",
            "0",
            "0",
            "1",
            "1",
            ["1", str(len(row_records)), *row_records],
            element.get("currentIndex") or "-1",
            selection_index,
        ],
        ["0", "0"],
    ]


def choice_list_item_record_from_xml(index: int, element: ET.Element) -> list[object]:
    presentation_type = element.get("presentationType") or "87024738-fc2a-4436-ada1-df79d395c424"
    return [
        "2",
        element.get("index") or str(index),
        "2",
        [quoted_atom(element.get("valueType") or "N"), element.get("value") or "0"],
        [quoted_atom("#"), presentation_type, choice_list_presentation_record(get_multilang_text(element, "Presentation"))],
        "0",
    ]


def choice_list_presentation_record(text: str) -> list[object]:
    return ["1", quoted_atom("ru"), quoted_atom(text)]


def choice_field_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    if element.find("ChoiceList") is not None:
        base[16:20] = ["0", "0", "0", "0"]
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
        action_records(actions),
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
    picture_record = image_picture_style_group_record(element, picture_payload) if picture_payload else page_style_group_record("0")
    return [
        "1",
        [
            image_base_info_record_from_xml(element),
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
        action_records(actions),
    ]


def image_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    if element.find("BorderColor") is None:
        base[6] = default_color_record()
    base[17] = "1"
    return base


def image_picture_style_group_record(element: ET.Element, picture_payload: str) -> list[object]:
    rendering = element.find("PictureRendering")
    return [
        "10",
        "0",
        ["4", "3", ["0"], '""', "-1", "-1", "0", [[picture_payload]], "0", '""'],
        empty_page_style_record(),
        empty_page_style_record(),
        "100",
        text_or_default(element, "PictureSize", "1"),
        bool_record_from_xml(element, "ScalePicture", default=True),
        "0",
        rendering.get("horizontalMode", "0") if rendering is not None else "0",
        rendering.get("verticalMode", "1") if rendering is not None else "1",
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
            record.insert(-109, action_records(actions))
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
        action_records(actions),
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
        action_records(actions),
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
        action_records(actions),
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
        action_records(actions),
    ]


def list_box_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    return base


def command_bar_control_info(element: ET.Element) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["CommandBar"]
    title = get_multilang_text(element, "Title")
    graph = command_bar_action_graph_from_xml(element)
    record = [
        command_bar_base_info_record(element),
        "9",
        "2",
        "1",
        "0",
        "0",
        "1",
        command_bar_items_record(element, title, graph),
        "b78f2e80-ec68-11d4-9dcf-0050bae2bc79",
        "4",
        graph.get(
            "profileUuid",
            "7aa39d8b-4bb3-4d97-9cd8-89b07dc4c30d"
            if element.get("name") == "ОсновныеДействияФормы" or title
            else "bf009aa1-86de-4918-8876-74f65410b0d9"
            if element.get("name") == "КоманднаяПанель1"
            else "9d0a2e40-b978-11d4-84b6-008048da06df",
        ),
        "0",
        "0",
        "0",
    ]
    if element.find("Buttons") is not None:
        record[3] = "0"
    elif element.get("name") == "ОсновныеДействияФормы" or element.find("Title") is not None:
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


def command_bar_action_graph_from_xml(element: ET.Element) -> dict[str, str]:
    graph = element.find("./SerializationProfile/ActionGraph")
    return dict(graph.attrib) if graph is not None else {}


def command_bar_items_record(element: ET.Element, title: str, graph: dict[str, str] | None = None) -> list[object]:
    graph = graph or {}
    buttons = element.find("Buttons")
    if buttons is not None:
        record = command_bar_items_record_from_xml(buttons)
        if record:
            return record
    if element.get("name") != "ОсновныеДействияФормы" and not title:
        if element.get("name") == "КоманднаяПанель1":
            palette_uuid = "be9879c8-ccdc-422b-bbcf-5ad790df187d"
            palette_kind = "3"
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
            graph.get("rootUuid", palette_uuid),
            graph.get("rootKind", palette_kind),
            "1",
            "0",
            "1",
            [
                "5",
                graph.get("branchUuid", "b78f2e80-ec68-11d4-9dcf-0050bae2bc79"),
                "4",
                graph.get("branchKind", "0"),
                graph.get("branchMode", "0"),
                placement,
            ],
        ]
    action_name = compact_identifier(title) or "КнопкаВыполнитьНажатие"
    root_uuid = graph.get("rootUuid", "87a7828f-3ea2-4ed5-9afd-292b4728c926")
    separator_uuid = graph.get("separatorUuid", "ab4be893-7c76-4903-a6fa-9922b4bd863a")
    close_uuid = graph.get("closeUuid", "de61b328-43e2-4c0b-9dd8-f3d146ef7a54")
    action_uuid = graph.get("actionUuid", "9eaf9ed0-41fc-4f23-b9cc-f9051ee23274")
    branch_uuid = graph.get("branchUuid", "b78f2e80-ec68-11d4-9dcf-0050bae2bc79")
    return [
        "5",
        root_uuid,
        "3",
        "1",
        "3",
        [
            "8",
            separator_uuid,
            "1",
            "fbe38877-b914-4fd5-8540-07dde06ba2e1",
            ["6", "2", "00000000-0000-0000-0000-000000000000", "142", ["1", "0", "357c6a54-357d-425d-a2bd-22f4f6e86c87", "2147483647", "0"], "0", "1"],
            "0",
            graph.get("actionMode", "1"),
            "1",
        ],
        ["8", close_uuid, "1", "abde0c9a-18a6-4e0c-bbaa-af26b911b3e6", ["1", "9d0a2e40-b978-11d4-84b6-008048da06df", "0"], "0", graph.get("actionMode", "1"), "1"],
        [
            "8",
            action_uuid,
            "1",
            DEFAULT_CONTROL_EVENT_UUID,
            ["3", quoted_atom(action_name), command_bar_action_descriptor(action_name, title or action_name)],
            "0",
            graph.get("actionMode", "1"),
            "1",
        ],
        "1",
        [
            "5",
            branch_uuid,
            "4",
            "0",
            "3",
            action_uuid,
            ["8", quoted_atom("ОсновныеДействияФормыВыполнить"), "0", "1", localized_text_record("Выполнить"), "1", root_uuid, "1", "1e2", "0", "1", "1", "0", "1", "0", "0"],
            close_uuid,
            ["8", quoted_atom("Разделитель"), "0", "1", ["1", "0"], "0", root_uuid, "2", "1e2", "2", "1", "1", "0", "1", "0", "0"],
            separator_uuid,
            ["8", quoted_atom("ОсновныеДействияФормыЗакрыть"), "0", "1", localized_text_record("Закрыть"), "1", root_uuid, "3", "1e2", "0", "1", "1", "0", "1", "0", "0"],
            ["-1", "0", ["0"]],
        ],
    ]


def command_bar_items_record_from_xml(buttons: ET.Element) -> list[object]:
    root_uuid = buttons.get("rootUuid")
    root_kind = buttons.get("rootKind")
    if not root_uuid or not root_kind:
        return []
    actions = []
    for action in sorted(buttons.findall("./Actions/Action"), key=lambda node: int(node.get("order") or "0")):
        actions.append(command_bar_action_record_from_xml(action))
    groups = []
    for group in sorted(buttons.findall("./Groups/Group"), key=lambda node: int(node.get("order") or "0")):
        groups.append(command_bar_group_record_from_xml(group))
    return [
        "5",
        root_uuid,
        root_kind,
        buttons.get("rootFlag") or "1",
        str(len(actions)),
        *actions,
        str(len(groups)),
        *groups,
    ]


def command_bar_action_record_from_xml(action: ET.Element) -> list[object]:
    record = [
        "8",
        action.get("uuid") or "",
        action.get("enabled") or "1",
        action.get("eventUuid") or DEFAULT_CONTROL_EVENT_UUID,
        command_bar_value_from_attr(action.get("kind"), default=command_bar_action_payload_from_xml(action)),
    ]
    index = 5
    while action.get(f"field{index}") is not None:
        record.append(command_bar_value_from_attr(action.get(f"field{index}"), default="0"))
        index += 1
    return record


def command_bar_action_payload_from_xml(action: ET.Element) -> list[object]:
    handler = action.get("handler") or action.get("title") or ""
    title = action.get("title") or handler
    return ["3", quoted_atom(handler), command_bar_action_descriptor(handler, title)]


def command_bar_group_record_from_xml(group: ET.Element) -> list[object]:
    buttons = sorted(group.findall("Button"), key=lambda node: int(node.get("order") or "0"))
    record: list[object] = [
        "5",
        group.get("uuid") or "",
        group.get("kind") or "4",
        group.get("mode") or "0",
        str(len(buttons)),
    ]
    for button in buttons:
        record.append(button.get("actionUuid") or "")
        record.append(command_bar_button_descriptor_from_xml(button))
    placement = group.find("Placement")
    if placement is not None:
        record.append(command_bar_value_from_attr(placement.get("value"), default=["-1", "0", ["0"]]))
    else:
        record.append(["-1", "0", ["0"]])
    return record


def command_bar_button_descriptor_from_xml(button: ET.Element) -> list[object]:
    descriptor = button.get("descriptor")
    if descriptor:
        value = command_bar_value_from_attr(descriptor, default=[])
        if isinstance(value, list):
            return value
    title = get_multilang_text(button, "Title")
    title_record: object = localized_text_record(title) if title else ["1", "0"]
    return [
        "8",
        quoted_atom(button.get("name") or ""),
        button.get("state") or "0",
        button.get("visible") or "1",
        title_record,
        button.get("hasAction") or "1",
        button.get("ownerUuid") or "",
        button.get("position") or "1",
        button.get("style") or "1e2",
        button.get("kind") or "0",
        button.get("groupMode") or "1",
        button.get("enabled") or "1",
        button.get("checked") or "0",
        button.get("showText") or "1",
        button.get("shortcut") or "0",
        button.get("default") or "0",
    ]


def command_bar_value_from_attr(value: str | None, *, default: object) -> object:
    if not value:
        return copy.deepcopy(default)
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        return value


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
    has_button_graph = element.find("Buttons") is not None
    if has_button_graph:
        command_scope = "0"
    elif element.get("name") == "ОсновныеДействияФормы" or element.find("Title") is not None:
        command_scope = "7"
    elif element.get("name") == "КоманднаяПанель1":
        command_scope = "4"
    else:
        command_scope = "0"
    record = [
        "19",
        visible_record_from_xml(element),
        default_color_record(),
        default_color_record(),
        font_record_from_xml(element.find("Font")),
        "0",
        color_record_from_xml(element, "BorderColor") if element.find("BorderColor") is not None else (default_color_record() if has_button_graph else ["4", "3", ["-22"], "3"]),
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
        "2" if has_button_graph else "0",
        "1" if has_button_graph else "0",
        "1" if has_button_graph else "0",
        "2" if has_button_graph else "0",
        default_color_record(),
    ]
    if has_button_graph and element.find("TextColor") is not None:
        record[2] = color_record_from_xml(element, "TextColor")
    if has_button_graph and element.find("BackColor") is not None:
        record[3] = color_record_from_xml(element, "BackColor")
    return record


def table_control_info(element: ET.Element, actions: list[object], type_pattern: list[object], asset_root: Path | None = None) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["Table"]
    pattern = type_pattern or [quoted_atom("#"), "00000000-0000-0000-0000-000000000000"]
    base = extended_base_info_record_from_xml(element)
    base[11] = ["3", "1", ["-18"], "0", "0", "0"]
    base[17] = "1"
    return [
        descriptor.info_kind,
        [quoted_atom("Pattern"), pattern],
        [
            base,
            table_view_record_from_xml(element, asset_root),
        ],
        table_data_source_record(element),
        action_records(actions),
    ]


TABLE_EVENT_ID_BY_NAME = {
    "Выбор": "34",
    "ПриИзмененииФлажка": "45",
    "ПриВыводеСтроки": "47",
    "ПриПолученииДанных": "53",
}


def table_data_source_record(element: ET.Element) -> list[object]:
    if table_columns_from_xml(element):
        return ["342cf854-134c-42bb-8af9-a2103d5d9723", ["5", "0", "0", "0"]]
    return ["00000000-0000-0000-0000-000000000000", ["2", "1", ["0", "1"]]]


def table_view_record_from_xml(element: ET.Element, asset_root: Path | None = None) -> list[object]:
    descriptor = CORE_CONTROL_INFO_DESCRIPTORS["Table"]
    columns_count = element.findtext("ColumnsCount") or "0"
    rows_count = element.findtext("RowsCount") or "0"
    columns = table_columns_from_xml(element)
    if columns:
        return extended_table_view_record(element, columns, asset_root)
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


def extended_table_view_record(element: ET.Element, columns: list[ET.Element], asset_root: Path | None = None) -> list[object]:
    record = [
        "23",
        text_or_default(element, "ViewProfile", "117644289"),
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
        [str(len(columns)), *[table_column_record(column, index, asset_root) for index, column in enumerate(columns)]],
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
    record[14] = bool_record_from_xml(element, "ReadOnly", default=False)
    if element.find("FieldBackColor") is not None:
        record[6] = color_record_from_xml(element, "FieldBackColor")
    record[20] = text_or_default(element, "LeftFixedColumns", "0")
    record[21] = text_or_default(element, "RightFixedColumns", "0")
    record[22] = bool_record_from_xml(element, "AutoMarkIncomplete", default=True)
    record[35] = text_or_default(element, "ViewSetupMode", "1")
    return record


def table_column_record(column: ET.Element, index: int, asset_root: Path | None = None) -> list[object]:
    title = get_multilang_text(column, "Title") or column.get("name") or f"Колонка{index + 1}"
    name = column.get("name") or title
    data_path = text_or_default(column, "DataPath", "")
    order = column.get("order") or str(index)
    width = text_or_default(column, "Width", "1e2")
    style = text_or_default(column, "Style", "12590592")
    visible = bool_text_as_record(column, "Visible", default=True)
    read_only = bool_text_as_record(column, "ReadOnly", default=False)
    column_kind = text_or_default(column, "ColumnKind", "0")
    check_mode = text_or_default(column, "CheckMode", "4")
    output_mode = text_or_default(column, "OutputMode", "0")
    data_path_mode = text_or_default(column, "DataPathMode", "1")
    presentation_index = text_or_default(column, "PresentationIndex", "15")
    use_picture = bool_text_as_record(column, "UsePicture", default=False)
    font = font_record_from_xml(column.find("Font")) if column.find("Font") is not None else ["8", "3", "0", "1", "100"]
    text_color = color_record_from_xml(column, "TextColor") if column.find("TextColor") is not None else default_color_record()
    format_record = localized_text_record(get_multilang_text(column, "Format")) if column.find("Format") is not None else ["1", "0"]
    pattern = table_column_type_pattern_from_xml(column)
    pattern_record = table_column_pattern_record_from_xml(column, pattern)
    payload = table_column_value_payload_from_xml(column, pattern)
    picture_payload = picture_payload_from_xml(column.find("Picture"), asset_root)
    editor_control = text_or_default(column, "EditorControl", "InputField")
    editor_guid = ORDINARY_CONTROL_GUID_BY_TYPE.get(editor_control, ORDINARY_CONTROL_GUID_BY_TYPE["InputField"])
    body = [
        "23",
        localized_text_record(title),
        ["1", "0"],
        ["1", "0"],
        width,
        order,
        "-1",
        "-1",
        "-1",
        style,
        empty_page_style_record(),
        column_picture_record(picture_payload),
        empty_page_style_record(),
        "16",
        "16",
        "d2314b5d-8da4-4e0f-822b-45e7500eae09",
        default_color_record(),
        text_color,
        default_color_record(),
        default_color_record(),
        default_color_record(),
        default_color_record(),
        font,
        ["8", "3", "0", "1", "100"],
        ["8", "3", "0", "1", "100"],
        visible,
        read_only,
        column_kind,
        check_mode,
        output_mode,
        quoted_atom(name),
        [],
        presentation_index,
        use_picture,
        format_record,
        pattern_record,
        "0",
        data_path_mode,
        editor_guid,
        [[payload], "0"],
        "0",
        "0",
        "0",
        "0",
        "0",
        width,
        "0",
        visible,
        "0",
        "0",
        "2",
        "0",
    ]
    return [
        "737535a4-21e6-4971-8513-3e3173a9fedd",
        ["8", ["8", body, ["-1"], ["-1"], ["-1"]], quoted_atom(data_path), '""' if data_path else quoted_atom(title), '""', "0"],
    ]


def table_column_type_pattern_from_xml(column: ET.Element) -> list[object]:
    pattern_node = column.find("./Type/Pattern")
    if pattern_node is not None and not pattern_node.findall("PatternItem"):
        return []
    return type_pattern_from_xml(column) or [quoted_atom("S")]


def table_column_pattern_record_from_xml(column: ET.Element, pattern: list[object]) -> list[object]:
    pattern_node = column.find("./Type/Pattern")
    if pattern_node is not None and not pattern_node.findall("PatternItem"):
        return [quoted_atom("Pattern")]
    return [quoted_atom("Pattern"), pattern]


def table_column_value_payload_from_xml(column: ET.Element, pattern: list[object]) -> str:
    descriptor = column.find("ValueDescriptor")
    if descriptor is not None and descriptor.text:
        payload = "".join(descriptor.text.split())
        if payload:
            return wrap_base64_payload(payload)
    return TABLE_COLUMN_VALUE_PAYLOAD_BY_PATTERN.get(tuple(pattern), TABLE_COLUMN_VALUE_PAYLOAD_BY_PATTERN[(quoted_atom("S"),)])


def column_picture_record(picture_payload: str) -> list[object]:
    if picture_payload:
        return ["4", "3", ["0"], '""', "-1", "-1", "0", [[picture_payload]], "0", '""']
    return empty_page_style_record()


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
        action_records(actions),
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
    if element.find("ButtonTextColor") is not None:
        base[9] = color_record_from_xml(element, "ButtonTextColor")
    if element.find("ButtonBackColor") is not None:
        base[10] = color_record_from_xml(element, "ButtonBackColor")
    return base


def extended_base_info_record_from_xml(element: ET.Element) -> list[object]:
    base = root_panel_base_info_record()
    base[17] = "1"
    base[1] = visible_record_from_xml(element)
    if element.find("TextColor") is not None:
        base[2] = color_record_from_xml(element, "TextColor")
    if element.find("BackColor") is not None:
        base[3] = color_record_from_xml(element, "BackColor")
    if element.find("Font") is not None:
        base[4] = font_record_from_xml(element.find("Font"))
    if element.find("BorderColor") is not None:
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
    return [[action.get("id") or "0", uuid, ["3", quoted_atom(name), event_descriptor(name, title)]]]


def event_table_from_xml(events: ET.Element | None, control_type: str = "") -> list[object]:
    if events is None:
        return []
    result: list[object] = []
    for event in events.findall("Event"):
        event_name = event.get("name", "")
        handler = (event.text or "").strip() or event_name
        uuid = event.get("uuid") or DEFAULT_CONTROL_EVENT_UUID
        event_id = event_platform_id(event, control_type)
        title = event.get("title") or event_name
        result.append([event_id, uuid, ["3", quoted_atom(handler), event_descriptor(handler, title)]])
    return result


def action_records(actions: list[object]) -> list[object]:
    return [str(len(actions)), *actions] if actions else ["0"]


def event_platform_id(event: ET.Element, control_type: str = "") -> str:
    event_name = event.get("name", "")
    if control_type != "Table":
        return event.get("id") or "2147483647"
    return event.get("id") or TABLE_EVENT_ID_BY_NAME.get(event_name, "2147483647")


def text_or_default(element: ET.Element, tag: str, default: str) -> str:
    value = (element.findtext(tag) or "").strip()
    return value or default


def bool_text_as_record(element: ET.Element, tag: str, *, default: bool) -> str:
    value = (element.findtext(tag) or "").strip().lower()
    if value in {"true", "1"}:
        return "1"
    if value in {"false", "0"}:
        return "0"
    return "1" if default else "0"


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
    return "\r\r\n".join(lines)


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
    layout_next_order = position.get("layoutNextOrder") if position is not None else None
    layout_mode = position.get("layoutMode", "1") if position is not None else "1"
    layout_flag1 = position.get("layoutFlag1", "0") if position is not None else "0"
    layout_flag2 = position.get("layoutFlag2", "0") if position is not None else "0"
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
        counted_geometry[5] = layout_mode
        return counted_geometry
    flagged_height_width_geometry = flagged_height_width_dimension_geometry_from_xml(position, left, top, right, bottom, bindings)
    if flagged_height_width_geometry is not None:
        flagged_height_width_geometry[5] = layout_mode
        return flagged_height_width_geometry
    inline_dual_counted_geometry = inline_dual_counted_dimension_geometry_from_xml(position, left, top, right, bottom, bindings)
    if inline_dual_counted_geometry is not None:
        inline_dual_counted_geometry[5] = layout_mode
        return inline_dual_counted_geometry
    inline_segmented_geometry = inline_segmented_dimension_geometry_from_xml(position, left, top, right, bottom, bindings)
    if inline_segmented_geometry is not None:
        inline_segmented_geometry[5] = layout_mode
        return inline_segmented_geometry
    inline_counted_geometry = inline_counted_dimension_geometry_from_xml(position, left, top, right, bottom, bindings)
    if inline_counted_geometry is not None:
        inline_counted_geometry[5] = layout_mode
        return inline_counted_geometry
    if layout_group is not None and (layout_flag1 != "0" or layout_flag2 != "0") and not any(dimension != "0" for dimension in dimensions):
        return [
            "8",
            left,
            top,
            right,
            bottom,
            layout_mode,
            *bindings,
            "0",
            "0",
            "0",
            "0",
            "0",
            "0",
            *layout_group_tail(layout_group, layout_order, "0", "0", layout_next_order, layout_flag1, layout_flag2),
        ]
    if control_type == "Splitter":
        default_group = str(page_order) if page_order is not None else "0"
        default_order = str(page_index) if page_index is not None else "0"
        return [
            "8",
            left,
            top,
            right,
            bottom,
            layout_mode,
            *bindings,
            "0",
            *dimensions,
            "0",
            "0",
            *layout_group_tail(layout_group, layout_order, default_group, default_order, layout_next_order),
        ]
    if control_type in {"SpreadsheetDocumentField", "TextDocumentField", "PivotChart"} and page_index is not None and page_order is not None:
        return [
            "8",
            left,
            top,
            right,
            bottom,
            layout_mode,
            *bindings,
            *dimensions,
            "0",
            "0",
            *layout_group_tail(layout_group, layout_order, str(page_order), str(page_index), layout_next_order),
        ]
    if (layout_flag1 != "0" or layout_flag2 != "0") and any(dimension != "0" for dimension in dimensions):
        return [
            "8",
            left,
            top,
            right,
            bottom,
            layout_mode,
            *bindings,
            "0",
            *dimensions,
            *layout_pre_tail(position),
            *layout_group_tail(layout_group, layout_order, str(page_order) if page_order is not None else "0", str(page_index) if page_index is not None else "0", layout_next_order, layout_flag1, layout_flag2),
        ]
    if page_index is None or page_order is None:
        trailer = GEOMETRY_TRAILER_PROFILE.get(control_type, GEOMETRY_TRAILER_PROFILE["default"])
        if control_type == "Panel" and (layout_group is not None or layout_order is not None):
            trailer = ["0", *layout_group_tail(layout_group, layout_order, "0", "0", layout_next_order)]
        if (layout_flag1 != "0" or layout_flag2 != "0") and any(dimension != "0" for dimension in dimensions):
            return [
                "8",
                left,
                top,
                right,
                bottom,
                layout_mode,
                *bindings,
                "0",
                *dimensions,
                *layout_group_tail(layout_group, layout_order, "0", "0", layout_next_order, layout_flag1, layout_flag2),
            ]
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
            layout_next_order,
        )
        return [
            "8",
            left,
            top,
            right,
            bottom,
            layout_mode,
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
        group_tail = layout_group_tail(layout_group, layout_order, data_slot, default_order, layout_next_order)
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
            layout_mode,
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
        group_tail = layout_group_tail(layout_group, layout_order, data_slot, "0", layout_next_order)
        return [
            "8",
            left,
            top,
            right,
            bottom,
            layout_mode,
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
        group_tail = layout_group_tail(layout_group, layout_order, data_slot, "1", layout_next_order)
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
            layout_mode,
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
            layout_next_order,
        )
        return [
            "8",
            left,
            top,
            right,
            bottom,
            layout_mode,
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
    group_tail = layout_group_tail(layout_group, layout_order, str(page_order), str(page_index), layout_next_order)
    return [
        "8",
        left,
        top,
            right,
            bottom,
            layout_mode,
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
    layout_next_order: str | None = None,
    flag1: str = "0",
    flag2: str = "0",
) -> list[str]:
    group = layout_group if layout_group is not None else default_group
    order = layout_order if layout_order is not None else default_order
    if layout_next_order is not None:
        next_order = layout_next_order
    else:
        try:
            next_order = str(int(order) + 1)
        except ValueError:
            next_order = "0"
    return [group, order, next_order, flag1, flag2]


def layout_pre_tail(position: ET.Element | None) -> list[str]:
    if position is None:
        return []
    return [value for value in (position.get("layoutPreTail") or "").split(" ") if value != ""]


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


def flagged_height_width_dimension_geometry_from_xml(
    position: ET.Element | None,
    left: str,
    top: str,
    right: str,
    bottom: str,
    bindings: list[object],
) -> list[object] | None:
    if position is None or position.get("dimensionProfile") != "flaggedHeightWidth":
        return None
    binding_container = position.find("Bindings")
    if binding_container is None:
        return None
    height: object | None = None
    width: object | None = None
    for binding in binding_container.findall("DimensionBinding"):
        dimension = binding.get("dimension")
        if dimension == "height":
            height = dimension_binding_to_raw(binding)
        elif dimension == "width":
            width = dimension_binding_to_raw(binding)
    if height is None or width is None:
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
        "1",
        height,
        "0",
        "0",
        "1",
        width,
        "0",
        "0",
        *tail,
    ]


def inline_counted_dimension_geometry_from_xml(
    position: ET.Element | None,
    left: str,
    top: str,
    right: str,
    bottom: str,
    bindings: list[object],
) -> list[object] | None:
    if position is None or position.get("dimensionProfile") != "inlineCounted":
        return None
    binding_container = position.find("Bindings")
    if binding_container is None:
        return None
    dimensions: list[object] = []
    for binding in binding_container.findall("DimensionBinding"):
        if (binding.get("section") or "primary") == "primary":
            dimensions.append(dimension_binding_to_raw(binding))
    if not dimensions:
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
        str(len(dimensions)),
        *dimensions,
        *tail,
    ]


def inline_dual_counted_dimension_geometry_from_xml(
    position: ET.Element | None,
    left: str,
    top: str,
    right: str,
    bottom: str,
    bindings: list[object],
) -> list[object] | None:
    if position is None or position.get("dimensionProfile") != "inlineDualCounted":
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
    if not primary or not secondary:
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
        str(len(primary)),
        *primary,
        position.get("secondaryDimensionMarker", "0"),
        str(len(secondary)),
        *secondary,
        *tail,
    ]


def inline_segmented_dimension_geometry_from_xml(
    position: ET.Element | None,
    left: str,
    top: str,
    right: str,
    bottom: str,
    bindings: list[object],
) -> list[object] | None:
    if position is None or position.get("dimensionProfile") != "inlineSegmented":
        return None
    binding_container = position.find("Bindings")
    if binding_container is None:
        return None
    segment_specs = [value for value in (position.get("dimensionSegments") or "").split(" ") if value]
    if not segment_specs:
        return None
    records_by_section: dict[str, list[object]] = {}
    for binding in binding_container.findall("DimensionBinding"):
        section = binding.get("section") or "primary"
        records_by_section.setdefault(section, []).append(dimension_binding_to_raw(binding))
    result: list[object] = ["8", left, top, right, bottom, "1", *bindings]
    for segment_index, spec in enumerate(segment_specs):
        section = "primary" if segment_index == 0 else f"segment{segment_index + 1}"
        records = records_by_section.get(section, [])
        marker, count = parse_dimension_segment_spec(spec)
        if count != len(records):
            return None
        if segment_index == 0:
            result.append(str(count))
        elif marker:
            result.extend([marker, str(count)])
        else:
            result.append(str(count))
        result.extend(records)
    tail = [value for value in (position.get("layoutTail") or "").split(" ") if value != ""]
    result.extend(tail)
    return result


def parse_dimension_segment_spec(spec: str) -> tuple[str, int]:
    if ":" in spec:
        marker, count = spec.split(":", 1)
        return marker, int(count)
    return "", int(spec)


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
