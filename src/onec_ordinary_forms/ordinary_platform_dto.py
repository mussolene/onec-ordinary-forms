"""DTO bridge between platform objects and public ordinary Form.xml.

The public XML shape is defined by ``OrdinaryForm.xsd`` and its embedded
``PlatformPalette`` annotations. This module connects that vocabulary to the
internal ``OrdinaryPlatformObject`` without making bracket/list-stream data
part of the XML contract.
"""

from __future__ import annotations

import copy
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from onec_ordinary_forms.bracket import control_index_from_list_stream_root
from onec_ordinary_forms.cli import (
    ORDINARY_FORM_SCHEMA,
    SCHEMA_VERSION,
    XSI_NS,
    add_form_events,
    add_form_properties,
    add_multilang_text,
    add_semantic_pages,
    add_type,
    add_type_from_domain_record,
    attribute_control_data_flag,
    attribute_record_name,
    attribute_records_from_form_root,
    attribute_slots_from_form_root,
    build_element_index,
    form_root_title_parts,
    metadata_object_type_map,
    pattern_node_from_prop,
    pretty_xml_bytes,
)
from onec_ordinary_forms.ordinary_platform_graph import (
    OrdinaryPlatformObject,
    platform_object_from_list_stream_text,
)
from onec_ordinary_forms.ordinary_platform_object import PlatformFormObject
from onec_ordinary_forms.ordinary_properties import (
    ORDINARY_CONTROL_DESCRIPTORS,
    load_platform_palette,
)
from onec_ordinary_forms.ordinary_stream import (
    CONTROL_INFO_SLOT_DESCRIPTORS,
    control_stream_from_xml,
    form_stream_from_object_xml,
    type_domain_pattern_from_xml,
)
from onec_ordinary_forms.value_codec import clean_atom


CONTROL_XML_TAG_BY_PLATFORM_TYPE = {
    control_type: descriptor.xml_tag
    for control_type, descriptor in ORDINARY_CONTROL_DESCRIPTORS.items()
}
PLATFORM_TYPE_BY_CONTROL_XML_TAG = {
    descriptor.xml_tag: control_type
    for control_type, descriptor in ORDINARY_CONTROL_DESCRIPTORS.items()
}
INCREMENTAL_SCALAR_PROPERTIES = {
    "Button": {"DefaultButton"},
    "ChoiceField": {"ChoiceButton", "ClearButton", "OpenButton", "ChoiceListOrCreateButton", "EditButton"},
    "CommandBar": {"Autofill"},
    "Image": {"DisplayMode", "DisplayState", "RenderingProfileFlag"},
    "Label": {"Hyperlink", "PictureSize", "TextPosition"},
    "ProgressBar": {"Orientation", "MinimumValue", "MaximumValue", "Step", "BigStep", "ShowPercent", "DisplayStyle"},
    "Splitter": {"Orientation"},
    "TrackBar": {"MinimumValue", "MaximumValue", "Step", "BigStep", "Orientation", "MarkStep", "CurrentValue"},
}


def platform_palette_catalog(*, include_nested: bool = True) -> dict[str, dict[str, object]]:
    """Return platform object/property/event descriptions embedded in the XSD."""

    return load_platform_palette(include_nested=include_nested)


def ordinary_form_xml_from_platform_object(
    platform_object: OrdinaryPlatformObject,
    *,
    metadata: dict[str, object] | None = None,
    asset_root: Path | None = None,
    container_created_ticks: int = 0,
    container_modified_ticks: int = 0,
) -> ET.Element:
    """Materialize a platform object as public, XSD-shaped ordinary Form.xml."""

    if asset_root is not None:
        return _ordinary_form_xml_from_platform_object(
            platform_object,
            metadata=metadata,
            asset_root=asset_root,
            container_created_ticks=container_created_ticks,
            container_modified_ticks=container_modified_ticks,
        )
    with tempfile.TemporaryDirectory(prefix="onec-ordinary-form-assets-") as temp_dir:
        return _ordinary_form_xml_from_platform_object(
            platform_object,
            metadata=metadata,
            asset_root=Path(temp_dir),
            container_created_ticks=container_created_ticks,
            container_modified_ticks=container_modified_ticks,
        )


def ordinary_form_xml_bytes_from_platform_object(
    platform_object: OrdinaryPlatformObject,
    *,
    metadata: dict[str, object] | None = None,
    asset_root: Path | None = None,
    container_created_ticks: int = 0,
    container_modified_ticks: int = 0,
) -> bytes:
    """Serialize a platform object as pretty public ordinary Form.xml bytes."""

    root = ordinary_form_xml_from_platform_object(
        platform_object,
        metadata=metadata,
        asset_root=asset_root,
        container_created_ticks=container_created_ticks,
        container_modified_ticks=container_modified_ticks,
    )
    return pretty_xml_bytes(root)


