#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace oof::platform::formbin {

constexpr std::int32_t container_end_marker = 0x7fffffff;
constexpr std::int32_t container_block_size = 0x200;
constexpr std::size_t container_header_size = 16;
constexpr std::size_t container_toc_block_size = 0x200;
constexpr std::size_t container_document_block_size = 0xa000;

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
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("Form.bin container integer is truncated");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline std::uint64_t read_u64_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
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

inline std::string utf16le_name_to_utf8(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::string out;
    while (offset + 1 < bytes.size()) {
        const std::uint16_t code = static_cast<std::uint16_t>(bytes[offset]) |
                                   (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
        if (code == 0) {
            break;
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xe0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
        offset += 2;
    }
    return out;
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
    for (const unsigned char ch : name) {
        out.push_back(ch);
        out.push_back(0);
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
    if (offset + 2 > data.size() || data[offset] != '\r' || data[offset + 1] != '\n') {
        throw std::runtime_error("invalid 1C container block header prefix");
    }
    std::size_t header_end = offset + 2;
    while (header_end + 1 < data.size() && !(data[header_end] == '\r' && data[header_end + 1] == '\n')) {
        ++header_end;
    }
    if (header_end + 1 >= data.size()) {
        throw std::runtime_error("invalid 1C container block header suffix");
    }
    const std::string header(reinterpret_cast<const char*>(&data[offset + 2]), header_end - offset - 2);
    std::istringstream in(header);
    BlockHeader result;
    in >> std::hex >> result.document_size >> result.current_size >> result.next_offset;
    if (!in) {
        throw std::runtime_error("invalid 1C container block header fields");
    }
    result.payload_offset = header_end + 2;
    return result;
}

inline std::vector<std::uint8_t> read_document(const std::vector<std::uint8_t>& data, std::size_t offset) {
    std::vector<std::uint8_t> payload;
    std::uint32_t total_size = 0;
    bool has_total_size = false;
    std::size_t current_offset = offset;
    while (true) {
        const BlockHeader header = read_block_header(data, current_offset);
        if (!has_total_size) {
            total_size = header.document_size;
            has_total_size = true;
        }
        const std::size_t payload_end = header.payload_offset + header.current_size;
        if (payload_end > data.size()) {
            throw std::runtime_error("1C container block payload exceeds file size");
        }
        payload.insert(payload.end(), data.begin() + static_cast<std::ptrdiff_t>(header.payload_offset),
            data.begin() + static_cast<std::ptrdiff_t>(payload_end));
        if (header.next_offset == static_cast<std::uint32_t>(container_end_marker)) {
            break;
        }
        current_offset = header.next_offset;
    }
    if (payload.size() > total_size) {
        payload.resize(total_size);
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

    const std::vector<std::uint8_t> toc = read_document(data, container_header_size);
    OneCContainer result;
    result.block_size = block_size;
    for (std::int32_t index = 0; index < file_count; ++index) {
        const std::size_t entry_offset = static_cast<std::size_t>(index) * 12;
        if (entry_offset + 12 > toc.size()) {
            throw std::runtime_error("invalid Form.bin container TOC: truncated entry");
        }
        const std::uint32_t descriptor_offset = read_u32_le(toc, entry_offset);
        const std::uint32_t payload_offset = read_u32_le(toc, entry_offset + 4);
        const auto entry_marker = static_cast<std::int32_t>(read_u32_le(toc, entry_offset + 8));
        if (entry_marker != container_end_marker) {
            throw std::runtime_error("invalid Form.bin container TOC marker");
        }

        const std::vector<std::uint8_t> descriptor = read_document(data, descriptor_offset);
        if (descriptor.size() < 24) {
            throw std::runtime_error("invalid Form.bin file descriptor: too small");
        }
        const std::uint64_t created = read_u64_le(descriptor, 0);
        const std::uint64_t modified = read_u64_le(descriptor, 8);
        const std::uint32_t flags = read_u32_le(descriptor, 16);
        if (flags != 0) {
            throw std::runtime_error("unsupported Form.bin file descriptor flags");
        }
        result.files.push_back({utf16le_name_to_utf8(descriptor, 20), created, modified, read_document(data, payload_offset)});
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
    const std::uint32_t size = document_size == 0 ? static_cast<std::uint32_t>(payload.size()) : document_size;
    const std::uint32_t current_size = static_cast<std::uint32_t>(std::max(min_block_size, payload.size()));
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
        const std::size_t chunk_size = std::min(container_document_block_size, payload.size() - index);
        const bool is_last = index + chunk_size >= payload.size();
        const std::size_t block_size = is_last ? std::max(min_block_size, chunk_size) : chunk_size;
        offsets.push_back(current_offset);
        current_offset += header_size + block_size;
    }

    for (std::size_t chunk_index = 0; chunk_index < offsets.size(); ++chunk_index) {
        const std::size_t payload_offset = chunk_index * container_document_block_size;
        const std::size_t chunk_size = std::min(container_document_block_size, payload.size() - payload_offset);
        const bool is_last = chunk_index + 1 == offsets.size();
        const std::uint32_t next = is_last ? container_end_marker : static_cast<std::uint32_t>(offsets[chunk_index + 1]);
        const std::uint32_t document_size = chunk_index == 0 ? static_cast<std::uint32_t>(payload.size()) : 0;
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
    write_u32_le(data, container.block_size == 0 ? container_block_size : static_cast<std::uint32_t>(container.block_size));
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
    std::copy(toc_payload.begin(), toc_payload.end(), data.begin() + static_cast<std::ptrdiff_t>(toc_offset + toc_placeholder.size()));
    return data;
}

}  // namespace oof::platform::formbin
