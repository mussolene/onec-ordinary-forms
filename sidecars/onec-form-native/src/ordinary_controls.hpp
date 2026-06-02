#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "platform_mechanism.hpp"

namespace oof::platform::ordinary {

enum class TransferFacet {
    controls,
    position,
    info,
};

struct TransferDescriptor {
    std::string_view symbol;
    TransferFacet facet;
    std::uint32_t format_id;
    std::uint32_t record_size;
    std::string_view count_semantics;
    std::string_view boundary;
    std::string_view platform_role;
    std::string_view native_role;
};

constexpr std::array<TransferDescriptor, 3> transfer_registry{{
    {
        "cf_form_controls_position8",
        TransferFacet::position,
        cf_form_controls_position8,
        position_transfer_record_size,
        "record count",
        "count + 0x20-byte records copied by the position transfer path",
        "ordinary control geometry/binding position records",
        "position and binding descriptor codec",
    },
    {
        "cf_form_controls8",
        TransferFacet::controls,
        cf_form_controls8,
        0,
        "payload byte size",
        "existing IFile/HGLOBAL payload returned by FUN_00270da0",
        "ordinary control payload file",
        "raw controls8 payload; not a fixed 40-byte record list at GetData boundary",
    },
    {
        "cf_form_controls_info8",
        TransferFacet::info,
        cf_form_controls_info8,
        info_transfer_record_size,
        "record count",
        "count + 0x10-byte records copied from the info linked list",
        "ordinary control shared/base info records",
        "control-info descriptor codec",
    },
}};

struct TripletEntryPoint {
    std::string_view address;
    std::string_view role;
};

constexpr std::array<TripletEntryPoint, 4> triplet_entry_points{{
    {"002709e0", "full control/position/info triplet"},
    {"00270da0", "full control/position/info triplet"},
    {"00270fe0", "full control/position/info triplet"},
    {"002c9430", "info-only entry"},
}};

constexpr std::uint32_t format_id_for(TransferFacet facet) {
    for (const auto& descriptor : transfer_registry) {
        if (descriptor.facet == facet) {
            return descriptor.format_id;
        }
    }
    return 0;
}

constexpr std::uint32_t record_size_for(TransferFacet facet) {
    for (const auto& descriptor : transfer_registry) {
        if (descriptor.facet == facet) {
            return descriptor.record_size;
        }
    }
    return 0;
}

inline void write_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

inline std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("ordinary transfer record is truncated");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

struct DiagnosticControlPayloadChunk {
    // This is a native fixture chunk for sidecar round-trip tests. Platform
    // evidence says cf_form_controls8 is exposed as raw payload bytes at the
    // transfer boundary, not as a fixed-size record list.
    static constexpr std::uint32_t serialized_size = format_entry_record_size;

    std::array<std::uint32_t, serialized_size / 4> words{};

    std::vector<std::uint8_t> serialize() const {
        std::vector<std::uint8_t> out;
        out.reserve(serialized_size);
        for (const std::uint32_t word : words) {
            write_u32_le(out, word);
        }
        return out;
    }

    static DiagnosticControlPayloadChunk deserialize(const std::vector<std::uint8_t>& bytes) {
        if (bytes.size() != serialized_size) {
            throw std::runtime_error("diagnostic cf_form_controls8 fixture chunk must be 40 bytes");
        }
        DiagnosticControlPayloadChunk record;
        for (std::size_t index = 0; index < record.words.size(); ++index) {
            record.words[index] = read_u32_le(bytes, index * 4);
        }
        return record;
    }
};

struct ControlPayloadFile {
    std::vector<std::uint8_t> bytes;
};

struct TransferSectionInfo {
    TransferFacet facet;
    std::uint32_t format_id = 0;
    std::uint32_t record_size = 0;
    std::uint32_t count_or_bytes = 0;
    std::size_t payload_offset = 0;
    std::size_t payload_size = 0;
};

struct ControlPositionRecord {
    static constexpr std::uint32_t serialized_size = position_transfer_record_size;

    std::array<std::uint32_t, serialized_size / 4> words{};

    std::vector<std::uint8_t> serialize() const {
        std::vector<std::uint8_t> out;
        out.reserve(serialized_size);
        for (const std::uint32_t word : words) {
            write_u32_le(out, word);
        }
        return out;
    }

