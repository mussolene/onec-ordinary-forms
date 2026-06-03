from __future__ import annotations

import xml.etree.ElementTree as ET

import pytest

from onec_ordinary_forms.public_contract import assert_v1_public_form_xml


def test_public_contract_accepts_named_v1_form_xml() -> None:
    root = ET.fromstring(
        """
        <Form version="1.0">
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
                <Position left="1" top="2" right="80" bottom="24"/>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )

    assert_v1_public_form_xml(root)


@pytest.mark.parametrize(
    ("xml", "message"),
    [
        ('<Form version="0.1"/>', "unsupported pre-v1"),
        ('<Form version="1.0"><ListStream/></Form>', "ListStream"),
        ('<Form version="1.0"><ChildItems/></Form>', "ChildItems"),
        ('<Form version="1.0"><Button recordKind="8"/></Form>', "recordKind"),
        ('<Form version="1.0"><Buttons rootKind="3"/></Form>', "rootKind"),
        ('<Form version="1.0"><Attribute name="A" slot="1"/></Form>', "slot"),
        ('<Form version="1.0"><Value index="1" kind="list"/></Form>', "indexed raw"),
        ('<Form ordinaryFormVersion="2.0-draft"/>', "OrdinaryFormV2"),
    ],
)
def test_public_contract_rejects_legacy_and_raw_shapes(xml: str, message: str) -> None:
    with pytest.raises(ValueError, match=message):
        assert_v1_public_form_xml(ET.fromstring(xml))
