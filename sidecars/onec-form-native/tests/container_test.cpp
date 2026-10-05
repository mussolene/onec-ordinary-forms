#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "oof/storage/form_bin_container.hpp"

namespace {

namespace formbin = oof::storage::formbin;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Operation>
void expect_rejected(Operation&& operation, std::string_view message) {
    try {
        operation();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

std::vector<std::uint8_t> container_prefix(std::uint32_t file_count = 0) {
    std::vector<std::uint8_t> data;
    formbin::write_u32_le(data, formbin::container_end_marker);
    formbin::write_u32_le(data, formbin::container_block_size);
    formbin::write_u32_le(data, file_count);
    formbin::write_u32_le(data, 0);
    return data;
}

void append_bytes(std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& suffix) {
    data.insert(data.end(), suffix.begin(), suffix.end());
}

void test_empty_container() {
    formbin::OneCContainer container;
    container.block_size = formbin::container_block_size;

    const auto bytes = formbin::serialize_container(container);
    const auto parsed = formbin::parse_container(bytes);
    expect(parsed.block_size == formbin::container_block_size, "empty container must retain block size");
    expect(parsed.files.empty(), "empty container must remain empty");
    expect(formbin::serialize_container(parsed) == bytes, "empty container reserialize must be deterministic");
}

void test_multi_file_container() {
    formbin::OneCContainer container;
    container.block_size = formbin::container_block_size;
    container.files.push_back({
        "module",
        UINT64_C(0x0102030405060708),
        UINT64_C(0x1112131415161718),
        {0x00, 0x01, 0x7f, 0x80, 0xff},
    });
    container.files.push_back({
        "form",
        UINT64_C(0x2122232425262728),
        UINT64_C(0x3132333435363738),
        {0xde, 0xad, 0xbe, 0xef},
    });
    const std::u8string unicode_name = u8"Форма😀";
    container.files.push_back({
        std::string(
            reinterpret_cast<const char*>(unicode_name.data()),
            unicode_name.size()),
        41,
        43,
        {0x42},
    });

    const auto bytes = formbin::serialize_container(container);
    expect(formbin::serialize_container(container) == bytes, "serialization must be deterministic");

    const auto parsed = formbin::parse_container(bytes);
    expect(parsed.files.size() == 3, "multi-file container must retain every file");
    for (std::size_t index = 0; index < container.files.size(); ++index) {
        expect(parsed.files[index].name == container.files[index].name, "file name must round-trip");
        expect(parsed.files[index].created == container.files[index].created, "created timestamp must round-trip");
        expect(parsed.files[index].modified == container.files[index].modified, "modified timestamp must round-trip");
        expect(parsed.files[index].payload == container.files[index].payload, "file payload must round-trip");
    }
    expect(formbin::serialize_container(parsed) == bytes, "parsed container reserialize must be byte-stable");
}

void test_multi_block_document() {
    formbin::OneCContainer container;
    container.block_size = formbin::container_block_size;
    std::vector<std::uint8_t> payload(formbin::container_document_block_size + 733);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<std::uint8_t>((index * 37 + 11) & 0xff);
    }
    container.files.push_back({"form", 17, 23, payload});

    const auto bytes = formbin::serialize_container(container);
    const auto parsed = formbin::parse_container(bytes);
    expect(parsed.files.size() == 1, "multi-block container must retain its file");
    expect(parsed.files.front().payload == payload, "multi-block payload must round-trip");
    expect(formbin::serialize_container(parsed) == bytes, "multi-block reserialize must be byte-stable");
}

void test_malformed_headers() {
    auto invalid_container = formbin::serialize_container({});
    invalid_container[0] = 0;
    expect_rejected(
        [&] { formbin::parse_container(invalid_container); },
        "malformed container header must be rejected");

    auto invalid_block = container_prefix();
    invalid_block.push_back('x');
    invalid_block.push_back('\n');
    expect_rejected(
        [&] { formbin::parse_container(invalid_block); },
        "malformed block header must be rejected");
}

void test_truncated_and_mismatched_blocks() {
    auto truncated = container_prefix();
    append_bytes(truncated, formbin::block_header(3, 3));
    truncated.push_back(0xaa);
    truncated.push_back(0xbb);
    expect_rejected(
        [&] { formbin::parse_container(truncated); },
        "truncated block payload must be rejected");

    auto size_mismatch = container_prefix();
    append_bytes(size_mismatch, formbin::block_header(3, 2));
    size_mismatch.push_back(0xaa);
    size_mismatch.push_back(0xbb);
    expect_rejected(
        [&] { formbin::parse_container(size_mismatch); },
        "document smaller than its declared size must be rejected");
}

void test_invalid_block_chains() {
    auto cyclic = container_prefix();
    append_bytes(cyclic, formbin::block_header(0, 0, formbin::container_header_size));
    expect_rejected(
        [&] { formbin::parse_container(cyclic); },
        "cyclic block chain must be rejected");

    auto invalid_next = container_prefix();
    append_bytes(invalid_next, formbin::block_header(0, 0, 0x1000));
    expect_rejected(
        [&] { formbin::parse_container(invalid_next); },
        "out-of-range next block offset must be rejected");
}

void test_bounded_document_accumulation() {
    auto padded = container_prefix();
    const auto first_offset = formbin::append_block(padded, {0xab}, 4096, 1);
    const auto logical = formbin::read_document(padded, first_offset);
    expect(logical == std::vector<std::uint8_t>{0xab}, "padded blocks must retain logical bytes");
    expect(logical.capacity() <= 1, "padding must not be allocated in the logical document");

    // Every block spans the remaining headers. The declared document is empty.
    auto overlapping = container_prefix();
    constexpr std::size_t block_count = 1000;
    const auto header_size = formbin::block_header(0, 0).size();
    for (std::size_t index = 0; index < block_count; ++index) {
        append_bytes(overlapping, formbin::block_header(0,
            static_cast<std::uint32_t>((block_count - index - 1) * header_size),
            index + 1 == block_count ? formbin::container_end_marker :
                static_cast<std::uint32_t>(formbin::container_header_size + (index + 1) * header_size)));
    }
    const auto empty = formbin::read_document(overlapping, formbin::container_header_size);
    expect(empty.empty() && empty.capacity() == 0,
        "overlapping padding must not amplify the empty document allocation");

    auto cycle_after_data = container_prefix();
    const auto start = formbin::append_block(cycle_after_data, {0xab}, 0, 1);
    const auto next = cycle_after_data.size();
    const auto first_header = formbin::block_header(1, 1, static_cast<std::uint32_t>(next));
    std::copy(first_header.begin(), first_header.end(), cycle_after_data.begin() + start);
    append_bytes(cycle_after_data, formbin::block_header(0, 0, static_cast<std::uint32_t>(next)));
    expect_rejected([&] { formbin::read_document(cycle_after_data, start); },
        "block cycles must still be checked after all declared bytes have been read");

    auto oversized = container_prefix();
    append_bytes(oversized, formbin::block_header(UINT32_MAX, 0));
    expect_rejected([&] { formbin::read_document(oversized, formbin::container_header_size); },
        "a document larger than the input must be rejected before accumulation");
}

void test_container_count_and_allocation_limits() {
    auto mismatch = container_prefix(UINT32_C(0x7fffffff));
    formbin::append_block(mismatch, {});
    expect_rejected([&] { formbin::parse_container(mismatch); },
        "file count must match the TOC before descriptor traversal");

    auto aliased = container_prefix(4);
    const auto toc = formbin::append_block(aliased, std::vector<std::uint8_t>(48));
    const auto descriptor = formbin::append_document(aliased, formbin::file_descriptor_payload("form", 0, 0));
    const auto payload = formbin::append_document(aliased, std::vector<std::uint8_t>(4096));
    std::vector<std::uint8_t> entries;
    for (std::size_t index = 0; index < 4; ++index) {
        formbin::write_u32_le(entries, static_cast<std::uint32_t>(descriptor));
        formbin::write_u32_le(entries, static_cast<std::uint32_t>(payload));
        formbin::write_u32_le(entries, formbin::container_end_marker);
    }
    const auto toc_start = formbin::read_block_header(aliased, toc).payload_offset;
    std::copy(entries.begin(), entries.end(), aliased.begin() + toc_start);
    expect_rejected([&] { formbin::parse_container(aliased); },
        "repeated TOC aliases must not multiply materialized bytes beyond the input size");
}

void test_shared_block_traversal_budget() {
    constexpr std::size_t file_count = 100;
    constexpr std::size_t block_count = 400;
    auto aliased = container_prefix(file_count);
    const auto toc = formbin::append_block(aliased, std::vector<std::uint8_t>(file_count * 12));
    const auto descriptor = formbin::append_document(aliased, formbin::file_descriptor_payload("form", 0, 0));
    const auto payload = aliased.size();
    const auto header_size = formbin::block_header(0, 0).size();
    for (std::size_t index = 0; index < block_count; ++index) {
        append_bytes(aliased, formbin::block_header(0, 0,
            index + 1 == block_count ? formbin::container_end_marker :
                static_cast<std::uint32_t>(payload + (index + 1) * header_size)));
    }
    std::vector<std::uint8_t> entries;
    for (std::size_t index = 0; index < file_count; ++index) {
        formbin::write_u32_le(entries, static_cast<std::uint32_t>(descriptor));
        formbin::write_u32_le(entries, static_cast<std::uint32_t>(payload));
        formbin::write_u32_le(entries, formbin::container_end_marker);
    }
    const auto toc_start = formbin::read_block_header(aliased, toc).payload_offset;
    std::copy(entries.begin(), entries.end(), aliased.begin() + toc_start);
    expect(formbin::read_document(aliased, payload).empty(),
        "one traversal of the empty chain must fit its standalone budget");
    bool rejected = false;
    try {
        static_cast<void>(formbin::parse_container(aliased));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view(error.what()).find("block traversal budget") != std::string_view::npos;
    }
    expect(rejected, "all files must share one traversal budget even when aliased payloads are empty");
}

void test_block_header_resource_limit() {
    auto at_limit = formbin::block_header(0, 0);
    at_limit.insert(at_limit.end() - 2, formbin::max_container_block_header_size - at_limit.size(), ' ');
    expect(formbin::read_block_header(at_limit, 0).payload_offset == formbin::max_container_block_header_size,
        "a block header at the resource limit must retain its payload offset");
    expect(formbin::read_document(at_limit, 0).empty(), "bounded header padding must remain accepted");
    at_limit.insert(at_limit.end() - 2, ' ');
    bool rejected = false;
    try {
        static_cast<void>(formbin::read_block_header(at_limit, 0));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view(error.what()).find("resource limit: block header") != std::string_view::npos;
    }
    expect(rejected, "a 65-byte block header must fail before unbounded header scanning");
}

void test_surrogate_handling() {
    const std::vector<std::uint8_t> valid_name{0x3d, 0xd8, 0x00, 0xde, 0x00, 0x00};
    expect(
        formbin::utf16le_name_to_utf8(valid_name, 0) == "\xf0\x9f\x98\x80",
        "UTF-16 surrogate pair must decode to one UTF-8 code point");

    const std::vector<std::uint8_t> invalid_name{0x3d, 0xd8, 0x00, 0x00};
    expect_rejected(
        [&] { formbin::utf16le_name_to_utf8(invalid_name, 0); },
        "unpaired UTF-16 surrogate must be rejected");

    formbin::OneCContainer invalid_utf8;
    invalid_utf8.files.push_back({std::string("\xc0\x80", 2), 0, 0, {}});
    expect_rejected(
        [&] { formbin::serialize_container(invalid_utf8); },
        "invalid UTF-8 file names must be rejected during serialization");
}

}  // namespace

int main() {
    try {
        test_empty_container();
        test_multi_file_container();
        test_multi_block_document();
        test_malformed_headers();
        test_truncated_and_mismatched_blocks();
        test_invalid_block_chains();
        test_bounded_document_accumulation();
        test_container_count_and_allocation_limits();
        test_shared_block_traversal_budget();
        test_block_header_resource_limit();
        test_surrogate_handling();
    } catch (const std::exception& error) {
        std::cerr << "container tests: FAIL: " << error.what() << '\n';
        return 1;
    }

    std::cout << "container tests: PASS\n";
    return 0;
}
