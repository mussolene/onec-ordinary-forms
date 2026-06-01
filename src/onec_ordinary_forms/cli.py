#!/usr/bin/env python3
"""Prototype ordinary 1C form XML dump and model-driven rebuild."""

from __future__ import annotations

import argparse
import copy
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
    CONTAINER_INFO_NAME,
    build_form_bin_container,
    pack_form_bin,
    unpack_form_bin,
)
from onec_ordinary_forms.liststream import parse_list_stream_document
from onec_ordinary_forms.ordinary_platform import ORDINARY_CONTROL_CLASS_BY_GUID
from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS, control_descriptor
from onec_ordinary_forms.ordinary_stream import (
    CONTROL_INFO_SLOT_DESCRIPTORS,
    MENU_MODE_BY_CODE,
    ORDINARY_FORM_SHADOW_NAME,
    TABLE_COLUMN_VALUE_PAYLOAD_BY_PATTERN,
    form_stream_from_object_xml,
    root_panel_base_info_record,
    shortcut_record_to_xml_attrs,
)
from onec_ordinary_forms.pipeline import dump_form_bin_to_xml
from onec_ordinary_forms.platform_value_xml import (
    add_color_node_from_record,
    add_font_node_from_record,
    color_decimal_to_rgb,
    is_default_color_record,
)
from onec_ordinary_forms.semantic_digest import semantic_graph, semantic_graph_digest
from onec_ordinary_forms.value_codec import (
    TYPE_CODE_NAMES,
    clean_atom,
    localized_text_from_record as decode_localized_text_record,
    localized_text_item_from_record,
    parse_type_domain_pattern,
)


SCHEMA_VERSION = "0.1"
XSI_NS = "http://www.w3.org/2001/XMLSchema-instance"
ORDINARY_FORM_SCHEMA = "OrdinaryForm.xsd"
PLATFORM_CONFIG_SCHEMA = "PlatformConfigStructure.xsd"
KNOWN_SCHEMAS = (ORDINARY_FORM_SCHEMA, PLATFORM_CONFIG_SCHEMA)

ET.register_namespace("xsi", XSI_NS)


TYPE_CODE_MAP = {key: value for key, value in TYPE_CODE_NAMES.items() if key != "U"}

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
    return clean_atom(value)


def clean_scalar_token(value: object) -> str:
    if isinstance(value, list) and len(value) == 1:
        return clean_scalar_token(value[0])
    return clean_token(value)


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
    return [
        {
            "code": item.code,
            "name": item.type_name,
            "kind": item.kind,
            **({"uuid": item.uuid} if item.uuid else {}),
            **({"digits": item.digits} if item.digits else {}),
            **({"fractionDigits": item.fraction_digits} if item.fraction_digits else {}),
            **({"allowedSign": item.allowed_sign} if item.allowed_sign else {}),
            **({"length": item.length} if item.length else {}),
            **({"allowedLength": item.allowed_length} if item.allowed_length else {}),
            **({"dateParts": item.date_parts} if item.date_parts else {}),
        }
        for item in parse_type_domain_pattern(pattern, object_types)
    ]


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
        for item in decoded:
            code = item["code"]
            item_node = ET.SubElement(pattern_node, "PatternItem")
            item_node.set("code", code)
            item_node.set("typeName", item["name"])
            item_node.set("kind", item["kind"])
            if item.get("uuid"):
                item_node.set("uuid", item["uuid"])
            if item.get("digits"):
                item_node.set("digits", item["digits"])
            if item.get("fractionDigits"):
                item_node.set("fractionDigits", item["fractionDigits"])
            if item.get("allowedSign"):
                item_node.set("allowedSign", item["allowedSign"])
            if item.get("length"):
                item_node.set("length", item["length"])
            if item.get("allowedLength"):
                item_node.set("allowedLength", item["allowedLength"])
            if item.get("dateParts"):
                item_node.set("dateParts", item["dateParts"])
    if pattern is not None and not decoded:
        pattern_node.set("itemCount", "0")


def add_type_from_domain_record(parent: ET.Element, record: object, object_types: dict[str, str]) -> None:
    if not isinstance(record, list) or not record or clean_token(record[0]) != "Pattern":
        return
    if len(record) == 1:
        add_type(parent, [], object_types)
        return
    pattern = record[1]
    add_type(parent, pattern if isinstance(pattern, list) else [pattern], object_types)


def add_multilang_text(parent: ET.Element, tag: str, value: str, *, lang: str = "ru") -> ET.Element:
    node = ET.SubElement(parent, tag)
    item = ET.SubElement(node, "Item")
    item.set("lang", lang)
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
    return page_title_parts(page_data)[1]


def page_title_parts(page_data: dict | None) -> tuple[str, str]:
    if not isinstance(page_data, dict):
        return "ru", ""
    raw = page_data.get("raw") or []
    try:
        item = raw[1][2]
        if isinstance(item, list) and len(item) >= 2:
            lang = clean_token(item[0])
            if lang == "#" or re.match(r"^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$", lang):
                return lang, clean_token(item[1])
            return "ru", clean_token(item[1])
    except (IndexError, TypeError):
        pass
    return "ru", ""


def localized_text_from_record(value: object) -> str:
    return decode_localized_text_record(value)


def localized_text_parts_from_record(value: object) -> tuple[str, str]:
    item = localized_text_item_from_record(value)
    return item if item is not None else ("ru", "")


NO_FALLBACK_TITLE_CONTROL_TYPES = {
    "HTMLDocumentField",
    "Image",
    "InputField",
    "SpreadsheetDocumentField",
    "Table",
}


def item_title(item_data: dict | None, control_type: str) -> str:
    return item_title_parts(item_data, control_type)[1]


def item_title_parts(item_data: dict | None, control_type: str) -> tuple[str, str]:
    if not isinstance(item_data, dict):
        return "ru", ""
    raw = item_data.get("raw") or []
    if not isinstance(raw, list):
        return "ru", ""
    if control_type == "Image" and (len(raw) <= 2 or not isinstance(raw[2], list)):
        if len(raw) > 5 and isinstance(raw[5], list) and len(raw[5]) > 2:
            lang, title = localized_text_parts_from_record(raw[5][2])
            if title:
                return lang, title
        return "ru", ""
    if len(raw) > 2 and isinstance(raw[2], list):
        lang, title = localized_text_parts_from_record(control_title_record(raw, control_type))
        if title:
            return lang, title
        if control_type == "Panel":
            pages = panel_pages_from_raw(raw)
            return (pages[0].get("titleLang", "ru"), pages[0]["title"]) if pages else ("ru", "")
    return "ru", ""


def control_title_record(raw: list[object], control_type: str) -> object:
    if len(raw) <= 2 or not isinstance(raw[2], list):
        return None
    info = raw[2]
    descriptor = CONTROL_INFO_SLOT_DESCRIPTORS.get(control_type)
    if descriptor is None:
        return None
    try:
        title_slot = descriptor.slot_index("Title")
        return nested_list_value(control_info_payload(info, descriptor.info_kind), (title_slot,))
    except KeyError:
        pass
    try:
        inner_slot = descriptor.slot_index("InnerInfo")
    except KeyError:
        return None
    if control_type == "CheckBox":
        return nested_list_value(control_info_payload(info, descriptor.info_kind), (inner_slot, 2))
    if control_type == "RadioButton":
        return nested_list_value(info, (inner_slot, 0, 2))
    return None


def control_info_payload(info: list[object], info_kind: str) -> list[object]:
    if len(info) > 1 and clean_token(info[0]) == info_kind and isinstance(info[1], list):
        return info[1]
    return info


def nested_list_value(value: object, path: tuple[int, ...]) -> object:
    current = value
    for index in path:
        if not isinstance(current, list) or len(current) <= index:
            return None
        current = current[index]
    return current


def first_localized_text_without_base_tooltip(value: object, control_type: str = "") -> str:
    return first_localized_text_parts_without_base_tooltip(value, control_type)[1]


def first_localized_text_parts_without_base_tooltip(value: object, control_type: str = "") -> tuple[str, str]:
    if not isinstance(value, list):
        return "ru", ""
    if event_binding_from_record(value, control_type):
        return "ru", ""
    lang, text = localized_text_parts_from_record(value)
    if text:
        return lang, text
    base = value if len(value) >= 13 and clean_token(value[0]) == "10" else None
    for index, child in enumerate(value):
        if base is not None and index == 12:
            continue
        found_lang, found = first_localized_text_parts_without_base_tooltip(child, control_type)
        if found:
            return found_lang, found
    return "ru", ""


def build_element_index(control_index: dict) -> dict[str, dict[str, str]]:
    data = control_index.get("data", {})
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
            result["id"] = clean_token(value[0])
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


def control_geometry_record(raw: object) -> list[object] | None:
    if not isinstance(raw, list):
        return None
    for child in reversed(raw):
        if isinstance(child, list) and geometry_from_raw(child):
            return child
    return None


def control_metadata_record(raw: object) -> list[object] | None:
    if not isinstance(raw, list):
        return None
    for child in reversed(raw):
        if isinstance(child, list) and len(child) >= 2 and clean_token(child[0]) == "14":
            return child
    return None


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
        node.set("coordinate", BINDING_SLOT_ROLE[slot])
    elif tag == "DimensionBinding":
        if slot in DIMENSION_SLOT_ROLE:
            node.set("dimension", DIMENSION_SLOT_ROLE[slot])
        else:
            node.set("dimension", "extra")
            node.set("extraIndex", str(slot - len(DIMENSION_SLOT_ROLE)))
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
    if not isinstance(raw, list):
        return
    geometry_raw = control_geometry_record(raw)
    if geometry_raw is None:
        return
    geometry = geometry_from_raw(geometry_raw)
    if not geometry:
        return
    current_id = str(item_data.get("id", ""))
    node = ET.SubElement(parent, "Position")
    for key, value in geometry.items():
        node.set(key, value)
    add_layout_flow(node, geometry_raw)

    anchors = ET.SubElement(node, "Bindings")
    if isinstance(geometry_raw, list):
        for index, binding in enumerate(geometry_raw[6:12], start=1):
            add_binding(anchors, "Binding", index, binding, current_id, element_index)
        if add_layout_flagged_dimension_bindings(node, anchors, geometry_raw, current_id, element_index):
            return
        if add_prefixed_flagged_height_width_dimension_bindings(node, anchors, geometry_raw, current_id, element_index):
            return
        if add_flagged_height_width_dimension_bindings(node, anchors, geometry_raw, current_id, element_index):
            return
        if add_flagged_dimension_bindings_with_extra_records(node, anchors, geometry_raw, current_id, element_index):
            return
        if add_inline_segmented_dimension_bindings(node, anchors, geometry_raw, current_id, element_index):
            return
        if add_inline_dual_counted_dimension_bindings(node, anchors, geometry_raw, current_id, element_index):
            return
        if add_inline_counted_dimension_bindings(node, anchors, geometry_raw, current_id, element_index):
            return
        if add_counted_dimension_bindings(node, anchors, geometry_raw, current_id, element_index):
            return
        for index, binding in enumerate(geometry_raw[13:17], start=1):
            add_binding(anchors, "DimensionBinding", index, binding, current_id, element_index)


