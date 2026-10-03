#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oof/model/metamodel.hpp"
#include "oof/source/form_xml.hpp"
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

std::size_t geometry_tail_start(const list_stream::ListValue& geometry) {
    std::size_t cursor = 12;
    for (std::size_t edge = 0; edge < 6; ++edge) {
        const auto count = static_cast<std::size_t>(std::stoul(geometry.items.at(cursor).atom));
        cursor += 1 + count;
    }
    expect(geometry.items.size() == cursor + 5, "geometry tail must follow the variable anchor records");
    return cursor;
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
    expect(encoded.ok(), encoded ? "high-ID Button must encode using the current writer allocation count" :
        encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message + " expected=" +
        encoded.diagnostics().front().expected + " actual=" + encoded.diagnostics().front().actual);
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

void test_attribute_allocator_is_separate_from_control_ids() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children.push_back(model::ControlRef{model::ObjectId{4}});
    model::OrdinaryFormDocument document(std::move(form));
    document.add_attribute(model::Attribute{
        model::ObjectId{1},
        "Value",
        {},
    });
    document.add_control(model::ControlNode{
        model::ObjectId{4},
        "Run",
        model::ButtonPayload{},
    });

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "attribute and control ID spaces must encode independently");
    expect(encoded.value().items[1].items[1].items[1].atom == "4",
        "form header max ID must include control ID 4");
    expect(encoded.value().items[2].items[1].atom == "3",
        "attribute slot count must use attribute ID 1, not control ID 4");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "attribute ID 1 and control ID 4 must decode with slot count 3");

    auto wrong_attribute_slots = encoded.value();
    wrong_attribute_slots.items[2].items[1] = list_stream::ListValue::raw_atom("5");
    expect_failure(
        form_stream::decode_document(wrong_attribute_slots, "Main"),
        "OOF1114",
        "$/2/1",
        "nonempty attribute slot count must be checked in its own ID space");
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
    wrong_sibling_index.items[1].items[2].items[2].items[2].items[3].items[geometry_tail_start(wrong_sibling_index.items[1].items[2].items[2].items[2].items[3]) + 1] =
        list_stream::ListValue::raw_atom("0");
    expect_failure(
        form_stream::decode_document(wrong_sibling_index, "Main"),
        "OOF1114",
        "$/1/2/2/2/3/20",
        "Button geometry with an incorrect sibling index must be rejected");
}

void test_button_multiline_round_trip_and_validation() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{7}},
        model::ControlRef{model::ObjectId{12}},
    };
    model::OrdinaryFormDocument document(std::move(form));

    model::ControlNode first{model::ObjectId{2}, "First", model::ButtonPayload{}};
    first.properties().set_explicit(model::PropertyId::from_name("MultiLine"), true);
    document.add_control(std::move(first));
    model::ControlNode second{model::ObjectId{7}, "Second", model::ButtonPayload{}};
    second.properties().set_explicit(model::PropertyId::from_name("MultiLine"), false);
    document.add_control(std::move(second));
    document.add_control(model::ControlNode{
        model::ObjectId{12}, "Third", model::ButtonPayload{}});

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "Button.MultiLine values must encode");
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    expect(records[1].items[2].items[1].items[5].atom == "0" &&
               records[2].items[2].items[1].items[5].atom == "0" &&
               records[3].items[2].items[1].items[5].atom == "0" &&
               records[1].items[2].items[1].items[10].atom == "1" &&
               records[2].items[2].items[1].items[10].atom == "0" &&
               records[3].items[2].items[1].items[10].atom == "0",
        "Button.MultiLine must change only property slot 10 and preserve slot 5");
    for (std::size_t button = 1; button < records.size(); ++button) {
        for (std::size_t slot = 0; slot < records[button].items[2].items[1].items.size(); ++slot) {
            if (slot == 10) {
                continue;
            }
            expect(
                list_stream::dump_compact(records[button].items[2].items[1].items[slot]) ==
                    list_stream::dump_compact(records[1].items[2].items[1].items[slot]),
                "Button.MultiLine must leave other property slots unchanged");
        }
    }

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "Button.MultiLine values must decode");
    const auto property_bool_or = [&](model::ObjectId id, bool fallback) {
        const auto* control = decoded.value().find_control(id);
        expect(control != nullptr, "Button.MultiLine control must exist after decoding");
        const auto* value = control->properties().find(model::PropertyId::from_name("MultiLine"));
        return value == nullptr ? fallback : std::get<bool>(value->value);
    };
    expect(property_bool_or(model::ObjectId{2}, false) &&
               !property_bool_or(model::ObjectId{7}, false) &&
               !property_bool_or(model::ObjectId{12}, false),
        "true and default-false Button.MultiLine values must remain independent");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded Button.MultiLine controls must re-encode");
    expect(
        list_stream::dump_compact(reencoded.value()) ==
            list_stream::dump_compact(encoded.value()),
        "Button.MultiLine storage must round-trip without changing other slots");

    auto invalid_slot_boolean = encoded.value();
    invalid_slot_boolean.items[1].items[2].items[2].items[1].items[2].items[1].items[10] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(
        form_stream::decode_document(invalid_slot_boolean, "Main"),
        "OOF1105",
        "$/1/2/2/1/2/1/10",
        "Button.MultiLine storage values outside Boolean 0 or 1 must be rejected");

    auto invalid_unknown_slot = encoded.value();
    invalid_unknown_slot.items[1].items[2].items[2].items[1].items[2].items[1].items[5] =
        list_stream::ListValue::raw_atom("1");
    expect_failure(
        form_stream::decode_document(invalid_unknown_slot, "Main"),
        "OOF1114",
        "$/1/2/2/1/2/1",
        "unsupported Button property slot 5 variation must be rejected");

    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "Main";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
    model::ControlNode wrong_type{model::ObjectId{2}, "WrongType", model::ButtonPayload{}};
    wrong_type.properties().set_explicit(
        model::PropertyId::from_name("MultiLine"), std::string("true"));
    invalid_document.add_control(std::move(wrong_type));
    expect_failure(
        form_stream::encode_document(invalid_document),
        "OOF1123",
        "$",
        "Button.MultiLine values with a non-Boolean model type must be rejected");
}

void test_button_then_label_decoration_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{3}},
        model::ControlRef{model::ObjectId{7}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{
        model::ObjectId{2},
        "Run",
        model::ButtonPayload{},
    });
    model::ControlNode label{
        model::ObjectId{3},
        "Label",
        model::LabelDecorationPayload{},
    };
    label.properties().set_explicit(
        model::PropertyId::from_name("Caption"),
        std::string("Updated caption"));
    label.properties().set_explicit(
        model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"HorizontalAlign", "Auto"});
    label.position.left.set(151);
    label.position.top.set(135);
    label.position.width.set(75);
    label.position.height.set(20);
    label.position.visible.set(false);
    document.add_control(std::move(label));
    model::ControlNode left_label{
        model::ObjectId{7}, "LeftLabel", model::LabelDecorationPayload{}};
    left_label.properties().set_explicit(
        model::PropertyId::from_name("Caption"), std::string("Explicit left"));
    left_label.properties().set_explicit(
        model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"HorizontalAlign", "Left"});
    document.add_control(std::move(left_label));

    const auto* align_descriptor = model::metamodel::find_property(
        model::ControlKind::label_decoration, "HorizontalAlign");
    expect(align_descriptor != nullptr &&
               align_descriptor->persistence == model::metamodel::PersistenceClass::persisted_editable &&
               align_descriptor->storage_codec == model::metamodel::StorageCodec::control_info &&
               align_descriptor->value_codec == model::metamodel::ValueCodec::enumeration,
        "LabelDecoration.HorizontalAlign must have a typed editable storage descriptor");

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "Button followed by LabelDecoration must encode" :
        encoded.diagnostics().front().code + ":" + encoded.diagnostics().front().path + ":" + encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "Button followed by LabelDecoration must decode");
    expect(decoded.value().form().children.size() == 3, "Button and two LabelDecorations order must survive");
    expect(std::get<model::ControlRef>(decoded.value().form().children[0]).id() == model::ObjectId{2} &&
               std::get<model::ControlRef>(decoded.value().form().children[1]).id() == model::ObjectId{3} &&
               std::get<model::ControlRef>(decoded.value().form().children[2]).id() == model::ObjectId{7},
        "mixed supported control IDs and order must survive");
    const auto* decoded_label = decoded.value().find_control(model::ObjectId{3});
    expect(decoded_label != nullptr && decoded_label->kind() == model::ControlKind::label_decoration,
        "LabelDecoration identity must survive round-trip");
    expect(decoded_label->name == "Label", "LabelDecoration name must survive round-trip");
    expect(decoded_label->properties().find(model::PropertyId::from_name("Caption")) != nullptr &&
               std::get<std::string>(decoded_label->properties().find(model::PropertyId::from_name("Caption"))->value) == "Updated caption",
        "named LabelDecoration Caption must survive round-trip");
    expect(decoded_label->position.left.value() == 151 && decoded_label->position.top.value() == 135 &&
               decoded_label->position.width.value() == 75 && decoded_label->position.height.value() == 20,
        "LabelDecoration Position must survive round-trip");
    expect(!decoded_label->position.visible.value(), "LabelDecoration Visible must survive round-trip");
    const auto* decoded_auto = decoded_label->properties().find(model::PropertyId::from_name("HorizontalAlign"));
    expect(decoded_auto != nullptr &&
               std::get<model::EnumerationValue>(decoded_auto->value) ==
                   model::EnumerationValue{"HorizontalAlign", "Auto"},
        "LabelDecoration HorizontalAlign Auto must round-trip");
    const auto* decoded_left = decoded.value().find_control(model::ObjectId{7});
    const auto* decoded_left_align = decoded_left->properties().find(
        model::PropertyId::from_name("HorizontalAlign"));
    expect(decoded_left_align != nullptr &&
               std::get<model::EnumerationValue>(decoded_left_align->value) ==
                   model::EnumerationValue{"HorizontalAlign", "Left"},
        "explicit LabelDecoration HorizontalAlign Left must round-trip");

    const auto rejects_alignment = [](model::EnumerationValue value) {
        model::Form invalid_form;
        invalid_form.id = model::ObjectId{1};
        invalid_form.name = "Invalid";
        invalid_form.children.push_back(model::ControlRef{model::ObjectId{2}});
        model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
        model::ControlNode invalid_label{
            model::ObjectId{2}, "Label", model::LabelDecorationPayload{}};
        invalid_label.properties().set_explicit(
            model::PropertyId::from_name("HorizontalAlign"), std::move(value));
        invalid_document.add_control(std::move(invalid_label));
        return !form_stream::encode_document(invalid_document);
    };
    expect(rejects_alignment(model::EnumerationValue{"VerticalAlign", "Auto"}),
        "HorizontalAlign must reject an enumeration of another type");
    expect(rejects_alignment(model::EnumerationValue{"HorizontalAlign", "Center"}),
        "unsupported HorizontalAlign members must be rejected");

    auto unknown_storage_value = encoded.value();
    auto& unknown_label = unknown_storage_value.items[1].items[2].items[2].items[2];
    unknown_label.items[2].items[1].items[3] = list_stream::ListValue::raw_atom("2");
    expect(
        !form_stream::decode_document(unknown_storage_value, "Main"),
        "unknown LabelDecoration.HorizontalAlign storage values must be rejected");

    auto unsupported_leaf = encoded.value();
    auto& label_record = unsupported_leaf.items[1].items[2].items[2].items[2];
    label_record.items[3].items[6].items[2] = list_stream::parse("{2,-1,6,7}");
    expect_failure(
        form_stream::decode_document(unsupported_leaf, "Main"),
        "OOF1114",
        "$/1/2/2/2/3/6/2",
        "unsupported LabelDecoration storage leaves must fail closed");
}

