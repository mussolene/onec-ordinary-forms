"""Split and join ordinary-form ``Form.bin`` section streams."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import base64
import json
import re


MARKER_RE = re.compile(rb"(?:(?<=\n)|^)([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) \r?\n")
MANIFEST_NAME = "Form.bin.parts.json"
BOM = b"\xef\xbb\xbf"


@dataclass(frozen=True)
class FormBinSection:
    marker_start: int
    marker_end: int
    first_size: int
    second_size: int
    limit: str
    payload: bytes


@dataclass(frozen=True)
class FormBinParts:
    prefix: bytes
    sections: list[FormBinSection]


def parse_form_bin(data: bytes) -> FormBinParts:
    matches = list(MARKER_RE.finditer(data))
    if len(matches) < 5:
        raise ValueError(f"Unsupported Form.bin layout: expected at least 5 sections, got {len(matches)}")

    sections: list[FormBinSection] = []
    for index, match in enumerate(matches):
        payload_start = match.end()
        first_size = int(match.group(1), 16)
        second_size = int(match.group(2), 16)
        payload_end = payload_start + second_size
        next_marker_start = matches[index + 1].start() if index + 1 < len(matches) else len(data)
        if payload_end > next_marker_start:
            raise ValueError(f"Unsupported Form.bin layout: section {index} overlaps the next marker")
        sections.append(
            FormBinSection(
                marker_start=match.start(),
                marker_end=match.end(),
                first_size=first_size,
                second_size=second_size,
                limit=match.group(3).decode("ascii"),
                payload=data[payload_start:payload_end],
            )
        )

    return FormBinParts(prefix=data[: matches[0].start()], sections=sections)


def _section_payload_file(index: int) -> str:
    return f"section-{index}.bin"


def _descriptor_name(payload: bytes) -> str:
    if b"f\x00o\x00r\x00m\x00" in payload:
        return "form"
    if b"m\x00o\x00d\x00u\x00l\x00e\x00" in payload:
        return "module"
    return ""


def _decode_text_payload(payload: bytes) -> str | None:
    for encoding in ("utf-8-sig", "utf-8", "cp1251"):
        try:
            return payload.decode(encoding)
        except UnicodeDecodeError:
            continue
    return None


def _looks_textual(payload: bytes) -> bool:
    text = _decode_text_payload(payload)
    if text is None:
        return False
    meaningful = [char for char in text if char not in "\ufeff\r\n\t "]
    if not meaningful:
        return False
    printable = sum(1 for char in meaningful if char.isprintable())
    return printable / len(meaningful) > 0.8


def _brace_balance(payload: bytes) -> int:
    text = _decode_text_payload(payload) or ""
    balance = 0
    in_string = False
    escaped = False
    for char in text:
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            balance += 1
        elif char == "}":
            balance -= 1
    return balance


def _first_bracket_payload_index(sections: list[FormBinSection], start: int = 0) -> int | None:
    for index, section in enumerate(sections[start:], start=start):
        if _descriptor_name(section.payload):
            continue
        text = _decode_text_payload(section.payload)
        if text is not None and text.lstrip("\ufeff\r\n\t ").startswith("{"):
            return index
    return None


def logical_streams(parts: FormBinParts) -> dict[str, bytes]:
    descriptors = {_descriptor_name(section.payload): index for index, section in enumerate(parts.sections)}
    module_index = descriptors.get("module")
    form_index = descriptors.get("form")

    module_payload_index = module_index + 1 if module_index is not None and module_index + 1 < len(parts.sections) else 3
    form_payload_indexes: list[int] = []
    first_form = _first_bracket_payload_index(parts.sections, (form_index + 1) if form_index is not None else 0)
    if first_form is not None:
        form_payload_indexes.append(first_form)
    elif len(parts.sections) > 4:
        form_payload_indexes.append(4)

    if form_payload_indexes:
        balance = sum(_brace_balance(parts.sections[index].payload) for index in form_payload_indexes)
        for index, section in enumerate(parts.sections):
            if index <= max(form_payload_indexes):
                continue
            if index in form_payload_indexes or index == module_payload_index:
                continue
            if _descriptor_name(section.payload) or not _looks_textual(section.payload):
                continue
            if balance > 0 or not section.payload.lstrip().startswith(BOM):
                form_payload_indexes.append(index)
                balance += _brace_balance(section.payload)
                if balance <= 0:
                    break

    streams: dict[str, bytes] = {}
    if module_payload_index < len(parts.sections):
        streams["Module.bsl"] = parts.sections[module_payload_index].payload
    if form_payload_indexes:
        streams["Form.xml"] = b"".join(parts.sections[index].payload for index in form_payload_indexes)
    return streams


def unpack_form_bin(form_bin: Path, out_dir: Path) -> dict[str, object]:
    parts = parse_form_bin(form_bin.read_bytes())
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest_sections: list[dict[str, object]] = []
    for index, section in enumerate(parts.sections):
        payload_file = _section_payload_file(index)
        (out_dir / payload_file).write_bytes(section.payload)
        manifest_sections.append(
            {
                "file": payload_file,
                "role": _descriptor_name(section.payload) or "payload",
                "firstSize": section.first_size,
                "secondSize": section.second_size,
                "limit": section.limit,
                "size": len(section.payload),
            }
        )

    for logical_file, payload in logical_streams(parts).items():
        (out_dir / logical_file).write_bytes(payload)

    manifest = {
        "format": "onec-ordinary-formbin-parts",
        "version": 1,
        "prefix": base64.b64encode(parts.prefix).decode("ascii"),
        "sections": manifest_sections,
    }
    (out_dir / MANIFEST_NAME).write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


def pack_form_bin(parts_dir: Path, out_form_bin: Path) -> None:
    manifest = json.loads((parts_dir / MANIFEST_NAME).read_text(encoding="utf-8"))
    if manifest.get("format") != "onec-ordinary-formbin-parts":
        raise ValueError(f"Unsupported manifest format: {manifest.get('format')}")

    data = bytearray(base64.b64decode(manifest["prefix"]))
    sections = manifest["sections"]
    for index, section in enumerate(sections):
        payload = (parts_dir / section["file"]).read_bytes()
        size = len(payload)
        limit = str(section["limit"])
        original_size = int(section.get("size", size))
        original_first_size = int(section.get("firstSize", size))
        original_second_size = int(section.get("secondSize", size))
        first_size = size if original_first_size == original_size else original_first_size
        second_size = size if original_second_size == original_size else original_second_size
        data.extend(f"{first_size:08x} {second_size:08x} {limit} \r\n".encode("ascii"))
        data.extend(payload)
        if index + 1 < len(sections):
            data.extend(b"\r\n")

    out_form_bin.parent.mkdir(parents=True, exist_ok=True)
    out_form_bin.write_bytes(bytes(data))
