"""Tools for decomposing and rebuilding 1C ordinary forms."""

from __future__ import annotations

from pathlib import Path

from onec_ordinary_forms.native_bridge import (
    NativeBridgeError,
    assert_payload_lossless,
    build_formbin_xml,
    dump_formbin_xml,
    formbin_roundtrip_report,
    native_binary,
    platform_object_report,
)
from onec_ordinary_forms.public_contract import PUBLIC_FORM_VERSION
from onec_ordinary_forms.semantic_digest import semantic_graph, semantic_graph_digest, semantic_graph_from_xml

__all__ = [
    "__version__",
    "NativeBridgeError",
    "assert_payload_lossless",
    "build_formbin_xml",
    "build_form_bin",
    "dump_formbin_xml",
    "dump_form_bin",
    "formbin_roundtrip_report",
    "native_binary",
    "platform_object_report",
    "semantic_graph",
    "semantic_graph_digest",
    "semantic_graph_from_xml",
    "validate_form_xml",
    "PUBLIC_FORM_VERSION",
]

__version__ = "0.4.6"


def dump_form_bin(form_bin: str | Path, out_xml: str | Path, metadata_json: str | Path | None = None) -> None:
    """Dump ordinary ``Form.bin`` into public ``Form.xml`` plus sidecars."""

    if metadata_json is not None:
        raise RuntimeError("metadata_json belonged to the removed Python dump path")
    dump_formbin_xml(Path(form_bin), Path(out_xml))


def build_form_bin(
    xml: str | Path,
    out_bin: str | Path,
    base_bin: str | Path,
    *,
    native_bin: str | Path | None = None,
    native_lossless_check: bool = True,
) -> None:
    """Build ordinary ``Form.bin`` by applying public ``Form.xml`` to a native baseline."""

    build_formbin_xml(Path(base_bin), Path(xml), Path(out_bin), binary=Path(native_bin) if native_bin is not None else None)
    if native_lossless_check:
        assert_payload_lossless(Path(out_bin))


def validate_form_xml(xml: str | Path, schema: str | Path | None = None) -> None:
    """Validate ordinary form object XML against the bundled XSD schema."""

    from onec_ordinary_forms.cli import validate_xml_file

    validate_xml_file(Path(xml), Path(schema) if schema is not None else None)
