#!/usr/bin/env python3
"""Verify independently prepared Button property EPF variants."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


ROLES = ("base", "base-repeat", "caption", "enabled")
PROVENANCE = {"platform-ui", "oof-writer", "unknown"}
HEADER = ("role", "epf", "button_name", "caption", "enabled", "provenance")


def parse_manifest(path: Path) -> list[dict[str, str | Path]]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = csv.DictReader(stream, delimiter="\t")
        if tuple(rows.fieldnames or ()) != HEADER:
            raise ValueError("manifest header must be: " + "\\t".join(HEADER))
        result = []
        for row in rows:
            if None in row or any(row.get(key) is None for key in HEADER):
                raise ValueError("manifest has a malformed row")
            if row["role"] not in ROLES:
                raise ValueError(f"unknown role: {row['role']}")
            if row["enabled"] not in ("true", "false"):
                raise ValueError(f"enabled must be true or false for role {row['role']}")
            if row["provenance"] not in PROVENANCE:
                raise ValueError(f"unsupported provenance label for role {row['role']}")
            if not row["button_name"]:
                raise ValueError(f"button_name must not be empty for role {row['role']}")
            epf = Path(row["epf"])
            if not epf.is_absolute():
                epf = path.parent / epf
            epf = epf.resolve(strict=True)
            if not epf.is_file() or not os.access(epf, os.R_OK):
                raise ValueError(f"EPF for role {row['role']} is not a readable file")
            result.append({**row, "epf": epf})
    if len(result) != len(ROLES) or {str(row["role"]) for row in result} != set(ROLES):
        raise ValueError("manifest must contain exactly one row for each required role")
    by_role = {str(row["role"]): row for row in result}
    base, repeat, caption, enabled = (by_role[role] for role in ROLES)
    if len({str(by_role[role]["button_name"]) for role in ROLES}) != 1:
        raise ValueError("button_name must match across all roles")
    if (base["caption"] != repeat["caption"] or base["caption"] != enabled["caption"]
            or base["enabled"] != repeat["enabled"] or base["enabled"] != caption["enabled"]):
        raise ValueError("variants must change only their declared property")
    if caption["caption"] == base["caption"] or enabled["enabled"] == base["enabled"]:
        raise ValueError("caption and enabled variants must differ from base")
    return [by_role[role] for role in ROLES]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def readback(form_xml: Path, button_name: str, caption: str, enabled: str) -> tuple[str, str]:
    try:
        root = ET.parse(form_xml).getroot()
    except (ET.ParseError, OSError) as exc:
        return "FAIL", f"cannot-parse-oof-xml:{type(exc).__name__}"
    buttons = [node for node in root.iter() if local_name(node.tag) == "Button"]
    matches = [node for node in buttons if node.get("name") == button_name]
    if len(buttons) != 1 or len(matches) != 1:
        return "FAIL", f"button-count:{len(buttons)}:matches:{len(matches)}"
    values = {local_name(child.tag): child.text or "" for child in matches[0]}
    actual_caption = values.get("Caption", "")
    # Enabled=true is the platform/model default and may be omitted in XML.
    actual_enabled = values.get("Enabled", "true")
    if actual_caption != caption or actual_enabled != enabled:
        return "FAIL", f"values-differ:caption={actual_caption!r}:enabled={actual_enabled!r}"
    return "PASS", "expected-named-values-read-back"


def read_diagnostics(path: Path) -> list[dict[str, str]]:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return []
    diagnostics = result.get("diagnostics", []) if isinstance(result, dict) else []
    fields = ("code", "path", "property", "message")
    return [
        {field: str(item.get(field, "")) for field in fields}
        for item in diagnostics
        if isinstance(item, dict)
    ]


def run_case(row: dict[str, str | Path], run_dir: Path, validator: Path, oof: Path) -> dict[str, object]:
    role = str(row["role"])
    case_dir = run_dir / role
    platform_dir = case_dir / "platform"
    case_dir.mkdir()
    strict = "FAIL"
    read_status, read_detail = "NOT_RUN", "strict-designer-acceptance-failed"
    read_diagnostics_result: list[dict[str, str]] = []
    form_digest = dump_digest = None
    source_digest = sha256(Path(row["epf"]))
    epf_copy = case_dir / "input.epf"
    shutil.copyfile(Path(row["epf"]), epf_copy)
    copied_digest = sha256(epf_copy)
    validator_stdout = (case_dir / "validator.stdout.log").open("wb")
    validator_stderr = (case_dir / "validator.stderr.log").open("wb")
    try:
        result = subprocess.run([str(validator), str(epf_copy), str(platform_dir)],
                                stdout=validator_stdout, stderr=validator_stderr, check=False,
                                cwd=validator.parents[1], timeout=300)
    except (OSError, subprocess.TimeoutExpired) as exc:
        result = None
        (case_dir / "runner-error.txt").write_text(type(exc).__name__ + "\n", encoding="utf-8")
    finally:
        validator_stdout.close()
        validator_stderr.close()

    dump_root = platform_dir / "dump" / "root.xml"
    code_file = platform_dir / "code.txt"
    designer_log = platform_dir / "platform-dump.log"
    code_ok = code_file.is_file() and code_file.read_text(encoding="utf-8").strip() == "0"
    if result is not None and result.returncode == 0 and code_ok and designer_log.is_file() and dump_root.is_file() and dump_root.stat().st_size:
        strict = "PASS"
        dump_digest = sha256(dump_root)
        form_bins = list((platform_dir / "dump").rglob("Form.bin"))
        if len(form_bins) != 1:
            read_status, read_detail = "PARTIAL", f"fresh-form-bin-count:{len(form_bins)}"
        else:
            form_bin = form_bins[0]
            form_digest = sha256(form_bin)
            readback_dir = case_dir / "readback"
            readback_dir.mkdir()
            form_xml = readback_dir / "Form.xml"
            try:
                with (case_dir / "oof.stdout.log").open("wb") as out, (case_dir / "oof.stderr.log").open("wb") as err:
                    oof_result = subprocess.run([str(oof), "dump", str(form_bin), str(form_xml), "--json"],
                                                stdout=out, stderr=err, check=False, timeout=30)
            except (OSError, subprocess.TimeoutExpired) as exc:
                (case_dir / "oof-runner-error.txt").write_text(type(exc).__name__ + "\n", encoding="utf-8")
                oof_result = None
            if oof_result is None or oof_result.returncode != 0:
                read_status, read_detail = "FAIL", "oof-dump-failed"
                read_diagnostics_result = read_diagnostics(case_dir / "oof.stdout.log")
            else:
                read_status, read_detail = readback(form_xml, str(row["button_name"]),
                                                    str(row["caption"]), str(row["enabled"]))
    return {
        "role": role,
        "strict_designer_acceptance": strict,
        "named_values_readback": read_status,
        "readback_detail": read_detail,
        "readback_diagnostics": read_diagnostics_result,
        "expected": {"button_name": row["button_name"], "caption": row["caption"],
                     "enabled": row["enabled"] == "true"},
        "declared_provenance": row["provenance"],
        "independent_mutation": "UNVERIFIED",
        "input_epf_sha256": source_digest,
        "container_input_copy_sha256": copied_digest,
        "input_copy_matches_source": source_digest == copied_digest,
        "input_epf_unchanged": source_digest == sha256(Path(row["epf"])),
        "designer_dump_root_sha256": dump_digest,
        "fresh_form_bin_sha256": form_digest,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Strictly validate four independently prepared Button EPF variants and read named values from each fresh Designer-dumped Form.bin.",
        epilog=("TSV header: role\\tepf\\tbutton_name\\tcaption\\tenabled\\tprovenance. "
                "Roles: base, base-repeat, caption, enabled. Paths are relative to the manifest or absolute. "
                "Provenance values: platform-ui, oof-writer, unknown. Declared provenance is never proof of independent mutation; "
                "oof readback is not an independent format oracle. Output is a new directory under scan-output/ only. "
                "Exit codes: 0 means all requested checks passed (overall may remain PARTIAL for provenance), "
                "1 means a check failed, 2 means invalid arguments/inputs, 3 means a check is PARTIAL."))
    parser.add_argument("manifest", type=Path)
    parser.add_argument("run_dir", nargs="?", type=Path,
                        help="new output directory under scan-output/ (default: unique timestamped directory)")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    try:
        manifest = args.manifest.resolve(strict=True)
    except OSError as exc:
        parser.error(f"cannot read manifest: {type(exc).__name__}")
    run_dir_arg = args.run_dir or Path("scan-output") / f"button-property-pilot-{datetime.now().strftime('%Y%m%dT%H%M%SZ')}-{os.getpid()}"
    if not run_dir_arg.is_absolute():
        run_dir_arg = repo / run_dir_arg
    scan_root_requested = repo / "scan-output"
    if scan_root_requested.is_symlink():
        parser.error("repository scan-output/ must not be a symlink")
    requested_parent = Path(os.path.abspath(run_dir_arg.parent))
    if requested_parent != scan_root_requested and scan_root_requested not in requested_parent.parents:
        parser.error("run-dir parent must be lexically under repository scan-output/")
    for parent in (requested_parent, *requested_parent.parents):
        if parent == scan_root_requested.parent:
            break
        if parent.is_symlink():
            parser.error("run-dir path must not pass through symlinks")
    output_parent = run_dir_arg.parent.resolve()
    scan_root = scan_root_requested.resolve()
    if output_parent != scan_root and scan_root not in output_parent.parents:
        parser.error("run-dir parent must resolve under repository scan-output/")
    run_dir = output_parent / run_dir_arg.name
    if run_dir.exists() or run_dir.is_symlink():
        parser.error("run-dir must not already exist")

    if not os.environ.get("OOF_PLATFORM_CONTAINER"):
        parser.error("set OOF_PLATFORM_CONTAINER to the approved running platform container")
    validator = repo / "tools" / "platform_validate_epf.sh"
    oof = repo / "build" / "sidecars" / "onec-form-native" / "oof"
    if not os.access(validator, os.X_OK):
        parser.error("tools/platform_validate_epf.sh must be executable")
    if not os.access(oof, os.X_OK):
        parser.error("build the oof CLI before running this verifier")
    try:
        cases = parse_manifest(manifest)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

    run_dir.mkdir(parents=True)
    results = [run_case(row, run_dir, validator, oof) for row in cases]
    by_role = {str(case["role"]): case for case in results}
    base_hash = by_role["base"]["fresh_form_bin_sha256"]
    repeat_hash = by_role["base-repeat"]["fresh_form_bin_sha256"]
    verification_status = "FAIL" if any(case["strict_designer_acceptance"] == "FAIL" or case["named_values_readback"] == "FAIL" or not case["input_copy_matches_source"] or not case["input_epf_unchanged"] for case in results) else (
        "PARTIAL" if any(case["strict_designer_acceptance"] != "PASS" or case["named_values_readback"] != "PASS" for case in results) else "PASS")
    report = {
        "protocol": "button-property-pilot-v1",
        "verification_status": verification_status,
        "overall": "FAIL" if verification_status == "FAIL" else "PARTIAL",
        "cases": results,
        "base_repeat_form_bin_byte_equal": bool(base_hash and repeat_hash and base_hash == repeat_hash),
        "limitations": [
            "Provenance is declared by the manifest and is never independently verified by this script.",
            "Named values are read by this repository's oof reader; this is not an independent format oracle.",
            "Exact Form.bin hashes report byte equality only and do not classify platform-owned changes or structural differences.",
        ],
    }
    report_path = run_dir / "report.json"
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return {"PASS": 0, "FAIL": 1, "PARTIAL": 3}[verification_status]


if __name__ == "__main__":
    sys.exit(main())
