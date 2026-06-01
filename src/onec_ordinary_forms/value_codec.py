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

LOCALIZED_LANG_RE = re.compile(r"^(#|[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*)$")

TYPE_CODE_NAMES = PLATFORM_TYPE_DOMAIN_CODE_NAMES
NUMBER_ALLOWED_SIGN_BY_CODE = {
    "0": "Any",
    "1": "NonNegative",
}
NUMBER_ALLOWED_SIGN_CODE_BY_NAME = {name: code for code, name in NUMBER_ALLOWED_SIGN_BY_CODE.items()}
STRING_ALLOWED_LENGTH_BY_CODE = {
    "0": "Variable",
    "1": "Fixed",
}
STRING_ALLOWED_LENGTH_CODE_BY_NAME = {name: code for code, name in STRING_ALLOWED_LENGTH_BY_CODE.items()}
DATE_PARTS_BY_CODE: dict[str, str] = {}
DATE_PARTS_CODE_BY_NAME: dict[str, str] = {}


@dataclass(frozen=True)
class TypeDomainPatternItem:
    code: str
    type_name: str
    kind: str
    uuid: str = ""
    digits: str = ""
    fraction_digits: str = ""
    allowed_sign: str = ""
    length: str = ""
    allowed_length: str = ""
    date_parts: str = ""


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
        if code == "N" and index + 3 < len(pattern):
            length = clean_atom(pattern[index + 1])
            precision = clean_atom(pattern[index + 2])
            allowed_sign = clean_atom(pattern[index + 3])
            if is_integer_atom(length) and is_integer_atom(precision) and is_integer_atom(allowed_sign):
                result.append(
                    TypeDomainPatternItem(
                        code="N",
                        type_name=TYPE_CODE_NAMES["N"],
                        kind="primitive",
                        digits=length,
                        fraction_digits=precision,
                        allowed_sign=NUMBER_ALLOWED_SIGN_BY_CODE.get(allowed_sign, f"code:{allowed_sign}"),
                    )
                )
                index += 4
                continue
        if code == "S" and index + 2 < len(pattern):
            length = clean_atom(pattern[index + 1])
            allowed_length = clean_atom(pattern[index + 2])
            if is_integer_atom(length) and is_integer_atom(allowed_length):
                result.append(
                    TypeDomainPatternItem(
                        code="S",
                        type_name=TYPE_CODE_NAMES["S"],
                        kind="primitive",
                        length=length,
                        allowed_length=STRING_ALLOWED_LENGTH_BY_CODE.get(allowed_length, f"code:{allowed_length}"),
                    )
                )
                index += 3
                continue
        if code == "D" and index + 1 < len(pattern):
            date_parts = clean_atom(pattern[index + 1])
            if is_integer_atom(date_parts):
                result.append(
                    TypeDomainPatternItem(
                        code="D",
                        type_name=TYPE_CODE_NAMES["D"],
                        kind="primitive",
                        date_parts=DATE_PARTS_BY_CODE.get(date_parts, f"code:{date_parts}"),
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
        elif item.code == "N" and item.digits and item.fraction_digits and item.allowed_sign:
            allowed_sign_code = NUMBER_ALLOWED_SIGN_CODE_BY_NAME.get(item.allowed_sign)
            if allowed_sign_code is None and item.allowed_sign.startswith("code:"):
                allowed_sign_code = item.allowed_sign.removeprefix("code:")
            if allowed_sign_code is None:
                raise ValueError(f"Unsupported number allowed sign: {item.allowed_sign}")
            result.extend([quote_atom("N"), item.digits, item.fraction_digits, allowed_sign_code])
        elif item.code == "S" and item.length and item.allowed_length:
            allowed_length_code = STRING_ALLOWED_LENGTH_CODE_BY_NAME.get(item.allowed_length)
            if allowed_length_code is None and item.allowed_length.startswith("code:"):
                allowed_length_code = item.allowed_length.removeprefix("code:")
            if allowed_length_code is None:
                raise ValueError(f"Unsupported string allowed length: {item.allowed_length}")
            result.extend([quote_atom("S"), item.length, allowed_length_code])
        elif item.code == "D" and item.date_parts:
            date_parts_code = DATE_PARTS_CODE_BY_NAME.get(item.date_parts)
            if date_parts_code is None and item.date_parts.startswith("code:"):
                date_parts_code = item.date_parts.removeprefix("code:")
            if date_parts_code is None:
                raise ValueError(f"Unsupported date parts: {item.date_parts}")
            result.extend([quote_atom("D"), date_parts_code])
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
    item = localized_text_item_from_record(value, preferred_lang=lang)
    return item[1] if item else ""


def localized_text_item_from_record(value: object, *, preferred_lang: str = "ru") -> tuple[str, str] | None:
    if (
        isinstance(value, list)
        and len(value) >= 3
        and clean_atom(value[0]) == "1"
        and clean_atom(value[1]) == "1"
        and isinstance(value[2], list)
        and len(value[2]) >= 2
    ):
        item_lang = clean_atom(value[2][0])
        if not LOCALIZED_LANG_RE.match(item_lang):
            return None
        item_text = clean_atom(value[2][1])
        if item_lang == preferred_lang:
            return item_lang, item_text
        return item_lang, item_text
    return None
