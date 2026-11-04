#include "chart_type.hpp"
#include "table_properties.hpp"
#include "oof/model/ordinary_form.hpp"

#include <array>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "oof/model/metamodel.hpp"
#include "oof/storage/value_codec.hpp"

namespace oof::model {

std::int64_t ValueListValue::effective_last_assigned_id() const {
    constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    std::int64_t maximum = -1;
    for (const auto& item : items) {
        if (item.id > limit) throw std::invalid_argument("ValueList item ID exceeds the signed 64-bit range");
        maximum = std::max(maximum, static_cast<std::int64_t>(item.id));
    }
    if (!last_assigned_id) return maximum;
    if (*last_assigned_id > limit || static_cast<std::int64_t>(*last_assigned_id) < maximum)
        throw std::invalid_argument("ValueList lastAssignedId must cover all item IDs in the signed 64-bit range");
    return static_cast<std::int64_t>(*last_assigned_id);
}

bool operator==(const ValueListValue& left, const ValueListValue& right) {
    try {
        if (left.effective_last_assigned_id() != right.effective_last_assigned_id()) return false;
    } catch (const std::invalid_argument&) { return false; }
    return left.items == right.items && left.value_type == right.value_type && left.available_values == right.available_values;
}

bool valid_border_value(const BorderValue& value) noexcept {
    if (static_cast<unsigned>(value.border_type) > static_cast<unsigned>(ControlBorderType::rounded) ||
        value.width > 5) return false;
    switch (value.kind) {
        case BorderKind::absolute:
            return std::holds_alternative<std::monostate>(value.style) &&
                (value.border_type != ControlBorderType::without_border || value.width <= 1) &&
                (value.border_type != ControlBorderType::rounded || value.width == 1);
        case BorderKind::style_reference:
            if (value.border_type != ControlBorderType::without_border || value.width != 0) return false;
            if (const auto* name = std::get_if<QualifiedName>(&value.style)) return !name->value.empty();
            if (const auto* composite = std::get_if<CompositeIdValue>(&value.style)) {
                const auto& uuid = composite->uuid.canonical;
                if (composite->is_null || uuid.size() != 36 ||
                    (composite->object_id == 0 && uuid == "00000000-0000-0000-0000-000000000000")) return false;
                for (std::size_t index = 0; index < uuid.size(); ++index) {
                    if (index == 8 || index == 13 || index == 18 || index == 23) {
                        if (uuid[index] != '-') return false;
                    } else if (!((uuid[index] >= '0' && uuid[index] <= '9') ||
                                 (uuid[index] >= 'a' && uuid[index] <= 'f') ||
                                 (uuid[index] >= 'A' && uuid[index] <= 'F'))) return false;
                }
                return true;
            }
            return false;
    }
    return false;
}

bool valid_chart_gauge_quality_band_range(const DecimalValue& begin, const DecimalValue& end) noexcept {
    try {
        const std::string left = storage::value_codec::canonical_decimal(begin.canonical);
        const std::string right = storage::value_codec::canonical_decimal(end.canonical);
        const auto magnitude_compare = [](std::string_view a, std::string_view b) {
            const auto a_dot = a.find('.');
            const auto b_dot = b.find('.');
            const auto a_integer = a.substr(0, a_dot);
            const auto b_integer = b.substr(0, b_dot);
            if (a_integer.size() != b_integer.size()) return a_integer.size() < b_integer.size() ? -1 : 1;
            if (a_integer != b_integer) return a_integer < b_integer ? -1 : 1;
            const auto a_fraction = a_dot == std::string_view::npos ? std::string_view{} : a.substr(a_dot + 1);
            const auto b_fraction = b_dot == std::string_view::npos ? std::string_view{} : b.substr(b_dot + 1);
            const auto count = std::max(a_fraction.size(), b_fraction.size());
            for (std::size_t i = 0; i < count; ++i) {
                const char ac = i < a_fraction.size() ? a_fraction[i] : '0';
                const char bc = i < b_fraction.size() ? b_fraction[i] : '0';
                if (ac != bc) return ac < bc ? -1 : 1;
            }
            return 0;
        };
        const bool left_negative = left.starts_with('-');
        const bool right_negative = right.starts_with('-');
        if (left_negative != right_negative) return left_negative;
        const std::string_view left_abs = left_negative ? std::string_view(left).substr(1) : std::string_view(left);
        const std::string_view right_abs = right_negative ? std::string_view(right).substr(1) : std::string_view(right);
        return magnitude_compare(left_abs, right_abs) * (left_negative ? -1 : 1) <= 0;
    } catch (const std::exception&) {
        return false;
    }
}

namespace {

bool valid_xs_language(std::string_view language) noexcept {
    if (language.empty()) return false;
    std::size_t segment_length = 0;
    bool first_segment = true;
    for (const unsigned char character : language) {
        if (character == '-') {
            if (segment_length == 0 || segment_length > 8) return false;
            segment_length = 0;
            first_segment = false;
        } else {
            const bool alpha = (character >= 'A' && character <= 'Z') ||
                (character >= 'a' && character <= 'z');
            const bool digit = character >= '0' && character <= '9';
            if ((!alpha && !digit) || (first_segment && !alpha) || segment_length == 8) return false;
            ++segment_length;
        }
    }
    return segment_length != 0;
}

bool valid_localized_string(const LocalizedStringValue& value) noexcept {
    if (value.items.size() > max_localized_text_items) return false;
    try {
        std::set<std::string_view> languages;
        for (const auto& item : value.items) {
            if (!valid_xs_language(item.language) || !languages.insert(item.language).second) return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

void add_violation(
    ValidationReport& report,
    InvariantCode code,
    ObjectId source,
    ObjectId target,
    std::string message
) {
    report.violations.push_back({code, source, target, std::move(message)});
}

std::optional<std::int32_t> normalize_positive_int32(std::string_view text) noexcept {
    try {
        const auto canonical = storage::value_codec::canonical_decimal(text);
        const auto dot = canonical.find('.');
        const std::string_view integral(canonical.data(), dot == std::string::npos ? canonical.size() : dot);
        std::int64_t result = 0;
        const auto parsed = std::from_chars(integral.data(), integral.data() + integral.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != integral.data() + integral.size() ||
            result < 1 || result > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
        return static_cast<std::int32_t>(result);
    } catch (const std::exception&) { return std::nullopt; }
}

bool is_positive_int32_property(const metamodel::PropertyDescriptor& descriptor) noexcept {
    return (descriptor.control_kind == ControlKind::gantt_chart && descriptor.api_name == "PeriodicVariantRepetition") ||
        (descriptor.control_kind == ControlKind::geographical_schema_field && descriptor.api_name == "Scale");
}

bool type_domain_numeric_precision_normalized(const TypeDomainPatternValue& type) {
    return std::ranges::none_of(type.entries, [](const auto& entry) {
        return entry.term == TypeDomainTerm::numeric && entry.numeric.length == 0 && entry.numeric.precision != 0;
    });
}

bool property_value_matches(
    const metamodel::PropertyDescriptor& descriptor,
    const PropertyValue& value
) noexcept {
    if (descriptor.value_codec == metamodel::ValueCodec::typed_value) {
        TypedValueBudget budget;
        return validate_typed_value(value, budget);
    }
    return std::visit(
        [&descriptor](const auto& typed_value) {
            const auto expected = descriptor.value_codec;
            using Value = std::remove_cvref_t<decltype(typed_value)>;
            if constexpr (std::is_same_v<Value, UndefinedValue>) {
                return expected == metamodel::ValueCodec::date ||
                       expected == metamodel::ValueCodec::action_source_reference ||
                       expected == metamodel::ValueCodec::context_menu_reference ||
                       (descriptor.control_kind == ControlKind::radio_button &&
                        descriptor.api_name == "SelectionValue" && expected == metamodel::ValueCodec::decimal);
            } else if constexpr (std::is_same_v<Value, bool>) {
                return expected == metamodel::ValueCodec::boolean;
            } else if constexpr (std::is_same_v<Value, std::int64_t>) {
                if (is_positive_int32_property(descriptor))
                    return typed_value >= 1 && typed_value <= std::numeric_limits<std::int32_t>::max();
                return expected == metamodel::ValueCodec::integer ||
                       (expected == metamodel::ValueCodec::integer32 &&
                        typed_value >= std::numeric_limits<std::int32_t>::min() &&
                        typed_value <= std::numeric_limits<std::int32_t>::max()) ||
                       expected == metamodel::ValueCodec::decimal;
            } else if constexpr (std::is_same_v<Value, DecimalValue>) {
                if (is_positive_int32_property(descriptor)) {
                    const auto normalized = normalize_positive_int32(typed_value.canonical);
                    if (!normalized) return false;
                    try { return storage::value_codec::canonical_decimal(typed_value.canonical) == std::to_string(*normalized); }
                    catch (const std::exception&) { return false; }
                }
                return expected == metamodel::ValueCodec::decimal;
            } else if constexpr (std::is_same_v<Value, std::string>) {
                return expected == metamodel::ValueCodec::string ||
                       expected == metamodel::ValueCodec::localized_text;
            } else if constexpr (std::is_same_v<Value, LocalizedStringValue>) {
                return expected == metamodel::ValueCodec::localized_string ||
                       (expected == metamodel::ValueCodec::localized_text &&
                        valid_localized_string(typed_value));
            } else if constexpr (std::is_same_v<Value, FormattedStringValue>) {
                if (expected != metamodel::ValueCodec::formatted_string) return false;
                try {
                    (void)storage::value_codec::encode_formatted_string(typed_value);
                    return true;
                } catch (...) { return false; }
            } else if constexpr (std::is_same_v<Value, DateValue>) {
                return expected == metamodel::ValueCodec::date;
            } else if constexpr (std::is_same_v<Value, UuidValue>) {
                return expected == metamodel::ValueCodec::uuid;
            } else if constexpr (std::is_same_v<Value, CompositeIdValue>) {
                return expected == metamodel::ValueCodec::composite_id;
            } else if constexpr (std::is_same_v<Value, TypeDomainPatternValue>) {
                return expected == metamodel::ValueCodec::type_domain && type_domain_numeric_precision_normalized(typed_value);
            } else if constexpr (std::is_same_v<Value, EnumerationValue>) {
                if (expected != metamodel::ValueCodec::enumeration) return false;
                if (descriptor.surface == metamodel::PropertySurface::panel_placement && descriptor.api_name == "Collapse")
                    return typed_value.type_name == "ControlCollapseMode" &&
                        (typed_value.member == "None" || typed_value.member == "Left" || typed_value.member == "Right" ||
                         typed_value.member == "Top" || typed_value.member == "Bottom");
                if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "EnterKeyBehavior")
                    return typed_value.type_name == "EnterKeyBehaviorType" &&
                        (typed_value.member == "ControlNavigation" || typed_value.member == "DefaultButton");
                if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "SizeChange")
                    return typed_value.type_name == "WindowSizeChange" &&
                        (typed_value.member == "Change" || typed_value.member == "DontChange");
                if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "ChangeWindowAppearanceMode")
                    return typed_value.type_name == "WindowAppearanceModeChange" &&
                        (typed_value.member == "Auto" || typed_value.member == "Enable" || typed_value.member == "Disable");
                if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "WindowLocation")
                    return typed_value.type_name == "WindowLocationVariant" &&
                        (typed_value.member == "Auto" || typed_value.member == "Center" || typed_value.member == "DontOverlapOwner");
                if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "WindowState")
                    return typed_value.type_name == "WindowStateVariant" &&
                        (typed_value.member == "Normal" || typed_value.member == "Docked" || typed_value.member == "Autohide" || typed_value.member == "Float");
                if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "WindowAppearanceMode")
                    return typed_value.type_name == "WindowAppearanceModeVariant" &&
                        (typed_value.member == "Normal" || typed_value.member == "Minimized" || typed_value.member == "Maximized");
                if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "WindowDockLocation")
                    return typed_value.type_name == "WindowDockVariant" &&
                        (typed_value.member == "Left" || typed_value.member == "Right" || typed_value.member == "Top" || typed_value.member == "Bottom");
                if (descriptor.control_kind == ControlKind::geographical_schema_field && descriptor.api_name == "ScaleKeeping")
                    return typed_value.type_name == "GeographicalSchemaShowMode" &&
                        (typed_value.member == "AllData" || typed_value.member == "ScaleDefined" || typed_value.member == "SpecifiedArea");
                if (descriptor.control_kind == ControlKind::gantt_chart && descriptor.api_name == "PeriodicVariantUnit") {
                    constexpr std::array<std::string_view, 8> members{"Second", "Minute", "Hour", "Day", "Week", "Month", "Quarter", "Year"};
                    return typed_value.type_name == "TimeScaleUnitType" &&
                        std::ranges::find(members, typed_value.member) != members.end();
                }
                if (descriptor.control_kind == ControlKind::pivot_chart && descriptor.api_name == "PivotChartType")
                    return typed_value.type_name == "PivotChartType" &&
                        (typed_value.member == "Column" || typed_value.member == "Column3D");
                if (descriptor.control_kind == ControlKind::chart && descriptor.api_name == "ChartType") {
                    return typed_value.type_name == "ChartType" &&
                        std::ranges::any_of(detail::chart_type_members, [&](const auto& member) { return member.first == typed_value.member; });
                }
                if (descriptor.control_kind == ControlKind::chart) {
                    const auto spec = std::ranges::find(detail::chart_enum_specs, descriptor.api_name, &detail::ChartEnumSpec::property);
                    if (spec != detail::chart_enum_specs.end()) {
                        return typed_value.type_name == spec->type_name &&
                            std::ranges::find(spec->members, typed_value.member) != spec->members.end();
                    }
                }
                if (descriptor.control_kind == ControlKind::table) {
                    const auto spec = std::ranges::find(detail::table_enum_specs, descriptor.api_name, &detail::TableEnumSpec::property);
                    if (spec != detail::table_enum_specs.end()) {
                        return typed_value.type_name == spec->type_name &&
                            std::ranges::find(spec->members, typed_value.member) != spec->members.end();
                    }
                }
                if (descriptor.control_kind == ControlKind::spreadsheet_document_field && descriptor.api_name == "ShowSelection") {
                    return typed_value.type_name == "SpreadsheetDocumentSelectionShowModeType" &&
                        (typed_value.member == "WhenActive" || typed_value.member == "Always");
                }
                if (descriptor.control_kind == ControlKind::input_field && descriptor.api_name == "IncompleteChoiceMode") {
                    return typed_value.type_name == "IncompleteChoiceMode" &&
                        (typed_value.member == "OnEnterPressed" || typed_value.member == "OnActivate");
                }
                if (descriptor.control_kind == ControlKind::panel && descriptor.api_name == "ShowTabs") {
                    constexpr std::array<std::string_view, 13> members{"DontUse", "Top", "Bottom", "LeftVertical",
                        "RightVertical", "LeftHorizontal", "RightHorizontal", "TopScrolling", "TopMultiLine",
                        "TopMultilineTransposition", "BottomScrolling", "BottomMultiLine", "BottomMultilineTransposition"};
                    return typed_value.type_name == "ShowTabs" &&
                        std::ranges::find(members, typed_value.member) != members.end();
                }
                if (descriptor.control_kind == ControlKind::command_bar &&
                    descriptor.api_name == "ButtonsAlignment") {
                    return typed_value.type_name == "CommandBarButtonAlignment" &&
                        (typed_value.member == "Left" || typed_value.member == "Center" ||
                         typed_value.member == "Right");
                }
                if ((descriptor.control_kind == ControlKind::command_bar ||
                     descriptor.control_kind == ControlKind::progress_bar ||
                     descriptor.control_kind == ControlKind::track_bar) && descriptor.api_name == "Orientation") {
                    return typed_value.type_name == "Orientation" &&
                        (typed_value.member == "Auto" || typed_value.member == "Horizontal" ||
                         typed_value.member == "Vertical");
                }
                if (descriptor.control_kind == ControlKind::progress_bar && descriptor.api_name == "ShowStyle") {
                    return typed_value.type_name == "ProgressBarSmoothingMode" &&
                        (typed_value.member == "Smooth" || typed_value.member == "Broken" || typed_value.member == "BrokenTilt");
                }
                if (descriptor.control_kind == ControlKind::track_bar && descriptor.api_name == "MarkingAppearance") {
                    return typed_value.type_name == "TrackBarMarkingAppearance" &&
                        (typed_value.member == "DontShow" || typed_value.member == "TopLeft" ||
                         typed_value.member == "BottomRight" || typed_value.member == "BothSides");
                }
                if (descriptor.control_kind == ControlKind::check_box || descriptor.control_kind == ControlKind::radio_button) {
                    if (descriptor.api_name == "HorizontalAlign")
                        return typed_value.type_name == "HorizontalAlign" &&
                            (typed_value.member == "Left" || typed_value.member == "Center" || typed_value.member == "Right");
                    if (descriptor.api_name == "VerticalAlign")
                        return typed_value.type_name == "VerticalAlign" &&
                            (typed_value.member == "Top" || typed_value.member == "Center" || typed_value.member == "Bottom");
                    if (descriptor.api_name == "TitleLocation")
                        return typed_value.type_name == "TitleLocation" &&
                            (typed_value.member == "TitleLeft" || typed_value.member == "TitleRight");
                }
                if (descriptor.control_kind == ControlKind::label_decoration && descriptor.api_name == "ScrollingText")
                    return typed_value.type_name == "ScrollingTextMode" &&
                        (typed_value.member == "DontUse" || typed_value.member == "Slow" ||
                         typed_value.member == "Normal" || typed_value.member == "Fast" || typed_value.member == "VeryFast");
                if (descriptor.control_kind == ControlKind::label_decoration && descriptor.api_name == "VerticalAlign")
                    return typed_value.type_name == "VerticalAlign" &&
                        (typed_value.member == "Top" || typed_value.member == "Center" || typed_value.member == "Bottom");
                if (descriptor.control_kind == ControlKind::panel && descriptor.api_name == "PanelPictureLocation")
                    return typed_value.type_name == "PanelPictureLocation" &&
                        (typed_value.member == "Left" || typed_value.member == "Top" || typed_value.member == "Right" ||
                         typed_value.member == "Bottom" || typed_value.member == "Center");
                if (descriptor.control_kind == ControlKind::label_decoration && descriptor.api_name == "LabelPictureLocation")
                    return typed_value.type_name == "LabelPictureLocation" &&
                        (typed_value.member == "Left" || typed_value.member == "Top" || typed_value.member == "Right" ||
                         typed_value.member == "Bottom" || typed_value.member == "Center");
                if ((descriptor.control_kind == ControlKind::picture_decoration || descriptor.control_kind == ControlKind::label_decoration || descriptor.control_kind == ControlKind::panel) && descriptor.api_name == "PictureSize")
                    return typed_value.type_name == "PictureSize" &&
                        (typed_value.member == "RealSize" || typed_value.member == "Stretch" ||
                         typed_value.member == "Proportionally" || typed_value.member == "Tile" ||
                         typed_value.member == "AutoSize" || typed_value.member == "ByFontSize");
                if (descriptor.control_kind == ControlKind::calendar_field && descriptor.api_name == "SelectionMode") {
                    return typed_value.type_name == "DateSelectionMode" &&
                        (typed_value.member == "Single" || typed_value.member == "Multiple" || typed_value.member == "Interval");
                }
                return true;
            } else if constexpr (std::is_same_v<Value, LineValue>) {
                if (expected != metamodel::ValueCodec::line) return false;
                try { (void)storage::value_codec::encode_line(typed_value); return true; }
                catch (const std::exception&) { return false; }
            } else if constexpr (std::is_same_v<Value, BorderValue>) {
                if (expected != metamodel::ValueCodec::border || !valid_border_value(typed_value)) return false;
                return typed_value.kind != BorderKind::absolute || typed_value.border_type != ControlBorderType::rounded ||
                    descriptor.owner != metamodel::DescriptorOwner::control || descriptor.control_kind == ControlKind::usual_group;
            } else if constexpr (std::is_same_v<Value, ColorValue>) {
                return expected == metamodel::ValueCodec::color;
            } else if constexpr (std::is_same_v<Value, FontValue>) {
                return expected == metamodel::ValueCodec::font;
            } else if constexpr (std::is_same_v<Value, ShortcutValue>) {
                return expected == metamodel::ValueCodec::shortcut &&
                       metamodel::find_shortcut_key(typed_value.key) != nullptr;
            } else if constexpr (std::is_same_v<Value, PictureRef>) {
                return expected == metamodel::ValueCodec::picture;
            } else if constexpr (std::is_same_v<Value, ControlRef>) {
                return expected == metamodel::ValueCodec::control_reference ||
                       expected == metamodel::ValueCodec::action_source_reference ||
                       expected == metamodel::ValueCodec::context_menu_reference;
            } else if constexpr (std::is_same_v<Value, PageRef>) {
                return expected == metamodel::ValueCodec::page_reference;
            } else if constexpr (std::is_same_v<Value, CommandBarButtonRef>) {
                return expected == metamodel::ValueCodec::context_menu_reference;
            } else if constexpr (std::is_same_v<Value, ChartCollectionRef>) {
                return (expected == metamodel::ValueCodec::chart_series_collection && typed_value.kind == ChartCollectionKind::series) ||
                    (expected == metamodel::ValueCodec::chart_points_collection && typed_value.kind == ChartCollectionKind::points) ||
                    (expected == metamodel::ValueCodec::chart_gauge_quality_bands && typed_value.kind == ChartCollectionKind::gauge_quality_bands);
            } else if constexpr (std::is_same_v<Value, FormItemCollectionRef>) {
                return (expected == metamodel::ValueCodec::form_controls && typed_value.kind == FormItemCollectionKind::controls) ||
                    (expected == metamodel::ValueCodec::panel_pages && typed_value.kind == FormItemCollectionKind::pages);
            } else if constexpr (std::is_same_v<Value, ValueListRef>) {
                return expected == metamodel::ValueCodec::value_list_reference && typed_value.get();
            } else if constexpr (std::is_same_v<Value, TableColumnsRef>) {
                return expected == metamodel::ValueCodec::table_columns;
            } else if constexpr (std::is_same_v<Value, ChartSeriesRef>) {
                return expected == metamodel::ValueCodec::chart_series_reference;
            } else if constexpr (std::is_same_v<Value, FormRef>) {
                return expected == metamodel::ValueCodec::action_source_reference || expected == metamodel::ValueCodec::form_reference;
            } else if constexpr (std::is_same_v<Value, AttributeRef>) {
                return expected == metamodel::ValueCodec::attribute_reference;
            } else if constexpr (std::is_same_v<Value, CommandRef>) {
                return expected == metamodel::ValueCodec::command_reference;
            }
            return false;
        },
        value);
}

bool property_api_enumeration_matches(
    const metamodel::PropertyDescriptor& descriptor,
    const EnumerationValue& value
) noexcept {
    if (descriptor.value_codec != metamodel::ValueCodec::enumeration) return false;
    if (descriptor.surface == metamodel::PropertySurface::panel_placement && descriptor.api_name == "Collapse")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "EnterKeyBehavior")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "SizeChange")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "ChangeWindowAppearanceMode")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "WindowLocation")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "WindowState")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "WindowAppearanceMode")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.owner == metamodel::DescriptorOwner::form && descriptor.api_name == "WindowDockLocation")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.control_kind == ControlKind::geographical_schema_field && descriptor.api_name == "ScaleKeeping")
        return property_value_matches(descriptor, PropertyValue{value});
    if (descriptor.control_kind == ControlKind::gantt_chart && descriptor.api_name == "PeriodicVariantUnit")
        return property_value_matches(descriptor, PropertyValue{value});
    if ((descriptor.control_kind == ControlKind::chart &&
            (descriptor.api_name == "ChartType" || std::ranges::find(detail::chart_enum_specs, descriptor.api_name,
                &detail::ChartEnumSpec::property) != detail::chart_enum_specs.end())) ||
        (descriptor.control_kind == ControlKind::pivot_chart && descriptor.api_name == "PivotChartType") ||
        (descriptor.control_kind == ControlKind::table &&
            std::ranges::find(detail::table_enum_specs, descriptor.api_name, &detail::TableEnumSpec::property) != detail::table_enum_specs.end()) ||
        (descriptor.control_kind == ControlKind::spreadsheet_document_field && descriptor.api_name == "ShowSelection") ||
        (descriptor.control_kind == ControlKind::panel && descriptor.api_name == "ShowTabs") ||
        (descriptor.control_kind == ControlKind::input_field && descriptor.api_name == "IncompleteChoiceMode") ||
        (descriptor.control_kind == ControlKind::command_bar &&
         (descriptor.api_name == "ButtonsAlignment" || descriptor.api_name == "Orientation")) ||
        ((descriptor.control_kind == ControlKind::check_box || descriptor.control_kind == ControlKind::radio_button) &&
         (descriptor.api_name == "HorizontalAlign" || descriptor.api_name == "VerticalAlign" || descriptor.api_name == "TitleLocation")) ||
        (descriptor.control_kind == ControlKind::label_decoration && descriptor.api_name == "ScrollingText") ||
        (descriptor.control_kind == ControlKind::panel && descriptor.api_name == "PanelPictureLocation") ||
        (descriptor.control_kind == ControlKind::label_decoration && descriptor.api_name == "LabelPictureLocation") ||
        ((descriptor.control_kind == ControlKind::picture_decoration || descriptor.control_kind == ControlKind::label_decoration || descriptor.control_kind == ControlKind::panel) && descriptor.api_name == "PictureSize") ||
        (descriptor.control_kind == ControlKind::calendar_field && descriptor.api_name == "SelectionMode") ||
        (descriptor.control_kind == ControlKind::progress_bar &&
         (descriptor.api_name == "Orientation" || descriptor.api_name == "ShowStyle")) ||
        (descriptor.control_kind == ControlKind::track_bar &&
         (descriptor.api_name == "Orientation" || descriptor.api_name == "MarkingAppearance"))) {
        return property_value_matches(descriptor, PropertyValue{value});
    }
    if (descriptor.api_name == "HorizontalAlign") {
        return value.type_name == "HorizontalAlign" &&
            (value.member == "Left" || value.member == "Center" || value.member == "Right" ||
             value.member == "Justify" || value.member == "Auto");
    }
    if (descriptor.api_name == "VerticalAlign") {
        return value.type_name == "VerticalAlign" &&
            (value.member == "Top" || value.member == "Center" || value.member == "Bottom");
    }
    if (descriptor.control_kind == ControlKind::button) {
        if (descriptor.api_name == "PictureLocation")
            return value.type_name == "PictureLocation" && (value.member == "Left" || value.member == "Right");
        if (descriptor.api_name == "PictureSize")
            return value.type_name == "PictureSize" &&
                (value.member == "RealSize" || value.member == "Stretch" || value.member == "Proportionally" ||
                 value.member == "Tile" || value.member == "AutoSize" || value.member == "ByFontSize");
        if (descriptor.api_name == "MenuMode")
            return value.type_name == "MenuMode" &&
                (value.member == "DontUse" || value.member == "Use" || value.member == "UseExtra");
    }
    if ((descriptor.control_kind == ControlKind::html_document_field ||
         descriptor.control_kind == ControlKind::geographical_schema_field ||
         descriptor.control_kind == ControlKind::graphical_schema_field) && descriptor.api_name == "Output")
        return value.type_name == "Output" &&
            (value.member == "Auto" || value.member == "Enable" || value.member == "Disable");
    if (descriptor.control_kind == ControlKind::splitter && descriptor.api_name == "Orientation")
        return value.type_name == "Orientation" &&
            (value.member == "Auto" || value.member == "Horizontal" || value.member == "Vertical");
    if (descriptor.control_kind == ControlKind::dendrogram &&
        descriptor.api_name == "Orientation") {
        return value.type_name == "DendrogramOrientation" &&
            (value.member == "Up" || value.member == "Down");
    }
    return false;
}

