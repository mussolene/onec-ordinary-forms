"""Internal ordinary-form ListOutStream codec.

This module is the boundary between public object-model ``Form.xml`` and the
platform list-stream payload stored as the ``form`` file inside ``Form.bin``.
Public XML must never expose this stream directly.
"""

from __future__ import annotations

import base64
import xml.etree.ElementTree as ET
from pathlib import Path

from onec_ordinary_forms.liststream import dumps_list_out_stream
from onec_ordinary_forms.ordinary_platform import (
    CF_FORM_CONTROLS8_FORMAT_ID,
    CF_FORM_CONTROLS_INFO8_FORMAT_ID,
    CF_FORM_CONTROLS_POSITION8_FORMAT_ID,
    ORDINARY_CONTROL_GUID_BY_TYPE,
)
from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS


PLATFORM_CONTROL_FORMAT_IDS = {
    "controls": CF_FORM_CONTROLS8_FORMAT_ID,
    "position": CF_FORM_CONTROLS_POSITION8_FORMAT_ID,
    "info": CF_FORM_CONTROLS_INFO8_FORMAT_ID,
}

DEFAULT_CONTROL_EVENT_UUID = "e1692cc2-605b-4535-84dd-28440238746c"


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
    for attribute in root.findall("./Attributes/Attribute"):
        name = attribute.get("name", "")
        if not name:
            continue
        attributes.append(attribute_record_from_xml(attribute))

    controls: list[object] = []
    for page in top_level_pages(root):
        for child in page:
            control = control_stream_from_xml(child, asset_root)
            if control:
                controls.append(control)

    stream = ordinary_form_stream(title or "Main", attributes, controls, events_from_xml(root), form_size_from_xml(root))
    return ("\ufeff" + dumps_list_out_stream(stream)).encode("utf-8")


def ordinary_form_stream(
    title: str,
    attributes: list[object],
    controls: list[object],
    events: list[object],
    form_size: tuple[str, str],
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


def form_root_record(title: str, controls: list[object], form_size: tuple[str, str]) -> list[object]:
    width, height = form_size
    root_panel = [
        ORDINARY_CONTROL_GUID_BY_TYPE["Panel"],
        root_panel_info(title),
        ["1", *controls] if len(controls) == 1 else [str(len(controls)), *controls],
    ]
    return [
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


def root_panel_info(title: str) -> list[object]:
    return [
        "1",
        [
            panel_base_info_record(),
            "21",
            "0",
            "1",
            ["0", "19", "1"],
            "0",
            "1",
            ["0", "19", "3"],
            "0",
            "0",
            ["3", "1", ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]],
            "0",
            "1",
            ["1", "1", ["3", localized_text_record("Страница1"), ["3", "0", ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]], "-1", "1", "1", quoted_atom("Страница1"), "1"]],
            "1",
            "1",
            "0",
            "4",
            ["2", "8", "1", "1", "1", "0", "0", "0", "0"],
            ["2", "8", "0", "1", "2", "0", "0", "0", "0"],
            ["2", "877", "1", "1", "3", "0", "0", "8", "0"],
            ["2", "236", "0", "1", "4", "0", "0", "8", "0"],
            "0",
            "4294967295",
            "5",
            "64",
            "0",
        ],
        ["0"],
    ]


def panel_base_info_record() -> list[object]:
    return base_info_record_from_xml(None)


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
        ["3", "0", ["0"], '""', "-1", "-1", "1", "0"],
        ["0", "0", "0"],
    ]


def form_title_from_xml(root: ET.Element) -> str:
    return get_multilang_text(root, "Title")


def form_size_from_xml(root: ET.Element) -> tuple[str, str]:
    width = (root.findtext("Width") or "").strip()
    height = (root.findtext("Height") or "").strip()
    return width or "885", height or "244"


def top_level_pages(root: ET.Element) -> list[ET.Element]:
    return root.findall("./Pages/Page")


def localized_text_record(text: str) -> list[object]:
    return ["1", "1", [quoted_atom("ru"), quoted_atom(text)]]


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
        result.append(quoted_atom(code) if not is_numeric_atom(code) else code)
        uuid = item.get("uuid")
        if code == "#" and uuid:
            result.append(uuid)
    return result


