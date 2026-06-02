#!/usr/bin/env python3
"""Summarize platform ordinary-form mechanism evidence from local artifacts.

The input artifacts are generated under ignored work/ or scan-output/ paths by
the Ghidra helper scripts. This tool intentionally emits only a compact,
sanitized mechanism map: function names, addresses, call references, and import
presence. It does not copy decompiled platform bodies into tracked files.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any


MECHANISM_SYMBOLS = (
    "cf_form_controls8",
    "cf_form_controls_position8",
    "cf_form_controls_info8",
    "ListOutStream",
    "ListInStream",
    "TypeDomainPattern",
    "CompositeID",
    "GenericValue",
    "PersistenceStorage",
    "LocalWString",
    "FormattedString",
    "Color",
    "Font",
    "V8Border",
    "V8Picture",
)


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def nm_symbols(path: Path) -> list[str]:
    try:
        output = subprocess.check_output(
            ["nm", "-D", "--demangle", str(path)],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return []
    return [line for line in output.splitlines() if any(symbol in line for symbol in MECHANISM_SYMBOLS)]


def calls_from_body(body: str) -> list[str]:
    calls = sorted(set(re.findall(r"([A-Za-z_][A-Za-z0-9_:~<>]*|FUN_[0-9a-fA-F]+)\s*\(", body)))
    ignored = {"if", "while", "for", "switch", "return", "catch"}
    return [call for call in calls if call not in ignored]


def classify_function(function: dict[str, Any]) -> str:
    body = function.get("body", "")
    address = function.get("address", "")
    if "ListOutStream::ListOutStream" in body or address == "00255f70":
        return "list-stream-write-entry"
    if "ListInStream::ListInStream" in body or address == "00256510":
        return "list-stream-read-entry"
    if all(symbol in body for symbol in ("cf_form_controls8", "cf_form_controls_position8", "cf_form_controls_info8")):
        return "ordinary-control-triplet-entry"
    if "cf_form_controls_info8" in body:
        return "ordinary-control-info-entry"
    return "support"


def build_summary(args: argparse.Namespace) -> dict[str, Any]:
    decompile = load_json(Path(args.decompile_json))
    xrefs = load_json(Path(args.xrefs_json)) if args.xrefs_json else {}

    functions = []
    for function in decompile.get("functions", []):
        body = function.get("body", "")
        calls = calls_from_body(body)
        focus_calls = [
            call
            for call in calls
            if call.startswith("FUN_") or any(symbol in call for symbol in MECHANISM_SYMBOLS)
        ]
        functions.append(
            {
                "address": function.get("address", ""),
                "name": function.get("name", ""),
                "role": classify_function(function),
                "signature": function.get("signature", ""),
                "callerCount": len(function.get("callers", [])),
                "callers": function.get("callers", []),
                "focusCalls": focus_calls,
                "contains": [symbol for symbol in MECHANISM_SYMBOLS if symbol in body],
                "decompileBodyRecorded": False,
            }
        )

    xref_symbols = []
    for symbol in xrefs.get("symbols", []):
        if symbol.get("needle") in {
            "cf_form_controls8",
            "cf_form_controls_position8",
            "cf_form_controls_info8",
        } and symbol.get("xrefs"):
            xref_symbols.append(symbol)

    libraries = []
    for lib in args.libs:
        path = Path(lib)
        symbols = nm_symbols(path)
        libraries.append(
            {
                "library": path.name,
                "mechanismImportCount": len(symbols),
                "imports": symbols[: args.max_imports],
            }
        )

    return {
        "program": decompile.get("program", ""),
        "sourceArtifacts": {
            "decompileJson": str(Path(args.decompile_json)),
            "xrefsJson": str(Path(args.xrefs_json)) if args.xrefs_json else "",
            "libraries": [Path(lib).name for lib in args.libs],
        },
        "targets": decompile.get("targets", []),
        "functions": functions,
        "cfFormControlXrefs": xref_symbols,
        "libraryImports": libraries,
        "implementationMapping": [
            {
                "platformRole": "ListOutStream entry",
                "platformEvidence": "FUN_00255f70 constructs core::ListOutStream and is called from write paths.",
                "repoTarget": "sidecars/onec-form-native list-stream writer, then Form.bin writer",
            },
            {
                "platformRole": "ListInStream entry",
                "platformEvidence": "FUN_00256510 constructs core::ListInStream and is called from read paths.",
                "repoTarget": "sidecars/onec-form-native list-stream reader, then Form.bin reader",
            },
            {
                "platformRole": "ordinary control triplet",
                "platformEvidence": "FUN_002709e0, FUN_00270da0, and FUN_00270fe0 call cf_form_controls8, cf_form_controls_position8, and cf_form_controls_info8.",
                "repoTarget": "native descriptor registry for control, position, and info records",
            },
            {
                "platformRole": "ordinary control info",
                "platformEvidence": "FUN_002c9430 calls cf_form_controls_info8 without the full triplet.",
                "repoTarget": "native control-info serializer for shared/base-info records",
            },
            {
                "platformRole": "core typed values",
                "platformEvidence": "dsgnfrm/mngbase/mngui imports TypeDomainPattern, CompositeID, GenericValue, Color, Font, V8Border, V8Picture serializers.",
                "repoTarget": "native platform value layer before public XML model bridge",
            },
        ],
    }


def write_markdown(summary: dict[str, Any]) -> str:
    lines = [
        "# Platform Ordinary-Form Mechanism",
        "",
        "This file is a sanitized extraction summary. It records function roles,",
        "symbol references, and implementation mapping, but not decompiled platform",
        "bodies or private platform binaries.",
        "",
        "## Entry Points",
        "",
    ]
    for function in summary["functions"]:
        lines.append(
            f"- `{function['address']}` `{function['name']}`: {function['role']}; "
            f"contains {', '.join(function['contains']) or 'no focus symbols'}."
        )
    lines += ["", "## Implementation Mapping", ""]
    for mapping in summary["implementationMapping"]:
        lines.append(f"- {mapping['platformRole']}: {mapping['repoTarget']}")
    lines += ["", "## Library Import Surface", ""]
    for lib in summary["libraryImports"]:
        lines.append(f"- `{lib['library']}`: {lib['mechanismImportCount']} relevant imports")
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--decompile-json", required=True)
    parser.add_argument("--xrefs-json")
    parser.add_argument("--lib", action="append", dest="libs", default=[])
    parser.add_argument("--json-out", required=True)
    parser.add_argument("--md-out")
    parser.add_argument("--max-imports", type=int, default=80)
    args = parser.parse_args()

    summary = build_summary(args)
    json_out = Path(args.json_out)
    json_out.parent.mkdir(parents=True, exist_ok=True)
    json_out.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.md_out:
        md_out = Path(args.md_out)
        md_out.parent.mkdir(parents=True, exist_ok=True)
        md_out.write_text(write_markdown(summary), encoding="utf-8")


if __name__ == "__main__":
    main()