def add_layout_flagged_dimension_bindings(
    position: ET.Element,
    bindings: ET.Element,
    geometry_raw: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> bool:
    if position.find("LayoutFlow") is None or len(geometry_raw) < 20:
        return False
    try:
        inline_count = int(clean_token(geometry_raw[12]))
    except ValueError:
        inline_count = 0
    inline_end = 13 + inline_count
    if (
        inline_count > 0
        and inline_end <= len(geometry_raw)
        and all(isinstance(record, list) for record in geometry_raw[13:inline_end])
        and not any(isinstance(value, list) for value in geometry_raw[inline_end:])
    ):
        return False
    if not (
        clean_token(geometry_raw[12]) == "1"
        and isinstance(geometry_raw[13], list)
        and clean_token(geometry_raw[14]) == "0"
    ):
        if not (
            len(geometry_raw) >= 22
            and clean_token(geometry_raw[12]) == "0"
            and clean_token(geometry_raw[13]) == "1"
            and isinstance(geometry_raw[14], list)
        ):
            return False
        tail_values = geometry_raw[15:]
        if any(isinstance(value, list) for value in tail_values):
            return False
        add_binding(bindings, "DimensionBinding", 1, geometry_raw[14], current_id, element_index)
        return True
    if len(geometry_raw) >= 22 and clean_token(geometry_raw[15]) == "1" and isinstance(geometry_raw[16], list):
        tail_values = geometry_raw[17:]
        if any(isinstance(value, list) for value in tail_values):
            return False
        add_binding(bindings, "DimensionBinding", 1, geometry_raw[13], current_id, element_index)
        add_binding(bindings, "DimensionBinding", 4, geometry_raw[16], current_id, element_index)
        return True
    tail_values = geometry_raw[14:]
    if any(isinstance(value, list) for value in tail_values):
        return False
    add_binding(bindings, "DimensionBinding", 1, geometry_raw[13], current_id, element_index)
    return True


def add_flagged_dimension_bindings_with_extra_records(
    position: ET.Element,
    bindings: ET.Element,
    geometry_raw: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> bool:
    if len(geometry_raw) < 23:
        return False
    if isinstance(geometry_raw[12], list):
        return False
    cursor = 17
    extra_records: list[object] = []
    while cursor < len(geometry_raw) and isinstance(geometry_raw[cursor], list):
        extra_records.append(geometry_raw[cursor])
        cursor += 1
    if not extra_records:
        return False
    tail_values = geometry_raw[cursor:]
    if not tail_values or any(isinstance(value, list) for value in tail_values):
        return False
    for index, binding in enumerate(geometry_raw[13:17], start=1):
        add_binding(bindings, "DimensionBinding", index, binding, current_id, element_index)
        bindings[-1].set("section", "primary")
    for index, binding in enumerate(extra_records, start=5):
        add_binding(bindings, "DimensionBinding", index, binding, current_id, element_index)
        bindings[-1].set("section", "extra")
    return True


def add_counted_dimension_bindings(
    position: ET.Element,
    bindings: ET.Element,
    geometry_raw: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> bool:
    if len(geometry_raw) < 15:
        return False
    try:
        primary_count = int(clean_token(geometry_raw[13]))
    except ValueError:
        return False
    primary_start = 14
    primary_end = primary_start + primary_count
    if primary_count <= 0 or primary_end >= len(geometry_raw):
        return False
    if not all(isinstance(record, list) for record in geometry_raw[primary_start:primary_end]):
        return False
    try:
        secondary_count = int(clean_token(geometry_raw[primary_end + 1]))
    except (IndexError, ValueError):
        return False
    secondary_start = primary_end + 2
    secondary_end = secondary_start + secondary_count
    if secondary_end > len(geometry_raw):
        return False
    if secondary_count > 0 and not all(isinstance(record, list) for record in geometry_raw[secondary_start:secondary_end]):
        return False
    for index, binding in enumerate(geometry_raw[primary_start:primary_end], start=1):
        add_binding(bindings, "DimensionBinding", index, binding, current_id, element_index)
        bindings[-1].set("section", "primary")
    for index, binding in enumerate(geometry_raw[secondary_start:secondary_end], start=1):
        add_binding(bindings, "DimensionBinding", index, binding, current_id, element_index)
        bindings[-1].set("section", "secondary")
    return True


def add_flagged_height_width_dimension_bindings(
    position: ET.Element,
    bindings: ET.Element,
    geometry_raw: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> bool:
    if len(geometry_raw) < 20:
        return False
    if not (
        clean_token(geometry_raw[12]) == "1"
        and isinstance(geometry_raw[13], list)
        and clean_token(geometry_raw[14]) == "0"
        and clean_token(geometry_raw[15]) == "0"
        and clean_token(geometry_raw[16]) == "1"
        and isinstance(geometry_raw[17], list)
        and clean_token(geometry_raw[18]) == "0"
        and clean_token(geometry_raw[19]) == "0"
    ):
        return False
    tail_values = geometry_raw[20:]
    if any(isinstance(value, list) for value in tail_values):
        return False
    add_binding(bindings, "DimensionBinding", 1, geometry_raw[13], current_id, element_index)
    add_binding(bindings, "DimensionBinding", 4, geometry_raw[17], current_id, element_index)
    return True


def add_prefixed_flagged_height_width_dimension_bindings(
    position: ET.Element,
    bindings: ET.Element,
    geometry_raw: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> bool:
    if len(geometry_raw) < 17:
        return False
    if not (
        not isinstance(geometry_raw[12], list)
        and clean_token(geometry_raw[13]) == "1"
        and isinstance(geometry_raw[14], list)
        and clean_token(geometry_raw[15]) == "1"
        and isinstance(geometry_raw[16], list)
    ):
        return False
    tail_values = geometry_raw[17:]
    if any(isinstance(value, list) for value in tail_values):
        return False
    add_binding(bindings, "DimensionBinding", 1, geometry_raw[14], current_id, element_index)
    add_binding(bindings, "DimensionBinding", 4, geometry_raw[16], current_id, element_index)
    return True


def add_inline_counted_dimension_bindings(
    position: ET.Element,
    bindings: ET.Element,
    geometry_raw: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> bool:
    if len(geometry_raw) < 14:
        return False
    try:
        count = int(clean_token(geometry_raw[12]))
    except ValueError:
        return False
    if count <= 0:
        return False
    start = 13
    end = start + count
    if end > len(geometry_raw):
        return False
    records = geometry_raw[start:end]
    if any(not isinstance(record, list) for record in records):
        return False
    tail_values = geometry_raw[end:]
    if any(isinstance(value, list) for value in tail_values):
        return False
    for index, binding in enumerate(records, start=1):
        add_binding(bindings, "DimensionBinding", index, binding, current_id, element_index)
        bindings[-1].set("section", "primary")
    return True


def add_inline_segmented_dimension_bindings(
    position: ET.Element,
    bindings: ET.Element,
    geometry_raw: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> bool:
    if len(geometry_raw) < 16:
        return False
    try:
        count = int(clean_token(geometry_raw[12]))
    except ValueError:
        return False
    if count <= 0:
        return False
    cursor = 13
    first_end = cursor + count
    if first_end > len(geometry_raw) or not all(isinstance(record, list) for record in geometry_raw[cursor:first_end]):
        return False
    segments: list[tuple[str, list[object]]] = [("", geometry_raw[cursor:first_end])]
    cursor = first_end
    while cursor < len(geometry_raw):
        marker = clean_token(geometry_raw[cursor])
        if isinstance(geometry_raw[cursor], list):
            return False
        try:
            marker_count = int(marker)
        except ValueError:
            break
        if cursor + 1 < len(geometry_raw) and isinstance(geometry_raw[cursor + 1], list):
            count = marker_count
            marker = ""
            cursor += 1
        elif cursor + 2 < len(geometry_raw) and isinstance(geometry_raw[cursor + 2], list):
            try:
                count = int(clean_token(geometry_raw[cursor + 1]))
            except ValueError:
                break
            cursor += 2
        else:
            break
        end = cursor + count
        if count <= 0 or end > len(geometry_raw):
            return False
        records = geometry_raw[cursor:end]
        if not all(isinstance(record, list) for record in records):
            return False
        segments.append((marker, records))
        cursor = end
    if len(segments) <= 1:
        return False
    tail_values = geometry_raw[cursor:]
    if any(isinstance(value, list) for value in tail_values):
        return False
    for segment_index, (_marker, records) in enumerate(segments):
        section = "primary" if segment_index == 0 else f"segment{segment_index + 1}"
        for index, binding in enumerate(records, start=1):
            add_binding(bindings, "DimensionBinding", index, binding, current_id, element_index)
            bindings[-1].set("section", section)
    return True


def add_inline_dual_counted_dimension_bindings(
    position: ET.Element,
    bindings: ET.Element,
    geometry_raw: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> bool:
    if len(geometry_raw) < 17:
        return False
    try:
        primary_count = int(clean_token(geometry_raw[12]))
    except ValueError:
        return False
    if primary_count <= 0:
        return False
    primary_start = 13
    primary_end = primary_start + primary_count
    if primary_end + 1 >= len(geometry_raw):
        return False
    primary = geometry_raw[primary_start:primary_end]
    if not all(isinstance(record, list) for record in primary):
        return False
    try:
        secondary_count = int(clean_token(geometry_raw[primary_end + 1]))
    except ValueError:
        return False
    secondary_start = primary_end + 2
    secondary_end = secondary_start + secondary_count
    if secondary_count <= 0 or secondary_end > len(geometry_raw):
        return False
    secondary = geometry_raw[secondary_start:secondary_end]
    if not all(isinstance(record, list) for record in secondary):
        return False
    tail_values = geometry_raw[secondary_end:]
    if any(isinstance(value, list) for value in tail_values):
        return False
    for index, binding in enumerate(primary, start=1):
        add_binding(bindings, "DimensionBinding", index, binding, current_id, element_index)
        bindings[-1].set("section", "primary")
    for index, binding in enumerate(secondary, start=1):
        add_binding(bindings, "DimensionBinding", index, binding, current_id, element_index)
        bindings[-1].set("section", "secondary")
    return True


def add_layout_flow(node: ET.Element, geometry_raw: list[object]) -> None:
    if len(geometry_raw) < 5:
        return
    flow = ET.SubElement(node, "LayoutFlow")
    if len(geometry_raw) > 5 and clean_token(geometry_raw[5]) != "1":
        flow.set("placementMode", clean_token(geometry_raw[5]))
    if any(isinstance(value, list) for value in geometry_raw[-5:]):
        if not flow.attrib:
            node.remove(flow)
        return
    group, order, next_order, flag1, flag2 = [clean_token(value) for value in geometry_raw[-5:]]
    flow.set("group", group)
    flow.set("order", order)
    if flag1 != "0":
        flow.set("horizontalBoundary", xml_bool_from_platform_flag(flag1))
    if flag2 != "0":
        flow.set("verticalBoundary", xml_bool_from_platform_flag(flag2))
    try:
        if int(next_order) != int(order) + 1:
            flow.set("nextOrder", next_order)
    except ValueError:
        flow.set("nextOrder", next_order)
    if not flow.attrib:
        node.remove(flow)


def xml_bool_from_platform_flag(value: str) -> str:
    return "true" if value not in {"", "0", "false", "False"} else "false"


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
    add_picture_node_from_payload(parent, payload, item_name, asset_root)


def add_picture_node_from_payload(parent: ET.Element, payload: str, item_name: str, asset_root: Path) -> None:
    try:
        data = base64.b64decode(payload)
    except Exception:
        data = b""
    if data.startswith(b"GIF"):
        ext = "gif"
        mime = "image/gif"
    elif data.startswith(b"\x89PNG\r\n\x1a\n"):
        ext = "png"
        mime = "image/png"
    else:
        ext = "bin"
        mime = ""
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
        if mime:
            node.set("mime", mime)


def add_button_picture(parent: ET.Element, item: dict, item_data: object, asset_root: Path) -> None:
    if str(item.get("type", "")) != "Button":
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2]
    if len(info) <= 1 or not isinstance(info[1], list):
        return
    button_info = info[1]
    if len(button_info) <= 8:
        return
    payload = find_base64_payload(button_info[8])
    if payload:
        add_picture_node_from_payload(parent, payload, str(item.get("name", "Button")), asset_root)


def add_label_picture(parent: ET.Element, item: dict, item_data: object, asset_root: Path) -> None:
    if str(item.get("type", "")) != "Label":
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2]
    if len(info) <= 1 or not isinstance(info[1], list):
        return
    label_info = info[1]
    if len(label_info) <= 12 or not isinstance(label_info[12], list):
        return
    payload = find_base64_payload(label_info[12])
    if payload:
        add_picture_node_from_payload(parent, payload, str(item.get("name", "Label")), asset_root)


def add_activex_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "ActiveXControl":
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2]
    if len(info) > 1:
        set_text(parent, "Clsid", clean_token(info[1]))


def add_semantic_item(
    parent: ET.Element,
    item: dict,
    data: dict,
    raw_key: str,
    element_index: dict[str, dict[str, str]],
    asset_root: Path,
) -> None:
    item_type = str(item.get("type") or "")
    public_type = "Panel" if item_type == "Item" else item_type
    descriptor = control_descriptor(public_type)
    node = ET.SubElement(parent, descriptor.xml_tag if descriptor is not None else str(item.get("type") or "Item"))
    node.set("name", str(item.get("name", "")))
    children = item.get("child") or []
    item_data = data.get(raw_key)
    if not isinstance(item_data, dict) and isinstance(item.get("raw"), list):
        item_data = item
    if isinstance(item_data, dict) and item_data.get("id") is not None:
        node.set("id", str(item_data["id"]))
    title_lang, title = item_title_parts(item_data, public_type)
    if title:
        add_multilang_text(node, "Title", title, lang=title_lang)
    add_tooltip(node, item_data)
    add_data_path(node, {**item, "type": public_type}, item_data)
    add_first_in_group(node, item, item_data)
    add_radio_button_properties(node, public_type, item_data)
    add_visible(node, item_data)
    add_enabled(node, item_data)
    add_read_only(node, item, item_data)
    add_input_field_properties(node, item, item_data)
    add_choice_field_properties(node, item, item_data)
    add_table_view_properties(node, item, item_data)
    add_table_columns(node, item, item_data, asset_root)
    add_chart_properties(node, item, item_data)
    add_pivot_chart_properties(node, item, item_data)
    add_geographical_schema_properties(node, item, item_data)
    add_progress_bar_properties(node, public_type, item_data)
    add_activex_properties(node, item, item_data)
    add_button_picture(node, item, item_data, asset_root)
    add_label_properties(node, item, item_data)
    add_label_picture(node, item, item_data, asset_root)
    add_button_style_properties(node, item, item_data)
    add_button_scalar_properties(node, item, item_data, asset_root)
    add_default_action(node, public_type, item_data)
    add_text_color(node, item_data)
    add_back_color(node, item_data)
    add_border_color(node, item_data)
    add_base_style_attributes(node, item_data)
    add_font(node, item_data)
    add_geometry(node, item_data, element_index)
    add_panel_layout(node, public_type, item_data)
    add_command_bar_command_source(node, public_type, item_data, asset_root)
    add_picture_decoration_picture_style(node, public_type, item_data)
    if public_type == "Image":
        add_picture(node, item_data, str(item.get("name", "Picture")), asset_root)
        add_picture_decoration_properties(node, item_data)
    action = action_binding(item_data) if public_type in {"Button", "Label", "ChoiceField"} else {}
    if action:
        action_node = ET.SubElement(node, "Action")
        for key, value in action.items():
            action_node.set(key, value)
    add_control_events(node, public_type, item_data)

    page_names = data.get(f"{raw_key}/-pages-", [])
    parsed_pages = panel_pages_from_raw(item_data.get("raw") if isinstance(item_data, dict) else None)
    page_descriptors = (
        parsed_pages
        if parsed_pages
        else [{"name": str(page_name), "title": page_title(data.get(f"{raw_key}/{page_name}"))} for page_name in page_names]
    )
    if page_descriptors:
        pages = ET.SubElement(node, "Pages")
        placed_child_keys: set[str] = set()

        def child_key_for_page(child: dict) -> str:
            return str(child.get("rawKey") or f"{raw_key}/{child.get('name', '')}")

        def child_belongs_to_page(child: dict, page_index: int, page_name: object, page_path: str) -> bool:
            geometry_page_index = child_page_index(data, raw_key, child)
            if geometry_page_index is not None:
                return geometry_page_index == page_index
            return str(child.get("page", "")) == str(page_name) or str(child.get("page", "")) == page_path

        for page_index, page_descriptor in enumerate(page_descriptors):
            page_name = page_descriptor["name"]
            page_path = f"{raw_key}/{page_name}"
            page = ET.SubElement(pages, "Page")
            page.set("name", str(page_name))
            if page_descriptor.get("styleMode") is not None:
                page.set("styleMode", str(page_descriptor["styleMode"]))
            title_lang, fallback_title = page_title_parts(data.get(page_path))
            title = page_descriptor.get("title") or fallback_title
            if title:
                add_multilang_text(page, "Title", title, lang=title_lang)
            page_items = [
                child
                for child in children
                if child_belongs_to_page(child, page_index, page_name, page_path)
            ]
            page_items_node = page
            for child in page_items:
                child_key = child_key_for_page(child)
                placed_child_keys.add(child_key)
                add_semantic_item(page_items_node, child, data, child_key, element_index, asset_root)
        loose_children = [
            child
            for child in children
            if child_key_for_page(child) not in placed_child_keys
        ]
        if loose_children:
            for child in loose_children:
                child_key = child_key_for_page(child)
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
    "PivotChart",
    "GeographicalSchemaField",
    "GraphicalSchemaField",
    "ListBox",
    "HTMLDocumentField",
    "ProgressBar",
    "TrackBar",
    "CalendarField",
    "TextDocumentField",
    "GanttChart",
    "Dendrogram",
}


def add_data_path(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) not in DATA_BOUND_CONTROL_TYPES:
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list):
        return
    metadata = control_metadata_record(raw)
    if metadata is None:
        return
    if len(metadata) < 2:
        return
    data_path = clean_token(metadata[1])
    if not data_path:
        return
    set_text(parent, "DataPath", data_path)


PANEL_LAYOUT_DIMENSION_NAMES = {
    "0": "top",
    "1": "bottom",
    "2": "left",
    "3": "right",
}


def add_panel_layout(parent: ET.Element, public_type: str, item_data: object) -> None:
    if public_type != "Panel" or not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2][1] if len(raw[2]) > 1 and isinstance(raw[2][1], list) else None
    if not isinstance(info, list) or len(info) < 3 or clean_token(info[1]) != "26":
        return
    dependencies, cursor = panel_dependency_groups_from_info(info)
    page_layouts = panel_page_layouts_from_info(info, cursor)
    page_capacity = len(page_layouts)
    if not dependencies and not page_layouts:
        return
    layout_node = ET.SubElement(parent, "PanelLayout")
    if page_capacity:
        layout_node.set("pageCapacity", str(page_capacity))
    page_state_flag, current_page_index = panel_page_state_scalars(info, cursor)
    if page_state_flag:
        layout_node.set("pageStateFlag", page_state_flag)
    if current_page_index:
        layout_node.set("currentPageIndex", current_page_index)
    add_layout_dependency_group_nodes(layout_node, dependencies)
    for layout in page_layouts:
        page = ET.SubElement(layout_node, "PageLayout")
        for key, value in layout.items():
            page.set(key, value)


def add_layout_dependency_group_nodes(parent: ET.Element, descriptors: list[dict[str, object]]) -> None:
    order = 1
    for descriptor in descriptors:
        for value in descriptor.get("prefix", []):
            if clean_token(value) == "0":
                group = ET.SubElement(parent, "LayoutDependencyGroup")
                group.set("order", str(order))
                order += 1
        records = [
            record
            for record in descriptor.get("records", [])
            if isinstance(record, list) and len(record) >= 3
        ]
        group = ET.SubElement(parent, "LayoutDependencyGroup")
        group.set("order", str(order))
        order += 1
        for record in records:
            dependency = ET.SubElement(group, "LayoutDependency")
            dependency.set("targetId", clean_token(record[1]))
            dimension = clean_token(record[2])
            dependency.set("dimension", PANEL_LAYOUT_DIMENSION_NAMES.get(dimension, f"dimension{dimension}"))


def add_command_bar_command_source(parent: ET.Element, public_type: str, item_data: object, asset_root: Path) -> None:
    if public_type != "CommandBar" or not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2][1] if len(raw[2]) > 1 and isinstance(raw[2][1], list) else None
    if not isinstance(info, list) or len(info) <= 10:
        return
    items = info[7] if len(info) > 7 and isinstance(info[7], list) else None
    if not isinstance(items, list) or len(items) < 2:
        return
    source = ET.SubElement(parent, "CommandSource")
    source.set("rootUuid", clean_token(items[1]))
    if len(items) > 2:
        source.set("rootKind", clean_token(items[2]))
    for source_index, attr_name in (
        (3, "actionPlacement"),
        (4, "actionAlignment"),
        (5, "sourceMode"),
    ):
        if len(info) > source_index:
            source.set(attr_name, clean_token(info[source_index]))
    base = info[0] if info and isinstance(info[0], list) else None
    if isinstance(base, list):
        presentation = base[11] if len(base) > 11 and isinstance(base[11], list) else None
        if isinstance(presentation, list):
            if len(presentation) > 3:
                source.set("presentationScope", clean_token(presentation[3]))
            if len(presentation) > 4:
                source.set("presentationScopeEnabled", clean_token(presentation[4]))
            if len(presentation) > 6:
                source.set("presentationScopeUuid", clean_token(presentation[6]))
        for source_index, attr_name in (
            (16, "buttonPanelMode"),
            (17, "buttonPanelState"),
            (18, "buttonPanelVisible"),
            (19, "buttonPanelDefaultMode"),
        ):
            if len(base) > source_index:
                source.set(attr_name, clean_token(base[source_index]))
    metadata = raw[4] if len(raw) > 4 and isinstance(raw[4], list) else None
    if isinstance(metadata, list) and len(metadata) > 2:
        source.set("metadataScope", clean_token(metadata[2]))
    if not command_bar_items_have_button_model(items):
        branch = items[9] if len(items) > 9 and isinstance(items[9], list) else items[6] if len(items) > 6 and isinstance(items[6], list) else None
        if isinstance(branch, list):
            if len(branch) > 1:
                source.set("branchUuid", clean_token(branch[1]))
            if len(branch) > 3:
                source.set("branchKind", clean_token(branch[3]))
            if len(branch) > 4:
                source.set("branchMode", clean_token(branch[4]))
        action = items[7] if len(items) > 7 and isinstance(items[7], list) else None
        if isinstance(action, list):
            if len(action) > 1:
                source.set("actionUuid", clean_token(action[1]))
            if len(action) > 3:
                source.set("actionTargetUuid", clean_token(action[3]))
            if len(action) > 6:
                source.set("actionMode", clean_token(action[6]))
        if len(items) > 9 and isinstance(items[9], list):
            children = items[9]
            if len(children) > 5:
                source.set("defaultActionUuid", clean_token(children[5]))
        if len(items) > 6 and isinstance(items[6], list) and len(items[6]) > 1:
            source.set("closeUuid", clean_token(items[6][1]))
        if len(items) > 5 and isinstance(items[5], list) and len(items[5]) > 1:
            source.set("separatorUuid", clean_token(items[5][1]))
    if not source.attrib:
        parent.remove(source)
    add_command_bar_buttons(parent, items, asset_root, parent.get("name") or "CommandBar")


def command_bar_items_have_button_model(items: list[object]) -> bool:
    if len(items) < 5 or clean_token(items[0]) != "5":
        return False
    action_count = safe_int_token(items[4])
    if action_count < 0:
        return False
    groups_index = 5 + action_count
    if len(items) <= groups_index:
        return False
    group_count = safe_int_token(items[groups_index])
    return group_count >= 0 and len(items) >= groups_index + 1 + group_count


def add_picture_decoration_picture_style(parent: ET.Element, public_type: str, item_data: object) -> None:
    if public_type != "Image":
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2][1] if len(raw[2]) > 1 and isinstance(raw[2][1], list) else None
    if not isinstance(info, list) or not info:
        return
    base = info[0] if isinstance(info[0], list) else None
    picture_style = info[4] if len(info) > 4 and isinstance(info[4], list) else None
    if base is None and picture_style is None:
        return
    picture_style_node = ET.SubElement(parent, "PictureStyle")
    if len(info) > 2 and (clean_token(info[2]) != "0" or (picture_style and clean_token(picture_style[0]) == "10")):
        picture_style_node.set("displayMode", clean_token(info[2]))
    if len(info) > 3 and (clean_token(info[3]) != "0" or (picture_style and clean_token(picture_style[0]) == "10")):
        picture_style_node.set("displayState", clean_token(info[3]))
    if isinstance(picture_style, list):
        style_kind = clean_token(picture_style[0])
        if style_kind != "10":
            picture_style_node.set("groupKind", style_kind)
        if len(picture_style) > 1 and (clean_token(picture_style[1]) != "0" or style_kind == "10"):
            picture_style_node.set("mode", clean_token(picture_style[1]))
    if len(info) > 13 and clean_token(info[13]) != "0":
        picture_style_node.set("renderingFlag", clean_token(info[13]))
    if base is not None and len(base) > 19:
        style = ET.SubElement(picture_style_node, "BaseStyle")
        style.set("mode", clean_token(base[16]))
        style.set("state", clean_token(base[17]))
        style.set("visible", clean_token(base[18]))
        style.set("defaultMode", clean_token(base[19]))
    if not picture_style_node.attrib and len(picture_style_node) == 0:
        parent.remove(picture_style_node)


