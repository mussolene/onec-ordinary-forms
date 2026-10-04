#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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
    dendrogram_items,
    dendrogram_links,
    boolean,
    integer,
    integer32,
    decimal,
    string,
    localized_string,
    formatted_string,
    shortcut,
    date,
    uuid,
    composite_id,
    type_domain,
    enumeration,
    color,
    font,
    picture,
    control_reference,
    attribute_reference,
    command_reference,
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
    color,
    font,
    shortcut,
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

struct FormExtensionDescriptor {
    FormExtensionKind kind;
    std::string_view guid;
    std::string_view xml_name;
    std::u8string_view russian_name;
};

inline constexpr FormExtensionDescriptor data_processor_form_extension{
    FormExtensionKind::data_processor,
    "59d6c227-97d3-46f6-84a0-584c5a2807e1",
    "DataProcessorFormExtension",
    u8"Расширение формы обработки",
};

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
};

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
};

struct StandardPictureDescriptor {
    std::string_view runtime_name;
    std::u8string_view russian_name;
    std::string_view guid;
    std::int32_t storage_id = 0;
};

struct MetamodelCoverage {
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

}  // namespace oof::model::metamodel
