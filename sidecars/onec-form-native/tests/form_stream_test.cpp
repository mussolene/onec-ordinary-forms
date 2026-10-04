#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oof/model/metamodel.hpp"
#include "oof/source/form_xml.hpp"
#include "oof/storage/form_stream.hpp"
#include "oof/storage/value_codec.hpp"

namespace {

namespace form_stream = oof::storage::form_stream;
namespace list_stream = oof::storage::list_stream;
namespace model = oof::model;
namespace value_codec = oof::storage::value_codec;

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

    auto designer_three_slots = encoded.value();
    designer_three_slots.items[2].items[1] = list_stream::ListValue::raw_atom("3");
    const auto decoded_three_slots = form_stream::decode_document(designer_three_slots, "Main");
    expect(decoded_three_slots.ok(),
        "empty attribute allocation header with three Designer slots must decode above control ID 2");
    expect(form_stream::encode_document(decoded_three_slots.value()).value().items[2].items[1].atom == "11",
        "empty three-slot header must normalize to the writer allocation count");

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

void test_button_alignments_and_tooltip_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{7}},
        model::ControlRef{model::ObjectId{12}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    const auto add_button = [&](std::uint64_t id, std::string name,
                                std::string horizontal, std::string vertical,
                                std::string tooltip) {
        model::ControlNode button{model::ObjectId{id}, std::move(name), model::ButtonPayload{}};
        button.properties().set_explicit(
            model::PropertyId::from_name("HorizontalAlign"),
            model::EnumerationValue{"HorizontalAlign", std::move(horizontal)});
        button.properties().set_explicit(
            model::PropertyId::from_name("VerticalAlign"),
            model::EnumerationValue{"VerticalAlign", std::move(vertical)});
        button.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tooltip));
        document.add_control(std::move(button));
    };
    add_button(2, "First", "Left", "Top", "Проверка Ω\nВторая строка");
    add_button(7, "Center", "Center", "Center", "");
    add_button(12, "Last", "Right", "Bottom", "Подсказка");

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "Button alignment and ToolTip properties must encode");
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    const std::int32_t horizontal[] = {0, 1, 2};
    const std::int32_t vertical[] = {0, 1, 2};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& properties = records[index + 1].items[2].items[1];
        expect(properties.items[3].atom == std::to_string(horizontal[index]) &&
                   properties.items[4].atom == std::to_string(vertical[index]),
            "Button alignment enums must use their observed three-value storage order");
    }
    expect(records[1].items[2].items[1].items[0].items[12].is_list,
        "Button.ToolTip must occupy its localized base slot");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "Button alignment and ToolTip properties must decode");
    const auto* first = decoded.value().find_control(model::ObjectId{2});
    const auto* middle = decoded.value().find_control(model::ObjectId{7});
    const auto* last = decoded.value().find_control(model::ObjectId{12});
    expect(first && middle && last, "all Button controls must survive property decoding");
    expect(!middle->properties().find(model::PropertyId::from_name("HorizontalAlign")) &&
               !middle->properties().find(model::PropertyId::from_name("VerticalAlign")) &&
               !middle->properties().find(model::PropertyId::from_name("ToolTip")),
        "center alignment and empty ToolTip must remain model defaults");
    const auto* first_tooltip = first->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(first_tooltip && std::get<std::string>(first_tooltip->value) == "Проверка Ω\nВторая строка",
        "Unicode multiline Button.ToolTip must survive");
    const auto* last_tooltip = last->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(last_tooltip && std::get<std::string>(last_tooltip->value) == "Подсказка",
        "Button.ToolTip must remain associated with its owner");
    expect(std::get<model::EnumerationValue>(first->properties().find(
               model::PropertyId::from_name("HorizontalAlign"))->value) ==
               model::EnumerationValue{"HorizontalAlign", "Left"} &&
               std::get<model::EnumerationValue>(first->properties().find(
                   model::PropertyId::from_name("VerticalAlign"))->value) ==
               model::EnumerationValue{"VerticalAlign", "Top"} &&
               std::get<model::EnumerationValue>(last->properties().find(
                   model::PropertyId::from_name("HorizontalAlign"))->value) ==
               model::EnumerationValue{"HorizontalAlign", "Right"} &&
               std::get<model::EnumerationValue>(last->properties().find(
                   model::PropertyId::from_name("VerticalAlign"))->value) ==
               model::EnumerationValue{"VerticalAlign", "Bottom"},
        "non-default Button alignment enum names must round-trip");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "Button alignment and ToolTip storage must round-trip without drift");

    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "Invalid";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument wrong_type(std::move(invalid_form));
    model::ControlNode wrong_button{model::ObjectId{2}, "WrongType", model::ButtonPayload{}};
    wrong_button.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"VerticalAlign", "Top"});
    wrong_type.add_control(std::move(wrong_button));
    expect_failure(form_stream::encode_document(wrong_type), "OOF1122", "$/Button/HorizontalAlign",
        "foreign Button alignment enum type must be rejected");

    auto invalid_member_form = model::Form{};
    invalid_member_form.id = model::ObjectId{1};
    invalid_member_form.name = "InvalidMember";
    invalid_member_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument invalid_member(std::move(invalid_member_form));
    model::ControlNode invalid_member_button{model::ObjectId{2}, "InvalidMember", model::ButtonPayload{}};
    invalid_member_button.properties().set_explicit(model::PropertyId::from_name("VerticalAlign"),
        model::EnumerationValue{"VerticalAlign", "Middle"});
    invalid_member.add_control(std::move(invalid_member_button));
    expect_failure(form_stream::encode_document(invalid_member), "OOF1122", "$/Button/VerticalAlign",
        "unknown Button alignment enum member must be rejected");

    auto invalid_storage = encoded.value();
    invalid_storage.items[1].items[2].items[2].items[1].items[2].items[1].items[3] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(invalid_storage, "Main"), "OOF1114",
        "$/1/2/2/1/2/1/3", "unknown Button alignment storage values must be rejected");

    const auto encode_tooltip = [](std::string text) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "LineEndings";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument tooltip_document(std::move(form));
        model::ControlNode button{model::ObjectId{2}, "Tip", model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(text));
        tooltip_document.add_control(std::move(button));
        return form_stream::encode_document(tooltip_document);
    };
    const auto lf = encode_tooltip("first\nsecond");
    const auto crlf = encode_tooltip("first\r\nsecond");
    expect(lf.ok() && crlf.ok() && list_stream::dump_compact(lf.value()) ==
               list_stream::dump_compact(crlf.value()),
        "LF and CRLF ToolTip text must produce identical localized storage");

    const std::string mixed_model_text = "first\r\nsecond\nthird\rlast";
    const auto mixed_storage = encode_tooltip(mixed_model_text);
    expect(mixed_storage.ok(), "mixed ToolTip line endings must encode");
    const auto mixed_decoded = form_stream::decode_document(mixed_storage.value(), "LineEndings");
    expect(mixed_decoded.ok(), "mixed ToolTip line endings must decode");
    const auto* mixed_control = mixed_decoded.value().find_control(model::ObjectId{2});
    expect(mixed_control != nullptr, "mixed ToolTip control must survive decoding");
    const auto* mixed_tooltip = mixed_control->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(mixed_tooltip && std::get<std::string>(mixed_tooltip->value) ==
               "first\nsecond\nthird\rlast",
        "decode must normalize CRLF to LF while preserving lone CR and the final line");
    const auto normalized_storage = encode_tooltip("first\nsecond\nthird\rlast");
    expect(normalized_storage.ok() && list_stream::dump_compact(normalized_storage.value()) ==
               list_stream::dump_compact(mixed_storage.value()),
        "normalizing a mixed-ending ToolTip through the model must preserve its storage record");
}

void test_check_box_tooltip_round_trip_and_validation() {
    const auto make_document = [](std::optional<std::string> tool_tip) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "CheckBoxToolTip";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue boolean_type;
        model::TypeDomainEntry boolean_entry;
        boolean_entry.term = model::TypeDomainTerm::boolean;
        boolean_type.entries.push_back(boolean_entry);
        document.add_attribute(model::Attribute{model::ObjectId{3}, "Flag", boolean_type});
        model::ControlNode check_box{model::ObjectId{2}, "FlagControl", model::CheckBoxPayload{}};
        check_box.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        if (tool_tip.has_value()) {
            check_box.properties().set_explicit(model::PropertyId::from_name("ToolTip"), *tool_tip);
        }
        document.add_control(std::move(check_box));
        return document;
    };
    const auto check_box_base = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        const auto& record = encoded.items[1].items[2].items[2].items[1];
        return record.items[2].items[1].items[0].items[0];
    };

    const auto default_encoded = form_stream::encode_document(make_document(std::nullopt));
    const auto explicit_empty_encoded = form_stream::encode_document(make_document(std::string{}));
    expect(default_encoded.ok() && explicit_empty_encoded.ok(),
        "default and explicit empty CheckBox ToolTip must encode");
    expect(list_stream::dump_compact(default_encoded.value()) ==
               list_stream::dump_compact(explicit_empty_encoded.value()),
        "explicit empty CheckBox ToolTip must normalize to the default storage");
    const auto default_decoded = form_stream::decode_document(explicit_empty_encoded.value(), "CheckBoxToolTip");
    expect(default_decoded.ok(), "explicit empty CheckBox ToolTip must decode");
    const auto* default_check_box = default_decoded.value().find_control(model::ObjectId{2});
    expect(default_check_box && !default_check_box->properties().find(model::PropertyId::from_name("ToolTip")),
        "empty CheckBox ToolTip must normalize to its implicit default");

    const std::string tool_tip = "Подсказка Ω <важно> & \"цитата\"\nВторая строка";
    const auto encoded = form_stream::encode_document(make_document(tool_tip));
    expect(encoded.ok(), "CheckBox ToolTip with Unicode, punctuation, and a newline must encode");
    const auto& stored_tool_tip = check_box_base(encoded.value()).items[12];
    expect(list_stream::dump_compact(stored_tool_tip) == value_codec::encode_localized_string(
               model::LocalizedStringValue{{{"ru", "Подсказка Ω <важно> & \"цитата\"\r\nВторая строка"}}}),
        "CheckBox ToolTip must occupy the observed localized base slot with canonical line endings");
    const auto decoded = form_stream::decode_document(encoded.value(), "CheckBoxToolTip");
    expect(decoded.ok(), "CheckBox ToolTip must decode");
    const auto* check_box = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_tool_tip = check_box == nullptr ? nullptr :
        check_box->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(decoded_tool_tip && std::get<std::string>(decoded_tool_tip->value) == tool_tip,
        "CheckBox ToolTip must round-trip Unicode, punctuation, and newlines");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "CheckBox ToolTip storage must round-trip without drift");

    constexpr std::string_view tooltip_path = "$/1/2/2/1/2/1/0/0/12";
    auto malformed = encoded.value();
    auto& malformed_tool_tip = malformed.items[1].items[2].items[2].items[1]
        .items[2].items[1].items[0].items[0].items[12];
    malformed_tool_tip = list_stream::ListValue::raw_atom("malformed");
    expect_failure(form_stream::decode_document(malformed, "CheckBoxToolTip"), "OOF1108", tooltip_path,
        "malformed CheckBox ToolTip localization must be rejected");

    auto multilingual = encoded.value();
    auto& multilingual_tool_tip = multilingual.items[1].items[2].items[2].items[1]
        .items[2].items[1].items[0].items[0].items[12];
    multilingual_tool_tip = list_stream::parse(value_codec::encode_localized_string(
        model::LocalizedStringValue{{{"ru", "Текст"}, {"en", "Text"}}}));
    expect_failure(form_stream::decode_document(multilingual, "CheckBoxToolTip"), "OOF1115", tooltip_path,
        "multilingual CheckBox ToolTip must be rejected without loss");
}

void test_check_box_font_round_trip_and_validation() {
    const auto make_document = [](std::optional<model::FontValue> font) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "CheckBoxFont";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue boolean_type;
        model::TypeDomainEntry boolean_entry;
        boolean_entry.term = model::TypeDomainTerm::boolean;
        boolean_type.entries.push_back(boolean_entry);
        document.add_attribute(model::Attribute{model::ObjectId{3}, "Flag", boolean_type});
        model::ControlNode check_box{model::ObjectId{2}, "FlagControl", model::CheckBoxPayload{}};
        check_box.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        if (font) check_box.properties().set_explicit(model::PropertyId::from_name("Font"), *font);
        document.add_control(std::move(check_box));
        return document;
    };
    const auto check_box_base = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[0];
    };

    const auto default_encoded = form_stream::encode_document(make_document(std::nullopt));
    model::FontValue automatic;
    const auto explicit_automatic_encoded = form_stream::encode_document(make_document(automatic));
    expect(default_encoded.ok() && explicit_automatic_encoded.ok(),
        "default and explicit automatic CheckBox Font must encode");
    expect(list_stream::dump_compact(default_encoded.value()) ==
               list_stream::dump_compact(explicit_automatic_encoded.value()),
        "explicit automatic CheckBox Font must normalize to the default storage");
    const auto default_decoded = form_stream::decode_document(explicit_automatic_encoded.value(), "CheckBoxFont");
    expect(default_decoded.ok(), "default CheckBox Font must decode");
    const auto* default_check_box = default_decoded.value().find_control(model::ObjectId{2});
    expect(default_check_box && !default_check_box->properties().find(model::PropertyId::from_name("Font")),
        "automatic CheckBox Font must normalize to its implicit default");

    model::FontValue font;
    font.kind = model::FontKind::absolute;
    font.face_name = "Arial";
    font.height = 12;
    font.bold = true;
    const auto encoded = form_stream::encode_document(make_document(font));
    expect(encoded.ok(), "supported absolute CheckBox Font must encode");
    const auto& stored_font = check_box_base(encoded.value()).items[4];
    expect(list_stream::dump_compact(stored_font) == value_codec::encode_font(font),
        "CheckBox Font must use the observed Button base Font record profile");
    const auto decoded = form_stream::decode_document(encoded.value(), "CheckBoxFont");
    expect(decoded.ok(), "supported absolute CheckBox Font must decode");
    const auto* check_box = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_font = check_box == nullptr ? nullptr :
        check_box->properties().find(model::PropertyId::from_name("Font"));
    expect(decoded_font && std::get<model::FontValue>(decoded_font->value) == font,
        "CheckBox Font must round-trip as its named FontValue");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "CheckBox Font storage must round-trip without drift");

    model::FontValue unsupported;
    unsupported.kind = model::FontKind::windows_font;
    const auto unsupported_encoded = form_stream::encode_document(make_document(unsupported));
    expect_failure(unsupported_encoded, "OOF1122", "$/CheckBox/Font",
        "unsupported WindowsFont CheckBox value must be rejected without fallback");
}

void test_button_colors_round_trip_and_validation() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "ButtonColors";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{4}},
        model::ControlRef{model::ObjectId{6}}, model::ControlRef{model::ObjectId{8}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode rgb{model::ObjectId{2}, "RGB", model::ButtonPayload{}};
    model::ColorValue absolute;
    absolute.kind = model::ColorKind::absolute;
    absolute.red = 17;
    absolute.green = 83;
    absolute.blue = 201;
    rgb.properties().set_explicit(model::PropertyId::from_name("BorderColor"), absolute);
    rgb.properties().set_explicit(model::PropertyId::from_name("ButtonBackColor"), absolute);
    rgb.properties().set_explicit(model::PropertyId::from_name("ButtonTextColor"),
        model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
            model::QualifiedName{"StyleColors.ButtonBackColor"}});
    const model::ShortcutValue shortcut{"Enter", false, true, false};
    rgb.properties().set_explicit(model::PropertyId::from_name("Shortcut"), shortcut);
    model::FontValue full_font;
    full_font.kind = model::FontKind::absolute;
    full_font.face_name = "Arial";
    full_font.height = 12.5;
    full_font.bold = false;
    full_font.italic = true;
    full_font.underline = false;
    full_font.strikeout = true;
    full_font.scale = 125;
    rgb.properties().set_explicit(model::PropertyId::from_name("Font"), full_font);
    document.add_control(std::move(rgb));
    model::ControlNode automatic{model::ObjectId{4}, "Automatic", model::ButtonPayload{}};
    automatic.properties().set_explicit(model::PropertyId::from_name("BorderColor"), model::ColorValue{});
    document.add_control(std::move(automatic));
    model::ControlNode named_styles{model::ObjectId{6}, "NamedStyles", model::ButtonPayload{}};
    named_styles.properties().set_explicit(model::PropertyId::from_name("BorderColor"),
        model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
            model::QualifiedName{"StyleColors.ButtonTextColor"}});
    named_styles.properties().set_explicit(model::PropertyId::from_name("ButtonTextColor"),
        model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
            model::QualifiedName{"StyleColors.ButtonBorderColor"}});
    model::FontValue text_font;
    text_font.kind = model::FontKind::style_reference;
    text_font.style = model::QualifiedName{"StyleFonts.TextFont"};
    named_styles.properties().set_explicit(model::PropertyId::from_name("Font"), text_font);
    document.add_control(std::move(named_styles));
    model::ControlNode copied_scale_button{model::ObjectId{8}, "CopiedScale", model::ButtonPayload{}};
    model::FontValue copied_scale_font;
    copied_scale_font.kind = model::FontKind::absolute;
    copied_scale_font.face_name = "Arial";
    copied_scale_font.scale = 125;
    copied_scale_font.scale_override = true;
    copied_scale_button.properties().set_explicit(model::PropertyId::from_name("Font"), copied_scale_font);
    document.add_control(std::move(copied_scale_button));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "Button absolute, automatic, and style colors must encode");
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    const auto& rgb_base = records[1].items[2].items[1].items[0];
    expect(rgb_base.items[6].items[1].atom == "0" && rgb_base.items[6].items[2].items[0].atom == "13194001",
        "Button absolute RGB must use the observed packed BGR integer");
    expect(rgb_base.items[10].items[2].items[0].atom == "-7" &&
               rgb_base.items[9].items[1].atom == "0" &&
               rgb_base.items[9].items[2].items[0].atom == "13194001",
        "Button style colors must use their named platform identifiers independently");
    expect(list_stream::dump_compact(records[1].items[2].items[1].items[9]) == "{0,13,8}",
        "Button.Shortcut must occupy info properties[9] independently of base[9] color");
    expect(list_stream::dump_compact(rgb_base.items[4]) ==
               "{8,0,63,125,0,0,0,400,1,0,1,0,0,0,0,0,\"Arial\",1,125,0}",
        "Button.Font must encode explicit false values and named height/scale data");
    const auto& named_base = records[3].items[2].items[1].items[0];
    expect(named_base.items[6].items[2].items[0].atom == "-21" &&
               named_base.items[10].items[2].items[0].atom == "-34",
        "Button BorderColor and ButtonTextColor must retain explicit -21 and -34 named styles");
    expect(list_stream::dump_compact(named_base.items[4]) == "{8,2,0,{-20},1,100}",
        "Button.Font must encode the observed TextFont style reference");
    const auto& copied_scale_base = records[4].items[2].items[1].items[0];
    expect(list_stream::dump_compact(copied_scale_base.items[4]) ==
               "{8,0,513,0,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,125,0}",
        "Button Font scale override marker must remain independent from the scale");

    const auto decoded = form_stream::decode_document(encoded.value(), "ButtonColors");
    expect(decoded.ok(), "Button color values must decode");
    const auto* decoded_rgb = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_auto = decoded.value().find_control(model::ObjectId{4});
    const auto* decoded_named = decoded.value().find_control(model::ObjectId{6});
    const auto* decoded_copy_scale = decoded.value().find_control(model::ObjectId{8});
    expect(decoded_rgb && decoded_auto && decoded_named && decoded_copy_scale,
        "Button color and Font owners must survive decoding");
    const auto* border = decoded_rgb->properties().find(model::PropertyId::from_name("BorderColor"));
    const auto* back_color = decoded_rgb->properties().find(model::PropertyId::from_name("ButtonBackColor"));
    const auto* decoded_shortcut = decoded_rgb->properties().find(model::PropertyId::from_name("Shortcut"));
    const auto* text = decoded_rgb->properties().find(model::PropertyId::from_name("ButtonTextColor"));
    const auto* decoded_style = text ? std::get_if<model::QualifiedName>(
        &std::get<model::ColorValue>(text->value).style) : nullptr;
    expect(border && std::get<model::ColorValue>(border->value) == absolute && decoded_style &&
               *decoded_style == model::QualifiedName{"StyleColors.ButtonBackColor"},
        "non-default Button RGB and cross-style reference must round-trip");
    expect(back_color && std::get<model::ColorValue>(back_color->value) == absolute &&
               decoded_shortcut && std::get<model::ShortcutValue>(decoded_shortcut->value) == shortcut,
        "Button.Shortcut and ButtonBackColor must decode from their distinct storage records");
    const auto* decoded_font = decoded_rgb->properties().find(model::PropertyId::from_name("Font"));
    expect(decoded_font && std::get<model::FontValue>(decoded_font->value) == full_font,
        "Button Font must preserve explicit false values and each named field");
    expect(!decoded_auto->properties().find(model::PropertyId::from_name("BorderColor")),
        "explicit canonical Button default color must normalize to absent");
    expect(!decoded_auto->properties().find(model::PropertyId::from_name("Font")),
        "automatic Button Font must normalize to the descriptor default");
    const auto* named_border = decoded_named->properties().find(model::PropertyId::from_name("BorderColor"));
    const auto* named_text = decoded_named->properties().find(model::PropertyId::from_name("ButtonTextColor"));
    expect(named_border && std::get<model::ColorValue>(named_border->value).style ==
               model::StyleReference{model::QualifiedName{"StyleColors.ButtonTextColor"}} &&
               named_text && std::get<model::ColorValue>(named_text->value).style ==
               model::StyleReference{model::QualifiedName{"StyleColors.ButtonBorderColor"}},
        "all observed named style identifiers must decode to their public names");
    const auto* decoded_text_font = decoded_named->properties().find(model::PropertyId::from_name("Font"));
    expect(decoded_text_font && std::get<model::FontValue>(decoded_text_font->value) == text_font,
        "Button TextFont style must decode to its named model value");
    const auto* decoded_copy_font = decoded_copy_scale->properties().find(model::PropertyId::from_name("Font"));
    expect(decoded_copy_font && std::get<model::FontValue>(decoded_copy_font->value) == copied_scale_font,
        "Button copy-scale marker and value must both survive decoding");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "Button color storage must round-trip without drift");

    auto invalid_color = absolute;
    invalid_color.kind = static_cast<model::ColorKind>(255);
    invalid_color.style = model::QualifiedName{"StyleColors.ButtonTextColor"};
    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "InvalidColor";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument unknown_kind(std::move(invalid_form));
    model::ControlNode unknown_button{model::ObjectId{2}, "Unknown", model::ButtonPayload{}};
    unknown_button.properties().set_explicit(model::PropertyId::from_name("BorderColor"), invalid_color);
    unknown_kind.add_control(std::move(unknown_button));
    expect_failure(form_stream::encode_document(unknown_kind), "OOF1122", "$/Button/BorderColor",
        "unknown ColorKind values must not be normalized as style references");

    invalid_color = absolute;
    invalid_color.alpha = 254;
    model::Form alpha_form;
    alpha_form.id = model::ObjectId{1};
    alpha_form.name = "InvalidAlpha";
    alpha_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument bad_alpha(std::move(alpha_form));
    model::ControlNode alpha_button{model::ObjectId{2}, "Alpha", model::ButtonPayload{}};
    alpha_button.properties().set_explicit(model::PropertyId::from_name("BorderColor"), invalid_color);
    bad_alpha.add_control(std::move(alpha_button));
    expect_failure(form_stream::encode_document(bad_alpha), "OOF1122", "$/Button/BorderColor",
        "Button absolute colors with alpha must be rejected");

    const auto reject_color = [](model::ColorValue color, std::string_view message) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "InvalidStyle";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument invalid(std::move(form));
        model::ControlNode button{model::ObjectId{2}, "Invalid", model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("ButtonTextColor"), std::move(color));
        invalid.add_control(std::move(button));
        expect_failure(form_stream::encode_document(invalid), "OOF1122", "$/Button/ButtonTextColor", message);
    };
    auto malformed_style = model::ColorValue{model::ColorKind::style_reference, 1, 0, 0, 254,
        model::QualifiedName{"StyleColors.ButtonTextColor"}};
    reject_color(malformed_style, "style colors with explicit channels or alpha must be rejected");
    malformed_style = model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
        model::QualifiedName{"StyleColors.UnknownColor"}};
    reject_color(malformed_style, "unknown qualified style references must be rejected");

    auto malformed_storage = encoded.value();
    malformed_storage.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[6].items[2].items[0] =
        list_stream::ListValue::raw_atom("16777216");
    expect_failure(form_stream::decode_document(malformed_storage, "ButtonColors"), "OOF1114",
        "$/1/2/2/1/2/1/0/6/2/0", "out-of-range packed Button RGB must be rejected");

    auto malformed_font = encoded.value();
    malformed_font.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[4] =
        list_stream::parse("{8,0,1024,125,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,125,0}");
    expect_failure(form_stream::decode_document(malformed_font, "ButtonColors"), "OOF1114",
        "$/1/2/2/1/2/1/0/4", "unknown Font presence flags must fail through the Button boundary");

    model::Form override_form;
    override_form.id = model::ObjectId{1};
    override_form.name = "FontOverride";
    override_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument font_override(std::move(override_form));
    model::ControlNode style_override{model::ObjectId{2}, "Text", model::ButtonPayload{}};
    auto unsupported_style = text_font;
    unsupported_style.underline = false;
    style_override.properties().set_explicit(model::PropertyId::from_name("Font"), unsupported_style);
    font_override.add_control(std::move(style_override));
    expect_failure(form_stream::encode_document(font_override), "OOF1122", "$/Button/Font",
        "unobserved style-font overrides must be rejected rather than discarded");
}

