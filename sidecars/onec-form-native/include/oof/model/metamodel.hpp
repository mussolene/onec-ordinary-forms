#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "oof/model/ordinary_form.hpp"

namespace oof::model::metamodel {

enum class VersionMask : std::uint32_t {
    none = 0,
    platform_8_2 = 1U << 0,
    platform_8_5 = 1U << 1,
    all_supported = (1U << 0) | (1U << 1),
    platform_8_2_and_8_5 = all_supported,
};

[[nodiscard]] constexpr VersionMask operator|(VersionMask lhs, VersionMask rhs) noexcept {
    return static_cast<VersionMask>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool includes(VersionMask mask, VersionMask version) noexcept {
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(version)) ==
           static_cast<std::uint32_t>(version);
}

enum class ClassificationStatus : std::uint8_t {
    platform_resource_backed,
    corpus_xsd_resource_correlated,
    corpus_xsd_correlated,
    platform_ui_guid_table_backed,
    platform_resource_backed_windows_oracle_pending,
    binary_guid_corpus_xsd_correlated,
    windows_harness_required,
};

enum class ChildPolicy : std::uint8_t {
    forbidden,
    ordered_controls,
    ordered_controls_and_pages,
};

enum class ApiAccess : std::uint8_t {
    unknown,
    read_only,
    write_only,
    read_write,
};

enum class ValueKind : std::uint8_t {
    unknown,
    boolean,
    number,
    string,
    date_time,
    picture,
    color,
    font,
    border,
    shortcut,
    binary,
    identifier,
    collection,
    enumeration,
    object,
    variant,
};

enum class ValueCodec : std::uint8_t {
    unclassified,
    command_bar_buttons,
    owned_panel,
    form_controls,
    panel_pages,
    value_list_reference,
    value_list_item_reference,
    table_columns,
    calendar_selected_dates,
    dendrogram_items,
    dendrogram_links,
    boolean,
    integer,
    integer32,
    decimal,
    typed_value,
    runtime_value,
    string,
    localized_string,
    formatted_string,
    shortcut,
    date,
    uuid,
    style,
    composite_id,
    type_domain,
    enumeration,
    line,
    border,
    color,
    font,
    picture,
    action_source_reference,
    context_menu_reference,
    chart_series_reference,
    chart_point_reference,
    chart_series_collection,
    chart_points_collection,
    chart_gauge_quality_bands,
    chart_plot_area,
    chart_title_area,
    chart_legend_area,
    chart_trendlines,
    chart_equation_area,
    control_reference,
    page_reference,
    form_reference,
    attribute_reference,
    command_reference,
    localized_text,
};

struct ShortcutKeyDescriptor {
    std::string_view name;
    std::uint32_t storage_code = 0;
};

enum class PersistenceClass : std::uint8_t {
    unclassified,
    persisted_editable,
    persisted_readonly,
    runtime_only,
    version_specific,
    unsupported_by_platform,
};

enum class StorageCodec : std::uint8_t {
    unclassified,
    none,
    root_record,
    control_base,
    control_info,
    position_record,
    binding_record,
    event_record,
    value_record,
    collection_record,
    picture_record,
    active_x_state,
};

enum class DefaultKind : std::uint8_t {
    unknown,
    none,
    undefined,
    boolean,
    integer,
    decimal,
    string,
    enumeration,
    border,
    color,
    font,
    shortcut,
    style,
};

struct DefaultValue {
    DefaultKind kind = DefaultKind::unknown;
    std::string_view canonical;
};

enum class DescriptorOwner : std::uint8_t {
    form,
    control,
};

enum class PropertySurface : std::uint8_t {
    form,
    form_extension,
    control_extension,
    panel_placement,
    control_payload,
};

struct ControlDescriptor {
    ControlKind kind = ControlKind::panel;
    std::string_view guid;
    std::string_view storage_tag;
    std::string_view public_name;
    std::string_view api_name;
    std::u8string_view russian_name;
    VersionMask version_mask = VersionMask::none;
    ClassificationStatus classification = ClassificationStatus::corpus_xsd_correlated;
    ChildPolicy child_policy = ChildPolicy::forbidden;
};

enum class FormExtensionStorageKind : std::uint8_t { custom_settings, empty };

struct FormExtensionDescriptor {
    FormExtensionKind kind;
    std::string_view guid;
    std::string_view xml_name;
    std::u8string_view russian_name;
    FormExtensionStorageKind storage = FormExtensionStorageKind::custom_settings;
};

inline constexpr FormExtensionDescriptor data_processor_form_extension{
    FormExtensionKind::data_processor,
    "59d6c227-97d3-46f6-84a0-584c5a2807e1",
    "DataProcessorFormExtension",
    u8"Расширение формы обработки",
};

inline constexpr FormExtensionDescriptor catalog_item_form_extension{
    FormExtensionKind::catalog_item,
    "c5251418-7f99-4a2f-ba62-7f3c9e7bf914",
    "CatalogItemFormExtension",
    u8"Расширение формы элемента справочника",
};

inline constexpr FormExtensionDescriptor catalog_list_form_extension{
    FormExtensionKind::catalog_list,
    "53fb5449-841a-459c-9a83-576f05941e1f",
    "CatalogListFormExtension",
    u8"Расширение формы списка справочника",
    FormExtensionStorageKind::empty,
};

inline constexpr FormExtensionDescriptor report_form_extension{
    FormExtensionKind::report,
    "343b4e00-29f0-11d6-a3c7-0050bae0a776",
    "ReportFormExtension",
    u8"Расширение формы отчета",
};

inline constexpr FormExtensionDescriptor document_list_form_extension{
    FormExtensionKind::document_list,
    "8d9c2110-ed81-11d5-b9b6-0050bae0a95d",
    "DocumentListFormExtension",
    u8"Расширение формы списка документов",
    FormExtensionStorageKind::empty,
};

inline constexpr FormExtensionDescriptor information_register_record_list_form_extension{
    FormExtensionKind::information_register_record_list,
    "cb575e8c-871e-4a9e-9767-e7c783bdf1e6",
    "InformationRegisterRecordListFormExtension",
    u8"Расширение формы списка записей регистра сведений",
    FormExtensionStorageKind::empty,
};

inline constexpr std::array form_extensions{
    data_processor_form_extension, catalog_item_form_extension,
    catalog_list_form_extension, report_form_extension,
    document_list_form_extension, information_register_record_list_form_extension,
};

struct InputFieldExtensionDescriptor {
    InputFieldExtensionKind kind;
    std::string_view guid;
    std::string_view xml_name;
    std::u8string_view russian_name;
};
inline constexpr InputFieldExtensionDescriptor input_field_extensions[]{
    {InputFieldExtensionKind::catalog, "8585207b-9ca7-425b-9385-e8fd67c4e148",
        "CatalogInputFieldExtension", u8"Расширение поля ввода справочника"},
    {InputFieldExtensionKind::chart_of_accounts, "16ab4ef5-818b-4f8a-a958-51ed44e07bda",
        "ChartOfAccountsInputFieldExtension", u8"Расширение поля ввода плана счетов"},
    {InputFieldExtensionKind::chart_of_characteristic_types, "5f656c99-ee3b-4bb7-92a1-e6f5a93c2c9d",
        "ChartOfCharacteristicTypesInputFieldExtension", u8"Расширение поля ввода вида характеристик"},
};

struct StandardMenuCommandDescriptor {
    StandardMenuCommand kind;
    std::string_view public_name;
    std::u8string_view russian_name;
    std::uint32_t storage_id;
};

inline constexpr std::array standard_menu_commands{
    StandardMenuCommandDescriptor{StandardMenuCommand::close, "Close", u8"Закрыть", 142},
    StandardMenuCommandDescriptor{StandardMenuCommand::save_values, "SaveValues", u8"СохранитьЗначения", 143},
    StandardMenuCommandDescriptor{StandardMenuCommand::restore_values, "RestoreValues", u8"ВосстановитьЗначения", 144},
};
inline constexpr StandardMenuCommandDescriptor standard_menu_close = standard_menu_commands[0];

[[nodiscard]] constexpr const StandardMenuCommandDescriptor* find_standard_menu_command(
    StandardMenuCommand command) noexcept {
    for (const auto& descriptor : standard_menu_commands)
        if (descriptor.kind == command) return &descriptor;
    return nullptr;
}

[[nodiscard]] constexpr const StandardMenuCommandDescriptor* find_standard_menu_command(
    std::string_view public_name) noexcept {
    for (const auto& descriptor : standard_menu_commands)
        if (descriptor.public_name == public_name) return &descriptor;
    return nullptr;
}

[[nodiscard]] constexpr const StandardMenuCommandDescriptor* find_standard_menu_command(
    std::uint32_t storage_id) noexcept {
    for (const auto& descriptor : standard_menu_commands)
        if (descriptor.storage_id == storage_id) return &descriptor;
    return nullptr;
}

struct PropertyDescriptor {
    PropertyId id{};
    DescriptorOwner owner = DescriptorOwner::control;
    PropertySurface surface = PropertySurface::control_payload;
    ControlKind control_kind = ControlKind::panel;
    std::size_t order = 0;
    std::string_view xml_name;
    std::string_view api_name;
    std::u8string_view russian_name;
    std::u8string_view platform_type;
    ValueKind value_kind = ValueKind::unknown;
    ValueCodec value_codec = ValueCodec::unclassified;
    ApiAccess api_access = ApiAccess::unknown;
    VersionMask version_mask = VersionMask::none;
    PersistenceClass persistence = PersistenceClass::unclassified;
    StorageCodec storage_codec = StorageCodec::unclassified;
    DefaultValue default_value{};
    std::string_view enumeration_type{};
    std::span<const std::string_view> enumeration_members{};
    std::optional<FormExtensionKind> extension_kind{};
    bool designer_only = false;
};

enum class ChartSeriesStorage : std::uint8_t { row, style, companion, computed };
enum class ChartSeriesFixed : std::uint8_t { none, text, value, color, marker };
struct ChartSeriesPropertyDescriptor {
    PropertyId id;
    std::string_view name;
    std::u8string_view russian_name;
    ValueCodec value_codec;
    std::u8string_view russian_type;
    ChartSeriesStorage storage;
    std::size_t slot;
    ChartSeriesFixed fixed = ChartSeriesFixed::none;
    std::string_view enumeration_type;
    std::span<const std::string_view> members;
    PropertyValue default_value{UndefinedValue{}};
    bool read_only = false;
    bool normal_runtime_only = false;
};
using ConstChartSeriesObject = std::variant<const ChartSeries*, const ChartSummarySeries*>;
using ChartSeriesObject = std::variant<ChartSeries*, ChartSummarySeries*>;
[[nodiscard]] std::span<const ChartSeriesPropertyDescriptor> chart_series_property_descriptors() noexcept;
[[nodiscard]] std::span<const ChartSeriesPropertyDescriptor> chart_trendline_property_descriptors() noexcept;
[[nodiscard]] std::optional<PropertyValue> chart_series_property_value(ConstChartSeriesObject object, const ChartSeriesPropertyDescriptor& field);
void set_chart_series_property_value(ChartSeriesObject object, const ChartSeriesPropertyDescriptor& field, PropertyValue value);
void reset_chart_series_property_value(ChartSeriesObject object, const ChartSeriesPropertyDescriptor& field);

struct ChartScaleOwnerDescriptor {
    PropertyId id;
    std::string_view name;
    std::u8string_view russian_name;
    ChartScaleKind kind;
    ChartScale ChartArea::*member;
    bool tail;
    std::size_t record_offset;
};
enum class ChartScaleNumericPolicy : std::uint8_t { none, nonnegative_decimal, signed_decimal, rounded_wrapped_int32, unit_decimal };
struct ChartScalePropertyDescriptor {
    PropertyId id;
    std::string_view name;
    std::u8string_view russian_name;
    ValueCodec value_codec;
    std::optional<PropertyValue> ChartScale::*member;
    std::size_t record_offset;
    std::string_view enumeration_type;
    std::span<const std::string_view> members;
    std::size_t default_ordinal = 0;
    std::u8string_view russian_type;
    ChartScaleNumericPolicy numeric_policy = ChartScaleNumericPolicy::none;
    std::optional<PropertyValue> native_default = std::nullopt;
};
[[nodiscard]] std::span<const ChartScaleOwnerDescriptor> chart_scale_owner_descriptors() noexcept;
[[nodiscard]] std::span<const ChartScalePropertyDescriptor> chart_scale_property_descriptors() noexcept;

[[nodiscard]] std::span<const ChartScalePropertyDescriptor> chart_label_area_property_descriptors() noexcept;
[[nodiscard]] PropertyValue chart_scale_property_default(const ChartScalePropertyDescriptor& field);
[[nodiscard]] const PropertyValue* chart_scale_property_value(const ChartScale& scale, const ChartScalePropertyDescriptor& field) noexcept;
void set_chart_scale_property_value(ChartScale& scale, const ChartScalePropertyDescriptor& field, PropertyValue value);
void reset_chart_scale_property_value(ChartScale& scale, const ChartScalePropertyDescriptor& field);

enum class ChartScaleCompanionBase : std::uint8_t { middle, cells_end };
enum class ChartScaleCompanionValue : std::uint8_t { ordinal, equals, differs };
struct ChartScaleCompanionDescriptor {
    ChartScaleKind owner;
    PropertyId property;
    ChartScaleCompanionBase base;
    std::size_t offset;
    ChartScaleCompanionValue value;
    std::size_t compared_ordinal;
};
[[nodiscard]] std::span<const ChartScaleCompanionDescriptor> chart_scale_companion_descriptors() noexcept;

struct ChartAreaPropertyDescriptor {
    PropertyId id{};
    std::string_view name;
    std::u8string_view russian_name;
    ValueCodec value_codec = ValueCodec::unclassified;
    DecimalValue ChartArea::*coordinate = nullptr;
    std::size_t coordinate_offset = 0;
    std::string_view plot_default;
    std::string_view legend_default;
    std::string_view title_default;
};

struct ChartAreaBooleanDescriptor {
    PropertyId id{};
    std::string_view name;
    std::u8string_view russian_name;
    ChartAreaKind kind = ChartAreaKind::none;
    std::optional<bool> ChartArea::*member = nullptr;
    std::size_t storage_offset = 0;
    bool default_value = false;
};

struct ChartPlotFormatDescriptor {
    PropertyId id{};
    std::string_view name;
    std::u8string_view russian_name;
    ValueCodec value_codec = ValueCodec::unclassified;
    std::optional<PropertyValue> ChartArea::*text = nullptr;
    std::optional<EnumerationValue> ChartArea::*enumeration = nullptr;
    std::string_view enumeration_type;
    std::span<const std::string_view> members;
    std::size_t default_ordinal = 0;
    std::size_t storage_offset = 0;
};

[[nodiscard]] std::span<const ChartPlotFormatDescriptor> chart_plot_format_descriptors() noexcept;
[[nodiscard]] const ChartPlotFormatDescriptor* find_chart_plot_format(PropertyId id) noexcept;

struct LineFamilyDescriptor {
    std::string_view name;
    std::string_view guid;
    std::span<const std::string_view> members;
};
[[nodiscard]] std::span<const LineFamilyDescriptor> line_family_descriptors() noexcept;

struct ChartAreaStyleDescriptor {
    PropertyId id{};
    std::string_view name;
    std::u8string_view russian_name;
    ChartAreaKind kind = ChartAreaKind::none;
    std::optional<BorderValue> ChartArea::*border = nullptr;
    std::optional<FontValue> ChartArea::*font = nullptr;
    std::size_t storage_offset = 0;
    std::optional<LineValue> ChartArea::*line = nullptr;
};

[[nodiscard]] std::span<const ChartAreaStyleDescriptor> chart_area_style_descriptors() noexcept;
[[nodiscard]] const ChartAreaStyleDescriptor* find_chart_area_style(ChartAreaKind kind, PropertyId id) noexcept;

enum class ChartAreaColorDefault : std::uint8_t { automatic, form_back, form_text, border, white, surface };

struct ChartAreaColorDescriptor {
    PropertyId id{};
    std::string_view name;
    std::u8string_view russian_name;
    ChartAreaKind kind = ChartAreaKind::none;
    std::optional<ColorValue> ChartArea::*member = nullptr;
    std::size_t storage_offset = 0;
    std::size_t companion_from_end = 0;
    ChartAreaColorDefault default_kind = ChartAreaColorDefault::automatic;
};

[[nodiscard]] std::span<const ChartAreaColorDescriptor> chart_area_color_descriptors() noexcept;
[[nodiscard]] const ChartAreaColorDescriptor* find_chart_area_color(ChartAreaKind kind, PropertyId id) noexcept;
[[nodiscard]] ColorValue chart_area_color_default(ChartAreaColorDefault kind);
[[nodiscard]] std::span<const ChartAreaBooleanDescriptor> chart_area_boolean_descriptors() noexcept;
[[nodiscard]] const ChartAreaBooleanDescriptor* find_chart_area_boolean(ChartAreaKind kind, PropertyId id) noexcept;
[[nodiscard]] std::span<const ChartAreaPropertyDescriptor> chart_area_property_descriptors() noexcept;
[[nodiscard]] std::span<const std::string_view> chart_area_placements(ChartAreaKind kind) noexcept;
[[nodiscard]] const ChartAreaPropertyDescriptor* find_chart_area_property(PropertyId id) noexcept;

struct EventDescriptor {
    DescriptorOwner owner = DescriptorOwner::control;
    ControlKind control_kind = ControlKind::panel;
    std::size_t order = 0;
    std::string_view xml_name;
    std::string_view api_name;
    std::u8string_view russian_name;
    VersionMask version_mask = VersionMask::none;
    PersistenceClass persistence = PersistenceClass::unclassified;
    StorageCodec storage_codec = StorageCodec::unclassified;
    std::string_view storage_tag;
    std::optional<std::uint32_t> storage_event_id{};
    std::span<const FormExtensionKind> extension_kinds{};
};

// Empty scope denotes the common Form table, not an arbitrary extension table.
[[nodiscard]] bool form_event_uses_table(const EventDescriptor& event,
    std::optional<FormExtensionKind> extension) noexcept;

struct PlatformValueTypeDescriptor {
    std::string_view name;
    std::string_view guid;
};

[[nodiscard]] std::span<const PlatformValueTypeDescriptor> platform_value_type_descriptors() noexcept;

struct StandardPictureDescriptor {
    std::string_view runtime_name;
    std::u8string_view russian_name;
    std::string_view guid;
    std::int32_t storage_id = 0;
};

struct MetamodelCoverage {
    std::string_view target_platform_version = "8.5.1.1343";
    std::size_t target_control_count = 0;
    std::size_t excluded_control_count = 0;
    std::size_t excluded_control_property_occurrences = 0;
    std::size_t control_count = 0;
    std::size_t control_property_occurrences = 0;
    std::size_t unique_control_property_names = 0;
    std::size_t control_event_occurrences = 0;
    std::size_t unique_control_event_names = 0;
    std::size_t form_property_count = 0;
    std::size_t form_event_count = 0;
    std::size_t control_extension_property_count = 0;
    std::size_t panel_placement_property_count = 0;
    std::size_t unclassified_properties = 0;
    std::size_t unclassified_events = 0;
    std::size_t unknown_value_kinds = 0;
    std::size_t unclassified_value_codecs = 0;
    std::size_t unknown_defaults = 0;
    std::size_t missing_storage_codecs = 0;
    std::size_t property_id_collisions = 0;
    bool release_ready = false;
};

class Metamodel {
public:
    Metamodel(const Metamodel&) = delete;
    Metamodel& operator=(const Metamodel&) = delete;
    ~Metamodel();

