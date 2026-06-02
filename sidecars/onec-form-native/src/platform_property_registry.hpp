#pragma once

#include <array>
#include <string_view>

namespace oof::platform::property_registry {

enum class SlotCodec {
    none,
    name_record,
    scalar_flag,
    position_record,
    color_record,
    font_record,
    border_record,
    picture_record,
    event_action_record,
    binding_record,
    collection_record,
};

struct PlatformPropertyDescriptor {
    std::string_view name;
    std::string_view localized_name;
    std::string_view value_type;
    SlotCodec slot_codec = SlotCodec::none;
    std::string_view slot_binding;
    bool readable = true;
    bool writable = false;
    std::string_view source;
};

inline constexpr std::array<PlatformPropertyDescriptor, 18> descriptors{{
    {"ObjectID", "", "CompositeID", SlotCodec::none, "", true, false, "materialized-list-stream"},
    {"Name", "Имя", "String", SlotCodec::name_record, "platform-name-record:{14,name,...}", true, true, "platform-name-record"},
    {"Title", "Заголовок", "String", SlotCodec::name_record, "platform-name-record:{14,name,...}", true, true, "platform-name-record-as-initial-title"},
    {"Caption", "Заголовок", "String", SlotCodec::name_record, "platform-name-record:{14,name,...}", true, true, "platform-name-record-as-initial-title"},
    {"Type", "Тип", "TypeDescription", SlotCodec::none, "", true, false, "descriptor-binding"},
    {"Parent", "Родитель", "FormItem", SlotCodec::none, "", true, false, "materialized-parent-chain"},
    {"Path", "", "String", SlotCodec::none, "", true, false, "list-stream-node-path"},
    {"Items", "Элементы", "FormItems", SlotCodec::collection_record, "materialized-object-collection", true, false, "materialized-object-collection"},
    {"RuntimeUUID", "", "UUID", SlotCodec::none, "", true, false, "runtime-form-envelope"},
    {"Visible", "Видимость", "Boolean", SlotCodec::scalar_flag, "control-visible-flag", true, true, "platform-api-catalog"},
    {"Enabled", "Доступность", "Boolean", SlotCodec::scalar_flag, "control-enabled-flag", true, true, "platform-api-catalog"},
    {"Left", "Лево", "Number", SlotCodec::position_record, "cf_form_controls_position8:left", true, true, "cf_form_controls_position8"},
    {"Top", "Верх", "Number", SlotCodec::position_record, "cf_form_controls_position8:top", true, true, "cf_form_controls_position8"},
    {"Width", "Ширина", "Number", SlotCodec::position_record, "cf_form_controls_position8:width", true, true, "cf_form_controls_position8"},
    {"Height", "Высота", "Number", SlotCodec::position_record, "cf_form_controls_position8:height", true, true, "cf_form_controls_position8"},
    {"Color", "Цвет", "Color", SlotCodec::color_record, "ui:Color", true, true, "xdto_root.res:data_ui.xsd Color"},
    {"Font", "Шрифт", "Font", SlotCodec::font_record, "ui:Font", true, true, "xdto_root.res:data_ui.xsd Font"},
    {"Picture", "Картинка", "Picture", SlotCodec::picture_record, "ui:Picture", true, true, "xdto_root.res:data_ui.xsd Picture"},
}};

inline constexpr std::string_view slot_codec_name(SlotCodec codec) {
    switch (codec) {
        case SlotCodec::none:
            return "none";
        case SlotCodec::name_record:
            return "name-record";
        case SlotCodec::scalar_flag:
            return "scalar-flag";
        case SlotCodec::position_record:
            return "position-record";
        case SlotCodec::color_record:
            return "color-record";
        case SlotCodec::font_record:
            return "font-record";
        case SlotCodec::border_record:
            return "border-record";
        case SlotCodec::picture_record:
            return "picture-record";
        case SlotCodec::event_action_record:
            return "event-action-record";
        case SlotCodec::binding_record:
            return "binding-record";
        case SlotCodec::collection_record:
            return "collection-record";
    }
    return "unknown";
}

inline const PlatformPropertyDescriptor* find_descriptor(std::string_view property_name) {
    for (const auto& descriptor : descriptors) {
        if (descriptor.name == property_name || descriptor.localized_name == property_name) {
            return &descriptor;
        }
    }
    return nullptr;
}

inline const PlatformPropertyDescriptor& generic_value_descriptor(std::string_view property_name) {
    static constexpr PlatformPropertyDescriptor fallback{
        "", "", "GenericValue", SlotCodec::none, "", true, false, "platform-api-catalog"
    };
    const auto* descriptor = find_descriptor(property_name);
    return descriptor == nullptr ? fallback : *descriptor;
}

inline bool can_set_with_current_codec(const PlatformPropertyDescriptor& descriptor) {
    return descriptor.writable &&
           (descriptor.slot_codec == SlotCodec::name_record ||
            descriptor.slot_codec == SlotCodec::position_record);
}

}  // namespace oof::platform::property_registry