void test_button_picture_enums_round_trip_and_validation() {
    static constexpr std::string_view size_names[] = {
        "RealSize", "Stretch", "Proportionally", "Tile", "AutoSize", "ByFontSize"};
    static constexpr std::int32_t size_values[] = {0, 1, 2, 3, 4, 7};
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "PictureEnums";
    for (std::size_t index = 0; index < std::size(size_names); ++index) {
        form.children.push_back(model::ControlRef{
            model::ObjectId{static_cast<std::uint64_t>(2 + index * 2)}});
    }
    model::OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < std::size(size_names); ++index) {
        const model::ObjectId id{static_cast<std::uint64_t>(2 + index * 2)};
        model::ControlNode button{id, "Button" + std::to_string(index), model::ButtonPayload{}};
        const bool right = (index % 2) != 0;
        button.properties().set_explicit(model::PropertyId::from_name("PictureLocation"),
            model::EnumerationValue{"PictureLocation", right ? "Right" : "Left"});
        button.properties().set_explicit(model::PropertyId::from_name("PictureSize"),
            model::EnumerationValue{"PictureSize", std::string(size_names[index])});
        document.add_control(std::move(button));
    }

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "all observed Button PictureLocation and PictureSize values must encode");
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    for (std::size_t index = 0; index < std::size(size_names); ++index) {
        const auto& properties = records[index + 1].items[2].items[1];
        expect(properties.items[6].atom == std::to_string(index % 2) &&
                   properties.items[7].atom == std::to_string(size_values[index]),
            "Button picture properties must use their independent storage slots and observed enum codes");
    }

    const auto decoded = form_stream::decode_document(encoded.value(), "PictureEnums");
    expect(decoded.ok(), "all observed Button picture enum values must decode");
    for (std::size_t index = 0; index < std::size(size_names); ++index) {
        const auto* button = decoded.value().find_control(
            model::ObjectId{static_cast<std::uint64_t>(2 + index * 2)});
        expect(button != nullptr, "Button picture enum control must survive decoding");
        const auto* location = button->properties().find(model::PropertyId::from_name("PictureLocation"));
        if (index % 2 == 0) {
            expect(location == nullptr, "default Left picture location must normalize to absent");
        } else {
            expect(location && std::get<model::EnumerationValue>(location->value) ==
                       model::EnumerationValue{"PictureLocation", "Right"},
                "Right picture location must remain explicit");
        }
        const auto* size = button->properties().find(model::PropertyId::from_name("PictureSize"));
        if (index == 0) {
            expect(size == nullptr, "default RealSize must normalize to absent");
        } else {
            expect(size && std::get<model::EnumerationValue>(size->value) ==
                       model::EnumerationValue{"PictureSize", std::string(size_names[index])},
                "each non-default picture size must remain explicit and independent");
        }
    }
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "Button picture enums must re-encode without changing either property");

    model::Form foreign_form;
    foreign_form.id = model::ObjectId{1};
    foreign_form.name = "ForeignEnums";
    foreign_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument foreign_document(std::move(foreign_form));
    model::ControlNode foreign_button{model::ObjectId{2}, "Foreign", model::ButtonPayload{}};
    foreign_button.properties().set_explicit(model::PropertyId::from_name("PictureLocation"),
        model::EnumerationValue{"PictureSize", "Right"});
    foreign_document.add_control(std::move(foreign_button));
    expect_failure(form_stream::encode_document(foreign_document), "OOF1122", "$/Button/PictureLocation",
        "foreign PictureLocation enum type must be rejected");

    model::Form unknown_form;
    unknown_form.id = model::ObjectId{1};
    unknown_form.name = "UnknownEnums";
    unknown_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument unknown_document(std::move(unknown_form));
    model::ControlNode unknown_button{model::ObjectId{2}, "Unknown", model::ButtonPayload{}};
    unknown_button.properties().set_explicit(model::PropertyId::from_name("PictureSize"),
        model::EnumerationValue{"PictureSize", "Unsupported"});
    unknown_document.add_control(std::move(unknown_button));
    expect_failure(form_stream::encode_document(unknown_document), "OOF1122", "$/Button/PictureSize",
        "unsupported PictureSize enum member must be rejected");

    for (const std::int32_t unsupported : {5, 6, 8}) {
        auto invalid = encoded.value();
        invalid.items[1].items[2].items[2].items[1].items[2].items[1].items[7] =
            list_stream::ListValue::raw_atom(std::to_string(unsupported));
        expect_failure(form_stream::decode_document(invalid, "PictureEnums"), "OOF1114",
            "$/1/2/2/1/2/1/7", "unsupported Button.PictureSize storage values must be rejected");
    }
    auto invalid_location = encoded.value();
    invalid_location.items[1].items[2].items[2].items[1].items[2].items[1].items[6] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(invalid_location, "PictureEnums"), "OOF1114",
        "$/1/2/2/1/2/1/6", "unsupported Button.PictureLocation storage values must be rejected");
}

void test_named_button_menu_round_trip_and_invalid_references() {
    model::Form form;
    form.id = model::ObjectId{1}; form.name = "Menu";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode button{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    button.properties().set_explicit(model::PropertyId::from_name("MenuMode"), model::EnumerationValue{"MenuMode", "UseExtra"});
    model::CommandBarButton action;
    action.name = "ActionOne"; action.action = "RunHandler"; action.text = "Первое";
    action.explanation = "Пояснение"; action.tooltip = "Подсказка";
    action.enabled = false; action.checked = true; action.changes_data = true;
    action.representation = model::ButtonRepresentation::picture_text;
    action.shortcut = {"A", false, true, false};
    action.picture = model::PictureRef{model::PictureAssetRef{}, model::QualifiedName{"PictureLib.ActivateTask"}};
    model::CommandBarButton divider; divider.name = "Divider"; divider.type = model::CommandBarButtonKind::separator;
    model::CommandBarButton submenu; submenu.name = "More"; submenu.type = model::CommandBarButtonKind::submenu;
    submenu.order = model::CommandBarButtonOrder::ascending;
    submenu.text = "Еще"; submenu.buttons = {action, divider};
    model::CommandBarButton nested; nested.name = "Nested"; nested.type = model::CommandBarButtonKind::submenu;
    nested.order = model::CommandBarButtonOrder::descending; nested.buttons = {divider};
    submenu.buttons.push_back(nested);
    model::CommandBarButton unordered; unordered.name = "Unordered"; unordered.type = model::CommandBarButtonKind::submenu;
    unordered.buttons = {divider};
    std::get<model::ButtonPayload>(button.payload).buttons = {action, divider, submenu, unordered};
    document.add_control(std::move(button));
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "named menu must encode" : encoded.diagnostics().front().message);
    const auto& menu_record = encoded.value().items[1].items[2].items[2].items[1].items[2].items[1].items[12];
    const auto action_records_end = menu_record.items.begin() + 5 + static_cast<std::ptrdiff_t>(std::stoul(menu_record.items[4].atom));
    const auto complete_action_it = std::find_if(menu_record.items.begin() + 5, action_records_end,
        [](const auto& candidate) { return candidate.items.size() > 5 && candidate.items[5].atom == "15"; });
    expect(complete_action_it != action_records_end, "menu action with all optional flags must exist");
    const auto& complete_action = *complete_action_it;
    expect(complete_action.items[5].atom == "15" && complete_action.items[6].items[0].atom == "1" &&
        complete_action.items[7].items[0].atom == "1" && complete_action.items[8].items[0].atom == "4" &&
        complete_action.items[9].items[0].atom == "0",
        "platform flags 15 store ToolTip, Explanation, Picture, Shortcut in that order, not bit order");
    const auto decoded = form_stream::decode_document(encoded.value(), "Menu");
    expect(decoded.ok(), decoded ? "named menu must decode" : decoded.diagnostics().front().message);
    expect(std::get<model::ButtonPayload>(decoded.value().find_control(model::ObjectId{2})->payload).buttons ==
        std::get<model::ButtonPayload>(document.find_control(model::ObjectId{2})->payload).buttons,
        "named recursive menu properties and actions must survive independent encoding and decoding");
    const auto& decoded_buttons = std::get<model::ButtonPayload>(decoded.value().find_control(model::ObjectId{2})->payload).buttons;
    expect(decoded_buttons[2].order == model::CommandBarButtonOrder::ascending &&
        decoded_buttons[2].buttons[2].order == model::CommandBarButtonOrder::descending,
        "each nested submenu order must survive its own footer entry");
    expect(decoded_buttons[3].order == model::CommandBarButtonOrder::none,
        "DontOrder must remain the default footer value");
    const auto repeated = form_stream::encode_document(decoded.value());
    expect(repeated.ok() && list_stream::dump_compact(repeated.value()) == list_stream::dump_compact(encoded.value()),
        "menu identity must be derived deterministically without preserving a source payload");
    const auto menu_at = [](auto& root) -> auto& { return root.items[1].items[2].items[2].items[1].items[2].items[1].items[12]; };
    auto dangling = encoded.value();
    auto& menu = menu_at(dangling);
    const auto count = static_cast<std::size_t>(std::stoul(menu.items[4].atom));
    menu.items[6 + count].items[5] = list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
    expect(!form_stream::decode_document(dangling, "Menu"), "dangling action references must be rejected");
    auto flags = encoded.value(); menu_at(flags).items[5].items[5] = list_stream::ListValue::raw_atom("16");
    expect(!form_stream::decode_document(flags, "Menu"), "unknown menu action flags must be rejected");
    auto cycle = encoded.value();
    menu_at(cycle).items[6 + count].items[10].items[7] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(cycle, "Menu"), "inconsistent submenu targets must be rejected");
    auto empty_order_footer = encoded.value();
    auto& empty_footer = menu_at(empty_order_footer).items[6 + count].items.back().items[2];
    empty_footer.items.clear();
    expect(!form_stream::decode_document(empty_order_footer, "Menu"), "empty menu order footer must be rejected safely");
    auto huge_order_footer = encoded.value();
    menu_at(huge_order_footer).items[6 + count].items.back().items[2].items[0] =
        list_stream::ListValue::raw_atom("18446744073709551615");
    expect(!form_stream::decode_document(huge_order_footer, "Menu"), "oversized menu order footer count must be rejected");
}

void test_command_bar_owner_pair_and_strict_profile() {
    model::Form form;
    form.id = model::ObjectId{1}; form.name = "CommandBarOwnerPair";
    form.children = {model::ControlRef{model::ObjectId{1}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode command_bar{model::ObjectId{1}, "Tools", model::CommandBarPayload{}};
    command_bar.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    command_bar.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::string("Run tools"));
    model::CommandBarButton action; action.name = "Run"; action.action = "RunHandler";
    action.picture = model::PictureRef{model::PictureAssetRef{}, model::QualifiedName{"PictureLib.ActivateTask"}};
    model::CommandBarButton submenu; submenu.name = "More";
    submenu.type = model::CommandBarButtonKind::submenu;
    submenu.buttons = {action};
    std::get<model::CommandBarPayload>(command_bar.payload).buttons = {submenu};
    document.add_control(std::move(command_bar));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "CommandBar with owner and submenu ID 1 must encode" :
        encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "CommandBarOwnerPair");
    expect(decoded.ok(), decoded ? "CommandBar root pair and submenu pair must decode" :
        decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* control = decoded.value().find_control(model::ObjectId{1});
    expect(control != nullptr && control->kind() == model::ControlKind::command_bar &&
        std::get<model::CommandBarPayload>(control->payload).buttons ==
            std::get<model::CommandBarPayload>(document.find_control(model::ObjectId{1})->payload).buttons,
        "root (marker,1) and submenu (header owner,1) must remain separate groups");
    expect(!std::get<bool>(control->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
        std::get<std::string>(control->properties().find(model::PropertyId::from_name("ToolTip"))->value) == "Run tools",
        "CommandBar named Enabled and ToolTip values must round-trip independently of Buttons");

    auto unknown_default = encoded.value();
    auto& control_record = unknown_default.items[1].items[2].items[2].items[1];
    control_record.items[2].items[1].items[1] = list_stream::ListValue::raw_atom("1");
    const auto rejected = form_stream::decode_document(unknown_default, "CommandBarUnknownDefault");
    expect(!rejected, "noncanonical unmodeled CommandBar default slot must be rejected");

    auto wrong_root_marker = encoded.value();
    wrong_root_marker.items[1].items[2].items[2].items[1].items[2].items[1].items[8] =
        list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
    expect(!form_stream::decode_document(wrong_root_marker, "CommandBarWrongRootMarker"),
        "unsupported root owner marker must be rejected");
    auto wrong_root_id = encoded.value();
    wrong_root_id.items[1].items[2].items[2].items[1].items[2].items[1].items[9] =
        list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(wrong_root_id, "CommandBarWrongRootId"),
        "root group ID that differs from control ID must be rejected");
    auto unsupported_base_leaf = encoded.value();
    unsupported_base_leaf.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[20] =
        list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(unsupported_base_leaf, "CommandBarUnknownBaseLeaf"),
        "noncanonical unmodeled CommandBar base leaf must be rejected");

    const auto encode_owner_four = [](std::vector<model::CommandBarButton> entries) {
        model::Form owner_form; owner_form.id = model::ObjectId{1}; owner_form.name = "CommandBarOwnerFour";
        owner_form.children = {model::ControlRef{model::ObjectId{4}}};
        model::OrdinaryFormDocument owner_document(std::move(owner_form));
        model::ControlNode owner_bar{model::ObjectId{4}, "Tools", model::CommandBarPayload{}};
        std::get<model::CommandBarPayload>(owner_bar.payload).buttons = std::move(entries);
        owner_document.add_control(std::move(owner_bar));
        return form_stream::encode_document(owner_document);
    };
    const auto menu_for_owner_four = [](const list_stream::ListValue& payload) -> const list_stream::ListValue& {
        const auto& records = payload.items[1].items[2].items[2].items;
        const auto record = std::ranges::find(records, std::string("4"), [](const auto& row) {
            return row.items.size() > 1 ? row.items[1].atom : std::string{};
        });
        if (record == records.end()) throw std::runtime_error("CommandBar owner ID 4 record is absent");
        return record->items[2].items[1].items[7];
    };
    const auto empty_owner_four = encode_owner_four({});
    expect(empty_owner_four.ok() && menu_for_owner_four(empty_owner_four.value()).items[2].atom == "0",
        "empty menu max ID must stay in menu-entry namespace even when root ID is 4");
    model::CommandBarButton one_entry; one_entry.name = "Only"; one_entry.action = "OnlyHandler";
    const auto one_entry_owner_four = encode_owner_four({one_entry});
    expect(one_entry_owner_four.ok() && menu_for_owner_four(one_entry_owner_four.value()).items[2].atom == "1",
        "one-entry menu max ID must be 1, independent of root owner ID 4");
}