    [[nodiscard]] static const Metamodel& instance();

    [[nodiscard]] std::span<const ControlDescriptor> controls() const noexcept;
    [[nodiscard]] std::span<const PropertyDescriptor> form_properties() const noexcept;
    [[nodiscard]] std::span<const PropertyDescriptor> form_extension_properties() const noexcept;
    [[nodiscard]] std::span<const PropertyDescriptor> form_extension_properties(
        FormExtensionKind kind) const noexcept;
    [[nodiscard]] std::span<const EventDescriptor> form_events() const noexcept;
    [[nodiscard]] std::span<const PropertyDescriptor> control_extension_properties() const noexcept;
    [[nodiscard]] std::span<const PropertyDescriptor> panel_placement_properties() const noexcept;
    [[nodiscard]] std::span<const PropertyDescriptor> properties_for(
        ControlKind kind) const noexcept;
    [[nodiscard]] std::span<const EventDescriptor> events_for(
        ControlKind kind) const noexcept;

    [[nodiscard]] const ControlDescriptor& control(ControlKind kind) const;
    [[nodiscard]] const ControlDescriptor* control_by_guid(std::string_view guid) const noexcept;
    [[nodiscard]] const ControlDescriptor* control_by_public_name(
        std::string_view name) const noexcept;
    [[nodiscard]] const ControlDescriptor* control_by_api_name(
        std::string_view name) const noexcept;
    [[nodiscard]] const ControlDescriptor* control_by_russian_name(
        std::u8string_view name) const noexcept;
    [[nodiscard]] const PropertyDescriptor* property(
        ControlKind kind,
        std::string_view name) const noexcept;
    [[nodiscard]] const PropertyDescriptor* property(
        ControlKind kind,
        PropertyId id) const noexcept;
    [[nodiscard]] const PropertyDescriptor* form_property(
        std::string_view name) const noexcept;
    [[nodiscard]] const PropertyDescriptor* form_property(PropertyId id) const noexcept;
    [[nodiscard]] const PropertyDescriptor* form_extension_property(
        FormExtensionKind kind, std::string_view name) const noexcept;
    [[nodiscard]] const PropertyDescriptor* form_extension_property(
        FormExtensionKind kind, PropertyId id) const noexcept;
    [[nodiscard]] const EventDescriptor* event(
        ControlKind kind,
        std::string_view name) const noexcept;
    [[nodiscard]] const EventDescriptor* form_event(std::string_view name) const noexcept;
    [[nodiscard]] const MetamodelCoverage& coverage() const noexcept;

private:
    struct Impl;

