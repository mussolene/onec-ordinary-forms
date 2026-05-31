"""Semantic object-model digest for ordinary Form.xml files."""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Iterable
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from onec_ordinary_forms.ordinary_properties import PLATFORM_PALETTE


DIGEST_VERSION = 1

NOISE_ATTRIBUTES = frozenset(
    {
        "containerCreatedTicks",
        "containerModifiedTicks",
        "layoutPreTail",
        "primaryDimensionMarker",
        "layoutTail",
    }
)

NON_CONTROL_PARENT_TAGS = frozenset(
    {
        "Actions",
        "Attributes",
        "Bindings",
        "Buttons",
        "ChoiceList",
        "Columns",
        "CommandSource",
        "Events",
        "Items",
        "ValueDescriptor",
    }
)

COLOR_TAGS = frozenset(
    {
        "BackColor",
        "BorderColor",
        "ButtonBackColor",
        "ButtonTextColor",
        "FieldBackColor",
        "FieldTextColor",
        "TextColor",
        "TransparentBackColor",
    }
)


def semantic_graph_from_xml(path: Path) -> dict[str, object]:
    return semantic_graph(ET.parse(path).getroot())


def semantic_graph_digest(root: ET.Element) -> str:
    return str(semantic_graph(root)["hash"])


def semantic_graph(root: ET.Element) -> dict[str, object]:
    paths = element_paths(root)
    controls = collect_controls(root, paths)
    graph: dict[str, object] = {
        "version": DIGEST_VERSION,
        "rootDigest": stable_digest(stable_node(root)),
        "summary": summary(root, controls, paths),
        "controls": controls,
        "attributes": collect_named(root, paths, "Attribute"),
        "events": collect_named(root, paths, "Event"),
        "tableColumns": collect_table_columns(root, paths),
        "pictures": collect_named(root, paths, "Picture"),
        "fonts": collect_named(root, paths, "Font"),
        "colors": collect_colors(root, paths),
        "commandSources": collect_command_sources(root, paths),
    }
    graph["hash"] = stable_digest(graph)
    return graph


def stable_node(element: ET.Element) -> dict[str, object]:
    node: dict[str, object] = {"tag": local_name(element.tag)}
    attrs = {
        local_name(name): normalize_text(value)
        for name, value in sorted(element.attrib.items(), key=lambda item: local_name(item[0]))
        if not is_noise_attribute(name)
    }
    if attrs:
        node["attributes"] = attrs
    if element.text and element.text.strip():
        node["text"] = normalize_text(element.text)
    children = [stable_node(child) for child in element if isinstance(child.tag, str)]
    if children:
        node["children"] = children
    return node


def element_paths(root: ET.Element) -> dict[int, str]:
    paths: dict[int, str] = {}

    def walk(element: ET.Element, parent_path: str) -> None:
        paths[id(element)] = parent_path
        sibling_counts: dict[str, int] = defaultdict(int)
        for child in element:
            if not isinstance(child.tag, str):
                continue
            tag = local_name(child.tag)
            sibling_counts[tag] += 1
            walk(child, f"{parent_path}/{path_segment(child, sibling_counts[tag])}")

    walk(root, path_segment(root, 1))
    return paths


def collect_controls(root: ET.Element, paths: dict[int, str]) -> list[dict[str, object]]:
    controls: list[dict[str, object]] = []

    def walk(
        element: ET.Element,
        *,
        parent_tag: str = "",
        parent_control: ET.Element | None = None,
        page: str | None = None,
    ) -> None:
        tag = local_name(element.tag)
        if tag == "Page":
            page = element.get("name") or element.get("id") or page
        current_parent = parent_control
        if is_control_element(element, parent_tag):
            controls.append(control_record(element, parent_control, page, paths))
            current_parent = element
        for child in element:
            if isinstance(child.tag, str):
                walk(child, parent_tag=tag, parent_control=current_parent, page=page)

    walk(root)
    for index, control in enumerate(controls, start=1):
        control["order"] = index
    return controls