void test_captured_command_bar_control_record_literal() {
    constexpr std::string_view captured_record = R"OOF({e69bf21d-97b2-4f37-86db-675aea9ec2cb,4,{2,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},9,2,0,0,1,1,{5,f6183561-313a-4f39-a1cd-591a0c2b8493,1,1,1,{8,7919d563-1cca-46f4-809c-27d4d06a62a5,1,e1692cc2-605b-4535-84dd-28440238746c,{3,"ProbeHandler",{1,"",{1,0},{1,0},{1,0},{4,0,{0},"",-1,-1,1,0,""},{0,0,0}}},0,0,0},1,{5,b78f2e80-ec68-11d4-9dcf-0050bae2bc79,4,0,1,7919d563-1cca-46f4-809c-27d4d06a62a5,{8,"ProbeAction",0,1,{1,1,{"ru","Probe"}},1,f6183561-313a-4f39-a1cd-591a0c2b8493,1,1e2,0,0,1,0,1,0,0},{-1,0,{0}}}},b78f2e80-ec68-11d4-9dcf-0050bae2bc79,4,9d0a2e40-b978-11d4-84b6-008048da06df,0,0,0}},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,1,2,0,0},{14,"ResearchCommandBar",4294967295,0,0,0},{0}})OOF";
    const auto actual_record = list_stream::parse(captured_record);
    expect(actual_record.is_list && actual_record.items.size() == 6,
        "captured literal CommandBar tuple must retain all six top-level fields");
    expect(actual_record.items[2].items[1].items[7].items[2].atom == "1" &&
        actual_record.items[2].items[1].items[9].atom == "4",
        "captured owner ID 4 tuple confirms menu max ID 1 is not the root ID");

    model::Form form; form.id = model::ObjectId{1}; form.name = "CapturedCommandBar";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{4}}};
    model::OrdinaryFormDocument seeded(std::move(form));
    seeded.add_control(model::ControlNode{model::ObjectId{2}, "Seed", model::ButtonPayload{}});
    seeded.add_control(model::ControlNode{model::ObjectId{4}, "ResearchCommandBar", model::CommandBarPayload{}});
    auto payload = form_stream::encode_document(seeded);
    expect(payload.ok(), "known Form plus control ID 2 seed must encode before literal injection");
    auto& records = payload.value().items[1].items[2].items[2].items;
    const auto target = std::ranges::find(records, std::string("4"), [](const auto& row) {
        return row.items.size() > 1 ? row.items[1].atom : std::string{};
    });
    expect(target != records.end(), "seeded known Form must expose the CommandBar owner record");
    *target = actual_record;
    const auto decoded = form_stream::decode_document(payload.value(), "CapturedCommandBar");
    expect(decoded.ok(), decoded ? "captured full control tuple must decode without rewriting geometry or info" :
        decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* control = decoded.value().find_control(model::ObjectId{4});
    expect(control != nullptr && control->kind() == model::ControlKind::command_bar &&
        control->name == "ResearchCommandBar" &&
        !std::get<model::CommandBarPayload>(control->payload).buttons.empty(),
        "literal actual CommandBar control must be decoded with its named owner and entries");
}

void test_button_menu_mode_round_trip_and_validation() {
    static constexpr std::string_view members[] = {"DontUse", "Use", "UseExtra"};
    const std::string menu_block =
        "{5,53232d71-06b1-4ec1-a94d-77fafadef407,0,1,0,1,"
        "{5,31946946-0a9b-40a2-95cf-82f200778341,0,0,0,{-1,0,{0}}}}";
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "MenuModes";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{4}},
                     model::ControlRef{model::ObjectId{6}}};
    model::OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < std::size(members); ++index) {
        model::ControlNode button{model::ObjectId{static_cast<std::uint64_t>(2 + index * 2)},
            "Button" + std::to_string(index), model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("MenuMode"),
            model::EnumerationValue{"MenuMode", std::string(members[index])});
        document.add_control(std::move(button));
    }

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "all supported Button MenuMode values must encode" :
        encoded.diagnostics().front().message);
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    for (std::size_t index = 0; index < std::size(members); ++index) {
        const auto& properties = records[index + 1].items[2].items[1];
        const auto mode = static_cast<std::int32_t>(index);
        expect(properties.items.size() == (mode == 0 ? 16 : 17) &&
                   properties.items[5].atom == "0" &&
                   properties.items[11].atom == std::to_string(mode),
            "MenuMode must occupy field 11 without disturbing HorizontalAlign or changing the confirmed arity");
        if (mode == 0) {
            expect(properties.items[12].atom == "0" && properties.items[13].atom == "0" &&
                       properties.items[14].atom == "0" && properties.items[15].atom == "1",
                "DontUse must keep the four canonical trailing values in place");
        } else {
            expect(list_stream::dump_compact(properties.items[12]) == menu_block &&
                       properties.items[13].atom == "0" && properties.items[14].atom == "0" &&
                       properties.items[15].atom == "0" && properties.items[16].atom == "1",
                "Use and UseExtra must insert only the confirmed internal menu descriptor before trailing fields");
        }
    }

    const auto decoded = form_stream::decode_document(encoded.value(), "MenuModes");
    expect(decoded.ok(), decoded ? "all supported Button MenuMode values must decode" :
        decoded.diagnostics().front().message);
    expect(decoded.value().find_control(model::ObjectId{2})->properties().find(
               model::PropertyId::from_name("MenuMode")) == nullptr,
        "default DontUse must normalize to an absent explicit property");
    for (const auto& [id, member] : {std::pair{4U, std::string_view("Use")},
                                    std::pair{6U, std::string_view("UseExtra")}}) {
        const auto* value = decoded.value().find_control(model::ObjectId{id})->properties().find(
            model::PropertyId::from_name("MenuMode"));
        expect(value != nullptr && std::get<model::EnumerationValue>(value->value) ==
                   model::EnumerationValue{"MenuMode", std::string(member)},
            "non-default MenuMode member must survive decoding as a named enumeration");
    }
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "all three MenuMode values must encode-decode-encode without storage drift");

    auto wrong_mode = encoded.value();
    wrong_mode.items[1].items[2].items[2].items[1].items[2].items[1].items[11] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(wrong_mode, "MenuModes"), "OOF1114",
        "$/1/2/2/1/2/1/11", "unknown MenuMode storage values must fail closed");
    auto wrong_arity_default = encoded.value();
    wrong_arity_default.items[1].items[2].items[2].items[1].items[2].items[1].items.push_back(
        list_stream::ListValue::raw_atom("0"));
    expect(!form_stream::decode_document(wrong_arity_default, "MenuModes"),
        "DontUse must reject a 17-field record");
    auto wrong_arity_enabled = encoded.value();
    wrong_arity_enabled.items[1].items[2].items[2].items[2].items[2].items[1].items.pop_back();
    expect(!form_stream::decode_document(wrong_arity_enabled, "MenuModes"),
        "Use must reject a 16-field record");
    auto unknown_menu_descriptor = encoded.value();
    unknown_menu_descriptor.items[1].items[2].items[2].items[2].items[2].items[1].items[12].items[0] =
        list_stream::ListValue::raw_atom("6");
    expect(!form_stream::decode_document(unknown_menu_descriptor, "MenuModes"),
        "unknown menu format version must be rejected rather than preserved as raw data");

    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "InvalidMenuMode";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument wrong_type(std::move(invalid_form));
    model::ControlNode wrong_type_button{model::ObjectId{2}, "WrongType", model::ButtonPayload{}};
    wrong_type_button.properties().set_explicit(model::PropertyId::from_name("MenuMode"),
        model::EnumerationValue{"PictureSize", "Use"});
    wrong_type.add_control(std::move(wrong_type_button));
    expect_failure(form_stream::encode_document(wrong_type), "OOF1122", "$/Button/MenuMode",
        "foreign MenuMode enum type must be rejected");

    model::Form unknown_form;
    unknown_form.id = model::ObjectId{1};
    unknown_form.name = "UnknownMenuMode";
    unknown_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument unknown(std::move(unknown_form));
    model::ControlNode unknown_button{model::ObjectId{2}, "Unknown", model::ButtonPayload{}};
    unknown_button.properties().set_explicit(model::PropertyId::from_name("MenuMode"),
        model::EnumerationValue{"MenuMode", "Unknown"});
    unknown.add_control(std::move(unknown_button));
    expect_failure(form_stream::encode_document(unknown), "OOF1122", "$/Button/MenuMode",
        "unknown MenuMode enum members must be rejected");
}

void test_button_external_picture_assets_round_trip() {
    const std::vector<std::vector<std::uint8_t>> bytes{
        {'G','I','F','8','9','a',0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62},
        {137,80,78,71,13,10,26,10,9,8,7,6,5,4},
        {0xff,0xd8,0xff,1,2,3,4,5},
        {'B','M',1,2,3,4,5,6},
    };
    const std::array<model::PictureFormat, 4> formats{
        model::PictureFormat::gif, model::PictureFormat::png,
        model::PictureFormat::jpeg, model::PictureFormat::bmp};
    const std::array<std::string_view, 4> names{"Gif", "Png", "Jpeg", "Bmp"};
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Pictures";
    for (std::size_t index = 0; index < formats.size(); ++index) {
        form.children.push_back(model::ControlRef{model::ObjectId{static_cast<std::uint64_t>(index + 2)}});
    }
    model::OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < formats.size(); ++index) {
        const model::ObjectId button_id{static_cast<std::uint64_t>(index + 2)};
        const model::ObjectId asset_id{static_cast<std::uint64_t>(index + 20)};
        model::PictureAsset asset{asset_id, "Items/" + std::string(names[index]) + "/Picture." +
            std::string(formats[index] == model::PictureFormat::jpeg ? "jpeg" :
                formats[index] == model::PictureFormat::gif ? "gif" :
                formats[index] == model::PictureFormat::png ? "png" : "bmp"), formats[index], bytes[index], index % 2 == 1};
        document.add_asset(std::move(asset));
        model::ControlNode button{button_id, std::string(names[index]), model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{asset_id}});
        document.add_control(std::move(button));
    }
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "named external GIF, PNG, JPEG, and BMP assets must encode");
    const auto decoded = form_stream::decode_document(encoded.value(), "Pictures");
    expect(decoded.ok(), "external Button picture descriptors must decode");
    expect(decoded.value().assets().size() == formats.size(), "each decoded button image must materialize one asset");
    for (std::size_t index = 0; index < formats.size(); ++index) {
        const auto* button = decoded.value().find_control(model::ObjectId{static_cast<std::uint64_t>(index + 2)});
        expect(button != nullptr, "picture button identity must survive");
        const auto* reference = button->properties().find(model::PropertyId::from_name("Picture"));
        expect(reference && std::holds_alternative<model::PictureRef>(reference->value), "picture must remain a named asset reference");
        const auto& asset_ref = std::get<model::PictureRef>(reference->value).asset;
        const auto* asset = decoded.value().find_asset(asset_ref.id());
        expect(asset && asset->format == formats[index] && asset->bytes == bytes[index] && asset->transparent == (index % 2 == 1),
            "picture format, bytes, and transparency must survive independently");
        expect(asset->relative_path == "Items/" + std::string(names[index]) + "/Picture." +
            std::string(formats[index] == model::PictureFormat::jpeg ? "jpeg" :
                formats[index] == model::PictureFormat::gif ? "gif" :
                formats[index] == model::PictureFormat::png ? "png" : "bmp"),
            "decoded asset must receive its named external package path");
    }
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "all four external picture descriptors must re-encode without loss");

    auto missing_bytes = document;
    missing_bytes.set_asset_bytes(model::ObjectId{20}, {});
    expect_failure(form_stream::encode_document(missing_bytes), "OOF1122", "$/Button/Picture",
        "empty asset bytes must be rejected");
    model::Form orphan_form;
    orphan_form.id = model::ObjectId{1};
    orphan_form.name = "Orphan";
    auto orphan = model::OrdinaryFormDocument(std::move(orphan_form));
    orphan.add_asset(model::PictureAsset{model::ObjectId{20}, "Items/Unused/Picture.gif", model::PictureFormat::gif, bytes[0], false});
    expect_failure(form_stream::encode_document(orphan), "OOF1122", "$/PictureAssets",
        "unreferenced external assets must be rejected");

    auto invalid_signature = document;
    invalid_signature.set_asset_bytes(model::ObjectId{20}, {'n','o','t','a','g','i','f'});
    expect_failure(form_stream::encode_document(invalid_signature), "OOF1122", "$/Button/Picture",
        "image signature and declared format must agree");
    auto invalid_transparency = encoded.value();
    invalid_transparency.items[1].items[2].items[2].items[1].items[2].items[1].items[8].items[6] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(invalid_transparency, "Pictures"), "OOF1114", "$/1/2/2/1/2/1/8/6",
        "unknown transparency descriptor value must be rejected");
    auto invalid_base64 = encoded.value();
    invalid_base64.items[1].items[2].items[2].items[1].items[2].items[1].items[8].items[7].items[0].items[0] =
        list_stream::ListValue::raw_atom("#base64:!!!!");
    expect_failure(form_stream::decode_document(invalid_base64, "Pictures"), "OOF1114", "$/1/2/2/1/2/1/8/7/0/0",
        "malformed Button image base64 must be rejected");
}

void test_all_standard_button_pictures_round_trip_without_assets() {
    const auto descriptors = model::metamodel::standard_picture_descriptors();
    expect(descriptors.size() == 294, "the complete sanitized standard picture catalog must be present");
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "StandardPictures";
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        form.children.push_back(model::ControlRef{model::ObjectId{1000 + index}});
    }
    model::OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto id = model::ObjectId{1000 + index};
        const auto name = "Std" + std::to_string(index);
        model::ControlNode button{id, name, model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
                model::QualifiedName{std::string(descriptors[index].runtime_name)}});
        document.add_control(std::move(button));
    }
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "all standard-picture descriptors must encode through Button.Picture");
    std::vector<std::string> storage_identities;
    std::function<void(const list_stream::ListValue&)> collect_identities = [&](const auto& value) {
        if (value.is_list && value.items.size() == 9 && value.items[0].atom == "4" &&
            value.items[1].atom == "1" && value.items[2].is_list) {
            storage_identities.push_back(list_stream::dump_compact(value.items[2]));
        }
        for (const auto& item : value.items) collect_identities(item);
    };
    collect_identities(encoded.value());
    expect(storage_identities.size() == descriptors.size(),
        "each standard picture must use the observed list-wrapped identity shape");
    for (const auto& descriptor : descriptors) {
        const std::string identity = descriptor.guid.empty()
            ? "{" + std::to_string(descriptor.storage_id) + "}"
            : "{0," + std::string(descriptor.guid) + "}";
        expect(std::find(storage_identities.begin(), storage_identities.end(), identity) != storage_identities.end(),
            "standard picture descriptor must use its observed GUID or negative-ID list shape");
    }
    const auto decoded = form_stream::decode_document(encoded.value(), "StandardPictures");
    expect(decoded.ok(), "all standard-picture descriptors must decode through Button.Picture");
    expect(decoded.value().assets().empty(), "standard pictures must not create external picture assets");
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto* button = decoded.value().find_control(model::ObjectId{1000 + index});
        const auto* property = button == nullptr ? nullptr : button->properties().find(model::PropertyId::from_name("Picture"));
        expect(property && std::holds_alternative<model::PictureRef>(property->value),
            "standard-picture property must remain a typed reference");
        const auto& reference = std::get<model::PictureRef>(property->value);
        expect(reference.standard_name == model::QualifiedName{std::string(descriptors[index].runtime_name)} &&
                   reference.asset.id().value() == 0,
            "the exact standard descriptor must survive without an asset target");
    }
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "standard-picture streams must re-encode canonically");

    model::Form bad_form;
    bad_form.id = model::ObjectId{1};
    bad_form.name = "UnknownStandard";
    bad_form.children.push_back(model::ControlRef{model::ObjectId{2}});
    model::OrdinaryFormDocument bad(std::move(bad_form));
    model::ControlNode button{model::ObjectId{2}, "Unknown", model::ButtonPayload{}};
    button.properties().set_explicit(model::PropertyId::from_name("Picture"),
        model::PictureRef{model::PictureAssetRef{model::ObjectId{0}}, model::QualifiedName{"PictureLib.Unknown"}});
    bad.add_control(std::move(button));
    expect_failure(form_stream::encode_document(bad), "OOF1123", "$",
        "unknown named standard pictures must be rejected");
}

void test_button_then_label_decoration_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{3}},
        model::ControlRef{model::ObjectId{7}},
        model::ControlRef{model::ObjectId{8}},
        model::ControlRef{model::ObjectId{9}},
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
    model::ControlNode center_label{model::ObjectId{8}, "CenterLabel", model::LabelDecorationPayload{}};
    center_label.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"HorizontalAlign", "Center"});
    document.add_control(std::move(center_label));
    model::ControlNode right_label{model::ObjectId{9}, "RightLabel", model::LabelDecorationPayload{}};
    right_label.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"HorizontalAlign", "Right"});
    document.add_control(std::move(right_label));

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
    expect(decoded.value().form().children.size() == 5, "Button and four LabelDecorations order must survive");
    expect(std::get<model::ControlRef>(decoded.value().form().children[0]).id() == model::ObjectId{2} &&
               std::get<model::ControlRef>(decoded.value().form().children[1]).id() == model::ObjectId{3} &&
               std::get<model::ControlRef>(decoded.value().form().children[2]).id() == model::ObjectId{7} &&
               std::get<model::ControlRef>(decoded.value().form().children[3]).id() == model::ObjectId{8} &&
               std::get<model::ControlRef>(decoded.value().form().children[4]).id() == model::ObjectId{9},
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
    for (const auto& [id, member] : {std::pair{model::ObjectId{8}, std::string_view{"Center"}},
                                     std::pair{model::ObjectId{9}, std::string_view{"Right"}}}) {
        const auto* label_control = decoded.value().find_control(id);
        const auto* alignment = label_control->properties().find(model::PropertyId::from_name("HorizontalAlign"));
        expect(alignment && std::get<model::EnumerationValue>(alignment->value) ==
                   model::EnumerationValue{"HorizontalAlign", std::string(member)},
            "LabelDecoration Center and Right must round-trip through their observed storage values");
    }

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
    expect(rejects_alignment(model::EnumerationValue{"HorizontalAlign", "Justify"}),
        "runtime-rejected LabelDecoration Justify must remain unsupported");

    auto unknown_storage_value = encoded.value();
    auto& unknown_label = unknown_storage_value.items[1].items[2].items[2].items[2];
    unknown_label.items[2].items[1].items[3] = list_stream::ListValue::raw_atom("3");
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

void test_label_decoration_observed_center_right_records() {
    const auto decode_observed = [](std::string_view record, std::string_view expected_member) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "ObservedLabelAlignment";
        form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
        model::ControlNode label{model::ObjectId{3}, "Notice", model::LabelDecorationPayload{}};
        label.position.left.set(10);
        label.position.top.set(45);
        label.position.width.set(160);
        label.position.height.set(65);
        document.add_control(std::move(label));
        auto stream = form_stream::encode_document(document);
        expect(stream.ok(), "seed stream must encode before inserting an observed LabelDecoration record");
        stream.value().items[1].items[2].items[2].items[2] = list_stream::parse(record);
        const auto decoded = form_stream::decode_document(stream.value(), "ObservedLabelAlignment");
        expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().code + ":" +
            decoded.diagnostics().front().path + ":" + decoded.diagnostics().front().message);
        const auto* decoded_label = decoded.value().find_control(model::ObjectId{3});
        const auto* alignment = decoded_label == nullptr ? nullptr : decoded_label->properties().find(
            model::PropertyId::from_name("HorizontalAlign"));
        expect(decoded_label != nullptr && decoded_label->name == "Notice" && alignment != nullptr &&
                   std::get<model::EnumerationValue>(alignment->value) ==
                       model::EnumerationValue{"HorizontalAlign", std::string(expected_member)},
            "complete observed LabelDecoration record must decode its named alignment value");
    };
    decode_observed(R"OOF(
{0fc7e20d-f241-460c-bdf4-5ad88e5474a5,3,
{3,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},11,
{1,1,
{"ru","Notice"}
},1,1,0,0,0,
{0,0,0},0,
{1,0},1,
{10,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,2,0,0,1,2},4,0,0,0,0,0,0,0},
{0}
},
{8,10,45,160,65,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,1,2,0,0},
{14,"Notice",4294967295,0,0,0},
{0}
}
)OOF", "Center");
    decode_observed(R"OOF(
{0fc7e20d-f241-460c-bdf4-5ad88e5474a5,3,
{3,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},11,
{1,1,
{"ru","Notice"}
},2,1,0,0,0,
{0,0,0},0,
{1,0},1,
{10,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,2,0,0,1,2},4,0,0,0,0,0,0,0},
{0}
},
{8,10,45,160,65,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,1,2,0,0},
{14,"Notice",4294967295,0,0,0},
{0}
}
)OOF", "Right");
}

