#include "../src/model/chart_type.hpp"
#include "../src/model/table_properties.hpp"
#include <array>
#include <exception>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "oof/model/metamodel.hpp"
#include "oof/model/ordinary_form.hpp"
#include "oof/form_bin.hpp"
#include "oof/source/form_xml.hpp"

void run_runtime_collection_tests();

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
    for (const auto& [name, default_value] : std::array{
             std::pair{std::string_view{"Transparent"}, std::string_view{"false"}},
             std::pair{std::string_view{"AutoSeriesText"}, std::string_view{"true"}},
             std::pair{std::string_view{"AutoPointText"}, std::string_view{"true"}},
             std::pair{std::string_view{"Outline"}, std::string_view{"false"}},
             std::pair{std::string_view{"Light"}, std::string_view{"true"}},
             std::pair{std::string_view{"Gradient"}, std::string_view{"false"}},
             std::pair{std::string_view{"SeriesInRows"}, std::string_view{"true"}},
             std::pair{std::string_view{"HideBaseValue"}, std::string_view{"false"}},
             std::pair{std::string_view{"AutoTransposition"}, std::string_view{"false"}},
             std::pair{std::string_view{"GaugeChartValuesScaleLabelsArcDirection"}, std::string_view{"false"}},
             std::pair{std::string_view{"AutoMaxValue"}, std::string_view{"true"}},
             std::pair{std::string_view{"AutoMinValue"}, std::string_view{"true"}},
             std::pair{std::string_view{"TransparentLabel"}, std::string_view{"true"}}}) {
        const auto* descriptor = find_property(ControlKind::chart, name);
        expect(descriptor && descriptor->value_codec == ValueCodec::boolean &&
                   descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == StorageCodec::control_info &&
                   descriptor->default_value.kind == DefaultKind::boolean &&
                   descriptor->default_value.canonical == default_value,
            std::string("Chart Boolean descriptor must preserve its observed default: ") + std::string(name));
    }
    const auto* begin_period = find_property(ControlKind::calendar_field, "BeginOfDisplayPeriod");
    expect(begin_period != nullptr && begin_period->value_codec == ValueCodec::date &&
               begin_period->persistence == PersistenceClass::persisted_editable &&
               begin_period->storage_codec == StorageCodec::control_info &&
               begin_period->default_value.kind == DefaultKind::undefined &&
               begin_period->default_value.canonical == "undefined",
        "CalendarField BeginOfDisplayPeriod must be a classified Date property with Undefined default");
    const auto* end_period = find_property(ControlKind::calendar_field, "EndOfDisplayPeriod");
    expect(end_period != nullptr && end_period->value_codec == ValueCodec::date &&
               end_period->persistence == PersistenceClass::persisted_editable &&
               end_period->storage_codec == StorageCodec::control_info &&
               end_period->default_value.kind == DefaultKind::undefined &&
               end_period->default_value.canonical == "undefined",
        "CalendarField EndOfDisplayPeriod must be a classified Date property with Undefined default");
    for (const auto& [name, default_value] : std::array{
             std::pair{std::string_view{"ShowCurrentDate"}, std::string_view{"true"}},
             std::pair{std::string_view{"CalendarNavigation"}, std::string_view{"true"}},
             std::pair{std::string_view{"EnableStartDrag"}, std::string_view{"false"}},
             std::pair{std::string_view{"EnableDrag"}, std::string_view{"false"}}}) {
        const auto* descriptor = find_property(ControlKind::calendar_field, name);
        expect(descriptor && descriptor->value_codec == ValueCodec::boolean &&
                   descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == StorageCodec::control_info &&
                   descriptor->default_value.kind == DefaultKind::boolean &&
                   descriptor->default_value.canonical == default_value,
            std::string("CalendarField ") + std::string(name) +
                " must use its persisted Boolean descriptor and observed default");
    }
    const auto* gantt_auto = find_property(ControlKind::gantt_chart, "AutoFullInterval");
    const auto* gantt_begin = find_property(ControlKind::gantt_chart, "FullIntervalBegin");
    const auto* gantt_end = find_property(ControlKind::gantt_chart, "FullIntervalEnd");
    expect(gantt_auto && gantt_auto->value_codec == ValueCodec::boolean &&
               gantt_auto->default_value.kind == DefaultKind::boolean &&
               gantt_auto->default_value.canonical == "true" &&
               gantt_begin && gantt_end && gantt_begin->value_codec == ValueCodec::date &&
               gantt_end->value_codec == ValueCodec::date &&
               gantt_begin->default_value.kind == DefaultKind::undefined &&
               gantt_end->default_value.kind == DefaultKind::undefined,
        "Gantt full interval properties must use named Boolean and Date descriptors with platform defaults");

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
    const auto* group_enabled = find_property(ControlKind::usual_group, "Enabled");
    for (const auto name : {"Enabled", "ToolTip", "Font", "Transparent"}) {
        const auto* property = find_property(ControlKind::panel, name);
        expect(property && property->storage_codec == StorageCodec::control_base,
            std::string("Panel common property must have a base codec: ") + name);
    }
    const auto* scroll_page_mode = find_property(ControlKind::panel, "ScrollPageMode");
    expect(scroll_page_mode && scroll_page_mode->storage_codec == StorageCodec::control_info &&
        scroll_page_mode->value_codec == ValueCodec::boolean &&
        scroll_page_mode->default_value.kind == DefaultKind::boolean &&
        scroll_page_mode->default_value.canonical == "false",
        "Panel ScrollPageMode must use the observed boolean default and control-info storage");
    const auto* separate_by_page = find_property(ControlKind::panel, "SeparateByPage");
    expect(separate_by_page && separate_by_page->storage_codec == StorageCodec::control_info &&
        separate_by_page->value_codec == ValueCodec::boolean &&
        separate_by_page->default_value.kind == DefaultKind::boolean &&
        separate_by_page->default_value.canonical == "true",
        "Panel SeparateByPage must use the observed boolean default and control-info storage");
    const auto* panel_tabs = find_property(ControlKind::panel, "ShowTabs");
    expect(panel_tabs && panel_tabs->storage_codec == StorageCodec::control_info &&
        panel_tabs->value_codec == ValueCodec::enumeration &&
        panel_tabs->default_value.kind == DefaultKind::enumeration && panel_tabs->default_value.canonical == "ShowTabs.Top",
        "Panel ShowTabs must have its observed default and descriptor");
    const auto* group_caption = find_property(ControlKind::usual_group, "Caption");
    const auto* group_tool_tip = find_property(ControlKind::usual_group, "ToolTip");
    expect(group_enabled && group_enabled->storage_codec == StorageCodec::control_base &&
               group_enabled->default_value.kind == DefaultKind::boolean && group_enabled->default_value.canonical == "true" &&
               group_caption && group_caption->storage_codec == StorageCodec::control_info &&
               group_caption->default_value.kind == DefaultKind::string && group_caption->default_value.canonical.empty() &&
               group_tool_tip && group_tool_tip->storage_codec == StorageCodec::control_base &&
               group_tool_tip->default_value.kind == DefaultKind::string && group_tool_tip->default_value.canonical.empty(),
        "UsualGroup named property descriptors must expose observed storage and defaults");

    const auto* label_vertical = find_property(ControlKind::label_decoration, "VerticalAlign");
    const auto* label_negatives = find_property(ControlKind::label_decoration, "MarkNegatives");
    const auto* label_scrolling = find_property(ControlKind::label_decoration, "ScrollingText");
    expect(label_vertical && label_vertical->storage_codec == StorageCodec::control_info &&
               label_vertical->default_value.kind == DefaultKind::enumeration && label_vertical->default_value.canonical == "Center" &&
               label_negatives && label_negatives->storage_codec == StorageCodec::control_info &&
               label_negatives->default_value.kind == DefaultKind::boolean && label_negatives->default_value.canonical == "false" &&
               label_scrolling && label_scrolling->storage_codec == StorageCodec::control_info &&
               label_scrolling->default_value.kind == DefaultKind::enumeration &&
               label_scrolling->default_value.canonical == "ScrollingTextMode.DontUse",
        "LabelDecoration descriptors must expose typed control-info defaults");
    for (const auto name : {"Enabled", "ToolTip", "Font", "BorderColor", "FieldBackColor"}) {
        const auto* descriptor = find_property(ControlKind::table, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == StorageCodec::control_base,
            std::string("Table.") + name + " must expose a persisted common-base descriptor");
    }
    for (const auto& [name, default_value] : std::array{
             std::pair{"AutoInsertNewRow", "false"}, std::pair{"Footer", "false"}, std::pair{"Header", "true"},
             std::pair{"EnableStartDrag", "false"}, std::pair{"EnableDrag", "false"},
             std::pair{"ChangeSettingOfColumns", "true"}, std::pair{"ChangePositionOfColumns", "true"},
             std::pair{"ChangeRowSet", "true"}, std::pair{"HorizontalLines", "true"},
             std::pair{"VerticalLines", "true"}, std::pair{"UseAlternationRowColor", "false"}}) {
        const auto* property = find_property(ControlKind::table, name);
        expect(property && property->persistence == PersistenceClass::persisted_editable &&
            property->storage_codec == StorageCodec::control_info && property->value_codec == ValueCodec::boolean &&
            property->default_value.kind == DefaultKind::boolean && property->default_value.canonical == default_value,
            std::string("Table.") + name + " must expose its Boolean default and storage descriptor");
    }
    for (const auto name : {"HeaderBackColor", "HeaderTextColor", "FooterBackColor", "FooterTextColor",
             "SelectionBackColor", "SelectionTextColor", "AlternationRowBackColor", "HeaderFont", "FooterFont"}) {
        const auto* property = find_property(ControlKind::table, name);
        expect(property && property->persistence == PersistenceClass::persisted_editable &&
            property->storage_codec == StorageCodec::control_info && property->default_value.kind != DefaultKind::unknown,
            std::string("Table.") + name + " must expose its appearance descriptor and default");
    }
    const auto* horizontal = find_property(ControlKind::button, "HorizontalAlign");
    const auto* vertical = find_property(ControlKind::button, "VerticalAlign");
    const auto* tool_tip = find_property(ControlKind::button, "ToolTip");
    const auto* picture_location = find_property(ControlKind::button, "PictureLocation");
    const auto* picture_size = find_property(ControlKind::button, "PictureSize");
    const auto* menu_mode = find_property(ControlKind::button, "MenuMode");
    expect(horizontal && horizontal->storage_codec == StorageCodec::control_info &&
               horizontal->default_value.kind == DefaultKind::enumeration &&
               horizontal->default_value.canonical == "Center",
        "Button HorizontalAlign descriptor must declare its encoded Center default");
    expect(vertical && vertical->storage_codec == StorageCodec::control_info &&
               vertical->default_value.kind == DefaultKind::enumeration &&
               vertical->default_value.canonical == "Center",
        "Button VerticalAlign descriptor must declare its encoded Center default");
    expect(tool_tip && tool_tip->storage_codec == StorageCodec::control_base &&
               tool_tip->default_value.kind == DefaultKind::string &&
               tool_tip->default_value.canonical.empty(),
        "Button ToolTip descriptor must declare its empty localized default");
    const auto* check_box_tool_tip = find_property(ControlKind::check_box, "ToolTip");
    expect(check_box_tool_tip && check_box_tool_tip->persistence == PersistenceClass::persisted_editable &&
               check_box_tool_tip->storage_codec == StorageCodec::control_base &&
               check_box_tool_tip->default_value.kind == DefaultKind::string &&
               check_box_tool_tip->default_value.canonical.empty(),
        "CheckBox ToolTip descriptor must declare its empty localized base-string default");
    const auto* check_box_three_state = find_property(ControlKind::check_box, "ThreeState");
    expect(check_box_three_state && check_box_three_state->persistence == PersistenceClass::persisted_editable &&
               check_box_three_state->storage_codec == StorageCodec::control_info &&
               check_box_three_state->value_codec == ValueCodec::boolean &&
               check_box_three_state->default_value.kind == DefaultKind::boolean &&
               check_box_three_state->default_value.canonical == "false",
        "CheckBox ThreeState descriptor must use its Boolean control-info slot and false default");
    const auto* check_box_font = find_property(ControlKind::check_box, "Font");
    expect(check_box_font && check_box_font->persistence == PersistenceClass::persisted_editable &&
               check_box_font->storage_codec == StorageCodec::control_base &&
               check_box_font->value_codec == ValueCodec::font &&
               check_box_font->default_value.kind == DefaultKind::font &&
               check_box_font->default_value.canonical == "automatic",
        "CheckBox Font descriptor must declare its editable automatic Font default");
    const auto* splitter_orientation = find_property(ControlKind::splitter, "Orientation");
    const auto* splitter_enabled = find_property(ControlKind::splitter, "Enabled");
    const auto* splitter_back_color = find_property(ControlKind::splitter, "BackColor");
    expect(splitter_orientation && splitter_orientation->persistence == PersistenceClass::persisted_editable &&
               splitter_orientation->storage_codec == StorageCodec::control_info &&
               splitter_orientation->value_codec == ValueCodec::enumeration &&
               splitter_orientation->default_value.kind == DefaultKind::enumeration &&
               splitter_orientation->default_value.canonical == "Auto",
        "Splitter Orientation descriptor must declare its editable Auto default");
    expect(splitter_enabled && splitter_enabled->storage_codec == StorageCodec::control_base &&
               splitter_enabled->default_value.kind == DefaultKind::boolean &&
               splitter_enabled->default_value.canonical == "true" &&
               splitter_back_color && splitter_back_color->storage_codec == StorageCodec::control_base &&
               splitter_back_color->value_codec == ValueCodec::color &&
               splitter_back_color->default_value.kind == DefaultKind::color &&
               splitter_back_color->default_value.canonical == "automatic",
        "Splitter base descriptors must declare proven editable defaults");
    expect(picture_location && picture_location->storage_codec == StorageCodec::control_info &&
               picture_location->default_value.kind == DefaultKind::enumeration &&
               picture_location->default_value.canonical == "Left",
        "Button PictureLocation descriptor must declare Left as its default");
    expect(picture_size && picture_size->storage_codec == StorageCodec::control_info &&
               picture_size->default_value.kind == DefaultKind::enumeration &&
               picture_size->default_value.canonical == "RealSize",
        "Button PictureSize descriptor must declare RealSize as its default");
    expect(menu_mode && menu_mode->storage_codec == StorageCodec::control_info &&
               menu_mode->default_value.kind == DefaultKind::enumeration &&
               menu_mode->default_value.canonical == "DontUse",
        "Button MenuMode descriptor must declare DontUse as its encoded default");
    const auto* label_enabled = find_property(ControlKind::label_decoration, "Enabled");
    const auto* label_tool_tip = find_property(ControlKind::label_decoration, "ToolTip");
    const auto* label_hyperlink = find_property(ControlKind::label_decoration, "Hyperlink");
    const auto* label_shortcut = find_property(ControlKind::label_decoration, "Shortcut");
    expect(label_enabled && label_enabled->persistence == PersistenceClass::persisted_editable &&
               label_enabled->storage_codec == StorageCodec::control_base &&
               label_enabled->value_codec == ValueCodec::boolean &&
               label_enabled->default_value.kind == DefaultKind::boolean &&
               label_enabled->default_value.canonical == "true",
        "LabelDecoration Enabled must declare its editable Boolean true default");
    expect(label_tool_tip && label_tool_tip->persistence == PersistenceClass::persisted_editable &&
               label_tool_tip->storage_codec == StorageCodec::control_base &&
               label_tool_tip->value_codec == ValueCodec::localized_text &&
               label_tool_tip->default_value.kind == DefaultKind::string &&
               label_tool_tip->default_value.canonical.empty(),
        "LabelDecoration ToolTip must declare its editable empty string default");
    expect(label_hyperlink && label_hyperlink->persistence == PersistenceClass::persisted_editable &&
               label_hyperlink->storage_codec == StorageCodec::control_info &&
               label_hyperlink->value_codec == ValueCodec::boolean &&
               label_hyperlink->default_value.kind == DefaultKind::boolean &&
               label_hyperlink->default_value.canonical == "false" &&
               label_shortcut && label_shortcut->persistence == PersistenceClass::persisted_editable &&
               label_shortcut->storage_codec == StorageCodec::control_info &&
               label_shortcut->value_codec == ValueCodec::shortcut &&
               label_shortcut->default_value.kind == DefaultKind::shortcut &&
               label_shortcut->default_value.canonical == "None",
        "LabelDecoration Hyperlink and Shortcut must declare their typed defaults in the existing info record");
    const auto* radio_enabled = find_property(ControlKind::radio_button, "Enabled");
    const auto* radio_caption = find_property(ControlKind::radio_button, "Caption");
    const auto* radio_tool_tip = find_property(ControlKind::radio_button, "ToolTip");
    expect(radio_enabled && radio_enabled->persistence == PersistenceClass::persisted_editable &&
               radio_enabled->storage_codec == StorageCodec::control_base &&
               radio_enabled->value_codec == ValueCodec::boolean &&
               radio_enabled->default_value.kind == DefaultKind::boolean &&
               radio_enabled->default_value.canonical == "true",
        "RadioButton Enabled must declare its editable Boolean true default");
    expect(radio_caption && radio_caption->persistence == PersistenceClass::persisted_editable &&
               radio_caption->storage_codec == StorageCodec::control_info &&
               radio_caption->value_codec == ValueCodec::localized_text &&
               radio_caption->default_value.kind == DefaultKind::string &&
               radio_caption->default_value.canonical.empty(),
        "RadioButton Caption must declare its editable empty string default");
    expect(radio_tool_tip && radio_tool_tip->persistence == PersistenceClass::persisted_editable &&
               radio_tool_tip->storage_codec == StorageCodec::control_base &&
               radio_tool_tip->value_codec == ValueCodec::localized_text &&
               radio_tool_tip->default_value.kind == DefaultKind::string &&
               radio_tool_tip->default_value.canonical.empty(),
        "RadioButton ToolTip must declare its editable empty string default");
    const auto* html_output = find_property(ControlKind::html_document_field, "Output");
    expect(html_output && html_output->persistence == PersistenceClass::persisted_editable &&
               html_output->storage_codec == StorageCodec::control_info &&
               html_output->value_codec == ValueCodec::enumeration &&
               html_output->default_value.kind == DefaultKind::enumeration &&
               html_output->default_value.canonical == "Auto",
        "HTMLDocumentField Output must declare its editable UseOutput Auto default");
    const auto* picture_enabled = find_property(ControlKind::picture_decoration, "Enabled");
    const auto* picture_tool_tip = find_property(ControlKind::picture_decoration, "ToolTip");
    const auto* picture_value = find_property(ControlKind::picture_decoration, "Picture");
    const auto* picture_shortcut = find_property(ControlKind::picture_decoration, "Shortcut");
    expect(picture_enabled && picture_enabled->persistence == PersistenceClass::persisted_editable &&
               picture_enabled->storage_codec == StorageCodec::control_base &&
               picture_enabled->value_codec == ValueCodec::boolean &&
               picture_enabled->default_value.kind == DefaultKind::boolean &&
               picture_enabled->default_value.canonical == "true",
        "PictureDecoration Enabled must declare its editable Boolean true default");
    expect(picture_tool_tip && picture_tool_tip->persistence == PersistenceClass::persisted_editable &&
               picture_tool_tip->storage_codec == StorageCodec::control_base &&
               picture_tool_tip->value_codec == ValueCodec::localized_text &&
               picture_tool_tip->default_value.kind == DefaultKind::string &&
               picture_tool_tip->default_value.canonical.empty(),
        "PictureDecoration ToolTip must declare its editable empty string default");
    expect(picture_value && picture_value->persistence == PersistenceClass::persisted_editable &&
               picture_value->storage_codec == StorageCodec::picture_record &&
               picture_value->value_codec == ValueCodec::picture &&
               picture_value->default_value.kind == DefaultKind::none,
        "PictureDecoration Picture must declare its typed editable picture record");
    expect(picture_shortcut && picture_shortcut->persistence == PersistenceClass::persisted_editable &&
               picture_shortcut->storage_codec == StorageCodec::control_info &&
               picture_shortcut->value_codec == ValueCodec::shortcut &&
               picture_shortcut->default_value.kind == DefaultKind::shortcut &&
               picture_shortcut->default_value.canonical == "None",
        "PictureDecoration Shortcut must use the shared typed Shortcut codec and None default");
    const auto* input_shortcut = find_property(ControlKind::input_field, "Shortcut");
    expect(input_shortcut && input_shortcut->persistence == PersistenceClass::persisted_editable &&
               input_shortcut->storage_codec == StorageCodec::control_info &&
               input_shortcut->value_codec == ValueCodec::shortcut &&
               input_shortcut->default_value.kind == DefaultKind::shortcut &&
               input_shortcut->default_value.canonical == "None",
        "InputField Shortcut must use the shared typed Shortcut codec and None default");
}

void test_z_order_reorders_siblings() {
    const std::array<ControlPayload, 12> payloads{
        CalendarFieldPayload{}, ChoiceFieldPayload{}, CommandBarPayload{}, CheckBoxPayload{},
        RadioButtonPayload{}, InputFieldPayload{}, ListBoxPayload{}, UsualGroupPayload{},
        ProgressBarPayload{}, TrackBarPayload{}, LabelDecorationPayload{}, PanelPayload{}};
    Form form; form.id = ObjectId{1}; form.name = "ZOrder";
    for (std::uint64_t id = 10; id < 22; ++id) form.children.push_back(ControlRef{ObjectId{id}});
    OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < payloads.size(); ++index)
        document.add_control(ControlNode{ObjectId{10 + index}, "Control" + std::to_string(index), payloads[index]});
    const auto z = PropertyId::from_name("ZOrder");
    const auto tab = PropertyId::from_name("TabOrder");
    const auto* descriptor = metamodel::find_property(ControlKind::input_field, z);
    expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::persisted_editable &&
        descriptor->storage_codec == metamodel::StorageCodec::position_record &&
        descriptor->value_codec == metamodel::ValueCodec::integer32 && descriptor->default_value.kind == metamodel::DefaultKind::none,
        "ZOrder must describe the geometry ordinal derived from ChildItems");
    const auto owner = [](std::uint64_t id) { return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{id}}; };
    for (std::uint64_t id = 10; id < 22; ++id) {
        expect(document.set_prop_val(owner(id), z, std::int64_t{0}) == PropertyMutationStatus::applied &&
                document.get_prop_val(owner(id), z).value == PropertyValue{std::int64_t{1}},
            "ZOrder below range must move each control to the first position");
        for (std::uint64_t sibling = 10; sibling < 22; ++sibling)
            expect(document.get_prop_val(owner(sibling), tab).value == PropertyValue{static_cast<std::int64_t>(sibling - 9)},
                "ZOrder must preserve every sibling TabOrder");
    }
    expect(document.set_prop_val(owner(10), z, std::int64_t{99999}) == PropertyMutationStatus::applied &&
            document.get_prop_val(owner(10), z).value == PropertyValue{std::int64_t{12}},
        "ZOrder above range must move the control to the last position");
    expect(document.set_prop_val(owner(10), z, DecimalValue{"1.5"}) == PropertyMutationStatus::applied &&
            document.get_prop_val(owner(10), z).value == PropertyValue{std::int64_t{1}},
        "fractional ZOrder must truncate");
    expect(document.set_prop_val(owner(10), z, std::int64_t{1}) == PropertyMutationStatus::applied &&
        document.reset_prop_val(owner(10), z) == PropertyMutationStatus::unsupported_surface &&
        document.get_prop_val(owner(10), z).value == PropertyValue{std::int64_t{1}},
        "same ZOrder is applied; no scalar default is invented for reset");
    expect(document.set_prop_val(owner(10), z, std::string{"1"}) == PropertyMutationStatus::invalid_value,
        "ZOrder must reject nonnumeric values");

    Form nested_form; nested_form.id = ObjectId{1000}; nested_form.name = "NestedZOrder";
    OrdinaryFormDocument nested(std::move(nested_form));
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId panel_id{1100 + index}, page_id{1200 + index};
        const ObjectId first_id{1300 + 2 * index}, second_id{1301 + 2 * index};
        Page page; page.id = page_id; page.name = "Page" + std::to_string(index);
        page.children = {ControlRef{first_id}, ControlRef{second_id}};
        ControlNode panel{panel_id, "Panel" + std::to_string(index), PanelPayload{}};
        panel.children = {PageRef{page_id}};
        nested.add_page(std::move(page));
        nested.add_control(std::move(panel));
        nested.add_control(ControlNode{first_id, "First" + std::to_string(index), LabelDecorationPayload{}});
        nested.add_control(ControlNode{second_id, "Second" + std::to_string(index), LabelDecorationPayload{}});
        expect(nested.set_prop_val({ObjectCategory::control, second_id}, z, std::int64_t{1}) == PropertyMutationStatus::applied &&
                nested.get_prop_val({ObjectCategory::control, first_id}, z).value == PropertyValue{std::int64_t{2}} &&
                nested.get_prop_val({ObjectCategory::control, second_id}, z).value == PropertyValue{std::int64_t{1}} &&
                nested.get_prop_val({ObjectCategory::control, first_id}, tab).value == PropertyValue{std::int64_t{1}} &&
                nested.get_prop_val({ObjectCategory::control, second_id}, tab).value == PropertyValue{std::int64_t{2}},
            "nested ZOrder must reorder only the page siblings and preserve their TabOrder");
    }

    Form sequence_form; sequence_form.id = ObjectId{2000}; sequence_form.name = "ZOrderSequence";
    OrdinaryFormDocument sequence(std::move(sequence_form));
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId panel_id{2100 + index}, page_id{2200 + index};
        const std::array<ObjectId, 4> ids{ObjectId{2300 + 4 * index}, ObjectId{2301 + 4 * index},
                                         ObjectId{2302 + 4 * index}, ObjectId{2303 + 4 * index}};
        Page page; page.id = page_id; page.name = "Page" + std::to_string(index);
        for (const auto id : ids) page.children.push_back(ControlRef{id});
        ControlNode panel{panel_id, "Panel" + std::to_string(index), PanelPayload{}};
        panel.children = {PageRef{page_id}};
        sequence.add_page(std::move(page));
        sequence.add_control(std::move(panel));
        for (const auto id : ids)
            sequence.add_control(ControlNode{id, "Label" + std::to_string(id.value()), LabelDecorationPayload{}});
        const auto check_order = [&](const std::array<std::int64_t, 4>& expected) {
            for (std::size_t item = 0; item < ids.size(); ++item)
                expect(sequence.get_prop_val({ObjectCategory::control, ids[item]}, z).value == PropertyValue{expected[item]},
                    "nested ZOrder sequence must match platform sibling positions");
        };
        const auto move = [&](std::size_t item, std::int64_t target) {
            expect(sequence.set_prop_val({ObjectCategory::control, ids[item]}, z, target) ==
                    PropertyMutationStatus::applied,
                "nested ZOrder assignment must succeed");
        };
        check_order({1, 2, 3, 4});
        move(3, 1); check_order({2, 3, 4, 1});
        move(1, 4); check_order({2, 4, 3, 1});
        move(2, 2); check_order({3, 4, 2, 1});
        move(0, 99); check_order({4, 3, 2, 1});
        move(1, -1); check_order({4, 1, 3, 2});
    }
}

void test_tab_order_reorders_siblings() {
    const std::array<ControlPayload, 12> payloads{
        CalendarFieldPayload{}, ChoiceFieldPayload{}, CommandBarPayload{}, CheckBoxPayload{},
        RadioButtonPayload{}, InputFieldPayload{}, ListBoxPayload{}, UsualGroupPayload{},
        ProgressBarPayload{}, TrackBarPayload{}, LabelDecorationPayload{}, PanelPayload{}};
    Form form; form.id = ObjectId{1}; form.name = "TabOrder";
    for (std::uint64_t id = 10; id < 22; ++id) form.children.push_back(ControlRef{ObjectId{id}});
    OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < payloads.size(); ++index)
        document.add_control(ControlNode{ObjectId{10 + index}, "Control" + std::to_string(index), payloads[index]});
    const auto property = PropertyId::from_name("TabOrder");
    const auto owner = [](std::uint64_t id) { return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{id}}; };
    for (std::uint64_t id = 10; id < 22; ++id)
        expect(document.get_prop_val(owner(id), property).value == PropertyValue{static_cast<std::int64_t>(id - 9)},
            "implicit order must follow sibling order");
    expect(document.set_prop_val(owner(14), property, DecimalValue{"1.5"}) == PropertyMutationStatus::applied &&
            document.get_prop_val(owner(14), property).value == PropertyValue{std::int64_t{1}} &&
            document.get_prop_val(owner(10), property).value == PropertyValue{std::int64_t{2}},
        "fractional TabOrder must truncate and shift siblings");
    expect(document.set_prop_val(owner(14), property, std::int64_t{12}) == PropertyMutationStatus::applied &&
            document.get_prop_val(owner(14), property).value == PropertyValue{std::int64_t{12}} &&
            document.get_prop_val(owner(10), property).value == PropertyValue{std::int64_t{1}},
        "moving TabOrder later must shift the intervening siblings");
    for (const auto invalid : {std::int64_t{-1}, std::int64_t{0}, std::int64_t{13}})
        expect(document.set_prop_val(owner(14), property, invalid) == PropertyMutationStatus::applied &&
                document.get_prop_val(owner(14), property).value == PropertyValue{std::int64_t{12}},
            "out-of-range TabOrder must leave order unchanged");
    expect(document.set_prop_val(owner(14), property, std::string{"1"}) == PropertyMutationStatus::invalid_value,
        "TabOrder must reject a nonnumeric value");
    expect(document.reset_prop_val(owner(14), property) == PropertyMutationStatus::reset &&
            document.get_prop_val(owner(14), property).value == PropertyValue{std::int64_t{5}},
        "reset must restore the child ordinal and keep sibling order valid");
    for (std::uint64_t id = 10; id < 22; ++id)
        expect(document.get_prop_val(owner(id), property).value == PropertyValue{static_cast<std::int64_t>(id - 9)},
            "reset must restore the sibling permutation");

    auto invalid_graph = document;
    auto* invalid_control = const_cast<ControlNode*>(invalid_graph.find_control(ObjectId{11}));
    invalid_control->position.tab_order.set(std::optional<std::int32_t>{1});
    expect(invalid_graph.set_prop_val(owner(12), property, std::int64_t{1}) == PropertyMutationStatus::unsupported_surface &&
            invalid_control->position.tab_order.value() == std::optional<std::int32_t>{1},
        "an ambiguous sibling permutation must be rejected without mutation");

    Form nested_form; nested_form.id = ObjectId{1}; nested_form.name = "NestedTabOrder";
    nested_form.children = {ControlRef{ObjectId{2}}, ControlRef{ObjectId{3}}};
    OrdinaryFormDocument nested(std::move(nested_form));
    ControlNode panel{ObjectId{2}, "Panel", PanelPayload{}};
    panel.children = {PageRef{ObjectId{4}}, PageRef{ObjectId{5}}};
    nested.add_control(std::move(panel)); nested.add_control(ControlNode{ObjectId{3}, "Outside", ButtonPayload{}});
    Page first; first.id = ObjectId{4}; first.name = "First"; first.children = {ControlRef{ObjectId{6}}, ControlRef{ObjectId{7}}};
    Page second; second.id = ObjectId{5}; second.name = "Second"; second.children = {ControlRef{ObjectId{8}}, ControlRef{ObjectId{9}}};
    nested.add_page(std::move(first)); nested.add_page(std::move(second));
    for (std::uint64_t id = 6; id <= 9; ++id) nested.add_control(ControlNode{ObjectId{id}, "Child" + std::to_string(id), ButtonPayload{}});
    expect(nested.set_prop_val(owner(7), property, std::int64_t{1}) == PropertyMutationStatus::applied &&
            nested.get_prop_val(owner(6), property).value == PropertyValue{std::int64_t{2}} &&
            nested.get_prop_val(owner(8), property).value == PropertyValue{std::int64_t{1}} &&
            nested.get_prop_val(owner(3), property).value == PropertyValue{std::int64_t{2}},
        "tab order changes must remain inside the owning page");
}

void test_label_picture_api_access() {
    using namespace oof::model::metamodel;
    const auto* descriptor = find_property(ControlKind::label_decoration, "Picture");
    expect(descriptor && descriptor->api_access == ApiAccess::read_write &&
            descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == StorageCodec::picture_record,
        "Label Picture must use the named picture storage codec");
    Form form;
    form.id = ObjectId{1};
    form.name = "LabelPictures";
    OrdinaryFormDocument document(std::move(form));
    const PictureRef picture{PictureAssetRef{}, QualifiedName{"PictureLib.Write"}};
    for (std::size_t index = 0; index < 11; ++index) {
        const ObjectId id{100 + index};
        document.add_control(ControlNode{id, "Label" + std::to_string(index), LabelDecorationPayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        expect(document.set_prop_val(owner, PropertyId::from_name("Picture"), picture) ==
                PropertyMutationStatus::applied &&
                document.get_prop_val(owner, PropertyId::from_name("Picture")).value == PropertyValue{picture},
            "Label Picture API must accept the verified library picture");
    }
}

void test_chart_scale_lines() {
    Form form; form.id = ObjectId{1}; form.name = "ChartScaleLines";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.push_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t i = 0; i < 12; ++i) document.add_control(ControlNode{ObjectId{100 + i}, "Chart" + std::to_string(i), ChartPayload{}});
    const auto id = PropertyId::from_name("ScaleLines");
    const auto families = metamodel::line_family_descriptors();
    for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey plot{ObjectCategory::control, ObjectId{100 + i}, ChartAreaKind::plot};
        const auto initial = document.get_prop_val(plot, id);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value == PropertyValue{LineValue{}}, "line default");
        const auto& family = families[i % families.size()];
        LineValue line{{std::string(family.name), std::string(family.members[i % family.members.size()])}, -1, family.name == "SpreadsheetDocumentCellLineType"};
        expect(document.set_prop_val(plot, id, line) == PropertyMutationStatus::applied_unclassified_storage, "set scale line");
        const auto before = oof::source::serialize_form_xml(document);
        expect(before.ok(), "valid line serializes");
        auto invalid = line; invalid.type.member = "Unknown";
        expect(document.set_prop_val(plot, id, invalid) == PropertyMutationStatus::invalid_value, "invalid line rejected");
        const auto after = oof::source::serialize_form_xml(document);
        expect(after.ok() && before.value() == after.value(), "invalid line preserves document");
        expect(document.reset_prop_val(plot, id) == PropertyMutationStatus::reset_unclassified_storage && document.get_prop_val(plot, id).value == PropertyValue{LineValue{}}, "reset line default");
        const OrdinaryFormDocument::ObjectKey title{ObjectCategory::control, ObjectId{100 + i}, ChartAreaKind::title};
        expect(document.set_prop_val(title, id, line) == PropertyMutationStatus::unknown_property, "scale line belongs to plot only");
    }
    Form bad; bad.id = ObjectId{1}; bad.name = "BadArea"; bad.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument invalid(std::move(bad));
    ChartPayload payload; payload.title_area.emplace(); payload.title_area->scale_lines = LineValue{};
    invalid.add_control(ControlNode{ObjectId{2}, "Chart", std::move(payload)});
    expect(!invalid.validate().ok(), "direct wrong area line must fail");
}

void test_chart_owned_collections() {
    using namespace oof::model::metamodel;
    for (const auto& [name, codec] : {std::pair{"PlotArea", ValueCodec::chart_plot_area}, std::pair{"TitleArea", ValueCodec::chart_title_area}, std::pair{"LegendArea", ValueCodec::chart_legend_area}}) {
        const auto* descriptor = find_property(ControlKind::chart, name);
        expect(descriptor && descriptor->value_kind == ValueKind::object && descriptor->api_access == ApiAccess::read_only &&
            descriptor->persistence == PersistenceClass::persisted_readonly && descriptor->storage_codec == StorageCodec::control_info &&
            descriptor->value_codec == codec && descriptor->default_value.kind == DefaultKind::none,
            "root Chart areas expose persisted readonly named object codecs");
    }
    for (const auto& [name, codec] : {std::pair{"Series", ValueCodec::chart_series_collection}, std::pair{"Points", ValueCodec::chart_points_collection},
             std::pair{"GaugeChartQualityBands", ValueCodec::chart_gauge_quality_bands}}) {
        const auto* descriptor = find_property(ControlKind::chart, name);
        expect(descriptor && descriptor->api_access == ApiAccess::read_only && descriptor->persistence == PersistenceClass::persisted_readonly &&
            descriptor->value_kind == ValueKind::collection && descriptor->storage_codec == StorageCodec::collection_record && descriptor->value_codec == codec && descriptor->default_value.kind == DefaultKind::none,
            "Chart collection descriptor must expose its owned collection");
    }
    Form form; form.id = ObjectId{1}; form.name = "OwnedCollections";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.push_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t i = 0; i < 12; ++i) document.add_control(ControlNode{ObjectId{100 + i}, "Chart" + std::to_string(i), ChartPayload{}});
    for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        for (const auto& [name, kind] : {std::pair{"PlotArea", ChartAreaKind::plot}, std::pair{"TitleArea", ChartAreaKind::title}, std::pair{"LegendArea", ChartAreaKind::legend}}) {
            const auto id = PropertyId::from_name(name);
            const auto reference = document.get_prop_val(owner, id);
            expect(reference.status == PropertyReadStatus::computed_value && reference.value == PropertyValue{ChartAreaRef{owner.id, kind}}, "root Chart owned area reference is computed and stable");
            expect(document.set_prop_val(owner, id, *reference.value) == PropertyMutationStatus::read_only &&
                document.set_prop_val(owner, id, UndefinedValue{}) == PropertyMutationStatus::read_only && document.reset_prop_val(owner, id) == PropertyMutationStatus::read_only,
                "root Chart owned areas cannot be replaced or reset");
        }
        const auto series_id = PropertyId::from_name("Series"), points_id = PropertyId::from_name("Points"), bands_id = PropertyId::from_name("GaugeChartQualityBands");
        const auto series = document.get_prop_val(owner, series_id), points = document.get_prop_val(owner, points_id), bands = document.get_prop_val(owner, bands_id);
        expect(series.status == PropertyReadStatus::computed_value && series.value == PropertyValue{ChartCollectionRef{owner.id, ChartCollectionKind::series}} &&
            points.status == PropertyReadStatus::computed_value && points.value == PropertyValue{ChartCollectionRef{owner.id, ChartCollectionKind::points}} &&
            bands.status == PropertyReadStatus::computed_value && bands.value == PropertyValue{ChartCollectionRef{owner.id, ChartCollectionKind::gauge_quality_bands}}, "owned Chart collection references must name owner and kind");
        expect(document.set_prop_val(owner, series_id, *series.value) == PropertyMutationStatus::read_only &&
            document.set_prop_val(owner, points_id, ChartCollectionRef{ObjectId{999}, ChartCollectionKind::points}) == PropertyMutationStatus::read_only &&
            document.reset_prop_val(owner, points_id) == PropertyMutationStatus::read_only &&
            document.set_prop_val(owner, bands_id, ChartCollectionRef{owner.id, ChartCollectionKind::gauge_quality_bands}) == PropertyMutationStatus::read_only &&
            document.reset_prop_val(owner, bands_id) == PropertyMutationStatus::read_only, "collection assignment and reset must be readonly");
        for (const auto& [series_count, point_count] : {std::pair{3, 4}, std::pair{0, 0}, std::pair{2, 3}}) {
            expect(document.set_prop_val(owner, PropertyId::from_name("SeriesCount"), std::int64_t{series_count}) == PropertyMutationStatus::applied &&
                document.set_prop_val(owner, PropertyId::from_name("PointCount"), std::int64_t{point_count}) == PropertyMutationStatus::applied, "collection resize must succeed");
            expect(document.get_prop_val(owner, series_id).value == series.value && document.get_prop_val(owner, points_id).value == points.value, "saved collection references must remain stable");
            const auto* node = document.find_control(std::get<ChartCollectionRef>(*series.value).owner);
            const auto& chart = std::get<ChartPayload>(node->payload);
            expect(chart.series.size() == static_cast<std::size_t>(series_count) && chart.points.size() == static_cast<std::size_t>(point_count), "saved references resolve current collections");
        }
        expect(document.reset_prop_val(owner, PropertyId::from_name("SeriesCount")) == PropertyMutationStatus::reset &&
            document.reset_prop_val(owner, PropertyId::from_name("PointCount")) == PropertyMutationStatus::reset,
            "classified chart counters reset their owned collections successfully");
    }
    expect(document.get_prop_val({ObjectCategory::control, ObjectId{999}}, PropertyId::from_name("Series")).status == PropertyReadStatus::unknown_owner,
        "missing Chart owner must fail");
    Form bad; bad.id = ObjectId{1}; bad.name = "BadCollection"; bad.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument invalid(std::move(bad));
    ControlNode control{ObjectId{2}, "Chart", ChartPayload{}};
    control.properties().set_explicit(PropertyId::from_name("Series"), ChartCollectionRef{ObjectId{999}, ChartCollectionKind::series});
    invalid.add_control(std::move(control));
    expect(!invalid.validate().ok(), "direct collection property bag must fail");
    for (const auto name : {"PlotArea", "TitleArea", "LegendArea"}) {
        Form bad_area; bad_area.id = ObjectId{1}; bad_area.name = "BadAreaReference"; bad_area.children = {ControlRef{ObjectId{2}}};
        OrdinaryFormDocument bad_document(std::move(bad_area));
        ControlNode bad_control{ObjectId{2}, "Chart", ChartPayload{}};
        bad_control.properties().set_explicit(PropertyId::from_name(name), ChartAreaRef{ObjectId{2}, ChartAreaKind::plot});
        bad_document.add_control(std::move(bad_control));
        expect(!bad_document.validate().ok(), "computed root Chart area references cannot be duplicated in scalar bags");
    }
    Form invalid_form; invalid_form.id = ObjectId{1}; invalid_form.name = "InvalidGaugeBand"; invalid_form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument invalid_band_document(std::move(invalid_form));
    ChartPayload invalid_payload;
    ChartGaugeQualityBand invalid_band; invalid_band.begin = DecimalValue{"2"}; invalid_band.end = DecimalValue{"1"};
    invalid_payload.gauge_quality_bands.push_back(invalid_band);
    invalid_band_document.add_control(ControlNode{ObjectId{2}, "Gauge", std::move(invalid_payload)});
    expect(!invalid_band_document.validate().ok(), "direct model rejects gauge quality bands whose Begin exceeds End");
    Form shadow_form; shadow_form.id = ObjectId{1}; shadow_form.name = "BandShadow";
    shadow_form.children.emplace_back(ControlRef{ObjectId{2}});
    OrdinaryFormDocument clean_shadow(shadow_form);
    ControlNode shadow_control{ObjectId{2}, "Gauge", ChartPayload{}};
    clean_shadow.add_control(shadow_control);
    expect(clean_shadow.validate().ok(), "scalar-shadow negative fixture must start valid");
    shadow_control.properties().set_explicit(PropertyId::from_name("GaugeChartQualityBands"),
        ChartCollectionRef{ObjectId{2}, ChartCollectionKind::gauge_quality_bands});
    OrdinaryFormDocument invalid_shadow(std::move(shadow_form));
    invalid_shadow.add_control(std::move(shadow_control));
    expect(!invalid_shadow.validate().ok(), "owned gauge bands cannot be duplicated as a scalar collection reference");

}

void test_chart_computed_counts() {
    Form form; form.id = ObjectId{1}; form.name = "ChartCounts";
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t i = 0; i < 12; ++i) {
        ChartPayload chart;
        for (std::uint64_t n = 0; n < i % 3; ++n) { ChartSeries s; s.id = ObjectId{n + 1}; chart.series.push_back(s); }
        for (std::uint64_t n = 0; n < i % 4; ++n) { ChartPoint p; p.id = ObjectId{n + 1}; chart.points.push_back(p); }
        document.add_control(ControlNode{ObjectId{100 + i}, "Chart" + std::to_string(i), std::move(chart)});
    }
    for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        const auto active = PropertyId::from_name("ActiveSeries");
        if (i % 3 == 0) {
            expect(document.get_prop_val(owner, active).value == PropertyValue{UndefinedValue{}},
                "empty chart active-series value must be undefined");
        } else {
            expect(document.get_prop_val(owner, active).value == PropertyValue{ChartSeriesRef{ObjectId{100 + i}, ObjectId{1}}},
                "first chart series must be active by default");
            const ChartSeriesRef selected{ObjectId{100 + i}, ObjectId{i % 3}};
            expect(document.set_prop_val(owner, active, selected) == PropertyMutationStatus::applied &&
                document.get_prop_val(owner, active).value == PropertyValue{selected}, "active series must retain its owner and identity");
            expect(document.set_prop_val(owner, active, ChartSeriesRef{ObjectId{999}, ObjectId{1}}) == PropertyMutationStatus::invalid_reference &&
                document.set_prop_val(owner, active, ChartSeriesRef{ObjectId{100 + i}, ObjectId{999}}) == PropertyMutationStatus::invalid_reference &&
                document.set_prop_val(owner, active, UndefinedValue{}) == PropertyMutationStatus::invalid_value,
                "foreign, missing and undefined series must be rejected");
        }
        for (const auto& [name, expected] : {std::pair{"SeriesCount", i % 3}, std::pair{"PointCount", i % 4}}) {
            const auto property = PropertyId::from_name(name);
            const auto result = document.get_prop_val(owner, property);
            expect(result.status == PropertyReadStatus::computed_value && result.value == PropertyValue{static_cast<std::int64_t>(expected)},
                "chart counts must reflect named series and points");

        }
    }
}

void test_chart_active_point_reference() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    Form form; form.id = ObjectId{1}; form.name = "ActivePoint";
    OrdinaryFormDocument document(std::move(form));
    const auto active_id = PropertyId::from_name("ActivePoint");
    const auto* descriptor = find_property(ControlKind::chart, active_id);
    expect(descriptor && descriptor->api_access == ApiAccess::read_write &&
        descriptor->persistence == PersistenceClass::persisted_editable &&
        descriptor->storage_codec == StorageCodec::control_info &&
        descriptor->value_codec == ValueCodec::chart_point_reference &&
        descriptor->default_value.kind == DefaultKind::none,
        "Chart ActivePoint is a persisted named point reference with no scalar default");

    for (std::uint64_t i = 0; i < 12; ++i) {
        ChartPayload chart;
        for (std::uint64_t j = 0; j < i % 5; ++j) {
            ChartPoint point; point.id = ObjectId{10 + j * 10}; chart.points.push_back(std::move(point));
        }
        document.add_control(ControlNode{ObjectId{100 + i}, "Chart" + std::to_string(i), std::move(chart)});
    }

    for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        const auto& chart = std::get<ChartPayload>(document.find_control(owner.id)->payload);
        const auto initial = document.get_prop_val(owner, active_id);
        if (chart.points.empty()) {
            expect(initial.status == PropertyReadStatus::computed_value && initial.value == PropertyValue{UndefinedValue{}},
                "Chart ActivePoint is Undefined when there are no points");
            continue;
        }
        const ChartPointRef first{owner.id, chart.points.front().id};
        expect(initial.status == PropertyReadStatus::computed_value && initial.value == PropertyValue{first},
            "Chart ActivePoint defaults to the first named point");
        const ChartPointRef selected{owner.id, chart.points.back().id};
        auto selected_form = document.form();
        selected_form.children = {ControlRef{owner.id}};
        OrdinaryFormDocument selected_document(std::move(selected_form));
        auto selected_control = *document.find_control(owner.id);
        selected_control.properties().set_explicit(active_id, selected);
        selected_document.add_control(std::move(selected_control));
        expect(selected_document.get_prop_val(owner, active_id).status == PropertyReadStatus::explicit_value &&
            selected_document.get_prop_val(owner, active_id).value == PropertyValue{selected},
            "explicit Chart ActivePoint preserves the selected point identity");
        expect(selected_document.set_prop_val(owner, active_id, selected) == PropertyMutationStatus::unsupported_surface &&
            selected_document.set_prop_val(owner, active_id, UndefinedValue{}) == PropertyMutationStatus::unsupported_surface &&
            selected_document.reset_prop_val(owner, active_id) == PropertyMutationStatus::unsupported_surface &&
            selected_document.get_prop_val(owner, active_id).value == PropertyValue{selected},
            "ActivePoint API mutations remain unsupported and do not alter stored state");
        if (chart.points.size() > 1) {
            expect(selected_document.set_prop_val(owner, PropertyId::from_name("PointCount"), std::int64_t{1}) == PropertyMutationStatus::invalid_reference &&
                selected_document.get_prop_val(owner, active_id).value == PropertyValue{selected},
                "PointCount shrink that would remove explicit ActivePoint is rejected atomically");
        }
    }

    Form valid_form; valid_form.id = ObjectId{1}; valid_form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument valid(std::move(valid_form));
    ChartPayload payload; ChartPoint point; point.id = ObjectId{10}; payload.points.push_back(point);
    ControlNode control{ObjectId{2}, "Chart", std::move(payload)};
    control.properties().set_explicit(active_id, UndefinedValue{});
    valid.add_control(std::move(control));
    expect(valid.validate().ok(), "explicit Undefined is a valid persisted ActivePoint state");

    for (const ChartPointRef invalid : {ChartPointRef{ObjectId{99}, ObjectId{10}}, ChartPointRef{ObjectId{2}, ObjectId{99}}}) {
        Form invalid_form; invalid_form.id = ObjectId{1}; invalid_form.children = {ControlRef{ObjectId{2}}};
        OrdinaryFormDocument invalid_document(std::move(invalid_form));
        ChartPayload invalid_payload; invalid_payload.points.push_back(point);
        ControlNode invalid_control{ObjectId{2}, "Chart", std::move(invalid_payload)};
        invalid_control.properties().set_explicit(active_id, invalid);
        invalid_document.add_control(std::move(invalid_control));
        expect(invalid_document.validate().has(InvariantCode::invalid_property),
            "foreign owner and missing Chart ActivePoint identities fail model validation");
    }
}

void test_form_style_property() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    const auto* descriptor = find_form_property("Style");
    expect(descriptor && descriptor->value_kind == ValueKind::object &&
        descriptor->value_codec == ValueCodec::style &&
        descriptor->persistence == PersistenceClass::persisted_editable &&
        descriptor->storage_codec == StorageCodec::root_record &&
        descriptor->default_value.kind == DefaultKind::style && descriptor->default_value.canonical == "auto",
        "Form.Style is a persisted root style value with Auto default");

    Form form; form.id = ObjectId{1}; form.name = "Styled";
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto property = PropertyId::from_name("Style");
    expect(document.get_prop_val(owner, property).value == PropertyValue{StyleValue{}},
        "Form.Style defaults to Auto");
    const StyleValue custom{UuidValue{"21436500-0000-4000-8000-000000000001"}};
    expect(document.set_prop_val(owner, property, custom) == PropertyMutationStatus::applied &&
        document.get_prop_val(owner, property).value == PropertyValue{custom} && document.validate().ok(),
        "Form.Style accepts and validates a named configuration style UUID");
    expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset &&
        document.get_prop_val(owner, property).value == PropertyValue{StyleValue{}},
        "Form.Style resets to Auto");
    for (const PropertyValue& invalid : {PropertyValue{UuidValue{"21436500-0000-4000-8000-000000000001"}},
             PropertyValue{StyleValue{UuidValue{"00000000-0000-0000-0000-000000000000"}}},
             PropertyValue{StyleValue{UuidValue{"not-a-uuid"}}}}) {
        expect(document.set_prop_val(owner, property, invalid) == PropertyMutationStatus::invalid_value &&
            document.get_prop_val(owner, property).value == PropertyValue{StyleValue{}},
            "Form.Style rejects wrong value types, zero UUIDs, and malformed UUIDs without mutation");
    }
}

void test_chart_source_data_mode() {
    Form form; form.id = ObjectId{1}; form.name = "SourceCharts";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t i = 0; i < 12; ++i) {
        ChartPayload chart;
        chart.data_mode = ChartDataMode::source;
        chart.data_source_description = "Источник " + std::to_string(i);
        for (std::uint64_t j = 0; j < 300; ++j) {
            ChartSeries series; series.id = ObjectId{j + 2}; chart.series.push_back(std::move(series));
            ChartPoint point; point.id = ObjectId{j + 1}; chart.points.push_back(std::move(point));
        }
        document.add_control(ControlNode{ObjectId{100 + i}, "Source" + std::to_string(i), std::move(chart)});
    }
    expect(document.validate().ok(), "Source mode permits varied dimensions without dense Values cells");
    const auto source_id = PropertyId::from_name("DataSource");
    const auto* source = metamodel::find_property(ControlKind::chart, source_id);
    expect(source && source->api_access == metamodel::ApiAccess::write_only &&
        source->persistence == metamodel::PersistenceClass::runtime_only &&
        source->storage_codec == metamodel::StorageCodec::none &&
        source->default_value.kind == metamodel::DefaultKind::none &&
        source->value_codec == metamodel::ValueCodec::unclassified,
        "live Chart DataSource does not persist, while named Source mode does; reference codec remains unfinished");
    for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        expect(document.get_prop_val(owner, source_id).status == PropertyReadStatus::write_only,
            "Chart source cannot expose a manufactured scalar getter");
        expect(document.set_prop_val(owner, source_id, UndefinedValue{}) == PropertyMutationStatus::unsupported_surface &&
            document.reset_prop_val(owner, source_id) == PropertyMutationStatus::unsupported_surface,
            "unfinished live source operations must not silently detach or materialize a source");
        expect(!document.find_control(owner.id)->properties().contains(source_id) &&
            std::get<ChartPayload>(document.find_control(owner.id)->payload).data_mode == ChartDataMode::source,
            "rejected live source operations preserve the named persisted mode");
    }

    for (std::uint64_t i = 0; i < 12; ++i) {
        const ObjectId owner{100 + i};
        const auto* chart = std::get_if<ChartPayload>(&document.find_control(owner)->payload);
        expect(chart && chart->data_mode == ChartDataMode::source && chart->values.empty() && chart->series.size() == 300 && chart->points.size() == 300,
            "Source mode retains dimensions and description without allocating matrix cells");
        const OrdinaryFormDocument::ObjectKey key{ObjectCategory::control, owner};
        const auto same_count = document.set_prop_val(key, PropertyId::from_name("SeriesCount"), std::int64_t{300});
        const auto series = document.set_prop_val(key, PropertyId::from_name("SeriesCount"), std::string{"invalid"});
        const auto points = document.set_prop_val(key, PropertyId::from_name("PointCount"), DecimalValue{"invalid"});
        expect(same_count == PropertyMutationStatus::read_only && series == PropertyMutationStatus::read_only && points == PropertyMutationStatus::read_only &&
            document.reset_prop_val(key, PropertyId::from_name("SeriesCount")) == PropertyMutationStatus::read_only &&
            document.reset_prop_val(key, PropertyId::from_name("PointCount")) == PropertyMutationStatus::read_only,
            "Source-mode Chart counts remain read-only for all assigned values");
        const auto* unchanged = std::get_if<ChartPayload>(&document.find_control(owner)->payload);
        expect(unchanged && unchanged->series.size() == 300 && unchanged->points.size() == 300,
            "read-only Source count operations do not mutate collection sizes");
    }
    Form invalid_form; invalid_form.id = ObjectId{1}; invalid_form.name = "DescriptionWithoutSource"; invalid_form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument invalid(std::move(invalid_form));
    ChartPayload invalid_chart; invalid_chart.data_source_description = "only valid in Source mode";
    invalid.add_control(ControlNode{ObjectId{2}, "Chart", std::move(invalid_chart)});
    expect(!invalid.validate().ok(), "nonempty source description requires Source mode");
}

void test_list_and_html_live_property_boundaries() {
    Form form; form.id = ObjectId{1}; form.name = "LiveObjects";
    for (std::uint64_t i = 0; i < 12; ++i) {
        form.children.emplace_back(ControlRef{ObjectId{100 + i}});
        form.children.emplace_back(ControlRef{ObjectId{200 + i}});
    }
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t i = 0; i < 12; ++i) {
        for (const bool html : {false, true}) {
            const ObjectId id{(html ? 200U : 100U) + i};
            const auto kind = html ? ControlKind::html_document_field : ControlKind::list_box;
            if (html) document.add_control(ControlNode{id, "Html" + std::to_string(i), HtmlDocumentFieldPayload{}});
            else document.add_control(ControlNode{id, "List" + std::to_string(i), ListBoxPayload{}});
            const auto property = PropertyId::from_name(html ? "Document" : "CurrentRow");
            const auto* descriptor = metamodel::find_property(kind, property);
            const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
            if (html) {
                expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
                    descriptor->storage_codec == metamodel::StorageCodec::none &&
                    descriptor->value_codec == metamodel::ValueCodec::unclassified &&
                    descriptor->default_value.kind == metamodel::DefaultKind::none,
                    "HTML DOM remains an unclassified runtime-only property");
                expect(document.get_prop_val(owner, property).status == PropertyReadStatus::unsupported_surface &&
                    document.set_prop_val(owner, property, UndefinedValue{}) == PropertyMutationStatus::read_only &&
                    document.reset_prop_val(owner, property) == PropertyMutationStatus::read_only,
                    "HTML DOM getter remains unsupported and mutation remains read-only");
            } else {
                expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
                    descriptor->storage_codec == metamodel::StorageCodec::none &&
                    descriptor->value_codec == metamodel::ValueCodec::value_list_item_reference &&
                    descriptor->value_kind == metamodel::ValueKind::variant &&
                    descriptor->default_value.kind == metamodel::DefaultKind::undefined,
                    "ListBox CurrentRow uses its named runtime reference codec and Undefined default");
                const auto result = document.get_prop_val(owner, property);
                expect(result.status == PropertyReadStatus::proven_default && result.value == PropertyValue{UndefinedValue{}},
                    "a known unbound ListBox starts with proven Undefined CurrentRow");
                expect(document.set_prop_val(owner, property, UndefinedValue{}) == PropertyMutationStatus::invalid_value &&
                    document.reset_prop_val(owner, property) == PropertyMutationStatus::reset_runtime_only,
                    "CurrentRow rejects Undefined assignment and runtime reset restores its default");
            }
            expect(document.find_control(id)->properties().find(property) == nullptr,
                "live-property operations must not mutate persisted properties");
        }
    }
    const auto* html = metamodel::find_property(ControlKind::html_document_field, PropertyId::from_name("Document"));
    expect(html->platform_type == u8"ВнешнийОбъект", "Linux HTML DOM must use the current platform type");
}

void test_list_box_current_row_runtime_model() {
    Form form; form.id = ObjectId{1}; form.name = "ListRows";
    for (std::uint64_t id = 100; id < 112; ++id) form.children.emplace_back(ControlRef{ObjectId{id}});
    form.children.emplace_back(ControlRef{ObjectId{122}});
    TypeDomainEntry list_type; list_type.term = TypeDomainTerm::value_list;
    const TypeDomainPatternValue value_list_type{{list_type}};
    Attribute source_attribute{ObjectId{900}, "Source", value_list_type};
    Attribute other_attribute{ObjectId{901}, "Other", value_list_type};
    OrdinaryFormDocument document(std::move(form));
    document.add_attribute(source_attribute);
    document.add_attribute(other_attribute);

    const auto current_row = PropertyId::from_name("CurrentRow");
    const auto owner = [](std::uint64_t id) {
        return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{id}};
    };
    const auto add_list = [&](std::uint64_t id, std::uint64_t attribute_id) {
        ControlNode list{ObjectId{id}, "List" + std::to_string(id), ListBoxPayload{}};
        list.data_path = DataPath{AttributeRef{ObjectId{attribute_id}}, {}};
        document.add_control(std::move(list));
    };
    for (std::uint64_t id = 100; id < 112; ++id) add_list(id, 900);
    add_list(122, 901);
    Form unsupported_form; unsupported_form.id = ObjectId{1}; unsupported_form.name = "UnsupportedPaths";
    unsupported_form.children = {ControlRef{ObjectId{120}}, ControlRef{ObjectId{121}}};
    OrdinaryFormDocument unsupported(std::move(unsupported_form));
    unsupported.add_attribute(source_attribute);
    ControlNode unbound{ObjectId{120}, "Unbound", ListBoxPayload{}};
    unsupported.add_control(std::move(unbound));
    ControlNode nested{ObjectId{121}, "Nested", ListBoxPayload{}};
    nested.data_path = DataPath{AttributeRef{ObjectId{900}}, {"Member"}};
    unsupported.add_control(std::move(nested));

    ValueListValue initial;
    initial.items.push_back({7, std::string{"seven"}, std::string{"Seven"}, false, {}});
    initial.items.push_back({11, std::int64_t{11}, std::string{"Eleven"}, true, {}});
    initial.last_assigned_id = 11;
    auto source = ValueListRef::create(initial);
    expect(source && document.bind_attribute_value_list(AttributeRef{ObjectId{900}}, *source) == PropertyMutationStatus::applied_runtime_only,
        "a one-ValueList attribute accepts a live source binding");
    TypeDomainEntry text_type_entry; text_type_entry.term = TypeDomainTerm::string;
    document.add_attribute(Attribute{ObjectId{902}, "Scalar", TypeDomainPatternValue{{text_type_entry}}});
    expect(!document.attribute_value_list(AttributeRef{ObjectId{901}}) &&
        !document.attribute_value_list(AttributeRef{ObjectId{999}}) &&
        document.bind_attribute_value_list(AttributeRef{ObjectId{999}}, *source) == PropertyMutationStatus::unknown_owner &&
        document.bind_attribute_value_list(AttributeRef{ObjectId{902}}, *source) == PropertyMutationStatus::invalid_value,
        "unknown, unbound and non-ValueList attributes do not acquire a source list");

    const auto read_row = [&](std::uint64_t id) {
        const auto result = document.get_prop_val(owner(id), current_row);
        expect((result.status == PropertyReadStatus::computed_value || result.status == PropertyReadStatus::proven_default) && result.value,
            "known ListBox CurrentRow has a selected value or its proven Undefined default");
        return *result.value;
    };
    const auto row7 = ValueListItemRef::create(*source, 7);
    const auto row11 = ValueListItemRef::create(*source, 11);
    expect(row7 && row11 && row7->id() == 7 && row7->is_attached() && row7->get() &&
        std::get<std::string>(row7->get()->value) == "seven" &&
        !ValueListItemRef::create(*source, 8),
        "item references resolve existing rows and expose the attached payload");
    auto reordered = *source->get();
    std::swap(reordered.items.front(), reordered.items.back());
    expect(source->replace(reordered) && row7->is_attached() && row11->is_attached() &&
        ValueListItemRef::create(*source, 7) == row7 && ValueListItemRef::create(*source, 11) == row11,
        "sorting by replacement order preserves identities for surviving numeric IDs");
    std::swap(reordered.items.front(), reordered.items.back());
    expect(source->replace(reordered), "restoring original order leaves the same live source list");
    for (std::uint64_t id = 100; id < 112; ++id)
        expect(read_row(id) == PropertyValue{*row7}, "new nonempty source bindings select their first row");

    expect(document.set_prop_val(owner(100), current_row, *row11) == PropertyMutationStatus::applied_runtime_only &&
        read_row(100) == PropertyValue{*row11} && read_row(101) == PropertyValue{*row7},
        "selection is local to its ListBox and preserves the source row identity");
    expect(document.set_prop_val(owner(100), current_row, *row11) == PropertyMutationStatus::applied_runtime_only &&
        read_row(100) == PropertyValue{*row11},
        "assigning the selected row again remains a valid runtime mutation");
    expect(document.set_prop_val(owner(101), current_row, *row11) == PropertyMutationStatus::applied_runtime_only &&
        document.set_prop_val(owner(102), current_row, *row11) == PropertyMutationStatus::applied_runtime_only,
        "different ListBoxes can independently select the same source row");
    expect(document.reset_prop_val(owner(100), current_row) == PropertyMutationStatus::reset_runtime_only &&
        read_row(100) == PropertyValue{UndefinedValue{}} && read_row(101) == PropertyValue{*row11} &&
        document.attribute_value_list(AttributeRef{ObjectId{900}}) == source &&
        document.set_prop_val(owner(100), current_row, *row11) == PropertyMutationStatus::applied_runtime_only,
        "reset clears only the selected row and leaves its source binding intact");

    ValueListValue foreign_values = initial;
    foreign_values.items[0].id = 11;
    foreign_values.items[1].id = 7;
    auto foreign = ValueListRef::create(foreign_values);
    const auto foreign11 = ValueListItemRef::create(*foreign, 11);
    expect(foreign11 && document.set_prop_val(owner(103), current_row, *foreign11) == PropertyMutationStatus::applied_runtime_only &&
        read_row(103) == PropertyValue{*row11},
        "an attached foreign candidate maps by numeric ID onto the ListBox source");
    auto foreign_missing_values = foreign_values;
    foreign_missing_values.items[0].id = 99;
    foreign_missing_values.items[1].id = 98;
    foreign_missing_values.last_assigned_id = 99;
    auto foreign_missing = ValueListRef::create(foreign_missing_values);
    const auto foreign99 = ValueListItemRef::create(*foreign_missing, 99);
    expect(foreign99 && document.set_prop_val(owner(103), current_row, *foreign99) == PropertyMutationStatus::applied_runtime_only &&
        read_row(103) == PropertyValue{*row11},
        "foreign rows with no matching source ID leave the previous selection intact");

    for (const auto& invalid : {PropertyValue{UndefinedValue{}}, PropertyValue{NullValue{}}, PropertyValue{std::int64_t{7}},
            PropertyValue{std::string{"7"}}, PropertyValue{ValueListItemRef{}}})
        expect(document.set_prop_val(owner(103), current_row, invalid) == PropertyMutationStatus::invalid_value &&
            read_row(103) == PropertyValue{*row11},
            "invalid CurrentRow values are rejected without replacing the prior selection");
    expect(unsupported.set_prop_val(owner(120), current_row, *row7) == PropertyMutationStatus::unsupported_surface &&
        unsupported.get_prop_val(owner(120), current_row).value == PropertyValue{UndefinedValue{}} &&
        unsupported.set_prop_val(owner(121), current_row, *row7) == PropertyMutationStatus::unsupported_surface &&
        unsupported.get_prop_val(owner(121), current_row).value == PropertyValue{UndefinedValue{}},
        "unbound or nested ListBoxes expose Undefined and reject unsupported selection context");
    expect(document.set_prop_val(owner(122), current_row, *row7) == PropertyMutationStatus::unsupported_surface &&
        read_row(122) == PropertyValue{UndefinedValue{}},
        "a valid direct binding still needs a supplied runtime attribute value");
    expect(document.get_prop_val(owner(999), current_row).status == PropertyReadStatus::unknown_owner,
        "unknown controls do not manufacture a CurrentRow context");

    expect(document.set_prop_val(owner(106), PropertyId::from_name("Enabled"), false) == PropertyMutationStatus::applied,
        "persisted ListBox state can coexist with runtime selection");
    const auto xml_before_runtime = oof::source::serialize_form_xml(document);
    const auto bin_before_runtime = oof::save_form_bin(document);
    expect(xml_before_runtime.ok() && bin_before_runtime.ok(), "row selection fixture serializes before runtime changes");
    expect(document.set_prop_val(owner(104), current_row, *row7) == PropertyMutationStatus::applied_runtime_only &&
        document.set_prop_val(owner(105), current_row, *row11) == PropertyMutationStatus::applied_runtime_only,
        "independent selections are established for persistence checks");
    const auto xml_after_runtime = oof::source::serialize_form_xml(document);
    const auto bin_after_runtime = oof::save_form_bin(document);
    expect(xml_after_runtime.ok() && xml_after_runtime.value() == xml_before_runtime.value() &&
        bin_after_runtime.ok() && bin_after_runtime.value() == bin_before_runtime.value(),
        "runtime row selection does not change XML or BIN output");
    const auto cold = oof::source::parse_form_xml(xml_after_runtime.value());
    expect(cold.ok() && cold.value().get_prop_val(owner(104), current_row).value == PropertyValue{UndefinedValue{}},
        "cold source parsing starts with Undefined CurrentRow");

    auto edited = *source->get();
    edited.items.erase(edited.items.begin());
    expect(source->replace(edited) && !row7->is_attached() && row7->id() == 7 && row7->get() &&
        std::get<std::string>(row7->get()->value) == "seven" && read_row(104) == PropertyValue{UndefinedValue{}},
        "removal detaches selected rows while an external reference retains its final payload snapshot");
    const auto row11_again = ValueListItemRef::create(*source, 11);
    edited.items.insert(edited.items.begin(), initial.items.front());
    expect(source->replace(edited) && row11_again && *row11_again == *row11 &&
        read_row(104) == PropertyValue{UndefinedValue{}} && read_row(105) == PropertyValue{*row11_again},
        "reordering preserves surviving row identity while removed rows cannot remain selected");
    const auto recreated7 = ValueListItemRef::create(*source, 7);
    expect(recreated7 && *recreated7 != *row7 && recreated7->is_attached(),
        "reintroducing a removed numeric ID creates a new row identity");

    expect(document.bind_attribute_value_list(AttributeRef{ObjectId{900}}, *source) == PropertyMutationStatus::applied_runtime_only,
        "rebinding even the same source identity is accepted");
    for (std::uint64_t id = 100; id < 112; ++id)
        expect(read_row(id) == PropertyValue{*recreated7},
            "rebinding the shared source resets every directly bound ListBox to its first item");
    expect(document.set_prop_val(owner(100), current_row, *row7) == PropertyMutationStatus::applied_runtime_only &&
        read_row(100) == PropertyValue{*recreated7},
        "a detached candidate cannot replace a selection even when its numeric ID exists again");
    auto empty = ValueListRef::create(ValueListValue{});
    expect(empty && document.bind_attribute_value_list(AttributeRef{ObjectId{900}}, *empty) == PropertyMutationStatus::applied_runtime_only,
        "an empty source can be bound to the ValueList attribute");
    for (std::uint64_t id = 100; id < 112; ++id)
        expect(read_row(id) == PropertyValue{UndefinedValue{}}, "empty source binding clears every ListBox selection");
    expect(document.bind_attribute_value_list(AttributeRef{ObjectId{900}}, *source) == PropertyMutationStatus::applied_runtime_only,
        "a populated source can be rebound to the ValueList attribute");
    for (std::uint64_t id = 100; id < 112; ++id)
        expect(read_row(id) == PropertyValue{*recreated7}, "populated source binding selects its first row in every ListBox");

    OrdinaryFormDocument copied = document;
    const auto copied_ref = copied.get_prop_val(owner(100), current_row);
    expect(copied.attribute_value_list(AttributeRef{ObjectId{900}}) == source &&
        copied_ref.value == PropertyValue{*recreated7} &&
        copied.set_prop_val(owner(100), current_row, *row11_again) == PropertyMutationStatus::applied_runtime_only &&
        document.get_prop_val(owner(100), current_row).value == PropertyValue{*recreated7},
        "document copies share source list identity and keep independent row selections");
    document.set_form(document.form());
    expect(!document.attribute_value_list(AttributeRef{ObjectId{900}}) &&
        document.get_prop_val(owner(100), current_row).value == PropertyValue{UndefinedValue{}} &&
        document.get_prop_val(owner(106), PropertyId::from_name("Enabled")).value == PropertyValue{false},
        "form replacement clears runtime bindings and selections while retaining persisted collections");

    ControlNode shadow{ObjectId{130}, "Shadow", ListBoxPayload{}};
    shadow.properties().set_explicit(current_row, *row7);
    Form shadow_form; shadow_form.id = ObjectId{129}; shadow_form.name = "ShadowCurrentRow";
    shadow_form.children.emplace_back(ControlRef{ObjectId{130}});
    OrdinaryFormDocument shadow_document(std::move(shadow_form));
    shadow_document.add_control(std::move(shadow));
    expect(shadow_document.validate().has(InvariantCode::invalid_property),
        "manually stored CurrentRow shadow is rejected by model validation");
}

void test_control_extension_value_runtime_boundary() {
    Form form; form.id = ObjectId{1}; form.name = "RuntimeValues";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    const auto property = PropertyId::from_name("Value");
    for (std::uint64_t i = 0; i < 12; ++i) {
        const ObjectId id{100 + i};
        if (i % 4 == 0) document.add_control(ControlNode{id, "Input" + std::to_string(i), InputFieldPayload{}});
        else if (i % 4 == 1) document.add_control(ControlNode{id, "Check" + std::to_string(i), CheckBoxPayload{}});
        else if (i % 4 == 2) document.add_control(ControlNode{id, "Choice" + std::to_string(i), ChoiceFieldPayload{}});
        else document.add_control(ControlNode{id, "Radio" + std::to_string(i), RadioButtonPayload{}});
        const auto* control = document.find_control(id);
        const auto* descriptor = metamodel::find_property(control->kind(), property);
        expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
            descriptor->storage_codec == metamodel::StorageCodec::none &&
            descriptor->default_value.kind == metamodel::DefaultKind::none &&
            descriptor->value_kind == metamodel::ValueKind::variant &&
            descriptor->value_codec == metamodel::ValueCodec::unclassified,
            "edited Value is typed by the live control, without persistent storage or a scalar universal default");
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        expect(document.get_prop_val(owner, property).status == PropertyReadStatus::unsupported_surface &&
            document.set_prop_val(owner, property, UndefinedValue{}) == PropertyMutationStatus::unsupported_surface &&
            document.reset_prop_val(owner, property) == PropertyMutationStatus::unsupported_surface &&
            !control->extension_properties.contains(property),
            "unfinished live Value access must not manufacture a default or persist a shadow value");
    }
}

void test_table_current_parent_runtime_boundary() {
    Form form; form.id = ObjectId{1}; form.name = "TableParents";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    const auto property = PropertyId::from_name("CurrentParent");
    const auto* descriptor = metamodel::find_property(ControlKind::table, property);
    expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
        descriptor->storage_codec == metamodel::StorageCodec::none &&
        descriptor->default_value.kind == metamodel::DefaultKind::none &&
        descriptor->value_kind == metamodel::ValueKind::variant &&
        descriptor->value_codec == metamodel::ValueCodec::unclassified,
        "Table CurrentParent is transient and depends on the bound data type, without a universal scalar default");
    for (std::uint64_t i = 0; i < 12; ++i) {
        const ObjectId id{100 + i};
        document.add_control(ControlNode{id, "Table" + std::to_string(i), TablePayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        expect(document.get_prop_val(owner, property).status == PropertyReadStatus::unsupported_surface &&
            document.set_prop_val(owner, property, UndefinedValue{}) == PropertyMutationStatus::unsupported_surface &&
            document.reset_prop_val(owner, property) == PropertyMutationStatus::unsupported_surface &&
            !document.find_control(id)->properties().contains(property),
            "unfinished current parent references must not create a scalar default or stored shadow");
    }
}

void test_geographical_showed_area_runtime_boundary() {
    Form form; form.id = ObjectId{1}; form.name = "GeographicalAreas";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    const auto property = PropertyId::from_name("ShowedArea");
    const auto* descriptor = metamodel::find_property(ControlKind::geographical_schema_field, property);
    expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
        descriptor->storage_codec == metamodel::StorageCodec::none &&
        descriptor->default_value.kind == metamodel::DefaultKind::none &&
        descriptor->value_kind == metamodel::ValueKind::object &&
        descriptor->value_codec == metamodel::ValueCodec::unclassified,
        "ShowedArea is a transient object, without persistent storage or a scalar default");
    for (std::uint64_t i = 0; i < 12; ++i) {
        const ObjectId id{100 + i};
        document.add_control(ControlNode{id, "Geo" + std::to_string(i), GeographicalSchemaFieldPayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        expect(document.get_prop_val(owner, property).status == PropertyReadStatus::unsupported_surface &&
            document.set_prop_val(owner, property, UndefinedValue{}) == PropertyMutationStatus::unsupported_surface &&
            document.reset_prop_val(owner, property) == PropertyMutationStatus::unsupported_surface &&
            !document.find_control(id)->properties().contains(property),
            "unfinished area object must not manufacture undefined or persist a shadow value");
    }
}

void test_graphical_current_item_runtime_boundary() {
    Form form; form.id = ObjectId{1}; form.name = "GraphicalCurrentItems";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    const auto property = PropertyId::from_name("CurrentItem");
    const auto* descriptor = metamodel::find_property(ControlKind::graphical_schema_field, property);
    expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
        descriptor->storage_codec == metamodel::StorageCodec::none &&
        descriptor->default_value.kind == metamodel::DefaultKind::undefined &&
        descriptor->value_codec == metamodel::ValueCodec::unclassified,
        "Graphical CurrentItem has proven transient storage but an unfinished live reference codec");
    for (std::uint64_t i = 0; i < 12; ++i) {
        const ObjectId id{100 + i};
        document.add_control(ControlNode{id, "Graph" + std::to_string(i), GraphicalSchemaFieldPayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        expect(document.get_prop_val(owner, property).status == PropertyReadStatus::unsupported_surface &&
            document.set_prop_val(owner, property, UndefinedValue{}) == PropertyMutationStatus::unsupported_surface &&
            document.reset_prop_val(owner, property) == PropertyMutationStatus::unsupported_surface,
            "unfinished live graphical item API must not fabricate an Undefined scalar or stored shadow");
        expect(!document.find_control(id)->properties().contains(property),
            "unsupported graphical current item operations cannot alter stored properties");
    }
}

void test_pivot_runtime_source_boundaries() {
    Form form; form.id = ObjectId{1}; form.name = "PivotSources";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t i = 0; i < 12; ++i) {
        const ObjectId id{100 + i};
        document.add_control(ControlNode{id, "Pivot" + std::to_string(i), PivotChartPayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        for (const auto name : {"DataSource", "Fields"}) {
            const auto property = PropertyId::from_name(name);
            const auto* descriptor = metamodel::find_property(ControlKind::pivot_chart, property);
            const bool source = name == std::string_view{"DataSource"};
            expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
                descriptor->storage_codec == metamodel::StorageCodec::none &&
                descriptor->value_codec == metamodel::ValueCodec::unclassified &&
                descriptor->default_value.kind == (source ? metamodel::DefaultKind::undefined : metamodel::DefaultKind::none),
                "proven Pivot source persistence must not imply completed query or field references");
            expect(document.get_prop_val(owner, property).status == PropertyReadStatus::unsupported_surface,
                "unfinished Pivot source and field getters must not manufacture scalar substitutes");
            const auto mutation = source ? PropertyMutationStatus::unsupported_surface : PropertyMutationStatus::read_only;
            expect(document.set_prop_val(owner, property, UndefinedValue{}) == mutation &&
                document.reset_prop_val(owner, property) == mutation,
                "Pivot API must retain unfinished source and readonly field boundaries");
            expect(!document.find_control(id)->properties().contains(property),
                "rejected Pivot runtime operations cannot create persisted shadow properties");
        }
    }
}

void test_form_runtime_variant_values() {
    Form form; form.id = ObjectId{1}; form.name = "RuntimeVariants";
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto current = PropertyId::from_name("CurrentControl");
    const auto* current_descriptor = metamodel::find_form_property(current);
    expect(current_descriptor && current_descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
        current_descriptor->storage_codec == metamodel::StorageCodec::none &&
        current_descriptor->value_kind == metamodel::ValueKind::object &&
        current_descriptor->value_codec == metamodel::ValueCodec::unclassified,
        "proven CurrentControl persistence must remain distinct from its unfinished live reference codec");
    expect(document.get_prop_val(owner, current).status == PropertyReadStatus::unsupported_surface &&
        document.set_prop_val(owner, current, UndefinedValue{}) == PropertyMutationStatus::unsupported_surface &&
        document.reset_prop_val(owner, current) == PropertyMutationStatus::unsupported_surface,
        "unfinished CurrentControl API must not manufacture an undefined scalar value");
    const auto form_owner = PropertyId::from_name("FormOwner");
    const auto* owner_descriptor = metamodel::find_form_property(form_owner);
    expect(owner_descriptor && owner_descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
        owner_descriptor->storage_codec == metamodel::StorageCodec::none &&
        owner_descriptor->default_value.kind == metamodel::DefaultKind::undefined &&
        owner_descriptor->value_codec == metamodel::ValueCodec::unclassified,
        "FormOwner persistence and initial undefined must not imply a completed live reference codec");
    expect(document.get_prop_val(owner, form_owner).status == PropertyReadStatus::unsupported_surface &&
        document.set_prop_val(owner, form_owner, FormRef{ObjectId{1}}) == PropertyMutationStatus::unsupported_surface &&
        document.set_prop_val(owner, form_owner, UndefinedValue{}) == PropertyMutationStatus::unsupported_surface &&
        document.reset_prop_val(owner, form_owner) == PropertyMutationStatus::unsupported_surface,
        "unfinished FormOwner API must reject scalar and local-only substitute implementations");
    const auto original_xml = oof::source::serialize_form_xml(document);
    const auto original_bin = oof::save_form_bin(document);
    expect(original_xml.ok() && original_bin.ok(), "form with runtime variants must serialize");
    const std::vector<PropertyValue> values{UndefinedValue{}, NullValue{}, true, std::int64_t{123}, DecimalValue{"123.45"},
        std::string{"Ключ Ω"}, DateValue{"2026-10-27T12:34:56"}, EnumerationValue{"WindowStateVariant", "Float"},
        UuidValue{"22222222-2222-2222-2222-222222222222"}, PlatformTypeValue{"String"},
        ColorValue{}, FontValue{}, BorderValue{}, TypeDomainPatternValue{}};
    for (const auto name : {"UniqueKey", "ChoiceInitialValue"}) {
        const auto property = PropertyId::from_name(name);
        const auto* descriptor = metamodel::find_form_property(property);
        expect(descriptor && descriptor->value_codec == metamodel::ValueCodec::runtime_value &&
            descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
            descriptor->storage_codec == metamodel::StorageCodec::none,
            "runtime form variants use the named runtime-value codec without storage");
        expect(metamodel::value_codec_name(metamodel::ValueCodec::runtime_value) == "runtime-value",
            "runtime-value codec has a stable public descriptor name");
        expect(document.get_prop_val(owner, property).value == PropertyValue{UndefinedValue{}}, "runtime variant must initially be undefined");
        for (const auto& value : values)
            expect(document.set_prop_val(owner, property, value) == PropertyMutationStatus::applied_runtime_only &&
                document.get_prop_val(owner, property).value == value, "runtime variant must retain the exact supported type and value");
        expect(document.set_prop_val(owner, property, DecimalValue{"bad"}) == PropertyMutationStatus::invalid_value &&
            document.set_prop_val(owner, property, ArrayValue{}) == PropertyMutationStatus::unsupported_surface,
            "invalid or unimplemented variant types must not report success");
        expect(document.set_prop_val(owner, property, std::string(1, static_cast<char>(0xff))) ==
            PropertyMutationStatus::invalid_value &&
            document.set_prop_val(owner, property, EnumerationValue{"Type", std::string(1, static_cast<char>(0xff))}) ==
            PropertyMutationStatus::invalid_value,
            "runtime scalar validation must reject malformed UTF-8 without replacing the prior value");
    }
    const auto live = RuntimeCollectionRef::create(RuntimeCollectionKind::structure);
    expect(live && live->set_field("key", std::string{"before"}),
        "runtime value validator accepts an initialized live collection");
    for (const auto name : {"UniqueKey", "ChoiceInitialValue"})
        expect(document.set_prop_val(owner, PropertyId::from_name(name), *live) == PropertyMutationStatus::applied_runtime_only,
            "both runtime form properties accept the same live collection handle");
    auto unique_key = std::get<RuntimeCollectionRef>(*document.get_prop_val(owner, PropertyId::from_name("UniqueKey")).value);
    auto choice_initial = std::get<RuntimeCollectionRef>(*document.get_prop_val(owner, PropertyId::from_name("ChoiceInitialValue")).value);
    expect(unique_key == choice_initial && unique_key.set_field("key", std::string{"after"}) &&
        std::get<std::string>(*choice_initial.field("key")) == "after",
        "runtime form values preserve shared mutable identity");
    const auto xml = oof::source::serialize_form_xml(document);
    const auto bin = oof::save_form_bin(document);
    expect(xml.ok() && xml.value() == original_xml.value() && bin.ok() && bin.value() == original_bin.value(),
        "runtime variants must not change saved forms");
    const auto restored = oof::load_form_bin(bin.value());
    expect(restored.ok() && restored.value().get_prop_val(owner, PropertyId::from_name("UniqueKey")).value == PropertyValue{UndefinedValue{}} &&
        restored.value().get_prop_val(owner, PropertyId::from_name("ChoiceInitialValue")).value == PropertyValue{UndefinedValue{}},
        "restored runtime variants must be undefined");
    document.mark_opening_started();
    expect(document.set_prop_val(owner, PropertyId::from_name("UniqueKey"), NullValue{}) == PropertyMutationStatus::applied_runtime_only &&
        document.get_prop_val(owner, PropertyId::from_name("UniqueKey")).value == PropertyValue{NullValue{}}, "Null must remain distinct from undefined after opening");
    expect(document.reset_prop_val(owner, PropertyId::from_name("UniqueKey")) == PropertyMutationStatus::reset_runtime_only &&
        document.get_prop_val(owner, PropertyId::from_name("UniqueKey")).value == PropertyValue{UndefinedValue{}}, "reset must restore undefined");
}

void test_window_options_aliases() {
    Form form; form.id = ObjectId{1}; form.name = "WindowOptions";
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto name = PropertyId::from_name("WindowOptionsName");
    const auto key = PropertyId::from_name("WindowOptionsKey");
    const auto xml = oof::source::serialize_form_xml(document);
    const auto bin = oof::save_form_bin(document);
    expect(xml.ok() && bin.ok(), "window options form must serialize");
    for (const auto property : {name, key})
        expect(document.get_prop_val(owner, property).value == PropertyValue{std::string{}}, "both window option aliases must initially be empty");
    expect(document.set_prop_val(owner, name, std::string{"имя Ω"}) == PropertyMutationStatus::applied_runtime_only &&
        document.get_prop_val(owner, key).value == PropertyValue{std::string{"имя Ω"}}, "name must update key alias");
    expect(document.set_prop_val(owner, key, std::string{"ключ Ω"}) == PropertyMutationStatus::applied_runtime_only &&
        document.get_prop_val(owner, name).value == PropertyValue{std::string{"ключ Ω"}}, "key must update name alias");
    expect(document.set_prop_val(owner, name, std::int64_t{123}) == PropertyMutationStatus::unsupported_surface &&
        document.get_prop_val(owner, key).value == PropertyValue{std::string{"ключ Ω"}}, "unimplemented locale conversion must not change value");
    const auto changed_xml = oof::source::serialize_form_xml(document);
    const auto changed_bin = oof::save_form_bin(document);
    expect(changed_xml.ok() && changed_xml.value() == xml.value() && changed_bin.ok() && changed_bin.value() == bin.value(),
        "window option aliases must not persist");
    document.mark_opening_started();
    expect(document.set_prop_val(owner, key, std::string{"after"}) == PropertyMutationStatus::applied_runtime_only &&
        document.get_prop_val(owner, name).value == PropertyValue{std::string{"after"}}, "window option aliases remain writable after opening");
    expect(document.set_prop_val(owner, key, UndefinedValue{}) == PropertyMutationStatus::applied_runtime_only &&
        document.get_prop_val(owner, name).value == PropertyValue{std::string{}}, "undefined must convert to the empty string");
    expect(document.set_prop_val(owner, key, NullValue{}) == PropertyMutationStatus::applied_runtime_only &&
        document.get_prop_val(owner, name).value == PropertyValue{std::string{}}, "Null must convert to the empty string");
    expect(document.reset_prop_val(owner, name) == PropertyMutationStatus::reset_runtime_only &&
        document.get_prop_val(owner, key).value == PropertyValue{std::string{}}, "reset must clear the shared alias value");
}

void test_form_runtime_boolean_values() {
    Form form; form.id = ObjectId{1}; form.name = "RuntimeFlags";
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto xml = oof::source::serialize_form_xml(document);
    const auto binary = oof::save_form_bin(document);
    expect(xml.ok() && binary.ok(), "form must serialize before runtime flags change");
    for (const auto name : {"ReadOnly", "MultipleChoice"}) {
        const auto property = PropertyId::from_name(name);
        const auto initial = document.get_prop_val(owner, property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value == PropertyValue{false}, "runtime flag must initially be false");
        expect(document.set_prop_val(owner, property, true) == PropertyMutationStatus::applied_runtime_only &&
            document.get_prop_val(owner, property).value == PropertyValue{true}, "runtime flag must be writable");
        expect(document.set_prop_val(owner, property, std::string{"invalid"}) == PropertyMutationStatus::invalid_value &&
            document.get_prop_val(owner, property).value == PropertyValue{true}, "invalid runtime flag must not change the value");
    }
    const auto changed_xml = oof::source::serialize_form_xml(document);
    const auto changed_binary = oof::save_form_bin(document);
    expect(changed_xml.ok() && changed_xml.value() == xml.value() && changed_binary.ok() && changed_binary.value() == binary.value(),
        "runtime flags must not change XML or binary serialization");
    const auto restored = oof::load_form_bin(changed_binary.value());
    expect(restored.ok() && restored.value().get_prop_val(owner, PropertyId::from_name("ReadOnly")).value == PropertyValue{false} &&
        restored.value().get_prop_val(owner, PropertyId::from_name("MultipleChoice")).value == PropertyValue{false}, "runtime flags must reset on load");
    document.mark_opening_started();
    for (const auto name : {"ReadOnly", "MultipleChoice"}) {
        const auto property = PropertyId::from_name(name);
        expect(document.set_prop_val(owner, property, false) == PropertyMutationStatus::applied_runtime_only &&
            document.get_prop_val(owner, property).value == PropertyValue{false}, "runtime flag must remain writable after opening");
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset_runtime_only &&
            document.get_prop_val(owner, property).value == PropertyValue{false}, "runtime flag reset must restore default");
    }
}

void test_window_permission_mutations() {
    Form form; form.id = ObjectId{1}; form.name = "WindowMutation";
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto normal = PropertyId::from_name("AllowNormalState");
    const auto docked = PropertyId::from_name("AllowDockedState");
    const auto join = PropertyId::from_name("AllowJoinWindow");
    const auto connect = PropertyId::from_name("ConnectableWindow");
    const auto state = PropertyId::from_name("WindowState");
    const auto before = oof::save_form_bin(document);
    expect(document.set_prop_val(owner, normal, false) == PropertyMutationStatus::window_state_disallowed_after_assignment &&
        document.get_prop_val(owner, normal).value == PropertyValue{false}, "disallowed current state must report an error after mutation");
    expect(document.set_prop_val(owner, docked, true) == PropertyMutationStatus::window_state_disallowed_after_assignment &&
        document.get_prop_val(owner, docked).value == PropertyValue{true}, "other state permission must be retained after error");
    expect(document.set_prop_val(owner, normal, true) == PropertyMutationStatus::applied_runtime_only, "restoring current state permission must succeed");
    expect(document.set_prop_val(owner, connect, true) == PropertyMutationStatus::window_join_disallowed_after_assignment &&
        document.get_prop_val(owner, connect).value == PropertyValue{true}, "join error must retain connectability");
    expect(document.set_prop_val(owner, join, true) == PropertyMutationStatus::applied_runtime_only, "enabling join must repair the constraint");
    expect(document.set_prop_val(owner, state, EnumerationValue{"WindowStateVariant", "Docked"}) == PropertyMutationStatus::applied,
        "permitted docked state with connectability must succeed");
    expect(document.set_prop_val(owner, connect, false) == PropertyMutationStatus::window_requires_connectable_after_assignment &&
        document.get_prop_val(owner, connect).value == PropertyValue{false}, "docked connectability error must retain assigned value");
    expect(document.set_prop_val(owner, state, EnumerationValue{"WindowStateVariant", "Normal"}) == PropertyMutationStatus::applied,
        "returning to an allowed state must succeed");
    const auto after = oof::save_form_bin(document);
    expect(before.ok() && after.ok(), "window mutation form must remain serializable");
    const auto restored = oof::load_form_bin(after.value());
    expect(restored.ok() && restored.value().get_prop_val(owner, docked).value == PropertyValue{false} &&
        restored.value().get_prop_val(owner, join).value == PropertyValue{false},
        "runtime permission mutations must not persist");
}

void test_window_permission_platform_matrix() {
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const std::array names{"AllowNormalState", "AllowDockedState", "AllowAutohideState", "AllowFloatState", "AllowJoinWindow", "ConnectableWindow"};
    const std::array states{"Normal", "Docked", "Autohide", "Float"};
    for (std::size_t state_index = 0; state_index < states.size(); ++state_index) {
        for (const bool connectable : {false, true}) {
            for (std::size_t property_index = 0; property_index < names.size(); ++property_index) {
                for (const bool value : {false, true}) {
                    Form form; form.id = ObjectId{1}; form.name = "WindowMatrix";
                    form.properties.set_explicit(PropertyId::from_name("WindowState"), EnumerationValue{"WindowStateVariant", states[state_index]});
                    form.properties.set_explicit(PropertyId::from_name("ConnectableWindow"), connectable);
                    OrdinaryFormDocument document(std::move(form));
                    const auto property = PropertyId::from_name(names[property_index]);
                    auto expected = property_index < 5 ? PropertyMutationStatus::applied_runtime_only : PropertyMutationStatus::applied;
                    if (property_index == state_index && !value) expected = PropertyMutationStatus::window_state_disallowed_after_assignment;
                    else if (property_index == 4 && connectable && !value) expected = PropertyMutationStatus::window_join_disallowed_after_assignment;
                    else if (property_index == 5 && value && state_index == 0 && !connectable) expected = PropertyMutationStatus::window_join_disallowed_after_assignment;
                    else if (property_index == 5 && !value && (state_index == 1 || state_index == 2)) expected = PropertyMutationStatus::window_requires_connectable_after_assignment;
                    expect(document.set_prop_val(owner, property, value) == expected && document.get_prop_val(owner, property).value == PropertyValue{value},
                        "window permission matrix must match platform results and retain assigned values");
                }
            }
        }
    }
}

void test_window_permission_initial_values() {
    using namespace oof::model::metamodel;
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    for (const auto state : {"Normal", "Docked", "Autohide", "Float"}) {
        for (const bool connectable : {false, true}) {
            Form form;
            form.id = ObjectId{1};
            form.name = "WindowPermissions";
            form.properties.set_explicit(PropertyId::from_name("WindowState"), EnumerationValue{"WindowStateVariant", state});
            form.properties.set_explicit(PropertyId::from_name("ConnectableWindow"), connectable);
            OrdinaryFormDocument document(std::move(form));
            const auto xml = oof::source::serialize_form_xml(document);
            const auto binary = oof::save_form_bin(document);
            expect(xml.ok() && binary.ok(), "window form must serialize before opening");
            for (const auto name : {"AllowNormalState", "AllowDockedState", "AllowAutohideState", "AllowFloatState", "AllowJoinWindow"}) {
                const auto property = PropertyId::from_name(name);
                const auto* descriptor = find_form_property(name);
                expect(descriptor && descriptor->persistence == PersistenceClass::runtime_only && descriptor->storage_codec == StorageCodec::none,
                    "window permissions must have no independent stored field");
                const bool expected = std::string_view{name} == "AllowNormalState" || std::string_view{state} != "Normal" ||
                    (std::string_view{name} == "AllowJoinWindow" && connectable);
                const auto result = document.get_prop_val(owner, property);
                expect(result.status == PropertyReadStatus::computed_value && result.value == PropertyValue{expected},
                    "initial window permission must match the platform snapshot");
            }
            document.mark_opening_started();
            const bool opened_connectable = connectable || std::string_view{state} == "Docked" || std::string_view{state} == "Autohide";
            expect(document.get_prop_val(owner, PropertyId::from_name("ConnectableWindow")).value == PropertyValue{opened_connectable},
                "opening must expose the platform's window connectability");
            const auto after_xml = oof::source::serialize_form_xml(document);
            const auto after_binary = oof::save_form_bin(document);
            expect(after_xml.ok() && after_xml.value() == xml.value() && after_binary.ok() && after_binary.value() == binary.value(),
                "opening transition must not change the saved form");
        }
    }
}

void test_opening_boundary_for_window_properties() {
    Form form;
    form.id = ObjectId{1};
    form.name = "WindowOpening";
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto appearance = PropertyId::from_name("ChangeWindowAppearanceMode");
    const auto location = PropertyId::from_name("WindowLocation");
    for (const auto member : {"Auto", "Enable", "Disable"})
        expect(document.set_prop_val(owner, appearance, EnumerationValue{"WindowAppearanceModeChange", member}) == PropertyMutationStatus::applied,
            "window appearance mode must be writable before opening");
    for (const auto member : {"Auto", "Center", "DontOverlapOwner"})
        expect(document.set_prop_val(owner, location, EnumerationValue{"WindowLocationVariant", member}) == PropertyMutationStatus::applied,
            "window location must be writable before opening");
    const auto before_appearance = document.get_prop_val(owner, appearance);
    const auto before_location = document.get_prop_val(owner, location);
    for (const auto name : {"AllowNormalState", "AllowDockedState", "AllowAutohideState", "AllowFloatState", "AllowJoinWindow"}) {
        const auto property = PropertyId::from_name(name);
        expect(document.set_prop_val(owner, property, true) == PropertyMutationStatus::applied_runtime_only &&
               document.reset_prop_val(owner, property) == PropertyMutationStatus::unsupported_surface,
            "window permission assignment must be runtime-only and reset must not invent semantics");
    }
    const auto xml = oof::source::serialize_form_xml(document);
    const auto binary = oof::save_form_bin(document);
    expect(xml.ok() && binary.ok(), "window properties must serialize before opening");
    expect(!document.opening_started(), "new document must be editable before opening");
    document.mark_opening_started();
    document.mark_opening_started();
    for (const auto name : {"AllowNormalState", "AllowDockedState", "AllowAutohideState", "AllowFloatState", "AllowJoinWindow"}) {
        const auto property = PropertyId::from_name(name);
        for (const auto value : {false, true})
            expect(document.set_prop_val(owner, property, value) == PropertyMutationStatus::forbidden_after_opening_started,
                "window permission assignment after opening starts must be forbidden");
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::forbidden_after_opening_started,
            "window permission reset must not bypass the opening restriction");
    }
    expect(document.opening_started(), "opening transition must be idempotent");
    for (const auto member : {"Auto", "Enable", "Disable"})
        expect(document.set_prop_val(owner, appearance, EnumerationValue{"WindowAppearanceModeChange", member}) ==
                   PropertyMutationStatus::forbidden_after_opening_started,
            "appearance assignments, including the current value, must be forbidden after opening starts");
    for (const auto member : {"Auto", "Center", "DontOverlapOwner"})
        expect(document.set_prop_val(owner, location, EnumerationValue{"WindowLocationVariant", member}) ==
                   PropertyMutationStatus::forbidden_after_opening_started,
            "location assignments, including the current value, must be forbidden after opening starts");
    expect(document.reset_prop_val(owner, appearance) == PropertyMutationStatus::forbidden_after_opening_started &&
            document.reset_prop_val(owner, location) == PropertyMutationStatus::forbidden_after_opening_started,
        "reset must not bypass the window property restriction");
    expect(document.get_prop_val(owner, appearance).value == before_appearance.value &&
            document.get_prop_val(owner, location).value == before_location.value,
        "forbidden changes must preserve both property values");
    const auto after_xml = oof::source::serialize_form_xml(document);
    const auto after_binary = oof::save_form_bin(document);
    expect(after_xml.ok() && after_xml.value() == xml.value() &&
            after_binary.ok() && after_binary.value() == binary.value(),
        "runtime opening state must not alter XML or Form.bin");
    const auto loaded = oof::load_form_bin(after_binary.value());
    expect(loaded.ok() && !loaded.value().opening_started(),
        "loading a saved form must not start its runtime opening");
    expect(document.set_prop_val(owner, PropertyId::from_name("SizeChange"),
               EnumerationValue{"WindowSizeChange", "DontChange"}) == PropertyMutationStatus::applied,
        "opening restriction must not affect other properties");
    expect(document.set_prop_val({ObjectCategory::form, ObjectId{2}}, appearance,
               EnumerationValue{"WindowAppearanceModeChange", "Auto"}) == PropertyMutationStatus::unknown_owner,
        "opening restriction must not mask an unknown owner");
}

void test_form_runtime_state_properties() {
    using namespace oof::model::metamodel;
    Form form;
    form.id = ObjectId{1};
    form.name = "RuntimeState";
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto* this_form = find_form_property("ThisForm");
    const auto this_form_id = PropertyId::from_name("ThisForm");
    expect(this_form && this_form->persistence == PersistenceClass::runtime_only &&
            this_form->storage_codec == StorageCodec::none && this_form->value_codec == ValueCodec::form_reference,
        "ThisForm must be a runtime-only form reference");
    expect(document.get_prop_val(owner, this_form_id).value == PropertyValue{FormRef{ObjectId{1}}} &&
            document.set_prop_val(owner, this_form_id, FormRef{ObjectId{1}}) == PropertyMutationStatus::read_only,
        "ThisForm must return the owning form and reject assignment");
    for (const auto name : {"ModalMode", "Modified"}) {
        const auto* descriptor = find_form_property(name);
        expect(descriptor && descriptor->persistence == PersistenceClass::runtime_only &&
                descriptor->storage_codec == StorageCodec::none && descriptor->default_value.kind == DefaultKind::none,
            "form runtime state must not claim persisted storage or a static default");
        expect(document.get_prop_val(owner, PropertyId::from_name(name)).status == PropertyReadStatus::no_default,
            "form runtime state must not fabricate an initial value");
    }
    for (const auto name : {"CloseOnChoice", "CloseOnOwnerClose", "ChoiceMode"}) {
        const auto* descriptor = find_form_property(name);
        expect(descriptor && descriptor->persistence == PersistenceClass::runtime_only &&
                descriptor->storage_codec == StorageCodec::none,
            "choice and owner behavior must be runtime-only");
        const auto property = PropertyId::from_name(name);
        const auto initial = document.get_prop_val(owner, property);
        if (std::string_view{name} == "CloseOnChoice")
            expect(initial.status == PropertyReadStatus::proven_default && initial.value == PropertyValue{true},
                "CloseOnChoice must expose the documented true default");
        else
            expect(initial.status == PropertyReadStatus::no_default,
                "runtime state without a documented default must not invent one");
        expect(document.set_prop_val(owner, property, false) == PropertyMutationStatus::applied_runtime_only &&
                document.get_prop_val(owner, property).value == PropertyValue{false} &&
                !oof::source::serialize_form_xml(document),
            "runtime choice state must be readable without being serialized");
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset_runtime_only &&
                oof::source::serialize_form_xml(document).ok(),
            "resetting runtime choice state must restore serialization");
    }
    const auto modal = PropertyId::from_name("ModalMode");
    const auto modified = PropertyId::from_name("Modified");
    expect(document.set_prop_val(owner, modal, true) == PropertyMutationStatus::read_only,
        "ModalMode must reject assignment");
    expect(document.set_prop_val(owner, modified, true) == PropertyMutationStatus::applied_runtime_only,
        "Modified must accept a runtime Boolean value");
    expect(!oof::source::serialize_form_xml(document),
        "runtime Modified state must not be serialized into public Form.xml");
    expect(document.reset_prop_val(owner, modified) == PropertyMutationStatus::reset_runtime_only &&
            oof::source::serialize_form_xml(document).ok(),
        "resetting Modified must remove runtime state before serialization");
}

void test_control_name_property() {
    Form form; form.id = ObjectId{1}; form.name = "ControlNames";
    OrdinaryFormDocument document(form);
    const auto name_property = PropertyId::from_name("Name");
    [&]<std::size_t... Index>(std::index_sequence<Index...>) {
        ([&] {
            using Payload = std::variant_alternative_t<Index, ControlPayload>;
            if constexpr (!std::is_same_v<Payload, ActiveXControlPayload>) {
                const ObjectId id{100 + Index};
                const auto name = "Элемент_Ω_" + std::to_string(Index);
                document.add_control(ControlNode{id, name, Payload{}});
                const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
                const auto* descriptor = metamodel::find_property(payload_kind(ControlPayload{Payload{}}), name_property);
                expect(descriptor && descriptor->api_access == metamodel::ApiAccess::read_only &&
                    descriptor->persistence == metamodel::PersistenceClass::persisted_readonly &&
                    descriptor->storage_codec == metamodel::StorageCodec::control_info &&
                    descriptor->default_value.kind == metamodel::DefaultKind::none,
                    "every supported control exposes its stored name with the target platform readonly contract");
                const auto result = document.get_prop_val(owner, name_property);
                expect(result.status == PropertyReadStatus::computed_value && result.value == PropertyValue{name},
                    "Name getter reads the authoritative named object for every control kind");
                for (const auto& value : {PropertyValue{name}, PropertyValue{std::string{"Renamed"}}, PropertyValue{UndefinedValue{}}})
                    expect(document.set_prop_val(owner, name_property, value) == PropertyMutationStatus::read_only,
                        "runtime Name rejects every assignment, including its existing name");
                expect(document.reset_prop_val(owner, name_property) == PropertyMutationStatus::read_only &&
                    document.find_control(id)->name == name && !document.find_control(id)->extension_properties.contains(name_property),
                    "Name mutation must preserve the object and never create a shadow property");
            }
        }(), ...);
    }(std::make_index_sequence<std::variant_size_v<ControlPayload>>{});
    expect(document.get_prop_val({ObjectCategory::control, ObjectId{999}}, name_property).status == PropertyReadStatus::unknown_owner,
        "Name does not manufacture an unknown owner");
}

void test_input_property_availability() {
    Form form; form.id = ObjectId{1}; form.name = "InputAvailability";
    OrdinaryFormDocument document(form);
    const auto value_type = PropertyId::from_name("ValueType");
    const auto key = [](std::uint64_t id) { return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{id}}; };
    for (std::uint64_t index = 0; index < 12; ++index) {
        TypeDomainEntry entry;
        entry.term = index % 3 == 0 ? TypeDomainTerm::string : index % 3 == 1 ? TypeDomainTerm::numeric : TypeDomainTerm::date;
        if (entry.term == TypeDomainTerm::string) entry.string = {32, true};
        else if (entry.term == TypeDomainTerm::numeric) entry.numeric = {12, 3, false};
        else entry.date = {true, false};
        TypeDomainPatternValue type{{entry}};
        const ObjectId id{100 + index};
        ControlNode input{id, "Input" + std::to_string(index), InputFieldPayload{}};
        if (index % 2 == 0) {
            Attribute attribute; attribute.id = ObjectId{900 + index}; attribute.name = "Data" + std::to_string(index); attribute.type = type;
            input.data_path = DataPath{AttributeRef{attribute.id}, {}};
            document.add_attribute(std::move(attribute));
        } else input.extension_properties.set_explicit(value_type, type);
        document.add_control(std::move(input));
        for (const auto name : {"Mask", "MultiLine", "ExtendedEdit", "PasswordMode"}) {
            const auto property = PropertyId::from_name(name);
            const auto result = document.get_prop_val(key(id.value()), property);
            if (entry.term == TypeDomainTerm::string) {
                expect(result.value == (std::string_view(name) == "Mask" ? PropertyValue{std::string{}} : PropertyValue{false}),
                    "String InputField exposes the platform defaults of its string properties");
            } else {
                expect(result.status == PropertyReadStatus::unsupported_surface &&
                    document.set_prop_val(key(id.value()), property, std::string_view(name) == "Mask" ? PropertyValue{std::string{}} : PropertyValue{false}) == PropertyMutationStatus::unsupported_surface &&
                    document.reset_prop_val(key(id.value()), property) == PropertyMutationStatus::unsupported_surface,
                    "Numeric and Date InputFields must reject unavailable string properties without changing flags");
            }
        }
        expect(document.get_prop_val(key(id.value()), PropertyId::from_name("TypeRestriction")).value == PropertyValue{TypeDomainPatternValue{}},
            "known single-type InputFields expose an empty type restriction without a shadow property");
        expect(document.get_prop_val(key(id.value()), PropertyId::from_name("TypeLinkItem")).value == PropertyValue{std::int64_t{1}},
            "the supported InputField profiles expose the one-based default type-link item");
        for (const auto name : {"MinValue", "MaxValue"}) {
            const auto result = document.get_prop_val(key(id.value()), PropertyId::from_name(name));
            expect(entry.term == TypeDomainTerm::numeric ? result.value == PropertyValue{UndefinedValue{}} : result.status == PropertyReadStatus::unsupported_surface,
                "bounds are available for bound and unbound Numeric profiles only");
        }
        expect(document.find_control(id)->properties().empty(), "availability reads and rejected writes cannot change stored properties");
    }
    for (std::uint64_t index = 0; index < 12; ++index) {
        TypeDomainEntry text; text.term = TypeDomainTerm::string; text.string = {64, true};
        const ObjectId id{300 + index};
        ControlNode input{id, "ListInput" + std::to_string(index), InputFieldPayload{}};
        input.extension_properties.set_explicit(value_type, TypeDomainPatternValue{{text}});
        input.properties().set_explicit(PropertyId::from_name("ListChoiceMode"), true);
        document.add_control(std::move(input));
        for (const auto name : {"Mask", "MultiLine", "ExtendedEdit", "PasswordMode"}) {
            const auto property = PropertyId::from_name(name);
            expect(document.get_prop_val(key(id.value()), property).status == PropertyReadStatus::unsupported_surface &&
                document.set_prop_val(key(id.value()), property, std::string_view(name) == "Mask" ? PropertyValue{std::string{"!999"}} : PropertyValue{true}) == PropertyMutationStatus::unsupported_surface &&
                document.reset_prop_val(key(id.value()), property) == PropertyMutationStatus::unsupported_surface,
                "ListChoiceMode has no String editor properties even with String ValueType");
        }
        expect(document.find_control(id)->properties().size() == 1, "rejected list editor writes must leave only ListChoiceMode");
    }
    ControlNode dangling{ObjectId{200}, "Dangling", InputFieldPayload{}};
    TypeDomainEntry text; text.term = TypeDomainTerm::string; text.string = {20, true};
    dangling.extension_properties.set_explicit(value_type, TypeDomainPatternValue{{text}});
    dangling.data_path = DataPath{AttributeRef{ObjectId{9999}}, {}};
    document.add_control(std::move(dangling));
    for (const auto name : {"Mask", "MultiLine", "TypeRestriction", "MinValue"})
        expect(document.get_prop_val(key(200), PropertyId::from_name(name)).status == PropertyReadStatus::unsupported_surface,
            "a dangling binding cannot fall back to the local type when deciding property availability");
}

void test_input_type_restriction_runtime() {
    Form form; form.id = ObjectId{1}; form.name = "RuntimeRestrictions";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    TypeDomainEntry text; text.term = TypeDomainTerm::string; text.string = {8, true};
    TypeDomainEntry numeric; numeric.term = TypeDomainTerm::numeric; numeric.numeric = {5, 1, false};
    TypeDomainEntry date; date.term = TypeDomainTerm::date;
    TypeDomainEntry generic_text; generic_text.term = TypeDomainTerm::string;
    TypeDomainEntry generic_numeric; generic_numeric.term = TypeDomainTerm::numeric;
    const std::array descriptions{TypeDomainPatternValue{{text}}, TypeDomainPatternValue{{numeric}},
        TypeDomainPatternValue{{date}}, TypeDomainPatternValue{{generic_text, generic_numeric, date}}, TypeDomainPatternValue{}};
    const auto property = PropertyId::from_name("TypeRestriction"), value_type = PropertyId::from_name("ValueType");
    const auto* descriptor = metamodel::find_property(ControlKind::input_field, property);
    expect(descriptor && descriptor->persistence == metamodel::PersistenceClass::runtime_only &&
        descriptor->storage_codec == metamodel::StorageCodec::none, "TypeRestriction has no persisted property value");
    for (std::uint64_t i = 0; i < 12; ++i) {
        TypeDomainEntry entry;
        entry.term = i % 3 == 0 ? TypeDomainTerm::string : i % 3 == 1 ? TypeDomainTerm::numeric : TypeDomainTerm::date;
        if (entry.term == TypeDomainTerm::string) entry.string = {64, true};
        else if (entry.term == TypeDomainTerm::numeric) entry.numeric = {12, 3, false};
        else entry.date = {true, false};
        const TypeDomainPatternValue type{{entry}};
        ControlNode input{ObjectId{100 + i}, "Input" + std::to_string(i), InputFieldPayload{}};
        if (i % 2 == 0) {
            document.add_attribute(Attribute{ObjectId{900 + i}, "Data" + std::to_string(i), type});
            input.data_path = DataPath{AttributeRef{ObjectId{900 + i}}, {}};
        } else input.extension_properties.set_explicit(value_type, type);
        document.add_control(std::move(input));
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        for (const auto& description : descriptions) {
            expect(document.set_prop_val(owner, property, description) == PropertyMutationStatus::applied_runtime_only &&
                document.get_prop_val(owner, property).value == PropertyValue{description} &&
                document.get_prop_val(owner, value_type).value == PropertyValue{type},
                "runtime restriction preserves each assigned description without changing the original ValueType");
            expect(!oof::source::serialize_form_xml(document).ok(),
                "an explicitly assigned runtime restriction must fail the public XML boundary, including an empty description");
        }
        expect(document.set_prop_val(owner, property, std::string{"wrong"}) == PropertyMutationStatus::invalid_value,
            "TypeRestriction must reject a value that is not a type description");
        expect(document.set_prop_val(owner, property, descriptions.front()) == PropertyMutationStatus::applied_runtime_only,
            "a valid restriction remains usable before testing invalid input");
        TypeDomainEntry invalid_object; invalid_object.term = TypeDomainTerm::object;
        expect(document.set_prop_val(owner, property, TypeDomainPatternValue{{invalid_object}}) == PropertyMutationStatus::invalid_value &&
            document.get_prop_val(owner, property).value == PropertyValue{descriptions.front()},
            "an object description without its type UUID must not replace the live restriction");
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset_runtime_only &&
            document.get_prop_val(owner, property).value == PropertyValue{TypeDomainPatternValue{}},
            "reset clears the runtime restriction and restores the empty computed description");
    }
    expect(oof::source::serialize_form_xml(document).ok(), "reset runtime restrictions leave a serializable named model");
    TypedValueBudget exhausted{max_typed_value_nodes - 1};
    expect(!validate_typed_value(PropertyValue{descriptions.front()}, exhausted),
        "the shared description validator enforces its entry budget before encoding a type description");
}

void test_binding_property_getters() {
    using namespace oof::model::metamodel;
    Form form; form.id = ObjectId{1}; form.name = "BindingProperties";
    OrdinaryFormDocument document(form);
    TypeDomainEntry text; text.term = TypeDomainTerm::string; text.string = {64, true};
    TypeDomainEntry number; number.term = TypeDomainTerm::numeric; number.numeric = {12, 3, false};
    const TypeDomainPatternValue text_type{{text}}, number_type{{number}};
    Attribute text_attribute; text_attribute.id = ObjectId{900}; text_attribute.name = "Текст"; text_attribute.type = text_type;
    Attribute numeric_attribute; numeric_attribute.id = ObjectId{23}; numeric_attribute.name = "Число"; numeric_attribute.type = number_type;
    Attribute empty_attribute; empty_attribute.id = ObjectId{501}; empty_attribute.name = "ПустойТип";
    document.add_attribute(numeric_attribute);
    document.add_attribute(empty_attribute);
    document.add_attribute(text_attribute);
    const auto data = PropertyId::from_name("Data"), type = PropertyId::from_name("ValueType");
    const auto* data_descriptor = find_property(ControlKind::input_field, data);
    const auto* type_descriptor = find_property(ControlKind::input_field, type);
    expect(data_descriptor && data_descriptor->persistence == PersistenceClass::persisted_editable &&
        data_descriptor->storage_codec == StorageCodec::binding_record && type_descriptor &&
        type_descriptor->persistence == PersistenceClass::persisted_editable && type_descriptor->storage_codec == StorageCodec::control_info,
        "Data and ValueType classify their existing binding and type records");
    const auto key = [](std::uint64_t id) { return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{id}}; };
    for (std::uint64_t id = 100; id < 112; ++id) {
        const auto& attribute = id % 2 ? text_attribute : numeric_attribute;
        ControlNode control{ObjectId{id}, "Field" + std::to_string(id), id % 2 ? ControlPayload{ChoiceFieldPayload{}} : ControlPayload{InputFieldPayload{}}};
        control.data_path = DataPath{AttributeRef{attribute.id}, {}};
        document.add_control(std::move(control));
        expect(document.get_prop_val(key(id), data).value == PropertyValue{attribute.name} &&
            document.get_prop_val(key(id), type).value == PropertyValue{attribute.type},
            "twelve getters resolve sparse Attribute IDs and their authoritative names and qualifiers");
        for (const auto& value : {PropertyValue{attribute.type}, PropertyValue{text_type}, PropertyValue{UndefinedValue{}}})
            expect(document.set_prop_val(key(id), type, value) == PropertyMutationStatus::read_only,
                "bound ValueType is readonly before value conversion");
        expect(document.reset_prop_val(key(id), type) == PropertyMutationStatus::read_only &&
            document.set_prop_val(key(id), data, std::string{"Число"}) == PropertyMutationStatus::unsupported_surface &&
            document.reset_prop_val(key(id), data) == PropertyMutationStatus::unsupported_surface,
            "unfinished Data side effects cannot be replaced with a bare binding assignment");
        const auto* actual = document.find_control(ObjectId{id});
        expect(actual->data_path->attribute.id() == attribute.id && actual->extension_properties.empty(),
            "binding getters and rejected setters never create shadow properties or change another owner");
    }
    ControlNode empty_bound{ObjectId{200}, "EmptyBound", ChoiceFieldPayload{}};
    empty_bound.data_path = DataPath{AttributeRef{empty_attribute.id}, {}};
    document.add_control(std::move(empty_bound));
    document.add_control(ControlNode{ObjectId{201}, "Unbound", ChoiceFieldPayload{}});
    expect(document.get_prop_val(key(200), type).value == PropertyValue{TypeDomainPatternValue{}} &&
        document.set_prop_val(key(200), type, number_type) == PropertyMutationStatus::read_only,
        "bound empty type must not be confused with an unbound ChoiceField");
    for (const auto& value : {text_type, number_type, TypeDomainPatternValue{}})
        expect(document.set_prop_val(key(201), type, value) == PropertyMutationStatus::applied &&
            document.get_prop_val(key(201), type).value == PropertyValue{TypeDomainPatternValue{}},
            "unbound ChoiceField accepts a type description without changing its empty type");
    expect(document.get_prop_val(key(201), data).value == PropertyValue{std::string{}} &&
        document.set_prop_val(key(201), type, UndefinedValue{}) == PropertyMutationStatus::invalid_value &&
        document.find_control(ObjectId{201})->extension_properties.empty(),
        "unbound ChoiceField type rejection must not create stored state");
    expect(document.set_prop_val(key(201), type, TypeDomainPatternValue{{TypeDomainEntry{.term = TypeDomainTerm::object}}}) == PropertyMutationStatus::invalid_value &&
        document.get_prop_val(key(201), type).value == PropertyValue{TypeDomainPatternValue{}},
        "ignored type assignment must still validate the type description before accepting it");
    ControlNode local{ObjectId{202}, "LocalType", InputFieldPayload{}};
    local.extension_properties.set_explicit(type, text_type);
    document.add_control(std::move(local));
    expect(document.get_prop_val(key(202), type).value == PropertyValue{text_type},
        "unbound InputField reads its named local type");
    ControlNode nested{ObjectId{203}, "Nested", InputFieldPayload{}};
    nested.data_path = DataPath{AttributeRef{text_attribute.id}, {"Member"}};
    document.add_control(std::move(nested));
    expect(document.get_prop_val(key(203), data).status == PropertyReadStatus::unsupported_surface &&
        document.get_prop_val(key(203), type).status == PropertyReadStatus::unsupported_surface,
        "a nested member cannot be reported as the root attribute or its type");
    document.add_control(ControlNode{ObjectId{204}, "MissingType", InputFieldPayload{}});
    expect(document.get_prop_val(key(204), type).status == PropertyReadStatus::unsupported_surface,
        "missing local type must not invent a standalone InputField default");
    document.add_control(ControlNode{ObjectId{205}, "RadioMember", RadioButtonPayload{}});
    expect(document.get_prop_val(key(205), data).status == PropertyReadStatus::unsupported_surface,
        "missing RadioButton link must not guess whether the group inherits another owner's data");
}

void test_input_field_type_link_model() {
    using namespace oof::model;
    Form form; form.id = ObjectId{1}; form.name = "TypeLink";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{200 + i}});
    OrdinaryFormDocument document(form);
    TypeDomainEntry object_type;
    object_type.term = TypeDomainTerm::object;
    object_type.type_uuid = UuidValue{"12345678-1234-1234-1234-123456789abc"};
    const TypeDomainPatternValue type{{object_type}};
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId attribute_id{100 + index};
        const ObjectId control_id{200 + index};
        document.add_attribute(Attribute{attribute_id, "Object" + std::to_string(index), type});
        ControlNode input{control_id, "Input" + std::to_string(index), InputFieldPayload{}};
        auto& payload = std::get<InputFieldPayload>(input.payload);
        payload.type_link = AttributeRef{attribute_id};
        input.properties().set_explicit(PropertyId::from_name("TypeLinkItem"), std::int64_t{2});
        document.add_control(std::move(input));
    }
    expect(document.validate().ok(), "twelve InputField TypeLinks to one-object-type attributes validate");
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId control_id{200 + index};
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, control_id};
        const auto* input = std::get_if<InputFieldPayload>(&document.find_control(control_id)->payload);
        expect(input && input->type_link == AttributeRef{ObjectId{100 + index}} &&
            document.get_prop_val(owner, PropertyId::from_name("TypeLinkItem")).value == PropertyValue{std::int64_t{2}} &&
            document.set_prop_val(owner, PropertyId::from_name("TypeLinkItem"), std::int64_t{3}) == PropertyMutationStatus::read_only &&
            document.reset_prop_val(owner, PropertyId::from_name("TypeLinkItem")) == PropertyMutationStatus::read_only,
            "named TypeLink and stored TypeLinkItem remain distinct, with readonly property semantics");
    }

    auto dangling_document = document;
    std::get<InputFieldPayload>(const_cast<ControlNode*>(dangling_document.find_control(ObjectId{200}))->payload).type_link = AttributeRef{ObjectId{999}};
    expect(dangling_document.validate().has(InvariantCode::dangling_reference), "TypeLink cannot reference a missing local attribute");

    TypeDomainEntry scalar_type; scalar_type.term = TypeDomainTerm::string;
    auto scalar_document = document;
    scalar_document.add_attribute(Attribute{ObjectId{500}, "Scalar", TypeDomainPatternValue{{scalar_type}}});
    std::get<InputFieldPayload>(const_cast<ControlNode*>(scalar_document.find_control(ObjectId{200}))->payload).type_link = AttributeRef{ObjectId{500}};
    expect(scalar_document.validate().has(InvariantCode::invalid_property), "TypeLink source must have exactly one object type with UUID");

    auto unbound_document = document;
    std::get<InputFieldPayload>(const_cast<ControlNode*>(unbound_document.find_control(ObjectId{200}))->payload).type_link.reset();
    expect(unbound_document.validate().has(InvariantCode::invalid_property), "nondefault TypeLinkItem requires named TypeLink");
    Form default_form; default_form.id = ObjectId{700}; default_form.name = "TypeLinkDefaults";
    default_form.children.emplace_back(ControlRef{ObjectId{601}});
    OrdinaryFormDocument defaults(std::move(default_form));
    defaults.add_control(ControlNode{ObjectId{601}, "DefaultItem", InputFieldPayload{}});
    expect(defaults.validate().ok() &&
        defaults.get_prop_val({ObjectCategory::control, ObjectId{601}}, PropertyId::from_name("TypeLinkItem")).value ==
            PropertyValue{std::int64_t{1}}, "default TypeLinkItem reads as 1 and needs no TypeLink");
}

void test_data_api_preserves_binding_until_supported() {
    const std::array<ControlPayload, 10> payloads{
        CalendarFieldPayload{}, ChoiceFieldPayload{}, CheckBoxPayload{}, RadioButtonPayload{},
        InputFieldPayload{}, ListBoxPayload{}, ProgressBarPayload{}, TrackBarPayload{},
        LabelDecorationPayload{}, TablePayload{}};
    Form form;
    form.id = ObjectId{1};
    form.name = "DataApi";
    OrdinaryFormDocument document(std::move(form));
    const auto property = PropertyId::from_name("Data");
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        const ObjectId id{100 + index};
        ControlNode control{id, "Control" + std::to_string(index), payloads[index]};
        control.data_path = DataPath{AttributeRef{ObjectId{2}}, {}};
        document.add_control(std::move(control));
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        expect(document.get_prop_val(owner, property).status == PropertyReadStatus::unsupported_surface,
            "Data read must not report an unrelated extension value");
        expect(document.set_prop_val(owner, property, std::string{"Other"}) == PropertyMutationStatus::unsupported_surface,
            "unimplemented Data write must report unsupported");
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::unsupported_surface,
            "unimplemented Data reset must report unsupported");
        const auto* current = document.find_control(id);
        expect(current && current->data_path && current->data_path->attribute.id() == ObjectId{2} &&
                !current->extension_properties.find(property),
            "Data API must not modify DataPath or create a shadow extension value");
    }
}

void test_unavailable_ordinary_geometry_properties() {
    using namespace oof::model::metamodel;

    struct Case {
        ControlKind kind;
        std::string_view name;
        ControlPayload payload;
    };
    const std::array cases{
        Case{ControlKind::dendrogram, "AutoMaxHeight", DendrogramPayload{}},
        Case{ControlKind::dendrogram, "AutoMaxWidth", DendrogramPayload{}},
        Case{ControlKind::dendrogram, "MaxHeight", DendrogramPayload{}},
        Case{ControlKind::dendrogram, "MaxWidth", DendrogramPayload{}},
        Case{ControlKind::dendrogram, "VerticalStretch", DendrogramPayload{}},
        Case{ControlKind::dendrogram, "HorizontalStretch", DendrogramPayload{}},
        Case{ControlKind::geographical_schema_field, "AutoMaxHeight", GeographicalSchemaFieldPayload{}},
        Case{ControlKind::geographical_schema_field, "AutoMaxWidth", GeographicalSchemaFieldPayload{}},
        Case{ControlKind::geographical_schema_field, "MaxHeight", GeographicalSchemaFieldPayload{}},
        Case{ControlKind::geographical_schema_field, "MaxWidth", GeographicalSchemaFieldPayload{}},
        Case{ControlKind::geographical_schema_field, "VerticalStretch", GeographicalSchemaFieldPayload{}},
        Case{ControlKind::geographical_schema_field, "HorizontalStretch", GeographicalSchemaFieldPayload{}},
        Case{ControlKind::graphical_schema_field, "VerticalStretch", GraphicalSchemaFieldPayload{}},
        Case{ControlKind::graphical_schema_field, "HorizontalStretch", GraphicalSchemaFieldPayload{}},
        Case{ControlKind::graphical_schema_field, "Edit", GraphicalSchemaFieldPayload{}},
        Case{ControlKind::gantt_chart, "AutoMaxWidth", GanttChartPayload{}},
        Case{ControlKind::gantt_chart, "MaxWidth", GanttChartPayload{}},
    };

    Form form;
    form.id = ObjectId{1};
    form.name = "UnavailableGeometry";
    for (std::size_t index = 0; index < cases.size(); ++index)
        form.children.push_back(ControlRef{ObjectId{100 + index}});
    OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < cases.size(); ++index)
        document.add_control(ControlNode{ObjectId{100 + index}, "Unavailable" + std::to_string(index), cases[index].payload});
    expect(document.validate().ok(), "unavailable-property fixture must be valid before inserting any property");
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& test = cases[index];
        const auto* descriptor = find_property(test.kind, test.name);
        expect(descriptor && descriptor->persistence == PersistenceClass::unsupported_by_platform &&
                   descriptor->storage_codec == StorageCodec::none &&
                   descriptor->default_value.kind == DefaultKind::none,
            "unavailable ordinary geometry property must have an unsupported descriptor without storage or default");

        const ObjectId id{static_cast<std::uint64_t>(100 + index)};
        const PropertyValue value = descriptor->value_kind == ValueKind::number
            ? PropertyValue{DecimalValue{"137"}} : PropertyValue{true};
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        const auto property = PropertyId::from_name(test.name);
        expect(document.get_prop_val(owner, property).status == PropertyReadStatus::unsupported_surface,
            "unavailable ordinary geometry property reads must report unsupported_surface");
        expect(document.set_prop_val(owner, property, value) == PropertyMutationStatus::unsupported_surface,
            "unavailable ordinary geometry property writes must report unsupported_surface");
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::unsupported_surface,
            "unavailable ordinary geometry property resets must report unsupported_surface");

        Form invalid_form;
        invalid_form.id = ObjectId{1};
        invalid_form.name = "RawUnavailable";
        invalid_form.children = {ControlRef{id}};
        OrdinaryFormDocument invalid_document(std::move(invalid_form));
        ControlNode invalid_control{id, "Unavailable", test.payload};
        invalid_control.properties().set_explicit(property, value);
        invalid_document.add_control(std::move(invalid_control));
        expect(!invalid_document.validate().ok(),
            "raw PropertySet insertion must not bypass unsupported-property validation");
    }
}

void test_command_bar_property_descriptors() {
    using namespace oof::model::metamodel;
    const auto* auto_fill = find_property(ControlKind::command_bar, "AutoFill");
    const auto* transparent = find_property(ControlKind::command_bar, "Transparent");
    const auto* alignment = find_property(ControlKind::command_bar, "ButtonsAlignment");
    const auto* orientation = find_property(ControlKind::command_bar, "Orientation");
    const auto* button_back_color = find_property(ControlKind::command_bar, "ButtonBackColor");
    const auto editable = [](const PropertyDescriptor* descriptor, StorageCodec codec, ValueCodec value_codec,
                             DefaultKind default_kind, std::string_view default_value) {
        return descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == codec && descriptor->value_codec == value_codec &&
            descriptor->default_value.kind == default_kind && descriptor->default_value.canonical == default_value;
    };
    expect(editable(auto_fill, StorageCodec::control_info, ValueCodec::boolean,
                    DefaultKind::boolean, "false") &&
               editable(transparent, StorageCodec::control_base, ValueCodec::boolean,
                    DefaultKind::boolean, "false") &&
               editable(alignment, StorageCodec::control_info, ValueCodec::enumeration,
                    DefaultKind::enumeration, "CommandBarButtonAlignment.Left") &&
               editable(orientation, StorageCodec::control_info, ValueCodec::enumeration,
                    DefaultKind::enumeration, "Orientation.Auto") &&
               editable(button_back_color, StorageCodec::control_base, ValueCodec::color,
                    DefaultKind::color, "automatic"),
        "CommandBar editable property descriptors must declare their proven codecs and defaults");

    const auto validate = [](EnumerationValue alignment_value, EnumerationValue orientation_value) {
        Form form;
        form.id = ObjectId{1};
        form.name = "CommandBar";
        form.children = {ControlRef{ObjectId{2}}};
        OrdinaryFormDocument document(std::move(form));
        ControlNode bar{ObjectId{2}, "Tools", CommandBarPayload{}};
        bar.properties().set_explicit(PropertyId::from_name("AutoFill"), true);
        bar.properties().set_explicit(PropertyId::from_name("Transparent"), true);
        bar.properties().set_explicit(PropertyId::from_name("ButtonsAlignment"), std::move(alignment_value));
        bar.properties().set_explicit(PropertyId::from_name("Orientation"), std::move(orientation_value));
        ColorValue color;
        color.kind = ColorKind::absolute;
        color.red = 21;
        color.green = 87;
        color.blue = 143;
        bar.properties().set_explicit(PropertyId::from_name("ButtonBackColor"), color);
        document.add_control(std::move(bar));
        return document.validate();
    };
    expect(validate({"CommandBarButtonAlignment", "Center"}, {"Orientation", "Horizontal"}).ok(),
        "valid typed CommandBar property values must pass model validation");
    expect(validate({"Orientation", "Center"}, {"Orientation", "Horizontal"}).has(InvariantCode::invalid_property),
        "CommandBar ButtonsAlignment must reject a different enumeration type");
    expect(validate({"CommandBarButtonAlignment", "Stretch"}, {"Orientation", "Horizontal"}).has(InvariantCode::invalid_property),
        "CommandBar ButtonsAlignment must reject an unknown enumeration member");
    expect(validate({"CommandBarButtonAlignment", "Center"}, {"HorizontalAlign", "Horizontal"}).has(InvariantCode::invalid_property),
        "CommandBar Orientation must reject a different enumeration type");
    expect(validate({"CommandBarButtonAlignment", "Center"}, {"Orientation", "Diagonal"}).has(InvariantCode::invalid_property),
        "CommandBar Orientation must reject an unknown enumeration member");
}

void test_default_button_model() {
    using namespace oof::model::metamodel;
    const auto property = PropertyId::from_name("DefaultButton");
    const auto* descriptor = find_property(ControlKind::button, property);
    expect(descriptor && descriptor->surface == PropertySurface::control_extension &&
               descriptor->value_codec == ValueCodec::boolean &&
               descriptor->persistence == PersistenceClass::persisted_editable &&
               descriptor->storage_codec == StorageCodec::control_info &&
               descriptor->default_value.kind == DefaultKind::boolean &&
               descriptor->default_value.canonical == "false",
        "Button DefaultButton must expose its persisted Boolean false default");
    for (const auto& control : control_descriptors()) {
        if (control.kind != ControlKind::button)
            expect(find_property(control.kind, property) == nullptr,
                "DefaultButton extension must only be available on Button");
    }

    Form form;
    form.id = ObjectId{1};
    form.name = "DefaultButton";
    for (std::uint64_t id = 2; id < 14; ++id) form.children.emplace_back(ControlRef{ObjectId{id}});
    form.children.emplace_back(ControlRef{ObjectId{14}});
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t id = 2; id < 14; ++id)
        document.add_control(ControlNode{ObjectId{id}, "Button" + std::to_string(id), ButtonPayload{}});
    ControlNode bar{ObjectId{14}, "Tools", CommandBarPayload{}};
    bar.properties().set_explicit(PropertyId::from_name("Secondary"), false);
    CommandBarButton action;
    action.name = "Run";
    action.action = CommandBarAction{"RunHandler", "", {}, {}, {}};
    std::get<CommandBarPayload>(bar.payload).buttons.push_back(action);
    document.add_control(std::move(bar));
    const auto key = [](std::uint64_t id) { return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{id}}; };
    for (std::uint64_t id = 2; id < 14; ++id) {
        const auto initial = document.get_prop_val(key(id), property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value == PropertyValue{false},
            "each Button must initially expose false");
        expect(document.set_prop_val(key(id), property, true) == PropertyMutationStatus::applied,
            "Button default selection must apply");
        for (std::uint64_t other = 2; other < 14; ++other)
            expect(document.get_prop_val(key(other), property).value == PropertyValue{other == id},
                "selecting a Button must clear all other Button selections");
    }
    expect(document.validate().ok(), "a single selected Button must validate");
    expect(document.set_prop_val(key(2), property, std::string("true")) == PropertyMutationStatus::invalid_value &&
               document.get_prop_val(key(13), property).value == PropertyValue{true},
        "invalid input must not alter the active selection");

    auto* command_bar = const_cast<ControlNode*>(document.find_control(ObjectId{14}));
    auto& menu_action = std::get<CommandBarPayload>(command_bar->payload).buttons.front();
    menu_action.default_button = true;
    expect(document.validate().has(InvariantCode::invalid_property),
        "a Button and an Action cannot both be active");
    expect(document.set_prop_val(key(2), property, true) == PropertyMutationStatus::applied &&
               !menu_action.default_button && document.get_prop_val(key(13), property).value == PropertyValue{false},
        "selecting a Button must clear an active command bar Action and other Buttons");
    expect(document.set_prop_val(key(2), property, false) == PropertyMutationStatus::applied &&
               !menu_action.default_button && document.get_prop_val(key(2), property).value == PropertyValue{false},
        "clearing a Button must not select a different default");
    auto* first = const_cast<ControlNode*>(document.find_control(ObjectId{2}));
    auto* second = const_cast<ControlNode*>(document.find_control(ObjectId{3}));
    first->extension_properties.set_explicit(property, true);
    second->extension_properties.set_explicit(property, true);
    expect(document.validate().has(InvariantCode::invalid_property),
        "directly constructed documents cannot contain two selected Buttons");
}

void test_context_menu_reference_model() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    Form form;
    form.id = ObjectId{1};
    form.name = "ContextMenu";
    form.children = {ControlRef{ObjectId{2}}, ControlRef{ObjectId{3}}, ControlRef{ObjectId{4}}, ControlRef{ObjectId{5}}};
    OrdinaryFormDocument document(std::move(form));
    document.add_control(ControlNode{ObjectId{2}, "Label", LabelDecorationPayload{}});
    CommandBarPayload bar;
    CommandBarButton parent;
    parent.name = "Parent";
    parent.type = CommandBarButtonKind::submenu;
    CommandBarButton nested;
    nested.name = "Nested/Name";
    nested.type = CommandBarButtonKind::submenu;
    parent.buttons.push_back(nested);
    bar.buttons.push_back(parent);
    document.add_control(ControlNode{ObjectId{3}, "Tools", std::move(bar)});
    document.add_control(ControlNode{ObjectId{4}, "Other", LabelDecorationPayload{}});
    ButtonPayload button;
    button.buttons.push_back(parent);
    document.add_control(ControlNode{ObjectId{5}, "Button", std::move(button)});
    const OrdinaryFormDocument::ObjectKey label{ObjectCategory::control, ObjectId{2}};
    const OrdinaryFormDocument::ObjectKey form_owner{ObjectCategory::form, ObjectId{1}};
    const auto property = PropertyId::from_name("ContextMenu");
    const auto automatic = PropertyId::from_name("AutoContextMenu");
    const auto* descriptor = find_property(ControlKind::label_decoration, property);
    expect(descriptor && descriptor->value_codec == ValueCodec::context_menu_reference &&
           descriptor->persistence == PersistenceClass::persisted_editable &&
           descriptor->storage_codec == StorageCodec::root_record &&
           descriptor->default_value.kind == DefaultKind::undefined,
        "ContextMenu should have a typed value, proven default, and root record storage");
    const auto* auto_descriptor = find_property(ControlKind::label_decoration, automatic);
    expect(auto_descriptor && auto_descriptor->value_codec == ValueCodec::boolean &&
           auto_descriptor->persistence == PersistenceClass::persisted_editable &&
           auto_descriptor->storage_codec == StorageCodec::root_record &&
           auto_descriptor->default_value.kind == DefaultKind::boolean &&
           auto_descriptor->default_value.canonical == "true",
        "AutoContextMenu must expose its proven default and root record storage");
    expect(document.get_prop_val(label, automatic).value == PropertyValue{true},
        "AutoContextMenu must expose its proven true default");
    expect(document.get_prop_val(label, property).value == PropertyValue{UndefinedValue{}},
        "ContextMenu must expose Undefined by default");
    const auto* form_auto = find_form_property("AutoContextMenu");
    const auto* form_context = find_form_property("ContextMenu");
    expect(form_auto && form_auto->value_codec == ValueCodec::boolean &&
               form_auto->persistence == PersistenceClass::persisted_editable &&
               form_auto->storage_codec == StorageCodec::root_record &&
               form_auto->default_value.kind == DefaultKind::boolean && form_auto->default_value.canonical == "true" &&
               form_context && form_context->value_codec == ValueCodec::context_menu_reference &&
               form_context->persistence == PersistenceClass::persisted_editable &&
               form_context->storage_codec == StorageCodec::root_record &&
               form_context->default_value.kind == DefaultKind::undefined,
        "form context properties must use proven defaults and root record storage");
    expect(document.get_prop_val(form_owner, automatic).value == PropertyValue{true} &&
               document.get_prop_val(form_owner, property).value == PropertyValue{UndefinedValue{}},
        "form context properties must return their proven defaults");
    expect(document.set_prop_val(form_owner, automatic, false) == PropertyMutationStatus::applied &&
               document.set_prop_val(form_owner, property, ControlRef{ObjectId{3}}) == PropertyMutationStatus::applied &&
               document.set_prop_val(form_owner, property, ControlRef{ObjectId{4}}) == PropertyMutationStatus::invalid_reference,
        "form context properties must accept writable values and reject a wrong menu owner");
    expect(document.set_prop_val(label, property, ControlRef{ObjectId{3}}) == PropertyMutationStatus::applied,
        "ContextMenu must accept a CommandBar reference");
    expect(document.set_prop_val(label, property, ControlRef{ObjectId{4}}) == PropertyMutationStatus::invalid_reference,
        "ContextMenu must reject a reference to a non-CommandBar control");
    const CommandBarButtonRef reference{ControlRef{ObjectId{3}}, {"Parent", "Nested/Name"}};
    expect(document.set_prop_val(form_owner, property, reference) == PropertyMutationStatus::applied &&
               document.get_prop_val(form_owner, property).value == PropertyValue{reference},
        "form ContextMenu must accept and return a nested button reference");
    auto& form_properties = const_cast<Form&>(document.form()).properties;
    form_properties.set_explicit(property, ControlRef{ObjectId{4}});
    expect(document.validate().has(InvariantCode::invalid_property),
        "form validation must reject a context menu pointing at a non-CommandBar control");
    form_properties.set_explicit(property, CommandBarButtonRef{ControlRef{ObjectId{3}}, {"Parent", "Missing"}});
    expect(document.validate().has(InvariantCode::dangling_reference),
        "form validation must reject an unresolved nested context menu path");
    form_properties.set_explicit(property, reference);
    expect(document.set_prop_val(label, property, reference) == PropertyMutationStatus::applied,
        "ContextMenu must accept an existing nested menu path without delimiter ambiguity");
    expect(document.validate().ok(), "named nested context menu path must validate");
    expect(document.set_prop_val(label, property, CommandBarButtonRef{ControlRef{ObjectId{3}}, {"Parent", "Missing"}}) == PropertyMutationStatus::invalid_reference &&
           document.set_prop_val(label, property, CommandBarButtonRef{ControlRef{ObjectId{3}}, {}}) == PropertyMutationStatus::invalid_reference &&
           document.set_prop_val(label, property, CommandBarButtonRef{ControlRef{ObjectId{99}}, {"Parent"}}) == PropertyMutationStatus::invalid_reference &&
           document.set_prop_val(label, property, CommandBarButtonRef{ControlRef{ObjectId{5}}, {"Parent"}}) == PropertyMutationStatus::invalid_reference,
        "missing owner, non-CommandBar owner, empty path, and missing path elements must be rejected");
    expect(document.set_prop_val(label, property, true) == PropertyMutationStatus::invalid_value,
        "ContextMenu must reject unrelated value types");
    auto* menu = const_cast<ControlNode*>(document.find_control(ObjectId{3}));
    auto& buttons = std::get<CommandBarPayload>(menu->payload).buttons;
    buttons.front().buttons.front().type = CommandBarButtonKind::separator;
    expect(document.set_prop_val(label, property, reference) == PropertyMutationStatus::invalid_reference,
        "ContextMenu must reject a non-submenu button");
    buttons.front().buttons.front().type = CommandBarButtonKind::submenu;
    buttons.push_back(buttons.front());
    expect(document.validate().has(InvariantCode::dangling_reference),
        "ambiguous context menu path must not resolve");
}

void test_command_bar_action_source_model() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    const auto* descriptor = find_property(ControlKind::command_bar, "ActionSource");
    expect(descriptor && descriptor->value_codec == ValueCodec::action_source_reference &&
               descriptor->value_kind == ValueKind::object &&
               descriptor->default_value.kind == DefaultKind::undefined &&
               descriptor->default_value.canonical == "undefined" &&
               descriptor->persistence == PersistenceClass::persisted_editable &&
               descriptor->storage_codec == StorageCodec::control_info &&
               find_property(ControlKind::button, "ActionSource") == nullptr,
        "ActionSource must be typed, Undefined by default, and declared only for CommandBar");

    const auto validate = [](PropertyValue value) {
        Form form;
        form.id = ObjectId{1};
        form.name = "ActionSource";
        form.children = {ControlRef{ObjectId{2}}, ControlRef{ObjectId{3}}, ControlRef{ObjectId{4}}};
        OrdinaryFormDocument document(std::move(form));
        ControlNode bar{ObjectId{2}, "Tools", CommandBarPayload{}};
        bar.extension_properties.set_explicit(PropertyId::from_name("ActionSource"), std::move(value));
        document.add_control(std::move(bar));
        document.add_control(ControlNode{ObjectId{3}, "Html", HtmlDocumentFieldPayload{}});
        document.add_control(ControlNode{ObjectId{4}, "Field", InputFieldPayload{}});
        return document.validate();
    };
    expect(validate(UndefinedValue{}).ok(), "Undefined ActionSource must validate as the default");
    expect(validate(FormRef{ObjectId{1}}).ok(), "ActionSource may reference the owning form");
    expect(validate(ControlRef{ObjectId{3}}).ok(), "ActionSource may reference HTMLDocumentField");
    expect(validate(FormRef{ObjectId{9}}).has(InvariantCode::dangling_reference),
        "ActionSource must reject a FormRef that does not identify the current form");
    expect(validate(ControlRef{ObjectId{4}}).has(InvariantCode::invalid_property),
        "ActionSource must reject a resolved control that is not a supported source kind");
    expect(validate(ControlRef{ObjectId{99}}).has(InvariantCode::dangling_reference),
        "ActionSource must reject a dangling ControlRef");
}

void test_standard_menu_command_registry_and_runtime_contracts() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    expect(standard_menu_commands.size() == 3 &&
        find_standard_menu_command(StandardMenuCommand::close) == &standard_menu_commands[0] &&
        find_standard_menu_command("SaveValues") == &standard_menu_commands[1] &&
        find_standard_menu_command(144) == &standard_menu_commands[2] &&
        standard_menu_close.kind == standard_menu_commands[0].kind &&
        standard_menu_close.public_name == standard_menu_commands[0].public_name &&
        standard_menu_close.storage_id == standard_menu_commands[0].storage_id,
        "standard menu commands must share one registry for enum, public name, and storage ID lookup");

    Form form; form.id = ObjectId{1}; form.name = "StandardCommandContracts";
    form.children = {ControlRef{ObjectId{42}}};
    OrdinaryFormDocument document(std::move(form));
    ControlNode bar{ObjectId{42}, "Tools", CommandBarPayload{}};
    auto& buttons = std::get<CommandBarPayload>(bar.payload).buttons;
    for (const auto command : {StandardMenuCommand::save_values, StandardMenuCommand::restore_values}) {
        CommandBarButton item; item.name = std::string(find_standard_menu_command(command)->public_name);
        item.standard_action = StandardMenuAction{command, {}, StandardMenuActionSource::form,
            StandardMenuActionContext::default_context, {}};
        buttons.push_back(std::move(item));
    }
    document.add_control(std::move(bar));
    expect(document.validate().ok(), "SaveValues and RestoreValues use Form source and command-owned Default context");

    auto invalid = document;
    auto invalid_bar = *invalid.find_control(ObjectId{42});
    auto& invalid_action = *std::get<CommandBarPayload>(invalid_bar.payload).buttons.front().standard_action;
    invalid_action.command = static_cast<StandardMenuCommand>(255);
    invalid.add_control(std::move(invalid_bar));
    expect(invalid.validate().has(InvariantCode::invalid_property), "unknown standard menu command enum must reject");

    invalid = document;
    invalid_bar = *invalid.find_control(ObjectId{42});
    auto& save_action = *std::get<CommandBarPayload>(invalid_bar.payload).buttons.front().standard_action;
    save_action.command_bar = ControlRef{ObjectId{42}};
    invalid.add_control(std::move(invalid_bar));
    expect(invalid.validate().has(InvariantCode::invalid_property), "runtime standard command must reject commandBarId");

    invalid = document;
    invalid_bar = *invalid.find_control(ObjectId{42});
    auto& restore_action = *std::get<CommandBarPayload>(invalid_bar.payload).buttons.back().standard_action;
    restore_action.source = StandardMenuActionSource::all_sources;
    invalid.add_control(std::move(invalid_bar));
    expect(invalid.validate().has(InvariantCode::invalid_property), "runtime standard command must reject non-Form sources");
}

void test_standard_picture_descriptor_catalog() {
    const auto pictures = metamodel::standard_picture_descriptors();
    expect(pictures.size() == 294, "standard picture catalog must expose all canonical runtime descriptors");
    std::set<std::string_view> names;
    std::set<std::string_view> guids;
    std::set<std::int32_t> storage_ids;
    for (const auto& picture : pictures) {
        expect(!picture.runtime_name.empty() && !picture.russian_name.empty(),
            "standard picture descriptors need public and Russian names");
        expect(names.insert(picture.runtime_name).second, "standard picture runtime names must be unique");
        expect(metamodel::find_standard_picture(picture.runtime_name) == &picture,
            "standard picture names must resolve to their catalog descriptor");
        if (!picture.guid.empty()) {
            expect(guids.insert(picture.guid).second && picture.storage_id == 0,
                "GUID-backed standard pictures must have one identity representation");
            expect(metamodel::find_standard_picture_by_guid(picture.guid) == &picture,
                "standard picture GUID lookup must be exact");
        } else {
            expect(picture.storage_id < 0 && storage_ids.insert(picture.storage_id).second,
                "numeric standard picture IDs must be unique negative references");
            expect(metamodel::find_standard_picture_by_storage_id(picture.storage_id) == &picture,
                "standard picture numeric ID lookup must be exact");
        }
    }
    expect(guids.size() == 274 && storage_ids.size() == 20,
        "standard picture catalog must retain GUID and negative-ID identity coverage");
}

void test_button_alignment_xml_defaults() {
    using namespace oof::model::metamodel;
    const auto* border_descriptor = find_property(ControlKind::button, "BorderColor");
    const auto* text_color_descriptor = find_property(ControlKind::button, "ButtonTextColor");
    const auto* back_color_descriptor = find_property(ControlKind::button, "ButtonBackColor");
    const auto* font_descriptor = find_property(ControlKind::button, "Font");
    expect(border_descriptor && border_descriptor->storage_codec == StorageCodec::control_base &&
               border_descriptor->default_value.kind == DefaultKind::color &&
               border_descriptor->default_value.canonical == "automatic",
        "Button BorderColor must describe its automatic serialized default");
    expect(text_color_descriptor && text_color_descriptor->storage_codec == StorageCodec::control_base &&
               text_color_descriptor->default_value.kind == DefaultKind::color &&
               text_color_descriptor->default_value.canonical == "StyleColors.ButtonTextColor",
        "Button ButtonTextColor must describe its named serialized default");
    expect(back_color_descriptor && back_color_descriptor->storage_codec == StorageCodec::control_base &&
               back_color_descriptor->default_value.kind == DefaultKind::color &&
               back_color_descriptor->default_value.canonical == "StyleColors.ButtonBackColor",
        "Button ButtonBackColor must describe its named serialized default");
    expect(font_descriptor && font_descriptor->storage_codec == StorageCodec::control_base &&
               font_descriptor->default_value.kind == DefaultKind::font &&
               font_descriptor->default_value.canonical == "automatic",
        "Button Font must describe its automatic serialized default");
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <Button id="2" name="Center"><Position/><VerticalAlign type="VerticalAlign" member="Center"/><HorizontalAlign type="HorizontalAlign" member="Center"/><ToolTip></ToolTip><PictureLocation type="PictureLocation" member="Left"/><PictureSize type="PictureSize" member="RealSize"/><MenuMode type="MenuMode" member="DontUse"/><BorderColor kind="automatic"/><ButtonTextColor kind="styleReference" styleName="StyleColors.ButtonTextColor"/><ButtonBackColor kind="styleReference" styleName="StyleColors.ButtonBackColor"/><Font kind="automatic"/></Button>
  <Button id="3" name="Right"><Position/><VerticalAlign type="VerticalAlign" member="Bottom"/><HorizontalAlign type="HorizontalAlign" member="Right"/><ToolTip>hint</ToolTip><PictureLocation type="PictureLocation" member="Right"/><PictureSize type="PictureSize" member="ByFontSize"/><MenuMode type="MenuMode" member="UseExtra"/><BorderColor kind="absolute" red="17" green="83" blue="201" alpha="255"/><ButtonTextColor kind="styleReference" styleName="StyleColors.ButtonBorderColor"/><ButtonBackColor kind="styleReference" styleName="StyleColors.ButtonTextColor"/><Font kind="absolute" faceName="Arial" height="10" bold="false" italic="true" underline="false" strikeout="true" scale="100" scaleOverride="true"/></Button>
</ChildItems></Form>
)XML";
    const auto parsed = oof::source::parse_form_xml(xml);
    if (!parsed) {
        throw std::runtime_error("Button alignment XML failed at " + parsed.diagnostics().front().path +
            ": " + parsed.diagnostics().front().message);
    }
    const auto* center = parsed.value().find_control(ObjectId{2});
    const auto* right = parsed.value().find_control(ObjectId{3});
    expect(center && !center->properties().find(PropertyId::from_name("HorizontalAlign")) &&
               !center->properties().find(PropertyId::from_name("VerticalAlign")) &&
               !center->properties().find(PropertyId::from_name("ToolTip")) &&
               !center->properties().find(PropertyId::from_name("PictureLocation")) &&
               !center->properties().find(PropertyId::from_name("PictureSize")) &&
               !center->properties().find(PropertyId::from_name("MenuMode")) &&
               !center->properties().find(PropertyId::from_name("BorderColor")) &&
               !center->properties().find(PropertyId::from_name("ButtonTextColor")) &&
               !center->properties().find(PropertyId::from_name("ButtonBackColor")) &&
               !center->properties().find(PropertyId::from_name("Font")),
        "XML Button defaults must normalize to descriptor defaults");
    expect(right && std::get<EnumerationValue>(right->properties().find(
               PropertyId::from_name("HorizontalAlign"))->value) ==
               EnumerationValue{"HorizontalAlign", "Right"} &&
               std::get<EnumerationValue>(right->properties().find(
                   PropertyId::from_name("VerticalAlign"))->value) ==
               EnumerationValue{"VerticalAlign", "Bottom"} &&
               std::get<EnumerationValue>(right->properties().find(
                   PropertyId::from_name("PictureLocation"))->value) ==
               EnumerationValue{"PictureLocation", "Right"} &&
               std::get<EnumerationValue>(right->properties().find(
                   PropertyId::from_name("PictureSize"))->value) ==
               EnumerationValue{"PictureSize", "ByFontSize"} &&
               std::get<EnumerationValue>(right->properties().find(
                   PropertyId::from_name("MenuMode"))->value) ==
               EnumerationValue{"MenuMode", "UseExtra"} &&
               std::get<ColorValue>(right->properties().find(PropertyId::from_name("BorderColor"))->value) ==
                   ColorValue{ColorKind::absolute, 17, 83, 201, 255, std::monostate{}} &&
               std::get<ColorValue>(right->properties().find(PropertyId::from_name("ButtonTextColor"))->value).style ==
                   StyleReference{QualifiedName{"StyleColors.ButtonBorderColor"}} &&
               std::get<ColorValue>(right->properties().find(PropertyId::from_name("ButtonBackColor"))->value).style ==
                   StyleReference{QualifiedName{"StyleColors.ButtonTextColor"}} &&
               std::get<FontValue>(right->properties().find(PropertyId::from_name("Font"))->value).face_name == "Arial" &&
               std::get<FontValue>(right->properties().find(PropertyId::from_name("Font"))->value).bold == false &&
               std::get<FontValue>(right->properties().find(PropertyId::from_name("Font"))->value).italic == true &&
               std::get<FontValue>(right->properties().find(PropertyId::from_name("Font"))->value).scale == 100 &&
               std::get<FontValue>(right->properties().find(PropertyId::from_name("Font"))->value).scale_override,
        "non-default Button enum values must remain explicit in the object model");
    const auto serialized = oof::source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "Button alignment default document must serialize");
    expect(serialized.value().find("member=\"Center\"") == std::string::npos &&
               serialized.value().find("member=\"Right\"") != std::string::npos &&
               serialized.value().find("member=\"Bottom\"") != std::string::npos &&
               serialized.value().find("type=\"PictureLocation\" member=\"Left\"") == std::string::npos &&
               serialized.value().find("type=\"PictureSize\" member=\"RealSize\"") == std::string::npos &&
               serialized.value().find("type=\"PictureLocation\" member=\"Right\"") != std::string::npos &&
               serialized.value().find("type=\"PictureSize\" member=\"ByFontSize\"") != std::string::npos &&
               serialized.value().find("type=\"MenuMode\" member=\"UseExtra\"") != std::string::npos &&
               serialized.value().find("<BorderColor kind=\"automatic\"/>") == std::string::npos &&
               serialized.value().find("<ButtonTextColor kind=\"styleReference\" styleName=\"StyleColors.ButtonTextColor\"/>") == std::string::npos &&
               serialized.value().find("<ButtonBackColor kind=\"styleReference\" styleName=\"StyleColors.ButtonBackColor\"/>") == std::string::npos &&
               serialized.value().find("red=\"17\" green=\"83\" blue=\"201\"") != std::string::npos &&
               serialized.value().find("StyleColors.ButtonBorderColor") != std::string::npos &&
               serialized.value().find("<Font kind=\"automatic\"/>") == std::string::npos &&
               serialized.value().find("mask=") == std::string::npos &&
               serialized.value().find("bold=\"false\"") != std::string::npos &&
               serialized.value().find("scale=\"100\"") != std::string::npos &&
               serialized.value().find("scaleOverride=\"true\"") != std::string::npos &&
               serialized.value().find("<ToolTip>hint</ToolTip>") != std::string::npos &&
               serialized.value().find("<ToolTip></ToolTip>") == std::string::npos,
        "XML writer must omit defaults but retain explicit Button enum values");
}

void test_progress_bar_storage_descriptors() {
    using namespace oof::model::metamodel;
    const auto* enabled = find_property(ControlKind::progress_bar, "Enabled");
    const auto* tool_tip = find_property(ControlKind::progress_bar, "ToolTip");
    const auto* max_value = find_property(ControlKind::progress_bar, "MaxValue");
    const auto* min_value = find_property(ControlKind::progress_bar, "MinValue");
    const auto* step = find_property(ControlKind::progress_bar, "Step");
    expect(enabled && enabled->persistence == PersistenceClass::persisted_editable &&
               enabled->storage_codec == StorageCodec::control_base &&
               enabled->default_value.kind == DefaultKind::boolean &&
               enabled->default_value.canonical == "true",
        "ProgressBar Enabled must have its observed control-base codec and true default");
    expect(tool_tip && tool_tip->persistence == PersistenceClass::persisted_editable &&
               tool_tip->storage_codec == StorageCodec::control_base &&
               tool_tip->default_value.kind == DefaultKind::string &&
               tool_tip->default_value.canonical.empty(),
        "ProgressBar ToolTip must have its observed control-base codec and empty default");
    expect(max_value && max_value->persistence == PersistenceClass::persisted_editable &&
               max_value->storage_codec == StorageCodec::control_info &&
               max_value->default_value.kind == DefaultKind::integer &&
               max_value->default_value.canonical == "100" && max_value->value_codec == ValueCodec::integer32,
        "ProgressBar MaxValue must use named int32 input, observed control-info storage, and default 100");
    expect(min_value && min_value->persistence == PersistenceClass::persisted_editable &&
               min_value->storage_codec == StorageCodec::control_info &&
               min_value->default_value.kind == DefaultKind::integer &&
               min_value->default_value.canonical == "0" && min_value->value_codec == ValueCodec::integer32,
        "ProgressBar MinValue must use named int32 input, observed control-info storage, and default 0");
    expect(step && step->persistence == PersistenceClass::persisted_editable &&
               step->storage_codec == StorageCodec::control_info &&
               step->default_value.kind == DefaultKind::integer &&
               step->default_value.canonical == "1" && step->value_codec == ValueCodec::integer32,
        "ProgressBar Step must use named int32 input, observed control-info storage, and default 1");
}

void test_track_bar_storage_descriptors() {
    using namespace oof::model::metamodel;
    const auto* enabled = find_property(ControlKind::track_bar, "Enabled");
    const auto* tool_tip = find_property(ControlKind::track_bar, "ToolTip");
    const auto* max_value = find_property(ControlKind::track_bar, "MaxValue");
    const auto* min_value = find_property(ControlKind::track_bar, "MinValue");
    const auto* step = find_property(ControlKind::track_bar, "Step");
    const auto* large_step = find_property(ControlKind::track_bar, "LargeStep");
    const auto* marking_step = find_property(ControlKind::track_bar, "MarkingStep");
    expect(enabled && enabled->persistence == PersistenceClass::persisted_editable &&
               enabled->storage_codec == StorageCodec::control_base &&
               enabled->default_value.kind == DefaultKind::boolean &&
               enabled->default_value.canonical == "true",
        "TrackBar Enabled must use its observed control-base codec and true default");
    expect(tool_tip && tool_tip->persistence == PersistenceClass::persisted_editable &&
               tool_tip->storage_codec == StorageCodec::control_base &&
               tool_tip->default_value.kind == DefaultKind::string &&
               tool_tip->default_value.canonical.empty(),
        "TrackBar ToolTip must use its observed control-base codec and empty default");
    expect(max_value && max_value->persistence == PersistenceClass::persisted_editable &&
               max_value->storage_codec == StorageCodec::control_info &&
               max_value->default_value.kind == DefaultKind::integer &&
               max_value->default_value.canonical == "100" && max_value->value_codec == ValueCodec::integer32,
        "TrackBar MaxValue must use named int32 input, observed control-info storage, and default 100");
    expect(min_value && min_value->persistence == PersistenceClass::persisted_editable &&
               min_value->storage_codec == StorageCodec::control_info &&
               min_value->default_value.kind == DefaultKind::integer &&
               min_value->default_value.canonical == "0" && min_value->value_codec == ValueCodec::integer32,
        "TrackBar MinValue must use named int32 input, observed control-info storage, and default 0");
    expect(step && step->persistence == PersistenceClass::persisted_editable &&
               step->storage_codec == StorageCodec::control_info &&
               step->default_value.kind == DefaultKind::integer &&
               step->default_value.canonical == "1" && step->value_codec == ValueCodec::integer32,
        "TrackBar Step must use named int32 input, observed control-info storage, and default 1");
    for (const auto [property, value] : {std::pair{large_step, "10"}, std::pair{marking_step, "5"}}) {
        expect(property && property->persistence == PersistenceClass::persisted_editable &&
                   property->storage_codec == StorageCodec::control_info &&
                   property->default_value.kind == DefaultKind::integer &&
                   property->default_value.canonical == value && property->value_codec == ValueCodec::integer32,
            "TrackBar step descriptors must expose their observed int32 storage and defaults");
    }
}

void test_chart_integer_and_delimiter_descriptors() {
    using namespace oof::model::metamodel;
    for (const auto name : {"ValueLabelFormat", "PercentLabelFormat"}) {
        const auto* property = find_property(ControlKind::chart, name);
        expect(property && property->storage_codec == StorageCodec::control_info &&
            property->value_codec == ValueCodec::localized_text && property->default_value.kind == DefaultKind::string &&
            property->default_value.canonical.empty(), "Chart format descriptor must use localized text and empty default");
    }
    const auto* delimiter = find_property(ControlKind::chart, "LabelDelimiter");
    expect(delimiter && delimiter->storage_codec == StorageCodec::control_info &&
        delimiter->value_codec == ValueCodec::string && delimiter->default_value.kind == DefaultKind::string &&
        delimiter->default_value.canonical == ", ", "Chart delimiter must use its named string and platform default");
    for (const auto& [name, default_value] : {std::pair{"MaxSeriesCount", "4"}, std::pair{"MaxSeriesPercent", "30"},
             std::pair{"BeginGaugeAngle", "0"}, std::pair{"EndGaugeAngle", "180"},
             std::pair{"GaugeThickness", "5"}, std::pair{"GaugeBushThickness", "4"}}) {
        const auto* property = find_property(ControlKind::chart, name);
        expect(property && property->storage_codec == StorageCodec::control_info &&
            property->value_codec == ValueCodec::integer32 && property->default_value.kind == DefaultKind::integer &&
            property->default_value.canonical == default_value, "Chart integer descriptor must match platform storage and default");
    }
}

void test_input_list_choice_mode_api() {
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));
    const auto property = PropertyId::from_name("ListChoiceMode");
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId id{2 + index};
        document.add_control(ControlNode{id, "Input" + std::to_string(index), InputFieldPayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        const auto initial = document.get_prop_val(owner, property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value &&
            *initial.value == PropertyValue{false}, "list choice mode must default to false");
        expect(document.set_prop_val(owner, property, true) == PropertyMutationStatus::applied,
            "list choice mode must accept true");
        const auto read = document.get_prop_val(owner, property);
        expect(read.value && *read.value == PropertyValue{true}, "list choice mode must return the assigned value");
        const auto button = PropertyId::from_name("ChoiceListButton");
        expect(document.get_prop_val(owner, button).value == std::optional<PropertyValue>{PropertyValue{true}},
            "enabling list choice mode must enable the list choice button");
        expect(document.set_prop_val(owner, button, false) == PropertyMutationStatus::applied,
            "the button can be independently disabled");
        expect(document.get_prop_val(owner, property).value == std::optional<PropertyValue>{PropertyValue{true}},
            "disabling the button must not disable list choice mode");
        expect(document.set_prop_val(owner, property, false) == PropertyMutationStatus::applied &&
            document.get_prop_val(owner, button).value == std::optional<PropertyValue>{PropertyValue{false}},
            "disabling list choice mode must disable the list choice button");
        expect(document.set_prop_val(owner, button, true) == PropertyMutationStatus::applied &&
            document.set_prop_val(owner, property, false) == PropertyMutationStatus::applied &&
            document.get_prop_val(owner, button).value == std::optional<PropertyValue>{PropertyValue{false}},
            "assigning false again must disable the button");
        expect(document.set_prop_val(owner, property, std::string{"true"}) == PropertyMutationStatus::invalid_value,
            "list choice mode must reject non-boolean values");
    }
}

void test_input_incomplete_choice_mode_api() {
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));
    const auto property = PropertyId::from_name("IncompleteChoiceMode");
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId id{2 + index};
        document.add_control(ControlNode{id, "Input" + std::to_string(index), InputFieldPayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        const auto initial = document.get_prop_val(owner, property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value &&
            *initial.value == PropertyValue{EnumerationValue{"IncompleteChoiceMode", "OnEnterPressed"}},
            "incomplete choice mode must default to OnEnterPressed");
        expect(document.set_prop_val(owner, property, EnumerationValue{"IncompleteChoiceMode", "OnActivate"}) ==
            PropertyMutationStatus::applied, "incomplete choice mode must accept OnActivate");
        const auto read = document.get_prop_val(owner, property);
        expect(read.value && *read.value == PropertyValue{EnumerationValue{"IncompleteChoiceMode", "OnActivate"}},
            "incomplete choice mode must return the assigned value");
        expect(document.set_prop_val(owner, property, EnumerationValue{"IncompleteChoiceMode", "Other"}) ==
            PropertyMutationStatus::invalid_value, "incomplete choice mode must reject unsupported members");
    }
}

void test_chart_refresh_runtime_property() {
    using namespace oof::model::metamodel;
    const auto* descriptor = find_property(ControlKind::chart, "RefreshEnabled");
    expect(descriptor && descriptor->persistence == PersistenceClass::runtime_only &&
        descriptor->storage_codec == StorageCodec::none && descriptor->default_value.kind == DefaultKind::boolean &&
        descriptor->default_value.canonical == "true", "Chart RefreshEnabled must have the observed runtime default");
    for (const auto name : {"ShowTitle", "ShowLegend"}) {
        const auto* property = find_property(ControlKind::chart, name);
        expect(property && property->version_mask == VersionMask::platform_8_2_and_8_5 &&
            property->persistence == PersistenceClass::persisted_editable &&
            property->storage_codec == StorageCodec::control_info &&
            property->default_value.kind == DefaultKind::boolean && property->default_value.canonical == "true",
            "Chart display properties must have observed storage, default, and 8.5 availability");
    }
    Form form; form.id = ObjectId{1}; OrdinaryFormDocument document(std::move(form));
    const auto id = PropertyId::from_name("RefreshEnabled");
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId object_id{2 + index};
        document.add_control(ControlNode{object_id, "Chart" + std::to_string(index), ChartPayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, object_id};
        expect(document.get_prop_val(owner, id).value == std::optional<PropertyValue>{PropertyValue{true}},
            "new Chart refresh must default to true");
        expect(document.set_prop_val(owner, id, false) == PropertyMutationStatus::applied_runtime_only &&
            document.get_prop_val(owner, id).value == std::optional<PropertyValue>{PropertyValue{false}},
            "Chart refresh must be mutable runtime state");
    }
}

void test_panel_scrolling_tab_read() {
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));
    const auto tabs = PropertyId::from_name("ShowTabs");
    const auto scroll = PropertyId::from_name("ScrollPageMode");
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId id{2 + index};
        document.add_control(ControlNode{id, "Panel" + std::to_string(index), PanelPayload{}});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        const auto initial = document.get_prop_val(owner, tabs);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value &&
            *initial.value == PropertyValue{EnumerationValue{"ShowTabs", "Top"}},
            "a panel without scrolling must expose its tab default");
        expect(document.set_prop_val(owner, scroll, true) == PropertyMutationStatus::applied,
            "scroll mode must accept true");
        expect(document.set_prop_val(owner, scroll, false) == PropertyMutationStatus::applied &&
                document.get_prop_val(owner, scroll).value == PropertyValue{true},
            "the platform ignores attempts to disable an active scrolling mode");
        const auto scrolling = document.get_prop_val(owner, tabs);
        expect(scrolling.status == PropertyReadStatus::computed_value && scrolling.value &&
            *scrolling.value == PropertyValue{UndefinedValue{}},
            "scrolling must expose undefined when no tab value is explicitly assigned");
        expect(document.set_prop_val(owner, tabs, EnumerationValue{"ShowTabs", "Bottom"}) == PropertyMutationStatus::applied,
            "an explicitly assigned tab mode must remain readable");
        const auto assigned = document.get_prop_val(owner, tabs);
        expect(assigned.status == PropertyReadStatus::explicit_value && assigned.value &&
            *assigned.value == PropertyValue{EnumerationValue{"ShowTabs", "Bottom"}},
            "an assigned tab mode must be read while scrolling remains enabled");
        expect(document.reset_prop_val(owner, tabs) == PropertyMutationStatus::reset,
            "tab mode can be reset");
        expect(document.get_prop_val(owner, tabs).status == PropertyReadStatus::computed_value,
            "resetting the explicit tab mode must reveal the computed value");
    }
}

void test_chart_base_value_property() {
    using namespace oof::model::metamodel;
    const auto* descriptor = find_property(ControlKind::chart, "BaseValue");
    expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
        descriptor->storage_codec == StorageCodec::control_info && descriptor->value_codec == ValueCodec::decimal &&
        descriptor->default_value.kind == DefaultKind::decimal && descriptor->default_value.canonical == "0",
        "Chart BaseValue must use decimal storage and its observed zero default");
    Form form;
    form.id = ObjectId{1};
    form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument document(std::move(form));
    document.add_control(ControlNode{ObjectId{2}, "Chart", ChartPayload{}});
    const auto chart = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{2}};
    const auto property = PropertyId::from_name("BaseValue");
    const auto default_value = document.get_prop_val(chart, property);
    expect(default_value.status == PropertyReadStatus::proven_default && default_value.value &&
        *default_value.value == PropertyValue{DecimalValue{"0"}}, "Chart BaseValue default must be zero");
    for (const auto* text : {"12345678901234567890123456789012345678", "0.00000000000000000000000000000000000001",
             "-123456789012345.12345678901234567890123", "0"}) {
        const PropertyValue value = DecimalValue{text};
        expect(document.set_prop_val(chart, property, value) == PropertyMutationStatus::applied,
            "Chart BaseValue must accept a decimal value");
        const auto read = document.get_prop_val(chart, property);
        expect(read.value && *read.value == value && document.validate().ok(),
            "Chart BaseValue property API must retain decimal precision");
    }
    expect(document.set_prop_val(chart, property, true) == PropertyMutationStatus::invalid_value &&
        document.set_prop_val(chart, property, std::string{"1.5"}) == PropertyMutationStatus::invalid_value,
        "Chart BaseValue must reject unrelated property types");
}

void test_chart_type_property() {
    using namespace oof::model::metamodel;
    const auto* descriptor = find_property(ControlKind::chart, "ChartType");
    expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
               descriptor->storage_codec == StorageCodec::control_info && descriptor->value_codec == ValueCodec::enumeration &&
               descriptor->default_value.kind == DefaultKind::enumeration && descriptor->default_value.canonical == "ChartType.Column",
        "ChartType must have its observed descriptor and default");
    Form form;
    form.id = ObjectId{1};
    form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument document(std::move(form));
    document.add_control(ControlNode{ObjectId{2}, "Chart", ChartPayload{}});
    const auto chart = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{2}};
    const auto property = PropertyId::from_name("ChartType");
    const auto default_value = document.get_prop_val(chart, property);
    expect(default_value.status == PropertyReadStatus::proven_default && default_value.value &&
               *default_value.value == PropertyValue{EnumerationValue{"ChartType", "Column"}},
        "ChartType default must be Column");
    expect(detail::chart_type_members.size() == 47, "ChartType must expose 47 mapped values");
    std::set<int> ordinals;
    std::set<std::string_view> names;
    for (const auto& [name, ordinal] : detail::chart_type_members) {
        expect(ordinals.insert(ordinal).second && ordinal >= 0 && ordinal < 47 && names.insert(name).second,
            "ChartType mappings must be unique and cover the ordinal domain");
        const EnumerationValue value{"ChartType", std::string(name)};
        expect(document.set_prop_val(chart, property, value) == PropertyMutationStatus::applied,
            "each mapped ChartType must be accepted by the property API");
        const auto read = document.get_prop_val(chart, property);
        expect(read.value && *read.value == PropertyValue{value} && document.validate().ok(),
            "each mapped ChartType must be readable and valid");
    }
    for (const auto& invalid : {EnumerationValue{"ChartType", "Unknown"}, EnumerationValue{"OtherType", "Column"}}) {
        expect(document.set_prop_val(chart, property, invalid) == PropertyMutationStatus::invalid_value,
            "ChartType must reject an invalid name or type in the property API");
        Form invalid_form;
        invalid_form.id = ObjectId{1};
        invalid_form.children = {ControlRef{ObjectId{2}}};
        OrdinaryFormDocument invalid_document(std::move(invalid_form));
        ControlNode invalid_chart{ObjectId{2}, "Chart", ChartPayload{}};
        invalid_chart.properties().set_explicit(property, invalid);
        invalid_document.add_control(std::move(invalid_chart));
        expect(invalid_document.validate().has(InvariantCode::invalid_property), "ChartType validation must reject an invalid name or type");
    }
    expect(document.set_prop_val(chart, property, true) == PropertyMutationStatus::invalid_value,
        "ChartType must reject a non-enumeration value");
}

void test_chart_enum_properties() {
    using namespace oof::model::metamodel;
    Form form;
    form.id = ObjectId{1};
    form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument document(std::move(form));
    document.add_control(ControlNode{ObjectId{2}, "Chart", ChartPayload{}});
    const auto chart = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{2}};
    for (const auto& spec : detail::chart_enum_specs) {
        const auto* descriptor = find_property(ControlKind::chart, spec.property);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == StorageCodec::control_info && descriptor->value_codec == ValueCodec::enumeration &&
                   descriptor->default_value.kind == DefaultKind::enumeration && descriptor->default_value.canonical == spec.default_qualified,
            "Chart enumeration descriptor must have proven storage and default");
        const auto property = PropertyId::from_name(spec.property);
        const auto initial = document.get_prop_val(chart, property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value &&
                   *initial.value == PropertyValue{EnumerationValue{std::string(spec.type_name), std::string(spec.members[spec.default_ordinal])}},
            "Chart enumeration default must materialize with its platform type and member");
        std::set<std::string_view> seen;
        for (const auto member : spec.members) {
            expect(seen.insert(member).second, "Chart enumeration names must be unique");
            const EnumerationValue value{std::string(spec.type_name), std::string(member)};
            expect(document.set_prop_val(chart, property, value) == PropertyMutationStatus::applied,
                "Chart enumeration member must be accepted by SetPropVal");
            const auto read = document.get_prop_val(chart, property);
            expect(read.value && *read.value == PropertyValue{value} && document.validate().ok(),
                "Chart enumeration member must be readable and valid");
        }
        for (const auto& invalid : {EnumerationValue{std::string(spec.type_name), "Unknown"},
                 EnumerationValue{"OtherType", std::string(spec.members[spec.default_ordinal])}}) {
            expect(document.set_prop_val(chart, property, invalid) == PropertyMutationStatus::invalid_value,
                "Chart enumeration must reject unknown member and type");
            Form invalid_form;
            invalid_form.id = ObjectId{1};
            invalid_form.children = {ControlRef{ObjectId{2}}};
            OrdinaryFormDocument invalid_document(std::move(invalid_form));
            ControlNode invalid_chart{ObjectId{2}, "Chart", ChartPayload{}};
            invalid_chart.properties().set_explicit(property, invalid);
            invalid_document.add_control(std::move(invalid_chart));
            expect(invalid_document.validate().has(InvariantCode::invalid_property),
                "Chart enumeration validation must reject invalid member and type");
        }
        expect(document.set_prop_val(chart, property, true) == PropertyMutationStatus::invalid_value,
            "Chart enumeration must reject a non-enumeration value");
    }
}

void test_table_enum_properties() {
    using namespace oof::model::metamodel;
    Form form;
    form.id = ObjectId{1};
    form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument document(std::move(form));
    document.add_control(ControlNode{ObjectId{2}, "Table", TablePayload{}});
    const auto table = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{2}};
    const auto* row_descriptor = find_property(ControlKind::table, "RowSelectionMode");
    expect(row_descriptor && row_descriptor->api_access == ApiAccess::read_write,
        "RowSelectionMode must be writable as observed on the platform");
    for (const auto& spec : detail::table_enum_specs) {
        const auto* descriptor = find_property(ControlKind::table, spec.property);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == StorageCodec::control_info && descriptor->value_codec == ValueCodec::enumeration &&
                   descriptor->default_value.kind == DefaultKind::enumeration && descriptor->default_value.canonical == spec.default_qualified,
            "Table enumeration descriptor must expose its observed storage and default");
        const auto property = PropertyId::from_name(spec.property);
        const auto initial = document.get_prop_val(table, property);
        const EnumerationValue default_value{std::string(spec.type_name), std::string(spec.members[spec.default_ordinal])};
        expect(initial.status == PropertyReadStatus::proven_default && initial.value && *initial.value == PropertyValue{default_value},
            "Table enumeration default must use the platform type and member");
        std::set<std::string_view> seen;
        for (const auto member : spec.members) {
            expect(seen.insert(member).second, "Table enumeration member names must be unique");
            const EnumerationValue value{std::string(spec.type_name), std::string(member)};
            expect(document.set_prop_val(table, property, value) == PropertyMutationStatus::applied,
                "Table enumeration member must be accepted by SetPropVal");
            const auto read = document.get_prop_val(table, property);
            expect(read.value && *read.value == PropertyValue{value}, "Table enumeration member must be readable");
        }
        for (const auto& invalid : {EnumerationValue{std::string(spec.type_name), "Unknown"},
                 EnumerationValue{"OtherType", std::string(spec.members[spec.default_ordinal])}}) {
            expect(document.set_prop_val(table, property, invalid) == PropertyMutationStatus::invalid_value,
                "Table enumeration must reject an unknown member or type");
            Form invalid_form;
            invalid_form.id = ObjectId{1};
            invalid_form.children = {ControlRef{ObjectId{2}}};
            OrdinaryFormDocument invalid_document(std::move(invalid_form));
            ControlNode invalid_table{ObjectId{2}, "Table", TablePayload{}};
            invalid_table.properties().set_explicit(property, invalid);
            invalid_document.add_control(std::move(invalid_table));
            expect(invalid_document.validate().has(InvariantCode::invalid_property),
                "Table enumeration validation must reject an unknown member or type");
        }
        expect(document.set_prop_val(table, property, true) == PropertyMutationStatus::invalid_value,
            "Table enumeration must reject a non-enumeration value");
        expect(document.reset_prop_val(table, property) == PropertyMutationStatus::reset,
            "Table enumeration reset must remove the explicit property");
        const auto reset = document.get_prop_val(table, property);
        expect(reset.status == PropertyReadStatus::proven_default && reset.value && *reset.value == PropertyValue{default_value},
            "Table enumeration reset must recover the default");
    }
}

void test_output_enum_properties() {
    using namespace oof::model::metamodel;
    struct Case {
        ControlKind kind;
        ControlPayload payload;
    };
    const std::array cases{
        Case{ControlKind::html_document_field, HtmlDocumentFieldPayload{}},
        Case{ControlKind::geographical_schema_field, GeographicalSchemaFieldPayload{}},
        Case{ControlKind::graphical_schema_field, GraphicalSchemaFieldPayload{}},
        Case{ControlKind::table, TablePayload{}},
    };
    constexpr std::array<std::string_view, 3> members{"Auto", "Enable", "Disable"};
    const auto property = PropertyId::from_name("Output");
    for (std::size_t index = 0; index < cases.size(); ++index) {
        Form form;
        form.id = ObjectId{1};
        const ObjectId control_id{static_cast<std::uint64_t>(index + 2)};
        form.children = {ControlRef{control_id}};
        OrdinaryFormDocument document(std::move(form));
        document.add_control(ControlNode{control_id, "OutputControl", cases[index].payload});
        const auto control = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, control_id};
        const auto* descriptor = find_property(cases[index].kind, "Output");
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == StorageCodec::control_info &&
                   descriptor->value_codec == ValueCodec::enumeration &&
                   descriptor->default_value.kind == DefaultKind::enumeration && descriptor->default_value.canonical == (cases[index].kind == ControlKind::table ? "Output.Auto" : "Auto"),
            "Output descriptors must use editable control-info enumeration with Auto default");
        const EnumerationValue default_value{"Output", "Auto"};
        const auto initial = document.get_prop_val(control, property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value &&
                   *initial.value == PropertyValue{default_value},
            "Output API default must materialize as Output.Auto");
        for (const auto member : members) {
            const EnumerationValue value{"Output", std::string(member)};
            expect(document.set_prop_val(control, property, value) == PropertyMutationStatus::applied,
                "Output enum member must be accepted");
            const auto read = document.get_prop_val(control, property);
            expect(read.value && *read.value == PropertyValue{value}, "Output enum member must round-trip through the API");
        }
        for (const auto& invalid : {EnumerationValue{"Output", "Unknown"}, EnumerationValue{"OtherType", "Auto"}})
            expect(document.set_prop_val(control, property, invalid) == PropertyMutationStatus::invalid_value,
                "Output API must reject unknown members and wrong enum types");
        expect(document.set_prop_val(control, property, true) == PropertyMutationStatus::invalid_value,
            "Output API must reject non-enumeration values");
        expect(document.reset_prop_val(control, property) == PropertyMutationStatus::reset,
            "Output API reset must remove explicit value");
        const auto reset = document.get_prop_val(control, property);
        expect(reset.status == PropertyReadStatus::proven_default && reset.value &&
                   *reset.value == PropertyValue{default_value},
            "Output API reset must restore Output.Auto");
    }
}

void test_chart_appearance_defaults() {
    using namespace oof::model::metamodel;
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));
    document.add_control(ControlNode{ObjectId{2}, "Chart", ChartPayload{}});
    const auto chart = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{2}};
    const auto* label_font = find_property(ControlKind::chart, "LabelFont");
    expect(label_font && label_font->persistence == PersistenceClass::persisted_editable &&
               label_font->storage_codec == StorageCodec::control_info &&
               label_font->value_codec == ValueCodec::font &&
               label_font->default_value.kind == DefaultKind::font &&
               label_font->default_value.canonical == "automatic",
        "Chart LabelFont must use persisted font storage and the automatic default");
    const auto font_id = PropertyId::from_name("LabelFont");
    const auto default_font = document.get_prop_val(chart, font_id);
    expect(default_font.status == PropertyReadStatus::proven_default && default_font.value &&
               *default_font.value == PropertyValue{FontValue{}},
        "Chart LabelFont must expose its automatic FontValue default");
    FontValue explicit_font;
    explicit_font.kind = FontKind::absolute;
    explicit_font.face_name = "Arial";
    explicit_font.height = 12;
    expect(document.set_prop_val(chart, font_id, explicit_font) == PropertyMutationStatus::applied,
        "Chart LabelFont must accept a typed font");
    const auto read_font = document.get_prop_val(chart, font_id);
    expect(read_font.status == PropertyReadStatus::explicit_value && read_font.value &&
               *read_font.value == PropertyValue{explicit_font},
        "Chart LabelFont must preserve and return an explicit font");
    expect(document.reset_prop_val(chart, font_id) == PropertyMutationStatus::reset &&
               document.get_prop_val(chart, font_id).status == PropertyReadStatus::proven_default,
        "resetting Chart LabelFont must restore its automatic default");
    for (const auto& [name, canonical, is_border] : {
             std::tuple{"Border", "WithoutBorder:0", true}, std::tuple{"LabelBorder", "Single:1", true},
             std::tuple{"BorderColor", "StyleColors.BorderColor", false},
             std::tuple{"LabelBorderColor", "automatic", false},
             std::tuple{"LabelTextColor", "StyleColors.FormTextColor", false},
             std::tuple{"LabelBackColor", "automatic", false},
             std::tuple{"GaugeBushColor", "RGB(169,169,169)", false}}) {
        const auto* descriptor = find_property(ControlKind::chart, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == StorageCodec::control_info &&
                   descriptor->value_codec == (is_border ? ValueCodec::border : ValueCodec::color) &&
                   descriptor->default_value.kind == (is_border ? DefaultKind::border : DefaultKind::color) &&
                   descriptor->default_value.canonical == canonical,
            std::string("Chart appearance descriptor must match observed storage and default: ") + name);
        const auto result = document.get_prop_val(chart, PropertyId::from_name(name));
        expect(result.status == PropertyReadStatus::proven_default && result.value,
            std::string("Chart appearance default must be readable: ") + name);
        if (!result.value) continue;
        if (is_border) {
            const auto* border = std::get_if<BorderValue>(&*result.value);
            const bool single = std::string_view(name) == "LabelBorder";
            expect(border && border->kind == BorderKind::absolute &&
                       border->border_type == (single ? ControlBorderType::single : ControlBorderType::without_border) &&
                       border->width == (single ? 1U : 0U), "Chart border must have its typed default");
        } else {
            const auto* color = std::get_if<ColorValue>(&*result.value);
            expect(color != nullptr, "Chart color must have a typed default");
            if (!color) continue;
            if (std::string_view(name) == "GaugeBushColor") {
                expect(color->kind == ColorKind::absolute && color->red == 169 && color->green == 169 &&
                           color->blue == 169 && color->alpha == 255, "Chart gauge color must be opaque RGB");
            } else if (std::string_view(canonical) == "automatic") {
                expect(color->kind == ColorKind::automatic, "Chart color must be automatic");
            } else {
                const auto* style = std::get_if<QualifiedName>(&color->style);
                expect(color->kind == ColorKind::style_reference && style && style->value == canonical,
                    "Chart color must reference its named style");
            }
        }
    }
}

void test_help_metamodel() {
    using namespace oof::model::metamodel;

    const MetamodelCoverage& coverage = metamodel_coverage();
    expect(coverage.target_platform_version == "8.5.1.1343" && coverage.target_control_count == 25 &&
               coverage.excluded_control_count == 1 && coverage.excluded_control_property_occurrences == 2,
        "coverage target must explicitly exclude ActiveX without changing the help catalog");
    expect(coverage.control_count == 26, "help catalog must cover 26 controls");
    expect(
        coverage.control_property_occurrences == 424,
        "executable metamodel must include Chart and spreadsheet properties");
    expect(
        coverage.unique_control_property_names == 207,
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
    expect(coverage.form_property_count == 39, "Form descriptors must include the two platform-confirmed context properties");
    expect(coverage.form_event_count == 19, "Form must retain 13 common, four shared CatalogItem/Document and two DataProcessor extension events");
    for (const auto name : {"BeforeWrite", "OnWrite", "AfterWrite", "OnDataChange"}) {
        const auto* descriptor = find_form_event(name);
        expect(descriptor && descriptor->extension_kinds.size() == 2 &&
            form_event_uses_table(*descriptor, FormExtensionKind::catalog_item) &&
            form_event_uses_table(*descriptor, FormExtensionKind::document) &&
            descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == StorageCodec::event_record && descriptor->storage_event_id &&
            !descriptor->storage_tag.empty(), "CatalogItem and Document events require shared scoped storage descriptors");
    }
    expect(coverage.property_id_collisions == 0, "property IDs must not collide");
    expect(!coverage.release_ready, "unclassified storage/defaults must block release");
    expect(coverage.unclassified_properties == 0 && coverage.missing_storage_codecs == 0 &&
            coverage.unclassified_value_codecs == 14,
        "release coverage is blocked by 14 unclassified live value codecs");

    expect(
        property_descriptors(ControlKind::input_field).size() == 45,
        "InputField must retain the exact 45-property TextBox surface");
    const auto* choice_enabled = find_property(ControlKind::choice_field, "Enabled");
    const auto* choice_tool_tip = find_property(ControlKind::choice_field, "ToolTip");
    const auto* choice_border_color = find_property(ControlKind::choice_field, "BorderColor");
    const auto* choice_list = find_property(ControlKind::choice_field, "ChoiceList");
    expect(choice_enabled && choice_enabled->persistence == PersistenceClass::persisted_editable &&
            choice_enabled->storage_codec == StorageCodec::control_base &&
            choice_enabled->default_value.kind == DefaultKind::boolean &&
            choice_enabled->default_value.canonical == "true",
        "ChoiceField Enabled must use the observed true base default");
    expect(choice_tool_tip && choice_tool_tip->persistence == PersistenceClass::persisted_editable &&
            choice_tool_tip->storage_codec == StorageCodec::control_base &&
            choice_tool_tip->default_value.kind == DefaultKind::string &&
            choice_tool_tip->default_value.canonical.empty(),
        "ChoiceField ToolTip must use the observed empty base string default");
    expect(choice_border_color && choice_border_color->persistence == PersistenceClass::persisted_editable &&
            choice_border_color->storage_codec == StorageCodec::control_base &&
            choice_border_color->default_value.kind == DefaultKind::color &&
            choice_border_color->default_value.canonical == "automatic",
        "ChoiceField BorderColor must describe its automatic persisted color default");
    expect(choice_list && choice_list->persistence == PersistenceClass::runtime_only &&
            choice_list->storage_codec == StorageCodec::none &&
            choice_list->value_codec == ValueCodec::value_list_reference &&
            choice_list->default_value.kind == DefaultKind::none,
        "ChoiceField ChoiceList must remain runtime-only rather than a persisted XML value");
    const auto* input_choice_list = find_property(ControlKind::input_field, "ChoiceList");
    expect(input_choice_list && input_choice_list->persistence == PersistenceClass::runtime_only &&
        input_choice_list->storage_codec == StorageCodec::none &&
        input_choice_list->value_codec == ValueCodec::value_list_reference,
        "InputField ChoiceList remains runtime state with a live reference codec");
    expect(
        event_descriptors(ControlKind::input_field).size() == 9,
        "InputField must retain the exact 9-event TextBox surface");
    for (const auto& [name, id] : {std::pair{"Selection", 0U}, std::pair{"DetailProcessing", 1U}}) {
        const auto* event = find_event(ControlKind::dendrogram, name);
        expect(event && event->persistence == PersistenceClass::persisted_editable &&
                event->storage_codec == StorageCodec::event_record && event->storage_event_id == id &&
                event->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c",
            "Dendrogram event descriptors must match observed IDs and tag");
    }
    for (const auto kind : {ControlKind::check_box, ControlKind::radio_button, ControlKind::track_bar}) {
        const auto* on_change = find_event(kind, "OnChange");
        expect(on_change && on_change->persistence == PersistenceClass::persisted_editable &&
                on_change->storage_codec == StorageCodec::event_record &&
                on_change->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
                on_change->storage_event_id == 2147483647U,
            "CheckBox, RadioButton and TrackBar OnChange must expose only the proven storage descriptor");
    }
    expect(
        find_property(ControlKind::input_field, "ReadOnly")->russian_name == u8"ТолькоПросмотр",
        "ReadOnly must resolve from exact bilingual help");
    expect(
        find_property(ControlKind::input_field, "ReadOnly")->value_codec == ValueCodec::boolean,
        "safe Boolean help types must receive the Boolean domain codec");
    const auto* dendrogram_items = find_property(ControlKind::dendrogram, "Items");
    const auto* dendrogram_links = find_property(ControlKind::dendrogram, "Links");
    const auto* dendrogram_orientation = find_property(ControlKind::dendrogram, "Orientation");
    expect(dendrogram_items && dendrogram_items->value_codec == ValueCodec::dendrogram_items &&
            dendrogram_items->storage_codec == StorageCodec::collection_record &&
            dendrogram_items->persistence == PersistenceClass::persisted_editable &&
            dendrogram_links && dendrogram_links->value_codec == ValueCodec::dendrogram_links &&
            dendrogram_links->storage_codec == StorageCodec::collection_record &&
            dendrogram_links->persistence == PersistenceClass::persisted_editable,
        "Dendrogram Items and Links must be named typed owned collections");
    expect(dendrogram_orientation && dendrogram_orientation->value_codec == ValueCodec::enumeration &&
            dendrogram_orientation->default_value.kind == DefaultKind::enumeration &&
            dendrogram_orientation->default_value.canonical == "DendrogramOrientation.Up",
        "Dendrogram Orientation must retain its documented enum type and implicit Up default");
    expect(
        find_property(ControlKind::input_field, "AutoChoiceIncomplete")->persistence == PersistenceClass::persisted_editable &&
            find_property(ControlKind::input_field, "AutoChoiceIncomplete")->storage_codec == StorageCodec::control_info &&
            find_property(ControlKind::input_field, "AutoChoiceIncomplete")->default_value.kind == DefaultKind::boolean &&
            find_property(ControlKind::input_field, "AutoChoiceIncomplete")->default_value.canonical == "false",
        "AutoChoiceIncomplete must have proven Boolean persistence and false default");
    const auto* input_tool_tip = find_property(ControlKind::input_field, "ToolTip");
    const auto* input_format = find_property(ControlKind::input_field, "Format");
    expect(input_tool_tip && input_tool_tip->persistence == PersistenceClass::persisted_editable &&
            input_tool_tip->storage_codec == StorageCodec::control_base &&
            input_tool_tip->default_value.kind == DefaultKind::string &&
            input_tool_tip->default_value.canonical.empty(),
        "InputField ToolTip must declare its empty base-string default");
    expect(input_format && input_format->persistence == PersistenceClass::persisted_editable &&
            input_format->storage_codec == StorageCodec::control_info &&
            input_format->default_value.kind == DefaultKind::string &&
            input_format->default_value.canonical.empty(),
        "InputField Format must declare its empty info-string default");
    const auto* input_horizontal = find_property(ControlKind::input_field, "HorizontalAlign");
    const auto* input_vertical = find_property(ControlKind::input_field, "VerticalAlign");
    const auto* input_choice_height = find_property(ControlKind::input_field, "ChoiceListHeight");
    expect(input_horizontal && input_horizontal->persistence == PersistenceClass::persisted_editable &&
            input_horizontal->storage_codec == StorageCodec::control_info &&
            input_horizontal->default_value.kind == DefaultKind::enumeration &&
            input_horizontal->default_value.canonical == "Auto",
        "InputField HorizontalAlign must declare its Auto enum default");
    expect(input_vertical && input_vertical->persistence == PersistenceClass::persisted_editable &&
            input_vertical->storage_codec == StorageCodec::control_info &&
            input_vertical->default_value.kind == DefaultKind::enumeration &&
            input_vertical->default_value.canonical == "Top",
        "InputField VerticalAlign must declare its Top enum default");
    expect(input_choice_height && input_choice_height->persistence == PersistenceClass::persisted_editable &&
            input_choice_height->storage_codec == StorageCodec::control_info &&
            input_choice_height->default_value.kind == DefaultKind::integer &&
            input_choice_height->default_value.canonical == "0" &&
            input_choice_height->value_codec == ValueCodec::integer32,
        "InputField ChoiceListHeight must declare its int32 zero default");
    expect(
        find_property(ControlKind::input_field, "AutoMarkIncomplete")->persistence == PersistenceClass::persisted_editable &&
            find_property(ControlKind::input_field, "AutoMarkIncomplete")->storage_codec == StorageCodec::control_info &&
            find_property(ControlKind::input_field, "AutoMarkIncomplete")->default_value.kind == DefaultKind::boolean &&
            find_property(ControlKind::input_field, "AutoMarkIncomplete")->default_value.canonical == "false",
        "AutoMarkIncomplete must have proven Boolean persistence and false default");
    for (const auto& [name, default_value] : std::array<std::pair<std::string_view, std::string_view>, 12>{
             {{"Wrap", "true"}, {"ChooseType", "true"}, {"MarkNegatives", "false"},
              {"ChoiceButton", "false"}, {"OpenButton", "false"}, {"ClearButton", "false"},
              {"SpinButton", "false"}, {"ChoiceListButton", "false"}, {"Transparent", "false"},
              {"MultiLine", "false"}, {"ExtendedEdit", "false"},
              {"PasswordMode", "false"}}}) {
        const auto* descriptor = find_property(ControlKind::input_field, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                descriptor->storage_codec == (name == "Transparent" ? StorageCodec::control_base : StorageCodec::control_info) &&
                descriptor->default_value.kind == DefaultKind::boolean &&
                descriptor->default_value.canonical == default_value,
            "proven InputField Boolean must have its persisted descriptor and default");
    }
    expect(find_property(ControlKind::input_field, "TextEdit")->persistence == PersistenceClass::persisted_editable &&
            find_property(ControlKind::input_field, "TextEdit")->storage_codec == StorageCodec::control_info,
        "TextEdit must use the verified InputField codec");
    for (const auto name : {"ChoiceIncomplete", "MarkIncomplete"}) {
        const auto* descriptor = find_property(ControlKind::input_field, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::runtime_only &&
                descriptor->storage_codec == StorageCodec::none,
            "getter-only InputField Boolean must be classified as runtime-only");
    }
    expect(
        find_property(ControlKind::input_field, "Border")->value_codec ==
            ValueCodec::border &&
        find_property(ControlKind::input_field, "Border")->persistence == PersistenceClass::persisted_editable &&
        find_property(ControlKind::input_field, "Border")->storage_codec == StorageCodec::control_base,
        "InputField Border must use its independently verified base storage");
    expect(
        find_property(ControlKind::input_field, "ValueType")->value_codec ==
            ValueCodec::type_domain,
        "DescriptionOfTypes must use the proven type-domain value model");
    expect(
        find_property(ControlKind::input_field, "Visible")->surface ==
            PropertySurface::panel_placement,
        "Visible must come from the panel-placement extension");
    const auto* calendar = find_by_public_name("CalendarField");
    const auto* calendar_enabled = find_property(ControlKind::calendar_field, "Enabled");
    expect(calendar != nullptr && calendar->guid == "e3c063d8-ef92-41be-9c89-b70290b5368b",
        "CalendarField must use its existing control descriptor GUID");
    expect(calendar_enabled != nullptr &&
               calendar_enabled->persistence == PersistenceClass::persisted_editable &&
               calendar_enabled->storage_codec == StorageCodec::control_base &&
               calendar_enabled->value_codec == ValueCodec::boolean &&
               calendar_enabled->default_value.kind == DefaultKind::boolean &&
               calendar_enabled->default_value.canonical == "true",
        "CalendarField Enabled must use its typed Boolean descriptor and verified default");
    expect(find_property(ControlKind::calendar_field, "Visible") != nullptr &&
               find_property(ControlKind::calendar_field, "Visible")->surface == PropertySurface::panel_placement,
        "CalendarField Visible must remain on the shared Position surface");
    const auto* calendar_current_date = find_property(ControlKind::calendar_field, "CurrentDate");
    expect(calendar_current_date && calendar_current_date->value_codec == ValueCodec::date &&
               calendar_current_date->persistence == PersistenceClass::runtime_only &&
               calendar_current_date->storage_codec == StorageCodec::none &&
               calendar_current_date->default_value.kind == DefaultKind::none,
        "CalendarField CurrentDate must be runtime-only, non-persisted, and have no permanent default");
    expect(
        find_property(ControlKind::input_field, "Top")->value_codec == ValueCodec::integer32,
        "typed Position coordinates must be 32-bit integer codecs in the metamodel");
    expect(
        find_property(ControlKind::radio_button, "FirstInGroup")->surface ==
            PropertySurface::control_extension,
        "inherited RadioButton FirstInGroup must have one shared extension owner");
    for (const auto& control : control_descriptors()) {
        if (control.kind == ControlKind::active_x_control) continue;
        for (const auto name : {"ModifiesData", "SkipOnInput", "FirstInGroup"}) {
            const auto* property = find_property(control.kind, name);
            expect(property && property->surface == PropertySurface::control_extension &&
                       property->value_codec == ValueCodec::boolean &&
                       property->persistence == PersistenceClass::persisted_editable &&
                       property->storage_codec == StorageCodec::control_info &&
                       property->default_value.kind == DefaultKind::boolean &&
                       property->default_value.canonical == "false",
                "common Boolean extension must use the proven descriptor for every target control");
        }
        const auto* automatic = find_property(control.kind, "AutoContextMenu");
        const auto* context = find_property(control.kind, "ContextMenu");
        expect(automatic && automatic->surface == PropertySurface::control_extension &&
                   automatic->value_codec == ValueCodec::boolean &&
                   automatic->persistence == PersistenceClass::persisted_editable &&
                   automatic->storage_codec == StorageCodec::root_record &&
                   automatic->default_value.kind == DefaultKind::boolean &&
                   automatic->default_value.canonical == "true",
            "AutoContextMenu must expose the proven descriptor for every target control");
        expect(context && context->surface == PropertySurface::control_extension &&
                   context->value_codec == ValueCodec::context_menu_reference &&
                   context->persistence == PersistenceClass::persisted_editable &&
                   context->storage_codec == StorageCodec::root_record &&
                   context->default_value.kind == DefaultKind::undefined &&
                   context->default_value.canonical == "undefined",
            "ContextMenu must expose the proven descriptor for every target control");
    }
    const auto* sel_button_picture = find_property(ControlKind::input_field, "SelButtonPicture");
    const auto* choice_button_picture = find_property(ControlKind::input_field, "ChoiceButtonPicture");
    expect(sel_button_picture && sel_button_picture == choice_button_picture,
        "SelButtonPicture must resolve to the proven ChoiceButtonPicture alias");
    expect(choice_button_picture &&
            choice_button_picture->persistence == PersistenceClass::persisted_editable &&
            choice_button_picture->storage_codec == StorageCodec::picture_record &&
            choice_button_picture->value_codec == ValueCodec::picture &&
            choice_button_picture->default_value.kind == DefaultKind::none,
        "InputField ChoiceButtonPicture must use the typed picture record without an assumed default");
    const auto* form_caption_descriptor = find_form_property("Caption");
    expect(
        form_caption_descriptor && form_caption_descriptor->russian_name == u8"Заголовок" &&
            form_caption_descriptor->value_codec == ValueCodec::localized_text &&
            form_caption_descriptor->storage_codec == StorageCodec::root_record &&
            form_caption_descriptor->default_value.kind == DefaultKind::string &&
            form_caption_descriptor->default_value.canonical.empty(),
        "Form Caption must use the existing localized_text codec, root record, and empty string default");
    const auto* auto_title_descriptor = find_form_property("AutoTitle");
    expect(auto_title_descriptor && auto_title_descriptor->value_codec == ValueCodec::boolean &&
               auto_title_descriptor->storage_codec == StorageCodec::root_record &&
               auto_title_descriptor->default_value.kind == DefaultKind::boolean &&
               auto_title_descriptor->default_value.canonical == "true",
        "Form AutoTitle must use the existing Boolean root-record descriptor and true default");
    expect(
        find_event(ControlKind::input_field, "OnChange") != nullptr,
        "InputField OnChange event must be executable metamodel data");
    constexpr std::array field_event_ids{
        std::pair{"StartListChoice", 1U}, std::pair{"StartChoice", 2U}, std::pair{"Clearing", 3U},
        std::pair{"Tuning", 4U}, std::pair{"Opening", 5U}, std::pair{"ChoiceProcessing", 7U},
        std::pair{"OnChange", 2147483647U}, std::pair{"TextEditEnd", 10U}, std::pair{"AutoCompleteText", 11U},
    };
    for (const auto kind : {ControlKind::choice_field, ControlKind::input_field}) {
        for (const auto& [name, id] : field_event_ids) {
            if (kind == ControlKind::choice_field && (id == 10U || id == 11U)) continue;
            const auto* event = find_event(kind, name);
            expect(event && event->persistence == PersistenceClass::persisted_editable &&
                event->storage_codec == StorageCodec::event_record && event->storage_event_id == id &&
                event->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c",
                std::string(name) + " must have its observed field event descriptor");
        }
    }
    constexpr std::array table_events{
        std::pair{"Selection", 34U}, std::pair{"ValueChoice", 48U}, std::pair{"DragStart", 900U},
        std::pair{"ChoiceProcessing", 52U}, std::pair{"NewObjectWriteProcessing", 50U},
        std::pair{"DragEnd", 902U}, std::pair{"BeforeAddRow", 40U}, std::pair{"BeforeRowChange", 41U},
        std::pair{"BeforeEditEnd", 44U}, std::pair{"BeforeDeleteRow", 42U}, std::pair{"Drag", 903U},
        std::pair{"AfterDeleteLine", 51U}, std::pair{"OnActivateColumn", 36U}, std::pair{"OnActivateRow", 35U},
        std::pair{"OnActivateCell", 37U}, std::pair{"OnRowOutput", 47U}, std::pair{"OnCheckChange", 45U},
        std::pair{"OnStartEdit", 43U}, std::pair{"OnEditEnd", 49U}, std::pair{"OnDataGet", 53U},
        std::pair{"DragCheck", 901U},
    };
    for (const auto& [name, id] : table_events) {
        const auto* event = find_event(ControlKind::table, name);
        expect(event && event->persistence == PersistenceClass::persisted_editable &&
            event->storage_codec == StorageCodec::event_record && event->storage_event_id == id &&
            event->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c",
            std::string(name) + " must have its observed Table event descriptor");
    }
    for (const auto name : {"CurrentRow", "CurrentData", "SelectedRows", "CurrentColumn", "CurrentParent"}) {
        const auto* property = find_property(ControlKind::table, name);
        expect(property && property->persistence == PersistenceClass::runtime_only &&
                   property->storage_codec == StorageCodec::none,
            std::string(name) + " must be classified as runtime state without a storage codec");
        if (std::string_view{name} == "CurrentRow" || std::string_view{name} == "CurrentData" ||
            std::string_view{name} == "CurrentParent") {
            expect(property->value_kind == ValueKind::variant && property->value_codec == ValueCodec::unclassified,
                "table row and parent value types depend on the data extension, with live codecs still unfinished");
        }
    }
    const auto* current_parent = find_event(ControlKind::table, "OnCurrentParentChange");
    expect(current_parent && current_parent->persistence == PersistenceClass::persisted_editable &&
        current_parent->storage_event_id == 46, "Table OnCurrentParentChange must use the observed storage ID");
    constexpr std::array form_event_ids{
        std::pair{"BeforeOpen", 70000U}, std::pair{"OnOpen", 70001U},
        std::pair{"BeforeClose", 70002U}, std::pair{"OnClose", 70003U},
        std::pair{"ChoiceProcessing", 70004U}, std::pair{"ObjectActivationProcessing", 70005U},
        std::pair{"NewObjectWriteProcessing", 70006U}, std::pair{"NotificationProcessing", 70007U},
        std::pair{"OnReopen", 70008U}, std::pair{"RefreshDisplay", 70009U},
        std::pair{"ExternalEvent", 70010U}, std::pair{"AddInDetachmentOnError", 70011U},
        std::pair{"FillCheckProcessing", 70012U}};
    for (const auto& [name, id] : form_event_ids) {
        const auto* event = find_form_event(name);
        expect(event && event->persistence == PersistenceClass::persisted_editable &&
            event->storage_codec == StorageCodec::event_record && event->storage_event_id == id &&
            event->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c", "Form event descriptor must match observed storage");
    }
    const auto* form_close = find_form_event("OnClose");
    expect(form_close != nullptr && form_close->persistence == PersistenceClass::persisted_editable &&
        form_close->storage_codec == StorageCodec::event_record &&
        form_close->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c",
        "Form.OnClose must expose its proven action descriptor");
    const auto* button_click = find_event(ControlKind::button, "Click");
    expect(
        button_click != nullptr &&
            button_click->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
            button_click->storage_codec == StorageCodec::event_record,
        "Button.Click must retain its proven storage identity and codec");
    const auto* label_click = find_event(ControlKind::label_decoration, "Click");
    expect(label_click && label_click->persistence == PersistenceClass::persisted_editable &&
               label_click->storage_codec == StorageCodec::event_record &&
               label_click->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
               label_click->storage_event_id.has_value() && *label_click->storage_event_id == 0,
        "LabelDecoration.Click must use the shared event record descriptor and observed storage ID zero");
    const auto* picture_click = find_event(ControlKind::picture_decoration, "Click");
    expect(picture_click && picture_click->persistence == PersistenceClass::persisted_editable &&
               picture_click->storage_codec == StorageCodec::event_record &&
               picture_click->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
               picture_click->storage_event_id.has_value() && *picture_click->storage_event_id == 0,
        "PictureDecoration.Click must use the shared event record descriptor and observed storage ID zero");
    const auto* panel_change = find_event(ControlKind::panel, "OnCurrentPageChange");
    expect(panel_change && panel_change->persistence == PersistenceClass::persisted_editable &&
        panel_change->storage_codec == StorageCodec::event_record &&
        panel_change->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
        panel_change->storage_event_id == 0U, "Panel page change must have the observed event descriptor");
    for (const auto& [kind, name, id] : std::array{
             std::tuple{ControlKind::picture_decoration, "DragStart", 3U},
             std::tuple{ControlKind::picture_decoration, "DragCheck", 4U},
             std::tuple{ControlKind::picture_decoration, "DragEnd", 5U},
             std::tuple{ControlKind::picture_decoration, "Drag", 6U},
             std::tuple{ControlKind::list_box, "Selection", 34U},
             std::tuple{ControlKind::list_box, "OnActivateRow", 37U},
             std::tuple{ControlKind::list_box, "OnCheckChange", 17U}}) {
        const auto* descriptor = find_event(kind, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == StorageCodec::event_record &&
            descriptor->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
            descriptor->storage_event_id == id, std::string(name) + " must have its proven event descriptor");
    }
    for (const auto& [name, id] : std::array{std::pair{"Selection", 0U}, std::pair{"DetailProcessing", 1U}}) {
        const auto* descriptor = find_event(ControlKind::chart, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == StorageCodec::event_record &&
            descriptor->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
            descriptor->storage_event_id == id, std::string(name) + " must have its observed Chart event descriptor");
    }
    for (const auto name : {"Enabled", "VerticalScrollBar", "HorizontalScrollBar", "EnableStartDrag", "EnableDrag"}) {
        const auto* descriptor = find_property(ControlKind::spreadsheet_document_field, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == StorageCodec::control_info && descriptor->default_value.kind == DefaultKind::boolean &&
            descriptor->default_value.canonical == "true", std::string(name) + " must have its observed default");
    }
    const auto* spreadsheet_border_color = find_property(ControlKind::spreadsheet_document_field, "BorderColor");
    expect(spreadsheet_border_color && spreadsheet_border_color->storage_codec == StorageCodec::control_info &&
        spreadsheet_border_color->default_value.canonical == "automatic", "spreadsheet BorderColor must default to automatic");
    const auto* sheet_border = find_property(ControlKind::spreadsheet_document_field, "Border");
    const auto* sheet_selection = find_property(ControlKind::spreadsheet_document_field, "ShowSelection");
    expect(sheet_border && sheet_border->storage_codec == StorageCodec::control_info &&
        sheet_border->default_value.canonical == "StyleBorders.ControlBorder", "spreadsheet border style default must be named");
    expect(sheet_selection && sheet_selection->storage_codec == StorageCodec::control_info &&
        sheet_selection->default_value.canonical == "SpreadsheetDocumentSelectionShowModeType.WhenActive", "spreadsheet selection default must be named");
    for (const auto& [name, id] : std::array{std::pair{"Selection", 0U}, std::pair{"DetailProcessing", 1U},
        std::pair{"DragStart", 3U}, std::pair{"DragCheck", 4U}, std::pair{"DragEnd", 5U}, std::pair{"Drag", 6U},
        std::pair{"OnChangeAreaContent", 7U}}) {
        const auto* descriptor = find_event(ControlKind::spreadsheet_document_field, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == StorageCodec::event_record && descriptor->storage_event_id == id &&
            descriptor->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c", "spreadsheet event descriptor must match the platform");
    }
    for (const auto name : {"Selection", "DetailProcessing"}) {
        const auto* descriptor = find_event(ControlKind::pivot_chart, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::unsupported_by_platform &&
            descriptor->storage_codec == StorageCodec::none && descriptor->storage_tag.empty() &&
            !descriptor->storage_event_id,
            std::string("PivotChart ") + name + " must remain classified as unsupported without storage metadata");
    }
    const auto* spreadsheet_activate_area = find_event(ControlKind::spreadsheet_document_field, "OnActivateArea");
    expect(spreadsheet_activate_area &&
        spreadsheet_activate_area->persistence == PersistenceClass::unsupported_by_platform &&
        spreadsheet_activate_area->storage_codec == StorageCodec::none &&
        spreadsheet_activate_area->storage_tag.empty() && !spreadsheet_activate_area->storage_event_id,
        "SpreadsheetDocumentField OnActivateArea must be unsupported without storage metadata");
    for (const auto& [kind, name, id] : std::array{
             std::tuple{ControlKind::gantt_chart, "OnIntervalEditEnd", 2U},
             std::tuple{ControlKind::html_document_field, "DocumentComplete", 0U},
             std::tuple{ControlKind::geographical_schema_field, "Selection", 0U},
             std::tuple{ControlKind::graphical_schema_field, "Selection", 0U},
             std::tuple{ControlKind::graphical_schema_field, "OnActivate", 1U}}) {
        const auto* event = find_event(kind, name);
        expect(event && event->persistence == PersistenceClass::persisted_editable &&
            event->storage_codec == StorageCodec::event_record && event->storage_event_id == id &&
            event->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c", "complex control event descriptor must match platform");
    }
    constexpr std::array calendar_event_storage{
        std::pair{"Selection", 1U}, std::pair{"DragStart", 3U}, std::pair{"DragEnd", 5U},
        std::pair{"Drag", 6U}, std::pair{"OnActivateDate", 0U}, std::pair{"OnPeriodOutput", 2U},
        std::pair{"OnChange", 2147483647U}, std::pair{"DragCheck", 4U},
    };
    std::set<std::uint32_t> calendar_storage_ids;
    for (const auto& [name, storage_id] : calendar_event_storage) {
        const auto* descriptor = find_event(ControlKind::calendar_field, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                descriptor->storage_codec == StorageCodec::event_record &&
                descriptor->storage_tag == "e1692cc2-605b-4535-84dd-28440238746c" &&
                descriptor->storage_event_id == storage_id && calendar_storage_ids.insert(storage_id).second,
            std::string("CalendarField.") + name + " must declare its proven event storage identity");
    }
    const auto expect_common_base_property = [](ControlKind kind, std::string_view name, DefaultKind default_kind) {
        const auto* descriptor = find_property(kind, name);
        return descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == StorageCodec::control_base &&
            descriptor->default_value.kind == default_kind && descriptor->default_value.canonical == "automatic";
    };
    for (const auto& [kind, name, default_kind] : std::array{
             std::tuple{ControlKind::calendar_field, std::string_view{"Font"}, DefaultKind::font},
             std::tuple{ControlKind::calendar_field, std::string_view{"BorderColor"}, DefaultKind::color},
             std::tuple{ControlKind::usual_group, std::string_view{"BorderColor"}, DefaultKind::color},
             std::tuple{ControlKind::progress_bar, std::string_view{"Font"}, DefaultKind::font},
             std::tuple{ControlKind::progress_bar, std::string_view{"BorderColor"}, DefaultKind::color},
             std::tuple{ControlKind::track_bar, std::string_view{"BorderColor"}, DefaultKind::color}}) {
        expect(expect_common_base_property(kind, name, default_kind),
            "new Font and BorderColor descriptors must use the automatic common-base default");
    }
    for (const auto kind : {ControlKind::progress_bar, ControlKind::track_bar}) {
        for (const auto name : {std::string_view{"Enabled"}, std::string_view{"ToolTip"}}) {
            const auto* descriptor = find_property(kind, name);
            expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                    descriptor->storage_codec == StorageCodec::control_base,
                "ProgressBar and TrackBar Enabled/ToolTip must match their observed base19 slots");
        }
    }
}

void test_shared_control_default_classifications() {
    using namespace oof::model::metamodel;

    const auto classified = [](ControlKind kind, std::string_view name, StorageCodec storage,
                               DefaultKind default_kind, std::string_view default_value,
                               ValueCodec value_codec) {
        const auto* descriptor = find_property(kind, name);
        return descriptor != nullptr && descriptor->persistence == PersistenceClass::persisted_editable &&
            descriptor->storage_codec == storage && descriptor->default_value.kind == default_kind &&
            descriptor->default_value.canonical == default_value && descriptor->value_codec == value_codec;
    };

    for (const auto kind : {ControlKind::html_document_field, ControlKind::geographical_schema_field,
             ControlKind::graphical_schema_field}) {
        expect(classified(kind, "BorderColor", StorageCodec::control_base,
                   DefaultKind::color, "automatic", ValueCodec::color),
            "document and schema field BorderColor must use the observed automatic default");
    }
    expect(classified(ControlKind::text_document_field, "Border", StorageCodec::control_base,
               DefaultKind::border, "WithoutBorder:0", ValueCodec::border),
        "TextDocumentField Border must use the observed absolute no-border default");

    expect(classified(ControlKind::button, "Enabled", StorageCodec::control_base,
               DefaultKind::boolean, "true", ValueCodec::boolean) &&
               classified(ControlKind::splitter, "Enabled", StorageCodec::control_base,
                   DefaultKind::boolean, "true", ValueCodec::boolean),
        "shared Enabled rule must retain the common control-base classification");
    for (const auto kind : {ControlKind::usual_group, ControlKind::button, ControlKind::radio_button,
             ControlKind::label_decoration, ControlKind::check_box}) {
        expect(classified(kind, "Caption", StorageCodec::control_info,
                   DefaultKind::string, "", ValueCodec::localized_text),
            "the selected ordinary control Caption properties must use the shared localized_text codec");
    }
    expect(classified(ControlKind::progress_bar, "Enabled", StorageCodec::control_base,
               DefaultKind::boolean, "true", ValueCodec::boolean) &&
               classified(ControlKind::label_decoration, "Enabled", StorageCodec::control_base,
                   DefaultKind::boolean, "true", ValueCodec::boolean) &&
               classified(ControlKind::label_decoration, "ToolTip", StorageCodec::control_base,
                   DefaultKind::string, "", ValueCodec::localized_text),
        "LabelDecoration Enabled and ToolTip must use the common control-base slots");
    expect(classified(ControlKind::usual_group, "Font", StorageCodec::control_base,
               DefaultKind::font, "automatic,bold=true", ValueCodec::font),
        "UsualGroup must expose its observed automatic bold Font default");
    constexpr std::array named_base_colors{
        std::tuple{ControlKind::chart, std::string_view{"BackColor"}, std::string_view{"StyleColors.FormBackColor"}},
        std::tuple{ControlKind::gantt_chart, std::string_view{"BackColor"}, std::string_view{"StyleColors.FormBackColor"}},
        std::tuple{ControlKind::dendrogram, std::string_view{"BackColor"}, std::string_view{"StyleColors.FormBackColor"}},
        std::tuple{ControlKind::pivot_chart, std::string_view{"BackColor"}, std::string_view{"StyleColors.FormBackColor"}},
        std::tuple{ControlKind::check_box, std::string_view{"TextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::check_box, std::string_view{"FieldTextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::check_box, std::string_view{"BackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::check_box, std::string_view{"FieldBackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::choice_field, std::string_view{"ButtonTextColor"}, std::string_view{"StyleColors.ButtonTextColor"}},
        std::tuple{ControlKind::choice_field, std::string_view{"FieldTextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::choice_field, std::string_view{"ButtonBackColor"}, std::string_view{"StyleColors.ButtonBackColor"}},
        std::tuple{ControlKind::choice_field, std::string_view{"FieldBackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::usual_group, std::string_view{"TextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::usual_group, std::string_view{"BackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::input_field, std::string_view{"ButtonTextColor"}, std::string_view{"StyleColors.ButtonTextColor"}},
        std::tuple{ControlKind::input_field, std::string_view{"FieldTextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::input_field, std::string_view{"ButtonBackColor"}, std::string_view{"StyleColors.ButtonBackColor"}},
        std::tuple{ControlKind::input_field, std::string_view{"FieldBackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::label_decoration, std::string_view{"TextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::label_decoration, std::string_view{"BackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::list_box, std::string_view{"FieldTextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::list_box, std::string_view{"FieldBackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::picture_decoration, std::string_view{"TextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::picture_decoration, std::string_view{"BackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::progress_bar, std::string_view{"BackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::radio_button, std::string_view{"TextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::radio_button, std::string_view{"FieldTextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::radio_button, std::string_view{"BackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::radio_button, std::string_view{"FieldBackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::track_bar, std::string_view{"TextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::track_bar, std::string_view{"BackColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::track_bar, std::string_view{"ButtonBackColor"}, std::string_view{"StyleColors.ButtonBackColor"}},
        std::tuple{ControlKind::calendar_field, std::string_view{"ButtonTextColor"}, std::string_view{"StyleColors.ButtonTextColor"}},
        std::tuple{ControlKind::calendar_field, std::string_view{"FieldTextColor"}, std::string_view{"automatic"}},
        std::tuple{ControlKind::calendar_field, std::string_view{"ButtonBackColor"}, std::string_view{"StyleColors.ButtonBackColor"}},
        std::tuple{ControlKind::calendar_field, std::string_view{"FieldBackColor"}, std::string_view{"automatic"}},
    };
    for (const auto& [kind, name, default_value] : named_base_colors) {
        expect(classified(kind, name, StorageCodec::control_base, DefaultKind::color,
                   default_value, ValueCodec::color),
            std::string("named base color descriptor mismatch: ") + std::string(name));
    }
    expect(classified(ControlKind::choice_field, "BorderColor", StorageCodec::control_base,
               DefaultKind::color, "automatic", ValueCodec::color) &&
               classified(ControlKind::check_box, "BorderColor", StorageCodec::control_base,
                   DefaultKind::color, "automatic", ValueCodec::color) &&
               classified(ControlKind::text_document_field, "Font", StorageCodec::control_base,
                   DefaultKind::font, "automatic", ValueCodec::font) &&
               classified(ControlKind::choice_field, "Font", StorageCodec::control_base,
                   DefaultKind::font, "automatic", ValueCodec::font) &&
               classified(ControlKind::command_bar, "Font", StorageCodec::control_base,
                   DefaultKind::font, "automatic", ValueCodec::font) &&
               classified(ControlKind::radio_button, "BorderColor", StorageCodec::control_base,
                   DefaultKind::color, "automatic", ValueCodec::color) &&
               classified(ControlKind::input_field, "BorderColor", StorageCodec::control_base,
                   DefaultKind::color, "automatic", ValueCodec::color) &&
               classified(ControlKind::list_box, "BorderColor", StorageCodec::control_base,
                   DefaultKind::color, "automatic", ValueCodec::color) &&
               classified(ControlKind::radio_button, "Font", StorageCodec::control_base,
                   DefaultKind::font, "automatic", ValueCodec::font) &&
               classified(ControlKind::input_field, "Font", StorageCodec::control_base,
                   DefaultKind::font, "automatic", ValueCodec::font) &&
               find_property(ControlKind::list_box, "Font") == nullptr,
        "shared BorderColor and Font rules must retain named codecs and defaults");

    expect(classified(ControlKind::panel, "Enabled", StorageCodec::control_base,
                   DefaultKind::boolean, "true", ValueCodec::boolean) &&
               classified(ControlKind::check_box, "BorderColor", StorageCodec::control_base,
                   DefaultKind::color, "automatic", ValueCodec::color),
        "proven shared properties must retain their descriptors");
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

void test_table_columns_reference() {
    using namespace oof::model::metamodel;
    const auto* descriptor = find_property(ControlKind::table, "Columns");
    expect(descriptor && descriptor->persistence == PersistenceClass::persisted_readonly &&
        descriptor->storage_codec == StorageCodec::collection_record && descriptor->value_codec == ValueCodec::table_columns &&
        descriptor->default_value.kind == DefaultKind::none, "Table.Columns describes its owned persisted collection");
    Form form; form.id = ObjectId{1};
    OrdinaryFormDocument document(form);
    const auto property = PropertyId::from_name("Columns");
    for (std::uint64_t id = 2; id < 14; ++id) {
        ControlNode control; control.id = ObjectId{id}; control.name = "Table" + std::to_string(id);
        TablePayload table;
        for (std::size_t i = 0; i < (id - 2) % 4; ++i) {
            TableColumn column; column.name = "Column" + std::to_string(i); column.data_path = column.name;
            table.columns.push_back(std::move(column));
        }
        control.payload = std::move(table);
        document.add_control(std::move(control));
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{id}};
        const auto result = document.get_prop_val(owner, property);
        expect(result.status == PropertyReadStatus::computed_value && result.value && std::holds_alternative<TableColumnsRef>(*result.value),
            "twelve Table getters expose their owned columns");
        const auto reference = std::get<TableColumnsRef>(*result.value);
        const auto* columns = document.table_columns(reference);
        expect(columns && columns == &std::get<TablePayload>(document.find_control(ObjectId{id})->payload).columns &&
            columns->size() == (id - 2) % 4 && *result.value == *document.get_prop_val(owner, property).value,
            "Table.Columns resolves the authoritative collection with stable identity");
        for (std::size_t i = 0; i < columns->size(); ++i)
            expect((*columns)[i].name == "Column" + std::to_string(i), "column order follows the existing owned model");
        expect(document.set_prop_val(owner, property, UndefinedValue{}) == PropertyMutationStatus::read_only &&
            document.set_prop_val(owner, property, reference) == PropertyMutationStatus::read_only &&
            document.reset_prop_val(owner, property) == PropertyMutationStatus::read_only,
            "owned collection cannot be replaced or reset");
    }
    ControlNode input; input.id = ObjectId{20}; input.name = "Input"; input.payload = InputFieldPayload{};
    document.add_control(std::move(input));
    expect(!document.table_columns({ControlRef{ObjectId{20}}}) && !document.table_columns({ControlRef{ObjectId{999}}}) &&
        !document.table_columns({}), "Table.Columns resolution rejects missing or wrong owners");
}

void test_table_column_optional_control() {
    const auto make_document = [](TableColumn column) {
        Form form; form.id = ObjectId{1}; form.name = "ColumnControl";
        form.children.emplace_back(ControlRef{ObjectId{3}});
        OrdinaryFormDocument document(std::move(form));
        document.add_attribute(Attribute{ObjectId{2}, "Rows",
            TypeDomainPatternValue{{TypeDomainEntry{.term = TypeDomainTerm::value_table}}}});
        ControlNode table{ObjectId{3}, "Rows", TablePayload{}};
        table.data_path = DataPath{AttributeRef{ObjectId{2}}, {}};
        std::get<TablePayload>(table.payload).columns.push_back(std::move(column));
        document.add_control(std::move(table));
        return document;
    };
    TableColumn column;
    column.name = "Code"; column.data_path = "Code";
    column.header.items.push_back({"en", "Code"});
    expect(column.control && column.control->kind == ControlKind::input_field && !column.value_type,
        "default TableColumn construction retains an owned InputField editor");
    expect(make_document(column).validate().ok(), "default TableColumn editor remains valid");
    auto default_copy = column;
    default_copy.control->properties.set_explicit(PropertyId::from_name("Enabled"), false);
    expect(column.control->properties.empty() && !default_copy.control->properties.empty(),
        "copied default column editor owns its properties independently");

    column.control.reset();
    expect(make_document(column).validate().ok(), "TableColumn may explicitly have no editor or ValueType");
    TypeDomainEntry text; text.term = TypeDomainTerm::string; text.string = {20, false};
    const TypeDomainEntry boolean{.term = TypeDomainTerm::boolean};
    for (const auto& type : {TypeDomainPatternValue{}, TypeDomainPatternValue{{text}},
            TypeDomainPatternValue{{boolean}}, TypeDomainPatternValue{{text, boolean}}}) {
        column.value_type = type;
        const auto document = make_document(column);
        expect(document.validate().ok(),
            "absent editor retains a valid column ValueType without InputField restrictions");
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{3}};
        const auto reference = document.get_prop_val(owner, PropertyId::from_name("Columns"));
        const auto* columns = reference.value
            ? document.table_columns(std::get<TableColumnsRef>(*reference.value)) : nullptr;
        expect(columns && columns->size() == 1 && !columns->front().control &&
                columns->front().value_type == type,
            "Table.Columns exposes the same owned column and its absent editor state");
    }
    auto absent_copy = column;
    absent_copy.value_type->entries.front().string.length = 30;
    expect(!absent_copy.control && !column.control && column.value_type->entries.front().string.length == 20,
        "copied absent editor and column ValueType retain independent values");
    auto copied_document = make_document(column);
    const auto copied_document_again = copied_document;
    const auto* copied_table = copied_document_again.find_control(ObjectId{3});
    expect(copied_table && !std::get<TablePayload>(copied_table->payload).columns.front().control &&
            std::get<TablePayload>(copied_table->payload).columns.front().value_type == column.value_type &&
            copied_document_again.validate().ok(),
        "document copy preserves the absent editor and column-owned ValueType");

    TypeDomainEntry invalid_number; invalid_number.term = TypeDomainTerm::numeric;
    invalid_number.numeric = {0, 2, false};
    column.value_type = TypeDomainPatternValue{{invalid_number}};
    expect(make_document(column).validate().has(InvariantCode::invalid_property),
        "absent editor column ValueType is validated by the typed codec");

    column.value_type = TypeDomainPatternValue{{text}};
    column.control.emplace();
    expect(make_document(column).validate().has(InvariantCode::invalid_property),
        "present editor rejects a separate column-owned ValueType even when editor ValueType is omitted");
    column.control->value_type = column.value_type;
    expect(make_document(column).validate().has(InvariantCode::invalid_property),
        "matching editor and column ValueType owners remain ambiguous and are rejected");
    column.value_type.reset();
    expect(make_document(column).validate().ok(), "present editor keeps its own authoritative ValueType");
    column.control->kind = ControlKind::check_box;
    expect(make_document(column).validate().has(InvariantCode::invalid_property),
        "optional editor preserves existing type-specific ValueType restrictions");
}

void test_choice_list_live_references() {
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(form);
    const auto property = PropertyId::from_name("ChoiceList");
    const auto key = [](std::uint64_t id) { return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{id}}; };
    const auto read = [&](std::uint64_t id) {
        const auto result = document.get_prop_val(key(id), property);
        expect(result.status == PropertyReadStatus::computed_value && result.value && std::holds_alternative<ValueListRef>(*result.value),
            "ChoiceList getter returns a live reference");
        return std::get<ValueListRef>(*result.value);
    };
    std::vector<ValueListRef> defaults;
    for (std::uint64_t id = 2; id < 14; ++id) {
        ControlNode control;
        control.id = ObjectId{id};
        control.name = "Field" + std::to_string(id);
        control.payload = id % 2 ? ControlPayload{ChoiceFieldPayload{}} : ControlPayload{InputFieldPayload{}};
        document.add_control(std::move(control));
        defaults.push_back(read(id));
        expect(defaults.back().get() && defaults.back().get()->items.empty() && defaults.back() == read(id),
            "all twelve fields have stable, initially empty lists");
        if (id > 2) expect(defaults.back() != defaults.front(), "unassigned fields own distinct lists");
    }
    ValueListValue initial;
    initial.items.push_back({0, std::string{"first"}, std::string{"First"}, false, {}});
    const auto created = ValueListRef::create(initial);
    expect(created.has_value(), "runtime list accepts shared typed values");
    const auto shared = *created;
    initial.items.front().value = false;
    expect(std::get<std::string>(shared.get()->items.front().value) == "first", "create owns a deep value copy");
    for (std::uint64_t id = 2; id < 14; ++id)
        expect(document.set_prop_val(key(id), property, shared) == PropertyMutationStatus::applied_runtime_only && read(id) == shared,
            "all twelve fields share the assigned list identity");
    auto edited = *shared.get();
    edited.items.push_back({1, std::int64_t{7}, std::string{"Seven"}, true, {}});
    expect(read(3).replace(edited), "mutation through a field updates its shared object");
    for (std::uint64_t id = 2; id < 14; ++id)
        expect(read(id).get()->items.size() == 2, "shared mutation is visible to every owner");
    const auto replacement = *ValueListRef::create(ValueListValue{});
    expect(document.set_prop_val(key(2), property, replacement) == PropertyMutationStatus::applied_runtime_only &&
        read(2) == replacement && read(3) == shared && defaults.front().get()->items.empty(),
        "replacement detaches only one owner, preserving old handles");
    auto cleared = *shared.get();
    cleared.last_assigned_id = 1;
    cleared.items.clear();
    expect(shared.replace(cleared) && read(3).get()->items.empty() && shared.get()->effective_last_assigned_id() == 1,
        "clearing shared content preserves native item ID history");
    auto invalid = edited;
    invalid.items.back().id = 0;
    expect(!shared.replace(invalid) && *shared.get() == cleared && !ValueListRef::create(invalid),
        "invalid replacement is rejected before changing the live object");
    invalid = {};
    invalid.items.push_back({0, shared, std::string{}, false, {}});
    expect(!shared.replace(invalid), "live references cannot create nested ownership cycles");
    invalid = {};
    invalid.available_values.push_back({});
    invalid.available_values.front().items.push_back({0, shared, std::string{}, false, {}});
    expect(!ValueListRef::create(invalid), "available values reject nested live references too");
    expect(!ValueListRef{}.replace({}), "null handle cannot acquire hidden ownership through replacement");
    for (const auto& invalid_value : {PropertyValue{UndefinedValue{}}, PropertyValue{NullValue{}}, PropertyValue{ArrayValue{}}, PropertyValue{ValueListValue{}}, PropertyValue{ValueListRef{}}})
        expect(document.set_prop_val(key(3), property, invalid_value) == PropertyMutationStatus::invalid_value && read(3) == shared,
            "invalid assignment preserves the current list");
    auto copied = document;
    const auto copied_read = copied.get_prop_val(key(3), property);
    expect(copied_read.value && std::get<ValueListRef>(*copied_read.value) == shared,
        "document copies preserve external runtime reference identity");
    OrdinaryFormDocument another(form);
    ControlNode other;
    other.id = ObjectId{20}; other.name = "Other"; other.payload = InputFieldPayload{};
    another.add_control(std::move(other));
    expect(another.set_prop_val(key(20), property, shared) == PropertyMutationStatus::applied_runtime_only,
        "a runtime list can be shared across forms");
    document.set_form(form);
    expect(read(3) != shared && shared.get()->effective_last_assigned_id() == 1,
        "form replacement releases its bindings without invalidating external handles");
    expect(shared.replace(edited) && std::get<ValueListRef>(*another.get_prop_val(key(20), property).value).get()->items.size() == 2,
        "detached handles remain live through another form owner");
    expect(document.get_prop_val(key(999), property).status == PropertyReadStatus::unknown_owner,
        "unknown owners cannot create runtime collections");
    TypedValueBudget budget;
    expect(!validate_typed_value(PropertyValue{shared}, budget), "runtime identity is excluded from serializable typed values");
}

void test_recursive_typed_values() {
    const auto valid = [](const PropertyValue& value) {
        TypedValueBudget budget;
        return validate_typed_value(value, budget);
    };
    static_assert(std::variant_size_v<PropertyValue::Variant> == 45);
    [&]<std::size_t... Index>(std::index_sequence<Index...>) {
        ([&] {
            using Value = std::variant_alternative_t<Index, PropertyValue::Variant>;
            PropertyValue value{Value{}};
            expect(std::get_if<Value>(&value) && std::holds_alternative<Value>(value), "derived variant retains typed access for every alternative");
            std::visit([&](const auto& item) {
                using Item = std::remove_cvref_t<decltype(item)>;
                expect((std::is_same_v<Value, Item>), "derived variant visits the active alternative");
            }, value);
            const auto copy = value;
            expect(copy == value && !(copy != value), "all default alternatives retain deep equality");
        }(), ...);
    }(std::make_index_sequence<std::variant_size_v<PropertyValue::Variant>>{});
    PropertyValue assigned = false;
    assigned = std::string{"value"};
    expect(std::get<std::string>(assigned) == "value", "inherited string assignment retains the selected type");
    assigned = std::int64_t{7};
    expect(std::get<std::int64_t>(assigned) == 7, "inherited numeric assignment retains the selected type");

    const StructureValue structure{{{"Имя", std::string{"nested"}}, {"Δοκιμή", false}, {"字段", std::int64_t{9}}}, true};
    const MapValue map{{{std::string{"data"}, structure}, {std::int64_t{7}, ArrayValue{{true, DecimalValue{"1.25"}}, false}}}, true};
    PropertyValue tree = ArrayValue{{map, ArrayValue{{false}, false}}, true};
    expect(valid(tree), "named fixed kinds are preserved independently without emulating platform constructors");
    auto copied = tree;
    std::get<StructureValue>(std::get<MapValue>(std::get<ArrayValue>(copied).items.front()).entries.front().value).fields.front().value = std::string{"changed"};
    expect(copied != tree &&
        std::get<std::string>(std::get<StructureValue>(std::get<MapValue>(std::get<ArrayValue>(tree).items.front()).entries.front().value).fields.front().value) == "nested",
        "nested arrays, maps and structures copy values deeply");
    auto fixed_kind = tree;
    std::get<ArrayValue>(fixed_kind).fixed = false;
    expect(fixed_kind != tree, "fixed and mutable collection kinds remain distinct values");
    const PropertyEntry entry{PropertyId::from_name("Value"), PropertyState::explicit_value, tree};
    expect(entry == PropertyEntry{entry}, "defaulted owner equality compares recursive values deeply");

    FontValue font; font.kind = FontKind::absolute; font.face_name = "Arial"; font.height = 12.5; font.bold = true;
    FontValue styled_font; styled_font.kind = FontKind::style_reference; styled_font.style = QualifiedName{"StyleFonts.TextFont"};
    ColorValue color; color.kind = ColorKind::absolute; color.red = 7; color.green = 8; color.blue = 9;
    ColorValue styled_color; styled_color.kind = ColorKind::style_reference; styled_color.style = QualifiedName{"StyleColors.ButtonTextColor"};
    BorderValue border; border.border_type = ControlBorderType::single; border.width = 2;
    BorderValue styled_border; styled_border.kind = BorderKind::style_reference; styled_border.style = QualifiedName{"StyleBorders.ControlBorder"};
    TypeDomainEntry number; number.term = TypeDomainTerm::numeric; number.numeric = {15, 2, false};
    const TypeDomainPatternValue type{{number}};
    for (const auto& value : {PropertyValue{UndefinedValue{}}, PropertyValue{NullValue{}}, PropertyValue{true}, PropertyValue{std::int64_t{-7}},
            PropertyValue{DecimalValue{"12.5"}}, PropertyValue{std::string{"Привет 世界"}}, PropertyValue{DateValue{"2026-11-04T12:30:00"}},
            PropertyValue{UuidValue{"514cd709-d736-4801-9061-13350de142ee"}}, PropertyValue{PlatformTypeValue{"Number"}},
            PropertyValue{color}, PropertyValue{styled_color}, PropertyValue{font}, PropertyValue{styled_font}, PropertyValue{border},
            PropertyValue{styled_border}, PropertyValue{type}})
        expect(valid(value), "the common typed value validator accepts established primitive, style and type-description values");
    ColorValue invalid_color = color; invalid_color.alpha = 1;
    FontValue invalid_font = font; invalid_font.height = std::numeric_limits<double>::quiet_NaN();
    BorderValue invalid_border = border; invalid_border.width = 6;
    ColorValue unknown_color = styled_color; unknown_color.style = QualifiedName{"StyleColors.Unknown"};
    TypeDomainPatternValue invalid_type = type; invalid_type.entries.front().numeric = {0, 2, false};
    for (const auto& value : {PropertyValue{DecimalValue{"01"}}, PropertyValue{DateValue{"2026-02-30T00:00:00"}},
            PropertyValue{UuidValue{"514CD709-D736-4801-9061-13350DE142EE"}}, PropertyValue{PlatformTypeValue{"Unknown"}},
            PropertyValue{std::string{"\xc0\x80", 2}}, PropertyValue{invalid_color}, PropertyValue{unknown_color}, PropertyValue{invalid_font},
            PropertyValue{invalid_border}, PropertyValue{invalid_type}, PropertyValue{ControlRef{ObjectId{2}}}, PropertyValue{PageRef{ObjectId{3}}},
            PropertyValue{ChartSeriesRef{ObjectId{2}, ObjectId{4}}}, PropertyValue{PictureRef{}}, PropertyValue{LocalizedStringValue{}}})
        expect(!valid(value), "invalid primitives, unknown styles and runtime object references are rejected explicitly");
    expect(!valid(StructureValue{{{"", false}}, false}) && !valid(StructureValue{{{"name", false}, {"name", true}}, false}) &&
        !valid(StructureValue{{{std::string{"\xed\xa0\x80", 3}, false}}, false}),
        "structure names must be nonempty valid UTF-8 and exact string keys must be unique");
    expect(valid(MapValue{{{std::string{"7"}, false}, {std::int64_t{7}, true}}, false}) &&
        !valid(MapValue{{{std::int64_t{7}, false}, {DecimalValue{"7"}, true}}, false}) &&
        !valid(MapValue{{{ArrayValue{}, false}}, false}),
        "Map distinguishes string and number keys, merges numeric identity and rejects unverified object keys");
    const MapValue ordered{{{std::string{"second"}, false}, {std::int64_t{7}, std::string{"value"}}}, false};
    const MapValue reversed{{{DecimalValue{"7"}, std::string{"value"}}, {std::string{"second"}, false}}, false};
    expect(ordered == reversed && reversed == ordered,
        "Map equality ignores entry order and shares numeric key identity across int64 and Decimal");
    auto different_kind = reversed; different_kind.fixed = true;
    auto different_value = reversed; different_value.entries.front().value = std::string{"changed"};
    expect(ordered != different_kind && ordered != different_value,
        "Map equality preserves fixed kinds and compares associated values deeply");
    const MapValue nested_left{{{false, ordered}, {true, ArrayValue{{ordered}, false}}}, true};
    const MapValue nested_right{{{true, ArrayValue{{reversed}, false}}, {false, reversed}}, true};
    expect(PropertyValue{nested_left} == PropertyValue{nested_right}, "nested maps compare independently of every entry order");
    const MapValue unsupported_keys{{{ArrayValue{}, false}}, false};
    const MapValue duplicate_keys{{{std::int64_t{7}, false}, {DecimalValue{"7"}, true}}, false};
    expect(!(unsupported_keys == unsupported_keys) && !(duplicate_keys == duplicate_keys),
        "Map equality rejects unsupported object keys and duplicate canonical primitive keys");
    expect(typed_map_key(std::int64_t{7}) == typed_map_key(DecimalValue{"7"}) && !typed_map_key(ArrayValue{}),
        "the exported map key helper provides the same canonical identities used by validation and equality");

    PropertyValue deep = false;
    for (std::size_t depth = 0; depth < max_typed_value_depth; ++depth) {
        ArrayValue outer; outer.items.push_back(std::move(deep)); deep = std::move(outer);
    }
    expect(valid(deep), "a typed value at the documented depth limit is accepted");
    ArrayValue beyond; beyond.items.push_back(std::move(deep));
    expect(!valid(beyond), "one level beyond the typed value depth limit is rejected before recursive copying");
    TypedValueBudget accumulated{max_typed_value_nodes - 3};
    expect(validate_typed_value(MapValue{{{false, true}}, false}, accumulated) && accumulated.nodes == max_typed_value_nodes &&
        !validate_typed_value(false, accumulated), "shared node accounting includes Map keys and cannot exceed its limit");
    TypedValueBudget description_budget;
    expect(validate_typed_value(type, description_budget) && description_budget.nodes == 2,
        "type-description entries consume the shared resource budget");
}

void test_value_list_named_values() {
    const auto valid = [](const PropertyValue& value) {
        TypedValueBudget budget;
        return validate_typed_value(value, budget);
    };
    const auto picture = PictureRef{{}, QualifiedName{"PictureLib.Write"}};
    ValueListValue base;
    base.items = {{0, std::string{"first"}, std::string{"Первый"}, true, {}},
        {1, DecimalValue{"78.125"}, std::string{"Второй"}, false, {}},
        {2, ArrayValue{{false, DecimalValue{"17.25"}}, false}, std::string{"Вложенный"}, true, {}}};
    std::array<ValueListValue, 12> variants;
    variants[1] = base; variants[1].items.erase(variants[1].items.begin() + 1); variants[1].last_assigned_id = 2;
    variants[2].last_assigned_id = 2;
    variants[3] = base; variants[3].items.front().id = 3; variants[3].last_assigned_id = 3;
    variants[4] = base; std::swap(variants[4].items.front(), variants[4].items.back());
    TypeDomainEntry text; text.term = TypeDomainTerm::string; text.string.length = 40;
    variants[5] = base; variants[5].value_type.entries.push_back(text);
    variants[6] = base; variants[6].available_values.push_back(base);
    variants[7] = base; variants[7].items.front().presentation = FormattedStringValue{std::string{"<b>Первый</b>"}, true};
    variants[8] = base; variants[8].items.front().picture = picture;
    variants[9] = base;
    variants[9].items.front().value = StructureValue{{{"Items", MapValue{{{false, ArrayValue{{DecimalValue{"1.5"}}, true}}}, true}}}, true};
    TypeDomainEntry number; number.term = TypeDomainTerm::numeric; number.numeric = {15, 2, false};
    variants[10] = base; variants[10].value_type.entries.push_back(number);
    variants[11].available_values.emplace_back();
    for (const auto& value : variants) {
        expect(valid(value), "twelve named ValueList states preserve histories, ordering, restrictions and available values without coercion");
        auto copy = value;
        expect(copy == value && copy.effective_last_assigned_id() == value.effective_last_assigned_id(),
            "ValueList copies retain local item identity and effective allocator history");
        copy.items.push_back(ValueListItem{99, false, std::string{"copy"}, false, {}});
        expect(copy.items.size() == value.items.size() + 1, "ValueList item collections copy without shared storage");
    }
    expect(variants[0].effective_last_assigned_id() == -1 && variants[2].effective_last_assigned_id() == 2 && variants[0] != variants[2],
        "a newly empty list differs from an empty list retaining allocation history");
    auto explicit_default = base; explicit_default.last_assigned_id = 2;
    expect(explicit_default == base, "explicit default and omitted ValueList history have the same effective meaning");
    expect(variants[4].effective_last_assigned_id() == 2 && variants[4] != base,
        "reordering preserves item IDs and allocator history but changes the ordered list value");
    expect(variants[0] != variants[11], "absent AvailableValues differs from an explicitly empty available list");
    auto nested_copy = variants[6];
    std::get<ArrayValue>(nested_copy.available_values.front().items.back().value).items.front() = true;
    expect(nested_copy != variants[6] && !std::get<bool>(std::get<ArrayValue>(variants[6].available_values.front().items.back().value).items.front()),
        "AvailableValues and item values copy recursively without aliasing");

    const FormattedStringValue localized{LocalizedStringValue{{LocalizedStringItem{"ru", "Локализовано"}}}, true};
    expect(std::holds_alternative<LocalizedStringValue>(localized.value) &&
        valid(FormattedStringValue{std::string{"<b>Текст</b>"}, true}) && !valid(localized),
        "legacy localized formatter aggregates remain constructible while runtime typed values require plain markup text");
    auto invalid = base; invalid.items.back().id = invalid.items.front().id;
    expect(!valid(invalid), "ValueList item IDs must be unique within the owning list");
    invalid = base; invalid.last_assigned_id = 1;
    expect(!valid(invalid) && !(invalid == invalid), "invalid allocation history is rejected by validation and equality");
    bool history_threw = false;
    try { (void)invalid.effective_last_assigned_id(); } catch (const std::invalid_argument&) { history_threw = true; }
    expect(history_threw, "the shared history helper reports invalid history explicitly");
    constexpr auto maximum_id = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    ValueListValue boundary; ValueListItem boundary_item; boundary_item.id = maximum_id; boundary.items.push_back(std::move(boundary_item));
    expect(valid(boundary) && boundary.effective_last_assigned_id() == std::numeric_limits<std::int64_t>::max(),
        "ValueList permits the largest signed 64-bit ID");
    boundary.items.front().id = maximum_id + 1;
    expect(!valid(boundary), "ValueList rejects item IDs outside the signed 64-bit range");
    boundary.items.clear(); boundary.last_assigned_id = maximum_id + 1;
    expect(!valid(boundary), "empty ValueList history also obeys the signed 64-bit range");
    invalid = base; invalid.available_values = {base, base};
    expect(!valid(invalid), "AvailableValues has zero-or-one cardinality");
    for (const auto& presentation : {PropertyValue{UndefinedValue{}}, PropertyValue{localized},
            PropertyValue{FormattedStringValue{std::string{"plain"}, false}}, PropertyValue{std::string{"\xc0\x80", 2}}}) {
        invalid = base; invalid.items.front().presentation = presentation;
        expect(!valid(invalid), "ValueList rejects unproven presentation kinds, false formatter flags and invalid UTF-8");
    }
    for (const auto& unsupported_picture : {PictureRef{PictureAssetRef{ObjectId{7}}, {}}, PictureRef{},
            PictureRef{{}, QualifiedName{"PictureLib.Unknown"}}}) {
        invalid = base; invalid.items.front().picture = unsupported_picture;
        expect(!valid(invalid), "ValueList pictures support existing named library pictures and reject file/context-dependent references explicitly");
    }

    ValueListValue counted;
    counted.value_type.entries.push_back(number);
    counted.items.push_back(ValueListItem{0, false, std::string{}, false, picture});
    counted.available_values.emplace_back();
    TypedValueBudget budget;
    expect(validate_typed_value(counted, budget) && budget.nodes == 7,
        "ValueList node accounting includes type entries, item records, default presentations, pictures and available-list roots");
    TypedValueBudget exact{max_typed_value_nodes - 7};
    expect(validate_typed_value(counted, exact) && exact.nodes == max_typed_value_nodes,
        "ValueList accepts exactly the remaining shared node budget");
    TypedValueBudget exhausted{max_typed_value_nodes - 3};
    expect(!validate_typed_value(counted, exhausted), "ValueList rejects insufficient item budget before copying child values");

    ValueListValue deep;
    for (std::size_t depth = 0; depth < max_typed_value_depth; ++depth) {
        ValueListValue outer; outer.available_values.push_back(std::move(deep)); deep = std::move(outer);
    }
    expect(valid(deep), "AvailableValues may reach the common semantic depth limit");
    ValueListValue beyond; beyond.available_values.push_back(std::move(deep));
    expect(!valid(beyond), "AvailableValues cannot bypass the common depth limit");
}

void test_typed_value_document_budget() {
    Form form; form.id = ObjectId{1}; form.name = "TypedBudget";
    form.children = {ControlRef{ObjectId{10}}, ControlRef{ObjectId{11}}};
    OrdinaryFormDocument document(std::move(form));
    TypeDomainEntry number; number.term = TypeDomainTerm::numeric; number.numeric = {15, 2, false};
    document.add_attribute(Attribute{ObjectId{3}, "Number", TypeDomainPatternValue{{number}}});
    for (const auto id : {ObjectId{10}, ObjectId{11}}) {
        ControlNode input{id, "Input" + std::to_string(id.value()), InputFieldPayload{}};
        input.data_path = DataPath{AttributeRef{ObjectId{3}}, {}};
        ArrayValue values; values.items.resize(max_typed_value_nodes / 2 - 1, PropertyValue{false});
        input.properties().set_explicit(PropertyId::from_name("MinValue"), std::move(values));
        document.add_control(std::move(input));
    }
    expect(document.validate().ok(), "typed trees across two controls may exactly consume the global node budget");
    const OrdinaryFormDocument::ObjectKey input{ObjectCategory::control, ObjectId{10}};
    expect(document.set_prop_val(input, PropertyId::from_name("MaxValue"), true) == PropertyMutationStatus::applied &&
        !document.validate().ok(), "one additional typed field exceeds the aggregate budget although each field fits alone");
    expect(document.reset_prop_val(input, PropertyId::from_name("MaxValue")) == PropertyMutationStatus::reset &&
        document.validate().ok(), "removing the additional field restores the bounded document");

    Form chart_form; chart_form.id = ObjectId{1}; chart_form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument invalid(std::move(chart_form));
    ChartPayload chart;
    ChartSeries series; series.id = ObjectId{2}; chart.series.push_back(std::move(series));
    ChartPoint point; point.id = ObjectId{1}; chart.points.push_back(std::move(point));
    chart.values.push_back(ChartValue{ObjectId{2}, ObjectId{1}, DecimalValue{"1"}, {}, ControlRef{ObjectId{2}}});
    invalid.add_control(ControlNode{ObjectId{2}, "Chart", std::move(chart)});
    expect(!invalid.validate().ok(), "Chart cell Details cannot bypass common typed value validation");
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

void test_document_property_api() {
    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));
    document.add_control(ControlNode{ObjectId{10}, "Input", InputFieldPayload{}});
    document.add_control(ControlNode{ObjectId{11}, "Button", ButtonPayload{}});
    document.add_control(ControlNode{ObjectId{12}, "Check", CheckBoxPayload{}});
    document.add_control(ControlNode{ObjectId{13}, "Menu", CommandBarPayload{}});
    document.add_control(ControlNode{ObjectId{14}, "Table", TablePayload{}});
    document.add_control(ControlNode{ObjectId{15}, "Label", LabelDecorationPayload{}});
    document.add_control(ControlNode{ObjectId{16}, "Choice", ChoiceFieldPayload{}});
    document.add_control(ControlNode{ObjectId{17}, "Dendrogram", DendrogramPayload{}});
    document.add_control(ControlNode{ObjectId{19}, "Radio", RadioButtonPayload{}});
    document.add_control(ControlNode{ObjectId{21}, "List", ListBoxPayload{}});
    document.add_control(ControlNode{ObjectId{22}, "Chart", ChartPayload{}});
    document.add_control(ControlNode{ObjectId{23}, "GanttChart", GanttChartPayload{}});
    document.add_control(ControlNode{ObjectId{24}, "PivotChart", PivotChartPayload{}});
    ControlNode active_x_node{ObjectId{18}, "ActiveX", ActiveXControlPayload{}};
    active_x_node.properties().set_explicit(PropertyId::from_name("CLSID"), std::string("existing"));
    document.add_control(std::move(active_x_node));
    Page page;
    page.id = ObjectId{20};
    page.name = "Page";
    document.add_page(std::move(page));

    const auto input = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{10}};
    const auto button = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{11}};
    const auto check = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{12}};
    const auto menu = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{13}};
    const auto label = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{15}};
    const auto choice = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{16}};
    const auto dendrogram = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{17}};
    const auto radio = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{19}};
    const auto list_box = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{21}};
    const auto active_x = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{18}};

    for (const auto id : {17, 22, 23, 24}) {
        const auto result = document.get_prop_val(
            OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{static_cast<unsigned>(id)}},
            PropertyId::from_name("BackColor"));
        const auto* color = result.value ? std::get_if<ColorValue>(&*result.value) : nullptr;
        const auto* style = color ? std::get_if<QualifiedName>(&color->style) : nullptr;
        expect(result.status == PropertyReadStatus::proven_default && color &&
                   color->kind == ColorKind::style_reference &&
                   style && *style == QualifiedName{"StyleColors.FormBackColor"},
            "chart BackColor must expose the form background style reference as its default");
    }

    const auto input_enabled = document.get_prop_val(input, PropertyId::from_name("Enabled"));
    expect(input_enabled.status == PropertyReadStatus::proven_default &&
               input_enabled.value && std::get<bool>(*input_enabled.value),
        "InputField Enabled must expose its proven true default");
    const auto button_enabled = document.get_prop_val(button, PropertyId::from_name("Enabled"));
    expect(button_enabled.status == PropertyReadStatus::proven_default &&
               button_enabled.value && *button_enabled.value == *input_enabled.value,
        "the same Enabled property must use one common API across control kinds");
    const auto input_tooltip = document.get_prop_val(input, PropertyId::from_name("ToolTip"));
    const auto button_tooltip = document.get_prop_val(button, PropertyId::from_name("ToolTip"));
    expect(input_tooltip.status == PropertyReadStatus::proven_default && input_tooltip.value &&
               *input_tooltip.value == PropertyValue{std::string{}} &&
               button_tooltip.status == PropertyReadStatus::proven_default && button_tooltip.value &&
               *button_tooltip.value == *input_tooltip.value,
        "the common ToolTip default must be shared across input and button controls");

    expect(document.set_prop_val(input, PropertyId::from_name("Enabled"), false) ==
               PropertyMutationStatus::applied,
        "a proven writable common property must be settable");
    const auto explicit_enabled = document.get_prop_val(input, PropertyId::from_name("Enabled"));
    expect(explicit_enabled.status == PropertyReadStatus::explicit_value &&
               explicit_enabled.value && !std::get<bool>(*explicit_enabled.value),
        "setting a value equal to or different from default must retain explicit state");
    expect(document.reset_prop_val(input, PropertyId::from_name("Enabled")) ==
               PropertyMutationStatus::reset,
        "reset must remove the explicit value");
    expect(document.get_prop_val(input, PropertyId::from_name("Enabled")).status ==
               PropertyReadStatus::proven_default,
        "reset must reveal the proven descriptor default");

    const auto input_read_only = document.get_prop_val(input, PropertyId::from_name("ReadOnly"));
    expect(input_read_only.status == PropertyReadStatus::proven_default && input_read_only.value &&
               *input_read_only.value == PropertyValue{false},
        "InputField ReadOnly must expose its proven false default");
    expect(document.set_prop_val(input, PropertyId::from_name("ReadOnly"), true) ==
               PropertyMutationStatus::applied,
        "InputField ReadOnly must use its persisted Boolean property descriptor");
    const auto explicit_read_only = document.get_prop_val(input, PropertyId::from_name("ReadOnly"));
    expect(explicit_read_only.status == PropertyReadStatus::explicit_value && explicit_read_only.value &&
               *explicit_read_only.value == PropertyValue{true},
        "InputField ReadOnly must be readable after setting it");
    expect(document.reset_prop_val(input, PropertyId::from_name("ReadOnly")) ==
               PropertyMutationStatus::reset &&
               document.get_prop_val(input, PropertyId::from_name("ReadOnly")).status ==
                   PropertyReadStatus::proven_default,
        "resetting InputField ReadOnly must reveal the false default");

    const auto label_caption = document.get_prop_val(label, PropertyId::from_name("Caption"));
    expect(label_caption.status == PropertyReadStatus::proven_default && label_caption.value &&
               *label_caption.value == PropertyValue{std::string{}},
        "LabelDecoration Caption must expose its proven empty-string default");
    expect(document.set_prop_val(label, PropertyId::from_name("Caption"), std::string("Label caption")) ==
               PropertyMutationStatus::applied,
        "LabelDecoration Caption must use its persisted string property descriptor");
    const auto explicit_label_caption = document.get_prop_val(label, PropertyId::from_name("Caption"));
    expect(explicit_label_caption.status == PropertyReadStatus::explicit_value &&
               explicit_label_caption.value &&
               *explicit_label_caption.value == PropertyValue{std::string("Label caption")},
        "LabelDecoration Caption must be readable after setting it");
    expect(document.reset_prop_val(label, PropertyId::from_name("Caption")) ==
               PropertyMutationStatus::reset &&
               document.get_prop_val(label, PropertyId::from_name("Caption")).status ==
                   PropertyReadStatus::proven_default,
        "resetting LabelDecoration Caption must reveal the empty-string default");

    const auto button_font = document.get_prop_val(button, PropertyId::from_name("Font"));
    const auto check_font = document.get_prop_val(check, PropertyId::from_name("Font"));
    const auto label_font = document.get_prop_val(label, PropertyId::from_name("Font"));
    const auto radio_font = document.get_prop_val(radio, PropertyId::from_name("Font"));
    const auto input_font = document.get_prop_val(input, PropertyId::from_name("Font"));
    expect(button_font.status == PropertyReadStatus::proven_default && button_font.value &&
               *button_font.value == PropertyValue{FontValue{}} &&
               check_font.status == PropertyReadStatus::proven_default && check_font.value &&
               *check_font.value == *button_font.value,
        "automatic Font default must be materialized consistently across controls");
    expect(label_font.status == PropertyReadStatus::proven_default && label_font.value &&
               *label_font.value == PropertyValue{FontValue{}},
        "LabelDecoration Font must expose the proven automatic default");
    expect(radio_font.status == PropertyReadStatus::proven_default && radio_font.value &&
               *radio_font.value == PropertyValue{FontValue{}} &&
               input_font.status == PropertyReadStatus::proven_default && input_font.value &&
               *input_font.value == PropertyValue{FontValue{}},
        "RadioButton and InputField Font must expose the automatic default");

    FontValue label_absolute_font;
    label_absolute_font.kind = FontKind::absolute;
    label_absolute_font.face_name = "Arial";
    label_absolute_font.height = 10.5;
    label_absolute_font.bold = false;
    label_absolute_font.italic = true;
    label_absolute_font.underline = false;
    label_absolute_font.strikeout = false;
    expect(document.set_prop_val(label, PropertyId::from_name("Font"), label_absolute_font) ==
               PropertyMutationStatus::applied,
        "LabelDecoration Font must accept a typed absolute FontValue");
    const auto explicit_label_font = document.get_prop_val(label, PropertyId::from_name("Font"));
    expect(explicit_label_font.status == PropertyReadStatus::explicit_value && explicit_label_font.value &&
               *explicit_label_font.value == PropertyValue{label_absolute_font},
        "LabelDecoration Font must remain explicitly readable");
    expect(document.set_prop_val(label, PropertyId::from_name("Font"), std::string("Arial")) ==
               PropertyMutationStatus::invalid_value &&
               document.get_prop_val(label, PropertyId::from_name("Font")).value == explicit_label_font.value,
        "wrong-typed LabelDecoration Font must be rejected without mutation");
    expect(document.reset_prop_val(label, PropertyId::from_name("Font")) ==
               PropertyMutationStatus::reset &&
               document.get_prop_val(label, PropertyId::from_name("Font")).status ==
                   PropertyReadStatus::proven_default,
        "resetting LabelDecoration Font must reveal the automatic default");
    const auto command_bar_orientation = document.get_prop_val(menu, PropertyId::from_name("Orientation"));
    const auto dendrogram_orientation = document.get_prop_val(
        dendrogram, PropertyId::from_name("Orientation"));
    expect(command_bar_orientation.status == PropertyReadStatus::proven_default &&
               command_bar_orientation.value &&
               std::get<EnumerationValue>(*command_bar_orientation.value) ==
                   EnumerationValue{"Orientation", "Auto"} &&
               dendrogram_orientation.status == PropertyReadStatus::proven_default &&
               dendrogram_orientation.value &&
               std::get<EnumerationValue>(*dendrogram_orientation.value) ==
                   EnumerationValue{"DendrogramOrientation", "Up"},
        "qualified enum defaults must retain their proven types and members");

    const auto button_border = document.get_prop_val(button, PropertyId::from_name("BorderColor"));
    const auto choice_border = document.get_prop_val(choice, PropertyId::from_name("BorderColor"));
    const auto radio_border = document.get_prop_val(radio, PropertyId::from_name("BorderColor"));
    const auto input_border = document.get_prop_val(input, PropertyId::from_name("BorderColor"));
    const auto list_box_border = document.get_prop_val(list_box, PropertyId::from_name("BorderColor"));
    expect(button_border.status == PropertyReadStatus::proven_default && button_border.value &&
               *button_border.value == PropertyValue{ColorValue{}} &&
               choice_border.status == PropertyReadStatus::proven_default && choice_border.value &&
               *choice_border.value == *button_border.value &&
               radio_border.status == PropertyReadStatus::proven_default && radio_border.value &&
               *radio_border.value == *button_border.value &&
               input_border.status == PropertyReadStatus::proven_default && input_border.value &&
               *input_border.value == *button_border.value &&
               list_box_border.status == PropertyReadStatus::proven_default && list_box_border.value &&
               *list_box_border.value == *button_border.value,
        "automatic BorderColor default must be shared across button and choice controls");

    ColorValue red;
    red.kind = ColorKind::absolute;
    red.red = 255;
    expect(document.set_prop_val(button, PropertyId::from_name("BorderColor"), red) ==
               PropertyMutationStatus::applied,
        "the shared BorderColor property must accept its typed value");
    const auto button_color = document.get_prop_val(button, PropertyId::from_name("BorderColor"));
    expect(button_color.status == PropertyReadStatus::explicit_value && button_color.value &&
               *button_color.value == PropertyValue{red},
        "explicit ColorValue must round-trip through the common property API");
    expect(document.set_prop_val(radio, PropertyId::from_name("Font"), FontValue{}) ==
               PropertyMutationStatus::applied &&
               document.get_prop_val(radio, PropertyId::from_name("Font")).status ==
                   PropertyReadStatus::explicit_value &&
               document.set_prop_val(input, PropertyId::from_name("Font"), std::string("Arial")) ==
                   PropertyMutationStatus::invalid_value,
        "RadioButton Font must be writable and wrong-typed InputField Font must fail closed");
    expect(document.set_prop_val(input, PropertyId::from_name("BorderColor"), red) ==
               PropertyMutationStatus::applied &&
               document.get_prop_val(input, PropertyId::from_name("BorderColor")).status ==
                   PropertyReadStatus::explicit_value &&
               document.set_prop_val(list_box, PropertyId::from_name("BorderColor"), true) ==
                   PropertyMutationStatus::invalid_value &&
               document.set_prop_val(radio, PropertyId::from_name("BorderColor"), std::string("red")) ==
                   PropertyMutationStatus::invalid_value,
        "new control BorderColor descriptors must accept ColorValue and reject other types");

    const auto input_border_default = document.get_prop_val(input, PropertyId::from_name("Border"));
    BorderValue expected_input_border;
    expected_input_border.kind = BorderKind::style_reference;
    expected_input_border.style = QualifiedName{"StyleBorders.ControlBorder"};
    expect(input_border_default.status == PropertyReadStatus::proven_default &&
            input_border_default.value == PropertyValue{expected_input_border},
        "InputField Border must expose its proven StyleBorders.ControlBorder default");
    const auto no_default = document.get_prop_val(
        OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{15}},
        PropertyId::from_name("HorizontalAlign"));
    expect(no_default.status == PropertyReadStatus::no_default && !no_default.value,
        "a descriptor declaring no default must remain distinct from unknown and Undefined");

    expect(document.set_prop_val(input, PropertyId::from_name("Enabled"), std::string("false")) ==
               PropertyMutationStatus::invalid_value,
        "wrong-typed values must be rejected before mutation");
    expect(document.get_prop_val(input, PropertyId::from_name("Enabled")).status ==
               PropertyReadStatus::proven_default,
        "a rejected set must leave the previous state unchanged");
    expect(document.set_prop_val(input, PropertyId::from_name("HorizontalAlign"),
                   EnumerationValue{"HorizontalAlign", "Auto"}) == PropertyMutationStatus::applied,
        "an evidenced enum type and member must be accepted by the common property API");
    const auto known_alignment = document.get_prop_val(input, PropertyId::from_name("HorizontalAlign"));
    expect(known_alignment.status == PropertyReadStatus::explicit_value && known_alignment.value &&
               *known_alignment.value == PropertyValue{EnumerationValue{"HorizontalAlign", "Auto"}},
        "a valid enum value must remain explicitly readable");
    expect(document.set_prop_val(input, PropertyId::from_name("HorizontalAlign"),
                   EnumerationValue{"BogusType", "BogusMember"}) == PropertyMutationStatus::invalid_value &&
               document.get_prop_val(input, PropertyId::from_name("HorizontalAlign")).value ==
                   known_alignment.value,
        "an enum with an unrecognized type and member must be rejected atomically");
    expect(document.set_prop_val(input, PropertyId::from_name("HorizontalAlign"),
                   EnumerationValue{"HorizontalAlign", "JustifyMaybe"}) == PropertyMutationStatus::invalid_value &&
               document.get_prop_val(input, PropertyId::from_name("HorizontalAlign")).value ==
                   known_alignment.value,
        "an enum member outside the proven catalog must leave the old value unchanged");
    expect(document.reset_prop_val(input, PropertyId::from_name("HorizontalAlign")) ==
               PropertyMutationStatus::reset,
        "a validated enum property must remain resettable");
    expect(document.set_prop_val(active_x, PropertyId::from_name("CLSID"), std::string("class")) ==
               PropertyMutationStatus::unknown_access &&
               document.reset_prop_val(active_x, PropertyId::from_name("CLSID")) ==
                   PropertyMutationStatus::unknown_access &&
               document.find_control(ObjectId{18})->properties().size() == 1 &&
               document.get_prop_val(active_x, PropertyId::from_name("CLSID")).status ==
                   PropertyReadStatus::explicit_value &&
               document.get_prop_val(active_x, PropertyId::from_name("CLSID")).value ==
                   PropertyValue{std::string("existing")},
        "unknown API access must reject both set and reset without mutating the control");
    expect(document.set_prop_val(input, PropertyId::from_name("ChoiceIncomplete"), true) ==
               PropertyMutationStatus::applied_runtime_only,
        "a writable runtime-only property must be kept in memory without claiming storage support");
    expect(document.get_prop_val(input, PropertyId::from_name("ChoiceIncomplete")).status ==
               PropertyReadStatus::explicit_value &&
               document.reset_prop_val(input, PropertyId::from_name("ChoiceIncomplete")) ==
                   PropertyMutationStatus::reset_runtime_only,
        "runtime-only explicit values must be readable and resettable");
    expect(document.set_prop_val(input, PropertyId::from_name("AutoContextMenu"), true) ==
               PropertyMutationStatus::applied &&
               document.get_prop_val(input, PropertyId::from_name("AutoContextMenu")).status ==
                   PropertyReadStatus::explicit_value,
        "AutoContextMenu must use the shared owner API and report persisted storage");
    for (const auto name : {"ModifiesData", "SkipOnInput", "FirstInGroup"}) {
        const auto property = PropertyId::from_name(name);
        const auto initial = document.get_prop_val(input, property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value == PropertyValue{false},
            "common Boolean extension must expose its proven false default");
        expect(document.set_prop_val(input, property, true) == PropertyMutationStatus::applied &&
                   document.get_prop_val(input, property).value == PropertyValue{true} &&
                   document.set_prop_val(input, property, std::string("true")) == PropertyMutationStatus::invalid_value &&
                   document.get_prop_val(input, property).value == PropertyValue{true} &&
                   document.reset_prop_val(input, property) == PropertyMutationStatus::reset &&
                   document.get_prop_val(input, property).value == PropertyValue{false},
            "common Boolean extension must support set, reject invalid values, and reset");
    }
    expect(document.set_prop_val(
               OrdinaryFormDocument::ObjectKey{ObjectCategory::form, ObjectId{1}},
               PropertyId::from_name("ModalMode"), false) == PropertyMutationStatus::read_only,
        "read-only properties must reject mutation");
    const auto form_caption = OrdinaryFormDocument::ObjectKey{ObjectCategory::form, ObjectId{1}};
    expect(document.set_prop_val(form_caption, PropertyId::from_name("Caption"), std::string("Title")) ==
               PropertyMutationStatus::applied,
        "a proven form property must use the same descriptor-backed API");
    expect(document.get_prop_val(form_caption, PropertyId::from_name("Caption")).status ==
               PropertyReadStatus::explicit_value,
        "the form property API must read an explicit property from Form");
    expect(document.reset_prop_val(form_caption, PropertyId::from_name("Caption")) ==
               PropertyMutationStatus::reset &&
               document.get_prop_val(form_caption, PropertyId::from_name("Caption")).status ==
                   PropertyReadStatus::proven_default &&
               document.get_prop_val(form_caption, PropertyId::from_name("Caption")).value ==
                   PropertyValue{std::string{}},
        "reset must restore the proven empty Form.Caption default");
    expect(document.get_prop_val(form_caption, PropertyId::from_name("AutoTitle")).status ==
               PropertyReadStatus::proven_default &&
               document.get_prop_val(form_caption, PropertyId::from_name("AutoTitle")).value ==
                   PropertyValue{true} &&
               document.set_prop_val(form_caption, PropertyId::from_name("AutoTitle"), false) ==
                   PropertyMutationStatus::applied &&
               document.get_prop_val(form_caption, PropertyId::from_name("AutoTitle")).value ==
                   PropertyValue{false} &&
               document.reset_prop_val(form_caption, PropertyId::from_name("AutoTitle")) ==
                   PropertyMutationStatus::reset,
        "Form AutoTitle must expose true default, accept false, and reset through the descriptor API");

    expect(document.get_prop_val(
               OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{999}},
               PropertyId::from_name("Enabled")).status == PropertyReadStatus::unknown_owner,
        "an unknown object key must return unknown_owner");
    expect(document.get_prop_val(input, PropertyId::from_name("MadeUp")).status ==
               PropertyReadStatus::unknown_property,
        "an unregistered property ID must return unknown_property");
    expect(document.get_prop_val(
               OrdinaryFormDocument::ObjectKey{ObjectCategory::page, ObjectId{20}},
               PropertyId::from_name("Visible")).status == PropertyReadStatus::unsupported_surface,
        "Page properties must report unsupported_surface in this API slice");
    expect(document.get_prop_val(form_caption, PropertyId::from_name("Controls")).status ==
               PropertyReadStatus::computed_value &&
               document.set_prop_val(form_caption, PropertyId::from_name("Controls"), false) ==
                   PropertyMutationStatus::read_only &&
               document.reset_prop_val(form_caption, PropertyId::from_name("Controls")) ==
                   PropertyMutationStatus::read_only,
        "Form.Controls exposes its owned handle and rejects replacement or reset");
    expect(document.get_prop_val(menu, PropertyId::from_name("Buttons")).status ==
               PropertyReadStatus::unsupported_surface,
        "typed collection payloads must report unsupported_surface");

    expect(document.set_prop_val(menu, PropertyId::from_name("ActionSource"), ControlRef{ObjectId{999}}) ==
               PropertyMutationStatus::invalid_reference,
        "a dangling control reference must be rejected before mutation");
    const auto rejected_action_source = document.get_prop_val(menu, PropertyId::from_name("ActionSource"));
    expect(rejected_action_source.status == PropertyReadStatus::proven_default &&
               rejected_action_source.value &&
               std::holds_alternative<UndefinedValue>(*rejected_action_source.value),
        "a rejected reference must not be stored");
    expect(document.set_prop_val(menu, PropertyId::from_name("ActionSource"), ControlRef{ObjectId{11}}) ==
               PropertyMutationStatus::invalid_reference,
        "ActionSource must reject an existing control of the wrong control kind");
    expect(document.set_prop_val(menu, PropertyId::from_name("ActionSource"), ControlRef{ObjectId{14}}) ==
               PropertyMutationStatus::applied,
        "ActionSource may point to an existing Table control");
    const auto action_source = document.get_prop_val(menu, PropertyId::from_name("ActionSource"));
    expect(action_source.status == PropertyReadStatus::explicit_value && action_source.value &&
               std::get<ControlRef>(*action_source.value).id() == ObjectId{14},
        "a valid typed reference must round-trip through the property API");
}

void test_catalog_list_form_extension_model() {
    Form form;
    form.id = ObjectId{1};
    form.name = "CatalogList";
    form.extension = FormExtensionKind::catalog_list;
    OrdinaryFormDocument document(form);
    expect(document.validate().ok(), "CatalogList must be a declared model extension");
    form.extension_properties.set_explicit(PropertyId::from_name("Autonumeration"),
        EnumerationValue{"AutonumerationInForm", "DontUse"});
    document.set_form(form);
    expect(!document.validate().ok(), "CatalogList must reject CatalogItem settings");
}

void test_saved_form_settings_model() {
    using namespace oof::model::metamodel;
    expect(form_extension_property_descriptors().size() == 11,
        "four form extension owners expose eleven scoped property descriptors");
    const auto save = PropertyId::from_name("SaveValues");
    const auto restore = PropertyId::from_name("RestoreValuesOnOpen");
    const auto result = PropertyId::from_name("ReportResult");
    for (const auto kind : {FormExtensionKind::data_processor, FormExtensionKind::report}) {
        Form form; form.id = ObjectId{1}; form.name = "SavedSettings"; form.extension = kind;
        form.main_attribute = AttributeRef{ObjectId{20}};
        form.saved_values = {
            {UuidValue{"22222222-2222-2222-2222-222222222222"}, "ModuleVariable", {}},
            {UuidValue{"22222222-2222-2222-2222-222222222222"}, "SelectedValue", AttributeRef{ObjectId{0}}}};
        for (std::uint64_t id = 100; id < 112; ++id) form.children.emplace_back(ControlRef{ObjectId{id}});
        form.children.emplace_back(ControlRef{ObjectId{200}});
        OrdinaryFormDocument document(std::move(form));
        Attribute object; object.id = ObjectId{20}; object.name = "Object";
        TypeDomainEntry object_type; object_type.term = TypeDomainTerm::object;
        object_type.type_uuid = UuidValue{"11111111-1111-1111-1111-111111111111"};
        object.type.entries.push_back(std::move(object_type)); document.add_attribute(std::move(object));
        Attribute attribute; attribute.id = ObjectId{0}; attribute.name = "SelectedValue";
        TypeDomainEntry string_type; string_type.term = TypeDomainTerm::string;
        attribute.type.entries.push_back(std::move(string_type)); document.add_attribute(std::move(attribute));
        for (std::uint64_t id = 100; id < 112; ++id)
            document.add_control(ControlNode{ObjectId{id}, "Result" + std::to_string(id), SpreadsheetDocumentFieldPayload{}});
        document.add_control(ControlNode{ObjectId{200}, "WrongResult", LabelDecorationPayload{}});
        const auto* save_descriptor = find_form_extension_property(kind, save);
        expect(save_descriptor && save_descriptor->designer_only && save_descriptor->api_access == ApiAccess::unknown,
            "SaveValues must identify its proven Designer surface without inventing a native script property");
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
        expect(document.get_prop_val(owner, save).value == PropertyValue{false} &&
            document.get_prop_val(owner, restore).value == PropertyValue{true} && document.validate().ok(),
            "saved bindings preserve external names and Attribute zero independently of default flags");
        for (const auto property : {save, restore}) {
            expect(document.set_prop_val(owner, property, property == save) == PropertyMutationStatus::applied &&
                document.get_prop_val(owner, property).value == PropertyValue{property == save},
                "named settings are editable through the source object model");
            expect(document.set_prop_val(owner, property, std::string{"invalid"}) == PropertyMutationStatus::invalid_value,
                "boolean settings reject untyped strings");
            expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset,
                "named settings reset to descriptor defaults");
        }
        for (const auto name : {"BeforeSaveValues", "AfterRestoreValues"}) {
            const auto* event = find_form_event(name);
            expect(event && event->extension_kinds.size() == 2 && form_event_uses_table(*event, kind) &&
                !form_event_uses_table(*event, std::nullopt) &&
                !form_event_uses_table(*event, FormExtensionKind::catalog_item),
                "settings events share two owners while retaining a separate storage table");
        }
        if (kind == FormExtensionKind::report) {
            expect(document.get_prop_val(owner, result).value == PropertyValue{UndefinedValue{}},
                "ReportResult defaults to Undefined");
            for (std::uint64_t id = 100; id < 112; ++id)
                expect(document.set_prop_val(owner, result, ControlRef{ObjectId{id}}) == PropertyMutationStatus::applied &&
                    document.get_prop_val(owner, result).value == PropertyValue{ControlRef{ObjectId{id}}},
                    "all twelve SpreadsheetDocumentField targets are accepted as named result references");
            expect(document.set_prop_val(owner, result, ControlRef{ObjectId{200}}) == PropertyMutationStatus::invalid_reference &&
                document.set_prop_val(owner, result, ControlRef{ObjectId{999}}) == PropertyMutationStatus::invalid_reference &&
                document.set_prop_val(owner, result, std::int64_t{100}) == PropertyMutationStatus::invalid_value,
                "ReportResult rejects wrong control kinds, dangling identities and scalar substitutes");
            expect(document.set_prop_val(owner, result, UndefinedValue{}) == PropertyMutationStatus::applied &&
                document.reset_prop_val(owner, result) == PropertyMutationStatus::reset,
                "ReportResult supports explicit Undefined and descriptor reset");
        } else {
            expect(document.get_prop_val(owner, result).status == PropertyReadStatus::unsupported_surface,
                "DataProcessor does not acquire the ReportResult property");
        }
        auto original = document.form();
        for (int variant = 0; variant < 7; ++variant) {
            auto invalid = original;
            if (variant == 0) invalid.saved_values[0].source_id.canonical = "invalid";
            else if (variant == 1) invalid.saved_values[0].name.clear();
            else if (variant == 2) invalid.saved_values[1].attribute = AttributeRef{ObjectId{999}};
            else if (variant == 3) invalid.saved_values[0].name = " \t\r\n";
            else if (variant == 4) invalid.saved_values[0].name = std::string(1, '\x01');
            else if (variant == 5) invalid.saved_values[0].name = "\xef\xbf\xbf";
            else invalid.extension = FormExtensionKind::catalog_list;
            document.set_form(std::move(invalid));
            expect(!document.validate().ok(), "malformed, dangling and wrongly owned saved bindings fail model validation");
        }
        document.set_form(original);
        expect(document.validate().ok(), "binding validation errors leave the retained valid source form recoverable");
    }
}

void test_catalog_item_form_extension_property_api() {
    Form form;
    form.id = ObjectId{1};
    form.extension = FormExtensionKind::catalog_item;
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto autonumeration = PropertyId::from_name("Autonumeration");
    const auto folders = PropertyId::from_name("UseForFoldersAndItems");
    expect(document.get_prop_val(owner, autonumeration).status == PropertyReadStatus::proven_default &&
               document.get_prop_val(owner, autonumeration).value ==
                   PropertyValue{EnumerationValue{"AutonumerationInForm", "Auto"}} &&
               document.get_prop_val(owner, folders).status == PropertyReadStatus::proven_default &&
               document.get_prop_val(owner, folders).value ==
                   PropertyValue{EnumerationValue{"FoldersAndItemsUse", "Items"}},
        "CatalogItemFormExtension must expose its typed Auto and Items defaults");
    for (const auto& [property, type, member] : std::array{
             std::tuple{autonumeration, std::string_view{"AutonumerationInForm"}, std::string_view{"DontUse"}},
             std::tuple{folders, std::string_view{"FoldersAndItemsUse"}, std::string_view{"Folders"}},
             std::tuple{folders, std::string_view{"FoldersAndItemsUse"}, std::string_view{"FoldersAndItems"}}}) {
        expect(document.set_prop_val(owner, property, EnumerationValue{std::string(type), std::string(member)}) ==
                   PropertyMutationStatus::applied &&
                   document.get_prop_val(owner, property).status == PropertyReadStatus::explicit_value,
            "CatalogItemFormExtension must accept each declared enumeration member");
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset,
            "CatalogItemFormExtension enum properties must reset to their defaults");
    }
    expect(document.set_prop_val(owner, autonumeration, EnumerationValue{"FoldersAndItemsUse", "Items"}) ==
               PropertyMutationStatus::invalid_value &&
               document.set_prop_val(owner, folders, EnumerationValue{"FoldersAndItemsUse", "Unknown"}) ==
                   PropertyMutationStatus::invalid_value &&
               document.set_prop_val(owner, folders, true) == PropertyMutationStatus::invalid_value,
        "CatalogItemFormExtension must reject unknown enum types, members, and value kinds");

    Form unextended_form;
    unextended_form.id = ObjectId{1};
    OrdinaryFormDocument unextended(std::move(unextended_form));
    expect(unextended.get_prop_val(owner, autonumeration).status == PropertyReadStatus::unsupported_surface &&
               unextended.set_prop_val(owner, autonumeration, EnumerationValue{"AutonumerationInForm", "Auto"}) ==
                   PropertyMutationStatus::unsupported_surface,
        "CatalogItemFormExtension properties must be unavailable when no extension is selected");
}

void test_document_form_extension_property_api() {
    Form form;
    form.id = ObjectId{1};
    form.extension = FormExtensionKind::document;
    OrdinaryFormDocument document(std::move(form));
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const auto repost = PropertyId::from_name("RepostOnWrite");
    const auto posting = PropertyId::from_name("UsePostingMode");
    const auto auto_time = PropertyId::from_name("AutoTime");
    const auto autonumeration = PropertyId::from_name("Autonumeration");
    expect(document.get_prop_val(owner, repost).status == PropertyReadStatus::proven_default &&
               document.get_prop_val(owner, repost).value == PropertyValue{true} &&
               document.get_prop_val(owner, posting).status == PropertyReadStatus::proven_default &&
               document.get_prop_val(owner, posting).value ==
                   PropertyValue{EnumerationValue{"PostingModeUse", "Auto"}} &&
               document.get_prop_val(owner, auto_time).status == PropertyReadStatus::proven_default &&
               document.get_prop_val(owner, auto_time).value ==
                   PropertyValue{EnumerationValue{"AutoTimeMode", "CurrentOrLast"}} &&
               document.get_prop_val(owner, autonumeration).status == PropertyReadStatus::proven_default &&
               document.get_prop_val(owner, autonumeration).value ==
                   PropertyValue{EnumerationValue{"AutonumerationInForm", "Auto"}},
        "DocumentFormExtension properties must expose the native confirmed defaults through the form API");

    expect(document.set_prop_val(owner, repost, false) == PropertyMutationStatus::applied &&
               document.get_prop_val(owner, repost).value == PropertyValue{false} &&
               document.set_prop_val(owner, repost, std::string{"true"}) == PropertyMutationStatus::invalid_value &&
               document.reset_prop_val(owner, repost) == PropertyMutationStatus::reset &&
               document.get_prop_val(owner, repost).value == PropertyValue{true},
        "RepostOnWrite must support typed set, reject wrong value kinds, and reset");

    for (const auto& [property, type, member] : std::array{
             std::tuple{posting, std::string_view{"PostingModeUse"}, std::string_view{"Regular"}},
             std::tuple{posting, std::string_view{"PostingModeUse"}, std::string_view{"RealTime"}},
             std::tuple{posting, std::string_view{"PostingModeUse"}, std::string_view{"Ask"}},
             std::tuple{posting, std::string_view{"PostingModeUse"}, std::string_view{"Auto"}},
             std::tuple{auto_time, std::string_view{"AutoTimeMode"}, std::string_view{"DontUse"}},
             std::tuple{auto_time, std::string_view{"AutoTimeMode"}, std::string_view{"First"}},
             std::tuple{auto_time, std::string_view{"AutoTimeMode"}, std::string_view{"Last"}},
             std::tuple{auto_time, std::string_view{"AutoTimeMode"}, std::string_view{"CurrentOrFirst"}},
             std::tuple{auto_time, std::string_view{"AutoTimeMode"}, std::string_view{"CurrentOrLast"}},
             std::tuple{autonumeration, std::string_view{"AutonumerationInForm"}, std::string_view{"Auto"}},
             std::tuple{autonumeration, std::string_view{"AutonumerationInForm"}, std::string_view{"DontUse"}}}) {
        const EnumerationValue value{std::string(type), std::string(member)};
        expect(document.set_prop_val(owner, property, value) == PropertyMutationStatus::applied &&
                   document.get_prop_val(owner, property).status == PropertyReadStatus::explicit_value &&
                   document.get_prop_val(owner, property).value == PropertyValue{value},
            "DocumentFormExtension must accept each documented enum member through the named property API");
        const PropertyValue expected_default = property == posting
            ? PropertyValue{EnumerationValue{"PostingModeUse", "Auto"}}
            : property == auto_time
                ? PropertyValue{EnumerationValue{"AutoTimeMode", "CurrentOrLast"}}
                : PropertyValue{EnumerationValue{"AutonumerationInForm", "Auto"}};
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset &&
                   document.get_prop_val(owner, property).status == PropertyReadStatus::proven_default &&
                   document.get_prop_val(owner, property).value == expected_default,
            "DocumentFormExtension enum properties must reset to their exact native confirmed defaults");
    }
    expect(document.set_prop_val(owner, posting, EnumerationValue{"AutoTimeMode", "First"}) ==
               PropertyMutationStatus::invalid_value &&
               document.set_prop_val(owner, posting, EnumerationValue{"PostingModeUse", "Unknown"}) ==
               PropertyMutationStatus::invalid_value &&
               document.set_prop_val(owner, auto_time, EnumerationValue{"AutoTimeMode", "Unknown"}) ==
               PropertyMutationStatus::invalid_value &&
               document.set_prop_val(owner, autonumeration, EnumerationValue{"AutonumerationInForm", "Unknown"}) ==
               PropertyMutationStatus::invalid_value,
        "DocumentFormExtension enum properties must reject unknown types and members");

    Form catalog_form;
    catalog_form.id = ObjectId{1};
    catalog_form.extension = FormExtensionKind::catalog_item;
    OrdinaryFormDocument catalog(std::move(catalog_form));
    expect(catalog.get_prop_val(owner, repost).status == PropertyReadStatus::unsupported_surface &&
               catalog.set_prop_val(owner, posting, EnumerationValue{"PostingModeUse", "Auto"}) ==
                   PropertyMutationStatus::unsupported_surface,
        "DocumentFormExtension properties must be unavailable on unrelated extensions");
}

void test_control_position_property_api() {
    using namespace oof::model;
    using namespace oof::model::metamodel;

    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));
    const std::array<ControlPayload, 12> payloads{{
        PanelPayload{},
        ButtonPayload{},
        CheckBoxPayload{},
        ChoiceFieldPayload{},
        RadioButtonPayload{},
        InputFieldPayload{},
        UsualGroupPayload{},
        SplitterPayload{},
        DendrogramPayload{},
        GeographicalSchemaFieldPayload{},
        GraphicalSchemaFieldPayload{},
        CalendarFieldPayload{},
    }};
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        document.add_control(ControlNode{
            ObjectId{static_cast<unsigned>(100 + index)},
            "PositionControl" + std::to_string(index),
            payloads[index]});
    }

    const std::array<std::pair<std::string_view, PropertyValue>, 5> values{{
        {"Left", std::int64_t{13}},
        {"Top", std::int64_t{17}},
        {"Width", std::int64_t{120}},
        {"Height", std::int64_t{45}},
        {"Visible", false},
    }};
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        const ObjectId control_id{static_cast<unsigned>(100 + index)};
        const auto owner = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, control_id};
        const auto* control = document.find_control(control_id);
        expect(control != nullptr, "position API fixture control must exist");
        if (!control) continue;

        for (const auto& [name, value] : values) {
            const auto property = PropertyId::from_name(name);
            const auto* descriptor = find_property(control->kind(), name);
            expect(descriptor && descriptor->surface == PropertySurface::panel_placement &&
                       descriptor->persistence == PersistenceClass::persisted_editable &&
                       descriptor->storage_codec == StorageCodec::position_record &&
                       descriptor->default_value.canonical == (name == "Visible" ? "true" : "0"),
                "shared Position fields must have persisted position-record descriptors");
            if (name != "Visible") {
                expect(descriptor && descriptor->value_codec == ValueCodec::integer32,
                    "Position coordinates and dimensions must use the int32 codec");
            }

            const auto default_value = document.get_prop_val(owner, property);
            expect(default_value.status == PropertyReadStatus::proven_default && default_value.value &&
                       *default_value.value == (name == "Visible" ? PropertyValue{true} : PropertyValue{std::int64_t{0}}),
                "Position API must expose the shared default");
            expect(document.set_prop_val(owner, property, value) == PropertyMutationStatus::applied,
                "Position API must accept a valid shared field value");
            const auto explicit_value = document.get_prop_val(owner, property);
            expect(explicit_value.status == PropertyReadStatus::explicit_value && explicit_value.value &&
                       *explicit_value.value == value,
                "Position API must return the explicit shared field value");

            const auto* updated = document.find_control(control_id);
            bool position_matches = false;
            if (updated) {
                if (name == "Left") position_matches = updated->position.left.is_explicit() && updated->position.left.value() == 13;
                else if (name == "Top") position_matches = updated->position.top.is_explicit() && updated->position.top.value() == 17;
                else if (name == "Width") position_matches = updated->position.width.is_explicit() && updated->position.width.value() == 120;
                else if (name == "Height") position_matches = updated->position.height.is_explicit() && updated->position.height.value() == 45;
                else position_matches = updated->position.visible.is_explicit() && !updated->position.visible.value();
            }
            expect(position_matches,
                "Position API mutation must synchronize the named Position member");
            expect(updated && !updated->properties().contains(property) &&
                       !updated->extension_properties.contains(property),
                "Position API fields must not be stored in payload or extension properties");

            expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset,
                "Position API fields must reset through the common API");
            const auto reset_value = document.get_prop_val(owner, property);
            expect(reset_value.status == PropertyReadStatus::proven_default && reset_value.value &&
                       *reset_value.value == (name == "Visible" ? PropertyValue{true} : PropertyValue{std::int64_t{0}}),
                "reset Position API fields must restore their defaults");
            const auto* reset = document.find_control(control_id);
            bool position_is_implicit = false;
            if (reset) {
                if (name == "Left") position_is_implicit = !reset->position.left.is_explicit();
                else if (name == "Top") position_is_implicit = !reset->position.top.is_explicit();
                else if (name == "Width") position_is_implicit = !reset->position.width.is_explicit();
                else if (name == "Height") position_is_implicit = !reset->position.height.is_explicit();
                else position_is_implicit = !reset->position.visible.is_explicit();
            }
            expect(position_is_implicit,
                "reset Position API fields must clear the matching explicit Position member");
        }

        for (const auto dimension : {std::string_view{"Width"}, std::string_view{"Height"}}) {
            expect(document.set_prop_val(owner, PropertyId::from_name(dimension), std::int64_t{-1}) ==
                       PropertyMutationStatus::invalid_value,
                "negative Position dimensions must be rejected");
        }
        const auto left = PropertyId::from_name("Left");
        expect(document.set_prop_val(owner, left, std::int64_t{2147483648LL}) ==
                   PropertyMutationStatus::invalid_value &&
                   document.get_prop_val(owner, left).status == PropertyReadStatus::proven_default,
            "Position int32 overflow must be rejected without changing the default");
        expect(document.set_prop_val(owner, left, std::string{"13"}) ==
                   PropertyMutationStatus::invalid_value &&
                   document.get_prop_val(owner, left).status == PropertyReadStatus::proven_default,
            "wrong-typed Position values must be rejected without changing the default");
    }
}

void test_default_control_focus_groups() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    const auto property = PropertyId::from_name("DefaultControl");
    Form form;
    form.id = ObjectId{1};
    form.children = {PageRef{ObjectId{20}}, PageRef{ObjectId{21}}, ControlRef{ObjectId{30}},
        ControlRef{ObjectId{31}}, ControlRef{ObjectId{112}}, ControlRef{ObjectId{113}}};
    OrdinaryFormDocument document(std::move(form));
    ControlNode normal{ObjectId{30}, "Normal", PanelPayload{}};
    normal.children = {PageRef{ObjectId{22}}, PageRef{ObjectId{23}}};
    document.add_control(std::move(normal));
    ControlNode shared{ObjectId{31}, "Shared", PanelPayload{}};
    shared.properties().set_explicit(PropertyId::from_name("SeparateByPage"), false);
    shared.children = {PageRef{ObjectId{24}}, PageRef{ObjectId{25}}};
    document.add_control(std::move(shared));
    constexpr std::array<std::size_t, 12> pages{0, 0, 0, 1, 1, 1, 2, 2, 3, 4, 4, 5};
    constexpr std::array<std::size_t, 12> groups{0, 0, 0, 1, 1, 1, 2, 2, 3, 4, 4, 4};
    for (std::size_t page_index = 0; page_index < 6; ++page_index) {
        Page page;
        page.id = ObjectId{20 + page_index};
        page.name = "Page" + std::to_string(page_index);
        for (std::size_t index = 0; index < pages.size(); ++index)
            if (pages[index] == page_index) page.children.emplace_back(ControlRef{ObjectId{100 + index}});
        document.add_page(std::move(page));
    }
    for (std::uint64_t id = 100; id < 114; ++id)
        document.add_control(ControlNode{ObjectId{id}, "Input" + std::to_string(id), InputFieldPayload{}});
    const auto owner = [](std::size_t index) {
        return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{100 + index}};
    };
    std::array<bool, 12> selected{};
    for (std::size_t index = 0; index < groups.size(); ++index) {
        const auto* descriptor = find_property(ControlKind::input_field, property);
        expect(descriptor && descriptor->surface == PropertySurface::panel_placement &&
                   descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == StorageCodec::position_record && descriptor->api_access == ApiAccess::read_write &&
                   descriptor->default_value.kind == DefaultKind::boolean && descriptor->default_value.canonical == "false",
            "DefaultControl must expose the persisted Boolean position-record contract");
        expect(document.get_prop_val(owner(index), property).value == PropertyValue{false},
            "each focus group control must initially expose false");
        expect(document.set_prop_val(owner(index), property, true) == PropertyMutationStatus::applied,
            "DefaultControl true must select the target in its immediate focus group");
        for (std::size_t other = 0; other < selected.size(); ++other) {
            if (groups[other] == groups[index]) selected[other] = other == index;
            expect(document.get_prop_val(owner(other), property).value == PropertyValue{selected[other]},
                "DefaultControl must clear only peers in the same root page, normal page, or shared panel");
        }
        expect(document.validate().ok(), "independent focus groups may each have one DefaultControl");
        expect(document.set_prop_val(owner(index), property, std::string{"true"}) == PropertyMutationStatus::invalid_value &&
                   document.get_prop_val(owner(index), property).value == PropertyValue{true},
            "invalid DefaultControl values must not mutate any selection");
    }
    expect(document.set_prop_val(owner(12), property, true) == PropertyMutationStatus::applied &&
               document.set_prop_val(owner(13), property, true) == PropertyMutationStatus::applied &&
               document.get_prop_val(owner(12), property).value == PropertyValue{false} &&
               document.get_prop_val(owner(13), property).value == PropertyValue{true},
        "direct root controls must share a focus group separate from root page children");
    for (std::size_t index = 0; index < selected.size(); ++index)
        expect(document.get_prop_val(owner(index), property).value == PropertyValue{selected[index]},
            "selecting a direct root control must preserve all nested focus groups");
    for (std::size_t index = 0; index < selected.size(); ++index) {
        expect(document.reset_prop_val(owner(index), property) == PropertyMutationStatus::reset,
            "DefaultControl reset must clear only its own selection");
        selected[index] = false;
        const auto reset = document.get_prop_val(owner(index), property);
        expect(reset.status == PropertyReadStatus::proven_default && reset.value == PropertyValue{false} &&
                   !document.find_control(ObjectId{100 + index})->position.default_control.is_explicit(),
            "DefaultControl reset must unset the named Position property and restore false");
        for (std::size_t other = 0; other < selected.size(); ++other)
            expect(document.get_prop_val(owner(other), property).value == PropertyValue{selected[other]},
                "DefaultControl reset must not reassign or clear any other group");
    }
    expect(document.set_prop_val(owner(13), property, false) == PropertyMutationStatus::applied &&
               document.get_prop_val(owner(13), property).status == PropertyReadStatus::proven_default,
        "DefaultControl false must clear the target without selecting another control");
    for (const auto pair : {std::pair{0u, 1u}, std::pair{6u, 7u}, std::pair{9u, 11u}}) {
        auto* first = const_cast<ControlNode*>(document.find_control(ObjectId{100 + pair.first}));
        auto* second = const_cast<ControlNode*>(document.find_control(ObjectId{100 + pair.second}));
        first->position.default_control.set(true);
        second->position.default_control.set(true);
        expect(document.validate().has(InvariantCode::invalid_property),
            "direct model construction must reject multiple DefaultControl values within each focus group");
        first->position.default_control.reset();
        second->position.default_control.reset();
    }
    auto* root_page = const_cast<Page*>(document.find_page(ObjectId{20}));
    root_page->children.emplace_back(ControlRef{ObjectId{999}});
    expect(document.set_prop_val(owner(0), property, true) == PropertyMutationStatus::unsupported_surface &&
               document.reset_prop_val(owner(0), property) == PropertyMutationStatus::unsupported_surface &&
               document.get_prop_val(owner(0), property).value == PropertyValue{false},
        "unresolved focus group references must prevent DefaultControl mutation");
    root_page->children.pop_back();
    auto& root = const_cast<Form&>(document.form());
    root.children.emplace_back(ControlRef{ObjectId{100}});
    expect(document.set_prop_val(owner(0), property, true) == PropertyMutationStatus::unsupported_surface &&
               document.reset_prop_val(owner(0), property) == PropertyMutationStatus::unsupported_surface &&
               !document.find_control(ObjectId{100})->position.default_control.is_explicit(),
        "ambiguous immediate ownership must prevent DefaultControl mutation");
    root.children.pop_back();
    expect(document.set_prop_val({ObjectCategory::control, ObjectId{999}}, property, true) == PropertyMutationStatus::unknown_owner &&
               document.validate().ok(),
        "unknown DefaultControl owners must not mutate a valid focus graph");
    expect(document.get_prop_val(owner(11), PropertyId::from_name("TabOrder")).value == PropertyValue{std::int64_t{3}} &&
        document.set_prop_val(owner(11), PropertyId::from_name("ZOrder"), std::int64_t{1}) == PropertyMutationStatus::applied &&
        document.get_prop_val(owner(11), PropertyId::from_name("ZOrder")).value == PropertyValue{std::int64_t{1}} &&
        document.get_prop_val(owner(11), PropertyId::from_name("TabOrder")).value == PropertyValue{std::int64_t{3}} && document.validate().ok(),
        "shared-page reordering must preserve the independent global TabOrder while canonicalizing one physical child sequence");
}

void test_control_collapse_runtime_property() {
    using namespace oof::model;
    using namespace oof::model::metamodel;

    Form form;
    form.id = ObjectId{1};
    OrdinaryFormDocument document(std::move(form));
    const std::array<ControlPayload, 12> payloads{{
        InputFieldPayload{}, PanelPayload{}, SpreadsheetDocumentFieldPayload{}, TablePayload{},
        ButtonPayload{}, CheckBoxPayload{}, ChoiceFieldPayload{}, RadioButtonPayload{},
        UsualGroupPayload{}, SplitterPayload{}, LabelDecorationPayload{}, CalendarFieldPayload{},
    }};
    const auto property = PropertyId::from_name("Collapse");
    const PropertyValue default_value{EnumerationValue{"ControlCollapseMode", "None"}};
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        const ObjectId id{static_cast<unsigned>(100 + index)};
        document.add_control(ControlNode{id, "CollapseControl" + std::to_string(index), payloads[index]});
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, id};
        const auto* descriptor = find_property(document.find_control(id)->kind(), property);
        expect(descriptor && descriptor->surface == PropertySurface::panel_placement &&
                   descriptor->persistence == PersistenceClass::runtime_only &&
                   descriptor->storage_codec == StorageCodec::none &&
                   descriptor->default_value.kind == DefaultKind::enumeration &&
                   descriptor->default_value.canonical == "ControlCollapseMode.None",
            "Collapse must be a shared runtime-only Position property with a proven None default");
        const auto initial = document.get_prop_val(owner, property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value && *initial.value == default_value,
            "Collapse API must expose None before assignment");
        for (const auto member : {"None", "Left", "Right", "Top", "Bottom"}) {
            const EnumerationValue value{"ControlCollapseMode", member};
            expect(document.set_prop_val(owner, property, value) == PropertyMutationStatus::applied_runtime_only,
                "Collapse assignments must accept every valid member on each common control kind");
            const auto read = document.get_prop_val(owner, property);
            expect(read.value && *read.value == PropertyValue{value} && read.status == PropertyReadStatus::explicit_value,
                "Collapse GET must expose each assigned runtime state");
            const auto* control = document.find_control(id);
            expect(control && !control->properties().contains(property) && !control->extension_properties.contains(property) &&
                       control->position.collapse.is_explicit() &&
                       control->position.collapse.value() == std::optional<EnumerationValue>{value},
                "Collapse runtime state must have one owner in Position");
            for (const PropertyValue& invalid : {PropertyValue{EnumerationValue{"ControlCollapseMode", "Unknown"}},
                     PropertyValue{EnumerationValue{"OtherType", member}}, PropertyValue{false}, PropertyValue{std::string{member}}}) {
                expect(document.set_prop_val(owner, property, invalid) == PropertyMutationStatus::invalid_value,
                    "Collapse must reject invalid enumeration types, members, and value types before mutation");
                const auto unchanged = document.get_prop_val(owner, property);
                expect(unchanged.status == read.status && unchanged.value == read.value,
                    "rejected Collapse assignments must preserve the previous runtime state");
            }
            expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::reset_runtime_only,
                "Collapse reset must report runtime-only state");
            const auto reset = document.get_prop_val(owner, property);
            expect(reset.status == PropertyReadStatus::proven_default && reset.value && *reset.value == default_value &&
                       !document.find_control(id)->position.collapse.is_explicit(),
                "Collapse reset must clear explicit Position state and restore None");
        }
    }
}

void test_direct_collapse_validation() {
    Form form; form.id = ObjectId{1}; form.name = "DirectCollapse";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t i = 0; i < 12; ++i) document.add_control(ControlNode{ObjectId{100 + i}, "Button" + std::to_string(i), ButtonPayload{}});
    expect(document.validate().ok(), "direct Collapse fixture must be valid before mutation");
    for (std::uint64_t i = 0; i < 12; ++i) for (const auto& value : {
            EnumerationValue{"OtherType", "None"}, EnumerationValue{"ControlCollapseMode", "Unknown"}}) {
        auto invalid = document;
        const_cast<ControlNode*>(invalid.find_control(ObjectId{100 + i}))->position.collapse.set(value);
        expect(invalid.validate().has(InvariantCode::invalid_property), "direct invalid Collapse values must fail model validation");
    }
    auto dangling = document;
    auto bad_form = dangling.form(); bad_form.children.emplace_back(ControlRef{ObjectId{999}}); dangling.set_form(std::move(bad_form));
    for (std::uint64_t i = 0; i < 12; ++i) const_cast<ControlNode*>(dangling.find_control(ObjectId{100 + i}))->position.default_control.set(true);
    expect(dangling.validate().has(InvariantCode::dangling_reference), "derived focus validation must preserve malformed ownership diagnostics");
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
    AttributeRef absent_attribute;
    const AttributeRef zero_attribute{ObjectId{0}};
    expect(!absent_attribute && !absent_attribute.has_value() &&
        zero_attribute && zero_attribute.has_value() && zero_attribute.id() == ObjectId{0} &&
        absent_attribute != zero_attribute,
        "default AttributeRef must be absent while explicit Attribute ID 0 remains present");
    expect(!ControlRef{} && !ControlRef{ObjectId{0}} && !PageRef{} && !PageRef{ObjectId{0}} &&
        !EventRef{} && !EventRef{ObjectId{0}},
        "ID 0 remains invalid for non-Attribute references and their defaults remain absent");

    Form form;
    form.id = ObjectId{1};
    form.name = "Main";
    form.children.push_back(ControlRef{ObjectId{4}});
    form.events.push_back(EventRef{ObjectId{5}});
    OrdinaryFormDocument document(std::move(form));

    document.add_attribute(Attribute{ObjectId{0}, "ZeroAttribute", TypeDomainPatternValue{}});
    document.add_attribute(Attribute{ObjectId{1}, "SyntheticValue", TypeDomainPatternValue{}});
    ControlNode input{ObjectId{4}, "InputSynthetic", InputFieldPayload{}};
    input.data_path = DataPath{zero_attribute, {}};
    document.add_control(std::move(input));
    document.add_event(Event{ObjectId{5}, "OnOpen", "OnOpen", FormRef{ObjectId{1}}});

    expect(document.find_attribute(ObjectId{1}) != nullptr,
        "AttributeRef ID 1 must resolve when FormRef also has ID 1");
    expect(document.find_attribute(ObjectId{0}) != nullptr &&
        document.find_control(ObjectId{4})->data_path->attribute == zero_attribute,
        "declared Attribute ID 0 must resolve as a present DataPath reference");
    expect(document.find_control(ObjectId{4}) != nullptr,
        "control ID 4 must resolve independently");
    expect(document.find(ObjectId{1}) == std::nullopt,
        "untyped lookup must report ambiguity for Form ID 1 and Attribute ID 1");
    expect(document.indexed_id_count() == 5,
        "index count must include each category-scoped object identity");
    expect(document.validate().ok(),
        "Form 1, Attribute 1, and DataPath from Control 4 must validate");

    auto duplicate_zero = document;
    duplicate_zero.add_attribute(Attribute{ObjectId{0}, "DuplicateZero", TypeDomainPatternValue{}});
    expect(duplicate_zero.validate().has(InvariantCode::duplicate_id),
        "duplicate Attribute ID 0 must be rejected by category-scoped identity validation");

    Form list_form;
    list_form.id = ObjectId{1};
    list_form.name = "ZeroList";
    OrdinaryFormDocument list_document(std::move(list_form));
    TypeDomainEntry list_entry;
    list_entry.term = TypeDomainTerm::value_list;
    list_document.add_attribute(Attribute{ObjectId{0}, "Values", TypeDomainPatternValue{{list_entry}}});
    const auto values = ValueListRef::create(ValueListValue{});
    expect(values && list_document.bind_attribute_value_list(zero_attribute, *values) ==
               PropertyMutationStatus::applied_runtime_only &&
               list_document.attribute_value_list(zero_attribute).has_value(),
        "runtime ValueList binding must accept the explicit Attribute ID 0");
    expect(!list_document.attribute_value_list(absent_attribute) &&
               list_document.bind_attribute_value_list(absent_attribute, *values) ==
                   PropertyMutationStatus::unknown_owner,
        "an absent runtime AttributeRef must not alias a declared and bound Attribute ID 0");

    Form invalid_form;
    invalid_form.id = ObjectId{1};
    OrdinaryFormDocument invalid_ids(std::move(invalid_form));
    invalid_ids.add_control(ControlNode{ObjectId{0}, "InvalidControlZero", ButtonPayload{}});
    Page page_zero;
    page_zero.id = ObjectId{0};
    page_zero.name = "InvalidPageZero";
    invalid_ids.add_page(std::move(page_zero));
    invalid_ids.add_event(Event{ObjectId{0}, "InvalidEventZero", "OnOpen", FormRef{ObjectId{1}}});
    expect(invalid_ids.validate().has(InvariantCode::invalid_id),
        "Control, Page, and Event object IDs must remain positive when Attribute ID 0 is valid");
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

void test_picture_property_model_api() {
    using namespace oof::model::metamodel;

    Form form;
    form.id = ObjectId{1};
    form.children = {ControlRef{ObjectId{10}}, ControlRef{ObjectId{11}}};
    OrdinaryFormDocument document(std::move(form));
    document.add_asset(PictureAsset{ObjectId{50}, "Items/Choice/Picture.gif", PictureFormat::gif, {}, true});
    document.add_asset(PictureAsset{ObjectId{51}, "Items/Input/Picture.gif", PictureFormat::gif, {}, true});
    document.add_control(ControlNode{ObjectId{10}, "Choice", ChoiceFieldPayload{}});
    document.add_control(ControlNode{ObjectId{11}, "Input", InputFieldPayload{}});

    const auto verify_descriptor = [](ControlKind kind, std::string_view name) {
        const auto* descriptor = find_property(kind, name);
        expect(descriptor && descriptor->value_kind == ValueKind::picture &&
                descriptor->value_codec == ValueCodec::picture &&
                descriptor->api_access == ApiAccess::read_write &&
                descriptor->persistence == PersistenceClass::persisted_editable &&
                descriptor->storage_codec == StorageCodec::picture_record &&
                descriptor->default_value.kind == DefaultKind::none,
            "field picture properties must use the persisted typed picture descriptor without a fabricated default");
    };
    for (const auto kind : {ControlKind::choice_field, ControlKind::input_field}) {
        verify_descriptor(kind, "Picture");
        verify_descriptor(kind, "ChoiceButtonPicture");
    }

    const auto choice = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{10}};
    const auto input = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, ObjectId{11}};
    for (const auto& [owner, property] : {
             std::pair{choice, PropertyId::from_name("Picture")},
             std::pair{choice, PropertyId::from_name("ChoiceButtonPicture")},
             std::pair{input, PropertyId::from_name("Picture")},
             std::pair{input, PropertyId::from_name("ChoiceButtonPicture")}}) {
        expect(document.get_prop_val(owner, property).status == PropertyReadStatus::no_default,
            "unset picture properties must report the unproven empty-picture default");
    }

    const PictureRef choice_standard{PictureAssetRef{}, QualifiedName{"PictureLib.Calendar"}};
    const PictureRef choice_button_asset{PictureAssetRef{ObjectId{50}}, std::nullopt};
    const PictureRef input_asset{PictureAssetRef{ObjectId{51}}, std::nullopt};
    const PictureRef input_button_standard{PictureAssetRef{}, QualifiedName{"PictureLib.ChooseValue"}};
    const auto set_and_check = [&](OrdinaryFormDocument::ObjectKey owner, std::string_view property_name,
                                   const PictureRef& picture) {
        const auto property = PropertyId::from_name(property_name);
        expect(document.set_prop_val(owner, property, picture) == PropertyMutationStatus::applied,
            "picture properties must accept a valid standard or asset reference");
        const auto read = document.get_prop_val(owner, property);
        expect(read.status == PropertyReadStatus::explicit_value && read.value &&
                *read.value == PropertyValue{picture},
            "picture properties must return the assigned typed reference");
    };
    set_and_check(choice, "Picture", choice_standard);
    set_and_check(choice, "ChoiceButtonPicture", choice_button_asset);
    set_and_check(input, "Picture", input_asset);
    set_and_check(input, "ChoiceButtonPicture", input_button_standard);

    const OrdinaryFormDocument::ObjectKey form_owner{ObjectCategory::form, ObjectId{1}};
    const auto caption_picture = PropertyId::from_name("CaptionPicture");
    expect(document.set_prop_val(form_owner, caption_picture, choice_standard) == PropertyMutationStatus::applied &&
        document.get_prop_val(form_owner, caption_picture).value == PropertyValue{choice_standard},
        "classified CaptionPicture reports persisted success and retains its named library reference");
    expect(document.set_prop_val(form_owner, caption_picture, input_asset) == PropertyMutationStatus::invalid_value &&
        document.get_prop_val(form_owner, caption_picture).value == PropertyValue{choice_standard},
        "CaptionPicture classification does not permit file-asset assignment through the runtime setter");
    expect(document.set_prop_val(form_owner, caption_picture, PictureRef{}) == PropertyMutationStatus::applied &&
        document.get_prop_val(form_owner, caption_picture).value == PropertyValue{PictureRef{}} &&
        document.reset_prop_val(form_owner, caption_picture) == PropertyMutationStatus::reset,
        "clearing and resetting classified CaptionPicture retain the existing empty-picture semantics");

    expect(document.set_prop_val(choice, PropertyId::from_name("Picture"),
               PictureRef{PictureAssetRef{ObjectId{999}}, std::nullopt}) ==
               PropertyMutationStatus::invalid_reference,
        "field picture properties must reject an asset reference that does not resolve");
    expect(document.set_prop_val(choice, PropertyId::from_name("Picture"), std::string{"not a picture"}) ==
            PropertyMutationStatus::invalid_value,
        "field picture properties must reject values outside the typed picture domain");
    const auto reset_property = PropertyId::from_name("ChoiceButtonPicture");
    expect(document.reset_prop_val(input, reset_property) == PropertyMutationStatus::reset &&
            document.get_prop_val(input, reset_property).status == PropertyReadStatus::no_default,
        "resetting an explicit field picture must restore its no-default state");
    expect(document.validate().ok(), "valid standard and transparent GIF references must validate");

    const auto canonical = PropertyId::from_name("ChoiceButtonPicture");
    const auto alias = PropertyId::from_name("SelButtonPicture");
    for (const auto owner : {choice, input}) {
        const auto* control = document.find_control(owner.id);
        expect(control != nullptr, "picture alias owner must exist");
        for (const auto write_id : {canonical, alias}) {
            const auto read_id = write_id == canonical ? alias : canonical;
            const PictureRef first{PictureAssetRef{}, QualifiedName{"PictureLib.Calendar"}};
            const PictureRef second{PictureAssetRef{}, QualifiedName{"PictureLib.ChooseValue"}};
            expect(document.set_prop_val(owner, write_id, first) == PropertyMutationStatus::applied,
                "either picture alias must set the same canonical value");
            const auto read = document.get_prop_val(owner, read_id);
            expect(read.status == PropertyReadStatus::explicit_value && read.value &&
                    *read.value == PropertyValue{first},
                "the other picture alias must read the assigned value");
            expect(document.set_prop_val(owner, read_id, second) == PropertyMutationStatus::applied,
                "writing through the other alias must replace the same value");
            const auto replaced = document.get_prop_val(owner, write_id);
            expect(replaced.status == PropertyReadStatus::explicit_value && replaced.value &&
                    *replaced.value == PropertyValue{second},
                "replacement must be visible through the original alias");
            expect(control->properties().find(canonical) != nullptr && control->properties().find(alias) == nullptr,
                "the PropertySet must contain one canonical entry and no alias entry");
            expect(document.reset_prop_val(owner, write_id) == PropertyMutationStatus::reset &&
                    document.get_prop_val(owner, read_id).status == PropertyReadStatus::no_default &&
                    control->properties().find(canonical) == nullptr,
                "reset through either alias must clear the shared value");
        }
        auto invalid = document;
        auto* direct = const_cast<ControlNode*>(invalid.find_control(owner.id));
        direct->properties().set_explicit(alias, PictureRef{PictureAssetRef{}, QualifiedName{"PictureLib.Calendar"}});
        expect(!invalid.validate().ok(),
            "a raw alias entry bypassing the property API must fail validation");
    }
}

void test_gantt_interval_date_validation() {
    const auto make_document = [](std::string start, std::string end) {
        Form form;
        form.id = ObjectId{1};
        form.children.push_back(ControlRef{ObjectId{2}});
        OrdinaryFormDocument document(std::move(form));
        GanttChartPayload payload;
        payload.series.push_back({ObjectId{71}, "series", "Series", std::nullopt});
        payload.points.push_back({ObjectId{83}, "point", "Point", std::nullopt});
        payload.intervals.push_back({ObjectId{83}, ObjectId{71}, DateValue{std::move(start)},
            DateValue{std::move(end)}, "Interval"});
        document.add_control(ControlNode{ObjectId{2}, "Timeline", std::move(payload)});
        return document;
    };

    const auto equal_bounds = make_document("2027-01-01T00:00:00", "2027-01-01T00:00:00");
    expect(equal_bounds.validate().ok(), "Gantt interval may have equal start and end until platform semantics prove otherwise");

    const auto malformed = make_document("2027-02-30T00:00:00", "2027-03-01T00:00:00");
    expect(malformed.validate().has(InvariantCode::invalid_property),
        "Gantt interval with an invalid calendar date must be rejected by model validation");

    const auto reversed = make_document("2027-03-02T00:00:00", "2027-03-01T00:00:00");
    expect(reversed.validate().has(InvariantCode::invalid_property),
        "Gantt interval whose start follows its end must be rejected by model validation");
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
    const auto validate_handler = [](bool control_owner, std::string handler) {
        Form form;
        form.id = ObjectId{1};
        if (control_owner) form.children.push_back(ControlRef{ObjectId{10}});
        else form.events.push_back(EventRef{ObjectId{40}});
        OrdinaryFormDocument document(std::move(form));
        if (control_owner) {
            ControlNode button{ObjectId{10}, "Button", ButtonPayload{}};
            button.events.push_back(EventRef{ObjectId{40}});
            document.add_control(std::move(button));
            document.add_event(Event{ObjectId{40}, "Click", std::move(handler), ControlRef{ObjectId{10}}});
        } else {
            document.add_event(Event{ObjectId{40}, "OnOpen", std::move(handler), FormRef{ObjectId{1}}});
        }
        return document.validate();
    };
    for (const bool control_owner : {false, true}) {
        expect(validate_handler(control_owner, "").has(InvariantCode::invalid_property),
            "empty event handler must be rejected for form and control owners");
        expect(validate_handler(control_owner, " Handle Event ").ok(),
            "non-empty event handler containing spaces must remain valid");
        expect(validate_handler(control_owner, " ").ok(),
            "whitespace-only event handler must remain valid");
    }

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

    Form unsupported_form;
    unsupported_form.id = ObjectId{1};
    unsupported_form.children.push_back(ControlRef{ObjectId{10}});
    OrdinaryFormDocument unsupported(std::move(unsupported_form));
    ControlNode pivot_chart{ObjectId{10}, "PivotChart", PivotChartPayload{}};
    pivot_chart.events.push_back(EventRef{ObjectId{40}});
    unsupported.add_control(std::move(pivot_chart));
    unsupported.add_event(Event{ObjectId{40}, "Selection", "Selection", ControlRef{ObjectId{10}}});
    expect(unsupported.validate().has(InvariantCode::invalid_property),
        "unsupported PivotChart event must fail model validation even with a matching owner reference");
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

void test_chart_number_lexical_validation() {
    using namespace oof::model;
    Form form;
    form.id = ObjectId{1};
    form.children.push_back(ControlRef{ObjectId{2}});
    OrdinaryFormDocument document(std::move(form));
    ChartPayload chart;
    ChartSeries series;
    series.id = ObjectId{2};
    series.color.kind = ColorKind::absolute;
    series.color.red = 255;
    series.marker = {"ChartMarkerType", "Auto"};
    chart.series.push_back(series);
    ChartPoint point;
    point.id = ObjectId{1};
    point.color.kind = ColorKind::absolute;
    point.color.blue = 128;
    chart.points.push_back(point);
    chart.values.push_back({ObjectId{2}, ObjectId{1}, DecimalValue{"NaN"}});
    document.add_control(ControlNode{ObjectId{2}, "Chart", std::move(chart)});
    expect(document.validate().has(InvariantCode::invalid_property),
        "Chart model validation must reject non-decimal numeric values such as NaN");
}

void test_chart_series_owner_persistence() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    Form form; form.id = ObjectId{1}; form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument document(std::move(form));
    ChartPayload payload; ChartSeries series; series.id = ObjectId{2}; payload.series.push_back(series);
    document.add_control(ControlNode{ObjectId{2}, "Chart", std::move(payload)});
    const OrdinaryFormDocument::ObjectKey root{ObjectCategory::control, ObjectId{2}};
    const OrdinaryFormDocument::ObjectKey normal{ObjectCategory::control, ObjectId{2}, ChartAreaKind::none, ChartScaleKind::none, ObjectId{2}};
    auto summary = normal; summary.chart_series = ObjectId{1};
    const auto reference_id = PropertyId::from_name("SummarySeries");
    const auto* descriptor = find_property(ControlKind::chart, reference_id);
    expect(descriptor && descriptor->api_access == ApiAccess::read_only && descriptor->persistence == PersistenceClass::persisted_readonly &&
        descriptor->storage_codec == StorageCodec::control_info && descriptor->value_codec == ValueCodec::chart_series_reference && descriptor->default_value.kind == DefaultKind::none,
        "root SummarySeries is a classified readonly owned reference");
    const auto reference = document.get_prop_val(root, reference_id);
    expect(reference.value == PropertyValue{ChartSeriesRef{ObjectId{2}, ObjectId{1}}} && document.get_prop_val(root, reference_id).value == reference.value,
        "root SummarySeries reference is stable and uses its reserved owner-local identity");
    expect(document.set_prop_val(root, reference_id, *reference.value) == PropertyMutationStatus::read_only &&
        document.set_prop_val(root, reference_id, UndefinedValue{}) == PropertyMutationStatus::read_only && document.reset_prop_val(root, reference_id) == PropertyMutationStatus::read_only,
        "root SummarySeries replacement and reset are readonly");
    const auto priority = PropertyId::from_name("ColorPriority");
    expect(document.set_prop_val(normal, priority, true) == PropertyMutationStatus::applied_runtime_only && document.get_prop_val(normal, priority).value == PropertyValue{true},
        "ordinary Series ColorPriority is mutable runtime state");
    expect(document.set_prop_val(normal, priority, std::string{"true"}) == PropertyMutationStatus::invalid_value && document.get_prop_val(normal, priority).value == PropertyValue{true},
        "invalid runtime priority assignments are atomic");
    expect(document.reset_prop_val(normal, priority) == PropertyMutationStatus::reset_runtime_only && document.get_prop_val(normal, priority).value == PropertyValue{false},
        "ordinary Series ColorPriority reset has runtime-only status");
    expect(document.set_prop_val(summary, priority, true) == PropertyMutationStatus::applied && document.get_prop_val(summary, priority).value == PropertyValue{true} &&
        document.reset_prop_val(summary, priority) == PropertyMutationStatus::reset, "SummarySeries ColorPriority retains its persisted owner policy");
    Form bad_form; bad_form.id = ObjectId{1}; bad_form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument bad(std::move(bad_form));
    ControlNode duplicate{ObjectId{2}, "Chart", ChartPayload{}};
    duplicate.properties().set_explicit(reference_id, ChartSeriesRef{ObjectId{2}, ObjectId{1}}); bad.add_control(std::move(duplicate));
    expect(bad.validate().has(InvariantCode::invalid_property), "computed root SummarySeries cannot be duplicated in a scalar bag");
}

void test_chart_summary_series_color_invariants() {
    using namespace oof::model;
    const auto validate = [](ColorValue color, EnumerationValue marker = {"ChartMarkerType", "Auto"}) {
        Form form; form.id = ObjectId{1}; form.children.push_back(ControlRef{ObjectId{2}});
        OrdinaryFormDocument document(std::move(form));
        ChartPayload chart; chart.summary_series.color = std::move(color); chart.summary_series.marker = std::move(marker);
        document.add_control(ControlNode{ObjectId{2}, "Chart", std::move(chart)});
        return document.validate();
    };
    expect(validate(ColorValue{}, {"ChartMarkerType", "Rhomb"}).ok(), "proven SummarySeries Rhomb must validate");
    for (const auto member : {"None", "Rect", "Circle", "Rhomb", "Auto", "Alternation"})
        expect(validate(ColorValue{}, {"ChartMarkerType", member}).ok(), "all six native SummarySeries Marker values validate");
    expect(!validate(ColorValue{}, {"OtherType", "Auto"}).ok(), "foreign SummarySeries Marker enum must fail");
    ColorValue color;
    expect(validate(color).ok(), "default SummarySeries Color must validate");
    color.kind = ColorKind::absolute; color.red = 153; color.green = 25; color.blue = 25;
    expect(validate(color).ok(), "absolute SummarySeries Color must validate");
    color.alpha = 254;
    expect(!validate(color).ok(), "SummarySeries Color must reject transparency");
    color.alpha = 255; color.style = QualifiedName{"Accent"};
    expect(!validate(color).ok(), "SummarySeries Color must reject style payload");
    color = ColorValue{}; color.red = 1;
    expect(!validate(color).ok(), "automatic SummarySeries Color must reject explicit RGB");
}

void test_border_value_invariants() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    const auto* descriptor = find_property(ControlKind::command_bar, "Border");
    expect(descriptor && descriptor->value_codec == ValueCodec::border &&
        descriptor->persistence == PersistenceClass::persisted_editable &&
        descriptor->storage_codec == StorageCodec::control_base &&
        descriptor->default_value.kind == DefaultKind::border &&
        descriptor->default_value.canonical == "WithoutBorder:0",
        "CommandBar Border must have a typed persisted descriptor and proven default");
    const auto validate = [](const BorderValue& border,
                             ControlKind kind = ControlKind::command_bar) {
        Form form; form.id = ObjectId{1}; form.name = "Border";
        form.children = {ControlRef{ObjectId{2}}};
        OrdinaryFormDocument document(std::move(form));
        ControlNode control = kind == ControlKind::usual_group
            ? ControlNode{ObjectId{2}, "Group", UsualGroupPayload{}}
            : ControlNode{ObjectId{2}, "Tools", CommandBarPayload{}};
        control.properties().set_explicit(PropertyId::from_name("Border"), border);
        document.add_control(std::move(control));
        return document.validate();
    };
    BorderValue value;
    expect(validate(value).ok(), "default absolute Border must validate");
    value.border_type = ControlBorderType::double_line;
    for (std::uint32_t width = 0; width <= 5; ++width) {
        value.width = width;
        expect(validate(value).ok(), "absolute Border integer width 0..5 must validate");
    }
    value.width = 6;
    expect(validate(value).has(InvariantCode::invalid_property), "Border width 6 must fail model validation");
    value.width = 2; value.border_type = ControlBorderType::without_border;
    expect(!validate(value).ok(), "WithoutBorder width above 1 must fail");
    value.border_type = ControlBorderType::rounded;
    expect(!validate(value).ok() && !validate(value, ControlKind::usual_group).ok(),
        "Rounded width 2 must fail for CommandBar and UsualGroup");
    value.width = 1;
    expect(!validate(value).ok(), "Rounded width 1 must be rejected for CommandBar");
    expect(validate(value, ControlKind::usual_group).ok(),
        "UsualGroup Rounded width 1 must remain a valid value");
    value.border_type = static_cast<ControlBorderType>(255);
    expect(!validate(value).ok(), "invalid ControlBorderType enum must fail");
    value = BorderValue{}; value.kind = static_cast<BorderKind>(255);
    expect(!validate(value).ok(), "invalid BorderKind enum must fail");
    value = BorderValue{}; value.style = QualifiedName{"StyleBorders.ControlBorder"};
    expect(!validate(value).ok(), "absolute Border must reject a style reference");
    value.kind = BorderKind::style_reference;
    expect(validate(value).ok(), "named style Border must validate");
    value.width = 1;
    expect(!validate(value).ok(), "style Border must reject unused absolute width");
    value.width = 0; value.border_type = ControlBorderType::single;
    expect(!validate(value).ok(), "style Border must reject unused absolute type");
    value.border_type = ControlBorderType::without_border; value.style = QualifiedName{};
    expect(!validate(value).ok(), "style Border must reject empty name");
    value.style = std::monostate{};
    expect(!validate(value).ok(), "style Border must reject missing reference");
    value.style = CompositeIdValue{5, UuidValue{"12345678-1234-1234-1234-123456789abc"}, false};
    expect(validate(value).ok(), "composite style Border must validate");
    value.style = CompositeIdValue{0, UuidValue{"00000000-0000-0000-0000-000000000000"}, false};
    expect(!validate(value).ok(), "style Border must reject null composite reference");
    value.style = CompositeIdValue{5, UuidValue{"invalid"}, false};
    expect(!validate(value).ok(), "style Border must reject malformed UUID");
}

void test_control_border_and_calendar_selection_descriptors() {
    using namespace oof::model;
    using namespace oof::model::metamodel;
    constexpr std::array border_owners{
        ControlKind::calendar_field, ControlKind::check_box, ControlKind::choice_field,
        ControlKind::input_field, ControlKind::label_decoration, ControlKind::list_box,
        ControlKind::picture_decoration, ControlKind::progress_bar, ControlKind::radio_button,
        ControlKind::track_bar, ControlKind::usual_group,
    };
    for (const auto kind : border_owners) {
        const auto* border = find_property(kind, "Border");
        expect(border && border->persistence == PersistenceClass::persisted_editable &&
                border->storage_codec == (kind == ControlKind::usual_group ?
                    StorageCodec::control_info : StorageCodec::control_base) &&
                border->value_codec == ValueCodec::border &&
                border->default_value.kind == DefaultKind::border,
            "each proven Border owner must expose the named typed persisted property");
    }
    struct BorderCase {
        ControlKind kind;
        ControlPayload payload;
        StorageCodec storage;
        std::string_view default_value;
    };
    const std::array newly_classified_borders{
        BorderCase{ControlKind::panel, PanelPayload{}, StorageCodec::control_base, "WithoutBorder:0"},
        BorderCase{ControlKind::splitter, SplitterPayload{}, StorageCodec::control_base, "WithoutBorder:0"},
        BorderCase{ControlKind::table, TablePayload{}, StorageCodec::control_base, "StyleBorders.ControlBorder"},
        BorderCase{ControlKind::html_document_field, HtmlDocumentFieldPayload{}, StorageCodec::control_info,
            "StyleBorders.ControlBorder"},
    };
    for (std::size_t index = 0; index < newly_classified_borders.size(); ++index) {
        const auto& test = newly_classified_borders[index];
        const auto* descriptor = find_property(test.kind, "Border");
        expect(descriptor && descriptor->persistence == PersistenceClass::persisted_editable &&
                   descriptor->storage_codec == test.storage && descriptor->value_codec == ValueCodec::border &&
                   descriptor->default_value.kind == DefaultKind::border &&
                   descriptor->default_value.canonical == test.default_value,
            "new Border descriptors must match their observed codec and default");

        Form form;
        form.id = ObjectId{1};
        const ObjectId control_id{static_cast<std::uint64_t>(index + 2)};
        form.children = {ControlRef{control_id}};
        OrdinaryFormDocument document(std::move(form));
        document.add_control(ControlNode{control_id, "BorderControl", test.payload});
        const auto control = OrdinaryFormDocument::ObjectKey{ObjectCategory::control, control_id};
        const auto property = PropertyId::from_name("Border");
        const auto initial = document.get_prop_val(control, property);
        expect(initial.status == PropertyReadStatus::proven_default && initial.value,
            "new Border property API must expose a typed default");
        BorderValue explicit_border;
        explicit_border.border_type = ControlBorderType::double_line;
        explicit_border.width = 2;
        expect(document.set_prop_val(control, property, explicit_border) == PropertyMutationStatus::applied,
            "new Border property API must accept a supported typed border");
        const auto read = document.get_prop_val(control, property);
        expect(read.status == PropertyReadStatus::explicit_value && read.value &&
                   *read.value == PropertyValue{explicit_border},
            "new Border property API must return the explicit typed value");
        expect(document.set_prop_val(control, property, std::string{"single"}) == PropertyMutationStatus::invalid_value,
            "new Border property API must reject a wrong value type");
        BorderValue rounded_border;
        rounded_border.border_type = ControlBorderType::rounded;
        rounded_border.width = 1;
        expect(document.set_prop_val(control, property, rounded_border) == PropertyMutationStatus::invalid_value,
            "rounded Border values must remain unsupported outside UsualGroup");
        expect(document.reset_prop_val(control, property) == PropertyMutationStatus::reset,
            "new Border property API must reset explicit values");
        const auto reset = document.get_prop_val(control, property);
        expect(reset.status == PropertyReadStatus::proven_default && reset.value && *reset.value == *initial.value,
            "new Border property API reset must restore the typed default");
    }
    const auto* selection_mode = find_property(ControlKind::calendar_field, "SelectionMode");
    expect(selection_mode && selection_mode->persistence == PersistenceClass::persisted_editable &&
            selection_mode->storage_codec == StorageCodec::control_info &&
            selection_mode->value_codec == ValueCodec::enumeration &&
            selection_mode->default_value.kind == DefaultKind::enumeration &&
            selection_mode->default_value.canonical == "DateSelectionMode.Single",
        "CalendarField SelectionMode must expose its persisted Single enum default");
    const auto* selected_dates = find_property(ControlKind::calendar_field, "SelectedDates");
    expect(selected_dates && selected_dates->persistence == PersistenceClass::runtime_only &&
            selected_dates->api_access == ApiAccess::read_only &&
            selected_dates->value_kind == ValueKind::collection &&
            selected_dates->storage_codec == StorageCodec::none &&
            selected_dates->default_value.kind == DefaultKind::none &&
            selected_dates->value_codec == ValueCodec::calendar_selected_dates,
        "CalendarField SelectedDates must expose its live collection codec without a permanent default");
    Form form; form.id = ObjectId{1}; form.name = "CalendarCollectionBoundary";
    for (std::uint64_t i = 0; i < 12; ++i) form.children.emplace_back(ControlRef{ObjectId{100 + i}});
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t i = 0; i < 12; ++i)
        document.add_control(ControlNode{ObjectId{100 + i}, "Calendar" + std::to_string(i), CalendarFieldPayload{}});
    const auto before_xml = oof::source::serialize_form_xml(document);
    const auto before_bin = oof::save_form_bin(document);
    expect(before_xml.ok() && before_bin.ok(), "calendar collection boundary fixture must serialize");
    const auto property = PropertyId::from_name("SelectedDates");
    for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        const auto selected_dates = document.get_prop_val(owner, property);
        expect(selected_dates.status == PropertyReadStatus::computed_value && selected_dates.value &&
                   std::get_if<CalendarSelectedDatesRef>(&*selected_dates.value) &&
                   *std::get_if<CalendarSelectedDatesRef>(&*selected_dates.value) ==
                       CalendarSelectedDatesRef{ObjectId{100 + i}},
            "SelectedDates getter must return the computed owner reference");
        for (const PropertyValue& value : {PropertyValue{UndefinedValue{}}, PropertyValue{ArrayValue{}},
                PropertyValue{DateValue{"2026-11-06T12:34:56"}}})
            expect(document.set_prop_val(owner, property, value) == PropertyMutationStatus::read_only,
                "SelectedDates assignment must reject replacement independently of its value type");
        expect(document.reset_prop_val(owner, property) == PropertyMutationStatus::read_only &&
            !document.find_control(owner.id)->properties().contains(property),
            "SelectedDates reset cannot replace the collection or create a stored shadow");
    }
    const auto after_xml = oof::source::serialize_form_xml(document);
    const auto after_bin = oof::save_form_bin(document);
    expect(after_xml.ok() && after_xml.value() == before_xml.value() &&
        after_bin.ok() && after_bin.value() == before_bin.value(),
        "rejected collection replacement must preserve complete XML and BIN");
}

void test_calendar_selected_dates_live_collection() {
    using namespace oof::model;
    constexpr std::array ids{ObjectId{100}, ObjectId{101}, ObjectId{102}, ObjectId{103}, ObjectId{104},
        ObjectId{105}, ObjectId{106}, ObjectId{107}, ObjectId{108}, ObjectId{109}, ObjectId{110}, ObjectId{111}};
    Form form;
    form.id = ObjectId{1};
    form.name = "CalendarRuntime";
    for (const auto id : ids) form.children.emplace_back(ControlRef{id});
    OrdinaryFormDocument document(std::move(form));
    for (std::size_t i = 0; i < ids.size(); ++i)
        document.add_control(ControlNode{ids[i], "Calendar" + std::to_string(i), CalendarFieldPayload{}});

    const auto mode_property = PropertyId::from_name("SelectionMode");
    const auto current_property = PropertyId::from_name("CurrentDate");
    const auto selected_property = PropertyId::from_name("SelectedDates");
    const auto owner_key = [](ObjectId id) {
        return OrdinaryFormDocument::ObjectKey{ObjectCategory::control, id};
    };
    const auto mode_value = [](std::string member) -> PropertyValue {
        return EnumerationValue{"DateSelectionMode", std::move(member)};
    };
    const DateValue nov4{"2026-11-04T00:00:00"};
    const DateValue nov5{"2026-11-05T00:00:00"};
    const DateValue nov6_late{"2026-11-06T12:34:56"};
    const auto get_dates = [&](ObjectId id) {
        std::vector<DateValue> result;
        const CalendarSelectedDatesRef ref{id};
        const auto count = document.calendar_selected_date_count(ref);
        if (!count) throw std::runtime_error("initialized calendar collection must have a count");
        for (std::size_t index = 0; index < *count; ++index) {
            const auto date = document.calendar_selected_date_at(ref, static_cast<std::int64_t>(index));
            if (!date) throw std::runtime_error("valid calendar collection index must return a date");
            result.push_back(*date);
        }
        return result;
    };
    const auto set_mode = [&](ObjectId id, std::string member) {
        return document.set_prop_val(owner_key(id), mode_property, mode_value(std::move(member)));
    };

    const CalendarSelectedDatesRef uninitialized{ids[0]};
    const auto initial_ref = document.get_prop_val(owner_key(ids[0]), selected_property);
    expect(initial_ref.status == PropertyReadStatus::computed_value && initial_ref.value &&
               std::get_if<CalendarSelectedDatesRef>(&*initial_ref.value) &&
               *std::get_if<CalendarSelectedDatesRef>(&*initial_ref.value) == uninitialized,
        "SelectedDates owner reference must be available before runtime initialization");
    expect(!document.calendar_selected_date_count(uninitialized) &&
               !document.calendar_selected_date_at(uninitialized, 0) &&
               !document.calendar_selected_dates_contains(uninitialized, nov4),
        "collection count, index, and membership must be absent before runtime initialization");
    expect(document.add_calendar_selected_date(uninitialized, nov4) == PropertyMutationStatus::unsupported_surface &&
               document.delete_calendar_selected_date(uninitialized, nov4) == PropertyMutationStatus::unsupported_surface &&
               document.clear_calendar_selected_dates(uninitialized) == PropertyMutationStatus::unsupported_surface,
        "collection mutations must require runtime initialization");
    expect(document.initialize_calendar_runtime(ids[0], DateValue{"2026-11-04T19:20:21"}) ==
               PropertyMutationStatus::applied_runtime_only && get_dates(ids[0]) == std::vector<DateValue>{nov4},
        "calendar initialization must seed one midnight selection from the local day");

    // The native probe exercises all twelve calendar controls and all nine mode transitions.
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto id = ids[index];
        expect(document.initialize_calendar_runtime(id, DateValue{"2026-11-04T07:08:09"}) ==
                   PropertyMutationStatus::applied_runtime_only,
            "each CalendarField must accept runtime initialization");
        const auto ref = CalendarSelectedDatesRef{id};
        expect(document.calendar_selected_date_count(ref) == std::optional<std::size_t>{1} &&
                   document.calendar_selected_date_at(ref, 0) == std::optional<DateValue>{nov4} &&
                   document.calendar_selected_dates_contains(ref, DateValue{"2026-11-04T22:00:00"}) == true &&
                   document.calendar_selected_dates_contains(ref, nov5) == false,
            "initial live collection must contain the runtime local date by day");
        expect(document.add_calendar_selected_date(ref, nov6_late) == PropertyMutationStatus::applied_runtime_only,
            "adding a valid selected date must mutate only runtime collection state");
        const std::string_view current_mode = (index % 3 == 0) ? "Single" : (index % 3 == 1) ? "Multiple" : "Interval";
        expect(set_mode(id, std::string(current_mode)) == PropertyMutationStatus::applied,
            "setting a valid SelectionMode must update persisted mode");
        expect(document.add_calendar_selected_date(ref, nov6_late) == PropertyMutationStatus::applied_runtime_only,
            "adding a duplicate day must be an accepted no-op or newest single selection");
        expect(document.calendar_selected_dates_contains(ref, DateValue{"2026-11-06T00:00:00"}) == true,
            "membership comparison must ignore the time portion");
        if (current_mode == "Single") {
            expect(get_dates(id) == std::vector<DateValue>{nov6_late},
                "Single mode must keep the newest selected timestamp");
        }
    }

    const std::array<std::string_view, 3> modes{"Single", "Multiple", "Interval"};
    for (const auto from : modes) for (const auto to : modes) {
        const auto id = ids[3 + static_cast<std::size_t>(std::ranges::find(modes, from) - modes.begin()) * 3 +
            static_cast<std::size_t>(std::ranges::find(modes, to) - modes.begin())];
        const auto ref = CalendarSelectedDatesRef{id};
        expect(document.initialize_calendar_runtime(id, nov4) == PropertyMutationStatus::applied_runtime_only,
            "mode transition fixture must initialize");
        expect(document.clear_calendar_selected_dates(ref) == PropertyMutationStatus::applied_runtime_only,
            "mode transition fixture must start empty");
        expect(set_mode(id, std::string(from)) == PropertyMutationStatus::applied,
            "mode transition fixture must set source mode");
        expect(document.add_calendar_selected_date(ref, DateValue{"2026-11-06T12:34:56"}) ==
                   PropertyMutationStatus::applied_runtime_only &&
                   document.add_calendar_selected_date(ref, DateValue{"2026-11-04T08:09:10"}) ==
                   PropertyMutationStatus::applied_runtime_only,
            "source mode must accept two test selections");
        if (from == "Multiple") {
            expect(document.add_calendar_selected_date(ref, DateValue{"2026-11-06T20:21:22"}) ==
                       PropertyMutationStatus::applied_runtime_only &&
                       get_dates(id) == std::vector<DateValue>{DateValue{"2026-11-06T12:34:56"},
                           DateValue{"2026-11-04T08:09:10"}},
                "a duplicate day in Multiple mode must retain its original timestamp and insertion position");
        }
        const auto before_change = get_dates(id);
        if (from == "Interval")
            expect(before_change == std::vector<DateValue>{DateValue{"2026-11-04T08:09:10"},
                       DateValue{"2026-11-05T08:09:10"}, DateValue{"2026-11-06T12:34:56"}},
                "prepending an interval must preserve the prior endpoint time in its separate span");
        expect(set_mode(id, std::string(to)) == PropertyMutationStatus::applied,
            "each source and destination mode pair must be accepted");
        const auto actual = get_dates(id);
        if (to == "Single") {
            expect(actual.size() == 1 && actual.front() == before_change.back(),
                "changing mode to Single must retain the final current selection");
        } else if (to == "Multiple") {
            expect(actual == before_change,
                "changing mode to Multiple must preserve selection order and timestamps");
        } else if (from == "Single") {
            expect(actual == std::vector<DateValue>{before_change.back()},
                "changing a Single selection to Interval must retain its one selected date");
        } else if (from == "Interval") {
            expect(actual == before_change, "reassigning Interval must preserve distinct adjacent span times");
        } else {
            expect(actual == std::vector<DateValue>{DateValue{"2026-11-04T08:09:10"},
                       DateValue{"2026-11-05T08:09:10"}, DateValue{"2026-11-06T08:09:10"}},
                "changing mode to Interval must build chronological days with the minimum date time");
        }
    }

    const auto current_owner = ids[9];
    const auto current_ref = CalendarSelectedDatesRef{current_owner};
    for (const auto mode : modes) {
        expect(set_mode(current_owner, std::string(mode)) == PropertyMutationStatus::applied &&
                   document.initialize_calendar_runtime(current_owner, nov4) == PropertyMutationStatus::applied_runtime_only &&
                   document.add_calendar_selected_date(current_ref, DateValue{"2026-11-06T12:34:56"}) == PropertyMutationStatus::applied_runtime_only &&
                   document.set_prop_val(owner_key(current_owner), current_property,
                       DateValue{"2026-11-10T08:09:10"}) == PropertyMutationStatus::applied_runtime_only &&
                   get_dates(current_owner) == std::vector<DateValue>{DateValue{"2026-11-10T08:09:10"}},
            "setting CurrentDate must replace the selection in every mode");
    }
    const auto current = document.get_prop_val(owner_key(current_owner), current_property);
    expect(current.status == PropertyReadStatus::computed_value && current.value &&
               *current.value == PropertyValue{DateValue{"2026-11-10T00:00:00"}} &&
               get_dates(current_owner) == std::vector<DateValue>{DateValue{"2026-11-10T08:09:10"}},
        "CurrentDate getter must be midnight while its selection retains the input timestamp");
    expect(document.set_prop_val(owner_key(current_owner), current_property, std::string{"2026-11-11"}) ==
               PropertyMutationStatus::invalid_value &&
               document.get_prop_val(owner_key(current_owner), current_property).value ==
                   std::optional<PropertyValue>{PropertyValue{DateValue{"2026-11-10T00:00:00"}}} &&
               get_dates(current_owner) == std::vector<DateValue>{DateValue{"2026-11-10T08:09:10"}},
        "invalid CurrentDate assignments must leave both current date and selection unchanged");
    expect(document.set_prop_val(owner_key(current_owner), current_property,
               DateValue{"2026-11-10T18:30:00"}) == PropertyMutationStatus::applied_runtime_only &&
               get_dates(current_owner) == std::vector<DateValue>{DateValue{"2026-11-10T18:30:00"}},
        "setting CurrentDate on its existing day must still replace selection timestamp");
    expect(document.reset_prop_val(owner_key(current_owner), current_property) == PropertyMutationStatus::reset_runtime_only &&
               document.get_prop_val(owner_key(current_owner), current_property).status == PropertyReadStatus::no_default &&
               get_dates(current_owner) == std::vector<DateValue>{DateValue{"2026-11-10T18:30:00"}},
        "resetting CurrentDate must unset the getter and preserve SelectedDates");

    const auto mode_owner = ids[10];
    const auto mode_ref = CalendarSelectedDatesRef{mode_owner};
    expect(document.initialize_calendar_runtime(mode_owner, nov4) == PropertyMutationStatus::applied_runtime_only &&
               set_mode(mode_owner, "Multiple") == PropertyMutationStatus::applied &&
               document.clear_calendar_selected_dates(mode_ref) == PropertyMutationStatus::applied_runtime_only &&
               document.add_calendar_selected_date(mode_ref, DateValue{"2024-02-28T12:00:00"}) == PropertyMutationStatus::applied_runtime_only &&
               document.add_calendar_selected_date(mode_ref, DateValue{"2024-03-02T13:00:00"}) == PropertyMutationStatus::applied_runtime_only &&
               set_mode(mode_owner, "Interval") == PropertyMutationStatus::applied,
        "leap-day interval fixture must accept its endpoints and mode conversion");
    expect(get_dates(mode_owner) == std::vector<DateValue>{DateValue{"2024-02-28T12:00:00"},
               DateValue{"2024-02-29T12:00:00"}, DateValue{"2024-03-01T12:00:00"},
               DateValue{"2024-03-02T12:00:00"}},
        "Interval mode conversion must include leap day and preserve minimum endpoint time");
    expect(document.delete_calendar_selected_date(mode_ref, DateValue{"2024-02-29T20:00:00"}) ==
               PropertyMutationStatus::applied_runtime_only && get_dates(mode_owner) ==
                   std::vector<DateValue>{DateValue{"2024-02-28T12:00:00"}},
        "deleting a day inside Interval must truncate that day and every later day");
    expect(document.initialize_calendar_runtime(mode_owner, nov4) == PropertyMutationStatus::applied_runtime_only &&
               set_mode(mode_owner, "Multiple") == PropertyMutationStatus::applied &&
               document.clear_calendar_selected_dates(mode_ref) == PropertyMutationStatus::applied_runtime_only &&
               document.add_calendar_selected_date(mode_ref, DateValue{"2024-02-28T12:00:00"}) == PropertyMutationStatus::applied_runtime_only &&
               document.add_calendar_selected_date(mode_ref, DateValue{"2024-03-02T13:00:00"}) == PropertyMutationStatus::applied_runtime_only &&
               set_mode(mode_owner, "Interval") == PropertyMutationStatus::applied &&
               set_mode(mode_owner, "Multiple") == PropertyMutationStatus::applied,
        "Multiple mode after Interval must preserve all four contiguous dates");
    expect(get_dates(mode_owner) == std::vector<DateValue>{DateValue{"2024-02-28T12:00:00"},
               DateValue{"2024-02-29T12:00:00"}, DateValue{"2024-03-01T12:00:00"},
               DateValue{"2024-03-02T12:00:00"}},
        "conversion from Interval to Multiple must materialize the full chronological span");
    expect(document.delete_calendar_selected_date(mode_ref, DateValue{"2024-03-01T20:00:00"}) ==
               PropertyMutationStatus::applied_runtime_only && get_dates(mode_owner) ==
                   std::vector<DateValue>{DateValue{"2024-02-28T12:00:00"}, DateValue{"2024-02-29T12:00:00"},
                       DateValue{"2024-03-02T12:00:00"}},
        "deleting an interior date after Interval conversion must split the span and retain remaining order and timestamps");
    expect(document.delete_calendar_selected_date(mode_ref, DateValue{"2024-03-02T20:00:00"}) ==
               PropertyMutationStatus::applied_runtime_only && get_dates(mode_owner) ==
                   std::vector<DateValue>{DateValue{"2024-02-28T12:00:00"}, DateValue{"2024-02-29T12:00:00"}},
        "deleting the end date after Interval conversion must retain all earlier dates");

    const auto bounds_ref = CalendarSelectedDatesRef{ids[11]};
    expect(document.initialize_calendar_runtime(ids[11], DateValue{"0001-01-01T00:00:00"}) ==
               PropertyMutationStatus::applied_runtime_only &&
               set_mode(ids[11], "Interval") == PropertyMutationStatus::applied &&
               document.add_calendar_selected_date(bounds_ref, DateValue{"9999-12-31T23:59:59"}) ==
                   PropertyMutationStatus::applied_runtime_only &&
               document.calendar_selected_date_count(bounds_ref) == std::optional<std::size_t>{3652059} &&
               document.calendar_selected_date_at(bounds_ref, 0) ==
                   std::optional<DateValue>{DateValue{"0001-01-01T00:00:00"}} &&
               document.calendar_selected_date_at(bounds_ref, 1154) ==
                   std::optional<DateValue>{DateValue{"0004-02-29T23:59:59"}} &&
               document.calendar_selected_date_at(bounds_ref, 3652058) ==
                   std::optional<DateValue>{DateValue{"9999-12-31T23:59:59"}},
        "Interval mode must count and index the complete Date1 through Date9999 range, including leap days");

    const auto uninitialized_other = CalendarSelectedDatesRef{ObjectId{9999}};
    expect(!document.calendar_selected_date_count(uninitialized_other) &&
               document.add_calendar_selected_date(uninitialized_other, nov4) == PropertyMutationStatus::unknown_owner &&
               document.initialize_calendar_runtime(ObjectId{9999}, nov4) == PropertyMutationStatus::unknown_owner &&
               document.initialize_calendar_runtime(ObjectId{1}, nov4) == PropertyMutationStatus::unknown_owner,
        "nonexistent and non-calendar owners must be rejected");
    expect(document.initialize_calendar_runtime(ids[0], DateValue{"2026-02-30T12:00:00"}) == PropertyMutationStatus::invalid_value &&
               document.initialize_calendar_runtime(ids[0], DateValue{"not-a-date"}) == PropertyMutationStatus::invalid_value &&
               document.add_calendar_selected_date(uninitialized, DateValue{"2026-02-30T12:00:00"}) == PropertyMutationStatus::invalid_value &&
               document.calendar_selected_dates_contains(current_ref, DateValue{"not-a-date"}) == std::nullopt &&
               document.delete_calendar_selected_date(CalendarSelectedDatesRef{ids[0]}, DateValue{"not-a-date"}) == PropertyMutationStatus::invalid_value,
        "invalid date values must be rejected by initialization and collection mutations");
    expect(!document.calendar_selected_date_at(current_ref, -1) &&
               !document.calendar_selected_date_at(current_ref, 100) &&
               document.add_calendar_selected_date(current_ref, DateValue{"2026-11-11T00:00:00"}) == PropertyMutationStatus::applied_runtime_only,
        "negative and out-of-range indexes must fail while valid mutations remain usable");

    const auto isolated = ids[0];
    expect(set_mode(isolated, "Multiple") == PropertyMutationStatus::applied &&
               document.initialize_calendar_runtime(isolated, nov4) == PropertyMutationStatus::applied_runtime_only &&
               document.add_calendar_selected_date(CalendarSelectedDatesRef{isolated}, nov6_late) == PropertyMutationStatus::applied_runtime_only,
        "isolation fixture must acquire two selected dates");
    OrdinaryFormDocument copied = document;
    expect(copied.calendar_selected_date_count(CalendarSelectedDatesRef{isolated}) == std::optional<std::size_t>{2} &&
               copied.clear_calendar_selected_dates(CalendarSelectedDatesRef{isolated}) == PropertyMutationStatus::applied_runtime_only &&
               document.calendar_selected_date_count(CalendarSelectedDatesRef{isolated}) == std::optional<std::size_t>{2},
        "document copies must carry independent runtime calendar state");
    for (const auto id : ids)
        expect(document.reset_prop_val(owner_key(id), mode_property) == PropertyMutationStatus::reset,
            "restoring default SelectionMode must succeed before persistence comparison");
    const auto xml_before_runtime = oof::source::serialize_form_xml(document);
    const auto bin_before_runtime = oof::save_form_bin(document);
    expect(xml_before_runtime.ok() && bin_before_runtime.ok(),
        "calendar runtime persistence fixture must serialize before runtime-only operations");
    expect(document.set_prop_val(owner_key(isolated), current_property, DateValue{"2026-11-07T09:10:11"}) ==
               PropertyMutationStatus::applied_runtime_only &&
               document.clear_calendar_selected_dates(CalendarSelectedDatesRef{isolated}) == PropertyMutationStatus::applied_runtime_only &&
               document.add_calendar_selected_date(CalendarSelectedDatesRef{isolated}, DateValue{"2026-11-08T10:11:12"}) ==
                   PropertyMutationStatus::applied_runtime_only,
        "runtime-only changes must be accepted after resetting persisted modes");
    const auto xml_after_runtime = oof::source::serialize_form_xml(document);
    const auto bin_after_runtime = oof::save_form_bin(document);
    expect(xml_after_runtime.ok() && xml_after_runtime.value() == xml_before_runtime.value() &&
               bin_after_runtime.ok() && bin_after_runtime.value() == bin_before_runtime.value(),
        "runtime CurrentDate and SelectedDates must not change complete XML or BIN output");
    const auto cold = oof::source::parse_form_xml(xml_after_runtime.value());
    expect(cold.ok() && !cold.value().calendar_selected_date_count(CalendarSelectedDatesRef{isolated}) &&
        cold.value().get_prop_val(owner_key(isolated), current_property).status == PropertyReadStatus::no_default,
        "a rebuilt source document must request a new runtime date instead of inheriting the previous selection");

    Form shadow_form;
    shadow_form.id = ObjectId{200};
    shadow_form.name = "CalendarCollectionShadow";
    shadow_form.children.emplace_back(ControlRef{ObjectId{201}});
    ControlNode shadow_calendar{ObjectId{201}, "ShadowCalendar", CalendarFieldPayload{}};
    shadow_calendar.properties().set_explicit(selected_property, CalendarSelectedDatesRef{ObjectId{201}});
    OrdinaryFormDocument shadow_document(std::move(shadow_form));
    shadow_document.add_control(std::move(shadow_calendar));
    const auto shadow_report = shadow_document.validate();
    expect(shadow_report.has(InvariantCode::invalid_property),
        "a manually stored SelectedDates owner reference must be rejected by model validation");
    for (const auto& bad_mode : {PropertyValue{true}, PropertyValue{EnumerationValue{"OtherMode", "Single"}},
             PropertyValue{EnumerationValue{"DateSelectionMode", "Unknown"}}}) {
        Form invalid_form; invalid_form.id = ObjectId{1}; invalid_form.name = "InvalidCalendarMode";
        invalid_form.children.emplace_back(ControlRef{ObjectId{2}});
        ControlNode invalid_calendar{ObjectId{2}, "Calendar", CalendarFieldPayload{}};
        invalid_calendar.properties().set_explicit(mode_property, bad_mode);
        OrdinaryFormDocument invalid(std::move(invalid_form));
        invalid.add_control(std::move(invalid_calendar));
        expect(invalid.initialize_calendar_runtime(ObjectId{2}, nov4) == PropertyMutationStatus::invalid_value &&
            invalid.set_prop_val(owner_key(ObjectId{2}), current_property, nov4) == PropertyMutationStatus::invalid_value &&
            !invalid.calendar_selected_date_count(CalendarSelectedDatesRef{ObjectId{2}}),
            "an invalid source mode must not silently initialize a default or interval selection");
        expect(invalid.set_prop_val(owner_key(ObjectId{2}), mode_property, mode_value("Single")) == PropertyMutationStatus::applied &&
            invalid.initialize_calendar_runtime(ObjectId{2}, nov4) == PropertyMutationStatus::applied_runtime_only,
            "assigning a valid mode must explicitly repair an invalid source mode");
    }

    Form replacement;
    replacement.id = ObjectId{2};
    replacement.name = "Replacement";
    document.set_form(std::move(replacement));
    const auto retained_owner = document.get_prop_val(owner_key(isolated), selected_property);
    expect(!document.calendar_selected_date_count(CalendarSelectedDatesRef{isolated}) &&
               retained_owner.status == PropertyReadStatus::computed_value && retained_owner.value &&
               *retained_owner.value == PropertyValue{CalendarSelectedDatesRef{isolated}},
        "replacing the form must clear runtime calendar state while retaining collection controls and computed owner references");
}

void test_form_item_collections() {
    using namespace oof::model::metamodel;
    const auto controls_id = PropertyId::from_name("Controls"), pages_id = PropertyId::from_name("Pages");
    const auto current_page_id = PropertyId::from_name("CurrentPage");
    const auto* controls_descriptor = find_form_property("Controls");
    const auto* pages_descriptor = find_property(ControlKind::panel, "Pages");
    for (const auto& [descriptor, codec] : {std::pair{controls_descriptor, ValueCodec::form_controls},
            std::pair{pages_descriptor, ValueCodec::panel_pages}}) {
        expect(descriptor && descriptor->value_kind == ValueKind::collection && descriptor->api_access == ApiAccess::read_only &&
            descriptor->persistence == PersistenceClass::persisted_readonly && descriptor->storage_codec == StorageCodec::collection_record &&
            descriptor->value_codec == codec && descriptor->default_value.kind == DefaultKind::none,
            "form item collections expose persisted readonly descriptors without invented defaults");
    }
    const auto* current_page_descriptor = find_property(ControlKind::panel, "CurrentPage");
    expect(current_page_descriptor && current_page_descriptor->value_kind == ValueKind::object &&
        current_page_descriptor->api_access == ApiAccess::read_write && current_page_descriptor->persistence == PersistenceClass::runtime_only &&
        current_page_descriptor->storage_codec == StorageCodec::none && current_page_descriptor->value_codec == ValueCodec::page_reference &&
        current_page_descriptor->default_value.kind == DefaultKind::none, "CurrentPage has a typed runtime reference descriptor");
    expect(value_codec_name(ValueCodec::form_controls) == "form-controls" && value_codec_name(ValueCodec::panel_pages) == "panel-pages" &&
        value_codec_name(ValueCodec::page_reference) == "page-reference", "form item codecs have distinct public names");

    Form form; form.id = ObjectId{1}; form.name = "Collections";
    std::vector<ChildItemRef> expected_controls;
    for (std::uint64_t i = 0; i < 12; ++i) {
        form.children.push_back(ControlRef{ObjectId{100 + i}});
        expected_controls.push_back(ControlRef{ObjectId{100 + i}});
        expected_controls.push_back(ControlRef{ObjectId{200 + i}});
    }
    OrdinaryFormDocument document(std::move(form));
    for (std::uint64_t remaining = 12; remaining != 0; --remaining) {
        const auto i = remaining - 1;
        const auto count = i % 3 + 1;
        ControlNode panel{ObjectId{100 + i}, "Panel" + std::to_string(i), PanelPayload{}};
        for (std::uint64_t p = 0; p < count; ++p) panel.children.push_back(PageRef{ObjectId{1000 + i * 3 + p}});
        for (std::uint64_t p = count; p != 0; --p) {
            Page page; page.id = ObjectId{1000 + i * 3 + p - 1};
            page.name = "Panel" + std::to_string(i) + "Page" + std::to_string(p - 1);
            if (p == 1) page.children.push_back(ControlRef{ObjectId{200 + i}});
            document.add_page(std::move(page));
        }
        document.add_control(ControlNode{ObjectId{200 + i}, "Panel" + std::to_string(i) + "Label0", LabelDecorationPayload{}});
        document.add_control(std::move(panel));
    }
    expect(document.validate().ok() && document.collections().controls.size() == 24 && document.collections().pages.size() == 24,
        "twelve panels with reversed storage insertion form a valid tree of 24 controls and 24 pages");
    const OrdinaryFormDocument::ObjectKey form_owner{ObjectCategory::form, ObjectId{1}};
    const FormItemCollectionRef controls_ref{ObjectId{1}, FormItemCollectionKind::controls};
    const auto controls = document.get_prop_val(form_owner, controls_id);
    expect(controls.status == PropertyReadStatus::computed_value && controls.value == PropertyValue{controls_ref} &&
        document.get_prop_val(form_owner, controls_id).value == controls.value && document.form_item_refs(controls_ref) == expected_controls,
        "Form.Controls is stable and follows authoritative preorder rather than storage insertion order");
    for (const auto& value : {PropertyValue{controls_ref}, PropertyValue{FormItemCollectionRef{ObjectId{999}, FormItemCollectionKind::controls}},
            PropertyValue{UndefinedValue{}}})
        expect(document.set_prop_val(form_owner, controls_id, value) == PropertyMutationStatus::read_only,
            "Form.Controls rejects same, foreign and Undefined replacements");
    expect(document.reset_prop_val(form_owner, controls_id) == PropertyMutationStatus::read_only, "Form.Controls cannot be reset");
    for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        const FormItemCollectionRef pages_ref{owner.id, FormItemCollectionKind::pages};
        std::vector<ChildItemRef> expected_pages;
        for (std::uint64_t p = 0; p <= i % 3; ++p) expected_pages.push_back(PageRef{ObjectId{1000 + i * 3 + p}});
        const auto pages = document.get_prop_val(owner, pages_id);
        expect(pages.status == PropertyReadStatus::computed_value && pages.value == PropertyValue{pages_ref} &&
            document.get_prop_val(owner, pages_id).value == pages.value && document.form_item_refs(pages_ref) == expected_pages,
            "Panel.Pages is stable and enumerates only direct pages in ChildItems order");
        for (const auto& value : {PropertyValue{pages_ref}, PropertyValue{FormItemCollectionRef{ObjectId{999}, FormItemCollectionKind::pages}},
                PropertyValue{UndefinedValue{}}})
            expect(document.set_prop_val(owner, pages_id, value) == PropertyMutationStatus::read_only,
                "Panel.Pages rejects same, foreign and Undefined replacements");
        expect(document.reset_prop_val(owner, pages_id) == PropertyMutationStatus::read_only, "Panel.Pages cannot be reset");
        const auto first = std::get<PageRef>(expected_pages.front()), last = std::get<PageRef>(expected_pages.back());
        expect(document.get_prop_val(owner, current_page_id).value == PropertyValue{first} &&
            document.set_prop_val(owner, current_page_id, last) == PropertyMutationStatus::applied_runtime_only &&
            document.get_prop_val(owner, current_page_id).value == PropertyValue{last}, "CurrentPage starts first and accepts the owned last page");
        const PageRef foreign{ObjectId{i == 0 ? std::uint64_t{1003} : std::uint64_t{1000}}};
        expect(document.set_prop_val(owner, current_page_id, foreign) == PropertyMutationStatus::applied_runtime_only &&
            document.get_prop_val(owner, current_page_id).value == PropertyValue{last} &&
            document.set_prop_val(owner, current_page_id, UndefinedValue{}) == PropertyMutationStatus::invalid_value,
            "CurrentPage keeps existing foreign-page and Undefined behavior");
        expect(document.form_item_refs(pages_ref) == expected_pages && document.get_prop_val(form_owner, controls_id).value == controls.value,
            "runtime selection does not replace or reorder owned collections");
    }
    expect(!document.form_item_refs({ObjectId{999}, FormItemCollectionKind::controls}) &&
        !document.form_item_refs({ObjectId{999}, FormItemCollectionKind::pages}) &&
        !document.form_item_refs({ObjectId{200}, FormItemCollectionKind::pages}) &&
        !document.form_item_refs({ObjectId{1}, static_cast<FormItemCollectionKind>(99)}), "invalid collection owner and kind must fail");
    expect(document.get_prop_val({ObjectCategory::form, ObjectId{999}}, controls_id).status == PropertyReadStatus::unknown_owner &&
        document.get_prop_val({ObjectCategory::control, ObjectId{999}}, pages_id).status == PropertyReadStatus::unknown_owner,
        "collection getters reject unknown owners");

    Form empty; empty.id = ObjectId{1}; empty.name = "Empty";
    OrdinaryFormDocument empty_document(std::move(empty));
    expect(empty_document.form_item_refs(controls_ref) == std::vector<ChildItemRef>{}, "empty Form.Controls is an empty sequence");
    Form empty_panel_form; empty_panel_form.id = ObjectId{1}; empty_panel_form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument empty_panel(std::move(empty_panel_form));
    empty_panel.add_control(ControlNode{ObjectId{2}, "EmptyPanel", PanelPayload{}});
    const FormItemCollectionRef empty_pages{ObjectId{2}, FormItemCollectionKind::pages};
    expect(empty_panel.get_prop_val({ObjectCategory::control, ObjectId{2}}, pages_id).value == PropertyValue{empty_pages} &&
        !empty_panel.form_item_refs(empty_pages), "empty panel retains its collection handle without inventing native empty-page semantics");

    for (const auto fault : {0, 1, 2, 3, 4, 5, 6}) {
        Form invalid_form; invalid_form.id = ObjectId{1}; invalid_form.children = {ControlRef{ObjectId{2}}};
        if (fault == 1) invalid_form.children.push_back(ControlRef{ObjectId{2}});
        if (fault == 4) invalid_form.children.push_back(PageRef{ObjectId{999}});
        OrdinaryFormDocument invalid(std::move(invalid_form));
        ControlNode panel{ObjectId{2}, "Panel", PanelPayload{}};
        panel.children = {PageRef{ObjectId{2}}};
        Page page; page.id = ObjectId{2}; page.name = "Page";
        if (fault == 0) page.children.push_back(ControlRef{ObjectId{999}});
        if (fault == 2) page.children.push_back(ControlRef{ObjectId{2}});
        if (fault == 5) page.children.push_back(PageRef{ObjectId{2}});
        if (fault == 6) {
            page.children.push_back(ControlRef{ObjectId{3}});
            ControlNode leaf{ObjectId{3}, "Leaf", LabelDecorationPayload{}};
            leaf.children.push_back(ControlRef{ObjectId{2}});
            invalid.add_control(std::move(leaf));
        }
        invalid.add_control(std::move(panel)); invalid.add_page(std::move(page));
        if (fault == 3) invalid.add_control(ControlNode{ObjectId{4}, "Orphan", LabelDecorationPayload{}});
        expect(!invalid.form_item_refs(controls_ref) && !invalid.form_item_refs(empty_pages),
            "dangling, repeated, cyclic, orphan and illegal child trees fail collection enumeration");
    }
    Form shared_ids; shared_ids.id = ObjectId{1}; shared_ids.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument category_scoped(std::move(shared_ids));
    ControlNode scoped_panel{ObjectId{2}, "Panel", PanelPayload{}}; scoped_panel.children = {PageRef{ObjectId{2}}};
    Page scoped_page; scoped_page.id = ObjectId{2}; scoped_page.name = "Page";
    category_scoped.add_control(std::move(scoped_panel)); category_scoped.add_page(std::move(scoped_page));
    expect(category_scoped.form_item_refs(controls_ref) == std::vector<ChildItemRef>{ControlRef{ObjectId{2}}} &&
        category_scoped.form_item_refs(empty_pages) == std::vector<ChildItemRef>{PageRef{ObjectId{2}}},
        "control and page numeric IDs remain distinct during traversal");
    for (const auto property : {controls_id, pages_id}) {
        auto invalid = category_scoped;
        if (property == controls_id) {
            auto invalid_form = invalid.form();
            invalid_form.properties.set_explicit(property, controls_ref);
            invalid.set_form(std::move(invalid_form));
        } else {
            Form invalid_form; invalid_form.id = ObjectId{1}; invalid_form.children = {ControlRef{ObjectId{2}}};
            invalid = OrdinaryFormDocument(std::move(invalid_form));
            ControlNode panel{ObjectId{2}, "Panel", PanelPayload{}}; panel.children = {PageRef{ObjectId{2}}};
            panel.properties().set_explicit(property, empty_pages);
            Page page; page.id = ObjectId{2}; page.name = "Page";
            invalid.add_control(std::move(panel)); invalid.add_page(std::move(page));
        }
        expect(!invalid.validate().ok(), "owned form item collections cannot be duplicated in scalar property bags");
    }
}

void test_panel_initial_current_page() {
    Form form;
    form.id = ObjectId{1};
    form.name = "Panels";
    OrdinaryFormDocument document(std::move(form));
    const auto current_page = PropertyId::from_name("CurrentPage");
    for (std::uint64_t index = 0; index < 12; ++index) {
        const ObjectId panel_id{100 + index};
        const ObjectId first_id{200 + index * 2};
        const ObjectId second_id{201 + index * 2};
        const ObjectId hidden_id{300 + index};
        ControlNode panel{panel_id, "Panel" + std::to_string(index), PanelPayload{}};
        panel.children = {PageRef{first_id}, PageRef{second_id}, PageRef{hidden_id}};
        Page first;
        first.id = first_id;
        if (index % 2 == 0) first.visible.set(false);
        Page second;
        second.id = second_id;
        second.enabled.set(false);
        Page hidden;
        hidden.id = hidden_id;
        hidden.visible.set(false);
        document.add_page(std::move(first));
        document.add_page(std::move(second));
        document.add_page(std::move(hidden));
        document.add_control(std::move(panel));
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, panel_id};
        const auto initial = document.get_prop_val(owner, current_page);
        expect(initial.status == PropertyReadStatus::computed_value && initial.value == PropertyValue{PageRef{first_id}},
            "initial current page must be the first page regardless of visibility");
        expect(document.set_prop_val(owner, current_page, PageRef{second_id}) == PropertyMutationStatus::applied_runtime_only &&
                document.get_prop_val(owner, current_page).value == PropertyValue{PageRef{second_id}},
            "a visible but disabled page must be selectable at runtime");
        expect(document.set_prop_val(owner, current_page, PageRef{hidden_id}) == PropertyMutationStatus::applied_runtime_only &&
                document.get_prop_val(owner, current_page).value == PropertyValue{PageRef{second_id}},
            "selecting a hidden page must leave the selection unchanged");
        expect(document.set_prop_val(owner, current_page, UndefinedValue{}) == PropertyMutationStatus::invalid_value &&
                document.set_prop_val(owner, current_page, PageRef{ObjectId{9999}}) == PropertyMutationStatus::invalid_reference &&
                document.reset_prop_val(owner, current_page) == PropertyMutationStatus::unsupported_surface,
            "invalid assignments and unimplemented reset must not change the selection");
        if (index != 0) {
            expect(document.set_prop_val(owner, current_page, PageRef{ObjectId{201}}) == PropertyMutationStatus::applied_runtime_only &&
                    document.get_prop_val(owner, current_page).value == PropertyValue{PageRef{second_id}},
                "a page belonging to another panel must not change the selection");
        }
        expect(document.set_prop_val(owner, PropertyId::from_name("ScrollPageMode"), true) == PropertyMutationStatus::applied &&
                document.set_prop_val(owner, current_page, PageRef{first_id}) == PropertyMutationStatus::applied_runtime_only &&
                document.get_prop_val(owner, current_page).value == PropertyValue{PageRef{second_id}},
            "scrolling mode must preserve the current page on assignment");
    }
    document.add_control(ControlNode{ObjectId{400}, "Empty", PanelPayload{}});
    expect(document.get_prop_val({ObjectCategory::control, ObjectId{400}}, current_page).status == PropertyReadStatus::unsupported_surface,
        "an incomplete panel must not invent a current page");
}

void test_current_page_is_runtime_only() {
    Form form;
    form.id = ObjectId{1};
    form.name = "PanelState";
    form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument document(std::move(form));
    ControlNode panel{ObjectId{2}, "Tabs", PanelPayload{}};
    for (std::uint64_t id = 3; id <= 4; ++id) {
        Page page;
        page.id = ObjectId{id};
        page.name = "Page" + std::to_string(id);
        Position position;
        position.left.set(6);
        position.top.set(7);
        position.width.set(200);
        position.height.set(100);
        for (const auto edge : {BindingCoordinate::right, BindingCoordinate::bottom}) {
            AnchorBinding binding;
            binding.coordinate = edge;
            binding.target_coordinate = edge;
            binding.target = ControlRef{ObjectId{2}};
            binding.offset.set(-6);
            position.bindings.anchors.push_back(std::move(binding));
        }
        page.position.set(std::move(position));
        panel.children.push_back(PageRef{page.id});
        document.add_page(std::move(page));
    }
    document.add_control(std::move(panel));
    const auto before_xml = oof::source::serialize_form_xml(document);
    const auto before_bin = oof::save_form_bin(document);
    expect(before_xml.ok() && before_bin.ok(), "synthetic panel must serialize before changing current page");
    expect(document.set_prop_val({ObjectCategory::control, ObjectId{2}}, PropertyId::from_name("CurrentPage"),
               PageRef{ObjectId{4}}) == PropertyMutationStatus::applied_runtime_only,
        "runtime current page assignment must succeed");
    const auto after_xml = oof::source::serialize_form_xml(document);
    const auto after_bin = oof::save_form_bin(document);
    expect(after_xml.ok() && after_xml.value() == before_xml.value(),
        "runtime page selection must not alter persisted XML");
    expect(after_bin.ok() && after_bin.value() == before_bin.value(),
        "runtime page selection must not alter Form.bin");
}


void test_chart_document_resource_budget() {
    Form form; form.id = ObjectId{1}; form.name = "Budget";
    form.children = {ControlRef{ObjectId{2}}, ControlRef{ObjectId{3}}};
    OrdinaryFormDocument document(std::move(form));
    ChartPayload first; first.points.resize(500);
    for (std::size_t i = 0; i < first.points.size(); ++i) first.points[i].id = ObjectId{i + 1};
    ChartTrendline trend; trend.id = ObjectId{1};
    first.summary_series.trendlines.push_back(std::move(trend));
    first.summary_series.next_trendline_id = 2;
    document.add_control(ControlNode{ObjectId{2}, "First", std::move(first)});
    document.add_control(ControlNode{ObjectId{3}, "Second", ChartPayload{}});
    const OrdinaryFormDocument::ObjectKey second{ObjectCategory::control, ObjectId{3}};
    expect(document.set_prop_val(second, PropertyId::from_name("SeriesCount"), std::int64_t{999500}) == PropertyMutationStatus::resource_limit,
        "Trendlines contribute to the document budget before resize allocation");
    expect(std::get<ChartPayload>(document.find_control(ObjectId{3})->payload).series.empty(),
        "budget rejection leaves resized Chart unchanged");

    Form bad; bad.id = ObjectId{1}; bad.name = "Oversized"; bad.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument oversized(std::move(bad));
    ChartPayload payload; payload.series.resize(1000); payload.points.resize(1000);
    oversized.add_control(ControlNode{ObjectId{2}, "Chart", std::move(payload)});
    bool budget_violation = false;
    for (const auto& violation : oversized.validate().violations)
        if (violation.message == "Chart collections exceed the document resource budget") budget_violation = true;
    expect(budget_violation, "direct oversized payload must report the document budget violation");
    expect(!oversized.add_chart_trendline(ObjectId{2}, ObjectId{1}), "Add must reject an already oversized document");
    const auto& summary = std::get<ChartPayload>(oversized.find_control(ObjectId{2})->payload).summary_series;
    expect(summary.trendlines.empty() && summary.next_trendline_id == 1,
        "rejected Add preserves the collection and its next identity");

    Form degree_form; degree_form.id = ObjectId{1}; degree_form.name = "Degree";
    degree_form.children = {ControlRef{ObjectId{2}}};
    OrdinaryFormDocument degree_document(std::move(degree_form));
    degree_document.add_control(ControlNode{ObjectId{2}, "Chart", ChartPayload{}});
    const auto degree_trend = degree_document.add_chart_trendline(ObjectId{2}, ObjectId{1});
    expect(degree_trend.has_value(), "create Trendline for degree validation");
    const OrdinaryFormDocument::ObjectKey degree_owner{ObjectCategory::control, ObjectId{2}, ChartAreaKind::none,
        ChartScaleKind::none, ObjectId{1}, degree_trend->trendline};
    const auto degree = PropertyId::from_name("ApproximationDegree");
    expect(degree_document.set_prop_val(degree_owner, degree, std::int64_t{7}) == PropertyMutationStatus::applied,
        "set initial approximation degree");
    for (const auto invalid : {std::int64_t{-2147483649LL}, std::int64_t{2147483649LL}}) {
        expect(degree_document.set_prop_val(degree_owner, degree, invalid) == PropertyMutationStatus::invalid_value,
            "degree outside signed 32-bit range must fail");
        expect(degree_document.get_prop_val(degree_owner, degree).value == PropertyValue{std::int64_t{7}},
            "rejected degree leaves previous value unchanged");
    }
    for (const auto& input : {DecimalValue{"-1"}, DecimalValue{"0"}, DecimalValue{"-0.5"}}) {
        expect(degree_document.set_prop_val(degree_owner, degree, input) == PropertyMutationStatus::applied &&
            degree_document.get_prop_val(degree_owner, degree).value == PropertyValue{std::int64_t{1}},
            "small nonpositive degrees normalize to one");
    }
}


void test_self_contained_field_type_model() {
    TypeDomainEntry identity;
    identity.term = TypeDomainTerm::reference;
    identity.type_uuid = UuidValue{"11111111-1111-1111-1111-111111111111"};
    identity.type_name = QualifiedName{"CatalogRef.First"};
    auto renamed = identity;
    renamed.type_name = QualifiedName{"CatalogRef.Second"};
    expect(identity == renamed, "descriptive type names must not redefine GUID type identity");
    renamed.type_uuid = UuidValue{"22222222-2222-2222-2222-222222222222"};
    expect(identity != renamed, "different type GUIDs must remain different types despite descriptions");
    TypeDomainPatternValue root_type{{TypeDomainEntry{.term = TypeDomainTerm::object,
        .type_uuid = UuidValue{"33333333-3333-3333-3333-333333333333"}}}};
    TypeDomainPatternValue field_type{{TypeDomainEntry{.term = TypeDomainTerm::string,
        .string = LengthQualifiers{40, true}}}};
    Form form;
    form.id = ObjectId{1};
    form.name = "FieldModel";
    form.children.push_back(ControlRef{ObjectId{2}});
    OrdinaryFormDocument document(std::move(form));
    document.add_attribute(Attribute{ObjectId{0}, "Root", root_type});
    ControlNode field{ObjectId{2}, "Field", InputFieldPayload{}};
    field.data_path = DataPath{AttributeRef{ObjectId{0}}, {{"Caption", UuidValue{"44444444-4444-4444-4444-444444444444"}, field_type}}};
    document.add_control(std::move(field));
    expect(document.validate().ok(), "a typed field reference must be valid without an external metadata index");
    expect(document.resolve_data_path_type(*document.find_control(ObjectId{2})->data_path) == field_type,
        "a field's explicit type must be available independently of its root object type");
    const auto value_type = document.get_prop_val({ObjectCategory::control, ObjectId{2}}, PropertyId::from_name("ValueType"));
    expect(value_type.value == PropertyValue{field_type}, "ValueType must return the field type through the shared property API");
    const auto data = document.get_prop_val({ObjectCategory::control, ObjectId{2}}, PropertyId::from_name("Data"));
    expect(data.value == PropertyValue{std::string{"Root.Caption"}}, "Data must preserve the explicit root and field names");
    const auto missing_root = document.resolve_data_path_type(DataPath{AttributeRef{}, {{"Caption",
        UuidValue{"44444444-4444-4444-4444-444444444444"}, field_type}}});
    expect(!missing_root, "an absent root must not alias declared Attribute zero even when a field type is supplied");
    const auto unknown_type = document.resolve_data_path_type(DataPath{AttributeRef{ObjectId{0}}, {{"Caption",
        UuidValue{"44444444-4444-4444-4444-444444444444"}}}});
    expect(!unknown_type, "a UUID-only field must not inherit its root's object type");
    const_cast<Attribute*>(document.find_attribute(ObjectId{0}))->type = field_type;
    expect(!document.validate().ok() &&
        !document.resolve_data_path_type(*document.find_control(ObjectId{2})->data_path),
        "an explicit leaf type must not disguise a scalar root as a metadata object");
}

}  // namespace

int main() {
    try {
        test_self_contained_field_type_model();
        test_descriptors();
        test_chart_refresh_runtime_property();
        test_input_list_choice_mode_api();
        test_input_incomplete_choice_mode_api();
        test_panel_scrolling_tab_read();
        test_panel_initial_current_page();
        test_form_item_collections();
        test_current_page_is_runtime_only();
        test_unavailable_ordinary_geometry_properties();
        test_data_api_preserves_binding_until_supported();
        test_binding_property_getters();
        test_input_property_availability();
        test_input_type_restriction_runtime();
        test_control_name_property();
        test_form_runtime_state_properties();
        test_chart_computed_counts();
        test_chart_active_point_reference();
        test_form_style_property();
        test_chart_source_data_mode();
        test_chart_owned_collections();
        test_chart_document_resource_budget();
        test_chart_scale_lines();
        test_list_and_html_live_property_boundaries();
        test_list_box_current_row_runtime_model();
        test_control_extension_value_runtime_boundary();
        test_table_current_parent_runtime_boundary();
        test_geographical_showed_area_runtime_boundary();
        test_graphical_current_item_runtime_boundary();
        test_pivot_runtime_source_boundaries();
        test_form_runtime_variant_values();
        run_runtime_collection_tests();
        test_window_options_aliases();
        test_form_runtime_boolean_values();
        test_window_permission_mutations();
        test_window_permission_platform_matrix();
        test_window_permission_initial_values();
        test_opening_boundary_for_window_properties();
        test_label_picture_api_access();
        test_tab_order_reorders_siblings();
        test_z_order_reorders_siblings();
        test_command_bar_property_descriptors();
        test_command_bar_action_source_model();
        test_standard_menu_command_registry_and_runtime_contracts();
        test_default_button_model();
        test_context_menu_reference_model();
        test_standard_picture_descriptor_catalog();
        test_button_alignment_xml_defaults();
        test_progress_bar_storage_descriptors();
        test_track_bar_storage_descriptors();
        test_chart_integer_and_delimiter_descriptors();
        test_chart_base_value_property();
        test_chart_type_property();
        test_chart_enum_properties();
        test_table_enum_properties();
        test_output_enum_properties();
        test_chart_appearance_defaults();
        test_help_metamodel();
        test_shared_control_default_classifications();
        test_variant_coverage();
        test_property_default_semantics();
        test_typed_property_set();
        test_recursive_typed_values();
        test_choice_list_live_references();
        test_table_columns_reference();
        test_table_column_optional_control();
        test_value_list_named_values();
        test_typed_value_document_budget();
        test_document_property_api();
        test_saved_form_settings_model();
        test_catalog_item_form_extension_property_api();
        test_document_form_extension_property_api();
        test_catalog_list_form_extension_model();
        test_control_position_property_api();
        test_input_field_type_link_model();
        test_control_collapse_runtime_property();
        test_direct_collapse_validation();
        test_default_control_focus_groups();
        test_id_lookup();
        test_duplicate_rejection();
        test_category_scoped_object_ids();
        test_cycle_rejection();
        test_page_object_graph();
        test_root_page_and_remaining_page_policies();
        test_dangling_and_child_policy_rejection();
        test_property_reference_rejection();
        test_picture_property_model_api();
        test_gantt_interval_date_validation();
        test_property_applicability_and_type_validation();
        test_event_sequence_invariants();
        test_duplicate_bindings_rejected();
        test_page_position_invariants();
        test_chart_number_lexical_validation();
        test_chart_series_owner_persistence();
        test_chart_summary_series_color_invariants();
        test_border_value_invariants();
        test_control_border_and_calendar_selection_descriptors();
        test_calendar_selected_dates_live_collection();
    } catch (const std::exception& error) {
        std::cerr << "model tests: FAIL: " << error.what() << '\n';
        return 1;
    }

    std::cout << "model tests: PASS\n";
    return 0;
}