void test_fresh_checkbox_stream_decode() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Fresh";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
    model::OrdinaryFormDocument seed(std::move(form));
    seed.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
    seed.add_control(model::ControlNode{model::ObjectId{3}, "Placeholder", model::LabelDecorationPayload{}});
    auto stream = form_stream::encode_document(seed);
    expect(stream.ok(), "fresh fixture root must encode before inserting the observed CheckBox record");
    stream.value().items[1].items[2].items[2].items[2] = list_stream::parse(R"OOF(
{35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26,3,{1,{{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},7,{1,1,{"ru","Флажок1"}},1,0,1,0,100,1},4,0,0,0,0,0},{0}},{8,68,82,218,107,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,3,0,25},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,3,2,150},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},1,{0,3,1},0,1,{0,3,3},0,0,0,0,1,2,0,0},{14,"Флажок1",4294967295,0,0,0},{0}}
)OOF");
    stream.value().items[2] = list_stream::parse(R"OOF({{-1},4,{1,{{3},1,0,1,"Флажок1",{"Pattern",{"B"}}}},{1,{3,{1,{3}}}}})OOF");
    const auto decoded = form_stream::decode_document(stream.value(), "Fresh");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* check_box = decoded.value().find_control(model::ObjectId{3});
    expect(check_box != nullptr && check_box->kind() == model::ControlKind::check_box &&
               check_box->name == "Флажок1" && check_box->data_path &&
               check_box->data_path->attribute.id() == model::ObjectId{3},
        "fresh CheckBox record and same-numbered Boolean Attribute must decode by distinct object category");
    expect(check_box->position.left.value() == 68 && check_box->position.top.value() == 82 &&
               check_box->position.width.value() == 150 && check_box->position.height.value() == 25,
        "fresh CheckBox geometry must decode");
    model::TypeDomainPatternValue boolean_type;
    model::TypeDomainEntry boolean_entry;
    boolean_entry.term = model::TypeDomainTerm::boolean;
    boolean_type.entries.push_back(boolean_entry);
    expect(decoded.value().find_attribute(model::ObjectId{3})->type == boolean_type,
        "fresh CheckBox linked Attribute must decode exact Boolean token");
}

void test_button_label_input_field_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{3}},
        model::ControlRef{model::ObjectId{9}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    model::TypeDomainPatternValue string10;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{10, true};
    string10.entries.push_back(string_entry);
    document.add_attribute(model::Attribute{model::ObjectId{1}, "SyntheticValue", string10});
    document.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
    document.add_control(model::ControlNode{model::ObjectId{3}, "Label", model::LabelDecorationPayload{}});
    model::ControlNode input{model::ObjectId{9}, "InputSynthetic", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    input.position.left.set(231);
    input.position.top.set(135);
    input.position.width.set(70);
    input.position.height.set(30);
    input.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    input.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
    document.add_control(std::move(input));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "Button-Label-InputField profile must encode" :
        encoded.diagnostics().front().code + ":" + encoded.diagnostics().front().path + ":" + encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    const auto* decoded_input = decoded.value().find_control(model::ObjectId{9});
    expect(decoded_input && decoded_input->kind() == model::ControlKind::input_field,
        "InputField identity must survive round-trip");
    expect(decoded_input->name == "InputSynthetic" && decoded_input->data_path &&
               decoded_input->data_path->attribute.id() == model::ObjectId{1},
        "InputField name and named DataPath link must survive round-trip");
    expect(decoded_input->position.left.value() == 231 && decoded_input->position.top.value() == 135 &&
               decoded_input->position.width.value() == 70 && decoded_input->position.height.value() == 30,
        "InputField Position must survive round-trip");
    expect(decoded_input->properties().find(model::PropertyId::from_name("Enabled")) != nullptr &&
               !std::get<bool>(decoded_input->properties().find(model::PropertyId::from_name("Enabled"))->value),
        "InputField Enabled must survive round-trip");
    expect(decoded_input->properties().find(model::PropertyId::from_name("ReadOnly")) != nullptr &&
               std::get<bool>(decoded_input->properties().find(model::PropertyId::from_name("ReadOnly"))->value),
        "InputField ReadOnly must survive round-trip");
    expect(decoded.value().find_attribute(model::ObjectId{1})->type == string10,
        "linked Attribute String(10) type must survive round-trip");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded InputField document must re-encode");
    const auto redecode = form_stream::decode_document(reencoded.value(), "Main");
    expect(redecode.ok() && redecode.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1},
        "InputField DataPath must survive a second decode");
    expect(redecode.ok() && std::get<bool>(redecode.value().find_control(model::ObjectId{9})
        ->properties().find(model::PropertyId::from_name("ReadOnly"))->value),
        "InputField ReadOnly must survive two round-trips");
    const auto& read_only_payload = reencoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[13];
    expect(read_only_payload.atom == "1", "InputField ReadOnly must encode at the proven payload slot");

    const auto make_input_document = [](std::uint32_t length, bool variable, bool non_string = false, bool mixed = false,
        bool explicit_read_only_false = false) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "Main";
        form.children = {
            model::ControlRef{model::ObjectId{2}},
            model::ControlRef{model::ObjectId{3}},
            model::ControlRef{model::ObjectId{9}},
        };
        model::OrdinaryFormDocument input_document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = non_string ? model::TypeDomainTerm::numeric : model::TypeDomainTerm::string;
        entry.string = model::LengthQualifiers{length, variable};
        type.entries.push_back(entry);
        if (mixed) {
            model::TypeDomainEntry numeric_entry;
            numeric_entry.term = model::TypeDomainTerm::numeric;
            type.entries.push_back(numeric_entry);
        }
        input_document.add_attribute(model::Attribute{model::ObjectId{1}, "SyntheticValue", type});
        input_document.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
        input_document.add_control(model::ControlNode{model::ObjectId{3}, "Label", model::LabelDecorationPayload{}});
        model::ControlNode input_field{model::ObjectId{9}, "InputSynthetic", model::InputFieldPayload{}};
        input_field.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        if (explicit_read_only_false) {
            input_field.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), false);
        }
        input_document.add_control(std::move(input_field));
        return input_document;
    };

    for (const auto length : {0U, 17U, 20U, 37U, 64U}) {
        for (const bool variable : {false, true}) {
            const auto length_encoded = form_stream::encode_document(make_input_document(length, variable));
            expect(length_encoded.ok(), "single-string InputField qualifiers must encode");
            const auto& input_payload = length_encoded.value().items[1].items[2].items[2]
                .items[3].items[2].items[2].items[0];
            expect(input_payload.items[14].atom == std::to_string(length),
                "InputField String qualifier length must encode at payload slot 14");
            expect(input_payload.items[13].atom == "0",
                "InputField ReadOnly must remain unchanged at payload slot 13");
            const auto& default_read_only_payload = length_encoded.value().items[1].items[2].items[2].items[3]
                .items[2].items[2].items[0].items[13];
            expect(default_read_only_payload.atom == "0", "default InputField ReadOnly must encode as false");
            const auto length_decoded = form_stream::decode_document(length_encoded.value(), "Main");
            expect(length_decoded.ok(), "single-string InputField qualifiers must decode");
            const auto* round_trip_attribute = length_decoded.value().find_attribute(model::ObjectId{1});
            expect(round_trip_attribute->type.entries.front().string.length == length,
                "InputField string length must survive round-trip");
            expect(round_trip_attribute->type.entries.front().string.variable == (length == 0 ? true : variable),
                "InputField variable length must survive canonical round-trip");
            expect(length_decoded.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1},
                "InputField DataPath must survive string qualifier round-trip");
        }
    }

    auto length_mismatch = form_stream::encode_document(make_input_document(17, true));
    expect(length_mismatch.ok(), "InputField qualifier mismatch fixture must encode");
    length_mismatch.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[14] = list_stream::ListValue::raw_atom("64");
    expect_failure(
        form_stream::decode_document(length_mismatch.value(), "Main"),
        "OOF1114",
        "$/1/2/2/3/2",
        "InputField payload length that disagrees with its named String type must be rejected");

    const auto explicit_false = form_stream::encode_document(make_input_document(10, true, false, false, true));
    expect(explicit_false.ok(), "explicit InputField ReadOnly=false must be accepted");
    const auto& explicit_false_payload = explicit_false.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[13];
    expect(explicit_false_payload.atom == "0", "explicit InputField ReadOnly=false must encode as false");

    auto mismatch = encoded.value();
    auto& mismatch_info = mismatch.items[1].items[2].items[2].items[3].items[2];
    mismatch_info.items[1] = list_stream::parse("{\"Pattern\",{\"S\",20,1}}");
    expect_failure(
        form_stream::decode_document(mismatch, "Main"),
        "OOF1122",
        "$/1/2/2/3/2/1",
        "InputField type must match its linked Attribute");

    expect_failure(
        form_stream::encode_document(make_input_document(10, true, true)),
        "OOF1122",
        "$/InputField/DataPath",
        "non-string InputField attribute types must remain unsupported");
    expect_failure(
        form_stream::encode_document(make_input_document(10, true, false, true)),
        "OOF1122",
        "$/InputField/DataPath",
        "mixed InputField attribute types must remain unsupported");

    auto unsupported_leaf = encoded.value();
    auto& input_record = unsupported_leaf.items[1].items[2].items[2].items[3];
    input_record.items[2].items[2].items[0].items[45] = list_stream::ListValue::raw_atom("9");
    expect_failure(form_stream::decode_document(unsupported_leaf, "Main"), "OOF1114", "$/1/2/2/3/2",
        "unknown InputField info leaf must fail closed");

    auto unknown_read_only_neighbor = encoded.value();
    auto& payload = unknown_read_only_neighbor.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0];
    payload.items[12] = list_stream::ListValue::raw_atom("9");
    expect_failure(form_stream::decode_document(unknown_read_only_neighbor, "Main"), "OOF1114", "$/1/2/2/3/2",
        "unknown payload field beside ReadOnly must fail closed");
}

