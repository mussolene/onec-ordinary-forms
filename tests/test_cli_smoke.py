import unittest
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from onec_ordinary_forms import __version__
from onec_ordinary_forms.corpus import build_corpus_report, classify_exported_forms
from onec_ordinary_forms.cli import replace_root_title
from onec_ordinary_forms.formbin import pack_form_bin, unpack_form_bin


class CliSmokeTest(unittest.TestCase):
    def test_version_is_present(self) -> None:
        self.assertRegex(__version__, r"^\d+\.\d+\.\d+$")

    def test_replace_root_title_updates_first_title_only(self) -> None:
        source = '{"ru","Old"}\n{"ru","Other"}\n'
        result = replace_root_title(source, "New")
        self.assertEqual(result, '{"ru","New"}\n{"ru","Other"}\n')

    def test_scan_corpus_uses_portable_paths(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "nested" / "sample_obychnaya.epf"
            source.parent.mkdir()
            source.write_bytes(b"epf")

            report = build_corpus_report(root)

        self.assertEqual(report["totalExternalFiles"], 1)
        self.assertEqual(report["files"][0]["file"], "nested/sample_obychnaya.epf")
        self.assertEqual(report["files"][0]["kind"], "externalDataProcessor")
        self.assertGreater(report["files"][0]["candidateScore"], 0)
        self.assertEqual(report["root"], "<input-root>")

    def test_classify_exported_ordinary_form(self) -> None:
        with TemporaryDirectory() as temp_dir:
            form_dir = Path(temp_dir) / "ExternalDataProcessors" / "Tool" / "Forms" / "Form" / "Ext" / "Form"
            form_dir.mkdir(parents=True)
            (form_dir / "form").write_text('{"ru","Title"}', encoding="utf-8")
            (form_dir / "Form.bin").write_bytes(b"bin")
            (form_dir / "Module.bsl").write_text("", encoding="utf-8")
            picture = form_dir / "Items" / "Image" / "Picture.gif"
            picture.parent.mkdir(parents=True)
            picture.write_bytes(b"GIF89a")

            forms = classify_exported_forms(Path(temp_dir))

        self.assertEqual(len(forms), 1)
        self.assertEqual(forms[0].classification, "ordinary")
        self.assertTrue(forms[0].module)
        self.assertEqual(forms[0].picture_files, ["Items/Image/Picture.gif"])

    def test_form_bin_unpack_pack_round_trip(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            source.write_bytes(
                b"HEAD\r\n"
                b"00000003 00000003 7fffffff \r\none"
                b"\r\n00000003 00000003 7fffffff \r\ntwo"
                b"\r\n00000005 00000005 7fffffff \r\nthree"
                b"\r\n00000006 00000006 7fffffff \r\nmodule"
                b"\r\n00000004 00000004 7fffffff \r\nform"
            )

            parts = root / "parts"
            unpack_form_bin(source, parts)
            rebuilt = root / "rebuilt.bin"
            pack_form_bin(parts, rebuilt)

            self.assertEqual(rebuilt.read_bytes(), source.read_bytes())
            self.assertEqual((parts / "Module.bsl").read_bytes(), b"module")
            self.assertEqual((parts / "Form.xml").read_bytes(), b"form")

    def test_committed_elem_json_fixture_documents_legacy_shape(self) -> None:
        fixture = Path(__file__).parents[1] / "examples" / "elem-json" / "minimal.json"
        data = json.loads(fixture.read_text(encoding="utf-8"))

        self.assertIn("props", data)
        self.assertIn("commands", data)
        self.assertIn("data", data)
        self.assertIn("tree", data)
        self.assertIn("-pages-", data["data"])


if __name__ == "__main__":
    unittest.main()
