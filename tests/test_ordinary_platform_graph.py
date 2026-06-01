import xml.etree.ElementTree as ET
from pathlib import Path

from onec_ordinary_forms.cli import validate_xml_file
from onec_ordinary_forms.ordinary_model import OrdinaryControl, OrdinaryFormModel
from onec_ordinary_forms.ordinary_platform import (
    CF_FORM_CONTROLS8_FORMAT_ID,
    CF_FORM_CONTROLS_INFO8_FORMAT_ID,
    CF_FORM_CONTROLS_POSITION8_FORMAT_ID,
    ORDINARY_CONTROL_CLASS_BY_GUID,
)
from onec_ordinary_forms.ordinary_platform_graph import (
    all_controls_skeleton_graph,
    platform_graph_from_list_stream_text,
    platform_graph_from_model,
    platform_object_from_list_stream_text,
    platform_object_from_model,
    runtime_skeleton_graph,
)
from onec_ordinary_forms.ordinary_platform_object import (
    PlatformFormObject,
    UnsupportedPlatformObjectOperation,
)
from onec_ordinary_forms.ordinary_platform_dto import (
    ordinary_form_xml_bytes_from_platform_object,
    ordinary_form_xml_from_platform_object,
    platform_object_from_ordinary_form_xml,
    platform_object_from_ordinary_form_xml_rebuild,
    platform_palette_catalog,
)
from onec_ordinary_forms.ordinary_platform_xml import (
    platform_object_from_xml,
    platform_object_from_xml_text,
    platform_object_to_xml,
    platform_object_xml_to_string,
)
from onec_ordinary_forms.ordinary_stream import form_stream_from_object_xml
from onec_ordinary_forms.semantic_digest import semantic_graph_digest


def test_runtime_skeleton_graph_keeps_platform_document_view_and_render_layers() -> None:
    graph = runtime_skeleton_graph()

    assert graph.node("FormDocument").layer == "runtime"
    assert graph.node("FormDocumentView").role == "ordinary-form-view"
    assert graph.node("ControlSite").layer == "bridge"
    assert graph.node("wbase::BaseWindow").layer == "render"
    assert graph.has_edge("FormDocument", "FormDocumentView", "document-view")
    assert graph.has_edge("FormDocumentView", "wbase::Window", "window-implementation")
    assert graph.has_edge("IFormDocumentViewSite", "ControlSite", "control-site-bridge")
    assert not graph.persistence_records


def test_platform_graph_from_model_adds_control_site_and_persistence_records() -> None:
    model = OrdinaryFormModel(
        [
            OrdinaryControl(
                class_id="6ff79819-710e-4145-97cd-1618da79e3e2",
                object_id="42",
                name="RunButton",
                type="Button",
                title="",
                raw=[],
                info_kind="1",
                position_record_count=1,
            )
        ]
    )

    graph = platform_graph_from_model(model)
    records = {(record.owner, record.family, record.format_id) for record in graph.persistence_records}

    assert graph.node("control:42").control_type == "Button"
    assert graph.has_edge("ControlSite", "control:42", "contains-control")
    assert ("control:42", "controls", CF_FORM_CONTROLS8_FORMAT_ID) in records
    assert ("control:42", "position", CF_FORM_CONTROLS_POSITION8_FORMAT_ID) in records
    assert ("control:42", "info", CF_FORM_CONTROLS_INFO8_FORMAT_ID) in records


def test_platform_graph_from_list_stream_text_uses_writer_output_controls() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")

    graph = platform_graph_from_list_stream_text(text)
    records = {(record.owner, record.family, record.format_id) for record in graph.persistence_records}

    assert graph.node("control:7").control_type == "Button"
    assert graph.node("control:8").control_type == "InputField"
    assert graph.has_edge("ControlSite", "control:7", "contains-control")
    assert graph.has_edge("ControlSite", "control:8", "contains-control")
    assert ("control:7", "controls", CF_FORM_CONTROLS8_FORMAT_ID) in records
    assert ("control:7", "info", CF_FORM_CONTROLS_INFO8_FORMAT_ID) in records
    assert ("control:8", "controls", CF_FORM_CONTROLS8_FORMAT_ID) in records
    assert ("control:8", "info", CF_FORM_CONTROLS_INFO8_FORMAT_ID) in records