def add_command_bar_buttons(
    parent: ET.Element,
    items: list[object],
    asset_root: Path | None = None,
    owner_name: str = "CommandBar",
) -> None:
    if len(items) < 5 or clean_token(items[0]) != "5":
        return
    action_count = safe_int_token(items[4])
    if action_count < 0:
        return
    actions_start = 5
    actions_end = actions_start + action_count
    if len(items) < actions_end + 1:
        return
    group_count = safe_int_token(items[actions_end])
    if group_count < 0:
        return
    groups_start = actions_end + 1
    groups = [group for group in items[groups_start : groups_start + group_count] if isinstance(group, list)]
    if not action_count and not groups:
        return
    buttons = ET.SubElement(parent, "Buttons")
    buttons.set("rootUuid", clean_token(items[1]))
    buttons.set("rootKind", clean_token(items[2]))
    buttons.set("rootFlag", clean_token(items[3]))
    actions_node = ET.SubElement(buttons, "Actions")
    for order, action in enumerate(items[actions_start:actions_end], start=1):
        if not isinstance(action, list) or len(action) < 8 or clean_token(action[0]) not in {"7", "8"}:
            continue
        action_node = ET.SubElement(actions_node, "Action")
        action_node.set("order", str(order))
        action_node.set("recordKind", clean_token(action[0]))
        action_node.set("uuid", clean_token(action[1]))
        action_node.set("enabled", clean_token(action[2]))
        action_node.set("eventUuid", clean_token(action[3]))
        handler, title = command_bar_action_handler_and_title(action[4])
        if handler:
            action_node.set("handler", handler)
        if title:
            action_node.set("title", title)
        add_command_bar_action_payload_fields(action_node, action[4])
        add_command_bar_action_extra_fields(
            action_node,
            action[5:],
            asset_root,
            f"{owner_name}/Actions/{clean_token(action[1]) or order}",
        )
    groups_node = ET.SubElement(buttons, "Groups")
    for order, group in enumerate(groups, start=1):
        if len(group) < 6 or clean_token(group[0]) != "5":
            continue
        group_node = ET.SubElement(groups_node, "Group")
        group_node.set("order", str(order))
        group_node.set("uuid", clean_token(group[1]))
        group_node.set("kind", clean_token(group[2]))
        group_node.set("mode", clean_token(group[3]))
        button_count = safe_int_token(group[4])
        group_node.set("buttonCount", str(max(button_count, 0)))
        cursor = 5
        for button_order in range(max(button_count, 0)):
            if cursor + 1 >= len(group):
                break
            action_uuid = clean_token(group[cursor])
            descriptor = group[cursor + 1] if isinstance(group[cursor + 1], list) else []
            cursor += 2
            button_node = ET.SubElement(group_node, "Button")
            button_node.set("order", str(button_order + 1))
            button_node.set("actionUuid", action_uuid)
            if isinstance(descriptor, list):
                add_command_bar_button_descriptor(button_node, descriptor)
        if cursor < len(group):
            add_command_bar_group_placement(group_node, group[cursor])


