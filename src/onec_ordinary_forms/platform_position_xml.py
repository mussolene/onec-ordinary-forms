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


def apply_position_node_to_control_record(position: ET.Element, raw: list[object]) -> bool:
    geometry = control_geometry_record(raw)
    if geometry is None:
        return False
    position = copy.deepcopy(position)
    _normalize_edited_bindings(position)
    for attr_name, slot in POSITION_COORDINATE_SLOTS.items():
        value = position.get(attr_name)
        if value is not None and len(geometry) > slot:
            geometry[slot] = value
    apply_geometry_bindings_to_raw(position, geometry)
    return True


def _normalize_edited_bindings(position: ET.Element) -> None:
    bindings = position.find("Bindings")
    if bindings is None:
        return
    for binding in list(bindings.findall("Binding")) + list(bindings.findall("DimensionBinding")):
        if binding.get("mode") is not None or len(binding):
            binding.attrib.pop("value", None)
