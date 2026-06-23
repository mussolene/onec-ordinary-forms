#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ordinary_control_type_registry.hpp"
#include "platform_form_descriptor_join.hpp"
#include "platform_property_registry.hpp"

namespace oof::ordinary::concept_registry {

enum class ConceptKind {
    control,
    property,
};

enum class ConceptStatus {
    accepted,
    proposed,
    diagnostic,
    rejected,
};

struct OrdinaryFormConcept {
    ConceptKind kind = ConceptKind::property;
    ConceptStatus status = ConceptStatus::diagnostic;
    std::string public_name;
    std::string api_name;
    std::string runtime_identity;
    std::string storage_binding;
    std::string codec;
    std::string proof;
    bool readable = true;
    bool writable = false;
};

inline constexpr std::string_view concept_kind_name(ConceptKind kind) {
    switch (kind) {
        case ConceptKind::control:
            return "control";
        case ConceptKind::property:
            return "property";
    }
    return "unknown";
}

inline constexpr std::string_view concept_status_name(ConceptStatus status) {
    switch (status) {
        case ConceptStatus::accepted:
            return "accepted";
        case ConceptStatus::proposed:
            return "proposed";
        case ConceptStatus::diagnostic:
            return "diagnostic";
        case ConceptStatus::rejected:
            return "rejected";
    }
    return "unknown";
}

inline bool is_release_writer_codec(std::string_view codec) {
    return codec == "name-record" ||
           codec == "scalar-flag" ||
           codec == "position-record" ||
           codec == "binding-record" ||
           codec == "control-info-slot" ||
           codec == "event-action-record" ||
           codec == "collection-record";
}

inline ConceptStatus property_status(
    const platform::property_registry::PlatformPropertyDescriptor& descriptor,
    std::string_view origin
) {
    if (descriptor.writable &&
        is_release_writer_codec(platform::property_registry::slot_codec_name(descriptor.slot_codec)) &&
        !descriptor.slot_binding.empty()) {
        return ConceptStatus::accepted;
    }
    if (origin == "static-platform-descriptor" && descriptor.readable && !descriptor.slot_binding.empty()) {
        return ConceptStatus::proposed;
    }
    return ConceptStatus::diagnostic;
}

inline void add_property_concept(
    std::vector<OrdinaryFormConcept>& concepts,
    const platform::property_registry::PlatformPropertyDescriptor& descriptor,
    std::string_view origin
) {
    concepts.push_back({
        ConceptKind::property,
        property_status(descriptor, origin),
        std::string(descriptor.name),
        std::string(descriptor.localized_name),
        std::string(origin),
        std::string(descriptor.slot_binding),
        std::string(platform::property_registry::slot_codec_name(descriptor.slot_codec)),
        std::string(descriptor.source),
        descriptor.readable,
        descriptor.writable,
    });
}

inline ConceptStatus control_status(const control_type::OrdinaryControlTypeBinding& binding) {
    if (binding.public_xml_tag.empty() || binding.guid.empty()) {
        return ConceptStatus::diagnostic;
    }
    if (binding.stream_element.empty()) {
        return ConceptStatus::proposed;
    }
    return ConceptStatus::accepted;
}

inline std::vector<OrdinaryFormConcept> build_concepts() {
    std::vector<OrdinaryFormConcept> concepts;
    concepts.reserve(
        control_type::bindings.size() +
        platform::property_registry::descriptors.size() +
        platform::property_registry::generated_api_descriptors().size());

    for (const auto& binding : control_type::bindings) {
        const auto* descriptor_binding = platform::form_descriptor::binding_for_guid(binding.guid);
        concepts.push_back({
            ConceptKind::control,
            control_status(binding),
            std::string(binding.public_xml_tag),
            std::string(binding.platform_type),
            std::string(binding.guid),
            std::string(binding.stream_element),
            std::string(binding.writer_control_type),
            descriptor_binding != nullptr ? std::string(descriptor_binding->evidence) : std::string(binding.status),
            true,
            true,
        });
    }

    for (const auto& descriptor : platform::property_registry::descriptors) {
        add_property_concept(concepts, descriptor, "static-platform-descriptor");
    }

    const auto& generated = platform::property_registry::generated_api_descriptors();
    const std::size_t generated_schema_count = platform::property_registry::generated_schema_descriptor_count();
    for (std::size_t index = 0; index < generated.size(); ++index) {
        add_property_concept(
            concepts,
            generated[index],
            index < generated_schema_count ? "generated-platform-schema-catalog" : "generated-platform-api-catalog");
    }

    return concepts;
}

struct ConceptRegistryStats {
    std::size_t total = 0;
    std::size_t controls = 0;
    std::size_t properties = 0;
    std::size_t accepted = 0;
    std::size_t proposed = 0;
    std::size_t diagnostic = 0;
    std::size_t rejected = 0;
};

inline ConceptRegistryStats stats_for(const std::vector<OrdinaryFormConcept>& concepts) {
    ConceptRegistryStats stats;
    stats.total = concepts.size();
    for (const auto& concept : concepts) {
        if (concept.kind == ConceptKind::control) {
            ++stats.controls;
        } else if (concept.kind == ConceptKind::property) {
            ++stats.properties;
        }
        if (concept.status == ConceptStatus::accepted) {
            ++stats.accepted;
        } else if (concept.status == ConceptStatus::proposed) {
            ++stats.proposed;
        } else if (concept.status == ConceptStatus::diagnostic) {
            ++stats.diagnostic;
        } else if (concept.status == ConceptStatus::rejected) {
            ++stats.rejected;
        }
    }
    return stats;
}

}  // namespace oof::ordinary::concept_registry
