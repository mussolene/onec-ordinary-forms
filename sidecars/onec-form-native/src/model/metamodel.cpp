#include "oof/model/metamodel.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oof::model::metamodel {
namespace {

struct ControlIdentity {
    ControlKind kind;
    std::string_view guid;
    std::string_view storage_tag;
    VersionMask version_mask;
    ClassificationStatus classification;
    ChildPolicy child_policy;
};

struct HelpControlName {
    ControlKind kind;
    std::string_view public_name;
    std::string_view api_name;
    std::u8string_view russian_name;
};

constexpr auto all_versions = VersionMask::all_supported;

ValueCodec initial_value_codec(ValueKind kind, std::u8string_view platform_type) noexcept {
    switch (kind) {
        case ValueKind::boolean:
            return ValueCodec::boolean;
        case ValueKind::number:
            return ValueCodec::decimal;
        case ValueKind::string:
            return ValueCodec::string;
        case ValueKind::date_time:
            return ValueCodec::date;
        case ValueKind::picture:
            return ValueCodec::picture;
        case ValueKind::color:
            return ValueCodec::color;
        case ValueKind::font:
            return ValueCodec::font;
        case ValueKind::identifier:
            return ValueCodec::uuid;
        case ValueKind::enumeration:
            return ValueCodec::enumeration;
        case ValueKind::object:
            return platform_type == u8"ОписаниеТипов" ? ValueCodec::type_domain
                                                       : ValueCodec::unclassified;
        case ValueKind::unknown:
        case ValueKind::border:
        case ValueKind::shortcut:
        case ValueKind::binary:
        case ValueKind::collection:
        case ValueKind::variant:
            return ValueCodec::unclassified;
    }
    return ValueCodec::unclassified;
}

ValueCodec panel_placement_value_codec(
    ValueKind kind,
    std::u8string_view platform_type
) noexcept {
    return kind == ValueKind::number ? ValueCodec::integer32
                                     : initial_value_codec(kind, platform_type);
}

constexpr std::array<ControlIdentity, control_kind_count> control_identities{{
    {ControlKind::panel, "09ccdc77-ea1a-4a6d-ab1c-3435eada2433", "pnl", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::ordered_controls_and_pages},
    {ControlKind::command_bar, "e69bf21d-97b2-4f37-86db-675aea9ec2cb", "cmdb", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::button, "6ff79819-710e-4145-97cd-1618da79e3e2", "btn", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::picture_decoration, "151ef23e-6bb2-4681-83d0-35bc2217230c", "img", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::check_box, "35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26", "chk", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::choice_field, "64483e7f-3833-48e2-8c75-2c31aac49f6e", "txt", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::radio_button, "782e569a-79a7-4a4f-a936-b48d013936ec", "rbtn", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::input_field, "381ed624-9217-4e63-85db-c4c3cb87daae", "txt", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::usual_group, "90db814a-c75f-4b54-bc96-df62e554d67d", "grpb", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::ordered_controls},
    {ControlKind::splitter, "36e52348-5d60-4770-8e89-a16ed50a2006", "sep", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::chart, "a8b97779-1a4b-4059-b09c-807f86d2a461", "chrt", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::pivot_chart, "a26da99e-184a-4823-b0d6-62816d38dc4e", "", all_versions, ClassificationStatus::platform_ui_guid_table_backed, ChildPolicy::forbidden},
    {ControlKind::gantt_chart, "e5fdc112-5c84-4a16-9728-72b85692b6e2", "gchrt", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::dendrogram, "984981b1-622d-4ebc-94f7-885f0cdfb59a", "dndrgm", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::html_document_field, "d92a805c-98ae-4750-9158-d9ce7cec2f20", "html", all_versions, ClassificationStatus::platform_resource_backed_windows_oracle_pending, ChildPolicy::forbidden},
    {ControlKind::list_box, "19f8b798-314e-4b4e-8121-905b2a7a03f5", "txt", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::progress_bar, "b1db1f86-abbb-4cf0-8852-fe6ae21650c2", "prgb", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::track_bar, "6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef", "trckb", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::calendar_field, "e3c063d8-ef92-41be-9c89-b70290b5368b", "clndr", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::text_document_field, "14c4a229-bfc3-42fe-9ce1-2da049fd0109", "txtd", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::geographical_schema_field, "ad37194e-555e-4305-b718-5dca84baf145", "gm", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::graphical_schema_field, "42248403-7748-49da-b782-e4438fd7bff3", "flwchrt", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::table, "ea83fe3a-ac3c-4cce-8045-3dddf35b28b1", "tbl", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::spreadsheet_document_field, "236a17b3-7f44-46d9-a907-75f9cdc61ab5", "sprdsht", all_versions, ClassificationStatus::binary_guid_corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::label_decoration, "0fc7e20d-f241-460c-bdf4-5ad88e5474a5", "lbl", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::active_x_control, "621e95f1-064f-11d4-9400-008048da11f9", "", all_versions, ClassificationStatus::windows_harness_required, ChildPolicy::forbidden},
}};

static_assert(control_identities.size() == 26);

std::vector<HelpControlName> make_help_control_names() {
    return {
#define OOF_HELP_CONTROL(kind_token, public_name_value, api_name_value, russian_name_value) \
        {ControlKind::kind_token, public_name_value, api_name_value, russian_name_value},
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<PropertyDescriptor> make_control_property_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(kind_token, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, value_kind_token, api_access_token, version_token) \
        {PropertyId::from_name(api_name_value), DescriptorOwner::control, PropertySurface::control_payload, ControlKind::kind_token, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, ValueKind::value_kind_token, initial_value_codec(ValueKind::value_kind_token, platform_type_value), ApiAccess::api_access_token, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<EventDescriptor> make_control_event_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(kind_token, order_value, xml_name_value, api_name_value, russian_name_value, version_token) \
        {DescriptorOwner::control, ControlKind::kind_token, order_value, xml_name_value, api_name_value, russian_name_value, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<PropertyDescriptor> make_form_property_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, value_kind_token, api_access_token, version_token) \
        {PropertyId::from_name(api_name_value), DescriptorOwner::form, PropertySurface::form, ControlKind::panel, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, ValueKind::value_kind_token, initial_value_codec(ValueKind::value_kind_token, platform_type_value), ApiAccess::api_access_token, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<EventDescriptor> make_form_event_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(order_value, xml_name_value, api_name_value, russian_name_value, version_token) \
        {DescriptorOwner::form, ControlKind::panel, order_value, xml_name_value, api_name_value, russian_name_value, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<PropertyDescriptor> make_control_extension_property_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, value_kind_token, api_access_token, version_token) \
        {PropertyId::from_name(api_name_value), DescriptorOwner::control, PropertySurface::control_extension, ControlKind::panel, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, ValueKind::value_kind_token, initial_value_codec(ValueKind::value_kind_token, platform_type_value), ApiAccess::api_access_token, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<PropertyDescriptor> make_panel_placement_property_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, value_kind_token, api_access_token, version_token) \
        {PropertyId::from_name(api_name_value), DescriptorOwner::control, PropertySurface::panel_placement, ControlKind::panel, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, ValueKind::value_kind_token, panel_placement_value_codec(ValueKind::value_kind_token, platform_type_value), ApiAccess::api_access_token, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

template <typename Descriptor>
void sort_and_validate_order(std::vector<Descriptor>& descriptors, std::string_view owner) {
    std::ranges::sort(descriptors, {}, &Descriptor::order);
    for (std::size_t index = 1; index < descriptors.size(); ++index) {
        if (descriptors[index - 1].order == descriptors[index].order) {
            throw std::logic_error(
                "duplicate ordinary-form help order for " + std::string(owner));
        }
    }
}

bool requires_storage(PersistenceClass classification) noexcept {
    return classification == PersistenceClass::unclassified ||
           classification == PersistenceClass::persisted_editable ||
           classification == PersistenceClass::persisted_readonly ||
           classification == PersistenceClass::version_specific;
}

bool requires_default(const PropertyDescriptor& descriptor) noexcept {
    return requires_storage(descriptor.persistence) &&
           descriptor.api_access != ApiAccess::read_only;
}

void classify_property(
    std::vector<PropertyDescriptor>& descriptors,
    std::string_view name,
    StorageCodec storage_codec,
    DefaultKind default_kind,
    std::string_view default_value) {
    const auto descriptor = std::ranges::find(
        descriptors,
        name,
        &PropertyDescriptor::api_name);
    if (descriptor == descriptors.end()) {
        throw std::logic_error("missing property for proven storage override: " + std::string(name));
    }
    descriptor->persistence = PersistenceClass::persisted_editable;
    descriptor->storage_codec = storage_codec;
    descriptor->default_value = {default_kind, default_value};
}

void apply_proven_storage_overrides(
    std::array<std::vector<PropertyDescriptor>, control_kind_count>& properties,
    std::vector<PropertyDescriptor>& panel_placement_properties,
    std::vector<PropertyDescriptor>& form_properties,
    std::array<std::vector<EventDescriptor>, control_kind_count>& events) {
    auto& button = properties[static_cast<std::size_t>(ControlKind::button)];
    classify_property(
        button,
        "Enabled",
        StorageCodec::control_base,
        DefaultKind::boolean,
        "true");
    classify_property(
        button,
        "Caption",
        StorageCodec::control_info,
        DefaultKind::string,
        "");

    auto& label_decoration = properties[static_cast<std::size_t>(ControlKind::label_decoration)];
    classify_property(
        label_decoration,
        "HorizontalAlign",
        StorageCodec::control_info,
        DefaultKind::none,
        "");

    auto& check_box = properties[static_cast<std::size_t>(ControlKind::check_box)];
    classify_property(
        check_box,
        "Enabled",
        StorageCodec::control_base,
        DefaultKind::boolean,
        "true");
    classify_property(
        check_box,
        "Caption",
        StorageCodec::control_info,
        DefaultKind::string,
        "");

    classify_property(
        panel_placement_properties,
        "Left",
        StorageCodec::position_record,
        DefaultKind::integer,
        "0");
    classify_property(
        panel_placement_properties,
        "Top",
        StorageCodec::position_record,
        DefaultKind::integer,
        "0");
    classify_property(
        panel_placement_properties,
        "Width",
        StorageCodec::position_record,
        DefaultKind::integer,
        "0");
    classify_property(
        panel_placement_properties,
        "Height",
        StorageCodec::position_record,
        DefaultKind::integer,
        "0");
    classify_property(
        panel_placement_properties,
        "Visible",
        StorageCodec::position_record,
        DefaultKind::boolean,
        "true");

    classify_property(
        form_properties,
        "Caption",
        StorageCodec::root_record,
        DefaultKind::string,
        "");
    classify_property(
        form_properties,
        "Width",
        StorageCodec::root_record,
        DefaultKind::integer,
        "400");
    classify_property(
        form_properties,
        "Height",
        StorageCodec::root_record,
        DefaultKind::integer,
        "300");

    auto& button_events = events[static_cast<std::size_t>(ControlKind::button)];
    const auto click = std::ranges::find(
        button_events,
        std::string_view{"Click"},
        &EventDescriptor::api_name);
    if (click == button_events.end()) {
        throw std::logic_error("missing Button.Click event for proven storage override");
    }
    click->persistence = PersistenceClass::persisted_editable;
    click->storage_codec = StorageCodec::event_record;
    click->storage_tag = "e1692cc2-605b-4535-84dd-28440238746c";
}

}  // namespace

struct Metamodel::Impl {
    std::array<ControlDescriptor, control_kind_count> controls{};
    std::array<std::vector<PropertyDescriptor>, control_kind_count> properties;
    std::array<std::vector<EventDescriptor>, control_kind_count> events;
    std::vector<PropertyDescriptor> control_extension_properties;
    std::vector<PropertyDescriptor> panel_placement_properties;
    std::vector<PropertyDescriptor> form_properties;
    std::vector<EventDescriptor> form_events;

    std::unordered_map<std::string_view, const ControlDescriptor*> controls_by_guid;
    std::unordered_map<std::string_view, const ControlDescriptor*> controls_by_public_name;
    std::unordered_map<std::string_view, const ControlDescriptor*> controls_by_api_name;
    std::unordered_map<std::u8string_view, const ControlDescriptor*> controls_by_russian_name;
    std::array<std::unordered_map<std::string_view, const PropertyDescriptor*>, control_kind_count>
        properties_by_name;
    std::array<std::unordered_map<PropertyId, const PropertyDescriptor*, PropertyIdHash>, control_kind_count>
        properties_by_id;
    std::array<std::unordered_map<std::string_view, const EventDescriptor*>, control_kind_count>
        events_by_name;
    std::unordered_map<std::string_view, const PropertyDescriptor*> form_properties_by_name;
    std::unordered_map<PropertyId, const PropertyDescriptor*, PropertyIdHash> form_properties_by_id;
    std::unordered_map<std::string_view, const EventDescriptor*> form_events_by_name;
    MetamodelCoverage coverage;

    Impl() {
        const auto help_names = make_help_control_names();
        std::array<bool, control_kind_count> help_name_seen{};
        for (const auto& help : help_names) {
            const auto index = static_cast<std::size_t>(help.kind);
            if (index >= control_kind_count || help_name_seen[index]) {
                throw std::logic_error("duplicate or invalid ordinary-form help control");
            }
            help_name_seen[index] = true;
            const auto& identity = control_identities[index];
            if (identity.kind != help.kind) {
                throw std::logic_error("ordinary-form control identity order is invalid");
            }
            controls[index] = {
                identity.kind,
                identity.guid,
                identity.storage_tag,
                help.public_name,
                help.api_name,
                help.russian_name,
                identity.version_mask,
                identity.classification,
                identity.child_policy,
            };
        }
        if (!std::ranges::all_of(help_name_seen, [](bool seen) { return seen; })) {
            throw std::logic_error("ordinary-form help catalog does not cover all controls");
        }

        for (auto descriptor : make_control_property_descriptors()) {
            properties[static_cast<std::size_t>(descriptor.control_kind)].push_back(descriptor);
        }
        for (auto descriptor : make_control_event_descriptors()) {
            events[static_cast<std::size_t>(descriptor.control_kind)].push_back(descriptor);
        }
        control_extension_properties = make_control_extension_property_descriptors();
        panel_placement_properties = make_panel_placement_property_descriptors();
        form_properties = make_form_property_descriptors();
        form_events = make_form_event_descriptors();

        apply_proven_storage_overrides(
            properties,
            panel_placement_properties,
            form_properties,
            events);

        // Help may repeat an inherited extension property on one concrete control.
        // The executable model keeps the shared extension as the single owner.
        for (auto& control_properties : properties) {
            for (const auto& shared : control_extension_properties) {
                const auto duplicate = std::ranges::find(
                    control_properties,
                    shared.api_name,
                    &PropertyDescriptor::api_name);
                if (duplicate == control_properties.end()) {
                    continue;
                }
                if (duplicate->xml_name != shared.xml_name ||
                    duplicate->russian_name != shared.russian_name ||
                    duplicate->platform_type != shared.platform_type ||
                    duplicate->value_kind != shared.value_kind ||
                    duplicate->value_codec != shared.value_codec ||
                    duplicate->api_access != shared.api_access ||
                    duplicate->version_mask != shared.version_mask) {
                    throw std::logic_error(
                        "conflicting inherited ordinary-form control property");
                }
                control_properties.erase(duplicate);
            }
        }

        for (std::size_t index = 0; index < control_kind_count; ++index) {
            sort_and_validate_order(properties[index], controls[index].public_name);
            sort_and_validate_order(events[index], controls[index].public_name);
        }
        sort_and_validate_order(control_extension_properties, "Form control extension");
        sort_and_validate_order(panel_placement_properties, "Panel control extension");
        sort_and_validate_order(form_properties, "Form properties");
        sort_and_validate_order(form_events, "Form events");

        for (const auto& descriptor : controls) {
            const auto add_unique = [](auto& index, auto key, const ControlDescriptor* value) {
                if (key.empty() || !index.emplace(key, value).second) {
                    throw std::logic_error("ordinary-form control name/GUID is empty or duplicated");
                }
            };
            add_unique(controls_by_guid, descriptor.guid, &descriptor);
            add_unique(controls_by_public_name, descriptor.public_name, &descriptor);
            add_unique(controls_by_api_name, descriptor.api_name, &descriptor);
            add_unique(controls_by_russian_name, descriptor.russian_name, &descriptor);
        }

        std::set<std::string_view> unique_property_names;
        std::set<std::string_view> unique_event_names;
        std::unordered_map<PropertyId, std::string_view, PropertyIdHash> property_ids;

        const auto index_property = []<typename NameIndex, typename IdIndex>(
                                        NameIndex& name_index,
                                        IdIndex& id_index,
                                        const PropertyDescriptor& descriptor) {
            const auto add_alias = [&](std::string_view name) {
                const auto [position, inserted] = name_index.emplace(name, &descriptor);
                if (!inserted && position->second != &descriptor &&
                    (position->second->id != descriptor.id ||
                     position->second->api_name != descriptor.api_name ||
                     position->second->value_kind != descriptor.value_kind)) {
                    throw std::logic_error("conflicting property name for ordinary-form owner");
                }
            };
            add_alias(descriptor.xml_name);
            add_alias(descriptor.api_name);
            const auto [id_position, id_inserted] = id_index.emplace(descriptor.id, &descriptor);
            if (!id_inserted && id_position->second->api_name != descriptor.api_name) {
                throw std::logic_error("conflicting property ID for ordinary-form owner");
            }
        };

        const auto account_property = [&](const PropertyDescriptor& descriptor) {
            const auto [position, inserted] = property_ids.emplace(descriptor.id, descriptor.api_name);
            if (!inserted && position->second != descriptor.api_name) {
                ++coverage.property_id_collisions;
            }
            if (descriptor.persistence == PersistenceClass::unclassified) {
                ++coverage.unclassified_properties;
            }
            if (descriptor.value_kind == ValueKind::unknown) {
                ++coverage.unknown_value_kinds;
            }
            if (descriptor.value_codec == ValueCodec::unclassified) {
                ++coverage.unclassified_value_codecs;
            }
            if (requires_default(descriptor) &&
                descriptor.default_value.kind == DefaultKind::unknown) {
                ++coverage.unknown_defaults;
            }
            if (requires_storage(descriptor.persistence) &&
                descriptor.storage_codec == StorageCodec::unclassified) {
                ++coverage.missing_storage_codecs;
            }
        };

        const auto index_event = [](auto& index, const EventDescriptor& descriptor) {
            const auto add_alias = [&](std::string_view name) {
                const auto [position, inserted] = index.emplace(name, &descriptor);
                if (!inserted && position->second != &descriptor) {
                    throw std::logic_error("duplicate event name for ordinary-form owner");
                }
            };
            add_alias(descriptor.xml_name);
            add_alias(descriptor.api_name);
        };

        const auto account_event = [&](const EventDescriptor& descriptor) {
            if (descriptor.persistence == PersistenceClass::unclassified) {
                ++coverage.unclassified_events;
            }
            if (requires_storage(descriptor.persistence) &&
                descriptor.storage_codec == StorageCodec::unclassified) {
                ++coverage.missing_storage_codecs;
            }
        };

        coverage.control_count = controls.size();
        for (std::size_t index = 0; index < control_kind_count; ++index) {
            coverage.control_property_occurrences += properties[index].size();
            coverage.control_event_occurrences += events[index].size();
            for (const auto& descriptor : properties[index]) {
                index_property(properties_by_name[index], properties_by_id[index], descriptor);
                account_property(descriptor);
                unique_property_names.insert(descriptor.api_name);
            }
            for (const auto& descriptor : events[index]) {
                index_event(events_by_name[index], descriptor);
                account_event(descriptor);
                unique_event_names.insert(descriptor.api_name);
            }
        }
        coverage.unique_control_property_names = unique_property_names.size();
        coverage.unique_control_event_names = unique_event_names.size();
        coverage.control_extension_property_count = control_extension_properties.size();
        coverage.panel_placement_property_count = panel_placement_properties.size();
        coverage.form_property_count = form_properties.size();
        coverage.form_event_count = form_events.size();
        for (const auto& descriptor : control_extension_properties) {
            account_property(descriptor);
        }
        for (const auto& descriptor : panel_placement_properties) {
            account_property(descriptor);
        }
        for (std::size_t index = 0; index < control_kind_count; ++index) {
            for (const auto& descriptor : control_extension_properties) {
                index_property(properties_by_name[index], properties_by_id[index], descriptor);
            }
            for (const auto& descriptor : panel_placement_properties) {
                index_property(properties_by_name[index], properties_by_id[index], descriptor);
            }
        }
        for (const auto& descriptor : form_properties) {
            index_property(form_properties_by_name, form_properties_by_id, descriptor);
            account_property(descriptor);
        }
        for (const auto& descriptor : form_events) {
            index_event(form_events_by_name, descriptor);
            account_event(descriptor);
        }
        coverage.release_ready = coverage.control_count == control_kind_count &&
                                 coverage.property_id_collisions == 0 &&
                                 coverage.unclassified_properties == 0 &&
                                 coverage.unclassified_events == 0 &&
                                 coverage.unknown_value_kinds == 0 &&
                                 coverage.unclassified_value_codecs == 0 &&
                                 coverage.unknown_defaults == 0 &&
                                 coverage.missing_storage_codecs == 0;
    }
};

Metamodel::Metamodel() : impl_(std::make_unique<Impl>()) {}

Metamodel::~Metamodel() = default;

const Metamodel& Metamodel::instance() {
    static const Metamodel metamodel;
    return metamodel;
}

std::span<const ControlDescriptor> Metamodel::controls() const noexcept {
    return impl_->controls;
}

std::span<const PropertyDescriptor> Metamodel::form_properties() const noexcept {
    return impl_->form_properties;
}

std::span<const EventDescriptor> Metamodel::form_events() const noexcept {
    return impl_->form_events;
}

std::span<const PropertyDescriptor> Metamodel::control_extension_properties() const noexcept {
    return impl_->control_extension_properties;
}

std::span<const PropertyDescriptor> Metamodel::panel_placement_properties() const noexcept {
    return impl_->panel_placement_properties;
}

std::span<const PropertyDescriptor> Metamodel::properties_for(ControlKind kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < control_kind_count ? std::span<const PropertyDescriptor>(impl_->properties[index])
                                      : std::span<const PropertyDescriptor>{};
}

std::span<const EventDescriptor> Metamodel::events_for(ControlKind kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < control_kind_count ? std::span<const EventDescriptor>(impl_->events[index])
                                      : std::span<const EventDescriptor>{};
}

const ControlDescriptor& Metamodel::control(ControlKind kind) const {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= control_kind_count) {
        throw std::out_of_range("unknown ordinary-form control kind");
    }
    return impl_->controls[index];
}

const ControlDescriptor* Metamodel::control_by_guid(std::string_view guid) const noexcept {
    const auto found = impl_->controls_by_guid.find(guid);
    return found == impl_->controls_by_guid.end() ? nullptr : found->second;
}

const ControlDescriptor* Metamodel::control_by_public_name(std::string_view name) const noexcept {
    const auto found = impl_->controls_by_public_name.find(name);
    return found == impl_->controls_by_public_name.end() ? nullptr : found->second;
}

const ControlDescriptor* Metamodel::control_by_api_name(std::string_view name) const noexcept {
    const auto found = impl_->controls_by_api_name.find(name);
    return found == impl_->controls_by_api_name.end() ? nullptr : found->second;
}

const ControlDescriptor* Metamodel::control_by_russian_name(std::u8string_view name) const noexcept {
    const auto found = impl_->controls_by_russian_name.find(name);
    return found == impl_->controls_by_russian_name.end() ? nullptr : found->second;
}

const PropertyDescriptor* Metamodel::property(
    ControlKind kind,
    std::string_view name) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= control_kind_count) {
        return nullptr;
    }
    const auto found = impl_->properties_by_name[index].find(name);
    return found == impl_->properties_by_name[index].end() ? nullptr : found->second;
}

const PropertyDescriptor* Metamodel::form_property(std::string_view name) const noexcept {
    const auto found = impl_->form_properties_by_name.find(name);
    return found == impl_->form_properties_by_name.end() ? nullptr : found->second;
}

const PropertyDescriptor* Metamodel::property(ControlKind kind, PropertyId id) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= control_kind_count) {
        return nullptr;
    }
    const auto found = impl_->properties_by_id[index].find(id);
    return found == impl_->properties_by_id[index].end() ? nullptr : found->second;
}