void test_label_enabled_and_tooltip_round_trip() {
    const auto encode_label = [](bool explicit_defaults, bool enabled, std::string tool_tip) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "LabelProperties";
        form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
        model::ControlNode label{model::ObjectId{3}, "Notice", model::LabelDecorationPayload{}};
        if (explicit_defaults || !enabled) {
            label.properties().set_explicit(model::PropertyId::from_name("Enabled"), enabled);
        }
        if (explicit_defaults || !tool_tip.empty()) {
            label.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tool_tip));
        }
        document.add_control(std::move(label));
        return form_stream::encode_document(document);
    };

    const auto implicit_defaults = encode_label(false, true, "");
    const auto explicit_defaults = encode_label(true, true, "");
    expect(implicit_defaults.ok() && explicit_defaults.ok(),
        "LabelDecoration defaults must encode with implicit and explicit model values");
    expect(list_stream::dump_compact(implicit_defaults.value()) ==
               list_stream::dump_compact(explicit_defaults.value()),
        "explicit LabelDecoration Enabled=true and empty ToolTip must omit from storage");

    const std::string tool_tip = "Подсказка Ω & <важно> \"цитата\"\nВторая\rстрока";
    const auto variant = encode_label(false, false, tool_tip);
    expect(variant.ok(), variant ? "LabelDecoration Enabled and ToolTip must encode" :
        variant.diagnostics().front().path + ": " + variant.diagnostics().front().message);
    const auto& label_record = variant.value().items[1].items[2].items[2].items[2];
    const auto& label_base = label_record.items[2].items[1].items[0];
    expect(label_base.items.size() == 21 && label_base.items[1].atom == "0" &&
               label_base.items[12].is_list &&
               list_stream::dump_compact(label_base.items[12]) !=
                   list_stream::dump_compact(implicit_defaults.value().items[1].items[2].items[2].items[2]
                       .items[2].items[1].items[0].items[12]),
        "LabelDecoration Enabled and ToolTip must occupy their observed named storage slots");

    const auto decoded = form_stream::decode_document(variant.value(), "LabelProperties");
    expect(decoded.ok(), "LabelDecoration Enabled and ToolTip must decode");
    const auto* label = decoded.value().find_control(model::ObjectId{3});
    expect(label != nullptr, "LabelDecoration must survive property decoding");
    const auto* enabled = label->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* decoded_tool_tip = label->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(enabled && !std::get<bool>(enabled->value),
        "LabelDecoration Enabled=false must survive decoding");
    expect(decoded_tool_tip && std::get<std::string>(decoded_tool_tip->value) ==
               "Подсказка Ω & <важно> \"цитата\"\nВторая\rстрока",
        "LabelDecoration ToolTip must round-trip Unicode, XML punctuation, and mixed newlines");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(variant.value()),
        "LabelDecoration Enabled and ToolTip storage must round-trip without drift");

    auto unsupported_localization = variant.value();
    auto& tooltip_slot = unsupported_localization.items[1].items[2].items[2].items[2]
        .items[2].items[1].items[0].items[12];
    tooltip_slot = list_stream::parse(value_codec::encode_localized_string(
        model::LocalizedStringValue{{{"en", "Hint"}, {"ru", "Подсказка"}}}));
    expect_failure(
        form_stream::decode_document(unsupported_localization, "LabelProperties"),
        "OOF1115",
        "$/1/2/2/2/2/1/0/12",
        "LabelDecoration ToolTip must reject multiple localized values");
}

void test_picture_decoration_default_enabled_tooltip_round_trip_and_rejections() {
    const auto make_document = [](bool explicit_defaults, bool enabled, std::string tool_tip) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "PictureDecorationCodec";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode picture{
            model::ObjectId{2}, "Picture", model::PictureDecorationPayload{}};
        if (explicit_defaults || !enabled) {
            picture.properties().set_explicit(model::PropertyId::from_name("Enabled"), enabled);
        }
        if (explicit_defaults || !tool_tip.empty()) {
            picture.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tool_tip));
        }
        picture.position.left.set(27);
        picture.position.top.set(18);
        picture.position.width.set(96);
        picture.position.height.set(44);
        picture.position.visible.set(false);
        document.add_control(std::move(picture));
        return document;
    };
    const auto picture_record = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };
    const auto mutable_picture_record = [](list_stream::ListValue& encoded) -> list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };
    const auto picture_base = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1].items[2].items[1].items[0];
    };
    const auto mutable_picture_base = [](list_stream::ListValue& encoded) -> list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1].items[2].items[1].items[0];
    };

    constexpr std::string_view observed_control_record = R"RAW(
{151ef23e-6bb2-4681-83d0-35bc2217230c,2,{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},20,0,0,{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,2,0,0,1,2},{0,0,0},1,1,0,0,{1,0},0,1,1,1},{0}},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,1,2,0,0},{14,"Picture",4294967295,0,0,0},{0}}
)RAW";
    const auto captured_picture_record = list_stream::parse(observed_control_record);
    const auto& captured_picture_info = captured_picture_record.items[2];
    const auto& captured_picture_base = captured_picture_info.items[1].items[0];
    const auto implicit_default = form_stream::encode_document(make_document(false, true, ""));
    const auto explicit_default = form_stream::encode_document(make_document(true, true, ""));
    expect(implicit_default.ok() && explicit_default.ok(),
        "default PictureDecoration must encode from a named model without a Form.bin fixture");
    expect(list_stream::dump_compact(implicit_default.value()) ==
               list_stream::dump_compact(explicit_default.value()),
        "explicit default PictureDecoration Enabled and ToolTip must normalize to omitted defaults");

    const auto& default_record = picture_record(implicit_default.value());
    const auto& default_info = default_record.items[2];
    const auto& default_base = picture_base(implicit_default.value());
    expect(default_record.items[0].atom == "151ef23e-6bb2-4681-83d0-35bc2217230c" &&
               default_info.items[0].atom == "1" && default_base.items.size() == 21 &&
               list_stream::dump_compact(default_base) == list_stream::dump_compact(captured_picture_base) &&
               list_stream::dump_compact(default_info.items[1]) ==
                   list_stream::dump_compact(captured_picture_info.items[1]) &&
               list_stream::dump_compact(default_info.items[2]) == "{0}",
        "PictureDecoration must use the observed v1 info record, exact default base, and empty event table");
    model::Form raw_form;
    raw_form.id = model::ObjectId{1};
    raw_form.name = "PictureDecorationObserved";
    raw_form.children = {model::ControlRef{model::ObjectId{3}}, model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument raw_document(std::move(raw_form));
    raw_document.add_control(model::ControlNode{
        model::ObjectId{3}, "BeforePicture", model::ButtonPayload{}});
    raw_document.add_control(model::ControlNode{
        model::ObjectId{2}, "Picture", model::PictureDecorationPayload{}});
    auto raw_envelope = form_stream::encode_document(raw_document);
    expect(raw_envelope.ok(), "named PictureDecoration envelope must be available for captured record test");
    mutable_picture_record(raw_envelope.value()) = captured_picture_record;
    const auto raw_decoded = form_stream::decode_document(raw_envelope.value(), "ObservedPictureDecoration");
    expect(raw_decoded.ok(), raw_decoded ? "actual PictureDecoration control record must decode" :
        raw_decoded.diagnostics().front().path + ": " + raw_decoded.diagnostics().front().message);
    const auto canonical_raw_roundtrip = form_stream::encode_document(raw_decoded.value());
    expect(canonical_raw_roundtrip.ok() &&
               list_stream::dump_compact(picture_record(canonical_raw_roundtrip.value())) ==
                   list_stream::dump_compact(captured_picture_record),
        "actual captured PictureDecoration control record must encode back canonically");

    const auto default_decoded = form_stream::decode_document(implicit_default.value(), "PictureDecorationCodec");
    expect(default_decoded.ok(), "default PictureDecoration record must decode");
    const auto* default_picture = default_decoded.value().find_control(model::ObjectId{2});
    expect(default_picture && default_picture->kind() == model::ControlKind::picture_decoration &&
               default_picture->name == "Picture" &&
               !default_picture->properties().find(model::PropertyId::from_name("Enabled")) &&
               !default_picture->properties().find(model::PropertyId::from_name("ToolTip")),
        "default PictureDecoration identity and implicit property defaults must decode by name");
    expect(default_picture->position.left.value() == 27 && default_picture->position.top.value() == 18 &&
               default_picture->position.width.value() == 96 && default_picture->position.height.value() == 44 &&
               !default_picture->position.visible.value(),
        "PictureDecoration Position and Visible must use the existing named geometry model");

    const std::string tool_tip = "Подсказка Ω <важно> & \"цитата\"\nВторая\rстрока";
    const auto changed = form_stream::encode_document(make_document(false, false, tool_tip));
    expect(changed.ok(), changed ? "PictureDecoration Enabled and ToolTip must encode" :
        changed.diagnostics().front().path + ": " + changed.diagnostics().front().message);
    const auto& changed_base = picture_base(changed.value());
    expect(changed_base.items[1].atom == "0" &&
               list_stream::dump_compact(changed_base.items[12]) == value_codec::encode_localized_string(
                   model::LocalizedStringValue{{{"ru", "Подсказка Ω <важно> & \"цитата\"\r\nВторая\rстрока"}}}),
        "PictureDecoration Enabled and ToolTip must occupy the observed base slots and canonicalize line endings");
    const auto decoded = form_stream::decode_document(changed.value(), "PictureDecorationCodec");
    expect(decoded.ok(), "PictureDecoration Enabled and ToolTip must decode");
    const auto* picture = decoded.value().find_control(model::ObjectId{2});
    const auto* enabled = picture == nullptr ? nullptr :
        picture->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* decoded_tool_tip = picture == nullptr ? nullptr :
        picture->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(enabled && !std::get<bool>(enabled->value) && decoded_tool_tip &&
               std::get<std::string>(decoded_tool_tip->value) == tool_tip,
        "PictureDecoration named properties must round-trip Unicode and mixed line endings");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(changed.value()),
        "PictureDecoration named properties must re-encode canonically");

    constexpr std::string_view properties_path = "$/1/2/2/1/2/1";
    constexpr std::string_view tool_tip_path = "$/1/2/2/1/2/1/0/12";
    auto malformed = changed.value();
    mutable_picture_base(malformed).items[12] = list_stream::ListValue::raw_atom("malformed");
    expect_failure(form_stream::decode_document(malformed, "PictureDecorationCodec"), "OOF1108",
        tool_tip_path, "malformed PictureDecoration ToolTip localization must be rejected");

    auto multilingual = changed.value();
    mutable_picture_base(multilingual).items[12] = list_stream::parse(value_codec::encode_localized_string(
        model::LocalizedStringValue{{{"ru", "Текст"}, {"en", "Text"}}}));
    expect_failure(form_stream::decode_document(multilingual, "PictureDecorationCodec"), "OOF1115",
        tool_tip_path, "multilingual PictureDecoration ToolTip must be rejected without loss");

    auto unsupported_leaf = implicit_default.value();
    mutable_picture_base(unsupported_leaf).items[5] = list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(unsupported_leaf, "PictureDecorationCodec"), "OOF1114",
        properties_path, "non-default unimplemented PictureDecoration base data must fail closed");

    auto unsupported_picture_tail = implicit_default.value();
    unsupported_picture_tail.items[1].items[2].items[2].items[1].items[2].items[1].items[1] =
        list_stream::ListValue::raw_atom("19");
    expect_failure(form_stream::decode_document(unsupported_picture_tail, "PictureDecorationCodec"), "OOF1114",
        "$/1/2/2/1/2/1", "non-default unimplemented PictureDecoration tuple data must fail closed");

    auto picture_property_document = make_document(false, true, "");
    const_cast<model::ControlNode*>(picture_property_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
                model::QualifiedName{"PictureLib.Write"}});
    const auto picture_encoded = form_stream::encode_document(picture_property_document);
    expect(picture_encoded.ok(), "standard PictureDecoration.Picture must encode");
    constexpr std::string_view observed_write_control_record = R"RAW(
{151ef23e-6bb2-4681-83d0-35bc2217230c,2,
{1,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},20,0,0,
{10,0,
{4,1,
{0,894cf65b-4109-4533-a1d7-c87b1fcc80a3},"",-1,-1,0,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,2,0,0,1,2},
{0,0,0},1,1,0,0,
{1,0},0,1,1,1},
{0}
},
{8,20,20,140,90,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,0,1,0,0},
{14,"Picture",4294967295,0,0,0},
{0}
}
)RAW";
    auto actual_picture_stream = picture_encoded.value();
    auto& actual_picture_record = actual_picture_stream.items[1].items[2].items[2].items[1];
    actual_picture_record = list_stream::parse(observed_write_control_record);
    const auto actual_picture_decoded = form_stream::decode_document(actual_picture_stream, "ObservedPictureWrite");
    expect(actual_picture_decoded.ok(), "actual PictureDecoration record from runtime after PictureLib.Write must decode");
    const auto actual_picture_reencoded = form_stream::encode_document(actual_picture_decoded.value());
    expect(actual_picture_reencoded.ok() &&
               list_stream::dump_compact(picture_record(actual_picture_reencoded.value())) ==
                   list_stream::dump_compact(list_stream::parse(observed_write_control_record)),
        "actual runtime PictureDecoration picture record must encode back canonically");
    constexpr std::string_view observed_write_picture =
        "{4,1,{0,894cf65b-4109-4533-a1d7-c87b1fcc80a3},\"\",-1,-1,0,0,\"\"}";
    const auto& picture_properties = picture_record(picture_encoded.value()).items[2].items[1];
    expect(
               list_stream::dump_compact(picture_properties.items[4].items[2]) == observed_write_picture,
        "PictureDecoration.Picture must use the exact observed PictureLib.Write record tuple");
    const auto picture_decoded = form_stream::decode_document(picture_encoded.value(), "PictureDecorationPicture");
    expect(picture_decoded.ok(), "standard PictureDecoration.Picture must decode");
    const auto* decoded_control = picture_decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_picture = decoded_control == nullptr ? nullptr :
        decoded_control->properties().find(model::PropertyId::from_name("Picture"));
    expect(decoded_picture && std::holds_alternative<model::PictureRef>(decoded_picture->value) &&
               std::get<model::PictureRef>(decoded_picture->value).standard_name ==
               model::QualifiedName{"PictureLib.Write"},
        "observed PictureDecoration picture identity must decode to a typed PictureRef");
    const auto picture_reencoded = form_stream::encode_document(picture_decoded.value());
    expect(picture_reencoded.ok() && list_stream::dump_compact(picture_reencoded.value()) ==
               list_stream::dump_compact(picture_encoded.value()),
        "standard PictureDecoration.Picture must re-encode canonically");

    auto unknown_picture_document = make_document(false, true, "");
    const_cast<model::ControlNode*>(unknown_picture_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
                model::QualifiedName{"PictureLib.Unknown"}});
    expect_failure(form_stream::encode_document(unknown_picture_document), "OOF1123",
        "$", "unknown standard PictureLib names must be rejected before encoding");

    auto external_picture_document = make_document(false, true, "");
    const_cast<model::ControlNode*>(external_picture_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{20}}});
    const std::vector<std::uint8_t> external_gif{
        0x47,0x49,0x46,0x38,0x39,0x61,0x01,0x00,0x01,0x00,0x80,0x00,0x00,0x00,0x00,0x00,
        0xff,0xff,0xff,0x21,0xf9,0x04,0x01,0x00,0x00,0x00,0x00,0x2c,0x00,0x00,0x00,0x00,
        0x01,0x00,0x01,0x00,0x00,0x02,0x01,0x44,0x00,0x3b};
    external_picture_document.add_asset(model::PictureAsset{model::ObjectId{20},
        "Items/Picture/Picture.gif", model::PictureFormat::gif, external_gif, false});
    const auto external_picture_encoded = form_stream::encode_document(external_picture_document);
    expect(external_picture_encoded.ok(), "synthetic external PictureDecoration GIF must encode");
    const auto& external_picture_value = picture_record(external_picture_encoded.value()).items[2].items[1].items[4].items[2];
    expect(external_picture_value.items.size() == 10 && external_picture_value.items[0].atom == "4" &&
               external_picture_value.items[1].atom == "3" && external_picture_value.items[6].atom == "0" &&
               external_picture_value.items[7].items[0].items[0].atom ==
                   "#base64:R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7",
        "PictureDecoration external GIF must use the observed Button picture tuple");
    const auto external_picture_decoded = form_stream::decode_document(
        external_picture_encoded.value(), "PictureDecorationExternalPicture");
    expect(external_picture_decoded.ok(), "synthetic external PictureDecoration GIF must decode");
    const auto* external_decoded_control = external_picture_decoded.value().find_control(model::ObjectId{2});
    const auto* external_decoded_property = external_decoded_control == nullptr ? nullptr :
        external_decoded_control->properties().find(model::PropertyId::from_name("Picture"));
    expect(external_decoded_property && std::holds_alternative<model::PictureRef>(external_decoded_property->value) &&
               external_picture_decoded.value().assets().size() == 1 &&
               external_picture_decoded.value().assets().front().bytes == external_gif &&
               external_picture_decoded.value().assets().front().format == model::PictureFormat::gif &&
               !external_picture_decoded.value().assets().front().transparent,
        "PictureDecoration external GIF must decode as a typed PictureRef and exact PictureAsset bytes");
    const auto external_picture_reencoded = form_stream::encode_document(external_picture_decoded.value());
    expect(external_picture_reencoded.ok() &&
               list_stream::dump_compact(picture_record(external_picture_reencoded.value())) ==
                   list_stream::dump_compact(picture_record(external_picture_encoded.value())),
        "PictureDecoration external GIF must re-encode canonically");

    auto unknown_property_document = make_document(false, true, "");
    const_cast<model::ControlNode*>(unknown_property_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Transparent"), true);
    expect_failure(form_stream::encode_document(unknown_property_document), "OOF1122",
        "$/PictureDecoration", "unimplemented non-default picture formatting must be rejected");

    auto event_document = make_document(false, true, "");
    auto* event_picture = const_cast<model::ControlNode*>(event_document.find_control(model::ObjectId{2}));
    event_picture->events.push_back(model::EventRef{model::ObjectId{3}});
    event_document.add_event(model::Event{
        model::ObjectId{3}, "Click", "PictureClick", model::ControlRef{model::ObjectId{2}}});
    expect_failure(form_stream::encode_document(event_document), "OOF1122", "$/PictureDecoration",
        "unimplemented PictureDecoration events must be rejected");
}

