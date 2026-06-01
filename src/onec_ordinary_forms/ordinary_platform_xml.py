"""Internal XML transfer view for ordinary-form platform objects.

This module materializes ``OrdinaryPlatformObject`` as named platform-object
concepts and applies XML edits back to the platform object. It intentionally
does not expose the underlying ListOutStream/bracket tree, binary payloads, or
raw indexed records.
"""

from __future__ import annotations

import copy
import xml.etree.ElementTree as ET

from onec_ordinary_forms.ordinary_platform_graph import (
    OrdinaryPlatformControlObject,
    OrdinaryPlatformObject,
    UnsupportedPlatformObjectOperation,
)
from onec_ordinary_forms.ordinary_platform_object import PlatformFormObject
from onec_ordinary_forms.platform_value_xml import (
    add_color_node_from_record,
    add_font_node_from_record,
    is_default_color_record,
    is_default_font_record,
)


ROOT_TAG = "OrdinaryPlatformObject"
FORMAT_VERSION = "1"


def platform_object_to_xml(platform_object: OrdinaryPlatformObject) -> ET.Element:
    """Materialize a platform object as an internal XML object-transfer view."""

    root = ET.Element(
        ROOT_TAG,
        {
            "version": FORMAT_VERSION,
            "usable": _xml_bool(platform_object.can_use_object_model),
        },
    )
    controls = ET.SubElement(root, "Controls")
    for control in platform_object.controls:
        controls.append(_control_to_xml(control, platform_object))
    if platform_object.diagnostics:
        diagnostics = ET.SubElement(root, "Diagnostics")
        for item in platform_object.diagnostics:
            attrs = {
                "severity": item.severity,
                "code": item.code,
            }
            if item.node_id:
                attrs["nodeId"] = item.node_id
            diagnostic = ET.SubElement(diagnostics, "Diagnostic", attrs)
            diagnostic.text = item.message
    return root


def platform_object_xml_to_string(
    platform_object: OrdinaryPlatformObject,
    *,
    include_declaration: bool = True,
) -> str:
    """Serialize the internal platform-object XML view as text."""

    root = platform_object_to_xml(platform_object)
    return platform_object_xml_element_to_string(root, include_declaration=include_declaration)


def platform_object_xml_element_to_string(
    root: ET.Element,
    *,
    include_declaration: bool = True,
) -> str:
    """Serialize an existing platform-object XML element as text."""

    document = copy.deepcopy(root)
    ET.indent(document, space="  ")
    text = ET.tostring(document, encoding="unicode", short_empty_elements=True)
    return f"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n{text}\n" if include_declaration else f"{text}\n"


def platform_object_from_xml(
    root: ET.Element,
    base_object: OrdinaryPlatformObject,
) -> OrdinaryPlatformObject:
    """Apply object XML edits to an existing platform object.

    The XML view carries stable object identity and currently writable
    properties. Unknown platform slots remain in ``base_object`` and are
    preserved when the updated object is serialized back to ListOutStream.
    """

    if root.tag != ROOT_TAG:
        raise ValueError(f"Expected {ROOT_TAG} root, got {root.tag!r}")
    version = root.get("version", FORMAT_VERSION)
    if version != FORMAT_VERSION:
        raise ValueError(f"Unsupported {ROOT_TAG} version: {version}")

    xml_controls = _xml_controls(root)
    base_controls = {control.node_id: control for control in base_object.flatten_controls()}
    xml_ids = _xml_control_ids(xml_controls)
    base_ids = set(base_controls)
    if set(xml_ids) != base_ids:
        missing = sorted(base_ids - set(xml_ids))
        extra = sorted(set(xml_ids) - base_ids)
        raise ValueError(f"Control set mismatch: missing={missing}, extra={extra}")

    _validate_control_tree(root, base_object.controls)
    for control_element in xml_controls:
        control = base_controls[control_element.get("nodeId", "")]
        _validate_control_identity(control_element, control)

    editor = PlatformFormObject(base_object)
    for control_element in xml_controls:
        node_id = control_element.get("nodeId", "")
        current = editor.to_platform_object().control(node_id)
        name = control_element.get("name", current.name)
        title = _title_from_xml(control_element)
        name_update = name if name != current.name else None
        title_update = title if title is not None and title != current.title else None
        if name_update is not None and title_update is not None:
            editor = editor.rename_control(node_id, name_update, title=title_update)
        elif name_update is not None:
            editor = editor.rename_control(node_id, name_update)
        elif title_update is not None:
            editor = editor.set_control_title(node_id, title_update)
        for property_name, value in _control_property_updates(control_element):
            editor = editor.set_control_property(node_id, property_name, value)
    return editor.to_platform_object()