bool context_menu_button_resolves(const OrdinaryFormDocument& document, const CommandBarButtonRef& reference) noexcept {
    const auto* owner = document.find_control(reference.owner.id());
    if (owner == nullptr || reference.path.empty()) return false;
    const std::vector<CommandBarButton>* items = nullptr;
    if (const auto* bar = std::get_if<CommandBarPayload>(&owner->payload)) items = &bar->buttons;
    if (items == nullptr) return false;
    for (const auto& name : reference.path) {
        if (name.empty()) return false;
        const CommandBarButton* match = nullptr;
        for (const auto& item : *items) {
            if (item.name != name) continue;
            if (match != nullptr) return false;
            match = &item;
        }
        if (match == nullptr || match->type != CommandBarButtonKind::submenu) return false;
        items = &match->buttons;
    }
    return true;
}

bool property_value_references_resolve(
    const OrdinaryFormDocument& document,
    const PropertyValue& value) noexcept {
    return std::visit(
        [&document](const auto& typed_value) {
            using Value = std::remove_cvref_t<decltype(typed_value)>;
            if constexpr (std::is_same_v<Value, ControlRef>) {
                return document.find_control(typed_value.id()) != nullptr;
            } else if constexpr (std::is_same_v<Value, PageRef>) {
                return document.find_page(typed_value.id()) != nullptr;
            } else if constexpr (std::is_same_v<Value, CommandBarButtonRef>) {
                return context_menu_button_resolves(document, typed_value);
            } else if constexpr (std::is_same_v<Value, FormRef>) {
                return typed_value.id() == document.form().id;
            } else if constexpr (std::is_same_v<Value, AttributeRef>) {
                return document.find_attribute(typed_value.id()) != nullptr;
            } else if constexpr (std::is_same_v<Value, CommandRef>) {
                return document.find_command(typed_value.id()) != nullptr;
            } else if constexpr (std::is_same_v<Value, PictureRef>) {
                if (typed_value.standard_name) {
                    return !typed_value.asset.id() &&
                        metamodel::find_standard_picture(typed_value.standard_name->value) != nullptr;
                }
                return static_cast<bool>(typed_value.asset.id()) &&
                    document.find_asset(typed_value.asset.id()) != nullptr;
            }
            return true;
        },
        value);
}

bool is_typed_collection_property(const metamodel::PropertyDescriptor& descriptor) noexcept {
    using metamodel::ValueCodec;
    return descriptor.value_kind == metamodel::ValueKind::collection ||
        descriptor.value_codec == ValueCodec::command_bar_buttons ||
        descriptor.value_codec == ValueCodec::owned_panel ||
        descriptor.value_codec == ValueCodec::form_controls ||
        descriptor.value_codec == ValueCodec::panel_pages ||
        descriptor.value_codec == ValueCodec::chart_plot_area ||
        descriptor.value_codec == ValueCodec::chart_title_area ||
        descriptor.value_codec == ValueCodec::chart_legend_area ||
        descriptor.value_codec == ValueCodec::chart_gauge_quality_bands ||
        (descriptor.control_kind == ControlKind::chart && descriptor.api_name == "SummarySeries") ||
        descriptor.value_codec == ValueCodec::dendrogram_items ||
        descriptor.value_codec == ValueCodec::dendrogram_links;
}

std::optional<PropertyValue> materialize_default(
    const metamodel::PropertyDescriptor& descriptor) {
    using metamodel::DefaultKind;
    const auto& value = descriptor.default_value;
    switch (value.kind) {
        case DefaultKind::undefined:
            if (value.canonical == "undefined") return PropertyValue{UndefinedValue{}};
            return std::nullopt;
        case DefaultKind::boolean:
            if (value.canonical == "true") return PropertyValue{true};
            if (value.canonical == "false") return PropertyValue{false};
            return std::nullopt;
        case DefaultKind::integer: {
            std::int64_t parsed = 0;
            const auto [end, error] = std::from_chars(
                value.canonical.data(), value.canonical.data() + value.canonical.size(), parsed);
            if (error == std::errc{} && end == value.canonical.data() + value.canonical.size()) {
                return PropertyValue{parsed};
            }
            return std::nullopt;
        }
        case DefaultKind::decimal:
            if (!value.canonical.empty()) return PropertyValue{DecimalValue{std::string(value.canonical)}};
            return std::nullopt;
        case DefaultKind::string:
            if (descriptor.value_codec == metamodel::ValueCodec::string ||
                descriptor.value_codec == metamodel::ValueCodec::localized_text) {
                return PropertyValue{std::string(value.canonical)};
            }
            return std::nullopt;
        case DefaultKind::color:
            if (value.canonical == "automatic") return PropertyValue{ColorValue{}};
            if (value.canonical == "RGB(83,106,194)") {
                ColorValue color;
                color.kind = ColorKind::absolute;
                color.red = 83; color.green = 106; color.blue = 194;
                return PropertyValue{color};
            }
            if (value.canonical == "RGB(169,169,169)") {
                ColorValue color;
                color.kind = ColorKind::absolute;
                color.red = 169; color.green = 169; color.blue = 169;
                return PropertyValue{color};
            }
            if (value.canonical == "StyleColors.ButtonTextColor" ||
                value.canonical == "StyleColors.ButtonBackColor" ||
                value.canonical == "StyleColors.FormBackColor" ||
                value.canonical == "StyleColors.FormTextColor" ||
                value.canonical == "StyleColors.ButtonBorderColor" ||
                value.canonical == "StyleColors.BorderColor" ||
                value.canonical == "StyleColors.SpecialTextColor" ||
                value.canonical == "StyleColors.FieldSelectionBackColor" ||
                value.canonical == "StyleColors.FieldSelectedTextColor" ||
                value.canonical == "StyleColors.FieldAlternativeBackColor") {
                ColorValue color;
                color.kind = ColorKind::style_reference;
                color.style = QualifiedName{std::string(value.canonical)};
                return PropertyValue{color};
            }
            return std::nullopt;
        case DefaultKind::font:
            if (value.canonical == "automatic") return PropertyValue{FontValue{}};
            if (value.canonical == "StyleFonts.TextFont") {
                FontValue font;
                font.kind = FontKind::style_reference;
                font.style = QualifiedName{"StyleFonts.TextFont"};
                return PropertyValue{font};
            }
            if (value.canonical == "automatic,bold=true") {
                FontValue font;
                font.bold = true;
                return PropertyValue{font};
            }
            return std::nullopt;
        case DefaultKind::enumeration: {
            const auto separator = value.canonical.find('.');
            if (value.canonical.empty() || separator == 0 ||
                (separator != std::string_view::npos && separator + 1 == value.canonical.size())) return std::nullopt;
            const auto type_name = separator == std::string_view::npos
                ? descriptor.api_name : value.canonical.substr(0, separator);
            const auto member = separator == std::string_view::npos
                ? value.canonical : value.canonical.substr(separator + 1);
            EnumerationValue parsed{std::string(type_name), std::string(member)};
            if (!property_api_enumeration_matches(descriptor, parsed)) return std::nullopt;
            return PropertyValue{std::move(parsed)};
        }
        case DefaultKind::border:
            if (value.canonical == "WithoutBorder:0") return PropertyValue{BorderValue{}};
            if (value.canonical == "Single:1") {
                BorderValue border;
                border.border_type = ControlBorderType::single;
                border.width = 1;
                return PropertyValue{border};
            }
            if (value.canonical == "Rounded:1") {
                BorderValue border;
                border.border_type = ControlBorderType::rounded;
                border.width = 1;
                return PropertyValue{border};
            }
            if (value.canonical == "StyleBorders.ControlBorder") {
                BorderValue border;
                border.kind = BorderKind::style_reference;
                border.style = QualifiedName{std::string(value.canonical)};
                return PropertyValue{border};
            }
            return std::nullopt;
        case DefaultKind::shortcut:
            if (value.canonical == "None") return PropertyValue{ShortcutValue{}};
            return std::nullopt;
        case DefaultKind::unknown:
        case DefaultKind::none:
            return std::nullopt;
    }
    return std::nullopt;
}

bool table_column_editor_property_allowed(ControlKind kind, std::string_view name) {
    switch (kind) {
        case ControlKind::input_field:
            return name == "Enabled" || name == "ReadOnly";
        case ControlKind::choice_field:
            return name == "Enabled" || name == "ToolTip";
        case ControlKind::check_box:
            return name == "Enabled" || name == "Caption" || name == "ToolTip" || name == "Font";
        default:
            return false;
    }
}

bool table_column_editor_property_value_supported(
    ControlKind kind, std::string_view name, const PropertyValue& value) {
    if (name == "Enabled") {
        return std::holds_alternative<bool>(value) &&
            (kind == ControlKind::input_field || std::get<bool>(value));
    }
    if (name == "ReadOnly") return kind == ControlKind::input_field && std::holds_alternative<bool>(value);
    if (name == "Caption" || name == "ToolTip") {
        return std::holds_alternative<std::string>(value) && std::get<std::string>(value).empty();
    }
    if (name == "Font") {
        return std::holds_alternative<FontValue>(value) && std::get<FontValue>(value) == FontValue{};
    }
    return false;
}

bool canonical_chart_decimal(std::string_view value) {
    if (value.empty()) return false;
    if (value.front() == '-') value.remove_prefix(1);
    if (value.empty()) return false;
    const auto point = value.find('.');
    if (point != std::string_view::npos && value.find('.', point + 1) != std::string_view::npos) return false;
    const auto integer = point == std::string_view::npos ? value : value.substr(0, point);
    const auto fraction = point == std::string_view::npos ? std::string_view{} : value.substr(point + 1);
    const auto digits = [](std::string_view part) {
        return std::ranges::all_of(part, [](unsigned char c) { return c >= '0' && c <= '9'; });
    };
    if ((!integer.empty() && !digits(integer)) || (!fraction.empty() && !digits(fraction)) ||
        (integer.empty() && fraction.empty())) return false;
    if (integer.size() > 1 && integer.front() == '0') return false;
    if (!fraction.empty() && fraction.back() == '0') return false;
    if (fraction.empty() && point != std::string_view::npos) return false;
    return !(value == "-0");
}

std::optional<std::vector<ObjectId>> placement_control_group(const OrdinaryFormDocument& document, ObjectId target) {
    struct Owner {
        const std::vector<ChildItemRef>* children = nullptr;
        const ControlNode* control = nullptr;
        const Page* page = nullptr;
    };
    struct Ownership { Owner owner; std::size_t count = 0; };
    std::unordered_map<std::uint64_t, Ownership> control_owners;
    std::unordered_map<std::uint64_t, Ownership> page_owners;
    const auto index_owner = [&](Owner owner) {
        for (const auto& item : *owner.children) {
            const auto* control = std::get_if<ControlRef>(&item);
            const auto id = control ? control->id() : std::get<PageRef>(item).id();
            auto& entry = (control ? control_owners : page_owners)[id.value()];
            entry.owner = owner;
            ++entry.count;
        }
    };
    index_owner(Owner{&document.form().children});
    for (const auto& control : document.collections().controls) index_owner(Owner{&control.children, &control});
    for (const auto& page : document.collections().pages) index_owner(Owner{&page.children, nullptr, &page});
    const auto find_owner = [&](const ChildItemRef& reference) -> std::optional<Owner> {
        const auto* control = std::get_if<ControlRef>(&reference);
        const auto id = control ? control->id() : std::get<PageRef>(reference).id();
        const auto& owners = control ? control_owners : page_owners;
        const auto found = owners.find(id.value());
        if (found == owners.end() || found->second.count != 1) return std::nullopt;
        return found->second.owner;
    };
    const auto owner = find_owner(ControlRef{target});
    if (!owner) return std::nullopt;
    const ControlNode* panel = owner->control;
    if (owner->page) {
        const auto page_owner = find_owner(PageRef{owner->page->id});
        if (!page_owner || page_owner->page ||
            (page_owner->control && page_owner->control->kind() != ControlKind::panel)) return std::nullopt;
        panel = page_owner->control;
    }
    bool shared_pages = false;
    if (panel && panel->kind() == ControlKind::panel) {
        const auto* separate = panel->properties().find(PropertyId::from_name("SeparateByPage"));
        if (separate) {
            const auto* boolean = std::get_if<bool>(&separate->value);
            if (!boolean) return std::nullopt;
            shared_pages = !*boolean;
        }
    } else if (panel && metamodel::descriptor_for(panel->kind()).child_policy == metamodel::ChildPolicy::forbidden) {
        return std::nullopt;
    }
    std::vector<ObjectId> result;
    const auto collect = [&](const Owner& group_owner) {
        for (const auto& child : *group_owner.children) {
            const auto* reference = std::get_if<ControlRef>(&child);
            if (!reference) continue;
            const auto resolved_owner = find_owner(child);
            if (!document.find_control(reference->id()) || !resolved_owner ||
                resolved_owner->children != group_owner.children) return false;
            result.push_back(reference->id());
        }
        return true;
    };
    if (shared_pages) {
        if (!collect(Owner{&panel->children, panel})) return std::nullopt;
        for (const auto& child : panel->children) {
            const auto* reference = std::get_if<PageRef>(&child);
            if (!reference) continue;
            const auto* page = document.find_page(reference->id());
            const auto page_owner = find_owner(child);
            if (!page || !page_owner || page_owner->control != panel ||
                !collect(Owner{&page->children, nullptr, page})) return std::nullopt;
        }
    } else if (!collect(*owner)) return std::nullopt;
    return result;
}

struct TabOrderGroup {
    std::vector<ObjectId> ids;
    std::vector<std::int32_t> orders;
    std::size_t target_index = 0;
};

std::optional<TabOrderGroup> tab_order_group(const OrdinaryFormDocument& document, ObjectId target) {
    auto ids = placement_control_group(document, target);
    if (!ids || ids->size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) return std::nullopt;
    const auto found = std::ranges::find(*ids, target);
    if (found == ids->end()) return std::nullopt;
    TabOrderGroup group;
    group.target_index = static_cast<std::size_t>(found - ids->begin());
    group.ids = std::move(*ids);
    std::set<std::int32_t> seen;
    for (std::size_t index = 0; index < group.ids.size(); ++index) {
        const auto* control = document.find_control(group.ids[index]);
        if (!control) return std::nullopt;
        const auto& property = control->position.tab_order;
        const auto value = property.is_explicit() ? property.value() : std::optional<std::int32_t>{static_cast<std::int32_t>(index + 1)};
        if (!value || *value <= 0 || static_cast<std::size_t>(*value) > group.ids.size() || !seen.insert(*value).second) return std::nullopt;
        group.orders.push_back(*value);
    }
    return group;
}

using PositionPropertyRef = std::variant<Property<std::int32_t>*, Property<bool>*>;
using ConstPositionPropertyRef = std::variant<const Property<std::int32_t>*, const Property<bool>*>;

std::optional<PositionPropertyRef> find_position_property(Position& position, std::string_view name) {
    if (name == "Left") return PositionPropertyRef{&position.left};
    if (name == "Top") return PositionPropertyRef{&position.top};
    if (name == "Width") return PositionPropertyRef{&position.width};
    if (name == "Height") return PositionPropertyRef{&position.height};
    if (name == "Visible") return PositionPropertyRef{&position.visible};
    return std::nullopt;
}

std::optional<ConstPositionPropertyRef> find_position_property(
    const Position& position, std::string_view name) {
    if (name == "Left") return ConstPositionPropertyRef{&position.left};
    if (name == "Top") return ConstPositionPropertyRef{&position.top};
    if (name == "Width") return ConstPositionPropertyRef{&position.width};
    if (name == "Height") return ConstPositionPropertyRef{&position.height};
    if (name == "Visible") return ConstPositionPropertyRef{&position.visible};
    return std::nullopt;
}

bool valid_typed_uuid(std::string_view uuid) noexcept {
    if (uuid.size() != 36) return false;
    for (std::size_t index = 0; index < uuid.size(); ++index) {
        const char character = uuid[index];
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (character != '-') return false;
        } else if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) return false;
    }
    return true;
}

bool valid_typed_utf8(std::string_view name) noexcept {
    for (std::size_t offset = 0; offset < name.size();) {
        const auto first = static_cast<unsigned char>(name[offset++]);
        if (first < 0x80) continue;
        std::uint32_t code_point = 0;
        std::size_t remaining = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { code_point = first & 0x1f; remaining = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { code_point = first & 0x0f; remaining = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { code_point = first & 0x07; remaining = 3; minimum = 0x10000; }
        else return false;
        if (remaining > name.size() - offset) return false;
        while (remaining-- != 0) {
            const auto next = static_cast<unsigned char>(name[offset++]);
            if ((next & 0xc0) != 0x80) return false;
            code_point = (code_point << 6) | (next & 0x3f);
        }
        if (code_point < minimum || code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff)) return false;
    }
    return true;
}


bool validate_typed_description(const TypeDomainPatternValue& value, TypedValueBudget& budget) {
    if (value.entries.size() > max_typed_value_nodes - budget.nodes) return false;
    budget.nodes += value.entries.size();
    return storage::value_codec::decode_type_domain(storage::value_codec::encode_type_domain(value)) == value;
}

bool validate_typed_value_node(const PropertyValue& value, TypedValueBudget& budget, std::size_t depth);

bool validate_value_list_contents(const ValueListValue& value, TypedValueBudget& budget, std::size_t depth) {
    if (value.available_values.size() > 1 || value.items.size() > (max_typed_value_nodes - budget.nodes) / 3) return false;
    const auto minimum_item_nodes = value.items.size() * 3;
    (void)value.effective_last_assigned_id();
    if (value.value_type.entries.size() > max_typed_value_nodes - budget.nodes - minimum_item_nodes ||
        !validate_typed_description(value.value_type, budget)) return false;
    std::set<std::uint64_t> ids;
    for (const auto& item : value.items) {
        if (depth + 2 > max_typed_value_depth || budget.nodes >= max_typed_value_nodes || !ids.insert(item.id).second) return false;
        ++budget.nodes;
        const auto* formatted = std::get_if<FormattedStringValue>(&item.presentation);
        if (!std::holds_alternative<std::string>(item.presentation) &&
            !(formatted && formatted->formatted && std::holds_alternative<std::string>(formatted->value))) return false;
        if (!validate_typed_value_node(item.value, budget, depth + 2) ||
            !validate_typed_value_node(item.presentation, budget, depth + 2)) return false;
        if (item.picture) {
            if (budget.nodes >= max_typed_value_nodes || item.picture->asset.id() || !item.picture->standard_name ||
                !metamodel::find_standard_picture(item.picture->standard_name->value)) return false;
            ++budget.nodes;
        }
    }
    for (const auto& available : value.available_values) {
        if (depth >= max_typed_value_depth || budget.nodes >= max_typed_value_nodes) return false;
        ++budget.nodes;
        if (!validate_value_list_contents(available, budget, depth + 1)) return false;
    }
    return true;
}

bool validate_typed_value_node(const PropertyValue& value, TypedValueBudget& budget, std::size_t depth) {
    if (depth > max_typed_value_depth || budget.nodes >= max_typed_value_nodes) return false;
    ++budget.nodes;
    return std::visit([&](const auto& item) -> bool {
        using Value = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, UndefinedValue> || std::is_same_v<Value, NullValue> ||
            std::is_same_v<Value, bool> || std::is_same_v<Value, std::int64_t>) return true;
        else if constexpr (std::is_same_v<Value, std::string>) return valid_typed_utf8(item);
        else if constexpr (std::is_same_v<Value, FormattedStringValue>) {
            const auto* text = std::get_if<std::string>(&item.value);
            return text && valid_typed_utf8(*text);
        }
        else if constexpr (std::is_same_v<Value, DecimalValue>) return storage::value_codec::canonical_decimal(item.canonical) == item.canonical;
        else if constexpr (std::is_same_v<Value, DateValue>) {
            (void)storage::value_codec::date_to_platform(item.canonical);
            return true;
        } else if constexpr (std::is_same_v<Value, UuidValue>) return valid_typed_uuid(item.canonical);
        else if constexpr (std::is_same_v<Value, PlatformTypeValue>) {
            const auto types = metamodel::platform_value_type_descriptors();
            return std::ranges::find(types, item.name, &metamodel::PlatformValueTypeDescriptor::name) != types.end();
        } else if constexpr (std::is_same_v<Value, ColorValue>) {
            if (item.alpha != 255) return false;
            if (item.kind == ColorKind::absolute) return std::holds_alternative<std::monostate>(item.style);
            if (item.red != 0 || item.green != 0 || item.blue != 0) return false;
            if (item.kind == ColorKind::automatic) return std::holds_alternative<std::monostate>(item.style);
            const auto* name = std::get_if<QualifiedName>(&item.style);
            return item.kind == ColorKind::style_reference && name && metamodel::find_named_color(name->value);
        } else if constexpr (std::is_same_v<Value, FontValue>) {
            (void)storage::value_codec::encode_font(item);
            return true;
        } else if constexpr (std::is_same_v<Value, BorderValue>) {
            if (!valid_border_value(item)) return false;
            if (item.kind == BorderKind::absolute) return true;
            const auto* name = std::get_if<QualifiedName>(&item.style);
            return name && name->value == "StyleBorders.ControlBorder";
        } else if constexpr (std::is_same_v<Value, TypeDomainPatternValue>) {
            return validate_typed_description(item, budget);
        } else if constexpr (std::is_same_v<Value, ValueListValue>) {
            return validate_value_list_contents(item, budget, depth);
        } else if constexpr (std::is_same_v<Value, ArrayValue>) {
            if (item.items.size() > max_typed_value_nodes - budget.nodes) return false;
            for (const auto& child : item.items)
                if (!validate_typed_value_node(child, budget, depth + 1)) return false;
            return true;
        } else if constexpr (std::is_same_v<Value, StructureValue>) {
            if (item.fields.size() > max_typed_value_nodes - budget.nodes) return false;
            std::set<std::string> names;
            for (const auto& field : item.fields) {
                if (field.name.empty() || !valid_typed_utf8(field.name) || !names.insert(field.name).second || !validate_typed_value_node(field.value, budget, depth + 1)) return false;
            }
            return true;
        } else if constexpr (std::is_same_v<Value, MapValue>) {
            if (item.entries.size() > (max_typed_value_nodes - budget.nodes) / 2) return false;
            std::set<std::string> keys;
            for (const auto& entry : item.entries) {
                if (!validate_typed_value_node(entry.key, budget, depth + 1)) return false;
                const auto key = typed_map_key(entry.key);
                if (!key || !keys.insert(*key).second || !validate_typed_value_node(entry.value, budget, depth + 1)) return false;
            }
            return true;
        } else return false;
    }, value);
}

}  // namespace

std::optional<std::string> typed_map_key(const PropertyValue& value) {
    return std::visit([](const auto& item) -> std::optional<std::string> {
        using Value = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, UndefinedValue>) return "U";
        else if constexpr (std::is_same_v<Value, NullValue>) return "L";
        else if constexpr (std::is_same_v<Value, bool>) return item ? "B1" : "B0";
        else if constexpr (std::is_same_v<Value, std::int64_t>) return "N" + std::to_string(item);
        else if constexpr (std::is_same_v<Value, DecimalValue>) return "N" + item.canonical;
        else if constexpr (std::is_same_v<Value, std::string>) return "S" + item;
        else if constexpr (std::is_same_v<Value, DateValue>) return "D" + item.canonical;
        else if constexpr (std::is_same_v<Value, UuidValue>) return "#" + item.canonical;
        else if constexpr (std::is_same_v<Value, PlatformTypeValue>) return "T" + item.name;
        else return std::nullopt;
    }, value);
}

bool operator==(const MapValue& left, const MapValue& right) {
    if (left.fixed != right.fixed || left.entries.size() != right.entries.size()) return false;
    const auto index_entries = [](const MapValue& value, std::map<std::string, const PropertyValue*>& index) {
        for (const auto& entry : value.entries) {
            const auto key = typed_map_key(entry.key);
            TypedValueBudget budget;
            if (!key || !validate_typed_value(entry.key, budget) || !index.emplace(*key, &entry.value).second) return false;
        }
        return true;
    };
    std::map<std::string, const PropertyValue*> left_index, right_index;
    if (!index_entries(left, left_index) || !index_entries(right, right_index)) return false;
    auto right_entry = right_index.begin();
    for (const auto& [key, value] : left_index) {
        if (key != right_entry->first || *value != *right_entry->second) return false;
        ++right_entry;
    }
    return true;
}

bool validate_typed_value(const PropertyValue& value, TypedValueBudget& budget) noexcept {
    try {
        return validate_typed_value_node(value, budget, 0);
    } catch (...) {
        return false;
    }
}

std::optional<ValueListRef> ValueListRef::create(const ValueListValue& value) {
    try {
        TypedValueBudget budget{1};
        if (!validate_value_list_contents(value, budget, 0)) return std::nullopt;
    } catch (const std::exception&) { return std::nullopt; }
    ValueListRef result;
    result.value_ = std::make_shared<ValueListValue>(value);
    return result;
}

bool ValueListRef::replace(const ValueListValue& value) const {
    if (!value_) return false;
    try {
        TypedValueBudget budget{1};
        if (!validate_value_list_contents(value, budget, 0)) return false;
    } catch (const std::exception&) { return false; }
    ValueListValue replacement = value;
    *value_ = std::move(replacement);
    return true;
}

const PropertyEntry* PropertySet::find(PropertyId id) const noexcept {
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &found->second;
}

PropertyEntry* PropertySet::find(PropertyId id) noexcept {
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &found->second;
}

bool PropertySet::contains(PropertyId id) const noexcept {
    return entries_.contains(id);
}

std::size_t PropertySet::size() const noexcept {
    return entries_.size();
}

bool PropertySet::empty() const noexcept {
    return entries_.empty();
}

void PropertySet::set_explicit(PropertyId id, PropertyValue value) {
    if (!id) {
        throw std::invalid_argument("ordinary-form property ID must be nonzero");
    }
    entries_.insert_or_assign(
        id,
        PropertyEntry{id, PropertyState::explicit_value, std::move(value)});
}

bool PropertySet::unset(PropertyId id) {
    return entries_.erase(id) != 0;
}

void PropertySet::clear() noexcept {
    entries_.clear();
}

ControlKind payload_kind(const ControlPayload& payload) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using Payload = std::remove_cvref_t<decltype(value)>;
            return Payload::kind;
        },
        payload);
}

PropertySet& payload_properties(ControlPayload& payload) noexcept {
    return std::visit([](auto& value) -> PropertySet& { return value.properties; }, payload);
}

const PropertySet& payload_properties(const ControlPayload& payload) noexcept {
    return std::visit(
        [](const auto& value) -> const PropertySet& { return value.properties; },
        payload);
}

ControlNode::ControlNode(
    ObjectId object_id,
    std::string object_name,
    ControlPayload control_payload
)
    : id(object_id), name(std::move(object_name)), payload(std::move(control_payload)) {}

ControlKind ControlNode::kind() const noexcept {
    return payload_kind(payload);
}

PropertySet& ControlNode::properties() noexcept {
    return payload_properties(payload);
}

const PropertySet& ControlNode::properties() const noexcept {
    return payload_properties(payload);
}

bool ValidationReport::has(InvariantCode code) const noexcept {
    for (const auto& violation : violations) {
        if (violation.code == code) {
            return true;
        }
    }
    return false;
}