const PropertyDescriptor* Metamodel::form_property(PropertyId id) const noexcept {
    const auto found = impl_->form_properties_by_id.find(id);
    return found == impl_->form_properties_by_id.end() ? nullptr : found->second;
}

const EventDescriptor* Metamodel::event(ControlKind kind, std::string_view name) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= control_kind_count) {
        return nullptr;
    }
    const auto found = impl_->events_by_name[index].find(name);
    return found == impl_->events_by_name[index].end() ? nullptr : found->second;
}

const EventDescriptor* Metamodel::form_event(std::string_view name) const noexcept {
    const auto found = impl_->form_events_by_name.find(name);
    return found == impl_->form_events_by_name.end() ? nullptr : found->second;
}

const MetamodelCoverage& Metamodel::coverage() const noexcept {
    return impl_->coverage;
}

std::span<const ControlDescriptor> control_descriptors() noexcept {
    return Metamodel::instance().controls();
}

std::span<const PropertyDescriptor> form_property_descriptors() noexcept {
    return Metamodel::instance().form_properties();
}

std::span<const EventDescriptor> form_event_descriptors() noexcept {
    return Metamodel::instance().form_events();
}

std::span<const PropertyDescriptor> control_extension_property_descriptors() noexcept {
    return Metamodel::instance().control_extension_properties();
}

