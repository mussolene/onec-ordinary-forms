"""Split and join ordinary-form ``Form.bin`` section streams."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import base64
import json
import re


MARKER_RE = re.compile(rb"(?:(?<=\n)|^)([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) \r?\n")
MANIFEST_NAME = "Form.bin.parts.json"


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
    if index == 3:
        return "Module.bsl"
    if index == 4:
        return "Form.xml"
    return f"section-{index}.bin"


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
                "firstSize": section.first_size,
                "secondSize": section.second_size,
                "limit": section.limit,
                "size": len(section.payload),
            }
        )

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
