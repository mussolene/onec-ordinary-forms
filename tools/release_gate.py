#!/usr/bin/env python3
"""Check non-private release gates that can run outside 1C Designer."""

from __future__ import annotations

import argparse
import json
import os
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


def run_native_selftests(repo: Path) -> dict[str, object]:
    binary = repo / "sidecars" / "onec-form-native" / "build" / "oof-native"
    commands = [
        "formbin-xml-build-selftest",
        "formbin-platform-object-selftest",
        "form-payload-structure-selftest",
    ]
    results: list[dict[str, object]] = []
    failures: list[str] = []
    for command in commands:
        completed = subprocess.run(
            [str(binary), command],
            cwd=repo,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        output = completed.stdout.strip()
        results.append({"command": command, "returncode": completed.returncode, "outputTail": output[-1200:]})
        if completed.returncode != 0:
            failures.append(f"native {command} failed")
    return {"binary": str(binary), "results": results, "failures": failures}


def run_pytest(repo: Path) -> dict[str, object]:
    env = os.environ.copy()
    src_path = str(repo / "src")
    env["PYTHONPATH"] = f"{src_path}{os.pathsep}{env['PYTHONPATH']}" if env.get("PYTHONPATH") else src_path
    completed = subprocess.run(
        [sys.executable, "-m", "pytest", "-q"],
        cwd=repo,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = completed.stdout.strip()
    return {
        "returncode": completed.returncode,
        "outputTail": output[-4000:],
    }


def run_public_contract_probes(repo: Path) -> list[str]:
    sys.path.insert(0, str(repo / "src"))
    from onec_ordinary_forms.cli import validate_xml_file

    failures: list[str] = []
    valid_xml = """<Form xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" ordinaryFormVersion="2.0" xsi:noNamespaceSchemaLocation="OrdinaryForm.xsd">
      <ChildItems><Page name="Main"><ChildItems><Button name="Run"><Title>Run</Title></Button></ChildItems></Page></ChildItems>
    </Form>"""
    invalid_xml = """<Form xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" ordinaryFormVersion="2.0" xsi:noNamespaceSchemaLocation="OrdinaryForm.xsd">
      <ChildItems><Page name="Main"><ChildItems><Button name="Run"><DefinitelyNotAContractProperty rawish="1" /></Button></ChildItems></Page></ChildItems>
    </Form>"""
    with tempfile.TemporaryDirectory() as temp_dir:
        temp = Path(temp_dir)
        valid = temp / "valid.xml"
        invalid = temp / "invalid.xml"
        valid.write_text(valid_xml, encoding="utf-8")
        invalid.write_text(invalid_xml, encoding="utf-8")
        try:
            validate_xml_file(valid)
        except Exception as exc:  # pragma: no cover - reported by release gate output
            failures.append(f"valid public Form.xml probe failed: {exc}")
        try:
            validate_xml_file(invalid)
        except ValueError as exc:
            if "unknown public property" not in str(exc):
                failures.append(f"invalid public property probe failed with wrong error: {exc}")
        except Exception as exc:  # pragma: no cover - reported by release gate output
            failures.append(f"invalid public property probe failed with wrong exception: {exc}")
        else:
            failures.append("invalid public property probe unexpectedly passed")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=".", help="Repository root")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    xsd = repo / "src" / "onec_ordinary_forms" / "schemas" / "OrdinaryFormPalette.xsd"
    pytest_result = run_pytest(repo)
    native_result = run_native_selftests(repo)
    public_contract_failures = run_public_contract_probes(repo)
    report = run_codec_coverage(repo, xsd)
    summary = report["summary"]

    failures: list[str] = []
    if pytest_result["returncode"] != 0:
        failures.append("pytest -q failed")
    failures.extend(native_result["failures"])
    failures.extend(public_contract_failures)
    expected_rows = 417
    if summary["platformPropertyRows"] != expected_rows:
        failures.append(f"platformPropertyRows expected {expected_rows}, got {summary['platformPropertyRows']}")
    if summary["mappedPlatformPropertyRows"] != summary["platformPropertyRows"]:
        failures.append("not all platform property rows are mapped")
    for key in (
        "controlsWithoutNativeSchema",
        "controlsWithoutPublicDescriptor",
        "unmappedPlatformProperties",
        "mappedPlatformPropertiesWithoutPublicXml",
        "xsdOnlyPublicProperties",
    ):
        if summary.get(key):
            failures.append(f"{key} is not empty")

    result = {
        "gate": "release",
        "status": "fail" if failures else "pass",
        "failures": failures,
        "pytest": pytest_result,
        "native": native_result,
        "summary": {
            "platformPropertyRows": summary["platformPropertyRows"],
            "mappedPlatformPropertyRows": summary["mappedPlatformPropertyRows"],
            "mappedPlatformPropertiesWithoutPublicXml": len(summary["mappedPlatformPropertiesWithoutPublicXml"]),
            "xsdOnlyPublicProperties": len(summary["xsdOnlyPublicProperties"]),
            "unmappedPlatformProperties": len(summary["unmappedPlatformProperties"]),
            "controlsWithoutNativeSchema": len(summary["controlsWithoutNativeSchema"]),
        },
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
