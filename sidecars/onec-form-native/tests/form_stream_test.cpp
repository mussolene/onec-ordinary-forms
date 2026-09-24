#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oof/storage/form_stream.hpp"

namespace {

namespace form_stream = oof::storage::form_stream;
namespace list_stream = oof::storage::list_stream;
namespace model = oof::model;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename T>
void expect_failure(
    const oof::Result<T>& result,
    std::string_view code,
    std::string_view path,
    std::string_view message) {
    expect(!result, message);
    expect(result.diagnostics().size() == 1, "storage failure must have one local diagnostic");
    if (result.diagnostics()[0].code != code) {
        throw std::runtime_error(
            "storage diagnostic code mismatch: expected " + std::string(code) +
            ", got " + result.diagnostics()[0].code + ": " +
            result.diagnostics()[0].message);
    }
    if (result.diagnostics()[0].path != path) {
        throw std::runtime_error(
            "storage diagnostic path mismatch: expected " + std::string(path) +
            ", got " + result.diagnostics()[0].path);
    }
}

model::CompositeIdValue composite(std::int64_t id, std::string uuid) {
    return model::CompositeIdValue{id, model::UuidValue{std::move(uuid)}, false};
}

list_stream::ListValue versioned_record(std::uint32_t version, std::size_t arity) {
    std::vector<list_stream::ListValue> items(
        arity,
        list_stream::ListValue::raw_atom("0"));
    items[0] = list_stream::ListValue::raw_atom(std::to_string(version));
    return list_stream::ListValue::list(std::move(items));
}

list_stream::ListValue layout_fixture(form_stream::LayoutKind kind) {
    std::vector<list_stream::ListValue> root(
        20,
        list_stream::ListValue::raw_atom("0"));
    root[0] = list_stream::ListValue::raw_atom("27");
    if (kind == form_stream::LayoutKind::form_section_16) {
        root[1] = versioned_record(16, 11);
        root[13] = versioned_record(3, 3);
    } else {
        root[1] = versioned_record(18, 14);
        root[13] = versioned_record(10, 11);
    }
    return list_stream::ListValue::list(std::move(root));
}

std::string runtime_envelope_text(const list_stream::ListValue& payload) {
    return list_stream::dump_compact(list_stream::ListValue::list({
        list_stream::ListValue::string_atom("#"),
        list_stream::ListValue::raw_atom(
            "01234567-89ab-cdef-0123-456789abcdef"),
        payload,
    }));
}

void test_outer_format_probe() {
    const auto format27 = form_stream::probe_outer_format(list_stream::parse("{27}"));
    expect(
        format27 && format27.value() == form_stream::OuterFormat::v27,
        "outer format 27 must probe");
    const auto format26 = form_stream::probe_outer_format(list_stream::parse("{26}"));
    expect(
        format26 && format26.value() == form_stream::OuterFormat::v26,
        "outer format 26 must probe");

    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{28}")),
        "OOF1111",
        "$/0",
        "unknown outer format must be rejected");
    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{}")),
        "OOF1110",
        "$/0",
        "missing outer format must be rejected");
    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{\"27\"}")),
        "OOF1104",
        "$/0",
        "quoted outer format must be rejected");
}

void test_layout_probe() {
    const auto section16 = form_stream::probe_layout(
        layout_fixture(form_stream::LayoutKind::form_section_16));
    expect(section16.ok(), "form section 16 layout must probe");
    expect(
        section16.value() == form_stream::StorageLayout{
            form_stream::OuterFormat::v27,
            form_stream::LayoutKind::form_section_16,
            20,
            16,
            11,
            3,
            3,
        },
        "form section 16 layout descriptor mismatch");

    const auto section18 = form_stream::probe_layout(
        layout_fixture(form_stream::LayoutKind::form_section_18));
    expect(section18.ok(), "form section 18 layout must probe");
    expect(
        section18.value() == form_stream::StorageLayout{
            form_stream::OuterFormat::v27,
            form_stream::LayoutKind::form_section_18,
            20,
            18,
            14,
            10,
            11,
        },
        "form section 18 layout descriptor mismatch");

    expect_failure(
        form_stream::probe_layout(list_stream::parse("{26}")),
        "OOF1112",
        "$/0",
        "outer format without a proven nested layout must be rejected");

    auto wrong_root_arity = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_root_arity.items.pop_back();
    expect_failure(
        form_stream::probe_layout(wrong_root_arity),
        "OOF1102",
        "$",
        "root arity drift must be rejected");

    auto wrong_version = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_version.items[1].items[0] = list_stream::ListValue::raw_atom("17");
    expect_failure(
        form_stream::probe_layout(wrong_version),
        "OOF1113",
        "$/1/0",
        "unknown form section version must be rejected");

    auto wrong_form_arity = layout_fixture(form_stream::LayoutKind::form_section_16);
    wrong_form_arity.items[1].items.push_back(list_stream::ListValue::raw_atom("0"));
    expect_failure(
        form_stream::probe_layout(wrong_form_arity),
        "OOF1102",
        "$/1",
        "form section arity drift must be rejected");

    auto wrong_page_style_version = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_page_style_version.items[13].items[0] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(
        form_stream::probe_layout(wrong_page_style_version),
        "OOF1106",
        "$/13/0",
        "page-style section version drift must be rejected");
}

