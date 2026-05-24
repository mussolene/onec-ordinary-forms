from pathlib import Path
import xml.etree.ElementTree as ET

from onec_ordinary_forms.platform_model import (
    PLATFORM_EDT_MCORE_CLASSES,
    PLATFORM_EDT_METADATA_CLASSES,
    PLATFORM_METADATA_OBJECT_KINDS,
    PLATFORM_SCHEMA_RESOURCES,
    PLATFORM_SERIALIZERS,
    PLATFORM_TYPE_DOMAIN_CODES,
    PLATFORM_TYPE_TREE_KINDS,
)


ROOT = Path(__file__).resolve().parents[1]
CONFIGURATION_XSD = ROOT / "src" / "onec_ordinary_forms" / "schemas" / "Configuration.xsd"
SCHEMAS_ROOT = ROOT / "src" / "onec_ordinary_forms" / "schemas"


def test_platform_model_matches_configuration_schema_appinfo() -> None:
    root = ET.parse(CONFIGURATION_XSD).getroot()
    resources = {
        (node.get("source"), node.get("schema"), node.get("namespace"), node.get("path"), node.get("sha256"))
        for node in root.findall(".//PlatformSchemaResources/Resource")
    }
    serializers = {
        (node.get("name"), node.get("direction"), node.get("role"))
        for node in root.findall(".//PlatformSerializers/Serializer")
    }

    assert resources == {(item.source, item.schema, item.namespace, item.path, item.sha256) for item in PLATFORM_SCHEMA_RESOURCES}
    assert serializers == {(item.name, item.direction, item.role) for item in PLATFORM_SERIALIZERS}


def test_configuration_schema_is_valid_xsd() -> None:
    from lxml import etree

    etree.XMLSchema(etree.parse(str(CONFIGURATION_XSD)))


def test_configuration_schema_vendors_full_platform_xsd_set() -> None:
    root = ET.parse(CONFIGURATION_XSD).getroot()
    resources = root.findall(".//PlatformSchemaResources/Resource")
    assert len(resources) == 81

    namespaces = {node.get("namespace") for node in resources}
    assert "http://v8.1c.ru/8.2/managed-application/logform" in namespaces
    assert "http://v8.1c.ru/8.1/data/core" in namespaces
    assert "http://v8.1c.ru/8.1/data/ui" in namespaces
    assert "http://v8.1c.ru/8.3/mobile-application/form" in namespaces

    for node in resources:
        path = SCHEMAS_ROOT / str(node.get("path"))
        assert path.is_file(), path
        assert ET.parse(path).getroot().tag.rsplit("}", 1)[-1] == "schema"


def test_platform_model_contains_confirmed_value_and_metadata_vocabulary() -> None:
    assert {item.code for item in PLATFORM_TYPE_DOMAIN_CODES} == {"S", "N", "B", "D", "U", "#"}
    assert "Configuration" in PLATFORM_METADATA_OBJECT_KINDS
    assert "ExternalDataProcessorObject" in PLATFORM_METADATA_OBJECT_KINDS
    assert "Document" in PLATFORM_METADATA_OBJECT_KINDS
    assert "ExternalDataSource" in PLATFORM_METADATA_OBJECT_KINDS
    assert "CatalogRef" in PLATFORM_TYPE_TREE_KINDS
    assert "StandardPeriod" in PLATFORM_TYPE_TREE_KINDS
    assert "Configuration" in PLATFORM_EDT_METADATA_CLASSES
    assert "DataProcessorForm" in PLATFORM_EDT_METADATA_CLASSES
    assert "TypeDescription" in PLATFORM_EDT_MCORE_CLASSES
    assert "ColorValue" in PLATFORM_EDT_MCORE_CLASSES
    assert len(PLATFORM_EDT_METADATA_CLASSES) > 100


def test_configuration_schema_contains_configuration_tree() -> None:
    root = ET.parse(CONFIGURATION_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}

    object_kind = root.find(".//xs:simpleType[@name='MetadataObjectKind']/xs:restriction", ns)
    assert object_kind is not None
    assert {item.get("value") for item in object_kind.findall("xs:enumeration", ns)} == set(PLATFORM_METADATA_OBJECT_KINDS)

    config = root.find(".//xs:complexType[@name='MetadataConfiguration']", ns)
    assert config is not None
    assert config.find(".//xs:element[@name='TypeDomain']", ns) is not None
    assert config.find(".//xs:element[@name='TypeTree']", ns) is not None
    assert config.find(".//xs:element[@name='Object']", ns) is not None

    metadata_object = root.find(".//xs:complexType[@name='MetadataObject']", ns)
    assert metadata_object is not None
    assert metadata_object.find(".//xs:element[@name='Attribute']", ns) is not None
    assert metadata_object.find(".//xs:element[@name='Form']", ns) is not None
    assert metadata_object.find(".//xs:element[@name='TablePart']", ns) is not None

    type_tree = root.find(".//xs:simpleType[@name='TypeTreeKind']/xs:restriction", ns)
    assert type_tree is not None
    assert {item.get("value") for item in type_tree.findall("xs:enumeration", ns)} == set(PLATFORM_TYPE_TREE_KINDS)