    static ControlPositionRecord deserialize(const std::vector<std::uint8_t>& bytes) {
        if (bytes.size() != serialized_size) {
            throw std::runtime_error("cf_form_controls_position8 record must be 32 bytes");
        }
        ControlPositionRecord record;
        for (std::size_t index = 0; index < record.words.size(); ++index) {
            record.words[index] = read_u32_le(bytes, index * 4);
        }
        return record;
    }
};

struct ControlInfoRecord {
    static constexpr std::uint32_t serialized_size = info_transfer_record_size;

    std::array<std::uint32_t, serialized_size / 4> words{};

    std::vector<std::uint8_t> serialize() const {
        std::vector<std::uint8_t> out;
        out.reserve(serialized_size);
        for (const std::uint32_t word : words) {
            write_u32_le(out, word);
        }
        return out;
    }

    static ControlInfoRecord deserialize(const std::vector<std::uint8_t>& bytes) {
        if (bytes.size() != serialized_size) {
            throw std::runtime_error("cf_form_controls_info8 record must be 16 bytes");
        }
        ControlInfoRecord record;
        for (std::size_t index = 0; index < record.words.size(); ++index) {
            record.words[index] = read_u32_le(bytes, index * 4);
        }
        return record;
    }
};

struct FormatEntryRecord {
    static constexpr std::uint32_t serialized_size = format_entry_record_size;

    std::uint32_t format_id = 0;
    std::uint32_t record_size = 0;
    std::uint32_t record_count = 0;
    std::array<std::uint32_t, (serialized_size / 4) - 3> reserved{};

    std::vector<std::uint8_t> serialize() const {
        std::vector<std::uint8_t> out;
        out.reserve(serialized_size);
        write_u32_le(out, format_id);
        write_u32_le(out, record_size);
        write_u32_le(out, record_count);
        for (const std::uint32_t word : reserved) {
            write_u32_le(out, word);
        }
        return out;
    }

    static FormatEntryRecord deserialize(const std::vector<std::uint8_t>& bytes) {
        if (bytes.size() != serialized_size) {
            throw std::runtime_error("format entry record must be 40 bytes");
        }
        FormatEntryRecord record;
        record.format_id = read_u32_le(bytes, 0);
        record.record_size = read_u32_le(bytes, 4);
        record.record_count = read_u32_le(bytes, 8);
        for (std::size_t index = 0; index < record.reserved.size(); ++index) {
            record.reserved[index] = read_u32_le(bytes, 12 + index * 4);
        }
        return record;
    }
};

class FormFormatEnumerator {
public:
    void add(TransferFacet facet, std::uint32_t count) {
        entries_.push_back({format_id_for(facet), record_size_for(facet), count});
    }

    const std::vector<FormatEntryRecord>& entries() const {
        return entries_;
    }

    std::vector<std::uint8_t> serialize_headers() const {
        std::vector<std::uint8_t> out;
        write_u32_le(out, static_cast<std::uint32_t>(entries_.size()));
        for (const auto& entry : entries_) {
            std::vector<std::uint8_t> header = entry.serialize();
            out.insert(out.end(), header.begin(), header.end());
        }
        return out;
    }

    static FormFormatEnumerator deserialize_headers(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
        if (offset + 4 > bytes.size()) {
            throw std::runtime_error("format enumerator is truncated");
        }
        const std::uint32_t count = read_u32_le(bytes, offset);
        offset += 4;
        FormFormatEnumerator enumerator;
        for (std::uint32_t index = 0; index < count; ++index) {
            if (offset + FormatEntryRecord::serialized_size > bytes.size()) {
                throw std::runtime_error("format entry record is truncated");
            }
            const std::vector<std::uint8_t> entry_bytes(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + FormatEntryRecord::serialized_size));
            enumerator.entries_.push_back(FormatEntryRecord::deserialize(entry_bytes));
            offset += FormatEntryRecord::serialized_size;
        }
        return enumerator;
    }

private:
    std::vector<FormatEntryRecord> entries_;
};

inline std::string_view facet_name(TransferFacet facet) {
    switch (facet) {
        case TransferFacet::controls:
            return "controls";
        case TransferFacet::position:
            return "position";
        case TransferFacet::info:
            return "info";
    }
    return "unknown";
}

inline TransferFacet facet_for_format_id(std::uint32_t format_id) {
    for (const auto& descriptor : transfer_registry) {
        if (descriptor.format_id == format_id) {
            return descriptor.facet;
        }
    }
    throw std::runtime_error("unknown ordinary transfer format id");
}

