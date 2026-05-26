"""Platform UI value helpers shared by ordinary form dump/build."""

from __future__ import annotations


ORDINARY_STYLE_COLOR_CODES: dict[str, str] = {
    "TextColor": "-1",
    "BackColor": "-3",
    "ButtonTextColor": "-7",
    "ButtonBackColor": "-21",
    "BorderColor": "-22",
}

ORDINARY_STYLE_COLOR_NAMES: dict[str, str] = {value: key for key, value in ORDINARY_STYLE_COLOR_CODES.items()}


def normalize_style_color_name(value: str) -> str:
    value = value.strip()
    if value.startswith("style:"):
        value = value[len("style:") :]
    return value


def ordinary_color_code_from_style_ref(value: str) -> str | None:
    return ORDINARY_STYLE_COLOR_CODES.get(normalize_style_color_name(value))
