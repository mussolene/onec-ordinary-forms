#pragma once

#include <string>
#include <string_view>

#include "oof/model/ordinary_form.hpp"
#include "oof/storage/list_stream.hpp"

namespace oof::storage::value_codec {

void write_localized_string(
    list_stream::ListOutStream& out,
    const model::LocalizedStringValue& value);
[[nodiscard]] model::LocalizedStringValue read_localized_string(
    list_stream::ListInStream& in);

void write_formatted_string(
    list_stream::ListOutStream& out,
    const model::FormattedStringValue& value);
[[nodiscard]] model::FormattedStringValue read_formatted_string(
    list_stream::ListInStream& in);

void write_composite_id(
    list_stream::ListOutStream& out,
    const model::CompositeIdValue& value);
[[nodiscard]] model::CompositeIdValue read_composite_id(
    list_stream::ListInStream& in);

void write_type_domain(
    list_stream::ListOutStream& out,
    const model::TypeDomainPatternValue& value);
[[nodiscard]] model::TypeDomainPatternValue read_type_domain(
    list_stream::ListInStream& in);

void write_style_reference(
    list_stream::ListOutStream& out,
    const model::StyleReference& value);
[[nodiscard]] model::StyleReference read_style_reference(
    list_stream::ListInStream& in);

void write_color(list_stream::ListOutStream& out, const model::ColorValue& value);
[[nodiscard]] model::ColorValue read_color(list_stream::ListInStream& in);

void write_font(list_stream::ListOutStream& out, const model::FontValue& value);
[[nodiscard]] model::FontValue read_font(list_stream::ListInStream& in);

[[nodiscard]] std::string encode_localized_string(
    const model::LocalizedStringValue& value);
[[nodiscard]] model::LocalizedStringValue decode_localized_string(std::string_view text);
[[nodiscard]] std::string encode_formatted_string(
    const model::FormattedStringValue& value);
[[nodiscard]] model::FormattedStringValue decode_formatted_string(std::string_view text);
[[nodiscard]] std::string encode_composite_id(const model::CompositeIdValue& value);
[[nodiscard]] model::CompositeIdValue decode_composite_id(std::string_view text);
[[nodiscard]] std::string encode_type_domain(const model::TypeDomainPatternValue& value);
[[nodiscard]] model::TypeDomainPatternValue decode_type_domain(std::string_view text);
[[nodiscard]] std::string encode_style_reference(const model::StyleReference& value);
[[nodiscard]] model::StyleReference decode_style_reference(std::string_view text);
[[nodiscard]] std::string encode_color(const model::ColorValue& value);
[[nodiscard]] model::ColorValue decode_color(std::string_view text);
[[nodiscard]] std::string encode_font(const model::FontValue& value);
[[nodiscard]] model::FontValue decode_font(std::string_view text);

}  // namespace oof::storage::value_codec
