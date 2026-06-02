#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oof::platform::guid_registry {

struct Guid {
    std::array<std::uint8_t, 16> bytes{};
};

struct SeedGuid {
    std::string_view guid;
    Guid bytes_le;
};

struct GuidHit {
    std::size_t offset = 0;
    std::string guid;
};

struct GuidBlockRepeat {
    std::size_t offset = 0;
};

struct CodeReference {
    std::size_t offset = 0;
    std::size_t target = 0;
};

struct ElfSection {
    std::string name;
    std::size_t address = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct PlatformGuidScan {
    std::vector<GuidHit> seed_hits;
    std::vector<GuidBlockRepeat> repeated_blocks;
    std::vector<CodeReference> code_refs;
};

inline std::uint8_t hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return static_cast<std::uint8_t>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<std::uint8_t>(10 + ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return static_cast<std::uint8_t>(10 + ch - 'A');
    }
    return 0;
}

inline Guid guid_from_text_le(std::string_view guid) {
    std::string hex;
    hex.reserve(32);
    for (const char ch : guid) {
        if (ch != '-') {
            hex.push_back(ch);
        }
    }
    Guid result;
    const std::array<int, 16> order{{3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15}};
    for (std::size_t index = 0; index < order.size(); ++index) {
        const int source = order[index] * 2;
        result.bytes[index] = static_cast<std::uint8_t>((hex_value(hex[source]) << 4) | hex_value(hex[source + 1]));
    }
    return result;
}

inline std::string guid_to_text_le(const std::array<std::uint8_t, 16>& bytes) {
    const std::array<int, 16> order{{3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15}};
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            out << '-';
        }
        out << std::setw(2) << static_cast<unsigned>(bytes[order[index]]);
    }
    return out.str();
}

inline std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline std::uint16_t read_u16_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

inline std::uint64_t read_u64_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint64_t>(read_u32_le(bytes, offset)) |
           (static_cast<std::uint64_t>(read_u32_le(bytes, offset + 4)) << 32);
}

inline std::int32_t read_i32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int32_t>(read_u32_le(bytes, offset));
}

inline bool has_at(const std::vector<std::uint8_t>& bytes, std::size_t offset, const Guid& guid) {
    if (offset + guid.bytes.size() > bytes.size()) {
        return false;
    }
    for (std::size_t index = 0; index < guid.bytes.size(); ++index) {
        if (bytes[offset + index] != guid.bytes[index]) {
            return false;
        }
    }
    return true;
}

inline std::vector<std::size_t> find_guid_offsets(const std::vector<std::uint8_t>& bytes, const Guid& guid) {
    std::vector<std::size_t> offsets;
    if (bytes.size() < guid.bytes.size()) {
        return offsets;
    }
    for (std::size_t offset = 0; offset + guid.bytes.size() <= bytes.size(); ++offset) {
        if (has_at(bytes, offset, guid)) {
            offsets.push_back(offset);
        }
    }
    return offsets;
}

inline bool bytes_equal(
    const std::vector<std::uint8_t>& bytes,
    std::size_t lhs,
    std::size_t rhs,
    std::size_t length
) {
    if (lhs + length > bytes.size() || rhs + length > bytes.size()) {
        return false;
    }
    for (std::size_t index = 0; index < length; ++index) {
        if (bytes[lhs + index] != bytes[rhs + index]) {
            return false;
        }
    }
    return true;
}

inline std::string read_c_string(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t limit) {
    std::string out;
    while (offset < limit && offset < bytes.size() && bytes[offset] != 0) {
        out.push_back(static_cast<char>(bytes[offset]));
        ++offset;
    }
    return out;
}

inline std::vector<ElfSection> read_elf64_sections(const std::vector<std::uint8_t>& bytes) {
    std::vector<ElfSection> sections;
    if (bytes.size() < 0x40 || bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F' ||
        bytes[4] != 2 || bytes[5] != 1) {
        return sections;
    }
    const std::size_t section_header_offset = static_cast<std::size_t>(read_u64_le(bytes, 0x28));
    const std::size_t section_entry_size = read_u16_le(bytes, 0x3a);
    const std::size_t section_count = read_u16_le(bytes, 0x3c);
    const std::size_t string_section_index = read_u16_le(bytes, 0x3e);
    if (section_entry_size < 0x40 || section_header_offset + section_entry_size * section_count > bytes.size() ||
        string_section_index >= section_count) {
        return sections;
    }
    const std::size_t string_header = section_header_offset + string_section_index * section_entry_size;
    const std::size_t string_offset = static_cast<std::size_t>(read_u64_le(bytes, string_header + 0x18));
    const std::size_t string_size = static_cast<std::size_t>(read_u64_le(bytes, string_header + 0x20));
    const std::size_t string_limit = string_offset + string_size;
    if (string_limit > bytes.size()) {
        return sections;
    }
    for (std::size_t index = 0; index < section_count; ++index) {
        const std::size_t header = section_header_offset + index * section_entry_size;
        const std::uint32_t name_offset = read_u32_le(bytes, header);
        ElfSection section;
        section.name = read_c_string(bytes, string_offset + name_offset, string_limit);
        section.address = static_cast<std::size_t>(read_u64_le(bytes, header + 0x10));
        section.offset = static_cast<std::size_t>(read_u64_le(bytes, header + 0x18));
        section.size = static_cast<std::size_t>(read_u64_le(bytes, header + 0x20));
        sections.push_back(section);
    }
    return sections;
}