def test_platform_object_from_list_stream_text_exposes_typed_controls() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")

    platform_object = platform_object_from_list_stream_text(text)
    run = platform_object.control("control:7")
    input_field = platform_object.control_by_name("Input")

    assert platform_object.can_use_object_model
    assert platform_object.diagnostics == ()
    assert [control.name for control in platform_object.flatten_controls()] == ["Run", "Input"]
    assert run.control_type == "Button"
    assert run.title == "Run"
    assert run.supports_platform_format(CF_FORM_CONTROLS8_FORMAT_ID)
    assert run.supports_platform_format(CF_FORM_CONTROLS_INFO8_FORMAT_ID)
    assert input_field.control_type == "InputField"
    assert input_field.info_kind == "9"
    assert len(platform_object.persistence_by_family("controls")) == 2


def test_platform_object_title_does_not_use_event_titles() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <InputField name="Input" id="8">
                <Events>
                  <Event name="ПриИзменении" title="Input changed title">InputOnChange</Event>
                </Events>
              </InputField>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")

    platform_object = platform_object_from_list_stream_text(text)

    assert platform_object.control_by_name("Input").title == ""


def test_platform_object_roundtrips_back_to_list_stream_text() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")

    platform_object = platform_object_from_list_stream_text(text)
    emitted = platform_object.to_list_stream_text()
    reparsed = platform_object_from_list_stream_text(emitted)

    assert emitted == text
    assert [control.name for control in reparsed.flatten_controls()] == ["Run", "Input"]
    assert len(reparsed.persistence_by_family("controls")) == 2
    assert len(reparsed.persistence_by_family("info")) == 2
    assert reparsed.to_list_stream_text(include_bom=True).startswith("\ufeff{")


def test_platform_object_writes_control_name_and_title_to_bracket_stream() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)

    updated = platform_object.with_control_updates(
        "control:7",
        name="RunChanged",
        title="Run title changed",
    )
    emitted = updated.to_list_stream_text()
    reparsed = platform_object_from_list_stream_text(emitted)
    changed = reparsed.control("control:7")

    assert changed.name == "RunChanged"
    assert changed.title == "Run title changed"
    assert changed.supports_platform_format(CF_FORM_CONTROLS8_FORMAT_ID)
    assert changed.supports_platform_format(CF_FORM_CONTROLS_INFO8_FORMAT_ID)
    assert "RunChanged" in emitted
    assert "Run title changed" in emitted
    assert reparsed.control_by_name("RunChanged").node_id == "control:7"
    assert reparsed.control_by_name("Input").node_id == "control:8"


def test_platform_form_object_edits_control_identity_and_title_by_object_name() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    form = PlatformFormObject.from_list_stream_text(form_stream_from_object_xml(root).decode("utf-8-sig"))

    updated = form.rename_control("Run", "RunObject", title="Run object title")
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())

    assert [control.name for control in updated.controls()] == ["RunObject", "Input"]
    assert reparsed.control_by_name("RunObject").title == "Run object title"
    assert reparsed.control_by_name("Input").node_id == "control:8"


def test_platform_form_object_sets_input_field_property_by_platform_name() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    form = PlatformFormObject.from_list_stream_text(form_stream_from_object_xml(root).decode("utf-8-sig"))

    updated = form.set_control_property("Input", "ТолькоПросмотр", True)
    platform_object = updated.to_platform_object()
    public_xml = ordinary_form_xml_from_platform_object(platform_object)
    read_only = public_xml.find("./Pages/Page/InputField[@name='Input']/ReadOnly")

    assert updated.get_control_property("Input", "ReadOnly") == "true"
    assert read_only is not None
    assert read_only.text == "true"
    assert platform_object.control_by_name("Run").title == "Run"
    assert len(platform_object.flatten_controls()) == 2


def test_platform_form_object_sets_common_base_info_properties() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )
    form = PlatformFormObject.from_list_stream_text(form_stream_from_object_xml(root).decode("utf-8-sig"))

    updated = form.set_control_property("Run", "Visible", False)
    updated = updated.set_control_property("Run", "Enabled", False)
    updated = updated.set_control_property("Run", "baseStyleVisible", "0")
    platform_object = updated.to_platform_object()
    reparsed = platform_object_from_list_stream_text(platform_object.to_list_stream_text())
    public_xml = ordinary_form_xml_from_platform_object(reparsed)
    button = public_xml.find("./Pages/Page/Button[@name='Run']")

    assert updated.get_control_property("Run", "Visible") == "false"
    assert updated.get_control_property("Run", "Enabled") == "false"
    assert updated.get_control_property("Run", "baseStyleVisible") == "0"
    assert button is not None
    assert button.findtext("Visible") == "false"
    assert button.findtext("Enabled") == "false"
    assert button.get("baseStyleVisible") == "0"