std::span<const PropertyDescriptor> panel_placement_property_descriptors() noexcept {
    return Metamodel::instance().panel_placement_properties();
}

std::span<const PropertyDescriptor> property_descriptors(ControlKind kind) noexcept {
    return Metamodel::instance().properties_for(kind);
}

std::span<const EventDescriptor> event_descriptors(ControlKind kind) noexcept {
    return Metamodel::instance().events_for(kind);
}

const ControlDescriptor& descriptor_for(ControlKind kind) {
    return Metamodel::instance().control(kind);
}

const ControlDescriptor* find_by_guid(std::string_view guid) noexcept {
    return Metamodel::instance().control_by_guid(guid);
}

const ControlDescriptor* find_by_public_name(std::string_view name) noexcept {
    return Metamodel::instance().control_by_public_name(name);
}

const ControlDescriptor* find_by_api_name(std::string_view name) noexcept {
    return Metamodel::instance().control_by_api_name(name);
}

const ControlDescriptor* find_by_russian_name(std::u8string_view name) noexcept {
    return Metamodel::instance().control_by_russian_name(name);
}

const PropertyDescriptor* find_property(ControlKind kind, std::string_view name) noexcept {
    return Metamodel::instance().property(kind, name);
}

const PropertyDescriptor* find_form_property(std::string_view name) noexcept {
    return Metamodel::instance().form_property(name);
}

