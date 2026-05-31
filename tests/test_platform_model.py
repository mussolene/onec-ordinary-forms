from pathlib import Path
import xml.etree.ElementTree as ET

from onec_ordinary_forms.platform_model import (
    PLATFORM_EDT_MCORE_CLASSES,
    PLATFORM_EDT_METADATA_CLASSES,
    PLATFORM_METADATA_OBJECT_KINDS,
    PLATFORM_RUNTIME_CALL_EDGES,
    PLATFORM_RUNTIME_EDGES,
    PLATFORM_RUNTIME_NODES,
    PLATFORM_SCHEMA_RESOURCES,
    PLATFORM_SERIALIZERS,
    PLATFORM_TYPE_DOMAIN_CODES,
    PLATFORM_TYPE_TREE_KINDS,
    runtime_call_edges_by_source,
    runtime_edges_from,
)


ROOT = Path(__file__).resolve().parents[1]
CONFIGURATION_XSD = ROOT / "src" / "onec_ordinary_forms" / "schemas" / "PlatformConfigStructure.xsd"
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


def test_configuration_schema_records_platform_schema_source_without_vendored_corpus() -> None:
    root = ET.parse(CONFIGURATION_XSD).getroot()
    resources = root.findall(".//PlatformSchemaResources/Resource")
    marker = root.find(".//PlatformSchemaResources")

    assert resources == []
    assert marker is not None
    assert marker.get("storage") == "external-reference"


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


def test_platform_model_contains_ordinary_form_runtime_graph() -> None:
    nodes = {node.name: node for node in PLATFORM_RUNTIME_NODES}
    edges = {(edge.source, edge.target, edge.kind) for edge in PLATFORM_RUNTIME_EDGES}

    assert nodes["FormDesDoc"].layer == "designer"
    assert nodes["FormDocument"].layer == "runtime"
    assert nodes["ControlSite"].layer == "bridge"
    assert nodes["wbase::BaseWindow"].role == "window-layout-paint-redraw-api"
    assert ("FormDesDoc", "FormDesView", "document-view") in edges
    assert ("FormDocument", "FormDocumentView", "document-view") in edges
    assert ("FormDesView", "wbase::Window", "window-implementation") in edges
    assert ("FormDocumentView", "wbase::Window", "window-implementation") in edges
    assert ("IFormDocumentViewSite", "ControlSite", "control-site-bridge") in edges
    assert runtime_edges_from("FormDocument", kind="document-view")[0].target == "FormDocumentView"


def test_platform_runtime_graph_keeps_persistence_and_render_edges_separate() -> None:
    call_edges = {(edge.source, edge.target, edge.role) for edge in PLATFORM_RUNTIME_CALL_EDGES}

    assert (
        "persistence-write",
        "core::ListOutStream::ListOutStream",
        "platform-list-stream-writer",
    ) in call_edges
    assert (
        "ordinary-control-format",
        "wbase::cf_form_controls_info8",
        "control-info-format",
    ) in call_edges
    assert (
        "render-invalidation",
        "wbase::BaseWindow::V8RedrawWindow",
        "redraw",
    ) in call_edges
    assert runtime_call_edges_by_source("paint-dispatch")[0].target == "wbase::Window::onPaint_handler"


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
