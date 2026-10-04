#include <array>
#include <exception>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "oof/model/metamodel.hpp"
#include "oof/model/ordinary_form.hpp"
#include "oof/source/form_xml.hpp"

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
    const auto* begin_period = find_property(ControlKind::calendar_field, "BeginOfDisplayPeriod");
    expect(begin_period != nullptr && begin_period->value_codec == ValueCodec::date &&
               begin_period->persistence == PersistenceClass::persisted_editable &&
               begin_period->storage_codec == StorageCodec::control_info &&
               begin_period->default_value.kind == DefaultKind::undefined &&
               begin_period->default_value.canonical == "undefined",
        "CalendarField BeginOfDisplayPeriod must be a classified Date property with Undefined default");
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
    const auto* group_caption = find_property(ControlKind::usual_group, "Caption");
    const auto* group_tool_tip = find_property(ControlKind::usual_group, "ToolTip");
    expect(group_enabled && group_enabled->storage_codec == StorageCodec::control_base &&
               group_enabled->default_value.kind == DefaultKind::boolean && group_enabled->default_value.canonical == "true" &&
               group_caption && group_caption->storage_codec == StorageCodec::control_info &&
               group_caption->default_value.kind == DefaultKind::string && group_caption->default_value.canonical.empty() &&
               group_tool_tip && group_tool_tip->storage_codec == StorageCodec::control_base &&
               group_tool_tip->default_value.kind == DefaultKind::string && group_tool_tip->default_value.canonical.empty(),
        "UsualGroup named property descriptors must expose observed storage and defaults");

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
    expect(label_enabled && label_enabled->persistence == PersistenceClass::persisted_editable &&
               label_enabled->storage_codec == StorageCodec::control_info &&
               label_enabled->value_codec == ValueCodec::boolean &&
               label_enabled->default_value.kind == DefaultKind::boolean &&
               label_enabled->default_value.canonical == "true",
        "LabelDecoration Enabled must declare its editable Boolean true default");
    expect(label_tool_tip && label_tool_tip->persistence == PersistenceClass::persisted_editable &&
               label_tool_tip->storage_codec == StorageCodec::control_info &&
               label_tool_tip->value_codec == ValueCodec::string &&
               label_tool_tip->default_value.kind == DefaultKind::string &&
               label_tool_tip->default_value.canonical.empty(),
        "LabelDecoration ToolTip must declare its editable empty string default");
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
               radio_caption->value_codec == ValueCodec::string &&
               radio_caption->default_value.kind == DefaultKind::string &&
               radio_caption->default_value.canonical.empty(),
        "RadioButton Caption must declare its editable empty string default");
    expect(radio_tool_tip && radio_tool_tip->persistence == PersistenceClass::persisted_editable &&
               radio_tool_tip->storage_codec == StorageCodec::control_base &&
               radio_tool_tip->value_codec == ValueCodec::string &&
               radio_tool_tip->default_value.kind == DefaultKind::string &&
               radio_tool_tip->default_value.canonical.empty(),
        "RadioButton ToolTip must declare its editable empty string default");
    const auto* picture_enabled = find_property(ControlKind::picture_decoration, "Enabled");
    const auto* picture_tool_tip = find_property(ControlKind::picture_decoration, "ToolTip");
    const auto* picture_value = find_property(ControlKind::picture_decoration, "Picture");
    expect(picture_enabled && picture_enabled->persistence == PersistenceClass::persisted_editable &&
               picture_enabled->storage_codec == StorageCodec::control_base &&
               picture_enabled->value_codec == ValueCodec::boolean &&
               picture_enabled->default_value.kind == DefaultKind::boolean &&
               picture_enabled->default_value.canonical == "true",
        "PictureDecoration Enabled must declare its editable Boolean true default");
    expect(picture_tool_tip && picture_tool_tip->persistence == PersistenceClass::persisted_editable &&
               picture_tool_tip->storage_codec == StorageCodec::control_base &&
               picture_tool_tip->value_codec == ValueCodec::string &&
               picture_tool_tip->default_value.kind == DefaultKind::string &&
               picture_tool_tip->default_value.canonical.empty(),
        "PictureDecoration ToolTip must declare its editable empty string default");
    expect(picture_value && picture_value->persistence == PersistenceClass::persisted_editable &&
               picture_value->storage_codec == StorageCodec::picture_record &&
               picture_value->value_codec == ValueCodec::picture &&
               picture_value->default_value.kind == DefaultKind::none,
        "PictureDecoration Picture must declare its typed editable picture record");
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
               enabled->storage_codec == StorageCodec::control_info &&
               enabled->default_value.kind == DefaultKind::boolean &&
               enabled->default_value.canonical == "true",
        "ProgressBar Enabled must have its observed control-info codec and true default");
    expect(tool_tip && tool_tip->persistence == PersistenceClass::persisted_editable &&
               tool_tip->storage_codec == StorageCodec::control_info &&
               tool_tip->default_value.kind == DefaultKind::string &&
               tool_tip->default_value.canonical.empty(),
        "ProgressBar ToolTip must have its observed control-info codec and empty default");
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
    expect(enabled && enabled->persistence == PersistenceClass::persisted_editable &&
               enabled->storage_codec == StorageCodec::control_info &&
               enabled->default_value.kind == DefaultKind::boolean &&
               enabled->default_value.canonical == "true",
        "TrackBar Enabled must use its observed control-info codec and true default");
    expect(tool_tip && tool_tip->persistence == PersistenceClass::persisted_editable &&
               tool_tip->storage_codec == StorageCodec::control_info &&
               tool_tip->default_value.kind == DefaultKind::string &&
               tool_tip->default_value.canonical.empty(),
        "TrackBar ToolTip must use its observed control-info codec and empty default");
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
}

void test_help_metamodel() {
    using namespace oof::model::metamodel;

    const MetamodelCoverage& coverage = metamodel_coverage();
    expect(coverage.control_count == 26, "help catalog must cover 26 controls");
    expect(
        coverage.control_property_occurrences == 419,
        "executable metamodel must include three named Gantt chart properties");
    expect(
        coverage.unique_control_property_names == 203,
        "type-specific metamodel must include three named Gantt chart properties");
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
    expect(find_property(ControlKind::input_field, "TextEdit")->persistence == PersistenceClass::unclassified &&
            find_property(ControlKind::input_field, "TextEdit")->storage_codec == StorageCodec::unclassified,
        "TextEdit must remain unsupported without verified strict persistence");
    for (const auto name : {"ChoiceIncomplete", "MarkIncomplete"}) {
        const auto* descriptor = find_property(ControlKind::input_field, name);
        expect(descriptor && descriptor->persistence == PersistenceClass::runtime_only &&
                descriptor->storage_codec == StorageCodec::none,
            "getter-only InputField Boolean must be classified as runtime-only");
    }
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
    expect(find_property(ControlKind::calendar_field, "CurrentDate")->persistence == PersistenceClass::unclassified &&
               find_property(ControlKind::calendar_field, "CurrentDate")->storage_codec == StorageCodec::unclassified,
        "CalendarField CurrentDate must remain outside this persisted property slice");
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
        test_standard_picture_descriptor_catalog();
        test_button_alignment_xml_defaults();
        test_progress_bar_storage_descriptors();
        test_track_bar_storage_descriptors();
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
        test_gantt_interval_date_validation();
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