InvariantError::InvariantError(ValidationReport report)
    : std::logic_error(
          "ordinary-form document violates " +
          std::to_string(report.violations.size()) + " invariant(s)"),
      report_(std::move(report)) {}

OrdinaryFormDocument::OrdinaryFormDocument() {
    rebuild_index();
}

OrdinaryFormDocument::OrdinaryFormDocument(Form form) : form_(std::move(form)) {
    rebuild_index();
}

void OrdinaryFormDocument::set_form(Form form) {
    form_ = std::move(form);
    window_permissions_.clear();
    runtime_form_values_.clear();
    runtime_choice_lists_.clear();
    rebuild_index();
}

void OrdinaryFormDocument::set_module(FormModule module) {
    module_ = std::move(module);
}

void OrdinaryFormDocument::add_asset(PictureAsset asset) {
    const std::size_t index = assets_.size();
    assets_.push_back(std::move(asset));
    index_first(assets_.back().id, ObjectCategory::picture_asset, index);
}

void OrdinaryFormDocument::set_asset_bytes(ObjectId id, std::vector<std::uint8_t> bytes) {
    const auto found = std::find_if(assets_.begin(), assets_.end(), [id](const PictureAsset& asset) {
        return asset.id == id;
    });
    if (found == assets_.end()) {
        throw std::invalid_argument("picture asset ID does not exist");
    }
    found->bytes = std::move(bytes);
}

void OrdinaryFormDocument::add_control(ControlNode control) {
    const std::size_t index = collections_.controls.size();
    collections_.controls.push_back(std::move(control));
    index_first(collections_.controls.back().id, ObjectCategory::control, index);
}

void OrdinaryFormDocument::add_page(Page page) {
    const std::size_t index = collections_.pages.size();
    collections_.pages.push_back(std::move(page));
    index_first(collections_.pages.back().id, ObjectCategory::page, index);
}

void OrdinaryFormDocument::add_attribute(Attribute attribute) {
    const std::size_t index = collections_.attributes.size();
    collections_.attributes.push_back(std::move(attribute));
    index_first(collections_.attributes.back().id, ObjectCategory::attribute, index);
}

void OrdinaryFormDocument::add_command(Command command) {
    const std::size_t index = collections_.commands.size();
    collections_.commands.push_back(std::move(command));
    index_first(collections_.commands.back().id, ObjectCategory::command, index);
}

void OrdinaryFormDocument::add_event(Event event) {
    const std::size_t index = collections_.events.size();
    collections_.events.push_back(std::move(event));
    index_first(collections_.events.back().id, ObjectCategory::event, index);
}

std::optional<OrdinaryFormDocument::ObjectView> OrdinaryFormDocument::find(ObjectId id) const {
    constexpr std::array categories{
        ObjectCategory::form,
        ObjectCategory::control,
        ObjectCategory::page,
        ObjectCategory::attribute,
        ObjectCategory::command,
        ObjectCategory::event,
        ObjectCategory::picture_asset,
    };
    std::optional<ObjectLocation> location;
    for (const ObjectCategory category : categories) {
        const auto candidate = find_location(category, id);
        if (!candidate) {
            continue;
        }
        if (location) {
            return std::nullopt;
        }
        location = candidate;
    }
    if (!location) {
        return std::nullopt;
    }

    switch (location->category) {
        case ObjectCategory::form:
            return ObjectView{std::cref(form_)};
        case ObjectCategory::control:
            return ObjectView{std::cref(collections_.controls.at(location->index))};
        case ObjectCategory::page:
            return ObjectView{std::cref(collections_.pages.at(location->index))};
        case ObjectCategory::attribute:
            return ObjectView{std::cref(collections_.attributes.at(location->index))};
        case ObjectCategory::command:
            return ObjectView{std::cref(collections_.commands.at(location->index))};
        case ObjectCategory::event:
            return ObjectView{std::cref(collections_.events.at(location->index))};
        case ObjectCategory::picture_asset:
            return ObjectView{std::cref(assets_.at(location->index))};
    }
    return std::nullopt;
}

const ControlNode* OrdinaryFormDocument::find_control(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::control, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.controls[found->index];
}

const Page* OrdinaryFormDocument::find_page(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::page, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.pages[found->index];
}

const Attribute* OrdinaryFormDocument::find_attribute(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::attribute, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.attributes[found->index];
}

const Command* OrdinaryFormDocument::find_command(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::command, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.commands[found->index];
}

const Event* OrdinaryFormDocument::find_event(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::event, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.events[found->index];
}

const PictureAsset* OrdinaryFormDocument::find_asset(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::picture_asset, id);
    if (!found) {
        return nullptr;
    }
    return &assets_[found->index];
}

std::optional<OrdinaryFormDocument::ObjectLocation> OrdinaryFormDocument::find_location(
    ObjectCategory category,
    ObjectId id) const noexcept {
    const auto found = index_.find(ObjectKey{category, id});
    return found == index_.end() ? std::nullopt : std::optional<ObjectLocation>{found->second};
}

std::size_t OrdinaryFormDocument::indexed_id_count() const noexcept {
    return index_.size();
}

const std::vector<TableColumn>* OrdinaryFormDocument::table_columns(TableColumnsRef reference) const noexcept {
    const auto* control = find_control(reference.owner.id());
    const auto* table = control ? std::get_if<TablePayload>(&control->payload) : nullptr;
    return table ? &table->columns : nullptr;
}

std::optional<std::vector<ChildItemRef>> OrdinaryFormDocument::form_item_refs(FormItemCollectionRef reference) const {
    const ControlNode* panel = nullptr;
    if (reference.kind == FormItemCollectionKind::controls) {
        if (!form_.id || reference.owner != form_.id) return std::nullopt;
    } else if (reference.kind == FormItemCollectionKind::pages) {
        panel = find_control(reference.owner);
        if (panel == nullptr || panel->kind() != ControlKind::panel) return std::nullopt;
    } else {
        return std::nullopt;
    }

    std::vector<ChildItemRef> pending(form_.children.rbegin(), form_.children.rend());
    std::unordered_set<ObjectKey, ObjectKeyHash> visited;
    std::vector<ChildItemRef> controls;
    std::size_t page_count = 0;
    while (!pending.empty()) {
        const auto child = pending.back();
        pending.pop_back();
        const std::vector<ChildItemRef>* children = nullptr;
        auto policy = metamodel::ChildPolicy::ordered_controls;
        if (const auto* control_ref = std::get_if<ControlRef>(&child)) {
            const auto* control = find_control(control_ref->id());
            if (control == nullptr || !control->id ||
                !visited.insert({ObjectCategory::control, control->id}).second) return std::nullopt;
            controls.push_back(child);
            children = &control->children;
            policy = metamodel::descriptor_for(control->kind()).child_policy;
        } else {
            const auto page_ref = std::get<PageRef>(child);
            const auto* page = find_page(page_ref.id());
            if (page == nullptr || !page->id ||
                !visited.insert({ObjectCategory::page, page->id}).second) return std::nullopt;
            ++page_count;
            children = &page->children;
        }
        if ((policy == metamodel::ChildPolicy::forbidden && !children->empty()) ||
            (policy == metamodel::ChildPolicy::ordered_controls &&
                std::ranges::any_of(*children, [](const ChildItemRef& item) { return std::holds_alternative<PageRef>(item); })))
            return std::nullopt;
        pending.insert(pending.end(), children->rbegin(), children->rend());
    }
    if (controls.size() != collections_.controls.size() || page_count != collections_.pages.size()) return std::nullopt;
    if (reference.kind == FormItemCollectionKind::controls) return controls;

    std::vector<ChildItemRef> pages;
    for (const auto& child : panel->children)
        if (std::holds_alternative<PageRef>(child)) pages.push_back(child);
    if (pages.empty()) return std::nullopt;
    return pages;
}

namespace {
bool is_string_input_property(const metamodel::PropertyDescriptor& descriptor) noexcept {
    return descriptor.control_kind == ControlKind::input_field &&
        (descriptor.api_name == "Mask" || descriptor.api_name == "MultiLine" ||
         descriptor.api_name == "ExtendedEdit" || descriptor.api_name == "PasswordMode");
}

const TypeDomainPatternValue* effective_input_type(
    const OrdinaryFormDocument& document, const ControlNode& control) noexcept {
    if (control.kind() != ControlKind::input_field) return nullptr;
    if (control.data_path) {
        if (!control.data_path->members.empty()) return nullptr;
        const auto* attribute = document.find_attribute(control.data_path->attribute.id());
        return attribute == nullptr ? nullptr : &attribute->type;
    }
    const auto* local = control.extension_properties.find(PropertyId::from_name("ValueType"));
    return local == nullptr ? nullptr : std::get_if<TypeDomainPatternValue>(&local->value);
}

bool table_tree_property_available(const OrdinaryFormDocument& document, const ControlNode& control) {
    if (!control.data_path || !control.data_path->members.empty()) return false;
    const auto* attribute = document.find_attribute(control.data_path->attribute.id());
    TypeDomainEntry expected;
    expected.term = TypeDomainTerm::value_tree;
    return attribute && attribute->type.entries.size() == 1 && attribute->type.entries.front() == expected;
}

ChartArea default_chart_area(ChartAreaKind kind, bool show_legend) {
    ChartArea area;
    for (const auto& descriptor : metamodel::chart_area_property_descriptors()) {
        if (descriptor.coordinate != nullptr)
            area.*(descriptor.coordinate) = DecimalValue{std::string(kind == ChartAreaKind::plot
                ? descriptor.plot_default : kind == ChartAreaKind::title ? descriptor.title_default : descriptor.legend_default)};
    }
    if ((kind == ChartAreaKind::legend || kind == ChartAreaKind::title) && !show_legend) area.placement = "None";
    return area;
}

bool chart_area_visible(const ChartPayload& chart, ChartAreaKind kind) {
    const auto* entry = chart.properties.find(PropertyId::from_name(kind == ChartAreaKind::title ? "ShowTitle" : "ShowLegend"));
    return entry == nullptr || (std::get_if<bool>(&entry->value) && std::get<bool>(entry->value));
}

bool valid_chart_style(metamodel::ValueCodec codec, const PropertyValue& value) {
    try {
        if (codec == metamodel::ValueCodec::line) {
            if (std::holds_alternative<UndefinedValue>(value)) return true;
            const auto* line = std::get_if<LineValue>(&value);
            if (!line) return false;
            (void)storage::value_codec::encode_line(*line);
            return true;
        }
        if ((codec == metamodel::ValueCodec::font && !std::holds_alternative<FontValue>(value)) ||
            (codec != metamodel::ValueCodec::font && !std::holds_alternative<ColorValue>(value))) return false;
        TypedValueBudget budget;
        return validate_typed_value(value, budget);
    } catch (const std::exception&) { return false; }
}

bool normalize_chart_scale_property_value(const metamodel::ChartScalePropertyDescriptor& field, PropertyValue& value) {
    if (field.numeric_policy != metamodel::ChartScaleNumericPolicy::none) {
        std::string text;
        if (const auto* n = std::get_if<DecimalValue>(&value)) text = n->canonical;
        else if (const auto* n = std::get_if<std::int64_t>(&value)) text = std::to_string(*n);
        else if (const auto* b = std::get_if<bool>(&value)) text = *b ? "1" : "0";
        else if (const auto* t = std::get_if<std::string>(&value)) text = *t;
        else return false;
        const auto first = text.find_first_not_of(" \t\r\n\f\v");
        if (first == std::string::npos) return false;
        text = text.substr(first, text.find_last_not_of(" \t\r\n\f\v") - first + 1);
        std::replace(text.begin(), text.end(), ',', '.');
        try { text = storage::value_codec::canonical_decimal(text); }
        catch (const std::exception&) { return false; }
        if (field.numeric_policy == metamodel::ChartScaleNumericPolicy::nonnegative_decimal && text.starts_with('-')) text = "0";
        if (field.numeric_policy == metamodel::ChartScaleNumericPolicy::unit_decimal) {
            if (text.starts_with('-')) text = "0";
            else if (text != "0" && text != "1" && !text.starts_with("0.")) text = "1";
        }
        if (field.numeric_policy == metamodel::ChartScaleNumericPolicy::rounded_wrapped_int32) {
            const bool negative = text.starts_with('-');
            const auto begin = negative ? std::size_t{1} : std::size_t{0};
            const auto point = text.find('.');
            const auto end = point == std::string::npos ? text.size() : point;
            std::uint32_t magnitude = 0;
            for (auto i = begin; i < end; ++i) magnitude = magnitude * 10U + static_cast<std::uint32_t>(text[i] - '0');
            if (point != std::string::npos && text[point + 1] >= '5') ++magnitude;
            const std::uint32_t bits = negative ? 0U - magnitude : magnitude;
            value = static_cast<std::int64_t>(bits <= static_cast<std::uint32_t>(INT32_MAX) ? static_cast<std::int64_t>(bits) : static_cast<std::int64_t>(bits) - 4294967296LL);
        } else value = DecimalValue{std::move(text)};
    } else if (field.value_codec == metamodel::ValueCodec::enumeration) {
        const auto* enumeration = std::get_if<EnumerationValue>(&value);
        if (!enumeration || enumeration->type_name != field.enumeration_type ||
            std::ranges::find(field.members, enumeration->member) == field.members.end())
            return false;
    } else if (field.value_codec == metamodel::ValueCodec::boolean) {
        if (!std::holds_alternative<bool>(value)) return false;
    } else if (field.value_codec == metamodel::ValueCodec::border) {
        const auto* border = std::get_if<BorderValue>(&value);
        if (!border || !valid_border_value(*border)) return false;
    } else if (field.value_codec == metamodel::ValueCodec::line || field.value_codec == metamodel::ValueCodec::color || field.value_codec == metamodel::ValueCodec::font) {
        if (!valid_chart_style(field.value_codec, value)) return false;
    } else if (!std::holds_alternative<std::string>(value) &&
        !(std::holds_alternative<LocalizedStringValue>(value) && valid_localized_string(std::get<LocalizedStringValue>(value))))
        return false;
    return true;
}

bool chart_budget_fits(const std::vector<ControlNode>& controls, ObjectId resized = {},
    std::size_t new_series = 0, std::size_t new_points = 0, std::size_t extra_trendlines = 0) {
    std::size_t budget = 0;
    const auto add = [&](std::size_t count) {
        if (count > max_chart_collection_items - budget) return false;
        budget += count;
        return true;
    };
    for (const auto& control : controls) {
        const auto* chart = std::get_if<ChartPayload>(&control.payload);
        if (!chart) continue;
        const bool changed = resized && control.id == resized;
        const auto series = changed ? new_series : chart->series.size();
        const auto points = changed ? new_points : chart->points.size();
        if (series > max_chart_collection_items || points > max_chart_collection_items ||
            (points && series > max_chart_collection_items / points) ||
            !add(series) || !add(points) || !add(series * points) || !add(chart->gauge_quality_bands.size()) ||
            !add(chart->summary_series.trendlines.size())) return false;
        const auto retained = std::min(series, chart->series.size());
        for (std::size_t i = 0; i < retained; ++i)
            if (!add(chart->series[i].trendlines.size())) return false;
    }
    return add(extra_trendlines);
}

template<class Chart>
auto chart_series_object(Chart& chart, ObjectId series) -> std::optional<std::conditional_t<std::is_const_v<Chart>, metamodel::ConstChartSeriesObject, metamodel::ChartSeriesObject>> {
    using View = std::conditional_t<std::is_const_v<Chart>, metamodel::ConstChartSeriesObject, metamodel::ChartSeriesObject>;
    if (series == ObjectId{1}) return View{&chart.summary_series};
    const auto found = std::ranges::find(chart.series, series, &ChartSeries::id);
    if (found == chart.series.end()) return std::nullopt;
    return View{&*found};
}
bool normalize_chart_series_property_value(const metamodel::ChartSeriesPropertyDescriptor& field, PropertyValue& value) {
    if (field.value_codec == metamodel::ValueCodec::enumeration) {
        const auto* enumeration = std::get_if<EnumerationValue>(&value);
        return enumeration && enumeration->type_name == field.enumeration_type && std::ranges::find(field.members, enumeration->member) != field.members.end();
    }
    if (field.value_codec == metamodel::ValueCodec::line)
        return std::holds_alternative<LineValue>(value) && valid_chart_style(field.value_codec, value);
    if (field.value_codec == metamodel::ValueCodec::color || field.value_codec == metamodel::ValueCodec::font)
        return valid_chart_style(field.value_codec, value);
    if (field.value_codec == metamodel::ValueCodec::boolean) return std::holds_alternative<bool>(value);
    if (field.value_codec == metamodel::ValueCodec::integer32) {
        std::string text;
        if (const auto* number = std::get_if<std::int64_t>(&value)) text = std::to_string(*number);
        else if (const auto* number = std::get_if<DecimalValue>(&value)) text = number->canonical;
        else if (const auto* boolean = std::get_if<bool>(&value)) text = *boolean ? "1" : "0";
        else if (const auto* string = std::get_if<std::string>(&value)) { text = *string; std::replace(text.begin(), text.end(), ',', '.'); }
        else return false;
        try { text = storage::value_codec::canonical_decimal(text); } catch (const std::exception&) { return false; }
        text.resize(text.find('.') == std::string::npos ? text.size() : text.find('.'));
        std::int32_t number = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
        value = static_cast<std::int64_t>(std::max(number, 1));
        return true;
    }
    if (field.value_codec == metamodel::ValueCodec::string) return std::holds_alternative<std::string>(value);
    if (field.value_codec == metamodel::ValueCodec::localized_text)
        return std::holds_alternative<std::string>(value) || (std::holds_alternative<LocalizedStringValue>(value) && valid_localized_string(std::get<LocalizedStringValue>(value)));
    if (field.value_codec == metamodel::ValueCodec::typed_value) {
        metamodel::PropertyDescriptor scalar;
        scalar.value_codec = metamodel::ValueCodec::typed_value;
        return property_value_matches(scalar, value);
    }
    return false;
}

const DecimalValue* chart_area_coordinate(const ChartArea& area, PropertyId property) {
    const auto* descriptor = metamodel::find_chart_area_property(property);
    return descriptor && descriptor->coordinate ? &(area.*(descriptor->coordinate)) : nullptr;
}
}