void test_single_input_field_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {model::ControlRef{model::ObjectId{9}}};
    model::OrdinaryFormDocument document(std::move(form));

    model::TypeDomainPatternValue string64;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{64, false};
    string64.entries.push_back(string_entry);
    document.add_attribute(model::Attribute{model::ObjectId{1}, "SyntheticValue", string64});

    model::ControlNode input{model::ObjectId{9}, "InputSynthetic", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    input.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    input.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
    document.add_control(std::move(input));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "single InputField profile must encode" : encoded.diagnostics().front().message);
    auto encoded_input = encoded.value();
    auto& input_record = encoded_input.items[1].items[2].items[2].items[1];
    expect(input_record.items[3].items[geometry_tail_start(input_record.items[3]) + 1].atom == "0" && input_record.items[3].items[geometry_tail_start(input_record.items[3]) + 2].atom == "1",
        "single InputField geometry must use sibling references 0 and 1");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    expect(decoded.value().form().children.size() == 1, "single InputField composition must survive round-trip");
    const auto* decoded_input = decoded.value().find_control(model::ObjectId{9});
    expect(decoded_input && decoded_input->data_path &&
               decoded_input->data_path->attribute.id() == model::ObjectId{1},
        "single InputField DataPath must survive round-trip");
    expect(decoded.value().find_attribute(model::ObjectId{1})->type == string64,
        "single InputField String(64) type must survive round-trip");
    expect(decoded_input->properties().find(model::PropertyId::from_name("Enabled")) != nullptr &&
               !std::get<bool>(decoded_input->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
               std::get<bool>(decoded_input->properties().find(model::PropertyId::from_name("ReadOnly"))->value),
        "single InputField Enabled and ReadOnly must survive round-trip");

    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "single InputField document must re-encode");
    const auto redecode = form_stream::decode_document(reencoded.value(), "Main");
    expect(redecode.ok() && redecode.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1},
        "single InputField DataPath must survive two round-trips");

    auto wrong_sibling_reference = encoded.value();
    wrong_sibling_reference.items[1].items[2].items[2].items[1].items[3].items[geometry_tail_start(wrong_sibling_reference.items[1].items[2].items[2].items[1].items[3]) + 1] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(
        form_stream::decode_document(wrong_sibling_reference, "Main"),
        "OOF1114",
        "$/1/2/2/1/3/20",
        "single InputField must reject triple-profile geometry references");
}

