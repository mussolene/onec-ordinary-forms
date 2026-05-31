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
    assert summary["writerBranchesWithoutXsdControl"] == []
    assert summary["controlsWithoutWriterBranch"] == ["PeriodChooser"]
    assert set(summary["controlsWithoutInfoDescriptor"]) >= {"ActiveXControl", "Button", "PeriodChooser"}


def test_codec_coverage_audit_normalizes_public_control_aliases() -> None:
    report = audit_codec_coverage.audit(
        ROOT / "src/onec_ordinary_forms/schemas/OrdinaryForm.xsd",
        ROOT / "src/onec_ordinary_forms/ordinary_stream.py",
    )
    controls = {item["control"]: item for item in report["controls"]}

    assert controls["LabelDecoration"]["streamControl"] == "Label"
    assert controls["LabelDecoration"]["writerBranch"] == "label_control_info"
    assert controls["PictureDecoration"]["streamControl"] == "Image"
    assert controls["PictureDecoration"]["writerBranch"] == "image_control_info"
