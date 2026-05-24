import unittest
import base64
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from struct import unpack
from tempfile import TemporaryDirectory

from onec_ordinary_forms import __version__
from onec_ordinary_forms.corpus import build_corpus_report, classify_exported_forms
from onec_ordinary_forms.cli import (
    format_xml_file,
    pretty_xml_bytes,
    validate_xml_file,
)
from onec_ordinary_forms.formbin import (
    CONTAINER_HEADER_SIZE,
    _read_document,
    build_form_bin_container,
    pack_form_bin,
    parse_form_bin_container,
    unpack_form_bin,
)
from onec_ordinary_forms.bracket import extract_elem_json_from_bracket
from onec_ordinary_forms.liststream import dumps, dumps_list_out_stream, parse_list_stream_document
from onec_ordinary_forms.ordinary_model import parse_ordinary_form_model
from onec_ordinary_forms.ordinary_platform import ordinary_control_type
from onec_ordinary_forms.ordinary_platform import (
    PLATFORM_TRANSFER_RECORD_SIZE,
    PlatformTransferRecord,
    pack_platform_transfer_records,
    unpack_platform_transfer_records,
)
from onec_ordinary_forms.ordinary_properties import ORDINARY_CONTROL_DESCRIPTORS
from onec_ordinary_forms.ordinary_stream import PLATFORM_CONTROL_FORMAT_IDS, apply_geometry_bindings_to_raw, form_stream_from_object_xml
from onec_ordinary_forms.pipeline import dump_form_bin_to_xml


