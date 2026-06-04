from pathlib import Path
import base64
import xml.etree.ElementTree as ET

import pytest

from onec_ordinary_forms.native_bridge import (
    assert_payload_lossless,
    build_formbin_package,
    dump_formbin_package,
    formbin_roundtrip_report,
    native_binary,
)
from onec_ordinary_forms.formbin import build_form_bin_container, parse_form_bin_container
from onec_ordinary_forms.ordinary_stream import form_stream_from_object_xml


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


def test_native_package_roundtrips_picture_sidecar(tmp_path: Path) -> None:
    png = base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADElEQVR42mNg+M8AAAAEAAH/6G3fAAAAAElFTkSuQmCC"
    )
    gif = b"GIF89a\x01\x00\x01\x00\x00\x00\x00;"
    source_assets = tmp_path / "source-assets"
    source_picture = source_assets / "Items" / "Run" / "Picture.png"
    source_picture.parent.mkdir(parents=True)
    source_picture.write_bytes(png)
    root = ET.fromstring(
        """<Form>
          <Title><Item lang="#">Main</Item></Title>
          <ChildItems>
            <Page name="Main">
              <ChildItems><Button name="Run" id="26">
                <Title><Item lang="ru">Run</Item></Title>
                <Picture file="Items/Run/Picture.png" />
              </Button></ChildItems>
            </Page>
          </ChildItems>
        </Form>"""
    )
    source = tmp_path / "Form.bin"
    source.write_bytes(build_form_bin_container(form_stream_from_object_xml(root, source_assets), b"module"))

    xml = tmp_path / "Ext" / "Form.xml"
    rebuilt = tmp_path / "rebuilt.bin"
    redump_xml = tmp_path / "redump" / "Form.xml"

    dump_report = dump_formbin_package(source, xml)
    dumped_picture = xml.with_suffix("") / "Items" / "Run" / "Picture.png"
    assert dump_report["pictureSidecars"] == 1
    assert dumped_picture.read_bytes() == png
    assert 'file="Items/Run/Picture.png"' in xml.read_text(encoding="utf-8")
    assert "#base64:" not in xml.read_text(encoding="utf-8")

    dumped_picture.write_bytes(gif)
    build_report = build_formbin_package(source, xml, rebuilt)
    assert build_report["pictureSidecarsRead"] == 1

    redump_report = dump_formbin_package(rebuilt, redump_xml)
    assert redump_report["pictureSidecars"] == 1
    assert (redump_xml.with_suffix("") / "Items" / "Run" / "Picture.gif").read_bytes() == gif
