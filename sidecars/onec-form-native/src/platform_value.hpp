#pragma once

#include <array>
#include <cstdint>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oof/storage/list_stream.hpp"
#include "platform_value_metadata.hpp"

namespace oof::platform {
namespace stream = ::oof::storage::list_stream;
}

namespace oof::platform::value {

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

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_string(type_name);
        out.write_string(value);
        out.end_list();
    }

    static GenericValue deserialize(stream::ListInStream& in) {
        GenericValue value;
        in.begin_list();
        value.type_name = in.read_string();
        value.value = in.read_string();
        in.end_list();
        return value;
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }
};

enum class AbstractRefKind {
    none,
    composite_id,
    qname,
};

inline std::string_view abstract_ref_kind_name(AbstractRefKind kind) {
    switch (kind) {
        case AbstractRefKind::none:
            return "none";
        case AbstractRefKind::composite_id:
            return "CompositeID";
        case AbstractRefKind::qname:
            return "QName";
    }
    return "none";
}

struct AbstractRef {
    AbstractRefKind kind = AbstractRefKind::none;
    CompositeID composite_id;
    std::string qname;

    static AbstractRef composite(CompositeID id) {
        AbstractRef ref;
        ref.kind = AbstractRefKind::composite_id;
        ref.composite_id = std::move(id);
        return ref;
    }

    static AbstractRef named(std::string value) {
        AbstractRef ref;
        ref.kind = AbstractRefKind::qname;
        ref.qname = std::move(value);
        return ref;
    }

    bool empty() const {
        return kind == AbstractRefKind::none;
    }

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_string(std::string(abstract_ref_kind_name(kind)));
        switch (kind) {
            case AbstractRefKind::none:
                break;
            case AbstractRefKind::composite_id:
                composite_id.serialize(out);
                break;
            case AbstractRefKind::qname:
                out.write_string(qname);
                break;
        }
        out.end_list();
    }

    static AbstractRef deserialize(stream::ListInStream& in) {
        AbstractRef ref;
        in.begin_list();
        const std::string kind = in.read_string();
        if (kind == "CompositeID") {
            ref.kind = AbstractRefKind::composite_id;
            ref.composite_id = CompositeID::deserialize(in);
        } else if (kind == "QName") {
            ref.kind = AbstractRefKind::qname;
            ref.qname = in.read_string();
        } else if (kind == "none") {
            ref.kind = AbstractRefKind::none;
        } else {
            throw std::runtime_error("AbstractRef unsupported kind " + kind);
        }
        in.end_list();
        return ref;
    }

    std::string schema_value() const {
        if (kind == AbstractRefKind::qname) {
            return qname;
        }
        if (kind == AbstractRefKind::composite_id) {
            return composite_id.serialize_list_stream();
        }
        return {};
    }
};

struct StyleRef {
    AbstractRef ref;
};

struct PictureRef {
    AbstractRef ref;
};

inline std::string hex_byte(std::uint8_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
        << static_cast<unsigned int>(value);
    return out.str();
}

inline void require_list_field_count(std::uint32_t actual, std::uint32_t expected, std::string_view type_name) {
    if (actual != expected) {
        throw std::runtime_error(
            std::string(type_name) + " unsupported list field count " + std::to_string(actual)
        );
    }
}

enum class ColorKind : std::uint32_t {
    absolute = 0,
    auto_color = 1,
    style_ref = 2,
};

inline ColorKind parse_color_kind(std::uint32_t value) {
    switch (value) {
        case 0:
            return ColorKind::absolute;
        case 1:
            return ColorKind::auto_color;
        case 2:
            return ColorKind::style_ref;
    }
    throw std::runtime_error("Color unsupported kind " + std::to_string(value));
}

struct Color {
    static constexpr std::uint32_t trace_list_field_count = 3;

    ColorKind kind = ColorKind::absolute;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;
    AbstractRef ref;

    static Color absolute_rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        Color color;
        color.kind = ColorKind::absolute;
        color.red = red;
        color.green = green;
        color.blue = blue;
        color.alpha = 255;
        return color;
    }

    static Color auto_color() {
        Color color;
        color.kind = ColorKind::auto_color;
        return color;
    }

    static Color style(AbstractRef ref) {
        Color color;
        color.kind = ColorKind::style_ref;
        color.ref = std::move(ref);
        return color;
    }

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_uint32(trace_list_field_count);
        out.write_uint32(static_cast<std::uint32_t>(kind));
        out.begin_list();
        out.write_uint32(red);
        out.write_uint32(green);
        out.write_uint32(blue);
        out.write_uint32(alpha);
        out.end_list();
        ref.serialize(out);
        out.end_list();
    }

    static Color deserialize(stream::ListInStream& in) {
        Color color;
        in.begin_list();
        require_list_field_count(in.read_uint32(), trace_list_field_count, "Color");
        color.kind = parse_color_kind(in.read_uint32());
        in.begin_list();
        color.red = static_cast<std::uint8_t>(in.read_uint32());
        color.green = static_cast<std::uint8_t>(in.read_uint32());
        color.blue = static_cast<std::uint8_t>(in.read_uint32());
        color.alpha = static_cast<std::uint8_t>(in.read_uint32());
        in.end_list();
        color.ref = AbstractRef::deserialize(in);
        in.end_list();
        return color;
    }

    std::string schema_value() const {
        if (kind == ColorKind::auto_color) {
            return "auto";
        }
        if (kind == ColorKind::style_ref) {
            return ref.schema_value();
        }
        return "#" + hex_byte(red) + hex_byte(green) + hex_byte(blue);
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }
};