void test_two_input_fields_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{9}},
        model::ControlRef{model::ObjectId{12}},
    };
    model::OrdinaryFormDocument document(std::move(form));

    model::TypeDomainPatternValue string64;
    model::TypeDomainEntry string64_entry;
    string64_entry.term = model::TypeDomainTerm::string;
    string64_entry.string = model::LengthQualifiers{64, false};
    string64.entries.push_back(string64_entry);
    model::TypeDomainPatternValue string20;
    model::TypeDomainEntry string20_entry;
    string20_entry.term = model::TypeDomainTerm::string;
    string20_entry.string = model::LengthQualifiers{20, true};
    string20.entries.push_back(string20_entry);
    model::Attribute second_attribute{model::ObjectId{7}, "SecondValue", string20};
    second_attribute.main.set(true);
    document.add_attribute(std::move(second_attribute));
    model::Attribute first_attribute{model::ObjectId{1}, "FirstValue", string64};
    first_attribute.main.set(false);
    document.add_attribute(std::move(first_attribute));

    model::ControlNode second{model::ObjectId{12}, "SecondInput", model::InputFieldPayload{}};
    second.data_path = model::DataPath{model::AttributeRef{model::ObjectId{7}}, {}};
    second.properties().set_explicit(model::PropertyId::from_name("Enabled"), true);
    second.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), false);
    document.add_control(std::move(second));
    model::ControlNode first{model::ObjectId{9}, "FirstInput", model::InputFieldPayload{}};
    first.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    first.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    first.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
    first.position.left.set(242);
    first.position.top.set(146);
    document.add_control(std::move(first));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "two InputFields with two linked Attributes must encode" : encoded.diagnostics().front().message);
    const auto& children = encoded.value().items[1].items[2].items[2];
    expect(children.items[1].items[1].atom == "9" && children.items[2].items[1].atom == "12",
        "two InputFields must preserve Form.children order");
    expect(children.items[1].items[3].items[geometry_tail_start(children.items[1].items[3]) + 1].atom == "0" && children.items[1].items[3].items[geometry_tail_start(children.items[1].items[3]) + 2].atom == "1" &&
               children.items[2].items[3].items[geometry_tail_start(children.items[2].items[3]) + 1].atom == "1" && children.items[2].items[3].items[geometry_tail_start(children.items[2].items[3]) + 2].atom == "2",
        "two InputFields must use sequential sibling geometry references");
    const auto& links = encoded.value().items[2].items[3];
    expect(links.items[1].items[0].atom == "9" && links.items[2].items[0].atom == "12",
        "InputField links must follow Form.children order instead of collection insertion order");
    expect(encoded.value().items[2].items[1].atom == "8",
        "sparse Attribute IDs must determine the Attribute slot count");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    expect(decoded.value().form().children.size() == 2 &&
               std::get<model::ControlRef>(decoded.value().form().children[0]).id() == model::ObjectId{9} &&
               std::get<model::ControlRef>(decoded.value().form().children[1]).id() == model::ObjectId{12},
        "two InputFields must decode in child order");
    const auto* decoded_first = decoded.value().find_control(model::ObjectId{9});
    const auto* decoded_second = decoded.value().find_control(model::ObjectId{12});
    expect(decoded_first->data_path->attribute.id() == model::ObjectId{1} &&
               decoded_second->data_path->attribute.id() == model::ObjectId{7},
        "each InputField must resolve its own attribute link");
    expect(decoded.value().find_attribute(model::ObjectId{1})->type == string64 &&
               decoded.value().find_attribute(model::ObjectId{7})->type == string20 &&
               !decoded.value().find_attribute(model::ObjectId{1})->main.value() &&
               decoded.value().find_attribute(model::ObjectId{7})->main.value(),
        "sparse Attribute IDs, types, and Main flags must survive decode");
    const auto property_bool_or = [](const model::ControlNode& control, std::string_view name, bool fallback) {
        const auto* property = control.properties().find(model::PropertyId::from_name(name));
        return property == nullptr ? fallback : std::get<bool>(property->value);
    };
    expect(!property_bool_or(*decoded_first, "Enabled", true) &&
               property_bool_or(*decoded_first, "ReadOnly", false) &&
               property_bool_or(*decoded_second, "Enabled", true) &&
               !property_bool_or(*decoded_second, "ReadOnly", false),
        "each InputField must preserve independent Enabled and ReadOnly values");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded two-InputField document must re-encode");
    const auto redecode = form_stream::decode_document(reencoded.value(), "Main");
    expect(redecode.ok() && redecode.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1} &&
               redecode.value().find_control(model::ObjectId{12})->data_path->attribute.id() == model::ObjectId{7},
        "both InputField links must survive two round-trips");

    auto duplicate_link = encoded.value();
    duplicate_link.items[2].items[3].items[2] = duplicate_link.items[2].items[3].items[1];
    expect_failure(form_stream::decode_document(duplicate_link, "Main"), "OOF1122", "$/2/3",
        "duplicate control links must be rejected as ambiguous");

    auto missing_link = encoded.value();
    missing_link.items[2].items[3].items.pop_back();
    missing_link.items[2].items[3].items[0] = list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(missing_link, "Main"), "OOF1122", "$/2/3",
        "an InputField without a DataPath link must be rejected");

    auto leftover_link = encoded.value();
    auto extra_link = leftover_link.items[2].items[3].items[1];
    extra_link.items[0] = list_stream::ListValue::raw_atom("99");
    leftover_link.items[2].items[3].items.push_back(std::move(extra_link));
    leftover_link.items[2].items[3].items[0] = list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(leftover_link, "Main"), "OOF1122", "$/2/3",
        "links without a matching InputField must be rejected");

    auto dangling_target = encoded.value();
    dangling_target.items[2].items[3].items[1].items[1].items[1].items[0] =
        list_stream::ListValue::raw_atom("99");
    expect_failure(form_stream::decode_document(dangling_target, "Main"), "OOF1122", "$/2/3",
        "links to missing Attributes must be rejected");

    auto swapped_links = encoded.value();
    std::swap(swapped_links.items[2].items[3].items[1], swapped_links.items[2].items[3].items[2]);
    const auto decoded_swapped_links = form_stream::decode_document(swapped_links, "Main");
    expect(decoded_swapped_links.ok(), "reordered InputField links must decode");
    const auto* swapped_first = decoded_swapped_links.value().find_control(model::ObjectId{9});
    const auto* swapped_second = decoded_swapped_links.value().find_control(model::ObjectId{12});
    expect(swapped_first != nullptr && swapped_second != nullptr &&
               swapped_first->data_path.has_value() && swapped_second->data_path.has_value() &&
               swapped_first->data_path->attribute.id() == model::ObjectId{1} &&
               swapped_second->data_path->attribute.id() == model::ObjectId{7},
        "InputField links must resolve by control ID regardless of table order");

    auto wrong_geometry = encoded.value();
    wrong_geometry.items[1].items[2].items[2].items[2].items[3].items[geometry_tail_start(wrong_geometry.items[1].items[2].items[2].items[2].items[3]) + 2] =
        list_stream::ListValue::raw_atom("4");
    expect_failure(form_stream::decode_document(wrong_geometry, "Main"), "OOF1114", "$/1/2/2/2/3/20",
        "unknown two-InputField sibling geometry must fail closed");

    auto truncated_input = encoded.value();
    const auto input_guid = truncated_input.items[1].items[2].items[2].items[1].items[0];
    truncated_input.items[1].items[2].items[2].items[1] =
        list_stream::ListValue::list({input_guid});
    expect_failure(form_stream::decode_document(truncated_input, "Main"), "OOF1103", "$/1/2/2/1/1",
        "truncated InputField record must report the missing ID slot");

    model::Form mixed_form;
    mixed_form.id = model::ObjectId{1};
    mixed_form.name = "Mixed";
    mixed_form.children = {
        model::ControlRef{model::ObjectId{4}}, model::ControlRef{model::ObjectId{5}},
        model::ControlRef{model::ObjectId{6}}, model::ControlRef{model::ObjectId{18}},
        model::ControlRef{model::ObjectId{21}}, model::ControlRef{model::ObjectId{8}},
        model::ControlRef{model::ObjectId{13}}, model::ControlRef{model::ObjectId{24}},
    };
    model::OrdinaryFormDocument mixed_document(std::move(mixed_form));
    mixed_document.add_attribute(model::Attribute{model::ObjectId{1}, "ValueA", string64});
    mixed_document.add_attribute(model::Attribute{model::ObjectId{3}, "ValueB", string64});
    model::TypeDomainPatternValue boolean_type;
    model::TypeDomainEntry boolean_entry;
    boolean_entry.term = model::TypeDomainTerm::boolean;
    boolean_type.entries.push_back(boolean_entry);
    mixed_document.add_attribute(model::Attribute{model::ObjectId{5}, "SharedFlag", boolean_type});
    mixed_document.add_attribute(model::Attribute{model::ObjectId{21}, "SeparateFlag", boolean_type});
    model::ControlNode first_input{model::ObjectId{4}, "InputA", model::InputFieldPayload{}};
    first_input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    mixed_document.add_control(std::move(first_input));
    model::ControlNode first_button{model::ObjectId{5}, "Run", model::ButtonPayload{}};
    first_button.events.push_back(model::EventRef{model::ObjectId{30}});
    mixed_document.add_event(model::Event{model::ObjectId{30}, "Click", "RunProbe", model::ControlRef{model::ObjectId{5}}});
    mixed_document.add_control(std::move(first_button));
    mixed_document.add_control(model::ControlNode{model::ObjectId{6}, "Label", model::LabelDecorationPayload{}});
    model::ControlNode second_input{model::ObjectId{18}, "InputB", model::InputFieldPayload{}};
    second_input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
    mixed_document.add_control(std::move(second_input));
    model::ControlNode second_button{model::ObjectId{21}, "Cancel", model::ButtonPayload{}};
    second_button.events.push_back(model::EventRef{model::ObjectId{31}});
    mixed_document.add_event(model::Event{model::ObjectId{31}, "Click", "CancelProbe", model::ControlRef{model::ObjectId{21}}});
    mixed_document.add_control(std::move(second_button));
    model::ControlNode shared_flag_a{model::ObjectId{8}, "SharedFlagA", model::CheckBoxPayload{}};
    shared_flag_a.data_path = model::DataPath{model::AttributeRef{model::ObjectId{5}}, {}};
    shared_flag_a.position.left.set(68);
    shared_flag_a.position.top.set(82);
    shared_flag_a.position.width.set(150);
    shared_flag_a.position.height.set(25);
    shared_flag_a.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Flag A"));
    mixed_document.add_control(std::move(shared_flag_a));
    model::ControlNode shared_flag_b{model::ObjectId{13}, "SharedFlagB", model::CheckBoxPayload{}};
    shared_flag_b.data_path = model::DataPath{model::AttributeRef{model::ObjectId{5}}, {}};
    shared_flag_b.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    mixed_document.add_control(std::move(shared_flag_b));
    model::ControlNode separate_flag{model::ObjectId{24}, "SeparateFlag", model::CheckBoxPayload{}};
    separate_flag.data_path = model::DataPath{model::AttributeRef{model::ObjectId{21}}, {}};
    mixed_document.add_control(std::move(separate_flag));

    const std::vector<model::ObjectId> expected_order{
        model::ObjectId{4}, model::ObjectId{5}, model::ObjectId{6}, model::ObjectId{18},
        model::ObjectId{21}, model::ObjectId{8}, model::ObjectId{13}, model::ObjectId{24}};
    const auto mixed_encoded = form_stream::encode_document(mixed_document);
    expect(mixed_encoded.ok(), "mixed supported controls must encode in ChildItems order");
    const auto& mixed_child_records = mixed_encoded.value().items[1].items[2].items[2];
    for (std::size_t index = 0; index < expected_order.size(); ++index) {
        const auto& record = mixed_child_records.items[index + 1];
        const auto& geometry = record.items[3];
        const auto control_id = record.items[1].atom;
        expect(geometry.items.size() == 23,
            "unbound controls must use the 23-field geometry form without implicit self-links");
        for (std::size_t slot = 6; slot < 12; ++slot) {
            expect(list_stream::dump_compact(geometry.items[slot]) == "{0,{2,-1,6,0},{2,-1,6,0}}",
                "unbound geometry slots must use the explicit empty-binding sentinel");
        }
        const auto logical_position = std::ranges::find(
            expected_order, model::ObjectId{std::stoull(control_id)});
        expect(logical_position != expected_order.end(),
            "every stored record must map to a logical ChildItems position");
        const auto logical_index = static_cast<std::size_t>(
            std::distance(expected_order.begin(), logical_position));
        expect(geometry.items[geometry_tail_start(geometry) + 1].atom == std::to_string(logical_index) &&
                   geometry.items[geometry_tail_start(geometry) + 2].atom == std::to_string(logical_index + 1),
            "mixed control geometry sibling indexes must derive from ChildItems order");
    }
    const auto mixed_decoded = form_stream::decode_document(mixed_encoded.value(), "Mixed");
    expect(mixed_decoded.ok(), "mixed supported controls and DataPaths must decode");
    expect(mixed_decoded.value().form().children.size() == expected_order.size(), "all mixed ChildItems must survive");
    for (std::size_t index = 0; index < expected_order.size(); ++index) {
        expect(std::get<model::ControlRef>(mixed_decoded.value().form().children[index]).id() == expected_order[index],
            "mixed ChildItems order must survive round-trip");
    }
    expect(mixed_decoded.value().find_control(model::ObjectId{4})->data_path->attribute.id() == model::ObjectId{1} &&
               mixed_decoded.value().find_control(model::ObjectId{18})->data_path->attribute.id() == model::ObjectId{3},
        "InputField DataPaths must resolve by control ID in mixed order");
    expect(mixed_decoded.value().find_control(model::ObjectId{8})->data_path->attribute.id() == model::ObjectId{5} &&
               mixed_decoded.value().find_control(model::ObjectId{13})->data_path->attribute.id() == model::ObjectId{5} &&
               mixed_decoded.value().find_control(model::ObjectId{24})->data_path->attribute.id() == model::ObjectId{21},
        "CheckBox links must resolve shared and separate Boolean attributes even when IDs overlap controls");
    expect(mixed_decoded.value().find_attribute(model::ObjectId{5})->type == boolean_type &&
               mixed_decoded.value().find_attribute(model::ObjectId{21})->type == boolean_type,
        "CheckBox Boolean attribute TypeDomain must survive round-trip");
    expect(mixed_decoded.value().find_control(model::ObjectId{13})->properties().find(
               model::PropertyId::from_name("Enabled")) != nullptr &&
               !std::get<bool>(mixed_decoded.value().find_control(model::ObjectId{13})->properties().find(
                   model::PropertyId::from_name("Enabled"))->value),
        "CheckBox Enabled must use its proven base property");
    expect(mixed_decoded.value().find_event(mixed_decoded.value().find_control(model::ObjectId{5})->events.front().id())->handler == "RunProbe" &&
               mixed_decoded.value().find_event(mixed_decoded.value().find_control(model::ObjectId{21})->events.front().id())->handler == "CancelProbe",
        "each mixed-order Button handler must remain attached to its owner");

    auto wrong_checkbox_target = mixed_encoded.value();
    wrong_checkbox_target.items[2].items[3].items[2].items[1].items[1].items[0] =
        list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(wrong_checkbox_target, "Mixed"),
        "CheckBox linked to a non-Boolean Attribute must be rejected");

    const auto rejects_checkbox_model = [](model::TypeDomainPatternValue type,
                                           std::vector<std::string> members = {},
                                           bool add_unsupported_property = false) {
        model::Form invalid_form;
        invalid_form.id = model::ObjectId{1};
        invalid_form.name = "InvalidCheckBox";
        invalid_form.children.push_back(model::ControlRef{model::ObjectId{8}});
        model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
        invalid_document.add_attribute(model::Attribute{model::ObjectId{8}, "Flag", std::move(type)});
        model::ControlNode invalid_checkbox{model::ObjectId{8}, "Flag", model::CheckBoxPayload{}};
        invalid_checkbox.data_path = model::DataPath{model::AttributeRef{model::ObjectId{8}}, std::move(members)};
        if (add_unsupported_property) {
            invalid_checkbox.properties().set_explicit(
                model::PropertyId::from_name("ThreeState"), true);
        }
        invalid_document.add_control(std::move(invalid_checkbox));
        return !form_stream::encode_document(invalid_document);
    };
    expect(rejects_checkbox_model(string64),
        "CheckBox must reject a string attribute target");
    expect(rejects_checkbox_model(boolean_type, {"Nested"}),
        "CheckBox must reject nested DataPath members");
    expect(rejects_checkbox_model(boolean_type, {}, true),
        "CheckBox must reject an unproven property");

    auto unsupported_checkbox_geometry = mixed_encoded.value();
    const auto check_box_record = std::ranges::find(
        unsupported_checkbox_geometry.items[1].items[2].items[2].items, std::string("8"),
        [](const list_stream::ListValue& item) { return item.items.size() > 1 ? item.items[1].atom : std::string{}; });
    expect(check_box_record != unsupported_checkbox_geometry.items[1].items[2].items[2].items.end(),
        "mixed stream must contain the CheckBox geometry record");
    check_box_record->items[3].items[6].items[2] = list_stream::parse("{2,-1,6,7}");
    expect_failure(form_stream::decode_document(unsupported_checkbox_geometry, "Mixed"), "OOF1114",
        "$/1/2/2/4/3/6/2", "CheckBox must reject a non-sentinel secondary tuple");

    auto changed_button_index = mixed_encoded.value();
    changed_button_index.items[1].items[2].items[2].items[2].items[3].items[geometry_tail_start(changed_button_index.items[1].items[2].items[2].items[2].items[3]) + 2] =
        list_stream::ListValue::raw_atom("9");
    expect_failure(form_stream::decode_document(changed_button_index, "Mixed"), "OOF1114", "$/1/2/2/2/3/20",
        "Button must reject geometry sibling indexes that differ from ChildItems");

    auto unsupported_action_text = mixed_encoded.value();
    auto& first_action = unsupported_action_text.items[1].items[2].items[2].items[2]
        .items[2].items[2].items[1].items[2].items[2];
    first_action.items[2] = list_stream::parse("{1,1,{\"ru\",\"different presentation\"}}");
    expect_failure(form_stream::decode_document(unsupported_action_text, "Mixed"), "OOF1114", "$/1/2/2/2/2/2/1/2/2/2",
        "noncanonical Button action presentation must fail closed");
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
    wrong_sibling_index.items[1].items[2].items[2].items[2].items[3].items[geometry_tail_start(wrong_sibling_index.items[1].items[2].items[2].items[2].items[3]) + 1] =
        list_stream::ListValue::raw_atom("0");
    expect_failure(
        form_stream::decode_document(wrong_sibling_index, "Main"),
        "OOF1114",
        "$/1/2/2/2/3/20",
        "second Button must reject a sibling index of zero");
}

