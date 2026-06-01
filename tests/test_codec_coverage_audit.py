import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("audit_codec_coverage", ROOT / "tools/audit_codec_coverage.py")
assert SPEC is not None and SPEC.loader is not None
audit_codec_coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit_codec_coverage)


def test_codec_coverage_audit_tracks_writer_and_descriptor_gap() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )

    summary = report["summary"]
    assert summary["xsdControls"] == summary["paletteControls"] == 26
    assert summary["legacyWriterBranches"] == 0
    assert summary["writerFallbackTokens"] == []
    assert summary["writerBranchesWithoutXsdControl"] == []
    assert summary["controlsWithoutWriterDescriptor"] == []
    assert "ActiveXControl" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "Button" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "Chart" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "CheckBox" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "ChoiceField" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "CalendarField" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "Dendrogram" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "GanttChart" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "GeographicalSchemaField" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "GraphicalSchemaField" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "GroupBox" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "HTMLDocumentField" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "LabelDecoration" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "ListBox" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "PictureDecoration" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "PivotChart" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "ProgressBar" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "RadioButton" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "Splitter" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "SpreadsheetDocumentField" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "TextDocumentField" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert "TrackBar" not in summary["controlsWithoutSharedInfoDescriptor"]
    assert summary["controlsWithoutSharedInfoDescriptor"] == []


def test_codec_coverage_audit_normalizes_public_control_aliases() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )
    controls = {item["control"]: item for item in report["controls"]}

    assert controls["LabelDecoration"]["streamControl"] == "Label"
    assert controls["LabelDecoration"]["writerDescriptor"] is True
    assert controls["LabelDecoration"]["sharedInfoDescriptor"] is True
    assert controls["PictureDecoration"]["streamControl"] == "Image"
    assert controls["PictureDecoration"]["writerDescriptor"] is True
    assert controls["PictureDecoration"]["sharedInfoDescriptor"] is True


def test_command_bar_command_source_is_public_descriptor_property() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )
    command_bar = {item["control"]: item for item in report["controls"]}["CommandBar"]

    assert "CommandSource" not in command_bar["xsdOnlyProperties"]
    assert "Buttons" not in command_bar["xsdOnlyProperties"]
    assert "SerializationProfile" not in command_bar["xsdOnlyProperties"]


def test_picture_decoration_picture_style_is_public_descriptor_property() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )
    picture = {item["control"]: item for item in report["controls"]}["PictureDecoration"]

    assert "PictureStyle" not in picture["xsdOnlyProperties"]
    assert "SerializationProfile" not in picture["xsdOnlyProperties"]


def test_panel_layout_is_public_descriptor_property() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )
    panel = {item["control"]: item for item in report["controls"]}["Panel"]

    assert "PanelLayout" not in panel["xsdOnlyProperties"]
    assert "SerializationProfile" not in panel["xsdOnlyProperties"]


def test_core_controls_have_no_xsd_only_public_properties() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )
    controls = {item["control"]: item for item in report["controls"]}

    for control in ("CommandBar", "InputField", "Panel", "Table"):
        assert controls[control]["xsdOnlyProperties"] == []


def test_codec_coverage_audit_tracks_platform_property_name_mapping_matrix() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )
    summary = report["summary"]
    matrix = {
        (item["control"], item["platformName"]): item
        for item in report["propertyMatrix"]
    }

    assert summary["platformPropertyRows"] == 417
    assert summary["mappedPlatformPropertyRows"] == summary["platformPropertyRows"]
    assert summary["unmappedPlatformProperties"] == []
    assert matrix[("Button", "Заголовок")]["xmlName"] == "Title"
    assert matrix[("InputField", "ТолькоПросмотр")]["xmlName"] == "ReadOnly"
    assert matrix[("InputField", "ТолькоПросмотр")]["status"] == "mapped-descriptor"
    assert matrix[("CommandBar", "АвтоЗаполнение")]["xmlName"] == "Autofill"
    assert matrix[("Table", "ФиксацияСлева")]["xmlName"] == "LeftFixedColumns"


def test_codec_coverage_audit_tracks_platform_event_name_mapping_matrix() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )
    summary = report["summary"]
    matrix = {
        (item["control"], item["platformName"]): item
        for item in report["eventMatrix"]
    }

    assert summary["platformEventRows"] == 79
    assert summary["eventsWithoutPublicXml"] == []
    assert matrix[("Button", "Нажатие")]["xmlName"] == "Нажатие"
    assert matrix[("InputField", "ПриИзменении")]["status"] == "mapped-xsd"