void test_runtime_envelope() {
    constexpr std::string_view uuid = "01234567-89ab-cdef-0123-456789abcdef";
    const auto payload = layout_fixture(form_stream::LayoutKind::form_section_18);
    const std::string source = runtime_envelope_text(payload);
    const auto decoded = form_stream::decode_runtime_envelope(source);
    expect(decoded.ok(), "runtime envelope must decode");
    expect(decoded.value().runtime_uuid.canonical == uuid, "runtime UUID must decode");
    const auto encoded = form_stream::encode_runtime_envelope(decoded.value());
    expect(encoded && encoded.value() == source, "runtime envelope must encode deterministically");

    const auto with_bom = form_stream::decode_runtime_envelope("\xef\xbb\xbf" + source);
    expect(with_bom.ok(), "runtime envelope must accept a UTF-8 BOM from TextDocument");
    const auto encoded_without_bom = form_stream::encode_runtime_envelope(with_bom.value());
    expect(
        encoded_without_bom && encoded_without_bom.value() == source,
        "runtime envelope encoding must be canonical without a BOM");

    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"!\"," + std::string(uuid) + "," +
            list_stream::dump_compact(payload) + "}"),
        "OOF1106",
        "$/0",
        "wrong runtime marker must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"#\",not-a-guid," + list_stream::dump_compact(payload) + "}"),
        "OOF1105",
        "$/1",
        "malformed runtime UUID must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope("{\"#\",01234567-89ab-cdef-0123-456789abcdef,{99}}"),
        "OOF1111",
        "$/2/0",
        "unknown payload format must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"#\",01234567-89ab-cdef-0123-456789abcdef,{26}}"),
        "OOF1112",
        "$/2/0",
        "runtime envelope must reject an outer format without a proven layout");
}

void test_attributes() {
    const auto empty = form_stream::decode_attributes(list_stream::parse("{{-1},3,{0},{0}}"));
    expect(empty.ok(), "empty attribute record must decode");
    expect(empty.value().slot_count == 3, "empty attribute slot count mismatch");
    expect(empty.value().attributes.empty(), "empty attribute table mismatch");
    expect(empty.value().links.empty(), "empty attribute-link table mismatch");

    constexpr std::string_view fixture =
        "{{-1},2,{1,{{1,01234567-89ab-cdef-0123-456789abcdef},1,0,1,\"Value\","
        "{\"Pattern\",{\"T\",d47d59f8-73f0-481c-8b5e-f6384c0a4804}}}},"
        "{1,{42,{1,{1,01234567-89ab-cdef-0123-456789abcdef}}}}}";
    const auto decoded = form_stream::decode_attributes(list_stream::parse(fixture));
    expect(decoded.ok(), "attribute fixture must decode");
    expect(decoded.value().slot_count == 2, "attribute slot count mismatch");
    expect(decoded.value().attributes.size() == 1, "attribute count mismatch");
    expect(decoded.value().attributes[0].name == "Value", "attribute name mismatch");
    expect(decoded.value().attributes[0].main, "attribute main flag mismatch");
    expect(decoded.value().links.size() == 1, "attribute link count mismatch");
    expect(decoded.value().links[0].control_id == 42, "attribute link control mismatch");

    const auto encoded = form_stream::encode_attributes(decoded.value());
    expect(encoded.ok(), "attribute fixture must encode");
    expect(list_stream::dump_compact(encoded.value()) == fixture, "attribute fixture must be canonical");
    const auto rebuilt = form_stream::decode_attributes(encoded.value());
    expect(rebuilt && rebuilt.value() == decoded.value(), "attribute DTO must round-trip");

    constexpr std::string_view one_component_fixture =
        "{{-1},4,{1,{{3},1,0,1,\"Value\",{\"Pattern\",{\"S\",10,1}}}},"
        "{1,{3,{1,{3}}}}}";
    const auto one_component = form_stream::decode_attributes(
        list_stream::parse(one_component_fixture));
    expect(one_component.ok(), "platform one-component attribute ID must decode");
    expect(
        one_component.value().attributes.front().id.object_id == 3 &&
            one_component.value().attributes.front().id.is_null,
        "one-component attribute ID semantics mismatch");
    const auto one_component_encoded = form_stream::encode_attributes(one_component.value());
    expect(one_component_encoded.ok(), "one-component attribute ID must encode");
    expect(
        list_stream::dump_compact(one_component_encoded.value()) == one_component_fixture,
        "one-component attribute record must remain canonical");

    expect_failure(
        form_stream::decode_attributes(list_stream::parse("{{-1},0,{1},{0}}")),
        "OOF1102",
        "$/2/2",
        "attribute count mismatch must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse("{{1},0,{0},{0}}")),
        "OOF1106",
        "$/2/0/0",
        "attribute version mismatch must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse(
            "{{-1},2,{1,{{1,01234567-89ab-cdef-0123-456789abcdef},1,0,1,\"Value\","
            "{\"Other\"}}},{0}}")),
        "OOF1108",
        "$/2/2/1/5",
        "malformed attribute type must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse(
            "{{-1},0,{0},{1,{-1,{1,{0}}}}}")),
        "OOF1105",
        "$/2/3/1/0",
        "negative control ID must be rejected");
}