def platform_object_from_ordinary_form_xml(
    root: ET.Element,
    base_object: OrdinaryPlatformObject,
) -> OrdinaryPlatformObject:
    """Apply supported public Form.xml edits back to an existing platform object."""

    if root.tag != "Form":
        raise ValueError(f"Expected Form root, got {root.tag!r}")
    xml_controls = _public_control_elements(root)
    base_controls = {control.node_id: control for control in base_object.flatten_controls()}
    xml_ids = _public_control_ids(xml_controls)

    for element in xml_controls:
        node_id = _public_control_node_id(element)
        control = base_controls.get(node_id)
        if control is None:
            continue
        expected_tag = CONTROL_XML_TAG_BY_PLATFORM_TYPE.get(control.control_type, control.control_type)
        if element.tag != expected_tag:
            raise ValueError(
                f"Control type mismatch for {control.node_id}: "
                f"expected XML tag {expected_tag!r}, got {element.tag!r}"
            )

    editor = PlatformFormObject(base_object)
    for element in xml_controls:
        node_id = _public_control_node_id(element)
        if node_id not in base_controls:
            continue
        current = editor.to_platform_object().control(node_id)
        name = element.get("name", current.name)
        title = _localized_title_from_xml(element)
        name_update = name if name != current.name else None
        title_update = title if title is not None and title != current.title else None
        if name_update is not None and title_update is not None:
            editor = editor.rename_control(node_id, name_update, title=title_update)
        elif name_update is not None:
            editor = editor.rename_control(node_id, name_update)
        elif title_update is not None:
            editor = editor.set_control_title(node_id, title_update)
        for property_name, value in _public_control_property_updates(element):
            editor = editor.set_control_property(node_id, property_name, value)
    updated = editor.to_platform_object()
    return _apply_public_control_tree(root, updated, base_controls)


def platform_object_from_ordinary_form_xml_text(
    text: str,
    base_object: OrdinaryPlatformObject,
) -> OrdinaryPlatformObject:
    """Parse public Form.xml text and apply supported edits to a platform object."""

    return platform_object_from_ordinary_form_xml(ET.fromstring(text), base_object)


def _apply_public_control_tree(
    root: ET.Element,
    base_object: OrdinaryPlatformObject,
    original_controls: dict[str, object],
) -> OrdinaryPlatformObject:
    base_parent_by_id = _platform_parent_map(base_object)
    xml_parent_by_id = _xml_parent_map(root)
    for node_id, parent_id in xml_parent_by_id.items():
        if node_id in original_controls and base_parent_by_id.get(node_id) != parent_id:
            raise ValueError(f"Moving existing controls between parents is not implemented yet: {node_id}")
    for node_id, parent_id in base_parent_by_id.items():
        if node_id in xml_parent_by_id and xml_parent_by_id[node_id] != parent_id:
            raise ValueError(f"Moving existing controls between parents is not implemented yet: {node_id}")

    attribute_type_patterns, attribute_slots = _attribute_type_maps(root)
    top_level_elements = _public_top_level_control_elements(root)
    existing_by_id = {control.object_id: control for control in base_object.flatten_controls()}
    existing_raw_by_id = {control.object_id: control.raw for control in base_object.model.flatten()}
    updated = base_object.with_top_level_control_records(
        _control_records_from_elements(
            top_level_elements,
            existing_by_id,
            existing_raw_by_id,
            attribute_type_patterns,
            attribute_slots,
        )
    )
    for element in _public_control_elements(root):
        object_id = element.get("id", "")
        if object_id not in existing_by_id:
            continue
        child_records = _control_records_from_elements(
            _public_direct_child_control_elements(element),
            {control.object_id: control for control in updated.flatten_controls()},
            {control.object_id: control.raw for control in updated.model.flatten()},
            attribute_type_patterns,
            attribute_slots,
        )
        updated = updated.with_control_child_records(f"control:{object_id}", child_records)
    return updated