def add_command_bar_button_descriptor(button_node: ET.Element, descriptor: list[object]) -> None:
    if len(descriptor) < 16:
        button_node.set("descriptor", command_bar_value_to_attr(descriptor))
        return
    button_node.set("name", clean_token(descriptor[1]))
    button_node.set("state", clean_token(descriptor[2]))
    button_node.set("visible", clean_token(descriptor[3]))
    title_lang, title = localized_title_parts_from_record(descriptor[4])
    if title:
        add_multilang_text(button_node, "Title", title, lang=title_lang)
    button_node.set("hasAction", clean_token(descriptor[5]))
    button_node.set("ownerUuid", clean_token(descriptor[6]))
    button_node.set("position", clean_token(descriptor[7]))
    button_node.set("style", clean_token(descriptor[8]))
    button_node.set("kind", clean_token(descriptor[9]))
    button_node.set("groupMode", clean_token(descriptor[10]))
    button_node.set("enabled", clean_token(descriptor[11]))
    button_node.set("checked", clean_token(descriptor[12]))
    button_node.set("showText", clean_token(descriptor[13]))
    button_node.set("shortcut", clean_token(descriptor[14]))
    button_node.set("default", clean_token(descriptor[15]))


COMMAND_BAR_ACTION_SCALAR_ATTRS = ("changesData", "display", "mode", "state", "flag", "variant")


def add_command_bar_group_placement(group_node: ET.Element, value: object) -> None:
    if not isinstance(value, list) or len(value) < 2:
        return
    placement = ET.SubElement(group_node, "Placement")
    placement.set("zone", clean_token(value[0]))
    placement.set("order", clean_token(value[1]))
    if len(value) <= 2 or not isinstance(value[2], list):
        return
    targets = value[2]
    if not targets:
        return
    placement.set("targetCount", clean_token(targets[0]))
    cursor = 1
    target_order = 1
    while cursor + 2 < len(targets):
        target = ET.SubElement(placement, "Target")
        target.set("order", str(target_order))
        target.set("uuid", clean_token(targets[cursor]))
        target.set("commandId", clean_token(targets[cursor + 1]))
        target.set("flag", clean_token(targets[cursor + 2]))
        cursor += 3
        target_order += 1


def add_command_bar_action_payload_fields(action_node: ET.Element, payload: object) -> None:
    if not isinstance(payload, list) or not payload:
        return
    action_kind = clean_token(payload[0])
    if action_kind:
        action_node.set("handlerKind", action_kind)
    if action_kind == "1" and len(payload) > 2:
        action_node.set("commandId", clean_token(payload[2]))
    if action_kind != "6":
        return
    if len(payload) > 1:
        action_node.set("commandScope", clean_token(payload[1]))
    if len(payload) > 2:
        action_node.set("commandTargetUuid", clean_token(payload[2]))
    if len(payload) > 3:
        action_node.set("commandCode", clean_token(payload[3]))
    if len(payload) > 4 and isinstance(payload[4], list):
        parameter = payload[4]
        for index, attr_name in (
            (0, "commandParamKind"),
            (1, "commandParamMode"),
            (2, "commandParamUuid"),
            (3, "commandParamId"),
            (4, "commandParamFlag"),
        ):
            if len(parameter) > index:
                action_node.set(attr_name, clean_token(parameter[index]))
    if len(payload) > 5:
        action_node.set("commandFlag", clean_token(payload[5]))
    if len(payload) > 6:
        action_node.set("commandMode", clean_token(payload[6]))


def add_command_bar_action_extra_fields(
    action_node: ET.Element,
    fields: list[object],
    asset_root: Path,
    item_name: str,
) -> None:
    scalar_index = 0
    text_tags = iter(("ToolTip", "Explanation"))
    picture_index = 0
    for value in fields:
        payload = find_base64_payload(value)
        if payload and asset_root is not None:
            picture_index += 1
            picture_name = item_name if picture_index == 1 else f"{item_name}_{picture_index}"
            add_picture_node_from_payload(action_node, payload, picture_name, asset_root)
            continue
        if is_command_bar_action_style_record(value):
            add_command_bar_action_style(action_node, value)
            continue
        if is_empty_localized_text_record(value):
            ET.SubElement(action_node, next(text_tags, "Explanation"))
            continue
        lang, text = localized_title_parts_from_record(value)
        if text:
            add_multilang_text(action_node, next(text_tags, "Explanation"), text, lang=lang)
            continue
        if scalar_index < len(COMMAND_BAR_ACTION_SCALAR_ATTRS):
            action_node.set(COMMAND_BAR_ACTION_SCALAR_ATTRS[scalar_index], clean_token(value))
        scalar_index += 1


def is_command_bar_action_style_record(value: object) -> bool:
    return isinstance(value, list) and len(value) >= 9 and clean_token(value[0]) == "4"


def is_empty_localized_text_record(value: object) -> bool:
    return isinstance(value, list) and len(value) == 2 and clean_token(value[0]) == "1" and clean_token(value[1]) == "0"


def add_command_bar_action_style(action_node: ET.Element, value: object) -> None:
    if not isinstance(value, list):
        return
    style = ET.SubElement(action_node, "Style")
    for index, attr_name in (
        (0, "kind"),
        (1, "mode"),
        (3, "name"),
        (4, "width"),
        (5, "height"),
        (6, "visible"),
        (7, "variant"),
        (8, "ref"),
    ):
        if len(value) > index:
            style.set(attr_name, clean_token(value[index]))
    if len(value) > 2 and isinstance(value[2], list) and value[2]:
        style.set("state", clean_token(value[2][0]))


def command_bar_action_handler_and_title(value: object) -> tuple[str, str]:
    if not isinstance(value, list) or len(value) < 3:
        return "", ""
    handler = clean_token(value[1]) if clean_token(value[0]) in {"1", "3"} else ""
    title = localized_title_from_record(value[2]) if isinstance(value[2], list) else ""
    return handler, title


def localized_title_from_record(value: object) -> str:
    return localized_title_parts_from_record(value)[1]


def localized_title_parts_from_record(value: object) -> tuple[str, str]:
    if (
        isinstance(value, list)
        and len(value) >= 3
        and clean_token(value[0]) == "1"
        and isinstance(value[2], list)
        and len(value[2]) >= 2
    ):
        lang = clean_token(value[2][0])
        if lang == "#" or re.match(r"^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$", lang):
            return lang, clean_token(value[2][1])
        return "ru", clean_token(value[2][1])
    return "ru", ""


def command_bar_value_to_attr(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def safe_int_token(value: object) -> int:
    try:
        return int(clean_token(value))
    except (TypeError, ValueError):
        return -1


def panel_dependency_groups_from_info(info: list[object]) -> tuple[list[dict[str, object]], int]:
    groups: list[dict[str, object]] = []
    cursor = 2
    while cursor < len(info):
        prefix: list[str] = []
        prefix_start = cursor
        while cursor < len(info) and not panel_dependency_count_at(info, cursor):
            if isinstance(info[cursor], list):
                return groups, prefix_start
            prefix.append(clean_token(info[cursor]))
            cursor += 1
        try:
            count = int(clean_token(info[cursor]))
        except (IndexError, ValueError):
            return groups, prefix_start
        cursor += 1
        records: list[list[object]] = []
        for _index in range(count):
            if cursor >= len(info) or not isinstance(info[cursor], list):
                return groups, cursor
            record = info[cursor]
            if len(record) >= 3:
                records.append(record)
            cursor += 1
        header: list[str] = []
        if records and len(records[0]) == 1 and isinstance(records[0][0], list):
            header = [clean_token(value) for value in records[0][0]]
            records = records[1:]
        groups.append({"prefix": prefix, "header": header, "records": records})
    return groups, cursor


def panel_dependency_count_at(info: list[object], cursor: int) -> bool:
    try:
        count = int(clean_token(info[cursor]))
    except (IndexError, ValueError):
        return False
    return count > 0 and len(info) >= cursor + 1 + count and all(isinstance(value, list) for value in info[cursor + 1 : cursor + 1 + count])


def is_page_style_group_record(value: object) -> bool:
    return isinstance(value, list) and len(value) >= 5 and clean_token(value[0]) in {"8", "10"}


def is_page_state_record(value: object) -> bool:
    return isinstance(value, list) and bool(value) and clean_token(value[0]) in {"3", "5", "6"}


def panel_page_state_scalars(info: list[object], cursor: int) -> tuple[str, str]:
    while cursor < len(info) and not is_page_style_group_record(info[cursor]):
        cursor += 1
    if cursor + 2 >= len(info):
        return "", ""
    page_state_flag = clean_token(info[cursor + 1]) if not isinstance(info[cursor + 1], list) else ""
    current_page_index = clean_token(info[cursor + 2]) if not isinstance(info[cursor + 2], list) else ""
    return page_state_flag, current_page_index


def panel_page_layouts_from_info(info: list[object], cursor: int) -> list[dict[str, str]]:
    while cursor < len(info) and not is_page_style_group_record(info[cursor]):
        cursor += 1
    if cursor >= len(info):
        return []
    cursor += 1
    while cursor < len(info) and not (isinstance(info[cursor], list) and clean_token(info[cursor][0]) == "1"):
        cursor += 1
    if cursor + 4 >= len(info):
        return []
    cursor += 4
    try:
        record_count = int(clean_token(info[cursor]))
    except ValueError:
        return []
    cursor += 1
    records = info[cursor : cursor + record_count]
    if record_count <= 0 or len(records) != record_count:
        return []
    layouts: list[dict[str, str]] = []
    for offset in range(0, len(records), 4):
        chunk = records[offset : offset + 4]
        if len(chunk) != 4 or not all(isinstance(record, list) and len(record) >= 9 for record in chunk):
            break
        page_index = clean_token(chunk[0][5])
        layouts.append(
            {
                "page": page_index,
                "left": clean_token(chunk[0][1]),
                "top": clean_token(chunk[1][1]),
                "width": clean_token(chunk[2][1]),
                "height": clean_token(chunk[3][1]),
                "horizontalMode": clean_token(chunk[2][7]),
                "verticalMode": clean_token(chunk[3][7]),
            }
        )
    return layouts


def add_first_in_group(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "RadioButton":
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list):
        return
    metadata = control_metadata_record(raw)
    if metadata is None or len(metadata) <= 5:
        return
    if clean_token(metadata[5]) == "1":
        set_text(parent, "FirstInGroup", "true")


def add_radio_button_properties(parent: ET.Element, control_type: str, item_data: object) -> None:
    if control_type != "RadioButton" or not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2]
    type_record = info[1] if len(info) > 1 else None
    if isinstance(type_record, list):
        pattern = type_record[1] if len(type_record) > 1 and isinstance(type_record[1], list) else []
        if pattern and pattern != ['"B"']:
            add_type_from_domain_record(parent, type_record, {})
    data_value = info[4] if len(info) > 4 and isinstance(info[4], list) and len(info[4]) > 1 else None
    if data_value is not None:
        value = ET.SubElement(parent, "DataValue")
        value.set("typeCode", clean_token(data_value[0]))
        value.text = clean_token(data_value[1])


