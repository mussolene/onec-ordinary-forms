"""High-level ordinary form conversion pipelines.

This module owns orchestration between lower-level formats. It should not know
how object-model XML is written internally; callers provide that writer.
"""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path
import tempfile
from typing import Optional

from onec_ordinary_forms.bracket import write_elem_json_from_bracket
from onec_ordinary_forms.formbin import unpack_form_bin


ModelXmlWriter = Callable[[Path, Path, Optional[Path], Path, Optional[Path], Path], None]


def dump_form_bin_to_xml(
    form_bin: Path,
    out_xml: Path,
    *,
    model_xml_writer: ModelXmlWriter,
    metadata_json: Path | None = None,
) -> None:
    """Convert ordinary ``Form.bin`` into object-model XML and sidecars.

    ``Form.bin`` is first split into its platform sections. The bracket stream
    section is converted into the legacy element index, then the supplied model
    XML writer emits the editable package at ``out_xml``.
    """

    with tempfile.TemporaryDirectory(prefix="onec-ordinary-form-") as temp_dir:
        work_dir = Path(temp_dir)
        unpack_form_bin(form_bin, work_dir)
        form_stream = work_dir / "Form.xml"
        elem_json = work_dir / "elem.json"
        write_elem_json_from_bracket(form_stream, elem_json)
        module = work_dir / "Module.bsl"
        model_xml_writer(
            form_stream,
            form_bin,
            module if module.exists() else None,
            elem_json,
            metadata_json,
            out_xml,
        )
