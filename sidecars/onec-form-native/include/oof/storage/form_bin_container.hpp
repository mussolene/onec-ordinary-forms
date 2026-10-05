#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace oof::storage::formbin {

constexpr std::int32_t container_end_marker = 0x7fffffff;
constexpr std::int32_t container_block_size = 0x200;
constexpr std::size_t container_header_size = 16;
constexpr std::size_t container_toc_block_size = 0x200;
constexpr std::size_t container_document_block_size = 0xa000;
inline constexpr std::size_t max_container_block_visits = 1024 * 1024;
inline constexpr std::size_t max_container_block_header_size = 64;

struct OneCContainerFile {
    std::string name;
    std::uint64_t created = 0;
    std::uint64_t modified = 0;
    std::vector<std::uint8_t> payload;
};

struct OneCContainer {
    std::int32_t block_size = 0;
    std::vector<OneCContainerFile> files;
};

inline std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("Form.bin container integer is truncated");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline std::uint64_t read_u64_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::runtime_error("Form.bin container integer is truncated");
    }
    const std::uint64_t low = read_u32_le(bytes, offset);
    const std::uint64_t high = read_u32_le(bytes, offset + 4);
    return low | (high << 32);
}

inline void write_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

inline void write_u64_le(std::vector<std::uint8_t>& out, std::uint64_t value) {
    write_u32_le(out, static_cast<std::uint32_t>(value & 0xffffffffu));
    write_u32_le(out, static_cast<std::uint32_t>(value >> 32));
}

namespace detail {

struct ContainerReadBudget {
    explicit ContainerReadBudget(std::size_t input_size)
        : remaining_block_visits(std::min(input_size, max_container_block_visits)) {}

    std::size_t remaining_block_visits;
};

inline void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point < 0x80) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else if (code_point < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
}

inline std::uint32_t read_utf8_code_point(std::string_view value, std::size_t& offset) {
    if (offset >= value.size()) {
        throw std::runtime_error("truncated UTF-8 Form.bin file name");
    }
    const auto byte = [&](std::size_t index) {
        return static_cast<std::uint8_t>(value[index]);
    };
    const std::uint8_t first = byte(offset++);
    if (first < 0x80) {
        return first;
    }

    std::size_t continuation_count = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        continuation_count = 1;
        code_point = first & 0x1f;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        continuation_count = 2;
        code_point = first & 0x0f;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        continuation_count = 3;
        code_point = first & 0x07;
        minimum = 0x10000;
    } else {
        throw std::runtime_error("invalid UTF-8 leading byte in Form.bin file name");
    }
    if (continuation_count > value.size() - offset) {
        throw std::runtime_error("truncated UTF-8 Form.bin file name");
    }
    for (std::size_t index = 0; index < continuation_count; ++index) {
        const std::uint8_t current = byte(offset++);
        if ((current & 0xc0) != 0x80) {
            throw std::runtime_error("invalid UTF-8 continuation byte in Form.bin file name");
        }
        code_point = (code_point << 6) | (current & 0x3f);
    }
    if (code_point < minimum || code_point > 0x10ffff ||
        (code_point >= 0xd800 && code_point <= 0xdfff)) {
        throw std::runtime_error("invalid UTF-8 code point in Form.bin file name");
    }
    return code_point;
}

inline void append_utf16le(std::vector<std::uint8_t>& out, std::uint16_t code_unit) {
    out.push_back(static_cast<std::uint8_t>(code_unit & 0xff));
    out.push_back(static_cast<std::uint8_t>(code_unit >> 8));
}

}  // namespace detail

