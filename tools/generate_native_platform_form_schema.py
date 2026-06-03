#!/usr/bin/env python3
"""Generate native ordinary-form platform schema metadata from platform XSD.

The generated header intentionally keeps the C++ runtime self-contained while
making the source of the object model reproducible: extracted platform XSD
resources are the input, not hand-written per-property mappings.
"""

from __future__ import annotations

import argparse
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


XS = "{http://www.w3.org/2001/XMLSchema}"
LOGFORM_NAMESPACE = "http://v8.1c.ru/8.2/managed-application/logform"


@dataclass(frozen=True)
class ControlSeed:
    type_name: str
    stream_element: str
    root_complex: str
    variant_element: str
    variant_complex: str
    evidence: str


CONTROL_SEEDS: tuple[ControlSeed, ...] = (
    ControlSeed("Label", "lbl", "Decoration", "textData", "TextDecorationData", "Decoration/textData ordinary label decoration"),
    ControlSeed("TextBox", "txt", "Field", "inputData", "InputFieldData", "Field/inputData ordinary input field"),
    ControlSeed("Button", "btn", "Button", "", "", "Button complexType ordinary button"),
    ControlSeed("CheckBox", "chk", "Field", "checkData", "CheckBoxFieldData", "Field/checkData ordinary checkbox field"),
    ControlSeed("RadioButton", "rbtn", "Field", "radioData", "RadioButtonsData", "Field/radioData ordinary radio buttons field"),
    ControlSeed("CommandBar", "cmdb", "Group", "buttonsData", "ButtonsGroupData", "Group/buttonsData ordinary command bar group"),
    ControlSeed("TableBox", "tbl", "Table", "", "", "Table complexType ordinary table box"),
    ControlSeed("TableColumn", "column", "Field", "inputData", "InputFieldData", "Field/inputData table column editor shape"),
    ControlSeed("TableColumnsGroup", "group", "Group", "columnsData", "ColumnsGroupData", "Group/columnsData table column group shape"),
    ControlSeed("GroupBox", "grpb", "Group", "usualData", "UsualGroupData", "Group/usualData ordinary group box"),
    ControlSeed("Separator", "sep", "Group", "", "", "Group fallback for separator layout descriptor"),
    ControlSeed("Panel", "pnl", "Group", "pagesData", "PagesGroupData", "Group/pagesData ordinary panel pages container"),
    ControlSeed("Page", "page", "Group", "pageData", "PageGroupData", "Group/pageData ordinary page"),
    ControlSeed("Image", "img", "Decoration", "pictureData", "PictureDecorationData", "Decoration/pictureData ordinary picture decoration"),
    ControlSeed("Spreadsheet", "sprdsht", "Field", "spereadsheetData", "SpreadsheetFieldData", "Field/spereadsheetData ordinary spreadsheet field"),
    ControlSeed("TextDocument", "txtd", "Field", "textDocData", "TextDocFieldData", "Field/textDocData ordinary text document field"),
    ControlSeed("FormattedDocument", "fmtd", "Field", "formattedDocData", "FormattedDocFieldData", "Field/formattedDocData ordinary formatted document field"),
    ControlSeed("Calendar", "clndr", "Field", "calendarData", "CalendarFieldData", "Field/calendarData ordinary calendar field"),
    ControlSeed("ProgressBar", "prgb", "Field", "progressBarData", "ProgressBarFieldData", "Field/progressBarData ordinary progress bar field"),
    ControlSeed("TrackBar", "trckb", "Field", "trackBarData", "TrackBarFieldData", "Field/trackBarData ordinary track bar field"),
    ControlSeed("Chart", "chrt", "Field", "chartData", "ChartFieldData", "Field/chartData ordinary chart field"),
    ControlSeed("GanttChart", "gchrt", "Field", "ganttChartData", "GanttChartFieldData", "Field/ganttChartData ordinary gantt chart field"),
    ControlSeed("Dendrogram", "dndrgm", "Field", "dendrogramData", "DendrogramFieldData", "Field/dendrogramData ordinary dendrogram field"),
    ControlSeed("Flowchart", "flwchrt", "Field", "flowchartData", "FlowchartFieldData", "Field/flowchartData ordinary flowchart field"),
    ControlSeed("HTML", "html", "Field", "htmlData", "HTMLFieldData", "Field/htmlData ordinary HTML field"),
    ControlSeed("GeographicalMap", "gm", "Field", "geographicalMapData", "GeographicalMapFieldData", "Field/geographicalMapData ordinary geographical map field"),
    ControlSeed("EmptyElement", "empt", "Group", "", "", "Empty ordinary layout placeholder"),
)