PropertyReadResult OrdinaryFormDocument::get_prop_val(ObjectKey owner, PropertyId property) const {
    if (owner.chart_series || owner.chart_trendline || owner.chart_equation_area) {
        if (owner.category != ObjectCategory::control || !owner.chart_series || owner.chart_area != ChartAreaKind::none || owner.chart_scale != ChartScaleKind::none || (owner.chart_equation_area && !owner.chart_trendline)) return {PropertyReadStatus::unknown_owner, std::nullopt};
        const auto* control = find_control(owner.id);
        if (!control || control->kind() != ControlKind::chart) return {PropertyReadStatus::unknown_owner, std::nullopt};
        const auto object = chart_series_object(std::get<ChartPayload>(control->payload), owner.chart_series);
        if (!object) return {PropertyReadStatus::unknown_owner, std::nullopt};
        if (owner.chart_trendline) {
            const ChartTrendline* trend = std::visit([&](auto* item) -> const ChartTrendline* {
                const auto found = std::ranges::find(item->trendlines, owner.chart_trendline, &ChartTrendline::id);
                return found == item->trendlines.end() ? nullptr : &*found;
            }, *object);
            if (!trend) return {PropertyReadStatus::unknown_owner, std::nullopt};
            if (owner.chart_equation_area) {
                for (const auto& field : metamodel::chart_label_area_property_descriptors()) if (field.id == property) {
                    const auto* entry = trend->equation_area.find(property);
                    return {entry ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default, entry ? entry->value : metamodel::chart_scale_property_default(field)};
                }
            } else for (const auto& field : metamodel::chart_trendline_property_descriptors()) if (field.id == property) {
                if (field.value_codec == metamodel::ValueCodec::chart_equation_area) return {PropertyReadStatus::computed_value, PropertyValue{ChartEquationAreaRef{owner.id, owner.chart_series, owner.chart_trendline}}};
                const auto* entry = trend->properties.find(property);
                return {entry ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default, entry ? entry->value : field.default_value};
            }
            return {PropertyReadStatus::unknown_property, std::nullopt};
        }
        for (const auto& field : metamodel::chart_series_property_descriptors()) if (field.id == property) {
            if (field.name == "SummarySeries") return {PropertyReadStatus::computed_value, PropertyValue{owner.chart_series == ObjectId{1}}};
            if (field.name == "Trendlines") return {PropertyReadStatus::computed_value, PropertyValue{ChartTrendlinesRef{owner.id, owner.chart_series}}};
            if (field.name == "Indicator") {
                const auto fields = metamodel::chart_series_property_descriptors();
                const auto graph = std::ranges::find(fields, std::string_view{"GraphicalRepresentationType"}, &metamodel::ChartSeriesPropertyDescriptor::name);
                const auto value = metamodel::chart_series_property_value(*object, *graph).value_or(graph->default_value);
                return {PropertyReadStatus::computed_value, PropertyValue{std::get<EnumerationValue>(value).member == "Line"}};
            }
            const auto value = metamodel::chart_series_property_value(*object, field);
            const auto default_value = field.name == "Text" && owner.chart_series == ObjectId{1} ? PropertyValue{std::string{"Сводная"}} : field.default_value;
            return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default, value.value_or(default_value)};
        }
        return {PropertyReadStatus::unknown_property, std::nullopt};
    }
    if (owner.chart_scale != ChartScaleKind::none) {
        const auto owners = metamodel::chart_scale_owner_descriptors();
        const auto scale_owner = std::ranges::find(owners, owner.chart_scale, &metamodel::ChartScaleOwnerDescriptor::kind);
        const auto* scale_control = find_control(owner.id);
        if (owner.category != ObjectCategory::control || (owner.chart_area != ChartAreaKind::plot && owner.chart_area != ChartAreaKind::title) || scale_owner == owners.end() || !scale_control || scale_control->kind() != ControlKind::chart) return {PropertyReadStatus::unknown_owner, std::nullopt};
        const auto& plot = std::get<ChartPayload>(scale_control->payload).plot_area;
        if (owner.chart_area == ChartAreaKind::plot && property == PropertyId::from_name("TitleArea"))
            return {PropertyReadStatus::computed_value, PropertyValue{ChartAreaRef{owner.id, ChartAreaKind::title, owner.chart_scale}}};
        for (const auto& field : owner.chart_area == ChartAreaKind::title ? metamodel::chart_label_area_property_descriptors() : metamodel::chart_scale_property_descriptors()) if (field.id == property) {
            const auto* value = plot ? metamodel::chart_scale_property_value((*plot).*(scale_owner->member), field) : nullptr;
            return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                value ? *value : metamodel::chart_scale_property_default(field)};
        }
        return {PropertyReadStatus::unknown_property, std::nullopt};
    }
    if (owner.chart_area != ChartAreaKind::none) {
        if (owner.category != ObjectCategory::control ||
            (owner.chart_area != ChartAreaKind::plot && owner.chart_area != ChartAreaKind::legend && owner.chart_area != ChartAreaKind::title))
            return {PropertyReadStatus::unknown_owner, std::nullopt};
        const auto* control = find_control(owner.id);
        if (control == nullptr || control->kind() != ControlKind::chart)
            return {PropertyReadStatus::unknown_owner, std::nullopt};
        const auto& chart = std::get<ChartPayload>(control->payload);
        if (owner.chart_area == ChartAreaKind::plot)
            for (const auto& scale : metamodel::chart_scale_owner_descriptors()) if (scale.id == property)
                return {PropertyReadStatus::computed_value, PropertyValue{ChartScaleRef{owner.id, scale.kind}}};
        const auto& stored = owner.chart_area == ChartAreaKind::plot ? chart.plot_area : owner.chart_area == ChartAreaKind::title ? chart.title_area : chart.legend_area;
        const ChartArea implicit = default_chart_area(owner.chart_area, chart_area_visible(chart, owner.chart_area));
        const ChartArea& area = stored ? *stored : implicit;
        if (owner.chart_area == ChartAreaKind::title && property == PropertyId::from_name("Text"))
            return get_prop_val({ObjectCategory::control, owner.id}, PropertyId::from_name("Title"));
        if (property == PropertyId::from_name("Placement"))
            return {(area.explicit_geometry[0] || area.placement != implicit.placement) ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                PropertyValue{EnumerationValue{owner.chart_area == ChartAreaKind::plot
                ? "ChartPlotAreaPlacement" : owner.chart_area == ChartAreaKind::title ? "ChartTitleAreaPlacement" : "ChartLegendPlacement", area.placement}}};
        if (const auto* coordinate = chart_area_coordinate(area, property)) {
            const auto* descriptor = metamodel::find_chart_area_property(property);
            const auto* default_value = chart_area_coordinate(implicit, property);
            return {(area.explicit_geometry[1 + descriptor->coordinate_offset] || *coordinate != *default_value)
                ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default, PropertyValue{*coordinate}};
        }
        if (const auto* descriptor = metamodel::find_chart_area_boolean(owner.chart_area, property)) {
            const auto& value = area.*(descriptor->member);
            return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                PropertyValue{value.value_or(descriptor->default_value)}};
        }
        if (const auto* descriptor = metamodel::find_chart_area_color(owner.chart_area, property)) {
            const auto& value = area.*(descriptor->member);
            return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                PropertyValue{value.value_or(metamodel::chart_area_color_default(descriptor->default_kind))}};
        }
        if (owner.chart_area == ChartAreaKind::plot) {
            if (const auto* descriptor = metamodel::find_chart_plot_format(property)) {
                if (descriptor->text) {
                    const auto& value = area.*(descriptor->text);
                    return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                        value.value_or(PropertyValue{std::string{}})};
                }
                const auto& value = area.*(descriptor->enumeration);
                const auto fallback = EnumerationValue{std::string(descriptor->enumeration_type), std::string(descriptor->members[descriptor->default_ordinal])};
                return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                    PropertyValue{value.value_or(fallback)}};
            }
        }
        if (const auto* descriptor = metamodel::find_chart_area_style(owner.chart_area, property)) {
            if (descriptor->border) {
                const auto& value = area.*(descriptor->border);
                return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                    PropertyValue{value.value_or(BorderValue{})}};
            }
            if (descriptor->font) {
                const auto& value = area.*(descriptor->font);
                return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                    PropertyValue{value.value_or(FontValue{})}};
            }
            const auto& value = area.*(descriptor->line);
            return {value ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                PropertyValue{value.value_or(LineValue{})}};
        }
        return {PropertyReadStatus::unknown_property, std::nullopt};
    }
    const metamodel::PropertyDescriptor* descriptor = nullptr;
    const PropertySet* properties = nullptr;
    const ControlNode* position_control = nullptr;
    if (owner.category == ObjectCategory::form) {
        if (owner.id != form_.id) return {PropertyReadStatus::unknown_owner, std::nullopt};
        if (property == PropertyId::from_name("CurrentControl") || property == PropertyId::from_name("FormOwner"))
            return {PropertyReadStatus::unsupported_surface, std::nullopt};
        if (property == PropertyId::from_name("Controls"))
            return {PropertyReadStatus::computed_value, PropertyValue{FormItemCollectionRef{form_.id, FormItemCollectionKind::controls}}};
        if (property == PropertyId::from_name("ThisForm"))
            return {PropertyReadStatus::computed_value, PropertyValue{FormRef{form_.id}}};
        if (property == PropertyId::from_name("UniqueKey") || property == PropertyId::from_name("ChoiceInitialValue")) {
            if (const auto* entry = runtime_form_values_.find(property))
                return {PropertyReadStatus::computed_value, entry->value};
            return {PropertyReadStatus::computed_value, PropertyValue{UndefinedValue{}}};
        }
        if (property == PropertyId::from_name("WindowOptionsName") || property == PropertyId::from_name("WindowOptionsKey")) {
            if (const auto* entry = runtime_form_values_.find(PropertyId::from_name("WindowOptionsName")))
                return {PropertyReadStatus::computed_value, entry->value};
        }
        if (property == PropertyId::from_name("ReadOnly") || property == PropertyId::from_name("MultipleChoice")) {
            if (const auto* entry = runtime_form_values_.find(property))
                return {PropertyReadStatus::computed_value, entry->value};
        }
        const bool is_permission = property == PropertyId::from_name("AllowNormalState") ||
            property == PropertyId::from_name("AllowDockedState") || property == PropertyId::from_name("AllowAutohideState") ||
            property == PropertyId::from_name("AllowFloatState") || property == PropertyId::from_name("AllowJoinWindow");
        if (is_permission) {
            if (const auto* entry = window_permissions_.find(property))
                return {PropertyReadStatus::computed_value, entry->value};
        }
        if (is_permission || (opening_started_ && property == PropertyId::from_name("ConnectableWindow"))) {
            std::string_view state = "Normal";
            if (const auto* entry = form_.properties.find(PropertyId::from_name("WindowState"))) {
                const auto* value = std::get_if<EnumerationValue>(&entry->value);
                if (!value || value->type_name != "WindowStateVariant" ||
                    (value->member != "Normal" && value->member != "Docked" && value->member != "Autohide" && value->member != "Float"))
                    return {PropertyReadStatus::unsupported_surface, std::nullopt};
                state = value->member;
            }
            bool connectable = false;
            if (const auto* entry = form_.properties.find(PropertyId::from_name("ConnectableWindow"))) {
                const auto* value = std::get_if<bool>(&entry->value);
                if (!value) return {PropertyReadStatus::unsupported_surface, std::nullopt};
                connectable = *value;
            }
            if (opening_started_ && (state == "Docked" || state == "Autohide")) connectable = true;
            if (!is_permission) return {PropertyReadStatus::computed_value, PropertyValue{connectable}};
            const bool value = property == PropertyId::from_name("AllowNormalState") || state != "Normal" ||
                (property == PropertyId::from_name("AllowJoinWindow") && connectable);
            return {PropertyReadStatus::computed_value, PropertyValue{value}};
        }
        descriptor = metamodel::find_form_property(property);
        properties = &form_.properties;
    } else if (owner.category == ObjectCategory::control) {
        const auto location = find_location(ObjectCategory::control, owner.id);
        if (!location) return {PropertyReadStatus::unknown_owner, std::nullopt};
        const auto& control = collections_.controls[location->index];
        if (control.kind() == ControlKind::table && property == PropertyId::from_name("Columns"))
            return {PropertyReadStatus::computed_value, PropertyValue{TableColumnsRef{ControlRef{control.id}}}};
        if ((control.kind() == ControlKind::input_field || control.kind() == ControlKind::choice_field) &&
            property == PropertyId::from_name("ChoiceList")) {
            auto found = runtime_choice_lists_.find(control.id);
            if (found == runtime_choice_lists_.end()) {
                const auto empty = ValueListRef::create(ValueListValue{});
                if (!empty) return {PropertyReadStatus::unsupported_surface, std::nullopt};
                found = runtime_choice_lists_.emplace(control.id, *empty).first;
            }
            return {PropertyReadStatus::computed_value, PropertyValue{found->second}};
        }
        if (control.kind() == ControlKind::panel && property == PropertyId::from_name("Pages"))
            return {PropertyReadStatus::computed_value, PropertyValue{FormItemCollectionRef{control.id, FormItemCollectionKind::pages}}};
        if (control.kind() == ControlKind::chart && property == PropertyId::from_name("SummarySeries"))
            return {PropertyReadStatus::computed_value, PropertyValue{ChartSeriesRef{owner.id, ObjectId{1}}}};
        if (control.kind() == ControlKind::chart &&
            (property == PropertyId::from_name("SeriesCount") || property == PropertyId::from_name("PointCount"))) {
            const auto& chart = std::get<ChartPayload>(control.payload);
            const auto count = property == PropertyId::from_name("SeriesCount") ? chart.series.size() : chart.points.size();
            if (count > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
                return {PropertyReadStatus::unsupported_surface, std::nullopt};
            return {PropertyReadStatus::computed_value, PropertyValue{static_cast<std::int64_t>(count)}};
        }
        if (control.kind() == ControlKind::chart &&
            (property == PropertyId::from_name("Series") || property == PropertyId::from_name("Points") ||
             property == PropertyId::from_name("GaugeChartQualityBands")))
            return {PropertyReadStatus::computed_value, PropertyValue{ChartCollectionRef{control.id,
                property == PropertyId::from_name("Series") ? ChartCollectionKind::series :
                    property == PropertyId::from_name("Points") ? ChartCollectionKind::points : ChartCollectionKind::gauge_quality_bands}}};
        if (control.kind() == ControlKind::chart && property == PropertyId::from_name("ActiveSeries")) {
            if (const auto* entry = control.properties().find(property)) return {PropertyReadStatus::explicit_value, entry->value};
            const auto& chart = std::get<ChartPayload>(control.payload);
            if (chart.series.empty()) return {PropertyReadStatus::computed_value, PropertyValue{UndefinedValue{}}};
            return {PropertyReadStatus::computed_value, PropertyValue{ChartSeriesRef{control.id, chart.series.front().id}}};
        }
        if (control.kind() == ControlKind::chart &&
            (property == PropertyId::from_name("PlotArea") || property == PropertyId::from_name("LegendArea") || property == PropertyId::from_name("TitleArea"))) {
            const auto kind = property == PropertyId::from_name("PlotArea") ? ChartAreaKind::plot : property == PropertyId::from_name("TitleArea") ? ChartAreaKind::title : ChartAreaKind::legend;
            return {PropertyReadStatus::computed_value, PropertyValue{ChartAreaRef{control.id, kind}}};
        }
        descriptor = metamodel::find_property(control.kind(), property);
        if (descriptor == nullptr) return {PropertyReadStatus::unknown_property, std::nullopt};
        if ((control.kind() == ControlKind::list_box && descriptor->api_name == "CurrentRow") ||
            (control.kind() == ControlKind::html_document_field && descriptor->api_name == "Document"))
            return {PropertyReadStatus::unsupported_surface, std::nullopt};
        if (descriptor->api_name == "Name")
            return {PropertyReadStatus::computed_value, PropertyValue{control.name}};
        if (descriptor->value_codec == metamodel::ValueCodec::typed_value) {
            const auto* type = effective_input_type(*this, control);
            const auto* list_mode = control.properties().find(PropertyId::from_name("ListChoiceMode"));
            if (type == nullptr || type->entries.size() != 1 || type->entries.front().term != TypeDomainTerm::numeric ||
                (list_mode != nullptr && std::get_if<bool>(&list_mode->value) && std::get<bool>(list_mode->value)))
                return {PropertyReadStatus::unsupported_surface, std::nullopt};
        }
        if (is_string_input_property(*descriptor) ||
            (control.kind() == ControlKind::input_field && descriptor->api_name == "TypeRestriction")) {
            const auto* type = effective_input_type(*this, control);
            if (type == nullptr || type->entries.size() != 1)
                return {PropertyReadStatus::unsupported_surface, std::nullopt};
            const auto term = type->entries.front().term;
            const auto* list_mode = control.properties().find(PropertyId::from_name("ListChoiceMode"));
            if (is_string_input_property(*descriptor) && list_mode &&
                std::get_if<bool>(&list_mode->value) && std::get<bool>(list_mode->value))
                return {PropertyReadStatus::unsupported_surface, std::nullopt};
            if (descriptor->api_name == "TypeRestriction") {
                if (term != TypeDomainTerm::string && term != TypeDomainTerm::numeric && term != TypeDomainTerm::date)
                    return {PropertyReadStatus::unsupported_surface, std::nullopt};
                if (!control.properties().contains(property))
                    return {PropertyReadStatus::computed_value, PropertyValue{TypeDomainPatternValue{}}};
            } else {
                if (term != TypeDomainTerm::string)
                    return {PropertyReadStatus::unsupported_surface, std::nullopt};
                if (descriptor->api_name == "Mask" && !control.properties().contains(property))
                    return {PropertyReadStatus::proven_default, PropertyValue{std::string{}}};
            }
        }
        if (control.kind() == ControlKind::panel && descriptor->api_name == "CurrentPage") {
            const auto& selected = std::get<PanelPayload>(control.payload).current_page;
            for (const auto& child : control.children) {
                const auto* page = std::get_if<PageRef>(&child);
                if (page == nullptr) continue;
                if (selected && *selected != *page) continue;
                if (find_page(page->id()) == nullptr) return {PropertyReadStatus::unsupported_surface, std::nullopt};
                return {PropertyReadStatus::computed_value, PropertyValue{*page}};
            }
            return {PropertyReadStatus::unsupported_surface, std::nullopt};
        }
        if (descriptor->api_name == "ZOrder") {
            const auto group = tab_order_group(*this, control.id);
            if (!group) return {PropertyReadStatus::unsupported_surface, std::nullopt};
            if (std::ranges::any_of(group->ids, [&](ObjectId id) {
                    return find_control(id)->position.z_order.is_explicit();
                })) return {PropertyReadStatus::unsupported_surface, std::nullopt};
            return {PropertyReadStatus::computed_value, PropertyValue{static_cast<std::int64_t>(group->target_index + 1)}};
        }
        if (descriptor->api_name == "TabOrder") {
            const auto group = tab_order_group(*this, control.id);
            if (!group) return {PropertyReadStatus::unsupported_surface, std::nullopt};
            return {PropertyReadStatus::computed_value, PropertyValue{static_cast<std::int64_t>(group->orders[group->target_index])}};
        }
        if (descriptor->api_name == "Data") {
            if (!control.data_path && control.kind() == ControlKind::radio_button)
                return {PropertyReadStatus::unsupported_surface, std::nullopt};
            if (!control.data_path) return {PropertyReadStatus::computed_value, PropertyValue{std::string{}}};
            const auto* attribute = find_attribute(control.data_path->attribute.id());
            if (!attribute || attribute->name.empty() || !control.data_path->members.empty())
                return {PropertyReadStatus::unsupported_surface, std::nullopt};
            return {PropertyReadStatus::computed_value, PropertyValue{attribute->name}};
        }
        if (descriptor->api_name == "ValueType") {
            if (control.data_path) {
                const auto* attribute = find_attribute(control.data_path->attribute.id());
                if (!attribute || !control.data_path->members.empty())
                    return {PropertyReadStatus::unsupported_surface, std::nullopt};
                return {PropertyReadStatus::computed_value, PropertyValue{attribute->type}};
            }
            if (control.kind() == ControlKind::choice_field)
                return {PropertyReadStatus::computed_value, PropertyValue{TypeDomainPatternValue{}}};
            if (control.kind() == ControlKind::input_field || control.kind() == ControlKind::radio_button) {
                const auto* local = control.extension_properties.find(property);
                if (local && std::holds_alternative<TypeDomainPatternValue>(local->value))
                    return {PropertyReadStatus::computed_value, local->value};
            }
            return {PropertyReadStatus::unsupported_surface, std::nullopt};
        }
        if (control.kind() == ControlKind::table && descriptor->api_name == "Tree" &&
            !table_tree_property_available(*this, control)) return {PropertyReadStatus::unsupported_surface, std::nullopt};
        if (is_typed_collection_property(*descriptor)) {
            return {PropertyReadStatus::unsupported_surface, std::nullopt};
        }
        if (descriptor->surface == metamodel::PropertySurface::panel_placement) {
            position_control = &control;
        } else if (descriptor->surface == metamodel::PropertySurface::control_payload) {
            properties = &control.properties();
        } else if (descriptor->surface == metamodel::PropertySurface::control_extension) {
            properties = &control.extension_properties;
        } else {
            return {PropertyReadStatus::unsupported_surface, std::nullopt};
        }
    } else {
        if (!find_location(owner.category, owner.id)) {
            return {PropertyReadStatus::unknown_owner, std::nullopt};
        }
        return {PropertyReadStatus::unsupported_surface, std::nullopt};
    }

    if (descriptor == nullptr || (properties == nullptr && position_control == nullptr)) {
        return {PropertyReadStatus::unknown_property, std::nullopt};
    }
    if (is_typed_collection_property(*descriptor)) {
        return {PropertyReadStatus::unsupported_surface, std::nullopt};
    }
    if (descriptor->api_access == metamodel::ApiAccess::write_only) {
        return {PropertyReadStatus::write_only, std::nullopt};
    }
    if (descriptor->persistence == metamodel::PersistenceClass::unsupported_by_platform) {
        return {PropertyReadStatus::unsupported_surface, std::nullopt};
    }
    if (position_control != nullptr) {
        if (descriptor->api_name == "DefaultControl") {
            const auto& selected = position_control->position.default_control;
            return {selected.is_explicit() ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                PropertyValue{selected.value().value_or(false)}};
        }
        if (descriptor->api_name == "Collapse") {
            const auto& collapse = position_control->position.collapse;
            if (collapse.is_explicit() && collapse.value())
                return {PropertyReadStatus::explicit_value, PropertyValue{*collapse.value()}};
            return {PropertyReadStatus::proven_default, PropertyValue{EnumerationValue{"ControlCollapseMode", "None"}}};
        }
        const auto position_property = find_position_property(position_control->position, descriptor->api_name);
        if (!position_property) return {PropertyReadStatus::unsupported_surface, std::nullopt};
        return std::visit([](const auto* property_value) -> PropertyReadResult {
            using Value = std::remove_cvref_t<decltype(property_value->value())>;
            if constexpr (std::is_same_v<Value, std::int32_t>) {
                return {property_value->is_explicit() ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                    PropertyValue{static_cast<std::int64_t>(property_value->value())}};
            } else {
                return {property_value->is_explicit() ? PropertyReadStatus::explicit_value : PropertyReadStatus::proven_default,
                    PropertyValue{property_value->value()}};
            }
        }, *position_property);
    }
    if (const PropertyEntry* entry = properties->find(descriptor->id)) {
        return {PropertyReadStatus::explicit_value, entry->value};
    }
    if (descriptor->owner == metamodel::DescriptorOwner::control &&
        descriptor->control_kind == ControlKind::panel && descriptor->api_name == "ShowTabs") {
        const auto* scroll = properties->find(PropertyId::from_name("ScrollPageMode"));
        if (scroll && std::get_if<bool>(&scroll->value) && std::get<bool>(scroll->value)) {
            return {PropertyReadStatus::computed_value, PropertyValue{UndefinedValue{}}};
        }
    }
    if (descriptor->owner == metamodel::DescriptorOwner::form && descriptor->api_name == "CaptionPicture")
        return {PropertyReadStatus::proven_default, PropertyValue{PictureRef{}}};
    if (descriptor->default_value.kind == metamodel::DefaultKind::none) {
        return {PropertyReadStatus::no_default, std::nullopt};
    }
    const auto value = materialize_default(*descriptor);
    if (!value || !property_value_matches(*descriptor, *value)) {
        return {PropertyReadStatus::unknown_default, std::nullopt};
    }
    return {PropertyReadStatus::proven_default, value};
}

