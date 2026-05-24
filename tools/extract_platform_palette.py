#!/usr/bin/env python3
"""Extract ordinary-form palette evidence from local 1C platform libraries.

The script does not use corpus files as a source of truth. It scans platform
DLL/SO binaries for public control/property vocabulary and compares the result
with the repository XSD palette.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


CONTROL_CANDIDATES = {
    "Panel": ("Панель", "LogFormMobileGroup", "LogFormHierarchyPanel"),
    "LabelDecoration": ("Надпись", "LogFormStaticText"),
    "PictureDecoration": ("ПолеКартинки", "LogFormImage"),
    "Button": ("Кнопка",),
    "InputField": ("ПолеВвода", "LogFormEditField"),
    "CommandBar": ("КоманднаяПанель",),
    "CheckBox": ("Флажок", "LogFormCheckBox"),
    "Table": ("ТабличноеПоле", "LogFormGrid"),
    "ChoiceField": ("ПолеВыбора", "LogFormSwitch"),
    "SpreadsheetDocumentField": ("ПолеТабличногоДокумента", "LogFormMoxel"),
    "GroupBox": ("РамкаГруппы",),
    "RadioButton": ("Переключатель", "LogFormRadioButton"),
    "Splitter": ("Разделитель",),
    "Chart": ("ПолеДиаграммы", "LogFormChart"),
    "PivotChart": ("СводнаяДиаграмма",),
    "GeographicalSchemaField": ("ПолеГеографическойСхемы",),
    "GraphicalSchemaField": ("ПолеГрафическойСхемы",),
    "ListBox": ("ПолеСписка",),
    "HTMLDocumentField": ("ПолеHTMLДокумента", "LogFormHTMLControl"),
    "ProgressBar": ("Индикатор", "LogFormProgressBar"),
    "TrackBar": ("ПолосаРегулирования", "LogFormTrackBar"),
    "CalendarField": ("ПолеКалендаря", "LogFormCalendarWnd"),
    "PeriodChooser": ("ПолеПериода",),
    "TextDocumentField": ("ПолеТекстовогоДокумента", "LogFormTxtEdt"),
    "GanttChart": ("ДиаграммаГанта",),
    "Dendrogram": ("Дендрограмма",),
    "CommandBarButton": ("КнопкаКоманднойПанели",),
}

PROPERTY_CANDIDATES = {
    "Title": ("Заголовок", "Текст"),
    "ToolTip": ("Подсказка",),
    "Visible": ("Видимость",),
    "Enabled": ("Доступность",),
    "ReadOnly": ("ТолькоПросмотр",),
    "DataPath": ("Данные", "ПутьКДанным"),
    "Font": ("Шрифт",),
    "TextColor": ("ЦветТекста",),
    "BackColor": ("ЦветФона",),
    "BorderColor": ("ЦветРамки",),
    "ContextMenu": ("КонтекстноеМеню",),
    "AutoContextMenu": ("АвтоКонтекстноеМеню",),
    "HorizontalAlign": ("ГоризонтальноеПоложение",),
    "VerticalAlign": ("ВертикальноеПоложение",),
    "Picture": ("Картинка",),
    "PicturePosition": ("ПоложениеКартинки",),
    "ButtonType": ("ВидКнопки",),
    "CommandName": ("ИмяКоманды", "Команда"),
    "PagesRepresentation": ("ОтображениеСтраниц",),
    "ChoiceButton": ("КнопкаВыбора",),
    "ClearButton": ("КнопкаОчистки",),
    "OpenButton": ("КнопкаОткрытия",),
    "CreateButton": ("КнопкаСоздания",),
    "EditButton": ("КнопкаРедактирования",),
    "RegulationButton": ("КнопкаРегулирования",),
    "ChoiceList": ("СписокВыбора",),
    "Orientation": ("Ориентация",),
    "Order": ("ПорядокОбхода",),
    "Left": ("Лево",),
    "Top": ("Верх",),
    "Width": ("Ширина",),
    "Height": ("Высота",),
}


@dataclass(frozen=True)
class Hit:
    token: str
    file: str


def utf16le_strings(data: bytes, min_chars: int = 4) -> list[str]:
    result: list[str] = []
    current: list[str] = []
    for index in range(0, len(data) - 1, 2):
        code = data[index] | (data[index + 1] << 8)
        if code in (9, 10, 13) or 32 <= code <= 0xD7FF:
            current.append(chr(code))
        else:
            if len(current) >= min_chars:
                result.append("".join(current))
            current = []
    if len(current) >= min_chars:
        result.append("".join(current))
    return result


def ascii_strings(path: Path) -> list[str]:
    try:
        output = subprocess.check_output(["strings", "-a", str(path)], text=True, stderr=subprocess.DEVNULL)
    except Exception:
        return []
    return output.splitlines()


def platform_libraries(root: Path) -> list[Path]:
    names = {
        "dsgnfrm.dll",
        "dsgnfrm.so",
        "wbase82.dll",
        "mngui.dll",
        "mngcore.dll",
        "core82.dll",
        "frmcore.so",
        "mngui.so",
        "mngcore.so",
        "core85.so",
    }
    return sorted(path for path in root.rglob("*") if path.is_file() and path.name in names)


def scan_tokens(libraries: list[Path], tokens: set[str]) -> dict[str, list[Hit]]:
    hits = {token: [] for token in tokens}
    for path in libraries:
        try:
            data = path.read_bytes()
        except OSError:
            continue
        strings = set(ascii_strings(path))
        strings.update(utf16le_strings(data))
        for text in strings:
            for token in tokens:
                if token in text:
                    hits[token].append(Hit(token=text[:180], file=str(path)))
    return {token: value for token, value in hits.items() if value}


def xsd_palette(path: Path) -> set[str]:
    ns = {"xs": "http://www.w3.org/2001/XMLSchema"}
    root = ET.parse(path).getroot()
    choice = root.find("xs:group[@name='ControlElementGroup']/xs:choice", ns)
    if choice is None:
        return set()
    return {element.get("name", "") for element in choice.findall("xs:element", ns)}


def appinfo_insertable_palette(path: Path) -> set[str]:
    root = ET.parse(path).getroot()
    return {
        control.get("name", "")
        for control in root.findall(".//PlatformPalette/Control")
        if control.get("name") and control.get("insertable") != "false"
    }


def summarize_candidates(candidates: dict[str, tuple[str, ...]], hits: dict[str, list[Hit]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for xml_name, tokens in candidates.items():
        evidence = []
        for token in tokens:
            for hit in hits.get(token, [])[:6]:
                evidence.append({"needle": token, "text": hit.token, "file": hit.file})
        result[xml_name] = {"tokens": tokens, "present": bool(evidence), "evidence": evidence}
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lib-root", default="scan-output/lib-inspect", help="Directory with copied platform libraries")
    parser.add_argument("--xsd", default="src/onec_ordinary_forms/schemas/ordinary-form.xsd")
    parser.add_argument("--out", help="Write JSON report")
    args = parser.parse_args()

    libraries = platform_libraries(Path(args.lib_root))
    tokens = {token for values in CONTROL_CANDIDATES.values() for token in values}
    tokens.update(token for values in PROPERTY_CANDIDATES.values() for token in values)
    hits = scan_tokens(libraries, tokens)
    xsd = xsd_palette(Path(args.xsd))
    appinfo_insertable = appinfo_insertable_palette(Path(args.xsd))
    controls = summarize_candidates(CONTROL_CANDIDATES, hits)
    properties = summarize_candidates(PROPERTY_CANDIDATES, hits)
    platform_controls = {name for name, data in controls.items() if data["present"]}
    report = {
        "libraries": [str(path) for path in libraries],
        "xsd_controls": sorted(xsd),
        "xsd_appinfo_insertable_controls": sorted(appinfo_insertable),
        "platform_controls": sorted(platform_controls),
        "missing_in_xsd": sorted(platform_controls - xsd),
        "xsd_without_platform_evidence": sorted(xsd - platform_controls),
        "xsd_group_appinfo_mismatch": sorted(xsd ^ appinfo_insertable),
        "controls": controls,
        "properties": properties,
    }
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