def platform_object_from_xml_text(
    text: str,
    base_object: OrdinaryPlatformObject,
) -> OrdinaryPlatformObject:
    """Parse object XML text and apply it to an existing platform object."""

    return platform_object_from_xml(ET.fromstring(text), base_object)


def _control_to_xml(
    control: OrdinaryPlatformControlObject,
    platform_object: OrdinaryPlatformObject,
) -> ET.Element:
    element = ET.Element(
        "Control",
        {
            "nodeId": control.node_id,
            "objectId": control.object_id,
            "name": control.name,
            "type": control.control_type,
            "classId": control.class_id,
            "infoKind": control.info_kind,
            "declaredChildCount": str(control.declared_child_count),
            "actualChildCount": str(control.actual_child_count),
            "stateCount": str(control.state_count),
            "positionRecordCount": str(control.position_record_count),
        },
    )
    title = ET.SubElement(element, "Title")
    title_item = ET.SubElement(title, "Item", {"lang": "ru"})
    title_item.text = control.title
    _add_control_properties(element, control, platform_object)
    if control.state_names:
        states = ET.SubElement(element, "States")
        for name in control.state_names:
            ET.SubElement(states, "State", {"name": name})
    if control.children:
        children = ET.SubElement(element, "Controls")
        for child in control.children:
            children.append(_control_to_xml(child, platform_object))
    return element


def _add_control_properties(
    element: ET.Element,
    control: OrdinaryPlatformControlObject,
    platform_object: OrdinaryPlatformObject,
) -> None:
    for attr_name in ("baseStyleMode", "baseStyleState", "baseStyleVisible", "baseStyleDefaultMode"):
        value = _optional_control_property(platform_object, control.node_id, attr_name)
        if value is not None:
            element.set(attr_name, value)
    for property_name, default in (("Visible", "true"), ("Enabled", "true"), ("ReadOnly", "false")):
        value = _optional_control_property(platform_object, control.node_id, property_name)
        if value is not None and value != default:
            node = ET.SubElement(element, property_name)
            node.text = str(value)
    for property_name in ("TextColor", "BackColor", "BorderColor"):
        value = _optional_control_property(platform_object, control.node_id, property_name)
        if value is not None and not is_default_color_record(value):
            add_color_node_from_record(element, property_name, value)
    font = _optional_control_property(platform_object, control.node_id, "Font")
    if font is not None and not is_default_font_record(font):
        add_font_node_from_record(element, font)
    position = _optional_control_property(platform_object, control.node_id, "Position")
    if isinstance(position, ET.Element):
        element.append(copy.deepcopy(position))


def _optional_control_property(
    platform_object: OrdinaryPlatformObject,
    node_id: str,
    property_name: str,
) -> object | None:
    try:
        return platform_object.control_property(node_id, property_name)
    except UnsupportedPlatformObjectOperation:
        return None


def _xml_controls(root: ET.Element) -> tuple[ET.Element, ...]:
    controls = root.find("Controls")
    if controls is None:
        return ()
    result: list[ET.Element] = []
    _collect_control_elements(controls, result)
    return tuple(result)


def _collect_control_elements(container: ET.Element, result: list[ET.Element]) -> None:
    for control in container.findall("Control"):
        result.append(control)
        children = control.find("Controls")
        if children is not None:
            _collect_control_elements(children, result)


