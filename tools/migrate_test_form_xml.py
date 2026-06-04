#!/usr/bin/env python3
"""Migrate embedded test Form.xml fixtures from Pages/version to ChildItems/2.0."""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = ROOT / "tests"

CONTROL_TAGS = {
    "ActiveXControl",
    "Button",
    "CalendarField",
    "Chart",
    "CheckBox",
    "ChoiceField",
    "CommandBar",
    "DocumentField",
    "FormattedDocumentField",
    "GeographicalSchema",
    "HTMLDocumentField",
    "InputField",
    "LabelDecoration",
    "Panel",
    "PictureDecoration",
    "PivotChart",
    "ProgressBar",
    "RadioButton",
    "SpreadsheetDocumentField",
    "Splitter",
    "Table",
    "TextDocumentField",
    "UsualGroup",
    "Image",
    "Label",
    "TrackBar",
}

FORM_OPEN = re.compile(
    r'<Form version="(?P<ver>1(?:\.0)?)"(?P<rest>[^>]*)>',
    re.MULTILINE,
)
FORM_OPEN_REPLACEMENT = (
    '<Form ordinaryFormVersion="2.0"{rest} '
    'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" '
    'xsi:noNamespaceSchemaLocation="OrdinaryForm.xsd">'
)


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _wrap_page_controls(page: ET.Element) -> None:
    movable: list[ET.Element] = []
    for child in list(page):
        name = _local_name(child.tag)
        if name in {"Title", "ChildItems"}:
            continue
        if name in CONTROL_TAGS or name == "Action":
            movable.append(child)
    if not movable:
        return
    child_items = page.find("ChildItems")
    if child_items is None:
        child_items = ET.SubElement(page, "ChildItems")
    for child in movable:
        page.remove(child)
        child_items.append(child)


def _migrate_element(element: ET.Element) -> None:
    if _local_name(element.tag) == "Page":
        _wrap_page_controls(element)
    for child in list(element):
        _migrate_element(child)


def migrate_content(text: str) -> str:
    text = FORM_OPEN.sub(
        lambda match: FORM_OPEN_REPLACEMENT.format(rest=match.group("rest")),
        text,
    )
    text = text.replace("<Pages>", "<ChildItems>")
    text = text.replace("</Pages>", "</ChildItems>")

    fragments: list[str] = []
    last = 0
    for match in re.finditer(r"<Form[\s/>][\s\S]*?</Form>", text):
        fragments.append(text[last : match.start()])
        snippet = match.group(0)
        try:
            root = ET.fromstring(snippet)
        except ET.ParseError:
            fragments.append(snippet)
        else:
            _migrate_element(root)
            fragments.append(ET.tostring(root, encoding="unicode"))
        last = match.end()
    fragments.append(text[last:])
    return "".join(fragments)


def main() -> int:
    changed = 0
    for path in sorted(TEST_DIR.glob("test_*.py")):
        original = path.read_text(encoding="utf-8")
        updated = migrate_content(original)
        if updated != original:
            path.write_text(updated, encoding="utf-8")
            changed += 1
            print(path.relative_to(ROOT))
    print(f"updated {changed} test files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
