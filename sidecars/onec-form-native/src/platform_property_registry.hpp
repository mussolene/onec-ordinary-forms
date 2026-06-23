#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "platform_object_schema.hpp"
#include "platform_runtime_binding.hpp"

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
    control_info_slot,
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

inline constexpr std::array<PlatformPropertyDescriptor, 47> descriptors{{
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
    {"ReadOnly", "ТолькоПросмотр", "Boolean", SlotCodec::control_info_slot, "cf_form_controls_info8:ReadOnly", true, true, "InputField/ChoiceField cf_form_controls_info8 descriptor slot"},
    {"Left", "Лево", "Number", SlotCodec::position_record, "cf_form_controls_position8:left", true, true, "cf_form_controls_position8"},
    {"Top", "Верх", "Number", SlotCodec::position_record, "cf_form_controls_position8:top", true, true, "cf_form_controls_position8"},
    {"Width", "Ширина", "Number", SlotCodec::position_record, "cf_form_controls_position8:width", true, true, "cf_form_controls_position8"},
    {"Height", "Высота", "Number", SlotCodec::position_record, "cf_form_controls_position8:height", true, true, "cf_form_controls_position8"},
    {"Right", "Правая граница", "Number", SlotCodec::position_record, "cf_form_controls_position8:right", true, true, "cf_form_controls_position8"},
    {"Bottom", "Нижняя граница", "Number", SlotCodec::position_record, "cf_form_controls_position8:bottom", true, true, "cf_form_controls_position8"},
    {"Color", "Цвет", "Color", SlotCodec::color_record, "PlatformObjectSchemaMember.slotBinding", true, false, "mngcore logform_layouter.xsd + xdto_root.res:data_ui.xsd Color; pending info8 property semantics"},
    {"Font", "Шрифт", "Font", SlotCodec::font_record, "PlatformObjectSchemaMember.slotBinding", true, false, "mngcore logform_layouter.xsd + xdto_root.res:data_ui.xsd Font; pending info8 property semantics"},
    {"Picture", "Картинка", "Picture", SlotCodec::picture_record, "PlatformObjectSchemaMember.slotBinding", true, false, "mngcore logform_layouter.xsd + xdto_root.res:data_ui.xsd Picture; pending info8 property semantics"},
    {"Binding.top", "Привязка.Верх", "FormControlBinding", SlotCodec::binding_record, "cf_form_controls_position8:binding:top", true, true, "cf_form_controls_position8"},
    {"Binding.bottom", "Привязка.Низ", "FormControlBinding", SlotCodec::binding_record, "cf_form_controls_position8:binding:bottom", true, true, "cf_form_controls_position8"},
    {"Binding.left", "Привязка.Лево", "FormControlBinding", SlotCodec::binding_record, "cf_form_controls_position8:binding:left", true, true, "cf_form_controls_position8"},
    {"Binding.right", "Привязка.Право", "FormControlBinding", SlotCodec::binding_record, "cf_form_controls_position8:binding:right", true, true, "cf_form_controls_position8"},
    {"Binding.verticalCenter", "Привязка.ВертикальныйЦентр", "FormControlBinding", SlotCodec::binding_record, "cf_form_controls_position8:binding:verticalCenter", true, true, "cf_form_controls_position8"},
    {"Binding.horizontalCenter", "Привязка.ГоризонтальныйЦентр", "FormControlBinding", SlotCodec::binding_record, "cf_form_controls_position8:binding:horizontalCenter", true, true, "cf_form_controls_position8"},
    {"DimensionBinding.height", "ПривязкаВысоты.Высота", "FormControlDimensionBinding", SlotCodec::binding_record, "cf_form_controls_position8:dimensionBinding:height", true, true, "cf_form_controls_position8"},
    {"DimensionBinding.minHeight", "ПривязкаВысоты.МинимальнаяВысота", "FormControlDimensionBinding", SlotCodec::binding_record, "cf_form_controls_position8:dimensionBinding:minHeight", true, true, "cf_form_controls_position8"},
    {"DimensionBinding.stretch", "Привязка.Растянуть", "FormControlDimensionBinding", SlotCodec::binding_record, "cf_form_controls_position8:dimensionBinding:stretch", true, true, "cf_form_controls_position8"},
    {"DimensionBinding.width", "ПривязкаШирины.Ширина", "FormControlDimensionBinding", SlotCodec::binding_record, "cf_form_controls_position8:dimensionBinding:width", true, true, "cf_form_controls_position8"},
    {"Events", "События", "FormEvents", SlotCodec::collection_record, "logform.xsd:m_elementEvents/event", true, false, "mngcore logform.xsd Event"},
    {"Attributes", "Реквизиты", "FormAttributes", SlotCodec::collection_record, "logform.xsd:m_pProperties/property", true, false, "mngcore logform.xsd Property"},
    {"Commands", "Команды", "FormCommands", SlotCodec::collection_record, "logform.xsd:m_pCommands/command", true, false, "mngcore logform.xsd Command + cmi.xsd CommandInfo"},
    {"Event.ID", "Событие.Идентификатор", "UUID", SlotCodec::event_action_record, "logform.xsd:Event/id", true, true, "mngcore logform.xsd Event"},
    {"Event.Handler", "Событие.Обработчик", "String", SlotCodec::event_action_record, "logform.xsd:Event@handler", true, true, "mngcore logform.xsd Event"},
    {"Command.ID", "Команда.Идентификатор", "CompositeID", SlotCodec::event_action_record, "logform.xsd:Command/id", true, true, "mngcore logform.xsd Command"},
    {"Command.Name", "Команда.Имя", "String", SlotCodec::event_action_record, "logform.xsd:Command@name", true, true, "mngcore logform.xsd Command"},
    {"Command.Handler", "Команда.Обработчик", "String", SlotCodec::event_action_record, "logform.xsd:Command@handler", true, true, "mngcore logform.xsd Command"},
    {"Command.ModifiesData", "Команда.ИзменяетДанные", "Boolean", SlotCodec::event_action_record, "logform.xsd:Command@modifiesData", true, true, "mngcore logform.xsd Command"},
    {"Attribute.ID", "Реквизит.Идентификатор", "CompositeID", SlotCodec::collection_record, "logform.xsd:Property@id", true, false, "mngcore logform.xsd Property"},
    {"Attribute.Main", "Реквизит.Основной", "Boolean", SlotCodec::collection_record, "logform.xsd:Property@main", true, false, "mngcore logform.xsd Property"},
    {"Attribute.StoredData", "Реквизит.СохраняемыеДанные", "Boolean", SlotCodec::collection_record, "logform.xsd:Property@storedData", true, false, "mngcore logform.xsd Property"},
    {"Clsid", "CLSID", "UUID", SlotCodec::control_info_slot, "cf_form_controls_info8:ActiveXControl:Clsid", true, true, "cf_form_controls_info8 ActiveXControl descriptor"},
    {"State1", "Состояние1", "ActiveXStateBlob", SlotCodec::control_info_slot, "cf_form_controls_info8:ActiveXControl:State1", true, true, "cf_form_controls_info8 ActiveXControl descriptor"},
    {"State2", "Состояние2", "ActiveXStateBlob", SlotCodec::control_info_slot, "cf_form_controls_info8:ActiveXControl:State2", true, true, "cf_form_controls_info8 ActiveXControl descriptor"},
    {"TableColumnsXml", "Колонки", "TableColumns", SlotCodec::none, "cf_form_controls_info8:Table:View:Columns", true, false, "cf_form_controls_info8 Table View Columns"},
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
        case SlotCodec::control_info_slot:
            return "control-info-slot";
        case SlotCodec::collection_record:
            return "collection-record";
    }
    return "unknown";
}

