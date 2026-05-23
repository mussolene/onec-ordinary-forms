from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path

from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS, PLATFORM_PALETTE


ROOT = Path(__file__).resolve().parents[1]
XSD = ROOT / "src" / "onec_ordinary_forms" / "schemas" / "ordinary-form.xsd"


def test_xsd_control_group_matches_platform_palette() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(XSD).getroot()
    choice = root.find("xs:group[@name='ControlElementGroup']/xs:choice", ns)
    assert choice is not None

    xsd_controls = {element.get("name", "") for element in choice.findall("xs:element", ns)}

    assert xsd_controls == set(PLATFORM_PALETTE)
    assert "Pages" not in xsd_controls


def test_descriptors_have_platform_palette_members() -> None:
    descriptor_tags = {descriptor.xml_tag for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()}

    assert descriptor_tags == set(PLATFORM_PALETTE)
    assert sum(len(descriptor.platform_properties) for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()) == 390
    assert sum(len(descriptor.platform_events) for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()) == 71


def test_palette_json_contains_required_controls() -> None:
    palette_path = ROOT / "src" / "onec_ordinary_forms" / "data" / "ordinary-form-palette-8.2.19.json"
    palette = json.loads(palette_path.read_text(encoding="utf-8"))

    assert palette["Button"]["platformName"] == "Кнопка"
    assert {event["name"] for event in palette["Button"]["events"]} == {"Нажатие"}
    assert "ПриИзменении" in {event["name"] for event in palette["InputField"]["events"]}
    assert "ПриВыводеСтроки" in {event["name"] for event in palette["Table"]["events"]}


def test_single_xsd_contains_platform_vocabulary() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(XSD).getroot()

    def enum_values(type_name: str) -> set[str]:
        restriction = root.find(f"xs:simpleType[@name='{type_name}']/xs:restriction", ns)
        assert restriction is not None
        return {element.get("value", "") for element in restriction.findall("xs:enumeration", ns)}

    assert enum_values("ControlElementNameType") == set(PLATFORM_PALETTE)
    assert len(enum_values("PlatformPropertyNameType")) == 190
    assert len(enum_values("PlatformEventNameType")) == 38
    assert "ОбъектМетаданныхКонфигурация" in enum_values("MetadataObjectNameType")
    assert "Документы" in enum_values("ConfigurationMetadataPropertyNameType")


def test_control_types_are_type_specific() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(XSD).getroot()

    def child_names(type_name: str) -> set[str]:
        choice = root.find(
            f"xs:complexType[@name='{type_name}']/xs:complexContent/xs:extension/xs:choice",
            ns,
        )
        assert choice is not None
        return {element.get("name", "") for element in choice.findall("xs:element", ns)}

    assert "Нажатие" not in child_names("ButtonType")
    assert "Events" in child_names("ButtonType")
    assert "КнопкаВыбора" in child_names("InputFieldType")
    assert "ПриВыводеСтроки" not in child_names("InputFieldType")
    assert "ПриВыводеСтроки" in {
        element.get("value", "")
        for element in root.findall(
            "xs:simpleType[@name='TableEventNameType']/xs:restriction/xs:enumeration",
            ns,
        )
    }