def is_numeric_atom(value: str) -> bool:
    try:
        int(value)
        return True
    except ValueError:
        return False


def control_stream_from_xml(element: ET.Element, asset_root: Path | None) -> list[object] | None:
    return control_stream_from_xml_with_page(element, asset_root, None, None)


def control_stream_from_xml_with_page(
    element: ET.Element,
    asset_root: Path | None,
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
    info = control_info_from_xml(element, name, control_type, asset_root)
    geometry = geometry_stream_from_xml(element.find("Position"), page_index, page_order)
    metadata_name = data_path_from_xml(element) if control_type in DATA_BOUND_CONTROL_TYPES else name
    metadata = ["14", quoted_atom(metadata_name), "4294967295", "0", "0", "0"]
    children: list[object] = []
    for child in element:
        child_stream = control_stream_from_xml_with_page(child, asset_root, None, None)
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
                child_stream = control_stream_from_xml_with_page(child, asset_root, page_number, page_child_order)
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


def control_info_from_xml(element: ET.Element, name: str, control_type: str, asset_root: Path | None) -> list[object]:
    title = get_multilang_text(element, "Title")
    title_record = localized_text_record(title or name)
    actions = action_table_from_xml(element.find("Action")) + event_table_from_xml(element.find("Events"))
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
        return radio_button_control_info(element, title_record, actions)
    if control_type == "InputField":
        return input_field_control_info(element, actions)
    if control_type == "GroupBox":
        return group_box_control_info(element, title_record)
    if control_type == "Splitter":
        return splitter_control_info(element)
    if control_type == "Chart":
        return chart_control_info()
    if control_type == "HTMLDocumentField":
        return html_document_field_control_info()
    if control_type == "ListBox":
        return list_box_control_info(element)
    if control_type == "CommandBar":
        return command_bar_control_info(element)
    if control_type == "Table":
        return table_control_info(element, actions)
    if control_type == "SpreadsheetDocumentField":
        return spreadsheet_document_field_control_info(element, actions)
    if control_type == "Label":
        return label_control_info(element, title_record, actions)
    raise ValueError(f"Unsupported ordinary form control type for stream writer: {control_type}")


def panel_control_info_from_xml(element: ET.Element, title_record: list[object]) -> list[object]:
    child_count = len([child for child in element if control_type_from_xml_tag(child.tag)])
    pages = element.find("Pages")
    page_nodes = pages.findall("Page") if pages is not None else []
    page_count = len(page_nodes) or 1
    right = element.find("Position").get("right", "877") if element.find("Position") is not None else "877"
    bottom = element.find("Position").get("bottom", "236") if element.find("Position") is not None else "236"
    width = str(max(int(right) - 12, 0)) if right.isdigit() else right
    height = str(max(int(bottom) - 30, 0)) if bottom.isdigit() else bottom
    state_table = panel_state_table(element, title_record)
    position_records = panel_position_records(page_count, width, height)
    return [
        "1",
        [
            base_info_record_from_xml(element),
            "21",
            "0",
            "0",
            "0",
            "0",
            "0",
            "0",
            ["3", "1", ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]],
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
            "5",
            "64",
            "0",
        ],
        ["0"],
    ]


def panel_state_table(element: ET.Element, fallback_title: list[object]) -> list[object]:
    pages = element.find("Pages")
    page_nodes = pages.findall("Page") if pages is not None else []
    if not page_nodes:
        name = element.get("name", "Страница1")
        return ["1", "1", ["3", fallback_title, ["3", "0", ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]], "-1", "1", "1", quoted_atom(name), "1"]]
    states: list[object] = []
    for page in page_nodes:
        name = page.get("name", "Страница1")
        title = get_multilang_text(page, "Title") or name
        states.append(
            [
                "3",
                localized_text_record(title),
                ["3", "0", ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]],
                "-1",
                "1",
                "1",
                quoted_atom(name),
                "1",
            ]
        )
    return ["1", str(len(states)), *states]