inline SlotCodec slot_codec_from_name(std::string_view codec) {
    if (codec == "name-record") {
        return SlotCodec::name_record;
    }
    if (codec == "scalar-flag") {
        return SlotCodec::scalar_flag;
    }
    if (codec == "position-record") {
        return SlotCodec::position_record;
    }
    if (codec == "color-record") {
        return SlotCodec::color_record;
    }
    if (codec == "font-record") {
        return SlotCodec::font_record;
    }
    if (codec == "border-record") {
        return SlotCodec::border_record;
    }
    if (codec == "picture-record") {
        return SlotCodec::picture_record;
    }
    if (codec == "event-action-record") {
        return SlotCodec::event_action_record;
    }
    if (codec == "binding-record") {
        return SlotCodec::binding_record;
    }
    if (codec == "control-info-slot") {
        return SlotCodec::control_info_slot;
    }
    if (codec == "collection-record") {
        return SlotCodec::collection_record;
    }
    return SlotCodec::none;
}

struct GeneratedDescriptorCatalog {
    std::vector<PlatformPropertyDescriptor> descriptors;
    std::size_t schema_count = 0;
    std::size_t api_count = 0;
};

inline const GeneratedDescriptorCatalog& generated_descriptor_catalog();
inline const std::vector<PlatformPropertyDescriptor>& generated_api_descriptors();

inline const PlatformPropertyDescriptor* find_descriptor(std::string_view property_name) {
    for (const auto& descriptor : descriptors) {
        if (descriptor.name == property_name || descriptor.localized_name == property_name) {
            return &descriptor;
        }
    }
    for (const auto& descriptor : generated_api_descriptors()) {
        if (descriptor.name == property_name || descriptor.localized_name == property_name) {
            return &descriptor;
        }
    }
    return nullptr;
}