PropertyMutationStatus OrdinaryFormDocument::set_prop_val(
    ObjectKey owner,
    PropertyId property,
    PropertyValue value) {
    if (owner.chart_series || owner.chart_trendline || owner.chart_equation_area) {
        if (owner.category != ObjectCategory::control || !owner.chart_series || owner.chart_area != ChartAreaKind::none || owner.chart_scale != ChartScaleKind::none || (owner.chart_equation_area && !owner.chart_trendline)) return PropertyMutationStatus::unknown_owner;
        const auto location = find_location(ObjectCategory::control, owner.id);
        if (!location || collections_.controls[location->index].kind() != ControlKind::chart) return PropertyMutationStatus::unknown_owner;
        auto object = chart_series_object(std::get<ChartPayload>(collections_.controls[location->index].payload), owner.chart_series);
        if (!object) return PropertyMutationStatus::unknown_owner;
        if (owner.chart_trendline) {
            auto* trend = std::visit([&](auto* item) -> ChartTrendline* {
                const auto found = std::ranges::find(item->trendlines, owner.chart_trendline, &ChartTrendline::id);
                return found == item->trendlines.end() ? nullptr : &*found;
            }, *object);
            if (!trend) return PropertyMutationStatus::unknown_owner;
            if (owner.chart_equation_area) {
                for (const auto& field : metamodel::chart_label_area_property_descriptors()) if (field.id == property) {
                    if (!normalize_chart_scale_property_value(field, value)) return PropertyMutationStatus::invalid_value;
                    trend->equation_area.set_explicit(property, std::move(value));
                    return PropertyMutationStatus::applied;
                }
            } else for (const auto& field : metamodel::chart_trendline_property_descriptors()) if (field.id == property) {
                if (field.value_codec == metamodel::ValueCodec::chart_equation_area) {
                    if (std::holds_alternative<UndefinedValue>(value)) return PropertyMutationStatus::applied;
                    const auto* reference = std::get_if<ChartEquationAreaRef>(&value);
                    if (!reference) return PropertyMutationStatus::invalid_value;
                    if (!chart_trendline_index(ChartTrendlineRef{reference->owner, reference->series, reference->trendline})) return PropertyMutationStatus::invalid_reference;
                    return PropertyMutationStatus::applied;
                }
                if (!normalize_chart_series_property_value(field, value)) return PropertyMutationStatus::invalid_value;
                trend->properties.set_explicit(property, std::move(value));
                return PropertyMutationStatus::applied;
            }
            return PropertyMutationStatus::unknown_property;
        }
        for (const auto& field : metamodel::chart_series_property_descriptors()) if (field.id == property) {
            if (field.read_only) return PropertyMutationStatus::read_only;
            if (field.name == "Indicator") {
                const auto* boolean = std::get_if<bool>(&value);
                if (!boolean) return PropertyMutationStatus::invalid_value;
                const auto fields = metamodel::chart_series_property_descriptors();
                const auto graph = std::ranges::find(fields, std::string_view{"GraphicalRepresentationType"}, &metamodel::ChartSeriesPropertyDescriptor::name);
                metamodel::set_chart_series_property_value(*object, *graph, EnumerationValue{"ChartSeriesGraphicalRepresentationType", *boolean ? "Line" : "Auto"});
                return PropertyMutationStatus::applied;
            }
            if (!normalize_chart_series_property_value(field, value)) return PropertyMutationStatus::invalid_value;
            metamodel::set_chart_series_property_value(*object, field, std::move(value));
            return field.normal_runtime_only && owner.chart_series != ObjectId{1} ? PropertyMutationStatus::applied_runtime_only : PropertyMutationStatus::applied;
        }
        return PropertyMutationStatus::unknown_property;
    }
    if (owner.chart_scale != ChartScaleKind::none) {
        const auto owners = metamodel::chart_scale_owner_descriptors();
        const auto scale_owner = std::ranges::find(owners, owner.chart_scale, &metamodel::ChartScaleOwnerDescriptor::kind);
        const auto* scale_control = find_control(owner.id);
        if (owner.category != ObjectCategory::control || (owner.chart_area != ChartAreaKind::plot && owner.chart_area != ChartAreaKind::title) || scale_owner == owners.end() || !scale_control || scale_control->kind() != ControlKind::chart) return PropertyMutationStatus::unknown_owner;
        if (owner.chart_area == ChartAreaKind::plot && property == PropertyId::from_name("TitleArea")) return PropertyMutationStatus::read_only;
        for (const auto& field : owner.chart_area == ChartAreaKind::title ? metamodel::chart_label_area_property_descriptors() : metamodel::chart_scale_property_descriptors()) if (field.id == property) {
            if (!normalize_chart_scale_property_value(field, value)) return PropertyMutationStatus::invalid_value;
            const auto location = find_location(ObjectCategory::control, owner.id);
            auto& plot = std::get<ChartPayload>(collections_.controls[location->index].payload).plot_area;
            if (!plot) plot = default_chart_area(ChartAreaKind::plot, true);
            metamodel::set_chart_scale_property_value((*plot).*(scale_owner->member), field, std::move(value));
            return PropertyMutationStatus::applied_unclassified_storage;
        }
        return PropertyMutationStatus::unknown_property;
    }
    if (owner.chart_area != ChartAreaKind::none) {
        if (owner.category != ObjectCategory::control ||
            (owner.chart_area != ChartAreaKind::plot && owner.chart_area != ChartAreaKind::legend && owner.chart_area != ChartAreaKind::title))
            return PropertyMutationStatus::unknown_owner;
        const auto location = find_location(ObjectCategory::control, owner.id);
        if (!location) return PropertyMutationStatus::unknown_owner;
        auto& control = collections_.controls[location->index];
        if (control.kind() != ControlKind::chart) return PropertyMutationStatus::unknown_owner;
        auto& chart = std::get<ChartPayload>(control.payload);
        if (owner.chart_area == ChartAreaKind::plot)
            for (const auto& scale : metamodel::chart_scale_owner_descriptors()) if (scale.id == property)
                return PropertyMutationStatus::read_only;
        auto& stored = owner.chart_area == ChartAreaKind::plot ? chart.plot_area : owner.chart_area == ChartAreaKind::title ? chart.title_area : chart.legend_area;
        ChartArea updated = stored ? *stored : default_chart_area(owner.chart_area, chart_area_visible(chart, owner.chart_area));
        if (owner.chart_area == ChartAreaKind::title && property == PropertyId::from_name("Text"))
            return set_prop_val({ObjectCategory::control, owner.id}, PropertyId::from_name("Title"), std::move(value));
        if (property == PropertyId::from_name("Placement")) {
            const auto* mode = std::get_if<EnumerationValue>(&value);
            if (mode == nullptr) return PropertyMutationStatus::invalid_value;
            const auto placements = metamodel::chart_area_placements(owner.chart_area);
            const auto expected_type = owner.chart_area == ChartAreaKind::plot ? "ChartPlotAreaPlacement" : owner.chart_area == ChartAreaKind::title ? "ChartTitleAreaPlacement" : "ChartLegendPlacement";
            if (mode->type_name != expected_type || std::ranges::find(placements, mode->member) == placements.end())
                return PropertyMutationStatus::invalid_value;
            updated.placement = mode->member;
            updated.explicit_geometry[0] = true;
        } else if (const auto* descriptor = metamodel::find_chart_area_boolean(owner.chart_area, property)) {
            const auto* boolean = std::get_if<bool>(&value);
            if (boolean == nullptr) return PropertyMutationStatus::invalid_value;
            updated.*(descriptor->member) = *boolean;
        } else if (const auto* descriptor = metamodel::find_chart_area_color(owner.chart_area, property)) {
            const auto* color = std::get_if<ColorValue>(&value);
            if (color == nullptr || !valid_chart_style(metamodel::ValueCodec::color, value)) return PropertyMutationStatus::invalid_value;
            updated.*(descriptor->member) = *color;
        } else if (const auto* descriptor = metamodel::find_chart_area_style(owner.chart_area, property)) {
            if (descriptor->border) {
                const auto* border = std::get_if<BorderValue>(&value);
                if (border == nullptr || !valid_border_value(*border)) return PropertyMutationStatus::invalid_value;
                updated.*(descriptor->border) = *border;
            } else if (descriptor->font) {
                const auto* font = std::get_if<FontValue>(&value);
                if (font == nullptr) return PropertyMutationStatus::invalid_value;
                if (!valid_chart_style(metamodel::ValueCodec::font, value)) return PropertyMutationStatus::invalid_value;
                updated.*(descriptor->font) = *font;
            } else {
                const auto* line = std::get_if<LineValue>(&value);
                if (!line) return PropertyMutationStatus::invalid_value;
                try { static_cast<void>(storage::value_codec::encode_line(*line)); }
                catch (const std::exception&) { return PropertyMutationStatus::invalid_value; }
                updated.*(descriptor->line) = *line;
            }
        } else if (owner.chart_area == ChartAreaKind::plot && metamodel::find_chart_plot_format(property) != nullptr) {
            const auto* descriptor = metamodel::find_chart_plot_format(property);
            if (descriptor->text) {
                if (!std::holds_alternative<std::string>(value) &&
                    !(std::holds_alternative<LocalizedStringValue>(value) && valid_localized_string(std::get<LocalizedStringValue>(value))))
                    return PropertyMutationStatus::invalid_value;
                updated.*(descriptor->text) = value;
            } else {
                const auto* enumeration = std::get_if<EnumerationValue>(&value);
                if (enumeration == nullptr || enumeration->type_name != descriptor->enumeration_type ||
                    std::ranges::find(descriptor->members, enumeration->member) == descriptor->members.end())
                    return PropertyMutationStatus::invalid_value;
                updated.*(descriptor->enumeration) = *enumeration;
            }
        } else if (chart_area_coordinate(updated, property) != nullptr) {
            std::string canonical;
            if (const auto* decimal = std::get_if<DecimalValue>(&value)) {
                try { canonical = oof::storage::value_codec::canonical_decimal(decimal->canonical); }
                catch (const std::exception&) { return PropertyMutationStatus::invalid_value; }
            } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
                canonical = std::to_string(*integer);
            } else return PropertyMutationStatus::invalid_value;
            if (canonical != "0" && canonical != "1" && !canonical.starts_with("0."))
                return PropertyMutationStatus::invalid_value;
            const auto* descriptor = metamodel::find_chart_area_property(property);
            updated.*(descriptor->coordinate) = DecimalValue{std::move(canonical)};
            updated.explicit_geometry[1 + descriptor->coordinate_offset] = true;
        } else return PropertyMutationStatus::unknown_property;
        if (owner.chart_area == ChartAreaKind::legend && property == PropertyId::from_name("Placement")) {
            chart.properties.set_explicit(PropertyId::from_name("ShowLegend"), updated.placement != "None");
        }
        if (owner.chart_area == ChartAreaKind::title && property == PropertyId::from_name("Placement"))
            chart.properties.set_explicit(PropertyId::from_name("ShowTitle"), updated.placement != "None");
        stored = std::move(updated);
        return PropertyMutationStatus::applied_unclassified_storage;
    }
    const metamodel::PropertyDescriptor* descriptor = nullptr;
    PropertySet* properties = nullptr;
    ControlNode* position_control = nullptr;
    ControlNode* property_control = nullptr;
    if (owner.category == ObjectCategory::form) {
        if (owner.id != form_.id) return PropertyMutationStatus::unknown_owner;
        if (property == PropertyId::from_name("CurrentControl") || property == PropertyId::from_name("FormOwner")) return PropertyMutationStatus::unsupported_surface;
        if (property == PropertyId::from_name("Controls")) return PropertyMutationStatus::read_only;
        if (property == PropertyId::from_name("UniqueKey") || property == PropertyId::from_name("ChoiceInitialValue")) {
            if (const auto* decimal = std::get_if<DecimalValue>(&value)) {
                if (!canonical_chart_decimal(decimal->canonical)) return PropertyMutationStatus::invalid_value;
            } else if (const auto* date = std::get_if<DateValue>(&value)) {
                try { static_cast<void>(storage::value_codec::date_to_platform(date->canonical)); }
                catch (const std::exception&) { return PropertyMutationStatus::invalid_value; }
            } else if (const auto* enumeration = std::get_if<EnumerationValue>(&value)) {
                if (enumeration->type_name.empty() || enumeration->member.empty()) return PropertyMutationStatus::invalid_value;
            } else if (!std::holds_alternative<UndefinedValue>(value) && !std::holds_alternative<NullValue>(value) &&
                       !std::holds_alternative<bool>(value) && !std::holds_alternative<std::int64_t>(value) &&
                       !std::holds_alternative<std::string>(value)) return PropertyMutationStatus::unsupported_surface;
            runtime_form_values_.set_explicit(property, std::move(value));
            return PropertyMutationStatus::applied_runtime_only;
        }
        if (property == PropertyId::from_name("WindowOptionsName") || property == PropertyId::from_name("WindowOptionsKey")) {
            if (std::holds_alternative<UndefinedValue>(value) || std::holds_alternative<NullValue>(value)) value = std::string{};
            if (!std::holds_alternative<std::string>(value)) return PropertyMutationStatus::unsupported_surface;
            runtime_form_values_.set_explicit(PropertyId::from_name("WindowOptionsName"), std::move(value));
            return PropertyMutationStatus::applied_runtime_only;
        }
        if (property == PropertyId::from_name("ReadOnly") || property == PropertyId::from_name("MultipleChoice")) {
            if (!std::holds_alternative<bool>(value)) return PropertyMutationStatus::invalid_value;
            runtime_form_values_.set_explicit(property, std::move(value));
            return PropertyMutationStatus::applied_runtime_only;
        }
        if (opening_started_ && (property == PropertyId::from_name("WindowState") ||
                                  property == PropertyId::from_name("WindowAppearanceMode") ||
                                  property == PropertyId::from_name("ChangeWindowAppearanceMode") ||
                                  property == PropertyId::from_name("WindowLocation") ||
                                  property == PropertyId::from_name("WindowDockLocation") ||
                                  property == PropertyId::from_name("ConnectableWindow") ||
                                  property == PropertyId::from_name("AllowNormalState") ||
                                  property == PropertyId::from_name("AllowDockedState") ||
                                  property == PropertyId::from_name("AllowAutohideState") ||
                                  property == PropertyId::from_name("AllowFloatState") ||
                                  property == PropertyId::from_name("AllowJoinWindow")))
            return PropertyMutationStatus::forbidden_after_opening_started;
        const bool is_permission = property == PropertyId::from_name("AllowNormalState") ||
            property == PropertyId::from_name("AllowDockedState") || property == PropertyId::from_name("AllowAutohideState") ||
            property == PropertyId::from_name("AllowFloatState") || property == PropertyId::from_name("AllowJoinWindow");
        const bool is_state = property == PropertyId::from_name("WindowState");
        const bool is_connectable = property == PropertyId::from_name("ConnectableWindow");
        if (is_permission || is_state || is_connectable) {
            if (is_state) {
                const auto* v = std::get_if<EnumerationValue>(&value);
                if (!v || v->type_name != "WindowStateVariant" || (v->member != "Normal" && v->member != "Docked" &&
                    v->member != "Autohide" && v->member != "Float")) return PropertyMutationStatus::invalid_value;
            } else if (!std::holds_alternative<bool>(value)) return PropertyMutationStatus::invalid_value;
            if (window_permissions_.empty()) {
                for (const auto name : {"AllowNormalState", "AllowDockedState", "AllowAutohideState", "AllowFloatState", "AllowJoinWindow"}) {
                    const auto id = PropertyId::from_name(name);
                    const auto initial = get_prop_val(owner, id);
                    if (!initial.value || !std::holds_alternative<bool>(*initial.value)) {
                        window_permissions_.clear();
                        return PropertyMutationStatus::unsupported_surface;
                    }
                    window_permissions_.set_explicit(id, *initial.value);
                }
            }
            if (is_permission) window_permissions_.set_explicit(property, value);
            else form_.properties.set_explicit(property, value);
            const auto state_result = get_prop_val(owner, PropertyId::from_name("WindowState"));
            const auto connect_result = get_prop_val(owner, PropertyId::from_name("ConnectableWindow"));
            if (!state_result.value || !connect_result.value) return PropertyMutationStatus::unsupported_surface;
            const auto* state = std::get_if<EnumerationValue>(&*state_result.value);
            const auto* connect = std::get_if<bool>(&*connect_result.value);
            if (!state || !connect) return PropertyMutationStatus::unsupported_surface;
            const auto state_permission = state->member == "Normal" ? "AllowNormalState" : state->member == "Docked" ? "AllowDockedState" :
                state->member == "Autohide" ? "AllowAutohideState" : "AllowFloatState";
            const bool permitted = std::get<bool>(window_permissions_.find(PropertyId::from_name(state_permission))->value);
            const bool join = std::get<bool>(window_permissions_.find(PropertyId::from_name("AllowJoinWindow"))->value);
            if ((is_state || (is_permission && property != PropertyId::from_name("AllowJoinWindow"))) && !permitted)
                return PropertyMutationStatus::window_state_disallowed_after_assignment;
            if ((is_connectable || property == PropertyId::from_name("AllowJoinWindow")) && *connect && !join)
                return PropertyMutationStatus::window_join_disallowed_after_assignment;
            if ((is_state || is_connectable) && (state->member == "Docked" || state->member == "Autohide") && !*connect)
                return PropertyMutationStatus::window_requires_connectable_after_assignment;
            return is_permission ? PropertyMutationStatus::applied_runtime_only : PropertyMutationStatus::applied;
        }
        descriptor = metamodel::find_form_property(property);
        properties = &form_.properties;
    } else if (owner.category == ObjectCategory::control) {
        const auto location = find_location(ObjectCategory::control, owner.id);
        if (!location) return PropertyMutationStatus::unknown_owner;
        auto& control = collections_.controls[location->index];
        property_control = &control;
        if (control.kind() == ControlKind::table && property == PropertyId::from_name("Columns"))
            return PropertyMutationStatus::read_only;
        if ((control.kind() == ControlKind::input_field || control.kind() == ControlKind::choice_field) &&
            property == PropertyId::from_name("ChoiceList")) {
            const auto* reference = std::get_if<ValueListRef>(&value);
            if (!reference || !reference->get()) return PropertyMutationStatus::invalid_value;
            runtime_choice_lists_.insert_or_assign(control.id, *reference);
            return PropertyMutationStatus::applied_runtime_only;
        }
        if (control.kind() == ControlKind::chart &&
            (property == PropertyId::from_name("SeriesCount") || property == PropertyId::from_name("PointCount")))
            return resize_chart(owner.id, property, value);
        if (control.kind() == ControlKind::chart && property == PropertyId::from_name("ActiveSeries")) {
            const auto* ref = std::get_if<ChartSeriesRef>(&value);
            if (!ref) return PropertyMutationStatus::invalid_value;
            const auto& chart = std::get<ChartPayload>(control.payload);
            if (ref->owner != control.id || std::ranges::none_of(chart.series, [&](const auto& series) { return series.id == ref->series; }))
                return PropertyMutationStatus::invalid_reference;
            control.properties().set_explicit(property, value);
            return PropertyMutationStatus::applied;
        }
        descriptor = metamodel::find_property(control.kind(), property);
        if (descriptor == nullptr) return PropertyMutationStatus::unknown_property;
        if (control.kind() == ControlKind::list_box && descriptor->api_name == "CurrentRow")
            return PropertyMutationStatus::unsupported_surface;
        if (is_string_input_property(*descriptor)) {
            const auto* type = effective_input_type(*this, control);
            const auto* list_mode = control.properties().find(PropertyId::from_name("ListChoiceMode"));
            if (!type || type->entries.size() != 1 || type->entries.front().term != TypeDomainTerm::string ||
                (list_mode && std::get_if<bool>(&list_mode->value) && std::get<bool>(list_mode->value)))
                return PropertyMutationStatus::unsupported_surface;
        }
        if (control.kind() == ControlKind::panel && descriptor->api_name == "CurrentPage") {
            const auto* page_ref = std::get_if<PageRef>(&value);
            if (page_ref == nullptr) return PropertyMutationStatus::invalid_value;
            const auto* page = find_page(page_ref->id());
            if (page == nullptr) return PropertyMutationStatus::invalid_reference;
            const auto belongs = std::ranges::any_of(control.children, [&](const ChildItemRef& child) {
                const auto* candidate = std::get_if<PageRef>(&child);
                return candidate != nullptr && *candidate == *page_ref;
            });
            const auto* scroll = control.properties().find(PropertyId::from_name("ScrollPageMode"));
            if (!belongs || !page->visible.value() ||
                (scroll != nullptr && std::get_if<bool>(&scroll->value) && std::get<bool>(scroll->value)))
                return PropertyMutationStatus::applied_runtime_only;
            std::get<PanelPayload>(control.payload).current_page = *page_ref;
            return PropertyMutationStatus::applied_runtime_only;
        }
        if (descriptor->api_name == "ZOrder") {
            const auto group = tab_order_group(*this, control.id);
            if (!group) return PropertyMutationStatus::unsupported_surface;
            if (std::ranges::any_of(group->ids, [&](ObjectId id) {
                    return find_control(id)->position.z_order.is_explicit();
                })) return PropertyMutationStatus::unsupported_surface;
            std::int64_t requested = 0;
            if (const auto* integer = std::get_if<std::int64_t>(&value)) requested = *integer;
            else if (const auto* decimal = std::get_if<DecimalValue>(&value)) {
                if (!canonical_chart_decimal(decimal->canonical)) return PropertyMutationStatus::invalid_value;
                const auto whole = std::string_view(decimal->canonical).substr(0, decimal->canonical.find('.'));
                if (whole != "-" && !whole.empty()) {
                    const auto parsed = std::from_chars(whole.data(), whole.data() + whole.size(), requested);
                    if (parsed.ec == std::errc::result_out_of_range)
                        requested = whole.front() == '-' ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max();
                    else if (parsed.ec != std::errc{} || parsed.ptr != whole.data() + whole.size())
                        return PropertyMutationStatus::invalid_value;
                }
            } else return PropertyMutationStatus::invalid_value;
            const auto bounded = std::clamp(requested, std::int64_t{1}, static_cast<std::int64_t>(group->ids.size()));
            const auto target = static_cast<std::size_t>(bounded - 1);
            if (target == group->target_index) return PropertyMutationStatus::applied;
            std::vector<ChildItemRef>* siblings = nullptr;
            const auto consider = [&](std::vector<ChildItemRef>& children) {
                if (std::ranges::any_of(children, [&](const ChildItemRef& item) {
                        const auto* ref = std::get_if<ControlRef>(&item);
                        return ref != nullptr && ref->id() == control.id;
                    })) siblings = &children;
            };
            consider(form_.children);
            for (auto& page : collections_.pages) consider(page.children);
            for (auto& candidate : collections_.controls) consider(candidate.children);
            if (siblings && siblings->size() != group->ids.size()) siblings = nullptr;
            if (!siblings) {
                for (auto& panel : collections_.controls) {
                    if (panel.kind() != ControlKind::panel) continue;
                    const auto* separate = panel.properties().find(PropertyId::from_name("SeparateByPage"));
                    if (!separate || !std::get_if<bool>(&separate->value) || std::get<bool>(separate->value)) continue;
                    std::vector<Page*> pages;
                    std::vector<ChildItemRef> merged;
                    for (const auto& item : panel.children) {
                        const auto* page = std::get_if<PageRef>(&item);
                        const auto location = page ? find_location(ObjectCategory::page, page->id()) : std::nullopt;
                        if (!location) { pages.clear(); break; }
                        auto& owned = collections_.pages[location->index];
                        pages.push_back(&owned);
                        merged.insert(merged.end(), owned.children.begin(), owned.children.end());
                    }
                    if (pages.empty() || merged.size() != group->ids.size() ||
                        !std::equal(merged.begin(), merged.end(), group->ids.begin(), [](const auto& item, ObjectId id) {
                            const auto* reference = std::get_if<ControlRef>(&item);
                            return reference && reference->id() == id;
                        })) continue;
                    pages.front()->children = std::move(merged);
                    for (std::size_t index = 1; index < pages.size(); ++index) pages[index]->children.clear();
                    siblings = &pages.front()->children;
                    break;
                }
            }
            if (!siblings) return PropertyMutationStatus::unsupported_surface;
            if (target < group->target_index)
                std::rotate(siblings->begin() + static_cast<std::ptrdiff_t>(target),
                    siblings->begin() + static_cast<std::ptrdiff_t>(group->target_index),
                    siblings->begin() + static_cast<std::ptrdiff_t>(group->target_index + 1));
            else
                std::rotate(siblings->begin() + static_cast<std::ptrdiff_t>(group->target_index),
                    siblings->begin() + static_cast<std::ptrdiff_t>(group->target_index + 1),
                    siblings->begin() + static_cast<std::ptrdiff_t>(target + 1));
            for (std::size_t index = 0; index < siblings->size(); ++index) {
                const auto id = std::get<ControlRef>((*siblings)[index]).id();
                const auto old = std::ranges::find(group->ids, id);
                const auto ordinal = group->orders[static_cast<std::size_t>(old - group->ids.begin())];
                const auto sibling = find_location(ObjectCategory::control, id);
                auto& tab = collections_.controls[sibling->index].position.tab_order;
                if (ordinal == static_cast<std::int32_t>(index + 1)) tab.reset();
                else tab.set(std::optional<std::int32_t>{ordinal});
            }
            return PropertyMutationStatus::applied;
        }
        if (descriptor->api_name == "TabOrder") {
            const auto group = tab_order_group(*this, control.id);
            if (!group) return PropertyMutationStatus::unsupported_surface;
            std::int64_t requested = 0;
            if (const auto* integer = std::get_if<std::int64_t>(&value)) requested = *integer;
            else if (const auto* decimal = std::get_if<DecimalValue>(&value)) {
                if (!canonical_chart_decimal(decimal->canonical)) return PropertyMutationStatus::invalid_value;
                const auto dot = decimal->canonical.find('.');
                const auto whole = std::string_view(decimal->canonical).substr(0, dot);
                if (whole.empty() || whole == "-") requested = 0;
                else {
                    const auto parsed = std::from_chars(whole.data(), whole.data() + whole.size(), requested);
                    if (parsed.ec == std::errc::result_out_of_range) return PropertyMutationStatus::applied;
                    if (parsed.ec != std::errc{} || parsed.ptr != whole.data() + whole.size()) return PropertyMutationStatus::invalid_value;
                }
            } else return PropertyMutationStatus::invalid_value;
            if (requested < 1 || requested > static_cast<std::int64_t>(group->ids.size())) return PropertyMutationStatus::applied;
            const auto old_order = group->orders[group->target_index];
            const auto new_order = static_cast<std::int32_t>(requested);
            for (std::size_t index = 0; index < group->ids.size(); ++index) {
                auto order = group->orders[index];
                if (index == group->target_index) order = new_order;
                else if (old_order < new_order && order > old_order && order <= new_order) --order;
                else if (old_order > new_order && order >= new_order && order < old_order) ++order;
                if (order == group->orders[index]) continue;
                const auto sibling = find_location(ObjectCategory::control, group->ids[index]);
                auto& position = collections_.controls[sibling->index].position.tab_order;
                if (order == static_cast<std::int32_t>(index + 1)) position.reset();
                else position.set(std::optional<std::int32_t>{order});
            }
            return PropertyMutationStatus::applied;
        }
        if (control.kind() == ControlKind::input_field && descriptor->api_name == "TypeRestriction") {
            TypedValueBudget budget;
            if (!std::holds_alternative<TypeDomainPatternValue>(value) || !validate_typed_value(value, budget))
                return PropertyMutationStatus::invalid_value;
        }
        if (descriptor->api_name == "Data") return PropertyMutationStatus::unsupported_surface;
        if (descriptor->api_name == "ValueType") {
            if (control.data_path) return PropertyMutationStatus::read_only;
            if (control.kind() != ControlKind::choice_field) return PropertyMutationStatus::unsupported_surface;
            TypedValueBudget budget;
            if (!std::holds_alternative<TypeDomainPatternValue>(value) || !validate_typed_value(value, budget))
                return PropertyMutationStatus::invalid_value;
            return PropertyMutationStatus::applied;
        }
        if (control.kind() == ControlKind::table && descriptor->api_name == "Tree" &&
            !table_tree_property_available(*this, control)) return PropertyMutationStatus::unsupported_surface;
        if (is_typed_collection_property(*descriptor)) {
            return (descriptor->value_codec == metamodel::ValueCodec::panel_pages || descriptor->value_codec == metamodel::ValueCodec::chart_series_collection || descriptor->value_codec == metamodel::ValueCodec::chart_points_collection || descriptor->value_codec == metamodel::ValueCodec::chart_gauge_quality_bands ||
                descriptor->value_codec == metamodel::ValueCodec::chart_plot_area || descriptor->value_codec == metamodel::ValueCodec::chart_title_area || descriptor->value_codec == metamodel::ValueCodec::chart_legend_area || descriptor->api_name == "SummarySeries") ? PropertyMutationStatus::read_only : PropertyMutationStatus::unsupported_surface;
        }
        if (descriptor->surface == metamodel::PropertySurface::panel_placement) {
            position_control = &control;
        } else if (descriptor->surface == metamodel::PropertySurface::control_payload) {
            properties = &control.properties();
        } else if (descriptor->surface == metamodel::PropertySurface::control_extension) {
            properties = &control.extension_properties;
        } else {
            return PropertyMutationStatus::unsupported_surface;
        }
    } else {
        if (!find_location(owner.category, owner.id)) return PropertyMutationStatus::unknown_owner;
        return PropertyMutationStatus::unsupported_surface;
    }
    if (descriptor == nullptr || (properties == nullptr && position_control == nullptr)) return PropertyMutationStatus::unknown_property;
    if (is_typed_collection_property(*descriptor)) return PropertyMutationStatus::unsupported_surface;
    if (descriptor->api_access == metamodel::ApiAccess::read_only) {
        return PropertyMutationStatus::read_only;
    }
    if (descriptor->api_access == metamodel::ApiAccess::unknown) {
        return PropertyMutationStatus::unknown_access;
    }
    if (descriptor->persistence == metamodel::PersistenceClass::unsupported_by_platform ||
        descriptor->persistence == metamodel::PersistenceClass::version_specific) {
        return PropertyMutationStatus::unsupported_surface;
    }
    if (descriptor->value_codec == metamodel::ValueCodec::typed_value && property_control != nullptr) {
        const auto* type = effective_input_type(*this, *property_control);
        const auto* list_mode = property_control->properties().find(PropertyId::from_name("ListChoiceMode"));
        if (type == nullptr || type->entries.size() != 1 || type->entries.front().term != TypeDomainTerm::numeric ||
            (list_mode != nullptr && std::get_if<bool>(&list_mode->value) && std::get<bool>(list_mode->value)))
            return PropertyMutationStatus::unsupported_surface;
    }
    if (is_positive_int32_property(*descriptor)) {
        std::optional<std::int32_t> normalized;
        if (const auto* integer = std::get_if<std::int64_t>(&value)) normalized = normalize_positive_int32(std::to_string(*integer));
        else if (const auto* decimal = std::get_if<DecimalValue>(&value)) normalized = normalize_positive_int32(decimal->canonical);
        if (!normalized) return PropertyMutationStatus::invalid_value;
        value = DecimalValue{std::to_string(*normalized)};
    }
    if (!property_value_matches(*descriptor, value)) return PropertyMutationStatus::invalid_value;
    if (property_control != nullptr && property_control->kind() == ControlKind::panel &&
        descriptor->api_name == "ScrollPageMode" && !std::get<bool>(value)) {
        const auto* current = property_control->properties().find(descriptor->id);
        if (current != nullptr && std::get_if<bool>(&current->value) && std::get<bool>(current->value))
            return PropertyMutationStatus::applied;
    }
    if (const auto* enumeration = std::get_if<EnumerationValue>(&value);
        enumeration != nullptr && !property_api_enumeration_matches(*descriptor, *enumeration)) {
        return PropertyMutationStatus::invalid_value;
    }
    if (descriptor->owner == metamodel::DescriptorOwner::form && descriptor->api_name == "CaptionPicture") {
        const auto& picture = std::get<PictureRef>(value);
        if (picture.asset.id()) return PropertyMutationStatus::invalid_value;
        if (picture.standard_name && !property_value_references_resolve(*this, value))
            return PropertyMutationStatus::invalid_reference;
        ObjectId old_asset{};
        if (const auto* old = properties->find(descriptor->id))
            if (const auto* old_picture = std::get_if<PictureRef>(&old->value)) old_asset = old_picture->asset.id();
        if (picture.standard_name) properties->set_explicit(descriptor->id, std::move(value));
        else properties->unset(descriptor->id);
        remove_unreferenced_caption_asset(old_asset);
        return PropertyMutationStatus::applied;
    }
    if (!property_value_references_resolve(*this, value)) {
        return PropertyMutationStatus::invalid_reference;
    }
    if (descriptor->value_codec == metamodel::ValueCodec::context_menu_reference) {
        if (const auto* target = std::get_if<ControlRef>(&value)) {
            const auto* control = find_control(target->id());
            if (control == nullptr || control->kind() != ControlKind::command_bar)
                return PropertyMutationStatus::invalid_reference;
        }
    }
    if (position_control != nullptr) {
        if (descriptor->api_name == "DefaultControl") {
            const auto group = placement_control_group(*this, position_control->id);
            if (!group) return PropertyMutationStatus::unsupported_surface;
            if (std::get<bool>(value)) {
                for (const auto id : *group) {
                    const auto location = find_location(ObjectCategory::control, id);
                    collections_.controls[location->index].position.default_control.reset();
                }
                position_control->position.default_control.set(true);
            } else position_control->position.default_control.reset();
            return PropertyMutationStatus::applied;
        }
        if (descriptor->api_name == "Collapse") {
            const auto& collapse = std::get<EnumerationValue>(value);
            position_control->position.collapse.set(collapse);
            return PropertyMutationStatus::applied_runtime_only;
        }
        const auto position_property = find_position_property(position_control->position, descriptor->api_name);
        if (!position_property) return PropertyMutationStatus::unsupported_surface;
        const bool nonnegative_dimension = descriptor->api_name == "Width" || descriptor->api_name == "Height";
        const bool applied = std::visit([&](auto* property_value) {
            using Value = std::remove_cvref_t<decltype(property_value->value())>;
            if constexpr (std::is_same_v<Value, std::int32_t>) {
                const auto* integer = std::get_if<std::int64_t>(&value);
                if (integer == nullptr || *integer < std::numeric_limits<std::int32_t>::min() ||
                    *integer > std::numeric_limits<std::int32_t>::max() ||
                    (nonnegative_dimension && *integer < 0)) return false;
                property_value->set(static_cast<std::int32_t>(*integer));
            } else {
                const auto* boolean = std::get_if<bool>(&value);
                if (boolean == nullptr) return false;
                property_value->set(*boolean);
            }
            return true;
        }, *position_property);
        return applied ? PropertyMutationStatus::applied : PropertyMutationStatus::invalid_value;
    }
    if (descriptor->api_name == "ActionSource" &&
        descriptor->control_kind == ControlKind::command_bar) {
        if (const auto* target = std::get_if<ControlRef>(&value)) {
            const ControlNode* source_control = find_control(target->id());
            if (source_control != nullptr && source_control->kind() != ControlKind::table &&
                source_control->kind() != ControlKind::html_document_field) {
                return PropertyMutationStatus::invalid_reference;
            }
        }
    }

    if (owner.category == ObjectCategory::control && descriptor->control_kind == ControlKind::input_field &&
        descriptor->api_name == "Mask" && !std::get<std::string>(value).empty())
        properties->set_explicit(PropertyId::from_name("Wrap"), false);
    if (owner.category == ObjectCategory::control && descriptor->control_kind == ControlKind::input_field &&
        descriptor->api_name == "ListChoiceMode") {
        properties->set_explicit(PropertyId::from_name("ChoiceListButton"), std::get<bool>(value));
    }
    if (property_control != nullptr && descriptor->control_kind == ControlKind::chart &&
        descriptor->api_name == "ShowLegend") {
        auto& chart = std::get<ChartPayload>(property_control->payload);
        if (chart.legend_area) {
            chart.legend_area->placement = std::get<bool>(value) ? "Auto" : "None";
            chart.legend_area->explicit_geometry[0] = false;
        }
    }
    if (property_control != nullptr && descriptor->control_kind == ControlKind::chart && descriptor->api_name == "ShowTitle") {
        auto& chart = std::get<ChartPayload>(property_control->payload);
        if (chart.title_area) {
            chart.title_area->placement = std::get<bool>(value) ? "Auto" : "None";
            chart.title_area->explicit_geometry[0] = false;
        }
    }
    if (property_control != nullptr && property_control->kind() == ControlKind::button &&
        descriptor->api_name == "DefaultButton") {
        const bool selected = std::get<bool>(value);
        // Assign first: allocation failure must not clear the existing selection.
        properties->set_explicit(descriptor->id, selected);
        if (selected) {
            for (auto& control : collections_.controls) {
                if (control.kind() == ControlKind::button && control.id != owner.id) {
                    control.extension_properties.unset(descriptor->id);
                }
                if (auto* bar = std::get_if<CommandBarPayload>(&control.payload)) {
                    const auto clear_default = [&](const auto& self, auto& buttons) -> void {
                        for (auto& button : buttons) {
                            button.default_button = false;
                            self(self, button.buttons);
                        }
                    };
                    clear_default(clear_default, bar->buttons);
                }
            }
        }
        return PropertyMutationStatus::applied;
    }
    properties->set_explicit(descriptor->id, std::move(value));
    if (descriptor->persistence == metamodel::PersistenceClass::runtime_only) {
        return PropertyMutationStatus::applied_runtime_only;
    }
    if (descriptor->persistence == metamodel::PersistenceClass::unclassified ||
        descriptor->storage_codec == metamodel::StorageCodec::unclassified ||
        descriptor->storage_codec == metamodel::StorageCodec::none) {
        return PropertyMutationStatus::applied_unclassified_storage;
    }
    return PropertyMutationStatus::applied;
}