class CliSmokeTest(unittest.TestCase):
    def test_version_is_present(self) -> None:
        self.assertRegex(__version__, r"^\d+\.\d+\.\d+$")

    def test_public_import_wrappers_dump_validate_and_build(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            source.write_bytes(build_form_bin_container(b'{{"MainCaption",1,1,{"ru","Main"}}}', b"module"))
            xml = root / "Forms" / "Form" / "Ext" / "Form.xml"
            rebuilt = root / "rebuilt.bin"

            from onec_ordinary_forms import build_form_bin, dump_form_bin, validate_form_xml

            dump_form_bin(source, xml)
            validate_form_xml(xml)
            build_form_bin(xml, rebuilt)

            rebuilt_files = {file.name: file.payload for file in parse_form_bin_container(rebuilt.read_bytes()).files}
            self.assertEqual(rebuilt_files["module"], b"module")
            self.assertIn(b"Main", rebuilt_files["form"])

    def test_ordinary_palette_describes_all_known_controls(self) -> None:
        self.assertEqual(len(ORDINARY_CONTROL_DESCRIPTORS), 21)
        self.assertIn("Title", ORDINARY_CONTROL_DESCRIPTORS["Label"].properties)
        self.assertIn("ChoiceButton", ORDINARY_CONTROL_DESCRIPTORS["InputField"].properties)
        self.assertIn("RegulationButton", ORDINARY_CONTROL_DESCRIPTORS["InputField"].properties)
        self.assertIn("ChoiceList", ORDINARY_CONTROL_DESCRIPTORS["ChoiceField"].properties)
        self.assertEqual(ORDINARY_CONTROL_DESCRIPTORS["Image"].xml_tag, "PictureDecoration")
        self.assertEqual(ORDINARY_CONTROL_DESCRIPTORS["ProgressBar"].platform_name, "Индикатор")
        self.assertEqual(ORDINARY_CONTROL_DESCRIPTORS["CommandBarButton"].platform_name, "КнопкаКоманднойПанели")

    def test_ordinary_palette_uses_platform_members_from_82_help(self) -> None:
        button = ORDINARY_CONTROL_DESCRIPTORS["Button"]
        input_field = ORDINARY_CONTROL_DESCRIPTORS["InputField"]
        table = ORDINARY_CONTROL_DESCRIPTORS["Table"]

        self.assertIn("Нажатие", {event.name for event in button.platform_events})
        self.assertIn("ПриИзменении", {event.name for event in input_field.platform_events})
        self.assertIn("НачалоВыбора", {event.name for event in input_field.platform_events})
        self.assertIn("ПриВыводеСтроки", {event.name for event in table.platform_events})
        self.assertIn("КнопкаВыбора", {prop.name for prop in input_field.platform_properties})
        self.assertIn("АвтоВводНовойСтроки", {prop.name for prop in table.platform_properties})

    def test_platform_control_guids_drive_item_types(self) -> None:
        self.assertEqual(ordinary_control_type("0fc7e20d-f241-460c-bdf4-5ad88e5474a5"), "Label")
        self.assertEqual(ordinary_control_type("6ff79819-710e-4145-97cd-1618da79e3e2"), "Button")
        self.assertEqual(ordinary_control_type("151ef23e-6bb2-4681-83d0-35bc2217230c"), "Image")
        self.assertEqual(ordinary_control_type("09ccdc77-ea1a-4a6d-ab1c-3435eada2433"), "Panel")
        self.assertEqual(ordinary_control_type("381ed624-9217-4e63-85db-c4c3cb87daae"), "InputField")
        self.assertEqual(ordinary_control_type("e69bf21d-97b2-4f37-86db-675aea9ec2cb"), "CommandBar")
        self.assertEqual(ordinary_control_type("35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26"), "CheckBox")

    def test_platform_transfer_records_use_confirmed_16_byte_layout(self) -> None:
        records = [
            PlatformTransferRecord(1, 2, 3, 4),
            PlatformTransferRecord(0x10, 0x20, 0x30, 0x40),
        ]

        payload = pack_platform_transfer_records(records)

        self.assertEqual(len(payload), 4 + len(records) * PLATFORM_TRANSFER_RECORD_SIZE)
        self.assertEqual(unpack_platform_transfer_records(payload), records)
        with self.assertRaises(ValueError):
            unpack_platform_transfer_records(payload + b"\x00")

    def test_ordinary_model_keeps_control_tables_together(self) -> None:
        form = [
            "27",
            [
                "18",
                [["1", "1", ['"ru"', '"Main"']], "2", "4294967295"],
                [
                    "09ccdc77-ea1a-4a6d-ab1c-3435eada2433",
                    "10",
                    [
                        "1",
                        [
                            [
                                "19",
                                "1",
                                ["4", "4", ["0"], "4"],
                                ["4", "4", ["0"], "4"],
                                ["8", "3", "0", "1", "100"],
                                "0",
                                ["4", "4", ["0"], "4"],
                                ["4", "4", ["0"], "4"],
                                ["4", "4", ["0"], "4"],
                                ["4", "3", ["-7"], "3"],
                                ["4", "3", ["-21"], "3"],
                                ["3", "0", ["0"], "0", "0", "0", "48312c09-257f-4b29-b280-284dd89efc1e"],
                                ["1", "0"],
                                "0",
                                "0",
                                "100",
                                "2",
                                "1",
                                "1",
                                "2",
                                ["4", "4", ["0"], "4"],
                            ],
                            "26",
                            "0",
                            "0",
                            "0",
                            "0",
                            "0",
                            "0",
                            ["10", "1"],
                            "1",
                            "1",
                            ["1", "1", ["6", ["1", "1", ['"ru"', '"State"']], ["10"], "-1", "1", "1", '"State"', "1"]],
                            "1",
                            "1",
                            "0",
                            "4",
                            ["2", "6", "1", "1", "1", "0", "0", "0", "0"],
                            "0",
                            "4294967295",
                            "5",
                            "64",
                            "0",
                            ["4", "4", ["0"], "4"],
                            "0",
                            "0",
                            "57",
                            "0",
                            "0",
                        ],
                        ["0"],
                    ],
                    ["8", "1", "2", "3", "4"],
                    ["14", '"Panel"', "4294967295", "0", "0", "0"],
                    [
                        "1",
                        [
                            "0fc7e20d-f241-460c-bdf4-5ad88e5474a5",
                            "11",
                            ["3", [["19"], "11", ["1", "1", ['"ru"', '"Hello"']]], ["0"]],
                            ["8", "5", "6", "7", "8"],
                            ["14", '"Greeting"', "4294967295", "0", "0", "0"],
                            ["0"],
                        ],
                    ],
                ],
            ],
        ]

        model = parse_ordinary_form_model(form)

        self.assertEqual(len(model.controls), 1)
        panel = model.controls[0]
        self.assertEqual(panel.type, "Panel")
        self.assertEqual(panel.declared_child_count, 1)
        self.assertEqual(panel.actual_child_count, 1)
        self.assertEqual(panel.state_count, 1)
        self.assertEqual(panel.position_record_count, 1)
        self.assertEqual(panel.children[0].type, "Label")
        self.assertEqual(panel.children[0].title, "Hello")

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

    def test_form_bin_container_splits_large_documents_like_platform(self) -> None:
        form = b"x" * (0xA000 + 17)
        data = build_form_bin_container(form, b"module")
        container = parse_form_bin_container(data)

        self.assertEqual({file.name: file.payload for file in container.files}["form"], form)
        self.assertIn(f"{len(form):08x} 0000a000".encode("ascii"), data)

    def test_form_bin_container_aligns_next_document_after_resized_form(self) -> None:
        form = b"x" * (0xA000 + 16)
        data = build_form_bin_container(form, b"module")
        toc = _read_document(data, CONTAINER_HEADER_SIZE)
        module_descriptor_offset, module_data_offset, marker = unpack("<3i", toc[12:24])

        self.assertEqual(marker, 0x7FFFFFFF)
        self.assertEqual(module_descriptor_offset % 2, 1)
        self.assertEqual(module_data_offset % 2, 0)
        self.assertEqual({file.name: file.payload for file in parse_form_bin_container(data).files}["form"], form)

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

    def test_list_out_stream_writer_uses_platform_crlf_layout(self) -> None:
        document = parse_list_stream_document('{1,{"ru","Main"},{2,{0},4}}')

        self.assertEqual(
            dumps_list_out_stream(document.value),
            '{1,\r\n{"ru","Main"},\r\n{2,\r\n{0},4}\r\n}',
        )

    def test_geometry_dimension_binding_round_trips_as_dimension_record(self) -> None:
        geometry = ET.fromstring(
            """
            <Position>
              <Bindings>
                <DimensionBinding dimension="height" mode="0" target="self" targetId="20" side="bottom"/>
              </Bindings>
            </Position>
            """
        )
        raw_geometry = ["8", "1", "2", "3", "4", "1", ["0"], ["0"], ["0"], ["0"], ["0"], ["0"], "0", "0", "0", "0", "0"]

        apply_geometry_bindings_to_raw(geometry, raw_geometry)

        self.assertEqual(raw_geometry[13], ["0", "20", "1"])

    def test_platform_control_format_ids_are_known(self) -> None:
        self.assertEqual(PLATFORM_CONTROL_FORMAT_IDS["controls"], 0x2500)
        self.assertEqual(PLATFORM_CONTROL_FORMAT_IDS["position"], 0x5500)
        self.assertEqual(PLATFORM_CONTROL_FORMAT_IDS["info"], 0x9D00)

    def test_stream_writer_requires_explicit_control_id(self) -> None:
        root = ET.fromstring(
            """
            <Form version="0.1">
              <Pages>
                <Page name="Main">
                  <Button name="Run"/>
                </Page>
              </Pages>
            </Form>
            """
        )

        with self.assertRaisesRegex(ValueError, "explicit id"):
            form_stream_from_object_xml(root)

    def test_button_action_roundtrips_as_named_xml(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            action_uuid = "e1692cc2-605b-4535-84dd-28440238746c"
            bracket = (
                "{"
                '{"MainCaption",1,1,{"ru","Main"}},'
                "{6ff79819-710e-4145-97cd-1618da79e3e2,7,"
                '{1,{1,1,{"ru","Run"}},{3,"RunCommand",' + action_uuid + "}},"
                "{8,1,2,101,22,0,0,0,0,0,0,0,0,0,0,0,0},"
                '{14,"RunButton",4294967295,0,0,0},'
                "{0}}"
                "}"
            ).encode("utf-8")
            source.write_bytes(build_form_bin_container(bracket, b""))

            out = root / "Form.xml"
            from onec_ordinary_forms.cli import build_bin, dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())
            xml = out.read_text(encoding="utf-8")
            self.assertIn('<Action name="RunCommand" uuid="e1692cc2-605b-4535-84dd-28440238746c"/>', xml)
            validate_xml_file(out)

            rebuilt = root / "rebuilt.bin"
            build_bin(type("Args", (), {"xml": str(out), "out_bin": str(rebuilt), "asset_root": None})())
            rebuilt_files = {file.name: file.payload for file in parse_form_bin_container(rebuilt.read_bytes()).files}
            rebuilt_form = rebuilt_files["form"].decode("utf-8-sig")
            self.assertTrue(rebuilt_files["form"].startswith(b"\xef\xbb\xbf"))
            self.assertTrue(rebuilt_form.startswith("{27,"))
            self.assertIn('"RunCommand"', rebuilt_form)
            self.assertIn("e1692cc2-605b-4535-84dd-28440238746c", rebuilt_form)

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
            self.assertTrue(xml.startswith("<?xml version='1.0' encoding='utf-8'?>\n"))
            self.assertIn("<Form", xml)
            self.assertIn("\n      <InputField ", xml)
            self.assertNotIn("<OrdinaryForm", xml)
            self.assertNotIn("<Source", xml)
            self.assertNotIn("<FormStructure", xml)
            self.assertNotIn("<ChildItems", xml)
            self.assertNotIn("<Commands", xml)
            self.assertIn("noNamespaceSchemaLocation", xml)
            self.assertIn("<Attributes>", xml)
            self.assertIn('name="InputValue"', xml)
            self.assertNotIn("rawKey=", xml)
            self.assertNotIn("platformType=", xml)
            self.assertNotIn("managedEquivalent=", xml)
            self.assertNotIn("childCount=", xml)
            self.assertIn('modeName="edgeToEdge"', xml)
            self.assertIn('relation="targetEdgeOffset"', xml)
            self.assertIn('offset="10"', xml)
            self.assertNotIn('offsetType=', xml)
            self.assertNotIn("<ListStream", xml)
            self.assertNotIn("<FormBin", xml)
            self.assertNotIn("<LogicalStream", xml)
            self.assertNotIn("binFile=", xml)
            self.assertEqual((root / "Form" / "Module.bsl").read_bytes(), module)
            validate_xml_file(out)

            rebuilt = root / "rebuilt.bin"
            from onec_ordinary_forms.cli import build_bin

            build_bin(type("Args", (), {"xml": str(out), "out_bin": str(rebuilt), "asset_root": None})())
            rebuilt_container = parse_form_bin_container(rebuilt.read_bytes())
            rebuilt_files = {file.name: file.payload for file in rebuilt_container.files}
            self.assertEqual(rebuilt_files["module"], module)
            rebuilt_text = rebuilt_files["form"].decode("utf-8-sig")
            self.assertTrue(rebuilt_text.startswith("{27,"))
            self.assertIn('"InputValue"', rebuilt_text)
            self.assertIn("381ed624-9217-4e63-85db-c4c3cb87daae", rebuilt_text)

    def test_dump_bin_emits_data_path_for_bound_input_field(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            bracket = (
                "{"
                '{"MainCaption",1,1,{"ru","Main"}},'
                "{381ed624-9217-4e63-85db-c4c3cb87daae,159,"
                '{9,{"Pattern",{"S"}},{{{10,1,{3,4,{0}},{3,4,{0}},{6,3,0,{0},0},0,{3,4,{0}},{3,4,{0}},{3,4,{0}},{3,3,{-7}},{3,3,{-21}},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,1,{"ru","Number"}}}},0,{0},0,1,0,{1,0},0}},'
                "{8,10,10,200,30,0,{0,{2,0,0,10},{2,-1,6,0}}},"
                '{14,"InputValue",4294967295,0,0,0},'
                "{0}}"
                "}"
            ).encode("utf-8")
            source.write_bytes(build_form_bin_container(bracket, b""))

            out = root / "Form.xml"
            from onec_ordinary_forms.cli import build_bin, dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())
            xml = out.read_text(encoding="utf-8")
            self.assertIn('<InputField name="InputValue" id="159">', xml)
            self.assertIn("<DataPath>InputValue</DataPath>", xml)
            validate_xml_file(out)

            rebuilt = root / "rebuilt.bin"
            build_bin(type("Args", (), {"xml": str(out), "out_bin": str(rebuilt), "asset_root": None})())
            rebuilt_form = {file.name: file.payload for file in parse_form_bin_container(rebuilt.read_bytes()).files}[
                "form"
            ].decode("utf-8-sig")
            self.assertIn('"InputValue"', rebuilt_form)

    def test_dump_bin_emits_saved_visible(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            bracket = (
                "{"
                '{"MainCaption",1,1,{"ru","Main"}},'
                "{381ed624-9217-4e63-85db-c4c3cb87daae,159,"
                '{9,{"Pattern",{"S"}},{{{10,0,{3,4,{0}},{3,4,{0}},{6,3,0,{0},0},0,{3,4,{0}},{3,4,{0}},{3,4,{0}},{3,3,{-7}},{3,3,{-21}},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,1,{"ru","Number"}}}},0,{0},0,1,0,{1,0},0}},'
                "{8,10,10,200,30,0,{0,{2,0,0,10},{2,-1,6,0}}},"
                '{14,"InputValue",4294967295,0,0,0},'
                "{0}}"
                "}"
            ).encode("utf-8")
            source.write_bytes(build_form_bin_container(bracket, b""))

            out = root / "Form.xml"
            from onec_ordinary_forms.cli import build_bin, dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())
            xml = out.read_text(encoding="utf-8")
            self.assertIn("<Visible>false</Visible>", xml)
            validate_xml_file(out)

            rebuilt = root / "rebuilt.bin"
            build_bin(type("Args", (), {"xml": str(out), "out_bin": str(rebuilt), "asset_root": None})())
            rebuilt_form = {file.name: file.payload for file in parse_form_bin_container(rebuilt.read_bytes()).files}[
                "form"
            ].decode("utf-8-sig")
            self.assertIn("{10,0,", rebuilt_form)

    def test_dump_bin_emits_tooltip_without_fake_input_title(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            bracket = (
                "{"
                '{"MainCaption",1,1,{"ru","Main"}},'
                "{381ed624-9217-4e63-85db-c4c3cb87daae,159,"
                '{9,{"Pattern",{"S"}},{{{10,1,{3,4,{0}},{3,4,{0}},{6,3,0,{0},0},0,{3,4,{0}},{3,4,{0}},{3,4,{0}},{3,3,{-7}},{3,3,{-21}},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,1,{"ru","Номер документа"}}}},0,{0},0,1,0,{1,0},0}},'
                "{8,10,10,200,30,0,{0,{2,0,0,10},{2,-1,6,0}}},"
                '{14,"Number",4294967295,0,0,0},'
                "{0}}"
                "}"
            ).encode("utf-8")
            source.write_bytes(build_form_bin_container(bracket, b""))

            out = root / "Form.xml"
            from onec_ordinary_forms.cli import dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())
            xml_root = ET.parse(out).getroot()
            field = xml_root.find(".//InputField[@name='Number']")
            self.assertIsNotNone(field)
            self.assertIsNone(field.find("Title"))
            self.assertEqual(field.findtext("./ToolTip/Item"), "Номер документа")
            validate_xml_file(out)

    def test_dump_bin_emits_control_events_without_fake_input_title(self) -> None:
        from onec_ordinary_forms.cli import add_control_events, item_title

        event_uuid = "e1692cc2-605b-4535-84dd-28440238746c"
        item_data = {
            "raw": [
                "381ed624-9217-4e63-85db-c4c3cb87daae",
                "160",
                [
                    "9",
                    ['"Pattern"', ['"D"']],
                    [],
                    [
                        "1",
                        [
                            "2147483647",
                            event_uuid,
                            [
                                "3",
                                '"DateOnChange"',
                                [
                                    "1",
                                    '"DateOnChange"',
                                    ["1", "1", ['"ru"', '"Дата при изменении"']],
                                    ["1", "1", ['"ru"', '"Дата при изменении"']],
                                    ["1", "1", ['"ru"', '"Дата при изменении"']],
                                    ["3", "0", ["0"], '""', "-1", "1242684", "1", "0"],
                                    ["0", "0", "0"],
                                ],
                            ],
                        ],
                    ],
                ],
            ]
        }
        field = ET.Element("InputField")

        add_control_events(field, "InputField", item_data)

        self.assertEqual(item_title(item_data, "InputField"), "")
        event = field.find("./Events/Event")
        self.assertIsNotNone(event)
        self.assertEqual(event.get("name"), "ПриИзменении")
        self.assertEqual(event.text, "DateOnChange")

    def test_build_bin_writes_tooltip_to_base_info_slot_12(self) -> None:
        root = ET.fromstring(
            """<Form>
              <Title><Item lang="ru">Main</Item></Title>
              <Pages>
                <Page name="Main">
                  <InputField name="Number" id="159">
                    <ToolTip><Item lang="ru">Номер документа</Item></ToolTip>
                  </InputField>
                </Page>
              </Pages>
            </Form>"""
        )

        form_text = form_stream_from_object_xml(root).decode("utf-8-sig")
        stream = parse_list_stream_document(form_text).value

        def find_tooltip_base(value: object) -> bool:
            if isinstance(value, list):
                if (
                    len(value) > 12
                    and value[0] == "10"
                    and isinstance(value[12], list)
                    and len(value[12]) > 2
                    and value[12][2] == ['"ru"', '"Номер документа"']
                ):
                    return True
                return any(find_tooltip_base(child) for child in value)
            return False

        self.assertTrue(find_tooltip_base(stream))

    def test_build_bin_writes_control_events_to_action_table(self) -> None:
        root = ET.fromstring(
            """<Form>
              <Title><Item lang="ru">Main</Item></Title>
              <Pages>
                <Page name="Main">
                  <InputField name="Date" id="160">
                    <Events><Event name="ПриИзменении">DateOnChange</Event></Events>
                  </InputField>
                </Page>
              </Pages>
            </Form>"""
        )

        form_text = form_stream_from_object_xml(root).decode("utf-8-sig")

        self.assertIn('"DateOnChange"', form_text)
        self.assertIn("e1692cc2-605b-4535-84dd-28440238746c", form_text)

    def test_build_bin_uses_input_field_info_kind_and_read_only_slot(self) -> None:
        root = ET.fromstring(
            """<Form>
              <Title><Item lang="ru">Main</Item></Title>
              <Pages>
                <Page name="Main">
                  <InputField name="Number" id="159">
                    <ReadOnly>true</ReadOnly>
                    <ToolTip><Item lang="ru">Номер документа</Item></ToolTip>
                  </InputField>
                </Page>
              </Pages>
            </Form>"""
        )

        form_text = form_stream_from_object_xml(root).decode("utf-8-sig")
        stream = parse_list_stream_document(form_text).value
        input_field = self._find_control(stream, "381ed624-9217-4e63-85db-c4c3cb87daae")

        self.assertIsNotNone(input_field)
        info = input_field[2]
        self.assertEqual(info[0], "9")
        input_info = info[2][0]
        self.assertEqual(input_info[12], "1")
        self.assertEqual(input_info[0][12][2], ['"ru"', '"Номер документа"'])

    def test_add_read_only_uses_input_field_info_slot(self) -> None:
        from onec_ordinary_forms.cli import add_read_only

        node = ET.Element("InputField")
        input_info = [["10", "1"], "21", "0", "0", "0", "0", "0", "0", "0", "0", "0", "1", "1"]
        item_data = {"raw": ["381ed624-9217-4e63-85db-c4c3cb87daae", "159", ["9", [], [input_info]]]}
        add_read_only(node, {"type": "InputField"}, item_data)
        self.assertEqual(node.findtext("ReadOnly"), "true")

    def test_build_bin_writes_button_base_info_from_xml(self) -> None:
        root = ET.fromstring(
            """<Form>
              <Title><Item lang="ru">Main</Item></Title>
              <Pages>
                <Page name="Main">
                  <Button name="Run" id="26">
                    <Title><Item lang="ru">Run</Item></Title>
                    <Visible>false</Visible>
                    <ToolTip><Item lang="ru">Run tooltip</Item></ToolTip>
                  </Button>
                </Page>
              </Pages>
            </Form>"""
        )

        form_text = form_stream_from_object_xml(root).decode("utf-8-sig")
        stream = parse_list_stream_document(form_text).value
        button = self._find_control(stream, "6ff79819-710e-4145-97cd-1618da79e3e2")

        self.assertIsNotNone(button)
        base = button[2][1][0]
        self.assertEqual(button[2][0], "1")
        self.assertEqual(base[1], "0")
        self.assertEqual(base[12][2], ['"ru"', '"Run tooltip"'])

    def test_build_bin_uses_checkbox_info_kind_and_named_title(self) -> None:
        root = ET.fromstring(
            """<Form>
              <Title><Item lang="ru">Main</Item></Title>
              <Pages>
                <Page name="Main">
                  <CheckBox name="UseColor" id="33">
                    <Title><Item lang="ru">Use color</Item></Title>
                    <ToolTip><Item lang="ru">Use color tooltip</Item></ToolTip>
                  </CheckBox>
                </Page>
              </Pages>
            </Form>"""
        )

        form_text = form_stream_from_object_xml(root).decode("utf-8-sig")
        stream = parse_list_stream_document(form_text).value
        checkbox = self._find_control(stream, "35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26")

        self.assertIsNotNone(checkbox)
        info = checkbox[2]
        self.assertEqual(info[0], "1")
        checkbox_info = info[1][0]
        self.assertEqual(checkbox_info[2][2], ['"ru"', '"Use color"'])
        self.assertEqual(checkbox_info[0][12][2], ['"ru"', '"Use color tooltip"'])

    def _find_control(self, value: object, class_id: str) -> list[object] | None:
        if isinstance(value, list):
            if value and value[0] == class_id:
                return value
            for child in value:
                found = self._find_control(child, class_id)
                if found is not None:
                    return found
        return None

    def test_format_xml_file_pretty_prints_schema_like_xml(self) -> None:
        with TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "schema.xsd"
            path.write_text(
                '<?xml version="1.0" encoding="UTF-8"?><xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:element name="A"/></xs:schema>',
                encoding="utf-8",
            )
            format_xml_file(path)
            text = path.read_text(encoding="utf-8")
            self.assertIn("\n  <xs:element name=\"A\"/>\n", text)

    def test_pretty_xml_keeps_multiline_text_on_one_xml_line(self) -> None:
        root = ET.Element("Form")
        title = ET.SubElement(root, "Title")
        item = ET.SubElement(title, "Item", {"lang": "ru"})
        item.text = "Первая строка\nВторая строка"

        xml = pretty_xml_bytes(root).decode("utf-8")

        self.assertIn("Первая строка&#10;Вторая строка", xml)
        self.assertNotIn("Первая строка\nВторая строка", xml)
        self.assertEqual(ET.fromstring(xml).findtext("./Title/Item"), "Первая строка\nВторая строка")

    def test_dump_bin_uses_managed_like_sidecars_for_module_and_pictures(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            picture = base64.b64encode(b"GIF89a").decode("ascii")
            bracket = (
                "{"
                '{"MainCaption",1,1,{"ru","Main"}},'
                '{"151ef23e-6bb2-4681-83d0-35bc2217230c",1,{'
                '1,{{19},11,{1,1,{"ru","Image1"}},"#base64:'
                + picture
                + '"},'
                "{8,1,2,3,4,1,"
                "{0,{2,-1,6,0},{2,-1,6,0}},"
                "{0,{2,1,0,22},{2,-1,6,0}},"
                "{0,{2,-1,6,0},{2,-1,6,0}},"
                "{0,{2,1,2,2},{2,-1,6,0}},"
                "{0,{2,-1,6,0},{2,-1,6,0}},"
                "{0,{2,-1,6,0},{2,-1,6,0}},"
                "0,{0,1,1},0,{0,1,3},0,0},"
                '{14,"Image1",0,0,0},'
                "{0}"
                "}}"
                "}"
            ).encode("utf-8")
            module = b"Procedure Run()\nEndProcedure\n"
            source.write_bytes(build_form_bin_container(bracket, module))

            out = root / "Forms" / "Form" / "Ext" / "Form.xml"
            from onec_ordinary_forms.cli import dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())

            form_dir = out.with_suffix("")
            xml = out.read_text(encoding="utf-8")
            self.assertEqual((form_dir / "Module.bsl").read_bytes(), module)
            self.assertEqual((form_dir / "Items" / "Image1" / "Picture.gif").read_bytes(), b"GIF89a")
            self.assertNotIn('file="Module.bsl"', xml)
            self.assertIn('file="Items/Image1/Picture.gif"', xml)
            self.assertNotIn("Procedure Run", xml)
            self.assertNotIn(picture, xml)

    def test_build_bin_writes_control_xml_edits_into_list_stream(self) -> None:
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "Form.bin"
            picture = base64.b64encode(b"GIF89a").decode("ascii")
            bracket = (
                "{"
                '{"MainCaption",1,1,{"ru","Main"}},'
                '{"151ef23e-6bb2-4681-83d0-35bc2217230c",1,{'
                '1,{{19},11,{1,1,{"ru","Image1"}},"#base64:'
                + picture
                + '"},'
                "{8,1,2,3,4,1,"
                "{0,{2,-1,6,0},{2,-1,6,0}},"
                "{0,{2,1,0,22},{2,-1,6,0}},"
                "{0,{2,-1,6,0},{2,-1,6,0}},"
                "{0,{2,1,2,2},{2,-1,6,0}},"
                "{0,{2,-1,6,0},{2,-1,6,0}},"
                "{0,{2,-1,6,0},{2,-1,6,0}},"
                "0,{0,1,1},0,{0,1,3},0,0},"
                '{14,"Image1",0,0,0},'
                "{0}"
                "}}"
                "}"
            ).encode("utf-8")
            source.write_bytes(build_form_bin_container(bracket, b"module"))

            out = root / "Forms" / "Form" / "Ext" / "Form.xml"
            from onec_ordinary_forms.cli import build_bin, dump_bin

            dump_bin(type("Args", (), {"bin": str(source), "out": str(out), "metadata_json": None})())
            tree = ET.parse(out)
            xml_root = tree.getroot()
            image = xml_root.find(".//PictureDecoration[@name='Image1']")
            self.assertIsNotNone(image)
            image.set("name", "Image2")
            title = image.find("./Title/Item")
            self.assertIsNotNone(title)
            title.text = "Image title"
            geometry = image.find("./Position")
            self.assertIsNotNone(geometry)
            geometry.set("left", "42")
            first_binding_from = geometry.find("./Bindings/Binding[@coordinate='top']/From")
            self.assertIsNotNone(first_binding_from)
            first_binding_from.set("offset", "99")
            picture_path = out.with_suffix("") / "Items" / "Image1" / "Picture.gif"
            picture_path.write_bytes(b"GIF89aChanged")
            tree.write(out, encoding="utf-8", xml_declaration=True)

            rebuilt = root / "rebuilt.bin"
            build_bin(type("Args", (), {"xml": str(out), "out_bin": str(rebuilt), "asset_root": None})())
            rebuilt_container = parse_form_bin_container(rebuilt.read_bytes())
            form_text = {file.name: file.payload for file in rebuilt_container.files}["form"].decode("utf-8")
            self.assertIn('"Image2"', form_text)
            self.assertIn("42", form_text)
            self.assertIn("99", form_text)
            self.assertIn(base64.b64encode(b"GIF89aChanged").decode("ascii"), form_text)

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
            self.assertIn('relation="rawList"', xml)
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
