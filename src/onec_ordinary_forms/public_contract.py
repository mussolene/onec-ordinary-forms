"""Public ordinary ``Form.xml`` contract checks.

The bundled public XSD is not enough to protect the user-facing source contract
while the writer still contains internal compatibility fields. These checks
reject legacy Pages-based XML, deprecated version attributes, and raw stream
shapes before validation or build code treats them as public XML.
"""

from __future__ import annotations

from functools import lru_cache
import xml.etree.ElementTree as ET

from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS

PUBLIC_FORM_VERSION = "2.0"
ORDINARY_FORM_SCHEMA = "OrdinaryForm.xsd"
XSI_NS = "http://www.w3.org/2001/XMLSchema-instance"

FORBIDDEN_PUBLIC_XML_ELEMENTS = frozenset(
    {
        "ObjectModel",
        "ListStream",
        "BracketStream",
        "FormBin",
        "LogicalStream",
        "RawBracket",
        "PlatformRecords",
        "SerializationProfile",
        "DataSourceProfile",
        "ViewProfile",
        "StateBlob",
        "ValueDescriptor",
    }
)

FORBIDDEN_PUBLIC_XML_ATTRIBUTES = frozenset(
    {
        "profileUuid",
        "actionProfileState",
        "actionProfileFlag1",
        "actionProfileFlag2",
        "linkModeShape",
        "rawKey",
        "recordKind",
        "recordSubKind",
        "tailKind",
        "rootKind",
        "rootFlag",
        "slot",
        "slotCount",
        "version",
    }
)

PUBLIC_FORM_CHILD_ELEMENTS = frozenset(
    {
        "Title",
        "Width",
        "Height",
        "AutoTitle",
        "Group",
        "CommandBarLocation",
        "VerticalScroll",
        "ShowCommandBar",
        "AutoCommandBar",
        "Events",
        "Attributes",
        "ChildItems",
        "Commands",
        "SerializationCounter",
        "RootPanelLayout",
    }
)

PUBLIC_PAGE_CHILD_ELEMENTS = frozenset({"Title", "ChildItems"})
INTERNAL_OR_LEGACY_PUBLIC_PROPERTIES = frozenset({"Pages"})
LEGACY_CONTROL_TAGS = frozenset(
    {
        "DocumentField",
        "FormattedDocumentField",
        "GeographicalSchema",
        "UsualGroup",
    }
)


def local_xml_name(name: str) -> str:
    if "}" in name:
        return name.rsplit("}", 1)[1]
    return name


@lru_cache(maxsize=1)
def public_control_tags() -> frozenset[str]:
    tags = {descriptor.xml_tag for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()}
    return frozenset(tags | LEGACY_CONTROL_TAGS)


@lru_cache(maxsize=1)
def public_control_property_names() -> dict[str, frozenset[str]]:
    common_properties: set[str] = set()
    result: dict[str, frozenset[str]] = {}
    for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values():
        properties = set(descriptor.properties)
        common_properties.update(properties)
        properties.difference_update(INTERNAL_OR_LEGACY_PUBLIC_PROPERTIES)
        properties.add("ChildItems")
        result[descriptor.xml_tag] = frozenset(properties)
    common_properties.difference_update(INTERNAL_OR_LEGACY_PUBLIC_PROPERTIES)
    common_properties.add("ChildItems")
    for legacy_tag in LEGACY_CONTROL_TAGS:
        result[legacy_tag] = frozenset(common_properties)
    return result


def form_child_items(parent: ET.Element, *, create: bool = False) -> ET.Element | None:
    for child in parent:
        if local_xml_name(child.tag) == "ChildItems":
            return child
    if create:
        return ET.SubElement(parent, "ChildItems")
    return None


def top_level_pages(root: ET.Element) -> list[ET.Element]:
    child_items = form_child_items(root)
    if child_items is None:
        return []
    return [element for element in child_items if local_xml_name(element.tag) == "Page"]


def panel_pages(element: ET.Element) -> list[ET.Element]:
    child_items = form_child_items(element)
    if child_items is None:
        return []
    return [page for page in child_items if local_xml_name(page.tag) == "Page"]


def iter_page_controls(page: ET.Element):
    child_items = form_child_items(page)
    if child_items is not None:
        for child in child_items:
            yield child
        return
    for child in page:
        if local_xml_name(child.tag) in {"Title", "ChildItems"}:
            continue
        yield child


