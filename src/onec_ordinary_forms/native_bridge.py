"""Thin subprocess bridge to the native ``oof-native`` ordinary-form engine.

The native sidecar is the canonical object/Form.bin codec. Python keeps the
public ``Form.xml`` contract, corpus tooling, and orchestration; it calls
into native code for materialization checks and (incrementally) dump/build.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_NATIVE_BIN = REPO_ROOT / "sidecars/onec-form-native/build/oof-native"


class NativeBridgeError(RuntimeError):
    pass


def native_binary(path: Path | None = None) -> Path:
    binary = path or DEFAULT_NATIVE_BIN
    if not binary.exists():
        raise NativeBridgeError(f"native binary is not built: {binary}")
    return binary


def run_native(command: str, *args: str, binary: Path | None = None) -> str:
    argv = [str(native_binary(binary)), command, *args]
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, check=True)
    except subprocess.CalledProcessError as exc:
        raise NativeBridgeError(
            f"native command failed ({' '.join(argv)}): {exc.stderr or exc.stdout}"
        ) from exc
    return proc.stdout


def run_native_json(command: str, *args: str, binary: Path | None = None) -> dict:
    stdout = run_native(command, *args, binary=binary)
    try:
        return json.loads(stdout)
    except json.JSONDecodeError as exc:
        raise NativeBridgeError(f"native command returned non-JSON output: {command}") from exc


def dump_formbin_package(form_bin: Path, out_xml: Path, *, binary: Path | None = None) -> dict:
    out_xml.parent.mkdir(parents=True, exist_ok=True)
    stdout = run_native("formbin-dump-package", str(form_bin), str(out_xml), binary=binary)
    return json.loads(stdout) if stdout.strip() else {}


def build_formbin_package(base_form_bin: Path, xml: Path, out_bin: Path, *, binary: Path | None = None) -> dict:
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    stdout = run_native("formbin-build-package", str(base_form_bin), str(xml), str(out_bin), binary=binary)
    return json.loads(stdout) if stdout.strip() else {}


def formbin_roundtrip_report(form_bin: Path, *, binary: Path | None = None) -> dict:
    return run_native_json("formbin-roundtrip", str(form_bin), binary=binary)


def assert_payload_lossless(form_bin: Path, *, binary: Path | None = None) -> dict:
    report = formbin_roundtrip_report(form_bin, binary=binary)
    if not report.get("logicalEqual"):
        raise NativeBridgeError(
            f"Form.bin payload is not lossless after native round-trip: {form_bin}"
        )
    return report


def platform_object_report(form_bin: Path, *, binary: Path | None = None) -> dict:
    return run_native_json("formbin-platform-object", str(form_bin), binary=binary)
