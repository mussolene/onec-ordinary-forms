#!/usr/bin/env python3
"""Audit ordinary-form codec coverage against palette/XSD/writer descriptors."""

from __future__ import annotations

import ast
import argparse
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS  # noqa: E402
from onec_ordinary_forms.ordinary_stream import CONTROL_INFO_SLOT_DESCRIPTORS, CONTROL_INFO_WRITER_DESCRIPTORS  # noqa: E402


XS = {"xs": "http://www.w3.org/2001/XMLSchema"}
XML_TO_STREAM_CONTROL_TYPE = {
    "LabelDecoration": "Label",
    "PictureDecoration": "Image",
}
FORBIDDEN_WRITER_FALLBACK_TOKENS = (
    "control_templates",
    "control_template",
    "template_info",
    "template_metadata",
    "template_geometry",
    "geometry_stream_from_template",
)


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


class ControlInfoBranchVisitor(ast.NodeVisitor):
    def __init__(self) -> None:
        self.branches: dict[str, str] = {}
        self._in_target = False

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        old = self._in_target
        self._in_target = node.name == "control_info_from_xml"
        self.generic_visit(node)
        self._in_target = old

    def visit_If(self, node: ast.If) -> None:
        if self._in_target:
            control = comparison_control_type(node.test)
            if control:
                self.branches.setdefault(control, return_function_name(node.body))
        self.generic_visit(node)


def comparison_control_type(node: ast.AST) -> str:
    if not isinstance(node, ast.Compare) or len(node.ops) != 1 or not isinstance(node.ops[0], ast.Eq):
        return ""
    left_is_control_type = isinstance(node.left, ast.Name) and node.left.id == "control_type"
    if not left_is_control_type or len(node.comparators) != 1:
        return ""
    comparator = node.comparators[0]
    if isinstance(comparator, ast.Constant) and isinstance(comparator.value, str):
        return comparator.value
    return ""


def return_function_name(body: list[ast.stmt]) -> str:
    for statement in body:
        if not isinstance(statement, ast.Return):
            continue
        value = statement.value
        if isinstance(value, ast.Call):
            if isinstance(value.func, ast.Name):
                return value.func.id
            if isinstance(value.func, ast.Attribute):
                return value.func.attr
    return ""


def writer_branches(path: Path) -> dict[str, str]:
    tree = ast.parse(path.read_text(encoding="utf-8"))
    visitor = ControlInfoBranchVisitor()
    visitor.visit(tree)
    return visitor.branches


def writer_fallback_tokens(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    return sorted(token for token in FORBIDDEN_WRITER_FALLBACK_TOKENS if token in text)


def audit(xsd_path: Path, stream_path: Path) -> dict[str, object]:
    root = xsd_root(xsd_path)
    xsd_control_set = xsd_controls(root)
    palette = palette_controls(root)
    branches = writer_branches(stream_path)
    fallback_tokens = writer_fallback_tokens(stream_path)
    writer_descriptor_controls = set(CONTROL_INFO_WRITER_DESCRIPTORS)
    shared_info_descriptor_controls = {
        descriptor.control_type
        for descriptor in CONTROL_INFO_SLOT_DESCRIPTORS.values()
        if descriptor.control_type != "FormRootPanel"
    }
    public_descriptors = {descriptor.xml_tag: descriptor for descriptor in ORDINARY_CONTROL_DESCRIPTORS.values()}

    controls: list[dict[str, object]] = []
    for control in sorted(xsd_control_set | set(palette) | set(branches) | set(public_descriptors)):
        stream_control = XML_TO_STREAM_CONTROL_TYPE.get(control, control)
        xsd_props = xsd_control_properties(root, control)
        palette_item = palette.get(control, {})
        public_descriptor = public_descriptors.get(control)
        descriptor_props = set(public_descriptor.properties) if public_descriptor else set()
        controls.append(
            {
                "control": control,
                "streamControl": stream_control,
                "inXsd": control in xsd_control_set,
                "inPalette": control in palette,
                "writerBranch": branches.get(stream_control, ""),
                "writerDescriptor": stream_control in writer_descriptor_controls,
                "sharedInfoDescriptor": stream_control in shared_info_descriptor_controls,
                "xsdPropertyCount": len(xsd_props),
                "publicDescriptorPropertyCount": len(descriptor_props),
                "platformPropertyCount": len(palette_item.get("platformProperties", [])),
                "platformEventCount": len(palette_item.get("platformEvents", [])),
                "xsdOnlyProperties": sorted(xsd_props - descriptor_props),
                "descriptorOnlyProperties": sorted(descriptor_props - xsd_props),
            }
        )

    return {
        "summary": {
            "xsdControls": len(xsd_control_set),
            "paletteControls": len(palette),
            "legacyWriterBranches": len(branches),
            "writerDescriptorControls": len(writer_descriptor_controls),
            "sharedInfoDescriptorControls": len(shared_info_descriptor_controls),
            "controlsWithoutWriterDescriptor": sorted(
                control for control in xsd_control_set if XML_TO_STREAM_CONTROL_TYPE.get(control, control) not in writer_descriptor_controls
            ),
            "writerBranchesWithoutXsdControl": sorted(
                control for control in set(branches) - {XML_TO_STREAM_CONTROL_TYPE.get(item, item) for item in xsd_control_set}
            ),
            "controlsWithoutSharedInfoDescriptor": sorted(
                control for control in xsd_control_set if XML_TO_STREAM_CONTROL_TYPE.get(control, control) not in shared_info_descriptor_controls
            ),
            "writerFallbackTokens": fallback_tokens,
        },
        "controls": controls,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xsd", default=str(ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd"))
    parser.add_argument("--stream", default=str(ROOT / "src/onec_ordinary_forms/ordinary_stream.py"))
    parser.add_argument("--out", help="Write JSON report")
    args = parser.parse_args()
    report = audit(Path(args.xsd), Path(args.stream))
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