void test_six_reordered_controls_use_logical_geometry_ordinals() {
    model::TypeDomainPatternValue string64;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{64, false};
    string64.entries.push_back(string_entry);

    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Reordered";
    const std::vector<model::ObjectId> logical_order{
        model::ObjectId{27}, model::ObjectId{18}, model::ObjectId{21},
        model::ObjectId{15}, model::ObjectId{9}, model::ObjectId{12}};
    for (const auto id : logical_order) form.children.push_back(model::ControlRef{id});
    model::OrdinaryFormDocument document(std::move(form));
    document.add_attribute(model::Attribute{model::ObjectId{3}, "ValueB", string64});
    document.add_attribute(model::Attribute{model::ObjectId{1}, "ValueA", string64});

    model::ControlNode input18{model::ObjectId{18}, "InputB", model::InputFieldPayload{}};
    input18.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
    document.add_control(std::move(input18));
    model::ControlNode button21{model::ObjectId{21}, "Cancel", model::ButtonPayload{}};
    button21.events.push_back(model::EventRef{model::ObjectId{41}});
    document.add_event(model::Event{model::ObjectId{41}, "Click", "CancelProbe", model::ControlRef{model::ObjectId{21}}});
    document.add_control(std::move(button21));
    document.add_control(model::ControlNode{model::ObjectId{27}, "LabelTop", model::LabelDecorationPayload{}});
    model::ControlNode input9{model::ObjectId{9}, "InputA", model::InputFieldPayload{}};
    input9.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    document.add_control(std::move(input9));
    model::ControlNode button12{model::ObjectId{12}, "Run", model::ButtonPayload{}};
    button12.events.push_back(model::EventRef{model::ObjectId{42}});
    document.add_event(model::Event{model::ObjectId{42}, "Click", "RunProbe", model::ControlRef{model::ObjectId{12}}});
    document.add_control(std::move(button12));
    document.add_control(model::ControlNode{model::ObjectId{15}, "LabelBottom", model::LabelDecorationPayload{}});

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "six reordered controls must encode");
    const auto& children = encoded.value().items[1].items[2].items[2];
    const std::vector<std::uint64_t> physical_ids{9, 12, 15, 18, 21, 27};
    const std::vector<std::uint32_t> physical_ordinals{4, 5, 3, 1, 2, 0};
    for (std::size_t index = 0; index < physical_ids.size(); ++index) {
        const auto& record = children.items[index + 1];
        expect(std::stoull(record.items[1].atom) == physical_ids[index],
            "writer must sort physical control records by numeric ID");
        expect(record.items[3].items[geometry_tail_start(record.items[3]) + 1].atom == std::to_string(physical_ordinals[index]) &&
                   record.items[3].items[geometry_tail_start(record.items[3]) + 2].atom == std::to_string(physical_ordinals[index] + 1),
            "physical control records must retain logical geometry ordinals");
    }
    const auto& links = encoded.value().items[2].items[3];
    expect(links.items[1].items[0].atom == "9" && links.items[2].items[0].atom == "18",
        "writer must sort InputField links by numeric control ID");

    const auto verify_logical = [&](const auto& storage, std::string_view message) {
        const auto decoded = form_stream::decode_document(storage, "Reordered");
        expect(decoded.ok(), decoded ? message : decoded.diagnostics().front().message);
        expect(decoded.value().form().children.size() == logical_order.size(), message);
        for (std::size_t index = 0; index < logical_order.size(); ++index) {
            expect(std::get<model::ControlRef>(decoded.value().form().children[index]).id() == logical_order[index], message);
        }
        expect(decoded.value().find_control(model::ObjectId{18})->data_path->attribute.id() == model::ObjectId{3} &&
                   decoded.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1},
            "sorted InputField links must still resolve by control ID");
        expect(decoded.value().find_event(decoded.value().find_control(model::ObjectId{21})->events.front().id())->handler == "CancelProbe" &&
                   decoded.value().find_event(decoded.value().find_control(model::ObjectId{12})->events.front().id())->handler == "RunProbe",
            "reordered Button handlers must remain with their owners");
    };
    verify_logical(encoded.value(), "sorted physical records must decode in logical ChildItems order");

    auto permuted = encoded.value();
    std::swap(permuted.items[1].items[2].items[2].items[1], permuted.items[1].items[2].items[2].items[6]);
    verify_logical(permuted, "noncanonical physical record permutation must preserve logical ChildItems order");

    auto duplicate_ordinal = encoded.value();
    auto& duplicate_geometry = duplicate_ordinal.items[1].items[2].items[2].items[6].items[3];
    const auto duplicate_tail = geometry_tail_start(duplicate_geometry);
    duplicate_geometry.items[duplicate_tail + 1] = list_stream::ListValue::raw_atom("4");
    duplicate_geometry.items[duplicate_tail + 2] = list_stream::ListValue::raw_atom("5");
    expect_failure(form_stream::decode_document(duplicate_ordinal, "Reordered"), "OOF1114", "$/1/2/2/6/3/19",
        "duplicate geometry ordinals must be rejected");

    auto out_of_range_ordinal = encoded.value();
    auto& out_of_range_geometry = out_of_range_ordinal.items[1].items[2].items[2].items[6].items[3];
    const auto out_of_range_tail = geometry_tail_start(out_of_range_geometry);
    out_of_range_geometry.items[out_of_range_tail + 1] = list_stream::ListValue::raw_atom("6");
    out_of_range_geometry.items[out_of_range_tail + 2] = list_stream::ListValue::raw_atom("7");
    expect_failure(form_stream::decode_document(out_of_range_ordinal, "Reordered"), "OOF1114", "$/1/2/2/6/3/19",
        "out-of-range geometry ordinals must be rejected");

    auto wrong_next_index = encoded.value();
    wrong_next_index.items[1].items[2].items[2].items[6].items[3].items[geometry_tail_start(wrong_next_index.items[1].items[2].items[2].items[6].items[3]) + 2] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(wrong_next_index, "Reordered"), "OOF1114", "$/1/2/2/6/3/20",
        "geometry next index must equal logical ordinal plus one");
}

void test_manual_bindings_are_not_silently_discarded() {
    for (const auto kind : {model::ControlKind::button, model::ControlKind::label_decoration,
                           model::ControlKind::input_field, model::ControlKind::check_box}) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "ManualBindings";
        form.children.push_back(model::ControlRef{model::ObjectId{2}});
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode control{model::ObjectId{2}, "Item", model::ButtonPayload{}};
        if (kind == model::ControlKind::label_decoration) control.payload = model::LabelDecorationPayload{};
        if (kind == model::ControlKind::input_field) control.payload = model::InputFieldPayload{};
        if (kind == model::ControlKind::check_box) control.payload = model::CheckBoxPayload{};
        if (kind == model::ControlKind::input_field || kind == model::ControlKind::check_box) {
            model::TypeDomainPatternValue type;
            model::TypeDomainEntry entry;
            entry.term = kind == model::ControlKind::check_box
                ? model::TypeDomainTerm::boolean : model::TypeDomainTerm::string;
            type.entries.push_back(entry);
            document.add_attribute(model::Attribute{model::ObjectId{3}, "Value", std::move(type)});
            control.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        }
        document.add_control(control);
        for (const bool horizontal : {true, false}) {
            model::OrdinaryFormDocument manual(document.form());
            for (const auto& attribute : document.collections().attributes) manual.add_attribute(attribute);
            auto manual_control = control;
            manual_control.position.bindings.manual_horizontal.set(horizontal);
            manual_control.position.bindings.manual_vertical.set(!horizontal);
            manual.add_control(std::move(manual_control));
            const auto encoded = form_stream::encode_document(manual);
            expect(encoded.ok(), "both manual axes must encode for each supported control type");
            const auto decoded = form_stream::decode_document(encoded.value(), "ManualBindings");
            expect(decoded.ok(), "both manual axes must decode for each supported control type");
            const auto* roundtrip = decoded.value().find_control(model::ObjectId{2});
            expect(roundtrip != nullptr && roundtrip->position.bindings.manual_horizontal.value() == horizontal &&
                       roundtrip->position.bindings.manual_vertical.value() == !horizontal,
                "manual axes must survive Form.bin round-trip");
        }
    }
}

