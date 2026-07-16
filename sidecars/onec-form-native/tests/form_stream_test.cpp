#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oof/storage/form_stream.hpp"

namespace {

namespace form_stream = oof::storage::form_stream;
namespace list_stream = oof::storage::list_stream;
namespace model = oof::model;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename T>
void expect_failure(
    const oof::Result<T>& result,
    std::string_view code,
    std::string_view path,
    std::string_view message) {
    expect(!result, message);
    expect(result.diagnostics().size() == 1, "storage failure must have one local diagnostic");
    if (result.diagnostics()[0].code != code) {
        throw std::runtime_error(
            "storage diagnostic code mismatch: expected " + std::string(code) +
            ", got " + result.diagnostics()[0].code + ": " +
            result.diagnostics()[0].message);
    }
    if (result.diagnostics()[0].path != path) {
        throw std::runtime_error(
            "storage diagnostic path mismatch: expected " + std::string(path) +
            ", got " + result.diagnostics()[0].path);
    }
}

model::CompositeIdValue composite(std::int64_t id, std::string uuid) {
    return model::CompositeIdValue{id, model::UuidValue{std::move(uuid)}, false};
}

list_stream::ListValue versioned_record(std::uint32_t version, std::size_t arity) {
    std::vector<list_stream::ListValue> items(
        arity,
        list_stream::ListValue::raw_atom("0"));
    items[0] = list_stream::ListValue::raw_atom(std::to_string(version));
    return list_stream::ListValue::list(std::move(items));
}

list_stream::ListValue layout_fixture(form_stream::LayoutKind kind) {
    std::vector<list_stream::ListValue> root(
        20,
        list_stream::ListValue::raw_atom("0"));
    root[0] = list_stream::ListValue::raw_atom("27");
    if (kind == form_stream::LayoutKind::form_section_16) {
        root[1] = versioned_record(16, 11);
        root[13] = versioned_record(3, 3);
    } else {
        root[1] = versioned_record(18, 14);
        root[13] = versioned_record(10, 11);
    }
    return list_stream::ListValue::list(std::move(root));
}

std::string runtime_envelope_text(const list_stream::ListValue& payload) {
    return list_stream::dump_compact(list_stream::ListValue::list({
        list_stream::ListValue::string_atom("#"),
        list_stream::ListValue::raw_atom(
            "01234567-89ab-cdef-0123-456789abcdef"),
        payload,
    }));
}

void test_outer_format_probe() {
    const auto format27 = form_stream::probe_outer_format(list_stream::parse("{27}"));
    expect(
        format27 && format27.value() == form_stream::OuterFormat::v27,
        "outer format 27 must probe");
    const auto format26 = form_stream::probe_outer_format(list_stream::parse("{26}"));
    expect(
        format26 && format26.value() == form_stream::OuterFormat::v26,
        "outer format 26 must probe");

    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{28}")),
        "OOF1111",
        "$/0",
        "unknown outer format must be rejected");
    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{}")),
        "OOF1110",
        "$/0",
        "missing outer format must be rejected");
    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{\"27\"}")),
        "OOF1104",
        "$/0",
        "quoted outer format must be rejected");
}

void test_layout_probe() {
    const auto section16 = form_stream::probe_layout(
        layout_fixture(form_stream::LayoutKind::form_section_16));
    expect(section16.ok(), "form section 16 layout must probe");
    expect(
        section16.value() == form_stream::StorageLayout{
            form_stream::OuterFormat::v27,
            form_stream::LayoutKind::form_section_16,
            20,
            16,
            11,
            3,
            3,
        },
        "form section 16 layout descriptor mismatch");

    const auto section18 = form_stream::probe_layout(
        layout_fixture(form_stream::LayoutKind::form_section_18));
    expect(section18.ok(), "form section 18 layout must probe");
    expect(
        section18.value() == form_stream::StorageLayout{
            form_stream::OuterFormat::v27,
            form_stream::LayoutKind::form_section_18,
            20,
            18,
            14,
            10,
            11,
        },
        "form section 18 layout descriptor mismatch");

    expect_failure(
        form_stream::probe_layout(list_stream::parse("{26}")),
        "OOF1112",
        "$/0",
        "outer format without a proven nested layout must be rejected");

    auto wrong_root_arity = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_root_arity.items.pop_back();
    expect_failure(
        form_stream::probe_layout(wrong_root_arity),
        "OOF1102",
        "$",
        "root arity drift must be rejected");

    auto wrong_version = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_version.items[1].items[0] = list_stream::ListValue::raw_atom("17");
    expect_failure(
        form_stream::probe_layout(wrong_version),
        "OOF1113",
        "$/1/0",
        "unknown form section version must be rejected");

    auto wrong_form_arity = layout_fixture(form_stream::LayoutKind::form_section_16);
    wrong_form_arity.items[1].items.push_back(list_stream::ListValue::raw_atom("0"));
    expect_failure(
        form_stream::probe_layout(wrong_form_arity),
        "OOF1102",
        "$/1",
        "form section arity drift must be rejected");

    auto wrong_page_style_version = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_page_style_version.items[13].items[0] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(
        form_stream::probe_layout(wrong_page_style_version),
        "OOF1106",
        "$/13/0",
        "page-style section version drift must be rejected");
}

