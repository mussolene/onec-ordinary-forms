from __future__ import annotations

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
    assert sum(len(descriptor.platform_properties) for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()) == 416
    assert sum(len(descriptor.platform_events) for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()) == 79


def test_single_xsd_appinfo_contains_platform_palette() -> None:
    root = ET.parse(XSD).getroot()
    controls = {control.get("name", ""): control for control in root.findall(".//PlatformPalette/Control")}

    assert controls["Button"].get("platformName") == "Кнопка"
    assert {event.get("platformName") for event in controls["Button"].findall("./Events/Event")} == {"Нажатие"}
    assert "ПриИзменении" in {
        event.get("platformName") for event in controls["InputField"].findall("./Events/Event")
    }
    assert "ПриВыводеСтроки" in {
        event.get("platformName") for event in controls["Table"].findall("./Events/Event")
    }


def test_single_xsd_contains_platform_vocabulary() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(XSD).getroot()

    def enum_values(type_name: str) -> set[str]:
        restriction = root.find(f"xs:simpleType[@name='{type_name}']/xs:restriction", ns)
        assert restriction is not None
        return {element.get("value", "") for element in restriction.findall("xs:enumeration", ns)}

    assert enum_values("ControlElementNameType") == set(PLATFORM_PALETTE)
    platform_properties = set()
    platform_events = set()
    platform_types = set()
    for control in root.findall(".//PlatformPalette/Control"):
        if control.get("insertable") == "false":
            continue
        for prop in control.findall("./Properties/Property"):
            platform_properties.add(prop.get("platformName", ""))
            platform_types.add(prop.get("platformType", ""))
        for event in control.findall("./Events/Event"):
            platform_events.add(event.get("platformName", ""))
    assert len(platform_properties) == 197
    assert len(platform_events) == 40
    assert "Цвет" in " ; ".join(platform_types)
    assert "Шрифт" in " ; ".join(platform_types)


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
    assert "КнопкаВыбора" not in child_names("InputFieldType")
    assert "ПриВыводеСтроки" not in child_names("InputFieldType")
    assert "ПриВыводеСтроки" in {
        event.get("platformName", "") for event in root.findall(".//PlatformPalette/Control[@name='Table']/Events/Event")
    }


def test_public_xsd_control_elements_use_english_vocabulary() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(XSD).getroot()
    public_element_names = {
        element.get("name", "")
        for element in root.findall(".//xs:complexType/xs:complexContent/xs:extension/xs:choice/xs:element", ns)
    }

    assert public_element_names
    assert not any(any("А" <= char <= "я" or char == "Ё" or char == "ё" for char in name) for name in public_element_names)
