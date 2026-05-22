"""Bidirectional XML representation for 1C list-stream bracket text."""

from __future__ import annotations

import xml.etree.ElementTree as ET

from onec_ordinary_forms.liststream import dumps, parse_list_stream


FORMAT = "1c-list-stream-xml"


def bracket_text_to_xml(text: str, *, has_bom: bool = False) -> ET.Element:
    root = ET.Element("BracketStream")
    root.set("format", FORMAT)
    root.set("bom", "true" if has_bom else "false")
    value_to_xml(root, parse_list_stream(text, allow_trailing=True))
    return root


def bracket_xml_to_text(node: ET.Element) -> str:
    if node.get("format") != FORMAT:
        raise ValueError("Unsupported BracketStream XML format")
    children = list(node)
    if len(children) != 1:
        raise ValueError("BracketStream must contain exactly one root value")
    return dumps(xml_to_value(children[0]))


def bracket_xml_to_bytes(node: ET.Element) -> bytes:
    data = bracket_xml_to_text(node).encode("utf-8")
    return b"\xef\xbb\xbf" + data if node.get("bom") == "true" else data


def value_to_xml(parent: ET.Element, value: object) -> ET.Element:
    if isinstance(value, list):
        node = ET.SubElement(parent, "List")
        node.set("count", str(len(value)))
        for item in value:
            value_to_xml(node, item)
        return node
    text = str(value)
    if is_quoted_atom(text):
        node = ET.SubElement(parent, "String")
        node.text = unquote_atom(text)
        return node
    node = ET.SubElement(parent, "Atom")
    node.text = text
    return node


def xml_to_value(node: ET.Element) -> object:
    if node.tag == "List":
        return [xml_to_value(child) for child in node]
    if node.tag == "String":
        return quote_atom(node.text or "")
    if node.tag == "Atom":
        text = node.text or ""
        if any(char.isspace() or char in "{}[]," for char in text):
            raise ValueError(f"Invalid list-stream atom: {text!r}")
        return text
    raise ValueError(f"Unsupported BracketStream node: {node.tag}")


def is_quoted_atom(value: str) -> bool:
    return len(value) >= 2 and value[0] == '"' and value[-1] == '"'


def unquote_atom(value: str) -> str:
    return value[1:-1].replace('\\"', '"').replace("\\\\", "\\")


def quote_atom(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
