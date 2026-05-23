#!/usr/bin/env python3
"""Prototype ordinary 1C form XML dump and model-driven rebuild."""

from __future__ import annotations

import argparse
import hashlib
import importlib.resources
import re
import textwrap
import xml.etree.ElementTree as ET
from pathlib import Path
import base64
import json

from onec_ordinary_forms.corpus import build_corpus_report, write_report
from onec_ordinary_forms.formbin import (
    build_form_bin_container,
    pack_form_bin,
    unpack_form_bin,
)
from onec_ordinary_forms.bracket import write_elem_json_from_bracket
from onec_ordinary_forms.liststream import parse_list_stream_document
from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS, control_descriptor
from onec_ordinary_forms.ordinary_stream import form_stream_from_object_xml
from onec_ordinary_forms.pipeline import dump_form_bin_to_xml


SCHEMA_VERSION = "0.1"
XSI_NS = "http://www.w3.org/2001/XMLSchema-instance"
ORDINARY_FORM_SCHEMA = "ordinary-form.xsd"

ET.register_namespace("xsi", XSI_NS)


TYPE_CODE_MAP = {
    "S": "xs:string",
    "N": "xs:decimal",
    "B": "xs:boolean",
    "D": "xs:dateTime",
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

BINDING_SLOT_ROLE = {
    1: "top",
    2: "bottom",
    3: "left",
    4: "right",
    5: "verticalCenter",
    6: "horizontalCenter",
}

DIMENSION_SLOT_ROLE = {
    1: "height",
    2: "minHeight",
    3: "stretch",
    4: "width",
}

DIMENSION_MODE_MAP = {
    "0": "fixed",
    "1": "auto",
    "2": "bound",
    "20": "stretch",
}

BINDING_MODE_MAP = {
    "0": "edgeToEdge",
    "1": "group",
    "10": "compound",
}

BINDING_COORDINATE_SLOT = {value: key for key, value in BINDING_SLOT_ROLE.items()}
DIMENSION_NAME_SLOT = {value: key for key, value in DIMENSION_SLOT_ROLE.items()}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def set_text(parent: ET.Element, tag: str, text: object | None) -> ET.Element:
    child = ET.SubElement(parent, tag)
    child.text = "" if text is None else str(text)
    return child


def raw_to_text(value: object) -> str:
    if isinstance(value, list):
        return " ".join(raw_to_text(item) for item in value)
    return str(value)


def clean_token(value: object) -> str:
    text = str(value)
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return text[1:-1].replace('""', '"').replace('\\"', '"')
    return text


def scalar_kind(value: object) -> str:
    text = clean_token(value)
    if text in ("true", "false"):
        return "boolean"
    try:
        int(text)
        return "integer"
    except (TypeError, ValueError):
        pass
    try:
        float(text)
        return "number"
    except (TypeError, ValueError):
        return "string"


def is_scalar(value: object) -> bool:
    return not isinstance(value, list)


def set_typed_attr(node: ET.Element, name: str, value: object) -> None:
    node.set(name, clean_token(value))
    node.set(f"{name}Type", scalar_kind(value))


def pattern_node_from_prop(prop: dict) -> list | None:
    raw = prop.get("raw", [])
    for index, item in enumerate(raw):
        if item == '"Pattern"' and index + 1 < len(raw):
            return raw[index + 1] if isinstance(raw[index + 1], list) else [raw[index + 1]]
        if isinstance(item, list) and item and item[0] == '"Pattern"':
            if len(item) == 1:
                return []
            tail = item[1:]
            return tail[0] if len(tail) == 1 and isinstance(tail[0], list) else tail
    return None


def pattern_from_prop(prop: dict) -> str:
    pattern = pattern_node_from_prop(prop)
    return "" if pattern is None else raw_to_text(pattern)


def metadata_object_type_map(metadata: dict | None) -> dict[str, str]:
    if not metadata:
        return {}
    result: dict[str, str] = {}
    name = metadata.get("name")
    header = metadata.get("header", [])
    try:
        object_uuid = header[0][3][1][1][1]
    except (IndexError, TypeError):
        object_uuid = None
    if name and object_uuid:
        result[str(object_uuid)] = f"cfg:ExternalDataProcessorObject.{name}"
    return result


def decoded_pattern_types(pattern: list | None, object_types: dict[str, str]) -> list[dict[str, str]]:
    if pattern is None or len(pattern) == 0:
        return []
    result: list[dict[str, str]] = []
    index = 0
    while index < len(pattern):
        code = clean_token(pattern[index])
        if code == "#":
            uuid = clean_token(pattern[index + 1]) if index + 1 < len(pattern) else ""
            result.append(
                {
                    "code": code,
                    "name": object_types.get(uuid, f"cfg:uuid.{uuid}" if uuid else "cfg:unknown"),
                    "uuid": uuid,
                    "kind": "reference",
                }
            )
            index += 2
            continue
        result.append(
            {
                "code": code,
                "name": TYPE_CODE_MAP.get(code, f"unknown:{code}"),
                "kind": "primitive" if code in TYPE_CODE_MAP else "unknown",
            }
        )
        index += 1
    return result


def add_type(parent: ET.Element, pattern: list | None, object_types: dict[str, str]) -> None:
    type_node = ET.SubElement(parent, "Type")
    decoded = decoded_pattern_types(pattern, object_types)
    if pattern is None:
        type_node.set("source", "missingPattern")
    elif not decoded:
        type_node.set("source", "emptyPattern")
    else:
        type_node.set("source", "TypeDomainPattern")
    if pattern is not None:
        pattern_node = ET.SubElement(type_node, "Pattern")
        pattern_node.set("encoding", "TypeDomainPattern")
        pattern_node.set("itemCount", str(len(decoded)))
        index = 0
        while index < len(pattern):
            code = clean_token(pattern[index])
            item_node = ET.SubElement(pattern_node, "PatternItem")
            item_node.set("code", code)
            if code == "#" and index + 1 < len(pattern):
                uuid = clean_token(pattern[index + 1])
                item_node.set("uuid", uuid)
                item_node.set("typeName", object_types.get(uuid, f"cfg:uuid.{uuid}"))
                index += 2
            else:
                item_node.set("typeName", TYPE_CODE_MAP.get(code, f"unknown:{code}"))
                index += 1

    for item in decoded:
        item_node = ET.SubElement(type_node, "TypeName")
        item_node.set("code", item["code"])
        item_node.set("kind", item["kind"])
        if item.get("uuid"):
            item_node.set("uuid", item["uuid"])
        item_node.text = item["name"]
    if pattern is not None and not decoded:
        item_node = ET.SubElement(type_node, "TypeName")
        item_node.set("kind", "any")
        item_node.set("code", "")
        item_node.text = "xs:anyType"


def add_multilang_text(parent: ET.Element, tag: str, value: str) -> ET.Element:
    node = ET.SubElement(parent, tag)
    item = ET.SubElement(node, "Item")
    item.set("lang", "ru")
    item.text = value
    return node


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


def quote_form_string(value: str) -> str:
    return value.replace('"', '""')


def page_title(page_data: dict | None) -> str:
    if not isinstance(page_data, dict):
        return ""
    raw = page_data.get("raw") or []
    try:
        return clean_token(raw[1][2][1])
    except (IndexError, TypeError):
        return ""


def localized_text_from_record(value: object) -> str:
    if (
        isinstance(value, list)
        and len(value) >= 3
        and clean_token(value[0]) == "1"
        and clean_token(value[1]) == "1"
        and isinstance(value[2], list)
        and len(value[2]) >= 2
        and clean_token(value[2][0]) == "ru"
    ):
        return clean_token(value[2][1])
    return ""


def item_title(item_data: dict | None, control_type: str) -> str:
    if not isinstance(item_data, dict):
        return ""
    raw = item_data.get("raw") or []
    if not isinstance(raw, list):
        return ""
    if len(raw) <= 2 or not isinstance(raw[2], list):
        return first_localized_text_without_base_tooltip(raw)
    info = raw[2]
    if clean_token(info[0]) == "3" and len(info) > 1 and isinstance(info[1], list) and len(info[1]) > 2:
        return localized_text_from_record(info[1][2])
    if clean_token(info[0]) == "1" and len(info) > 1 and isinstance(info[1], list):
        if len(info[1]) > 2:
            title = localized_text_from_record(info[1][2])
            if title:
                return title
        if control_type == "Panel":
            pages = panel_pages_from_raw(raw)
            return pages[0]["title"] if pages else ""
    return first_localized_text_without_base_tooltip(raw)


def first_localized_text_without_base_tooltip(value: object) -> str:
    if not isinstance(value, list):
        return ""
    text = localized_text_from_record(value)
    if text:
        return text
    base = value if len(value) >= 13 and clean_token(value[0]) == "10" else None
    for index, child in enumerate(value):
        if base is not None and index == 12:
            continue
        found = first_localized_text_without_base_tooltip(child)
        if found:
            return found
    return ""


def build_element_index(elem: dict) -> dict[str, dict[str, str]]:
    data = elem.get("data", {})
    result: dict[str, dict[str, str]] = {}
    for path, value in data.items():
        if not isinstance(value, dict) or value.get("id") is None:
            continue
        result[str(value["id"])] = {
            "id": str(value["id"]),
            "name": path.rsplit("/", 1)[-1],
            "path": path,
        }
    return result


def action_binding(item_data: dict | None) -> dict[str, str]:
    if not isinstance(item_data, dict):
        return {}
    raw = item_data.get("raw") or []
    result: dict[str, str] = {}

    def walk(value: object) -> None:
        if result.get("name") and result.get("uuid"):
            return
        if not isinstance(value, list):
            return
        if (
            len(value) >= 3
            and clean_token(value[0]) == "3"
            and isinstance(value[1], str)
            and isinstance(value[2], str)
            and re.fullmatch(
                r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}",
                clean_token(value[2]),
            )
        ):
            result["name"] = clean_token(value[1])
            result["uuid"] = clean_token(value[2])
            return
        if (
            len(value) >= 3
            and isinstance(value[1], str)
            and re.fullmatch(
                r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}",
                value[1],
            )
            and isinstance(value[2], list)
            and len(value[2]) >= 2
            and value[2][0] == "3"
        ):
            result["uuid"] = value[1]
            result["name"] = clean_token(value[2][1])
            title = first_localized_text(value[2])
            if title:
                result["title"] = title
            return
        for index, child in enumerate(value):
            walk(child)

    walk(raw)
    return result


