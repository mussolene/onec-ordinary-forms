from pathlib import Path

import pytest

from onec_ordinary_forms.native_bridge import (
    assert_payload_lossless,
    formbin_roundtrip_report,
    native_binary,
)


ROOT = Path(__file__).resolve().parents[1]


def test_native_binary_exists_after_build() -> None:
    binary = native_binary()
    assert binary.exists()


def test_blank_source_formbin_is_payload_lossless() -> None:
    form_bin = ROOT / "work/oracle-runtime/blank-source/root/Forms/Форма/Ext/Form.bin"
    if not form_bin.exists():
        pytest.skip("blank-source fixture is not present")
    report = formbin_roundtrip_report(form_bin)
    assert report.get("logicalEqual") is True


def test_assert_payload_lossless_helper() -> None:
    form_bin = ROOT / "work/oracle-runtime/blank-source/root/Forms/Форма/Ext/Form.bin"
    if not form_bin.exists():
        pytest.skip("blank-source fixture is not present")
    assert_payload_lossless(form_bin)