void test_attribute_encode_validation() {
    form_stream::AttributesRecord invalid;
    invalid.attributes.push_back(form_stream::AttributeRecord{
        composite(1, "not-a-guid"),
        false,
        false,
        "Value",
        {},
    });
    expect_failure(
        form_stream::encode_attributes(invalid),
        "OOF1107",
        "$/2/2/1/0",
        "encoder must reject malformed CompositeID UUID");
}

void test_empty_attributes_allocator_header() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children.push_back(model::ControlRef{model::ObjectId{10}});
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{
        model::ObjectId{10},
        "Run",
        model::ButtonPayload{},
    });

    auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "high-ID Button must encode using the current writer allocation count");
    expect(
        encoded.value().items[2].items[1].atom == "11",
        "current writer allocation count must be derived from the high object ID");

    auto fresh_designer = encoded.value();
    fresh_designer.items[2].items[1] = list_stream::ListValue::raw_atom("1");
    const auto decoded = form_stream::decode_document(fresh_designer, "Main");
    expect(decoded.ok(), "empty attribute allocation header from fresh Designer must decode");
    expect(
        decoded.value().collections().controls.front().id == model::ObjectId{10},
        "empty attribute allocation header must not constrain Button IDs");

    const auto normalized = form_stream::encode_document(decoded.value());
    expect(normalized.ok(), "decoded fresh-Designer form must encode");
    expect(
        normalized.value().items[2].items[1].atom == "11",
        "empty attribute allocation header must normalize to the current writer value");

    auto unknown_slot_count = encoded.value();
    unknown_slot_count.items[2].items[1] = list_stream::ListValue::raw_atom("2");
    expect_failure(
        form_stream::decode_document(unknown_slot_count, "Main"),
        "OOF1114",
        "$/2/1",
        "unsupported empty attribute allocation header must be rejected");
}

void test_multiple_top_level_buttons_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{7}},
        model::ControlRef{model::ObjectId{12}},
    };
    model::OrdinaryFormDocument document(std::move(form));

    model::ControlNode first{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    first.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Запуск"));
    first.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    first.position.left.set(11);
    first.position.top.set(12);
    first.position.width.set(120);
    first.position.height.set(24);
    first.events.push_back(model::EventRef{model::ObjectId{20}});
    document.add_event(model::Event{model::ObjectId{20}, "Click", "RunHandler", model::ControlRef{model::ObjectId{2}}});
    document.add_control(std::move(first));

    model::ControlNode second{model::ObjectId{7}, "Cancel", model::ButtonPayload{}};
    second.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Отмена"));
    second.position.left.set(145);
    second.position.top.set(12);
    second.position.width.set(90);
    second.position.height.set(24);
    second.events.push_back(model::EventRef{model::ObjectId{21}});
    document.add_event(model::Event{model::ObjectId{21}, "Click", "CancelHandler", model::ControlRef{model::ObjectId{7}}});
    document.add_control(std::move(second));

    model::ControlNode third{model::ObjectId{12}, "Help", model::ButtonPayload{}};
    third.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Справка"));
    third.position.left.set(250);
    third.position.top.set(12);
    third.position.width.set(90);
    third.position.height.set(24);
    document.add_control(std::move(third));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "two named top-level Buttons must encode");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "three top-level Buttons must decode");
    const auto& controls = decoded.value().collections().controls;
    expect(controls.size() == 3, "all Buttons must materialize as named controls");
    expect(controls[0].id == model::ObjectId{2} && controls[0].name == "Run",
        "first Button identity and order must survive");
    expect(controls[1].id == model::ObjectId{7} && controls[1].name == "Cancel",
        "second Button identity and order must survive");
    expect(controls[2].id == model::ObjectId{12} && controls[2].name == "Help",
        "third Button identity and order must survive");
    expect(std::get<std::string>(controls[0].properties().find(model::PropertyId::from_name("Caption"))->value) == "Запуск",
        "first Button Caption must survive");
    expect(std::get<bool>(controls[0].properties().find(model::PropertyId::from_name("Enabled"))->value) == false,
        "first Button Enabled=false must survive");
    expect(std::get<std::string>(controls[1].properties().find(model::PropertyId::from_name("Caption"))->value) == "Отмена",
        "second Button Caption must survive");
    expect(std::get<std::string>(controls[2].properties().find(model::PropertyId::from_name("Caption"))->value) == "Справка",
        "third Button Caption must survive");
    expect(controls[0].position.left.value() == 11 && controls[1].position.left.value() == 145 &&
        controls[2].position.left.value() == 250,
        "each Button Position must survive independently");
    expect(controls[0].events.front().id() != controls[1].events.front().id() &&
        controls[0].events.front().id() > model::ObjectId{12} && controls[1].events.front().id() > model::ObjectId{12},
        "synthetic event IDs must be unique and above stored object IDs");
    expect(decoded.value().find_event(controls[0].events.front().id())->handler == "RunHandler" &&
        decoded.value().find_event(controls[1].events.front().id())->handler == "CancelHandler",
        "each Button Click handler must remain attached to its owner");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded two-Button model must re-encode");
    expect(list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "three-Button storage must round-trip in order without drift");

    auto wrong_sibling_index = encoded.value();
    wrong_sibling_index.items[1].items[2].items[2].items[2].items[3].items[21] =
        list_stream::ListValue::raw_atom("0");
    expect_failure(
        form_stream::decode_document(wrong_sibling_index, "Main"),
        "OOF1114",
        "$/1/2/2/2/3",
        "Button geometry with an incorrect sibling index must be rejected");
}

