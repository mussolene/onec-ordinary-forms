#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
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

constexpr std::array<PlatformSymbol, 38> localized_value_symbols{{
    {"core::ListInStream::ListInStream(IReader*)", "core85.so", "0x56fd70", "container 8.5.1.1343"},
    {"core::ListInStream::ListInStream(IFile*)", "core85.so", "0x56fe90", "container 8.5.1.1343"},
    {"core::ListInStream::ListInStream(IListInStream*)", "core85.so", "0x56fff0", "container 8.5.1.1343"},
    {"core::ListOutStream::ListOutStream(IWriter*)", "core85.so", "0x5702f0", "container 8.5.1.1343"},
    {"core::ListOutStream::ListOutStream(IFile*)", "core85.so", "0x570410", "container 8.5.1.1343"},
    {"core::ListOutStream::ListOutStream(IListOutStream*)", "core85.so", "0x570570", "container 8.5.1.1343"},
    {"core::ListOutStream::close", "core85.so", "0x5705d0", "container 8.5.1.1343"},
    {"core::CompositeID::serialize", "core85.so", "0x4f3780", "container 8.5.1.1343"},
    {"core::CompositeID::deserialize", "core85.so", "0x4f37d0", "container 8.5.1.1343"},
    {"core::kNullCompositeID", "core85.so", "0xb0a618", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::serialize", "core85.so", "0x6d5ac0", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::deserialize", "core85.so", "0x6d5ec0", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::addType", "core85.so", "0x6d50f0", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::setNumericQualifiers", "core85.so", "0x6d5320", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::setStringQualifiers", "core85.so", "0x6d5410", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::setDateQualifiers", "core85.so", "0x6d5500", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::setBinaryQualifiers", "core85.so", "0x6d55e0", "container 8.5.1.1343"},
    {"core::GenericValue::serialize", "core85.so", "0x6d1fb0", "container 8.5.1.1343"},
    {"core::GenericValue::deserialize", "core85.so", "0x6d23d0", "container 8.5.1.1343"},
    {"core::GenericValue::fromString", "core85.so", "0x6d2f70", "container 8.5.1.1343"},
    {"core::LocalWString::serialize", "core85.so", "0x57c520", "container 8.5.1.1343"},
    {"core::LocalWString::deserialize", "core85.so", "0x57c610", "container 8.5.1.1343"},
    {"core::LocalWString::addItem", "core85.so", "0x579d40", "container 8.5.1.1343"},
    {"core::FormattedString::serialize", "core85.so", "0x5347b0", "container 8.5.1.1343"},
    {"core::FormattedString::deserialize", "core85.so", "0x534800", "container 8.5.1.1343"},
    {"core::create_local_str_val", "core85.so", "0x579ae0", "container 8.5.1.1343"},
    {"core::load_wstring", "core85.so", "0x61f000", "container 8.5.1.1343"},
    {"core::Thread::getResourceLocale", "core85.so", "0x8d6f60", "container 8.5.1.1343"},
    {"core::Color::serialize", "core85.so", "0x6bcf10", "container 8.5.1.1343"},
    {"core::Color::deserialize", "core85.so", "0x6bcf70", "container 8.5.1.1343"},
    {"core::Font::serialize", "core85.so", "0x6bda70", "container 8.5.1.1343"},
    {"core::Font::deserialize", "core85.so", "0x6bdd00", "container 8.5.1.1343"},
    {"core::V8Border::serialize", "core85.so", "0x6be670", "container 8.5.1.1343"},
    {"core::V8Border::deserialize", "core85.so", "0x6be700", "container 8.5.1.1343"},
    {"core::V8Picture::to_storage", "core85.so", "0x5df650", "container 8.5.1.1343"},
    {"core::V8Picture::from_storage", "core85.so", "0x5df850", "container 8.5.1.1343"},
    {"core::ShortCut::to_stream", "core85.so", "0x4c2790", "container 8.5.1.1343"},
    {"core::ShortCut::from_stream", "core85.so", "0x4c27e0", "container 8.5.1.1343"},
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

inline bool is_null_guid(std::string_view guid) {
    return guid == "00000000-0000-0000-0000-000000000000";
}

struct CompositeID {
    static constexpr std::uint32_t object_id_offset = 0x0;
    static constexpr std::uint32_t guid_offset = 0x8;
    static constexpr std::uint32_t null_flag_offset = 0x18;

    std::int64_t object_id = 0;
    std::string guid = "00000000-0000-0000-0000-000000000000";
    bool null = true;

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_int64(object_id);
        if (!null) {
            out.write_guid(guid);
        }
        out.end_list();
    }

    static CompositeID deserialize(stream::ListInStream& in) {
        CompositeID id;
        in.begin_list();
        id.object_id = in.read_int64();
        if (in.has_next()) {
            id.guid = in.read_guid();
            id.null = id.object_id == 0 && is_null_guid(id.guid);
        } else {
            id.guid = "00000000-0000-0000-0000-000000000000";
            id.null = true;
        }
        in.end_list();
        return id;
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }
};

enum class TypeDomainTerm {
    unknown,
    list,
    binary,
    date,
    numeric,
    reference,
    string,
    type,
};

inline std::string_view term_name(TypeDomainTerm term) {
    switch (term) {
        case TypeDomainTerm::list:
            return "L";
        case TypeDomainTerm::binary:
            return "B";
        case TypeDomainTerm::date:
            return "D";
        case TypeDomainTerm::numeric:
            return "N";
        case TypeDomainTerm::reference:
            return "R";
        case TypeDomainTerm::string:
            return "S";
        case TypeDomainTerm::type:
            return "T";
        case TypeDomainTerm::unknown:
            return "#";
    }
    return "#";
}

inline TypeDomainTerm parse_term(std::string_view term) {
    if (term == "L") {
        return TypeDomainTerm::list;
    }
    if (term == "B") {
        return TypeDomainTerm::binary;
    }
    if (term == "D") {
        return TypeDomainTerm::date;
    }
    if (term == "N") {
        return TypeDomainTerm::numeric;
    }
    if (term == "R") {
        return TypeDomainTerm::reference;
    }
    if (term == "S") {
        return TypeDomainTerm::string;
    }
    if (term == "T") {
        return TypeDomainTerm::type;
    }
    return TypeDomainTerm::unknown;
}

struct NumericQualifiers {
    std::uint32_t length = 0;
    std::uint32_t precision = 0;
    bool non_negative = false;
};

struct LengthQualifiers {
    std::uint32_t length = 0;
    bool variable = true;
};

struct DateQualifiers {
    bool date = true;
    bool time = true;
};

struct TypeDomainEntry {
    TypeDomainTerm term = TypeDomainTerm::unknown;
    std::string type_guid;
    NumericQualifiers numeric;
    LengthQualifiers string;
    LengthQualifiers binary;
    DateQualifiers date;
};

struct TypeDomainPattern {
    static constexpr std::string_view root_term = "Pattern";
    static constexpr std::string_view unknown_entry_term = "#";
    static constexpr std::string_view type_term_guid = "d47d59f8-73f0-481c-8b5e-f6384c0a4804";

    std::vector<TypeDomainEntry> entries;

    void add_type(std::string guid) {
        TypeDomainEntry entry;
        entry.term = TypeDomainTerm::type;
        entry.type_guid = std::move(guid);
        entries.push_back(std::move(entry));
    }

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_string(std::string(root_term));
        for (const auto& entry : entries) {
            out.begin_list();
            out.write_string(std::string(term_name(entry.term)));
            switch (entry.term) {
                case TypeDomainTerm::type:
                    if (!entry.type_guid.empty()) {
                        out.write_guid(entry.type_guid);
                    }
                    break;
                case TypeDomainTerm::numeric:
                    if (entry.numeric.length != 0 || entry.numeric.precision != 0 || entry.numeric.non_negative) {
                        out.write_uint32(entry.numeric.length);
                        out.write_uint32(entry.numeric.precision);
                        out.write_bool(entry.numeric.non_negative);
                    }
                    break;
                case TypeDomainTerm::string:
                    if (entry.string.length != 0) {
                        out.write_uint32(entry.string.length);
                        out.write_bool(entry.string.variable);
                    }
                    break;
                case TypeDomainTerm::binary:
                    if (entry.binary.length != 0) {
                        out.write_uint32(entry.binary.length);
                        out.write_bool(entry.binary.variable);
                    }
                    break;
                case TypeDomainTerm::date:
                    if (!(entry.date.date && entry.date.time)) {
                        std::string flags;
                        if (entry.date.date) {
                            flags += "D";
                        }
                        if (entry.date.time) {
                            flags += "T";
                        }
                        out.write_string(flags);
                    }
                    break;
                case TypeDomainTerm::reference:
                case TypeDomainTerm::list:
                case TypeDomainTerm::unknown:
                    if (!entry.type_guid.empty()) {
                        out.write_guid(entry.type_guid);
                    }
                    break;
            }
            out.end_list();
        }
        out.end_list();
    }

    static TypeDomainPattern deserialize(stream::ListInStream& in) {
        TypeDomainPattern pattern;
        in.begin_list();
        const std::string root = in.read_string();
        if (root != root_term) {
            throw std::runtime_error("TypeDomainPattern unsupported root term " + root);
        }
        while (in.has_next()) {
            in.begin_list();
            TypeDomainEntry entry;
            entry.term = parse_term(in.read_string());
            switch (entry.term) {
                case TypeDomainTerm::type:
                    if (in.has_next()) {
                        entry.type_guid = in.read_guid();
                    }
                    break;
                case TypeDomainTerm::numeric:
                    if (in.has_next()) {
                        entry.numeric.length = in.read_uint32();
                        entry.numeric.precision = in.read_uint32();
                        entry.numeric.non_negative = in.read_bool();
                    }
                    break;
                case TypeDomainTerm::string:
                    if (in.has_next()) {
                        entry.string.length = in.read_uint32();
                        entry.string.variable = in.read_bool();
                    }
                    break;
                case TypeDomainTerm::binary:
                    if (in.has_next()) {
                        entry.binary.length = in.read_uint32();
                        entry.binary.variable = in.read_bool();
                    }
                    break;
                case TypeDomainTerm::date:
                    if (in.has_next()) {
                        const std::string flags = in.read_string();
                        entry.date.date = flags.find('D') != std::string::npos;
                        entry.date.time = flags.find('T') != std::string::npos;
                    }
                    break;
                case TypeDomainTerm::reference:
                case TypeDomainTerm::list:
                case TypeDomainTerm::unknown:
                    if (in.has_next()) {
                        entry.type_guid = in.read_guid();
                    }
                    break;
            }
            in.end_list();
            pattern.entries.push_back(std::move(entry));
        }
        in.end_list();
        return pattern;
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }
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
