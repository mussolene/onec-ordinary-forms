"""Tools for decomposing and rebuilding 1C ordinary forms."""

from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace

from onec_ordinary_forms.cli import build_bin, dump_bin, validate_xml_file

__all__ = [
    "__version__",
    "build_form_bin",
    "dump_form_bin",
    "validate_form_xml",
]

__version__ = "0.3.0"


def dump_form_bin(form_bin: str | Path, out_xml: str | Path, metadata_json: str | Path | None = None) -> None:
    """Dump ordinary ``Form.bin`` into public ``Form.xml`` plus sidecars."""

    dump_bin(
        SimpleNamespace(
            bin=str(form_bin),
            out=str(out_xml),
            metadata_json=None if metadata_json is None else str(metadata_json),
        )
    )


def build_form_bin(xml: str | Path, out_bin: str | Path, asset_root: str | Path | None = None) -> None:
    """Build ordinary ``Form.bin`` from public ``Form.xml`` and sidecars."""

    build_bin(
        SimpleNamespace(
            xml=str(xml),
            out_bin=str(out_bin),
            asset_root=None if asset_root is None else str(asset_root),
        )
    )


def validate_form_xml(xml: str | Path, schema: str | Path | None = None) -> None:
    """Validate ordinary form object XML against the bundled XSD schema."""

    validate_xml_file(Path(xml), Path(schema) if schema is not None else None)