def panel_position_records(page_count: int, width: str, height: str) -> list[list[object]]:
    records: list[list[object]] = []
    for page_index in range(page_count):
        page_width = width
        page_height = height
        horizontal_mode = "2"
        vertical_mode = "2"
        if page_count > 1 and page_index == page_count - 1 and width.isdigit() and height.isdigit():
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
    return [
        "3",
        [
            base_info_record_from_xml(element),
            "7",
            title_record,
            "4",
            "1",
            "1" if actions else "0",
            "0",
            "0",
            ["0", "0", "0"],
            "0",
            ["1", "0"],
            "1",
            ["3", "4", ["3", "0", ["0"], '""', "-1", "-1", "1", "0"]],
            "4",
        ],
        ["1", *actions] if actions else ["0"],
    ]


def input_field_control_info(element: ET.Element, actions: list[object]) -> list[object]:
    return [
        "9",
        [quoted_atom("Pattern"), [quoted_atom("S")]],
        [input_field_info_record_from_xml(element)],
        ["1", *actions] if actions else ["0"],
    ]


def input_field_info_record_from_xml(element: ET.Element) -> list[object]:
    return [
        base_info_record_from_xml(element),
        "21",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "1",
        bool_record_from_xml(element, "ReadOnly", default=False),
    ]


def button_control_info(element: ET.Element, title_record: list[object], actions: list[object]) -> list[object]:
    return [
        "1",
        [
            button_base_info_record(element),
            "10",
            title_record,
            "1",
            "1",
            "0",
            "0",
            "0",
            ["3", "0", ["0"], '""', "-1", "-1", "1", "0"],
            ["0", "0", "0"],
            "0",
            "0",
        ],
        ["1", *actions] if actions else ["0"],
    ]


def checkbox_control_info(element: ET.Element, title_record: list[object], actions: list[object]) -> list[object]:
    return [
        "1",
        [
            [
                base_info_record_from_xml(element),
                "4",
                title_record,
                "1",
                "0",
                "1",
            ],
            "1",
            "0",
            "0",
        ],
        ["1", *actions] if actions else ["0"],
    ]


def choice_field_control_info(element: ET.Element, actions: list[object]) -> list[object]:
    return [
        "2",
        [choice_field_info_record_from_xml(element)],
        ["1", *actions] if actions else ["0"],
    ]


def choice_field_info_record_from_xml(element: ET.Element) -> list[object]:
    return [
        base_info_record_from_xml(element),
        "21",
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
        ["3", "0", ["0"], '""', "-1", "-1", "1", "0"],
        ["3", "0", ["0"], '""', "-1", "-1", "1", "0"],
        "0",
        "0",
        "0",
        ["0", "0", "0"],
        ["1", "0"],
        "0",
        "0",
        "0",
        "0",
    ]


def radio_button_control_info(
    element: ET.Element,
    title_record: list[object],
    actions: list[object],
) -> list[object]:
    return [
        "4",
        [quoted_atom("Pattern"), [quoted_atom("B")]],
        [
            [
                checkbox_control_inner_info(element, title_record),
                "1",
            ],
            "0",
            [quoted_atom("B"), "1"],
            ["1", *actions] if actions else ["0"],
        ],
    ]


def checkbox_control_inner_info(element: ET.Element, title_record: list[object]) -> list[object]:
    return [
        base_info_record_from_xml(element),
        "4",
        title_record,
        "1",
        "0",
        "1",
    ]


def image_control_info(element: ET.Element, title_record: list[object], picture_payload: str) -> list[object]:
    picture_record = ["3", "3", ["0"], '""', "-1", "-1", "0", [[picture_payload if picture_payload else '""']], "0"]
    return [
        "1",
        [
            base_info_record_from_xml(element),
            "15",
            "2",
            "0",
            picture_record,
            ["0", "0", "0"],
            "1",
            "1",
            "0",
            "0",
            ["1", "0"],
        ],
        ["0"],
    ]


def group_box_control_info(element: ET.Element, title_record: list[object]) -> list[object]:
    return [
        "0",
        [
            base_info_record_from_xml(element),
            "7",
            title_record,
            ["3", "0", ["0"], "4", "1", "0", "cf48d3ca-5bd4-45b9-bb8f-a0922a8335f2"],
        ],
    ]


