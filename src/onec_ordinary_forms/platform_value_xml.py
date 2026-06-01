"""XML materializers for typed platform value records."""

from __future__ import annotations

import xml.etree.ElementTree as ET

from onec_ordinary_forms.ui_values import ORDINARY_STYLE_COLOR_NAMES
from onec_ordinary_forms.value_codec import clean_atom


def is_default_color_record(value: object) -> bool:
    return value in (["3", "4", ["0"]], ["4", "4", ["0"], "4"])


def is_default_font_record(value: object) -> bool:
    return value == ["6", "3", "0", "1"]


def add_color_node_from_record(parent: ET.Element, tag: str, value: object) -> ET.Element | None:
    if not isinstance(value, list) or len(value) < 3:
        return None
    if not isinstance(value[2], list) or not value[2]:
        return None
    color_value = clean_atom(value[2][0])
    node = ET.SubElement(parent, tag)
    node.set("value", color_value)
    node.set("recordKind", clean_atom(value[0]))
    if len(value) > 1:
        node.set("recordSubKind", clean_atom(value[1]))
    if len(value) > 3:
        node.set("tailKind", clean_atom(value[3]))
    style_name = ORDINARY_STYLE_COLOR_NAMES.get(color_value)
    if style_name:
        node.set("kind", "StyleItem")
        node.set("name", style_name)
        node.text = f"style:{style_name}"
        return node
    node.set("kind", "Absolute")
    rgb = color_decimal_to_rgb(color_value)
    if rgb:
        node.set("rgb", rgb)
        node.text = rgb
    else:
        node.text = color_value
    return node


def add_font_node_from_record(parent: ET.Element, value: object) -> ET.Element | None:
    if not isinstance(value, list) or len(value) < 4:
        return None
    node = ET.SubElement(parent, "Font")
    node.set("kind", clean_atom(value[0]))
    node.set("family", clean_atom(value[1]))
    node.set("style", clean_atom(value[2]))
    if isinstance(value[3], list):
        for item in value[3]:
            delta = ET.SubElement(node, "Delta")
            delta.text = clean_atom(item)
    else:
        node.set("delta", clean_atom(value[3]))
    for index, item in enumerate(value[4:], start=1):
        extra = ET.SubElement(node, "Value")
        extra.set("index", str(index))
        clean_value = clean_atom(item)
        if isinstance(item, str) and item.startswith('"'):
            extra.set("atom", item)
        if index == 1:
            node.set("size", clean_value)
            node.set("height", clean_value)
        extra.text = clean_value
    return node


def color_decimal_to_rgb(value: str) -> str | None:
    try:
        number = int(value)
    except ValueError:
        return None
    if number < 0 or number > 0xFFFFFF:
        return None
    return f"#{number:06X}"
