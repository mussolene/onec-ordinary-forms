"""Typed codecs for platform value fragments used by ordinary forms.

The platform exposes these operations through ListInStream/ListOutStream and
ValueToStringInternal/ValueFromStringInternal-like serializers. This module is
the clean-room contract layer for the parts already observed in ordinary form
streams: atoms, CompositeID values, localized strings, and TypeDomainPattern.
"""

from __future__ import annotations

from dataclasses import dataclass
import re

from onec_ordinary_forms.platform_model import PLATFORM_TYPE_DOMAIN_CODE_NAMES

COMPOSITE_ID_RE = re.compile(
    r"^-?[0-9]+(?::[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})?$"
)

TYPE_CODE_NAMES = PLATFORM_TYPE_DOMAIN_CODE_NAMES


@dataclass(frozen=True)
class TypeDomainPatternItem:
    code: str
    type_name: str
    kind: str
    uuid: str = ""


def clean_atom(value: object) -> str:
    text = str(value)
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return text[1:-1].replace('""', '"').replace('\\"', '"').replace("\\\\", "\\")
    return text


def quote_atom(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '""') + '"'


def is_integer_atom(value: str) -> bool:
    try:
        int(value)
        return True
    except ValueError:
        return False


def parse_composite_id(value: object) -> str:
    text = clean_atom(value)
    if not COMPOSITE_ID_RE.match(text):
        raise ValueError(f"Invalid 1C CompositeID: {text}")
    return text


def parse_type_domain_pattern(pattern: list[object] | None, object_types: dict[str, str] | None = None) -> list[TypeDomainPatternItem]:
    if not pattern:
        return []
    object_types = object_types or {}
    result: list[TypeDomainPatternItem] = []
    index = 0
    while index < len(pattern):
        code = clean_atom(pattern[index])
        if code == "#":
            uuid = clean_atom(pattern[index + 1]) if index + 1 < len(pattern) else ""
            result.append(
                TypeDomainPatternItem(
                    code="#",
                    uuid=uuid,
                    type_name=object_types.get(uuid, f"cfg:uuid.{uuid}" if uuid else "cfg:unknown"),
                    kind="reference",
                )
            )
            index += 2
            continue
        result.append(
            TypeDomainPatternItem(
                code=code,
                type_name=TYPE_CODE_NAMES.get(code, f"unknown:{code}"),
                kind="primitive" if code in TYPE_CODE_NAMES else "unknown",
            )
        )
        index += 1
    return result


def dump_type_domain_pattern(items: list[TypeDomainPatternItem]) -> list[object]:
    result: list[object] = []
    for item in items:
        if item.code == "#":
            result.extend([quote_atom("#"), item.uuid])
        elif is_integer_atom(item.code):
            result.append(item.code)
        else:
            result.append(quote_atom(item.code))
    return result


def value_to_string_internal(value: object) -> str:
    if value is None:
        return ""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    return quote_atom(str(value))


def value_from_string_internal(value: object) -> object:
    text = str(value)
    if text == "":
        return None
    if text == "true":
        return True
    if text == "false":
        return False
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return clean_atom(text)
    try:
        return int(text)
    except ValueError:
        pass
    try:
        return float(text)
    except ValueError:
        return text


def localized_text_record(text: str, *, lang: str = "ru") -> list[object]:
    return ["1", "1", [quote_atom(lang), quote_atom(text)]]


def localized_text_from_record(value: object, *, lang: str = "ru") -> str:
    if (
        isinstance(value, list)
        and len(value) >= 3
        and clean_atom(value[0]) == "1"
        and clean_atom(value[1]) == "1"
        and isinstance(value[2], list)
        and len(value[2]) >= 2
        and clean_atom(value[2][0]) == lang
    ):
        return clean_atom(value[2][1])
    return ""