PropertyMutationStatus OrdinaryFormDocument::reset_prop_val(ObjectKey owner, PropertyId property) {
    if (owner.chart_series || owner.chart_trendline || owner.chart_equation_area) {
        if (owner.category != ObjectCategory::control || !owner.chart_series || owner.chart_area != ChartAreaKind::none || owner.chart_scale != ChartScaleKind::none || (owner.chart_equation_area && !owner.chart_trendline)) return PropertyMutationStatus::unknown_owner;
        const auto location = find_location(ObjectCategory::control, owner.id);
        if (!location || collections_.controls[location->index].kind() != ControlKind::chart) return PropertyMutationStatus::unknown_owner;
        auto object = chart_series_object(std::get<ChartPayload>(collections_.controls[location->index].payload), owner.chart_series);
        if (!object) return PropertyMutationStatus::unknown_owner;
        if (owner.chart_trendline) {
            auto* trend = std::visit([&](auto* item) -> ChartTrendline* {
                const auto found = std::ranges::find(item->trendlines, owner.chart_trendline, &ChartTrendline::id);
                return found == item->trendlines.end() ? nullptr : &*found;
            }, *object);
            if (!trend) return PropertyMutationStatus::unknown_owner;
            if (owner.chart_equation_area) {
                for (const auto& field : metamodel::chart_label_area_property_descriptors()) if (field.id == property) {
                    trend->equation_area.unset(property);
                    return PropertyMutationStatus::reset;
                }
            } else for (const auto& field : metamodel::chart_trendline_property_descriptors()) if (field.id == property) {
                if (field.value_codec == metamodel::ValueCodec::chart_equation_area) return PropertyMutationStatus::reset;
                trend->properties.unset(property);
                return PropertyMutationStatus::reset;
            }
            return PropertyMutationStatus::unknown_property;
        }
        for (const auto& field : metamodel::chart_series_property_descriptors()) if (field.id == property) {
            if (field.read_only) return PropertyMutationStatus::read_only;
            if (field.name == "Indicator") {
                const auto fields = metamodel::chart_series_property_descriptors();
                const auto graph = std::ranges::find(fields, std::string_view{"GraphicalRepresentationType"}, &metamodel::ChartSeriesPropertyDescriptor::name);
                metamodel::reset_chart_series_property_value(*object, *graph);
                return PropertyMutationStatus::reset;
            }
            metamodel::reset_chart_series_property_value(*object, field);
            return field.normal_runtime_only && owner.chart_series != ObjectId{1} ? PropertyMutationStatus::reset_runtime_only : PropertyMutationStatus::reset;
        }
        return PropertyMutationStatus::unknown_property;
    }
    if (owner.chart_scale != ChartScaleKind::none) {
        const auto owners = metamodel::chart_scale_owner_descriptors();
        const auto scale_owner = std::ranges::find(owners, owner.chart_scale, &metamodel::ChartScaleOwnerDescriptor::kind);
        const auto* scale_control = find_control(owner.id);
        if (owner.category != ObjectCategory::control || (owner.chart_area != ChartAreaKind::plot && owner.chart_area != ChartAreaKind::title) || scale_owner == owners.end() || !scale_control || scale_control->kind() != ControlKind::chart) return PropertyMutationStatus::unknown_owner;
        if (owner.chart_area == ChartAreaKind::plot && property == PropertyId::from_name("TitleArea")) return PropertyMutationStatus::read_only;
        for (const auto& field : owner.chart_area == ChartAreaKind::title ? metamodel::chart_label_area_property_descriptors() : metamodel::chart_scale_property_descriptors()) if (field.id == property) {
            const auto location = find_location(ObjectCategory::control, owner.id);
            auto& plot = std::get<ChartPayload>(collections_.controls[location->index].payload).plot_area;
            if (plot) metamodel::reset_chart_scale_property_value((*plot).*(scale_owner->member), field);
            return PropertyMutationStatus::reset_unclassified_storage;
        }
        return PropertyMutationStatus::unknown_property;
    }
    if (owner.chart_area != ChartAreaKind::none) {
        if (owner.category != ObjectCategory::control ||
            (owner.chart_area != ChartAreaKind::plot && owner.chart_area != ChartAreaKind::legend && owner.chart_area != ChartAreaKind::title))
            return PropertyMutationStatus::unknown_owner;
        const auto* control = find_control(owner.id);
        if (control == nullptr || control->kind() != ControlKind::chart) return PropertyMutationStatus::unknown_owner;
        if (owner.chart_area == ChartAreaKind::plot)
            for (const auto& scale : metamodel::chart_scale_owner_descriptors()) if (scale.id == property)
                return PropertyMutationStatus::read_only;
        if (owner.chart_area == ChartAreaKind::title && property == PropertyId::from_name("Text"))
            return reset_prop_val({ObjectCategory::control, owner.id}, PropertyId::from_name("Title"));
        if (const auto* descriptor = metamodel::find_chart_area_boolean(owner.chart_area, property)) {
            const auto location = find_location(ObjectCategory::control, owner.id);
            auto& chart = std::get<ChartPayload>(collections_.controls[location->index].payload);
            auto& area = owner.chart_area == ChartAreaKind::plot ? chart.plot_area : owner.chart_area == ChartAreaKind::title ? chart.title_area : chart.legend_area;
            if (area) ((*area).*(descriptor->member)).reset();
            return PropertyMutationStatus::reset_unclassified_storage;
        }
        if (const auto* descriptor = metamodel::find_chart_area_color(owner.chart_area, property)) {
            const auto location = find_location(ObjectCategory::control, owner.id);
            auto& chart = std::get<ChartPayload>(collections_.controls[location->index].payload);
            auto& area = owner.chart_area == ChartAreaKind::plot ? chart.plot_area : owner.chart_area == ChartAreaKind::title ? chart.title_area : chart.legend_area;
            if (area) ((*area).*(descriptor->member)).reset();
            return PropertyMutationStatus::reset_unclassified_storage;
        }
        if (const auto* descriptor = metamodel::find_chart_area_style(owner.chart_area, property)) {
            const auto location = find_location(ObjectCategory::control, owner.id);
            auto& chart = std::get<ChartPayload>(collections_.controls[location->index].payload);
            auto& area = owner.chart_area == ChartAreaKind::plot ? chart.plot_area : owner.chart_area == ChartAreaKind::title ? chart.title_area : chart.legend_area;
            if (area && descriptor->border) ((*area).*(descriptor->border)).reset();
            if (area && descriptor->font) ((*area).*(descriptor->font)).reset();
            if (area && descriptor->line) ((*area).*(descriptor->line)).reset();
            return PropertyMutationStatus::reset_unclassified_storage;
        }
        if (owner.chart_area == ChartAreaKind::plot) {
            if (const auto* descriptor = metamodel::find_chart_plot_format(property)) {
                const auto location = find_location(ObjectCategory::control, owner.id);
                auto& area = std::get<ChartPayload>(collections_.controls[location->index].payload).plot_area;
                if (area && descriptor->text) ((*area).*(descriptor->text)).reset();
                if (area && descriptor->enumeration) ((*area).*(descriptor->enumeration)).reset();
                return PropertyMutationStatus::reset_unclassified_storage;
            }
        }
        const auto defaults = default_chart_area(owner.chart_area, true);
        PropertyValue value;
        if (property == PropertyId::from_name("Placement"))
            value = EnumerationValue{owner.chart_area == ChartAreaKind::plot ? "ChartPlotAreaPlacement" : owner.chart_area == ChartAreaKind::title ? "ChartTitleAreaPlacement" : "ChartLegendPlacement", "Auto"};
        else if (const auto* coordinate = chart_area_coordinate(defaults, property)) value = *coordinate;
        else return PropertyMutationStatus::unknown_property;
        const auto result = set_prop_val(owner, property, std::move(value));
        if (result == PropertyMutationStatus::applied_unclassified_storage) {
            const auto location = find_location(ObjectCategory::control, owner.id);
            auto& chart = std::get<ChartPayload>(collections_.controls[location->index].payload);
            auto& area = owner.chart_area == ChartAreaKind::plot ? chart.plot_area : owner.chart_area == ChartAreaKind::title ? chart.title_area : chart.legend_area;
            const auto* descriptor = metamodel::find_chart_area_property(property);
            area->explicit_geometry[descriptor->coordinate == nullptr ? 0 : 1 + descriptor->coordinate_offset] = false;
            return PropertyMutationStatus::reset_unclassified_storage;
        }
        return result;
    }
    const metamodel::PropertyDescriptor* descriptor = nullptr;
    PropertySet* properties = nullptr;
    ControlNode* position_control = nullptr;
    ControlNode* property_control = nullptr;
    if (owner.category == ObjectCategory::form) {
        if (owner.id != form_.id) return PropertyMutationStatus::unknown_owner;
        if (property == PropertyId::from_name("CurrentControl") || property == PropertyId::from_name("FormOwner")) return PropertyMutationStatus::unsupported_surface;
        if (property == PropertyId::from_name("Controls")) return PropertyMutationStatus::read_only;
        if (property == PropertyId::from_name("UniqueKey") || property == PropertyId::from_name("ChoiceInitialValue")) {
            runtime_form_values_.unset(property);
            return PropertyMutationStatus::reset_runtime_only;
        }
        if (property == PropertyId::from_name("WindowOptionsName") || property == PropertyId::from_name("WindowOptionsKey")) {
            runtime_form_values_.unset(PropertyId::from_name("WindowOptionsName"));
            return PropertyMutationStatus::reset_runtime_only;
        }
        if (property == PropertyId::from_name("ReadOnly") || property == PropertyId::from_name("MultipleChoice")) {
            runtime_form_values_.unset(property);
            return PropertyMutationStatus::reset_runtime_only;
        }
        if (opening_started_ && (property == PropertyId::from_name("WindowState") ||
                                  property == PropertyId::from_name("WindowAppearanceMode") ||
                                  property == PropertyId::from_name("ChangeWindowAppearanceMode") ||
                                  property == PropertyId::from_name("WindowLocation") ||
                                  property == PropertyId::from_name("WindowDockLocation") ||
                                  property == PropertyId::from_name("ConnectableWindow") ||
                                  property == PropertyId::from_name("AllowNormalState") ||
                                  property == PropertyId::from_name("AllowDockedState") ||
                                  property == PropertyId::from_name("AllowAutohideState") ||
                                  property == PropertyId::from_name("AllowFloatState") ||
                                  property == PropertyId::from_name("AllowJoinWindow")))
            return PropertyMutationStatus::forbidden_after_opening_started;
        if (property == PropertyId::from_name("WindowState") || property == PropertyId::from_name("ConnectableWindow") ||
            property == PropertyId::from_name("AllowNormalState") || property == PropertyId::from_name("AllowDockedState") ||
            property == PropertyId::from_name("AllowAutohideState") || property == PropertyId::from_name("AllowFloatState") ||
            property == PropertyId::from_name("AllowJoinWindow")) return PropertyMutationStatus::unsupported_surface;
        descriptor = metamodel::find_form_property(property);
        properties = &form_.properties;
    } else if (owner.category == ObjectCategory::control) {
        const auto location = find_location(ObjectCategory::control, owner.id);
        if (!location) return PropertyMutationStatus::unknown_owner;
        auto& control = collections_.controls[location->index];
        property_control = &control;
        if (control.kind() == ControlKind::table && property == PropertyId::from_name("Columns"))
            return PropertyMutationStatus::read_only;
        if ((control.kind() == ControlKind::input_field || control.kind() == ControlKind::choice_field) &&
            property == PropertyId::from_name("ChoiceList"))
            return PropertyMutationStatus::unsupported_surface;
        if (control.kind() == ControlKind::chart &&
            (property == PropertyId::from_name("SeriesCount") || property == PropertyId::from_name("PointCount"))) {
            const auto status = resize_chart(owner.id, property, std::int64_t{0});
            return status == PropertyMutationStatus::applied ? PropertyMutationStatus::reset : status;
        }
        descriptor = metamodel::find_property(control.kind(), property);
        if (descriptor == nullptr) return PropertyMutationStatus::unknown_property;
        if (control.kind() == ControlKind::list_box && descriptor->api_name == "CurrentRow")
            return PropertyMutationStatus::unsupported_surface;
        if (is_string_input_property(*descriptor)) {
            const auto* type = effective_input_type(*this, control);
            const auto* list_mode = control.properties().find(PropertyId::from_name("ListChoiceMode"));
            if (!type || type->entries.size() != 1 || type->entries.front().term != TypeDomainTerm::string ||
                (list_mode && std::get_if<bool>(&list_mode->value) && std::get<bool>(list_mode->value)))
                return PropertyMutationStatus::unsupported_surface;
        }
        if (control.kind() == ControlKind::panel && descriptor->api_name == "CurrentPage")
            return PropertyMutationStatus::unsupported_surface;
        if (descriptor->api_name == "TabOrder") {
            const auto group = tab_order_group(*this, control.id);
            if (!group) return PropertyMutationStatus::unsupported_surface;
            const auto status = set_prop_val(owner, property, static_cast<std::int64_t>(group->target_index + 1));
            return status == PropertyMutationStatus::applied ? PropertyMutationStatus::reset : status;
        }
        if (descriptor->api_name == "Data") return PropertyMutationStatus::unsupported_surface;
        if (descriptor->api_name == "ValueType")
            return control.data_path ? PropertyMutationStatus::read_only : PropertyMutationStatus::unsupported_surface;
        if (control.kind() == ControlKind::table && descriptor->api_name == "Tree" &&
            !table_tree_property_available(*this, control)) return PropertyMutationStatus::unsupported_surface;
        if (is_typed_collection_property(*descriptor)) {
            return (descriptor->value_codec == metamodel::ValueCodec::panel_pages || descriptor->value_codec == metamodel::ValueCodec::chart_series_collection || descriptor->value_codec == metamodel::ValueCodec::chart_points_collection || descriptor->value_codec == metamodel::ValueCodec::chart_gauge_quality_bands ||
                descriptor->value_codec == metamodel::ValueCodec::chart_plot_area || descriptor->value_codec == metamodel::ValueCodec::chart_title_area || descriptor->value_codec == metamodel::ValueCodec::chart_legend_area || descriptor->api_name == "SummarySeries") ? PropertyMutationStatus::read_only : PropertyMutationStatus::unsupported_surface;
        }
        if (descriptor->surface == metamodel::PropertySurface::panel_placement) {
            position_control = &control;
        } else if (descriptor->surface == metamodel::PropertySurface::control_payload) {
            properties = &control.properties();
        } else if (descriptor->surface == metamodel::PropertySurface::control_extension) {
            properties = &control.extension_properties;
        } else {
            return PropertyMutationStatus::unsupported_surface;
        }
    } else {
        if (!find_location(owner.category, owner.id)) return PropertyMutationStatus::unknown_owner;
        return PropertyMutationStatus::unsupported_surface;
    }
    if (descriptor == nullptr || (properties == nullptr && position_control == nullptr)) return PropertyMutationStatus::unknown_property;
    if (is_typed_collection_property(*descriptor)) return PropertyMutationStatus::unsupported_surface;
    if (descriptor->api_access == metamodel::ApiAccess::read_only) {
        return PropertyMutationStatus::read_only;
    }
    if (descriptor->api_access == metamodel::ApiAccess::unknown) {
        return PropertyMutationStatus::unknown_access;
    }
    if (descriptor->persistence == metamodel::PersistenceClass::unsupported_by_platform ||
        descriptor->persistence == metamodel::PersistenceClass::version_specific) {
        return PropertyMutationStatus::unsupported_surface;
    }

    if (descriptor->value_codec == metamodel::ValueCodec::typed_value && property_control != nullptr) {
        const auto* type = effective_input_type(*this, *property_control);
        const auto* list_mode = property_control->properties().find(PropertyId::from_name("ListChoiceMode"));
        if (type == nullptr || type->entries.size() != 1 || type->entries.front().term != TypeDomainTerm::numeric ||
            (list_mode != nullptr && std::get_if<bool>(&list_mode->value) && std::get<bool>(list_mode->value)))
            return PropertyMutationStatus::unsupported_surface;
    }

    if (position_control != nullptr) {
        if (descriptor->api_name == "DefaultControl") {
            if (!placement_control_group(*this, position_control->id)) return PropertyMutationStatus::unsupported_surface;
            position_control->position.default_control.reset();
            return PropertyMutationStatus::reset;
        }
        if (descriptor->api_name == "Collapse") {
            position_control->position.collapse.reset();
            return PropertyMutationStatus::reset_runtime_only;
        }
        const auto position_property = find_position_property(position_control->position, descriptor->api_name);
        if (!position_property) return PropertyMutationStatus::unsupported_surface;
        std::visit([](auto* property_value) { property_value->reset(); }, *position_property);
        return PropertyMutationStatus::reset;
    }

    if (property_control != nullptr && descriptor->control_kind == ControlKind::chart &&
        descriptor->api_name == "ShowLegend") {
        auto& chart = std::get<ChartPayload>(property_control->payload);
        if (chart.legend_area) {
            chart.legend_area->placement = "Auto";
            chart.legend_area->explicit_geometry[0] = false;
        }
    }
    if (property_control != nullptr && descriptor->control_kind == ControlKind::chart && descriptor->api_name == "ShowTitle") {
        auto& chart = std::get<ChartPayload>(property_control->payload);
        if (chart.title_area) {
            chart.title_area->placement = "Auto";
            chart.title_area->explicit_geometry[0] = false;
        }
    }
    ObjectId old_caption_asset{};
    if (descriptor->owner == metamodel::DescriptorOwner::form && descriptor->api_name == "CaptionPicture") {
        if (const auto* old = properties->find(descriptor->id))
            if (const auto* picture = std::get_if<PictureRef>(&old->value)) old_caption_asset = picture->asset.id();
    }
    properties->unset(descriptor->id);
    remove_unreferenced_caption_asset(old_caption_asset);
    if (descriptor->persistence == metamodel::PersistenceClass::runtime_only) {
        return PropertyMutationStatus::reset_runtime_only;
    }
    if (descriptor->persistence == metamodel::PersistenceClass::unclassified ||
        descriptor->storage_codec == metamodel::StorageCodec::unclassified ||
        descriptor->storage_codec == metamodel::StorageCodec::none) {
        return PropertyMutationStatus::reset_unclassified_storage;
    }
    return PropertyMutationStatus::reset;
}

