from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from pathlib import Path

from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS, PLATFORM_PALETTE


ROOT = Path(__file__).resolve().parents[1]
ORDINARY_FORM_XSD = ROOT / "src" / "onec_ordinary_forms" / "schemas" / "OrdinaryForm.xsd"


def test_xsd_control_group_matches_platform_palette() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    choice = root.find("xs:group[@name='ControlElementGroup']/xs:choice", ns)
    assert choice is not None

    xsd_controls = {element.get("name", "") for element in choice.findall("xs:element", ns)}

    assert xsd_controls == set(PLATFORM_PALETTE)
    assert "Pages" not in xsd_controls


def test_descriptors_have_platform_palette_members() -> None:
    descriptor_tags = {descriptor.xml_tag for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()}

    assert descriptor_tags == set(PLATFORM_PALETTE)
    assert sum(len(descriptor.platform_properties) for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()) == 417
    assert sum(len(descriptor.platform_events) for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()) == 79


def test_elements_xsd_appinfo_contains_platform_palette() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    controls = {control.get("name", ""): control for control in root.findall(".//PlatformPalette/Control")}

    assert controls["Button"].get("platformName") == "Кнопка"
    assert {event.get("platformName") for event in controls["Button"].findall("./Events/Event")} == {"Нажатие"}
    assert "ПриИзменении" in {
        event.get("platformName") for event in controls["InputField"].findall("./Events/Event")
    }
    assert "ПриВыводеСтроки" in {
        event.get("platformName") for event in controls["Table"].findall("./Events/Event")
    }


def test_elements_xsd_contains_platform_vocabulary() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(ORDINARY_FORM_XSD).getroot()

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
    assert len(platform_properties) == 200
    assert len(platform_events) == 40
    assert "Цвет" in " ; ".join(platform_types)
    assert "Шрифт" in " ; ".join(platform_types)


def test_control_types_are_type_specific() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(ORDINARY_FORM_XSD).getroot()

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


def test_core_control_types_cover_platform_property_surface() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(ORDINARY_FORM_XSD).getroot()

    def child_names(type_name: str) -> set[str]:
        choice = root.find(
            f"xs:complexType[@name='{type_name}']/xs:complexContent/xs:extension/xs:choice",
            ns,
        )
        assert choice is not None
        return {element.get("name", "") for element in choice.findall("xs:element", ns)}

    expected = {
        "PanelType": {
            "AutoTraversalOrder",
            "TraversalOrder",
            "Picture",
            "PanelPicturePosition",
            "PictureSize",
            "DistributeByPages",
            "ScrollablePagesMode",
            "CurrentPage",
            "TransparentBackColor",
            "Border",
        },
        "InputFieldType": {
            "AutoSelectIncomplete",
            "AutoMarkIncomplete",
            "WordWrap",
            "TypeChoice",
            "IncompleteChoice",
            "HighlightNegative",
            "ChoiceListHeight",
            "ChoiceListWidth",
            "Mask",
            "Format",
            "TypeRestriction",
            "ChoiceList",
            "FieldTextColor",
            "FieldBackColor",
            "ButtonTextColor",
            "ButtonBackColor",
        },
        "CommandBarType": {
            "Auxiliary",
            "ButtonAlignment",
            "Buttons",
            "Orientation",
            "TransparentBackColor",
            "Border",
            "ButtonTextColor",
            "ButtonBackColor",
        },
        "TableType": {
            "AutoInsertNewRow",
            "Output",
            "SelectedRows",
            "CurrentColumn",
            "Header",
            "Footer",
            "HorizontalLines",
            "VerticalLines",
            "HorizontalScrollBar",
            "VerticalScrollBar",
            "InputRowsMode",
            "SelectionMode",
            "AllowChangeRows",
            "AllowChangeColumnOrder",
            "AllowColumnSetup",
            "LeftFixedColumns",
            "RightFixedColumns",
            "AlternatingRowBackColor",
            "AlternateRowColors",
        },
    }

    for type_name, names in expected.items():
        assert names <= child_names(type_name)


def test_public_xsd_control_elements_use_english_vocabulary() -> None:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    public_element_names = {
        element.get("name", "")
        for element in root.findall(".//xs:complexType/xs:complexContent/xs:extension/xs:choice/xs:element", ns)
    }

    assert public_element_names
    assert not any(any("А" <= char <= "я" or char == "Ё" or char == "ё" for char in name) for name in public_element_names)


def test_public_xsd_has_no_raw_or_extension_pockets() -> None:
    forbidden = {
        "ObjectModel",
        "ListStream",
        "BracketStream",
        "FormBin",
        "LogicalStream",
        "RawBracket",
        "PlatformRecords",
        "Extensions",
        "ExtensionProperty",
        "SerializationProfile",
        "RootRecord",
        "TopLevel",
        "dimensionProfile",
    }
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    names = {node.get("name", "") for node in root.iter() if node.get("name")}

    assert forbidden.isdisjoint(names)

    assert not any(name.startswith("FormSerializationProfile") for name in names)
    assert not any(re.fullmatch(r"slot\d+", name or "") for name in names)


def test_position_schema_does_not_expose_constant_unit_marker() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    position_type = root.find("xs:complexType[@name='PositionType']", ns)
    assert position_type is not None

    attrs = {node.get("name", "") for node in position_type.findall("xs:attribute", ns)}

    assert "unit" not in attrs


