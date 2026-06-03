import importlib.util
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
NATIVE_BIN = ROOT / "sidecars/onec-form-native/build/oof-native"

SPEC = importlib.util.spec_from_file_location("object_model_gate", ROOT / "tools/object_model_gate.py")
assert SPEC is not None and SPEC.loader is not None
object_model_gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(object_model_gate)


def _ensure_native() -> Path:
    if NATIVE_BIN.exists():
        return NATIVE_BIN
    if shutil.which("c++") or shutil.which("clang++") or shutil.which("g++"):
        subprocess.run(["make", "-C", str(ROOT / "sidecars/onec-form-native")], check=True, capture_output=True)
        if NATIVE_BIN.exists():
            return NATIVE_BIN
    pytest.skip("native oof-native binary is not built")


def test_object_model_gate_passes_no_writable_unproven_or_raw_shape() -> None:
    native_bin = _ensure_native()
    violations, stats = object_model_gate.gate(native_bin)
    assert violations == [], "\n".join(violations)
    # No value-layer (info8) property may be writable until proven by oracle.
    assert stats["writableValueCodecs"] == 0
    # Value properties must still exist as readable coverage gaps, not be deleted.
    assert stats["readableValueCoverageGaps"] > 0


def test_proven_writable_codec_set_excludes_info8_value_codecs() -> None:
    proven = object_model_gate.PROVEN_WRITABLE_CODECS
    unproven = object_model_gate.UNPROVEN_VALUE_CODECS
    assert proven.isdisjoint(unproven)
    assert {"color-record", "font-record", "picture-record", "border-record"} <= unproven