def test_platform_form_object_rejects_unverified_property_writer() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )
    form = PlatformFormObject.from_list_stream_text(form_stream_from_object_xml(root).decode("utf-8-sig"))

    try:
        form.set_control_property("Run", "Кнопки", "0")
    except UnsupportedPlatformObjectOperation as error:
        assert "Button.Buttons" in str(error)
    else:
        raise AssertionError("Expected unverified object writer to fail")


def test_platform_object_xml_roundtrips_without_stream_payload() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)

    xml_text = platform_object_xml_to_string(platform_object)
    rematerialized = platform_object_from_xml_text(xml_text, platform_object)
    reparsed = platform_object_from_list_stream_text(rematerialized.to_list_stream_text())

    for forbidden in (
        "ObjectModel",
        "ListStream",
        "BracketStream",
        "FormBin",
        "LogicalStream",
        "RawBracket",
        "PlatformRecords",
        "base64",
    ):
        assert forbidden not in xml_text
    assert rematerialized.to_list_stream_text() == text
    assert [control.name for control in reparsed.flatten_controls()] == ["Run", "Input"]
    assert reparsed.control("control:7").title == "Run"


def test_platform_object_xml_edit_writes_control_name_and_title_to_bracket_stream() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    xml_root = platform_object_to_xml(platform_object)
    button = xml_root.find("./Controls/Control[@nodeId='control:7']")
    assert button is not None
    title = button.find("./Title/Item[@lang='ru']")
    assert title is not None
    button.set("name", "RunXml")
    title.text = "Run XML title"

    updated = platform_object_from_xml(xml_root, platform_object)
    emitted = updated.to_list_stream_text()
    reparsed = platform_object_from_list_stream_text(emitted)
    changed = reparsed.control("control:7")

    assert changed.name == "RunXml"
    assert changed.title == "Run XML title"
    assert changed.supports_platform_format(CF_FORM_CONTROLS8_FORMAT_ID)
    assert changed.supports_platform_format(CF_FORM_CONTROLS_INFO8_FORMAT_ID)
    assert reparsed.control_by_name("RunXml").node_id == "control:7"
    assert "RunXml" in emitted
    assert "Run XML title" in emitted


def test_platform_object_xml_edit_writes_common_properties_to_bracket_stream() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    xml_root = platform_object_to_xml(platform_object)
    button = xml_root.find("./Controls/Control[@nodeId='control:7']")
    input_field = xml_root.find("./Controls/Control[@nodeId='control:8']")
    assert button is not None
    assert input_field is not None
    ET.SubElement(button, "Visible").text = "false"
    ET.SubElement(button, "Enabled").text = "false"
    button.set("baseStyleVisible", "0")
    ET.SubElement(input_field, "ReadOnly").text = "true"

    updated = platform_object_from_xml(xml_root, platform_object)
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())
    public_xml = ordinary_form_xml_from_platform_object(reparsed)
    public_button = public_xml.find("./Pages/Page/Button[@name='Run']")
    public_input = public_xml.find("./Pages/Page/InputField[@name='Input']")

    assert reparsed.control_property("control:7", "Visible") == "false"
    assert reparsed.control_property("control:7", "Enabled") == "false"
    assert reparsed.control_property("control:7", "baseStyleVisible") == "0"
    assert reparsed.control_property("control:8", "ReadOnly") == "true"
    assert public_button is not None
    assert public_button.findtext("Visible") == "false"
    assert public_button.findtext("Enabled") == "false"
    assert public_button.get("baseStyleVisible") == "0"
    assert public_input is not None
    assert public_input.findtext("ReadOnly") == "true"