inline std::string utf16le_name_to_utf8(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::string out;
    while (true) {
        if (offset > bytes.size() || bytes.size() - offset < 2) {
            throw std::runtime_error("unterminated UTF-16 Form.bin file name");
        }
        const std::uint16_t code_unit = static_cast<std::uint16_t>(bytes[offset]) |
                                        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
        offset += 2;
        if (code_unit == 0) {
            return out;
        }

        std::uint32_t code_point = code_unit;
        if (code_unit >= 0xd800 && code_unit <= 0xdbff) {
            if (bytes.size() - offset < 2) {
                throw std::runtime_error("truncated UTF-16 surrogate pair in Form.bin file name");
            }
            const std::uint16_t low = static_cast<std::uint16_t>(bytes[offset]) |
                                      (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
            if (low < 0xdc00 || low > 0xdfff) {
                throw std::runtime_error("invalid UTF-16 surrogate pair in Form.bin file name");
            }
            offset += 2;
            code_point = 0x10000 + ((static_cast<std::uint32_t>(code_unit) - 0xd800) << 10) +
                         (static_cast<std::uint32_t>(low) - 0xdc00);
        } else if (code_unit >= 0xdc00 && code_unit <= 0xdfff) {
            throw std::runtime_error("unpaired UTF-16 surrogate in Form.bin file name");
        }
        detail::append_utf8(out, code_point);
    }
}

inline std::vector<std::uint8_t> file_descriptor_payload(
    const std::string& name,
    std::uint64_t created,
    std::uint64_t modified
) {
    std::vector<std::uint8_t> out;
    write_u64_le(out, created);
    write_u64_le(out, modified);
    write_u32_le(out, 0);
    std::size_t offset = 0;
    while (offset < name.size()) {
        const std::uint32_t code_point = detail::read_utf8_code_point(name, offset);
        if (code_point == 0) {
            throw std::runtime_error("Form.bin file name contains a null character");
        }
        if (code_point <= 0xffff) {
            detail::append_utf16le(out, static_cast<std::uint16_t>(code_point));
        } else {
            const std::uint32_t surrogate = code_point - 0x10000;
            detail::append_utf16le(
                out,
                static_cast<std::uint16_t>(0xd800 + (surrogate >> 10)));
            detail::append_utf16le(
                out,
                static_cast<std::uint16_t>(0xdc00 + (surrogate & 0x3ff)));
        }
    }
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
    return out;
}

struct BlockHeader {
    std::uint32_t document_size = 0;
    std::uint32_t current_size = 0;
    std::uint32_t next_offset = 0;
    std::size_t payload_offset = 0;
};

inline BlockHeader read_block_header(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 2 || data[offset] != '\r' ||
        data[offset + 1] != '\n') {
        throw std::runtime_error("invalid 1C container block header prefix");
    }
    std::size_t header_end = offset + 2;
    while (header_end + 1 < data.size() &&
           !(data[header_end] == '\r' && data[header_end + 1] == '\n')) {
        if (header_end - offset >= max_container_block_header_size - 2) {
            throw std::runtime_error("1C container resource limit: block header exceeds 64 bytes");
        }
        ++header_end;
    }
    if (header_end + 1 >= data.size()) {
        throw std::runtime_error("invalid 1C container block header suffix");
    }
    const std::string header(
        reinterpret_cast<const char*>(&data[offset + 2]), header_end - offset - 2);
    std::istringstream in(header);
    BlockHeader result;
    in >> std::hex >> result.document_size >> result.current_size >> result.next_offset;
    if (!in) {
        throw std::runtime_error("invalid 1C container block header fields");
    }
    in >> std::ws;
    if (!in.eof()) {
        throw std::runtime_error("invalid 1C container block header fields");
    }
    result.payload_offset = header_end + 2;
    return result;
}

inline std::vector<std::uint8_t> read_document(
    const std::vector<std::uint8_t>& data,
    std::size_t offset,
    detail::ContainerReadBudget* shared_budget = nullptr
) {
    detail::ContainerReadBudget local_budget(data.size());
    auto& remaining_block_visits = shared_budget == nullptr
        ? local_budget.remaining_block_visits : shared_budget->remaining_block_visits;
    std::vector<std::uint8_t> payload;
    std::uint32_t total_size = 0;
    bool has_total_size = false;
    std::size_t current_offset = offset;
    std::unordered_set<std::size_t> visited_offsets;
    while (true) {
        if (current_offset >= data.size()) {
            throw std::runtime_error("1C container block offset exceeds file size");
        }
        if (remaining_block_visits == 0) {
            throw std::runtime_error("1C container resource limit: block traversal budget exhausted");
        }
        --remaining_block_visits;
        if (!visited_offsets.insert(current_offset).second) {
            throw std::runtime_error("1C container block chain contains a cycle");
        }

        const BlockHeader header = read_block_header(data, current_offset);
        if (!has_total_size) {
            total_size = header.document_size;
            if (total_size > data.size()) {
                throw std::runtime_error("1C container document size exceeds file size");
            }
            has_total_size = true;
        }
        if (header.payload_offset > data.size() ||
            header.current_size > data.size() - header.payload_offset) {
            throw std::runtime_error("1C container block payload exceeds file size");
        }
        // Fixed-size blocks may contain padding after the logical document.
        // Validate the complete chain, but never accumulate that padding.
        const std::size_t copy_size = std::min<std::size_t>(
            header.current_size, total_size - payload.size());
        const std::size_t payload_end = header.payload_offset + copy_size;
        payload.insert(
            payload.end(),
            data.begin() + static_cast<std::ptrdiff_t>(header.payload_offset),
            data.begin() + static_cast<std::ptrdiff_t>(payload_end));
        if (header.next_offset == static_cast<std::uint32_t>(container_end_marker)) {
            break;
        }
        if (header.next_offset >= data.size()) {
            throw std::runtime_error("1C container next block offset exceeds file size");
        }
        current_offset = header.next_offset;
    }
    if (payload.size() < total_size) {
        throw std::runtime_error("1C container document is smaller than its declared size");
    }
    return payload;
}