void test_anchor_bindings_round_trip_and_fanout() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Bindings";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{9}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode run{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    model::AnchorBinding right_to_form;
    right_to_form.coordinate = model::BindingCoordinate::right;
    right_to_form.target_coordinate = model::BindingCoordinate::right;
    right_to_form.offset.set(-380);
    model::AnchorBindingTarget form_left;
    form_left.coordinate = model::BindingCoordinate::left;
    form_left.offset.set(120);
    right_to_form.proportional = form_left;
    run.position.bindings.anchors.push_back(right_to_form);

    model::AnchorBinding top_to_control;
    top_to_control.coordinate = model::BindingCoordinate::top;
    top_to_control.target = model::ControlRef{model::ObjectId{9}};
    top_to_control.target_coordinate = model::BindingCoordinate::bottom;
    top_to_control.offset.set(15);
    model::AnchorBindingTarget control_left;
    control_left.target = model::ControlRef{model::ObjectId{9}};
    control_left.coordinate = model::BindingCoordinate::left;
    control_left.offset.set(-5);
    top_to_control.proportional = control_left;
    run.position.bindings.anchors.push_back(top_to_control);
    run.position.bindings.manual_horizontal.set(true);
    run.position.bindings.manual_vertical.set(true);

    model::ControlNode text{model::ObjectId{9}, "Text", model::ButtonPayload{}};
    model::AnchorBinding bottom_to_form;
    bottom_to_form.coordinate = model::BindingCoordinate::bottom;
    bottom_to_form.target_coordinate = model::BindingCoordinate::bottom;
    bottom_to_form.offset.set(40);
    text.position.bindings.anchors.push_back(bottom_to_form);
    model::AnchorBinding left_to_self;
    left_to_self.coordinate = model::BindingCoordinate::left;
    left_to_self.target = model::ControlRef{model::ObjectId{9}};
    left_to_self.target_coordinate = model::BindingCoordinate::right;
    text.position.bindings.anchors.push_back(left_to_self);
    document.add_control(std::move(run));
    document.add_control(std::move(text));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "primary and proportional bindings must encode" : encoded.diagnostics().front().message);
    const auto& root_body = encoded.value().items[1].items[2].items[1].items[1];
    expect(root_body.items[3].atom == "1" &&
               list_stream::dump_compact(root_body.items[4]) == "{0,9,1}" &&
               root_body.items[5].atom == "1" &&
               list_stream::dump_compact(root_body.items[6]) == "{0,2,3}" &&
               root_body.items[7].atom == "1" &&
               list_stream::dump_compact(root_body.items[8]) == "{0,2,3}",
        "root Bottom, Left, and Right fanout counts and tuples must be derived from outgoing links: " +
            list_stream::dump_compact(root_body));

    const auto& children = encoded.value().items[1].items[2].items[2];
    const auto find_record = [&](std::uint64_t id) -> const list_stream::ListValue& {
        for (std::size_t index = 1; index < children.items.size(); ++index) {
            const auto& record = children.items[index];
            if (record.is_list && record.items.size() > 1 && !record.items[1].is_list &&
                record.items[1].atom == std::to_string(id)) return record;
        }
        throw std::runtime_error("encoded control record is missing");
    };
    const auto& run_geometry = find_record(2).items[3];
    expect(list_stream::dump_compact(run_geometry.items[9]) == "{0,{2,0,3,-380},{2,0,2,120}}" &&
               list_stream::dump_compact(run_geometry.items[6]) == "{0,{2,9,1,15},{2,9,2,-5}}",
        "primary and proportional target IDs, edge codes, and offsets must map into shared geometry slots");
    const auto& text_geometry = find_record(9).items[3];
    expect(list_stream::dump_compact(text_geometry.items[7]) == "{0,{2,0,1,40},{2,-1,6,0}}" &&
               list_stream::dump_compact(text_geometry.items[8]) == "{0,{2,9,3,0},{2,-1,6,0}}",
        "Form and self-control primary targets must encode in their source-edge slots");

    const auto decoded = form_stream::decode_document(encoded.value(), "Bindings");
    expect(decoded.ok(), decoded ? "anchor binding graph must decode" : decoded.diagnostics().front().message);
    const auto* roundtrip_run = decoded.value().find_control(model::ObjectId{2});
    const auto* roundtrip_text = decoded.value().find_control(model::ObjectId{9});
    expect(roundtrip_run != nullptr && roundtrip_text != nullptr &&
               roundtrip_run->position.bindings.anchors.size() == 2 &&
               roundtrip_run->position.bindings.anchors[1].target_coordinate == model::BindingCoordinate::right &&
               !roundtrip_run->position.bindings.anchors[1].target.has_value() &&
               roundtrip_run->position.bindings.anchors[1].proportional.has_value() &&
               !roundtrip_run->position.bindings.anchors[1].proportional->target.has_value() &&
               roundtrip_run->position.bindings.anchors[1].proportional->coordinate == model::BindingCoordinate::left &&
               roundtrip_run->position.bindings.anchors[1].proportional->offset.value() == 120 &&
               roundtrip_run->position.bindings.anchors[0].target == model::ControlRef{model::ObjectId{9}} &&
               roundtrip_run->position.bindings.anchors[0].proportional->target == model::ControlRef{model::ObjectId{9}} &&
               roundtrip_run->position.bindings.manual_horizontal.value() &&
               roundtrip_run->position.bindings.manual_vertical.value() &&
               roundtrip_text->position.bindings.anchors.size() == 2 &&
               roundtrip_text->position.bindings.anchors[1].target == model::ControlRef{model::ObjectId{9}},
        "Form, cross-control, self, proportional, and manual bindings must survive decode; Run anchors=" +
            (roundtrip_run == nullptr ? std::string("missing") :
             std::to_string(roundtrip_run->position.bindings.anchors.size())) +
            ", Text anchors=" + (roundtrip_text == nullptr ? std::string("missing") :
             std::to_string(roundtrip_text->position.bindings.anchors.size())));
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "all binding tuples and fanout counts must be stable across decode and encode");

    auto stale_root = encoded.value();
    stale_root.items[1].items[2].items[1].items[1].items[6].items[1] = list_stream::ListValue::raw_atom("7");
    expect(!form_stream::decode_document(stale_root, "Bindings"),
        "root incoming tuples must be checked against the outgoing graph");
    auto unsupported_root_marker = encoded.value();
    unsupported_root_marker.items[1].items[2].items[1].items[0] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(unsupported_root_marker, "Bindings"), "OOF1114", "$/1/2/1/0",
        "unsupported root-panel envelope marker must be rejected");
    auto unsupported_root_trailer = encoded.value();
    unsupported_root_trailer.items[1].items[2].items[1].items[2] = list_stream::parse("{1}");
    expect_failure(form_stream::decode_document(unsupported_root_trailer, "Bindings"), "OOF1114", "$/1/2/1/2",
        "unsupported root-panel envelope trailer must be rejected");
    auto dangling_target = encoded.value();
    dangling_target.items[1].items[2].items[2].items[1].items[3].items[9].items[1].items[1] =
        list_stream::ListValue::raw_atom("77");
    expect(!form_stream::decode_document(dangling_target, "Bindings"),
        "primary target IDs absent from the form graph must be rejected");
    auto unknown_target_edge = encoded.value();
    unknown_target_edge.items[1].items[2].items[2].items[1].items[3].items[9].items[1].items[2] =
        list_stream::ListValue::raw_atom("6");
    expect(!form_stream::decode_document(unknown_target_edge, "Bindings"),
        "unknown target edge 6 must be rejected");
    model::OrdinaryFormDocument centered_source(document.form());
    for (const auto& source_control : document.collections().controls) {
        auto centered_control = source_control;
        if (centered_control.id == model::ObjectId{2}) {
            centered_control.position.bindings.anchors.front().coordinate = model::BindingCoordinate::horizontal_center;
        }
        centered_source.add_control(std::move(centered_control));
    }
    expect(!form_stream::encode_document(centered_source), "center coordinates must remain unsupported as sources");
    auto orphan_proportional = encoded.value();
    orphan_proportional.items[1].items[2].items[2].items[1].items[3].items[9].items[1] =
        list_stream::parse("{2,-1,6,0}");
    expect(!form_stream::decode_document(orphan_proportional, "Bindings"),
        "proportional tuple without a primary tuple must be rejected");
}

void test_platform_empty_document_fixture() {
    constexpr std::string_view fixture = R"OOF(
{27,{18,{{1,1,{"ru","Form"}},1,4294967295},{09ccdc77-ea1a-4a6d-ab1c-3435eada2433,{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},26,0,0,0,0,0,0,{10,1,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},0,1,{1,1,{6,{1,1,{"ru","Страница1"}},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},-1,1,1,"Страница1",1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},1}},1,1,0,4,{2,8,1,1,1,0,0,0,0},{2,8,0,1,2,0,0,0,0},{2,392,1,1,3,0,0,8,0},{2,292,0,1,4,0,0,8,0},0,4294967295,5,64,0,{4,4,{0},4},0,0,57,0,0},{0}},{0}},400,300,1,0,1,4,4,3,400,300,96},{{-1},3,{0},{0}},{00000000-0000-0000-0000-000000000000,0},{0},1,4,1,0,0,0,{0},{0},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},1,2,0,0,1,1}
)OOF";
    const auto payload = list_stream::parse(fixture);
    const auto decoded = form_stream::decode_document(payload, "Empty");
    expect(decoded.ok(), decoded ? "platform empty-form fixture must decode into the product model" :
        decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
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

void test_center_target_coordinates() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "CenterTargets";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{9}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode run{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    model::AnchorBinding binding;
    binding.coordinate = model::BindingCoordinate::right;
    binding.target_coordinate = model::BindingCoordinate::horizontal_center;
    model::AnchorBindingTarget proportional;
    proportional.coordinate = model::BindingCoordinate::vertical_center;
    binding.proportional = proportional;
    run.position.bindings.anchors.push_back(binding);
    document.add_control(std::move(run));
    model::ControlNode label{model::ObjectId{9}, "Label", model::ButtonPayload{}};
    model::AnchorBinding control_binding;
    control_binding.coordinate = model::BindingCoordinate::left;
    control_binding.target = model::ControlRef{model::ObjectId{2}};
    control_binding.target_coordinate = model::BindingCoordinate::vertical_center;
    model::AnchorBindingTarget control_proportional;
    control_proportional.target = model::ControlRef{model::ObjectId{2}};
    control_proportional.coordinate = model::BindingCoordinate::horizontal_center;
    control_binding.proportional = control_proportional;
    label.position.bindings.anchors.push_back(control_binding);
    document.add_control(std::move(label));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "center targets must encode" : encoded.diagnostics().front().message);
    const auto& children = encoded.value().items[1].items[2].items[2];
    const auto& geometry = children.items[1].items[3];
    expect(list_stream::dump_compact(geometry.items[9]) == "{0,{2,0,5,0},{2,0,4,0}}",
        "horizontal and vertical center targets must encode as platform edges 5 and 4");
    expect(list_stream::dump_compact(children.items[2].items[3].items[8]) == "{0,{2,2,4,0},{2,2,5,0}}",
        "control targets and proportional control targets must encode center edges 4 and 5");
    const auto& root_body = encoded.value().items[1].items[2].items[1].items[1];
    const auto root_text = list_stream::dump_compact(root_body);
    expect(root_text.find("},26,0,1,{0,2,3},0,1,{0,2,3},0,0,") != std::string::npos,
        "vertical center edge 4 and horizontal center edge 5 must populate incoming buckets 1 and 3: " + root_text);
    const auto decoded = form_stream::decode_document(encoded.value(), "CenterTargets");
    expect(decoded.ok(), decoded ? "center targets must decode" : decoded.diagnostics().front().message);
    const auto* roundtrip = decoded.value().find_control(model::ObjectId{2});
    expect(roundtrip != nullptr && roundtrip->position.bindings.anchors.size() == 1 &&
               roundtrip->position.bindings.anchors.front().target_coordinate == model::BindingCoordinate::horizontal_center &&
               roundtrip->position.bindings.anchors.front().proportional.has_value() &&
               roundtrip->position.bindings.anchors.front().proportional->coordinate == model::BindingCoordinate::vertical_center,
        "both center target enum values must round-trip independently");
    const auto* roundtrip_label = decoded.value().find_control(model::ObjectId{9});
    expect(roundtrip_label != nullptr && roundtrip_label->position.bindings.anchors.front().target_coordinate == model::BindingCoordinate::vertical_center &&
               roundtrip_label->position.bindings.anchors.front().proportional->coordinate == model::BindingCoordinate::horizontal_center,
        "center targets on controls must round-trip independently");

    model::OrdinaryFormDocument centered_source(document.form());
    for (const auto& source_control : document.collections().controls) {
        auto centered_control = source_control;
        if (centered_control.id == model::ObjectId{2}) {
            centered_control.position.bindings.anchors.front().coordinate = model::BindingCoordinate::vertical_center;
        }
        centered_source.add_control(std::move(centered_control));
    }
    expect_failure(form_stream::encode_document(centered_source), "OOF1122", "$/Position/Bindings/coordinate",
        "center coordinates must remain unsupported as sources");

    auto encoded_center_source = encoded.value();
    auto& center_source_geometry = encoded_center_source.items[1].items[2].items[2].items[1].items[3];
    center_source_geometry.items[11] = center_source_geometry.items[9];
    center_source_geometry.items[9] = list_stream::parse("{0,{2,-1,6,0},{2,-1,6,0}}");
    expect_failure(form_stream::decode_document(encoded_center_source, "CenterTargets"), "OOF1122",
        "$/Position/Bindings/coordinate", "center coordinates must remain unsupported as decoded sources");

    auto invalid_incoming = encoded.value();
    bool changed = false;
    std::string bad_source_edge;
    const auto corrupt_source_edge = [&](auto&& self, list_stream::ListValue& value) -> void {
        if (value.is_list && value.items.size() == 3 && !value.items[0].is_list && value.items[0].atom == "0" &&
            !value.items[1].is_list && value.items[1].atom == "2" && !value.items[2].is_list && value.items[2].atom == "3") {
            value.items[2] = list_stream::ListValue::raw_atom(bad_source_edge);
            changed = true;
            return;
        }
        for (auto& item : value.items) self(self, item);
    };
    for (const auto source_edge : {"4", "5"}) {
        changed = false;
        bad_source_edge = source_edge;
        invalid_incoming = encoded.value();
        auto& root_body = invalid_incoming.items[1].items[2].items[1].items[1];
        corrupt_source_edge(corrupt_source_edge, root_body);
        expect(changed, "test fixture must contain an incoming source edge tuple");
        expect(!form_stream::decode_document(invalid_incoming, "CenterTargets"),
            std::string("incoming source edge ") + source_edge + " must be rejected");
    }
}

