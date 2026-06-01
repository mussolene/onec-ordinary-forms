"""XML materializers for ordinary control platform position records."""

from __future__ import annotations

import copy
import xml.etree.ElementTree as ET

from onec_ordinary_forms.cli import add_geometry, control_geometry_record
from onec_ordinary_forms.ordinary_stream import apply_geometry_bindings_to_raw


POSITION_COORDINATE_SLOTS = {
    "left": 1,
    "top": 2,
    "right": 3,
    "bottom": 4,
}


def position_node_from_control_record(
    raw: list[object],
    *,
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> ET.Element | None:
    parent = ET.Element("Control")
    add_geometry(parent, {"raw": raw, "id": current_id}, element_index)
    position = parent.find("Position")
    return copy.deepcopy(position) if position is not None else None


def apply_position_node_to_control_record(
    position: ET.Element,
    raw: list[object],
    *,
    current_id: str = "",
    element_index: dict[str, dict[str, str]] | None = None,
) -> bool:
    geometry = control_geometry_record(raw)
    if geometry is None:
        return False
    position = copy.deepcopy(position)
    _normalize_edited_bindings(position)
    for attr_name, slot in POSITION_COORDINATE_SLOTS.items():
        value = position.get(attr_name)
        if value is not None and len(geometry) > slot:
            geometry[slot] = value
    _fill_implicit_binding_offsets(position, geometry, current_id, element_index or {})
    apply_geometry_bindings_to_raw(position, geometry)
    return True


def _normalize_edited_bindings(position: ET.Element) -> None:
    bindings = position.find("Bindings")
    if bindings is None:
        return
    for binding in list(bindings.findall("Binding")) + list(bindings.findall("DimensionBinding")):
        if binding.get("mode") is not None or len(binding):
            binding.attrib.pop("value", None)


def _fill_implicit_binding_offsets(
    position: ET.Element,
    geometry: list[object],
    current_id: str,
    element_index: dict[str, dict[str, str]],
) -> None:
    bindings = position.find("Bindings")
    if bindings is None:
        return
    current_geometry = _geometry_map_from_record(geometry)
    for binding in bindings.findall("Binding"):
        coordinate = binding.get("coordinate") or _coordinate_from_slot(binding.get("slot"))
        if coordinate is None:
            continue
        current_value = _coordinate_value(current_geometry, coordinate)
        if current_value is None:
            continue
        for anchor in binding:
            if anchor.tag not in {"From", "To"} or anchor.get("offset") is not None:
                continue
            if anchor.get("relation") not in {None, "targetEdgeOffset"}:
                continue
            target_geometry = _target_geometry(anchor, current_id, current_geometry, element_index)
            if target_geometry is None:
                continue
            target_value = _edge_value(target_geometry, anchor.get("side", "none"))
            if target_value is None:
                continue
            anchor.set("offset", str(current_value - target_value))


def _coordinate_from_slot(slot: str | None) -> str | None:
    return {
        "1": "top",
        "2": "bottom",
        "3": "left",
        "4": "right",
        "5": "verticalCenter",
        "6": "horizontalCenter",
    }.get(slot or "")


def _geometry_map_from_record(raw: list[object]) -> dict[str, str]:
    if len(raw) < 5:
        return {}
    return {
        "left": str(raw[1]),
        "top": str(raw[2]),
        "right": str(raw[3]),
        "bottom": str(raw[4]),
    }


def _target_geometry(
    anchor: ET.Element,
    current_id: str,
    current_geometry: dict[str, str],
    element_index: dict[str, dict[str, str]],
) -> dict[str, str] | None:
    target = anchor.get("target")
    if target == "self":
        return current_geometry
    if target != "element":
        return None
    target_id = anchor.get("targetId") or _target_id_by_name(anchor.get("targetName", ""), element_index)
    if not target_id or target_id == current_id:
        return current_geometry if target_id == current_id else None
    entry = element_index.get(target_id)
    return entry if entry is not None else None


def _target_id_by_name(name: str, element_index: dict[str, dict[str, str]]) -> str:
    for target_id, entry in element_index.items():
        if entry.get("name") == name:
            return target_id
    return ""


def _coordinate_value(geometry: dict[str, str], coordinate: str) -> int | None:
    if coordinate in {"top", "bottom", "left", "right"}:
        return _int_value(geometry.get(coordinate))
    if coordinate == "verticalCenter":
        top = _int_value(geometry.get("top"))
        bottom = _int_value(geometry.get("bottom"))
        return None if top is None or bottom is None else (top + bottom) // 2
    if coordinate == "horizontalCenter":
        left = _int_value(geometry.get("left"))
        right = _int_value(geometry.get("right"))
        return None if left is None or right is None else (left + right) // 2
    return None


def _edge_value(geometry: dict[str, str], side: str) -> int | None:
    if side in {"top", "bottom", "left", "right"}:
        return _int_value(geometry.get(side))
    if side == "width":
        left = _int_value(geometry.get("left"))
        right = _int_value(geometry.get("right"))
        return None if left is None or right is None else right - left
    if side == "height":
        top = _int_value(geometry.get("top"))
        bottom = _int_value(geometry.get("bottom"))
        return None if top is None or bottom is None else bottom - top
    return None


def _int_value(value: str | None) -> int | None:
    try:
        return int(value) if value is not None else None
    except ValueError:
        return None
