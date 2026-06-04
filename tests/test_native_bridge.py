from pathlib import Path
import base64
import xml.etree.ElementTree as ET

import pytest

from onec_ordinary_forms.native_bridge import (
    assert_payload_lossless,
    build_formbin_package,
    build_formbin_source_package,
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


def test_native_source_package_builds_without_base_bin(tmp_path: Path) -> None:
    xml = tmp_path / "Ext" / "Form.xml"
    rebuilt = tmp_path / "rebuilt.bin"
    module = xml.with_suffix("") / "Module.bsl"
    module.parent.mkdir(parents=True)
    xml.parent.mkdir(parents=True, exist_ok=True)
    xml.write_text(
        """<?xml version='1.0' encoding='utf-8'?>
<Form ordinaryFormVersion="2.0">
  <ChildItems>
    <Page name="Main" id="4">
      <ChildItems>
        <Button name="ButtonSourceEdited" id="7">
          <Title>SourceTitle</Title>
          <Position left="9" top="2" right="101" bottom="22"/>
        </Button>
        <InputField name="InputSourceEdited" id="8"/>
      </ChildItems>
    </Page>
  </ChildItems>
</Form>
""",
        encoding="utf-8",
    )
    module.write_bytes(b"source module")

    report = build_formbin_source_package(xml, rebuilt)

    assert report["operation"] == "formbin-build-source-package"
    assert report["baseBinRequired"] is False
    assert report["moduleSource"] == "sidecar"
    files = {file.name: file.payload for file in parse_form_bin_container(rebuilt.read_bytes()).files}
    assert b"ButtonSourceEdited" in files["form"]
    assert files["module"] == b"source module"


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


def test_native_package_deletes_leaf_control_from_public_xml(tmp_path: Path) -> None:
    root = ET.fromstring(
        """<Form>
          <Title><Item lang="ru">Main</Item></Title>
          <ChildItems>
            <Page name="Main">
              <ChildItems>
                <Button name="Keep" id="7">
                  <Title><Item lang="ru">Keep</Item></Title>
                  <Position left="20" top="20" right="120" bottom="45" />
                </Button>
                <Button name="Drop" id="8">
                  <Title><Item lang="ru">Drop</Item></Title>
                  <Position left="140" top="20" right="240" bottom="45" />
                </Button>
              </ChildItems>
            </Page>
          </ChildItems>
        </Form>"""
    )
    source = tmp_path / "Form.bin"
    source.write_bytes(build_form_bin_container(form_stream_from_object_xml(root), b"module"))

    xml = tmp_path / "Ext" / "Form.xml"
    rebuilt = tmp_path / "rebuilt.bin"
    redump_xml = tmp_path / "redump" / "Form.xml"

    dump_formbin_package(source, xml)
    public_tree = ET.parse(xml)
    public_root = public_tree.getroot()
    drop = public_root.find(".//Button[@id='8']")
    assert drop is not None
    for child_items in public_root.findall(".//ChildItems"):
        if drop in list(child_items):
            child_items.remove(drop)
            break
    public_tree.write(xml, encoding="utf-8", xml_declaration=True)

    build_report = build_formbin_package(source, xml, rebuilt)
    assert build_report["deletedControls"] == 1

    dump_formbin_package(rebuilt, redump_xml)
    redump_text = redump_xml.read_text(encoding="utf-8")
    assert 'name="Keep"' in redump_text
    assert 'name="Drop"' not in redump_text