def add_default_action(parent: ET.Element, control_type: str, item_data: object) -> None:
    if control_type not in {"Label", "Button"}:
        return
    if not isinstance(item_data, dict):
        return
    metadata = control_metadata_record(item_data.get("raw"))
    if metadata is None:
        return
    if control_type == "Label" and len(metadata) > 4 and clean_token(metadata[4]) == "1":
        set_text(parent, "DefaultAction", "true")
    if control_type == "Button" and len(metadata) > 5 and clean_token(metadata[5]) == "1":
        set_text(parent, "DefaultAction", "true")


def add_progress_bar_properties(parent: ET.Element, control_type: str, item_data: object) -> None:
    if control_type != "ProgressBar" or not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2]
    if clean_token(info[0]) != "0" or len(info) <= 1 or not isinstance(info[1], list):
        return
    record = info[1]
    if len(record) <= 7:
        return
    orientation = clean_token(record[1])
    minimum = clean_token(record[2])
    maximum = clean_token(record[3])
    step = clean_token(record[4])
    big_step = clean_token(record[5])
    show_percent = clean_token(record[6])
    display_style = clean_token(record[7])
    if orientation != "3":
        set_text(parent, "Orientation", orientation)
    if minimum != "0":
        set_text(parent, "MinimumValue", minimum)
    if maximum != "100":
        set_text(parent, "MaximumValue", maximum)
    if step != "1":
        set_text(parent, "Step", step)
    if big_step != "1":
        set_text(parent, "BigStep", big_step)
    if show_percent != "0":
        set_text(parent, "ShowPercent", "true")
    if display_style != "2":
        set_text(parent, "DisplayStyle", display_style)


def add_visible(parent: ET.Element, item_data: object) -> None:
    base = base_info_from_item_data(item_data)
    if base is None or len(base) <= 1:
        return
    value = clean_token(base[1])
    if value == "0":
        set_text(parent, "Visible", "false")


def add_enabled(parent: ET.Element, item_data: object) -> None:
    base = base_info_from_item_data(item_data)
    if base is None or len(base) <= 5:
        return
    value = clean_token(base[5])
    if value == "0":
        set_text(parent, "Enabled", "false")


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


def add_input_field_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "InputField":
        return
    input_info = input_field_info_record(item_data)
    if input_info is None:
        return
    if len(input_info) > 3 and clean_token(input_info[3]) != "0":
        set_text(parent, "EditMode", clean_token(input_info[3]))
    if len(input_info) > 4 and clean_token(input_info[4]) != "1":
        set_text(parent, "ChoiceMode", clean_token(input_info[4]))
    if len(input_info) > 5 and clean_token(input_info[5]) == "1":
        set_text(parent, "PasswordMode", "true")
    if len(input_info) > 7 and clean_token(input_info[7]) == "1":
        set_text(parent, "ExtendedEdit", "true")
    if len(input_info) > 14 and clean_token(input_info[13]) == "1" and clean_token(input_info[14]) != "0":
        set_text(parent, "MaxLength", clean_token(input_info[14]))
    if len(input_info) > 21 and not isinstance(input_info[21], list):
        mask = clean_token(input_info[21])
        if mask:
            set_text(parent, "Mask", mask)
    if len(input_info) > 26 and clean_token(input_info[26]) == "1":
        set_text(parent, "MultiLine", "true")
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2]
    if len(info) <= 3 or not isinstance(info[3], list) or len(info[3]) <= 1:
        return
    if len(info) > 1:
        add_type_from_domain_record(parent, info[1], {})
    data_link = info[3][1]
    if not isinstance(data_link, list) or len(data_link) <= 1 or not isinstance(data_link[1], list):
        return
    payload = data_link[1]
    if len(payload) > 3 and clean_token(payload[3]) != "0":
        set_text(parent, "DataBindingMode", clean_token(payload[3]))
    if len(payload) > 5 and clean_token(payload[5]) != "0":
        set_text(parent, "DataBindingFlag", clean_token(payload[5]))


def add_choice_field_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "ChoiceField":
        return
    choice_info = choice_field_info_record(item_data)
    if choice_info is None:
        return
    if len(choice_info) > 23 and clean_token(choice_info[23]) == "0":
        set_text(parent, "ChoiceButton", "false")
    if len(choice_info) > 24 and clean_token(choice_info[24]) == "0":
        set_text(parent, "ClearButton", "false")
    if len(choice_info) > 25 and clean_token(choice_info[25]) == "1":
        set_text(parent, "OpenButton", "true")
    if len(choice_info) > 26:
        add_choice_list(parent, choice_info[26])


def choice_field_info_record(item_data: object) -> list[object] | None:
    if not isinstance(item_data, dict):
        return None
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return None
    info = raw[2]
    if not info or clean_token(info[0]) != "2" or len(info) <= 1 or not isinstance(info[1], list):
        return None
    return info[1]


def add_choice_list(parent: ET.Element, value: object) -> None:
    if not isinstance(value, list) or len(value) < 4 or clean_token(value[0]) != "9":
        return
    table = value[2]
    if not isinstance(table, list) or len(table) < 9 or clean_token(table[0]) != "2":
        return
    rows = table[6]
    if not isinstance(rows, list) or len(rows) < 2 or clean_token(rows[0]) != "1":
        return
    choice_list = ET.SubElement(parent, "ChoiceList")
    choice_list.set("currentIndex", clean_token(table[7]))
    choice_list.set("selectionIndex", clean_token(table[8]))
    try:
        declared_count = int(clean_token(rows[1]))
    except ValueError:
        declared_count = max(len(rows) - 2, 0)
    for row in rows[2 : 2 + declared_count]:
        parsed = choice_list_item(row)
        if parsed is None:
            continue
        item = ET.SubElement(choice_list, "Item")
        item.set("index", parsed["index"])
        item.set("valueType", parsed["valueType"])
        item.set("value", parsed["value"])
        item.set("presentationType", parsed["presentationType"])
        add_multilang_text(item, "Presentation", parsed["presentation"])
    if not list(choice_list):
        parent.remove(choice_list)


def choice_list_item(row: object) -> dict[str, str] | None:
    if not isinstance(row, list) or len(row) < 6 or clean_token(row[0]) != "2":
        return None
    value = row[3]
    presentation = row[4]
    if not isinstance(value, list) or len(value) < 2:
        return None
    if not isinstance(presentation, list) or len(presentation) < 3:
        return None
    return {
        "index": clean_token(row[1]),
        "valueType": clean_token(value[0]),
        "value": clean_token(value[1]),
        "presentationType": clean_token(presentation[1]),
        "presentation": choice_list_presentation_text(presentation[2]),
    }


def choice_list_presentation_text(value: object) -> str:
    if (
        isinstance(value, list)
        and len(value) >= 3
        and clean_token(value[0]) == "1"
        and clean_token(value[1]) == "ru"
    ):
        return clean_token(value[2])
    return localized_text_from_record(value)


def add_table_view_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "Table":
        return
    view = table_view_from_item_data(item_data)
    if view is None:
        return
    if len(view) > 14 and clean_token(view[14]) == "1":
        set_text(parent, "ReadOnly", "true")
    extended_view = len(view) > 0 and clean_token(view[0]) == "23"
    if extended_view and len(view) > 20 and clean_token(view[20]) != "0":
        set_text(parent, "LeftFixedColumns", clean_token(view[20]))
    if extended_view and len(view) > 21 and clean_token(view[21]) != "0":
        set_text(parent, "RightFixedColumns", clean_token(view[21]))
    if len(view) > 22 and clean_token(view[22]) == "0":
        set_text(parent, "AutoMarkIncomplete", "false")
    if len(view) > 35 and clean_token(view[35]) != "1":
        set_text(parent, "ViewSetupMode", clean_token(view[35]))
    if len(view) > 6 and not is_default_color_record(view[6]):
        add_color_node_from_record(parent, "FieldBackColor", view[6])


def add_table_columns(parent: ET.Element, item: dict, item_data: object, asset_root: Path) -> None:
    if str(item.get("type", "")) != "Table":
        return
    columns = table_columns_from_item_data(item_data)
    if not columns:
        return
    columns_node = ET.SubElement(parent, "Columns")
    for column in columns:
        column_node = ET.SubElement(columns_node, "Column")
        column_node.set("name", column["name"])
        column_node.set("order", column["order"])
        if column["title"]:
            add_multilang_text(column_node, "Title", column["title"], lang=str(column.get("title_lang") or "ru"))
        if column["data_path"]:
            set_text(column_node, "DataPath", column["data_path"])
        set_text(column_node, "Width", column["width"])
        if column["style"] != "12590592":
            set_text(column_node, "Style", column["style"])
        if column["text_color"]:
            add_color_node_from_record(column_node, "TextColor", column["text_color"])
        if column["visible"] == "0":
            set_text(column_node, "Visible", "false")
        if column["read_only"] == "1":
            set_text(column_node, "ReadOnly", "true")
        if column["column_kind"] != "0":
            set_text(column_node, "ColumnKind", column["column_kind"])
        set_text(column_node, "CheckMode", column["check_mode"])
        if column["output_mode"] != "0":
            set_text(column_node, "OutputMode", column["output_mode"])
        set_text(column_node, "DataPathMode", column["data_path_mode"])
        set_text(column_node, "PresentationIndex", column["presentation_index"])
        if column["editor_control"] and column["editor_control"] != "InputField":
            set_text(column_node, "EditorControl", column["editor_control"])
        if column["use_picture"] == "1":
            set_text(column_node, "UsePicture", "true")
        if column["format"]:
            add_multilang_text(column_node, "Format", column["format"], lang=str(column.get("format_lang") or "ru"))
        if column["font"]:
            add_font_node_from_record(column_node, column["font"])
        if column["picture"]:
            add_picture_node_from_payload(column_node, column["picture"], f"{item.get('name', 'Table')}/{column['name']}", asset_root)
        add_type(column_node, column["pattern"], {})


def table_columns_from_item_data(item_data: object) -> list[dict[str, object]]:
    view = table_view_from_item_data(item_data)
    if not isinstance(view, list) or len(view) <= 23 or not isinstance(view[23], list):
        return []
    result: list[dict[str, object]] = []
    for column in view[23][1:]:
        if not isinstance(column, list) or len(column) < 2 or not isinstance(column[1], list):
            continue
        if len(column[1]) <= 1 or not isinstance(column[1][1], list) or len(column[1][1]) <= 1:
            continue
        body = column[1][1][1]
        if not isinstance(body, list) or len(body) <= 35:
            continue
        title_lang, title = localized_text_parts_from_record(body[1]) if len(body) > 1 else ("ru", "")
        data_path = clean_token(column[1][2]) if len(column[1]) > 2 else ""
        width = clean_token(body[4]) if len(body) > 4 else "1e2"
        order = clean_token(body[5]) if len(body) > 5 else str(len(result))
        style = clean_token(body[9]) if len(body) > 9 else "12590592"
        text_color = body[17] if len(body) > 17 and isinstance(body[17], list) and not is_default_color_record(body[17]) else []
        visible = clean_token(body[25]) if len(body) > 25 else "1"
        read_only = clean_token(body[26]) if len(body) > 26 else "0"
        column_kind = clean_token(body[27]) if len(body) > 27 else "0"
        check_mode = clean_token(body[28]) if len(body) > 28 else "4"
        output_mode = clean_token(body[29]) if len(body) > 29 else "0"
        name = clean_token(body[30]) if len(body) > 30 else title
        presentation_index = clean_token(body[32]) if len(body) > 32 else "15"
        use_picture = clean_token(body[33]) if len(body) > 33 else "0"
        format_lang, format_text = localized_text_parts_from_record(body[34]) if len(body) > 34 else ("ru", "")
        pattern_record = body[35]
        pattern = pattern_record[1] if isinstance(pattern_record, list) and len(pattern_record) > 1 and isinstance(pattern_record[1], list) else []
        data_path_mode = clean_token(body[37]) if len(body) > 37 else "1"
        editor_control = ORDINARY_CONTROL_CLASS_BY_GUID.get(clean_token(body[38]).lower(), "") if len(body) > 38 else ""
        font = body[22] if len(body) > 22 and isinstance(body[22], list) and body[22] != ["8", "3", "0", "1", "100"] else []
        picture = find_base64_payload(body[11]) if len(body) > 11 else ""
        value_descriptor = ""
        if len(body) > 39 and isinstance(body[39], list) and body[39] and isinstance(body[39][0], list) and body[39][0]:
            payload = clean_token(body[39][0][0])
            if payload.startswith("#base64:"):
                raw_payload = payload.removeprefix("#base64:")
                compact_payload = "".join(raw_payload.split())
                default_payload = TABLE_COLUMN_VALUE_PAYLOAD_BY_PATTERN.get(tuple(pattern))
                default_compact = (
                    "".join(default_payload.removeprefix("#base64:").split())
                    if isinstance(default_payload, str) and default_payload.startswith("#base64:")
                    else None
                )
                if compact_payload != default_compact:
                    value_descriptor = compact_payload
                value_descriptor_trailing_line_break = "true" if raw_payload.endswith(("\r", "\n")) else "false"
            else:
                value_descriptor_trailing_line_break = "false"
        else:
            value_descriptor_trailing_line_break = "false"
        if name or title:
            result.append(
                {
                    "name": name or title,
                    "title": title,
                    "title_lang": title_lang,
                    "data_path": data_path,
                    "width": width,
                    "order": order,
                    "style": style,
                    "text_color": text_color,
                    "visible": visible,
                    "read_only": read_only,
                    "column_kind": column_kind,
                    "check_mode": check_mode,
                    "output_mode": output_mode,
                    "presentation_index": presentation_index,
                    "use_picture": use_picture,
                    "format": format_text,
                    "format_lang": format_lang,
                    "data_path_mode": data_path_mode,
                    "editor_control": editor_control,
                    "font": font,
                    "picture": picture,
                    "pattern": pattern,
                    "value_descriptor": value_descriptor,
                    "value_descriptor_trailing_line_break": value_descriptor_trailing_line_break,
                }
            )
    return result