void test_page_boundary_position_codec() {
    const auto boundaries = list_stream::parse(
        "{{2,6,1,1,1,0,0,0,0},{2,6,0,1,2,0,0,0,0},"
        "{2,296,1,1,3,0,0,40,0},{2,197,0,1,4,0,0,2,0}}");
    const model::ControlRef owner{model::ObjectId{2}};
    auto decoded = form_stream::decode_page_position(boundaries, 0, owner);
    expect(decoded.ok(), "proven Page boundary shape must decode to named Position");
    const auto& position = decoded.value();
    expect(position.left.value() == 6 && position.top.value() == 6 &&
               position.width.value() == 290 && position.height.value() == 191,
        "Page right/bottom coordinates must become dimensions, not be mislabeled as width/height");
    expect(position.bindings.anchors.size() == 2 &&
               position.bindings.anchors[0].target == owner &&
               position.bindings.anchors[0].coordinate == model::BindingCoordinate::right &&
               position.bindings.anchors[0].offset.value() == -40 &&
               position.bindings.anchors[1].coordinate == model::BindingCoordinate::bottom &&
               position.bindings.anchors[1].offset.value() == -2,
        "Page runtime constraints must be distinct from static rectangle coordinates");
    auto rebuilt = form_stream::encode_page_position(position, 0, owner);
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(boundaries),
        "named Page Position must rebuild the proven boundary records without retained source bytes");

    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "PageBoundary";
    form.children.emplace_back(owner);
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode panel{owner.id(), "Tabs", model::PanelPayload{}};
    panel.children.emplace_back(model::PageRef{model::ObjectId{1}});
    document.add_control(std::move(panel));
    model::Page page;
    page.id = model::ObjectId{1};
    page.name = "First";
    page.position.set(position);
    document.add_page(std::move(page));
    auto xml = oof::source::serialize_form_xml(document);
    expect(xml.ok(), "named Page boundary model must serialize as public XML");
    auto reparsed = oof::source::parse_form_xml(xml.value());
    expect(reparsed.ok(), "named Page boundary XML must parse independently");
    auto xml_rebuilt = form_stream::encode_page_position(
        reparsed.value().find_page(model::ObjectId{1})->position.value(), 0, owner);
    expect(xml_rebuilt.ok() && list_stream::dump_compact(xml_rebuilt.value()) == list_stream::dump_compact(boundaries),
        "Page boundaries must survive model -> XML -> model -> codec without source storage");
    const std::string original_offset = "offset=\"-40\"";
    const auto offset_at = xml.value().find(original_offset);
    expect(offset_at != std::string::npos, "Page runtime constraint must be a named editable XML offset");
    auto edited_xml = xml.value();
    edited_xml.replace(offset_at, original_offset.size(), "offset=\"-4\"");
    auto xml_edit = oof::source::parse_form_xml(edited_xml);
    expect(xml_edit.ok(), "editing the named Page offset must remain valid XML");
    auto edited_boundaries = form_stream::encode_page_position(
        xml_edit.value().find_page(model::ObjectId{1})->position.value(), 0, owner);
    expect(edited_boundaries.ok() && edited_boundaries.value().items[2].items[7].atom == "4" &&
               edited_boundaries.value().items[2].items[1].atom == "296",
        "named Page offset editing must preserve independent static coordinates");
    expect(!form_stream::encode_page_position(position, 0),
        "nested Page bindings must not silently become Form bindings");
    expect(!form_stream::decode_page_position(boundaries, 1, owner),
        "Page index mismatch must be rejected");

    auto invalid = boundaries;
    invalid.items[2].items[1] = list_stream::ListValue::raw_atom("5");
    expect(!form_stream::decode_page_position(invalid, 0, owner),
        "inverted Page rectangle must be rejected");
    invalid = boundaries;
    invalid.items[0].items[7] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_page_position(invalid, 0, owner),
        "unproven Left constraint must be rejected");
    invalid = boundaries;
    invalid.items[2].items[7] = list_stream::ListValue::raw_atom("-2147483648");
    expect(!form_stream::decode_page_position(invalid, 0, owner),
        "Page offset negation overflow must be rejected");

    auto edited = position;
    edited.width.set(330);
    edited.bindings.anchors[0].offset.set(-4);
    auto encoded_edit = form_stream::encode_page_position(edited, 0, owner);
    expect(encoded_edit.ok() && encoded_edit.value().items[2].items[1].atom == "336" &&
               encoded_edit.value().items[2].items[7].atom == "4",
        "editing named static width and runtime offset must affect independent boundary values");
    edited.bindings.anchors.clear();
    expect(!form_stream::encode_page_position(edited, 0, owner),
        "missing Page constraints must not be inferred from coordinates");
}

void test_page_table_codec() {
    model::Page page;
    page.id = model::ObjectId{1};
    page.name = "Страница1";
    model::LocalizedStringValue default_title;
    default_title.items.push_back({"ru", "Страница1"});
    page.title.set(default_title);

    const auto table = form_stream::encode_page_table({page});
    expect(table.ok(), table ? "default Page table must encode" : table.diagnostics().front().message);
    const auto expected = list_stream::parse(
        R"({1,1,{6,{1,1,{"ru","Страница1"}},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},-1,1,1,"Страница1",1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},1}})");
    expect(list_stream::dump_compact(table.value()) == list_stream::dump_compact(expected),
        "Page table writer must match the confirmed default row exactly");

    const auto decoded = form_stream::decode_page_table(expected, 40);
    expect(decoded.ok() && decoded.value().size() == 1 &&
               decoded.value()[0].id == model::ObjectId{40} && decoded.value()[0].name == "Страница1" &&
               decoded.value()[0].title.value() == default_title &&
               decoded.value()[0].visible.value() && decoded.value()[0].enabled.value(),
        "default Page table must decode named properties and assign caller-selected IDs");

    auto edited = page;
    edited.id = model::ObjectId{8};
    edited.name = "Operations";
    edited.title.set(model::LocalizedStringValue{{{"ru", "Операции"}, {"en", "Operations"}}});
    edited.visible.set(false);
    edited.enabled.set(false);
    const auto edited_table = form_stream::encode_page_table({edited});
    expect(edited_table.ok(), "named Page Name, multilingual Title, Visible, and Enabled must encode");
    const auto edited_roundtrip = form_stream::decode_page_table(edited_table.value(), 80);
    expect(edited_roundtrip.ok() && edited_roundtrip.value()[0].id == model::ObjectId{80} &&
               edited_roundtrip.value()[0].name == "Operations" &&
               edited_roundtrip.value()[0].title.value() == edited.title.value() &&
               !edited_roundtrip.value()[0].visible.value() && !edited_roundtrip.value()[0].enabled.value(),
        "Page table roundtrip must preserve all localizations and supported edits");

    const auto two_pages = form_stream::encode_page_table({page, edited});
    expect(two_pages.ok(), "multiple Page rows must encode");
    const auto two_pages_decoded = form_stream::decode_page_table(two_pages.value(), 100);
    expect(two_pages_decoded.ok() && two_pages_decoded.value().size() == 2 &&
               two_pages_decoded.value()[0].id == model::ObjectId{100} &&
               two_pages_decoded.value()[1].id == model::ObjectId{101},
        "Page table must preserve row order and allocate consecutive internal IDs");

    auto malformed = expected;
    malformed.items[1] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1102", "$/Pages",
        "Page table count that disagrees with row arity must fail");
    malformed = expected;
    malformed.items[2].items.pop_back();
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1102", "$/Pages/2",
        "Page rows with wrong arity must fail");
    malformed = expected;
    malformed.items[2].items[0] = list_stream::ListValue::raw_atom("7");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1106", "$/Pages/2/0",
        "unknown Page record versions must fail");
    malformed = expected;
    malformed.items[2].items[2].items[0] = list_stream::ListValue::raw_atom("11");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1114", "$/Pages/2/2",
        "unproven Page style values must fail");

    auto duplicate_title = page;
    duplicate_title.title.set(model::LocalizedStringValue{{{"ru", "Первый"}, {"ru", "Второй"}}});
    expect_failure(form_stream::encode_page_table({duplicate_title}), "OOF1122", "$/Pages/0/1/1",
        "duplicate Title language keys must be rejected by the writer");
    malformed = expected;
    malformed.items[2].items[1] = list_stream::parse("{1,2,{\"ru\",\"Первый\"},{\"ru\",\"Второй\"}}");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1122", "$/Pages/2/1/1",
        "duplicate Title language keys must be rejected by the reader");

    model::Page empty_language = page;
    empty_language.title.set(model::LocalizedStringValue{{{"", "Title without language tag"}}});
    const auto empty_language_table = form_stream::encode_page_table({empty_language});
    expect(empty_language_table.ok(), "a single empty language tag remains accepted like the XML parser");
    const auto empty_language_decoded = form_stream::decode_page_table(empty_language_table.value(), 1);
    expect(empty_language_decoded.ok() &&
               empty_language_decoded.value()[0].title.value() == empty_language.title.value(),
        "an empty language tag must survive the Page table codec");

    auto empty_name = page;
    empty_name.name.clear();
    expect_failure(form_stream::encode_page_table({empty_name}), "OOF1122", "$/Pages/0",
        "empty Page names must fail on encode");
    malformed = expected;
    malformed.items[2].items[6] = list_stream::ListValue::string_atom("");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1115", "$/Pages/2/6",
        "empty Page names must fail on decode");

    auto duplicate_id = edited;
    duplicate_id.id = page.id;
    expect_failure(form_stream::encode_page_table({page, duplicate_id}), "OOF1122", "$/Pages/1",
        "duplicate Page IDs must fail on encode");
    const auto names_table = form_stream::encode_page_table({page, edited});
    expect(names_table.ok(), "distinct Page names must encode");
    malformed = names_table.value();
    malformed.items[3].items[6] = list_stream::ListValue::string_atom(page.name);
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1122", "$/Pages/3/6",
        "duplicate Page names within one owner table must fail on decode");
    auto duplicate_name = page;
    duplicate_name.id = model::ObjectId{9};
    expect_failure(form_stream::encode_page_table({page, duplicate_name}), "OOF1122", "$/Pages/1",
        "duplicate Page names within one owner table must fail on encode");

    expect_failure(form_stream::decode_page_table(expected, 0), "OOF1122", "$/Pages",
        "zero starting Page IDs must fail");
    const auto overflow = form_stream::decode_page_table(
        two_pages.value(), std::numeric_limits<std::uint64_t>::max());
    expect_failure(overflow, "OOF1122", "$/Pages",
        "Page ID assignment overflow must fail before allocating rows");
}

