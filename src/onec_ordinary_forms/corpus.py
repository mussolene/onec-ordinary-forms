"""Corpus scanning helpers for external 1C processors and reports."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import json
import re
import xml.etree.ElementTree as ET

from onec_ordinary_forms.semantic_digest import semantic_graph_from_xml


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
    public_form_xml: bool = False
    picture_files: list[str] = field(default_factory=list)
    form_stream_size: int = 0
    form_bin_size: int = 0
    semantic_digest: dict[str, object] | None = None
    comparison_semantic_digest: dict[str, object] | None = None

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
        result: dict[str, object] = {
            "object": self.object_name,
            "form": self.form_name,
            "classification": self.classification,
            "hasFormStream": self.ordinary_stream,
            "hasFormBin": self.ordinary_bin,
            "hasManagedXml": self.managed_xml,
            "hasModule": self.module,
            "hasPublicFormXml": self.public_form_xml,
            "pictureFiles": self.picture_files,
            "formStreamSize": self.form_stream_size,
            "formBinSize": self.form_bin_size,
        }
        if self.semantic_digest is not None:
            result["semanticDigest"] = self.semantic_digest
        if self.comparison_semantic_digest is not None:
            result["comparisonSemanticDigest"] = self.comparison_semantic_digest
        return result


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


def classify_exported_forms(
    exported_root: Path,
    *,
    include_semantic_digest: bool = False,
    compare_exported_root: Path | None = None,
) -> list[ExportedForm]:
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
        public_form_xml = form_dir.parent / "Form.xml"
        semantic_digest = (
            exported_form_semantic_digest(public_form_xml)
            if include_semantic_digest or compare_exported_root is not None
            else None
        )
        comparison_semantic_digest = None
        if compare_exported_root is not None:
            comparison_form_xml = compare_exported_root / form_dir.relative_to(exported_root).parent / "Form.xml"
            comparison_semantic_digest = compare_semantic_digest(semantic_digest, comparison_form_xml)
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
                public_form_xml=public_form_xml.is_file(),
                picture_files=picture_files,
                form_stream_size=stream.stat().st_size if stream.is_file() else 0,
                form_bin_size=form_bin.stat().st_size if form_bin.is_file() else 0,
                semantic_digest=semantic_digest,
                comparison_semantic_digest=comparison_semantic_digest,
            )
        )
    return forms


def build_corpus_report(
    root: Path,
    name_regex: str | None = None,
    limit: int | None = None,
    exported_root: Path | None = None,
    include_semantic_digest: bool = False,
    compare_exported_root: Path | None = None,
) -> dict[str, object]:
    all_files = iter_external_files(root)
    selected_files = filter_external_files(all_files, name_regex, limit)
    forms = (
        classify_exported_forms(
            exported_root,
            include_semantic_digest=include_semantic_digest or compare_exported_root is not None,
            compare_exported_root=compare_exported_root,
        )
        if exported_root
        else []
    )
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
            "formsWithSemanticDigest": sum(1 for item in forms if digest_status(item.semantic_digest) == "ok"),
            "semanticDigestInvalid": sum(1 for item in forms if digest_status(item.semantic_digest) == "invalid"),
            "semanticDigestDifferences": sum(
                1 for item in forms if digest_status(item.comparison_semantic_digest) == "different"
            ),
        },
    }


def exported_form_semantic_digest(form_xml: Path) -> dict[str, object]:
    if not form_xml.is_file():
        return {"status": "missing"}
    try:
        graph = semantic_graph_from_xml(form_xml)
    except (OSError, ET.ParseError, ValueError):
        return {"status": "invalid"}
    return {
        "status": "ok",
        "hash": graph["hash"],
        "summary": graph["summary"],
    }


def compare_semantic_digest(
    source_digest: dict[str, object] | None,
    comparison_form_xml: Path,
) -> dict[str, object]:
    comparison_digest = exported_form_semantic_digest(comparison_form_xml)
    if digest_status(source_digest) != "ok":
        return {"status": "sourceUnavailable", "targetStatus": digest_status(comparison_digest)}
    if digest_status(comparison_digest) != "ok":
        return {"status": "targetUnavailable", "targetStatus": digest_status(comparison_digest)}
    return {
        "status": "equal" if source_digest.get("hash") == comparison_digest.get("hash") else "different",
        "hash": comparison_digest["hash"],
        "summary": comparison_digest["summary"],
    }


def digest_status(digest: dict[str, object] | None) -> str:
    if digest is None:
        return "notRequested"
    return str(digest.get("status", "unknown"))


def write_report(report: dict[str, object], out: Path | None) -> None:
    data = json.dumps(report, ensure_ascii=False, indent=2)
    if out is None:
        print(data)
        return
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(data + "\n", encoding="utf-8")
