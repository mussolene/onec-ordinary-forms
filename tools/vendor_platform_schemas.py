#!/usr/bin/env python3
"""Vendor extracted platform XSD resources into the package schema tree.

Input is the ``resources.json`` produced by ``extract_platform_xml_resources.py``.
Only schema documents are copied. Files are grouped by platform version and
resource module so the vendored layout stays stable and readable.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path
from xml.etree import ElementTree as ET


def resource_group(source: str) -> str:
    return source.removesuffix(".res").removesuffix(".dll").removesuffix(".exe").replace("_root", "")


def update_configuration_schema(configuration_xsd: Path, platform_version: str, vendored: list[dict[str, object]]) -> None:
    text = configuration_xsd.read_text(encoding="utf-8")
    start = text.index("      <PlatformSchemaResources")
    end = text.index("      </PlatformSchemaResources>", start) + len("      </PlatformSchemaResources>")

    base = configuration_xsd.parent / "platform"
    lines = [
        f'      <PlatformSchemaResources source="1C {platform_version} platform embedded XML/XSD resources" '
        f'version="{platform_version}" count="{len(vendored)}">'
    ]
    for item in vendored:
        path = base / str(item["path"])
        namespace = ET.parse(path).getroot().get("targetNamespace", "")
        lines.append(
            '        <Resource source="{source}" schema="{schema}" namespace="{namespace}" '
            'path="platform/{path}" sha256="{sha256}"/>'.format(
                source=item["source"],
                schema=item["schema"],
                namespace=namespace,
                path=item["path"],
                sha256=item["sha256"],
            )
        )
    lines.append("      </PlatformSchemaResources>")
    configuration_xsd.write_text(text[:start] + "\n".join(lines) + text[end:], encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--resources-json", required=True)
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--platform-version", default="8.2")
    parser.add_argument(
        "--configuration-xsd",
        help="Optional PlatformConfigStructure.xsd to sync PlatformSchemaResources appinfo",
    )
    args = parser.parse_args()

    source_dir = Path(args.source_dir)
    out_root = Path(args.out_dir) / args.platform_version
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True, exist_ok=True)

    report = json.loads(Path(args.resources_json).read_text(encoding="utf-8"))
    vendored: list[dict[str, object]] = []
    for item in report.get("resources", []):
        if not item.get("schema") or not str(item.get("file", "")).endswith(".xsd"):
            continue
        source = str(item["source"])
        group = resource_group(source)
        target = out_root / group / str(item["file"])
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_dir / str(item["file"]), target)
        vendored.append(
            {
                "source": source,
                "schema": str(item["file"]),
                "path": target.relative_to(Path(args.out_dir)).as_posix(),
                "sha256": item.get("sha256", ""),
                "bytes": item.get("bytes", 0),
            }
        )

    (out_root / "schemas.json").write_text(
        json.dumps({"platformVersion": args.platform_version, "count": len(vendored), "schemas": vendored}, ensure_ascii=False, indent=2)
        + "\n",
        encoding="utf-8",
    )
    if args.configuration_xsd:
        update_configuration_schema(Path(args.configuration_xsd), args.platform_version, vendored)
    print(json.dumps({"count": len(vendored), "out_dir": str(out_root)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