def geometry_from_raw(raw: object) -> dict[str, str]:
    if not isinstance(raw, list) or len(raw) < 5:
        return {}
    try:
        left, top, right, bottom = (int(raw[1]), int(raw[2]), int(raw[3]), int(raw[4]))
    except (TypeError, ValueError):
        return {}
    return {
        "left": str(left),
        "top": str(top),
        "right": str(right),
        "bottom": str(bottom),
        "width": str(right - left),
        "height": str(bottom - top),
    }


def int_attr(node: ET.Element, name: str, value: object) -> None:
    try:
        node.set(name, str(int(value)))
    except (TypeError, ValueError):
        node.set(name, clean_token(value))


def describe_target(target: object, current_id: str, element_index: dict[str, dict[str, str]]) -> dict[str, str]:
    target_id = clean_token(target)
    if target_id == "-1":
        return {"target": "none"}
    if target_id == "0":
        return {"target": "parent"}
    if target_id == current_id:
        entry = element_index.get(target_id, {})
        return {"target": "self", "targetId": target_id, "targetName": entry.get("name", "")}
    entry = element_index.get(target_id)
    if entry:
        return {"target": "element", "targetId": target_id, "targetName": entry["name"]}
    return {"target": "unknown", "targetId": target_id}


def add_raw_value(parent: ET.Element, tag: str, value: object, *, index: int | None = None) -> ET.Element:
    node = ET.SubElement(parent, tag)
    if index is not None:
        node.set("index", str(index))
    if isinstance(value, list):
        node.set("kind", "list")
        node.set("count", str(len(value)))
        for child_index, child in enumerate(value, start=1):
            add_raw_value(node, "Value", child, index=child_index)
    else:
        node.set("kind", scalar_kind(value))
        node.text = clean_token(value)
    return node


