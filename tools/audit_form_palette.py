#!/usr/bin/env python3
"""Audit ordinary-form palette consistency against XSD and platform help."""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path


def xsd_controls(path: Path) -> set[str]:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(path).getroot()
    choice = root.find("xs:group[@name='ControlElementGroup']/xs:choice", ns)
    if choice is None:
        raise ValueError(f"{path}: ControlElementGroup not found")
    return {element.get("name", "") for element in choice.findall("xs:element", ns)}


def xsd_control_properties(path: Path, controls: set[str]) -> set[str]:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(path).getroot()
    result: set[str] = set()
    for control in controls:
        choice = root.find(
            f"xs:complexType[@name='{control}Type']/xs:complexContent/xs:extension/xs:choice",
            ns,
        )
        if choice is None:
            raise ValueError(f"{path}: {control}Type choice not found")
        result.update(
            element.get("name", "")
            for element in choice.findall("xs:element", ns)
            if element.get("name") != "Events"
        )
    return result


def load_palette(path: Path, *, include_nested: bool = False) -> dict[str, object]:
    root = ET.parse(path).getroot()
    result: dict[str, object] = {}
    for control in root.findall(".//PlatformPalette/Control"):
        name = control.get("name")
        if not name:
            continue
        if not include_nested and control.get("insertable") == "false":
            continue
        result[name] = {
            "platformName": control.get("platformName", ""),
            "properties": [
                {"name": prop.get("platformName", ""), "type": prop.get("platformType", "")}
                for prop in control.findall("./Properties/Property")
                if prop.get("platformName")
            ],
            "events": [
                {"name": event.get("platformName", "")}
                for event in control.findall("./Events/Event")
                if event.get("platformName")
            ],
        }
    return result


def platform_member_names(palette: dict[str, object], section: str) -> set[str]:
    names: set[str] = set()
    for value in palette.values():
        if not isinstance(value, dict):
            continue
        for item in value.get(section, []):
            if isinstance(item, dict) and item.get("name"):
                names.add(str(item["name"]))
    return names


def platform_type_names(palette: dict[str, object]) -> Counter[str]:
    result: Counter[str] = Counter()
    for value in palette.values():
        if not isinstance(value, dict):
            continue
        for section in ("properties", "events"):
            for item in value.get(section, []):
                if not isinstance(item, dict):
                    continue
                for type_name in re.split(r"\s*;\s*", str(item.get("type", ""))):
                    type_name = type_name.strip()
                    if type_name:
                        result[type_name] += 1
    return result


def help_summary(db_path: Path, palette: dict[str, object]) -> dict[str, object]:
    connection = sqlite3.connect(db_path)
    help_controls = set()
    for value in palette.values():
        if not isinstance(value, dict):
            continue
        platform_name = str(value.get("platformName", ""))
        if not platform_name:
            continue
        rows = connection.execute(
            """
            select count(*)
            from docs
            where domain = 'api_members' and name like ?
            """,
            (f"{platform_name}.%",),
        ).fetchone()
        if rows and rows[0]:
            help_controls.add(platform_name)

    metadata_objects = [
        row[0]
        for row in connection.execute(
            """
            select name
            from docs
            where domain = 'api_objects'
              and (name like 'ОбъектМетаданных%' or name = 'Метаданные' or name = 'ОписаниеТипов')
            order by name
            """
        ).fetchall()
    ]
    configuration_members = [
        row[0]
        for row in connection.execute(
            """
            select name
            from docs
            where domain = 'api_members' and name like 'ОбъектМетаданныхКонфигурация.%'
            order by name
            """
        ).fetchall()
    ]
    api_objects = {
        row[0]
        for row in connection.execute("select name from docs where domain = 'api_objects'").fetchall()
        if row[0]
    }
    type_counter = platform_type_names(palette)
    return {
        "help_controls_with_members": sorted(help_controls),
        "metadata_object_count": len(metadata_objects),
        "metadata_object_sample": metadata_objects[:80],
        "configuration_member_count": len(configuration_members),
        "configuration_member_sample": configuration_members[:80],
        "palette_type_count": len(type_counter),
        "palette_type_names": sorted(type_counter),
        "palette_types_without_api_object": sorted(type_name for type_name in type_counter if type_name not in api_objects),
    }


def audit(args: argparse.Namespace) -> dict[str, object]:
    xsd_control_set = xsd_controls(Path(args.xsd))
    xsd_property_set = xsd_control_properties(Path(args.xsd), xsd_control_set)
    palette = load_palette(Path(args.xsd))
    palette_controls = set(palette)
    platform_properties = platform_member_names(palette, "properties")
    platform_events = platform_member_names(palette, "events")
    public_vocabulary = ET.parse(Path(args.xsd)).getroot().find(".//PlatformPalette")
    public_vocabulary_name = public_vocabulary.get("publicVocabulary", "") if public_vocabulary is not None else ""
    report: dict[str, object] = {
        "xsd_controls": sorted(xsd_control_set),
        "palette_controls": sorted(palette_controls),
        "missing_controls_in_xsd": sorted(palette_controls - xsd_control_set),
        "extra_controls_in_xsd": sorted(xsd_control_set - palette_controls),
        "public_vocabulary": public_vocabulary_name,
        "xsd_control_property_count": len(xsd_property_set),
        "platform_property_name_count": len(platform_properties),
        "platform_event_name_count": len(platform_events),
        "platform_palette_embedded_in_xsd": bool(palette_controls and platform_properties),
        "public_xml_property_names": sorted(xsd_property_set),
        "platform_events": sorted(platform_events),
    }
    if args.help_db:
        report["help"] = help_summary(Path(args.help_db), palette)
    return report


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xsd", default="src/onec_ordinary_forms/schemas/OrdinaryForm.xsd")
    parser.add_argument("--help-db", help="Structured 1C platform help SQLite database")
    parser.add_argument("--out", help="Write JSON report")
    args = parser.parse_args()
    report = audit(args)
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
