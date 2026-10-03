#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "oof/model/ordinary_form.hpp"
#include "oof/result.hpp"
#include "oof/storage/list_stream.hpp"

namespace oof::storage::form_stream {

enum class OuterFormat : std::uint8_t {
    v26 = 26,
    v27 = 27,
};

enum class LayoutKind : std::uint8_t {
    form_section_16 = 16,
    form_section_18 = 18,
};

struct StorageLayout {
    OuterFormat outer_format = OuterFormat::v27;
    LayoutKind kind = LayoutKind::form_section_18;
    std::size_t root_arity = 0;
    std::uint32_t form_section_version = 0;
    std::size_t form_section_arity = 0;
    std::uint32_t page_style_section_version = 0;
    std::size_t page_style_section_arity = 0;

    friend bool operator==(const StorageLayout&, const StorageLayout&) = default;
};

struct RuntimeEnvelope {
    model::UuidValue runtime_uuid;
    list_stream::ListValue payload;
};

struct AttributeRecord {
    model::CompositeIdValue id;
    bool main = false;
    bool stored_data = false;
    std::string name;
    model::TypeDomainPatternValue type;

    friend bool operator==(const AttributeRecord&, const AttributeRecord&) = default;
};

struct AttributeLink {
    std::int64_t control_id = 0;
    model::CompositeIdValue attribute_id;

    friend bool operator==(const AttributeLink&, const AttributeLink&) = default;
};

struct AttributesRecord {
    std::uint32_t slot_count = 0;
    std::vector<AttributeRecord> attributes;
    std::vector<AttributeLink> links;

    friend bool operator==(const AttributesRecord&, const AttributesRecord&) = default;
};

[[nodiscard]] Result<RuntimeEnvelope> decode_runtime_envelope(std::string_view text);
[[nodiscard]] Result<std::string> encode_runtime_envelope(const RuntimeEnvelope& envelope);

[[nodiscard]] Result<OuterFormat> probe_outer_format(
    const list_stream::ListValue& payload,
    std::string_view path = "$");
[[nodiscard]] Result<StorageLayout> probe_layout(
    const list_stream::ListValue& payload,
    std::string_view path = "$");

[[nodiscard]] Result<AttributesRecord> decode_attributes(
    const list_stream::ListValue& record,
    std::string_view path = "$/2");
[[nodiscard]] Result<list_stream::ListValue> encode_attributes(
    const AttributesRecord& record);

// Page boundary constraints are distinct from ordinary-control geometry.
// The owner is Form for root pages and the containing Panel for nested pages.
[[nodiscard]] Result<model::Position> decode_page_position(
    const list_stream::ListValue& boundaries,
    std::uint32_t page_index,
    std::optional<model::ControlRef> owner = std::nullopt);
[[nodiscard]] Result<list_stream::ListValue> encode_page_position(
    const model::Position& position,
    std::uint32_t page_index,
    std::optional<model::ControlRef> owner = std::nullopt);

// Product storage boundary. The current executable slice accepts the proven
// 8.5 section-18 layout and fails closed on storage concepts not represented
// by OrdinaryFormDocument.
[[nodiscard]] Result<model::OrdinaryFormDocument> decode_document(
    const list_stream::ListValue& payload,
    std::string_view form_name = "Form");
[[nodiscard]] Result<list_stream::ListValue> encode_document(
    const model::OrdinaryFormDocument& document);

}  // namespace oof::storage::form_stream