void test_splitter_observed_record_and_named_codec() {
    const auto make_document = [](std::string name) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "SplitterCodec";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{2}, std::move(name), model::SplitterPayload{}});
        return document;
    };
    const auto splitter_record = [](const list_stream::ListValue& stream) -> const list_stream::ListValue& {
        return stream.items[1].items[2].items[2].items[1];
    };
    const auto mutable_splitter_record = [](list_stream::ListValue& stream) -> list_stream::ListValue& {
        return stream.items[1].items[2].items[2].items[1];
    };

    constexpr std::string_view captured_record = R"SPLITTER(
{36e52348-5d60-4770-8e89-a16ed50a2006,2,
{0,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},1,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{-18},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},2,2,0}
},
{8,0,0,0,0,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,0,1,0,0},
{14,"SplitterProbe",4294967295,0,0,0},
{0}
}
)SPLITTER";
    const auto actual_record = list_stream::parse(captured_record);
    const auto defaults = form_stream::encode_document(make_document("SplitterProbe"));
    expect(defaults.ok(), defaults ? "default named Splitter must encode without a source binary" :
        defaults.diagnostics().front().path + ": " + defaults.diagnostics().front().message);
    const auto default_record_decoded = form_stream::decode_document(defaults.value(), "SplitterCodec");
    expect(default_record_decoded.ok(), "default encoded Splitter must decode");
    const auto* default_control = default_record_decoded.value().find_control(model::ObjectId{2});
    expect(default_control && default_control->kind() == model::ControlKind::splitter &&
               default_control->name == "SplitterProbe" &&
               !default_control->properties().find(model::PropertyId::from_name("Orientation")) &&
               !default_control->properties().find(model::PropertyId::from_name("Enabled")) &&
               !default_control->properties().find(model::PropertyId::from_name("ToolTip")),
        "default Splitter must decode to its named identity with implicit property defaults");

    auto observed_stream = form_stream::encode_document(make_document("SplitterProbe"));
    expect(observed_stream.ok(), "named Splitter envelope must encode before inserting the independent platform record");
    mutable_splitter_record(observed_stream.value()) = actual_record;
    const auto decoded_actual = form_stream::decode_document(observed_stream.value(), "CapturedSplitter");
    expect(decoded_actual.ok(), decoded_actual ? "" : decoded_actual.diagnostics().front().path + ": " +
        decoded_actual.diagnostics().front().message);
    const auto* actual_control = decoded_actual.value().find_control(model::ObjectId{2});
    expect(actual_control && actual_control->name == "SplitterProbe" &&
               actual_control->kind() == model::ControlKind::splitter,
        "full captured Splitter record must decode to the named Splitter model");
    const auto captured_roundtrip = form_stream::encode_document(decoded_actual.value());
    expect(captured_roundtrip.ok() && list_stream::dump_compact(splitter_record(captured_roundtrip.value())) ==
               list_stream::dump_compact(actual_record),
        "full captured Splitter record must re-encode without dropping its canonical payload");

    const std::string tool_tip = "Подсказка Ω <важно> & \"цитата\"\nВторая\rстрока";
    auto changed_document = make_document("SplitterChanged");
    auto* changed = const_cast<model::ControlNode*>(changed_document.find_control(model::ObjectId{2}));
    changed->properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    changed->properties().set_explicit(model::PropertyId::from_name("Orientation"),
        model::EnumerationValue{"Orientation", "Horizontal"});
    changed->properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    model::ColorValue back_color;
    back_color.kind = model::ColorKind::absolute;
    back_color.red = 31; back_color.green = 127; back_color.blue = 223;
    changed->properties().set_explicit(model::PropertyId::from_name("BackColor"), back_color);
    model::ColorValue border_color;
    border_color.kind = model::ColorKind::absolute;
    border_color.red = 223; border_color.green = 127; border_color.blue = 31;
    changed->properties().set_explicit(model::PropertyId::from_name("BorderColor"), border_color);
    const auto xml = oof::source::serialize_form_xml(changed_document);
    expect(xml.ok(), "named Splitter model must serialize as public XML");
    const auto parsed = oof::source::parse_form_xml(xml.value());
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto changed_stream = form_stream::encode_document(parsed.value());
    expect(changed_stream.ok(), changed_stream ? "" : changed_stream.diagnostics().front().path + ": " +
        changed_stream.diagnostics().front().message);
    const auto decoded_changed = form_stream::decode_document(changed_stream.value(), "SplitterChanged");
    expect(decoded_changed.ok(), "XML-only changed Splitter must decode after storage encoding");
    const auto* result = decoded_changed.value().find_control(model::ObjectId{2});
    expect(result && !std::get<bool>(result->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
               std::get<model::EnumerationValue>(result->properties().find(model::PropertyId::from_name("Orientation"))->value) ==
                   model::EnumerationValue{"Orientation", "Horizontal"} &&
               std::get<std::string>(result->properties().find(model::PropertyId::from_name("ToolTip"))->value) == tool_tip &&
               std::get<model::ColorValue>(result->properties().find(model::PropertyId::from_name("BackColor"))->value) == back_color &&
               std::get<model::ColorValue>(result->properties().find(model::PropertyId::from_name("BorderColor"))->value) == border_color,
        "named Splitter Enabled, Orientation, ToolTip, and observed RGB colors must survive XML-only round-trip");
    const auto reencoded = form_stream::encode_document(decoded_changed.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(changed_stream.value()),
        "changed Splitter storage must round-trip canonically");

    auto explicit_auto = make_document("SplitterProbe");
    const_cast<model::ControlNode*>(explicit_auto.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Orientation"),
            model::EnumerationValue{"Orientation", "Auto"});
    const auto auto_encoded = form_stream::encode_document(explicit_auto);
    expect(auto_encoded.ok() && list_stream::dump_compact(auto_encoded.value()) ==
               list_stream::dump_compact(defaults.value()),
        "explicit Splitter Orientation Auto must normalize to its implicit default");
    auto vertical_document = make_document("Vertical");
    const_cast<model::ControlNode*>(vertical_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Orientation"),
            model::EnumerationValue{"Orientation", "Vertical"});
    const auto vertical_encoded = form_stream::encode_document(vertical_document);
    expect(vertical_encoded.ok(), "observed Vertical orientation must encode");
    const auto vertical_decoded = form_stream::decode_document(vertical_encoded.value(), "SplitterVertical");
    const auto* vertical_control = vertical_decoded ?
        vertical_decoded.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* vertical_value = vertical_control ? vertical_control->properties().find(
        model::PropertyId::from_name("Orientation")) : nullptr;
    expect(vertical_decoded.ok() && vertical_value &&
               std::get<model::EnumerationValue>(vertical_value->value) ==
                   model::EnumerationValue{"Orientation", "Vertical"},
        "observed Vertical orientation must round-trip by its named enum value");

    auto unsupported_orientation = make_document("BadOrientation");
    const_cast<model::ControlNode*>(unsupported_orientation.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Orientation"),
            model::EnumerationValue{"Orientation", "Diagonal"});
    expect_failure(form_stream::encode_document(unsupported_orientation), "OOF1122", "$/Splitter/Orientation",
        "unknown Orientation members must be rejected");
    auto unsupported_property = make_document("BadProperty");
    const_cast<model::ControlNode*>(unsupported_property.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Border"), std::string("unsupported"));
    expect(!form_stream::encode_document(unsupported_property),
        "unimplemented Border property must be rejected before it is silently discarded");
    auto event_document = make_document("Eventful");
    auto* eventful = const_cast<model::ControlNode*>(event_document.find_control(model::ObjectId{2}));
    eventful->events.push_back(model::EventRef{model::ObjectId{4}});
    event_document.add_event(model::Event{model::ObjectId{4}, "OnChange", "Handler",
        model::ControlRef{model::ObjectId{2}}});
    expect(!form_stream::encode_document(event_document),
        "unimplemented Splitter events must be rejected before they are silently discarded");
    auto unsupported_storage = defaults.value();
    mutable_splitter_record(unsupported_storage).items[2].items[1].items[0].items[5] =
        list_stream::ListValue::raw_atom("0");
    expect_failure(form_stream::decode_document(unsupported_storage, "SplitterCodec"), "OOF1114",
        "$/1/2/2/1/2/1", "non-default unimplemented Splitter base data must fail closed");
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

void test_radio_button_basic_observed_record_and_rejections() {
    const auto make_document = [](std::string caption = {}, bool enabled = true, std::string tool_tip = {}) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "RadioButtonCodec";
        for (std::uint64_t id = 100; id <= 103; ++id) form.children.push_back(model::ControlRef{model::ObjectId{id}});
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{100}, "CalendarFieldDefault", model::CalendarFieldPayload{}});
        document.add_control(model::ControlNode{model::ObjectId{101}, "CalendarFieldDisabled", model::CalendarFieldPayload{}});
        document.add_control(model::ControlNode{model::ObjectId{102}, "CalendarFieldHidden", model::CalendarFieldPayload{}});
        model::ControlNode radio{model::ObjectId{103}, "RadioRuntime", model::RadioButtonPayload{}};
        if (!caption.empty()) radio.properties().set_explicit(model::PropertyId::from_name("Caption"), std::move(caption));
        if (!enabled) radio.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
        if (!tool_tip.empty()) radio.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tool_tip));
        document.add_control(std::move(radio));
        return document;
    };
    constexpr std::string_view observed_radio_control_record = R"RAW(
{782e569a-79a7-4a4f-a936-b48d013936ec,103,
{4,{"Pattern"},
{{
{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},7,{1,0},1,0,1,0,100,1},4,0,0,0,0},0,{"U"},{0}},
{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,3,4,0,0},
{14,"RadioRuntime",4294967295,0,0,0},{0}}
)RAW";
    const auto expected_observed_record = list_stream::parse(observed_radio_control_record);
    const auto default_encoded = form_stream::encode_document(make_document());
    expect(default_encoded.ok(), "default RadioButton from the observed unbound profile must encode");
    const auto& default_records = default_encoded.value().items[1].items[2].items[2].items;
    expect(default_records.size() == 5 &&
               list_stream::dump_compact(default_records[4]) == list_stream::dump_compact(expected_observed_record),
        "RadioButton writer must reproduce the full observed default control record");
    const auto default_decoded = form_stream::decode_document(default_encoded.value(), "RadioButtonCodec");
    expect(default_decoded.ok(), "full observed default RadioButton must decode");
    const auto* default_radio = default_decoded.value().find_control(model::ObjectId{103});
    expect(default_radio && default_radio->kind() == model::ControlKind::radio_button &&
               default_radio->name == "RadioRuntime" && default_radio->position.visible.value() &&
               default_radio->properties().find(model::PropertyId::from_name("Enabled")) == nullptr &&
               default_radio->properties().find(model::PropertyId::from_name("Caption")) == nullptr &&
               default_radio->properties().find(model::PropertyId::from_name("ToolTip")) == nullptr,
        "RadioButton identity, Visible, and implicit Caption/Enabled/ToolTip defaults must decode by name");
    const auto default_reencoded = form_stream::encode_document(default_decoded.value());
    expect(default_reencoded.ok() && list_stream::dump_compact(default_reencoded.value()) ==
               list_stream::dump_compact(default_encoded.value()),
        "default RadioButton document must round-trip canonically");

    const std::string caption = "Radio Ω <tag> & текст";
    const std::string tool_tip = "Radio hint Ω <tag> & текст";
    const auto changed_encoded = form_stream::encode_document(make_document(caption, false, tool_tip));
    expect(changed_encoded.ok(), "RadioButton Caption, Enabled, and ToolTip must encode");
    const auto& changed_records = changed_encoded.value().items[1].items[2].items[2].items;
    const auto& changed_info = changed_records[4].items[2];
    const auto& changed_properties = changed_info.items[2].items[0];
    expect(changed_properties.items[0].items[1].atom == "0" &&
               list_stream::dump_compact(changed_properties.items[2]) == value_codec::encode_localized_string(
                   model::LocalizedStringValue{{{"ru", caption}}}) &&
               list_stream::dump_compact(changed_properties.items[0].items[12]) == value_codec::encode_localized_string(
                   model::LocalizedStringValue{{{"ru", tool_tip}}}),
        "RadioButton properties must occupy the observed Enabled, Caption, and ToolTip slots");
    const auto changed_decoded = form_stream::decode_document(changed_encoded.value(), "RadioButtonCodec");
    expect(changed_decoded.ok(), "RadioButton Caption, Enabled, and ToolTip must decode");
    const auto* decoded_radio = changed_decoded.value().find_control(model::ObjectId{103});
    const auto* decoded_enabled = decoded_radio == nullptr ? nullptr :
        decoded_radio->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* decoded_caption = decoded_radio == nullptr ? nullptr :
        decoded_radio->properties().find(model::PropertyId::from_name("Caption"));
    const auto* decoded_tool_tip = decoded_radio == nullptr ? nullptr :
        decoded_radio->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(decoded_radio && decoded_enabled && decoded_caption && decoded_tool_tip &&
               !std::get<bool>(decoded_enabled->value) &&
               std::get<std::string>(decoded_caption->value) == caption &&
               std::get<std::string>(decoded_tool_tip->value) == tool_tip,
        "RadioButton properties must round-trip their runtime-set values");
    const auto changed_reencoded = form_stream::encode_document(changed_decoded.value());
    expect(changed_reencoded.ok() && list_stream::dump_compact(changed_reencoded.value()) ==
               list_stream::dump_compact(changed_encoded.value()),
        "changed RadioButton document must round-trip without drift");

    auto data_path_document = make_document();
    model::TypeDomainPatternValue string_type;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_type.entries.push_back(string_entry);
    data_path_document.add_attribute(model::Attribute{model::ObjectId{200}, "Pattern", string_type});
    auto* radio_with_data = const_cast<model::ControlNode*>(data_path_document.find_control(model::ObjectId{103}));
    radio_with_data->data_path = model::DataPath{model::AttributeRef{model::ObjectId{200}}, {}};
    expect_failure(form_stream::encode_document(data_path_document), "OOF1122", "$/RadioButton",
        "RadioButton DataPath must be explicitly rejected outside the proven profile");

    auto unsupported_default = default_encoded.value();
    auto& unsupported_data_header = unsupported_default.items[1].items[2].items[2].items[4].items[2].items[1];
    unsupported_data_header = list_stream::ListValue::list({list_stream::ListValue::string_atom("Unobserved")});
    expect(!form_stream::decode_document(unsupported_default, "RadioButtonCodec"),
        "unobserved RadioButton data header must fail closed");

    auto multilingual = changed_encoded.value();
    auto& multilingual_caption = multilingual.items[1].items[2].items[2].items[4]
        .items[2].items[2].items[0].items[2];
    multilingual_caption = list_stream::parse(value_codec::encode_localized_string(
        model::LocalizedStringValue{{{"ru", "Текст"}, {"en", "Text"}}}));
    expect(!form_stream::decode_document(multilingual, "RadioButtonCodec"),
        "multilingual RadioButton Caption must be rejected without loss");
}

void test_calendar_field_enabled_round_trip_and_rejections() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "CalendarForm";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{model::ObjectId{2}, "BeforeCalendar", model::ButtonPayload{}});
    model::ControlNode calendar{model::ObjectId{3}, "Calendar", model::CalendarFieldPayload{}};
    calendar.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    calendar.position.left.set(24);
    calendar.position.top.set(32);
    calendar.position.width.set(180);
    calendar.position.height.set(140);
    calendar.position.visible.set(false);
    document.add_control(std::move(calendar));

    const auto* enabled_descriptor = model::metamodel::find_property(model::ControlKind::calendar_field, "Enabled");
    expect(enabled_descriptor != nullptr &&
               enabled_descriptor->persistence == model::metamodel::PersistenceClass::persisted_editable &&
               enabled_descriptor->storage_codec == model::metamodel::StorageCodec::control_base,
        "CalendarField Enabled must route through its typed persisted descriptor");
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "mixed Button and CalendarField must encode" :
        encoded.diagnostics().front().code + ":" + encoded.diagnostics().front().path + ":" +
            encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "CalendarForm");
    expect(decoded.ok(), "mixed Button and CalendarField must decode");
    expect(decoded.value().form().children.size() == 2 &&
               std::get<model::ControlRef>(decoded.value().form().children[0]).id() == model::ObjectId{2} &&
               std::get<model::ControlRef>(decoded.value().form().children[1]).id() == model::ObjectId{3},
        "mixed owner child order and ordinals must survive CalendarField round-trip");
    const auto* decoded_calendar = decoded.value().find_control(model::ObjectId{3});
    const auto* enabled = decoded_calendar == nullptr ? nullptr : decoded_calendar->properties().find(
        model::PropertyId::from_name("Enabled"));
    expect(decoded_calendar != nullptr && decoded_calendar->kind() == model::ControlKind::calendar_field &&
               decoded_calendar->name == "Calendar" && enabled != nullptr && !std::get<bool>(enabled->value),
        "CalendarField identity and explicit Enabled=false must survive storage round-trip");
    expect(decoded_calendar->position.left.value() == 24 && decoded_calendar->position.top.value() == 32 &&
               decoded_calendar->position.width.value() == 180 && decoded_calendar->position.height.value() == 140 &&
               !decoded_calendar->position.visible.value(),
        "CalendarField Position and Visible must survive storage round-trip in the sibling owner context");

    model::Form unsupported_form;
    unsupported_form.id = model::ObjectId{1};
    unsupported_form.name = "UnsupportedCalendar";
    unsupported_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument unsupported_document(std::move(unsupported_form));
    model::ControlNode unsupported_calendar{
        model::ObjectId{2}, "Calendar", model::CalendarFieldPayload{}};
    unsupported_calendar.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::string("unproven"));
    unsupported_document.add_control(std::move(unsupported_calendar));
    expect_failure(form_stream::encode_document(unsupported_document), "OOF1122", "$/CalendarField",
        "unproven CalendarField ToolTip must fail closed");

    auto changed_date_atom = encoded.value();
    auto& calendar_record = changed_date_atom.items[1].items[2].items[2].items[2];
    calendar_record.items[2].items[1].items[5] = list_stream::ListValue::raw_atom("00010101000001");
    expect_failure(form_stream::decode_document(changed_date_atom, "CalendarForm"), "OOF1114",
        "$/1/2/2/2/2", "unmapped CalendarField date-like leaf variation must fail closed");

    auto changed_flag_atom = encoded.value();
    auto& changed_flag_properties = changed_flag_atom.items[1].items[2].items[2].items[2]
        .items[2].items[1].items[0];
    changed_flag_properties.items[13] = list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(changed_flag_atom, "CalendarForm"), "OOF1114",
        "$/1/2/2/2/2", "unmapped CalendarField flag-like leaf variation must fail closed");
}

