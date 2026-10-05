#!/usr/bin/env python3
"""Generate the research-only ordinary-form help macro catalog.

The PlatformPalette appinfo is the ordered bootstrap. Bilingual 1C help is the
authority for API object/member names, property access, and version presence.
"""

from __future__ import annotations

import argparse
import html
from collections import Counter, defaultdict
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
from urllib.parse import unquote


EXPECTED_CONTROL_COUNT = 26
EXPECTED_PROPERTY_COUNT = 417
EXPECTED_EVENT_COUNT = 79
EXPECTED_FORM_PROPERTY_COUNT = 37
EXPECTED_FORM_EVENT_COUNT = 13
EXPECTED_COMMAND_BAR_BUTTON_PROPERTY_COUNT = 15
EXPECTED_COMMAND_BAR_BUTTON_EVENT_COUNT = 0
EXPECTED_FORM_CONTROL_EXTENSION_PROPERTY_COUNT = 11
EXPECTED_PANEL_CONTROL_EXTENSION_PROPERTY_COUNT = 9

VERSION_8_2 = "platform_8_2"
VERSION_8_5 = "platform_8_5"
VERSION_BOTH = "platform_8_2_and_8_5"

ALLOWED_ACCESS_TOKENS = {"read_only", "write_only", "read_write", "unknown"}
ALLOWED_VALUE_KIND_TOKENS = {
    "boolean",
    "number",
    "string",
    "date_time",
    "picture",
    "color",
    "font",
    "border",
    "shortcut",
    "binary",
    "identifier",
    "collection",
    "enumeration",
    "object",
    "variant",
    "unknown",
}


# Tokens are the current native ControlKind spellings. Keeping this explicit
# makes an enum rename visible instead of silently deriving a new C++ token.
CONTROL_KIND_TOKENS = {
    "ActiveXControl": "active_x_control",
    "Button": "button",
    "CalendarField": "calendar_field",
    "Chart": "chart",
    "PivotChart": "pivot_chart",
    "CheckBox": "check_box",
    "ChoiceField": "choice_field",
    "CommandBar": "command_bar",
    "Dendrogram": "dendrogram",
    "GeographicalSchemaField": "geographical_schema_field",
    "GraphicalSchemaField": "graphical_schema_field",
    "GroupBox": "usual_group",
    "HTMLDocumentField": "html_document_field",
    "InputField": "input_field",
    "LabelDecoration": "label_decoration",
    "ListBox": "list_box",
    "Panel": "panel",
    "PictureDecoration": "picture_decoration",
    "ProgressBar": "progress_bar",
    "RadioButton": "radio_button",
    "Splitter": "splitter",
    "SpreadsheetDocumentField": "spreadsheet_document_field",
    "Table": "table",
    "TextDocumentField": "text_document_field",
    "GanttChart": "gantt_chart",
    "TrackBar": "track_bar",
}


# Palette bootstrap names normally equal the product XML vocabulary. The
# ordinary GroupBox API is the one deliberate exception in the managed-style
# target object model.
PRODUCT_PUBLIC_CONTROL_NAMES = {"GroupBox": "UsualGroup"}


# Exact bilingual extension pages prove inherited members omitted by the
# corresponding ordinary-object page. They are evidence selectors, not name
# mappings: API names still come from bilingual member occurrences.
FALLBACK_OBJECT_TITLES = {
    "Dendrogram": (
        "Расширение поля формы для поля дендрограммы",
        "Form field extension for a dendrogram field",
    ),
    "GeographicalSchemaField": (
        "Расширение поля формы для поля географической схемы",
        "Form field extension for a geographical schema field",
    ),
    "GraphicalSchemaField": (
        "Расширение поля формы для поля графической схемы",
        "Form field extension for a graphical schema field",
    ),
    "GanttChart": (
        "Расширение поля формы для поля диаграммы Ганта",
        "Form field extension for a Gantt chart field",
    ),
    "RadioButton": (
        "Расширение элементов управления, расположенных в форме",
        "Extension for controls located in a form",
    ),
}


# These are the only globally ambiguous Russian member names needed by the
# palette. Each selected API spelling occurs on the exact fallback page above.
MEMBER_API_OVERRIDES = {
    ("Dendrogram", "property", "Высота", 0): "Height",
    ("GeographicalSchemaField", "property", "Высота", 0): "Height",
    ("GraphicalSchemaField", "property", "Высота", 0): "Height",
    ("GraphicalSchemaField", "property", "Редактирование", 0): "Edit",
}


# The Windows-only ActiveX surface is absent from both unpacked help trees.
# Its two names and types are the explicit PlatformPalette bootstrap evidence;
# no additional ActiveX members are synthesized.
ACTIVEX_API_OBJECT = "ActiveXControl"
ACTIVEX_MEMBER_API_NAMES = {"CLSID": "CLSID", "Состояние": "State"}


FORM_CONTROL_EXTENSION_TITLE = (
    "Расширение элементов управления, расположенных в форме",
    "Extension for controls located in a form",
)
FORM_CONTROL_EXTENSION_API_ORDER = (
    "AutoContextMenu",
    "Data",
    "Value",
    "ModifiesData",
    "Name",
    "ActionSource",
    "DefaultButton",
    "ContextMenu",
    "FirstInGroup",
    "SkipOnInput",
    "ValueType",
)
PANEL_CONTROL_EXTENSION_TITLE = (
    "Расширение элементов управления, расположенных на панели",
    "Panel controls extension",
)
PANEL_CONTROL_EXTENSION_API_ORDER = (
    "DefaultControl",
    "Top",
    "Visible",
    "Height",
    "Left",
    "TabOrder",
    "ZOrder",
    "Collapse",
    "Width",
)