def test_platform_object_xml_edit_writes_color_and_font_to_bracket_stream() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    xml_root = platform_object_to_xml(platform_object)
    button = xml_root.find("./Controls/Control[@nodeId='control:7']")
    assert button is not None
    ET.SubElement(button, "BackColor", {"rgb": "#445566"})
    ET.SubElement(button, "TextColor", {"value": "-1", "recordKind": "4", "recordSubKind": "3", "tailKind": "3"})
    ET.SubElement(button, "BorderColor", {"value": "-22", "recordKind": "4", "recordSubKind": "3", "tailKind": "3"})
    ET.SubElement(button, "Font", {"kind": "8", "family": "3", "style": "0", "size": "120"})

    updated = platform_object_from_xml(xml_root, platform_object)
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())
    rematerialized = platform_object_to_xml(reparsed)
    public_xml = ordinary_form_xml_from_platform_object(reparsed)
    internal_button = rematerialized.find("./Controls/Control[@nodeId='control:7']")
    public_button = public_xml.find("./Pages/Page/Button[@name='Run']")

    assert internal_button is not None
    assert internal_button.find("BackColor").get("rgb") == "#445566"
    assert internal_button.find("TextColor").get("name") == "TextColor"
    assert internal_button.find("BorderColor").get("name") == "BorderColor"
    assert internal_button.find("Font").get("size") == "120"
    assert public_button is not None
    assert public_button.find("BackColor").get("rgb") == "#445566"
    assert public_button.find("TextColor").get("name") == "TextColor"
    assert public_button.find("BorderColor").get("name") == "BorderColor"
    assert public_button.find("Font").get("size") == "120"


def test_platform_object_xml_edit_writes_position_bindings_to_bracket_stream() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Anchor" id="7">
                <Title><Item lang="ru">Anchor</Item></Title>
                <Position left="20" top="20" right="120" bottom="45"/>
              </Button>
              <Button name="Follower" id="8">
                <Title><Item lang="ru">Follower</Item></Title>
                <Position left="150" top="20" right="250" bottom="45"/>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    xml_root = platform_object_to_xml(platform_object)
    follower = xml_root.find("./Controls/Control[@nodeId='control:8']")
    assert follower is not None
    position = follower.find("Position")
    assert position is not None
    position.set("left", "130")
    left_binding = position.find("./Bindings/Binding[@coordinate='left']")
    assert left_binding is not None
    left_binding[:] = []
    left_binding.set("mode", "0")
    ET.SubElement(
        left_binding,
        "From",
        {"relation": "targetEdgeOffset", "target": "element", "targetId": "7", "side": "right", "offset": "10"},
    )
    ET.SubElement(left_binding, "To", {"relation": "targetEdgeOffset", "target": "none", "side": "none", "offset": "0"})

    updated = platform_object_from_xml(xml_root, platform_object)
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())
    rematerialized = platform_object_to_xml(reparsed)
    public_xml = ordinary_form_xml_from_platform_object(reparsed)
    internal_binding = rematerialized.find(
        "./Controls/Control[@nodeId='control:8']/Position/Bindings/Binding[@coordinate='left']/From"
    )
    public_binding = public_xml.find("./Pages/Page/Button[@name='Follower']/Position/Bindings/Binding[@coordinate='left']/From")

    assert internal_binding is not None
    assert internal_binding.get("targetId") == "7"
    assert internal_binding.get("targetName") == "Anchor"
    assert internal_binding.get("side") == "right"
    assert internal_binding.get("offset") == "10"
    assert public_binding is not None
    assert public_binding.get("targetId") == "7"
    assert public_binding.get("targetName") == "Anchor"
    assert public_binding.get("side") == "right"
    assert public_binding.get("offset") == "10"
    assert public_xml.find("./Pages/Page/Button[@name='Follower']/Position").get("left") == "130"


