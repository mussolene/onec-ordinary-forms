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
    assert summary["xsdControls"] == summary["paletteControls"] == 27
    assert summary["legacyWriterBranches"] == 0
    assert summary["writerBranchesWithoutXsdControl"] == []
    assert summary["controlsWithoutWriterDescriptor"] == ["PeriodChooser"]
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
    assert summary["controlsWithoutSharedInfoDescriptor"] == ["PeriodChooser"]


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
