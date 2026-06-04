#!/usr/bin/env python3
"""Audit ordinary-form public schema coverage against native platform descriptors."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS  # noqa: E402
from onec_ordinary_forms.ordinary_platform_mappings import (  # noqa: E402
    platform_event_xml_name,
    platform_property_xml_name,
)


XS = {"xs": "http://www.w3.org/2001/XMLSchema"}
XML_TO_STREAM_CONTROL_TYPE = {
    "ActiveXControl": "HTML",
    "CalendarField": "Calendar",
    "ChoiceField": "TextBox",
    "GeographicalSchemaField": "GeographicalMap",
    "GraphicalSchemaField": "Flowchart",
    "HTMLDocumentField": "HTML",
    "InputField": "TextBox",
    "LabelDecoration": "Label",
    "ListBox": "TextBox",
    "PictureDecoration": "Image",
    "PivotChart": "Chart",
    "Splitter": "Separator",
    "SpreadsheetDocumentField": "Spreadsheet",
    "Table": "TableBox",
    "TextDocumentField": "TextDocument",
}
DEFAULT_NATIVE_BIN = ROOT / "sidecars/onec-form-native/build/oof-native"


def xsd_root(path: Path) -> ET.Element:
    return ET.parse(path).getroot()


def xsd_controls(root: ET.Element) -> set[str]:
    choice = root.find("xs:group[@name='ControlElementGroup']/xs:choice", XS)
    if choice is None:
        raise ValueError("ControlElementGroup not found")
    return {element.get("name", "") for element in choice.findall("xs:element", XS) if element.get("name")}


def xsd_control_properties(root: ET.Element, control: str) -> set[str]:
    choice = root.find(
        f"xs:complexType[@name='{control}Type']/xs:complexContent/xs:extension/xs:choice",
        XS,
    )
    if choice is None:
        return set()
    return {element.get("name", "") for element in choice.findall("xs:element", XS) if element.get("name")}


def xsd_control_events(root: ET.Element, control: str) -> set[str]:
    restriction = root.find(
        f"xs:simpleType[@name='{control}EventNameType']/xs:restriction",
        XS,
    )
    if restriction is None:
        return set()
    return {item.get("value", "") for item in restriction.findall("xs:enumeration", XS) if item.get("value")}


def palette_controls(root: ET.Element, *, include_nested: bool = False) -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for control in root.findall(".//PlatformPalette/Control"):
        name = control.get("name")
        if not name:
            continue
        if not include_nested and control.get("insertable") == "false":
            continue
        result[name] = {
            "platformName": control.get("platformName", ""),
            "platformProperties": [
                prop.get("platformName", "")
                for prop in control.findall("./Properties/Property")
                if prop.get("platformName")
            ],
            "platformEvents": [
                event.get("platformName", "")
                for event in control.findall("./Events/Event")
                if event.get("platformName")
            ],
        }
    return result


def native_json(command: str, native_bin: Path) -> dict[str, object]:
    completed = subprocess.run(
        [str(native_bin), command],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"native command failed: {command}: {completed.stderr or completed.stdout}")
    return json.loads(completed.stdout)


def audit(xsd_path: Path, native_bin: Path = DEFAULT_NATIVE_BIN) -> dict[str, object]:
    root = xsd_root(xsd_path)
    xsd_control_set = xsd_controls(root)
    palette = palette_controls(root)
    native_schema = native_json("platform-object-schema", native_bin)
    native_registry = native_json("platform-property-registry", native_bin)
    native_schema_controls = {
        str(schema.get("typeName", ""))
        for schema in native_schema.get("schemas", [])
        if isinstance(schema, dict)
    }
    native_registry_properties = {
        str(descriptor.get("name", ""))
        for descriptor in native_registry.get("descriptors", [])
        if isinstance(descriptor, dict)
    }
    public_descriptors = {descriptor.xml_tag: descriptor for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()}

    controls: list[dict[str, object]] = []
    property_matrix: list[dict[str, object]] = []
    event_matrix: list[dict[str, object]] = []
    for control in sorted(xsd_control_set | set(palette) | set(public_descriptors)):
        stream_control = XML_TO_STREAM_CONTROL_TYPE.get(control, control)
        xsd_props = xsd_control_properties(root, control)
        xsd_events = xsd_control_events(root, control)
        palette_item = palette.get(control, {})
        public_descriptor = public_descriptors.get(control)
        descriptor_props = set(public_descriptor.properties) if public_descriptor else set()
        platform_properties = list(palette_item.get("platformProperties", []))
        platform_events = list(palette_item.get("platformEvents", []))
        for platform_property in platform_properties:
            xml_name = platform_property_xml_name(control, platform_property)
            in_xsd = xml_name in xsd_props
            in_descriptor = xml_name in descriptor_props
            property_matrix.append(
                {
                    "control": control,
                    "streamControl": stream_control,
                    "platformName": platform_property,
                    "xmlName": xml_name,
                    "inXsd": in_xsd,
                    "inPublicDescriptor": in_descriptor,
                    "inNativeRegistry": xml_name in native_registry_properties,
                    "status": (
                        "mapped-descriptor"
                        if xml_name and in_xsd and in_descriptor
                        else "mapped-xsd-only"
                        if xml_name and in_xsd
                        else "mapped-no-public-xml"
                        if xml_name
                        else "unmapped"
                    ),
                }
            )
        for platform_event in platform_events:
            xml_name = platform_event_xml_name(control, platform_event)
            event_matrix.append(
                {
                    "control": control,
                    "streamControl": stream_control,
                    "platformName": platform_event,
                    "xmlName": xml_name,
                    "inXsd": xml_name in xsd_events,
                    "status": "mapped-xsd" if xml_name in xsd_events else "mapped-no-public-xml",
                }
            )
        controls.append(
            {
                "control": control,
                "streamControl": stream_control,
                "inXsd": control in xsd_control_set,
                "inPalette": control in palette,
                "nativeSchema": stream_control in native_schema_controls,
                "xsdPropertyCount": len(xsd_props),
                "publicDescriptorPropertyCount": len(descriptor_props),
                "nativeRegistryPropertyCount": len(descriptor_props & native_registry_properties),
                "platformPropertyCount": len(platform_properties),
                "platformEventCount": len(platform_events),
                "xsdOnlyProperties": sorted(xsd_props - descriptor_props),
                "descriptorOnlyProperties": sorted(descriptor_props - xsd_props),
                "unmappedPlatformProperties": sorted(
                    item["platformName"]
                    for item in property_matrix
                    if item["control"] == control and item["status"] == "unmapped"
                ),
                "mappedPlatformPropertiesWithoutPublicXml": sorted(
                    item["platformName"]
                    for item in property_matrix
                    if item["control"] == control and item["status"] == "mapped-no-public-xml"
                ),
            }
        )
    unmapped_properties = [
        f"{item['control']}:{item['platformName']}"
        for item in property_matrix
        if item["status"] == "unmapped"
    ]
    mapped_without_public_xml = [
        f"{item['control']}:{item['platformName']}->{item['xmlName']}"
        for item in property_matrix
        if item["status"] == "mapped-no-public-xml"
    ]
    events_without_public_xml = [
        f"{item['control']}:{item['platformName']}"
        for item in event_matrix
        if item["status"] == "mapped-no-public-xml"
    ]
    xsd_only_public_properties = [
        f"{item['control']}:{property_name}"
        for item in controls
        for property_name in item["xsdOnlyProperties"]
    ]

    return {
        "summary": {
            "xsdControls": len(xsd_control_set),
            "paletteControls": len(palette),
            "nativeSchemaControls": len(native_schema_controls),
            "nativeRegistryProperties": len(native_registry_properties),
            "controlsWithoutNativeSchema": sorted(
                control for control in xsd_control_set if XML_TO_STREAM_CONTROL_TYPE.get(control, control) not in native_schema_controls
            ),
            "controlsWithoutPublicDescriptor": sorted(control for control in xsd_control_set if control not in public_descriptors),
            "platformPropertyRows": len(property_matrix),
            "mappedPlatformPropertyRows": len(property_matrix) - len(unmapped_properties),
            "unmappedPlatformProperties": sorted(unmapped_properties),
            "mappedPlatformPropertiesWithoutPublicXml": sorted(mapped_without_public_xml),
            "xsdOnlyPublicProperties": sorted(xsd_only_public_properties),
            "platformEventRows": len(event_matrix),
            "eventsWithoutPublicXml": sorted(events_without_public_xml),
        },
        "controls": controls,
        "propertyMatrix": property_matrix,
        "eventMatrix": event_matrix,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xsd", default=str(ROOT / "src/onec_ordinary_forms/schemas/OrdinaryFormPalette.xsd"))
    parser.add_argument("--native-bin", default=str(DEFAULT_NATIVE_BIN))
    parser.add_argument("--out", help="Write JSON report")
    args = parser.parse_args()
    report = audit(Path(args.xsd), Path(args.native_bin))
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