def is_simple_anchor(value: object) -> bool:
    return isinstance(value, list) and len(value) >= 4 and all(is_scalar(item) for item in value[:4])


def add_anchor(
    parent: ET.Element,
    tag: str,
    value: object,
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> None:
    node = ET.SubElement(parent, tag)
    if is_simple_anchor(value):
        kind = clean_token(value[0])
        node.set("relation", ANCHOR_KIND_MAP.get(kind, f"kind{kind}"))
        if len(value) > 1:
            for name, attr_value in describe_target(value[1], current_id, element_index).items():
                if attr_value:
                    node.set(name, attr_value)
        if len(value) > 2:
            edge = clean_token(value[2])
            node.set("side", EDGE_NAME_MAP.get(edge, f"edge{edge}"))
        if len(value) > 3:
            node.set("offset", clean_token(value[3]))
        for extra_index, extra in enumerate(value[4:], start=1):
            add_raw_value(node, "ExtraValue", extra, index=extra_index)
    elif isinstance(value, list):
        node.set("relation", "rawList")
        node.set("count", str(len(value)))
        for index, item in enumerate(value, start=1):
            add_raw_value(node, "Value", item, index=index)
    else:
        set_typed_attr(node, "value", value)


def add_binding(
    parent: ET.Element,
    tag: str,
    slot: int,
    binding: object,
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> None:
    node = ET.SubElement(parent, tag)
    if tag == "Binding":
        node.set("coordinate", BINDING_SLOT_ROLE.get(slot, f"slot{slot}"))
    elif tag == "DimensionBinding":
        node.set("dimension", DIMENSION_SLOT_ROLE.get(slot, f"slot{slot}"))
        if isinstance(binding, list):
            if binding:
                int_attr(node, "mode", binding[0])
                mode = clean_token(binding[0])
                node.set("modeName", DIMENSION_MODE_MAP.get(mode, f"mode{mode}"))
            if len(binding) > 1:
                for name, attr_value in describe_target(binding[1], current_id, element_index).items():
                    if attr_value:
                        node.set(name, attr_value)
            if len(binding) > 2:
                edge = clean_token(binding[2])
                node.set("side", EDGE_NAME_MAP.get(edge, f"edge{edge}"))
            for extra_index, extra in enumerate(binding[3:], start=1):
                add_anchor(node, f"Extra{extra_index}", extra, current_id, element_index)
            return
    if isinstance(binding, list):
        if binding:
            int_attr(node, "mode", binding[0])
            mode = clean_token(binding[0])
            node.set("modeName", BINDING_MODE_MAP.get(mode, f"mode{mode}"))
        if len(binding) > 1:
            add_anchor(node, "From", binding[1], current_id, element_index)
        if len(binding) > 2:
            add_anchor(node, "To", binding[2], current_id, element_index)
        for extra_index, extra in enumerate(binding[3:], start=1):
            add_anchor(node, f"Extra{extra_index}", extra, current_id, element_index)
    else:
        node.set("value", clean_token(binding))


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


def add_geometry(
    parent: ET.Element,
    item_data: dict | None,
    element_index: dict[str, dict[str, str]],
) -> None:
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw") or []
    if not isinstance(raw, list) or len(raw) < 4:
        return
    geometry_raw = raw[3]
    geometry = geometry_from_raw(geometry_raw)
    if not geometry:
        return
    current_id = str(item_data.get("id", ""))
    node = ET.SubElement(parent, "Position")
    for key, value in geometry.items():
        node.set(key, value)
    node.set("unit", "form")

    anchors = ET.SubElement(node, "Bindings")
    if isinstance(geometry_raw, list):
        for index, binding in enumerate(geometry_raw[6:12], start=1):
            add_binding(anchors, "Binding", index, binding, current_id, element_index)
        for index, binding in enumerate(geometry_raw[13:17], start=1):
            add_binding(anchors, "DimensionBinding", index, binding, current_id, element_index)


def find_base64_payload(value: object) -> str:
    if isinstance(value, str):
        text = clean_token(value)
        if text.startswith("#base64:"):
            return text[8:]
    if isinstance(value, list):
        for child in value:
            found = find_base64_payload(child)
            if found:
                return found
    return ""


def add_picture(parent: ET.Element, item_data: dict | None, item_name: str, asset_root: Path) -> None:
    if not isinstance(item_data, dict):
        return
    payload = find_base64_payload(item_data.get("raw"))
    if not payload:
        return
    try:
        data = base64.b64decode(payload)
    except Exception:
        data = b""
    ext = "gif" if data.startswith(b"GIF") else "bin"
    rel_path = Path("Items") / item_name / f"Picture.{ext}"
    asset_path = asset_root / rel_path
    asset_path.parent.mkdir(parents=True, exist_ok=True)
    if data:
        asset_path.write_bytes(data)
    node = ET.SubElement(parent, "Picture")
    node.set("file", rel_path.as_posix())
    node.set("size", str(len(data)))
    if data:
        node.set("sha256", sha256_bytes(data))
        if data.startswith(b"GIF"):
            node.set("mime", "image/gif")


def add_semantic_item(
    parent: ET.Element,
    item: dict,
    data: dict,
    raw_key: str,
    element_index: dict[str, dict[str, str]],
    asset_root: Path,
) -> None:
    descriptor = control_descriptor(item.get("type"))
    node = ET.SubElement(parent, descriptor.xml_tag if descriptor is not None else str(item.get("type") or "Item"))
    node.set("name", str(item.get("name", "")))
    children = item.get("child") or []
    item_data = data.get(raw_key)
    if isinstance(item_data, dict) and item_data.get("id") is not None:
        node.set("id", str(item_data["id"]))
    title = item_title(data.get(raw_key), str(item.get("type", "")))
    if title:
        add_multilang_text(node, "Title", title)
    add_tooltip(node, item_data)
    add_data_path(node, item, item_data)
    add_visible(node, item_data)
    add_read_only(node, item, item_data)
    add_text_color(node, item_data)
    add_back_color(node, item_data)
    add_border_color(node, item_data)
    add_font(node, item_data)
    add_geometry(node, item_data, element_index)
    if str(item.get("type", "")) == "Image":
        add_picture(node, item_data, str(item.get("name", "Picture")), asset_root)
    action = action_binding(item_data) if str(item.get("type", "")) in {"Button", "Label"} else {}
    if action:
        action_node = ET.SubElement(node, "Action")
        for key, value in action.items():
            action_node.set(key, value)

    page_names = data.get(f"{raw_key}/-pages-", [])
    parsed_pages = panel_pages_from_raw(item_data.get("raw") if isinstance(item_data, dict) else None)
    page_descriptors = (
        parsed_pages
        if parsed_pages
        else [{"name": str(page_name), "title": page_title(data.get(f"{raw_key}/{page_name}"))} for page_name in page_names]
    )
    if page_descriptors:
        pages = ET.SubElement(node, "Pages")
        placed_child_names: set[str] = set()
        for page_index, page_descriptor in enumerate(page_descriptors):
            page_name = page_descriptor["name"]
            page_path = f"{raw_key}/{page_name}"
            page = ET.SubElement(pages, "Page")
            page.set("name", str(page_name))
            title = page_descriptor.get("title") or page_title(data.get(page_path))
            if title:
                add_multilang_text(page, "Title", title)
            page_items = [
                child
                for child in children
                if child_page_index(data, raw_key, child) == page_index
                or str(child.get("page", "")) == str(page_name)
                or str(child.get("page", "")) == page_path
            ]
            page_items_node = page
            for child in page_items:
                placed_child_names.add(str(child.get("name", "")))
                child_key = str(child.get("rawKey") or f"{page_path}/{child.get('name', '')}")
                add_semantic_item(page_items_node, child, data, child_key, element_index, asset_root)
        loose_children = [
            child
            for child in children
            if str(child.get("name", "")) not in placed_child_names and child_page_index(data, raw_key, child) is None
        ]
        if loose_children:
            for child in loose_children:
                child_key = f"{raw_key}/{child.get('name', '')}"
                add_semantic_item(node, child, data, child_key, element_index, asset_root)
    elif children:
        for child in children:
            child_key = str(child.get("rawKey") or f"{raw_key}/{child.get('name', '')}")
            add_semantic_item(node, child, data, child_key, element_index, asset_root)


DATA_BOUND_CONTROL_TYPES = {
    "InputField",
    "CheckBox",
    "Table",
    "ChoiceField",
    "SpreadsheetDocumentField",
    "RadioButton",
    "Chart",
    "ListBox",
    "HTMLDocumentField",
    "ProgressBar",
    "TrackBar",
    "CalendarField",
    "TextDocumentField",
}


def add_data_path(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) not in DATA_BOUND_CONTROL_TYPES:
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 4 or not isinstance(raw[4], list):
        return
    metadata = raw[4]
    if len(metadata) < 2:
        return
    data_path = clean_token(metadata[1])
    if not data_path:
        return
    set_text(parent, "DataPath", data_path)


def add_visible(parent: ET.Element, item_data: object) -> None:
    base = base_info_from_item_data(item_data)
    if base is None or len(base) <= 1:
        return
    value = clean_token(base[1])
    if value == "0":
        set_text(parent, "Visible", "false")


def add_tooltip(parent: ET.Element, item_data: object) -> None:
    base = base_info_from_item_data(item_data)
    if base is None or len(base) <= 12:
        return
    tooltip = localized_text_from_record(base[12])
    if tooltip:
        add_multilang_text(parent, "ToolTip", tooltip)


def add_read_only(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "InputField":
        return
    input_info = input_field_info_record(item_data)
    if input_info is None or len(input_info) <= 12:
        return
    if clean_token(input_info[12]) == "1":
        set_text(parent, "ReadOnly", "true")


def input_field_info_record(item_data: object) -> list[object] | None:
    if not isinstance(item_data, dict):
        return None
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return None
    info = raw[2]
    if not info or clean_token(info[0]) != "9" or len(info) <= 2 or not isinstance(info[2], list):
        return None
    for candidate in info[2]:
        if isinstance(candidate, list) and candidate and isinstance(candidate[0], list):
            return candidate
    return None


def panel_pages_from_raw(raw: object) -> list[dict[str, str]]:
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return []
    info = raw[2]
    if len(info) <= 1 or not isinstance(info[1], list):
        return []
    for child in info[1]:
        if not isinstance(child, list) or len(child) < 3 or clean_token(child[0]) != "1":
            continue
        try:
            count = int(clean_token(child[1]))
        except ValueError:
            continue
        states = [state for state in child[2:] if isinstance(state, list) and state and clean_token(state[0]) == "3"]
        if count != len(states) or not states:
            continue
        result: list[dict[str, str]] = []
        for state in states:
            name = clean_token(state[6]) if len(state) > 6 else ""
            title = first_localized_text(state)
            if name:
                result.append({"name": name, "title": title or name})
        if result:
            return result
    return []


def first_localized_text(value: object) -> str:
    if isinstance(value, list):
        text = localized_text_from_record(value)
        if text:
            return text
        for child in value:
            found = first_localized_text(child)
            if found:
                return found
    return ""


def child_page_index(data: dict, parent_raw_key: str, child: dict) -> int | None:
    child_key = str(child.get("rawKey") or f"{parent_raw_key}/{child.get('name', '')}")
    child_data = data.get(child_key)
    if not isinstance(child_data, dict):
        return None
    raw = child_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 3 or not isinstance(raw[3], list):
        return None
    geometry = raw[3]
    if len(geometry) < 25:
        return None
    try:
        return int(clean_token(geometry[-5]))
    except ValueError:
        return None


def add_font(parent: ET.Element, item_data: object) -> None:
    base = base_info_from_item_data(item_data)
    if base is None:
        return
    if len(base) <= 4 or not isinstance(base[4], list):
        return
    font = base[4]
    if font == ["6", "3", "0", "1"] or len(font) < 4:
        return
    node = ET.SubElement(parent, "Font")
    node.set("kind", clean_token(font[0]))
    node.set("family", clean_token(font[1]))
    node.set("style", clean_token(font[2]))
    if isinstance(font[3], list):
        for value in font[3]:
            delta = ET.SubElement(node, "Delta")
            delta.text = clean_token(value)
    for index, value in enumerate(font[4:], start=1):
        extra = ET.SubElement(node, "Value")
        extra.set("index", str(index))
        extra.text = clean_token(value)


def add_back_color(parent: ET.Element, item_data: object) -> None:
    add_color(parent, "BackColor", item_data, 3)


def add_text_color(parent: ET.Element, item_data: object) -> None:
    add_color(parent, "TextColor", item_data, 2)


def add_border_color(parent: ET.Element, item_data: object) -> None:
    add_color(parent, "BorderColor", item_data, 6)


def add_color(parent: ET.Element, tag: str, item_data: object, slot: int) -> None:
    base = base_info_from_item_data(item_data)
    if base is None or len(base) <= slot or base[slot] == ["3", "4", ["0"]]:
        return
    value = base[slot]
    if isinstance(value, list) and len(value) >= 3 and isinstance(value[2], list) and value[2]:
        node = ET.SubElement(parent, tag)
        node.text = clean_token(value[2][0])


def base_info_from_item_data(item_data: object) -> list[object] | None:
    if not isinstance(item_data, dict):
        return None
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return None
    return find_base_info_record(raw[2])


def find_base_info_record(value: object) -> list[object] | None:
    if not isinstance(value, list):
        return None
    if len(value) >= 13 and clean_token(value[0]) == "10":
        return value
    for child in value:
        found = find_base_info_record(child)
        if found is not None:
            return found
    return None


def add_semantic_pages(
    parent: ET.Element,
    elem: dict,
    element_index: dict[str, dict[str, str]],
    asset_root: Path,
) -> None:
    data = elem.get("data", {})
    pages = ET.SubElement(parent, "Pages")
    for page_name in data.get("-pages-", []):
        page_path = str(page_name)
        page = ET.SubElement(pages, "Page")
        page.set("name", page_path)
        title = page_title(data.get(page_path))
        if title:
            add_multilang_text(page, "Title", title)
        for item in elem.get("tree", []):
            if str(item.get("page", "")) != page_path:
                continue
            raw_key = str(item.get("rawKey") or f"{page_path}/{item.get('name', '')}")
            add_semantic_item(page, item, data, raw_key, element_index, asset_root)


def dump_xml(args: argparse.Namespace) -> None:
    form_path = Path(args.form)
    bin_path = Path(args.bin)
    module_path = Path(args.module) if args.module else None
    elem_path = Path(args.elem_json)
    out_path = Path(args.out)
    metadata_path = Path(args.metadata_json) if args.metadata_json else None

    dump_xml_from_paths(form_path, bin_path, module_path, elem_path, metadata_path, out_path)


def dump_xml_from_paths(
    form_path: Path,
    bin_path: Path,
    module_path: Path | None,
    elem_path: Path,
    metadata_path: Path | None,
    out_path: Path,
) -> None:
    asset_root = out_path.with_suffix("")

    module_bytes = module_path.read_bytes() if module_path and module_path.exists() else b""
    form_root = parse_list_stream_document(form_path.read_text(encoding="utf-8-sig"), allow_trailing=True).value
    elem = json.loads(elem_path.read_text(encoding="utf-8"))
    metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path else None
    object_types = metadata_object_type_map(metadata)
    element_index = build_element_index(elem)
    root_title = str((elem.get("data", {}).get("-pages-") or [""])[0])

    root = ET.Element("Form")
    root.set("version", SCHEMA_VERSION)
    root.set(f"{{{XSI_NS}}}noNamespaceSchemaLocation", ORDINARY_FORM_SCHEMA)

    if root_title:
        add_multilang_text(root, "Title", root_title)
    add_form_properties(root, form_root)
    if module_path and module_bytes:
        module_out = asset_root / "Module.bsl"
        module_out.parent.mkdir(parents=True, exist_ok=True)
        module_out.write_bytes(module_bytes)

    add_form_events(root, form_root)

    attrs = ET.SubElement(root, "Attributes")
    attribute_slots = attribute_slots_from_form_root(form_root)
    for prop in elem.get("props", []):
        prop_name = str(prop.get("name", ""))
        if re.fullmatch(r"Attribute\d+", prop_name):
            continue
        attr = ET.SubElement(attrs, "Attribute")
        attr.set("name", prop_name)
        attr.set("id", str(prop.get("id", "")))
        if prop_name in attribute_slots:
            attr.set("slot", attribute_slots[prop_name])
        pattern_node = pattern_node_from_prop(prop)
        add_type(attr, pattern_node, object_types)

    add_semantic_pages(root, elem, element_index, asset_root)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_pretty_xml(root, out_path)


def add_form_properties(parent: ET.Element, form_root: object) -> None:
    if not isinstance(form_root, list) or len(form_root) <= 1 or not isinstance(form_root[1], list):
        return
    form_record = form_root[1]
    if len(form_record) <= 4:
        return
    width = clean_token(form_record[3])
    height = clean_token(form_record[4])
    if width.isdigit():
        set_text(parent, "Width", width)
    if height.isdigit():
        set_text(parent, "Height", height)


def add_form_events(parent: ET.Element, form_root: object) -> None:
    if not isinstance(form_root, list) or len(form_root) <= 4 or not isinstance(form_root[4], list):
        return
    event_table = form_root[4]
    if not event_table or clean_token(event_table[0]) not in {"1", str(max(len(event_table) - 1, 0))}:
        return
    events_node = ET.SubElement(parent, "Events")
    for event_record in event_table[1:]:
        event = event_from_record(event_record)
        if event:
            node = ET.SubElement(events_node, "Event")
            node.set("name", event["name"])
            node.set("uuid", event["uuid"])
    if len(events_node) == 0:
        parent.remove(events_node)


def attribute_slots_from_form_root(form_root: object) -> dict[str, str]:
    if not isinstance(form_root, list) or len(form_root) <= 2 or not isinstance(form_root[2], list):
        return {}
    table = form_root[2]
    if len(table) <= 2 or not isinstance(table[2], list):
        return {}
    result: dict[str, str] = {}
    for record in table[2][1:]:
        if not isinstance(record, list) or len(record) < 5:
            continue
        slot_record = record[0]
        if isinstance(slot_record, list) and slot_record:
            result[clean_token(record[4])] = clean_token(slot_record[0])
    return result


def event_from_record(record: object) -> dict[str, str]:
    if not isinstance(record, list) or len(record) < 3:
        return {}
    uuid = clean_token(record[1])
    payload = record[2]
    if not isinstance(payload, list) or len(payload) < 2:
        return {}
    name = clean_token(payload[1])
    if not name or not re.fullmatch(
        r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}",
        uuid,
    ):
        return {}
    return {"name": name, "uuid": uuid}


XML_TEXT_NEWLINE_SENTINEL = "__ONEC_ORDINARY_FORMS_XML_TEXT_LF_8F6F0B2194AA4F0C__"


def _encode_multiline_text_nodes(document: object) -> None:
    for element in document.iter():
        text = getattr(element, "text", None)
        if text and text.strip() and "\n" in text:
            element.text = text.replace("\r\n", "\n").replace("\r", "\n").replace(
                "\n",
                XML_TEXT_NEWLINE_SENTINEL,
            )


def _restore_text_newline_entities(xml: bytes) -> bytes:
    return xml.replace(XML_TEXT_NEWLINE_SENTINEL.encode("ascii"), b"&#10;")


def pretty_xml_bytes(root: ET.Element) -> bytes:
    try:
        from lxml import etree
    except ImportError:
        ET.indent(root, space="  ")
        _encode_multiline_text_nodes(root)
        return _restore_text_newline_entities(
            ET.tostring(root, encoding="utf-8", xml_declaration=True)
        )
    parser = etree.XMLParser(remove_blank_text=True)
    document = etree.fromstring(ET.tostring(root, encoding="utf-8"), parser)
    _encode_multiline_text_nodes(document)
    return _restore_text_newline_entities(
        etree.tostring(
            document,
            encoding="utf-8",
            xml_declaration=True,
            pretty_print=True,
        )
    )


def write_pretty_xml(root: ET.Element, path: Path) -> None:
    path.write_bytes(pretty_xml_bytes(root))


def semantic_model_hash(root: ET.Element) -> str:
    model = ET.Element("SemanticModel")
    for tag in ("Title", "Attributes", "Pages"):
        child = root.find(tag)
        if child is not None:
            model.append(clone_without_blank_text(child))
    return sha256_bytes(ET.tostring(model, encoding="utf-8"))


def clone_without_blank_text(element: ET.Element) -> ET.Element:
    clone = ET.Element(element.tag, element.attrib)
    if element.text and element.text.strip():
        clone.text = normalize_xml_text_for_hash(element.text)
    for child in element:
        clone.append(clone_without_blank_text(child))
    if element.tail and element.tail.strip():
        clone.tail = normalize_xml_text_for_hash(element.tail)
    return clone


def normalize_xml_text_for_hash(value: str) -> str:
    return value.replace("\r\r\n", "\n").replace("\r\n", "\n").replace("\r", "\n")


def schema_path() -> Path:
    return Path(str(importlib.resources.files("onec_ordinary_forms") / "schemas" / ORDINARY_FORM_SCHEMA))


def validate_xml_file(xml_path: Path, xsd_path: Path | None = None) -> None:
    try:
        from lxml import etree
    except ImportError as exc:
        raise RuntimeError("XML schema validation requires lxml") from exc
    schema_doc = etree.parse(str(xsd_path or schema_path()))
    schema = etree.XMLSchema(schema_doc)
    document = etree.parse(str(xml_path))
    schema.assertValid(document)


def validate_xml(args: argparse.Namespace) -> None:
    validate_xml_file(Path(args.xml), Path(args.schema) if args.schema else None)
    print("OK")


def format_xml_file(path: Path) -> None:
    try:
        from lxml import etree
    except ImportError as exc:
        raise RuntimeError("XML formatting requires lxml") from exc
    parser = etree.XMLParser(remove_blank_text=True)
    document = etree.parse(str(path), parser)
    path.write_bytes(etree.tostring(document, encoding="utf-8", xml_declaration=True, pretty_print=True))


def format_xml(args: argparse.Namespace) -> None:
    format_xml_file(Path(args.xml))


def module_data_from_xml(root: ET.Element, asset_root: Path) -> bytes:
    module_path = asset_root / "Module.bsl"
    if not module_path.exists():
        return b""
    return module_path.read_bytes()


def container_times_from_xml(root: ET.Element) -> tuple[int | None, int | None]:
    source = root.find("./Source")
    if source is None:
        return None, None
    created = source.get("formCreatedTicks")
    modified = source.get("formModifiedTicks")
    try:
        return (
            int(created) if created is not None else None,
            int(modified) if modified is not None else None,
        )
    except ValueError:
        return None, None
    return None, None


def form_title_from_xml(root: ET.Element) -> str:
    return get_multilang_text(root, "Title")


def top_level_pages(root: ET.Element) -> list[ET.Element]:
    return root.findall("./Pages/Page")


def item_container(element: ET.Element) -> ET.Element | None:
    return element


def build_bin(args: argparse.Namespace) -> None:
    xml_path = Path(args.xml)
    out_bin = Path(args.out_bin)
    asset_root = Path(args.asset_root) if args.asset_root else xml_path.with_suffix("")
    validate_xml_file(xml_path)
    root = ET.parse(xml_path).getroot()
    form_data = form_stream_from_object_xml(root, asset_root)
    module_data = module_data_from_xml(root, asset_root)
    created, modified = container_times_from_xml(root)
    bin_data = build_form_bin_container(form_data, module_data, created=created, modified=modified)
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    out_bin.write_bytes(bin_data)


def scan_corpus(args: argparse.Namespace) -> None:
    report = build_corpus_report(
        root=Path(args.root),
        name_regex=args.name_regex,
        limit=args.limit,
        exported_root=Path(args.exported_root) if args.exported_root else None,
    )
    write_report(report, Path(args.out_json) if args.out_json else None)


def unpack_bin(args: argparse.Namespace) -> None:
    unpack_form_bin(Path(args.bin), Path(args.out_dir))


def pack_bin(args: argparse.Namespace) -> None:
    pack_form_bin(Path(args.parts_dir), Path(args.out_bin))


def extract_elem_json(args: argparse.Namespace) -> None:
    write_elem_json_from_bracket(Path(args.form), Path(args.out))


def dump_bin(args: argparse.Namespace) -> None:
    dump_form_bin_to_xml(
        Path(args.bin),
        Path(args.out),
        model_xml_writer=dump_xml_from_paths,
        metadata_json=Path(args.metadata_json) if args.metadata_json else None,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    dump_parser = subparsers.add_parser("dump")
    dump_parser.add_argument("--form", required=True)
    dump_parser.add_argument("--bin", required=True)
    dump_parser.add_argument("--module")
    dump_parser.add_argument(
        "--elem-json",
        required=True,
        help="Legacy semantic element index; see docs/elem-json.md",
    )
    dump_parser.add_argument("--metadata-json")
    dump_parser.add_argument("--out", required=True)
    dump_parser.set_defaults(func=dump_xml)

    build_bin_parser = subparsers.add_parser("build-bin")
    build_bin_parser.add_argument("--xml", required=True, help="Form.xml produced by dump-bin")
    build_bin_parser.add_argument("--out-bin", required=True, help="Rebuilt ordinary form Form.bin")
    build_bin_parser.add_argument("--asset-root", help="Directory with Module.bsl and extracted assets")
    build_bin_parser.set_defaults(func=build_bin)

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("--xml", required=True, help="Form.xml to validate")
    validate_parser.add_argument("--schema", help="Override ordinary-form XSD path")
    validate_parser.set_defaults(func=validate_xml)

    format_parser = subparsers.add_parser("format-xml")
    format_parser.add_argument("--xml", required=True, help="XML or XSD file to rewrite with stable pretty formatting")
    format_parser.set_defaults(func=format_xml)

    scan_parser = subparsers.add_parser("scan-corpus")
    scan_parser.add_argument("--root", required=True, help="Directory with .epf/.erf files")
    scan_parser.add_argument("--name-regex", help="Optional regex filter for relative paths")
    scan_parser.add_argument("--limit", type=int, help="Limit selected files after sorting/filtering")
    scan_parser.add_argument("--exported-root", help="Optional ibcmd-exported directory to classify")
    scan_parser.add_argument("--out-json", help="Write JSON report instead of stdout")
    scan_parser.set_defaults(func=scan_corpus)

    unpack_bin_parser = subparsers.add_parser("unpack-bin")
    unpack_bin_parser.add_argument("--bin", required=True, help="Ordinary form Form.bin")
    unpack_bin_parser.add_argument("--out-dir", required=True, help="Directory for Form.xml, Module.bsl, and container metadata")
    unpack_bin_parser.set_defaults(func=unpack_bin)

    pack_bin_parser = subparsers.add_parser("pack-bin")
    pack_bin_parser.add_argument("--parts-dir", required=True, help="Directory created by unpack-bin")
    pack_bin_parser.add_argument("--out-bin", required=True, help="Rebuilt ordinary form Form.bin")
    pack_bin_parser.set_defaults(func=pack_bin)

    extract_elem_parser = subparsers.add_parser("extract-elem-json")
    extract_elem_parser.add_argument("--form", required=True, help="Ordinary form bracket stream, often unpack-bin Form.xml")
    extract_elem_parser.add_argument("--out", required=True, help="elem-json output path")
    extract_elem_parser.set_defaults(func=extract_elem_json)

    dump_bin_parser = subparsers.add_parser("dump-bin")
    dump_bin_parser.add_argument("--bin", required=True, help="Ordinary form Form.bin")
    dump_bin_parser.add_argument("--metadata-json")
    dump_bin_parser.add_argument("--out", required=True, help="Form.xml output path")
    dump_bin_parser.set_defaults(func=dump_bin)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
