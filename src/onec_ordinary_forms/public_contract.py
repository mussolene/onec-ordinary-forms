"""Public ordinary ``Form.xml`` contract checks.

The bundled XSD is not enough to protect the user-facing source contract while
the writer still contains internal compatibility fields. These checks reject
legacy/raw stream shapes before validation or build code treats them as public
XML.
"""

from __future__ import annotations

import xml.etree.ElementTree as ET


V1_CONTRACT_VERSION = "1.0"

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
        "ChildItems",
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
    }
)


def local_xml_name(name: str) -> str:
    if "}" in name:
        return name.rsplit("}", 1)[1]
    return name


def assert_v1_public_form_xml(root: ET.Element) -> None:
    """Reject pre-v1 and raw-shape public ordinary form XML."""

    if local_xml_name(root.tag) != "Form":
        raise ValueError("Expected public ordinary form XML root <Form>")

    version = root.get("version")
    if version and version.startswith("0."):
        raise ValueError("unsupported pre-v1 Form.xml; re-dump from Form.bin")
    if root.get("ordinaryFormVersion") is not None:
        raise ValueError("OrdinaryFormV2 XML is internal-only; re-dump as v1 Form.xml")

    schema_location = root.get("{http://www.w3.org/2001/XMLSchema-instance}noNamespaceSchemaLocation", "")
    if "OrdinaryFormV2.xsd" in schema_location:
        raise ValueError("OrdinaryFormV2 XML is internal-only; re-dump as v1 Form.xml")

    for element in root.iter():
        tag = local_xml_name(element.tag)
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
