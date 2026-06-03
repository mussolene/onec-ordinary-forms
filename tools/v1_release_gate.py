#!/usr/bin/env python3
"""Check non-private v1.0 release gates that can run outside 1C Designer."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run_codec_coverage(repo: Path, xsd: Path) -> dict[str, object]:
    with tempfile.TemporaryDirectory() as temp_dir:
        out = Path(temp_dir) / "codec-coverage.json"
        subprocess.run(
            [
                sys.executable,
                str(repo / "tools" / "audit_codec_coverage.py"),
                "--xsd",
                str(xsd),
                "--out",
                str(out),
            ],
            cwd=repo,
            check=True,
        )
        return json.loads(out.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=".", help="Repository root")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    xsd = repo / "src" / "onec_ordinary_forms" / "schemas" / "OrdinaryForm.xsd"
    report = run_codec_coverage(repo, xsd)
    summary = report["summary"]

    failures: list[str] = []
    expected_rows = 417
    if summary["platformPropertyRows"] != expected_rows:
        failures.append(f"platformPropertyRows expected {expected_rows}, got {summary['platformPropertyRows']}")
    if summary["mappedPlatformPropertyRows"] != summary["platformPropertyRows"]:
        failures.append("not all platform property rows are mapped")
    for key in (
        "controlsWithoutWriterDescriptor",
        "controlsWithoutSharedInfoDescriptor",
        "writerBranchesWithoutXsdControl",
        "writerFallbackTokens",
        "unmappedPlatformProperties",
        "mappedPlatformPropertiesWithoutPublicXml",
    ):
        if summary.get(key):
            failures.append(f"{key} is not empty")

    result = {
        "gate": "v1-release",
        "status": "fail" if failures else "pass",
        "failures": failures,
        "summary": {
            "platformPropertyRows": summary["platformPropertyRows"],
            "mappedPlatformPropertyRows": summary["mappedPlatformPropertyRows"],
            "mappedPlatformPropertiesWithoutPublicXml": len(summary["mappedPlatformPropertiesWithoutPublicXml"]),
            "unmappedPlatformProperties": len(summary["unmappedPlatformProperties"]),
            "writerFallbackTokens": len(summary["writerFallbackTokens"]),
        },
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