inline std::size_t payload_size_for_entry(const FormatEntryRecord& entry) {
    const TransferFacet facet = facet_for_format_id(entry.format_id);
    if (facet == TransferFacet::controls) {
        if (entry.record_size != 0) {
            throw std::runtime_error("cf_form_controls8 transfer entry must use raw byte payload semantics");
        }
        return entry.record_count;
    }
    const std::uint32_t expected_size = record_size_for(facet);
    if (entry.record_size != expected_size) {
        throw std::runtime_error("ordinary transfer record size mismatch");
    }
    return static_cast<std::size_t>(entry.record_count) * expected_size;
}

inline std::vector<TransferSectionInfo> inspect_transfer_sections(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    const FormFormatEnumerator enumerator = FormFormatEnumerator::deserialize_headers(bytes, offset);
    std::vector<TransferSectionInfo> sections;
    sections.reserve(enumerator.entries().size());
    for (const auto& entry : enumerator.entries()) {
        const TransferFacet facet = facet_for_format_id(entry.format_id);
        const std::size_t payload_size = payload_size_for_entry(entry);
        if (offset + payload_size > bytes.size()) {
            throw std::runtime_error("ordinary transfer section payload is truncated");
        }
        sections.push_back({
            facet,
            entry.format_id,
            entry.record_size,
            entry.record_count,
            offset,
            payload_size,
        });
        offset += payload_size;
    }
    if (offset != bytes.size()) {
        throw std::runtime_error("ordinary transfer set has trailing bytes");
    }
    return sections;
}

struct OrdinaryTransferSet {
    ControlPayloadFile controls;
    std::vector<ControlPositionRecord> positions;
    std::vector<ControlInfoRecord> infos;

    FormFormatEnumerator enumerator() const {
        FormFormatEnumerator result;
        result.add(TransferFacet::position, static_cast<std::uint32_t>(positions.size()));
        result.add(TransferFacet::controls, static_cast<std::uint32_t>(controls.bytes.size()));
        result.add(TransferFacet::info, static_cast<std::uint32_t>(infos.size()));
        return result;
    }

    std::vector<std::uint8_t> serialize_records() const {
        std::vector<std::uint8_t> out = enumerator().serialize_headers();
        append_records(out, positions);
        out.insert(out.end(), controls.bytes.begin(), controls.bytes.end());
        append_records(out, infos);
        return out;
    }

    static OrdinaryTransferSet deserialize_records(const std::vector<std::uint8_t>& bytes) {
        std::size_t offset = 0;
        const FormFormatEnumerator enumerator = FormFormatEnumerator::deserialize_headers(bytes, offset);
        OrdinaryTransferSet set;
        for (const auto& entry : enumerator.entries()) {
            if (entry.format_id == cf_form_controls8) {
                const std::size_t payload_size = payload_size_for_entry(entry);
                if (offset + payload_size > bytes.size()) {
                    throw std::runtime_error("cf_form_controls8 payload is truncated");
                }
                set.controls.bytes.assign(
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset + payload_size));
                offset += payload_size;
            } else if (entry.format_id == cf_form_controls_position8) {
                read_records<ControlPositionRecord>(bytes, offset, entry, set.positions);
            } else if (entry.format_id == cf_form_controls_info8) {
                read_records<ControlInfoRecord>(bytes, offset, entry, set.infos);
            } else {
                throw std::runtime_error("unknown ordinary transfer format id");
            }
        }
        if (offset != bytes.size()) {
            throw std::runtime_error("ordinary transfer set has trailing bytes");
        }
        return set;
    }

private:
    template <typename Record>
    static void append_records(std::vector<std::uint8_t>& out, const std::vector<Record>& records) {
        for (const auto& record : records) {
            std::vector<std::uint8_t> bytes = record.serialize();
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
    }

    template <typename Record>
    static void read_records(
        const std::vector<std::uint8_t>& bytes,
        std::size_t& offset,
        const FormatEntryRecord& entry,
        std::vector<Record>& records
    ) {
        if (entry.record_size != Record::serialized_size) {
            throw std::runtime_error("ordinary transfer record size mismatch");
        }
        records.reserve(records.size() + entry.record_count);
        for (std::uint32_t index = 0; index < entry.record_count; ++index) {
            if (offset + Record::serialized_size > bytes.size()) {
                throw std::runtime_error("ordinary transfer record payload is truncated");
            }
            const std::vector<std::uint8_t> record_bytes(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + Record::serialized_size));
            records.push_back(Record::deserialize(record_bytes));
            offset += Record::serialized_size;
        }
    }
};

}  // namespace oof::platform::ordinary