void test_owner_aware_control_geometry_codec() {
    const model::ControlRef owner{model::ObjectId{42}};
    const form_stream::GeometryContext context{owner, 3, 7};
    form_stream::ControlGeometry source;
    source.position.width.set(120);
    source.position.height.set(24);
    model::AnchorBinding binding;
    binding.coordinate = model::BindingCoordinate::right;
    binding.target_coordinate = model::BindingCoordinate::right;
    binding.target = owner;
    model::AnchorBindingTarget proportional;
    proportional.coordinate = model::BindingCoordinate::left;
    proportional.target = owner;
    binding.proportional = proportional;
    source.position.bindings.anchors.push_back(binding);
    source.incoming[0].push_back({9, 1});

    const auto encoded = form_stream::encode_control_geometry(source, context);
    expect(encoded.ok(), encoded ? "nested geometry must encode" : encoded.diagnostics().front().message);
    const auto cursor = geometry_tail_start(encoded.value());
    expect(encoded.value().items[cursor].atom == "3" && encoded.value().items[cursor + 1].atom == "7" &&
               encoded.value().items[cursor + 2].atom == "8",
        "geometry must encode the explicit page and local sibling ordinal");
    expect(list_stream::dump_compact(encoded.value().items[9]) == "{0,{2,0,3,0},{2,0,2,0}}",
        "primary and proportional references to the owning Panel must encode as target zero");

    const auto decoded = form_stream::decode_control_geometry(encoded.value(), context);
    expect(decoded.ok(), decoded ? "nested geometry must decode" : decoded.diagnostics().front().message);
    const auto& roundtrip = decoded.value();
    expect(roundtrip.position.bindings.anchors.size() == 1 &&
               roundtrip.position.bindings.anchors[0].target == owner &&
               roundtrip.position.bindings.anchors[0].proportional.has_value() &&
               roundtrip.position.bindings.anchors[0].proportional->target == owner,
        "target-zero primary and proportional bindings must resolve to the owning Panel");
    expect(roundtrip.incoming == source.incoming,
        "owner-aware geometry roundtrip must preserve named incoming dependencies");
    expect(roundtrip.position.width.value() == 120 && roundtrip.position.height.value() == 24,
        "nested geometry must preserve named Position dimensions");

    auto bad = encoded.value();
    bad.items[cursor] = list_stream::ListValue::raw_atom("4");
    expect_failure(form_stream::decode_control_geometry(bad, context), "OOF1114",
        "$/" + std::to_string(cursor),
        "geometry with a different page index must fail against its owner context");
    bad = encoded.value();
    bad.items[cursor + 2] = list_stream::ListValue::raw_atom("9");
    expect_failure(form_stream::decode_control_geometry(bad, context), "OOF1114",
        "$/" + std::to_string(cursor + 1),
        "geometry with a mismatched next index must be rejected");
    bad = encoded.value();
    bad.items[cursor + 1] = list_stream::ListValue::raw_atom("6");
    expect_failure(form_stream::decode_control_geometry(bad, context), "OOF1114",
        "$/" + std::to_string(cursor + 1),
        "geometry with a different local ordinal must be rejected");

    auto form_target = source;
    form_target.position.bindings.anchors[0].target.reset();
    expect_failure(form_stream::encode_control_geometry(form_target, context), "OOF1122",
        "$/Position/Bindings/targetId", "Form target cannot be encoded in nested geometry");
    auto invalid_target = source;
    invalid_target.position.bindings.anchors[0].target = model::ControlRef{model::ObjectId{0}};
    expect_failure(form_stream::encode_control_geometry(invalid_target, context), "OOF1122",
        "$/Position/Bindings/targetId", "explicit primary target ID zero must not collapse to the owner sentinel");
    invalid_target = source;
    invalid_target.position.bindings.anchors[0].proportional->target =
        model::ControlRef{model::ObjectId{0}};
    expect_failure(form_stream::encode_control_geometry(invalid_target, context), "OOF1122",
        "$/Position/Bindings/ProportionalBinding/targetId",
        "explicit proportional target ID zero must not collapse to the owner sentinel");

    auto unsupported_position = source;
    unsupported_position.position.default_control.set(std::optional<bool>{true});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "explicit DefaultControl must be rejected by standalone geometry encoding");
    unsupported_position = source;
    unsupported_position.position.tab_order.set(std::optional<std::int32_t>{2});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "explicit TabOrder must be rejected by standalone geometry encoding");
    unsupported_position = source;
    unsupported_position.position.z_order.set(std::optional<std::int32_t>{3});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "explicit ZOrder must be rejected by standalone geometry encoding");
    unsupported_position = source;
    unsupported_position.position.collapse.set(
        std::optional<model::EnumerationValue>{model::EnumerationValue{"Collapse", "None"}});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "explicit Collapse must be rejected by standalone geometry encoding");
    unsupported_position = source;
    unsupported_position.position.bindings.dimensions.push_back(
        model::DimensionBinding{model::BindingDimension::width, {}});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "dimension bindings must be rejected by standalone geometry encoding");

    auto invalid_incoming = source;
    invalid_incoming.incoming[0][0].source_control_id = 0;
    expect_failure(form_stream::encode_control_geometry(invalid_incoming, context), "OOF1122",
        "$/Position/Incoming/0/0", "incoming source ID zero must be rejected by the writer");
    invalid_incoming = source;
    invalid_incoming.incoming[0][0].source_control_id =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
    expect_failure(form_stream::encode_control_geometry(invalid_incoming, context), "OOF1122",
        "$/Position/Incoming/0/0", "incoming source IDs outside int64 must be rejected by the writer");
    invalid_incoming = source;
    invalid_incoming.incoming[0][0].source_edge = 4;
    expect_failure(form_stream::encode_control_geometry(invalid_incoming, context), "OOF1122",
        "$/Position/Incoming/0/0", "unsupported incoming source edges must be rejected by the writer");
    const form_stream::GeometryContext invalid_owner{model::ControlRef{model::ObjectId{0}}, 3, 7};
    expect_failure(form_stream::encode_control_geometry(source, invalid_owner), "OOF1122",
        "$/Position/0", "a zero Panel owner ID must be rejected");
    const form_stream::GeometryContext root_context{form_stream::FormGeometryOwner{}, 0, 2};
    form_target.position.bindings.anchors.clear();
    form_target.position.bindings.anchors.push_back(binding);
    form_target.position.bindings.anchors[0].target.reset();
    form_target.position.bindings.anchors[0].proportional->target.reset();
    const auto root_geometry = form_stream::encode_control_geometry(form_target, root_context);
    expect(root_geometry.ok(), "Form-target primary and proportional references must remain supported at root");
    const auto root_roundtrip = form_stream::decode_control_geometry(root_geometry.value(), root_context);
    expect(root_roundtrip.ok() &&
               !root_roundtrip.value().position.bindings.anchors[0].target.has_value() &&
               !root_roundtrip.value().position.bindings.anchors[0].proportional->target.has_value(),
        "root target-zero references must retain Form semantics");

    auto foreign = source;
    foreign.position.bindings.anchors[0].target = model::ControlRef{model::ObjectId{77}};
    foreign.position.bindings.anchors[0].proportional->target = model::ControlRef{model::ObjectId{77}};
    const auto foreign_encoded = form_stream::encode_control_geometry(foreign, context);
    expect(foreign_encoded.ok() && foreign_encoded.value().items[9].items[1].items[1].atom == "77" &&
               foreign_encoded.value().items[9].items[2].items[1].atom == "77",
        "references to controls other than the owner must retain their explicit IDs");
    const auto foreign_decoded = form_stream::decode_control_geometry(foreign_encoded.value(), context);
    expect(foreign_decoded.ok() &&
               foreign_decoded.value().position.bindings.anchors[0].target == model::ControlRef{model::ObjectId{77}} &&
               foreign_decoded.value().position.bindings.anchors[0].proportional->target ==
                   model::ControlRef{model::ObjectId{77}},
        "references other than target zero must decode as their explicit control IDs");
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
        test_attribute_allocator_is_separate_from_control_ids();
        test_two_button_sibling_index();
        test_multiple_top_level_buttons_round_trip();
        test_button_multiline_round_trip_and_validation();
        test_button_then_label_decoration_round_trip();
        test_fresh_checkbox_stream_decode();
        test_button_label_input_field_round_trip();
        test_single_input_field_round_trip();
        test_two_input_fields_round_trip();
        test_six_reordered_controls_use_logical_geometry_ordinals();
        test_manual_bindings_are_not_silently_discarded();
        test_anchor_bindings_round_trip_and_fanout();
        test_center_target_coordinates();
        test_page_boundary_position_codec();
        test_page_table_codec();
        test_owner_aware_control_geometry_codec();
        test_platform_empty_document_fixture();
    } catch (const std::exception& error) {
        std::cerr << "form stream tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form stream tests: PASS\n";
    return 0;
}