# Exact platform type strings map to deliberately broad integrator categories.
# Unknown is explicit only for a missing type; property names never participate.
PLATFORM_TYPE_TO_VALUE_KIND = {
    "": "unknown",
    "COMОбъект": "object",
    "АвтоРаздвижениеСерий": "enumeration",
    "Булево": "boolean",
    "ВариантПоложенияОкна": "enumeration",
    "ВариантПрикрепленияОкна": "enumeration",
    "ВариантСостоянияОкна": "enumeration",
    "ВариантСпособаОтображенияОкна": "enumeration",
    "ВертикальноеПоложение": "enumeration",
    "ВидПодписейКДиаграмме": "enumeration",
    "ВыделенныеСтрокиТабличногоПоля": "collection",
    "ВыравниваниеКнопокКоманднойПанели": "enumeration",
    "ГоризонтальноеПоложение": "enumeration",
    "Дата": "date_time",
    "Дата ; Неопределено": "date_time",
    "ДвоичныеДанные": "binary",
    "Действие": "object",
    "Идентификатор": "identifier",
    "ИзменениеРазмераОкна": "enumeration",
    "ИзменениеСпособаОтображенияОкна": "enumeration",
    "ИспользованиеВывода": "enumeration",
    "ИспользованиеПолосыПрокрутки": "enumeration",
    "ИспользованиеРежимаМеню": "enumeration",
    "Картинка": "picture",
    "КнопкиКоманднойПанели": "collection",
    "КоллекцияВыделенныхДат": "collection",
    "КоллекцияПолейСводнойДиаграммы": "collection",
    "КолонкаТабличногоПоля": "object",
    "КолонкиТабличногоПоля": "collection",
    "МаксимумСерий": "enumeration",
    "НачальноеОтображениеДерева": "enumeration",
    "НачальноеОтображениеСписка": "enumeration",
    "Неопределено": "variant",
    "Неопределено ; КоманднаяПанель ; КнопкаКоманднойПанели": "variant",
    "ОбластьЗаголовкаДиаграммы": "object",
    "ОбластьЛегендыДиаграммы": "object",
    "ОбластьПостроенияДиаграммы": "object",
    "ОписаниеТипов": "object",
    "Ориентация": "enumeration",
    "ОриентацияДиаграммы": "enumeration",
    "ОтображениеЗакладок": "enumeration",
    "ОтображениеЗначенияИзмерительнойДиаграммы": "enumeration",
    "ОтображениеКнопкиКоманднойПанели": "enumeration",
    "ОтображениеРазметкиПолосыРегулирования": "enumeration",
    "ПалитраЦветовДиаграммы": "enumeration",
    "Панель": "object",
    "ПоказываемаяОбластьГеографическойСхемы": "object",
    "ПоложениеЗаголовка": "enumeration",
    "ПоложениеКартинкиКнопки": "enumeration",
    "ПоложениеКартинкиНадписи": "enumeration",
    "ПоложениеКартинкиПанели": "enumeration",
    "ПоложениеПодписейКДиаграмме": "enumeration",
    "ПоложениеПодписейШкалыЗначенийИзмерительнойДиаграммы": "enumeration",
    "ПолосыИзмерительнойДиаграммы": "collection",
    "ПорядокКнопокКоманднойПанели": "enumeration",
    "Произвольный": "variant",
    "РазмерКартинки": "enumeration",
    "Рамка": "border",
    "РежимБегущейСтроки": "enumeration",
    "РежимВводаСтрокТабличногоПоля": "enumeration",
    "РежимВыбораНезаполненного": "enumeration",
    "РежимВыделенияДаты": "enumeration",
    "РежимВыделенияСтрокиТабличногоПоля": "enumeration",
    "РежимВыделенияТабличногоПоля": "enumeration",
    "РежимОтображенияГеографическойСхемы": "enumeration",
    "РежимПробеловДиаграммы": "enumeration",
    "РежимСглаживанияИндикатора": "enumeration",
    "РежимСверткиЭлементаУправления": "enumeration",
    "РезультатЗапроса ; ПостроительОтчета": "variant",
    "СерииДиаграммы": "collection",
    "СерияДиаграммы": "object",
    "Совпадает с описанием типа первого в группе переключателя": "variant",
    "СочетаниеКлавиш": "shortcut",
    "СписокЗначений": "collection",
    "Стиль": "object",
    "СтраницаПанели": "object",
    "СтраницыПанели": "collection",
    "Строка": "string",
    "ТаблицаЗначений ; ОбластьЯчеекТабличногоДокумента": "variant",
    "ТипДиаграммы": "enumeration",
    "ТипЕдиницыШкалыВремени": "enumeration",
    "ТипКнопкиКоманднойПанели": "enumeration",
    "ТипОтображенияВыделенияТабличногоДокумента": "enumeration",
    "ТипПоведенияКлавишиEnter": "enumeration",
    "ТипСводнойДиаграммы": "enumeration",
    "ТочкаДиаграммы": "object",
    "ТочкиДиаграммы": "collection",
    "Форма": "object",
    "Форма ; Элемент управления": "variant",
    "Цвет": "color",
    "Число": "number",
    "Шрифт": "font",
    "ЭлементГрафическойСхемы": "object",
    "ЭлементСпискаЗначений": "object",
    "ЭлементыФормы": "collection",
}


class ExtractionError(RuntimeError):
    """A reproducibility or metamodel contract violation."""


@dataclass(frozen=True)
class BilingualPair:
    russian: str
    english: str


@dataclass(frozen=True)
class PageMember:
    kind: str
    russian_name: str
    api_name: str
    href: str


@dataclass(frozen=True)
class ParsedPage:
    title: str
    heading: str
    members: tuple[PageMember, ...]
    sections: dict[str, str]


@dataclass(frozen=True)
class ObjectPage:
    path: Path
    russian_name: str
    api_name: str
    members: tuple[PageMember, ...]


@dataclass(frozen=True)
class MemberEvidence:
    path: Path
    kind: str
    owner_russian: str
    owner_api: str
    russian_name: str
    api_name: str
    access: str
    platform_type: str


@dataclass(frozen=True)
class PaletteMember:
    russian_name: str
    platform_type: str = ""


@dataclass(frozen=True)
class PaletteControl:
    public_name: str
    russian_name: str
    insertable: bool
    properties: tuple[PaletteMember, ...]
    events: tuple[PaletteMember, ...]


@dataclass(frozen=True)
class PropertyCatalogEntry:
    order: int
    xml_name: str
    api_name: str
    russian_name: str
    platform_type: str
    value_kind: str
    access: str
    version_mask: str


@dataclass(frozen=True)
class EventCatalogEntry:
    order: int
    xml_name: str
    api_name: str
    russian_name: str
    version_mask: str


@dataclass(frozen=True)
class ControlCatalogEntry:
    kind_token: str
    public_name: str
    api_object: str
    russian_name: str
    properties: tuple[PropertyCatalogEntry, ...]
    events: tuple[EventCatalogEntry, ...]


@dataclass(frozen=True)
class Catalog:
    controls: tuple[ControlCatalogEntry, ...]
    form_control_extension_properties: tuple[PropertyCatalogEntry, ...]
    panel_control_extension_properties: tuple[PropertyCatalogEntry, ...]
    form_properties: tuple[PropertyCatalogEntry, ...]
    form_events: tuple[EventCatalogEntry, ...]
    command_bar_button_properties: int
    command_bar_button_events: int
    dynamic_form_attributes_excluded: int


def normalize_text(value: str) -> str:
    return " ".join(html.unescape(value).replace("\ufeff", "").split())


def canonical_platform_type(value: str) -> str:
    compact = normalize_text(value)
    return re.sub(r"\s*[,;]\s*", " ; ", compact)


