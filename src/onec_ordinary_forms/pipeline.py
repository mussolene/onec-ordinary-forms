"""High-level ordinary form conversion pipelines.

This module owns orchestration between lower-level formats. It should not know
how object-model XML is written internally; callers provide that writer.
"""

from __future__ import annotations

from pathlib import Path
import tempfile
from typing import Protocol

from onec_ordinary_forms.bracket import extract_control_index_from_bracket
from onec_ordinary_forms.formbin import unpack_form_bin


class ModelXmlWriter(Protocol):
    def __call__(
        self,
        form_path: Path,
        module_path: Path | None,
        control_index: dict[str, object],
        metadata_json: Path | None,
        out_xml: Path,
    ) -> None: ...


def dump_form_bin_to_xml(
    form_bin: Path,
    out_xml: Path,
    *,
    model_xml_writer: ModelXmlWriter,
    metadata_json: Path | None = None,
) -> None:
    """Convert ordinary ``Form.bin`` into object-model XML and sidecars.

    ``Form.bin`` is split into its platform sections, then the form
    list-stream is decoded into the internal control index used by the current
    object XML writer. The public output is always the editable ``Form.xml``
    object model plus sidecars.
    """

    with tempfile.TemporaryDirectory(prefix="onec-ordinary-form-") as temp_dir:
        work_dir = Path(temp_dir)
        unpack_form_bin(form_bin, work_dir)
        form_stream = work_dir / "Form.xml"
        form_text = form_stream.read_text(encoding="utf-8-sig")
        element_index = extract_control_index_from_bracket(form_text)
        module = work_dir / "Module.bsl"
        model_xml_writer(
            form_stream,
            module if module.exists() else None,
            element_index,
            metadata_json,
            out_xml,
        )