const PropertyDescriptor* find_property(ControlKind kind, PropertyId id) noexcept {
    return Metamodel::instance().property(kind, id);
}

const PropertyDescriptor* find_form_property(PropertyId id) noexcept {
    return Metamodel::instance().form_property(id);
}

const EventDescriptor* find_event(ControlKind kind, std::string_view name) noexcept {
    return Metamodel::instance().event(kind, name);
}

const EventDescriptor* find_form_event(std::string_view name) noexcept {
    return Metamodel::instance().form_event(name);
}

const MetamodelCoverage& metamodel_coverage() noexcept {
    return Metamodel::instance().coverage();
}

std::string_view classification_name(ClassificationStatus status) noexcept {
    switch (status) {
        case ClassificationStatus::platform_resource_backed:
            return "platform-resource-backed";
        case ClassificationStatus::corpus_xsd_resource_correlated:
            return "corpus-xsd-resource-correlated";
        case ClassificationStatus::corpus_xsd_correlated:
            return "corpus-xsd-correlated";
        case ClassificationStatus::platform_ui_guid_table_backed:
            return "platform-ui-guid-table-backed";
        case ClassificationStatus::platform_resource_backed_windows_oracle_pending:
            return "platform-resource-backed-windows-oracle-pending";
        case ClassificationStatus::binary_guid_corpus_xsd_correlated:
            return "binary-guid-corpus-xsd-correlated";
        case ClassificationStatus::windows_harness_required:
            return "windows-harness-required";
    }
    return "unknown";
}