ValidationReport OrdinaryFormDocument::validate() const {
    ValidationReport report;
    if (!chart_budget_fits(collections_.controls))
        add_violation(report, InvariantCode::invalid_property, form_.id, {}, "Chart collections exceed the document resource budget");
    TypedValueBudget typed_budget;
    bool typed_values_valid = true;
    const auto check_typed_value = [&](const PropertyValue& value) {
        if (typed_values_valid && !validate_typed_value(value, typed_budget)) typed_values_valid = false;
    };
    const auto check_composite_value = [&](const PropertyValue& value) {
        if (std::holds_alternative<ArrayValue>(value) || std::holds_alternative<StructureValue>(value) || std::holds_alternative<MapValue>(value) ||
            std::holds_alternative<ValueListValue>(value))
            check_typed_value(value);
    };
    const auto check_property_values = [&](const PropertySet& properties, auto&& resolve) {
        properties.for_each_explicit([&](const PropertyEntry& entry) {
            const auto* descriptor = resolve(entry.id);
            if (descriptor && descriptor->value_codec == metamodel::ValueCodec::typed_value) check_typed_value(entry.value);
            else check_composite_value(entry.value);
        });
    };
    check_property_values(form_.properties, [](PropertyId id) { return metamodel::find_form_property(id); });
    check_property_values(form_.panel.properties, [](PropertyId id) { return metamodel::find_property(ControlKind::panel, id); });
    for (const auto& control : collections_.controls) {
        const auto resolve = [&](PropertyId id) { return metamodel::find_property(control.kind(), id); };
        check_property_values(control.properties(), resolve);
        check_property_values(control.extension_properties, resolve);
        if (const auto* chart = std::get_if<ChartPayload>(&control.payload)) {
            const auto check_series = [&](const auto& series) {
                check_typed_value(series.value);
                const auto fields = metamodel::chart_series_property_descriptors();
                check_property_values(series.properties, [&](PropertyId id) -> const metamodel::ChartSeriesPropertyDescriptor* {
                    const auto found = std::ranges::find(fields, id, &metamodel::ChartSeriesPropertyDescriptor::id);
                    return found == fields.end() ? nullptr : &*found;
                });
                for (const auto& trend : series.trendlines) {
                    const auto trend_fields = metamodel::chart_trendline_property_descriptors();
                    check_property_values(trend.properties, [&](PropertyId id) -> const metamodel::ChartSeriesPropertyDescriptor* {
                        const auto found = std::ranges::find(trend_fields, id, &metamodel::ChartSeriesPropertyDescriptor::id);
                        return found == trend_fields.end() ? nullptr : &*found;
                    });
                    trend.equation_area.for_each_explicit([&](const PropertyEntry& entry) { check_composite_value(entry.value); });
                }
            };
            check_series(chart->summary_series);
            if (chart->summary_series.text) check_composite_value(*chart->summary_series.text);
            for (const auto& series : chart->series) {
                check_series(series);
                check_composite_value(series.text);
            }
            for (const auto& point : chart->points) check_typed_value(point.value);
            for (const auto& cell : chart->values) {
                check_typed_value(cell.value);
                check_typed_value(cell.details);
            }
            for (const auto* area : {chart->plot_area ? &*chart->plot_area : nullptr, chart->title_area ? &*chart->title_area : nullptr,
                    chart->legend_area ? &*chart->legend_area : nullptr}) {
                if (!area) continue;
                if (area->data_table_format) check_composite_value(*area->data_table_format);
                for (const auto* scale : {&area->points_scale, &area->values_scale, &area->additional_values_scale, &area->series_scale}) {
                    for (const auto* value : {&scale->scale_step, &scale->title_text, &scale->label_format})
                        if (*value) check_composite_value(**value);
                    scale->properties.for_each_explicit([&](const PropertyEntry& entry) { check_composite_value(entry.value); });
                }
            }
        } else if (const auto* spreadsheet = std::get_if<SpreadsheetDocumentFieldPayload>(&control.payload)) {
            for (const auto& cell : spreadsheet->cells) {
                if (cell.typed_value) check_typed_value(cell.typed_value->value);
                if (cell.control) check_property_values(cell.control->properties,
                    [&](PropertyId id) { return metamodel::find_property(cell.control->kind, id); });
            }
        } else if (const auto* table = std::get_if<TablePayload>(&control.payload)) {
            for (const auto& column : table->columns) check_property_values(column.control.properties,
                [&](PropertyId id) { return metamodel::find_property(column.control.kind, id); });
        }
    }
    if (!typed_values_valid) {
        add_violation(report, InvariantCode::invalid_property, form_.id, {}, "Typed values are invalid or exceed the document depth or node budget");
        return report;
    }
    std::unordered_set<ObjectKey, ObjectKeyHash> seen;

    const auto record_id = [&](ObjectId id, ObjectCategory category) {
        if (!id) {
            add_violation(
                report,
                InvariantCode::invalid_id,
                id,
                id,
                "ordinary-form object IDs must be nonzero");
        }
        if (!seen.insert(ObjectKey{category, id}).second) {
            add_violation(
                report,
                InvariantCode::duplicate_id,
                id,
                id,
                "ordinary-form object ID is duplicated");
        }
    };

    record_id(form_.id, ObjectCategory::form);
    if (form_.main_attribute.id() && find_attribute(form_.main_attribute.id()) == nullptr) {
        add_violation(report, InvariantCode::dangling_reference, form_.id, form_.main_attribute.id(),
            "main form attribute must reference an existing Attribute");
    }
    if (form_.extension && *form_.extension != metamodel::data_processor_form_extension.kind) {
        add_violation(report, InvariantCode::invalid_property, form_.id, form_.id,
            "form extension is not declared by the executable metamodel");
    }
    for (const auto& control : collections_.controls) {
        record_id(control.id, ObjectCategory::control);
    }
    for (const auto& page : collections_.pages) {
        record_id(page.id, ObjectCategory::page);
    }
    for (const auto& attribute : collections_.attributes) {
        record_id(attribute.id, ObjectCategory::attribute);
        if (!type_domain_numeric_precision_normalized(attribute.type))
            add_violation(report, InvariantCode::invalid_property, attribute.id, attribute.id,
                "numeric type-domain precision must be zero when length is zero");
    }
    for (const auto& command : collections_.commands) {
        record_id(command.id, ObjectCategory::command);
    }
    for (const auto& event : collections_.events) {
        record_id(event.id, ObjectCategory::event);
    }
    for (const auto& asset : assets_) {
        record_id(asset.id, ObjectCategory::picture_asset);
    }

    const auto require_control = [&](ObjectId source, ControlRef reference) {
        if (find_control(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "control reference does not resolve to a control");
        }
    };
    const auto require_page = [&](ObjectId source, PageRef reference) {
        if (find_page(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "page reference does not resolve to a page");
        }
    };
    const auto require_attribute = [&](ObjectId source, AttributeRef reference) {
        if (find_attribute(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "attribute reference does not resolve to an attribute");
        }
    };
    const auto require_command = [&](ObjectId source, CommandRef reference) {
        if (find_command(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "command reference does not resolve to a command");
        }
    };
    const auto require_event = [&](ObjectId source, EventRef reference) {
        if (find_event(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "event reference does not resolve to an event");
        }
    };

    std::unordered_map<ObjectId, std::size_t, ObjectIdHash> event_reference_counts;
    const auto require_owned_event = [&]<typename OwnerRef>(
                                         ObjectId source,
                                         EventRef reference,
                                         OwnerRef expected_owner,
                                         auto&& descriptor_lookup) {
        require_event(source, reference);
        const Event* event = find_event(reference.id());
        if (event == nullptr) {
            return;
        }
        ++event_reference_counts[event->id];
        const OwnerRef* actual_owner = std::get_if<OwnerRef>(&event->owner);
        if (actual_owner == nullptr || actual_owner->id() != expected_owner.id()) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                source,
                event->id,
                "event is listed by an owner different from Event.owner");
        }
        const auto* descriptor = descriptor_lookup(event->name);
        if (descriptor != nullptr && descriptor->persistence == metamodel::PersistenceClass::unsupported_by_platform) {
            add_violation(report, InvariantCode::invalid_property, source, event->id,
                "event is unavailable as an action on the target ordinary-form owner");
        }
        if (descriptor == nullptr) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                source,
                event->id,
                "event name is not declared for its ordinary-form owner");
        }
    };
    const auto require_picture = [&](ObjectId source, const PictureRef& reference) {
        if (reference.standard_name) {
            if (reference.asset.id() ||
                metamodel::find_standard_picture(reference.standard_name->value) == nullptr) {
                add_violation(report, InvariantCode::invalid_property, source, reference.asset.id(),
                    "standard picture reference must name one known descriptor and no file asset");
            }
            return;
        }
        if (!reference.asset.id()) {
            add_violation(report, InvariantCode::dangling_reference, source, reference.asset.id(),
                "picture reference has neither a file asset nor a standard picture name");
            return;
        }
        if (find_asset(reference.asset.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.asset.id(),
                "picture reference does not resolve to an asset");
        }
    };

    const auto validate_property_value = [&](ObjectId source, const PropertyValue& value) {
        std::visit(
            [&](const auto& typed_value) {
                using Value = std::remove_cvref_t<decltype(typed_value)>;
                if constexpr (std::is_same_v<Value, ControlRef>) {
                    require_control(source, typed_value);
                } else if constexpr (std::is_same_v<Value, PageRef>) {
                    require_page(source, typed_value);
                } else if constexpr (std::is_same_v<Value, CommandBarButtonRef>) {
                    if (!context_menu_button_resolves(*this, typed_value)) {
                        add_violation(report, InvariantCode::dangling_reference, source,
                            typed_value.owner.id(), "context menu button path does not resolve uniquely");
                    }
                } else if constexpr (std::is_same_v<Value, FormRef>) {
                    if (typed_value.id() != form_.id) {
                        add_violation(report, InvariantCode::dangling_reference, source,
                            typed_value.id(), "form reference does not resolve to this form");
                    }
                } else if constexpr (std::is_same_v<Value, AttributeRef>) {
                    require_attribute(source, typed_value);
                } else if constexpr (std::is_same_v<Value, CommandRef>) {
                    require_command(source, typed_value);
                } else if constexpr (std::is_same_v<Value, PictureRef>) {
                    require_picture(source, typed_value);
                }
            },
            value);
    };

    const auto validate_property_set = [&]<typename Resolve>(
                                           ObjectId source,
                                           const PropertySet& properties,
                                           Resolve&& resolve,
                                           auto surface_is_allowed) {
        properties.for_each_explicit([&](const PropertyEntry& property) {
            if (!property.id || property.state != PropertyState::explicit_value) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "stored property entries must have an ID and explicit state");
            }
            const metamodel::PropertyDescriptor* descriptor = resolve(property.id);
            if (descriptor == nullptr || !surface_is_allowed(*descriptor)) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "property is not declared for this ordinary-form object surface");
            } else if (property.id != descriptor->id) {
                add_violation(report, InvariantCode::invalid_property, source, {},
                    "stored property must use its canonical metamodel ID");
            } else if (descriptor->persistence == metamodel::PersistenceClass::unsupported_by_platform) {
                add_violation(report, InvariantCode::invalid_property, source, {},
                    "property is unavailable on the ordinary-form platform owner");
            } else if (descriptor->value_codec == metamodel::ValueCodec::form_controls ||
                descriptor->value_codec == metamodel::ValueCodec::panel_pages ||
                descriptor->value_codec == metamodel::ValueCodec::value_list_reference ||
                descriptor->value_codec == metamodel::ValueCodec::table_columns) {
                add_violation(report, InvariantCode::invalid_property, source, {},
                    "form item collections are owned objects and cannot be stored as properties");
            } else if (!property_value_matches(*descriptor, property.value)) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "property value does not match its metamodel value kind");
            }
            if (descriptor != nullptr && descriptor->api_name == "ActionSource" &&
                descriptor->control_kind == ControlKind::command_bar) {
                if (const auto* target = std::get_if<ControlRef>(&property.value)) {
                    const ControlNode* source_control = find_control(target->id());
                    if (source_control != nullptr &&
                        source_control->kind() != ControlKind::table &&
                        source_control->kind() != ControlKind::html_document_field) {
                        add_violation(report, InvariantCode::invalid_property, source,
                            target->id(), "CommandBar ActionSource control must be Table or HTMLDocumentField");
                    }
                }
            }
            if (descriptor != nullptr && descriptor->value_codec == metamodel::ValueCodec::context_menu_reference) {
                if (const auto* target = std::get_if<ControlRef>(&property.value)) {
                    const auto* control = find_control(target->id());
                    if (control != nullptr && control->kind() != ControlKind::command_bar)
                        add_violation(report, InvariantCode::invalid_property, source,
                            target->id(), "ContextMenu control reference must resolve to a CommandBar");
                }
            }
            validate_property_value(source, property.value);
        });
    };

    const auto validate_position = [&](ObjectId source, const Position& position) {
        if (position.collapse.value() && !property_value_matches(
                *metamodel::find_property(ControlKind::panel, PropertyId::from_name("Collapse")),
                PropertyValue{*position.collapse.value()}))
            add_violation(report, InvariantCode::invalid_property, source, {}, "Position Collapse must use a known ControlCollapseMode member");
        if (position.width.value() < 0 || position.height.value() < 0) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                source,
                {},
                "Position dimensions must be non-negative");
        }
        std::array<bool, 6> anchor_coordinates{};
        for (const auto& binding : position.bindings.anchors) {
            const auto coordinate = static_cast<std::size_t>(binding.coordinate);
            if (coordinate >= anchor_coordinates.size() || anchor_coordinates[coordinate]) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "Position contains a duplicate binding coordinate");
            } else {
                anchor_coordinates[coordinate] = true;
            }
            if (binding.target.has_value()) {
                require_control(source, *binding.target);
            }
            const auto target_coordinate = static_cast<std::size_t>(binding.target_coordinate);
            if (target_coordinate >= anchor_coordinates.size()) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "Position contains an invalid target binding coordinate");
            }
            if (binding.proportional.has_value()) {
                const auto& target = *binding.proportional;
                const auto proportional_coordinate = static_cast<std::size_t>(target.coordinate);
                if (proportional_coordinate >= anchor_coordinates.size()) {
                    add_violation(
                        report,
                        InvariantCode::invalid_property,
                        source,
                        {},
                        "Position contains an invalid proportional target coordinate");
                }
                if (target.target.has_value()) {
                    require_control(source, *target.target);
                }
            }
        }
        std::array<bool, 5> binding_dimensions{};
        for (const auto& binding : position.bindings.dimensions) {
            const auto dimension = static_cast<std::size_t>(binding.dimension);
            if (dimension >= binding_dimensions.size() || binding_dimensions[dimension]) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "Position contains a duplicate dimension binding");
            } else {
                binding_dimensions[dimension] = true;
            }
        }
    };

    const auto child_id = [](const ChildItemRef& child) {
        return std::visit([](const auto& reference) { return reference.id(); }, child);
    };
    const auto child_key = [](const ChildItemRef& child) {
        return std::visit(
            [](const auto& reference) {
                using ReferenceType = std::remove_cvref_t<decltype(reference)>;
                if constexpr (std::is_same_v<ReferenceType, ControlRef>) {
                    return ObjectKey{ObjectCategory::control, reference.id()};
                } else {
                    return ObjectKey{ObjectCategory::page, reference.id()};
                }
            },
            child);
    };
    const auto require_child = [&](ObjectId source, const ChildItemRef& child) {
        std::visit(
            [&](const auto& reference) {
                using ReferenceType = std::remove_cvref_t<decltype(reference)>;
                if constexpr (std::is_same_v<ReferenceType, ControlRef>) {
                    require_control(source, reference);
                } else {
                    require_page(source, reference);
                }
            },
            child);
    };

    for (const ChildItemRef& child : form_.children) {
        require_child(form_.id, child);
    }
    std::set<std::string_view> form_event_names;
    for (const EventRef event : form_.events) {
        require_owned_event(
            form_.id,
            event,
            FormRef{form_.id},
            [](std::string_view name) { return metamodel::find_form_event(name); });
        if (const Event* resolved = find_event(event.id());
            resolved != nullptr && !form_event_names.insert(resolved->name).second) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                form_.id,
                resolved->id,
                "event name occurs more than once for the same owner");
        }
    }
    validate_property_set(
        form_.id,
        form_.properties,
        [](PropertyId id) { return metamodel::find_form_property(id); },
        [](const metamodel::PropertyDescriptor& descriptor) {
            return descriptor.surface == metamodel::PropertySurface::form;
        });

    validate_property_set(form_.id, form_.panel.properties,
        [](PropertyId id) { return metamodel::find_property(ControlKind::panel, id); },
        [](const metamodel::PropertyDescriptor& descriptor) { return descriptor.surface == metamodel::PropertySurface::control_payload &&
            descriptor.persistence == metamodel::PersistenceClass::persisted_editable &&
            (descriptor.api_name == "AutoTabOrder" || descriptor.api_name == "BorderColor" ||
             descriptor.api_name == "TextColor" || descriptor.api_name == "BackColor"); });

    for (const auto& command : collections_.commands) {
        if (const auto& picture = command.picture.value(); picture.has_value()) {
            require_picture(command.id, *picture);
        }
    }

    for (const auto& event : collections_.events) {
        if (event.handler.empty()) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                event.id,
                event.id,
                "event handler must not be empty");
        }
        std::visit(
            [&](const auto& owner) {
                using Owner = std::remove_cvref_t<decltype(owner)>;
                if constexpr (std::is_same_v<Owner, FormRef>) {
                    if (owner.id() != form_.id) {
                        add_violation(
                            report,
                            InvariantCode::dangling_reference,
                            event.id,
                            owner.id(),
                            "event owner does not resolve to the form");
                    }
                } else {
                    require_control(event.id, owner);
                }
            },
            event.owner);
    }

    std::unordered_map<ObjectKey, std::size_t, ObjectKeyHash> parent_counts;
    const auto register_parent = [&](ObjectId source, const ChildItemRef& child) {
        const ObjectId id = child_id(child);
        const ObjectKey key = child_key(child);
        const bool resolves = std::visit(
            [&](const auto& reference) {
                using ReferenceType = std::remove_cvref_t<decltype(reference)>;
                if constexpr (std::is_same_v<ReferenceType, ControlRef>) {
                    return find_control(reference.id()) != nullptr;
                } else {
                    return find_page(reference.id()) != nullptr;
                }
            },
            child);
        if (!resolves) {
            return;
        }
        const std::size_t count = ++parent_counts[key];
        if (count > 1) {
            add_violation(
                report,
                InvariantCode::multiple_parents,
                source,
                id,
                "child item appears in more than one authoritative child sequence");
        }
    };
    for (const ChildItemRef& child : form_.children) {
        register_parent(form_.id, child);
    }

    std::size_t default_button_count = 0;
    for (const auto& control : collections_.controls) {
        if (control.kind() == ControlKind::button) {
            const auto* selected = control.extension_properties.find(PropertyId::from_name("DefaultButton"));
            if (selected != nullptr && std::holds_alternative<bool>(selected->value) &&
                std::get<bool>(selected->value) && ++default_button_count > 1) {
                add_violation(report, InvariantCode::invalid_property, control.id, {},
                    "the form may have only one DefaultButton");
            }
        }
        const std::vector<CommandBarButton>* owned_buttons = nullptr;
        if (const auto* button = std::get_if<ButtonPayload>(&control.payload)) owned_buttons = &button->buttons;
        if (const auto* command_bar = std::get_if<CommandBarPayload>(&control.payload)) owned_buttons = &command_bar->buttons;
        if (owned_buttons != nullptr) {
            const auto validate_buttons = [&](const auto& self, const std::vector<CommandBarButton>& items, std::size_t depth) -> void {
                if (depth > 256) {
                    add_violation(report, InvariantCode::invalid_property, control.id, {}, "button menu nesting is too deep");
                    return;
                }
                std::set<std::string> names;
                for (const auto& item : items) {
                    const auto invalid = [&](std::string reason) {
                        add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(reason));
                    };
                    if (item.default_button) {
                        const auto* secondary = control.properties().find(PropertyId::from_name("Secondary"));
                        if (control.kind() != ControlKind::command_bar || secondary == nullptr ||
                            !std::holds_alternative<bool>(secondary->value) || std::get<bool>(secondary->value) ||
                            item.type != CommandBarButtonKind::action || depth != 0)
                            invalid("DefaultButton requires a top-level Action on a primary CommandBar (Secondary=false)");
                        if (++default_button_count > 1) invalid("the form may have only one DefaultButton");
                    }
                    if (item.type != CommandBarButtonKind::action && item.type != CommandBarButtonKind::submenu && item.type != CommandBarButtonKind::separator) invalid("unknown menu item type");
                    if (item.representation != ButtonRepresentation::automatic && item.representation != ButtonRepresentation::picture && item.representation != ButtonRepresentation::text && item.representation != ButtonRepresentation::picture_text) invalid("unknown menu representation");
                    if (item.name.empty() || !names.insert(item.name).second) invalid("button menu item names must be non-empty and unique within each collection");
                    if (item.type == CommandBarButtonKind::action && (item.action.has_value() == item.standard_action.has_value())) invalid("Action menu item requires exactly one Action or StandardAction value");
                    if (item.action && item.action->handler.empty()) invalid("Action menu item requires a handler");
                    if (item.type != CommandBarButtonKind::action && (item.action || item.standard_action)) invalid("only Action menu items may have an Action or StandardAction value");
                    if (item.standard_action) {
                        const auto& standard = *item.standard_action;
                        if (standard.command != StandardMenuCommand::close) invalid("unknown StandardAction command");
                        if (standard.context == StandardMenuActionContext::command_bar) {
                            const ControlNode* command_bar = find_control(standard.command_bar.id());
                            if (command_bar == nullptr || command_bar->kind() != ControlKind::command_bar)
                                invalid("StandardAction commandBarId must resolve to a CommandBar control");
                        } else if (standard.context == StandardMenuActionContext::default_context) {
                            if (standard.command_bar.id().value() != 0)
                                invalid("Default StandardAction context cannot have commandBarId");
                        } else invalid("unknown StandardAction context");
                        if (standard.source == StandardMenuActionSource::control) {
                            const auto id = standard.source_control.id();
                            if (id.value() == 0 || id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) ||
                                find_control(id) == nullptr)
                                invalid("StandardAction sourceControlId must be a positive int32 reference to an existing control");
                        } else {
                            if (standard.source != StandardMenuActionSource::form && standard.source != StandardMenuActionSource::all_sources)
                                invalid("unknown StandardAction source");
                            if (standard.source_control.id().value() != 0)
                                invalid("StandardAction sourceControlId requires source Control");
                        }
                    }
                    if (item.order != CommandBarButtonOrder::none && item.order != CommandBarButtonOrder::ascending && item.order != CommandBarButtonOrder::descending) invalid("unknown menu order");
                    if (item.type != CommandBarButtonKind::submenu && item.order != CommandBarButtonOrder::none) invalid("only Submenu menu items may have an order");
                    if (item.type != CommandBarButtonKind::submenu && !item.buttons.empty()) invalid("only Submenu items may contain buttons");
                    if (item.type == CommandBarButtonKind::separator &&
                        (item.text || item.explanation || item.tooltip || !item.enabled || item.checked || item.changes_data || item.representation != ButtonRepresentation::automatic || item.shortcut != ShortcutValue{} || item.picture || item.action || item.standard_action)) invalid("Separator cannot have properties");
                    if (metamodel::find_shortcut_key(item.shortcut.key) == nullptr) invalid("button menu Shortcut has unsupported key");
                    if (item.picture) require_picture(control.id, *item.picture);
                    self(self, item.buttons, depth + 1);
                }
            };
            validate_buttons(validate_buttons, *owned_buttons, 0);
        }
        const auto& descriptor = metamodel::descriptor_for(control.kind());
        if (const auto* spreadsheet = std::get_if<SpreadsheetDocumentFieldPayload>(&control.payload)) {
            std::set<std::pair<std::uint32_t, std::uint32_t>> coordinates;
            std::optional<std::pair<std::uint32_t, std::uint32_t>> previous;
            for (const auto& cell : spreadsheet->cells) {
                const auto coordinate = std::pair{cell.row, cell.column};
                if (cell.row == 0 || cell.column == 0 || !coordinates.emplace(coordinate).second ||
                    (previous && coordinate <= *previous)) {
                    add_violation(
                        report,
                        InvariantCode::invalid_property,
                        control.id,
                        control.id,
                        "Spreadsheet Document cells require unique positive uint32 row and column coordinates in row-major order");
                    break;
                }
                if (cell.typed_value.has_value()) {
                    const auto& typed = *cell.typed_value;
                    bool supported = false;
                    if (typed.type.entries.size() == 1) {
                        const auto& entry = typed.type.entries.front();
                        if (entry.term == TypeDomainTerm::string) {
                            const bool unrelated_qualifiers_default = !entry.type_uuid.has_value() &&
                                entry.numeric == NumericQualifiers{} && entry.binary == LengthQualifiers{} &&
                                entry.date == DateQualifiers{};
                            supported = unrelated_qualifiers_default &&
                                std::holds_alternative<std::string>(typed.value);
                        } else if (entry.term == TypeDomainTerm::numeric) {
                            const bool unrelated_qualifiers_default = !entry.type_uuid.has_value() &&
                                entry.string == LengthQualifiers{} && entry.binary == LengthQualifiers{} &&
                                entry.date == DateQualifiers{};
                            supported = unrelated_qualifiers_default &&
                                (entry.numeric.length == 0 || entry.numeric.precision <= entry.numeric.length) &&
                                std::holds_alternative<DecimalValue>(typed.value);
                        } else if (entry.term == TypeDomainTerm::boolean) {
                            supported = entry == TypeDomainEntry{.term = TypeDomainTerm::boolean} &&
                                std::holds_alternative<bool>(typed.value);
                        } else if (entry.term == TypeDomainTerm::date) {
                            supported = !entry.type_uuid.has_value() && entry.numeric == NumericQualifiers{} &&
                                entry.string == LengthQualifiers{} && entry.binary == LengthQualifiers{} &&
                                entry.date == DateQualifiers{true, true} &&
                                std::holds_alternative<DateValue>(typed.value);
                        }
                    }
                    if (!cell.text.empty() || !supported) {
                        add_violation(
                            report,
                            InvariantCode::invalid_property,
                            control.id,
                            control.id,
                            "Spreadsheet typed cells require an empty Text and a supported ValueType/Value pair");
                        break;
                    }
                }
                if (cell.control.has_value()) {
                    bool supported = cell.typed_value.has_value() &&
                        cell.control->kind == ControlKind::input_field;
                    cell.control->properties.for_each_explicit([&](const PropertyEntry& entry) {
                        if (entry.id != PropertyId::from_name("ReadOnly") ||
                            !std::holds_alternative<bool>(entry.value)) supported = false;
                    });
                    if (!supported) {
                        add_violation(report, InvariantCode::invalid_property, control.id, control.id,
                            "Spreadsheet Cell.Control requires a typed cell, InputField, and Boolean ReadOnly only");
                        break;
                    }
                }
                previous = coordinate;
            }
        }
        if (!control.children.empty() && descriptor.child_policy == metamodel::ChildPolicy::forbidden) {
            add_violation(
                report,
                InvariantCode::illegal_children,
                control.id,
                child_id(control.children.front()),
                "control kind does not permit child controls");
        }

        for (const ChildItemRef& child : control.children) {
            require_child(control.id, child);
            if (descriptor.child_policy == metamodel::ChildPolicy::ordered_controls &&
                std::holds_alternative<PageRef>(child)) {
                add_violation(
                    report,
                    InvariantCode::illegal_children,
                    control.id,
                    child_id(child),
                    "control kind accepts controls but not panel pages");
            }
            register_parent(control.id, child);
        }
        std::set<std::string_view> control_event_names;
        for (const EventRef event : control.events) {
            require_owned_event(
                control.id,
                event,
                ControlRef{control.id},
                [&](std::string_view name) {
                    return metamodel::find_event(control.kind(), name);
                });
            if (const Event* resolved = find_event(event.id());
                resolved != nullptr && !control_event_names.insert(resolved->name).second) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    control.id,
                    resolved->id,
                    "event name occurs more than once for the same owner");
            }
        }
        validate_position(control.id, control.position);
        if (control.data_path.has_value()) {
            require_attribute(control.id, control.data_path->attribute);
            const bool calendar_field = control.kind() == ControlKind::calendar_field;
            const bool track_bar = control.kind() == ControlKind::track_bar;
            if (calendar_field || track_bar) {
                const auto invalid_data_path = [&](std::string reason) {
                    add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(reason));
                };
                if (!control.data_path->members.empty()) {
                    invalid_data_path("CalendarField and TrackBar DataPath must reference an Attribute directly");
                } else if (const Attribute* attribute = find_attribute(control.data_path->attribute.id());
                           attribute != nullptr) {
                    TypeDomainEntry expected;
                    expected.term = calendar_field ? TypeDomainTerm::date : TypeDomainTerm::numeric;
                    if (calendar_field) expected.date = DateQualifiers{true, true};
                    else expected.numeric = NumericQualifiers{10, 0, true};
                    if (attribute->type.entries.size() != 1 || attribute->type.entries.front() != expected) {
                        invalid_data_path(calendar_field ?
                            "CalendarField DataPath must reference a single DateTime Attribute" :
                            "TrackBar DataPath must reference a single Numeric(10,0,nonnegative) Attribute");
                    }
                }
            }
        }
        if (const auto* gantt = std::get_if<GanttChartPayload>(&control.payload)) {
            const auto invalid_gantt = [&](std::string message) {
                add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(message));
            };
            std::unordered_set<std::uint64_t> series_ids;
            std::unordered_set<std::uint64_t> point_ids;
            for (const auto& item : gantt->series) {
                if (!item.id || !series_ids.insert(item.id.value()).second) {
                    invalid_gantt("Gantt Series IDs must be non-empty and unique in their domain");
                }
            }
            for (const auto& item : gantt->points) {
                if (!item.id || !point_ids.insert(item.id.value()).second) {
                    invalid_gantt("Gantt Point IDs must be non-empty and unique in their domain");
                }
            }
            const auto platform_date_key = [](const DateValue& date) {
                const std::string encoded = storage::value_codec::date_to_platform(date.canonical);
                std::uint64_t key = 0;
                const auto parsed = std::from_chars(encoded.data(), encoded.data() + encoded.size(), key, 10);
                if (encoded.empty() || parsed.ec != std::errc{} || parsed.ptr != encoded.data() + encoded.size())
                    throw std::invalid_argument("invalid encoded platform date");
                return key;
            };
            for (const auto& interval : gantt->intervals) {
                if (!series_ids.contains(interval.series_ref.value()) || !point_ids.contains(interval.point_ref.value())) {
                    invalid_gantt("Gantt Interval references must resolve to a Series and Point in the same control");
                }
                try {
                    if (platform_date_key(interval.start_date) > platform_date_key(interval.end_date)) {
                        invalid_gantt("Gantt Interval start date must not follow its end date");
                    }
                } catch (const std::exception&) {
                    invalid_gantt("Gantt Interval dates must be valid local date-times");
                }
            }
            bool auto_full_interval = true;
            const auto* auto_entry = control.properties().find(PropertyId::from_name("AutoFullInterval"));
            if (auto_entry != nullptr) {
                if (const auto* value = std::get_if<bool>(&auto_entry->value)) auto_full_interval = *value;
            }
            const auto date_property = [&](std::string_view name) -> const DateValue* {
                const auto* entry = control.properties().find(PropertyId::from_name(name));
                return entry == nullptr ? nullptr : std::get_if<DateValue>(&entry->value);
            };
            const DateValue* begin = date_property("FullIntervalBegin");
            const DateValue* end = date_property("FullIntervalEnd");
            if (!auto_full_interval && (begin == nullptr || end == nullptr)) {
                invalid_gantt("Manual Gantt full interval requires both FullIntervalBegin and FullIntervalEnd");
            }
            std::optional<std::uint64_t> begin_key;
            std::optional<std::uint64_t> end_key;
            try {
                if (begin != nullptr) begin_key = platform_date_key(*begin);
                if (end != nullptr) end_key = platform_date_key(*end);
            } catch (const std::exception&) {
                invalid_gantt("Gantt full interval properties must be valid local date-times");
            }
            if (begin_key.has_value() && end_key.has_value() && *begin_key >= *end_key) {
                invalid_gantt("Gantt FullIntervalBegin must precede FullIntervalEnd");
            }
        }
        if (const auto* table = std::get_if<TablePayload>(&control.payload)) {
            const auto invalid_table = [&](std::string reason) {
                add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(reason));
            };
            bool tree_source = false;
            if (!control.data_path || !control.data_path->members.empty()) {
                invalid_table("Table requires a direct DataPath to a ValueTable or ValueTree Attribute");
            } else {
                const Attribute* attribute = find_attribute(control.data_path->attribute.id());
                const TypeDomainEntry value_table{.term = TypeDomainTerm::value_table};
                const TypeDomainEntry value_tree{.term = TypeDomainTerm::value_tree};
                if (attribute == nullptr || attribute->type.entries.size() != 1 ||
                    (attribute->type.entries.front() != value_table && attribute->type.entries.front() != value_tree)) {
                    invalid_table("Table DataPath must reference the named ValueTable or ValueTree type");
                } else {
                    tree_source = attribute->type.entries.front() == value_tree;
                }
            }
            if (!tree_source && control.properties().find(PropertyId::from_name("Tree")) != nullptr) {
                invalid_table("Table Tree property requires a ValueTree DataPath");
            }
            if (!tree_source) {
                for (const EventRef reference : control.events) {
                    const Event* event = find_event(reference.id());
                    if (event != nullptr && event->name == "OnCurrentParentChange") {
                        invalid_table("Table OnCurrentParentChange requires a ValueTree DataPath");
                    }
                }
            }
            if (!tree_source && table->columns.empty()) {
                invalid_table("ValueTable requires at least one Column");
            }
            std::set<std::string> column_names;
            for (const auto& column : table->columns) {
                if (column.layout_width) {
                    try {
                        const auto width = storage::value_codec::canonical_decimal(column.layout_width->canonical);
                        if (width != column.layout_width->canonical || width.starts_with('-')) {
                            invalid_table("Table Column layout width must be a canonical nonnegative decimal");
                        }
                    } catch (const std::exception&) {
                        invalid_table("Table Column layout width is invalid");
                    }
                }
                try { (void)storage::value_codec::encode_font(column.text_font); }
                catch (const std::exception&) { invalid_table("Table Column TextFont is invalid"); }
                if (column.name.empty() || !column_names.insert(column.name).second) {
                    invalid_table("Table Column names must be non-empty and unique");
                }
                if (column.data_path.empty()) {
                    invalid_table("Table Column DataPath must be non-empty");
                }
                if (column.header.items.empty()) {
                    invalid_table("Table Column Header must contain localized text");
                }
                std::set<std::string> languages;
                for (const auto& item : column.header.items) {
                    if (item.language.empty() || !languages.insert(item.language).second) {
                        invalid_table("Table Column Header languages must be non-empty and unique");
                    }
                }
                if (column.control.value_type.has_value()) {
                    if (column.control.kind != ControlKind::input_field) {
                        invalid_table("Table Column ValueType is supported only for an InputField editor");
                    } else {
                        try {
                            (void)storage::value_codec::encode_type_domain(*column.control.value_type);
                        } catch (const std::exception&) {
                            invalid_table("Table Column InputField ValueType is not supported by the typed codec");
                        }
                    }
                }
                column.control.properties.for_each_explicit([&](const PropertyEntry& entry) {
                    const auto* property = metamodel::find_property(column.control.kind, entry.id);
                    if (!table_column_editor_property_allowed(column.control.kind,
                            property == nullptr ? std::string_view{} : property->api_name) ||
                        !property_value_matches(*property, entry.value) ||
                        !table_column_editor_property_value_supported(column.control.kind, property->api_name, entry.value)) {
                        invalid_table("Table Column editor has an incompatible, unsupported, or nondefault property");
                        return;
                    }
                });
                if (column.control.kind != ControlKind::input_field &&
                    column.control.kind != ControlKind::choice_field &&
                    column.control.kind != ControlKind::check_box) {
                    invalid_table("Table Column Control kind is unsupported");
                }
            }
            for (const auto name : {"FixedLeft", "FixedRight"}) {
                const auto* entry = control.properties().find(PropertyId::from_name(name));
                if (!entry) continue;
                const auto* value = std::get_if<std::int64_t>(&entry->value);
                if (value && (*value < 0 || *value > 256))
                    invalid_table("Table fixed column counts must be between 0 and 256");
            }
        }
        validate_property_set(
            control.id,
            control.extension_properties,
            [&](PropertyId id) { return metamodel::find_property(control.kind(), id); },
            [](const metamodel::PropertyDescriptor& descriptor) {
                return descriptor.surface == metamodel::PropertySurface::control_extension &&
                       descriptor.api_name != "Name" && descriptor.api_name != "Data";
            });
        validate_property_set(
            control.id,
            control.properties(),
            [&](PropertyId id) { return metamodel::find_property(control.kind(), id); },
            [](const metamodel::PropertyDescriptor& descriptor) {
                return descriptor.surface == metamodel::PropertySurface::control_payload;
            });
        if (const auto* chart = std::get_if<ChartPayload>(&control.payload)) {
            const auto invalid_chart = [&](std::string reason) {
                add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(reason));
            };
            for (const auto& band : chart->gauge_quality_bands) {
                bool valid_text = false;
                bool valid_tool_tip = false;
                const auto check_band_text = [](const PropertyValue& value) {
                    if (const auto* text = std::get_if<std::string>(&value)) return valid_typed_utf8(*text);
                    if (const auto* localized = std::get_if<LocalizedStringValue>(&value)) return valid_localized_string(*localized);
                    return false;
                };
                valid_text = check_band_text(band.text);
                valid_tool_tip = check_band_text(band.tool_tip);
                if (!valid_chart_gauge_quality_band_range(band.begin, band.end) ||
                    storage::value_codec::canonical_decimal(band.begin.canonical) != band.begin.canonical ||
                    storage::value_codec::canonical_decimal(band.end.canonical) != band.end.canonical ||
                    !valid_chart_style(metamodel::ValueCodec::color, PropertyValue{band.back_color}) ||
                    !valid_text || !valid_tool_tip)
                    invalid_chart("Invalid GaugeChartQualityBand value");
            }
            for (const auto& field : metamodel::chart_area_color_descriptors()) {
                const auto& area = field.kind == ChartAreaKind::plot ? chart->plot_area : field.kind == ChartAreaKind::title ? chart->title_area : chart->legend_area;
                if (area && ((*area).*(field.member)) && !valid_chart_style(metamodel::ValueCodec::color, PropertyValue{*((*area).*(field.member))}))
                    invalid_chart("Invalid Chart area Color value");
            }
            for (const auto& field : metamodel::chart_area_style_descriptors()) {
                if (!field.font) continue;
                const auto& area = field.kind == ChartAreaKind::plot ? chart->plot_area : field.kind == ChartAreaKind::title ? chart->title_area : chart->legend_area;
                if (area && ((*area).*(field.font)) && !valid_chart_style(metamodel::ValueCodec::font, PropertyValue{*((*area).*(field.font))}))
                    invalid_chart("Invalid Chart area Font value");
            }
            for (const auto* area : {chart->plot_area ? &*chart->plot_area : nullptr, chart->title_area ? &*chart->title_area : nullptr, chart->legend_area ? &*chart->legend_area : nullptr}) {
                if (!area || !area->scale_lines) continue;
                if (area != (chart->plot_area ? &*chart->plot_area : nullptr)) invalid_chart("ScaleLines belongs only to PlotArea");
                try { (void)storage::value_codec::encode_line(*area->scale_lines); }
                catch (const std::exception&) { invalid_chart("ScaleLines contains invalid line"); }
            }
            for (const auto& owner : metamodel::chart_scale_owner_descriptors()) {
                if (control.properties().contains(owner.id)) invalid_chart("Chart scales cannot be stored in scalar property bags");
                for (const auto* area : {chart->plot_area ? &*chart->plot_area : nullptr, chart->title_area ? &*chart->title_area : nullptr, chart->legend_area ? &*chart->legend_area : nullptr}) {
                    if (!area) continue;
                    const auto& scale = area->*(owner.member);
                    scale.properties.for_each_explicit([&](const PropertyEntry& entry) {
                        const auto fields = metamodel::chart_scale_property_descriptors();
                        const auto field = std::ranges::find(fields, entry.id, &metamodel::ChartScalePropertyDescriptor::id);
                        const auto title_fields = metamodel::chart_label_area_property_descriptors();
                        const auto title_field = std::ranges::find(title_fields, entry.id, &metamodel::ChartScalePropertyDescriptor::id);
                        if ((field == fields.end() && title_field == title_fields.end()) || (field != fields.end() && field->member != nullptr))
                            invalid_chart("Chart scale sparse properties contain an unknown or fixed property");
                    });
                    for (const auto fields : {metamodel::chart_scale_property_descriptors(), metamodel::chart_label_area_property_descriptors()})
                    for (const auto& field : fields) {
                        if (control.properties().contains(field.id) && metamodel::find_property(ControlKind::chart, field.id) == nullptr) invalid_chart("Chart scale fields cannot be stored in scalar property bags");
                        const auto* value = metamodel::chart_scale_property_value(scale, field);
                        if (!value) continue;
                        if (area != (chart->plot_area ? &*chart->plot_area : nullptr)) invalid_chart("Chart scales belong only to PlotArea");
                        auto normalized = *value;
                        if (!normalize_chart_scale_property_value(field, normalized) || normalized != *value)
                            invalid_chart("Chart scale property has invalid type, value, or canonical representation");
                    }
                }
            }
            if (control.properties().contains(PropertyId::from_name("Series")) ||
                control.properties().contains(PropertyId::from_name("Points")) ||
                control.properties().contains(PropertyId::from_name("SummarySeries")) ||
                control.properties().contains(PropertyId::from_name("GaugeChartQualityBands")))
                invalid_chart("Chart collections are owned objects and cannot be stored as properties");
            if (control.properties().contains(PropertyId::from_name("SeriesCount")) ||
                control.properties().contains(PropertyId::from_name("PointCount")))
                invalid_chart("Chart counts are derived from collections and cannot be stored as properties");
            if (control.kind() != ControlKind::chart) {
                invalid_chart("Chart payload is attached to a non-Chart control");
            }
            if (!chart->points.empty() && chart->series.size() >
                std::numeric_limits<std::size_t>::max() / chart->points.size()) {
                invalid_chart("Chart dense Values dimensions overflow");
            } else if (chart->values.size() != chart->series.size() * chart->points.size()) {
                invalid_chart("Chart Values must be a dense Series by Point matrix");
            }
            if (chart->data_mode && *chart->data_mode != ChartDataMode::sample && *chart->data_mode != ChartDataMode::explicit_values)
                invalid_chart("Chart Values dataMode is invalid");
            if (chart->summary_series.text && !std::holds_alternative<std::string>(*chart->summary_series.text) &&
                !(std::holds_alternative<LocalizedStringValue>(*chart->summary_series.text) && valid_localized_string(std::get<LocalizedStringValue>(*chart->summary_series.text))))
                invalid_chart("Chart SummarySeries Text must be plain or localized text");
            metamodel::PropertyDescriptor scalar_descriptor;
            scalar_descriptor.value_codec = metamodel::ValueCodec::typed_value;
            if (!property_value_matches(scalar_descriptor, chart->summary_series.value))
                invalid_chart("Chart SummarySeries Value must be a supported typed scalar");
            const auto validate_series_object = [&](metamodel::ConstChartSeriesObject object) {
                std::visit([&](auto* item) {
                    item->properties.for_each_explicit([&](const PropertyEntry& entry) {
                        const auto fields = metamodel::chart_series_property_descriptors();
                        const auto field = std::ranges::find(fields, entry.id, &metamodel::ChartSeriesPropertyDescriptor::id);
                        if (field == fields.end() || field->fixed != metamodel::ChartSeriesFixed::none || field->storage == metamodel::ChartSeriesStorage::computed)
                            invalid_chart("Series sparse properties contain an unknown, fixed, or computed property");
                    });
                    if (item->trendlines.size() > max_chart_collection_items) invalid_chart("Trendlines collection exceeds supported size");
                    std::unordered_set<ObjectId, ObjectIdHash> ids;
                    for (const auto& trend : item->trendlines) {
                        if (!trend.id || !ids.insert(trend.id).second || trend.id.value() >= item->next_trendline_id) invalid_chart("Trendline runtime identities are invalid");
                        trend.properties.for_each_explicit([&](const PropertyEntry& entry) {
                            const auto fields = metamodel::chart_trendline_property_descriptors();
                            const auto field = std::ranges::find(fields, entry.id, &metamodel::ChartSeriesPropertyDescriptor::id);
                            auto normalized = entry.value;
                            if (field == fields.end() || !normalize_chart_series_property_value(*field, normalized) || normalized != entry.value)
                                invalid_chart("Trendline property is unknown or invalid");
                        });
                        trend.equation_area.for_each_explicit([&](const PropertyEntry& entry) {
                            const auto fields = metamodel::chart_label_area_property_descriptors();
                            const auto field = std::ranges::find(fields, entry.id, &metamodel::ChartScalePropertyDescriptor::id);
                            auto normalized = entry.value;
                            if (field == fields.end() || !normalize_chart_scale_property_value(*field, normalized) || normalized != entry.value)
                                invalid_chart("Trendline EquationArea property is unknown or invalid");
                        });
                    }
                }, object);
                for (const auto& field : metamodel::chart_series_property_descriptors()) {
                    if (field.storage == metamodel::ChartSeriesStorage::computed) continue;
                    const auto value = metamodel::chart_series_property_value(object, field);
                    if (!value) continue;
                    auto normalized = *value;
                    if (!normalize_chart_series_property_value(field, normalized) || normalized != *value) invalid_chart("Series named property is invalid");
                }
            };
            validate_series_object(metamodel::ConstChartSeriesObject{&chart->summary_series});
            for (const auto& series : chart->series) validate_series_object(metamodel::ConstChartSeriesObject{&series});
            std::unordered_set<ObjectId, ObjectIdHash> series_ids;
            for (const auto& series : chart->series) {
                if (!series.id || series.id.value() == 1 || !series_ids.insert(series.id).second) {
                    invalid_chart("Chart Series IDs must be unique, positive, and distinct from reserved summary ID 1");
                }
                if (series.marker.type_name != "ChartMarkerType" ||
                    (series.marker.member != "Auto" && series.marker.member != "Alternation" &&
                     series.marker.member != "Rect" && series.marker.member != "Circle" &&
                     series.marker.member != "None" && series.marker.member != "Rhomb")) {
                    invalid_chart("Chart Series Marker must name a supported ТипМаркераДиаграммы member");
                }
            }
            std::unordered_set<ObjectId, ObjectIdHash> point_ids;
            for (const auto& point : chart->points) {
                if (!point.id || !point_ids.insert(point.id).second) {
                    invalid_chart("Chart Point IDs must be positive and unique");
                }
                const bool absolute_color = point.color.kind == ColorKind::absolute && point.color.alpha == 255 &&
                    std::holds_alternative<std::monostate>(point.color.style);
                const bool automatic_color = point.color == ColorValue{};
                if (!absolute_color && !automatic_color) {
                    invalid_chart("Chart Point Color must be automatic or an absolute opaque RGB value");
                }
            }
            std::uint64_t max_series_id = 1;
            for (const auto& series : chart->series) max_series_id = std::max(max_series_id, series.id.value());
            std::uint64_t max_point_id = 0;
            for (const auto& point : chart->points) max_point_id = std::max(max_point_id, point.id.value());
            constexpr auto chart_id_limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            if (max_series_id > chart_id_limit || max_point_id > chart_id_limit ||
                (chart->last_series_id && *chart->last_series_id > chart_id_limit) ||
                (chart->last_point_id && *chart->last_point_id > chart_id_limit))
                invalid_chart("Chart allocated IDs must fit in signed 64-bit storage");
            if (chart->last_series_id && *chart->last_series_id < max_series_id)
                invalid_chart("Chart Series lastAssignedId must cover all series and reserved summary ID");
            if (chart->last_point_id && *chart->last_point_id < max_point_id)
                invalid_chart("Chart Points lastAssignedId must cover all points");
            std::set<std::pair<ObjectId, ObjectId>> value_pairs;
            for (const auto& value : chart->values) {
                if (!series_ids.contains(value.series_ref) || !point_ids.contains(value.point_ref)) {
                    invalid_chart("Chart Value references must resolve to a Series and a Point");
                }
                if (!value_pairs.emplace(value.series_ref, value.point_ref).second) {
                    invalid_chart("Chart Value pairs must be unique");
                }
                if (const auto* number = std::get_if<DecimalValue>(&value.value);
                    number != nullptr && !canonical_chart_decimal(number->canonical)) {
                    invalid_chart("Chart Value Number must contain a decimal value");
                }
            }
        }
    }

    for (const auto& page : collections_.pages) {
        validate_position(page.id, page.position.value());
        for (const ChildItemRef& child : page.children) {
            require_child(page.id, child);
            if (std::holds_alternative<PageRef>(child)) {
                add_violation(
                    report,
                    InvariantCode::illegal_children,
                    page.id,
                    child_id(child),
                    "panel page accepts controls but not nested pages");
            }
            register_parent(page.id, child);
        }
    }

    const bool valid_focus_ownership = std::ranges::none_of(report.violations,
        [](const auto& violation) { return violation.code != InvariantCode::invalid_property; });
    std::unordered_set<std::uint64_t> checked_focus_controls;
    for (const auto& control : collections_.controls) {
        if (!valid_focus_ownership) break;
        if (!control.position.default_control.value().value_or(false) || checked_focus_controls.contains(control.id.value())) continue;
        const auto group = placement_control_group(*this, control.id);
        if (!group) continue;
        for (const auto id : *group) checked_focus_controls.insert(id.value());
        if (std::ranges::count_if(*group, [&](ObjectId id) {
                return find_control(id)->position.default_control.value().value_or(false);
            }) > 1)
            add_violation(report, InvariantCode::invalid_property, control.id, {},
                "DefaultControl must select at most one control in its focus group");
    }

    for (const auto& event : collections_.events) {
        const std::size_t count = event_reference_counts[event.id];
        if (count == 0) {
            add_violation(
                report,
                InvariantCode::orphan,
                form_.id,
                event.id,
                "event is not present in its owner's authoritative event sequence");
        } else if (count > 1) {
            add_violation(
                report,
                InvariantCode::multiple_parents,
                form_.id,
                event.id,
                "event appears more than once in authoritative event sequences");
        }
    }

    for (const auto& control : collections_.controls) {
        if (!parent_counts.contains(ObjectKey{ObjectCategory::control, control.id})) {
            add_violation(
                report,
                InvariantCode::orphan,
                form_.id,
                control.id,
                "control is not present in the authoritative child tree");
        }
    }
    for (const auto& page : collections_.pages) {
        if (!parent_counts.contains(ObjectKey{ObjectCategory::page, page.id})) {
            add_violation(
                report,
                InvariantCode::orphan,
                form_.id,
                page.id,
                "panel page is not present in the authoritative child tree");
        }
    }

    enum class VisitState : std::uint8_t {
        visiting,
        complete,
    };
    std::unordered_map<ObjectKey, VisitState, ObjectKeyHash> visit_states;
    std::function<void(ObjectKey, const std::vector<ChildItemRef>&)> visit =
        [&](ObjectKey source, const std::vector<ChildItemRef>& children) {
        visit_states[source] = VisitState::visiting;
        for (const ChildItemRef& child_ref : children) {
            const ObjectId id = child_id(child_ref);
            const ObjectKey key = child_key(child_ref);
            const auto state = visit_states.find(key);
            if (state != visit_states.end() && state->second == VisitState::visiting) {
                add_violation(
                    report,
                    InvariantCode::cycle,
                    source.id,
                    id,
                    "form child graph contains a cycle");
                continue;
            }
            if (state == visit_states.end()) {
                std::visit(
                    [&](const auto& reference) {
                        using ReferenceType = std::remove_cvref_t<decltype(reference)>;
                        if constexpr (std::is_same_v<ReferenceType, ControlRef>) {
                            if (const ControlNode* child = find_control(reference.id())) {
                                visit(key, child->children);
                            }
                        } else if (const Page* child = find_page(reference.id())) {
                            visit(key, child->children);
                        }
                    },
                    child_ref);
            }
        }
        visit_states[source] = VisitState::complete;
    };

    for (const auto& control : collections_.controls) {
        if (!visit_states.contains(ObjectKey{ObjectCategory::control, control.id})) {
            visit(ObjectKey{ObjectCategory::control, control.id}, control.children);
        }
    }
    for (const auto& page : collections_.pages) {
        if (!visit_states.contains(ObjectKey{ObjectCategory::page, page.id})) {
            visit(ObjectKey{ObjectCategory::page, page.id}, page.children);
        }
    }

    return report;
}

