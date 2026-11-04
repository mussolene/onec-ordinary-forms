#pragma once

#include <span>
#include <string>

#include "oof/metadata_context.hpp"

namespace oof::source {

inline constexpr std::size_t metadata_xml_max_documents = 16384;
inline constexpr std::size_t metadata_xml_max_document_bytes = 16U * 1024U * 1024U;
inline constexpr std::size_t metadata_xml_max_total_bytes = 128U * 1024U * 1024U;

[[nodiscard]] Result<MetadataContext> parse_metadata_context(
    std::span<const std::string> xml_documents);

}  // namespace oof::source
