#include "oof/storage/form_stream.hpp"

#include <charconv>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "oof/storage/value_codec.hpp"

namespace oof::storage::form_stream {
namespace {

class DecodeFailure final : public std::runtime_error {
public:
    explicit DecodeFailure(Diagnostic diagnostic)
        : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}

    [[nodiscard]] const Diagnostic& diagnostic() const noexcept {
        return diagnostic_;
    }

private:
    Diagnostic diagnostic_;
};

std::string child_path(std::string_view parent, std::size_t index) {
    return std::string(parent) + "/" + std::to_string(index);
}

std::string_view without_utf8_bom(std::string_view text) noexcept {
    constexpr std::string_view bom{"\xef\xbb\xbf", 3};
    return text.starts_with(bom) ? text.substr(bom.size()) : text;
}

std::string describe(const list_stream::ListValue& value) {
    std::string actual = list_stream::dump_compact(value);
    constexpr std::size_t limit = 120;
    if (actual.size() > limit) {
        actual.resize(limit);
        actual += "...";
    }
    return actual;
}

[[noreturn]] void fail(
    std::string code,
    std::string path,
    std::string expected,
    std::string actual,
    std::string message) {
    throw DecodeFailure(Diagnostic{
        std::move(code),
        DiagnosticSeverity::error,
        {},
        std::move(path),
        {},
        std::move(expected),
        std::move(actual),
        std::move(message),
    });
}

void require_list(const list_stream::ListValue& value, std::string_view path) {
    if (!value.is_list) {
        fail(
            "OOF1101",
            std::string(path),
            "list",
            describe(value),
            "Storage record must be a list");
    }
}

void require_arity(
    const list_stream::ListValue& value,
    std::size_t arity,
    std::string_view path) {
    require_list(value, path);
    if (value.items.size() != arity) {
        fail(
            "OOF1102",
            std::string(path),
            "list arity " + std::to_string(arity),
            "list arity " + std::to_string(value.items.size()),
            "Storage record has unexpected arity");
    }
}

const list_stream::ListValue& at(
    const list_stream::ListValue& value,
    std::size_t index,
    std::string_view path) {
    require_list(value, path);
    if (index >= value.items.size()) {
        fail(
            "OOF1103",
            child_path(path, index),
            "existing record slot",
            "missing",
            "Storage record slot is missing");
    }
    return value.items[index];
}

std::string raw_atom(const list_stream::ListValue& value, std::string_view path) {
    if (value.is_list || value.atom_kind != list_stream::ListValue::AtomKind::raw) {
        fail(
            "OOF1104",
            std::string(path),
            "raw atom",
            describe(value),
            "Storage slot has unexpected value kind");
    }
    return value.atom;
}

std::string string_atom(const list_stream::ListValue& value, std::string_view path) {
    if (value.is_list || value.atom_kind != list_stream::ListValue::AtomKind::string) {
        fail(
            "OOF1104",
            std::string(path),
            "string atom",
            describe(value),
            "Storage slot has unexpected value kind");
    }
    return value.atom;
}

template <typename Integer>
Integer integer_atom(const list_stream::ListValue& value, std::string_view path) {
    const std::string atom = raw_atom(value, path);
    Integer result{};
    const char* const begin = atom.data();
    const char* const end = begin + atom.size();
    const auto parsed = std::from_chars(begin, end, result, 10);
    if (atom.empty() || parsed.ec != std::errc{} || parsed.ptr != end) {
        fail(
            "OOF1105",
            std::string(path),
            "base-10 integer",
            atom,
            "Storage integer is malformed or out of range");
    }
    return result;
}

bool bool_atom(const list_stream::ListValue& value, std::string_view path) {
    const std::uint32_t parsed = integer_atom<std::uint32_t>(value, path);
    if (parsed > 1) {
        fail(
            "OOF1105",
            std::string(path),
            "0 or 1",
            std::to_string(parsed),
            "Storage Boolean is malformed");
    }
    return parsed != 0;
}

void require_raw_constant(
    const list_stream::ListValue& value,
    std::string_view expected,
    std::string_view path) {
    const std::string actual = raw_atom(value, path);
    if (actual != expected) {
        fail(
            "OOF1106",
            std::string(path),
            std::string(expected),
            actual,
            "Storage constant does not match the record contract");
    }
}

model::UuidValue uuid_atom(const list_stream::ListValue& value, std::string_view path) {
    try {
        list_stream::ListInStream in(value);
        return model::UuidValue{in.read_guid()};
    } catch (const std::exception& error) {
        fail(
            "OOF1105",
            std::string(path),
            "canonical UUID atom",
            describe(value),
            error.what());
    }
}

model::CompositeIdValue composite_id(
    const list_stream::ListValue& value,
    std::string_view path) {
    try {
        list_stream::ListInStream in(value);
        return value_codec::read_composite_id(in);
    } catch (const std::exception& error) {
        fail(
            "OOF1107",
            std::string(path),
            "CompositeID record",
            describe(value),
            error.what());
    }
}

model::TypeDomainPatternValue type_domain(
    const list_stream::ListValue& value,
    std::string_view path) {
    try {
        list_stream::ListInStream in(value);
        return value_codec::read_type_domain(in);
    } catch (const std::exception& error) {
        fail(
            "OOF1108",
            std::string(path),
            "TypeDomainPattern record",
            describe(value),
            error.what());
    }
}

template <typename T, typename Operation>
Result<T> capture_decode_failure(Operation&& operation) {
    try {
        return Result<T>::success(operation());
    } catch (const DecodeFailure& error) {
        return Result<T>::failure({error.diagnostic()});
    } catch (const std::exception& error) {
        return Result<T>::failure({Diagnostic{
            "OOF1100",
            DiagnosticSeverity::error,
            {},
            "$",
            {},
            "valid ordinary-form storage record",
            {},
            error.what(),
        }});
    }
}

list_stream::ListValue encoded_composite_id(
    const model::CompositeIdValue& value,
    std::string_view path) {
    try {
        return list_stream::parse(value_codec::encode_composite_id(value));
    } catch (const std::exception& error) {
        fail(
            "OOF1107",
            std::string(path),
            "valid CompositeID",
            {},
            error.what());
    }
}

list_stream::ListValue encoded_type_domain(
    const model::TypeDomainPatternValue& value,
    std::string_view path) {
    try {
        return list_stream::parse(value_codec::encode_type_domain(value));
    } catch (const std::exception& error) {
        fail(
            "OOF1108",
            std::string(path),
            "valid TypeDomainPattern",
            {},
            error.what());
    }
}

}  // namespace