def table_view_from_item_data(item_data: object) -> list[object] | None:
    if not isinstance(item_data, dict):
        return None
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return None
    info = raw[2]
    if len(info) <= 2 or not isinstance(info[2], list) or len(info[2]) <= 1:
        return None
    view = info[2][1]
    return view if isinstance(view, list) else None


def table_data_source_from_item_data(item_data: object) -> list[object] | None:
    if not isinstance(item_data, dict):
        return None
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return None
    info = raw[2]
    return info[3] if len(info) > 3 and isinstance(info[3], list) else None


def add_pivot_chart_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "PivotChart":
        return
    presentation = pivot_chart_presentation_record(item_data)
    if presentation is None or len(presentation) <= 4:
        return
    kind = clean_token(presentation[4])
    if kind:
        set_text(parent, "PivotChartKind", kind)
    add_pivot_chart_fields(parent, presentation)
    add_pivot_chart_source_data(parent, presentation)


def add_chart_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "Chart":
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 3 or not isinstance(raw[3], list):
        return
    presentation = raw[3]
    if len(presentation) > 20:
        set_text(parent, "ChartKind", clean_token(presentation[20]))


def pivot_chart_presentation_record(item_data: object) -> list[object] | None:
    if not isinstance(item_data, dict):
        return None
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return None
    info = raw[2]
    if len(info) <= 1 or not isinstance(info[1], list):
        return None
    body = info[1]
    if len(body) <= 2 or not isinstance(body[2], list):
        return None
    presentation = body[2]
    if presentation and str(presentation[0]) == "75":
        return presentation
    return None


def add_pivot_chart_fields(parent: ET.Element, presentation: list[object]) -> None:
    fields_node = ET.SubElement(parent, "Fields")
    added = 0
    added += add_pivot_chart_field_group(
        fields_node,
        presentation,
        role="dimension",
        count_index=1,
        start_index=2,
        title_offset=6,
        axis_offset=5,
        value_offset=10,
        color_offset=3,
        enabled_offset=7,
    )
    added += add_pivot_chart_field_group(
        fields_node,
        presentation,
        role="measure",
        count_index=61,
        start_index=62,
        title_offset=0,
        axis_offset=5,
        value_offset=2,
        color_offset=3,
        enabled_offset=1,
    )
    if added == 0:
        parent.remove(fields_node)


def add_pivot_chart_field_group(
    parent: ET.Element,
    presentation: list[object],
    *,
    role: str,
    count_index: int,
    start_index: int,
    title_offset: int,
    axis_offset: int,
    value_offset: int,
    color_offset: int,
    enabled_offset: int,
) -> int:
    if len(presentation) <= count_index:
        return 0
    try:
        count = int(clean_token(presentation[count_index]))
    except ValueError:
        return 0
    added = 0
    for order in range(count):
        offset = start_index + order * 11
        record = presentation[offset : offset + 11]
        if len(record) < 11:
            break
        title = localized_text_from_record(record[title_offset])
        if not title:
            continue
        node = ET.SubElement(parent, "Field")
        node.set("role", role)
        node.set("order", str(order))
        node.set("title", title)
        node.set("axis", clean_token(record[axis_offset]))
        node.set("value", clean_token(record[value_offset]))
        node.set("enabled", bool_text_from_record(record[enabled_offset], default=True))
        color = color_value_from_record(record[color_offset])
        if color:
            node.set("color", color)
        added += 1
    return added


def add_pivot_chart_source_data(parent: ET.Element, presentation: list[object]) -> None:
    source_node = ET.SubElement(parent, "SourceData")
    added = 0
    for index in range(len(presentation) - 2):
        value = presentation[index]
        unit = presentation[index + 1]
        label = presentation[index + 2]
        label_text = clean_token(label)
        if not (
            isinstance(value, list)
            and len(value) >= 2
            and isinstance(unit, list)
            and len(unit) == 1
            and isinstance(label, str)
            and "\n" in label_text
        ):
            continue
        point = ET.SubElement(source_node, "Point")
        point.set("valueType", clean_token(value[0]))
        point.set("value", clean_token(value[1]))
        point.set("unit", clean_token(unit[0]))
        point.text = label_text
        added += 1
    if added == 0:
        parent.remove(source_node)


def color_value_from_record(value: object) -> str:
    if isinstance(value, list) and len(value) >= 3 and isinstance(value[2], list) and value[2]:
        return clean_token(value[2][0])
    return ""


def bool_text_from_record(value: object, *, default: bool) -> str:
    text = clean_token(value)
    if text in {"0", "false"}:
        return "false"
    if text in {"1", "true"}:
        return "true"
    return "true" if default else "false"


def add_geographical_schema_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "GeographicalSchemaField":
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 3:
        return
    info = raw[2]
    if isinstance(info, list) and len(info) > 15 and clean_token(info[0]) == "19":
        set_text(parent, "Output", clean_token(info[1]))
        set_text(parent, "Scale", clean_token(info[15]))
    settings = raw[3]
    if isinstance(settings, list) and len(settings) > 1 and clean_token(settings[0]) == "2":
        set_text(parent, "ScaleSupport", clean_token(settings[1]))


def add_picture_decoration_properties(parent: ET.Element, item_data: object) -> None:
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2]
    if len(info) <= 1 or not isinstance(info[1], list) or len(info[1]) <= 4:
        return
    picture_group = info[1][4]
    if not isinstance(picture_group, list) or len(picture_group) <= 10 or clean_token(picture_group[0]) != "10":
        return
    set_text(parent, "PictureSize", clean_token(picture_group[6]))
    set_text(parent, "ScalePicture", bool_text_from_record(picture_group[7], default=True))
    rendering = ET.SubElement(parent, "PictureRendering")
    rendering.set("horizontalMode", clean_token(picture_group[9]))
    rendering.set("verticalMode", clean_token(picture_group[10]))


def add_label_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "Label":
        return
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    info = raw[2]
    if len(info) <= 1 or not isinstance(info[1], list):
        return
    label_info = info[1]
    if len(label_info) > 3:
        set_text(parent, "HorizontalAlign", clean_token(label_info[3]))
    if len(label_info) > 4:
        set_text(parent, "VerticalAlign", clean_token(label_info[4]))
    if len(label_info) > 5:
        set_text(parent, "Hyperlink", bool_text_from_record(label_info[5], default=False))
    if len(label_info) > 11:
        set_text(parent, "PictureSize", clean_token(label_info[11]))
    if len(label_info) > 12 and isinstance(label_info[12], list) and len(label_info[12]) > 1:
        set_text(parent, "PicturePosition", clean_token(label_info[12][1]))
    if len(label_info) > 13:
        set_text(parent, "TextPosition", clean_token(label_info[13]))


def add_button_style_properties(parent: ET.Element, item: dict, item_data: object) -> None:
    if str(item.get("type", "")) != "Button":
        return
    base = base_info_from_item_data(item_data)
    if base is None:
        return
    if len(base) > 9 and not is_default_button_text_color_record(base[9]):
        add_color_node_from_record(parent, "ButtonTextColor", base[9])
    if len(base) > 10 and not is_default_button_back_color_record(base[10]):
        add_color_node_from_record(parent, "ButtonBackColor", base[10])


def add_button_scalar_properties(parent: ET.Element, item: dict, item_data: object, asset_root: Path | None = None) -> None:
    if str(item.get("type", "")) != "Button" or not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list) or len(raw) <= 2 or not isinstance(raw[2], list):
        return
    descriptor = CONTROL_INFO_SLOT_DESCRIPTORS["Button"]
    info = control_info_payload(raw[2], descriptor.info_kind)
    for property_name, default in (
        ("HorizontalAlign", "1"),
        ("VerticalAlign", "1"),
        ("PictureSize", "0"),
    ):
        value = nested_list_value(info, (descriptor.slot_index(property_name),))
        if clean_token(value) != default:
            set_text(parent, property_name, clean_token(value))
    shortcut_attrs = shortcut_record_to_xml_attrs(nested_list_value(info, (descriptor.slot_index("Shortcut"),)))
    if shortcut_attrs is not None:
        ET.SubElement(parent, "Shortcut", shortcut_attrs)
    multiline = nested_list_value(info, (descriptor.slot_index("MultiLine"),))
    if clean_token(multiline) == "1":
        set_text(parent, "MultiLine", "true")
    menu_mode = clean_token(nested_list_value(info, (descriptor.slot_index("MenuMode"),)))
    if menu_mode and menu_mode != "0":
        set_text(parent, "MenuMode", MENU_MODE_BY_CODE.get(menu_mode, f"code:{menu_mode}"))
    menu_buttons = nested_list_value(info, (descriptor.slot_index("MenuButtons"),))
    if isinstance(menu_buttons, list):
        add_command_bar_buttons(parent, menu_buttons, asset_root, parent.get("name") or "Button")


def add_control_events(parent: ET.Element, control_type: str, item_data: object) -> None:
    if not isinstance(item_data, dict):
        return
    raw = item_data.get("raw")
    if not isinstance(raw, list):
        return
    events = control_events_from_raw(raw, control_type)
    if not events:
        return
    events_node = ET.SubElement(parent, "Events")
    seen: set[tuple[str, str]] = set()
    for event in events:
        key = (event["name"], event["handler"])
        if key in seen:
            continue
        seen.add(key)
        node = ET.SubElement(events_node, "Event")
        node.set("name", event["name"])
        if event.get("uuid"):
            node.set("uuid", event["uuid"])
        if event.get("id"):
            node.set("id", event["id"])
        if event.get("title"):
            node.set("title", event["title"])
        node.text = event["handler"]


def control_events_from_raw(raw: object, control_type: str) -> list[dict[str, str]]:
    result: list[dict[str, str]] = []

    def walk(value: object) -> None:
        event = event_binding_from_record(value, control_type)
        if event:
            result.append(event)
            return
        if isinstance(value, list):
            for child in value:
                walk(child)

    walk(raw)
    return result


def event_binding_from_record(record: object, control_type: str) -> dict[str, str]:
    if not isinstance(record, list) or len(record) < 3:
        return {}
    uuid = clean_token(record[1])
    payload = record[2]
    if not isinstance(payload, list) or len(payload) < 3 or clean_token(payload[0]) != "3":
        return {}
    if not re.fullmatch(r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}", uuid):
        return {}
    handler = clean_token(payload[1])
    if not handler:
        return {}
    title = first_localized_text(payload[2])
    event_name = infer_platform_event_name(control_type, handler, title)
    if not event_name:
        return {}
    return {"name": event_name, "handler": handler, "uuid": uuid, "id": clean_token(record[0]), "title": title}


