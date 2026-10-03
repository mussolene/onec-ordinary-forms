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
        find_by_public_name("PictureDecoration")->api_name == "PictureBox",
        "public-name lookup must preserve the exact PictureBox API object");
    expect(
        find_by_public_name("InputField")->api_name == "TextBox",
        "public InputField must resolve to the exact TextBox API object");
    expect(
        find_by_russian_name(u8"РамкаГруппы")->kind == ControlKind::usual_group,
        "Russian-name lookup must resolve UsualGroup");
}

void test_help_metamodel() {
    using namespace oof::model::metamodel;

    const MetamodelCoverage& coverage = metamodel_coverage();
    expect(coverage.control_count == 26, "help catalog must cover 26 controls");
    expect(
        coverage.control_property_occurrences == 416,
        "executable metamodel must collapse one inherited property duplicate");
    expect(
        coverage.unique_control_property_names == 200,
        "type-specific metamodel must exclude the inherited property duplicate");
    expect(
        coverage.control_event_occurrences == 79,
        "help catalog must retain 79 control event occurrences");
    expect(
        coverage.unique_control_event_names == 40,
        "help catalog must retain 40 unique control event names");
    expect(
        coverage.control_extension_property_count == 11,
        "form-control extension must retain its 11 properties");
    expect(
        coverage.panel_placement_property_count == 9,
        "panel-control extension must retain its 9 properties");
    expect(coverage.form_property_count == 37, "Form help must retain 37 fixed properties");
    expect(coverage.form_event_count == 13, "Form help must retain 13 events");
    expect(coverage.property_id_collisions == 0, "property IDs must not collide");
    expect(!coverage.release_ready, "unclassified storage/defaults must block release");
    expect(
        coverage.unclassified_properties != 0 &&
            coverage.unclassified_value_codecs != 0 &&
            coverage.missing_storage_codecs != 0,
        "coverage must expose unfinished property/storage classification");

    expect(
        property_descriptors(ControlKind::input_field).size() == 45,
        "InputField must retain the exact 45-property TextBox surface");
    expect(
        event_descriptors(ControlKind::input_field).size() == 9,
        "InputField must retain the exact 9-event TextBox surface");
    expect(
        find_property(ControlKind::input_field, "ReadOnly")->russian_name == u8"ТолькоПросмотр",
        "ReadOnly must resolve from exact bilingual help");
    expect(
        find_property(ControlKind::input_field, "ReadOnly")->value_codec == ValueCodec::boolean,
        "safe Boolean help types must receive the Boolean domain codec");
    expect(
        find_property(ControlKind::input_field, "Border")->value_codec ==
            ValueCodec::unclassified,
        "incomplete Border skeletons must not become product value codecs");
    expect(
        find_property(ControlKind::input_field, "ValueType")->value_codec ==
            ValueCodec::type_domain,
        "DescriptionOfTypes must use the proven type-domain value model");
    expect(
        find_property(ControlKind::input_field, "Visible")->surface ==
            PropertySurface::panel_placement,
        "Visible must come from the panel-placement extension");
    expect(
        find_property(ControlKind::input_field, "Top")->value_codec == ValueCodec::integer32,
        "typed Position coordinates must be 32-bit integer codecs in the metamodel");
    expect(
        find_property(ControlKind::radio_button, "FirstInGroup")->surface ==
            PropertySurface::control_extension,
        "inherited RadioButton FirstInGroup must have one shared extension owner");
    expect(
        find_property(ControlKind::input_field, "SelButtonPicture") != nullptr &&
            find_property(ControlKind::input_field, "ChoiceButtonPicture") != nullptr,
        "duplicate Russian choice-button labels must retain distinct API properties");
    expect(
        find_form_property("Caption")->russian_name == u8"Заголовок",
        "Form Caption must resolve from exact bilingual help");
    expect(
        find_event(ControlKind::input_field, "OnChange") != nullptr,
        "InputField OnChange event must be executable metamodel data");
    const auto* button_click = find_event(ControlKind::button, "Click");
    expect(
        button_click != nullptr &&
            button_click->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
            button_click->storage_codec == StorageCodec::event_record,
        "Button.Click must retain its proven storage identity and codec");
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

void test_typed_property_set() {
    PropertySet properties;
    constexpr PropertyId read_only = PropertyId::from_name("ReadOnly");
    constexpr PropertyId value_type = PropertyId::from_name("ValueType");

    static_assert(read_only != value_type);
    expect(properties.empty(), "new property set must be empty");

    properties.set_explicit(read_only, true);
    expect(properties.contains(read_only), "set property must be addressable by ID");
    expect(properties.size() == 1, "set property must occupy one entry");
    expect(
        std::get<bool>(properties.find(read_only)->value),
        "typed boolean property must retain its value");

    properties.set_explicit(read_only, false);
    expect(properties.size() == 1, "setting a property twice must replace it");
    expect(
        !std::get<bool>(properties.find(read_only)->value),
        "replacement property value must be observable");

    properties.set_explicit(value_type, TypeDomainPatternValue{});
    expect(properties.size() == 2, "different property IDs must coexist");
    expect(properties.unset(read_only), "unset must remove an explicit property");
    expect(!properties.contains(read_only), "unset property must disappear");
}

void test_id_lookup() {
    Form form;
    form.id = ObjectId{1};
    form.name = "MainForm";
    form.properties.set_explicit(
        PropertyId::from_name("Caption"),
        std::string("Main form"));
    form.children.push_back(ControlRef{ObjectId{10}});
    form.events.push_back(EventRef{ObjectId{40}});

    OrdinaryFormDocument document(std::move(form));
    document.set_module(FormModule{"procedure OnOpen()\nendprocedure"});
    document.add_asset(PictureAsset{ObjectId{50}, "Items/Icon/Picture.gif", PictureFormat::gif});
    document.add_attribute(Attribute{ObjectId{20}, "Value", TypeDomainPatternValue{}});

    Command command;
    command.id = ObjectId{30};
    command.name = "Apply";
    command.picture.set(PictureRef{PictureAssetRef{ObjectId{50}}});
    document.add_command(std::move(command));

    document.add_event(Event{ObjectId{40}, "OnOpen", "OnOpen", FormRef{ObjectId{1}}});

    ControlNode input{ObjectId{10}, "Input", InputFieldPayload{}};
    input.data_path = DataPath{AttributeRef{ObjectId{20}}, {}};
    document.add_control(std::move(input));

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
    document.add_control(ControlNode{ObjectId{10}, "OtherPanel", PanelPayload{}});

    const ValidationReport report = document.validate();
    expect(report.has(InvariantCode::duplicate_id), "duplicate IDs must be reported");
    expect_invariant_error(
        [&] { document.validate_or_throw(); },
        "duplicate IDs must be rejected by validate_or_throw");
}

void test_category_scoped_object_ids() {
    Form form;
    form.id = ObjectId{1};
    form.name = "Main";
    form.children.push_back(ControlRef{ObjectId{4}});
    form.events.push_back(EventRef{ObjectId{5}});
    OrdinaryFormDocument document(std::move(form));

    document.add_attribute(Attribute{ObjectId{1}, "SyntheticValue", TypeDomainPatternValue{}});
    ControlNode input{ObjectId{4}, "InputSynthetic", InputFieldPayload{}};
    input.data_path = DataPath{AttributeRef{ObjectId{1}}, {}};
    document.add_control(std::move(input));
    document.add_event(Event{ObjectId{5}, "OnOpen", "OnOpen", FormRef{ObjectId{1}}});

    expect(document.find_attribute(ObjectId{1}) != nullptr,
        "AttributeRef ID 1 must resolve when FormRef also has ID 1");
    expect(document.find_control(ObjectId{4}) != nullptr,
        "control ID 4 must resolve independently");
    expect(document.find(ObjectId{1}) == std::nullopt,
        "untyped lookup must report ambiguity for Form ID 1 and Attribute ID 1");
    expect(document.indexed_id_count() == 4,
        "index count must include each category-scoped object identity");
    expect(document.validate().ok(),
        "Form 1, Attribute 1, and DataPath from Control 4 must validate");
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

void test_page_object_graph() {
    Form form;
    form.id = ObjectId{1};
    form.children.push_back(ControlRef{ObjectId{10}});
    OrdinaryFormDocument document(std::move(form));

    ControlNode panel{ObjectId{10}, "Panel", PanelPayload{}};
    panel.children.push_back(PageRef{ObjectId{10}});
    document.add_control(std::move(panel));

    Page page;
    page.id = ObjectId{10};
    page.name = "MainPage";
    page.title.set(LocalizedStringValue{{LocalizedStringItem{"ru", "Main"}}});
    page.children.push_back(ControlRef{ObjectId{12}});
    document.add_page(std::move(page));
    document.add_control(ControlNode{ObjectId{12}, "Button", ButtonPayload{}});

    expect(document.find_page(ObjectId{10}) != nullptr && document.find_control(ObjectId{10}) != nullptr,
        "control and page with the same numeric ID must both resolve by category");
    expect(document.indexed_id_count() == 4, "page must participate in the object index");
    expect(document.validate().ok(), "panel-page-control graph must validate");
}

void test_root_page_and_remaining_page_policies() {
    Form form;
    form.id = ObjectId{1};
    form.children.push_back(PageRef{ObjectId{11}});
    OrdinaryFormDocument document(std::move(form));

    Page page;
    page.id = ObjectId{11};
    page.name = "RootPage";
    page.children.push_back(ControlRef{ObjectId{12}});
    document.add_page(std::move(page));
    document.add_control(ControlNode{ObjectId{12}, "PageButton", ButtonPayload{}});

    expect(document.validate().ok(), "form root may own a page with control children");

    Form dangling_seed;
    dangling_seed.id = ObjectId{1};
    dangling_seed.children.push_back(PageRef{ObjectId{99}});
    OrdinaryFormDocument dangling_document(std::move(dangling_seed));
    expect(dangling_document.validate().has(InvariantCode::dangling_reference),
        "a root Page reference must still resolve to a Page object");

    Form group_seed;
    group_seed.id = ObjectId{1};
    group_seed.name = "Main";
    OrdinaryFormDocument group_document(std::move(group_seed));
    ControlNode group{ObjectId{2}, "Group", UsualGroupPayload{}};
    group.children.push_back(PageRef{ObjectId{3}});
    group_document.add_control(std::move(group));
    Page illegal_page;
    illegal_page.id = ObjectId{3};
    illegal_page.name = "IllegalPage";
    group_document.add_page(std::move(illegal_page));
    auto group_form = group_document.form();
    group_form.children.push_back(ControlRef{ObjectId{2}});
    group_document.set_form(std::move(group_form));
    expect(group_document.validate().has(InvariantCode::illegal_children),
        "UsualGroup must continue to reject Page children");

    Form nested_seed;
    nested_seed.id = ObjectId{1};
    nested_seed.name = "Main";
    OrdinaryFormDocument nested_page_document(std::move(nested_seed));
    Page parent_page;
    parent_page.id = ObjectId{2};
    parent_page.name = "ParentPage";
    parent_page.children.push_back(PageRef{ObjectId{3}});
    nested_page_document.add_page(std::move(parent_page));
    Page nested_page;
    nested_page.id = ObjectId{3};
    nested_page.name = "NestedPage";
    nested_page_document.add_page(std::move(nested_page));
    auto nested_form = nested_page_document.form();
    nested_form.children.push_back(PageRef{ObjectId{2}});
    nested_page_document.set_form(std::move(nested_form));
    expect(nested_page_document.validate().has(InvariantCode::illegal_children),
        "Page must continue to reject nested Page children");

    Form multiply_owned_seed;
    multiply_owned_seed.id = ObjectId{1};
    multiply_owned_seed.name = "Main";
    OrdinaryFormDocument multiply_owned_document(std::move(multiply_owned_seed));
    Page multiply_owned_page;
    multiply_owned_page.id = ObjectId{2};
    multiply_owned_page.name = "SharedPage";
    multiply_owned_document.add_page(std::move(multiply_owned_page));
    auto multiply_owned_form = multiply_owned_document.form();
    multiply_owned_form.children.push_back(PageRef{ObjectId{2}});
    multiply_owned_document.set_form(std::move(multiply_owned_form));
    ControlNode panel{ObjectId{3}, "Panel", PanelPayload{}};
    panel.children.push_back(PageRef{ObjectId{2}});
    multiply_owned_document.add_control(std::move(panel));
    auto multiply_owned_form_again = multiply_owned_document.form();
    multiply_owned_form_again.children.push_back(ControlRef{ObjectId{3}});
    multiply_owned_document.set_form(std::move(multiply_owned_form_again));
    expect(multiply_owned_document.validate().has(InvariantCode::multiple_parents),
        "a root Page must still have exactly one authoritative parent");

    Form page_cycle_seed;
    page_cycle_seed.id = ObjectId{1};
    page_cycle_seed.name = "Main";
    OrdinaryFormDocument page_cycle_document(std::move(page_cycle_seed));
    Page root_page;
    root_page.id = ObjectId{2};
    root_page.name = "RootPage";
    root_page.children.push_back(ControlRef{ObjectId{3}});
    page_cycle_document.add_page(std::move(root_page));
    ControlNode cycle_panel{ObjectId{3}, "CyclePanel", PanelPayload{}};
    cycle_panel.children.push_back(PageRef{ObjectId{2}});
    page_cycle_document.add_control(std::move(cycle_panel));
    auto page_cycle_form = page_cycle_document.form();
    page_cycle_form.children.push_back(PageRef{ObjectId{2}});
    page_cycle_document.set_form(std::move(page_cycle_form));
    expect(page_cycle_document.validate().has(InvariantCode::cycle),
        "a root Page cycle through Panel must continue to be rejected");
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

void test_property_reference_rejection() {
    Form form;
    form.id = ObjectId{1};
    form.children.push_back(ControlRef{ObjectId{10}});
    OrdinaryFormDocument document(std::move(form));

    ButtonPayload payload;
    payload.properties.set_explicit(
        PropertyId::from_name("Command"),
        CommandRef{ObjectId{99}});
    document.add_control(ControlNode{ObjectId{10}, "Button", std::move(payload)});

    const ValidationReport report = document.validate();
    expect(
        report.has(InvariantCode::dangling_reference),
        "typed property references must participate in invariant validation");
}

void test_property_applicability_and_type_validation() {
    Form form;
    form.id = ObjectId{1};
    form.children.push_back(ControlRef{ObjectId{10}});
    OrdinaryFormDocument valid(std::move(form));

    InputFieldPayload input_payload;
    input_payload.properties.set_explicit(PropertyId::from_name("ReadOnly"), true);
    ControlNode input{ObjectId{10}, "Input", std::move(input_payload)};
    input.extension_properties.set_explicit(
        PropertyId::from_name("AutoContextMenu"),
        false);
    valid.add_control(std::move(input));
    expect(valid.validate().ok(), "declared payload and extension properties must validate");

    Form wrong_type_form;
    wrong_type_form.id = ObjectId{1};
    wrong_type_form.children.push_back(ControlRef{ObjectId{10}});
    OrdinaryFormDocument wrong_type(std::move(wrong_type_form));
    InputFieldPayload wrong_type_payload;
    wrong_type_payload.properties.set_explicit(
        PropertyId::from_name("ReadOnly"),
        std::string("false"));
    wrong_type.add_control(ControlNode{ObjectId{10}, "Input", std::move(wrong_type_payload)});
    expect(
        wrong_type.validate().has(InvariantCode::invalid_property),
        "property values with the wrong metamodel kind must be rejected");

    Form wrong_owner_form;
    wrong_owner_form.id = ObjectId{1};
    wrong_owner_form.children.push_back(ControlRef{ObjectId{10}});
    OrdinaryFormDocument wrong_owner(std::move(wrong_owner_form));
    ButtonPayload button;
    button.properties.set_explicit(PropertyId::from_name("ReadOnly"), true);
    wrong_owner.add_control(ControlNode{ObjectId{10}, "Button", std::move(button)});
    expect(
        wrong_owner.validate().has(InvariantCode::invalid_property),
        "a property from another control kind must be rejected");

    Form wrong_surface_form;
    wrong_surface_form.id = ObjectId{1};
    wrong_surface_form.children.push_back(ControlRef{ObjectId{10}});
    OrdinaryFormDocument wrong_surface(std::move(wrong_surface_form));
    ControlNode misplaced{ObjectId{10}, "Input", InputFieldPayload{}};
    misplaced.extension_properties.set_explicit(PropertyId::from_name("Visible"), true);
    wrong_surface.add_control(std::move(misplaced));
    expect(
        wrong_surface.validate().has(InvariantCode::invalid_property),
        "placement properties must not be duplicated in the extension property set");
}

void test_event_sequence_invariants() {
    Form form;
    form.id = ObjectId{1};
    form.children.push_back(ControlRef{ObjectId{10}});
    form.events.push_back(EventRef{ObjectId{40}});
    OrdinaryFormDocument wrong_owner(std::move(form));
    wrong_owner.add_control(ControlNode{ObjectId{10}, "Button", ButtonPayload{}});
    wrong_owner.add_event(Event{
        ObjectId{40},
        "Click",
        "Click",
        ControlRef{ObjectId{10}},
    });
    expect(
        wrong_owner.validate().has(InvariantCode::invalid_property),
        "event sequence owner must match Event.owner");

    Form orphan_form;
    orphan_form.id = ObjectId{1};
    OrdinaryFormDocument orphan(std::move(orphan_form));
    orphan.add_event(Event{
        ObjectId{40},
        "OnOpen",
        "OnOpen",
        FormRef{ObjectId{1}},
    });
    expect(
        orphan.validate().has(InvariantCode::orphan),
        "event missing from its owner's sequence must be rejected");

    Form duplicate_form;
    duplicate_form.id = ObjectId{1};
    duplicate_form.events.push_back(EventRef{ObjectId{40}});
    duplicate_form.events.push_back(EventRef{ObjectId{41}});
    OrdinaryFormDocument duplicate(std::move(duplicate_form));
    duplicate.add_event(Event{ObjectId{40}, "OnOpen", "First", FormRef{ObjectId{1}}});
    duplicate.add_event(Event{ObjectId{41}, "OnOpen", "Second", FormRef{ObjectId{1}}});
    expect(
        duplicate.validate().has(InvariantCode::invalid_property),
        "one event name may occur only once for an owner");
}

void test_duplicate_bindings_rejected() {
    Form form;
    form.id = ObjectId{1};
    form.children.push_back(ControlRef{ObjectId{10}});
    OrdinaryFormDocument document(std::move(form));
    ControlNode input{ObjectId{10}, "Input", InputFieldPayload{}};
    input.position.bindings.anchors.push_back(
        AnchorBinding{
            BindingCoordinate::left,
            std::nullopt,
            Property<std::int32_t>{0},
            BindingCoordinate::left,
            std::nullopt,
        });
    input.position.bindings.anchors.push_back(
        AnchorBinding{
            BindingCoordinate::left,
            std::nullopt,
            Property<std::int32_t>{0},
            BindingCoordinate::left,
            std::nullopt,
        });
    document.add_control(std::move(input));
    expect(
        document.validate().has(InvariantCode::invalid_property),
        "duplicate binding coordinates must be rejected");
}

void test_page_position_invariants() {
    Form form;
    form.id = ObjectId{1};
    form.children.push_back(PageRef{ObjectId{2}});
    OrdinaryFormDocument dangling(std::move(form));
    Page page;
    page.id = ObjectId{2};
    page.name = "Settings";
    Position dangling_position;
    dangling_position.bindings.anchors.push_back(
        AnchorBinding{
            BindingCoordinate::left,
            ControlRef{ObjectId{99}},
            Property<std::int32_t>{0},
            BindingCoordinate::left,
            std::nullopt,
        });
    page.position.set(std::move(dangling_position));
    dangling.add_page(std::move(page));
    expect(
        dangling.validate().has(InvariantCode::dangling_reference),
        "Page Position binding targets must resolve to controls");

    Form negative_form;
    negative_form.id = ObjectId{1};
    negative_form.children.push_back(PageRef{ObjectId{2}});
    OrdinaryFormDocument negative(std::move(negative_form));
    Page negative_page;
    negative_page.id = ObjectId{2};
    negative_page.name = "Negative";
    Position negative_position;
    negative_position.height.set(-1);
    negative_page.position.set(std::move(negative_position));
    negative.add_page(std::move(negative_page));
    expect(
        negative.validate().has(InvariantCode::invalid_property),
        "Page Position dimensions must be non-negative");
}

}  // namespace

int main() {
    try {
        test_descriptors();
        test_help_metamodel();
        test_variant_coverage();
        test_property_default_semantics();
        test_typed_property_set();
        test_id_lookup();
        test_duplicate_rejection();
        test_category_scoped_object_ids();
        test_cycle_rejection();
        test_page_object_graph();
        test_root_page_and_remaining_page_policies();
        test_dangling_and_child_policy_rejection();
        test_property_reference_rejection();
        test_property_applicability_and_type_validation();
        test_event_sequence_invariants();
        test_duplicate_bindings_rejected();
        test_page_position_invariants();
    } catch (const std::exception& error) {
        std::cerr << "model tests: FAIL: " << error.what() << '\n';
        return 1;
    }

    std::cout << "model tests: PASS\n";
    return 0;
}