Result<RuntimeEnvelope> decode_runtime_envelope(std::string_view text) {
    return capture_decode_failure<RuntimeEnvelope>([text] {
        list_stream::ListValue root;
        try {
            root = list_stream::parse(without_utf8_bom(text));
        } catch (const std::exception& error) {
            fail(
                "OOF1100",
                "$",
                "runtime envelope list",
                {},
                error.what());
        }
        require_arity(root, 3, "$" );
        const std::string marker = string_atom(at(root, 0, "$"), "$/0");
        if (marker != "#") {
            fail(
                "OOF1106",
                "$/0",
                "#",
                marker,
                "Runtime envelope marker is invalid");
        }
        RuntimeEnvelope envelope;
        envelope.runtime_uuid = uuid_atom(at(root, 1, "$"), "$/1");
        envelope.payload = at(root, 2, "$" );
        require_list(envelope.payload, "$/2");
        const auto layout = probe_layout(envelope.payload, "$/2");
        if (!layout) {
            throw DecodeFailure(layout.diagnostics().front());
        }
        return envelope;
    });
}

Result<std::string> encode_runtime_envelope(const RuntimeEnvelope& envelope) {
    return capture_decode_failure<std::string>([&envelope] {
        static_cast<void>(uuid_atom(
            list_stream::ListValue::raw_atom(envelope.runtime_uuid.canonical),
            "$/1"));
        const auto layout = probe_layout(envelope.payload, "$/2");
        if (!layout) {
            throw DecodeFailure(layout.diagnostics().front());
        }
        return list_stream::dump_compact(list_stream::ListValue::list({
            list_stream::ListValue::string_atom("#"),
            list_stream::ListValue::raw_atom(envelope.runtime_uuid.canonical),
            envelope.payload,
        }));
    });
}

Result<OuterFormat> probe_outer_format(
    const list_stream::ListValue& payload,
    std::string_view path) {
    return capture_decode_failure<OuterFormat>([&payload, path] {
        require_list(payload, path);
        if (payload.items.empty()) {
            fail(
                "OOF1110",
                child_path(path, 0),
                "format marker 26 or 27",
                "missing",
                "Form stream has no format marker");
        }
        const std::string marker_path = child_path(path, 0);
        const std::uint32_t marker = integer_atom<std::uint32_t>(payload.items[0], marker_path);
        if (marker == 26) {
            return OuterFormat::v26;
        }
        if (marker == 27) {
            return OuterFormat::v27;
        }
        fail(
            "OOF1111",
            marker_path,
            "supported format marker 26 or 27",
            std::to_string(marker),
            "Ordinary-form stream format is unsupported");
    });
}