inline OneCContainer parse_container(const std::vector<std::uint8_t>& data) {
    if (data.size() < container_header_size) {
        throw std::runtime_error("invalid Form.bin container: too small");
    }
    const auto end_marker = static_cast<std::int32_t>(read_u32_le(data, 0));
    const auto block_size = static_cast<std::int32_t>(read_u32_le(data, 4));
    const auto file_count = static_cast<std::int32_t>(read_u32_le(data, 8));
    const auto reserved = static_cast<std::int32_t>(read_u32_le(data, 12));
    if (end_marker != container_end_marker || block_size <= 0 || file_count < 0 || reserved != 0) {
        throw std::runtime_error("invalid Form.bin container header");
    }

    detail::ContainerReadBudget budget(data.size());
    const std::vector<std::uint8_t> toc = read_document(data, container_header_size, &budget);
    if (static_cast<std::size_t>(file_count) > toc.size() / 12 ||
        toc.size() != static_cast<std::size_t>(file_count) * 12) {
        throw std::runtime_error("invalid Form.bin container TOC: entry count mismatch");
    }
    OneCContainer result;
    result.block_size = block_size;
    std::size_t remaining_bytes = data.size() - toc.size();
    for (std::int32_t index = 0; index < file_count; ++index) {
        const std::size_t entry_offset = static_cast<std::size_t>(index) * 12;
        const std::uint32_t descriptor_offset = read_u32_le(toc, entry_offset);
        const std::uint32_t payload_offset = read_u32_le(toc, entry_offset + 4);
        const auto entry_marker = static_cast<std::int32_t>(read_u32_le(toc, entry_offset + 8));
        if (entry_marker != container_end_marker) {
            throw std::runtime_error("invalid Form.bin container TOC marker");
        }

        const auto descriptor_header = read_block_header(data, descriptor_offset);
        if (descriptor_header.document_size > remaining_bytes) {
            throw std::runtime_error("1C container resource limit: total document sizes exceed file size");
        }
        remaining_bytes -= descriptor_header.document_size;
        const std::vector<std::uint8_t> descriptor = read_document(data, descriptor_offset, &budget);
        if (descriptor.size() < 24) {
            throw std::runtime_error("invalid Form.bin file descriptor: too small");
        }
        const std::uint64_t created = read_u64_le(descriptor, 0);
        const std::uint64_t modified = read_u64_le(descriptor, 8);
        const std::uint32_t flags = read_u32_le(descriptor, 16);
        if (flags != 0) {
            throw std::runtime_error("unsupported Form.bin file descriptor flags");
        }
        const auto payload_header = read_block_header(data, payload_offset);
        if (payload_header.document_size > remaining_bytes) {
            throw std::runtime_error("1C container resource limit: total document sizes exceed file size");
        }
        remaining_bytes -= payload_header.document_size;
        result.files.push_back({
            utf16le_name_to_utf8(descriptor, 20),
            created,
            modified,
            read_document(data, payload_offset, &budget),
        });
    }
    return result;
}