enum class FontKind : std::uint32_t {
    absolute = 0,
    windows_font = 1,
    style_item = 2,
    auto_font = 3,
};

inline FontKind parse_font_kind(std::uint32_t value) {
    switch (value) {
        case 0:
            return FontKind::absolute;
        case 1:
            return FontKind::windows_font;
        case 2:
            return FontKind::style_item;
        case 3:
            return FontKind::auto_font;
    }
    throw std::runtime_error("Font unsupported kind " + std::to_string(value));
}

struct Font {
    static constexpr std::uint32_t trace_list_field_count = 6;

    FontKind kind = FontKind::auto_font;
    std::uint32_t mask = 0;
    AbstractRef ref;
    std::string face_name;
    double height = 0.0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeout = false;

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_uint32(trace_list_field_count);
        out.write_uint32(static_cast<std::uint32_t>(kind));
        out.write_uint32(mask);
        ref.serialize(out);
        out.write_string(face_name);
        out.write_double(height);
        out.begin_list();
        out.write_bool(bold);
        out.write_bool(italic);
        out.write_bool(underline);
        out.write_bool(strikeout);
        out.end_list();
        out.end_list();
    }

    static Font deserialize(stream::ListInStream& in) {
        Font font;
        in.begin_list();
        require_list_field_count(in.read_uint32(), trace_list_field_count, "Font");
        font.kind = parse_font_kind(in.read_uint32());
        font.mask = in.read_uint32();
        font.ref = AbstractRef::deserialize(in);
        font.face_name = in.read_string();
        font.height = in.read_double();
        in.begin_list();
        font.bold = in.read_bool();
        font.italic = in.read_bool();
        font.underline = in.read_bool();
        font.strikeout = in.read_bool();
        in.end_list();
        in.end_list();
        return font;
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }
};

enum class BorderType : std::uint32_t {
    without_border = 0,
    single = 1,
    double_line = 2,
    embossed = 3,
    indented = 4,
    underline = 5,
    double_underline = 6,
    rounded = 7,
    overline = 8,
};

inline std::string_view border_type_name(BorderType type) {
    switch (type) {
        case BorderType::without_border:
            return "WithoutBorder";
        case BorderType::single:
            return "Single";
        case BorderType::double_line:
            return "Double";
        case BorderType::embossed:
            return "Embossed";
        case BorderType::indented:
            return "Indented";
        case BorderType::underline:
            return "Underline";
        case BorderType::double_underline:
            return "DoubleUnderline";
        case BorderType::rounded:
            return "Rounded";
        case BorderType::overline:
            return "Overline";
    }
    return "WithoutBorder";
}

inline BorderType parse_border_type(std::uint32_t value) {
    switch (value) {
        case 0:
            return BorderType::without_border;
        case 1:
            return BorderType::single;
        case 2:
            return BorderType::double_line;
        case 3:
            return BorderType::embossed;
        case 4:
            return BorderType::indented;
        case 5:
            return BorderType::underline;
        case 6:
            return BorderType::double_underline;
        case 7:
            return BorderType::rounded;
        case 8:
            return BorderType::overline;
    }
    throw std::runtime_error("V8Border unsupported border type " + std::to_string(value));
}

struct V8Border {
    static constexpr std::uint32_t trace_list_field_count = 3;

    BorderType style = BorderType::without_border;
    std::uint32_t width = 0;
    AbstractRef ref;
    Color color = Color::auto_color();

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_uint32(trace_list_field_count);
        out.write_uint32(static_cast<std::uint32_t>(style));
        out.write_uint32(width);
        ref.serialize(out);
        color.serialize(out);
        out.end_list();
    }

    static V8Border deserialize(stream::ListInStream& in) {
        V8Border border;
        in.begin_list();
        require_list_field_count(in.read_uint32(), trace_list_field_count, "V8Border");
        border.style = parse_border_type(in.read_uint32());
        border.width = in.read_uint32();
        border.ref = AbstractRef::deserialize(in);
        border.color = Color::deserialize(in);
        in.end_list();
        return border;
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }
};

struct V8Picture {
    PictureRef ref;
    std::string storage_id;

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        ref.ref.serialize(out);
        out.write_string(storage_id);
        out.end_list();
    }

    static V8Picture deserialize(stream::ListInStream& in) {
        V8Picture picture;
        in.begin_list();
        picture.ref.ref = AbstractRef::deserialize(in);
        picture.storage_id = in.read_string();
        in.end_list();
        return picture;
    }

    std::string serialize_list_stream() const {
        stream::ListOutStream out;
        serialize(out);
        return out.text();
    }
};

struct ShortCut {
    std::string key;

    void serialize(stream::ListOutStream& out) const {
        out.begin_list();
        out.write_string(key);
        out.end_list();
    }

    static ShortCut deserialize(stream::ListInStream& in) {
        ShortCut shortcut;
        in.begin_list();
        shortcut.key = in.read_string();
        in.end_list();
        return shortcut;
    }
};

struct Date {
    std::string iso_value;

    void serialize(stream::ListOutStream& out) const {
        out.write_string(iso_value);
    }

    static Date deserialize(stream::ListInStream& in) {
        Date date;
        date.iso_value = in.read_string();
        return date;
    }
};

struct Numeric {
    std::string decimal_value;

    void serialize(stream::ListOutStream& out) const {
        out.write_raw_atom(decimal_value);
    }

    static Numeric deserialize(stream::ListInStream& in) {
        Numeric numeric;
        numeric.decimal_value = in.read_raw_atom();
        return numeric;
    }
};

struct PersistenceStorage {
    std::string object_name;
    std::vector<std::uint8_t> bytes;
};

}  // namespace oof::platform::value