def splitter_control_info(element: ET.Element) -> list[object]:
    return [
        "0",
        [
            base_info_record_from_xml(element),
            "2",
            "2",
            "0",
        ],
    ]


def chart_control_info() -> list[object]:
    return ["11"]


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


def list_box_control_info(element: ET.Element) -> list[object]:
    return [
        "1",
        [
            base_info_record_from_xml(element),
            list_box_view_record_from_xml(element),
            "6",
            "0",
            "0",
            "0",
            "0",
        ],
        ["0"],
    ]


def command_bar_control_info(element: ET.Element) -> list[object]:
    return [
        "2",
        [
            base_info_record_from_xml(element),
            "8",
            "2",
            "0",
            "0",
            "0",
            bool_record_from_xml(element, "Autofill", default=True),
            ["0"],
            "0",
            "0",
            "0",
            "0",
        ],
    ]


def table_control_info(element: ET.Element, actions: list[object]) -> list[object]:
    return [
        "5",
        [quoted_atom("Pattern"), [quoted_atom("#"), "00000000-0000-0000-0000-000000000000"]],
        [
            base_info_record_from_xml(element),
            table_view_record_from_xml(element),
        ],
        ["00000000-0000-0000-0000-000000000000", ["2", "1", ["0", "1"]]],
        ["1", *actions] if actions else ["0"],
    ]


def table_view_record_from_xml(element: ET.Element) -> list[object]:
    columns_count = element.findtext("ColumnsCount") or "0"
    rows_count = element.findtext("RowsCount") or "0"
    return [
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
        rows_count.strip() or "0",
        columns_count.strip() or "0",
        bool_record_from_xml(element, "AutoMarkIncomplete", default=True),
        ["0"],
    ]


def spreadsheet_document_field_control_info(element: ET.Element, actions: list[object]) -> list[object]:
    position = element.find("Position")
    width = position.get("width", "100") if position is not None else "100"
    height = position.get("height", "100") if position is not None else "100"
    return [
        "14",
        "8",
        "0",
        width,
        height,
        "5",
        "5",
        "1",
        "1",
        color_record_from_xml(element, "BackColor"),
        ["3", "1", ["-18"], "0", "0", "0"],
        spreadsheet_settings_record(),
        "1",
        "1",
        ["1", "6", "0", "100", "0", "0", "0", "1", "1", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", quoted_atom("ru"), "0", "1", ["3", "6", "4", "6", "4", "00000000-0000-0000-0000-000000000000"], "0"],
        "1",
        "1",
        ["1", *actions] if actions else ["0"],
        "1",
        "0",
        "1",
        "0",
        "0",
        "1",
        "0",
        "1",
        "1",
        "0",
        "0",
    ]


def spreadsheet_settings_record() -> list[object]:
    return [
        "8",
        "1",
        "1",
        [quoted_atom("ru"), quoted_atom("ru"), "1", "1", quoted_atom("ru"), quoted_atom("Русский"), quoted_atom("Русский")],
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
        "1",
        "1",
        "0",
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
        [["0"]],
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
        ["3", "3", ["-1"]],
        ["3", "3", ["-3"]],
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


def button_base_info_record(element: ET.Element) -> list[object]:
    base = base_info_record_from_xml(element)
    base[5] = "1"
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
    position: ET.Element | None,
    page_index: int | None = None,
    page_order: int | None = None,
) -> list[object]:
    left = position.get("left", "0") if position is not None else "0"
    top = position.get("top", "0") if position is not None else "0"
    right = position.get("right", "0") if position is not None else "0"
    bottom = position.get("bottom", "0") if position is not None else "0"
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
    if page_index is None or page_order is None:
        return ["8", left, top, right, bottom, "1", *bindings, "0", *dimensions, "0", "0", "0", "1", "0", "0"]
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
        "0",
        "0",
        "0",
        str(page_index),
        str(page_order),
        str(page_order + 1),
        "0",
        "0",
    ]


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
    return '"' + value.replace('"', '""') + '"'
