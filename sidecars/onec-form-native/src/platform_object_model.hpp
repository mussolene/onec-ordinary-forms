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
    std::string value_type;
    std::string value;
    std::string source;
    std::string slot_binding;
    std::string slot_codec;
    bool readable = true;
    bool writable = false;
};

struct PlatformObjectMethod {
    std::string name;
    std::string localized_name;
};

struct PlatformObjectEvent {
    std::string name;
    std::string localized_name;
};

struct PlatformObjectCollectionDescriptor {
    std::string name;
    std::string localized_name;
    std::string value_type;
    std::string source;
    std::string slot_binding;
    std::string slot_codec;
    std::size_t count = 0;
    bool readable = true;
    bool writable = false;
};

struct PlatformObject {
    std::string object_id;
    std::string name;
    std::string platform_type;
    std::string type_category;
    std::string type_source;
    std::string path;
    std::string parent_object_id;
    std::vector<PlatformObjectProperty> properties;
    std::vector<PlatformObjectCollectionDescriptor> collections;
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

struct PlatformObjectPropertyEdit {
    std::string name;
    std::string value;
};

struct PlatformObjectEdit {
    std::string object_id;
    std::string platform_type;
    std::vector<PlatformObjectPropertyEdit> properties;
};

struct PlatformFormObjectEdit {
    std::vector<PlatformObjectEdit> objects;
};

inline PlatformObjectProperty make_property(
    std::string name,
    std::string localized_name,
    std::string value,
    std::string source,
    std::string value_type = {},
    std::string slot_binding = {},
    bool writable = false,
    std::string slot_codec = {}
) {
    PlatformObjectProperty property;
    property.name = std::move(name);
    property.localized_name = std::move(localized_name);
    property.value_type = std::move(value_type);
    property.value = std::move(value);
    property.source = std::move(source);
    property.slot_binding = std::move(slot_binding);
    property.slot_codec = std::move(slot_codec);
    property.writable = writable;
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

inline PlatformObjectCollectionDescriptor make_collection_descriptor(
    std::string name,
    std::string localized_name,
    std::string value_type,
    std::size_t count,
    std::string source,
    std::string slot_binding = {},
    bool writable = false,
    std::string slot_codec = "collection-record"
) {
    PlatformObjectCollectionDescriptor collection;
    collection.name = std::move(name);
    collection.localized_name = std::move(localized_name);
    collection.value_type = std::move(value_type);
    collection.count = count;
    collection.source = std::move(source);
    collection.slot_binding = std::move(slot_binding);
    collection.slot_codec = std::move(slot_codec);
    collection.writable = writable;
    return collection;
}

}  // namespace oof::platform::object_model
