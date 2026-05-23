#!/usr/bin/env python3
"""Extract ordinary-form control properties/events from structured 1C help."""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
from pathlib import Path


ORDINARY_CONTROLS = {
    "Panel": "Панель",
    "LabelDecoration": "Надпись",
    "PictureDecoration": "ПолеКартинки",
    "Button": "Кнопка",
    "InputField": "ПолеВвода",
    "CommandBar": "КоманднаяПанель",
    "CheckBox": "Флажок",
    "Table": "ТабличноеПоле",
    "ChoiceField": "ПолеВыбора",
    "SpreadsheetDocumentField": "ПолеТабличногоДокумента",
    "GroupBox": "РамкаГруппы",
    "RadioButton": "Переключатель",
    "Splitter": "Разделитель",
    "Chart": "Диаграмма",
    "ListBox": "ПолеСписка",
    "HTMLDocumentField": "ПолеHTMLДокумента",
    "ProgressBar": "Индикатор",
    "TrackBar": "ПолосаРегулирования",
    "CalendarField": "ПолеКалендаря",
    "TextDocumentField": "ПолеТекстовогоДокумента",
    "CommandBarButton": "КнопкаКоманднойПанели",
}


def member_type(payload: str) -> str:
    match = re.search(r"Тип:\s*([^\.]+)", payload)
    if match is None:
        return ""
    return " ".join(match.group(1).split())


def extract_palette(db_path: Path) -> dict[str, object]:
    connection = sqlite3.connect(db_path)
    result: dict[str, object] = {}
    for xml_name, platform_name in ORDINARY_CONTROLS.items():
        rows = connection.execute(
            """
            select name, topic_path, payload
            from docs
            where domain = 'api_members' and name like ?
            order by name
            """,
            (f"{platform_name}.%",),
        ).fetchall()
        properties: list[dict[str, str]] = []
        events: list[dict[str, str]] = []
        for name, topic_path, payload in rows:
            member_name = str(name).split(".", 1)[1]
            item = {"name": member_name}
            type_name = member_type(str(payload or ""))
            if type_name:
                item["type"] = type_name
            if "/events/" in str(topic_path):
                events.append(item)
            elif "/properties/" in str(topic_path):
                properties.append(item)
        result[xml_name] = {
            "platformName": platform_name,
            "properties": properties,
            "events": events,
        }
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--db", required=True, help="Structured platform help SQLite database")
    parser.add_argument("--out", required=True, help="Output JSON palette")
    args = parser.parse_args()

    palette = extract_palette(Path(args.db))
    output = Path(args.out)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(palette, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