std::string_view persistence_name(PersistenceClass classification) noexcept {
    switch (classification) {
        case PersistenceClass::unclassified:
            return "unclassified";
        case PersistenceClass::persisted_editable:
            return "persisted-editable";
        case PersistenceClass::persisted_readonly:
            return "persisted-readonly";
        case PersistenceClass::runtime_only:
            return "runtime-only";
        case PersistenceClass::version_specific:
            return "version-specific";
        case PersistenceClass::unsupported_by_platform:
            return "unsupported-by-platform";
    }
    return "unclassified";
}

std::string_view storage_codec_name(StorageCodec codec) noexcept {
    switch (codec) {
        case StorageCodec::unclassified:
            return "unclassified";
        case StorageCodec::none:
            return "none";
        case StorageCodec::root_record:
            return "root-record";
        case StorageCodec::control_base:
            return "control-base";
        case StorageCodec::control_info:
            return "control-info";
        case StorageCodec::position_record:
            return "position-record";
        case StorageCodec::binding_record:
            return "binding-record";
        case StorageCodec::event_record:
            return "event-record";
        case StorageCodec::value_record:
            return "value-record";
        case StorageCodec::collection_record:
            return "collection-record";
        case StorageCodec::picture_record:
            return "picture-record";
        case StorageCodec::active_x_state:
            return "active-x-state";
    }
    return "unclassified";
}