void test_runtime_envelope() {
    constexpr std::string_view uuid = "01234567-89ab-cdef-0123-456789abcdef";
    const auto payload = layout_fixture(form_stream::LayoutKind::form_section_18);
    const std::string source = runtime_envelope_text(payload);
    const auto decoded = form_stream::decode_runtime_envelope(source);
    expect(decoded.ok(), "runtime envelope must decode");
    expect(decoded.value().runtime_uuid.canonical == uuid, "runtime UUID must decode");
    const auto encoded = form_stream::encode_runtime_envelope(decoded.value());
    expect(encoded && encoded.value() == source, "runtime envelope must encode deterministically");

    const auto with_bom = form_stream::decode_runtime_envelope("\xef\xbb\xbf" + source);
    expect(with_bom.ok(), "runtime envelope must accept a UTF-8 BOM from TextDocument");
    const auto encoded_without_bom = form_stream::encode_runtime_envelope(with_bom.value());
    expect(
        encoded_without_bom && encoded_without_bom.value() == source,
        "runtime envelope encoding must be canonical without a BOM");

    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"!\"," + std::string(uuid) + "," +
            list_stream::dump_compact(payload) + "}"),
        "OOF1106",
        "$/0",
        "wrong runtime marker must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"#\",not-a-guid," + list_stream::dump_compact(payload) + "}"),
        "OOF1105",
        "$/1",
        "malformed runtime UUID must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope("{\"#\",01234567-89ab-cdef-0123-456789abcdef,{99}}"),
        "OOF1111",
        "$/2/0",
        "unknown payload format must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"#\",01234567-89ab-cdef-0123-456789abcdef,{26}}"),
        "OOF1112",
        "$/2/0",
        "runtime envelope must reject an outer format without a proven layout");
}

void test_attributes() {
    const auto empty = form_stream::decode_attributes(list_stream::parse("{{1},0,{0},{0}}"));
    expect(empty.ok(), "empty attribute record must decode");
    expect(empty.value() == form_stream::AttributesRecord{}, "empty attribute DTO mismatch");

    constexpr std::string_view fixture =
        "{{1},2,{1,{{1,01234567-89ab-cdef-0123-456789abcdef},1,0,1,\"Value\","
        "{\"Pattern\",{\"T\",d47d59f8-73f0-481c-8b5e-f6384c0a4804}}}},"
        "{1,{42,{1,{1,01234567-89ab-cdef-0123-456789abcdef}}}}}";
    const auto decoded = form_stream::decode_attributes(list_stream::parse(fixture));
    expect(decoded.ok(), "attribute fixture must decode");
    expect(decoded.value().slot_count == 2, "attribute slot count mismatch");
    expect(decoded.value().attributes.size() == 1, "attribute count mismatch");
    expect(decoded.value().attributes[0].name == "Value", "attribute name mismatch");
    expect(decoded.value().attributes[0].main, "attribute main flag mismatch");
    expect(decoded.value().links.size() == 1, "attribute link count mismatch");
    expect(decoded.value().links[0].control_id == 42, "attribute link control mismatch");

    const auto encoded = form_stream::encode_attributes(decoded.value());
    expect(encoded.ok(), "attribute fixture must encode");
    expect(list_stream::dump_compact(encoded.value()) == fixture, "attribute fixture must be canonical");
    const auto rebuilt = form_stream::decode_attributes(encoded.value());
    expect(rebuilt && rebuilt.value() == decoded.value(), "attribute DTO must round-trip");

    expect_failure(
        form_stream::decode_attributes(list_stream::parse("{{1},0,{1},{0}}")),
        "OOF1102",
        "$/2/2",
        "attribute count mismatch must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse("{{2},0,{0},{0}}")),
        "OOF1106",
        "$/2/0/0",
        "attribute version mismatch must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse(
            "{{1},2,{1,{{1,01234567-89ab-cdef-0123-456789abcdef},1,0,1,\"Value\","
            "{\"Other\"}}},{0}}")),
        "OOF1108",
        "$/2/2/1/5",
        "malformed attribute type must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse(
            "{{1},0,{0},{1,{-1,{1,{0}}}}}")),
        "OOF1105",
        "$/2/3/1/0",
        "negative control ID must be rejected");
}

void test_attribute_encode_validation() {
    form_stream::AttributesRecord invalid;
    invalid.attributes.push_back(form_stream::AttributeRecord{
        composite(1, "not-a-guid"),
        false,
        false,
        "Value",
        {},
    });
    expect_failure(
        form_stream::encode_attributes(invalid),
        "OOF1107",
        "$/2/2/1/0",
        "encoder must reject malformed CompositeID UUID");
}

}  // namespace

int main() {
    try {
        test_outer_format_probe();
        test_layout_probe();
        test_runtime_envelope();
        test_attributes();
        test_attribute_encode_validation();
    } catch (const std::exception& error) {
        std::cerr << "form stream tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form stream tests: PASS\n";
    return 0;
}