def iter_control_elements(parent: ET.Element):
    child_items = form_child_items(parent)
    if child_items is not None:
        for child in child_items:
            yield child
        return
    for child in parent:
        tag = local_xml_name(child.tag)
        if tag in {"Title", "Position", "Events", "ChildItems", "Attributes", "Commands", "Width", "Height"}:
            continue
        yield child


def assert_public_form_xml(root: ET.Element, *, require_version: bool = True) -> None:
    """Reject legacy Pages-based XML, deprecated version attrs, and raw shapes."""

    if local_xml_name(root.tag) != "Form":
        raise ValueError("Expected public ordinary form XML root <Form>")

    if root.get("version") is not None:
        raise ValueError("deprecated Form.xml version attribute; use ordinaryFormVersion from Form.bin dump")

    form_version = root.get("ordinaryFormVersion")
    if require_version and (not form_version or not form_version.startswith("2.")):
        raise ValueError('public Form.xml must declare ordinaryFormVersion="2.*"')
    if form_version and not form_version.startswith("2."):
        raise ValueError('public Form.xml must declare ordinaryFormVersion="2.*"')

    schema_location = root.get(f"{{{XSI_NS}}}noNamespaceSchemaLocation", "")
    if schema_location and ORDINARY_FORM_SCHEMA not in schema_location:
        raise ValueError(f"public Form.xml must reference {ORDINARY_FORM_SCHEMA}")

    for element in root.iter():
        tag = local_xml_name(element.tag)
        if tag == "Pages":
            raise ValueError("deprecated <Pages> container; use <ChildItems> with <Page> elements")
        if tag in FORBIDDEN_PUBLIC_XML_ELEMENTS:
            raise ValueError(f"Public ordinary Form.xml must not contain <{tag}>")
        if tag == "Value" and any(name in element.attrib for name in ("kind", "count", "relation")):
            raise ValueError("Public ordinary Form.xml must not contain indexed raw <Value> records")
        for attr_name in element.attrib:
            name = local_xml_name(attr_name)
            if name in FORBIDDEN_PUBLIC_XML_ATTRIBUTES:
                raise ValueError(f"Public ordinary Form.xml must not contain @{name}")
            if "profile" in name.lower():
                raise ValueError(f"Public ordinary Form.xml must not contain profile attribute @{name}")

    _assert_public_form_children(root)


def assert_public_object_xml(root: ET.Element) -> None:
    """Validate public object-model XML before direct ListOutStream writes."""

    assert_public_form_xml(root, require_version=False)


def _assert_public_form_children(root: ET.Element) -> None:
    for child in root:
        tag = local_xml_name(child.tag)
        if tag not in PUBLIC_FORM_CHILD_ELEMENTS:
            raise ValueError(f"unknown public Form child <{tag}>")
        if tag == "ChildItems":
            _assert_public_child_items(child)


def _assert_public_child_items(child_items: ET.Element) -> None:
    controls = public_control_tags()
    for child in child_items:
        tag = local_xml_name(child.tag)
        if tag == "Page":
            _assert_public_page(child)
        elif tag in controls:
            _assert_public_control(child)
        else:
            raise ValueError(f"unknown public ChildItems element <{tag}>")


def _assert_public_page(page: ET.Element) -> None:
    for child in page:
        tag = local_xml_name(child.tag)
        if tag not in PUBLIC_PAGE_CHILD_ELEMENTS:
            if tag in public_control_tags():
                raise ValueError("public controls under <Page> must be nested in <ChildItems>")
            raise ValueError(f"unknown public Page child <{tag}>")
        if tag == "ChildItems":
            _assert_public_child_items(child)


def _assert_public_control(control: ET.Element) -> None:
    control_tag = local_xml_name(control.tag)
    allowed_properties = public_control_property_names().get(control_tag, frozenset())
    controls = public_control_tags()
    for child in control:
        tag = local_xml_name(child.tag)
        if tag in controls:
            raise ValueError("nested public controls must be placed under <ChildItems>")
        if tag not in allowed_properties:
            raise ValueError(f"unknown public property <{tag}> for <{control_tag}>")
        if tag == "ChildItems":
            _assert_public_child_items(child)