    Metamodel();

    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::span<const ControlDescriptor> control_descriptors() noexcept;
[[nodiscard]] std::span<const PropertyDescriptor> form_property_descriptors() noexcept;
[[nodiscard]] std::span<const PropertyDescriptor> form_extension_property_descriptors() noexcept;
[[nodiscard]] std::span<const EventDescriptor> form_event_descriptors() noexcept;
[[nodiscard]] std::span<const PropertyDescriptor> control_extension_property_descriptors() noexcept;
[[nodiscard]] std::span<const PropertyDescriptor> panel_placement_property_descriptors() noexcept;
[[nodiscard]] std::span<const PropertyDescriptor> property_descriptors(
    ControlKind kind) noexcept;
[[nodiscard]] std::span<const EventDescriptor> event_descriptors(ControlKind kind) noexcept;
[[nodiscard]] std::span<const StandardPictureDescriptor> standard_picture_descriptors() noexcept;
[[nodiscard]] const StandardPictureDescriptor* find_standard_picture(std::string_view runtime_name) noexcept;
[[nodiscard]] const StandardPictureDescriptor* find_standard_picture_by_guid(std::string_view guid) noexcept;
[[nodiscard]] const StandardPictureDescriptor* find_standard_picture_by_storage_id(std::int32_t storage_id) noexcept;
[[nodiscard]] const ControlDescriptor& descriptor_for(ControlKind kind);
[[nodiscard]] const ControlDescriptor* find_by_guid(std::string_view guid) noexcept;
[[nodiscard]] const ControlDescriptor* find_by_public_name(std::string_view name) noexcept;
[[nodiscard]] const ControlDescriptor* find_by_api_name(std::string_view name) noexcept;
[[nodiscard]] const ControlDescriptor* find_by_russian_name(std::u8string_view name) noexcept;
[[nodiscard]] const PropertyDescriptor* find_property(
    ControlKind kind,
    std::string_view name) noexcept;
[[nodiscard]] const PropertyDescriptor* find_property(
    ControlKind kind,
    PropertyId id) noexcept;
[[nodiscard]] const PropertyDescriptor* find_form_property(std::string_view name) noexcept;
[[nodiscard]] const PropertyDescriptor* find_form_property(PropertyId id) noexcept;
[[nodiscard]] const PropertyDescriptor* find_form_extension_property(
    FormExtensionKind kind, std::string_view name) noexcept;
[[nodiscard]] const PropertyDescriptor* find_form_extension_property(
    FormExtensionKind kind, PropertyId id) noexcept;
[[nodiscard]] const EventDescriptor* find_event(
    ControlKind kind,
    std::string_view name) noexcept;
[[nodiscard]] const EventDescriptor* find_form_event(std::string_view name) noexcept;
[[nodiscard]] const MetamodelCoverage& metamodel_coverage() noexcept;
[[nodiscard]] std::string_view classification_name(ClassificationStatus status) noexcept;
[[nodiscard]] std::string_view persistence_name(PersistenceClass classification) noexcept;
[[nodiscard]] std::string_view storage_codec_name(StorageCodec codec) noexcept;
[[nodiscard]] std::string_view value_kind_name(ValueKind kind) noexcept;
[[nodiscard]] std::string_view value_codec_name(ValueCodec codec) noexcept;
[[nodiscard]] std::span<const ShortcutKeyDescriptor> shortcut_key_descriptors() noexcept;
[[nodiscard]] const ShortcutKeyDescriptor* find_shortcut_key(std::string_view name) noexcept;

struct NamedColorDescriptor {
    std::string_view name;
    std::u8string_view russian_name;
    std::int32_t native_kind;
    std::int32_t native_id;
};

[[nodiscard]] std::span<const NamedColorDescriptor> named_colors() noexcept;
[[nodiscard]] const NamedColorDescriptor* find_named_color(std::string_view name) noexcept;
[[nodiscard]] const NamedColorDescriptor* find_named_color(std::int32_t native_kind, std::int32_t native_id) noexcept;

}  // namespace oof::model::metamodel