def _xml_control_ids(xml_controls: tuple[ET.Element, ...]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for control in xml_controls:
        node_id = control.get("nodeId", "")
        if not node_id:
            raise ValueError("Control element is missing nodeId")
        if node_id in seen:
            raise ValueError(f"Duplicate control nodeId in XML: {node_id}")
        seen.add(node_id)
        result.append(node_id)
    return result


def _validate_control_tree(
    root: ET.Element,
    controls: tuple[OrdinaryPlatformControlObject, ...],
) -> None:
    xml_controls = root.find("Controls")
    top_level = xml_controls.findall("Control") if xml_controls is not None else []
    expected = [control.node_id for control in controls]
    actual = [control.get("nodeId", "") for control in top_level]
    if actual != expected:
        raise ValueError(f"Top-level control order mismatch: expected={expected}, actual={actual}")
    for xml_control, control in zip(top_level, controls):
        _validate_control_children(xml_control, control)


def _validate_control_children(
    xml_control: ET.Element,
    control: OrdinaryPlatformControlObject,
) -> None:
    xml_children = xml_control.find("Controls")
    child_elements = xml_children.findall("Control") if xml_children is not None else []
    expected = [child.node_id for child in control.children]
    actual = [child.get("nodeId", "") for child in child_elements]
    if actual != expected:
        raise ValueError(
            f"Control child order mismatch for {control.node_id}: "
            f"expected={expected}, actual={actual}"
        )
    for child_element, child in zip(child_elements, control.children):
        _validate_control_children(child_element, child)


def _validate_control_identity(
    element: ET.Element,
    control: OrdinaryPlatformControlObject,
) -> None:
    _same_attr(element, "objectId", control.object_id)
    _same_attr(element, "type", control.control_type)
    _same_attr(element, "classId", control.class_id)
    _same_attr(element, "infoKind", control.info_kind)
    _same_attr(element, "declaredChildCount", str(control.declared_child_count))
    _same_attr(element, "actualChildCount", str(control.actual_child_count))
    _same_attr(element, "stateCount", str(control.state_count))
    _same_attr(element, "positionRecordCount", str(control.position_record_count))
    states = element.find("States")
    actual_state_names = [state.get("name", "") for state in states.findall("State")] if states is not None else []
    expected_state_names = list(control.state_names)
    if actual_state_names != expected_state_names:
        raise ValueError(
            f"State list mismatch for {control.node_id}: "
            f"expected={expected_state_names}, actual={actual_state_names}"
        )


def _same_attr(element: ET.Element, name: str, expected: str) -> None:
    actual = element.get(name)
    if actual is not None and actual != expected:
        node_id = element.get("nodeId", "")
        raise ValueError(f"Immutable attribute {name} changed for {node_id}: {expected!r} -> {actual!r}")


def _title_from_xml(element: ET.Element) -> str | None:
    title = element.find("Title")
    if title is None:
        return None
    item = title.find("Item[@lang='ru']")
    if item is None:
        item = title.find("Item")
    if item is None:
        return title.text or ""
    return item.text or ""


def _control_property_updates(element: ET.Element) -> tuple[tuple[str, object], ...]:
    updates: list[tuple[str, object]] = []
    for attr_name in ("baseStyleMode", "baseStyleState", "baseStyleVisible", "baseStyleDefaultMode"):
        value = element.get(attr_name)
        if value is not None:
            updates.append((attr_name, value))
    for property_name in ("Visible", "Enabled", "ReadOnly"):
        node = _last_child(element, property_name)
        if node is not None:
            updates.append((property_name, node.text or ""))
    for property_name in ("TextColor", "BackColor", "BorderColor", "Font", "Position"):
        node = _last_child(element, property_name)
        if node is not None:
            updates.append((property_name, node))
    return tuple(updates)


def _last_child(element: ET.Element, tag: str) -> ET.Element | None:
    nodes = element.findall(tag)
    return nodes[-1] if nodes else None


def _xml_bool(value: bool) -> str:
    return "true" if value else "false"