void test_fresh_progress_bar_runtime_record_and_rejections() {
    constexpr std::string_view xml =
        R"OOF(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><ChildItems><ProgressBar id="4" name="ProgressResearch"><Position/></ProgressBar></ChildItems></Form>)OOF";
    const auto parsed = oof::source::parse_form_xml(xml);
    expect(parsed.ok(), "ProgressBar XML-only source must parse before native encoding");
    auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), "ProgressBar must encode from its named XML object model");

    constexpr std::string_view explicit_defaults_xml =
        R"OOF(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><ChildItems><ProgressBar id="4" name="ProgressResearch"><Position/><MaxValue>100</MaxValue><MinValue>0</MinValue><Step>1</Step></ProgressBar></ChildItems></Form>)OOF";
    const auto explicit_defaults = oof::source::parse_form_xml(explicit_defaults_xml);
    expect(explicit_defaults.ok(), "explicit ProgressBar numeric defaults must parse");
    if (explicit_defaults) {
        const auto defaults_xml = oof::source::serialize_form_xml(explicit_defaults.value());
        expect(defaults_xml.ok() && defaults_xml.value().find("<MaxValue>") == std::string::npos &&
                   defaults_xml.value().find("<MinValue>") == std::string::npos &&
                   defaults_xml.value().find("<Step>") == std::string::npos,
            "XML writer must omit explicit ProgressBar values equal to DecimalValue defaults");
    }

    constexpr std::string_view runtime_record = R"OOF({b1db1f86-abbb-4cf0-8852-fe6ae21650c2,4,{0,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},3,0,100,1,1,0,2}},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,1,2,0,0},{14,"ProgressResearch",4294967295,0,0,0},{0}})OOF";
    auto observed = list_stream::parse(runtime_record);
    auto* generated_record = static_cast<list_stream::ListValue*>(nullptr);
    const auto find_progress = [&](auto&& self, list_stream::ListValue& value) -> list_stream::ListValue* {
        if (!value.is_list) return nullptr;
        if (value.items.size() == 6 && !value.items[0].is_list &&
            value.items[0].atom == "b1db1f86-abbb-4cf0-8852-fe6ae21650c2") return &value;
        for (auto& item : value.items) if (auto* found = self(self, item)) return found;
        return nullptr;
    };
    generated_record = find_progress(find_progress, encoded.value());
    expect(generated_record != nullptr, "fresh ProgressBar output must contain a named child record");
    observed.items[3] = generated_record->items[3];
    *generated_record = observed;

    const auto decoded = form_stream::decode_document(encoded.value(), "Progress");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* progress = decoded.value().find_control(model::ObjectId{4});
    expect(progress && progress->kind() == model::ControlKind::progress_bar && progress->name == "ProgressResearch" &&
               progress->properties().find(model::PropertyId::from_name("Enabled")) == nullptr,
        "observed native ProgressBar record must decode to named identity and Enabled=true");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "observed native ProgressBar default record must re-encode without drift");

    constexpr std::string_view numeric_xml = R"OOF(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><ChildItems><ProgressBar id="4" name="ProgressResearch"><Position/><MaxValue>321</MaxValue><MinValue>-17</MinValue><Step>25</Step></ProgressBar></ChildItems></Form>)OOF";
    const auto numeric_parsed = oof::source::parse_form_xml(numeric_xml);
    expect(numeric_parsed.ok(), "named ProgressBar numeric properties must parse from XML");
    const auto numeric_encoded = form_stream::encode_document(numeric_parsed.value());
    expect(numeric_encoded.ok(), "named ProgressBar integer values must encode from XML");
    auto numeric_observed = observed;
    numeric_observed.items[2].items[1].items[2] = list_stream::ListValue::raw_atom("-17");
    numeric_observed.items[2].items[1].items[3] = list_stream::ListValue::raw_atom("321");
    numeric_observed.items[2].items[1].items[4] = list_stream::ListValue::raw_atom("25");
    auto numeric_stream = numeric_encoded.value();
    auto* numeric_record = find_progress(find_progress, numeric_stream);
    expect(numeric_record != nullptr, "numeric ProgressBar output must contain a named child record");
    numeric_observed.items[3] = numeric_record->items[3];
    *numeric_record = numeric_observed;
    const auto numeric_decoded = form_stream::decode_document(numeric_stream, "ProgressNumeric");
    expect(numeric_decoded.ok(), "observed numeric ProgressBar record must decode");
    const auto* numeric_control = numeric_decoded.value().find_control(model::ObjectId{4});
    const auto* decoded_max = numeric_control->properties().find(model::PropertyId::from_name("MaxValue"));
    const auto* decoded_min = numeric_control->properties().find(model::PropertyId::from_name("MinValue"));
    const auto* decoded_step = numeric_control->properties().find(model::PropertyId::from_name("Step"));
    expect(decoded_max && std::get<std::int64_t>(decoded_max->value) == 321 &&
               decoded_min && std::get<std::int64_t>(decoded_min->value) == -17 &&
               decoded_step && std::get<std::int64_t>(decoded_step->value) == 25,
        "observed MaxValue, MinValue, and Step must decode as named numeric properties");
    const auto numeric_reencoded = form_stream::encode_document(numeric_decoded.value());
    expect(numeric_reencoded.ok() &&
               list_stream::dump_compact(numeric_reencoded.value()) == list_stream::dump_compact(numeric_stream),
        "observed numeric ProgressBar properties must re-encode to their integer info slots");

    for (const std::string_view property : {"MaxValue", "MinValue", "Step"}) {
        const auto property_xml = [property](std::string_view value) {
            return std::string("<Form id=\"1\" name=\"Progress\" ordinaryFormVersion=\"2.1\"><ChildItems><ProgressBar id=\"4\" name=\"P\"><Position/><") +
                std::string(property) + ">" + std::string(value) + "</" + std::string(property) +
                "></ProgressBar></ChildItems></Form>";
        };
        const auto fractional = oof::source::parse_form_xml(property_xml("12.5"));
        expect(!fractional, "fractional ProgressBar XML must be rejected by the int32 contract");
        const std::size_t storage_slot = property == "MaxValue" ? 3 : property == "MinValue" ? 2 : 4;
        for (const std::string_view endpoint : {"2147483647", "-2147483648"}) {
            const auto endpoint_parsed = oof::source::parse_form_xml(property_xml(endpoint));
            expect(endpoint_parsed.ok(), "ProgressBar int32 endpoint must parse as a named integer");
            const auto endpoint_encoded = form_stream::encode_document(endpoint_parsed.value());
            expect(endpoint_encoded.ok(), "ProgressBar signed int32 endpoint must encode");
            auto endpoint_stream = endpoint_encoded.value();
            auto* endpoint_record = find_progress(find_progress, endpoint_stream);
            expect(endpoint_record && endpoint_record->items[2].items[1].items[storage_slot].atom == endpoint,
                "ProgressBar signed int32 endpoint must occupy its observed numeric info slot");
        }
        for (const std::string_view out_of_range : {"2147483648", "-2147483649"}) {
            const auto parsed = oof::source::parse_form_xml(property_xml(out_of_range));
            expect(!parsed, "ProgressBar XML outside signed int32 must be rejected before storage");
        }
    }

    model::Form changed_form;
    changed_form.id = model::ObjectId{1};
    changed_form.name = "Progress";
    changed_form.children = {model::ControlRef{model::ObjectId{4}}};
    model::OrdinaryFormDocument changed(std::move(changed_form));
    model::ControlNode changed_progress{model::ObjectId{4}, "ProgressResearch", model::ProgressBarPayload{}};
    changed_progress.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    changed_progress.position.visible.set(false);
    changed_progress.properties().set_explicit(model::PropertyId::from_name("ToolTip"), "Прогресс Ω & <тег>");
    changed.add_control(std::move(changed_progress));
    const auto changed_stream = form_stream::encode_document(changed);
    expect(changed_stream.ok(), "ProgressBar Enabled, Visible, and confirmed ToolTip changes must encode");
    const auto changed_decoded = form_stream::decode_document(changed_stream.value(), "Progress");
    expect(changed_decoded.ok(), "changed ProgressBar stream must decode fresh");
    const auto* changed_readback = changed_decoded.value().find_control(model::ObjectId{4});
    expect(changed_readback && !std::get<bool>(changed_readback->properties().find(
               model::PropertyId::from_name("Enabled"))->value) && !changed_readback->position.visible.value() &&
               std::get<std::string>(changed_readback->properties().find(
                   model::PropertyId::from_name("ToolTip"))->value) == "Прогресс Ω & <тег>",
        "named ProgressBar changes must survive fresh decode");

    auto unsupported_leaf = encoded.value();
    auto* unsupported_record = find_progress(find_progress, unsupported_leaf);
    unsupported_record->items[2].items[1].items[0].items[15] = list_stream::ListValue::raw_atom("1");
    const auto unsupported_decode = form_stream::decode_document(unsupported_leaf, "Progress");
    expect(!unsupported_decode && unsupported_decode.diagnostics().front().code == "OOF1114",
        unsupported_decode ? "unclassified ProgressBar leaf unexpectedly decoded" :
            "unclassified ProgressBar leaf diagnostic was " + unsupported_decode.diagnostics().front().code +
                " at " + unsupported_decode.diagnostics().front().path);

    constexpr std::string_view data_path_xml = R"OOF(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><Attributes><Attribute id="3" name="Amount"><TypeDomain><Entry term="numeric" length="10" precision="2" nonNegative="true"/></TypeDomain></Attribute></Attributes><ChildItems><ProgressBar id="4" name="P"><DataPath attributeId="3"/><Position/></ProgressBar></ChildItems></Form>)OOF";
    const auto data_path = oof::source::parse_form_xml(data_path_xml);
    expect(data_path.ok(), "ProgressBar direct numeric DataPath with named qualifiers must parse");
    const auto data_path_encoded = form_stream::encode_document(data_path.value());
    expect(data_path_encoded.ok(), data_path_encoded ? "" : data_path_encoded.diagnostics().front().message);
    const auto& progress_links = data_path_encoded.value().items[2].items[3];
    expect(progress_links.items.size() == 2 && progress_links.items[1].items[0].atom == "4" &&
               progress_links.items[1].items[1].items[1].items[0].atom == "3",
        "ProgressBar DataPath must encode as control 4 linked to local Attribute 3");
    const auto data_path_decoded = form_stream::decode_document(data_path_encoded.value(), "Progress");
    expect(data_path_decoded.ok() && data_path_decoded.value().find_control(model::ObjectId{4})->data_path &&
               data_path_decoded.value().find_control(model::ObjectId{4})->data_path->attribute.id() == model::ObjectId{3},
        "ProgressBar direct numeric DataPath must survive decode");
    const auto data_path_reencoded = form_stream::encode_document(data_path_decoded.value());
    expect(data_path_reencoded.ok() && list_stream::dump_compact(data_path_reencoded.value()) ==
               list_stream::dump_compact(data_path_encoded.value()),
        "ProgressBar direct numeric DataPath must survive encode-decode-encode");

    const auto xml_for_domain = [](std::string_view domain, std::string_view target = "3") {
        return std::string("<Form id=\"1\" name=\"Progress\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"3\" name=\"Amount\"><TypeDomain>") +
            std::string(domain) + "</TypeDomain></Attribute></Attributes><ChildItems><ProgressBar id=\"4\" name=\"P\"><DataPath attributeId=\"" +
            std::string(target) + "\"/><Position/></ProgressBar></ChildItems></Form>";
    };
    for (const auto [domain, label] : std::array<std::pair<std::string_view, std::string_view>, 4>{{
             {"<Entry term=\"string\" length=\"64\"/>", "nonNumeric"},
             {"<Entry term=\"numeric\" length=\"10\"/><Entry term=\"numeric\" length=\"8\"/>", "compound"},
             {"<Entry term=\"unknown\" typeUuid=\"01234567-89AB-CDEF-0123-456789ABCDEF\"/>", "unknown"},
             {"<Entry term=\"numeric\" length=\"10\"/><Entry term=\"string\" length=\"2\"/>", "variant"},
         }}) {
        const auto invalid = oof::source::parse_form_xml(xml_for_domain(domain));
        expect(invalid.ok() && !form_stream::encode_document(invalid.value()),
            std::string("ProgressBar must reject a ") + std::string(label) + " Attribute domain");
    }
    const auto dangling_xml = oof::source::parse_form_xml(xml_for_domain(
        "<Entry term=\"numeric\" length=\"10\" precision=\"2\"/>", "99"));
    expect(!dangling_xml || !form_stream::encode_document(dangling_xml.value()),
        "ProgressBar must reject a dangling DataPath Attribute ID");
    auto compound_xml_text = xml_for_domain(
        "<Entry term=\"numeric\" length=\"10\" precision=\"2\"/>");
    const auto empty_path_end = compound_xml_text.find("/><Position/>");
    compound_xml_text.replace(empty_path_end, std::string("/><Position/>").size(),
        "><Member>Nested</Member></DataPath><Position/>");
    const auto compound_path_xml = oof::source::parse_form_xml(compound_xml_text);
    expect(compound_path_xml.ok() && !form_stream::encode_document(compound_path_xml.value()),
        "ProgressBar must reject compound DataPath members");
    auto metadata_uuid = data_path_encoded.value();
    metadata_uuid.items[2].items[3].items[1].items[1].items[1] =
        list_stream::ListValue::list({list_stream::ListValue::raw_atom("3"),
            list_stream::ListValue::raw_atom("01234567-89AB-CDEF-0123-456789ABCDEF")});
    expect(!form_stream::decode_document(metadata_uuid, "Progress"),
        "ProgressBar must reject metadata UUID Attribute links");
    auto dangling_link = data_path_encoded.value();
    dangling_link.items[2].items[3].items[1].items[1].items[1].items[0] =
        list_stream::ListValue::raw_atom("99");
    expect(!form_stream::decode_document(dangling_link, "Progress"),
        "ProgressBar decoder must reject dangling Attribute links");
    auto non_numeric_link = data_path_encoded.value();
    model::TypeDomainPatternValue linked_string_type;
    model::TypeDomainEntry linked_string_entry;
    linked_string_entry.term = model::TypeDomainTerm::string;
    linked_string_entry.string = model::LengthQualifiers{64, false};
    linked_string_type.entries.push_back(linked_string_entry);
    non_numeric_link.items[2].items[2].items[1].items[5] =
        list_stream::parse(value_codec::encode_type_domain(linked_string_type));
    expect(!form_stream::decode_document(non_numeric_link, "Progress"),
        "ProgressBar decoder must reject a linked nonNumeric Attribute");

    model::Form overflow_form;
    overflow_form.id = model::ObjectId{1};
    overflow_form.name = "Progress";
    constexpr auto overflow_id = std::numeric_limits<std::uint64_t>::max();
    overflow_form.children = {model::ControlRef{model::ObjectId{overflow_id}}};
    model::OrdinaryFormDocument overflow(std::move(overflow_form));
    overflow.add_control(model::ControlNode{model::ObjectId{overflow_id}, "P", model::ProgressBarPayload{}});
    const auto overflow_result = form_stream::encode_document(overflow);
    expect(!overflow_result && overflow_result.diagnostics().front().code == "OOF1122",
        "ProgressBar ID above int64 must be rejected before encoding");
}

void test_progress_data_path_mixed_with_existing_links() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "MixedProgressLinks";
    form.children = {model::ControlRef{model::ObjectId{42}}, model::ControlRef{model::ObjectId{5}},
        model::ControlRef{model::ObjectId{9}}, model::ControlRef{model::ObjectId{10}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::TypeDomainEntry numeric_entry;
    numeric_entry.term = model::TypeDomainTerm::numeric;
    numeric_entry.numeric = model::NumericQualifiers{10, 2, true};
    model::TypeDomainPatternValue numeric_type;
    numeric_type.entries.push_back(numeric_entry);
    model::TypeDomainPatternValue string_type;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{64, false};
    string_type.entries.push_back(string_entry);
    model::TypeDomainPatternValue boolean_type;
    model::TypeDomainEntry boolean_entry;
    boolean_entry.term = model::TypeDomainTerm::boolean;
    boolean_type.entries.push_back(boolean_entry);
    document.add_attribute(model::Attribute{model::ObjectId{17}, "Amount", numeric_type});
    document.add_attribute(model::Attribute{model::ObjectId{6}, "Text", string_type});
    document.add_attribute(model::Attribute{model::ObjectId{7}, "Flag", boolean_type});
    model::ControlNode bound{model::ObjectId{42}, "BoundProgress", model::ProgressBarPayload{}};
    bound.data_path = model::DataPath{model::AttributeRef{model::ObjectId{17}}, {}};
    document.add_control(std::move(bound));
    document.add_control(model::ControlNode{model::ObjectId{5}, "UnboundProgress", model::ProgressBarPayload{}});
    model::ControlNode input{model::ObjectId{9}, "Input", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{6}}, {}};
    document.add_control(std::move(input));
    model::ControlNode checkbox{model::ObjectId{10}, "Check", model::CheckBoxPayload{}};
    checkbox.data_path = model::DataPath{model::AttributeRef{model::ObjectId{7}}, {}};
    document.add_control(std::move(checkbox));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().message);
    const auto& links = encoded.value().items[2].items[3];
    expect(links.items.size() == 4 && links.items[0].atom == "3" &&
               links.items[1].items[0].atom == "9" && links.items[2].items[0].atom == "10" &&
               links.items[3].items[0].atom == "42",
        "bound ProgressBar, InputField, and CheckBox must produce exactly three correctly counted links");
    const auto decoded = form_stream::decode_document(encoded.value(), "MixedProgressLinks");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    expect(decoded.value().find_control(model::ObjectId{42})->data_path.has_value() &&
               !decoded.value().find_control(model::ObjectId{5})->data_path.has_value() &&
               decoded.value().find_control(model::ObjectId{9})->data_path.has_value() &&
               decoded.value().find_control(model::ObjectId{10})->data_path.has_value(),
        "bound and unbound ProgressBars must coexist with required InputField and CheckBox links");
}

void test_calendar_field_observed_record_decode() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "ObservedCalendar";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{4}}};
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{model::ObjectId{2}, "BeforeCalendar", model::ButtonPayload{}});
    document.add_control(model::ControlNode{model::ObjectId{4}, "CalendarResearch", model::CalendarFieldPayload{}});
    auto stream = form_stream::encode_document(document);
    expect(stream.ok(), "seed stream must encode before inserting the complete observed CalendarField record");
    stream.value().items[1].items[2].items[2].items[2] = list_stream::parse(R"OOF(
{e3c063d8-ef92-41be-9c89-b70290b5368b,4,
{1,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},9,
{4,3,
{-16},3},
{4,3,
{-14},3},
{4,3,
{-15},3},00010101000000,00010101000000,1,1,0,0,0,0,1},
{0}
},
{8,0,0,0,0,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,1,2,0,0},
{14,"CalendarResearch",4294967295,0,0,0},
{0}
}
)OOF");

    const auto decoded = form_stream::decode_document(stream.value(), "ObservedCalendar");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* calendar = decoded.value().find_control(model::ObjectId{4});
    expect(calendar != nullptr && calendar->kind() == model::ControlKind::calendar_field &&
               calendar->name == "CalendarResearch",
        "observed CalendarField identity, ID, and metadata name must decode");
    expect(calendar->properties().find(model::PropertyId::from_name("Enabled")) == nullptr,
        "observed default Enabled=true must remain implicit");

    auto changed_enabled = stream.value();
    auto& enabled_slot = changed_enabled.items[1].items[2].items[2].items[2]
        .items[2].items[1].items[0].items[1];
    enabled_slot = list_stream::ListValue::raw_atom("0");
    const auto disabled = form_stream::decode_document(changed_enabled, "ObservedCalendar");
    expect(disabled.ok(), "observed CalendarField Enabled=false variation must decode");
    const auto* disabled_calendar = disabled.value().find_control(model::ObjectId{4});
    const auto* enabled = disabled_calendar == nullptr ? nullptr : disabled_calendar->properties().find(
        model::PropertyId::from_name("Enabled"));
    expect(enabled != nullptr && !std::get<bool>(enabled->value),
        "observed Enabled=false must become an explicit named property");

    auto changed_date = stream.value();
    auto& date_slot = changed_date.items[1].items[2].items[2].items[2].items[2].items[1].items[5];
    date_slot = list_stream::ListValue::raw_atom("00010101000001");
    expect_failure(form_stream::decode_document(changed_date, "ObservedCalendar"), "OOF1114",
        "$/1/2/2/2/2", "unmapped observed CalendarField date variation must fail closed");

    auto changed_events = stream.value();
    auto& events_slot = changed_events.items[1].items[2].items[2].items[2].items[2].items[2];
    events_slot = list_stream::parse("{1}");
    expect_failure(form_stream::decode_document(changed_events, "ObservedCalendar"), "OOF1114",
        "$/1/2/2/2/2", "unsupported observed CalendarField event variation must fail closed");
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
        bool explicit_read_only_false = false, bool auto_choice_incomplete = false, bool auto_mark_incomplete = false,
        const std::vector<std::pair<std::string_view, bool>>& extra_flags = {}) {
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
        if (auto_choice_incomplete) {
            input_field.properties().set_explicit(model::PropertyId::from_name("AutoChoiceIncomplete"), true);
        }
        if (auto_mark_incomplete) {
            input_field.properties().set_explicit(model::PropertyId::from_name("AutoMarkIncomplete"), true);
        }
        for (const auto& [name, value] : extra_flags) {
            input_field.properties().set_explicit(model::PropertyId::from_name(name), value);
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

    auto auto_choice_document = make_input_document(10, true, false, false, false, true);
    const auto auto_choice_encoded = form_stream::encode_document(auto_choice_document);
    expect(auto_choice_encoded.ok(), "InputField AutoChoiceIncomplete=true must encode");
    const auto& auto_choice_payload = auto_choice_encoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0];
    expect(auto_choice_payload.items[36].atom == "1", "AutoChoiceIncomplete must encode at payload slot 36");
    const auto auto_choice_decoded = form_stream::decode_document(auto_choice_encoded.value(), "Main");
    expect(auto_choice_decoded.ok(), "InputField AutoChoiceIncomplete=true must decode");
    const auto* decoded_auto_choice = auto_choice_decoded.value().find_control(model::ObjectId{9});
    const auto* auto_choice_property = decoded_auto_choice == nullptr ? nullptr :
        decoded_auto_choice->properties().find(model::PropertyId::from_name("AutoChoiceIncomplete"));
    expect(auto_choice_property != nullptr && std::get<bool>(auto_choice_property->value),
        "InputField AutoChoiceIncomplete=true must survive round-trip");
    const auto auto_choice_reencoded = form_stream::encode_document(auto_choice_decoded.value());
    expect(auto_choice_reencoded.ok(), "decoded AutoChoiceIncomplete document must re-encode");
    expect(auto_choice_reencoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[36].atom == "1",
        "AutoChoiceIncomplete=true must remain stable after re-encoding");

    auto unknown_auto_choice_flag = auto_choice_encoded.value();
    unknown_auto_choice_flag.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[36] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(unknown_auto_choice_flag, "Main"), "OOF1105",
        "$/1/2/2/3/2/2/0/36", "unknown AutoChoiceIncomplete flag value must be rejected");

    auto auto_mark_document = make_input_document(10, true, false, false, false, false, true);
    const auto auto_mark_encoded = form_stream::encode_document(auto_mark_document);
    expect(auto_mark_encoded.ok(), "InputField AutoMarkIncomplete=true must encode");
    const auto& auto_mark_payload = auto_mark_encoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0];
    expect(auto_mark_payload.items[35].atom == "1", "AutoMarkIncomplete must encode at payload slot 35");
    const auto auto_mark_decoded = form_stream::decode_document(auto_mark_encoded.value(), "Main");
    expect(auto_mark_decoded.ok(), "InputField AutoMarkIncomplete=true must decode");
    const auto* decoded_auto_mark = auto_mark_decoded.value().find_control(model::ObjectId{9});
    const auto* auto_mark_property = decoded_auto_mark == nullptr ? nullptr :
        decoded_auto_mark->properties().find(model::PropertyId::from_name("AutoMarkIncomplete"));
    expect(auto_mark_property != nullptr && std::get<bool>(auto_mark_property->value),
        "InputField AutoMarkIncomplete=true must survive round-trip");
    const auto auto_mark_reencoded = form_stream::encode_document(auto_mark_decoded.value());
    expect(auto_mark_reencoded.ok() && auto_mark_reencoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[35].atom == "1",
        "AutoMarkIncomplete=true must remain stable after re-encoding");
    auto unknown_auto_mark_flag = auto_mark_encoded.value();
    unknown_auto_mark_flag.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[35] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(unknown_auto_mark_flag, "Main"), "OOF1105",
        "$/1/2/2/3/2/2/0/35", "unknown AutoMarkIncomplete flag value must be rejected");

    constexpr std::array<std::pair<std::string_view, bool>, 14> input_field_flags{{
        {"Wrap", true}, {"ChooseType", true}, {"MarkNegatives", false}, {"ChoiceButton", false},
        {"OpenButton", false}, {"ClearButton", false}, {"SpinButton", false},
        {"ChoiceListButton", false}, {"Transparent", false},
        {"MultiLine", false}, {"ExtendedEdit", false}, {"PasswordMode", false},
        {"AutoMarkIncomplete", false}, {"AutoChoiceIncomplete", false},
    }};
    const auto default_flags_encoded = form_stream::encode_document(make_input_document(10, true));
    expect(default_flags_encoded.ok(), "InputField Boolean defaults must encode");
    const auto default_flags_decoded = form_stream::decode_document(default_flags_encoded.value(), "Main");
    expect(default_flags_decoded.ok(), "InputField Boolean defaults must decode");
    for (const auto& [name, default_value] : input_field_flags) {
        const auto* default_control = default_flags_decoded.value().find_control(model::ObjectId{9});
        expect(default_control->properties().find(model::PropertyId::from_name(name)) == nullptr,
            "InputField Boolean default must decode as implicit");
        auto toggled_document = make_input_document(10, true, false, false, false, false, false, {{name, !default_value}});
        const auto toggled_encoded = form_stream::encode_document(toggled_document);
        expect(toggled_encoded.ok(), "individual InputField Boolean toggle must encode");
        const auto toggled_decoded = form_stream::decode_document(toggled_encoded.value(), "Main");
        expect(toggled_decoded.ok(), "individual InputField Boolean toggle must decode");
        const auto* decoded_control = toggled_decoded.value().find_control(model::ObjectId{9});
        const auto* property = decoded_control->properties().find(model::PropertyId::from_name(name));
        expect(property != nullptr && std::get<bool>(property->value) == !default_value,
            "individual InputField Boolean toggle must survive round-trip");
        const auto toggled_reencoded = form_stream::encode_document(toggled_decoded.value());
        expect(toggled_reencoded.ok() &&
                list_stream::dump_compact(toggled_reencoded.value().items[1].items[2].items[2].items[3]) ==
                    list_stream::dump_compact(toggled_encoded.value().items[1].items[2].items[2].items[3]),
            "individual InputField Boolean toggle must preserve its exact control stream");
    }

    auto mismatched_paired_flag = default_flags_encoded.value();
    mismatched_paired_flag.items[1].items[2].items[2].items[3].items[2].items[2].items[0].items[26] =
        list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(mismatched_paired_flag, "Main"), "OOF1122",
        "$/1/2/2/3/2/2/0/26", "mismatched paired InputField MultiLine flags must be rejected");

    auto malformed_paired_flag = default_flags_encoded.value();
    malformed_paired_flag.items[1].items[2].items[2].items[3].items[2].items[3] = list_stream::ListValue::raw_atom("0");
    expect_failure(form_stream::decode_document(malformed_paired_flag, "Main"), "OOF1101",
        "$/1/2/2/3/2/3", "malformed paired InputField flag structure must be rejected");

    std::vector<std::pair<std::string_view, bool>> mixed_flag_values;
    for (const auto& [name, default_value] : input_field_flags) {
        mixed_flag_values.emplace_back(name, !default_value);
    }
    auto mixed_flags_document = make_input_document(10, true, false, false, false, false, false, mixed_flag_values);
    const auto mixed_flags_encoded = form_stream::encode_document(mixed_flags_document);
    expect(mixed_flags_encoded.ok(), "mixed persisted InputField Boolean flags must encode");
    const auto mixed_flags_decoded = form_stream::decode_document(mixed_flags_encoded.value(), "Main");
    expect(mixed_flags_decoded.ok(), "mixed persisted InputField Boolean flags must decode");
    const auto mixed_flags_reencoded = form_stream::encode_document(mixed_flags_decoded.value());
    expect(mixed_flags_reencoded.ok() &&
            list_stream::dump_compact(mixed_flags_reencoded.value().items[1].items[2].items[2].items[3]) ==
                list_stream::dump_compact(mixed_flags_encoded.value().items[1].items[2].items[2].items[3]),
        "mixed persisted InputField Boolean flags must preserve the exact control stream");

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

    auto malformed_text_edit = encoded.value();
    auto& payload = malformed_text_edit.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0];
    payload.items[12] = list_stream::ListValue::raw_atom("9");
    expect_failure(form_stream::decode_document(malformed_text_edit, "Main"), "OOF1114", "$/1/2/2/3/2",
        "unclassified TextEdit slot value must be rejected");
    auto unsupported_text_edit_flag = encoded.value();
    unsupported_text_edit_flag.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[12] = list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(unsupported_text_edit_flag, "Main"), "OOF1114",
        "$/1/2/2/3/2", "non-default TextEdit slot value must be rejected");
    expect_failure(form_stream::encode_document(make_input_document(
        10, true, false, false, false, false, false, {{"TextEdit", false}})),
        "OOF1122", "$/InputField", "unsupported explicit TextEdit=false must fail encoding");
}