inline ElfSection find_section(const std::vector<ElfSection>& sections, std::string_view name) {
    for (const ElfSection& section : sections) {
        if (section.name == name) {
            return section;
        }
    }
    return {};
}

inline std::vector<CodeReference> find_rip_relative_refs(
    const std::vector<std::uint8_t>& bytes,
    const ElfSection& text,
    const std::vector<std::size_t>& targets
) {
    std::vector<CodeReference> refs;
    if (bytes.size() < 8 || text.size == 0 || text.offset + text.size > bytes.size()) {
        return refs;
    }
    const std::size_t end = text.offset + text.size;
    for (std::size_t offset = text.offset; offset + 8 <= end; ++offset) {
        for (const auto [disp_offset, instruction_size] : {std::pair<std::size_t, std::size_t>{3, 7},
                 std::pair<std::size_t, std::size_t>{4, 8}}) {
            bool is_rip_relative = false;
            if (disp_offset == 3) {
                const bool has_rex = bytes[offset] == 0x48 || bytes[offset] == 0x4c;
                const bool has_opcode = bytes[offset + 1] == 0x8d || bytes[offset + 1] == 0x8b ||
                                        bytes[offset + 1] == 0x39 || bytes[offset + 1] == 0x3b;
                is_rip_relative = has_rex && has_opcode && ((bytes[offset + 2] & 0xc7) == 0x05);
            } else {
                const bool has_prefix = bytes[offset] == 0xf3 || bytes[offset] == 0x66;
                is_rip_relative = has_prefix && bytes[offset + 1] == 0x0f && ((bytes[offset + 3] & 0xc7) == 0x05);
            }
            if (!is_rip_relative) {
                continue;
            }
            const std::int64_t disp = read_i32_le(bytes, offset + disp_offset);
            const std::int64_t instruction_address =
                static_cast<std::int64_t>(text.address + (offset - text.offset));
            const std::int64_t target = instruction_address + static_cast<std::int64_t>(instruction_size) + disp;
            if (target < 0) {
                continue;
            }
            for (const std::size_t known_target : targets) {
                if (static_cast<std::size_t>(target) == known_target) {
                    refs.push_back({static_cast<std::size_t>(instruction_address), known_target});
                }
            }
        }
    }
    return refs;
}

inline PlatformGuidScan scan_dsgnfrm_guid_registry(const std::vector<std::uint8_t>& bytes) {
    const std::array<SeedGuid, 7> seeds{{
        {"09ccdc77-ea1a-4a6d-ab1c-3435eada2433", guid_from_text_le("09ccdc77-ea1a-4a6d-ab1c-3435eada2433")},
        {"e69bf21d-97b2-4f37-86db-675aea9ec2cb", guid_from_text_le("e69bf21d-97b2-4f37-86db-675aea9ec2cb")},
        {"6ff79819-710e-4145-97cd-1618da79e3e2", guid_from_text_le("6ff79819-710e-4145-97cd-1618da79e3e2")},
        {"381ed624-9217-4e63-85db-c4c3cb87daae", guid_from_text_le("381ed624-9217-4e63-85db-c4c3cb87daae")},
        {"ea83fe3a-ac3c-4cce-8045-3dddf35b28b1", guid_from_text_le("ea83fe3a-ac3c-4cce-8045-3dddf35b28b1")},
        {"151ef23e-6bb2-4681-83d0-35bc2217230c", guid_from_text_le("151ef23e-6bb2-4681-83d0-35bc2217230c")},
        {"0fc7e20d-f241-460c-bdf4-5ad88e5474a5", guid_from_text_le("0fc7e20d-f241-460c-bdf4-5ad88e5474a5")},
    }};

    PlatformGuidScan scan;
    for (const SeedGuid& seed : seeds) {
        for (const std::size_t offset : find_guid_offsets(bytes, seed.bytes_le)) {
            scan.seed_hits.push_back({offset, std::string(seed.guid)});
        }
    }

    const auto root_hits = find_guid_offsets(bytes, seeds[1].bytes_le);
    std::vector<std::size_t> targets;
    if (!root_hits.empty()) {
        const std::size_t first_block = root_hits.front() >= 0x80 ? root_hits.front() - 0x80 : root_hits.front();
        constexpr std::size_t block_size = 0x320;
        for (std::size_t offset = 0; offset + block_size <= bytes.size(); ++offset) {
            if (bytes_equal(bytes, first_block, offset, block_size)) {
                scan.repeated_blocks.push_back({offset});
            }
        }
        for (const auto& repeat : scan.repeated_blocks) {
            targets.push_back(repeat.offset);
            targets.push_back(repeat.offset + 0x80);
            targets.push_back(repeat.offset + 0x120);
            targets.push_back(repeat.offset + 0x1b0);
        }
    }
    const auto sections = read_elf64_sections(bytes);
    scan.code_refs = find_rip_relative_refs(bytes, find_section(sections, ".text"), targets);
    return scan;
}

}  // namespace oof::platform::guid_registry