def test_platform_object_xml_edit_computes_binding_offset_from_geometry() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Anchor" id="7">
                <Title><Item lang="ru">Anchor</Item></Title>
                <Position left="20" top="40" right="120" bottom="65"/>
              </Button>
              <Button name="Follower" id="8">
                <Title><Item lang="ru">Follower</Item></Title>
                <Position left="150" top="40" right="250" bottom="65"/>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    xml_root = platform_object_to_xml(platform_object)
    follower = xml_root.find("./Controls/Control[@nodeId='control:8']")
    assert follower is not None
    position = follower.find("Position")
    assert position is not None
    left_binding = position.find("./Bindings/Binding[@coordinate='left']")
    top_binding = position.find("./Bindings/Binding[@coordinate='top']")
    assert left_binding is not None
    assert top_binding is not None
    left_binding[:] = []
    left_binding.set("mode", "0")
    ET.SubElement(left_binding, "From", {"relation": "targetEdgeOffset", "target": "element", "targetId": "7", "side": "right"})
    ET.SubElement(left_binding, "To", {"relation": "targetEdgeOffset", "target": "none", "side": "none", "offset": "0"})
    top_binding[:] = []
    top_binding.set("mode", "0")
    ET.SubElement(top_binding, "From", {"relation": "targetEdgeOffset", "target": "element", "targetId": "7", "side": "bottom"})
    ET.SubElement(top_binding, "To", {"relation": "targetEdgeOffset", "target": "none", "side": "none", "offset": "0"})

    updated = platform_object_from_xml(xml_root, platform_object)
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())
    rematerialized = platform_object_to_xml(reparsed)
    left_from = rematerialized.find("./Controls/Control[@nodeId='control:8']/Position/Bindings/Binding[@coordinate='left']/From")
    top_from = rematerialized.find("./Controls/Control[@nodeId='control:8']/Position/Bindings/Binding[@coordinate='top']/From")

    assert left_from is not None
    assert left_from.get("targetName") == "Anchor"
    assert left_from.get("side") == "right"
    assert left_from.get("offset") == "30"
    assert top_from is not None
    assert top_from.get("targetName") == "Anchor"
    assert top_from.get("side") == "bottom"
    assert top_from.get("offset") == "-25"


def test_platform_object_xml_rejects_control_identity_changes() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    xml_root = platform_object_to_xml(platform_object)
    control = xml_root.find("./Controls/Control[@nodeId='control:7']")
    assert control is not None
    control.set("type", "InputField")

    try:
        platform_object_from_xml(xml_root, platform_object)
    except ValueError as error:
        assert "Immutable attribute type changed" in str(error)
    else:
        raise AssertionError("Expected platform object XML identity mismatch to fail")


def test_platform_object_materializes_public_xsd_form_xml(tmp_path: Path) -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)

    public_xml = ordinary_form_xml_from_platform_object(platform_object)
    xml_bytes = ordinary_form_xml_bytes_from_platform_object(platform_object)
    xml_path = tmp_path / "Form.xml"
    xml_path.write_bytes(xml_bytes)

    validate_xml_file(xml_path)
    assert public_xml.tag == "Form"
    assert public_xml.find("./Pages/Page/Button[@name='Run']") is not None
    assert public_xml.find("./Pages/Page/InputField[@name='Input']") is not None
    assert public_xml.find(".//Control") is None
    xml_text = xml_bytes.decode("utf-8")
    for forbidden in (
        "ObjectModel",
        "ListStream",
        "BracketStream",
        "FormBin",
        "LogicalStream",
        "RawBracket",
        "PlatformRecords",
    ):
        assert forbidden not in xml_text


def test_public_xsd_form_xml_dematerializes_name_and_title_to_platform_object() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    public_xml = ordinary_form_xml_from_platform_object(platform_object)
    button = public_xml.find("./Pages/Page/Button[@id='7']")
    assert button is not None
    title = button.find("./Title/Item[@lang='ru']")
    assert title is not None
    button.set("name", "RunPublicXml")
    title.text = "Run public XML title"

    updated = platform_object_from_ordinary_form_xml(public_xml, platform_object)
    emitted = updated.to_list_stream_text()
    reparsed = platform_object_from_list_stream_text(emitted)
    changed = reparsed.control("control:7")

    assert changed.name == "RunPublicXml"
    assert changed.title == "Run public XML title"
    assert "RunPublicXml" in emitted
    assert "Run public XML title" in emitted


