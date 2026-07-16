#include <array>
#include <exception>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "oof/model/metamodel.hpp"
#include "oof/model/ordinary_form.hpp"

namespace {

using namespace oof::model;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Operation>
void expect_invariant_error(Operation&& operation, std::string_view message) {
    try {
        operation();
    } catch (const InvariantError&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void test_descriptors() {
    using namespace oof::model::metamodel;

    const auto descriptors = control_descriptors();
    expect(descriptors.size() == 26, "metamodel must contain exactly 26 descriptors");

    std::set<std::string_view> guids;
    std::set<std::string_view> public_names;
    std::set<std::string_view> api_names;
    std::set<std::u8string_view> russian_names;
    std::array<bool, control_kind_count> kinds{};
    std::size_t controls_without_storage_tag = 0;

    for (const auto& descriptor : descriptors) {
        const auto kind_index = static_cast<std::size_t>(descriptor.kind);
        expect(kind_index < kinds.size(), "descriptor kind must be in range");
        expect(!kinds[kind_index], "descriptor kind must be unique");
        kinds[kind_index] = true;

        expect(guids.insert(descriptor.guid).second, "descriptor GUID must be unique");
        expect(
            public_names.insert(descriptor.public_name).second,
            "descriptor public name must be unique");
        expect(api_names.insert(descriptor.api_name).second, "descriptor API name must be unique");
        expect(
            russian_names.insert(descriptor.russian_name).second,
            "descriptor Russian name must be unique");
        expect(!descriptor.guid.empty(), "descriptor GUID is required");
        expect(!descriptor.public_name.empty(), "descriptor public name is required");
        expect(!descriptor.api_name.empty(), "descriptor API name is required");
        expect(!descriptor.russian_name.empty(), "descriptor Russian name is required");
        expect(
            includes(descriptor.version_mask, VersionMask::platform_8_2) &&
                includes(descriptor.version_mask, VersionMask::platform_8_3) &&
                includes(descriptor.version_mask, VersionMask::platform_8_5),
            "descriptor version mask must cover supported platform families");
        expect(
            !classification_name(descriptor.classification).empty(),
            "descriptor classification must be executable");
        controls_without_storage_tag += descriptor.storage_tag.empty() ? 1U : 0U;
    }

    for (const bool covered : kinds) {
        expect(covered, "every control kind must have one descriptor");
    }
    expect(
        controls_without_storage_tag == 2,
        "only PivotChart and ActiveXControl may lack a registry storage tag");
    expect(
        find_by_guid("6ff79819-710e-4145-97cd-1618da79e3e2")->kind ==
            ControlKind::button,
        "GUID lookup must resolve Button");
    expect(
        find_by_public_name("PictureDecoration")->api_name == "Image",
        "public-name lookup must preserve the Image API alias");
    expect(
        find_by_russian_name(u8"РамкаГруппы")->kind == ControlKind::usual_group,
        "Russian-name lookup must resolve UsualGroup");
}

void test_variant_coverage() {
    static_assert(std::variant_size_v<ControlPayload> == 26);

    const std::array<ControlPayload, control_kind_count> payloads{{
        PanelPayload{},
        CommandBarPayload{},
        ButtonPayload{},
        PictureDecorationPayload{},
        CheckBoxPayload{},
        ChoiceFieldPayload{},
        RadioButtonPayload{},
        InputFieldPayload{},
        UsualGroupPayload{},
        SplitterPayload{},
        ChartPayload{},
        PivotChartPayload{},
        GanttChartPayload{},
        DendrogramPayload{},
        HtmlDocumentFieldPayload{},
        ListBoxPayload{},
        ProgressBarPayload{},
        TrackBarPayload{},
        CalendarFieldPayload{},
        TextDocumentFieldPayload{},
        GeographicalSchemaFieldPayload{},
        GraphicalSchemaFieldPayload{},
        TablePayload{},
        SpreadsheetDocumentFieldPayload{},
        LabelDecorationPayload{},
        ActiveXControlPayload{},
    }};

    for (std::size_t index = 0; index < payloads.size(); ++index) {
        expect(
            payload_kind(payloads[index]) == static_cast<ControlKind>(index),
            "variant alternative order must cover its matching control kind");
    }

    static_assert(!std::is_same_v<ControlRef, AttributeRef>);
    static_assert(!std::is_convertible_v<ControlRef, AttributeRef>);
}

void test_property_default_semantics() {
    Property<bool> visible{true};
    expect(
        visible.state() == PropertyState::implicit_default,
        "constructed default must be implicit");
    expect(visible.value(), "implicit default value must be observable");

    visible.set(true);
    expect(
        visible.state() == PropertyState::explicit_value,
        "setting the default value must still be explicit");

    visible.reset();
    expect(
        visible.state() == PropertyState::implicit_default,
        "reset must restore implicit state");
    expect(visible.value(), "reset must restore the declared default value");

    auto width = Property<int>::explicit_value(12, 4);
    expect(width.is_explicit() && width.value() == 12, "explicit factory must retain value");
    expect(width.default_value() == 4, "explicit factory must retain declared default");
    width.reset();
    expect(
        !width.is_explicit() && width.value() == 4,
        "reset must recover the factory default");
}

void test_id_lookup() {
    Form form;
    form.id = ObjectId{1};
    form.name = "MainForm";
    form.children.push_back(ControlRef{ObjectId{10}});
    form.events.push_back(EventRef{ObjectId{40}});

    OrdinaryFormDocument document(std::move(form));
    document.set_module(FormModule{"procedure OnOpen()\nendprocedure"});
    document.add_asset(PictureAsset{ObjectId{50}, "Items/Icon/Picture.gif", PictureFormat::gif});
    document.add_attribute(Attribute{ObjectId{20}, "Value", AttributeType::string});

    Command command;
    command.id = ObjectId{30};
    command.name = "Apply";
    command.picture.set(PictureRef{PictureAssetRef{ObjectId{50}}});
    document.add_command(std::move(command));

    document.add_event(Event{ObjectId{40}, "OnOpen", "OnOpen", FormRef{ObjectId{1}}});

    InputFieldPayload input;
    input.data_attribute.set(AttributeRef{ObjectId{20}});
    document.add_control(ControlNode{ObjectId{10}, "Input", std::move(input)});

    expect(document.indexed_id_count() == 6, "all document objects must enter the ID index");
    expect(document.find_control(ObjectId{10}) != nullptr, "control lookup must be indexed");
    expect(document.find_attribute(ObjectId{20}) != nullptr, "attribute lookup must be indexed");
    expect(document.find_command(ObjectId{30}) != nullptr, "command lookup must be indexed");
    expect(document.find_event(ObjectId{40}) != nullptr, "event lookup must be indexed");
    expect(document.find_asset(ObjectId{50}) != nullptr, "asset lookup must be indexed");
    expect(document.find(ObjectId{1}).has_value(), "generic lookup must find the form");
    expect(!document.find(ObjectId{999}).has_value(), "generic lookup must reject missing IDs");
    expect(!document.module().text.empty(), "document must own module text");
    expect(document.assets().size() == 1, "document must own picture assets");
    expect(document.validate().ok(), "well-formed indexed document must validate");
}

void test_duplicate_rejection() {
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));
    document.add_control(ControlNode{ObjectId{10}, "Panel", PanelPayload{}});
    document.add_attribute(Attribute{ObjectId{10}, "Duplicate", AttributeType::string});

    const ValidationReport report = document.validate();
    expect(report.has(InvariantCode::duplicate_id), "duplicate IDs must be reported");
    expect_invariant_error(
        [&] { document.validate_or_throw(); },
        "duplicate IDs must be rejected by validate_or_throw");
}

void test_cycle_rejection() {
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));

    ControlNode panel{ObjectId{10}, "Panel", PanelPayload{}};
    panel.children.push_back(ControlRef{ObjectId{11}});
    ControlNode group{ObjectId{11}, "Group", UsualGroupPayload{}};
    group.children.push_back(ControlRef{ObjectId{10}});
    document.add_control(std::move(panel));
    document.add_control(std::move(group));

    const ValidationReport report = document.validate();
    expect(report.has(InvariantCode::cycle), "control cycles must be reported");
    expect_invariant_error(
        [&] { document.validate_or_throw(); },
        "control cycles must be rejected by validate_or_throw");
}

void test_dangling_and_child_policy_rejection() {
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));

    ControlNode button{ObjectId{10}, "Button", ButtonPayload{}};
    button.children.push_back(ControlRef{ObjectId{99}});
    document.add_control(std::move(button));

    const ValidationReport report = document.validate();
    expect(
        report.has(InvariantCode::dangling_reference),
        "dangling child references must be reported");
    expect(
        report.has(InvariantCode::illegal_children),
        "descriptor child policy must reject button children");
}

}  // namespace

int main() {
    try {
        test_descriptors();
        test_variant_coverage();
        test_property_default_semantics();
        test_id_lookup();
        test_duplicate_rejection();
        test_cycle_rejection();
        test_dangling_and_child_policy_rejection();
    } catch (const std::exception& error) {
        std::cerr << "model tests: FAIL: " << error.what() << '\n';
        return 1;
    }

    std::cout << "model tests: PASS\n";
    return 0;
}