inline std::vector<std::string> split_descriptor_csv(std::string_view value) {
    std::vector<std::string> out;
    while (!value.empty()) {
        const std::size_t comma = value.find(',');
        std::string_view item = value.substr(0, comma);
        while (!item.empty() && item.front() == ' ') {
            item.remove_prefix(1);
        }
        while (!item.empty() && item.back() == ' ') {
            item.remove_suffix(1);
        }
        if (!item.empty()) {
            out.emplace_back(item);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        value.remove_prefix(comma + 1);
    }
    return out;
}

inline const PlatformPropertyDescriptor* find_base_descriptor(std::string_view property_name) {
    for (const auto& descriptor : descriptors) {
        if (descriptor.name == property_name || descriptor.localized_name == property_name) {
            return &descriptor;
        }
    }
    return nullptr;
}

inline bool generated_descriptor_exists(
    const std::vector<PlatformPropertyDescriptor>& generated,
    std::string_view property_name
) {
    for (const auto& descriptor : generated) {
        if (descriptor.name == property_name || descriptor.localized_name == property_name) {
            return true;
        }
    }
    return false;
}

inline std::string_view inferred_api_value_type(std::string_view name) {
    if (name == "CaptionPicture" || name == "TitlePicture" ||
        name.find("Picture") != std::string_view::npos) {
        return "Picture";
    }
    if (name.find("Font") != std::string_view::npos) {
        return "Font";
    }
    if (name.find("Color") != std::string_view::npos) {
        return "Color";
    }
    if (name == "AllowClose" || name == "AutoTitle" || name == "ChoiceMode" ||
        name == "CloseOnChoice" || name == "ReadOnly" || name == "ShowTabs" ||
        name.find("Enabled") != std::string_view::npos ||
        name.find("Visible") != std::string_view::npos ||
        name.find("Allow") != std::string_view::npos ||
        name.rfind("Auto", 0) == 0 ||
        name.rfind("Show", 0) == 0) {
        return "Boolean";
    }
    if (name == "ToolTip" || name == "Shortcut" || name == "Format" ||
        name.find("Title") != std::string_view::npos ||
        name.find("Caption") != std::string_view::npos) {
        return "String";
    }
    return "PlatformApiValue";
}

inline std::vector<std::string>& generated_api_descriptor_string_storage() {
    static std::vector<std::string> storage;
    return storage;
}

inline std::string_view store_generated_descriptor_string(std::string value) {
    auto& storage = generated_api_descriptor_string_storage();
    storage.push_back(std::move(value));
    return storage.back();
}

inline const GeneratedDescriptorCatalog& generated_descriptor_catalog() {
    static const GeneratedDescriptorCatalog catalog = [] {
        auto& strings = generated_api_descriptor_string_storage();
        strings.clear();
        strings.reserve(4096);

        GeneratedDescriptorCatalog generated;
        generated.descriptors.reserve(512);

        for (const auto& schema : object_schema::build_platform_object_schemas()) {
            for (const auto& member : schema.xsd_members) {
                if (find_base_descriptor(member.name) != nullptr ||
                    generated_descriptor_exists(generated.descriptors, member.name)) {
                    continue;
                }
                generated.descriptors.push_back({
                    store_generated_descriptor_string(member.name),
                    "",
                    store_generated_descriptor_string(member.value_type),
                    slot_codec_from_name(member.slot_codec),
                    store_generated_descriptor_string(member.slot_binding),
                    true,
                    member.writable,
                    store_generated_descriptor_string(member.source),
                });
                ++generated.schema_count;
            }
        }

        for (const auto& api : runtime_binding::api_objects) {
            for (const auto& property_name : split_descriptor_csv(api.sample_properties)) {
                if (find_base_descriptor(property_name) != nullptr ||
                    generated_descriptor_exists(generated.descriptors, property_name)) {
                    continue;
                }
                generated.descriptors.push_back({
                    store_generated_descriptor_string(property_name),
                    "",
                    inferred_api_value_type(property_name),
                    SlotCodec::none,
                    "",
                    true,
                    false,
                    api.api_source,
                });
                ++generated.api_count;
            }
        }
        return generated;
    }();
    return catalog;
}

inline const std::vector<PlatformPropertyDescriptor>& generated_api_descriptors() {
    return generated_descriptor_catalog().descriptors;
}

inline std::size_t generated_schema_descriptor_count() {
    return generated_descriptor_catalog().schema_count;
}

inline std::size_t generated_api_descriptor_count() {
    return generated_descriptor_catalog().api_count;
}

inline bool can_set_with_current_codec(const PlatformPropertyDescriptor& descriptor) {
    return descriptor.writable &&
           (descriptor.slot_codec == SlotCodec::name_record ||
            descriptor.slot_codec == SlotCodec::scalar_flag ||
            descriptor.slot_codec == SlotCodec::position_record ||
            descriptor.slot_codec == SlotCodec::binding_record ||
            descriptor.slot_codec == SlotCodec::control_info_slot);
}

}  // namespace oof::platform::property_registry
