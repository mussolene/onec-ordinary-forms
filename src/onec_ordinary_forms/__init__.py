"""Tools for decomposing and rebuilding 1C ordinary forms."""

from __future__ import annotations

from pathlib import Path
import xml.etree.ElementTree as ET

from onec_ordinary_forms.formbin import build_form_bin_container
from onec_ordinary_forms.native_bridge import (
    NativeBridgeError,
    assert_payload_lossless,
    build_formbin_package,
    dump_formbin_package,
    formbin_roundtrip_report,
    native_binary,
    platform_object_report,
)
from onec_ordinary_forms.ordinary_stream import form_stream_from_object_xml
from onec_ordinary_forms.public_contract import PUBLIC_FORM_VERSION
from onec_ordinary_forms.semantic_digest import semantic_graph, semantic_graph_digest, semantic_graph_from_xml

__all__ = [
    "__version__",
    "NativeBridgeError",
    "assert_payload_lossless",
    "build_formbin_package",
    "build_form_bin",
    "dump_formbin_package",
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


def dump_form_bin(form_bin: str | Path, out_xml: str | Path) -> None:
    """Dump ordinary ``Form.bin`` into the public native package."""

    dump_formbin_package(Path(form_bin), Path(out_xml))


def build_form_bin(
    xml: str | Path,
    out_bin: str | Path,
    base_bin: str | Path | None = None,
    *,
    asset_root: str | Path | None = None,
    native_bin: str | Path | None = None,
    native_lossless_check: bool = True,
) -> None:
    """Build ordinary ``Form.bin`` from the public package.

    When ``base_bin`` is provided, use the native compatibility patch path.
    Without ``base_bin``, serialize public ``Form.xml`` directly to the platform
    list stream and assemble a new ``Form.bin`` container.
    """

    xml_path = Path(xml)
    out_path = Path(out_bin)
    if base_bin is not None:
        build_formbin_package(Path(base_bin), xml_path, out_path, binary=Path(native_bin) if native_bin is not None else None)
        if native_lossless_check:
            assert_payload_lossless(out_path)
        return

    package_root = Path(asset_root) if asset_root is not None else xml_path.with_suffix("")
    form_payload = form_stream_from_object_xml(ET.parse(xml_path).getroot(), package_root)
    module_path = package_root / "Module.bsl"
    module_payload = module_path.read_bytes() if module_path.exists() else b""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(build_form_bin_container(form_payload, module_payload))


def validate_form_xml(xml: str | Path, schema: str | Path | None = None) -> None:
    """Validate ordinary form object XML against the bundled XSD schema."""

    from onec_ordinary_forms.cli import validate_xml_file

    validate_xml_file(Path(xml), Path(schema) if schema is not None else None)