Result<StorageLayout> probe_layout(
    const list_stream::ListValue& payload,
    std::string_view path) {
    return capture_decode_failure<StorageLayout>([&payload, path] {
        // 8.2 and 8.5 both emit outer 27; the paired nested records select the layout.
        const auto outer = probe_outer_format(payload, path);
        if (!outer) {
            throw DecodeFailure(outer.diagnostics().front());
        }
        if (outer.value() != OuterFormat::v27) {
            fail(
                "OOF1112",
                child_path(path, 0),
                "outer format 27 with a proven nested layout",
                "26",
                "Ordinary-form outer format has no proven storage layout");
        }

        require_arity(payload, 20, path);
        const std::string form_path = child_path(path, 1);
        const auto& form_section = at(payload, 1, path);
        require_list(form_section, form_path);
        const std::uint32_t form_version = integer_atom<std::uint32_t>(
            at(form_section, 0, form_path),
            child_path(form_path, 0));

        const std::string page_style_path = child_path(path, 13);
        const auto& page_style_section = at(payload, 13, path);
        require_list(page_style_section, page_style_path);

        if (form_version == 16) {
            require_arity(form_section, 11, form_path);
            require_arity(page_style_section, 3, page_style_path);
            require_raw_constant(
                page_style_section.items[0],
                "3",
                child_path(page_style_path, 0));
            return StorageLayout{
                OuterFormat::v27,
                LayoutKind::form_section_16,
                20,
                16,
                11,
                3,
                3,
            };
        }
        if (form_version == 18) {
            require_arity(form_section, 14, form_path);
            require_arity(page_style_section, 11, page_style_path);
            require_raw_constant(
                page_style_section.items[0],
                "10",
                child_path(page_style_path, 0));
            return StorageLayout{
                OuterFormat::v27,
                LayoutKind::form_section_18,
                20,
                18,
                14,
                10,
                11,
            };
        }
        fail(
            "OOF1113",
            child_path(form_path, 0),
            "supported form section version 16 or 18",
            std::to_string(form_version),
            "Ordinary-form nested storage layout is unsupported");
    });
}

Result<AttributesRecord> decode_attributes(
    const list_stream::ListValue& record,
    std::string_view path) {
    return capture_decode_failure<AttributesRecord>([&record, path] {
        require_arity(record, 4, path);
        const auto& version = at(record, 0, path);
        require_arity(version, 1, child_path(path, 0));
        require_raw_constant(version.items[0], "1", child_path(child_path(path, 0), 0));

        AttributesRecord result;
        result.slot_count = integer_atom<std::uint32_t>(
            at(record, 1, path),
            child_path(path, 1));

        const auto& table = at(record, 2, path);
        require_list(table, child_path(path, 2));
        if (table.items.empty()) {
            fail(
                "OOF1103",
                child_path(child_path(path, 2), 0),
                "attribute count",
                "missing",
                "Attribute table has no count");
        }
        const std::uint32_t attribute_count = integer_atom<std::uint32_t>(
            table.items[0],
            child_path(child_path(path, 2), 0));
        if (table.items.size() != static_cast<std::size_t>(attribute_count) + 1) {
            fail(
                "OOF1102",
                child_path(path, 2),
                "list arity " + std::to_string(static_cast<std::size_t>(attribute_count) + 1),
                "list arity " + std::to_string(table.items.size()),
                "Attribute table count does not match its records");
        }
        result.attributes.reserve(attribute_count);
        for (std::uint32_t index = 0; index < attribute_count; ++index) {
            const std::string record_path = child_path(child_path(path, 2), index + 1);
            const auto& item = table.items[index + 1];
            require_arity(item, 6, record_path);
            AttributeRecord attribute;
            attribute.id = composite_id(item.items[0], child_path(record_path, 0));
            attribute.main = bool_atom(item.items[1], child_path(record_path, 1));
            attribute.stored_data = bool_atom(item.items[2], child_path(record_path, 2));
            require_raw_constant(item.items[3], "1", child_path(record_path, 3));
            attribute.name = string_atom(item.items[4], child_path(record_path, 4));
            attribute.type = type_domain(item.items[5], child_path(record_path, 5));
            result.attributes.push_back(std::move(attribute));
        }

        const auto& links = at(record, 3, path);
        require_list(links, child_path(path, 3));
        if (links.items.empty()) {
            fail(
                "OOF1103",
                child_path(child_path(path, 3), 0),
                "attribute-link count",
                "missing",
                "Attribute-link table has no count");
        }
        const std::uint32_t link_count = integer_atom<std::uint32_t>(
            links.items[0],
            child_path(child_path(path, 3), 0));
        if (links.items.size() != static_cast<std::size_t>(link_count) + 1) {
            fail(
                "OOF1102",
                child_path(path, 3),
                "list arity " + std::to_string(static_cast<std::size_t>(link_count) + 1),
                "list arity " + std::to_string(links.items.size()),
                "Attribute-link table count does not match its records");
        }
        result.links.reserve(link_count);
        for (std::uint32_t index = 0; index < link_count; ++index) {
            const std::string link_path = child_path(child_path(path, 3), index + 1);
            const auto& item = links.items[index + 1];
            require_arity(item, 2, link_path);
            AttributeLink link;
            link.control_id = integer_atom<std::int64_t>(item.items[0], child_path(link_path, 0));
            if (link.control_id < 0) {
                fail(
                    "OOF1105",
                    child_path(link_path, 0),
                    "non-negative control ID",
                    std::to_string(link.control_id),
                    "Attribute link uses an invalid control ID");
            }
            const auto& target = item.items[1];
            require_arity(target, 2, child_path(link_path, 1));
            require_raw_constant(
                target.items[0],
                "1",
                child_path(child_path(link_path, 1), 0));
            link.attribute_id = composite_id(
                target.items[1],
                child_path(child_path(link_path, 1), 1));
            result.links.push_back(std::move(link));
        }
        return result;
    });
}

