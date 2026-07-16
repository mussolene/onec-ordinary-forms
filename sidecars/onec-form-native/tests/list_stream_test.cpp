#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "oof/storage/list_stream.hpp"

namespace {

namespace list_stream = oof::storage::list_stream;

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

bool equal(const list_stream::ListValue& left, const list_stream::ListValue& right) {
    if (left.is_list != right.is_list ||
        left.atom_kind != right.atom_kind ||
        left.atom != right.atom ||
        left.items.size() != right.items.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.items.size(); ++index) {
        if (!equal(left.items[index], right.items[index])) {
            return false;
        }
    }
    return true;
}

void test_empty_and_nested_lists() {
    const auto empty = list_stream::parse("{}");
    expect(empty.is_list, "empty list must parse as a list");
    expect(empty.items.empty(), "empty list must have no items");
    expect(list_stream::dump_compact(empty) == "{}", "empty list must serialize canonically");

    const auto nested = list_stream::parse("{{},{1,{\"two\"}}}");
    expect(nested.is_list && nested.items.size() == 2, "nested list must retain root items");
    expect(nested.items[0].is_list && nested.items[0].items.empty(), "nested empty list must survive parsing");
    expect(nested.items[1].items[1].items[0].atom == "two", "deep string atom must survive parsing");
    expect(
        list_stream::dump_compact(nested) == "{{},{1,{\"two\"}}}",
        "nested list must serialize canonically");

    const auto two_empty_slots = list_stream::parse("{,}");
    expect(two_empty_slots.items.size() == 2, "one separator must retain two empty slots");
    expect(two_empty_slots.items.front().atom.empty(), "leading empty slot must remain empty");
    expect(two_empty_slots.items.back().atom.empty(), "trailing empty slot must remain empty");
    expect(list_stream::dump_compact(two_empty_slots) == "{,}", "empty slots must serialize canonically");

    const auto trailing_empty_slot = list_stream::parse("{1,}");
    expect(trailing_empty_slot.items.size() == 2, "trailing separator must retain an empty slot");
    expect(trailing_empty_slot.items.back().atom.empty(), "trailing separator slot must remain empty");
    expect(
        list_stream::dump_compact(trailing_empty_slot) == "{1,}",
        "trailing empty slot must serialize canonically");
}

void test_quoted_strings() {
    const std::string text = "{\"comma,value\",\"{braces}\",\"say \"\"hello\"\"\",\"\"}";
    const auto value = list_stream::parse(text);
    expect(value.items.size() == 4, "quoted strings must remain distinct list items");
    expect(value.items[0].atom == "comma,value", "comma inside string must not split the list");
    expect(value.items[1].atom == "{braces}", "braces inside string must remain text");
    expect(value.items[2].atom == "say \"hello\"", "doubled quotes must decode to one quote");
    expect(value.items[3].atom.empty(), "empty quoted string must remain a string atom");
    expect(list_stream::dump_compact(value) == text, "quoted string output must be deterministic");
}

void test_bool_codec() {
    list_stream::ListInStream in("{0,1}");
    in.begin_list();
    expect(!in.read_bool(), "bool atom 0 must decode as false");
    expect(in.read_bool(), "bool atom 1 must decode as true");
    in.end_list();

    expect_rejected(
        [] { list_stream::ListInStream("2").read_bool(); },
        "bool atom 2 must be rejected");
    expect_rejected(
        [] { list_stream::ListInStream("1x").read_bool(); },
        "bool atom with a trailing suffix must be rejected");
}

void test_integer_codecs() {
    list_stream::ListInStream signed_in("{-9223372036854775808,9223372036854775807}");
    signed_in.begin_list();
    expect(
        signed_in.read_int64() == std::numeric_limits<std::int64_t>::min(),
        "minimum int64 must decode exactly");
    expect(
        signed_in.read_int64() == std::numeric_limits<std::int64_t>::max(),
        "maximum int64 must decode exactly");
    signed_in.end_list();

    expect_rejected(
        [] { list_stream::ListInStream("-9223372036854775809").read_int64(); },
        "negative int64 overflow must be rejected");
    expect_rejected(
        [] { list_stream::ListInStream("9223372036854775808").read_int64(); },
        "positive int64 overflow must be rejected");

    list_stream::ListInStream unsigned_in("{0,4294967295}");
    unsigned_in.begin_list();
    expect(unsigned_in.read_uint32() == 0, "zero uint32 must decode exactly");
    expect(
        unsigned_in.read_uint32() == std::numeric_limits<std::uint32_t>::max(),
        "maximum uint32 must decode exactly");
    unsigned_in.end_list();

    expect_rejected(
        [] { list_stream::ListInStream("4294967296").read_uint32(); },
        "uint32 overflow must be rejected");
    expect_rejected(
        [] { list_stream::ListInStream("-1").read_uint32(); },
        "negative uint32 must be rejected");
}

void test_double_codec() {
    list_stream::ListInStream in("{0.125,-1.25e+10}");
    in.begin_list();
    expect(in.read_double() == 0.125, "fractional double must decode exactly");
    expect(in.read_double() == -1.25e10, "exponent double must decode exactly");
    in.end_list();

    list_stream::ListOutStream out;
    out.write_double(0.125);
    expect(out.text() == "0.125", "double output must be deterministic and locale-independent");
    expect(list_stream::ListInStream(out.text()).read_double() == 0.125, "written double must decode exactly");

    expect_rejected(
        [] { list_stream::ListInStream("1e9999").read_double(); },
        "double overflow must be rejected");
    expect_rejected(
        [] { list_stream::ListInStream("nan").read_double(); },
        "non-finite double must be rejected");
    expect_rejected(
        [] { list_stream::ListInStream("1.0suffix").read_double(); },
        "double atom with a trailing suffix must be rejected");
    expect_rejected(
        [] {
            list_stream::ListOutStream invalid;
            invalid.write_double(std::numeric_limits<double>::infinity());
        },
        "non-finite double must not be serialized");
}

void test_guid_codec() {
    const std::string canonical = "01234567-89ab-cdef-0123-456789abcdef";
    expect(
        list_stream::ListInStream(canonical).read_guid() == canonical,
        "canonical GUID must decode unchanged");

    list_stream::ListOutStream out;
    out.write_guid(canonical);
    expect(out.text() == canonical, "canonical GUID must serialize unchanged");

    expect_rejected(
        [] { list_stream::ListInStream("0123456789abcdef0123456789abcdef").read_guid(); },
        "GUID without canonical hyphens must be rejected");
    expect_rejected(
        [] { list_stream::ListInStream("01234567-89ab-cdef-0123-456789abcdeg").read_guid(); },
        "GUID with non-hex digits must be rejected");
    expect_rejected(
        [] {
            list_stream::ListOutStream invalid;
            invalid.write_guid("not-a-guid");
        },
        "malformed GUID must not be serialized");
}

void test_tree_roundtrip_and_determinism() {
    const auto first = list_stream::parse("{{1,\"a,b\"},{2,{\"x\"\"y\"}}}");
    const std::string serialized = list_stream::dump_compact(first);
    const auto second = list_stream::parse(serialized);
    expect(equal(first, second), "ListValue parse-write-parse must preserve the tree");
    expect(list_stream::dump_compact(second) == serialized, "ListValue output must be deterministic");

    list_stream::ListOutStream out;
    out.begin_list();
    out.write_uint32(std::numeric_limits<std::uint32_t>::max());
    out.write_int64(std::numeric_limits<std::int64_t>::min());
    out.write_bool(true);
    out.write_string("a,b");
    out.begin_list();
    out.end_list();
    out.end_list();
    const std::string expected = "{4294967295,-9223372036854775808,1,\"a,b\",{}}";
    expect(out.text() == expected, "ListOutStream must emit canonical compact output");
    expect(out.text() == expected, "repeated ListOutStream output must be deterministic");
    const auto reparsed_output = list_stream::parse(out.text());
    expect(equal(reparsed_output, out.root()), "direct ListOutStream write-parse must preserve the tree");
    expect(
        list_stream::dump_compact(reparsed_output) == out.text(),
        "direct ListOutStream write-parse-write must remain deterministic");
}

void test_listout_layout() {
    const auto payload = list_stream::ListValue::list({
        list_stream::ListValue::raw_atom("#base64:QQ=="),
        list_stream::ListValue::raw_atom("Rk9STQ=="),
    });
    const std::string expected = "{#base64:QQ==\r\n\r\nRk9STQ==}";
    expect(list_stream::dump_listout(payload) == expected, "ListOut base64 layout must remain byte-stable");
    expect(list_stream::dump_listout(payload) == expected, "ListOut layout must be deterministic");
    expect(equal(list_stream::parse(expected), payload), "ListOut base64 layout must parse back to the same tree");
}

void test_malformed_input() {
    expect_rejected(
        [] { list_stream::parse("{} trailing"); },
        "trailing document input must be rejected");
    expect_rejected(
        [] { list_stream::parse("{1"); },
        "missing closing brace must be rejected");
    expect_rejected(
        [] { list_stream::parse("1}"); },
        "unbalanced closing brace must be rejected");
    expect_rejected(
        [] { list_stream::parse("{1 2}"); },
        "adjacent raw values without a comma must be rejected");
    expect_rejected(
        [] { list_stream::parse(R"({"x"{1}})"); },
        "adjacent string and list values without a comma must be rejected");
    expect_rejected(
        [] { list_stream::parse(R"({#base64:QQ== "Rk9STQ=="})"); },
        "base64 adjacency exception must reject a quoted chunk");
    expect_rejected(
        [] { list_stream::parse("{#base64:QQ== {Rk9STQ==}}"); },
        "base64 adjacency exception must reject a nested list chunk");
    expect_rejected(
        [] { list_stream::parse(R"({"bad\"quote"})"); },
        "backslash quote escape must be rejected");
    expect_rejected(
        [] { list_stream::parse(R"({raw"quote})"); },
        "quote inside a raw atom must be rejected");
    expect_rejected(
        [] { list_stream::parse("{\"unterminated}"); },
        "unterminated quoted string must be rejected");
}

}  // namespace

int main() {
    try {
        test_empty_and_nested_lists();
        test_quoted_strings();
        test_bool_codec();
        test_integer_codecs();
        test_double_codec();
        test_guid_codec();
        test_tree_roundtrip_and_determinism();
        test_listout_layout();
        test_malformed_input();
    } catch (const std::exception& error) {
        std::cerr << "list stream tests: FAIL: " << error.what() << '\n';
        return 1;
    }

    std::cout << "list stream tests: PASS\n";
    return 0;
}
