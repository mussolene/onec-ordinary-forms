import unittest
import base64
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from tempfile import TemporaryDirectory

from onec_ordinary_forms import __version__
from onec_ordinary_forms.corpus import build_corpus_report, classify_exported_forms
from onec_ordinary_forms.cli import apply_semantic_edits_to_form, replace_root_title
from onec_ordinary_forms.formbin import build_form_bin_container, pack_form_bin, unpack_form_bin
from onec_ordinary_forms.bracket import extract_elem_json_from_bracket
from onec_ordinary_forms.liststream import dumps, parse_list_stream_document
from onec_ordinary_forms.pipeline import dump_form_bin_to_xml


class CliSmokeTest(unittest.TestCase):
    def test_version_is_present(self) -> None:
        self.assertRegex(__version__, r"^\d+\.\d+\.\d+$")

    def test_replace_root_title_updates_first_title_only(self) -> None:
        source = '{"ru","Old"}\n{"ru","Other"}\n'
        result = replace_root_title(source, "New")
        self.assertEqual(result, '{"ru","New"}\n{"ru","Other"}\n')

    def test_apply_semantic_edits_inserts_raw_xml_item(self) -> None:
        source = (
            "{27,{18,{{1,1,{\"ru\",\"Main\"}},2,4294967295},"
            "{1,{1,{0},{0},{14,\"A\",4294967295,0,0,0},{0}},"
            "{2,{0},{0},{14,\"B\",4294967295,0,0,0},{0}}}}}}"
        )
        raw_insert = "{3,{0},{0},{14,\"Inserted\",4294967295,0,0,0},{0}}"
        root = ET.Element("OrdinaryForm")
        structure = ET.SubElement(root, "FormStructure")
        inserted = ET.SubElement(structure, "Item", {"name": "Inserted", "insert": "true", "after": "A"})
        raw_node = ET.SubElement(inserted, "RawBracket", {"encoding": "base64"})
        raw_node.text = base64.b64encode(raw_insert.encode("utf-8")).decode("ascii")

        result = apply_semantic_edits_to_form(root, source.encode("utf-8")).decode("utf-8")

        self.assertIn('{14,"A",4294967295,0,0,0},{0}},{3,{0}', result)
        self.assertIn('{14,"Inserted",4294967295,0,0,0}', result)
        self.assertIn('{"ru","Main"}},3,4294967295', result)

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
            source.write_bytes(build_form_bin_container(b"form", b"module"))

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
            form = b'\xef\xbb\xbf{1,2}'
            module = b'\xef\xbb\xbf// module\r\n'
            source.write_bytes(build_form_bin_container(form, module))

            parts = root / "parts"
            unpack_form_bin(source, parts)
            rebuilt = root / "rebuilt.bin"
            pack_form_bin(parts, rebuilt)

            self.assertEqual(rebuilt.read_bytes(), source.read_bytes())
            self.assertEqual((parts / "Form.xml").read_bytes(), form)
            self.assertEqual((parts / "Module.bsl").read_bytes(), module)

    def test_form_bin_unpack_detects_bom_bracket_with_invalid_text_bytes(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            form = b'\xef\xbb\xbf{1,"\xff"}'
            module = b'\xef\xbb\xbf// module\r\n'
            source.write_bytes(build_form_bin_container(form, module))

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

    def test_list_stream_parser_preserves_trailing_text_boundary(self) -> None:
        document = parse_list_stream_document('{1,{"ru","Main"}}\n// module tail', allow_trailing=True)

        self.assertEqual(document.value, ["1", ['"ru"', '"Main"']])
        self.assertEqual(document.trailing, "// module tail")
        self.assertEqual(dumps(document.value), '{1,{"ru","Main"}}')

    def test_list_stream_parser_accepts_square_brackets_as_lists(self) -> None:
        document = parse_list_stream_document('[1,{"x","y"}]')

        self.assertEqual(document.value, ["1", ['"x"', '"y"']])
        self.assertEqual(dumps(document.value), '{1,{"x","y"}}')

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
            source.write_bytes(build_form_bin_container(bracket, module))

            out = root / "Form.xml"
            from onec_ordinary_forms.cli import dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())

            xml = out.read_text(encoding="utf-8")
            self.assertIn("<OrdinaryForm", xml)
            self.assertIn("<Attributes>", xml)
            self.assertIn('name="InputValue"', xml)
            self.assertIn('rawKey="Main/InputValue"', xml)
            self.assertIn('modeName="edgeToEdge"', xml)
            self.assertIn('kindName="targetEdgeOffset"', xml)
            self.assertIn('offsetType="integer"', xml)
            self.assertIn("<FormBin", xml)
            self.assertIn("<LogicalStream", xml)
            self.assertEqual((root / "Form" / "Module.bsl").read_bytes(), module)

            rebuilt = root / "rebuilt.bin"
            from onec_ordinary_forms.cli import build_bin

            build_bin(type("Args", (), {"xml": str(out), "out_bin": str(rebuilt), "asset_root": None})())

            from onec_ordinary_forms.formbin import logical_streams, parse_form_bin

            rebuilt_streams = logical_streams(parse_form_bin(rebuilt.read_bytes()))
            self.assertEqual(rebuilt_streams["Form.xml"], bracket)
            self.assertEqual(rebuilt_streams["Module.bsl"], module)

    def test_dump_bin_keeps_complex_bindings_structured(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            bracket = (
                "{"
                '{"MainCaption",1,1,{"ru","Main"}},'
                '{"Panel","Panel",0,{0,1,2,3,4,0,0,0,0,'
                '{10,1,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100},'
                '0,0,0,0}}'
                "}"
            ).encode("utf-8")
            module = b""
            source.write_bytes(build_form_bin_container(bracket, module))

            out = root / "Form.xml"
            from onec_ordinary_forms.cli import dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())

            xml = out.read_text(encoding="utf-8")
            self.assertIn('modeName="compound"', xml)
            self.assertIn('kindName="rawList"', xml)
            self.assertIn("<Value", xml)
            self.assertNotIn('edge="[', xml)
            self.assertNotIn('side="edge[', xml)

    def test_form_bin_pipeline_keeps_cli_out_of_section_details(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            bracket = b'{{"MainCaption",1,1,{"ru","Main"}}}'
            module = b"Procedure Run()\nEndProcedure\n"
            source.write_bytes(build_form_bin_container(bracket, module))
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