Result<list_stream::ListValue> encode_attributes(const AttributesRecord& record) {
    return capture_decode_failure<list_stream::ListValue>([&record] {
        if (record.attributes.size() > std::numeric_limits<std::uint32_t>::max() ||
            record.links.size() > std::numeric_limits<std::uint32_t>::max()) {
            fail(
                "OOF1112",
                "$/2",
                "attribute and link counts within uint32 range",
                "count overflow",
                "Attribute collection is too large for the platform record");
        }

        std::vector<list_stream::ListValue> attributes;
        attributes.reserve(record.attributes.size() + 1);
        attributes.push_back(
            list_stream::ListValue::raw_atom(std::to_string(record.attributes.size())));
        for (std::size_t index = 0; index < record.attributes.size(); ++index) {
            const auto& attribute = record.attributes[index];
            const std::string record_path = "$/2/2/" + std::to_string(index + 1);
            attributes.push_back(list_stream::ListValue::list({
                encoded_composite_id(attribute.id, record_path + "/0"),
                list_stream::ListValue::raw_atom(attribute.main ? "1" : "0"),
                list_stream::ListValue::raw_atom(attribute.stored_data ? "1" : "0"),
                list_stream::ListValue::raw_atom("1"),
                list_stream::ListValue::string_atom(attribute.name),
                encoded_type_domain(attribute.type, record_path + "/5"),
            }));
        }

        std::vector<list_stream::ListValue> links;
        links.reserve(record.links.size() + 1);
        links.push_back(list_stream::ListValue::raw_atom(std::to_string(record.links.size())));
        for (std::size_t index = 0; index < record.links.size(); ++index) {
            const auto& link = record.links[index];
            const std::string link_path = "$/2/3/" + std::to_string(index + 1);
            if (link.control_id < 0) {
                fail(
                    "OOF1105",
                    link_path + "/0",
                    "non-negative control ID",
                    std::to_string(link.control_id),
                    "Attribute link uses an invalid control ID");
            }
            links.push_back(list_stream::ListValue::list({
                list_stream::ListValue::raw_atom(std::to_string(link.control_id)),
                list_stream::ListValue::list({
                    list_stream::ListValue::raw_atom("1"),
                    encoded_composite_id(link.attribute_id, link_path + "/1/1"),
                }),
            }));
        }

        auto encoded = list_stream::ListValue::list({
            list_stream::ListValue::list({list_stream::ListValue::raw_atom("1")}),
            list_stream::ListValue::raw_atom(std::to_string(record.slot_count)),
            list_stream::ListValue::list(std::move(attributes)),
            list_stream::ListValue::list(std::move(links)),
        });
        const auto validation = decode_attributes(encoded);
        if (!validation) {
            throw DecodeFailure(validation.diagnostics().front());
        }
        return encoded;
    });
}

}  // namespace oof::storage::form_stream
