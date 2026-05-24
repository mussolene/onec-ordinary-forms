"""Platform object-model vocabulary used by the ordinary-form codec.

``schemas/PlatformConfigStructure.xsd`` is the bundled source for configuration
metadata, type tree, value-domain codes, and platform serializer evidence. This
module only reads that schema into Python tuples for codec code and tests.
"""

from __future__ import annotations

from dataclasses import dataclass
from importlib import resources
import xml.etree.ElementTree as ET


@dataclass(frozen=True)
class PlatformSchemaResource:
    source: str
    schema: str
    namespace: str
    path: str
    sha256: str


@dataclass(frozen=True)
class PlatformSerializer:
    name: str
    direction: str
    role: str


@dataclass(frozen=True)
class PlatformTypeDomainCode:
    code: str
    type_name: str
    kind: str


XS_NS = {"xs": "http://www.w3.org/2001/XMLSchema"}


def configuration_schema_root() -> ET.Element:
    data = resources.files(__package__).joinpath("schemas/PlatformConfigStructure.xsd").read_text(encoding="utf-8")
    return ET.fromstring(data)


_SCHEMA_ROOT = configuration_schema_root()


PLATFORM_SCHEMA_RESOURCES = tuple(
    PlatformSchemaResource(
        str(node.get("source")),
        str(node.get("schema")),
        str(node.get("namespace")),
        str(node.get("path")),
        str(node.get("sha256")),
    )
    for node in _SCHEMA_ROOT.findall(".//PlatformSchemaResources/Resource")
)


PLATFORM_SERIALIZERS = tuple(
    PlatformSerializer(str(node.get("name")), str(node.get("direction")), str(node.get("role")))
    for node in _SCHEMA_ROOT.findall(".//PlatformSerializers/Serializer")
)


PLATFORM_TYPE_DOMAIN_CODES = tuple(
    PlatformTypeDomainCode(str(node.get("value")), str(node.get("typeName")), str(node.get("kind")))
    for node in _SCHEMA_ROOT.findall(".//TypeDomainCodes/Code")
)


PLATFORM_TYPE_DOMAIN_CODE_NAMES = {item.code: item.type_name for item in PLATFORM_TYPE_DOMAIN_CODES}


PLATFORM_METADATA_OBJECT_KINDS = tuple(
    str(node.get("value"))
    for node in _SCHEMA_ROOT.findall(".//xs:simpleType[@name='MetadataObjectKind']/xs:restriction/xs:enumeration", XS_NS)
)


PLATFORM_TYPE_TREE_KINDS = tuple(
    str(node.get("value"))
    for node in _SCHEMA_ROOT.findall(".//xs:simpleType[@name='TypeTreeKind']/xs:restriction/xs:enumeration", XS_NS)
)


PLATFORM_EDT_METADATA_CLASSES = tuple(str(node.get("name")) for node in _SCHEMA_ROOT.findall(".//MetadataClasses/Class"))


PLATFORM_EDT_MCORE_CLASSES = tuple(str(node.get("name")) for node in _SCHEMA_ROOT.findall(".//McoreClasses/Class"))
