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
    std::string default_value;
    std::string write_policy;
    std::string value_origin;
    std::string source;
    std::string platform_member;
    std::string platform_default;
    std::string slot_binding;
    std::string slot_codec;
    std::string value_object_class;
    std::string value_object_constructor;
    std::string value_object_storage;
    std::string value_object_literal;
    std::string value_object_schema_value;
    std::string value_object_list_stream;
    std::string value_object_owner_member;
    std::string value_object_evidence;
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

    PlatformObjectProperty* property(std::string_view property_name) {
        for (auto& prop : properties) {
            if (prop.name == property_name || prop.localized_name == property_name) {
                return &prop;
            }
        }
        return nullptr;
    }

    std::string get_prop_val(std::string_view property_name) const {
        const auto* prop = property(property_name);
        if (prop == nullptr) {
            throw std::runtime_error("platform object property is not found: " + std::string(property_name));
        }
        if (!prop->readable) {
            throw std::runtime_error("platform object property is not readable: " + std::string(property_name));
        }
        return prop->value;
    }

    void set_prop_val(std::string_view property_name, std::string value) {
        auto* prop = property(property_name);
        if (prop == nullptr) {
            throw std::runtime_error("platform object property is not found: " + std::string(property_name));
        }
        if (!prop->writable) {
            throw std::runtime_error("platform object property is not writable: " + std::string(property_name));
        }
        prop->value = std::move(value);
    }

    bool has_method(std::string_view method_name) const {
        for (const auto& method : methods) {
            if (method.name == method_name || method.localized_name == method_name) {
                return true;
            }
        }
        return false;
    }

    const PlatformObjectCollectionDescriptor* collection_descriptor(std::string_view collection_name) const {
        for (const auto& collection : collections) {
            if (collection.name == collection_name || collection.localized_name == collection_name) {
                return &collection;
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

    std::ptrdiff_t index_of(std::string_view object_id) const {
        for (std::size_t index = 0; index < objects_.size(); ++index) {
            if (objects_[index].object_id == object_id) {
                return static_cast<std::ptrdiff_t>(index);
            }
        }
        return -1;
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
    PlatformObjectCollection attributes;
    PlatformObjectCollection commands;
    PlatformObjectCollection events;

    const PlatformObjectProperty* property(std::string_view property_name) const {
        return form.property(property_name);
    }

    const PlatformObject* find_object_by_id(std::string_view object_id) const {
        if (form.object_id == object_id) {
            return &form;
        }
        for (const auto& object : items.objects()) {
            if (object.object_id == object_id) {
                return &object;
            }
        }
        for (const auto& object : attributes.objects()) {
            if (object.object_id == object_id) {
                return &object;
            }
        }
        for (const auto& object : commands.objects()) {
            if (object.object_id == object_id) {
                return &object;
            }
        }
        for (const auto& object : events.objects()) {
            if (object.object_id == object_id) {
                return &object;
            }
        }
        return nullptr;
    }

    PlatformObject* find_object_by_id(std::string_view object_id) {
        if (form.object_id == object_id) {
            return &form;
        }
        for (auto& object : items.mutable_objects()) {
            if (object.object_id == object_id) {
                return &object;
            }
        }
        for (auto& object : attributes.mutable_objects()) {
            if (object.object_id == object_id) {
                return &object;
            }
        }
        for (auto& object : commands.mutable_objects()) {
            if (object.object_id == object_id) {
                return &object;
            }
        }
        for (auto& object : events.mutable_objects()) {
            if (object.object_id == object_id) {
                return &object;
            }
        }
        return nullptr;
    }

    std::string get_prop_val(std::string_view object_id, std::string_view property_name) const {
        const auto* object = find_object_by_id(object_id);
        if (object == nullptr) {
            throw std::runtime_error("platform object is not found: " + std::string(object_id));
        }
        return object->get_prop_val(property_name);
    }

    void set_prop_val(std::string_view object_id, std::string_view property_name, std::string value) {
        auto* object = find_object_by_id(object_id);
        if (object == nullptr) {
            throw std::runtime_error("platform object is not found: " + std::string(object_id));
        }
        object->set_prop_val(property_name, std::move(value));
    }

    const PlatformObjectCollection& collection(std::string_view collection_name) const {
        if (collection_name == "Items" || collection_name == "Элементы") {
            return items;
        }
        if (collection_name == "Attributes" || collection_name == "Реквизиты") {
            return attributes;
        }
        if (collection_name == "Commands" || collection_name == "Команды") {
            return commands;
        }
        if (collection_name == "Events" || collection_name == "События") {
            return events;
        }
        throw std::runtime_error("platform form collection is not found: " + std::string(collection_name));
    }

    PlatformObjectCollection& collection(std::string_view collection_name) {
        if (collection_name == "Items" || collection_name == "Элементы") {
            return items;
        }
        if (collection_name == "Attributes" || collection_name == "Реквизиты") {
            return attributes;
        }
        if (collection_name == "Commands" || collection_name == "Команды") {
            return commands;
        }
        if (collection_name == "Events" || collection_name == "События") {
            return events;
        }
        throw std::runtime_error("platform form collection is not found: " + std::string(collection_name));
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

    void set_property(std::string name, std::string value) {
        for (auto& property : properties) {
            if (property.name == name) {
                property.value = std::move(value);
                return;
            }
        }
        properties.push_back({std::move(name), std::move(value)});
    }
};

struct PlatformFormObjectEdit {
    std::vector<PlatformObjectEdit> objects;

    PlatformObjectEdit& object(std::string object_id, std::string platform_type = {}) {
        for (auto& item : objects) {
            if (item.object_id == object_id) {
                if (item.platform_type.empty()) {
                    item.platform_type = std::move(platform_type);
                }
                return item;
            }
        }
        PlatformObjectEdit item;
        item.object_id = std::move(object_id);
        item.platform_type = std::move(platform_type);
        objects.push_back(std::move(item));
        return objects.back();
    }

    bool empty() const {
        for (const auto& object_edit : objects) {
            if (!object_edit.properties.empty()) {
                return false;
            }
        }
        return true;
    }
};

inline PlatformObjectProperty make_property(
    std::string name,
    std::string localized_name,
    std::string value,
    std::string source,
    std::string value_type = {},
    std::string slot_binding = {},
    bool writable = false,
    std::string slot_codec = {},
    std::string default_value = {},
    std::string write_policy = {},
    std::string value_origin = "stream",
    std::string platform_member = {},
    std::string platform_default = {}
) {
    PlatformObjectProperty property;
    property.name = std::move(name);
    property.localized_name = std::move(localized_name);
    property.value_type = std::move(value_type);
    property.value = std::move(value);
    property.default_value = std::move(default_value);
    property.write_policy = std::move(write_policy);
    property.value_origin = std::move(value_origin);
    property.source = std::move(source);
    property.platform_member = std::move(platform_member);
    property.platform_default = std::move(platform_default);
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
