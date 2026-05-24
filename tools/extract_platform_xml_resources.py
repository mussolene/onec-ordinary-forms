#!/usr/bin/env python3
"""Extract embedded XML/XSD resources from local 1C platform binaries.

Platform DLL/RES/HBK files contain XML schemas and metadata descriptions as
plain embedded XML fragments. This helper extracts those fragments into an
ignored work directory so schema work can cite platform resources without
committing platform binaries or generated private outputs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path


XML_START_RE = re.compile(br"(?:\xef\xbb\xbf)?<\?xml\b")
END_TAGS = (
    b"</xs:schema>",
    b"</schema>",
    b"</model>",
    b"</package>",
    b"</AppearanceTemplate>",
)


def decode_xml(payload: bytes) -> str:
    for encoding in ("utf-8-sig", "utf-8", "utf-16le"):
        try:
            return payload.decode(encoding)
        except UnicodeDecodeError:
            continue
    return payload.decode("utf-8", errors="replace")


def embedded_xml_fragments(data: bytes) -> list[tuple[int, bytes]]:
    result: list[tuple[int, bytes]] = []
    for match in XML_START_RE.finditer(data):
        start = match.start()
        best_end: int | None = None
        for tag in END_TAGS:
            end = data.find(tag, match.end())
            if end >= 0:
                candidate = end + len(tag)
                if best_end is None or candidate < best_end:
                    best_end = candidate
        if best_end is None:
            continue
        result.append((start, data[start:best_end]))
    return result


def safe_stem(path: Path, index: int, text: str) -> str:
    namespace_match = re.search(r'targetNamespace="([^"]+)"', text)
    if namespace_match:
        suffix = re.sub(r"[^A-Za-z0-9_.-]+", "_", namespace_match.group(1)).strip("_")
        return f"{path.stem}-{index:02d}-{suffix[:80]}"
    return f"{path.stem}-{index:02d}"


def is_schema_document(text: str) -> bool:
    try:
        root = ET.fromstring(text)
    except ET.ParseError:
        return False
    return root.tag.rsplit("}", 1)[-1] == "schema"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, help="1C platform bin/resource directory")
    parser.add_argument("--out-dir", required=True, help="Ignored output directory")
    parser.add_argument("--schemas-only", action="store_true", help="Write only fragments containing xs:schema")
    args = parser.parse_args()

    root = Path(args.root)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    report: list[dict[str, object]] = []
    for path in sorted(root.iterdir()):
        if not path.is_file() or path.suffix.lower() not in {".dll", ".exe", ".res", ".hbk"}:
            continue
        try:
            data = path.read_bytes()
        except OSError:
            continue
        for index, (offset, payload) in enumerate(embedded_xml_fragments(data), start=1):
            text = decode_xml(payload)
            if args.schemas_only and not is_schema_document(text):
                continue
            stem = safe_stem(path, index, text)
            extension = ".xsd" if is_schema_document(text) else ".xml"
            target = out_dir / f"{stem}{extension}"
            target.write_text(text + "\n", encoding="utf-8")
            report.append(
                {
                    "source": path.name,
                    "offset": offset,
                    "file": target.name,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "bytes": len(payload),
                    "schema": is_schema_document(text),
                }
            )

    (out_dir / "resources.json").write_text(
        json.dumps({"root": str(root), "count": len(report), "resources": report}, ensure_ascii=False, indent=2)
        + "\n",
        encoding="utf-8",
    )
    print(json.dumps({"count": len(report), "out_dir": str(out_dir)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