void OrdinaryFormDocument::validate_or_throw() const {
    ValidationReport report = validate();
    if (!report.ok()) {
        throw InvariantError(std::move(report));
    }
}

std::optional<ChartTrendlineRef> OrdinaryFormDocument::add_chart_trendline(ObjectId chart_id, ObjectId series_id) {
    const auto location = find_location(ObjectCategory::control, chart_id);
    if (!location || collections_.controls[location->index].kind() != ControlKind::chart) return std::nullopt;
    auto object = chart_series_object(std::get<ChartPayload>(collections_.controls[location->index].payload), series_id);
    if (!object || !chart_budget_fits(collections_.controls, {}, 0, 0, 1)) return std::nullopt;
    return std::visit([&](auto* item) -> std::optional<ChartTrendlineRef> {
        if (item->trendlines.size() >= max_chart_collection_items || item->next_trendline_id == 0 || item->next_trendline_id == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        ChartTrendline trend;
        trend.id = ObjectId{item->next_trendline_id};
        const auto reference = ChartTrendlineRef{chart_id, series_id, trend.id};
        item->trendlines.push_back(std::move(trend));
        ++item->next_trendline_id;
        return reference;
    }, *object);
}
PropertyReadResult OrdinaryFormDocument::insert_chart_trendline(ObjectId chart_id, ObjectId series_id, const PropertyValue&) {
    if (!chart_trendline_count(chart_id, series_id)) return {PropertyReadStatus::unknown_owner, std::nullopt};
    return {PropertyReadStatus::computed_value, PropertyValue{UndefinedValue{}}};
}
PropertyMutationStatus OrdinaryFormDocument::delete_chart_trendline(ObjectId chart_id, ObjectId series_id, std::int64_t index) {
    const auto location = find_location(ObjectCategory::control, chart_id);
    if (!location || collections_.controls[location->index].kind() != ControlKind::chart) return PropertyMutationStatus::unknown_owner;
    auto object = chart_series_object(std::get<ChartPayload>(collections_.controls[location->index].payload), series_id);
    if (!object) return PropertyMutationStatus::unknown_owner;
    return std::visit([&](auto* item) {
        if (index < 0 || static_cast<std::uint64_t>(index) >= item->trendlines.size()) return PropertyMutationStatus::invalid_value;
        item->trendlines.erase(item->trendlines.begin() + index);
        return PropertyMutationStatus::applied;
    }, *object);
}
std::optional<std::size_t> OrdinaryFormDocument::chart_trendline_count(ObjectId chart_id, ObjectId series_id) const {
    const auto* control = find_control(chart_id);
    if (!control || control->kind() != ControlKind::chart) return std::nullopt;
    const auto object = chart_series_object(std::get<ChartPayload>(control->payload), series_id);
    if (!object) return std::nullopt;
    return std::visit([](auto* item) { return item->trendlines.size(); }, *object);
}
std::optional<ChartTrendlineRef> OrdinaryFormDocument::chart_trendline_at(ObjectId chart_id, ObjectId series_id, std::int64_t index) const {
    const auto* control = find_control(chart_id);
    if (!control || control->kind() != ControlKind::chart || index < 0) return std::nullopt;
    const auto object = chart_series_object(std::get<ChartPayload>(control->payload), series_id);
    if (!object) return std::nullopt;
    return std::visit([&](auto* item) -> std::optional<ChartTrendlineRef> {
        if (static_cast<std::uint64_t>(index) >= item->trendlines.size()) return std::nullopt;
        return ChartTrendlineRef{chart_id, series_id, item->trendlines[static_cast<std::size_t>(index)].id};
    }, *object);
}
std::optional<std::int64_t> OrdinaryFormDocument::chart_trendline_index(ChartTrendlineRef reference) const {
    const auto* control = find_control(reference.owner);
    if (!control || control->kind() != ControlKind::chart) return std::nullopt;
    const auto object = chart_series_object(std::get<ChartPayload>(control->payload), reference.series);
    if (!object) return std::nullopt;
    return std::visit([&](auto* item) -> std::optional<std::int64_t> {
        const auto found = std::ranges::find(item->trendlines, reference.trendline, &ChartTrendline::id);
        if (found == item->trendlines.end()) return std::nullopt;
        return static_cast<std::int64_t>(std::distance(item->trendlines.begin(), found));
    }, *object);
}

PropertyMutationStatus OrdinaryFormDocument::resize_chart(ObjectId id, PropertyId property, const PropertyValue& value) {
    try {
        std::string text;
        if (const auto* number = std::get_if<std::int64_t>(&value)) text = std::to_string(*number);
        else if (const auto* number = std::get_if<DecimalValue>(&value)) text = number->canonical;
        else if (const auto* boolean = std::get_if<bool>(&value)) text = *boolean ? "1" : "0";
        else if (const auto* string = std::get_if<std::string>(&value)) { text = *string; std::replace(text.begin(), text.end(), ',', '.'); }
        else return PropertyMutationStatus::invalid_value;
        std::string canonical;
        try { canonical = storage::value_codec::canonical_decimal(text); }
        catch (const std::exception&) { return PropertyMutationStatus::invalid_value; }
        if (!canonical.empty() && canonical.front() == '-') return PropertyMutationStatus::invalid_value;
        const auto dot = canonical.find('.');
        const auto integral = std::string_view(canonical).substr(0, dot);
        std::uint64_t requested = 0;
        const auto parsed = std::from_chars(integral.data(), integral.data() + integral.size(), requested);
        if (parsed.ec == std::errc::result_out_of_range) return PropertyMutationStatus::resource_limit;
        if (parsed.ec != std::errc{} || parsed.ptr != integral.data() + integral.size()) return PropertyMutationStatus::invalid_value;
        if (requested > max_chart_collection_items) return PropertyMutationStatus::resource_limit;
        const auto location = find_location(ObjectCategory::control, id);
        if (!location || !reconstruction_complete_) return PropertyMutationStatus::unsupported_surface;
        auto& control = collections_.controls[location->index];
        auto* chart = std::get_if<ChartPayload>(&control.payload);
        if (!chart || control.data_path || control.properties().contains(PropertyId::from_name("DataSource")))
            return PropertyMutationStatus::unsupported_surface;
        const bool series_axis = property == PropertyId::from_name("SeriesCount");
        const auto new_series_count = series_axis ? static_cast<std::size_t>(requested) : chart->series.size();
        const auto new_point_count = series_axis ? chart->points.size() : static_cast<std::size_t>(requested);
        if (!chart_budget_fits(collections_.controls, id, new_series_count, new_point_count))
            return PropertyMutationStatus::resource_limit;
        const auto report = validate();
        if (std::ranges::any_of(report.violations, [id](const InvariantViolation& violation) { return violation.source == id; }))
            return PropertyMutationStatus::unsupported_surface;
        const auto old_count = series_axis ? chart->series.size() : chart->points.size();
        if (requested == old_count) return PropertyMutationStatus::applied;
        std::uint64_t last_series = chart->last_series_id.value_or(1);
        std::uint64_t last_point = chart->last_point_id.value_or(0);
        for (const auto& series : chart->series) last_series = std::max(last_series, series.id.value());
        for (const auto& point : chart->points) last_point = std::max(last_point, point.id.value());
        const auto added = requested > old_count ? requested - old_count : 0;
        const auto last = series_axis ? last_series : last_point;
        const auto max_id = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (last > max_id || added > max_id - last) return PropertyMutationStatus::resource_limit;
        const auto effective_mode = chart->data_mode.value_or(chart->series.empty() || chart->points.empty() ? ChartDataMode::sample : ChartDataMode::explicit_values);
        const auto selected = get_prop_val({ObjectCategory::control, id}, PropertyId::from_name("ActiveSeries"));
        ChartPayload result = *chart;
        result.data_mode = effective_mode;
        const auto auto_text = [&](std::string_view name) {
            const auto* entry = result.properties.find(PropertyId::from_name(name));
            return entry == nullptr || (std::get_if<bool>(&entry->value) && std::get<bool>(entry->value));
        };
        if (series_axis) {
            result.series.resize(std::min<std::size_t>(result.series.size(), new_series_count));
            while (result.series.size() < new_series_count) {
                ChartSeries series; series.id = ObjectId{++last_series}; series.marker = {"ChartMarkerType", "Auto"};
                if (auto_text("AutoSeriesText")) series.text = "Серия " + std::to_string(result.series.size() + 1);
                result.series.push_back(std::move(series));
            }
            result.last_series_id = last_series;
            if (result.series.empty()) result.properties.unset(PropertyId::from_name("ActiveSeries"));
            else if (selected.value) {
                if (const auto* ref = std::get_if<ChartSeriesRef>(&*selected.value);
                    ref && std::ranges::none_of(result.series, [&](const auto& series) { return series.id == ref->series; }))
                    result.properties.set_explicit(PropertyId::from_name("ActiveSeries"), ChartSeriesRef{id, result.series.back().id});
            }
        } else {
            result.points.resize(std::min<std::size_t>(result.points.size(), new_point_count));
            while (result.points.size() < new_point_count) {
                ChartPoint point; point.id = ObjectId{++last_point};
                if (auto_text("AutoPointText")) point.text = "Точка " + std::to_string(result.points.size() + 1);
                result.points.push_back(std::move(point));
            }
            result.last_point_id = last_point;
        }
        std::map<std::pair<std::uint64_t, std::uint64_t>, ChartValue> previous;
        for (const auto& cell : chart->values) previous.emplace(std::pair{cell.series_ref.value(), cell.point_ref.value()}, cell);
        result.values.clear(); result.values.reserve(new_series_count * new_point_count);
        for (const auto& series : result.series) for (const auto& point : result.points) {
            const auto found = previous.find({series.id.value(), point.id.value()});
            if (found != previous.end()) result.values.push_back(found->second);
            else {
                ChartValue cell; cell.series_ref = series.id; cell.point_ref = point.id;
                if (effective_mode == ChartDataMode::explicit_values) cell.value = UndefinedValue{};
                else {
                    thread_local std::mt19937 engine(std::random_device{}());
                    const auto amount = std::uniform_int_distribution<unsigned>{0, 2000}(engine);
                    cell.value = DecimalValue{storage::value_codec::canonical_decimal(std::to_string(amount / 100) + "." + std::to_string(100 + amount % 100).substr(1))};
                }
                result.values.push_back(std::move(cell));
            }
        }
        *chart = std::move(result);
        return PropertyMutationStatus::applied;
    } catch (const std::bad_alloc&) { return PropertyMutationStatus::resource_limit; }
      catch (const std::length_error&) { return PropertyMutationStatus::resource_limit; }
}

void OrdinaryFormDocument::remove_unreferenced_caption_asset(ObjectId id) {
    if (!id) return;
    const auto matches = [id](const PictureRef& picture) { return picture.asset.id() == id; };
    const auto has_picture = [&](const PropertySet& properties) {
        bool found = false;
        properties.for_each_explicit([&](const PropertyEntry& entry) {
            if (const auto* picture = std::get_if<PictureRef>(&entry.value); picture && matches(*picture)) found = true;
        });
        return found;
    };
    if (has_picture(form_.properties) || has_picture(form_.panel.properties) ||
        has_picture(window_permissions_) || has_picture(runtime_form_values_)) return;
    for (const auto& command : collections_.commands)
        if (command.picture.value() && matches(*command.picture.value())) return;
    const auto menu_has_picture = [&](const auto& self, const std::vector<CommandBarButton>& buttons) -> bool {
        for (const auto& button : buttons)
            if ((button.picture && matches(*button.picture)) || self(self, button.buttons)) return true;
        return false;
    };
    for (const auto& control : collections_.controls) {
        if (has_picture(control.properties()) || has_picture(control.extension_properties)) return;
        if (const auto* button = std::get_if<ButtonPayload>(&control.payload); button && menu_has_picture(menu_has_picture, button->buttons)) return;
        if (const auto* bar = std::get_if<CommandBarPayload>(&control.payload); bar && menu_has_picture(menu_has_picture, bar->buttons)) return;
    }
    const auto asset = std::ranges::find(assets_, id, &PictureAsset::id);
    if (asset != assets_.end()) {
        assets_.erase(asset);
        rebuild_index();
    }
}

void OrdinaryFormDocument::rebuild_index() {
    index_.clear();
    index_first(form_.id, ObjectCategory::form, 0);
    for (std::size_t index = 0; index < collections_.controls.size(); ++index) {
        index_first(collections_.controls[index].id, ObjectCategory::control, index);
    }
    for (std::size_t index = 0; index < collections_.pages.size(); ++index) {
        index_first(collections_.pages[index].id, ObjectCategory::page, index);
    }
    for (std::size_t index = 0; index < collections_.attributes.size(); ++index) {
        index_first(collections_.attributes[index].id, ObjectCategory::attribute, index);
    }
    for (std::size_t index = 0; index < collections_.commands.size(); ++index) {
        index_first(collections_.commands[index].id, ObjectCategory::command, index);
    }
    for (std::size_t index = 0; index < collections_.events.size(); ++index) {
        index_first(collections_.events[index].id, ObjectCategory::event, index);
    }
    for (std::size_t index = 0; index < assets_.size(); ++index) {
        index_first(assets_[index].id, ObjectCategory::picture_asset, index);
    }
}

void OrdinaryFormDocument::index_first(
    ObjectId id,
    ObjectCategory category,
    std::size_t index
) {
    index_.try_emplace(ObjectKey{category, id}, ObjectLocation{category, index});
}

}  // namespace oof::model
