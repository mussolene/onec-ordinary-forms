#!/usr/bin/env python3
"""Lossless opaque-preserving round-trip gate over a local Form.bin corpus.

This locks the foundational invariant the object engine builds on: a no-op
parse -> rebuild of a Form.bin container preserves every file payload exactly
(``logicalEqual``). Container bytes may differ only by deflate recompression
level (``byteEqual`` is reported as a statistic, not enforced).

The object-level no-op round-trip (materialize -> dematerialize preserves the
form payload list-stream) is locked separately on fixtures in the native
``make test`` target via ``runtime-form-roundtrip`` ``payloadRoundtripEqual`` /
``canonicalRoundtripEqual`` assertions. This gate adds the corpus dimension.

The gate is a no-op (exit 0) when no corpus root is present, so environments
without the private corpus do not fail.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


DEFAULT_ROOTS = [
    "work/oracle-runtime/runtime-source/root",
    "work/oracle-runtime/blank-source/root",
]


def find_form_bins(root: Path) -> list[Path]:
    return sorted(root.glob("**/Forms/*/Ext/Form.bin"))


def roundtrip(native_bin: Path, form_bin: Path) -> dict:
    proc = subprocess.run(
        [str(native_bin), "formbin-roundtrip", str(form_bin)],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(proc.stdout)


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--native-bin",
        default=str(repo_root / "sidecars/onec-form-native/build/oof-native"),
    )
    parser.add_argument(
        "--root",
        action="append",
        help="Corpus root(s) to scan for Forms/*/Ext/Form.bin (repeatable)",
    )
    args = parser.parse_args()

    native_bin = Path(args.native_bin)
    if not native_bin.exists():
        print(f"lossless-roundtrip-gate: native binary not found: {native_bin}", file=sys.stderr)
        return 2

    roots = [Path(r) for r in (args.root or DEFAULT_ROOTS)]
    roots = [r if r.is_absolute() else repo_root / r for r in roots]

    form_bins: list[Path] = []
    for root in roots:
        if root.exists():
            form_bins.extend(find_form_bins(root))

    if not form_bins:
        print("lossless-roundtrip-gate: no corpus present, skipping (PASS)")
        return 0

    total = 0
    logical_ok = 0
    byte_ok = 0
    failures: list[str] = []
    for form_bin in form_bins:
        total += 1
        try:
            report = roundtrip(native_bin, form_bin)
        except (subprocess.CalledProcessError, json.JSONDecodeError) as exc:
            failures.append(f"{form_bin}: roundtrip crashed: {exc}")
            continue
        if report.get("logicalEqual"):
            logical_ok += 1
        else:
            failures.append(f"{form_bin}: logicalEqual=false (payload not preserved)")
        if report.get("byteEqual"):
            byte_ok += 1

    print(
        "lossless-roundtrip-gate stats: "
        + json.dumps({"total": total, "logicalEqual": logical_ok, "byteEqual": byte_ok})
    )
    if failures:
        print(f"lossless-roundtrip-gate: FAIL ({len(failures)} form(s))", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(f"lossless-roundtrip-gate: PASS ({logical_ok}/{total} payload-lossless)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
