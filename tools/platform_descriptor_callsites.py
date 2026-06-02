#!/usr/bin/env python3
"""Extract descriptor callsite facts from a platform ELF.

The tool is intentionally narrow: it uses the platform binary as the only
source, finds padded function boundaries around requested VMAs, disassembles
those functions with objdump, and decodes GUID operands that point into
.rodata. Generated reports belong in ignored work/ or scan-output/.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ADDR_RE = re.compile(r"^\s*([0-9a-fA-F]+):")
COMMENT_ADDR_RE = re.compile(r"#\s*0x([0-9a-fA-F]+)")
INDIRECT_CALL_RE = re.compile(r"\bcallq?\s+\*([^#]+)")


@dataclass(frozen=True)
class Section:
    name: str
    vma: int
    size: int
    file_offset: int


def run(args: list[str]) -> str:
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def parse_int(value: str) -> int:
    return int(value, 16) if value.lower().startswith("0x") else int(value, 16)


def load_sections(path: Path) -> list[Section]:
    blob = path.read_bytes()
    if blob[:4] != b"\x7fELF" or blob[4] != 2 or blob[5] != 1:
        raise ValueError("expected little-endian ELF64")

    e_shoff = struct.unpack_from("<Q", blob, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", blob, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", blob, 0x3C)[0]
    e_shstrndx = struct.unpack_from("<H", blob, 0x3E)[0]
    shstr = section_header(blob, e_shoff, e_shentsize, e_shstrndx)
    names = blob[shstr["offset"] : shstr["offset"] + shstr["size"]]

    sections: list[Section] = []
    for idx in range(e_shnum):
        header = section_header(blob, e_shoff, e_shentsize, idx)
        name = c_string(names, header["name"])
        if not name:
            continue
        sections.append(
            Section(
                name=name,
                vma=header["addr"],
                size=header["size"],
                file_offset=header["offset"],
            )
        )
    return sections


def section_header(blob: bytes, shoff: int, shentsize: int, index: int) -> dict:
    at = shoff + index * shentsize
    name, _type = struct.unpack_from("<II", blob, at)
    flags, addr, offset, size = struct.unpack_from("<QQQQ", blob, at + 0x08)
    return {"name": name, "type": _type, "flags": flags, "addr": addr, "offset": offset, "size": size}


def c_string(blob: bytes, offset: int) -> str:
    end = blob.find(b"\0", offset)
    if end < 0:
        end = len(blob)
    return blob[offset:end].decode("utf-8", errors="replace")


def section_for(sections: Iterable[Section], address: int) -> Section | None:
    for section in sections:
        if section.vma <= address < section.vma + section.size:
            return section
    return None


def file_offset(sections: Iterable[Section], address: int) -> int | None:
    section = section_for(sections, address)
    if section is None:
        return None
    return section.file_offset + address - section.vma


def find_function_start(blob: bytes, sections: list[Section], address: int) -> int:
    offset = file_offset(sections, address)
    section = section_for(sections, address)
    if offset is None or section is None:
        return address

    run_high: int | None = None
    cc_run = 0
    section_start = section.file_offset
    for cursor in range(offset - 1, max(section_start, offset - 0x3000), -1):
        if blob[cursor] == 0xCC:
            if cc_run == 0:
                run_high = cursor
            cc_run += 1
            continue
        if cc_run >= 4 and run_high is not None:
            return section.vma + (run_high + 1 - section.file_offset)
        cc_run = 0
        run_high = None
    return address


def find_function_end(blob: bytes, sections: list[Section], start: int) -> int:
    offset = file_offset(sections, start)
    section = section_for(sections, start)
    if offset is None or section is None:
        return start + 0x700
    section_end = section.file_offset + section.size
    cc_run = 0
    for cursor in range(offset + 1, min(section_end, offset + 0x5000)):
        if blob[cursor] == 0xCC:
            cc_run += 1
            if cc_run >= 4:
                end_offset = cursor - cc_run + 1
                return section.vma + (end_offset - section.file_offset)
        else:
            cc_run = 0
    return min(section.vma + section.size, start + 0x700)


def decode_guid(blob: bytes, sections: list[Section], address: int) -> str | None:
    offset = file_offset(sections, address)
    if offset is None or offset + 16 > len(blob):
        return None
    raw = blob[offset : offset + 16]
    d1, d2, d3 = struct.unpack_from("<IHH", raw)
    d4 = raw[8:]
    return (
        f"{d1:08x}-{d2:04x}-{d3:04x}-"
        f"{d4[0]:02x}{d4[1]:02x}-{d4[2:].hex()}"
    )


def disassemble(path: Path, start: int, end: int) -> list[str]:
    text = run(
        [
            "objdump",
            "-d",
            f"--start-address=0x{start:x}",
            f"--stop-address=0x{end:x}",
            str(path),
        ]
    )
    return [line.rstrip() for line in text.splitlines()]


def instruction_address(line: str) -> int | None:
    match = ADDR_RE.match(line)
    return int(match.group(1), 16) if match else None


def extract_function(path: Path, blob: bytes, sections: list[Section], callsite: int) -> dict:
    start = find_function_start(blob, sections, callsite)
    end = find_function_end(blob, sections, start)
    lines = disassemble(path, start, end)
    guid_refs = []
    indirect_calls = []
    for line in lines:
        at = instruction_address(line)
        for match in COMMENT_ADDR_RE.finditer(line):
            ref = int(match.group(1), 16)
            guid = decode_guid(blob, sections, ref)
            if guid is not None:
                guid_refs.append({"at": f"0x{at:x}" if at is not None else "", "ref": f"0x{ref:x}", "guid": guid})
        call = INDIRECT_CALL_RE.search(line)
        if call:
            indirect_calls.append({"at": f"0x{at:x}" if at is not None else "", "target": call.group(1).strip()})

    return {
        "callsite": f"0x{callsite:x}",
        "function_start": f"0x{start:x}",
        "function_end": f"0x{end:x}",
        "guid_refs": guid_refs,
        "indirect_calls": indirect_calls,
        "disassembly": lines,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("platform_elf", type=Path)
    parser.add_argument("callsites", nargs="+")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    blob = args.platform_elf.read_bytes()
    sections = load_sections(args.platform_elf)
    result = {
        "platform_elf": args.platform_elf.name,
        "callsites": [
            extract_function(args.platform_elf, blob, sections, parse_int(callsite))
            for callsite in args.callsites
        ],
    }

    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