def infer_platform_event_name(control_type: str, handler: str, title: str = "") -> str:
    descriptor = control_descriptor(control_type)
    if descriptor is None:
        return ""
    candidates = sorted((member.name for member in descriptor.platform_events), key=len, reverse=True)
    handler_key = compact_event_text(handler)
    title_key = compact_event_text(title)
    for event_name in candidates:
        event_key = compact_event_text(event_name)
        if event_key and (handler_key.endswith(event_key) or title_key.endswith(event_key)):
            return event_name
    return ""


def compact_event_text(value: str) -> str:
    return re.sub(r"[^0-9A-Za-zА-Яа-яЁё]", "", value).lower()


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
        states = [
            state
            for state in child[2:]
            if is_page_state_record(state)
        ]
        if count != len(states) or not states:
            continue
        result: list[dict[str, str]] = []
        for state in states:
            name = clean_token(state[6]) if len(state) > 6 else ""
            title_lang, title = first_localized_text_parts_without_base_tooltip(state)
            if name:
                descriptor = {"name": name, "title": title or name, "titleLang": title_lang}
                if len(state) > 2 and isinstance(state[2], list) and len(state[2]) > 6:
                    descriptor["styleMode"] = clean_token(state[2][6])
                result.append(descriptor)
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
    if not isinstance(child_data, dict) and isinstance(child.get("raw"), list):
        child_data = child
    if not isinstance(child_data, dict):
        return None
    raw = child_data.get("raw")
    if not isinstance(raw, list):
        return None
    geometry = control_geometry_record(raw)
    if geometry is None:
        return None
    if len(geometry) <= 18:
        return None
    try:
        return int(clean_token(geometry[18]))
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
    add_font_node_from_record(parent, font)


def add_back_color(parent: ET.Element, item_data: object) -> None:
    add_color(parent, "BackColor", item_data, 3)


def add_text_color(parent: ET.Element, item_data: object) -> None:
    add_color(parent, "TextColor", item_data, 2)


def add_border_color(parent: ET.Element, item_data: object) -> None:
    add_color(parent, "BorderColor", item_data, 6)


def add_base_style_attributes(parent: ET.Element, item_data: object) -> None:
    base = base_info_from_item_data(item_data)
    if base is None or len(base) <= 19:
        return
    parent.set("baseStyleMode", clean_token(base[16]))
    parent.set("baseStyleState", clean_token(base[17]))
    parent.set("baseStyleVisible", clean_token(base[18]))
    parent.set("baseStyleDefaultMode", clean_token(base[19]))


def add_color(parent: ET.Element, tag: str, item_data: object, slot: int) -> None:
    base = base_info_from_item_data(item_data)
    if base is None or len(base) <= slot or is_default_color_record(base[slot]):
        return
    add_color_node_from_record(parent, tag, base[slot])


def is_default_button_text_color_record(value: object) -> bool:
    return value in (["3", "3", ["-7"]], ["4", "3", ["-7"], "3"])


def is_default_button_back_color_record(value: object) -> bool:
    return value in (["3", "3", ["-21"]], ["4", "3", ["-21"], "3"])


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
    if len(value) >= 13 and clean_token(value[0]) in {"10", "16", "19"}:
        return value
    for child in value:
        found = find_base_info_record(child)
        if found is not None:
            return found
    return None


def add_semantic_pages(
    parent: ET.Element,
    control_index: dict,
    element_index: dict[str, dict[str, str]],
    asset_root: Path,
) -> None:
    data = control_index.get("data", {})
    pages = ET.SubElement(parent, "Pages")
    for page_name in data.get("-pages-", []):
        page_path = str(page_name)
        page = ET.SubElement(pages, "Page")
        page.set("name", page_path)
        title_lang, title = page_title_parts(data.get(page_path))
        if title:
            add_multilang_text(page, "Title", title, lang=title_lang)
        for item in control_index.get("tree", []):
            if str(item.get("page", "")) != page_path:
                continue
            raw_key = str(item.get("rawKey") or f"{page_path}/{item.get('name', '')}")
            add_semantic_item(page, item, data, raw_key, element_index, asset_root)


def dump_xml_from_paths(
    form_path: Path,
    module_path: Path | None,
    control_index: dict,
    metadata_path: Path | None,
    out_path: Path,
) -> None:
    asset_root = out_path.with_suffix("")

    module_bytes = module_path.read_bytes() if module_path and module_path.exists() else b""
    form_root = parse_list_stream_document(form_path.read_bytes().decode("utf-8-sig"), allow_trailing=True).value
    metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path else None
    container_file_times = container_file_times_from_metadata(container_metadata_for_form(form_path))
    object_types = metadata_object_type_map(metadata)
    element_index = build_element_index(control_index)
    root_title_lang, root_title = form_root_title_parts(form_root)

    root = ET.Element("Form")
    root.set("version", SCHEMA_VERSION)
    root.set(f"{{{XSI_NS}}}noNamespaceSchemaLocation", ORDINARY_FORM_SCHEMA)
    set_container_time_attributes(root, container_file_times)

    if root_title:
        add_multilang_text(root, "Title", root_title, lang=root_title_lang)
    add_form_properties(root, form_root)
    if module_path and module_bytes:
        module_out = asset_root / "Module.bsl"
        module_out.parent.mkdir(parents=True, exist_ok=True)
        module_out.write_bytes(module_bytes)
    add_form_events(root, form_root)

    attrs = ET.SubElement(root, "Attributes")
    attribute_slots = attribute_slots_from_form_root(form_root)
    props_by_name = {str(prop.get("name", "")): prop for prop in control_index.get("props", [])}
    form_attribute_records = attribute_records_from_form_root(form_root)
    if form_attribute_records:
        for record in form_attribute_records:
            prop_name = attribute_record_name(record)
            if not prop_name or re.fullmatch(r"Attribute\d+", prop_name):
                continue
            prop = props_by_name.get(prop_name, {})
            attr = ET.SubElement(attrs, "Attribute")
            attr.set("name", prop_name)
            attr.set("id", str(prop.get("id", "")))
            if prop_name in attribute_slots:
                attr.set("slot", attribute_slots[prop_name])
            if len(record) > 1 and clean_token(record[1]) == "0":
                attr.set("controlData", "false")
            if len(record) >= 2:
                add_type_from_domain_record(attr, record[-1], object_types)
    else:
        for prop in control_index.get("props", []):
            prop_name = str(prop.get("name", ""))
            if re.fullmatch(r"Attribute\d+", prop_name):
                continue
            attr = ET.SubElement(attrs, "Attribute")
            attr.set("name", prop_name)
            attr.set("id", str(prop.get("id", "")))
            if prop_name in attribute_slots:
                attr.set("slot", attribute_slots[prop_name])
            if attribute_control_data_flag(form_root, prop_name) == "0":
                attr.set("controlData", "false")
            pattern_node = pattern_node_from_prop(prop)
            add_type(attr, pattern_node, object_types)

    add_semantic_pages(root, control_index, element_index, asset_root)
    write_form_shadow(control_index, asset_root)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_pretty_xml(root, out_path)


