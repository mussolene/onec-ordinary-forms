"""Parse ordinary 1C bracket streams and extract a legacy elem-json index."""

from __future__ import annotations

from pathlib import Path
import json
import re

from onec_ordinary_forms.liststream import ListStreamParseError, parse_list_stream
from onec_ordinary_forms.ordinary_model import parse_ordinary_form_model
from onec_ordinary_forms.ordinary_platform import ordinary_control_type


IDENT_RE = re.compile(r"^[A-Za-zА-Яа-яЁё_][A-Za-zА-Яа-яЁё0-9_]*$")
KNOWN_ITEM_TYPES = {
    "Button",
    "InputField",
    "Table",
    "TableBox",
    "Panel",
    "CommandBar",
    "Image",
    "Picture",
    "Label",
    "CheckBox",
    "RadioButton",
    "TextDocumentField",
    "SpreadsheetDocumentField",
}


BracketParseError = ListStreamParseError


def parse_bracket_text(text: str, *, allow_trailing: bool = False) -> object:
    return parse_list_stream(text, allow_trailing=allow_trailing)


def read_bracket_text(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-8", "cp1251"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def extract_elem_json_from_bracket(text: str) -> dict[str, object]:
    root = parse_bracket_text(text, allow_trailing=True)
    pages = _extract_pages(root)
    props = _extract_props(root)
    model = parse_ordinary_form_model(root)
    items = _items_from_model(model, pages[0] if pages else "Main") if model.controls else _extract_items(root, pages[0] if pages else "Main")
    data: dict[str, object] = {"-pages-": pages}
    for page in pages:
        data[page] = _page_raw(page)
    tree = [item for item in items if item.get("parent") is None]
    for item in items:
        raw_key = str(item.get("rawKey") or f"{item['page']}/{item['name']}")
        data[raw_key] = {
            "id": item["id"],
            "raw": item["raw"],
            "ordinary": item.get("ordinary", {}),
        }
    return {
        "props": props,
        "commands": [],
        "data": data,
        "tree": tree,
    }


def _items_from_model(model: object, default_page: str) -> list[dict[str, object]]:
    items: list[dict[str, object]] = []

    def add(control: object, parent: dict[str, object] | None = None) -> dict[str, object]:
        item = {
            "id": str(getattr(control, "object_id")),
            "name": getattr(control, "name"),
            "type": getattr(control, "type"),
            "page": default_page,
            "rawKey": f"{default_page}/{getattr(control, 'name')}" if parent is None else f"{parent['rawKey']}/{getattr(control, 'name')}",
            "raw": getattr(control, "raw"),
            "child": [],
            "ordinary": {
                "classId": getattr(control, "class_id"),
                "objectId": getattr(control, "object_id"),
                "type": getattr(control, "type"),
                "infoKind": getattr(control, "info_kind"),
                "metadataRecordType": getattr(control, "metadata_record_type"),
                "metadataOwnerId": getattr(control, "metadata_owner_id"),
                "metadataFlag1": getattr(control, "metadata_flag1"),
                "metadataFlag2": getattr(control, "metadata_flag2"),
                "metadataFlag3": getattr(control, "metadata_flag3"),
                "declaredChildCount": str(getattr(control, "declared_child_count")),
                "actualChildCount": str(getattr(control, "actual_child_count")),
                "stateCount": str(getattr(control, "state_count")),
                "stateNames": list(getattr(control, "state_names")),
                "positionRecordCount": str(getattr(control, "position_record_count")),
            },
        }
        if parent is not None:
            item["parent"] = parent.get("name")
            parent.setdefault("child", []).append(item)
        items.append(item)
        for child in getattr(control, "children"):
            add(child, item)
        return item

    for control in getattr(model, "controls"):
        add(control)
    return items


def write_elem_json_from_bracket(form_path: Path, out_path: Path) -> None:
    elem = extract_elem_json_from_bracket(read_bracket_text(form_path))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(elem, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _walk_lists(value: object) -> list[list[object]]:
    result: list[list[object]] = []
    if isinstance(value, list):
        result.append(value)
        for item in value:
            result.extend(_walk_lists(item))
    return result


def _clean(value: object) -> str:
    text = str(value)
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return text[1:-1].replace('\\"', '"')
    return text


def _quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _human_text(value: str) -> bool:
    return any("А" <= char <= "я" or char in "Ёё" for char in value)


def _all_atoms(value: object) -> list[str]:
    if isinstance(value, list):
        atoms: list[str] = []
        for item in value:
            atoms.extend(_all_atoms(item))
        return atoms
    return [str(value)]


def _extract_pages(root: object) -> list[str]:
    pages: list[str] = []
    for node in _walk_lists(root):
        if len(node) >= 3 and str(node[0]) == "1" and str(node[1]) == "1" and isinstance(node[2], list):
            if len(node[2]) >= 2 and _clean(node[2][0]) == "ru":
                title = _clean(node[2][1])
                if title and title not in pages:
                    pages.append(title)
    if pages:
        return pages[:1]
    for atom in _all_atoms(root):
        text = _clean(atom)
        if text and _human_text(text):
            return [text]
    return ["Main"]


def _page_raw(title: str) -> dict[str, object]:
    return {
        "id": "0",
        "raw": [
            "0",
            [
                "0",
                "0",
                [
                    "0",
                    _quote(title),
                ],
            ],
        ],
    }


def _extract_props(root: object) -> list[dict[str, object]]:
    props: list[dict[str, object]] = []
    seen: set[str] = set()
    next_id = 1
    for node in _walk_lists(root):
        for index, item in enumerate(node):
            nested_pattern = isinstance(item, list) and item and item[0] == '"Pattern"'
            if item != '"Pattern"' and not nested_pattern:
                continue
            pattern = _normalize_pattern(item[1:] if nested_pattern else (node[index + 1] if index + 1 < len(node) else []))
            name = _nearby_identifier(node, index) or f"Attribute{next_id}"
            if name in seen:
                continue
            seen.add(name)
            props.append({"name": name, "id": str(next_id), "raw": ['"Pattern"', pattern]})
            next_id += 1
    return props


def _normalize_pattern(value: object) -> list[object]:
    if not isinstance(value, list):
        return [value]
    if len(value) == 1 and isinstance(value[0], list):
        return value[0]
    return value


def _nearby_identifier(node: list[object], index: int) -> str:
    candidates = list(node[max(0, index - 4) : index]) + list(node[index + 1 : index + 5])
    for value in candidates:
        if isinstance(value, list):
            continue
        text = _clean(value)
        if IDENT_RE.fullmatch(text) and text not in {"Pattern", "ru"}:
            return text
    return ""


def _extract_items(root: object, default_page: str) -> list[dict[str, object]]:
    items: list[dict[str, object]] = []
    seen_names: set[str] = set()
    next_id = 1
    for node in _walk_lists(root):
        geometry_index = _geometry_index(node)
        if geometry_index is None:
            continue
        name = _element_name(node, len(items) + 1)
        if name in seen_names:
            continue
        seen_names.add(name)
        items.append(
            {
                "id": str(next_id),
                "name": name,
                "type": _element_type(node),
                "page": default_page,
                "raw": _normalize_item_raw(node, geometry_index),
            }
        )
        next_id += 1
    return items


def _geometry_index(node: list[object]) -> int | None:
    for index, item in enumerate(node):
        if _looks_like_geometry(item):
            return index
    return None


def _looks_like_geometry(value: object) -> bool:
    if not isinstance(value, list) or len(value) < 5:
        return False
    try:
        int(value[1])
        int(value[2])
        int(value[3])
        int(value[4])
    except (TypeError, ValueError):
        return False
    return True


def _element_name(node: list[object], fallback_index: int) -> str:
    metadata_name = _metadata_name(node)
    if metadata_name:
        return metadata_name
    for atom in _all_atoms(node):
        text = _clean(atom)
        if IDENT_RE.fullmatch(text) and text not in KNOWN_ITEM_TYPES and text not in {"ru", "Pattern"}:
            return text
    return f"Item{fallback_index}"


def _element_type(node: list[object]) -> str:
    platform_type = ordinary_control_type(node[0] if node else "")
    if platform_type:
        return platform_type
    atoms = {_clean(atom) for atom in _all_atoms(node)}
    metadata_name = _metadata_name(node).lower()
    if any(atom.startswith("#base64:") for atom in atoms) and (
        "картин" in metadata_name or "image" in metadata_name or "picture" in metadata_name
    ):
        return "Image"
    for item_type in KNOWN_ITEM_TYPES:
        if item_type in atoms:
            return item_type
    return "Item"


def _metadata_name(node: list[object]) -> str:
    for child in _walk_lists(node):
        if len(child) < 2 or str(child[0]) != "14":
            continue
        name = _clean(child[1])
        if IDENT_RE.fullmatch(name):
            return name
    return ""


def _normalize_item_raw(node: list[object], geometry_index: int) -> list[object]:
    if geometry_index == 3:
        return node
    result = ["0", "0", "0", node[geometry_index]]
    for item in node:
        if item is not node[geometry_index]:
            result.append(item)
    return result