def _control_records_from_elements(
    elements: tuple[ET.Element, ...],
    existing_by_id: dict[str, object],
    existing_raw_by_id: dict[str, list[object]],
    attribute_type_patterns: dict[str, list[object]],
    attribute_slots: dict[str, str],
) -> list[list[object]]:
    result: list[list[object]] = []
    for element in elements:
        object_id = element.get("id", "")
        existing = existing_by_id.get(object_id)
        if existing is not None:
            expected_tag = CONTROL_XML_TAG_BY_PLATFORM_TYPE.get(existing.control_type, existing.control_type)
            if element.tag != expected_tag:
                raise ValueError(
                    f"Control type mismatch for control:{object_id}: "
                    f"expected XML tag {expected_tag!r}, got {element.tag!r}"
                )
            result.append(copy.deepcopy(existing_raw_by_id[object_id]))
            continue
        stream = control_stream_from_xml(element, None, attribute_type_patterns, attribute_slots)
        if stream is not None:
            result.append(stream)
    return result


def _platform_parent_map(platform_object: OrdinaryPlatformObject) -> dict[str, str]:
    result: dict[str, str] = {}

    def walk(control: object, parent_id: str) -> None:
        result[control.node_id] = parent_id
        for child in control.children:
            walk(child, control.node_id)

    for control in platform_object.controls:
        walk(control, "")
    return result


def _xml_parent_map(root: ET.Element) -> dict[str, str]:
    result: dict[str, str] = {}

    def walk(parent: ET.Element, parent_id: str) -> None:
        for child in _public_direct_child_control_elements(parent):
            node_id = _public_control_node_id(child)
            result[node_id] = parent_id
            walk(child, node_id)

    pages = root.find("Pages")
    if pages is not None:
        for page in pages.findall("Page"):
            walk(page, "")
    return result


def _attribute_type_maps(root: ET.Element) -> tuple[dict[str, list[object]], dict[str, str]]:
    attribute_type_patterns: dict[str, list[object]] = {}
    attribute_slots: dict[str, str] = {}
    for attribute in root.findall("./Attributes/Attribute"):
        name = attribute.get("name", "")
        if not name:
            continue
        attribute_type_patterns[name] = type_domain_pattern_from_xml(attribute)
        if attribute.get("slot"):
            attribute_slots[name] = attribute.get("slot", "")
    return attribute_type_patterns, attribute_slots


def _public_control_property_updates(element: ET.Element) -> tuple[tuple[str, object], ...]:
    updates: list[tuple[str, object]] = []
    handled = {
        "Title",
        "Visible",
        "Enabled",
        "ReadOnly",
        "TextColor",
        "BackColor",
        "BorderColor",
        "Font",
        "Position",
    }
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
    control_type = PLATFORM_TYPE_BY_CONTROL_XML_TAG.get(element.tag, element.tag)
    descriptor = CONTROL_INFO_SLOT_DESCRIPTORS.get(control_type)
    public_descriptor = ORDINARY_CONTROL_DESCRIPTORS.get(control_type)
    if descriptor is not None and public_descriptor is not None:
        slot_names = {slot.name for slot in descriptor.slots}
        writable = INCREMENTAL_SCALAR_PROPERTIES.get(control_type, set())
        for property_name in public_descriptor.properties:
            if property_name in handled or property_name not in writable or property_name not in slot_names:
                continue
            node = _last_child(element, property_name)
            if node is not None and len(node) == 0:
                updates.append((property_name, node.text or ""))
    return tuple(updates)


def _last_child(element: ET.Element, tag: str) -> ET.Element | None:
    nodes = element.findall(tag)
    return nodes[-1] if nodes else None


def platform_object_from_ordinary_form_xml_rebuild(
    root: ET.Element,
    *,
    asset_root: Path | None = None,
) -> OrdinaryPlatformObject:
    """Rebuild a platform object from public Form.xml through the canonical writer."""

    form_text = form_stream_from_object_xml(root, asset_root).decode("utf-8-sig")
    return platform_object_from_list_stream_text(form_text)


def platform_object_from_ordinary_form_xml_text_rebuild(
    text: str,
    *,
    asset_root: Path | None = None,
) -> OrdinaryPlatformObject:
    """Parse public Form.xml text and rebuild a platform object with all writer mappings."""

    return platform_object_from_ordinary_form_xml_rebuild(ET.fromstring(text), asset_root=asset_root)