def test_position_schema_does_not_expose_dimension_segments_marker() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    position_type = root.find("xs:complexType[@name='PositionType']", ns)
    assert position_type is not None

    attrs = {node.get("name", "") for node in position_type.findall("xs:attribute", ns)}

    assert "dimensionSegments" not in attrs


def test_position_schema_does_not_expose_secondary_dimension_marker() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    position_type = root.find("xs:complexType[@name='PositionType']", ns)
    assert position_type is not None

    attrs = {node.get("name", "") for node in position_type.findall("xs:attribute", ns)}

    assert "secondaryDimensionMarker" not in attrs


def test_position_schema_does_not_expose_layout_tail_markers() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    position_type = root.find("xs:complexType[@name='PositionType']", ns)
    assert position_type is not None

    attrs = {node.get("name", "") for node in position_type.findall("xs:attribute", ns)}

    assert "layoutTail" not in attrs
    assert "layoutPreTail" not in attrs
    assert "primaryDimensionMarker" not in attrs
    assert "layoutMode" not in attrs
    assert "layoutGroup" not in attrs
    assert "layoutOrder" not in attrs
    assert "layoutNextOrder" not in attrs
    assert "layoutFlag1" not in attrs
    assert "layoutFlag2" not in attrs
    assert position_type.find("xs:sequence/xs:element[@name='LayoutFlow']", ns) is not None


def test_panel_layout_schema_does_not_expose_stream_prefixes() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root_layout = root.find("xs:complexType[@name='RootPanelLayoutType']", ns)
    panel_group = root.find("xs:complexType[@name='PanelLayoutDependencyGroupType']", ns)
    plain_group = root.find("xs:complexType[@name='PanelDependencyGroupType']", ns)
    assert root_layout is not None
    assert panel_group is not None
    assert plain_group is not None

    root_attrs = {node.get("name", "") for node in root_layout.findall("xs:attribute", ns)}
    panel_group_attrs = {node.get("name", "") for node in panel_group.findall("xs:attribute", ns)}
    plain_group_attrs = {node.get("name", "") for node in plain_group.findall("xs:attribute", ns)}

    assert "dependencyTail" not in root_attrs
    assert "pageLayoutHeader" not in root_attrs
    assert "postLayoutTailBeforeColor" not in root_attrs
    assert "postLayoutTailAfterColor" not in root_attrs
    assert "prefix" not in panel_group_attrs
    assert "header" not in panel_group_attrs
    assert "prefix" not in plain_group_attrs
    assert "header" not in plain_group_attrs


def test_form_schema_has_root_panel_layout_not_serialization_profile() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    sequence = root.find("xs:complexType[@name='FormType']/xs:sequence", ns)
    assert sequence is not None
    public_elements = {node.get("name") for node in sequence.findall("xs:element", ns)}

    assert "RootPanelLayout" in public_elements
    assert "SerializationProfile" not in public_elements
    assert root.find("xs:complexType[@name='RootPanelLayoutType']", ns) is not None
    assert root.find("xs:complexType[@name='FormSerializationProfileType']", ns) is None
    assert root.find("xs:complexType[@name='FormTopLevelProfileType']", ns) is None
    assert root.find("xs:complexType[@name='FormRootRecordProfileType']", ns) is None


def test_command_bar_schema_has_typed_command_source_not_profile_pocket() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    choice = root.find(
        "xs:complexType[@name='CommandBarType']/xs:complexContent/xs:extension/xs:choice",
        ns,
    )
    assert choice is not None
    public_elements = {node.get("name") for node in choice.findall("xs:element", ns)}

    assert "CommandSource" in public_elements
    assert "SerializationProfile" not in public_elements
    assert root.find("xs:complexType[@name='CommandBarSerializationProfileType']", ns) is None
    assert root.find("xs:complexType[@name='CommandBarActionGraphType']", ns) is None


def test_picture_decoration_schema_has_typed_picture_style_not_profile_pocket() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    choice = root.find(
        "xs:complexType[@name='PictureDecorationType']/xs:complexContent/xs:extension/xs:choice",
        ns,
    )
    assert choice is not None
    public_elements = {node.get("name") for node in choice.findall("xs:element", ns)}

    assert "PictureStyle" in public_elements
    assert "SerializationProfile" not in public_elements
    assert root.find("xs:complexType[@name='PictureDecorationSerializationProfileType']", ns) is None
    assert root.find("xs:complexType[@name='PictureDecorationStyleProfileType']", ns) is None


def test_panel_schema_has_typed_panel_layout_not_profile_pocket() -> None:
    root = ET.parse(ORDINARY_FORM_XSD).getroot()
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    choice = root.find(
        "xs:complexType[@name='PanelType']/xs:complexContent/xs:extension/xs:choice",
        ns,
    )
    assert choice is not None
    public_elements = {node.get("name") for node in choice.findall("xs:element", ns)}

    assert "PanelLayout" in public_elements
    assert "SerializationProfile" not in public_elements
    assert root.find("xs:complexType[@name='PanelSerializationProfileType']", ns) is None
    assert root.find("xs:complexType[@name='PanelLayoutType']", ns) is not None
    assert root.find("xs:complexType[@name='PanelLayoutDependencyGroupType']", ns) is not None
