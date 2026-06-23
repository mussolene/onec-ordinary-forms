#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "ordinary_form_concept_registry.hpp"
#include "platform_object_model.hpp"

namespace oof::ordinary::object {

class OrdinaryForm {
public:
    explicit OrdinaryForm(platform::object_model::PlatformFormObject object)
        : object_(std::move(object)) {}

    const platform::object_model::PlatformFormObject& platform_object() const {
        return object_;
    }

    platform::object_model::PlatformFormObject& platform_object() {
        return object_;
    }

    const platform::object_model::PlatformObject& form() const {
        return object_.form;
    }

    platform::object_model::PlatformObject& form() {
        return object_.form;
    }

    const platform::object_model::PlatformObjectCollection& items() const {
        return object_.items;
    }

    const platform::object_model::PlatformObjectCollection& attributes() const {
        return object_.attributes;
    }

    const platform::object_model::PlatformObjectCollection& commands() const {
        return object_.commands;
    }

    const platform::object_model::PlatformObjectCollection& events() const {
        return object_.events;
    }

    const platform::object_model::PlatformObject* find_object(std::string_view object_id) const {
        return object_.find_object_by_id(object_id);
    }

    platform::object_model::PlatformObject* find_object(std::string_view object_id) {
        return object_.find_object_by_id(object_id);
    }

    std::string get_prop_val(std::string_view object_id, std::string_view property_name) const {
        return object_.get_prop_val(object_id, property_name);
    }

    void set_prop_val(std::string_view object_id, std::string_view property_name, std::string value) {
        const auto* concept = accepted_property_concept(property_name);
        if (concept == nullptr) {
            throw std::runtime_error(
                "ordinary form property is not an accepted object-model concept: " +
                std::string(property_name));
        }
        auto* object = object_.find_object_by_id(object_id);
        if (object == nullptr) {
            throw std::runtime_error("ordinary form object is not found: " + std::string(object_id));
        }
        object->set_prop_val(concept->public_name, std::move(value));
        if (concept->public_name == "Name") {
            if (const auto* name = object->property("Name")) {
                object->name = name->value;
            }
        }
    }

    const platform::object_model::PlatformObjectCollection& collection(std::string_view collection_name) const {
        return object_.collection(collection_name);
    }

private:
    static const concept_registry::OrdinaryFormConcept* accepted_property_concept(std::string_view property_name) {
        static const auto concepts = concept_registry::build_concepts();
        for (const auto& concept : concepts) {
            if (concept.kind != concept_registry::ConceptKind::property ||
                concept.status != concept_registry::ConceptStatus::accepted) {
                continue;
            }
            if (concept.public_name == property_name || concept.api_name == property_name) {
                return &concept;
            }
        }
        return nullptr;
    }

    platform::object_model::PlatformFormObject object_;
};

}  // namespace oof::ordinary::object