def control_record(
    element: ET.Element,
    parent_control: ET.Element | None,
    page: str | None,
    paths: dict[int, str],
) -> dict[str, object]:
    position = element.find("Position")
    events = element.find("Events")
    command_source = element.find("CommandSource")
    columns = element.find("Columns")
    record: dict[str, object] = {
        "path": paths[id(element)],
        "type": local_name(element.tag),
        "id": element.get("id", ""),
        "name": element.get("name", ""),
        "uuid": element.get("uuid", ""),
        "parentId": parent_control.get("id", "") if parent_control is not None else "",
        "parentName": parent_control.get("name", "") if parent_control is not None else "",
        "page": page or "",
        "positionDigest": stable_digest(stable_node(position)) if position is not None else "",
        "position": normalized_attributes(position) if position is not None else {},
        "bindings": len(list(element.findall("./Position/Bindings/Binding"))),
        "dimensionBindings": len(list(element.findall("./Position/Bindings/DimensionBinding"))),
        "events": len(list(events.findall("Event"))) if events is not None else 0,
        "actions": len(list(element.findall("./Action"))),
        "columns": len(list(columns.findall("Column"))) if columns is not None else 0,
        "hasCommandSource": command_source is not None,
        "digest": stable_digest(stable_node(element)),
    }
    title = element.find("Title")
    if title is not None:
        record["titleDigest"] = stable_digest(stable_node(title))
    command_name = element.find("CommandName")
    if command_name is not None and command_name.text:
        record["commandName"] = normalize_text(command_name.text)
    return record


def collect_table_columns(root: ET.Element, paths: dict[int, str]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for column in iter_local(root, "Column"):
        editor = column.find("EditorControl")
        records.append(
            {
                "path": paths[id(column)],
                "name": column.get("name", ""),
                "order": column.get("order", ""),
                "editorControl": normalize_text(editor.text) if editor is not None and editor.text else "",
                "digest": stable_digest(stable_node(column)),
            }
        )
    return records


def collect_named(root: ET.Element, paths: dict[int, str], tag: str) -> list[dict[str, object]]:
    return [
        {
            "path": paths[id(element)],
            "name": element.get("name", ""),
            "digest": stable_digest(stable_node(element)),
        }
        for element in iter_local(root, tag)
    ]


def collect_colors(root: ET.Element, paths: dict[int, str]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for element in root.iter():
        if not isinstance(element.tag, str):
            continue
        tag = local_name(element.tag)
        if tag in COLOR_TAGS or tag.endswith("Color"):
            records.append(
                {
                    "path": paths[id(element)],
                    "type": tag,
                    "digest": stable_digest(stable_node(element)),
                }
            )
    return records


def collect_command_sources(root: ET.Element, paths: dict[int, str]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for tag in ("CommandSource", "CommandName", "Action"):
        for element in iter_local(root, tag):
            records.append(
                {
                    "path": paths[id(element)],
                    "type": tag,
                    "digest": stable_digest(stable_node(element)),
                }
            )
    return records


def summary(root: ET.Element, controls: list[dict[str, object]], paths: dict[int, str]) -> dict[str, int]:
    return {
        "controls": len(controls),
        "positions": count_local(root, "Position"),
        "bindings": count_local(root, "Binding"),
        "dimensionBindings": count_local(root, "DimensionBinding"),
        "events": count_local(root, "Event"),
        "attributes": count_local(root, "Attribute"),
        "columns": count_local(root, "Column"),
        "pictures": count_local(root, "Picture"),
        "fonts": count_local(root, "Font"),
        "colors": len(list(collect_colors(root, paths))),
        "commandSources": count_local(root, "CommandSource")
        + count_local(root, "CommandName")
        + count_local(root, "Action"),
    }


def iter_local(root: ET.Element, tag: str) -> Iterable[ET.Element]:
    for element in root.iter():
        if not isinstance(element.tag, str):
            continue
        if local_name(element.tag) == tag:
            yield element


def count_local(root: ET.Element, tag: str) -> int:
    return sum(1 for _ in iter_local(root, tag))


def normalized_attributes(element: ET.Element) -> dict[str, str]:
    return {
        local_name(name): normalize_text(value)
        for name, value in sorted(element.attrib.items(), key=lambda item: local_name(item[0]))
        if not is_noise_attribute(name)
    }


def is_control_element(element: ET.Element, parent_tag: str) -> bool:
    return local_name(element.tag) in PLATFORM_PALETTE and parent_tag not in NON_CONTROL_PARENT_TAGS


def path_segment(element: ET.Element, index: int) -> str:
    tag = local_name(element.tag)
    key = element.get("name") or element.get("id") or str(index)
    return f"{tag}[{key}]"


def stable_digest(value: object) -> str:
    data = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def normalize_text(value: str) -> str:
    return value.replace("\r\r\n", "\n").replace("\r\n", "\n").replace("\r", "\n")


def is_noise_attribute(name: str) -> bool:
    local = local_name(name)
    return local in NOISE_ATTRIBUTES or local == "schemaLocation" or local == "noNamespaceSchemaLocation"


def local_name(name: str) -> str:
    if "}" in name:
        return name.rsplit("}", 1)[1]
    if ":" in name:
        return name.rsplit(":", 1)[1]
    return name