def write_form_shadow(control_index: dict, asset_root: Path) -> None:
    table_payloads: dict[str, dict[str, str]] = {}
    for item in control_index.get("tree", []):
        if not isinstance(item, dict) or item.get("type") != "Table":
            continue
        table_name = str(item.get("name") or "")
        if not table_name:
            continue
        view = table_view_from_item_data(item)
        if not isinstance(view, list) or len(view) <= 23 or not isinstance(view[23], list):
            continue
        for column in view[23][1:]:
            if not isinstance(column, list) or len(column) < 2 or not isinstance(column[1], list):
                continue
            try:
                body = column[1][1][1]
            except (IndexError, TypeError):
                continue
            if not isinstance(body, list) or len(body) <= 39:
                continue
            column_name = clean_token(body[30]) if len(body) > 30 else ""
            payload = find_base64_payload(body[39])
            if column_name and payload:
                table_payloads[f"{table_name}/{column_name}"] = {"payload": "#base64:" + payload}
    shadow_path = asset_root / ORDINARY_FORM_SHADOW_NAME
    if table_payloads:
        asset_root.mkdir(parents=True, exist_ok=True)
        shadow_path.write_text(
            json.dumps({"tableColumnValuePayloads": table_payloads}, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    elif shadow_path.exists():
        shadow_path.unlink()


def add_form_properties(parent: ET.Element, form_root: object) -> None:
    if not isinstance(form_root, list) or len(form_root) <= 1 or not isinstance(form_root[1], list):
        return
    form_record = form_root[1]
    if len(form_record) <= 4:
        return
    if clean_token(form_record[0]) != "18":
        return
    width = clean_token(form_record[3])
    height = clean_token(form_record[4])
    if width.isdigit():
        set_text(parent, "Width", width)
    if height.isdigit():
        set_text(parent, "Height", height)
    if len(form_record) > 10:
        counter = clean_token(form_record[10])
        if counter.isdigit():
            set_text(parent, "SerializationCounter", counter)
    root_panel_info = form_root_panel_info(form_record)
    if root_panel_info is not None:
        add_form_root_panel_layout(parent, root_panel_info)


def form_root_title(form_root: object) -> str:
    return form_root_title_parts(form_root)[1]


def form_root_title_parts(form_root: object) -> tuple[str, str]:
    if not isinstance(form_root, list) or len(form_root) <= 1 or not isinstance(form_root[1], list):
        return "ru", ""
    form_record = form_root[1]
    try:
        return localized_text_parts_from_record(form_record[1][0])
    except (IndexError, TypeError):
        return "ru", ""


def form_root_panel_info(form_record: list[object]) -> list[object] | None:
    try:
        root_panel = form_record[2]
        info_record = root_panel[1]
        info = info_record[1]
    except (IndexError, TypeError):
        return None
    return info if isinstance(info, list) else None


def add_form_root_panel_layout(parent: ET.Element, info: list[object]) -> None:
    if len(info) < 2 or clean_token(info[1]) != "26":
        return
    root_panel = ET.SubElement(parent, "RootPanelLayout")
    if info and isinstance(info[0], list):
        add_form_root_panel_base_style(root_panel, info[0])
    dependencies, cursor = form_root_panel_dependency_group_descriptors_and_cursor(info)
    add_layout_dependency_group_nodes(root_panel, dependencies)
    page_state_flag, current_page_index = form_root_panel_page_state_scalars(info, cursor)
    if page_state_flag:
        root_panel.set("pageStateFlag", page_state_flag)
    if current_page_index:
        root_panel.set("currentPageIndex", current_page_index)
    pages = form_root_panel_pages_from_info(info)
    for page_descriptor in pages:
        state = ET.SubElement(root_panel, "PageState")
        state.set("name", page_descriptor["name"])
        if page_descriptor.get("styleMode") is not None:
            state.set("styleMode", page_descriptor["styleMode"])
        add_multilang_text(state, "Title", page_descriptor.get("title") or page_descriptor["name"], lang=page_descriptor.get("titleLang") or "ru")
    layouts = panel_page_layouts_from_info(info, cursor)
    for layout_descriptor in layouts:
        layout = ET.SubElement(root_panel, "PageLayout")
        for key, value in layout_descriptor.items():
            layout.set(key, value)
    if not list(root_panel) and not root_panel.attrib:
        parent.remove(root_panel)


def add_form_root_panel_base_style(root_panel: ET.Element, base: list[object]) -> None:
    default_base = root_panel_base_info_record()
    if base == default_base:
        return
    style = ET.SubElement(root_panel, "BaseStyle")
    if len(base) > 4 and isinstance(base[4], list) and base[4] != default_base[4]:
        add_font_node_from_record(style, base[4])
    if len(base) > 2 and base[2] != default_base[2]:
        add_root_panel_base_color_node(style, "TextColor", base[2])
    if len(base) > 3 and base[3] != default_base[3]:
        add_root_panel_base_color_node(style, "BackColor", base[3])
    if len(base) > 6 and base[6] != default_base[6]:
        add_root_panel_base_color_node(style, "BorderColor", base[6])
    if not list(style):
        root_panel.remove(style)


def add_root_panel_base_color_node(parent: ET.Element, tag: str, value: object) -> None:
    if is_default_color_record(value) and isinstance(value, list) and len(value) >= 4:
        node = ET.SubElement(parent, tag)
        node.set("kind", "Auto")
        node.set("value", clean_token(value[2][0]))
        node.set("recordKind", clean_token(value[0]))
        node.set("recordSubKind", clean_token(value[1]))
        node.set("tailKind", clean_token(value[3]))
        node.text = "auto"
        return
    add_color_node_from_record(parent, tag, value)


def form_root_panel_dependency_groups_from_info(info: list[object]) -> list[list[list[object]]]:
    groups, _cursor = form_root_panel_dependency_groups_and_cursor(info)
    return groups


def form_root_panel_dependency_prefix(info: list[object]) -> list[str]:
    cursor = form_root_panel_first_dependency_count_cursor(info)
    if cursor == 2:
        return []
    return [clean_token(value) for value in info[2:cursor] if not isinstance(value, list)]


def form_root_panel_dependency_groups_and_cursor(info: list[object]) -> tuple[list[list[list[object]]], int]:
    descriptors, cursor = form_root_panel_dependency_group_descriptors_and_cursor(info)
    return [descriptor["records"] for descriptor in descriptors], cursor


def form_root_panel_dependency_group_descriptors_and_cursor(info: list[object]) -> tuple[list[dict[str, object]], int]:
    groups: list[dict[str, object]] = []
    cursor = 2
    while cursor < len(info):
        prefix: list[str] = []
        prefix_start = cursor
        while cursor < len(info) and not form_root_panel_dependency_count_at(info, cursor):
            if isinstance(info[cursor], list):
                return groups, prefix_start
            prefix.append(clean_token(info[cursor]))
            cursor += 1
        try:
            count = int(clean_token(info[cursor]))
        except (IndexError, ValueError):
            return groups, prefix_start
        if count <= 0:
            break
        cursor += 1
        records: list[list[object]] = []
        for _index in range(count):
            if cursor >= len(info) or not isinstance(info[cursor], list):
                return groups, cursor
            record = info[cursor]
            if len(record) >= 3:
                records.append(record)
            cursor += 1
        groups.append({"prefix": prefix, "records": records})
    return groups, cursor


def form_root_panel_first_dependency_count_cursor(info: list[object]) -> int:
    cursor = 2
    while cursor < len(info):
        if form_root_panel_dependency_count_at(info, cursor):
            return cursor
        if isinstance(info[cursor], list):
            break
        cursor += 1
    return len(info)


def form_root_panel_dependency_count_at(info: list[object], cursor: int) -> bool:
    try:
        count = int(clean_token(info[cursor]))
    except (IndexError, ValueError):
        return False
    return count > 0 and len(info) >= cursor + 1 + count and all(isinstance(value, list) for value in info[cursor + 1 : cursor + 1 + count])


def form_root_panel_page_layout_header(info: list[object], cursor: int) -> list[str]:
    while cursor < len(info) and not is_page_style_group_record(info[cursor]):
        cursor += 1
    if cursor >= len(info):
        return []
    cursor += 1
    while cursor < len(info) and not (isinstance(info[cursor], list) and clean_token(info[cursor][0]) == "1"):
        cursor += 1
    if cursor + 4 >= len(info):
        return []
    return [clean_token(value) for value in info[cursor + 1 : cursor + 4] if not isinstance(value, list)]


def form_root_panel_page_state_scalars(info: list[object], cursor: int) -> tuple[str, str]:
    while cursor < len(info) and not is_page_style_group_record(info[cursor]):
        cursor += 1
    if cursor + 2 >= len(info):
        return "", ""
    page_state_flag = clean_token(info[cursor + 1]) if not isinstance(info[cursor + 1], list) else ""
    current_page_index = clean_token(info[cursor + 2]) if not isinstance(info[cursor + 2], list) else ""
    return page_state_flag, current_page_index


def form_root_panel_dependency_tail(info: list[object], cursor: int) -> list[str]:
    result: list[str] = []
    while cursor < len(info) and not isinstance(info[cursor], list):
        result.append(clean_token(info[cursor]))
        cursor += 1
    return result


def form_root_panel_post_layout_tail(info: list[object], cursor: int) -> tuple[list[str], list[str]]:
    while cursor < len(info) and not is_page_style_group_record(info[cursor]):
        cursor += 1
    if cursor >= len(info):
        return [], []
    cursor += 1
    while cursor < len(info) and not (isinstance(info[cursor], list) and clean_token(info[cursor][0]) == "1"):
        cursor += 1
    if cursor + 4 >= len(info):
        return [], []
    cursor += 4
    try:
        record_count = int(clean_token(info[cursor]))
    except ValueError:
        return [], []
    tail = info[cursor + 1 + record_count :]
    color_index = next((index for index, value in enumerate(tail) if isinstance(value, list) and is_default_color_record(value)), -1)
    if color_index < 0:
        return [], []
    before = [clean_token(value) for value in tail[:color_index] if not isinstance(value, list)]
    after = [clean_token(value) for value in tail[color_index + 1 :] if not isinstance(value, list)]
    return before, after


def form_root_panel_pages_from_info(info: list[object]) -> list[dict[str, str]]:
    for child in info:
        if not isinstance(child, list) or len(child) < 3 or clean_token(child[0]) != "1":
            continue
        try:
            count = int(clean_token(child[1]))
        except ValueError:
            continue
        states = [
            state
            for state in child[2:]
            if is_page_state_record(state)
        ]
        if count != len(states) or not states:
            continue
        result: list[dict[str, str]] = []
        for state in states:
            name = clean_token(state[6]) if len(state) > 6 else ""
            title = first_localized_text(state)
            if name:
                descriptor = {"name": name, "title": title or name}
                if len(state) > 2 and isinstance(state[2], list) and len(state[2]) > 6:
                    descriptor["styleMode"] = clean_token(state[2][6])
                result.append(descriptor)
        if result:
            return result
    return []


def attribute_record_name(record: list[object]) -> str:
    if len(record) >= 2 and isinstance(record[-1], list) and record[-1] and clean_token(record[-1][0]) == "Pattern":
        return clean_token(record[-2])
    if len(record) > 4:
        return clean_token(record[4])
    return ""


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
            if event.get("id"):
                node.set("id", event["id"])
            if event.get("title"):
                node.set("title", event["title"])
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
            name = attribute_record_name(record)
            if name:
                result[name] = clean_token(slot_record[0])
    return result


def attribute_records_from_form_root(form_root: object) -> list[list[object]]:
    if not isinstance(form_root, list) or len(form_root) <= 2 or not isinstance(form_root[2], list):
        return []
    table = form_root[2]
    if len(table) <= 2 or not isinstance(table[2], list):
        return []
    return [record for record in table[2][1:] if isinstance(record, list)]


def attribute_control_data_flag(form_root: object, name: str) -> str:
    record = attribute_record_from_form_root(form_root, name)
    if record is None or len(record) <= 1:
        return "1"
    return clean_token(record[1])


def attribute_record_from_form_root(form_root: object, name: str) -> list[object] | None:
    if not isinstance(form_root, list) or len(form_root) <= 2 or not isinstance(form_root[2], list):
        return None
    table = form_root[2]
    if len(table) <= 2 or not isinstance(table[2], list):
        return None
    for record in table[2][1:]:
        if isinstance(record, list) and attribute_record_name(record) == name:
            return record
    return None


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
    title = first_localized_text(payload[2]) if len(payload) > 2 else ""
    return {"name": name, "uuid": uuid, "title": title, "id": clean_token(record[0])}


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


def container_metadata_for_form(form_path: Path) -> dict[str, object] | None:
    info_path = form_path.parent / CONTAINER_INFO_NAME
    if not info_path.exists():
        return None
    try:
        metadata = json.loads(info_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return metadata if isinstance(metadata, dict) else None


def container_file_times_from_metadata(metadata: dict[str, object] | None) -> dict[str, tuple[int | None, int | None]]:
    if not isinstance(metadata, dict):
        return {}
    result: dict[str, tuple[int | None, int | None]] = {}
    for file in metadata.get("files", []):
        if not isinstance(file, dict):
            continue
        name = str(file.get("name", ""))
        if not name:
            continue
        try:
            created = int(file["createdTicks"]) if file.get("createdTicks") is not None else None
            modified = int(file["modifiedTicks"]) if file.get("modifiedTicks") is not None else None
        except (TypeError, ValueError):
            continue
        result[name] = (created, modified)
    return result


def set_container_time_attributes(root: ET.Element, file_times: dict[str, tuple[int | None, int | None]]) -> None:
    created, modified = file_times.get("form", file_times.get("module", (None, None)))
    if created is not None:
        root.set("containerCreatedTicks", str(created))
    if modified is not None:
        root.set("containerModifiedTicks", str(modified))


def container_file_times_from_xml(root: ET.Element) -> dict[str, tuple[int | None, int | None]]:
    try:
        created = int(root.get("containerCreatedTicks", ""))
        modified = int(root.get("containerModifiedTicks", ""))
    except ValueError:
        return {}
    return {"form": (created, modified), "module": (created, modified)}


def semantic_model_hash(root: ET.Element) -> str:
    return semantic_graph_digest(root)


def schema_path(name: str = ORDINARY_FORM_SCHEMA) -> Path:
    if name not in KNOWN_SCHEMAS:
        raise ValueError(f"Unknown bundled schema: {name}")
    return Path(str(importlib.resources.files("onec_ordinary_forms") / "schemas" / name))


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


def list_schemas(args: argparse.Namespace) -> None:
    for name in KNOWN_SCHEMAS:
        print(schema_path(name))


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
    bin_data = build_form_bin_container(
        form_data,
        module_data,
        file_times=container_file_times_from_xml(root),
    )
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    out_bin.write_bytes(bin_data)


def scan_corpus(args: argparse.Namespace) -> None:
    report = build_corpus_report(
        root=Path(args.root),
        name_regex=args.name_regex,
        limit=args.limit,
        exported_root=Path(args.exported_root) if args.exported_root else None,
        include_semantic_digest=args.semantic_digest,
        compare_exported_root=Path(args.compare_exported_root) if args.compare_exported_root else None,
    )
    write_report(report, Path(args.out_json) if args.out_json else None)


def unpack_bin(args: argparse.Namespace) -> None:
    unpack_form_bin(Path(args.bin), Path(args.out_dir))


def pack_bin(args: argparse.Namespace) -> None:
    pack_form_bin(Path(args.parts_dir), Path(args.out_bin))


def dump_bin(args: argparse.Namespace) -> None:
    dump_form_bin_to_xml(
        Path(args.bin),
        Path(args.out),
        model_xml_writer=dump_xml_from_paths,
        metadata_json=Path(args.metadata_json) if args.metadata_json else None,
    )


def digest_xml(args: argparse.Namespace) -> None:
    graph = semantic_graph(ET.parse(args.xml).getroot())
    write_report(graph, Path(args.out_json) if args.out_json else None)


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_bin_parser = subparsers.add_parser("build-bin")
    build_bin_parser.add_argument("--xml", required=True, help="Form.xml produced by dump-bin")
    build_bin_parser.add_argument("--out-bin", required=True, help="Rebuilt ordinary form Form.bin")
    build_bin_parser.add_argument("--asset-root", help="Directory with Module.bsl and extracted assets")
    build_bin_parser.set_defaults(func=build_bin)

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("--xml", required=True, help="Form.xml to validate")
    validate_parser.add_argument("--schema", help="Override ordinary-form XSD path")
    validate_parser.set_defaults(func=validate_xml)

    schemas_parser = subparsers.add_parser("schemas")
    schemas_parser.set_defaults(func=list_schemas)

    format_parser = subparsers.add_parser("format-xml")
    format_parser.add_argument("--xml", required=True, help="XML or XSD file to rewrite with stable pretty formatting")
    format_parser.set_defaults(func=format_xml)

    scan_parser = subparsers.add_parser("scan-corpus")
    scan_parser.add_argument("--root", required=True, help="Directory with .epf/.erf files")
    scan_parser.add_argument("--name-regex", help="Optional regex filter for relative paths")
    scan_parser.add_argument("--limit", type=int, help="Limit selected files after sorting/filtering")
    scan_parser.add_argument("--exported-root", help="Optional ibcmd-exported directory to classify")
    scan_parser.add_argument("--semantic-digest", action="store_true", help="Add Form.xml semantic graph hashes to exported forms")
    scan_parser.add_argument("--compare-exported-root", help="Compare exported Form.xml semantic hashes with another exported tree")
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

    dump_bin_parser = subparsers.add_parser("dump-bin")
    dump_bin_parser.add_argument("--bin", required=True, help="Ordinary form Form.bin")
    dump_bin_parser.add_argument("--metadata-json")
    dump_bin_parser.add_argument("--out", required=True, help="Form.xml output path")
    dump_bin_parser.set_defaults(func=dump_bin)

    digest_xml_parser = subparsers.add_parser("digest-xml")
    digest_xml_parser.add_argument("--xml", required=True, help="Form.xml to summarize as a semantic graph")
    digest_xml_parser.add_argument("--out-json", help="Write JSON report instead of stdout")
    digest_xml_parser.set_defaults(func=digest_xml)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