def _ordinary_form_xml_from_platform_object(
    platform_object: OrdinaryPlatformObject,
    *,
    metadata: dict[str, object] | None,
    asset_root: Path,
    container_created_ticks: int,
    container_modified_ticks: int,
) -> ET.Element:
    form_root = platform_object.to_list_stream_root()
    control_index = control_index_from_list_stream_root(form_root)
    object_types = metadata_object_type_map(metadata)
    element_index = build_element_index(control_index)
    root_title_lang, root_title = form_root_title_parts(form_root)

    root = ET.Element("Form")
    root.set("version", SCHEMA_VERSION)
    root.set(f"{{{XSI_NS}}}noNamespaceSchemaLocation", ORDINARY_FORM_SCHEMA)
    root.set("containerCreatedTicks", str(container_created_ticks))
    root.set("containerModifiedTicks", str(container_modified_ticks))
    if root_title:
        add_multilang_text(root, "Title", root_title, lang=root_title_lang)
    add_form_properties(root, form_root)
    add_form_events(root, form_root)
    _add_attributes(root, form_root, control_index, object_types)
    add_semantic_pages(root, control_index, element_index, asset_root)
    return root


def _add_attributes(
    root: ET.Element,
    form_root: object,
    control_index: dict[str, object],
    object_types: dict[str, str],
) -> None:
    attrs = ET.SubElement(root, "Attributes")
    attribute_slots = attribute_slots_from_form_root(form_root)
    props_by_name = {
        str(prop.get("name", "")): prop
        for prop in control_index.get("props", [])
        if isinstance(prop, dict)
    }
    form_attribute_records = attribute_records_from_form_root(form_root)
    if form_attribute_records:
        for record in form_attribute_records:
            prop_name = attribute_record_name(record)
            if not prop_name:
                continue
            prop = props_by_name.get(prop_name, {})
            attr = ET.SubElement(attrs, "Attribute")
            attr.set("name", prop_name)
            attr.set("id", str(prop.get("id", "")))
            if prop_name in attribute_slots:
                attr.set("slot", attribute_slots[prop_name])
            if len(record) > 1 and clean_atom(record[1]) == "0":
                attr.set("controlData", "false")
            if len(record) >= 2:
                add_type_from_domain_record(attr, record[-1], object_types)
        return

    for prop in control_index.get("props", []):
        if not isinstance(prop, dict):
            continue
        prop_name = str(prop.get("name", ""))
        if not prop_name:
            continue
        attr = ET.SubElement(attrs, "Attribute")
        attr.set("name", prop_name)
        attr.set("id", str(prop.get("id", "")))
        if prop_name in attribute_slots:
            attr.set("slot", attribute_slots[prop_name])
        if attribute_control_data_flag(form_root, prop_name) == "0":
            attr.set("controlData", "false")
        pattern = pattern_node_from_prop(prop)
        add_type(attr, pattern, object_types)


def _public_control_elements(root: ET.Element) -> tuple[ET.Element, ...]:
    result: list[ET.Element] = []
    pages = root.find("Pages")
    if pages is not None:
        for page in pages.findall("Page"):
            _collect_public_controls(page, result)
    return tuple(result)


def _public_top_level_control_elements(root: ET.Element) -> tuple[ET.Element, ...]:
    result: list[ET.Element] = []
    pages = root.find("Pages")
    if pages is None:
        return ()
    for page in pages.findall("Page"):
        result.extend(child for child in list(page) if child.tag in PLATFORM_TYPE_BY_CONTROL_XML_TAG)
    return tuple(result)


def _public_direct_child_control_elements(element: ET.Element) -> tuple[ET.Element, ...]:
    result = [child for child in list(element) if child.tag in PLATFORM_TYPE_BY_CONTROL_XML_TAG]
    pages = element.find("Pages")
    if pages is not None:
        for page in pages.findall("Page"):
            result.extend(child for child in list(page) if child.tag in PLATFORM_TYPE_BY_CONTROL_XML_TAG)
    return tuple(result)


def _collect_public_controls(parent: ET.Element, result: list[ET.Element]) -> None:
    for child in list(parent):
        if child.tag in PLATFORM_TYPE_BY_CONTROL_XML_TAG:
            result.append(child)
            _collect_public_controls(child, result)
            pages = child.find("Pages")
            if pages is not None:
                for page in pages.findall("Page"):
                    _collect_public_controls(page, result)


def _public_control_ids(elements: tuple[ET.Element, ...]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for element in elements:
        node_id = _public_control_node_id(element)
        if node_id in seen:
            raise ValueError(f"Duplicate control id in public XML: {node_id}")
        seen.add(node_id)
        result.append(node_id)
    return result


def _public_control_node_id(element: ET.Element) -> str:
    object_id = element.get("id")
    if not object_id:
        raise ValueError(f"Control {element.tag} is missing id")
    return f"control:{object_id}"


def _localized_title_from_xml(element: ET.Element) -> str | None:
    title = element.find("Title")
    if title is None:
        return None
    item = title.find("Item[@lang='ru']")
    if item is None:
        item = title.find("Item")
    if item is None:
        return title.text or ""
    return item.text or ""
