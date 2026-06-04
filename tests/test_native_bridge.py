from pathlib import Path

import pytest

from onec_ordinary_forms.native_bridge import (
    assert_payload_lossless,
    build_formbin_xml,
    dump_formbin_xml,
    formbin_roundtrip_report,
    native_binary,
)
from onec_ordinary_forms.formbin import build_form_bin_container, parse_form_bin_container


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


def test_native_dump_and_build_xml_use_cpp_backend(tmp_path: Path) -> None:
    source = tmp_path / "Form.bin"
    xml = tmp_path / "Form.xml"
    rebuilt = tmp_path / "rebuilt.bin"
    source.write_bytes(build_form_bin_container(b'{{"MainCaption",1,1,{"ru","Main"}}}', b"module"))

    dump_formbin_xml(source, xml)
    report = build_formbin_xml(source, xml, rebuilt)

    assert report["operation"] == "formbin-build-xml"
    assert report["publicContract"] == "OrdinaryForm"
    files = {file.name: file.payload for file in parse_form_bin_container(rebuilt.read_bytes()).files}
    assert files["module"] == b"module"