inline std::vector<std::uint8_t> block_header(
    std::uint32_t document_size,
    std::uint32_t current_size,
    std::uint32_t next_offset = container_end_marker
) {
    std::ostringstream text;
    text << "\r\n" << std::hex << std::setfill('0') << std::setw(8) << document_size << " "
         << std::setw(8) << current_size << " " << std::setw(8) << next_offset << " \r\n";
    const std::string value = text.str();
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

inline std::size_t append_block(
    std::vector<std::uint8_t>& data,
    const std::vector<std::uint8_t>& payload,
    std::size_t min_block_size = 0,
    std::uint32_t document_size = 0,
    std::uint32_t next_offset = container_end_marker
) {
    const std::size_t offset = data.size();
    const std::uint32_t size =
        document_size == 0 ? static_cast<std::uint32_t>(payload.size()) : document_size;
    const std::uint32_t current_size =
        static_cast<std::uint32_t>(std::max(min_block_size, payload.size()));
    const std::vector<std::uint8_t> header = block_header(size, current_size, next_offset);
    data.insert(data.end(), header.begin(), header.end());
    data.insert(data.end(), payload.begin(), payload.end());
    if (current_size > payload.size()) {
        data.insert(data.end(), current_size - payload.size(), 0);
    }
    return offset;
}

inline std::size_t append_document(
    std::vector<std::uint8_t>& data,
    const std::vector<std::uint8_t>& payload,
    std::size_t min_block_size = 0
) {
    if (payload.size() <= container_document_block_size) {
        return append_block(data, payload, min_block_size);
    }

    const std::size_t first_offset = data.size();
    const std::size_t header_size = block_header(0, 0).size();
    std::vector<std::size_t> offsets;
    std::size_t current_offset = first_offset;
    for (std::size_t index = 0; index < payload.size(); index += container_document_block_size) {
        const std::size_t chunk_size =
            std::min(container_document_block_size, payload.size() - index);
        const bool is_last = index + chunk_size >= payload.size();
        const std::size_t block_size = is_last ? std::max(min_block_size, chunk_size) : chunk_size;
        offsets.push_back(current_offset);
        current_offset += header_size + block_size;
    }

    for (std::size_t chunk_index = 0; chunk_index < offsets.size(); ++chunk_index) {
        const std::size_t payload_offset = chunk_index * container_document_block_size;
        const std::size_t chunk_size =
            std::min(container_document_block_size, payload.size() - payload_offset);
        const bool is_last = chunk_index + 1 == offsets.size();
        const std::uint32_t next = is_last
                                       ? container_end_marker
                                       : static_cast<std::uint32_t>(offsets[chunk_index + 1]);
        const std::uint32_t document_size =
            chunk_index == 0 ? static_cast<std::uint32_t>(payload.size()) : 0;
        const std::vector<std::uint8_t> chunk(
            payload.begin() + static_cast<std::ptrdiff_t>(payload_offset),
            payload.begin() + static_cast<std::ptrdiff_t>(payload_offset + chunk_size));
        append_block(data, chunk, is_last ? min_block_size : 0, document_size, next);
    }
    return first_offset;
}

inline std::vector<std::uint8_t> serialize_container(const OneCContainer& container) {
    std::vector<std::uint8_t> data;
    write_u32_le(data, container_end_marker);
    write_u32_le(
        data,
        container.block_size == 0 ? container_block_size
                                  : static_cast<std::uint32_t>(container.block_size));
    write_u32_le(data, static_cast<std::uint32_t>(container.files.size()));
    write_u32_le(data, 0);

    const std::vector<std::uint8_t> toc_placeholder =
        block_header(static_cast<std::uint32_t>(container.files.size() * 12), container_toc_block_size);
    const std::size_t toc_offset = data.size();
    data.insert(data.end(), toc_placeholder.begin(), toc_placeholder.end());
    data.insert(data.end(), container_toc_block_size, 0);

    struct TocEntry {
        std::uint32_t descriptor_offset;
        std::uint32_t payload_offset = 0;
    };
    std::vector<TocEntry> entries;
    for (const auto& file : container.files) {
        const auto descriptor = file_descriptor_payload(file.name, file.created, file.modified);
        const std::uint32_t descriptor_offset =
            static_cast<std::uint32_t>(append_document(data, descriptor));
        entries.push_back({descriptor_offset, 0});
    }

    for (std::size_t reverse_index = container.files.size(); reverse_index > 0; --reverse_index) {
        const std::size_t index = reverse_index - 1;
        entries[index].payload_offset =
            static_cast<std::uint32_t>(append_document(data, container.files[index].payload));
    }

    std::vector<std::uint8_t> toc_payload;
    for (const TocEntry& entry : entries) {
        write_u32_le(toc_payload, entry.descriptor_offset);
        write_u32_le(toc_payload, entry.payload_offset);
        write_u32_le(toc_payload, container_end_marker);
    }
    std::copy(
        toc_payload.begin(),
        toc_payload.end(),
        data.begin() + static_cast<std::ptrdiff_t>(toc_offset + toc_placeholder.size()));
    return data;
}

}  // namespace oof::storage::formbin
