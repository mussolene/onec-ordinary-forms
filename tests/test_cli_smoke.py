import unittest
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from onec_ordinary_forms import __version__
from onec_ordinary_forms.corpus import build_corpus_report, classify_exported_forms
from onec_ordinary_forms.cli import replace_root_title
from onec_ordinary_forms.formbin import pack_form_bin, unpack_form_bin
from onec_ordinary_forms.bracket import extract_elem_json_from_bracket
from onec_ordinary_forms.pipeline import dump_form_bin_to_xml


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

    def test_form_bin_unpack_assembles_descriptor_split_streams(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            form_head = b'\xef\xbb\xbf{1,'
            form_tail = b'2}'
            module = b'\xef\xbb\xbf// module\r\n'
            source.write_bytes(
                b"HEAD\r\n"
                b"00000003 00000003 7fffffff \r\none"
                b"\r\n00000020 00000020 7fffffff \r\n"
                + b"\x00" * 16
                + b"f\x00o\x00r\x00m\x00"
                + b"\x00" * 8
                + f"\r\n{len(form_head):08x} {len(form_head):08x} 7fffffff \r\n".encode("ascii")
                + form_head
                + b"\r\n00000024 00000024 7fffffff \r\n"
                + b"\x00" * 16
                + b"m\x00o\x00d\x00u\x00l\x00e\x00"
                + b"\x00" * 8
                + f"\r\n{len(module):08x} {len(module):08x} 7fffffff \r\n".encode("ascii")
                + module
                + f"\r\n{len(form_tail):08x} {len(form_tail):08x} 7fffffff \r\n".encode("ascii")
                + form_tail
            )

            parts = root / "parts"
            unpack_form_bin(source, parts)
            rebuilt = root / "rebuilt.bin"
            pack_form_bin(parts, rebuilt)

            self.assertEqual(rebuilt.read_bytes(), source.read_bytes())
            self.assertEqual((parts / "Form.xml").read_bytes(), form_head + form_tail)
            self.assertEqual((parts / "Module.bsl").read_bytes(), module)

    def test_form_bin_unpack_detects_bom_bracket_with_invalid_text_bytes(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            form = b'\xef\xbb\xbf{1,"\xff"}'
            module = b'\xef\xbb\xbf// module\r\n'
            source.write_bytes(
                b"HEAD\r\n"
                b"00000003 00000003 7fffffff \r\none"
                b"\r\n00000020 00000020 7fffffff \r\n"
                + b"\x00" * 16
                + b"f\x00o\x00r\x00m\x00"
                + b"\x00" * 8
                + f"\r\n{len(form):08x} {len(form):08x} 7fffffff \r\n".encode("ascii")
                + form
                + b"\r\n00000024 00000024 7fffffff \r\n"
                + b"\x00" * 16
                + b"m\x00o\x00d\x00u\x00l\x00e\x00"
                + b"\x00" * 8
                + f"\r\n{len(module):08x} {len(module):08x} 7fffffff \r\n".encode("ascii")
                + module
            )

            parts = root / "parts"
            unpack_form_bin(source, parts)

            self.assertEqual((parts / "Form.xml").read_bytes(), form)
            self.assertEqual((parts / "Module.bsl").read_bytes(), module)

    def test_committed_elem_json_fixture_documents_legacy_shape(self) -> None:
        fixture = Path(__file__).parents[1] / "examples" / "elem-json" / "minimal.json"
        data = json.loads(fixture.read_text(encoding="utf-8"))

        self.assertIn("props", data)
        self.assertIn("commands", data)
        self.assertIn("data", data)
        self.assertIn("tree", data)
        self.assertIn("-pages-", data["data"])

    def test_extract_elem_json_from_bracket_stream(self) -> None:
        bracket = """
        {
          {"InputValue","Pattern",{"S"}},
          {"MainCaption",1,1,{"ru","Main"}},
          {"InputValue","InputField",0,{0,10,10,200,30,0,{0,{2,0,0,10},{2,-1,6,0}}}}
        }
        """

        elem = extract_elem_json_from_bracket(bracket)

        self.assertEqual(elem["props"][0]["name"], "InputValue")
        self.assertEqual(elem["data"]["-pages-"], ["Main"])
        self.assertEqual(elem["tree"][0]["name"], "InputValue")
        self.assertEqual(elem["tree"][0]["type"], "InputField")
        self.assertIn("Main/InputValue", elem["data"])

    def test_extract_elem_json_keeps_only_root_page_until_page_groups_are_decoded(self) -> None:
        bracket = """
        {
          {1,1,{"ru","Document title"}},
          {1,1,{"ru","Number:"}},
          {"InputValue","InputField",0,{0,10,10,200,30,0,{0,{2,0,0,10},{2,-1,6,0}}}}
        }
        """

        elem = extract_elem_json_from_bracket(bracket)

        self.assertEqual(elem["data"]["-pages-"], ["Document title"])

    def test_extract_elem_json_ignores_trailing_module_text(self) -> None:
        bracket = """
        {
          {1,1,{"ru","Main"}}
        }
        // trailing module text can be stored after the bracket payload
        Procedure Run()
        EndProcedure
        """

        elem = extract_elem_json_from_bracket(bracket)

        self.assertEqual(elem["data"]["-pages-"], ["Main"])

    def test_extract_elem_json_prefers_metadata_name_and_detects_images(self) -> None:
        bracket = """
        {
          {1,1,{"ru","Main"}},
          {
            {"#base64:R0lGODlhAQABAIAAAP///wAAACH5BAAAAAAALAAAAAABAAEAAAICRAEAOw=="},
            {0,1,2,3,4,0,{0,{2,0,0,1},{2,-1,6,0}}},
            {14,"КартинкаДлительнаяОперация",4294967295,0,0,0}
          }
        }
        """

        elem = extract_elem_json_from_bracket(bracket)

        self.assertEqual(elem["tree"][0]["name"], "КартинкаДлительнаяОперация")
        self.assertEqual(elem["tree"][0]["type"], "Image")
        self.assertIn("Main/КартинкаДлительнаяОперация", elem["data"])

    def test_dump_bin_creates_object_xml(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            bracket = (
                "{"
                '{"InputValue","Pattern",{"S"}},'
                '{"MainCaption",1,1,{"ru","Main"}},'
                '{"InputValue","InputField",0,{0,10,10,200,30,0,{0,{2,0,0,10},{2,-1,6,0}}}}'
                "}"
            ).encode("utf-8")
            module = b"Procedure Run()\nEndProcedure\n"
            source.write_bytes(
                b"HEAD\r\n"
                b"00000003 00000003 7fffffff \r\none"
                b"\r\n00000003 00000003 7fffffff \r\ntwo"
                b"\r\n00000005 00000005 7fffffff \r\nthree"
                + f"\r\n{len(module):08x} {len(module):08x} 7fffffff \r\n".encode("ascii")
                + module
                + f"\r\n{len(bracket):08x} {len(bracket):08x} 7fffffff \r\n".encode("ascii")
                + bracket
            )

            out = root / "Form.xml"
            from onec_ordinary_forms.cli import dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())

            xml = out.read_text(encoding="utf-8")
            self.assertIn("<OrdinaryForm", xml)
            self.assertIn("<Attributes>", xml)
            self.assertIn('name="InputValue"', xml)
            self.assertIn('rawKey="Main/InputValue"', xml)
            self.assertEqual((root / "Form" / "Module.bsl").read_bytes(), module)

    def test_form_bin_pipeline_keeps_cli_out_of_section_details(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            bracket = b'{{"MainCaption",1,1,{"ru","Main"}}}'
            module = b"Procedure Run()\nEndProcedure\n"
            source.write_bytes(
                b"HEAD\r\n"
                b"00000003 00000003 7fffffff \r\none"
                b"\r\n00000003 00000003 7fffffff \r\ntwo"
                b"\r\n00000005 00000005 7fffffff \r\nthree"
                + f"\r\n{len(module):08x} {len(module):08x} 7fffffff \r\n".encode("ascii")
                + module
                + f"\r\n{len(bracket):08x} {len(bracket):08x} 7fffffff \r\n".encode("ascii")
                + bracket
            )
            calls = []
            observed = {}

            def writer(form_path, bin_path, module_path, elem_path, metadata_path, out_path):
                calls.append((form_path, bin_path, module_path, elem_path, metadata_path, out_path))
                observed["module"] = module_path.read_bytes()
                observed["elem_exists"] = elem_path.exists()
                out_path.write_text("ok", encoding="utf-8")

            out = root / "Form.xml"
            dump_form_bin_to_xml(source, out, model_xml_writer=writer)

            self.assertEqual(out.read_text(encoding="utf-8"), "ok")
            self.assertEqual(len(calls), 1)
            self.assertEqual(calls[0][1], source)
            self.assertEqual(observed["module"], module)
            self.assertTrue(observed["elem_exists"])


if __name__ == "__main__":
    unittest.main()