def parse_bilingual_pair(value: str) -> BilingualPair | None:
    text = normalize_text(value)
    marker = text.rfind(" (")
    if marker <= 0 or not text.endswith(")"):
        return None
    russian = text[:marker].strip()
    english = text[marker + 2 : -1].strip()
    if not russian or not english:
        return None
    return BilingualPair(russian, english)


def chapter_key(value: str) -> str:
    normalized = normalize_text(value).rstrip(":").casefold()
    if normalized.startswith("использование в версии") or normalized.startswith(
        "version use"
    ):
        return "version"
    prefixes = {
        "свойства": "properties",
        "properties": "properties",
        "события": "events",
        "events": "events",
        "использование": "usage",
        "usage": "usage",
        "описание": "description",
        "description": "description",
    }
    for prefix, key in prefixes.items():
        if normalized.startswith(prefix):
            return key
    return normalized


class HelpHtmlParser(HTMLParser):
    """Small structured parser for the V8SH pages used by this extractor."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.title = ""
        self.heading = ""
        self.members: list[PageMember] = []
        self.section_chunks: defaultdict[str, list[str]] = defaultdict(list)
        self.current_section = ""
        self.capture_kind = ""
        self.capture_tag = ""
        self.capture_chunks: list[str] = []
        self.anchor_href = ""
        self.anchor_chunks: list[str] = []

    @staticmethod
    def _classes(attrs: list[tuple[str, str | None]]) -> set[str]:
        value = next((value for key, value in attrs if key.casefold() == "class"), "")
        return set((value or "").split())

    def _begin_capture(self, kind: str, tag: str) -> None:
        self.capture_kind = kind
        self.capture_tag = tag.casefold()
        self.capture_chunks = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        classes = self._classes(attrs)
        lowered = tag.casefold()
        if "V8SH_pagetitle" in classes:
            self._begin_capture("title", lowered)
        elif "V8SH_heading" in classes:
            self._begin_capture("heading", lowered)
        elif "V8SH_chapter" in classes:
            self.current_section = ""
            self._begin_capture("chapter", lowered)

        if lowered == "a":
            self.anchor_href = next(
                (value or "" for key, value in attrs if key.casefold() == "href"), ""
            )
            self.anchor_chunks = []
        elif lowered == "hr":
            self.current_section = ""

    def handle_data(self, data: str) -> None:
        if self.capture_kind:
            self.capture_chunks.append(data)
        if self.anchor_href:
            self.anchor_chunks.append(data)
        if self.current_section and self.capture_kind != "chapter":
            self.section_chunks[self.current_section].append(data)

    def handle_endtag(self, tag: str) -> None:
        lowered = tag.casefold()
        if lowered == "a" and self.anchor_href:
            text = normalize_text("".join(self.anchor_chunks))
            pair = parse_bilingual_pair(text)
            if pair is not None and self.current_section in {"properties", "events"}:
                kind = "property" if self.current_section == "properties" else "event"
                self.members.append(
                    PageMember(kind, pair.russian, pair.english, self.anchor_href)
                )
            self.anchor_href = ""
            self.anchor_chunks = []

        if self.capture_kind and lowered == self.capture_tag:
            value = normalize_text("".join(self.capture_chunks))
            if self.capture_kind == "title":
                self.title = value
            elif self.capture_kind == "heading":
                self.heading = value
            elif self.capture_kind == "chapter":
                self.current_section = chapter_key(value)
            self.capture_kind = ""
            self.capture_tag = ""
            self.capture_chunks = []

    def parsed(self) -> ParsedPage:
        return ParsedPage(
            title=self.title,
            heading=self.heading,
            members=tuple(self.members),
            sections={
                key: normalize_text(" ".join(chunks))
                for key, chunks in self.section_chunks.items()
            },
        )


def read_help_text(path: Path) -> str:
    raw = path.read_bytes()
    for encoding in ("utf-8-sig", "cp1251", "cp866", "latin-1"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", errors="replace")


def parse_help_page(path: Path, *, header_only: bool = False) -> ParsedPage:
    text = read_help_text(path)
    parser = HelpHtmlParser()
    parser.feed(text[:65536] if header_only else text)
    parser.close()
    return parser.parsed()


def parse_member_identity(
    page: ParsedPage,
) -> tuple[BilingualPair, BilingualPair] | None:
    title_pair = parse_bilingual_pair(page.title)
    heading_pair = parse_bilingual_pair(page.heading)
    if title_pair is None:
        return None
    if heading_pair is not None:
        russian_suffix = "." + heading_pair.russian
        english_suffix = "." + heading_pair.english
        if not title_pair.russian.endswith(
            russian_suffix
        ) or not title_pair.english.endswith(english_suffix):
            return None
        owner = BilingualPair(
            title_pair.russian[: -len(russian_suffix)],
            title_pair.english[: -len(english_suffix)],
        )
        return owner, heading_pair
    if "." not in title_pair.russian or "." not in title_pair.english:
        return None
    owner_russian, member_russian = title_pair.russian.rsplit(".", 1)
    owner_api, member_api = title_pair.english.rsplit(".", 1)
    return BilingualPair(owner_russian, owner_api), BilingualPair(
        member_russian, member_api
    )


def access_from_usage(value: str) -> str:
    normalized = normalize_text(value).rstrip(".").casefold()
    exact = {
        "чтение и запись": "read_write",
        "чтение, запись": "read_write",
        "read and write": "read_write",
        "только чтение": "read_only",
        "чтение": "read_only",
        "read only": "read_only",
        "только запись": "write_only",
        "запись": "write_only",
        "write only": "write_only",
    }
    return exact.get(normalized, "unknown")


def platform_type_from_description(value: str) -> str:
    description = normalize_text(value)
    match = re.search(r"(?:^|\s)Тип:\s*(.*?)(?:\s*\.\s|$)", description)
    if match is None:
        return ""
    return canonical_platform_type(match.group(1))


class HelpCorpus:
    def __init__(self, root: Path, target_object_names: set[str]) -> None:
        self.root = root.resolve()
        self.object_pages: defaultdict[str, list[ObjectPage]] = defaultdict(list)
        self.member_pages: dict[Path, MemberEvidence] = {}
        self.global_members: defaultdict[tuple[str, str], list[MemberEvidence]] = (
            defaultdict(list)
        )
        self._build(target_object_names)

    def _build(self, target_object_names: set[str]) -> None:
        objects_root = self.root / "objects"
        if not objects_root.is_dir():
            raise ExtractionError(f"help root has no objects directory: {self.root}")
        paths = sorted(
            objects_root.rglob("*.html"),
            key=lambda path: path.relative_to(self.root).as_posix(),
        )
        if not paths:
            raise ExtractionError(f"help root contains no HTML pages: {self.root}")

        for path in paths:
            relative_parts = {
                part.casefold() for part in path.relative_to(self.root).parts
            }
            if "properties" in relative_parts:
                member_kind = "property"
            elif "events" in relative_parts:
                member_kind = "event"
            else:
                member_kind = ""

            if member_kind:
                page = parse_help_page(path)
                identity = parse_member_identity(page)
                if identity is None:
                    continue
                owner, member = identity
                evidence = MemberEvidence(
                    path=path.resolve(),
                    kind=member_kind,
                    owner_russian=owner.russian,
                    owner_api=owner.english,
                    russian_name=member.russian,
                    api_name=member.english,
                    access=access_from_usage(page.sections.get("usage", "")),
                    platform_type=platform_type_from_description(
                        page.sections.get("description", "")
                    ),
                )
                self.member_pages[evidence.path] = evidence
                self.global_members[(member_kind, member.russian)].append(evidence)
                continue

            header = parse_help_page(path, header_only=True)
            pair = parse_bilingual_pair(header.title)
            if pair is None or pair.russian not in target_object_names:
                continue
            page = parse_help_page(path)
            full_pair = parse_bilingual_pair(page.title)
            if full_pair != pair:
                raise ExtractionError(f"object title changed during full parse: {path}")
            self.object_pages[pair.russian].append(
                ObjectPage(path.resolve(), pair.russian, pair.english, page.members)
            )

        for key in self.global_members:
            self.global_members[key].sort(
                key=lambda item: item.path.relative_to(self.root).as_posix()
            )
        for key in self.object_pages:
            self.object_pages[key].sort(
                key=lambda item: item.path.relative_to(self.root).as_posix()
            )

    def require_object(
        self, russian_name: str, api_name: str | None = None
    ) -> ObjectPage:
        candidates = self.object_pages.get(russian_name, [])
        if api_name is not None:
            candidates = [item for item in candidates if item.api_name == api_name]
        if len(candidates) != 1:
            names = [item.api_name for item in candidates]
            raise ExtractionError(
                f"expected one exact object page for {russian_name!r}/{api_name!r}, got {names!r}"
            )
        return candidates[0]

    def require_object_with_property_order(
        self,
        russian_name: str,
        api_name: str,
        expected_api_order: tuple[str, ...],
    ) -> ObjectPage:
        candidates = [
            item
            for item in self.object_pages.get(russian_name, [])
            if item.api_name == api_name
            and tuple(
                member.api_name for member in item.members if member.kind == "property"
            )
            == expected_api_order
        ]
        if len(candidates) != 1:
            observed = [
                tuple(
                    member.api_name
                    for member in item.members
                    if member.kind == "property"
                )
                for item in self.object_pages.get(russian_name, [])
                if item.api_name == api_name
            ]
            raise ExtractionError(
                f"expected one exact extension page for {russian_name!r}/{api_name!r}; "
                f"property orders={observed!r}"
            )
        return candidates[0]

    def api_candidates(self, kind: str, russian_name: str) -> set[str]:
        return {
            item.api_name for item in self.global_members.get((kind, russian_name), [])
        }

    def evidence_for_link(self, page: ObjectPage, member: PageMember) -> MemberEvidence:
        href = unquote(member.href.split("#", 1)[0])
        if not href or "://" in href:
            raise ExtractionError(
                f"member link is not a local help page: {member.href!r}"
            )
        target = (page.path.parent / href).resolve()
        try:
            target.relative_to(self.root)
        except ValueError as error:
            raise ExtractionError(
                f"member link escapes help root: {member.href!r}"
            ) from error
        evidence = self.member_pages.get(target)
        if evidence is None:
            raise ExtractionError(
                f"linked member page was not indexed: {member.href!r}"
            )
        if (
            evidence.kind != member.kind
            or evidence.russian_name != member.russian_name
            or evidence.api_name != member.api_name
        ):
            raise ExtractionError(
                f"linked member identity mismatch for {member.russian_name!r}/{member.api_name!r}"
            )
        return evidence


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def load_palette(path: Path) -> tuple[tuple[PaletteControl, ...], PaletteControl]:
    root = ET.parse(path).getroot()
    platform_palette = next(
        (
            element
            for element in root.iter()
            if local_name(element.tag) == "PlatformPalette"
        ),
        None,
    )
    if platform_palette is None:
        raise ExtractionError("PlatformPalette appinfo not found")

    controls: list[PaletteControl] = []
    for element in platform_palette:
        if local_name(element.tag) != "Control":
            continue
        public_name = element.get("name", "").strip()
        russian_name = element.get("platformName", "").strip()
        if not public_name or not russian_name:
            raise ExtractionError("palette control has an empty name or platformName")
        properties_parent = next(
            (child for child in element if local_name(child.tag) == "Properties"), None
        )
        events_parent = next(
            (child for child in element if local_name(child.tag) == "Events"), None
        )
        properties = tuple(
            PaletteMember(
                child.get("platformName", "").strip(),
                canonical_platform_type(child.get("platformType", "")),
            )
            for child in (properties_parent if properties_parent is not None else ())
            if local_name(child.tag) == "Property"
        )
        events = tuple(
            PaletteMember(child.get("platformName", "").strip())
            for child in (events_parent if events_parent is not None else ())
            if local_name(child.tag) == "Event"
        )
        if any(not member.russian_name for member in properties + events):
            raise ExtractionError(
                f"palette control {public_name!r} has an unnamed member"
            )
        controls.append(
            PaletteControl(
                public_name=public_name,
                russian_name=russian_name,
                insertable=element.get("insertable", "true").casefold() != "false",
                properties=properties,
                events=events,
            )
        )

    public_ids = [control.public_name for control in controls]
    russian_ids = [control.russian_name for control in controls]
    if len(public_ids) != len(set(public_ids)):
        raise ExtractionError("duplicate public control IDs in PlatformPalette")
    if len(russian_ids) != len(set(russian_ids)):
        raise ExtractionError("duplicate Russian control IDs in PlatformPalette")

    insertable = tuple(control for control in controls if control.insertable)
    nested = tuple(control for control in controls if not control.insertable)
    if len(insertable) != EXPECTED_CONTROL_COUNT:
        raise ExtractionError(
            f"control count drift: {len(insertable)} != {EXPECTED_CONTROL_COUNT}"
        )
    if (
        sum(len(control.properties) for control in insertable)
        != EXPECTED_PROPERTY_COUNT
    ):
        raise ExtractionError("insertable property occurrence count drift")
    if sum(len(control.events) for control in insertable) != EXPECTED_EVENT_COUNT:
        raise ExtractionError("insertable event occurrence count drift")
    if set(CONTROL_KIND_TOKENS) != {control.public_name for control in insertable}:
        raise ExtractionError(
            "ControlKind token table does not exactly cover insertable controls"
        )
    if not set(PRODUCT_PUBLIC_CONTROL_NAMES).issubset(
        {control.public_name for control in insertable}
    ):
        raise ExtractionError(
            "product public-control override names an unknown palette control"
        )
    product_public_ids = [
        PRODUCT_PUBLIC_CONTROL_NAMES.get(control.public_name, control.public_name)
        for control in insertable
    ]
    if len(product_public_ids) != len(set(product_public_ids)):
        raise ExtractionError("duplicate product public control IDs")
    kind_tokens = [CONTROL_KIND_TOKENS[control.public_name] for control in insertable]
    if len(kind_tokens) != len(set(kind_tokens)):
        raise ExtractionError("duplicate native ControlKind tokens")
    if len(nested) != 1 or nested[0].public_name != "CommandBarButton":
        raise ExtractionError(
            "expected CommandBarButton as the only non-insertable control"
        )
    if len(nested[0].properties) != EXPECTED_COMMAND_BAR_BUTTON_PROPERTY_COUNT:
        raise ExtractionError("CommandBarButton property count drift")
    if len(nested[0].events) != EXPECTED_COMMAND_BAR_BUTTON_EVENT_COUNT:
        raise ExtractionError("CommandBarButton event count drift")
    return insertable, nested[0]


def members_named(page: ObjectPage, kind: str, russian_name: str) -> list[PageMember]:
    return [
        member
        for member in page.members
        if member.kind == kind and member.russian_name == russian_name
    ]


def nth_member(
    page: ObjectPage, kind: str, russian_name: str, occurrence: int
) -> PageMember | None:
    matches = members_named(page, kind, russian_name)
    return matches[occurrence] if occurrence < len(matches) else None


def version_mask(present_8_2: bool, present_8_5: bool) -> str:
    if present_8_2 and present_8_5:
        return VERSION_BOTH
    if present_8_2:
        return VERSION_8_2
    if present_8_5:
        return VERSION_8_5
    raise ExtractionError("member has no presence in either help corpus")


def combine_access(evidence: list[MemberEvidence]) -> str:
    known = {item.access for item in evidence if item.access != "unknown"}
    if len(known) > 1:
        raise ExtractionError(f"conflicting API access evidence: {sorted(known)!r}")
    result = next(iter(known), "unknown")
    if result not in ALLOWED_ACCESS_TOKENS:
        raise ExtractionError(f"unsupported API access token: {result!r}")
    return result


def combine_platform_type(evidence: list[MemberEvidence]) -> str:
    known = {item.platform_type for item in evidence if item.platform_type}
    if len(known) > 1:
        raise ExtractionError(f"conflicting platform type evidence: {sorted(known)!r}")
    return next(iter(known), "")


def value_kind_for(platform_type: str) -> str:
    canonical = canonical_platform_type(platform_type)
    if canonical not in PLATFORM_TYPE_TO_VALUE_KIND:
        raise ExtractionError(
            f"platform type has no explicit value-kind mapping: {canonical!r}"
        )
    token = PLATFORM_TYPE_TO_VALUE_KIND[canonical]
    if token not in ALLOWED_VALUE_KIND_TOKENS:
        raise ExtractionError(f"unsupported value-kind token: {token!r}")
    return token


def exact_control_pages(
    control: PaletteControl, corpus_8_2: HelpCorpus, corpus_8_5: HelpCorpus
) -> tuple[ObjectPage, ObjectPage, str]:
    page_8_2 = corpus_8_2.require_object(control.russian_name)
    page_8_5 = corpus_8_5.require_object(control.russian_name)
    if page_8_2.api_name != page_8_5.api_name:
        raise ExtractionError(
            f"control API object drift for {control.public_name}: "
            f"{page_8_2.api_name!r} versus {page_8_5.api_name!r}"
        )
    return page_8_2, page_8_5, page_8_2.api_name


def fallback_pages(
    control: PaletteControl, corpus_8_2: HelpCorpus, corpus_8_5: HelpCorpus
) -> tuple[ObjectPage, ObjectPage]:
    title = FALLBACK_OBJECT_TITLES.get(control.public_name)
    if title is None:
        raise ExtractionError(
            f"{control.public_name} needs inherited-member evidence but has no exact fallback page"
        )
    if control.public_name == "RadioButton":
        return (
            corpus_8_2.require_object_with_property_order(
                title[0], title[1], FORM_CONTROL_EXTENSION_API_ORDER
            ),
            corpus_8_5.require_object_with_property_order(
                title[0], title[1], FORM_CONTROL_EXTENSION_API_ORDER
            ),
        )
    return (
        corpus_8_2.require_object(title[0], title[1]),
        corpus_8_5.require_object(title[0], title[1]),
    )


def resolve_palette_member(
    control: PaletteControl,
    member: PaletteMember,
    kind: str,
    order: int,
    same_name_occurrence: int,
    direct_pages: tuple[ObjectPage, ObjectPage],
    corpora: tuple[HelpCorpus, HelpCorpus],
) -> tuple[str, list[MemberEvidence], str]:
    direct_links = tuple(
        nth_member(page, kind, member.russian_name, same_name_occurrence)
        for page in direct_pages
    )
    direct_api_names = {link.api_name for link in direct_links if link is not None}
    using_fallback = not direct_api_names

    if len(direct_api_names) > 1:
        raise ExtractionError(
            f"direct API name drift for {control.public_name}.{member.russian_name}: "
            f"{sorted(direct_api_names)!r}"
        )
    if direct_api_names:
        api_name = next(iter(direct_api_names))
        selected_pages = direct_pages
        selected_links = direct_links
    else:
        global_candidates: set[str] = set()
        for corpus in corpora:
            global_candidates.update(corpus.api_candidates(kind, member.russian_name))
        if len(global_candidates) == 1:
            api_name = next(iter(global_candidates))
        else:
            override_key = (
                control.public_name,
                kind,
                member.russian_name,
                same_name_occurrence,
            )
            api_name = MEMBER_API_OVERRIDES.get(override_key, "")
            if not api_name or api_name not in global_candidates:
                raise ExtractionError(
                    f"unresolved English name for {control.public_name}.{member.russian_name} "
                    f"at order {order}: candidates={sorted(global_candidates)!r}"
                )
        selected_pages = fallback_pages(control, *corpora)
        selected_links = tuple(
            next(
                (
                    item
                    for item in page.members
                    if item.kind == kind
                    and item.russian_name == member.russian_name
                    and item.api_name == api_name
                ),
                None,
            )
            for page in selected_pages
        )

    evidence: list[MemberEvidence] = []
    presence: list[bool] = []
    for corpus, page, link in zip(corpora, selected_pages, selected_links, strict=True):
        if link is None:
            presence.append(False)
            continue
        if link.api_name != api_name:
            raise ExtractionError(
                f"API occurrence mismatch for {control.public_name}.{member.russian_name}"
            )
        presence.append(True)
        evidence.append(corpus.evidence_for_link(page, link))
    if using_fallback and not evidence:
        raise ExtractionError(
            f"fallback produced no evidence for {control.public_name}.{member.russian_name}"
        )
    return api_name, evidence, version_mask(presence[0], presence[1])


def build_control_catalog(
    controls: tuple[PaletteControl, ...], corpus_8_2: HelpCorpus, corpus_8_5: HelpCorpus
) -> tuple[ControlCatalogEntry, ...]:
    result: list[ControlCatalogEntry] = []
    corpora = (corpus_8_2, corpus_8_5)
    for control in controls:
        if control.public_name == "ActiveXControl":
            properties = tuple(
                PropertyCatalogEntry(
                    order=order,
                    xml_name=ACTIVEX_MEMBER_API_NAMES[member.russian_name],
                    api_name=ACTIVEX_MEMBER_API_NAMES[member.russian_name],
                    russian_name=member.russian_name,
                    platform_type=member.platform_type,
                    value_kind=value_kind_for(member.platform_type),
                    access="unknown",
                    version_mask=VERSION_BOTH,
                )
                for order, member in enumerate(control.properties)
            )
            result.append(
                ControlCatalogEntry(
                    CONTROL_KIND_TOKENS[control.public_name],
                    PRODUCT_PUBLIC_CONTROL_NAMES.get(
                        control.public_name, control.public_name
                    ),
                    ACTIVEX_API_OBJECT,
                    control.russian_name,
                    properties,
                    (),
                )
            )
            continue

        page_8_2, page_8_5, api_object = exact_control_pages(control, *corpora)
        direct_pages = (page_8_2, page_8_5)
        properties: list[PropertyCatalogEntry] = []
        property_occurrences: Counter[str] = Counter()
        for order, member in enumerate(control.properties):
            occurrence = property_occurrences[member.russian_name]
            property_occurrences[member.russian_name] += 1
            api_name, evidence, mask = resolve_palette_member(
                control,
                member,
                "property",
                order,
                occurrence,
                direct_pages,
                corpora,
            )
            properties.append(
                PropertyCatalogEntry(
                    order=order,
                    xml_name=api_name,
                    api_name=api_name,
                    russian_name=member.russian_name,
                    platform_type=member.platform_type,
                    value_kind=value_kind_for(member.platform_type),
                    access=combine_access(evidence),
                    version_mask=mask,
                )
            )

        events: list[EventCatalogEntry] = []
        event_occurrences: Counter[str] = Counter()
        for order, member in enumerate(control.events):
            occurrence = event_occurrences[member.russian_name]
            event_occurrences[member.russian_name] += 1
            api_name, _evidence, mask = resolve_palette_member(
                control,
                member,
                "event",
                order,
                occurrence,
                direct_pages,
                corpora,
            )
            events.append(
                EventCatalogEntry(order, api_name, api_name, member.russian_name, mask)
            )

        result.append(
            ControlCatalogEntry(
                CONTROL_KIND_TOKENS[control.public_name],
                PRODUCT_PUBLIC_CONTROL_NAMES.get(
                    control.public_name, control.public_name
                ),
                api_object,
                control.russian_name,
                tuple(properties),
                tuple(events),
            )
        )
    return tuple(result)


def form_links(page: ObjectPage, kind: str) -> list[PageMember]:
    return [member for member in page.members if member.kind == kind]


def is_dynamic_form_attribute(member: PageMember) -> bool:
    return (
        member.api_name == "<Attribute name>"
        or member.russian_name == "<Имя реквизита>"
    )


def merge_form_order(
    primary: list[PageMember], secondary: list[PageMember]
) -> list[PageMember]:
    result = list(primary)
    seen = {(member.russian_name, member.api_name) for member in result}
    for member in secondary:
        key = (member.russian_name, member.api_name)
        if key not in seen:
            result.append(member)
            seen.add(key)
    if len(seen) != len(result):
        raise ExtractionError("duplicate exact member IDs on Form page")
    return result


def exact_member_by_pair(
    page: ObjectPage, kind: str, member: PageMember
) -> PageMember | None:
    matches = [
        item
        for item in page.members
        if item.kind == kind
        and item.russian_name == member.russian_name
        and item.api_name == member.api_name
    ]
    if len(matches) > 1:
        raise ExtractionError(
            f"duplicate Form member pair: {member.russian_name}/{member.api_name}"
        )
    return matches[0] if matches else None


def build_extension_property_catalog(
    title: tuple[str, str],
    expected_api_order: tuple[str, ...],
    corpus_8_2: HelpCorpus,
    corpus_8_5: HelpCorpus,
) -> tuple[PropertyCatalogEntry, ...]:
    corpora = (corpus_8_2, corpus_8_5)
    pages = tuple(
        corpus.require_object_with_property_order(
            title[0], title[1], expected_api_order
        )
        for corpus in corpora
    )
    members_by_version = tuple(form_links(page, "property") for page in pages)
    if tuple(member.api_name for member in members_by_version[0]) != expected_api_order:
        raise ExtractionError(f"8.2 extension property order drift for {title[1]}")
    if tuple(member.api_name for member in members_by_version[1]) != expected_api_order:
        raise ExtractionError(f"8.5 extension property order drift for {title[1]}")

    result: list[PropertyCatalogEntry] = []
    for order, api_name in enumerate(expected_api_order):
        links = (members_by_version[0][order], members_by_version[1][order])
        if links[0].api_name != api_name or links[1].api_name != api_name:
            raise ExtractionError(f"extension API name drift for {title[1]}.{api_name}")
        if links[0].russian_name != links[1].russian_name:
            raise ExtractionError(
                f"extension Russian name drift for {title[1]}.{api_name}"
            )
        evidence = [
            corpus.evidence_for_link(page, link)
            for corpus, page, link in zip(corpora, pages, links, strict=True)
        ]
        platform_type = combine_platform_type(evidence)
        result.append(
            PropertyCatalogEntry(
                order=order,
                xml_name=api_name,
                api_name=api_name,
                russian_name=links[1].russian_name,
                platform_type=platform_type,
                value_kind=value_kind_for(platform_type),
                access=combine_access(evidence),
                version_mask=VERSION_BOTH,
            )
        )
    return tuple(result)


def build_form_catalog(
    corpus_8_2: HelpCorpus, corpus_8_5: HelpCorpus
) -> tuple[tuple[PropertyCatalogEntry, ...], tuple[EventCatalogEntry, ...], int]:
    page_8_2 = corpus_8_2.require_object("Форма", "Form")
    page_8_5 = corpus_8_5.require_object("Форма", "Form")
    corpora = (corpus_8_2, corpus_8_5)
    pages = (page_8_2, page_8_5)

    raw_properties = (
        form_links(page_8_2, "property"),
        form_links(page_8_5, "property"),
    )
    dynamic_pairs = {
        (member.russian_name, member.api_name)
        for members in raw_properties
        for member in members
        if is_dynamic_form_attribute(member)
    }
    property_order = merge_form_order(
        [
            member
            for member in raw_properties[1]
            if not is_dynamic_form_attribute(member)
        ],
        [
            member
            for member in raw_properties[0]
            if not is_dynamic_form_attribute(member)
        ],
    )
    event_order = merge_form_order(
        form_links(page_8_5, "event"), form_links(page_8_2, "event")
    )
    if len(property_order) != EXPECTED_FORM_PROPERTY_COUNT:
        raise ExtractionError(
            f"Form property count drift: {len(property_order)} != {EXPECTED_FORM_PROPERTY_COUNT}"
        )
    if len(event_order) != EXPECTED_FORM_EVENT_COUNT:
        raise ExtractionError(
            f"Form event count drift: {len(event_order)} != {EXPECTED_FORM_EVENT_COUNT}"
        )
    if len(dynamic_pairs) != 1:
        raise ExtractionError(
            f"dynamic Form attribute count drift: {len(dynamic_pairs)} != 1"
        )

    properties: list[PropertyCatalogEntry] = []
    for order, canonical in enumerate(property_order):
        links = tuple(
            exact_member_by_pair(page, "property", canonical) for page in pages
        )
        evidence = [
            corpus.evidence_for_link(page, link)
            for corpus, page, link in zip(corpora, pages, links, strict=True)
            if link is not None
        ]
        platform_type = combine_platform_type(evidence)
        properties.append(
            PropertyCatalogEntry(
                order=order,
                xml_name=canonical.api_name,
                api_name=canonical.api_name,
                russian_name=canonical.russian_name,
                platform_type=platform_type,
                value_kind=value_kind_for(platform_type),
                access=combine_access(evidence),
                version_mask=version_mask(links[0] is not None, links[1] is not None),
            )
        )

    events: list[EventCatalogEntry] = []
    for order, canonical in enumerate(event_order):
        links = tuple(exact_member_by_pair(page, "event", canonical) for page in pages)
        if any(link is not None for link in links):
            for corpus, page, link in zip(corpora, pages, links, strict=True):
                if link is not None:
                    corpus.evidence_for_link(page, link)
        events.append(
            EventCatalogEntry(
                order,
                canonical.api_name,
                canonical.api_name,
                canonical.russian_name,
                version_mask(links[0] is not None, links[1] is not None),
            )
        )
    return tuple(properties), tuple(events), len(dynamic_pairs)


def validate_input_field_examples(controls: tuple[ControlCatalogEntry, ...]) -> None:
    input_field = next(
        (control for control in controls if control.public_name == "InputField"), None
    )
    if input_field is None:
        raise ExtractionError("InputField catalog entry is missing")
    required = {
        ("АвтоВыборНезаполненного", "AutoChoiceIncomplete"),
        ("ВыбиратьТип", "ChooseType"),
        ("ТолькоПросмотр", "ReadOnly"),
        ("КартинкаКнопкиВыбора", "SelButtonPicture"),
        ("КартинкаКнопкиВыбора", "ChoiceButtonPicture"),
    }
    actual = {(item.russian_name, item.api_name) for item in input_field.properties}
    missing = required - actual
    if missing:
        raise ExtractionError(
            f"InputField bilingual examples are missing: {sorted(missing)!r}"
        )


def validate_usual_group_product_vocabulary(
    controls: tuple[ControlCatalogEntry, ...],
) -> None:
    matches = [control for control in controls if control.kind_token == "usual_group"]
    if len(matches) != 1:
        raise ExtractionError("usual_group must have exactly one control catalog entry")
    control = matches[0]
    triplet = (control.public_name, control.api_object, control.russian_name)
    expected = ("UsualGroup", "GroupBox", "РамкаГруппы")
    if triplet != expected:
        raise ExtractionError(
            f"usual_group product/API/Russian triplet drift: {triplet!r}"
        )
    if any(item.public_name == "GroupBox" for item in controls):
        raise ExtractionError(
            "GroupBox must not be emitted as a product public control name"
        )


def build_catalog(palette: Path, help_8_2: Path, help_8_5: Path) -> Catalog:
    controls, command_bar_button = load_palette(palette)
    target_object_names = {control.russian_name for control in controls}
    target_object_names.update(title[0] for title in FALLBACK_OBJECT_TITLES.values())
    target_object_names.update(
        {
            "Форма",
            FORM_CONTROL_EXTENSION_TITLE[0],
            PANEL_CONTROL_EXTENSION_TITLE[0],
        }
    )
    corpus_8_2 = HelpCorpus(help_8_2, target_object_names)
    corpus_8_5 = HelpCorpus(help_8_5, target_object_names)
    control_catalog = build_control_catalog(controls, corpus_8_2, corpus_8_5)
    form_control_extension_properties = build_extension_property_catalog(
        FORM_CONTROL_EXTENSION_TITLE,
        FORM_CONTROL_EXTENSION_API_ORDER,
        corpus_8_2,
        corpus_8_5,
    )
    panel_control_extension_properties = build_extension_property_catalog(
        PANEL_CONTROL_EXTENSION_TITLE,
        PANEL_CONTROL_EXTENSION_API_ORDER,
        corpus_8_2,
        corpus_8_5,
    )
    form_properties, form_events, dynamic_excluded = build_form_catalog(
        corpus_8_2, corpus_8_5
    )
    validate_input_field_examples(control_catalog)
    validate_usual_group_product_vocabulary(control_catalog)
    catalog = Catalog(
        controls=control_catalog,
        form_control_extension_properties=form_control_extension_properties,
        panel_control_extension_properties=panel_control_extension_properties,
        form_properties=form_properties,
        form_events=form_events,
        command_bar_button_properties=len(command_bar_button.properties),
        command_bar_button_events=len(command_bar_button.events),
        dynamic_form_attributes_excluded=dynamic_excluded,
    )
    validate_catalog_counts(catalog)
    return catalog


def validate_catalog_counts(catalog: Catalog) -> None:
    if len(catalog.controls) != EXPECTED_CONTROL_COUNT:
        raise ExtractionError("generated control count drift")
    if (
        sum(len(control.properties) for control in catalog.controls)
        != EXPECTED_PROPERTY_COUNT
    ):
        raise ExtractionError("generated property occurrence count drift")
    if sum(len(control.events) for control in catalog.controls) != EXPECTED_EVENT_COUNT:
        raise ExtractionError("generated event occurrence count drift")
    if (
        len(catalog.form_control_extension_properties)
        != EXPECTED_FORM_CONTROL_EXTENSION_PROPERTY_COUNT
    ):
        raise ExtractionError("generated form-control extension property count drift")
    if (
        len(catalog.panel_control_extension_properties)
        != EXPECTED_PANEL_CONTROL_EXTENSION_PROPERTY_COUNT
    ):
        raise ExtractionError("generated panel-control extension property count drift")
    if len(catalog.form_properties) != EXPECTED_FORM_PROPERTY_COUNT:
        raise ExtractionError("generated Form property count drift")
    if len(catalog.form_events) != EXPECTED_FORM_EVENT_COUNT:
        raise ExtractionError("generated Form event count drift")


def cpp_string(value: str, *, utf8: bool = False) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )
    return ("u8" if utf8 else "") + f'"{escaped}"'


def unknown_counts(catalog: Catalog) -> tuple[int, int]:
    properties = [item for control in catalog.controls for item in control.properties]
    properties.extend(catalog.form_control_extension_properties)
    properties.extend(catalog.panel_control_extension_properties)
    properties.extend(catalog.form_properties)
    return (
        sum(item.access == "unknown" for item in properties),
        sum(item.value_kind == "unknown" for item in properties),
    )


def render_catalog(catalog: Catalog) -> str:
    unknown_access, unknown_value_kind = unknown_counts(catalog)
    lines = [
        "// Generated by tools/platform/extract_ordinary_help_metamodel.py; DO NOT EDIT.",
        "// Provenance: OrdinaryFormPalette.xsd PlatformPalette plus bilingual 1C 8.2/8.5 help.",
        (
            "// Counts: controls=26 properties=417 events=79 "
            "form_control_extension_properties=11 panel_control_extension_properties=9 "
            "form_properties=37 form_events=13 order_base=0."
        ),
        (
            "// Excluded: CommandBarButton properties=15 events=0; "
            "dynamic_form_attributes=1."
        ),
        (
            f"// Resolution: unresolved_english=0 unknown_access={unknown_access} "
            f"unknown_value_kind={unknown_value_kind}."
        ),
    ]
    for control in catalog.controls:
        lines.append(
            "OOF_HELP_CONTROL("
            f"{control.kind_token}, {cpp_string(control.public_name)}, "
            f"{cpp_string(control.api_object)}, {cpp_string(control.russian_name, utf8=True)})"
        )
        for item in control.properties:
            lines.append(
                "OOF_HELP_PROPERTY("
                f"{control.kind_token}, {item.order}, {cpp_string(item.xml_name)}, "
                f"{cpp_string(item.api_name)}, {cpp_string(item.russian_name, utf8=True)}, "
                f"{cpp_string(item.platform_type, utf8=True)}, {item.value_kind}, "
                f"{item.access}, {item.version_mask})"
            )
        for item in control.events:
            lines.append(
                "OOF_HELP_EVENT("
                f"{control.kind_token}, {item.order}, {cpp_string(item.xml_name)}, "
                f"{cpp_string(item.api_name)}, {cpp_string(item.russian_name, utf8=True)}, "
                f"{item.version_mask})"
            )
    for item in catalog.form_control_extension_properties:
        lines.append(
            "OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY("
            f"{item.order}, {cpp_string(item.xml_name)}, {cpp_string(item.api_name)}, "
            f"{cpp_string(item.russian_name, utf8=True)}, "
            f"{cpp_string(item.platform_type, utf8=True)}, {item.value_kind}, "
            f"{item.access}, {item.version_mask})"
        )
    for item in catalog.panel_control_extension_properties:
        lines.append(
            "OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY("
            f"{item.order}, {cpp_string(item.xml_name)}, {cpp_string(item.api_name)}, "
            f"{cpp_string(item.russian_name, utf8=True)}, "
            f"{cpp_string(item.platform_type, utf8=True)}, {item.value_kind}, "
            f"{item.access}, {item.version_mask})"
        )
    for item in catalog.form_properties:
        lines.append(
            "OOF_HELP_FORM_PROPERTY("
            f"{item.order}, {cpp_string(item.xml_name)}, {cpp_string(item.api_name)}, "
            f"{cpp_string(item.russian_name, utf8=True)}, "
            f"{cpp_string(item.platform_type, utf8=True)}, {item.value_kind}, "
            f"{item.access}, {item.version_mask})"
        )
    for item in catalog.form_events:
        lines.append(
            "OOF_HELP_FORM_EVENT("
            f"{item.order}, {cpp_string(item.xml_name)}, {cpp_string(item.api_name)}, "
            f"{cpp_string(item.russian_name, utf8=True)}, {item.version_mask})"
        )
    return "\n".join(lines) + "\n"


def summary(catalog: Catalog) -> str:
    unknown_access, unknown_value_kind = unknown_counts(catalog)
    return (
        f"controls={len(catalog.controls)} "
        f"properties={sum(len(control.properties) for control in catalog.controls)} "
        f"events={sum(len(control.events) for control in catalog.controls)} "
        f"form_control_extension_properties={len(catalog.form_control_extension_properties)} "
        f"panel_control_extension_properties={len(catalog.panel_control_extension_properties)} "
        f"form_properties={len(catalog.form_properties)} "
        f"form_events={len(catalog.form_events)} "
        f"command_bar_button_properties={catalog.command_bar_button_properties} "
        f"command_bar_button_events={catalog.command_bar_button_events} "
        "unresolved_english=0 "
        f"unknown_access={unknown_access} "
        f"unknown_value_kind={unknown_value_kind}"
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--help-8-2",
        type=Path,
        required=True,
        help="unpacked bilingual 1C 8.2 help root",
    )
    parser.add_argument(
        "--help-8-5",
        type=Path,
        required=True,
        help="unpacked bilingual 1C 8.5 help root",
    )
    parser.add_argument(
        "--palette",
        type=Path,
        default=repository / "schemas" / "OrdinaryFormPalette.xsd",
        help="PlatformPalette bootstrap XSD",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repository
        / "sidecars"
        / "onec-form-native"
        / "src"
        / "model"
        / "generated_help_catalog.inc",
        help="generated C++ macro catalog",
    )
    parser.add_argument(
        "--check", action="store_true", help="verify output without writing it"
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        catalog = build_catalog(args.palette, args.help_8_2, args.help_8_5)
        first = render_catalog(catalog)
        second = render_catalog(catalog)
        if first != second:
            raise ExtractionError("nondeterministic catalog rendering detected")
        encoded = first.encode("utf-8")
        if args.check:
            if not args.output.is_file():
                raise ExtractionError("generated catalog is missing")
            if args.output.read_bytes() != encoded:
                raise ExtractionError("generated catalog is stale")
            print(f"CHECK PASS {summary(catalog)}")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(encoded)
            print(f"GENERATED {summary(catalog)}")
        return 0
    except (ExtractionError, ET.ParseError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
