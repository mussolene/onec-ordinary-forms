#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oof::platform::object_model {

struct PlatformObjectProperty {
    std::string name;
    std::string localized_name;
    std::string value;
    std::string source;
};

struct PlatformObjectMethod {
    std::string name;
    std::string localized_name;
};

struct PlatformObjectEvent {
    std::string name;
    std::string localized_name;
};

struct PlatformObject {
    std::string object_id;
    std::string name;
    std::string platform_type;
    std::string path;
    std::string parent_object_id;
    std::vector<PlatformObjectProperty> properties;
    std::vector<PlatformObjectMethod> methods;
    std::vector<PlatformObjectEvent> events;
    std::vector<std::size_t> children;

    const PlatformObjectProperty* property(std::string_view property_name) const {
        for (const auto& prop : properties) {
            if (prop.name == property_name || prop.localized_name == property_name) {
                return &prop;
            }
        }
        return nullptr;
    }
};

class PlatformObjectCollection {
public:
    void add(PlatformObject object) {
        objects_.push_back(std::move(object));
    }

    std::size_t count() const {
        return objects_.size();
    }

    const PlatformObject& get(std::size_t index) const {
        if (index >= objects_.size()) {
            throw std::out_of_range("platform object collection index is out of range");
        }
        return objects_[index];
    }

    const PlatformObject* find(std::string_view name) const {
        for (const auto& object : objects_) {
            if (object.name == name) {
                return &object;
            }
        }
        return nullptr;
    }

    const std::vector<PlatformObject>& objects() const {
        return objects_;
    }

    std::vector<PlatformObject>& mutable_objects() {
        return objects_;
    }

private:
    std::vector<PlatformObject> objects_;
};

struct PlatformFormObject {
    PlatformObject form;
    PlatformObjectCollection items;

    const PlatformObjectProperty* property(std::string_view property_name) const {
        return form.property(property_name);
    }
};

inline PlatformObjectProperty make_property(
    std::string name,
    std::string localized_name,
    std::string value,
    std::string source
) {
    PlatformObjectProperty property;
    property.name = std::move(name);
    property.localized_name = std::move(localized_name);
    property.value = std::move(value);
    property.source = std::move(source);
    return property;
}

inline PlatformObjectMethod make_method(std::string name, std::string localized_name = {}) {
    PlatformObjectMethod method;
    method.name = std::move(name);
    method.localized_name = std::move(localized_name);
    return method;
}

inline PlatformObjectEvent make_event(std::string name, std::string localized_name = {}) {
    PlatformObjectEvent event;
    event.name = std::move(name);
    event.localized_name = std::move(localized_name);
    return event;
}

}  // namespace oof::platform::object_model
