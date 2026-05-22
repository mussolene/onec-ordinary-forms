"""Corpus scanning helpers for external 1C processors and reports."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import json
import re


EXTERNAL_FORM_SUFFIXES = {".epf", ".erf"}

ORDINARY_NAME_MARKERS = (
    "обычн",
    "obychn",
    "ordinary",
    "8.1",
    "8.2",
    "v81",
    "v82",
)

MANAGED_NAME_MARKERS = (
    "управляем",
    "upravlyaem",
    "managed",
    "такси",
    "taxi",
)

PICTURE_SUFFIXES = {".bmp", ".gif", ".jpg", ".jpeg", ".png", ".ico"}


@dataclass(frozen=True)
class ExternalFile:
    """A found EPF/ERF file with only portable metadata."""

    path: Path
    root: Path

    @property
    def relative_path(self) -> str:
        return self.path.relative_to(self.root).as_posix()

    @property
    def kind(self) -> str:
        suffix = self.path.suffix.lower()
        if suffix == ".epf":
            return "externalDataProcessor"
        if suffix == ".erf":
            return "externalReport"
        return "unknown"

    @property
    def score(self) -> int:
        name = self.path.name.lower()
        score = 0
        score += sum(3 for marker in ORDINARY_NAME_MARKERS if marker in name)
        score += sum(1 for marker in MANAGED_NAME_MARKERS if marker in name)
        return score

    def to_dict(self) -> dict[str, object]:
        return {
            "file": self.relative_path,
            "name": self.path.name,
            "kind": self.kind,
            "size": self.path.stat().st_size,
            "candidateScore": self.score,
        }


@dataclass
class ExportedForm:
    """Classification for a platform-exported form directory."""

    object_name: str
    form_name: str
    ordinary_stream: bool = False
    ordinary_bin: bool = False
    managed_xml: bool = False
    module: bool = False
    picture_files: list[str] = field(default_factory=list)
    form_stream_size: int = 0
    form_bin_size: int = 0

    @property
    def classification(self) -> str:
        if self.ordinary_stream and self.ordinary_bin:
            return "ordinary"
        if self.managed_xml:
            return "managed"
        if self.ordinary_stream:
            return "ordinary-partial"
        return "unknown"

    def to_dict(self) -> dict[str, object]:
        return {
            "object": self.object_name,
            "form": self.form_name,
            "classification": self.classification,
            "hasFormStream": self.ordinary_stream,
            "hasFormBin": self.ordinary_bin,
            "hasManagedXml": self.managed_xml,
            "hasModule": self.module,
            "pictureFiles": self.picture_files,
            "formStreamSize": self.form_stream_size,
            "formBinSize": self.form_bin_size,
        }


def iter_external_files(root: Path) -> list[ExternalFile]:
    root = root.resolve()
    files = [
        ExternalFile(path=path, root=root)
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in EXTERNAL_FORM_SUFFIXES
    ]
    return sorted(files, key=lambda item: (-item.score, item.relative_path.lower()))


def filter_external_files(
    files: list[ExternalFile],
    name_regex: str | None,
    limit: int | None,
) -> list[ExternalFile]:
    if name_regex:
        pattern = re.compile(name_regex, re.IGNORECASE)
        files = [item for item in files if pattern.search(item.relative_path)]
    if limit is not None:
        files = files[:limit]
    return files


def classify_exported_forms(exported_root: Path) -> list[ExportedForm]:
    forms: list[ExportedForm] = []
    for form_dir in sorted(exported_root.glob("**/Forms/*/Ext/Form")):
        if not form_dir.is_dir():
            continue
        try:
            object_name = form_dir.parents[3].name
        except IndexError:
            object_name = ""
        form_name = form_dir.parents[1].name
        stream = form_dir / "form"
        form_bin = form_dir / "Form.bin"
        managed_xml = form_dir.parent / "Form.xml"
        module = form_dir / "Module.bsl"
        picture_files = [
            item.relative_to(form_dir).as_posix()
            for item in sorted(form_dir.rglob("*"))
            if item.is_file() and item.suffix.lower() in PICTURE_SUFFIXES
        ]
        forms.append(
            ExportedForm(
                object_name=object_name,
                form_name=form_name,
                ordinary_stream=stream.is_file(),
                ordinary_bin=form_bin.is_file(),
                managed_xml=managed_xml.is_file(),
                module=module.is_file(),
                picture_files=picture_files,
                form_stream_size=stream.stat().st_size if stream.is_file() else 0,
                form_bin_size=form_bin.stat().st_size if form_bin.is_file() else 0,
            )
        )
    return forms


def build_corpus_report(
    root: Path,
    name_regex: str | None = None,
    limit: int | None = None,
    exported_root: Path | None = None,
) -> dict[str, object]:
    all_files = iter_external_files(root)
    selected_files = filter_external_files(all_files, name_regex, limit)
    forms = classify_exported_forms(exported_root) if exported_root else []
    return {
        "root": "<input-root>",
        "totalExternalFiles": len(all_files),
        "selectedExternalFiles": len(selected_files),
        "files": [item.to_dict() for item in selected_files],
        "exportedForms": [item.to_dict() for item in forms],
        "summary": {
            "ordinaryForms": sum(1 for item in forms if item.classification == "ordinary"),
            "managedForms": sum(1 for item in forms if item.classification == "managed"),
            "formsWithPictures": sum(1 for item in forms if item.picture_files),
            "formsWithModules": sum(1 for item in forms if item.module),
        },
    }


def write_report(report: dict[str, object], out: Path | None) -> None:
    data = json.dumps(report, ensure_ascii=False, indent=2)
    if out is None:
        print(data)
        return
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(data + "\n", encoding="utf-8")
