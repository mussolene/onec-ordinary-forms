from __future__ import annotations

import xml.etree.ElementTree as ET

import pytest

from onec_ordinary_forms.cli import validate_xml_file
from onec_ordinary_forms.public_contract import assert_public_form_xml
from onec_ordinary_forms.ordinary_stream import form_stream_from_object_xml


def test_public_contract_accepts_managed_style_form_xml() -> None:
    root = ET.fromstring(
        """
        <Form xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" ordinaryFormVersion="2.0" xsi:noNamespaceSchemaLocation="OrdinaryForm.xsd">
          <Title><Item lang="ru">Main</Item></Title>
          <ChildItems>
            <Page name="Main">
              <ChildItems>
                <Button name="Run" id="7">
                  <Title><Item lang="ru">Run</Item></Title>
                  <Position left="1" top="2" right="80" bottom="24" />
                </Button>
              </ChildItems>
            </Page>
          </ChildItems>
        </Form>
        """
    )

    assert_public_form_xml(root)


@pytest.mark.parametrize(
    ("xml", "message"),
    [
        ('<Form version="0.1"/>', "version attribute"),
        ('<Form ordinaryFormVersion="2.0" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="OrdinaryForm.xsd"><ListStream/></Form>', "ListStream"),
        (
            '<Form ordinaryFormVersion="2.0"><Pages><Page name="Main" /></Pages></Form>',
            "deprecated <Pages>",
        ),
        (
            '<Form ordinaryFormVersion="2.0"><ChildItems><Page name="Main"><Button name="Run" /></Page></ChildItems></Form>',
            "must be nested in <ChildItems>",
        ),
        (
            '<Form ordinaryFormVersion="2.0"><ChildItems><Page name="Main"><ChildItems><Button name="Run"><DefinitelyNotAContractProperty rawish="1" /></Button></ChildItems></Page></ChildItems></Form>',
            "unknown public property",
        ),
        ('<Form ordinaryFormVersion="2.0"><Button recordKind="8" /></Form>', "recordKind"),
        ('<Form ordinaryFormVersion="2.0"><Buttons rootKind="3" /></Form>', "rootKind"),
        ('<Form ordinaryFormVersion="2.0"><Attribute name="A" slot="1" /></Form>', "slot"),
        ('<Form ordinaryFormVersion="2.0"><Value index="1" kind="list" /></Form>', "indexed raw"),
        ('<Form/>', "ordinaryFormVersion"),
    ],
)
def test_public_contract_rejects_legacy_and_raw_shapes(xml: str, message: str) -> None:
    with pytest.raises(ValueError, match=message):
        assert_public_form_xml(ET.fromstring(xml))


def test_validate_xml_file_rejects_unknown_public_property(tmp_path) -> None:
    xml = tmp_path / "Form.xml"
    xml.write_text(
        """<Form xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" ordinaryFormVersion="2.0" xsi:noNamespaceSchemaLocation="OrdinaryForm.xsd">
          <ChildItems><Page name="Main"><ChildItems><Button name="Run"><DefinitelyNotAContractProperty rawish="1" /></Button></ChildItems></Page></ChildItems>
        </Form>""",
        encoding="utf-8",
    )

    with pytest.raises(ValueError, match="unknown public property"):
        validate_xml_file(xml)


def test_writer_rejects_unknown_public_property_without_version() -> None:
    root = ET.fromstring(
        '<Form><ChildItems><Page name="Main"><ChildItems><Button name="Run"><DefinitelyNotAContractProperty /></Button></ChildItems></Page></ChildItems></Form>'
    )

    with pytest.raises(ValueError, match="unknown public property"):
        form_stream_from_object_xml(root)
