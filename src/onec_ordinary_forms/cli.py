#!/usr/bin/env python3
"""Prototype ordinary 1C form XML dump and model-driven rebuild."""

from __future__ import annotations

import argparse
import hashlib
import re
import textwrap
import xml.etree.ElementTree as ET
from pathlib import Path
import base64
import json

from onec_ordinary_forms.corpus import build_corpus_report, write_report
from onec_ordinary_forms.formbin import (
    build_form_bin_container,
    file_descriptors,
    pack_form_bin,
    parse_form_bin,
    unpack_form_bin,
)
from onec_ordinary_forms.bracket import write_elem_json_from_bracket
from onec_ordinary_forms.liststream_xml import bracket_text_to_xml, bracket_xml_to_bytes
from onec_ordinary_forms.pipeline import dump_form_bin_to_xml


SCHEMA_VERSION = "0.1"


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


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_text_lossless(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-8", "cp1251"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def decode_text_preserve_bom(data: bytes) -> tuple[str, bool]:
    has_bom = data.startswith(b"\xef\xbb\xbf")
    payload = data[3:] if has_bom else data
    for encoding in ("utf-8", "cp1251"):
        try:
            return payload.decode(encoding), has_bom
        except UnicodeDecodeError:
            continue
    return payload.decode("utf-8", errors="replace"), has_bom


def encode_text_preserve_bom(text: str, has_bom: bool) -> bytes:
    data = text.encode("utf-8")
    return b"\xef\xbb\xbf" + data if has_bom else data


def set_text(parent: ET.Element, tag: str, text: object | None) -> ET.Element:
    child = ET.SubElement(parent, tag)
    child.text = "" if text is None else str(text)
    return child


def unique_in_order(values: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value in seen:
            continue
        seen.add(value)
        result.append(value)
    return result


def extract_quoted_strings(form_text: str) -> list[str]:
    values: list[str] = []
    for match in re.finditer(r'"((?:[^"\\]|\\.)*)"', form_text):
        value = match.group(1).replace('\\"', '"')
        if value:
            values.append(value)
    return unique_in_order(values)


def looks_human_text(value: str) -> bool:
    return any("А" <= ch <= "я" or ch == "ё" or ch == "Ё" for ch in value)


def raw_to_text(value: object) -> str:
    if isinstance(value, list):
        return " ".join(raw_to_text(item) for item in value)
    return str(value)


def clean_token(value: object) -> str:
    text = str(value)
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return text[1:-1].replace('\\"', '"')
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
    return value.replace("\\", "\\\\").replace('"', '\\"')


def page_title(page_data: dict | None) -> str:
    if not isinstance(page_data, dict):
        return ""
    raw = page_data.get("raw") or []
    try:
        return clean_token(raw[1][2][1])
    except (IndexError, TypeError):
        return ""


def item_title(item_data: dict | None) -> str:
    if not isinstance(item_data, dict):
        return ""
    raw = item_data.get("raw") or []
    candidates: list[str] = []

    def walk(value: object) -> None:
        if isinstance(value, list):
            if len(value) >= 3 and value[0] == "1" and value[1] == "1" and isinstance(value[2], list):
                if len(value[2]) >= 2 and clean_token(value[2][0]) == "ru":
                    candidates.append(clean_token(value[2][1]))
            for child in value:
                walk(child)

    walk(raw)
    return candidates[0] if candidates else ""


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
        fields = ("kind", "targetId", "edge", "offset")
        for index, item in enumerate(value):
            name = fields[index] if index < len(fields) else f"value{index + 1}"
            if index < len(fields):
                set_typed_attr(node, name, item)
            else:
                add_raw_value(node, "ExtraValue", item, index=index + 1)
        kind = clean_token(value[0])
        node.set("kindName", ANCHOR_KIND_MAP.get(kind, f"kind{kind}"))
        if len(value) > 1:
            for name, attr_value in describe_target(value[1], current_id, element_index).items():
                if attr_value:
                    node.set(name, attr_value)
        if len(value) > 2:
            edge = clean_token(value[2])
            node.set("side", EDGE_NAME_MAP.get(edge, f"edge{edge}"))
    elif isinstance(value, list):
        node.set("kind", "complex")
        node.set("kindName", "rawList")
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
    node.set("slot", str(slot))
    if tag == "Binding":
        node.set("coordinate", BINDING_SLOT_ROLE.get(slot, f"slot{slot}"))
    elif tag == "DimensionBinding":
        node.set("dimension", DIMENSION_SLOT_ROLE.get(slot, f"slot{slot}"))
        if isinstance(binding, list):
            if binding:
                int_attr(node, "mode", binding[0])
                node.set("modeType", scalar_kind(binding[0]))
                mode = clean_token(binding[0])
                node.set("modeName", DIMENSION_MODE_MAP.get(mode, f"mode{mode}"))
            if len(binding) > 1:
                int_attr(node, "targetId", binding[1])
                node.set("targetIdType", scalar_kind(binding[1]))
                for name, attr_value in describe_target(binding[1], current_id, element_index).items():
                    if attr_value:
                        node.set(name, attr_value)
            if len(binding) > 2:
                int_attr(node, "edge", binding[2])
                node.set("edgeType", scalar_kind(binding[2]))
                edge = clean_token(binding[2])
                node.set("side", EDGE_NAME_MAP.get(edge, f"edge{edge}"))
            for extra_index, extra in enumerate(binding[3:], start=1):
                add_anchor(node, f"Extra{extra_index}", extra, current_id, element_index)
            return
    if isinstance(binding, list):
        if binding:
            int_attr(node, "mode", binding[0])
            node.set("modeType", scalar_kind(binding[0]))
            mode = clean_token(binding[0])
            node.set("modeName", BINDING_MODE_MAP.get(mode, f"mode{mode}"))
        if len(binding) > 1:
            add_anchor(node, "From", binding[1], current_id, element_index)
        if len(binding) > 2:
            add_anchor(node, "To", binding[2], current_id, element_index)
        for extra_index, extra in enumerate(binding[3:], start=1):
            add_anchor(node, f"Extra{extra_index}", extra, current_id, element_index)
    else:
        set_typed_attr(node, "value", binding)


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


def anchor_to_raw(node: ET.Element) -> object:
    if "value" in node.attrib:
        return node.get("value", "")
    values: list[str] = []
    for name in ("kind", "target", "edge", "offset"):
        if name in node.attrib:
            values.append(node.get(name, ""))
    index = 1
    while f"value{index}" in node.attrib:
        values.append(node.get(f"value{index}", ""))
        index += 1
    return values


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
    node = ET.SubElement(parent, "Geometry")
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


def add_items(parent: ET.Element, items: list[dict]) -> None:
    for item in items:
        node = ET.SubElement(parent, "Item")
        for attr in ("name", "type", "page"):
            value = item.get(attr)
            if value is not None:
                node.set(attr, str(value))
        children = item.get("child") or []
        node.set("childCount", str(len(children)))
        if children:
            child_items = ET.SubElement(node, "ChildItems")
            add_items(child_items, children)


def add_semantic_item(
    parent: ET.Element,
    item: dict,
    data: dict,
    raw_key: str,
    element_index: dict[str, dict[str, str]],
    asset_root: Path,
) -> None:
    node = ET.SubElement(parent, str(item.get("type") or "Item"))
    node.set("name", str(item.get("name", "")))
    node.set("rawKey", raw_key)
    if item.get("type") is not None:
        node.set("type", str(item["type"]))
    if item.get("page") is not None:
        node.set("page", str(item["page"]))
    children = item.get("child") or []
    node.set("childCount", str(len(children)))
    item_data = data.get(raw_key)
    if isinstance(item_data, dict) and item_data.get("id") is not None:
        node.set("id", str(item_data["id"]))
    add_ordinary_control_metadata(node, item_data)
    title = item_title(data.get(raw_key))
    if title:
        add_multilang_text(node, "Title", title)
    add_geometry(node, item_data, element_index)
    if str(item.get("type", "")) == "Image":
        add_picture(node, item_data, str(item.get("name", "Picture")), asset_root)
    if str(item.get("type", "")) == "Button":
        action = action_binding(item_data)
        if action:
            action_node = ET.SubElement(node, "Action")
            for key, value in action.items():
                action_node.set(key, value)

    page_names = data.get(f"{raw_key}/-pages-", [])
    if page_names:
        pages = ET.SubElement(node, "Pages")
        for page_name in page_names:
            page_path = f"{raw_key}/{page_name}"
            page = ET.SubElement(pages, "Page")
            page.set("name", str(page_name))
            title = page_title(data.get(page_path))
            if title:
                add_multilang_text(page, "Title", title)
            page_items = [
                child
                for child in children
                if str(child.get("page", "")) == str(page_name)
                or str(child.get("page", "")) == page_path
            ]
            page_items_node = ET.SubElement(page, "Items")
            for child in page_items:
                add_semantic_item(page_items_node, child, data, f"{page_path}/{child.get('name', '')}", element_index, asset_root)
        loose_children = [
            child
            for child in children
            if all(
                str(child.get("page", "")) not in {str(page_name), f"{raw_key}/{page_name}"}
                for page_name in page_names
            )
        ]
        if loose_children:
            child_items = ET.SubElement(node, "ChildItems")
            for child in loose_children:
                child_key = f"{raw_key}/{child.get('name', '')}"
                add_semantic_item(child_items, child, data, child_key, element_index, asset_root)
    elif children:
        child_items = ET.SubElement(node, "ChildItems")
        for child in children:
            child_key = str(child.get("rawKey") or f"{raw_key}/{child.get('name', '')}")
            add_semantic_item(child_items, child, data, child_key, element_index, asset_root)


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
        page.set("rawKey", page_path)
        title = page_title(data.get(page_path))
        if title:
            add_multilang_text(page, "Title", title)
        items = ET.SubElement(page, "Items")
        for item in elem.get("tree", []):
            if str(item.get("page", "")) != page_path:
                continue
            raw_key = str(item.get("rawKey") or f"{page_path}/{item.get('name', '')}")
            add_semantic_item(items, item, data, raw_key, element_index, asset_root)


def add_ordinary_control_metadata(parent: ET.Element, item_data: dict | None) -> None:
    if not isinstance(item_data, dict):
        return
    ordinary = item_data.get("ordinary")
    if not isinstance(ordinary, dict) or not ordinary.get("classId"):
        return
    node = ET.SubElement(parent, "OrdinaryControl")
    for attr in ("classId", "objectId", "declaredChildCount", "actualChildCount", "stateCount", "positionRecordCount"):
        value = ordinary.get(attr)
        if value is not None:
            node.set(attr, str(value))
    state_names = ordinary.get("stateNames")
    if isinstance(state_names, list) and state_names:
        states = ET.SubElement(node, "States")
        for state_name in state_names:
            state = ET.SubElement(states, "State")
            state.set("name", str(state_name))


def add_form_bin_container(root: ET.Element, bin_bytes: bytes, form_bytes: bytes) -> None:
    parts = parse_form_bin(bin_bytes)
    descriptors = file_descriptors(parts)
    container = ET.SubElement(root, "FormBin")
    container.set("format", "onec-ordinary-formbin-container")
    container.set("version", "3")
    container.set("container", "1c-container32")
    container.set("blockSize", "512")

    form_stream = ET.SubElement(container, "LogicalStream")
    form_stream.set("file", "Form.xml")
    form_stream.set("encoding", "base64")
    form_stream.set("size", str(len(form_bytes)))
    form_stream.set("sha256", sha256_bytes(form_bytes))
    form_stream.text = base64.b64encode(form_bytes).decode("ascii")

    form_text, has_bom = decode_text_preserve_bom(form_bytes)
    container.append(bracket_text_to_xml(form_text, has_bom=has_bom))

    files = ET.SubElement(container, "Files")
    for name in ("form", "module"):
        descriptor = descriptors.get(name)
        file_node = ET.SubElement(files, "File")
        file_node.set("name", name)
        if descriptor is not None:
            file_node.set("createdTicks", str(descriptor.created))
            file_node.set("modifiedTicks", str(descriptor.modified))


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

    form_bytes = form_path.read_bytes()
    bin_bytes = bin_path.read_bytes()
    module_bytes = module_path.read_bytes() if module_path and module_path.exists() else b""
    form_text = read_text_lossless(form_path)
    elem = json.loads(elem_path.read_text(encoding="utf-8"))
    metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path else None
    object_types = metadata_object_type_map(metadata)
    element_index = build_element_index(elem)
    quoted_strings = extract_quoted_strings(form_text)
    root_title = str((elem.get("data", {}).get("-pages-") or [""])[0])

    root = ET.Element("OrdinaryForm")
    root.set("schemaVersion", SCHEMA_VERSION)
    root.set("sourceKind", "1C.OrdinaryForm")
    root.set("roundTrip", "object-model")

    source = ET.SubElement(root, "Source")
    source.set("formFile", form_path.name)
    source.set("binFile", bin_path.name)
    source.set("formSha256", sha256_bytes(form_bytes))
    source.set("binSha256", sha256_bytes(bin_bytes))
    source.set("formSize", str(len(form_bytes)))
    source.set("binSize", str(len(bin_bytes)))
    if module_path and module_bytes:
        source.set("moduleFile", module_path.name)
        source.set("moduleSha256", sha256_bytes(module_bytes))
        source.set("moduleSize", str(len(module_bytes)))

    properties = ET.SubElement(root, "Properties")
    if root_title:
        add_multilang_text(properties, "Title", root_title)
    set_text(properties, "BracketFormatVersion", quoted_strings[0] if quoted_strings else "")
    if module_path and module_bytes:
        module_out = asset_root / "Module.bsl"
        module_out.parent.mkdir(parents=True, exist_ok=True)
        module_out.write_bytes(module_bytes)
        module = ET.SubElement(properties, "Module")
        module.set("file", "Module.bsl")
        module.set("sha256", sha256_bytes(module_bytes))
        module.set("size", str(len(module_bytes)))

    attrs = ET.SubElement(root, "Attributes")
    for prop in elem.get("props", []):
        attr = ET.SubElement(attrs, "Attribute")
        attr.set("name", str(prop.get("name", "")))
        attr.set("id", str(prop.get("id", "")))
        pattern_node = pattern_node_from_prop(prop)
        add_type(attr, pattern_node, object_types)

    commands = ET.SubElement(root, "Commands")
    for command in elem.get("commands", []):
        cmd = ET.SubElement(commands, "Command")
        if isinstance(command, dict):
            for key, value in command.items():
                if not isinstance(value, (dict, list)):
                    cmd.set(str(key), str(value))
        else:
            cmd.text = str(command)

    form_structure = ET.SubElement(root, "FormStructure")
    add_semantic_pages(form_structure, elem, element_index, asset_root)
    add_form_bin_container(root, bin_bytes, form_bytes)

    ET.indent(root, space="  ")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(root).write(out_path, encoding="utf-8", xml_declaration=True)


def rebuild(args: argparse.Namespace) -> None:
    xml_path = Path(args.xml)
    out_form = Path(args.out_form)
    out_bin = Path(args.out_bin)
    out_module = Path(args.out_module) if args.out_module else None
    asset_root = Path(args.asset_root) if args.asset_root else xml_path.with_suffix("")

    root = ET.parse(xml_path).getroot()
    if root.find("./RawPayloads") is not None:
        raise ValueError("RawPayloads are not supported by the object-model rebuild")
    for bad in root.findall(".//*[@raw]"):
        raise ValueError(f"Raw attribute is not supported in object model: <{bad.tag}>")

    base_form = Path(args.base_form)
    base_bin = Path(args.base_bin)
    form_data = apply_semantic_edits_to_form(root, base_form.read_bytes())
    bin_data = base_bin.read_bytes()

    out_form.parent.mkdir(parents=True, exist_ok=True)
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    out_form.write_bytes(form_data)
    out_bin.write_bytes(bin_data)
    if out_module:
        module_data = module_data_from_xml(root, asset_root)
        if module_data:
            out_module.parent.mkdir(parents=True, exist_ok=True)
            out_module.write_bytes(module_data)


def module_data_from_xml(root: ET.Element, asset_root: Path) -> bytes:
    module = root.find("./Properties/Module")
    if module is None or not module.get("file"):
        return b""
    module_path = asset_root / module.get("file", "")
    data = module_path.read_bytes()
    expected_hash = module.get("sha256")
    if expected_hash and sha256_bytes(data) != expected_hash:
        raise ValueError(f"Module hash mismatch: {module_path}")
    return data


def embedded_form_stream_from_xml(root: ET.Element) -> bytes:
    bracket_stream = root.find("./FormBin/BracketStream")
    if bracket_stream is not None:
        return bracket_xml_to_bytes(bracket_stream)
    stream = root.find("./FormBin/LogicalStream[@file='Form.xml']")
    if stream is None or stream.get("encoding") != "base64" or not stream.text:
        raise ValueError("OrdinaryForm XML does not contain embedded Form.xml stream")
    data = base64.b64decode("".join(stream.text.split()))
    expected_hash = stream.get("sha256")
    if expected_hash and sha256_bytes(data) != expected_hash:
        raise ValueError("Embedded Form.xml stream hash mismatch")
    return data


def container_times_from_xml(root: ET.Element) -> tuple[int | None, int | None]:
    for file_node in root.findall("./FormBin/Files/File"):
        if file_node.get("name") != "form":
            continue
        created = file_node.get("createdTicks")
        modified = file_node.get("modifiedTicks")
        try:
            return (
                int(created) if created is not None else None,
                int(modified) if modified is not None else None,
            )
        except ValueError:
            return None, None
    return None, None


def build_bin(args: argparse.Namespace) -> None:
    xml_path = Path(args.xml)
    out_bin = Path(args.out_bin)
    asset_root = Path(args.asset_root) if args.asset_root else xml_path.with_suffix("")
    root = ET.parse(xml_path).getroot()
    form_data = apply_semantic_edits_to_form(root, embedded_form_stream_from_xml(root))
    module_data = module_data_from_xml(root, asset_root)
    created, modified = container_times_from_xml(root)
    bin_data = build_form_bin_container(form_data, module_data, created=created, modified=modified)
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    out_bin.write_bytes(bin_data)


def apply_semantic_edits_to_form(root: ET.Element, base_form_data: bytes) -> bytes:
    if root.find(".//RawBracket") is not None or root.find(".//*[@insert='true']") is not None:
        raise ValueError("RawBracket/insert XML edits are not supported; ordinary form elements require typed rebuild")
    form_text, has_bom = decode_text_preserve_bom(base_form_data)
    title = get_multilang_text(root.find("./Properties"), "Title")
    if title:
        form_text = replace_root_title(form_text, title)
    return encode_text_preserve_bom(form_text, has_bom)


def replace_root_title(form_text: str, title: str) -> str:
    pattern = re.compile(r'(\{\s*"ru"\s*,\s*")((?:[^"\\]|\\.)*)("\s*\})', re.MULTILINE)
    new_text, count = pattern.subn(lambda match: match.group(1) + quote_form_string(title) + match.group(3), form_text, count=1)
    if count != 1:
        raise ValueError("Root form title was not found in bracket form stream")
    return new_text


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

    rebuild_parser = subparsers.add_parser("rebuild")
    rebuild_parser.add_argument("--xml", required=True)
    rebuild_parser.add_argument("--base-form", required=True)
    rebuild_parser.add_argument("--base-bin", required=True)
    rebuild_parser.add_argument("--out-form", required=True)
    rebuild_parser.add_argument("--out-bin", required=True)
    rebuild_parser.add_argument("--out-module")
    rebuild_parser.add_argument("--asset-root")
    rebuild_parser.set_defaults(func=rebuild)

    build_bin_parser = subparsers.add_parser("build-bin")
    build_bin_parser.add_argument("--xml", required=True, help="OrdinaryForm XML produced by dump-bin")
    build_bin_parser.add_argument("--out-bin", required=True, help="Rebuilt ordinary form Form.bin")
    build_bin_parser.add_argument("--asset-root", help="Directory with Module.bsl and extracted assets")
    build_bin_parser.set_defaults(func=build_bin)

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
    dump_bin_parser.add_argument("--out", required=True, help="OrdinaryForm XML output path")
    dump_bin_parser.set_defaults(func=dump_bin)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