def strip_ns(name: str | None) -> str:
    if not name:
        return ""
    if ":" in name:
        return name.split(":", 1)[1]
    return name


def q(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def member_from_comment(comment: str) -> str:
    match = re.search(r"\b(m_[A-Za-zА-Яа-я0-9_]+)\b", comment)
    return match.group(1) if match else ""


class Schema:
    def __init__(self, path: Path) -> None:
        self.path = path
        parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
        self.root = ET.parse(path, parser=parser).getroot()
        self.target_namespace = self.root.get("targetNamespace", "")
        self.types = {ct.get("name"): ct for ct in self.root.findall(f"{XS}complexType") if ct.get("name")}

    def direct_sequence_members(self, complex_type: str) -> list[dict[str, str]]:
        ct = self.types.get(complex_type)
        if ct is None:
            return []
        seq = ct.find(f"{XS}sequence")
        if seq is None:
            return []
        out: list[dict[str, str]] = []
        pending_comment = ""
        for child in list(seq):
            if child.tag is ET.Comment:
                pending_comment = child.text or ""
                continue
            if child.tag == f"{XS}element":
                name = child.get("name", "")
                out.append({
                    "name": name,
                    "type": child.get("type", ""),
                    "value_type": child.get("type", ""),
                    "min": child.get("minOccurs", "1"),
                    "max": child.get("maxOccurs", "1"),
                    "platform_member": member_from_comment(pending_comment),
                    "platform_default": platform_default_from_comment(pending_comment),
                    "kind": "element",
                })
                pending_comment = ""
                continue
            if child.tag == f"{XS}choice":
                choice_names = []
                for elem in child.findall(f"{XS}element"):
                    choice_names.append(elem.get("name", ""))
                if choice_names:
                    out.append({
                        "name": "|".join(choice_names),
                        "type": "xs:choice",
                        "value_type": "xs:choice",
                        "min": child.get("minOccurs", "1"),
                        "max": child.get("maxOccurs", "1"),
                        "platform_member": member_from_comment(pending_comment),
                        "platform_default": platform_default_from_comment(pending_comment),
                        "kind": "choice",
                    })
                pending_comment = ""
        return out

    def direct_attributes(self, complex_type: str) -> list[dict[str, str]]:
        ct = self.types.get(complex_type)
        if ct is None:
            return []
        out: list[dict[str, str]] = []
        pending_comment = ""
        for child in list(ct):
            if child.tag is ET.Comment:
                pending_comment = child.text or ""
                continue
            if child.tag == f"{XS}attribute":
                out.append({
                    "name": child.get("name", ""),
                    "type": child.get("type", ""),
                    "value_type": child.get("type", ""),
                    "min": "0",
                    "max": "1",
                    "platform_member": member_from_comment(pending_comment),
                    "platform_default": platform_default_from_comment(pending_comment),
                    "kind": "attribute",
                })
                pending_comment = ""
        return out


def platform_default_from_comment(comment: str) -> str:
    if "по умолчанию" not in comment:
        return ""
    tail = comment.split("по умолчанию", 1)[1].strip()
    tail = " ".join(tail.split())
    return tail


def csv(values: list[str]) -> str:
    return ",".join(v for v in values if v)


def typed_csv(members: list[dict[str, str]]) -> str:
    return ",".join(f"{m['name']}:{m['value_type']}" for m in members if m["name"])


def member_csv(members: list[dict[str, str]]) -> str:
    return ",".join(f"{m['name']}->{m['platform_member']}" for m in members if m["name"] and m["platform_member"])


def find_logform_schema(resource_dir: Path) -> Path:
    candidates: list[Path] = []
    for path in sorted(resource_dir.rglob("*.xsd")):
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        if root.get("targetNamespace", "") == LOGFORM_NAMESPACE:
            candidates.append(path)
    if not candidates:
        raise SystemExit(f"no XSD with targetNamespace {LOGFORM_NAMESPACE!r} under {resource_dir}")
    return candidates[-1]


def emit_header(schema: Schema) -> str:
    lines: list[str] = []
    lines += [
        "#pragma once",
        "",
        "#include <array>",
        "#include <string_view>",
        "",
        "namespace oof::platform::form_schema {",
        "",
        "struct PlatformFormSchemaControl {",
        "    std::string_view type_name;",
        "    std::string_view stream_element;",
        "    std::string_view schema_source;",
        "    std::string_view base_type;",
        "    std::string_view child_elements;",
        "    std::string_view attributes;",
        "    std::string_view value_types;",
        "    std::string_view root_complex_type;",
        "    std::string_view variant_element;",
        "    std::string_view variant_complex_type;",
        "    std::string_view root_sequence;",
        "    std::string_view variant_sequence;",
        "    std::string_view root_attributes;",
        "    std::string_view variant_attributes;",
        "    std::string_view platform_members;",
        "    std::string_view default_contract;",
        "    std::string_view evidence;",
        "};",
        "",
        "constexpr std::string_view logform_layouter_schema =",
        f"    {q('platform-resource:' + schema.path.name + ':' + schema.target_namespace)};",
        "",
        f"constexpr std::array<PlatformFormSchemaControl, {len(CONTROL_SEEDS)}> logform_layouter_controls{{{{",
    ]
    for seed in CONTROL_SEEDS:
        root_seq = schema.direct_sequence_members(seed.root_complex)
        variant_seq = schema.direct_sequence_members(seed.variant_complex)
        root_attrs = schema.direct_attributes(seed.root_complex)
        variant_attrs = schema.direct_attributes(seed.variant_complex)
        element_names = [m["name"] for m in root_seq if m["kind"] == "element"]
        element_names += [m["name"] for m in variant_seq if m["kind"] == "element"]
        attr_names = [m["name"] for m in root_attrs + variant_attrs]
        value_types = [m["value_type"] for m in root_seq + variant_seq if m["value_type"]]
        defaults = []
        for m in root_seq + variant_seq + root_attrs + variant_attrs:
            if m["platform_default"]:
                defaults.append(f"{m['name']}={m['platform_default']}")
        members = root_seq + variant_seq + root_attrs + variant_attrs
        evidence = (
            f"{seed.evidence}; generated from {schema.path.name}; "
            f"root={seed.root_complex}"
            + (f" variant={seed.variant_element}:{seed.variant_complex}" if seed.variant_complex else "")
        )
        lines += [
            "    {",
            f"        {q(seed.type_name)},",
            f"        {q(seed.stream_element)},",
            "        logform_layouter_schema,",
            f"        {q(seed.root_complex)},",
            f"        {q(csv(element_names))},",
            f"        {q(csv(attr_names))},",
            f"        {q(csv(value_types))},",
            f"        {q(seed.root_complex)},",
            f"        {q(seed.variant_element)},",
            f"        {q(seed.variant_complex)},",
            f"        {q(typed_csv(root_seq))},",
            f"        {q(typed_csv(variant_seq))},",
            f"        {q(typed_csv(root_attrs))},",
            f"        {q(typed_csv(variant_attrs))},",
            f"        {q(member_csv(members))},",
            f"        {q(';'.join(defaults))},",
            f"        {q(evidence)},",
            "    },",
        ]
    lines += [
        "}};",
        "",
        "inline const PlatformFormSchemaControl* control_by_type_name(std::string_view type_name) {",
        "    for (const auto& control : logform_layouter_controls) {",
        "        if (control.type_name == type_name) {",
        "            return &control;",
        "        }",
        "    }",
        "    return nullptr;",
        "}",
        "",
        "}  // namespace oof::platform::form_schema",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--xsd", type=Path)
    source.add_argument("--resource-dir", type=Path, help="Directory produced by extract_platform_xml_resources.py")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    xsd = args.xsd if args.xsd is not None else find_logform_schema(args.resource_dir)
    schema = Schema(xsd)
    args.out.write_text(emit_header(schema), encoding="utf-8")


if __name__ == "__main__":
    main()
