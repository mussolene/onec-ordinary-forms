import xml.etree.ElementTree as ET

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
    runtime_skeleton_graph,
)
from onec_ordinary_forms.ordinary_stream import form_stream_from_object_xml


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


def test_all_controls_skeleton_graph_covers_known_platform_control_classes() -> None:
    graph = all_controls_skeleton_graph()
    control_types = {node.control_type for node in graph.control_nodes()}

    assert control_types == set(ORDINARY_CONTROL_CLASS_BY_GUID.values())
    assert len(graph.control_nodes()) == len(ORDINARY_CONTROL_CLASS_BY_GUID)
    assert len(graph.persistence_by_family("controls")) == len(ORDINARY_CONTROL_CLASS_BY_GUID)
    assert len(graph.persistence_by_family("position")) == len(ORDINARY_CONTROL_CLASS_BY_GUID)
    assert len(graph.persistence_by_family("info")) == len(ORDINARY_CONTROL_CLASS_BY_GUID)
