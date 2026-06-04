from pathlib import Path

import pytest

from onec_ordinary_forms.native_bridge import (
    assert_payload_lossless,
    build_formbin_package,
    dump_formbin_package,
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


def test_native_dump_and_build_package_use_cpp_backend(tmp_path: Path) -> None:
    source = tmp_path / "Form.bin"
    xml = tmp_path / "Form.xml"
    rebuilt = tmp_path / "rebuilt.bin"
    source.write_bytes(build_form_bin_container(b'{{"MainCaption",1,1,{"ru","Main"}}}', b"module"))

    dump_report = dump_formbin_package(source, xml)
    module_path = xml.with_suffix("") / "Module.bsl"
    assert dump_report["operation"] == "formbin-dump-package"
    assert dump_report["moduleWritten"] is True
    assert module_path.read_bytes() == b"module"

    module_path.write_bytes(b"edited module")
    report = build_formbin_package(source, xml, rebuilt)

    assert report["operation"] == "formbin-build-package"
    assert report["moduleSource"] == "sidecar"
    assert report["publicContract"] == "OrdinaryForm"
    files = {file.name: file.payload for file in parse_form_bin_container(rebuilt.read_bytes()).files}
    assert files["module"] == b"edited module"