void test_input_field_tooltip_and_format_round_trip() {
    struct TextProperties {
        std::optional<std::string> tool_tip;
        std::optional<std::string> format;
        bool with_flags = false;
    };
    const auto make_document = [](const TextProperties& text) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "InputStrings";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry type_entry;
        type_entry.term = model::TypeDomainTerm::string;
        type_entry.string = model::LengthQualifiers{64, false};
        type.entries.push_back(type_entry);
        document.add_attribute(model::Attribute{model::ObjectId{1}, "Value", type});
        model::ControlNode input{model::ObjectId{2}, "Input", model::InputFieldPayload{}};
        input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        if (text.tool_tip.has_value()) {
            input.properties().set_explicit(model::PropertyId::from_name("ToolTip"), *text.tool_tip);
        }
        if (text.format.has_value()) {
            input.properties().set_explicit(model::PropertyId::from_name("Format"), *text.format);
        }
        if (text.with_flags) {
            input.properties().set_explicit(model::PropertyId::from_name("AutoMarkIncomplete"), true);
            input.properties().set_explicit(model::PropertyId::from_name("MultiLine"), true);
            input.properties().set_explicit(model::PropertyId::from_name("PasswordMode"), true);
        }
        document.add_control(std::move(input));
        return document;
    };
    const auto input_record = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };
    const auto expect_round_trip = [&](const TextProperties& values, std::string_view case_name) {
        const auto encoded = form_stream::encode_document(make_document(values));
        expect(encoded.ok(), "InputField localized string case must encode");
        const auto decoded = form_stream::decode_document(encoded.value(), "InputStrings");
        expect(decoded.ok(), "InputField localized string case must decode");
        const auto* input = decoded.value().find_control(model::ObjectId{2});
        expect(input != nullptr, "InputField localized string case must resolve");
        const auto* tool_tip = input->properties().find(model::PropertyId::from_name("ToolTip"));
        const auto* format = input->properties().find(model::PropertyId::from_name("Format"));
        if (values.tool_tip.has_value() && !values.tool_tip->empty()) {
            expect(tool_tip && std::get<std::string>(tool_tip->value) == *values.tool_tip,
                "InputField ToolTip must round-trip Unicode and XML-sensitive text");
        } else {
            expect(tool_tip == nullptr, "empty InputField ToolTip must normalize to its default");
        }
        if (values.format.has_value() && !values.format->empty()) {
            expect(format && std::get<std::string>(format->value) == *values.format,
                "InputField Format must round-trip Unicode and punctuation");
        } else {
            expect(format == nullptr, "empty InputField Format must normalize to its default");
        }
        if (values.with_flags) {
            expect(input->properties().find(model::PropertyId::from_name("AutoMarkIncomplete")) != nullptr &&
                    input->properties().find(model::PropertyId::from_name("MultiLine")) != nullptr &&
                    input->properties().find(model::PropertyId::from_name("PasswordMode")) != nullptr,
                "InputField localized strings must coexist with independent and paired flags");
        }
        const auto reencoded = form_stream::encode_document(decoded.value());
        expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
                list_stream::dump_compact(encoded.value()),
            case_name);
        return encoded.value();
    };

    const auto empty = expect_round_trip(TextProperties{std::string{}, std::string{}, false},
        "empty InputField ToolTip and Format must retain canonical storage");
    expect(input_record(empty).items[2].items[2].items[0].items[34].is_list &&
            input_record(empty).items[2].items[2].items[0].items[0].items[12].is_list,
        "empty InputField localized properties must use typed empty localization records");

    const std::string tool_tip_text = "Подсказка Ω <важно> & \"цитата\"";
    const std::string format_text = "Л=ru_RU; NFD=2; ЧРГ='Ω & <>'";
    const auto tool_tip_only = expect_round_trip(TextProperties{tool_tip_text, std::nullopt, false},
        "InputField ToolTip-only storage must be stable");
    expect(list_stream::dump_compact(input_record(tool_tip_only).items[2].items[2].items[0].items[0].items[12]) ==
            value_codec::encode_localized_string(model::LocalizedStringValue{{{"ru", tool_tip_text}}}),
        "InputField ToolTip must occupy its proven localized base-info slot");
    const auto format_only = expect_round_trip(TextProperties{std::nullopt, format_text, false},
        "InputField Format-only storage must be stable");
    expect(list_stream::dump_compact(input_record(format_only).items[2].items[2].items[0].items[34]) ==
            value_codec::encode_localized_string(model::LocalizedStringValue{{{"ru", format_text}}}),
        "InputField Format must occupy its proven localized control-info slot");
    const auto combined = expect_round_trip(TextProperties{tool_tip_text, format_text, true},
        "both InputField strings and Boolean flags must preserve exact storage");

    const auto set_localized_record = [](list_stream::ListValue& encoded, bool tool_tip, list_stream::ListValue value) {
        auto& payload = encoded.items[1].items[2].items[2].items[1].items[2].items[2].items[0];
        if (tool_tip) payload.items[0].items[12] = std::move(value);
        else payload.items[34] = std::move(value);
    };
    for (const bool tool_tip : {true, false}) {
        const auto property_name = tool_tip ? "ToolTip" : "Format";
        const auto property_path = tool_tip ? "$/1/2/2/1/2/2/0/0/12" : "$/1/2/2/1/2/2/0/34";
        auto malformed = combined;
        set_localized_record(malformed, tool_tip, list_stream::ListValue::raw_atom("malformed"));
        expect_failure(form_stream::decode_document(malformed, "InputStrings"), "OOF1108", property_path,
            std::string("malformed InputField ") + property_name + " localization must be rejected");
        auto multilingual = combined;
        set_localized_record(multilingual, tool_tip, list_stream::parse(value_codec::encode_localized_string(
            model::LocalizedStringValue{{{"ru", "Текст"}, {"en", "Text"}}})));
        expect_failure(form_stream::decode_document(multilingual, "InputStrings"), "OOF1115", property_path,
            std::string("multilingual InputField ") + property_name + " must be rejected without loss");
    }
}

void test_input_field_alignment_and_choice_list_height_round_trip() {
    struct LayoutProperties {
        std::optional<std::string> horizontal;
        std::optional<std::string> vertical;
        std::optional<std::int64_t> height;
        bool include_existing_properties = false;
        std::optional<model::DecimalValue> decimal_height;
    };
    const auto make_document = [](const LayoutProperties& values) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "InputLayout";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::string;
        entry.string = model::LengthQualifiers{64, false};
        type.entries.push_back(entry);
        document.add_attribute(model::Attribute{model::ObjectId{1}, "Value", type});
        model::ControlNode input{model::ObjectId{2}, "Entry", model::InputFieldPayload{}};
        input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        if (values.horizontal) {
            input.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
                model::EnumerationValue{"HorizontalAlign", *values.horizontal});
        }
        if (values.vertical) {
            input.properties().set_explicit(model::PropertyId::from_name("VerticalAlign"),
                model::EnumerationValue{"VerticalAlign", *values.vertical});
        }
        if (values.height) {
            input.properties().set_explicit(model::PropertyId::from_name("ChoiceListHeight"), *values.height);
        }
        if (values.decimal_height) {
            input.properties().set_explicit(model::PropertyId::from_name("ChoiceListHeight"), *values.decimal_height);
        }
        if (values.include_existing_properties) {
            input.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
            input.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
            input.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::string("Hint"));
            input.properties().set_explicit(model::PropertyId::from_name("Format"), std::string("N=2"));
            const std::array<std::pair<std::string_view, bool>, 14> flags{{
                {"AutoChoiceIncomplete", true}, {"Wrap", false}, {"ChooseType", false},
                {"MarkNegatives", true}, {"ChoiceButton", true}, {"OpenButton", true},
                {"ClearButton", true}, {"SpinButton", true}, {"ChoiceListButton", true},
                {"Transparent", true}, {"MultiLine", true}, {"ExtendedEdit", true},
                {"PasswordMode", true}, {"AutoMarkIncomplete", true},
            }};
            for (const auto& [name, value] : flags) {
                input.properties().set_explicit(model::PropertyId::from_name(name), value);
            }
        }
        document.add_control(std::move(input));
        return document;
    };
    const auto input_record = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };
    const auto payload = [&](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return input_record(encoded).items[2].items[2].items[0];
    };
    const auto round_trip = [&](const LayoutProperties& values, std::int32_t horizontal,
                                std::int32_t vertical, std::int32_t height) {
        const auto encoded = form_stream::encode_document(make_document(values));
        expect(encoded.ok(), "InputField layout values must encode");
        const auto& encoded_payload = payload(encoded.value());
        expect(encoded_payload.items[17].atom == std::to_string(horizontal) &&
                encoded_payload.items[18].atom == std::to_string(vertical) &&
                encoded_payload.items[31].atom == std::to_string(height),
            "InputField alignment and height must occupy their proven control-info slots");
        const auto decoded = form_stream::decode_document(encoded.value(), "InputLayout");
        expect(decoded.ok(), "InputField layout values must decode");
        const auto* input = decoded.value().find_control(model::ObjectId{2});
        expect(input != nullptr, "InputField layout control must resolve");
        const auto* decoded_horizontal = input->properties().find(model::PropertyId::from_name("HorizontalAlign"));
        const auto* decoded_vertical = input->properties().find(model::PropertyId::from_name("VerticalAlign"));
        const auto* decoded_height = input->properties().find(model::PropertyId::from_name("ChoiceListHeight"));
        constexpr std::array<std::string_view, 5> horizontal_members{"Left", "Center", "Right", "Justify", "Auto"};
        constexpr std::array<std::string_view, 3> vertical_members{"Top", "Center", "Bottom"};
        expect((horizontal == 4 && decoded_horizontal == nullptr) ||
                (decoded_horizontal && std::get<model::EnumerationValue>(decoded_horizontal->value) ==
                    model::EnumerationValue{"HorizontalAlign", std::string(horizontal_members[horizontal])}),
            "InputField HorizontalAlign must round-trip and omit Auto default");
        expect((vertical == 0 && decoded_vertical == nullptr) ||
                (decoded_vertical && std::get<model::EnumerationValue>(decoded_vertical->value) ==
                    model::EnumerationValue{"VerticalAlign", std::string(vertical_members[vertical])}),
            "InputField VerticalAlign must round-trip and omit Top default");
        expect((height == 0 && decoded_height == nullptr) ||
                (decoded_height && std::get<std::int64_t>(decoded_height->value) == height),
            "InputField ChoiceListHeight must round-trip and omit zero default");
        if (values.include_existing_properties) {
            std::size_t explicit_count = 0;
            input->properties().for_each_explicit([&](const model::PropertyEntry&) { ++explicit_count; });
            expect(explicit_count == 21,
                "new InputField layout properties must coexist with all 18 prior properties");
        }
        const auto reencoded = form_stream::encode_document(decoded.value());
        expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
                list_stream::dump_compact(encoded.value()),
            "InputField layout properties must preserve exact storage on reencode");
        return encoded.value();
    };

    constexpr std::array<std::pair<std::string_view, std::int32_t>, 5> horizontal_values{{
        {"Left", 0}, {"Center", 1}, {"Right", 2}, {"Justify", 3}, {"Auto", 4},
    }};
    for (const auto& [member, slot] : horizontal_values) {
        round_trip(LayoutProperties{std::string(member), std::nullopt, std::nullopt, false, std::nullopt}, slot, 0, 0);
    }
    constexpr std::array<std::pair<std::string_view, std::int32_t>, 3> vertical_values{{
        {"Top", 0}, {"Center", 1}, {"Bottom", 2},
    }};
    for (const auto& [member, slot] : vertical_values) {
        round_trip(LayoutProperties{std::nullopt, std::string(member), std::nullopt, false, std::nullopt}, 4, slot, 0);
    }
    for (const std::int32_t value : {0, 7, -7, std::numeric_limits<std::int32_t>::min(),
             std::numeric_limits<std::int32_t>::max()}) {
        round_trip(LayoutProperties{std::nullopt, std::nullopt, value, false, std::nullopt}, 4, 0, value);
    }
    round_trip(LayoutProperties{"Right", "Bottom", 7, true, std::nullopt}, 2, 2, 7);

    const auto defaults = form_stream::encode_document(make_document({}));
    expect(defaults.ok(), "default InputField layout must encode");
    auto invalid_horizontal = defaults.value();
    invalid_horizontal.items[1].items[2].items[2].items[1].items[2].items[2].items[0].items[17] =
        list_stream::ListValue::raw_atom("5");
    expect_failure(form_stream::decode_document(invalid_horizontal, "InputLayout"), "OOF1114",
        "$/1/2/2/1/2/2/0/17", "unknown InputField HorizontalAlign storage values must fail closed");
    auto invalid_vertical = defaults.value();
    invalid_vertical.items[1].items[2].items[2].items[1].items[2].items[2].items[0].items[18] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(invalid_vertical, "InputLayout"), "OOF1114",
        "$/1/2/2/1/2/2/0/18", "unknown InputField VerticalAlign storage values must fail closed");
    auto out_of_range_storage_height = defaults.value();
    out_of_range_storage_height.items[1].items[2].items[2].items[1].items[2].items[2].items[0].items[31] =
        list_stream::ListValue::raw_atom("2147483648");
    expect_failure(form_stream::decode_document(out_of_range_storage_height, "InputLayout"), "OOF1105",
        "$/1/2/2/1/2/2/0/31", "out-of-int32 InputField ChoiceListHeight storage must fail closed");

    const auto expect_invalid_property = [&](std::optional<model::EnumerationValue> horizontal,
                                             std::optional<model::EnumerationValue> vertical,
                                             std::string_view property_path) {
        auto form = model::Form{};
        form.id = model::ObjectId{1};
        form.name = "InvalidInputLayout";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::string;
        entry.string = model::LengthQualifiers{64, false};
        type.entries.push_back(entry);
        document.add_attribute(model::Attribute{model::ObjectId{1}, "Value", type});
        model::ControlNode input{model::ObjectId{2}, "Entry", model::InputFieldPayload{}};
        input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        if (horizontal) input.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"), *horizontal);
        if (vertical) input.properties().set_explicit(model::PropertyId::from_name("VerticalAlign"), *vertical);
        document.add_control(std::move(input));
        expect_failure(form_stream::encode_document(document), "OOF1122", property_path,
            "InputField must reject invalid enum type or member");
    };
    expect_invalid_property(model::EnumerationValue{"VerticalAlign", "Top"}, std::nullopt,
        "$/InputField/HorizontalAlign");
    expect_invalid_property(model::EnumerationValue{"HorizontalAlign", "Middle"}, std::nullopt,
        "$/InputField/HorizontalAlign");
    expect_invalid_property(std::nullopt, model::EnumerationValue{"HorizontalAlign", "Center"},
        "$/InputField/VerticalAlign");
    expect_invalid_property(std::nullopt, model::EnumerationValue{"VerticalAlign", "Middle"},
        "$/InputField/VerticalAlign");

    auto fractional_document = make_document({std::nullopt, std::nullopt, std::nullopt, false, model::DecimalValue{"7.5"}});
    expect_failure(form_stream::encode_document(fractional_document), "OOF1123", "$",
        "fractional ChoiceListHeight must be rejected as invalid for the integer32 model property");
    auto overflow_document = make_document({std::nullopt, std::nullopt, std::nullopt, false, model::DecimalValue{"2147483648"}});
    expect_failure(form_stream::encode_document(overflow_document), "OOF1123", "$",
        "ChoiceListHeight outside int32 must be rejected by model validation");
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