def test_public_xsd_form_xml_dematerializes_common_properties_to_platform_object() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8"/>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    public_xml = ordinary_form_xml_from_platform_object(platform_object)
    button = public_xml.find("./Pages/Page/Button[@id='7']")
    input_field = public_xml.find("./Pages/Page/InputField[@id='8']")
    assert button is not None
    assert input_field is not None
    ET.SubElement(button, "Visible").text = "false"
    ET.SubElement(button, "Enabled").text = "false"
    button.set("baseStyleVisible", "0")
    ET.SubElement(input_field, "ReadOnly").text = "true"

    updated = platform_object_from_ordinary_form_xml(public_xml, platform_object)
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())
    rematerialized = ordinary_form_xml_from_platform_object(reparsed)
    public_button = rematerialized.find("./Pages/Page/Button[@name='Run']")
    public_input = rematerialized.find("./Pages/Page/InputField[@name='Input']")

    assert reparsed.control_property("control:7", "Visible") == "false"
    assert reparsed.control_property("control:7", "Enabled") == "false"
    assert reparsed.control_property("control:7", "baseStyleVisible") == "0"
    assert reparsed.control_property("control:8", "ReadOnly") == "true"
    assert public_button is not None
    assert public_button.findtext("Visible") == "false"
    assert public_button.findtext("Enabled") == "false"
    assert public_button.get("baseStyleVisible") == "0"
    assert public_input is not None
    assert public_input.findtext("ReadOnly") == "true"


def test_public_xsd_form_xml_dematerializes_color_and_font_to_platform_object() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    public_xml = ordinary_form_xml_from_platform_object(platform_object)
    button = public_xml.find("./Pages/Page/Button[@id='7']")
    assert button is not None
    ET.SubElement(button, "BackColor", {"rgb": "#112233"})
    ET.SubElement(button, "Font", {"kind": "8", "family": "3", "style": "0", "size": "140"})

    updated = platform_object_from_ordinary_form_xml(public_xml, platform_object)
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())
    rematerialized = ordinary_form_xml_from_platform_object(reparsed)
    public_button = rematerialized.find("./Pages/Page/Button[@name='Run']")

    assert public_button is not None
    assert public_button.find("BackColor").get("rgb") == "#112233"
    assert public_button.find("Font").get("size") == "140"


def test_public_xsd_form_xml_dematerializes_position_bindings_to_platform_object() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Anchor" id="7">
                <Title><Item lang="ru">Anchor</Item></Title>
                <Position left="20" top="20" right="120" bottom="45"/>
              </Button>
              <Button name="Follower" id="8">
                <Title><Item lang="ru">Follower</Item></Title>
                <Position left="150" top="20" right="250" bottom="45"/>
              </Button>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    public_xml = ordinary_form_xml_from_platform_object(platform_object)
    follower = public_xml.find("./Pages/Page/Button[@id='8']")
    assert follower is not None
    position = follower.find("Position")
    assert position is not None
    position.set("top", "32")
    top_binding = position.find("./Bindings/Binding[@coordinate='top']")
    assert top_binding is not None
    top_binding[:] = []
    top_binding.set("mode", "0")
    ET.SubElement(
        top_binding,
        "From",
        {"relation": "targetEdgeOffset", "target": "element", "targetId": "7", "side": "bottom", "offset": "5"},
    )
    ET.SubElement(top_binding, "To", {"relation": "targetEdgeOffset", "target": "none", "side": "none", "offset": "0"})

    updated = platform_object_from_ordinary_form_xml(public_xml, platform_object)
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())
    rematerialized = ordinary_form_xml_from_platform_object(reparsed)
    public_binding = rematerialized.find(
        "./Pages/Page/Button[@name='Follower']/Position/Bindings/Binding[@coordinate='top']/From"
    )

    assert public_binding is not None
    assert public_binding.get("targetId") == "7"
    assert public_binding.get("targetName") == "Anchor"
    assert public_binding.get("side") == "bottom"
    assert public_binding.get("offset") == "5"
    assert rematerialized.find("./Pages/Page/Button[@name='Follower']/Position").get("top") == "32"


