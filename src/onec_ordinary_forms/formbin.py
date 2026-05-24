"""Split and join ordinary-form ``Form.bin`` section streams."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path
import json
import re
from struct import pack, unpack
from typing import Mapping


MARKER_RE = re.compile(rb"(?:(?<=\n)|^)([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) \r?\n")
CONTAINER_INFO_NAME = "Form.bin.container.json"
CONTAINER_END_MARKER = 0x7FFFFFFF
CONTAINER_BLOCK_SIZE = 0x200
CONTAINER_HEADER_SIZE = 16
CONTAINER_BLOCK_HEADER_SIZE = 31
CONTAINER_TOC_BLOCK_SIZE = 0x200
CONTAINER_DOCUMENT_BLOCK_SIZE = 0xA000
DEFAULT_CONTAINER_TIME = datetime(2000, 1, 1)


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


@dataclass(frozen=True)
class FormBinFileDescriptor:
    name: str
    created: int
    modified: int
    section_index: int


@dataclass(frozen=True)
class OneCContainerFile:
    name: str
    created: int
    modified: int
    payload: bytes


@dataclass(frozen=True)
class OneCContainer:
    block_size: int
    files: list[OneCContainerFile]


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


def _read_block_header(data: bytes, offset: int) -> tuple[int, int, int, int]:
    if data[offset : offset + 2] != b"\r\n":
        raise ValueError(f"Invalid 1C container block header at offset {offset}: missing CRLF prefix")
    header_end = data.find(b"\r\n", offset + 2)
    if header_end < 0:
        raise ValueError(f"Invalid 1C container block header at offset {offset}: missing CRLF suffix")
    header = data[offset + 2 : header_end].decode("ascii")
    parts = header.split()
    if len(parts) != 3:
        raise ValueError(f"Invalid 1C container block header at offset {offset}: {header!r}")
    doc_size, current_size, next_offset = (int(part, 16) for part in parts)
    return doc_size, current_size, next_offset, header_end + 2


def _read_document(data: bytes, offset: int) -> bytes:
    chunks: list[bytes] = []
    total_size: int | None = None
    current_offset = offset
    while True:
        doc_size, current_size, next_offset, payload_offset = _read_block_header(data, current_offset)
        if total_size is None:
            total_size = doc_size
        payload_end = payload_offset + current_size
        if payload_end > len(data):
            raise ValueError(f"Invalid 1C container block at offset {current_offset}: payload exceeds file size")
        chunks.append(data[payload_offset:payload_end])
        if next_offset == CONTAINER_END_MARKER:
            break
        current_offset = next_offset
    payload = b"".join(chunks)
    return payload[:total_size]


def parse_form_bin_container(data: bytes) -> OneCContainer:
    if len(data) < CONTAINER_HEADER_SIZE:
        raise ValueError("Invalid Form.bin container: too small")
    end_marker, block_size, count_files, reserved = unpack("<4i", data[:CONTAINER_HEADER_SIZE])
    if end_marker != CONTAINER_END_MARKER or block_size <= 0 or count_files < 0 or reserved != 0:
        raise ValueError("Invalid Form.bin container header")

    toc = _read_document(data, CONTAINER_HEADER_SIZE)
    files: list[OneCContainerFile] = []
    for index in range(count_files):
        entry_offset = index * 12
        if entry_offset + 12 > len(toc):
            raise ValueError("Invalid Form.bin container TOC: truncated entry")
        descriptor_offset, data_offset, entry_marker = unpack("<3i", toc[entry_offset : entry_offset + 12])
        if entry_marker != CONTAINER_END_MARKER:
            raise ValueError("Invalid Form.bin container TOC: bad entry marker")
        descriptor = _read_document(data, descriptor_offset)
        if len(descriptor) < 24:
            raise ValueError("Invalid Form.bin file descriptor: too small")
        created, modified, flags = unpack("<QQi", descriptor[:20])
        if flags != 0:
            raise ValueError("Invalid Form.bin file descriptor: unsupported flags")
        name = descriptor[20:].decode("utf-16le").partition("\x00")[0]
        files.append(
            OneCContainerFile(
                name=name,
                created=created,
                modified=modified,
                payload=_read_document(data, data_offset),
            )
        )
    return OneCContainer(block_size=block_size, files=files)


def datetime_to_container_ticks(value: datetime) -> int:
    return int((value - datetime(1, 1, 1)) // timedelta(microseconds=100))


def container_ticks_to_datetime(value: int) -> datetime:
    return datetime(1, 1, 1) + timedelta(microseconds=value * 100)


def _default_container_ticks() -> int:
    return datetime_to_container_ticks(DEFAULT_CONTAINER_TIME)


def _parse_file_descriptor(section: FormBinSection, index: int) -> FormBinFileDescriptor | None:
    if len(section.payload) < 24:
        return None
    try:
        created, modified, flags = unpack("<QQi", section.payload[:20])
        name = section.payload[20:].decode("utf-16le").partition("\x00")[0]
    except (UnicodeDecodeError, ValueError):
        return None
    if flags != 0 or name not in {"form", "module"}:
        return None
    return FormBinFileDescriptor(name=name, created=created, modified=modified, section_index=index)


def file_descriptors(parts: FormBinParts) -> dict[str, FormBinFileDescriptor]:
    result: dict[str, FormBinFileDescriptor] = {}
    for index, section in enumerate(parts.sections):
        descriptor = _parse_file_descriptor(section, index)
        if descriptor is not None:
            result[descriptor.name] = descriptor
    return result


def logical_streams(parts: FormBinParts) -> dict[str, bytes]:
    container = parse_form_bin_container(serialize_parts(parts))
    by_name = {file.name: file.payload for file in container.files}
    streams: dict[str, bytes] = {}
    if "module" in by_name:
        streams["Module.bsl"] = by_name["module"]
    if "form" in by_name:
        streams["Form.xml"] = by_name["form"]
    return streams


def serialize_parts(parts: FormBinParts) -> bytes:
    data = bytearray(parts.prefix)
    for index, section in enumerate(parts.sections):
        data.extend(f"{section.first_size:08x} {section.second_size:08x} {section.limit} \r\n".encode("ascii"))
        data.extend(section.payload)
        if index + 1 < len(parts.sections):
            data.extend(b"\r\n")
    return bytes(data)


def _block_header(doc_size: int, current_block_size: int, next_block_offset: int = CONTAINER_END_MARKER) -> bytes:
    return f"\r\n{doc_size:08x} {current_block_size:08x} {next_block_offset:08x} \r\n".encode("ascii")


def _append_block(
    data: bytearray,
    payload: bytes,
    *,
    doc_size: int | None = None,
    min_block_size: int = 0,
    next_block_offset: int = CONTAINER_END_MARKER,
) -> int:
    offset = len(data)
    size = len(payload) if doc_size is None else doc_size
    current_size = max(min_block_size, len(payload))
    data.extend(_block_header(size, current_size, next_block_offset))
    data.extend(payload)
    if current_size > len(payload):
        data.extend(b"\x00" * (current_size - len(payload)))
    return offset


def _append_document(data: bytearray, payload: bytes, *, min_block_size: int = 0) -> int:
    if len(payload) <= CONTAINER_DOCUMENT_BLOCK_SIZE:
        return _append_block(data, payload, min_block_size=min_block_size)

    chunks = [payload[index : index + CONTAINER_DOCUMENT_BLOCK_SIZE] for index in range(0, len(payload), CONTAINER_DOCUMENT_BLOCK_SIZE)]
    first_offset = len(data)
    header_size = len(_block_header(0, 0))
    offsets: list[int] = []
    current_offset = first_offset
    for index, chunk in enumerate(chunks):
        offsets.append(current_offset)
        current_size = max(min_block_size, len(chunk)) if index + 1 == len(chunks) else len(chunk)
        current_offset += header_size + current_size

    for index, chunk in enumerate(chunks):
        next_offset = offsets[index + 1] if index + 1 < len(offsets) else CONTAINER_END_MARKER
        doc_size = len(payload) if index == 0 else 0
        current_size = max(min_block_size, len(chunk)) if index + 1 == len(chunks) else len(chunk)
        _append_block(data, chunk, doc_size=doc_size, min_block_size=current_size, next_block_offset=next_offset)
    return first_offset


def _file_descriptor_payload(name: str, created: int, modified: int) -> bytes:
    return pack("<QQi", created, modified, 0) + name.encode("utf-16le") + b"\x00" * 4


def _container_file_ticks(
    name: str,
    *,
    file_times: Mapping[str, tuple[int | None, int | None]] | None = None,
    created: int | None = None,
    modified: int | None = None,
) -> tuple[int, int]:
    file_created, file_modified = (file_times or {}).get(name, (created, modified))
    created_ticks = _default_container_ticks() if file_created is None else file_created
    modified_ticks = created_ticks if file_modified is None else file_modified
    return created_ticks, modified_ticks


def build_form_bin_container(
    form_payload: bytes,
    module_payload: bytes,
    *,
    created: int | None = None,
    modified: int | None = None,
    file_times: Mapping[str, tuple[int | None, int | None]] | None = None,
    toc_padding: bytes | None = None,
) -> bytes:
    """Build ordinary-form ``Form.bin`` as a platform-style 32-bit 1C container.

    The container stores two files named ``form`` and ``module``. File
    descriptors use the standard 1C layout: created ticks, modified ticks,
    zero flags, UTF-16LE file name, and a UTF-16 terminator.
    """

    data = bytearray(pack("<4i", CONTAINER_END_MARKER, CONTAINER_BLOCK_SIZE, 2, 0))

    toc_offset = len(data)
    data.extend(b"\x00" * (CONTAINER_BLOCK_HEADER_SIZE + CONTAINER_TOC_BLOCK_SIZE))

    form_created, form_modified = _container_file_ticks("form", file_times=file_times, created=created, modified=modified)
    module_created, module_modified = _container_file_ticks("module", file_times=file_times, created=created, modified=modified)

    form_descriptor_offset = _append_document(data, _file_descriptor_payload("form", form_created, form_modified))
    module_descriptor_offset = _append_document(data, _file_descriptor_payload("module", module_created, module_modified))
    module_payload_offset = _append_block(data, module_payload, min_block_size=CONTAINER_BLOCK_SIZE)
    form_payload_offset = _append_block(data, form_payload, min_block_size=CONTAINER_BLOCK_SIZE)

    entries = (
        (form_descriptor_offset, form_payload_offset),
        (module_descriptor_offset, module_payload_offset),
    )

    toc_payload = b"".join(pack("<3i", descriptor_offset, payload_offset, CONTAINER_END_MARKER) for descriptor_offset, payload_offset in entries)
    toc_block = _block_header(len(toc_payload), CONTAINER_TOC_BLOCK_SIZE) + toc_payload
    padding_size = CONTAINER_BLOCK_HEADER_SIZE + CONTAINER_TOC_BLOCK_SIZE - len(toc_block)
    padding = (toc_padding or b"")[:padding_size]
    toc_block += padding
    toc_block += b"\x00" * (padding_size - len(padding))
    data[toc_offset : toc_offset + len(toc_block)] = toc_block
    return bytes(data)


def _toc_padding(data: bytes) -> bytes:
    doc_size, current_size, _next_offset, payload_offset = _read_block_header(data, CONTAINER_HEADER_SIZE)
    if current_size <= doc_size:
        return b""
    return data[payload_offset + doc_size : payload_offset + current_size]


def _metadata_toc_padding(metadata: dict[str, object]) -> bytes | None:
    try:
        return bytes.fromhex(str(metadata.get("tocPaddingHex", "")))
    except ValueError:
        return None


def unpack_form_bin(form_bin: Path, out_dir: Path) -> dict[str, object]:
    source_data = form_bin.read_bytes()
    container = parse_form_bin_container(source_data)
    out_dir.mkdir(parents=True, exist_ok=True)

    files = {file.name: file for file in container.files}
    if "form" not in files or "module" not in files:
        raise ValueError("Ordinary Form.bin container must contain form and module files")

    (out_dir / "Form.xml").write_bytes(files["form"].payload)
    (out_dir / "Module.bsl").write_bytes(files["module"].payload)

    metadata = {
        "format": "onec-ordinary-formbin-container",
        "version": 1,
        "blockSize": container.block_size,
        "tocPaddingHex": _toc_padding(source_data).hex(),
        "files": [
            {
                "name": file.name,
                "createdTicks": file.created,
                "modifiedTicks": file.modified,
                "size": len(file.payload),
            }
            for file in container.files
        ],
    }
    (out_dir / CONTAINER_INFO_NAME).write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return metadata


def pack_form_bin(parts_dir: Path, out_form_bin: Path) -> None:
    info_path = parts_dir / CONTAINER_INFO_NAME
    metadata = json.loads(info_path.read_text(encoding="utf-8")) if info_path.exists() else {}
    times = {
        str(file.get("name")): (int(file["createdTicks"]), int(file["modifiedTicks"]))
        for file in metadata.get("files", [])
        if isinstance(file, dict) and file.get("createdTicks") is not None and file.get("modifiedTicks") is not None
    }
    created, modified = times.get("form", (None, None))
    data = build_form_bin_container(
        (parts_dir / "Form.xml").read_bytes(),
        (parts_dir / "Module.bsl").read_bytes(),
        created=created,
        modified=modified,
        file_times=times,
        toc_padding=_metadata_toc_padding(metadata),
    )
    out_form_bin.parent.mkdir(parents=True, exist_ok=True)
    out_form_bin.write_bytes(data)