void test_root_pages_round_trip_with_page_local_control_order() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Paged";
    form.children = {model::PageRef{model::ObjectId{30}}, model::PageRef{model::ObjectId{31}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::Page first;
    const auto page_position = [](std::int32_t left, std::int32_t top, std::int32_t width,
                                  std::int32_t height, std::int32_t right_margin,
                                  std::int32_t bottom_margin) {
        model::Position position;
        position.left.set(left);
        position.top.set(top);
        position.width.set(width);
        position.height.set(height);
        for (const auto edge : {model::BindingCoordinate::right, model::BindingCoordinate::bottom}) {
            model::AnchorBinding binding;
            binding.coordinate = edge;
            binding.target_coordinate = edge;
            binding.offset.set(edge == model::BindingCoordinate::right ? -right_margin : -bottom_margin);
            position.bindings.anchors.push_back(std::move(binding));
        }
        return position;
    };
    first.id = model::ObjectId{30};
    first.name = "Overview";
    first.title.set(model::LocalizedStringValue{{{"ru", "Обзор"}}});
    first.visible.set(false);
    first.position.set(page_position(0, 0, 400, 300, 12, 14));
    first.children = {model::ControlRef{model::ObjectId{20}}, model::ControlRef{model::ObjectId{9}}};
    model::Page second;
    second.id = model::ObjectId{31};
    second.name = "Details";
    second.title.set(model::LocalizedStringValue{{{"en", "Details"}, {"ru", "Подробности"}}});
    second.position.set(page_position(4, 5, 350, 260, 8, 9));
    second.children = {model::ControlRef{model::ObjectId{4}}, model::ControlRef{model::ObjectId{5}}};
    document.add_page(first);
    document.add_page(second);

    model::ControlNode input{model::ObjectId{4}, "Value", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{2}}, {}};
    input.position.left.set(24);
    model::AnchorBinding input_binding;
    input_binding.coordinate = model::BindingCoordinate::left;
    input_binding.target_coordinate = model::BindingCoordinate::right;
    input_binding.target = model::ControlRef{model::ObjectId{20}};
    input_binding.offset.set(3);
    input.position.bindings.anchors.push_back(input_binding);
    document.add_control(std::move(input));
    model::ControlNode detail_button{model::ObjectId{5}, "Apply", model::ButtonPayload{}};
    detail_button.events.push_back(model::EventRef{model::ObjectId{40}});
    document.add_event(model::Event{model::ObjectId{40}, "Click", "ApplyHandler", model::ControlRef{model::ObjectId{5}}});
    document.add_control(std::move(detail_button));
    model::ControlNode label{model::ObjectId{9}, "Summary", model::LabelDecorationPayload{}};
    document.add_control(std::move(label));
    model::ControlNode overview_button{model::ObjectId{20}, "Open", model::ButtonPayload{}};
    model::AnchorBinding form_binding;
    form_binding.coordinate = model::BindingCoordinate::right;
    form_binding.target_coordinate = model::BindingCoordinate::right;
    form_binding.offset.set(5);
    overview_button.position.bindings.anchors.push_back(form_binding);
    overview_button.events.push_back(model::EventRef{model::ObjectId{41}});
    document.add_event(model::Event{model::ObjectId{41}, "Click", "OpenHandler", model::ControlRef{model::ObjectId{20}}});
    document.add_control(std::move(overview_button));
    model::TypeDomainPatternValue string_type;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{32, false};
    string_type.entries.push_back(string_entry);
    document.add_attribute(model::Attribute{model::ObjectId{2}, "Value", string_type});

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "named root Pages must encode" : encoded.diagnostics().front().message);
    auto payload = encoded.value().items[1].items[2].items[1].items[1];
    std::size_t incoming_end = 2;
    for (std::size_t edge = 0; edge < 6; ++edge) {
        const auto count = static_cast<std::size_t>(std::stoul(payload.items[incoming_end].atom));
        incoming_end += 1 + count;
    }
    payload.items.erase(payload.items.begin() + 2,
        payload.items.begin() + static_cast<std::ptrdiff_t>(incoming_end));
    expect(payload.items[3].atom == "1" && payload.items[9].atom == "8",
        "multi-page root payload must select its confirmed page-table and boundary-count variant");
    const auto& physical = encoded.value().items[1].items[2].items[2].items;
    expect(physical[1].items[1].atom == "4" && physical[2].items[1].atom == "5" &&
           physical[3].items[1].atom == "9" && physical[4].items[1].atom == "20",
        "records inside root Pages must remain physically sorted by control ID");
    const auto first_tail = geometry_tail_start(physical[1].items[3]);
    const auto second_tail = geometry_tail_start(physical[2].items[3]);
    const auto third_tail = geometry_tail_start(physical[3].items[3]);
    const auto fourth_tail = geometry_tail_start(physical[4].items[3]);
    expect(physical[1].items[3].items[first_tail].atom == "1" && physical[1].items[3].items[first_tail + 1].atom == "0" &&
           physical[2].items[3].items[second_tail].atom == "1" && physical[2].items[3].items[second_tail + 1].atom == "1" &&
           physical[3].items[3].items[third_tail].atom == "0" && physical[3].items[3].items[third_tail + 1].atom == "1" &&
           physical[4].items[3].items[fourth_tail].atom == "0" && physical[4].items[3].items[fourth_tail + 1].atom == "0",
        "geometry page indexes and ordinals must follow each Page ChildItems order");

    const auto decoded = form_stream::decode_document(encoded.value(), "Paged");
    expect(decoded.ok(), decoded ? "multi-page root stream must decode" : decoded.diagnostics().front().message);
    expect(decoded.value().collections().pages.size() == 2 && decoded.value().form().children.size() == 2,
        "decoded root Page table and Form order must remain named");
    const auto* decoded_first = decoded.value().find_page(model::ObjectId{21});
    const auto* decoded_second = decoded.value().find_page(model::ObjectId{22});
    expect(decoded_first && decoded_second && decoded_first->name == "Overview" && decoded_second->name == "Details",
        "decoded Page metadata must follow table order and receive fresh nonconflicting IDs");
    expect(!decoded_first->visible.value() && decoded_second->enabled.value() &&
           decoded_second->title.value() == second.title.value(),
        "Page visibility, enabled state, and multilingual title must survive");
    expect(decoded_first->position.value().left.value() == 0 && decoded_first->position.value().width.value() == 400 &&
           decoded_second->position.value().left.value() == 4 && decoded_second->position.value().width.value() == 350,
        "each Page Position must survive independently");
    expect(decoded_first->children.size() == 2 &&
           std::get<model::ControlRef>(decoded_first->children[0]).id() == model::ObjectId{20} &&
           std::get<model::ControlRef>(decoded_first->children[1]).id() == model::ObjectId{9} &&
           std::get<model::ControlRef>(decoded_second->children[0]).id() == model::ObjectId{4} &&
           std::get<model::ControlRef>(decoded_second->children[1]).id() == model::ObjectId{5},
        "page-local logical control order must survive physical ID sorting");
    expect(decoded.value().find_control(model::ObjectId{4})->data_path->attribute.id() == model::ObjectId{2} &&
           decoded.value().find_event(decoded.value().find_control(model::ObjectId{5})->events.front().id())->handler == "ApplyHandler" &&
           decoded.value().find_event(decoded.value().find_control(model::ObjectId{20})->events.front().id())->handler == "OpenHandler",
        "DataPath and Button event links must remain attached across root Pages");
    const auto& input_anchors = decoded.value().find_control(model::ObjectId{4})->position.bindings.anchors;
    const auto& form_anchors = decoded.value().find_control(model::ObjectId{20})->position.bindings.anchors;
    expect(input_anchors.size() == 1 && input_anchors.front().target == model::ControlRef{model::ObjectId{20}} &&
           form_anchors.size() == 1 && !form_anchors.front().target,
        "incoming geometry must resolve both cross-page control targets and Form target zero");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "multi-page root document must encode-decode-encode without storage drift");

    auto bad_page = encoded.value();
    auto& bad_page_geometry = bad_page.items[1].items[2].items[2].items[1].items[3];
    bad_page_geometry.items[geometry_tail_start(bad_page_geometry)] = list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(bad_page, "Paged"), "out-of-range root Page indexes must fail");
    auto bad_ordinal = encoded.value();
    auto& bad_ordinal_geometry = bad_ordinal.items[1].items[2].items[2].items[1].items[3];
    const auto tail = geometry_tail_start(bad_ordinal_geometry);
    bad_ordinal_geometry.items[tail + 1] = list_stream::ListValue::raw_atom("2");
    bad_ordinal_geometry.items[tail + 2] = list_stream::ListValue::raw_atom("3");
    expect(!form_stream::decode_document(bad_ordinal, "Paged"), "out-of-range page-local ordinals must fail");

    model::Form single_form;
    single_form.id = model::ObjectId{1};
    single_form.name = "SingleNamedPage";
    single_form.children = {model::PageRef{model::ObjectId{30}}};
    model::OrdinaryFormDocument single(std::move(single_form));
    model::Page customized;
    customized.id = model::ObjectId{30};
    customized.name = "Custom";
    customized.title.set(model::LocalizedStringValue{{{"ru", "Своя страница"}}});
    customized.enabled.set(false);
    customized.position.set(page_position(7, 9, 320, 210, 6, 8));
    customized.children = {model::ControlRef{model::ObjectId{8}}};
    single.add_page(customized);
    single.add_control(model::ControlNode{model::ObjectId{8}, "Only", model::ButtonPayload{}});
    const auto single_encoded = form_stream::encode_document(single);
    expect(single_encoded.ok(), "custom single root Page must encode");
    const auto single_decoded = form_stream::decode_document(single_encoded.value(), "SingleNamedPage");
    expect(single_decoded.ok() && single_decoded.value().collections().pages.size() == 1,
        "custom single root Page must remain explicitly represented");
    const auto* retained = single_decoded.value().find_page(model::ObjectId{9});
    expect(retained && retained->name == "Custom" && !retained->enabled.value() &&
           retained->title.value() == customized.title.value() && retained->position.value().left.value() == 7,
        "single Page metadata and changed geometry must not collapse into the implicit default");
}

void test_recursive_panel_pages_keep_owner_geometry_separate() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "NestedPanels";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{30}}};
    model::OrdinaryFormDocument document(std::move(form));
    const auto make_page_position = [](std::int32_t left, std::int32_t top, std::int32_t width,
                                       std::int32_t height, model::ControlRef owner) {
        model::Position position;
        position.left.set(left);
        position.top.set(top);
        position.width.set(width);
        position.height.set(height);
        for (const auto edge : {model::BindingCoordinate::right, model::BindingCoordinate::bottom}) {
            model::AnchorBinding binding;
            binding.coordinate = edge;
            binding.target_coordinate = edge;
            binding.target = owner;
            position.bindings.anchors.push_back(std::move(binding));
        }
        return position;
    };
    const auto add_anchor = [](model::ControlNode& control, model::BindingCoordinate source,
                               model::BindingCoordinate target_edge,
                               std::optional<model::ControlRef> target, std::int32_t offset) {
        model::AnchorBinding binding;
        binding.coordinate = source;
        binding.target_coordinate = target_edge;
        binding.target = target;
        binding.offset.set(offset);
        control.position.bindings.anchors.push_back(std::move(binding));
    };

    model::ControlNode outer_panel{model::ObjectId{2}, "Outer", model::PanelPayload{}};
    outer_panel.children = {model::PageRef{model::ObjectId{40}}};
    model::Page outer_page;
    outer_page.id = model::ObjectId{40};
    outer_page.name = "OuterPage";
    outer_page.title.set(model::LocalizedStringValue{{{"en", "Outer page"}}});
    outer_page.position.set(make_page_position(0, 0, 500, 400, model::ControlRef{model::ObjectId{2}}));
    outer_page.children = {model::ControlRef{model::ObjectId{3}}, model::ControlRef{model::ObjectId{6}}};
    document.add_page(outer_page);

    model::ControlNode inner_panel{model::ObjectId{3}, "Inner", model::PanelPayload{}};
    inner_panel.children = {model::PageRef{model::ObjectId{41}}};
    model::Page inner_page;
    inner_page.id = model::ObjectId{41};
    inner_page.name = "InnerPage";
    inner_page.title.set(model::LocalizedStringValue{{{"ru", "Внутренняя"}}});
    inner_page.position.set(make_page_position(5, 7, 300, 200, model::ControlRef{model::ObjectId{3}}));
    inner_page.children = {model::ControlRef{model::ObjectId{4}}, model::ControlRef{model::ObjectId{5}}};
    document.add_page(inner_page);

    model::ControlNode input{model::ObjectId{4}, "Value", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{7}}, {}};
    add_anchor(input, model::BindingCoordinate::right, model::BindingCoordinate::right,
        model::ControlRef{model::ObjectId{3}}, 2);
    document.add_control(std::move(input));
    model::ControlNode inner_button{model::ObjectId{5}, "InnerRun", model::ButtonPayload{}};
    add_anchor(inner_button, model::BindingCoordinate::left, model::BindingCoordinate::right,
        model::ControlRef{model::ObjectId{4}}, 3);
    inner_button.events.push_back(model::EventRef{model::ObjectId{60}});
    document.add_event(model::Event{model::ObjectId{60}, "Click", "InnerHandler", model::ControlRef{model::ObjectId{5}}});
    document.add_control(std::move(inner_button));
    model::ControlNode outer_button{model::ObjectId{6}, "OuterRun", model::ButtonPayload{}};
    add_anchor(outer_button, model::BindingCoordinate::left, model::BindingCoordinate::right,
        model::ControlRef{model::ObjectId{3}}, 4);
    add_anchor(outer_button, model::BindingCoordinate::top, model::BindingCoordinate::top,
        model::ControlRef{model::ObjectId{2}}, 1);
    document.add_control(std::move(outer_button));
    model::ControlNode root_button{model::ObjectId{30}, "RootRun", model::ButtonPayload{}};
    add_anchor(root_button, model::BindingCoordinate::left, model::BindingCoordinate::right,
        model::ControlRef{model::ObjectId{2}}, 5);
    add_anchor(root_button, model::BindingCoordinate::top, model::BindingCoordinate::top, std::nullopt, 6);
    document.add_control(std::move(outer_panel));
    document.add_control(std::move(inner_panel));
    document.add_control(std::move(root_button));
    model::TypeDomainPatternValue string_type;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{32, false};
    string_type.entries.push_back(string_entry);
    document.add_attribute(model::Attribute{model::ObjectId{7}, "Value", string_type});

    const auto preflight = document.validate();
    expect(preflight.ok(), "recursive Panel fixture must satisfy model invariants");
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "recursive Panel graph must encode" : encoded.diagnostics().front().message);
    auto find_record = [](auto&& self, list_stream::ListValue& table, std::uint64_t id) -> list_stream::ListValue* {
        if (!table.is_list || table.items.empty()) return nullptr;
        for (std::size_t index = 1; index < table.items.size(); ++index) {
            auto& record = table.items[index];
            if (!record.is_list || record.items.size() < 2 || record.items[1].is_list) continue;
            if (std::stoull(record.items[1].atom) == id) return &record;
            if (record.items.size() == 6 && record.items[5].is_list) {
                if (auto* found = self(self, record.items[5], id)) return found;
            }
        }
        return nullptr;
    };
    auto storage = encoded.value();
    auto& root_children = storage.items[1].items[2].items[2];
    auto* outer_record = find_record(find_record, root_children, 2);
    auto* inner_record = find_record(find_record, root_children, 3);
    expect(outer_record && inner_record, "both nested Panel records must be present");

    const auto has_source = [](const list_stream::ListValue& node, std::uint64_t source_id, std::size_t cursor) {
        if (node.items.size() < cursor) return false;
        for (std::size_t edge = 0; edge < 6; ++edge) {
            if (cursor >= node.items.size() || node.items[cursor].is_list) return false;
            const auto count = static_cast<std::size_t>(std::stoul(node.items[cursor].atom));
            for (std::size_t index = 0; index < count; ++index) {
                const auto& tuple = node.items.at(cursor + index + 1);
                if (tuple.is_list && tuple.items.size() == 3 && tuple.items[1].atom == std::to_string(source_id)) return true;
            }
            cursor += count + 1;
        }
        return false;
    };
    const auto& outer_payload = outer_record->items[2].items[1];
    const auto& inner_payload = inner_record->items[2].items[1];
    expect(has_source(outer_payload, 6, 2) && has_source(outer_record->items[3], 30, 12) &&
           has_source(inner_payload, 4, 2) && has_source(inner_record->items[3], 6, 12),
        "Panel owner fanout must live in Panel properties while sibling fanout stays in outer geometry");

    const auto decoded = form_stream::decode_document(encoded.value(), "NestedPanels");
    expect(decoded.ok(), decoded ? "two-level Panel graph must decode" : decoded.diagnostics().front().message);
    const auto* decoded_outer = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_inner = decoded.value().find_control(model::ObjectId{3});
    const auto* decoded_input = decoded.value().find_control(model::ObjectId{4});
    expect(decoded_outer && decoded_inner && decoded_input && decoded_outer->kind() == model::ControlKind::panel &&
           decoded_inner->kind() == model::ControlKind::panel && decoded_outer->children.size() == 1 &&
           decoded_inner->children.size() == 1,
        "two Panel levels and their named Page references must materialize");
    expect(decoded_input->data_path->attribute.id() == model::ObjectId{7} &&
           decoded.value().collections().events.size() == 1 &&
           decoded.value().collections().events.front().handler == "InnerHandler",
        "global DataPath and nested Click links must survive");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "two-level Panel graph must round-trip without storage drift");

    auto moved_incoming = encoded.value();
    auto& moved_root_children = moved_incoming.items[1].items[2].items[2];
    auto* moved_outer = find_record(find_record, moved_root_children, 2);
    auto& panel_payload = moved_outer->items[2].items[1];
    std::size_t cursor = 2;
    bool removed = false;
    for (std::size_t edge = 0; edge < 6 && !removed; ++edge) {
        const auto count = static_cast<std::size_t>(std::stoul(panel_payload.items[cursor].atom));
        for (std::size_t index = 0; index < count; ++index) {
            const auto tuple_index = cursor + index + 1;
            if (panel_payload.items[tuple_index].items[1].atom == "6") {
                panel_payload.items[cursor] = list_stream::ListValue::raw_atom(std::to_string(count - 1));
                panel_payload.items.erase(panel_payload.items.begin() + static_cast<std::ptrdiff_t>(tuple_index));
                auto& outer_geometry = moved_outer->items[3];
                std::size_t geometry_cursor = 12;
                for (std::size_t group = 0; group < edge; ++group) {
                    geometry_cursor += 1 + static_cast<std::size_t>(std::stoul(outer_geometry.items[geometry_cursor].atom));
                }
                const auto geometry_count = static_cast<std::size_t>(std::stoul(outer_geometry.items[geometry_cursor].atom));
                outer_geometry.items[geometry_cursor] = list_stream::ListValue::raw_atom(std::to_string(geometry_count + 1));
                outer_geometry.items.insert(outer_geometry.items.begin() + static_cast<std::ptrdiff_t>(geometry_cursor + geometry_count + 1),
                    list_stream::ListValue::list({list_stream::ListValue::raw_atom("0"),
                        list_stream::ListValue::raw_atom("6"), list_stream::ListValue::raw_atom("3")}));
                removed = true;
                break;
            }
        }
        cursor += count + 1;
    }
    expect(removed, "Panel owner incoming tuple must be locatable for the forged-transfer negative case");
    expect(!form_stream::decode_document(moved_incoming, "NestedPanels"),
        "moving a Panel owner tuple into outer geometry must be rejected");

    auto unknown_property = encoded.value();
    auto& unknown_root_children = unknown_property.items[1].items[2].items[2];
    auto* unknown_panel = find_record(find_record, unknown_root_children, 2);
    unknown_panel->items[4].items[3].atom = "1";
    expect(!form_stream::decode_document(unknown_property, "NestedPanels"),
        "unknown explicit Panel property variation must fail closed");

    model::OrdinaryFormDocument changed_panel_property{document.form()};
    for (const auto& page : document.collections().pages) changed_panel_property.add_page(page);
    for (const auto& attribute : document.collections().attributes) changed_panel_property.add_attribute(attribute);
    for (const auto& event : document.collections().events) changed_panel_property.add_event(event);
    for (auto control : document.collections().controls) {
        if (control.id == model::ObjectId{2}) {
            control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
        }
        changed_panel_property.add_control(std::move(control));
    }
    expect(!form_stream::encode_document(changed_panel_property),
        "unsupported explicit Panel Enabled variation must fail closed");
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
        test_command_bar_owner_pair_and_strict_profile();
        test_captured_command_bar_control_record_literal();
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
        test_button_alignments_and_tooltip_round_trip();
        test_check_box_tooltip_round_trip_and_validation();
        test_check_box_font_round_trip_and_validation();
        test_button_colors_round_trip_and_validation();
        test_button_picture_enums_round_trip_and_validation();
        test_button_menu_mode_round_trip_and_validation();
        test_named_button_menu_round_trip_and_invalid_references();
        test_all_standard_button_pictures_round_trip_without_assets();
        test_button_external_picture_assets_round_trip();
        test_button_then_label_decoration_round_trip();
        test_label_decoration_observed_center_right_records();
        test_label_enabled_and_tooltip_round_trip();
        test_picture_decoration_default_enabled_tooltip_round_trip_and_rejections();
        test_splitter_observed_record_and_named_codec();
        test_fresh_checkbox_stream_decode();
        test_radio_button_basic_observed_record_and_rejections();
        test_calendar_field_enabled_round_trip_and_rejections();
        test_calendar_field_observed_record_decode();
        test_fresh_progress_bar_runtime_record_and_rejections();
        test_progress_data_path_mixed_with_existing_links();
        test_button_label_input_field_round_trip();
        test_input_field_tooltip_and_format_round_trip();
        test_input_field_alignment_and_choice_list_height_round_trip();
        test_single_input_field_round_trip();
        test_two_input_fields_round_trip();
        test_six_reordered_controls_use_logical_geometry_ordinals();
        test_root_pages_round_trip_with_page_local_control_order();
        test_recursive_panel_pages_keep_owner_geometry_separate();
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