std::string_view value_kind_name(ValueKind kind) noexcept {
    switch (kind) {
        case ValueKind::unknown:
            return "unknown";
        case ValueKind::boolean:
            return "boolean";
        case ValueKind::number:
            return "number";
        case ValueKind::string:
            return "string";
        case ValueKind::date_time:
            return "date-time";
        case ValueKind::picture:
            return "picture";
        case ValueKind::color:
            return "color";
        case ValueKind::font:
            return "font";
        case ValueKind::border:
            return "border";
        case ValueKind::shortcut:
            return "shortcut";
        case ValueKind::binary:
            return "binary";
        case ValueKind::identifier:
            return "identifier";
        case ValueKind::collection:
            return "collection";
        case ValueKind::enumeration:
            return "enumeration";
        case ValueKind::object:
            return "object";
        case ValueKind::variant:
            return "variant";
    }
    return "unknown";
}

std::string_view value_codec_name(ValueCodec codec) noexcept {
    switch (codec) {
        case ValueCodec::unclassified:
            return "unclassified";
        case ValueCodec::boolean:
            return "boolean";
        case ValueCodec::integer:
            return "integer";
        case ValueCodec::integer32:
            return "integer32";
        case ValueCodec::decimal:
            return "decimal";
        case ValueCodec::string:
            return "string";
        case ValueCodec::localized_string:
            return "localized-string";
        case ValueCodec::formatted_string:
            return "formatted-string";
        case ValueCodec::date:
            return "date";
        case ValueCodec::uuid:
            return "uuid";
        case ValueCodec::composite_id:
            return "composite-id";
        case ValueCodec::type_domain:
            return "type-domain";
        case ValueCodec::enumeration:
            return "enumeration";
        case ValueCodec::color:
            return "color";
        case ValueCodec::font:
            return "font";
        case ValueCodec::picture:
            return "picture";
        case ValueCodec::control_reference:
            return "control-reference";
        case ValueCodec::attribute_reference:
            return "attribute-reference";
        case ValueCodec::command_reference:
            return "command-reference";
    }
    return "unclassified";
}

}  // namespace oof::model::metamodel