def test_public_xsd_form_xml_computes_binding_offset_from_geometry() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Anchor" id="7">
                <Title><Item lang="ru">Anchor</Item></Title>
                <Position left="20" top="40" right="120" bottom="65"/>
              </Button>
              <InputField name="Follower" id="8">
                <Position left="20" top="90" right="250" bottom="115"/>
              </InputField>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    public_xml = ordinary_form_xml_from_platform_object(platform_object)
    follower = public_xml.find("./Pages/Page/InputField[@id='8']")
    assert follower is not None
    position = follower.find("Position")
    assert position is not None
    top_binding = position.find("./Bindings/Binding[@coordinate='top']")
    assert top_binding is not None
    top_binding[:] = []
    top_binding.set("mode", "0")
    ET.SubElement(top_binding, "From", {"relation": "targetEdgeOffset", "target": "element", "targetId": "7", "side": "bottom"})
    ET.SubElement(top_binding, "To", {"relation": "targetEdgeOffset", "target": "none", "side": "none", "offset": "0"})

    updated = platform_object_from_ordinary_form_xml(public_xml, platform_object)
    reparsed = platform_object_from_list_stream_text(updated.to_list_stream_text())
    rematerialized = ordinary_form_xml_from_platform_object(reparsed)
    public_binding = rematerialized.find(
        "./Pages/Page/InputField[@name='Follower']/Position/Bindings/Binding[@coordinate='top']/From"
    )

    assert public_binding is not None
    assert public_binding.get("targetId") == "7"
    assert public_binding.get("targetName") == "Anchor"
    assert public_binding.get("side") == "bottom"
    assert public_binding.get("offset") == "25"


def test_public_xsd_form_xml_full_rebuild_roundtrip_is_semantically_stable() -> None:
    root = ET.fromstring(
        """
        <Form>
          <Title><Item lang="ru">Main</Item></Title>
          <Pages>
            <Page name="Main">
              <Button name="Run" id="7">
                <Title><Item lang="ru">Run</Item></Title>
              </Button>
              <InputField name="Input" id="8">
                <ReadOnly>true</ReadOnly>
              </InputField>
            </Page>
          </Pages>
        </Form>
        """
    )
    text = form_stream_from_object_xml(root).decode("utf-8-sig")
    platform_object = platform_object_from_list_stream_text(text)
    public_xml = ordinary_form_xml_from_platform_object(platform_object)

    rebuilt = platform_object_from_ordinary_form_xml_rebuild(public_xml)
    rematerialized = ordinary_form_xml_from_platform_object(rebuilt)
    rebuilt_again = platform_object_from_ordinary_form_xml_rebuild(rematerialized)
    rematerialized_again = ordinary_form_xml_from_platform_object(rebuilt_again)

    assert rebuilt.control_by_name("Run").title == "Run"
    assert rebuilt.control_by_name("Input").control_type == "InputField"
    read_only = rematerialized.find("./Pages/Page/InputField[@name='Input']/ReadOnly")
    assert read_only is not None
    assert read_only.text == "true"
    assert semantic_graph_digest(rematerialized_again) == semantic_graph_digest(rematerialized)


def test_platform_palette_catalog_exposes_xsd_platform_property_descriptions() -> None:
    catalog = platform_palette_catalog()

    assert catalog["Button"]["platformName"] == "Кнопка"
    assert {"name": "Заголовок", "type": "Строка"} in catalog["Button"]["properties"]
    assert {"name": "Нажатие"} in catalog["Button"]["events"]
    assert any(item["name"] == "ТолькоПросмотр" for item in catalog["InputField"]["properties"])


def test_platform_object_from_model_reports_child_count_mismatch() -> None:
    model = OrdinaryFormModel(
        [
            OrdinaryControl(
                class_id="09ccdc77-ea1a-4a6d-ab1c-3435eada2433",
                object_id="10",
                name="Panel1",
                type="Panel",
                title="",
                raw=[],
                declared_child_count=2,
                children=[],
                info_kind="1",
            )
        ]
    )

    platform_object = platform_object_from_model([], model)

    assert not platform_object.can_use_object_model
    assert len(platform_object.diagnostics) == 1
    assert platform_object.diagnostics[0].code == "child-count-mismatch"
    assert platform_object.diagnostics[0].node_id == "control:10"


def test_all_controls_skeleton_graph_covers_known_platform_control_classes() -> None:
    graph = all_controls_skeleton_graph()
    control_types = {node.control_type for node in graph.control_nodes()}

    assert control_types == set(ORDINARY_CONTROL_CLASS_BY_GUID.values())
    assert len(graph.control_nodes()) == len(ORDINARY_CONTROL_CLASS_BY_GUID)
    assert len(graph.persistence_by_family("controls")) == len(ORDINARY_CONTROL_CLASS_BY_GUID)
    assert len(graph.persistence_by_family("position")) == len(ORDINARY_CONTROL_CLASS_BY_GUID)
    assert len(graph.persistence_by_family("info")) == len(ORDINARY_CONTROL_CLASS_BY_GUID)