void test_two_button_sibling_index() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{4}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{model::ObjectId{2}, "First", model::ButtonPayload{}});
    document.add_control(model::ControlNode{model::ObjectId{4}, "Second", model::ButtonPayload{}});

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "two Buttons must encode with derived sibling indexes");
    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok() && decoded.value().collections().controls.size() == 2,
        "two Buttons must decode with matching sibling indexes");

    auto wrong_sibling_index = encoded.value();
    wrong_sibling_index.items[1].items[2].items[2].items[2].items[3].items[21] =
        list_stream::ListValue::raw_atom("0");
    expect_failure(
        form_stream::decode_document(wrong_sibling_index, "Main"),
        "OOF1114",
        "$/1/2/2/2/3",
        "second Button must reject a sibling index of zero");
}

void test_platform_empty_document_fixture() {
    constexpr std::string_view fixture = R"OOF(
{27,{18,{{1,1,{"ru","Form"}},1,4294967295},{09ccdc77-ea1a-4a6d-ab1c-3435eada2433,{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},26,0,0,0,0,0,0,{10,1,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},0,1,{1,1,{6,{1,1,{"ru","Страница1"}},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},-1,1,1,"Страница1",1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},1}},1,1,0,4,{2,8,1,1,1,0,0,0,0},{2,8,0,1,2,0,0,0,0},{2,392,1,1,3,0,0,8,0},{2,292,0,1,4,0,0,8,0},0,4294967295,5,64,0,{4,4,{0},4},0,0,57,0,0},{0}},{0}},400,300,1,0,1,4,4,3,400,300,96},{{-1},3,{0},{0}},{00000000-0000-0000-0000-000000000000,0},{0},1,4,1,0,0,0,{0},{0},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},1,2,0,0,1,1}
)OOF";
    const auto payload = list_stream::parse(fixture);
    const auto decoded = form_stream::decode_document(payload, "Empty");
    expect(decoded.ok(), "platform empty-form fixture must decode into the product model");
    expect(decoded.value().form().name == "Empty", "external form name must be retained");
    expect(decoded.value().collections().controls.empty(), "empty fixture must have no controls");
    const auto* caption = decoded.value().form().properties.find(
        model::PropertyId::from_name("Caption"));
    expect(caption != nullptr, "platform form caption must materialize");
    expect(std::get<std::string>(caption->value) == "Form", "platform form caption mismatch");

    const auto encoded = form_stream::encode_document(decoded.value());
    expect(encoded.ok(), "decoded platform empty form must encode");
    expect(
        list_stream::dump_compact(encoded.value()) == list_stream::dump_compact(payload),
        "platform empty-form storage must canonicalize without semantic drift");
}

}  // namespace

int main() {
    try {
        test_outer_format_probe();
        test_layout_probe();
        test_runtime_envelope();
        test_attributes();
        test_attribute_encode_validation();
        test_empty_attributes_allocator_header();
        test_two_button_sibling_index();
        test_multiple_top_level_buttons_round_trip();
        test_platform_empty_document_fixture();
    } catch (const std::exception& error) {
        std::cerr << "form stream tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form stream tests: PASS\n";
    return 0;
}
