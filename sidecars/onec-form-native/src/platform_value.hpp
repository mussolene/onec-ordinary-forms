#pragma once

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform_list_stream.hpp"

namespace oof::platform::value {

struct PlatformSymbol {
    std::string_view name;
    std::string_view provider;
    std::string_view address;
    std::string_view evidence;
};

constexpr std::array<PlatformSymbol, 8> localized_value_symbols{{
    {"core::LocalWString::serialize", "core85.so", "0x57c520", "container 8.5.1.1343"},
    {"core::LocalWString::deserialize", "core85.so", "0x57c610", "container 8.5.1.1343"},
    {"core::LocalWString::addItem", "core85.so", "0x579d40", "container 8.5.1.1343"},
    {"core::FormattedString::serialize", "core85.so", "0x5347b0", "container 8.5.1.1343"},
    {"core::FormattedString::deserialize", "core85.so", "0x534800", "container 8.5.1.1343"},
    {"core::create_local_str_val", "core85.so", "0x579ae0", "container 8.5.1.1343"},
    {"core::load_wstring", "core85.so", "0x61f000", "container 8.5.1.1343"},
    {"core::Thread::getResourceLocale", "core85.so", "0x8d6f60", "container 8.5.1.1343"},
}};

struct ValueSurfaceEntry {
    std::string_view type_name;
    std::string_view platform_symbol;
    std::string_view native_role;
};

constexpr std::array<ValueSurfaceEntry, 15> value_surface{{
    {"CompositeID", "core::CompositeID", "typed metadata identity"},
    {"TypeDomainPattern", "core::TypeDomainPattern", "type-domain descriptor"},
    {"GenericValue", "core::GenericValue", "ValueFromStringInternal/ValueToStringInternal scalar"},
    {"LocalWString", "core::LocalWString", "localized string payload"},
    {"FormattedString", "core::FormattedString", "localized string with formatting flag"},
    {"Color", "core::Color", "UI color value"},
    {"Font", "core::Font", "UI font value"},
    {"V8Border", "core::V8Border", "UI border value"},
    {"V8Picture", "core::V8Picture", "picture value"},
    {"ShortCut", "core::ShortCut", "keyboard shortcut value"},
    {"Date", "core::Date", "date scalar value"},
    {"Numeric", "core::Numeric", "numeric scalar value"},
    {"PersistenceStorage", "core::IInPersistenceStorage/core::IOutPersistenceStorage", "nested persisted object"},
    {"ListInStream", "core::ListInStream", "platform list reader"},
    {"ListOutStream", "core::ListOutStream", "platform list writer"},
}};

struct LocalWStringItem {
    std::string language;
    std::string text;
};

class LocalWString {
public:
    static constexpr std::uint32_t platform_version = 1;
    static constexpr std::uint32_t first_item_language_offset = 0x8;
    static constexpr std::uint32_t first_item_text_offset = 0x20;
    static constexpr std::uint32_t vector_offset = 0x38;
    static constexpr std::uint32_t vector_stride = 0x30;
    static constexpr std::uint32_t vector_language_offset = 0x0;
    static constexpr std::uint32_t vector_text_offset = 0x18;

    LocalWString() = default;
    explicit LocalWString(std::vector<LocalWStringItem> items) : items_(std::move(items)) {}

    const std::vector<LocalWStringItem>& items() const {
        return items_;
    }

    void add_item(std::string language, std::string text) {
        items_.push_back({std::move(language), std::move(text)});
    }

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_uint32(platform_version);
        out.write_uint32(static_cast<std::uint32_t>(items_.size()));
        for (const auto& item : items_) {
            out.begin_list();
            out.write_string(item.language);
            out.write_string(item.text);
            out.end_list();
        }
        out.end_list();
    }

    static LocalWString deserialize(stream::ListInStream& in) {
        in.begin_list();
        const std::uint32_t version = in.read_uint32();
        if (version != platform_version) {
            throw std::runtime_error("LocalWString unsupported platform version " + std::to_string(version));
        }
        const std::uint32_t count = in.read_uint32();
        std::vector<LocalWStringItem> items;
        items.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            in.begin_list();
            LocalWStringItem item;
            item.language = in.read_string();
            item.text = in.read_string();
            in.end_list();
            items.push_back(std::move(item));
        }
        in.end_list();
        return LocalWString(std::move(items));
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }

private:
    std::vector<LocalWStringItem> items_;
};

class FormattedString {
public:
    static constexpr std::uint32_t platform_version = 1;
    static constexpr std::uint32_t formatted_flag_offset = 0x40;

    FormattedString() = default;
    FormattedString(LocalWString value, bool formatted) : value_(std::move(value)), formatted_(formatted) {}

    const LocalWString& value() const {
        return value_;
    }

    bool formatted() const {
        return formatted_;
    }

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_uint32(platform_version);
        value_.serialize(out);
        out.write_bool(formatted_);
        out.end_list();
    }

    static FormattedString deserialize(stream::ListInStream& in) {
        in.begin_list();
        const std::uint32_t version = in.read_uint32();
        if (version != platform_version) {
            throw std::runtime_error("FormattedString unsupported platform version " + std::to_string(version));
        }
        LocalWString value = LocalWString::deserialize(in);
        const bool formatted = in.read_bool();
        in.end_list();
        return FormattedString(std::move(value), formatted);
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }

private:
    LocalWString value_;
    bool formatted_ = false;
};

struct CompositeID {
    std::string value;
};

struct TypeDomainPattern {
    std::string domain;
};

struct GenericValue {
    std::string type_name;
    std::string value;
};

struct Color {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;
};

struct Font {
    std::string name;
    double size = 0.0;
    bool bold = false;
    bool italic = false;
};

struct V8Border {
    std::string style;
    std::uint32_t width = 0;
};

struct V8Picture {
    std::string storage_id;
};

struct ShortCut {
    std::string key;
};

struct Date {
    std::string iso_value;
};

struct Numeric {
    std::string decimal_value;
};

struct PersistenceStorage {
    std::string object_name;
};

}  // namespace oof::platform::value
