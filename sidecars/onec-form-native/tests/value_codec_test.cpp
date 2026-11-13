#include <array>
#include <exception>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <string>
#include <string_view>
#include <vector>

#include "oof/storage/value_codec.hpp"
#include "oof/model/metamodel.hpp"

namespace {

namespace model = oof::model;
namespace codec = oof::storage::value_codec;

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

void test_line() {
    std::size_t count = 0;
    for (const auto& family : model::metamodel::line_family_descriptors()) {
        const bool cell = family.name == "SpreadsheetDocumentCellLineType";
        const bool drawing = family.name == "SpreadsheetDocumentDrawingLineType";
        for (const auto member : family.members) {
            ++count;
            const auto maximum = member == "None" || (cell && member == "Double") ? 1 : (cell || drawing ? 3 : 50);
            model::LineValue value{{std::string(family.name), std::string(member)}, maximum, false};
            expect(codec::decode_line(codec::encode_line(value)) == value, "line max must round trip");
            value.width = maximum + 1;
            expect_rejected([&] { (void)codec::encode_line(value); }, "line over max must fail");
            value.width = std::numeric_limits<std::int32_t>::min();
            expect(codec::decode_line(codec::encode_line(value)) == value, "minimum line width must round trip");
            value.width = -1;
            expect(codec::decode_line(codec::encode_line(value)) == value, "negative line width must round trip");
            value.gap = true;
            if (cell) expect(codec::decode_line(codec::encode_line(value)) == value, "cell gap must round trip");
            else expect_rejected([&] { (void)codec::encode_line(value); }, "other families reject gap");
        }
    }
    expect(count == 25, "line family registry must cover all members");
    for (const auto* text : {
        "{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0,0}",
        "{4,1,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0}",
        "{4,0,{0},1,1,4294967296,e5cabe59-d992-4d31-8086-3116931aff81,0}",
        "{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,2}",
        "{4,0,{0},50,1,7,e5cabe59-d992-4d31-8086-3116931aff81,0}",
        "{4,0,{0},1,1,0,00000000-0000-0000-0000-000000000000,0}"})
        expect_rejected([&] { (void)codec::decode_line(text); }, "malformed line must fail");
}

void test_absolute_line_override_flags() {
    struct LineCase { model::LineValue value; std::string_view guid; unsigned ordinal; };
    const std::array<LineCase, 12> cases{{
        {{{"ChartLineType", "Solid"}, 1, false}, "e5cabe59-d992-4d31-8086-3116931aff81", 1},
        {{{"ChartLineType", "Dashed"}, 50, false}, "e5cabe59-d992-4d31-8086-3116931aff81", 3},
        {{{"ChartLineType", "Dotted"}, 0, false}, "e5cabe59-d992-4d31-8086-3116931aff81", 2},
        {{{"SpreadsheetDocumentDrawingLineType", "Solid"}, 3, false}, "b7438842-27cc-42a3-846f-2250cd9c1bc3", 1},
        {{{"SpreadsheetDocumentDrawingLineType", "Dotted"}, 2, false}, "b7438842-27cc-42a3-846f-2250cd9c1bc3", 3},
        {{{"SpreadsheetDocumentDrawingLineType", "None"}, -1, false}, "b7438842-27cc-42a3-846f-2250cd9c1bc3", 0},
        {{{"SpreadsheetDocumentCellLineType", "Double"}, 1, true}, "f527dc88-1d39-40b3-bcbb-d98b690ead68", 3},
        {{{"SpreadsheetDocumentCellLineType", "ThickDashed"}, 3, false}, "f527dc88-1d39-40b3-bcbb-d98b690ead68", 5},
        {{{"SpreadsheetDocumentCellLineType", "Solid"}, -2, true}, "f527dc88-1d39-40b3-bcbb-d98b690ead68", 1},
        {{{"GeographicalSchemaLineType", "Solid"}, 50, false}, "e7bbf659-5435-44b8-b759-461ee479e513", 1},
        {{{"GeographicalSchemaLineType", "Dotted"}, 1, false}, "e7bbf659-5435-44b8-b759-461ee479e513", 2},
        {{{"GeographicalSchemaLineType", "DashDottedDotted"}, -2, false}, "e7bbf659-5435-44b8-b759-461ee479e513", 5},
    }};
    // core::V8Line kind zero compares all three values and the GUID, without flags.
    for (const auto& item : cases) {
        const auto prefix = "{4,0,{0}," + std::to_string(item.ordinal) + "," +
            std::to_string(static_cast<std::uint32_t>(item.value.width)) + ",";
        const auto suffix = "," + std::string(item.guid) + "," + (item.value.gap ? "1}" : "0}");
        const auto canonical = prefix + (item.value == model::LineValue{} ? "0" : "7") + suffix;
        for (const std::uint32_t flags : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 2147483648U, 4294967295U}) {
            const auto decoded = codec::decode_line(prefix + std::to_string(flags) + suffix);
            expect(decoded == item.value && codec::encode_line(decoded) == canonical &&
                       codec::decode_line(codec::encode_line(decoded)) == item.value,
                "absolute Line flags must normalize without losing type, signed width, family or gap");
        }
    }
    for (const auto invalid : {
             "{4,0,{0},1,1,-1,e5cabe59-d992-4d31-8086-3116931aff81,0}",
             "{4,0,{0},1,1,4294967296,e5cabe59-d992-4d31-8086-3116931aff81,0}",
             "{4,1,{0},1,1,1,e5cabe59-d992-4d31-8086-3116931aff81,0}",
             "{4,0,{1},1,1,1,e5cabe59-d992-4d31-8086-3116931aff81,0}",
             "{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,1}",
             "{4,0,{0},3,2,0,f527dc88-1d39-40b3-bcbb-d98b690ead68,0}"})
        expect_rejected([&] { (void)codec::decode_line(invalid); },
            "Line flag normalization must retain uint32, absolute-kind, reference, width and gap constraints");
}

void test_localized_string() {
    const model::LocalizedStringValue empty;
    expect(codec::encode_localized_string(empty) == "{1,0}", "empty localization must encode");
    expect(codec::decode_localized_string("{1,0}") == empty, "empty localization must decode");

    const model::LocalizedStringValue value{{
        {"ru", "Заголовок"},
        {"en", "Title, \"quoted\""},
    }};
    const std::string encoded = codec::encode_localized_string(value);
    expect(codec::decode_localized_string(encoded) == value, "localized string must round-trip");
    expect(codec::encode_localized_string(codec::decode_localized_string(encoded)) == encoded,
           "localized string output must be deterministic");

    expect_rejected(
        [] { static_cast<void>(codec::decode_localized_string("{2,0}")); },
        "unknown LocalizedString version must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_localized_string("{1,2,{\"ru\",\"one\"}}")); },
        "localized item count mismatch must be rejected");
    for (const std::string_view invalid : {"{1,4294967295}", "{1,4294967295,{\"ru\",\"one\"}}"}) {
        expect_rejected([invalid] { static_cast<void>(codec::decode_localized_string(invalid)); },
            "an excessive localized count must report a truncated list without reserving that count");
    }
}

void test_formatted_string() {
    const model::FormattedStringValue value{
        model::LocalizedStringValue{{{"ru", "Текст"}}},
        true,
    };
    const std::string encoded = codec::encode_formatted_string(value);
    expect(
        encoded == "{1,{1,1,{\"ru\",\"Текст\"}},1}",
        "formatted string must match the proven platform fixture");
    expect(codec::decode_formatted_string(encoded) == value, "formatted string must round-trip");
    for (const bool formatted : {false, true}) {
        const model::FormattedStringValue runtime{std::string{"До <img 0:448d6f55-d885-496c-870d-d1bd78374745/> после"}, formatted};
        const auto text = codec::encode_formatted_string(runtime);
        expect(codec::decode_formatted_string(text) == runtime,
            "plain runtime FormattedString must retain its text and format flag without a localization sentinel");
    }
    expect_rejected([] { static_cast<void>(codec::decode_formatted_string(
        R"VALUE({"#",00000000-0000-0000-0000-000000000001,1,"text"})VALUE")); },
        "runtime FormattedString must reject a different type factory");
    expect_rejected([] { static_cast<void>(codec::decode_formatted_string(
        R"VALUE({"#",140b5ff4-37b1-4df5-b5ec-a0bfd2b94f8f,2,"text"})VALUE")); },
        "runtime FormattedString must reject a nonboolean format flag");
    expect_rejected(
        [] { static_cast<void>(codec::decode_formatted_string("{2,{1,0},0}")); },
        "unknown FormattedString version must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_formatted_string("{1,{1,0},2}")); },
        "invalid formatted flag must be rejected");
}

void test_composite_id() {
    const model::CompositeIdValue null_value;
    expect(codec::encode_composite_id(null_value) == "{0}", "null CompositeID must be compact");
    expect(codec::decode_composite_id("{0}") == null_value, "null CompositeID must round-trip");

    const model::CompositeIdValue value{
        42,
        model::UuidValue{"01234567-89ab-cdef-0123-456789abcdef"},
        false,
    };
    const std::string encoded = codec::encode_composite_id(value);
    expect(
        encoded == "{42,01234567-89ab-cdef-0123-456789abcdef}",
        "CompositeID must match the proven platform fixture");
    expect(codec::decode_composite_id(encoded) == value, "CompositeID must round-trip");
    expect_rejected(
        [] { static_cast<void>(codec::decode_composite_id("{42,not-a-guid}")); },
        "malformed CompositeID UUID must be rejected");
}

void test_type_domain() {
    model::TypeDomainEntry boolean;
    boolean.term = model::TypeDomainTerm::boolean;
    const model::TypeDomainPatternValue platform_boolean_fixture{{boolean}};
    expect(
        codec::encode_type_domain(platform_boolean_fixture) == "{\"Pattern\",{\"B\"}}",
        "Boolean type-domain must match the Designer 8.5.1 fixture");
    expect(
        codec::decode_type_domain("{\"Pattern\",{\"B\"}}") == platform_boolean_fixture,
        "Boolean type-domain fixture must decode");
    expect_rejected(
        [] { static_cast<void>(codec::decode_type_domain("{\"Pattern\",{\"B\",1}}")); },
        "Boolean type-domain qualifiers must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_type_domain("{\"Pattern\",{\"B\",1,0}}")); },
        "Boolean type-domain qualifiers must be rejected");

    model::TypeDomainEntry platform_type_entry;
    platform_type_entry.term = model::TypeDomainTerm::type;
    platform_type_entry.type_uuid =
        model::UuidValue{"d47d59f8-73f0-481c-8b5e-f6384c0a4804"};
    const model::TypeDomainPatternValue platform_type_fixture{{platform_type_entry}};
    expect(
        codec::encode_type_domain(platform_type_fixture) ==
            "{\"Pattern\",{\"T\",d47d59f8-73f0-481c-8b5e-f6384c0a4804}}",
        "type-domain must match the proven platform fixture");

    model::TypeDomainEntry value_list_entry;
    value_list_entry.term = model::TypeDomainTerm::value_list;
    const model::TypeDomainPatternValue value_list_fixture{{value_list_entry}};
    expect(
        codec::encode_type_domain(value_list_fixture) ==
            "{\"Pattern\",{\"#\",4772b3b4-f4a3-49c0-a1a5-8cb5961511a3}}",
        "ValueList term must use the independent platform type descriptor");
    expect(
        codec::decode_type_domain("{\"Pattern\",{\"#\",4772b3b4-f4a3-49c0-a1a5-8cb5961511a3}}") ==
            value_list_fixture,
        "ValueList platform type descriptor must decode as the named term");
    model::TypeDomainEntry value_tree_entry;
    value_tree_entry.term = model::TypeDomainTerm::value_tree;
    const model::TypeDomainPatternValue value_tree_fixture{{value_tree_entry}};
    // Независимая строгая выгрузка формы с реквизитом ДеревоЗначений.
    constexpr std::string_view value_tree_pattern =
        "{\"Pattern\",{\"#\",e603c0f2-92fb-4d47-8f38-a44a381cf235}}";
    expect(codec::encode_type_domain(value_tree_fixture) == value_tree_pattern &&
        codec::decode_type_domain(value_tree_pattern) == value_tree_fixture,
        "ValueTree must roundtrip the independently captured platform type");
    for (const auto token : {"T", "R", "L"}) {
        const auto other = codec::decode_type_domain(std::string("{\"Pattern\",{\"") + token +
            "\",e603c0f2-92fb-4d47-8f38-a44a381cf235}}");
        expect(other.entries.front().term != model::TypeDomainTerm::value_tree,
            "ValueTree recognition must be limited to the # token");
    }
    auto invalid_tree = value_tree_entry;
    invalid_tree.type_uuid = model::UuidValue{"11111111-1111-1111-1111-111111111111"};
    expect_rejected([&] { (void)codec::encode_type_domain(model::TypeDomainPatternValue{{invalid_tree}}); },
        "ValueTree must reject caller-supplied UUID");
    invalid_tree = value_tree_entry;
    invalid_tree.numeric = {8, 2, false};
    expect_rejected([&] { (void)codec::encode_type_domain(model::TypeDomainPatternValue{{invalid_tree}}); },
        "ValueTree must reject qualifiers");
    model::TypeDomainEntry disguised_tree;
    disguised_tree.term = model::TypeDomainTerm::object;
    disguised_tree.type_uuid = model::UuidValue{"e603c0f2-92fb-4d47-8f38-a44a381cf235"};
    expect_rejected([&] { (void)codec::encode_type_domain(model::TypeDomainPatternValue{{disguised_tree}}); },
        "ValueTree must not be encoded as a generic object");
    for (const auto token : {"T", "R", "L"}) {
        const auto other_platform_term = codec::decode_type_domain(
            std::string("{\"Pattern\",{\"") + token +
            "\",4772b3b4-f4a3-49c0-a1a5-8cb5961511a3}}");
        expect(other_platform_term.entries.front().term != model::TypeDomainTerm::value_list,
            "ValueList recognition must be limited to the proven # type token");
    }

    // Платформенный GUID может использовать любой регистр шестнадцатеричных цифр.
    for (const auto& [upper_uuid, lower_uuid, term] : {
             std::tuple{"4772B3B4-F4A3-49C0-A1A5-8CB5961511A3", "4772b3b4-f4a3-49c0-a1a5-8cb5961511a3", model::TypeDomainTerm::value_list},
             std::tuple{"ACF6192E-81CA-46EF-93A6-5A6968B78663", "acf6192e-81ca-46ef-93a6-5a6968b78663", model::TypeDomainTerm::value_table},
             std::tuple{"E603C0F2-92FB-4D47-8F38-A44A381CF235", "e603c0f2-92fb-4d47-8f38-a44a381cf235", model::TypeDomainTerm::value_tree}}) {
        const auto upper_stream = std::string("{\"Pattern\",{\"#\",") + upper_uuid + "}}";
        const auto canonical_stream = std::string("{\"Pattern\",{\"#\",") + lower_uuid + "}}";
        const auto parsed = codec::decode_type_domain(upper_stream);
        expect(parsed.entries.size() == 1 && parsed.entries.front().term == term && !parsed.entries.front().type_uuid,
            "Known uppercase UUID must resolve to its named type");
        expect(codec::encode_type_domain(parsed) == canonical_stream,
            "Named known UUID must rebuild in canonical lowercase");
        model::TypeDomainEntry disguised;
        disguised.term = model::TypeDomainTerm::object;
        disguised.type_uuid = model::UuidValue{upper_uuid};
        expect_rejected([&] { (void)codec::encode_type_domain(model::TypeDomainPatternValue{{disguised}}); },
            "Known uppercase UUID must not be encoded as a generic object");
    }
    constexpr std::string_view unrelated_upper = "{\"Pattern\",{\"#\",ABCDEF12-1234-1234-1234-123456789ABC}}";
    const auto unrelated = codec::decode_type_domain(unrelated_upper);
    expect(unrelated.entries.front().term == model::TypeDomainTerm::object &&
        codec::encode_type_domain(unrelated) == unrelated_upper,
        "Unknown uppercase object UUID must retain its original representation");

    model::TypeDomainEntry value_table_entry;
    value_table_entry.term = model::TypeDomainTerm::value_table;
    const model::TypeDomainPatternValue value_table_fixture{{value_table_entry}};
    constexpr std::string_view value_table_pattern =
        "{\"Pattern\",{\"#\",acf6192e-81ca-46ef-93a6-5a6968b78663}}";
    expect(codec::encode_type_domain(value_table_fixture) == value_table_pattern,
        "ValueTable term must emit its independently proven fixed platform descriptor");
    expect(codec::decode_type_domain(value_table_pattern) == value_table_fixture,
        "ValueTable platform descriptor must decode as the named term");
    for (const auto token : {"T", "R", "L"}) {
        const auto other_platform_term = codec::decode_type_domain(
            std::string("{\"Pattern\",{\"") + token +
            "\",acf6192e-81ca-46ef-93a6-5a6968b78663}}");
        expect(other_platform_term.entries.front().term != model::TypeDomainTerm::value_table,
            "ValueTable recognition must be limited to the proven # type token");
    }
    expect(codec::decode_type_domain(
        "{\"Pattern\",{\"#\",d47d59f8-73f0-481c-8b5e-f6384c0a4804}}")
        .entries.front().term == model::TypeDomainTerm::object,
        "unrelated object type UUID must remain a named object rather than ValueTable");
    model::TypeDomainEntry object_entry;
    object_entry.term = model::TypeDomainTerm::object;
    object_entry.type_uuid = model::UuidValue{"11111111-1111-1111-1111-111111111111"};
    const model::TypeDomainPatternValue object_domain{{object_entry}};
    const auto object_stream = codec::encode_type_domain(object_domain);
    expect(object_stream == "{\"Pattern\",{\"#\",11111111-1111-1111-1111-111111111111}}" &&
        codec::decode_type_domain(object_stream) == object_domain,
        "Concrete object type must map to the native UUID descriptor without becoming unknown");
    object_entry.term = model::TypeDomainTerm::unknown;
    expect_rejected([&] { static_cast<void>(codec::encode_type_domain(model::TypeDomainPatternValue{{object_entry}})); },
        "The old unknown term must not remain an alternate path for concrete objects");
    object_entry.term = model::TypeDomainTerm::object;
    object_entry.type_uuid.reset();
    expect_rejected([&] { static_cast<void>(codec::encode_type_domain(model::TypeDomainPatternValue{{object_entry}})); },
        "An object type cannot omit its UUID");
    model::TypeDomainEntry invalid_value_table = value_table_entry;
    invalid_value_table.type_uuid = model::UuidValue{"d47d59f8-73f0-481c-8b5e-f6384c0a4804"};
    expect_rejected(
        [&] { static_cast<void>(codec::encode_type_domain(
            model::TypeDomainPatternValue{{invalid_value_table}})); },
        "ValueTable must reject an overriding type UUID");
    invalid_value_table = value_table_entry;
    invalid_value_table.numeric = {8, 2, false};
    expect_rejected(
        [&] { static_cast<void>(codec::encode_type_domain(
            model::TypeDomainPatternValue{{invalid_value_table}})); },
        "ValueTable must reject type qualifiers");

    model::TypeDomainEntry invalid_value_list = value_list_entry;
    invalid_value_list.type_uuid = model::UuidValue{"d47d59f8-73f0-481c-8b5e-f6384c0a4804"};
    expect_rejected(
        [&] { static_cast<void>(codec::encode_type_domain(
            model::TypeDomainPatternValue{{invalid_value_list}})); },
        "ValueList must reject an overriding type UUID");
    invalid_value_list = value_list_entry;
    invalid_value_list.string = {64, false};
    expect_rejected(
        [&] { static_cast<void>(codec::encode_type_domain(
            model::TypeDomainPatternValue{{invalid_value_list}})); },
        "ValueList must reject qualifiers");

    model::TypeDomainPatternValue value;

    model::TypeDomainEntry type;
    type.term = model::TypeDomainTerm::type;
    type.type_uuid = model::UuidValue{"d47d59f8-73f0-481c-8b5e-f6384c0a4804"};
    value.entries.push_back(type);

    model::TypeDomainEntry numeric;
    numeric.term = model::TypeDomainTerm::numeric;
    numeric.numeric = {15, 3, true};
    value.entries.push_back(numeric);

    model::TypeDomainEntry string;
    string.term = model::TypeDomainTerm::string;
    string.string = {64, false};
    value.entries.push_back(string);

    model::TypeDomainEntry date;
    date.term = model::TypeDomainTerm::date;
    date.date = {true, false};
    value.entries.push_back(date);

    model::TypeDomainEntry reference;
    reference.term = model::TypeDomainTerm::reference;
    reference.type_uuid = model::UuidValue{"01234567-89ab-cdef-0123-456789abcdef"};
    value.entries.push_back(reference);

    const std::string encoded = codec::encode_type_domain(value);
    expect(codec::decode_type_domain(encoded) == value, "type-domain qualifiers must round-trip");
    expect_rejected([] { static_cast<void>(codec::decode_type_domain("{\"Pattern\",{\"N\",0,65,0}}")); },
        "numeric type-domain binary reader must reject nonzero precision at zero length");
    model::TypeDomainEntry zero_numeric;
    zero_numeric.term = model::TypeDomainTerm::numeric;
    expect(codec::decode_type_domain("{\"Pattern\",{\"N\",0,0,0}}") == model::TypeDomainPatternValue{{zero_numeric}},
        "numeric type-domain binary reader must accept zero length with zero precision");
    expect(codec::encode_type_domain(codec::decode_type_domain(encoded)) == encoded,
           "type-domain output must be deterministic");

    expect_rejected(
        [] { static_cast<void>(codec::decode_type_domain("{\"Other\"}")); },
        "unknown type-domain root must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_type_domain("{\"Pattern\",{\"X\"}}")); },
        "unknown type-domain term must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_type_domain("{\"Pattern\",{\"D\",\"DD\"}}")); },
        "duplicate date flags must be rejected");

    model::TypeDomainEntry unconfirmed_binary;
    unconfirmed_binary.term = model::TypeDomainTerm::binary;
    unconfirmed_binary.binary = {128, true};
    expect_rejected(
        [&] {
            static_cast<void>(codec::encode_type_domain(
                model::TypeDomainPatternValue{{unconfirmed_binary}}));
        },
        "unconfirmed BinaryData type-domain encoding must be rejected");
}

void test_style_reference() {
    const model::StyleReference none;
    expect(codec::encode_style_reference(none) == "{\"none\"}", "empty style ref must encode");
    expect(codec::decode_style_reference("{\"none\"}") == none, "empty style ref must decode");

    const model::StyleReference qualified = model::QualifiedName{"ui:Picture"};
    expect(
        codec::encode_style_reference(qualified) == "{\"QName\",\"ui:Picture\"}",
        "qualified style ref must match the platform fixture");
    expect(
        codec::decode_style_reference(codec::encode_style_reference(qualified)) == qualified,
        "qualified style ref must round-trip");

    const model::StyleReference composite = model::CompositeIdValue{
        42,
        model::UuidValue{"01234567-89ab-cdef-0123-456789abcdef"},
        false,
    };
    expect(
        codec::decode_style_reference(codec::encode_style_reference(composite)) == composite,
        "CompositeID style ref must round-trip");
    expect_rejected(
        [] { static_cast<void>(codec::decode_style_reference("{\"Other\"}")); },
        "unknown style ref kind must be rejected");
}

void test_color() {
    model::ColorValue absolute;
    absolute.kind = model::ColorKind::absolute;
    absolute.red = 0x12;
    absolute.green = 0x34;
    absolute.blue = 0x56;
    expect(
        codec::encode_color(absolute) == "{3,0,{18,52,86,255},{\"none\"}}",
        "absolute color must match the proven platform fixture");
    expect(
        codec::decode_color(codec::encode_color(absolute)) == absolute,
        "absolute color must round-trip");

    model::ColorValue style;
    style.kind = model::ColorKind::style_reference;
    style.style = model::QualifiedName{"ui:TextColor"};
    expect(codec::decode_color(codec::encode_color(style)) == style, "style color must round-trip");

    expect_rejected(
        [] { static_cast<void>(codec::decode_color("{2,0,{0,0,0,255},{\"none\"}}")); },
        "unknown color field count must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_color("{3,0,{256,0,0,255},{\"none\"}}")); },
        "out-of-range color channel must be rejected");
    expect_rejected(
        [] {
            static_cast<void>(
                codec::decode_color("{3,2,{0,0,0,255},{\"none\"}}"));
        },
        "style color without a reference must be rejected");
}

void test_font() {
    model::FontValue font;
    font.kind = model::FontKind::absolute;
    font.face_name = "Arial";
    font.height = 10.0;
    font.bold = false;
    font.italic = true;
    font.underline = false;
    font.strikeout = true;
    font.scale = 100;
    expect(
        codec::encode_font(font) == "{8,0,63,100,0,0,0,400,1,0,1,0,0,0,0,0,\"Arial\",1,100,0}",
        "absolute Font must match the named presence and storage tuple");
    expect(codec::decode_font(codec::encode_font(font)) == font, "font must round-trip");
    model::FontValue copied_scale = font;
    copied_scale.scale = 125;
    copied_scale.scale_override = true;
    expect(codec::encode_font(copied_scale) == "{8,0,575,100,0,0,0,400,1,0,1,0,0,0,0,0,\"Arial\",1,125,0}" &&
               codec::decode_font(codec::encode_font(copied_scale)) == copied_scale,
        "copy-scale override must use its named marker independently from scale value");
    model::FontValue direct_scale = copied_scale;
    direct_scale.scale_override = false;
    expect(codec::encode_font(direct_scale) == "{8,0,63,100,0,0,0,400,1,0,1,0,0,0,0,0,\"Arial\",1,125,0}" &&
               codec::decode_font(codec::encode_font(direct_scale)) == direct_scale,
        "direct scale parameter must preserve scale without the override marker");

    model::FontValue automatic;
    expect(codec::encode_font(automatic) == "{8,3,0,1,100}", "automatic Font must match its exact default tuple");
    expect(codec::decode_font(codec::encode_font(automatic)) == automatic, "automatic Font must round-trip");
    for (auto value : {font, automatic, codec::decode_font("{8,2,0,{-20},1,100}"),
                       codec::decode_font("{8,1,0,{0},1,100}")}) {
        value.auto_char_set = false;
        const auto modern = codec::encode_font(value);
        expect(codec::decode_font(modern) == value,
            "every Font kind must preserve disabled automatic character set selection");
        auto legacy = modern;
        legacy[1] = '7';
        expect(codec::decode_font(legacy) == value, "Font7 must preserve autoCharSet independently from scale");
        legacy[1] = '6';
        const auto scale = legacy.rfind(",100");
        legacy.erase(scale, 4);
        expect(codec::decode_font(legacy) == value, "Font6 must preserve autoCharSet with implicit scale");
    }
    const auto legacy_automatic = codec::decode_font("{6,3,0,1}");
    expect(legacy_automatic == automatic && codec::encode_font(legacy_automatic) == "{8,3,0,1,100}",
        "observed legacy automatic Font6 must decode to the typed default and rebuild as Font8");
    const auto legacy_style = codec::decode_font("{6,2,0,{-20},1}");
    expect(legacy_style.kind == model::FontKind::style_reference &&
               legacy_style.style == model::StyleReference{model::QualifiedName{"StyleFonts.TextFont"}} &&
               codec::encode_font(legacy_style) == "{8,2,0,{-20},1,100}",
        "observed legacy style Font6 must preserve its named font reference and append default scale");
    const std::string composite_record = "{6,2,0,{0,12345678-1234-1234-1234-123456789abc},1}";
    const auto composite_style = codec::decode_font(composite_record);
    expect(composite_style.kind == model::FontKind::style_reference &&
               composite_style.style == model::StyleReference{model::CompositeIdValue{0,
                   model::UuidValue{"12345678-1234-1234-1234-123456789abc"}, false}} &&
               codec::encode_font(composite_style) ==
                   "{8,2,0,{0,12345678-1234-1234-1234-123456789abc},1,100}",
        "Font6 must normalize to platform Font8 while retaining its typed composite style reference");
    expect_rejected([] { static_cast<void>(codec::decode_font(
        "{6,2,0,{0,00000000-0000-0000-0000-000000000000},1}")); },
        "Font6 null composite style must reject");
    for (const auto weight : {400, 700}) {
        const auto legacy_bold = codec::decode_font("{6,3,4," + std::to_string(weight) + ",1}");
        expect(legacy_bold.bold == std::optional<bool>{weight == 700} && legacy_bold.scale == 100 &&
                   !legacy_bold.scale_override && codec::encode_font(legacy_bold) ==
                       "{8,3,4," + std::to_string(weight) + ",1,100}",
            "legacy Font6 bold override must remain explicit through the canonical Font8 writer");
    }
    for (const auto legacy : {"{6,2,0,1}", "{6,3,4,1}", "{6,3,0,2}", "{6,3,0,1,100}"}) {
        expect_rejected([legacy] { static_cast<void>(codec::decode_font(legacy)); },
            "unobserved Font6 kinds, settings, and trailing fields must be rejected");
    }
    for (const auto& [legacy, modern] : {
             std::pair{"{6,3,2,80,1}", "{8,3,2,80,1,100}"},
             std::pair{"{6,3,8,1,1}", "{8,3,8,1,1,100}"},
             std::pair{"{6,3,16,1,1}", "{8,3,16,1,1,100}"},
             std::pair{"{6,3,32,1,1}", "{8,3,32,1,1,100}"},
             std::pair{"{6,3,1,\"Arial\",1}", "{8,3,1,\"Arial\",1,100}"},
             std::pair{"{6,3,6,80,700,1}", "{8,3,6,80,700,1,100}"},
             std::pair{"{6,2,4,{-20},700,1}", "{8,2,4,{-20},700,1,100}"},
             std::pair{"{6,0,63,200,0,0,0,700,0,0,0,0,0,0,0,0,\"Arial\",1,0}",
                       "{8,0,63,200,0,0,0,700,0,0,0,0,0,0,0,0,\"Arial\",1,100,0}"}}) {
        const auto value = codec::decode_font(legacy);
        expect(value == codec::decode_font(modern) && codec::encode_font(value) == modern &&
                   value.scale == 100 && !value.scale_override,
            "native Font6 variants must preserve typed properties and append default Font8 scale");
    }
    for (const auto selector : {0, 1, 2, 3, 4}) {
        for (const auto flags : {0, 4}) {
            const auto prefix = std::to_string(flags) + ",{" + std::to_string(selector) + "}," +
                (flags == 4 ? "700," : "") + "1";
            const auto value = codec::decode_font("{6,1," + prefix + "}");
            expect(value.kind == model::FontKind::windows_font && value.windows_font &&
                       static_cast<unsigned>(*value.windows_font) == static_cast<unsigned>(selector) &&
                       codec::encode_font(value) == "{8,1," + prefix + ",100}",
                "native Windows Font6 selectors must preserve named kind and overrides through Font8");
        }
    }
    for (const auto legacy : {"{6,3,512,1}", "{6,3,64,1}", "{6,1,0,{5},1}"}) {
        expect_rejected([legacy] { static_cast<void>(codec::decode_font(legacy)); },
            "unproven legacy Font6 settings must remain rejected");
    }
    for (const auto& value : {font, automatic}) {
        const auto modern = codec::encode_font(value);
        auto intermediate = modern;
        intermediate[1] = '7';
        const auto decoded = codec::decode_font(intermediate);
        expect(decoded == value && codec::encode_font(decoded) == modern,
            "native-confirmed Font7 payload must preserve typed properties and write Font8");
    }
    for (const auto intermediate : {"{7,3,512,1,100}", "{7,3,64,1,100}",
             "{7,3,0,1}", "{7,3,0,1,100,0}", "{7,3,0,2,100}",
             "{7,3,0,1,2147483648}"}) {
        expect_rejected([intermediate] { static_cast<void>(codec::decode_font(intermediate)); },
            "Font7 must reject unsupported flags, missing scale, trailers, and invalid ranges");
    }
    model::FontValue automatic_bold_false = automatic;
    automatic_bold_false.bold = false;
    expect(codec::encode_font(automatic_bold_false) == "{8,3,4,400,1,100}" &&
               codec::decode_font(codec::encode_font(automatic_bold_false)) == automatic_bold_false,
        "automatic Font must preserve an explicitly false bold setting");
    model::FontValue automatic_bold_true = automatic;
    automatic_bold_true.bold = true;
    expect(codec::encode_font(automatic_bold_true) == "{8,3,4,700,1,100}" &&
               codec::decode_font(codec::encode_font(automatic_bold_true)) == automatic_bold_true,
        "automatic Font must preserve an explicitly enabled bold setting");
    expect(codec::decode_font("{8,3,8,0,1,100}").italic == false,
        "automatic Font preserves explicit false italic");
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,3,4,500,1,100}")); },
        "automatic Font unobserved bold weights must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,3,4,700,1,100},0")); },
        "Font must reject trailing values outside its own record");
    model::FontValue automatic_italic = automatic;
    automatic_italic.italic = false;
    expect(codec::decode_font(codec::encode_font(automatic_italic)) == automatic_italic,
        "automatic Font preserves italic overrides");

    model::FontValue family_only;
    family_only.kind = model::FontKind::absolute;
    family_only.face_name = "Arial";
    expect(codec::encode_font(family_only) == "{8,0,1,0,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,100,0}",
        "family-only Font must not invent optional named properties");
    expect(codec::decode_font(codec::encode_font(family_only)) == family_only,
        "family-only Font must round-trip with absent optional fields");

    model::FontValue style;
    style.kind = model::FontKind::style_reference;
    style.style = model::QualifiedName{"StyleFonts.TextFont"};
    expect(codec::encode_font(style) == "{8,2,0,{-20},1,100}", "TextFont must use its observed reference tuple");
    expect(codec::decode_font(codec::encode_font(style)) == style, "style font must round-trip");
    const auto styles = model::metamodel::named_fonts();
    expect(styles.size() == 15, "all fifteen native standard font styles must be registered");
    for (const auto& descriptor : styles) {
        expect(model::metamodel::find_named_font(descriptor.name) == &descriptor &&
               model::metamodel::find_named_font(descriptor.native_id) == &descriptor && !descriptor.russian_name.empty(),
            "standard Font names, numeric identities and localized names must resolve uniquely");
        auto named = style;
        named.style = model::QualifiedName{std::string(descriptor.name)};
        named.height = 12; named.bold = true; named.italic = false;
        named.underline = false; named.strikeout = false;
        const auto current = "{8,2,62,{" + std::to_string(descriptor.native_id) + "},120,700,0,0,0,1,100}";
        expect(codec::encode_font(named) == current, "standard style Font overrides must use the native tuple");
        for (const auto version : {6, 7, 8}) {
            const auto source = "{" + std::to_string(version) + ",2,62,{" + std::to_string(descriptor.native_id) +
                "},120,700,0,0,0,1" + (version == 6 ? "}" : ",100}");
            expect(codec::decode_font(source) == named && codec::encode_font(codec::decode_font(source)) == current,
                "all fifteen standard Font styles and their explicit overrides must survive generations6,7,8");
        }
    }
    expect(!model::metamodel::find_named_font("StyleFonts.Unknown") &&
           !model::metamodel::find_named_font(std::int64_t{-21}) &&
           !model::metamodel::find_named_font(std::numeric_limits<std::int64_t>::min()),
        "unknown or out-of-range style Font identities must not resolve");
    auto unknown_style = style; unknown_style.style = model::QualifiedName{"StyleFonts.Unknown"};
    expect_rejected([&] { static_cast<void>(codec::encode_font(unknown_style)); },
        "unknown standard Font names must remain rejected");


    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,0,1024,0,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,100,0}")); },
        "unknown Font presence flags must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,0,0,0,0,0,0,400,0,0,0,0,0,0,0,0,\"\",1,100,0}")); },
        "absolute Font without a faceName flag must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,0,1,0,0,0,0,400,0,0,0,0,0,0,0,0,\"\",1,100,0}")); },
        "flagged empty absolute Font faceName must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,0,3,2147483648,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,100,0}")); },
        "Font height above signed 32-bit range must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,0,513,0,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,2147483648,0}")); },
        "Font scale above signed 32-bit range must be rejected");
    expect_rejected(
        [] {
            static_cast<void>(codec::decode_font("{8,2,0,{-21},1,100}"));
        },
        "unobserved style fonts must be rejected");
    for (std::uint32_t selector = 0; selector < 5; ++selector) {
        model::FontValue windows;
        windows.kind = model::FontKind::windows_font;
        windows.windows_font = static_cast<model::WindowsFont>(selector);
        const auto expected = "{8,1,0,{" + std::to_string(selector) + "},1,100}";
        expect(codec::encode_font(windows) == expected && codec::decode_font(expected) == windows,
            "platform Windows font must preserve its named selector");
    }
    expect_rejected([] { static_cast<void>(codec::decode_font("{8,1,0,{5},1,100}")); },
        "unknown Windows font selector must fail closed");
    expect_rejected(
        [] {
            model::FontValue unsupported;
            unsupported.kind = model::FontKind::windows_font;
            static_cast<void>(codec::encode_font(unsupported));
        },
        "Windows font without a named selector must be rejected");
    model::FontValue no_face;
    no_face.kind = model::FontKind::absolute;
    expect_rejected([&] { static_cast<void>(codec::encode_font(no_face)); },
        "absolute Font without faceName must not invent storage values");
    model::FontValue fractional_height;
    fractional_height.kind = model::FontKind::absolute;
    fractional_height.face_name = "Arial";
    fractional_height.height = 10.01;
    expect_rejected([&] { static_cast<void>(codec::encode_font(fractional_height)); },
        "unrepresentable Font height must be rejected");
    model::FontValue fractional_scale;
    fractional_scale.kind = model::FontKind::absolute;
    fractional_scale.face_name = "Arial";
    fractional_scale.scale = 100.5;
    expect_rejected([&] { static_cast<void>(codec::encode_font(fractional_scale)); },
        "unrepresentable Font scale must be rejected");
    model::FontValue zero_scale;
    zero_scale.kind = model::FontKind::absolute;
    zero_scale.face_name = "Arial";
    zero_scale.scale = 0;
    expect(codec::encode_font(zero_scale) == "{8,0,1,0,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,0,0}" &&
               codec::decode_font(codec::encode_font(zero_scale)) == zero_scale,
        "observed explicit zero Font scale must remain distinguishable from absence");
    model::FontValue style_override = style;
    style_override.bold = false;
    expect(codec::decode_font(codec::encode_font(style_override)) == style_override,
        "style Font preserves explicit false bold");

    for (const auto& [base, prefix] : std::array{
             std::pair{automatic, std::string{"{8,3,"}},
             std::pair{style, std::string{"{8,2,"}},
             std::pair{[] { model::FontValue f; f.kind = model::FontKind::windows_font; f.windows_font = model::WindowsFont::default_gui; return f; }(), std::string{"{8,1,"}},
             std::pair{[] { model::FontValue f; f.kind = model::FontKind::windows_font; f.windows_font = model::WindowsFont::system; return f; }(), std::string{"{8,1,"}}}) {
        const std::string selector = base.kind == model::FontKind::windows_font ? ",{" + std::to_string(static_cast<unsigned>(*base.windows_font)) + "}" :
            base.kind == model::FontKind::style_reference ? ",{-20}" : "";
        const auto check = [&](const model::FontValue& value, std::string_view suffix) {
            const auto expected = prefix + std::string(suffix.substr(0, suffix.find(','))) + selector + std::string(suffix.substr(suffix.find(',')));
            expect(codec::encode_font(value) == expected && codec::decode_font(expected) == value,
                "nonabsolute Font named presence fields must match platform tuple");
        };
        check(base, "0,1,100}");
        auto height = base; height.height = 0; check(height, "2,0,1,100}");
        height.height = 11; check(height, "2,110,1,100}");
        auto fields = base; fields.height = 0; fields.bold = false; fields.italic = false;
        fields.underline = false; fields.strikeout = false; check(fields, "62,0,400,0,0,0,1,100}");
        fields.face_name = "Arial"; check(fields, "63,0,400,0,0,0,\"Arial\",1,100}");
        fields.height = 11; fields.bold = true; fields.italic = true; fields.underline = true;
        fields.strikeout = true; fields.scale = 120;
        check(fields, "63,110,700,1,1,1,\"Arial\",1,120}");
        fields.scale_override = true;
        check(fields, "575,110,700,1,1,1,\"Arial\",1,120}");
        auto scale = base; scale.scale_override = true; scale.scale = 125; check(scale, "512,1,125}");
        auto direct = base; direct.scale = 125; check(direct, "0,1,125}");
    }
    for (const auto* invalid : {"{8,3,1024,1,100}", "{8,1,0,{0,1},1,100}",
             "{8,2,0,{-20},2,100}", "{8,3,2,2147483648,1,100}",
             "{8,3,8,2,1,100}", "{8,3,0,1,2147483648}", "{8,3,0,1,100,0}"})
        expect_rejected([&] { (void)codec::decode_font(invalid); }, "invalid compact Font record rejected");
}

void test_shortcut() {
    const auto keys = model::metamodel::shortcut_key_descriptors();
    expect(keys.size() == 80, "Shortcut must expose all platform key names");
    for (const auto& key : keys) {
        for (std::uint32_t flags = 0; flags < 8; ++flags) {
            const model::ShortcutValue value{
                std::string(key.name),
                (flags & 1U) != 0,
                (flags & 2U) != 0,
                (flags & 4U) != 0,
            };
            expect(codec::decode_shortcut(codec::encode_shortcut(value)) == value,
                "every named Shortcut key and modifier combination must round-trip");
        }
    }
    expect(codec::encode_shortcut(model::ShortcutValue{"A", true, true, false}) == "{0,65,24}",
        "Shortcut must map named modifiers to the platform flags");
    expect(codec::decode_shortcut("{0,13,28}") == model::ShortcutValue{"Enter", true, true, true},
        "navigation Shortcut key must decode by its named Win32 key value");
    expect_rejected([] { static_cast<void>(codec::decode_shortcut("{1,65,0}")); },
        "unknown Shortcut version must be rejected");
    expect_rejected([] { static_cast<void>(codec::decode_shortcut("{0,65,32}")); },
        "unknown Shortcut modifier flags must be rejected");
    expect_rejected([] { static_cast<void>(codec::decode_shortcut("{0,999,0}")); },
        "unknown Shortcut key code must be rejected");
    expect_rejected([] { static_cast<void>(codec::encode_shortcut(model::ShortcutValue{"NotAKey"})); },
        "unknown named Shortcut key must be rejected");
}

void test_spreadsheet_format_fields() {
    namespace stream = oof::storage::list_stream;
    const model::LineValue border{{"SpreadsheetDocumentCellLineType", "Dotted"}, 2, true};
    const model::ColorValue color{model::ColorKind::absolute, 12, 34, 56, 255, {}};
    const model::LocalizedStringValue text{{{"en", "N=12"}}};
    model::TypeDomainEntry boolean;
    boolean.term = model::TypeDomainTerm::boolean;
    const model::TypeDomainPatternValue type{{boolean}};
    const model::UuidValue uuid{"01234567-89ab-cdef-0123-456789abcdef"};
    const auto font = codec::decode_font(
        "{8,0,959,110,0,0,0,700,0,1,0,0,0,0,0,34,\"Arial\",1,100,0}");
    struct FieldCase { std::string_view name; model::PropertyValue value; std::string_view storage; };
    // Native Format bit order and XDTO units, independent of descriptor codecs.
    const std::array<FieldCase, 48> fields{{
        {"Font", font, "0"}, {"LeftBorder", border, "0"}, {"TopBorder", border, "0"},
        {"RightBorder", border, "0"}, {"BottomBorder", border, "0"}, {"BorderColor", color, "0"},
        {"Height", model::DecimalValue{"96"}, "96"}, {"Width", model::DecimalValue{"144"}, "144"},
        {"HorizontalAlignment", model::EnumerationValue{"HorizontalAlign", "Center"}, "6"},
        {"VerticalAlignment", model::EnumerationValue{"VerticalAlign", "Center"}, "24"},
        {"TextColor", color, "0"}, {"BackColor", color, "0"},
        {"Pattern", model::EnumerationValue{"SpreadsheetDocumentPatternType", "WithoutPattern"}, "255"},
        {"PatternColor", color, "0"},
        {"TextPlacement", model::EnumerationValue{"SpreadsheetDocumentTextPlacementType", "Wrap"}, "3"},
        {"FillType", model::EnumerationValue{"SpreadsheetDocumentAreaFillType", "Template"}, "2"},
        {"Protection", false, "1"}, {"Hidden", true, "1"},
        {"TextOrientation", model::DecimalValue{"150"}, "150"},
        {"DetailsUse", model::EnumerationValue{"SpreadsheetDocumentDetailUse", "WithoutProcessing"}, "2"},
        {"BySelectedColumns", false, "0"}, {"MarkNegatives", true, "1"}, {"ContainsValue", false, "0"},
        {"ValueType", type, "0"}, {"Format", text, "0"}, {"ControlType", uuid, "0"},
        {"HyperLink", true, "1"}, {"QuickDrag", false, "0"}, {"AutoMarkIncomplete", true, "1"},
        {"MarkIncomplete", false, "0"}, {"Indent", model::DecimalValue{"7"}, "7"},
        {"AutoIndent", model::DecimalValue{"3"}, "3"}, {"EditFormat", text, "0"},
        {"ColumnSizeChange", model::EnumerationValue{"SizeChangeMode", "QuickChange"}, "1"},
        {"Mask", text, "0"}, {"Picture", model::PictureRef{}, "1"},
        {"PictureHorizontalAlignment", model::EnumerationValue{"HorizontalAlign", "Right"}, "2"},
        {"PictureVerticalAlignment", model::EnumerationValue{"VerticalAlign", "Bottom"}, "8"},
        {"PictureSize", model::EnumerationValue{"PictureSize", "AutoSizeIgnoreScale"}, "6"},
        {"TextPosition", model::EnumerationValue{"TextPositionRelativeToPicture", "OnTop"}, "4"},
        {"AutoWidthCalculation", false, "0"}, {"WidthWeightFactor", model::DecimalValue{"20"}, "20"},
        {"LeftMargin", model::DecimalValue{"1"}, "1"}, {"TopMargin", model::DecimalValue{"2"}, "2"},
        {"RightMargin", model::DecimalValue{"3"}, "3"}, {"BottomMargin", model::DecimalValue{"4"}, "4"},
        {"HighlightMode", model::EnumerationValue{"SpreadsheetDocumentCellHighlightMode", "DontHighlight"}, "3"},
        {"ShowEditMode", model::EnumerationValue{"SpreadsheetDocumentEditableCellShowType", "AsInputField"}, "1"},
    }};
    const auto descriptors = model::metamodel::spreadsheet_format_properties();
    expect(descriptors.size() == fields.size(), "Format must expose all 48 named properties");
    codec::SpreadsheetFormatResources resources;
    model::SpreadsheetDocumentFormat all;
    for (std::size_t owner = 0; owner < 12; ++owner) {
        model::SpreadsheetDocumentFormat format;
        const auto mask = std::uint64_t{15} << (owner * 4);
        std::string expected = "{" + std::to_string(mask);
        for (std::size_t bit = owner * 4; bit < owner * 4 + 4; ++bit) {
            const auto& field = fields[bit];
            expect(descriptors[bit].xml_name == field.name,
                "Format descriptor order must agree with the native property bit");
            format.properties.set_explicit(model::PropertyId::from_name(field.name), field.value);
            all.properties.set_explicit(model::PropertyId::from_name(field.name), field.value);
            expected += "," + std::string(field.storage);
        }
        expected += "}";
        const auto encoded = codec::encode_spreadsheet_format(format, resources);
        expect(stream::dump_compact(encoded) == expected,
            "each of twelve Format owners must use its four native bits, values, and resource references");
        expect(codec::decode_spreadsheet_format(encoded, resources) == format,
            "each Format owner must preserve typed properties and explicit false values");
    }
    std::string expected = "{281474976710655";
    for (const auto& field : fields) expected += "," + std::string(field.storage);
    expected += "}";
    const auto encoded = codec::encode_spreadsheet_format(all, resources);
    expect(stream::dump_compact(encoded) == expected &&
               codec::decode_spreadsheet_format(encoded, resources) == all,
        "a Format combining all 48 properties must preserve bit order and typed values");
    expect(resources.fonts.size() == 1 && resources.lines.size() == 1 && resources.colors.size() == 1 &&
               resources.strings.size() == 1 && resources.types.size() == 1 && resources.control_types.size() == 1 &&
               resources.masks.size() == 1 && resources.pictures.size() == 1 &&
               resources.pictures.front() == model::PictureRef{},
        "repeated Format owners must intern every typed resource, including explicit empty Picture");
    model::SpreadsheetDocumentFormat empty;
    expect(stream::dump_compact(codec::encode_spreadsheet_format(empty, resources)) == "{0}" &&
               codec::decode_spreadsheet_format(stream::ListValue::list({stream::ListValue::raw_atom("0")}),
                   resources).properties.empty(),
        "an absent Format property must remain distinct from an explicitly empty resource or false Boolean");

    model::SpreadsheetDocumentFormat second;
    auto second_font = font; second_font.height = 12;
    auto second_color = color; second_color.red = 90;
    const model::LocalizedStringValue other_text{{{"en", "Other"}}};
    model::TypeDomainEntry number; number.term = model::TypeDomainTerm::numeric;
    model::PictureRef picture; picture.standard_name = model::QualifiedName{"StdPicture.Save"};
    second.properties.set_explicit(model::PropertyId::from_name("Font"), second_font);
    second.properties.set_explicit(model::PropertyId::from_name("LeftBorder"),
        model::LineValue{{"SpreadsheetDocumentCellLineType", "Solid"}, 1, false});
    second.properties.set_explicit(model::PropertyId::from_name("TextColor"), second_color);
    second.properties.set_explicit(model::PropertyId::from_name("ValueType"), model::TypeDomainPatternValue{{number}});
    second.properties.set_explicit(model::PropertyId::from_name("Format"), other_text);
    second.properties.set_explicit(model::PropertyId::from_name("ControlType"),
        model::UuidValue{"11111111-1111-1111-1111-111111111111"});
    second.properties.set_explicit(model::PropertyId::from_name("Mask"), other_text);
    second.properties.set_explicit(model::PropertyId::from_name("Picture"), picture);
    const auto second_encoded = codec::encode_spreadsheet_format(second, resources);
    expect(second_encoded.items.size() == 9 && second_encoded.items.back().atom == "2",
        "a distinct Picture must use the next one-based reference after an empty Picture");
    for (std::size_t index = 1; index < 8; ++index)
        expect(second_encoded.items[index].atom == "1", "distinct nonpicture resources must use zero-based index one");
    expect(codec::decode_spreadsheet_format(second_encoded, resources) == second &&
               stream::dump_compact(codec::encode_spreadsheet_format(second, resources)) == stream::dump_compact(second_encoded) &&
               resources.fonts.size() == 2 && resources.lines.size() == 2 && resources.colors.size() == 2 &&
               resources.strings.size() == 2 && resources.types.size() == 2 && resources.control_types.size() == 2 &&
               resources.masks.size() == 2 && resources.pictures.size() == 2,
        "distinct resources must resolve correctly and repeated encoding must not append duplicates");
    for (const auto& descriptor : descriptors) {
        if (descriptor.value_codec != model::metamodel::ValueCodec::boolean) continue;
        for (const bool enabled : {false, true}) {
            model::SpreadsheetDocumentFormat format;
            format.properties.set_explicit(descriptor.id, enabled);
            const auto binary = codec::encode_spreadsheet_format(format, resources);
            expect(binary.items[1].atom == ((enabled != (descriptor.xml_name == "Protection")) ? "1" : "0") &&
                       codec::decode_spreadsheet_format(binary, resources) == format,
                "every Format Boolean must retain both explicit values, with Protection inverted");
        }
    }
}

void test_spreadsheet_format_enums() {
    namespace stream = oof::storage::list_stream;
    using Member = std::pair<std::string_view, int>;
    struct EnumCase {
        std::string_view property;
        std::string_view type;
        unsigned bit;
        std::vector<Member> members;
    };
    // Independently captured native enum storage, not public XSD member order.
    const std::array cases{
        EnumCase{"HorizontalAlignment", "HorizontalAlign", 8, {{"Left", 0}, {"Center", 6}, {"Right", 2}, {"Justify", 4}, {"Auto", 5}}},
        EnumCase{"VerticalAlignment", "VerticalAlign", 9, {{"Top", 0}, {"Center", 24}, {"Bottom", 8}}},
        EnumCase{"TextPlacement", "SpreadsheetDocumentTextPlacementType", 14, {{"Auto", 0}, {"Cut", 1}, {"Block", 2}, {"Wrap", 3}}},
        EnumCase{"FillType", "SpreadsheetDocumentAreaFillType", 15, {{"Text", 0}, {"Parameter", 1}, {"Template", 2}}},
        EnumCase{"DetailsUse", "SpreadsheetDocumentDetailUse", 19, {{"Cell", 0}, {"Row", 1}, {"WithoutProcessing", 2}}},
        EnumCase{"ColumnSizeChange", "SizeChangeMode", 33, {{"Normal", 0}, {"QuickChange", 1}}},
        EnumCase{"PictureSize", "PictureSize", 38, {{"RealSize", 0}, {"Stretch", 1}, {"Proportionally", 2}, {"Tile", 3},
            {"AutoSize", 4}, {"RealSizeIgnoreScale", 5}, {"AutoSizeIgnoreScale", 6}, {"ByFontSize", 7}}},
        EnumCase{"TextPosition", "TextPositionRelativeToPicture", 39, {{"Left", 0}, {"Right", 1}, {"Top", 2}, {"Bottom", 3}, {"OnTop", 4}, {"Auto", 5}}},
        EnumCase{"HighlightMode", "SpreadsheetDocumentCellHighlightMode", 46, {{"Auto", 0}, {"Cell", 1}, {"Row", 2}, {"DontHighlight", 3}}},
        EnumCase{"ShowEditMode", "SpreadsheetDocumentEditableCellShowType", 47, {{"Auto", 0}, {"AsInputField", 1}, {"Regular", 2}}},
    };
    codec::SpreadsheetFormatResources resources;
    const auto check = [&](std::string_view property, std::string_view type, unsigned bit,
                           std::string_view member, int storage) {
        model::SpreadsheetDocumentFormat format;
        format.properties.set_explicit(model::PropertyId::from_name(property),
            model::EnumerationValue{std::string(type), std::string(member)});
        const auto encoded = codec::encode_spreadsheet_format(format, resources);
        expect(stream::dump_compact(encoded) == "{" + std::to_string(std::uint64_t{1} << bit) + "," +
                   std::to_string(storage) + "}" && codec::decode_spreadsheet_format(encoded, resources) == format,
            "named Format enum members must match native ordinals in both directions");
    };
    std::size_t members = 0;
    for (const auto& item : cases) {
        for (const auto& [name, storage] : item.members) {
            check(item.property, item.type, item.bit, name, storage);
            ++members;
        }
    }
    check("Pattern", "SpreadsheetDocumentPatternType", 12, "WithoutPattern", 255);
    check("Pattern", "SpreadsheetDocumentPatternType", 12, "Solid", 0);
    for (int pattern = 1; pattern <= 17; ++pattern)
        check("Pattern", "SpreadsheetDocumentPatternType", 12, "Pattern" + std::to_string(pattern), pattern);
    expect(members + 19 == 60, "Format enum cases must cover all 60 independently named members");
}

void test_spreadsheet_format_rejections() {
    namespace stream = oof::storage::list_stream;
    using List = stream::ListValue;
    codec::SpreadsheetFormatResources resources;
    const auto record = [](unsigned bit, std::string value) {
        return List::list({List::raw_atom(std::to_string(std::uint64_t{1} << bit)), List::raw_atom(std::move(value))});
    };
    for (const auto& invalid : {
             List::raw_atom("0"), List::list({}), List::list({List::string_atom("0")}),
             List::list({List::raw_atom("281474976710656"), List::raw_atom("0")}),
             List::list({List::raw_atom("-1")}), List::list({List::raw_atom("64")}),
             List::list({List::raw_atom("0"), List::raw_atom("0")}),
             List::list({List::raw_atom("64"), List::string_atom("1")}),
             List::list({List::raw_atom("64"), List::list({List::raw_atom("1")})})})
        expect_rejected([&] { (void)codec::decode_spreadsheet_format(invalid, resources); },
            "Format must reject invalid list shape, unknown flags, arity and noninteger values");
    for (const unsigned bit : {0U, 1U, 5U, 23U, 24U, 25U, 34U, 35U}) {
        for (const auto reference : {"-1", "0", "1"}) {
            const auto invalid = record(bit, reference);
            expect_rejected([&] { (void)codec::decode_spreadsheet_format(invalid, resources); },
                "Format must reject missing or negative resource references and an unproved null Picture");
        }
    }
    for (const auto& [bit, value] : {
             std::pair{0U, "32768"}, {1U, "256"}, {6U, "32768"}, {6U, "-32769"},
             {7U, "32768"}, {7U, "-32769"}, {18U, "2147483648"}, {42U, "-2147483649"},
             {16U, "2"}, {17U, "-1"}, {8U, "1"}, {9U, "1"}, {12U, "254"}, {38U, "8"}, {39U, "6"}}) {
        const auto invalid = record(bit, value);
        expect_rejected([&] { (void)codec::decode_spreadsheet_format(invalid, resources); },
            "Format must reject storage overflow, nonboolean values and unknown enum ordinals");
    }
    for (const auto& [name, value] : {
             std::pair{"Height", "32768"}, {"Width", "-32769"}, {"Indent", "2147483648"}, {"TopMargin", "0.5"}}) {
        model::SpreadsheetDocumentFormat invalid;
        invalid.properties.set_explicit(model::PropertyId::from_name(name), model::DecimalValue{value});
        expect_rejected([&] { (void)codec::encode_spreadsheet_format(invalid, resources); },
            "Format must reject fractional or overflowing scalar values during encoding");
    }
    for (const auto& [name, value] : std::array<std::pair<std::string_view, model::PropertyValue>, 6>{{
             {"UnknownFormatProperty", false}, {"Height", true},
             {"HorizontalAlignment", model::EnumerationValue{"VerticalAlign", "Center"}},
             {"HorizontalAlignment", model::EnumerationValue{"HorizontalAlign", "Unknown"}},
             {"LeftBorder", model::LineValue{}}, {"ControlType", model::UuidValue{"not-a-uuid"}}}}) {
        model::SpreadsheetDocumentFormat invalid;
        invalid.properties.set_explicit(model::PropertyId::from_name(name), value);
        expect_rejected([&] { (void)codec::encode_spreadsheet_format(invalid, resources); },
            "Format must reject unknown properties, wrong types, line families, enum members and UUIDs");
    }
    resources.lines.resize(257);
    expect_rejected([&] { (void)codec::decode_spreadsheet_format(record(1, "256"), resources); },
        "a present border resource must still fit its uint8 reference");
    expect_rejected([&] { (void)codec::decode_spreadsheet_format(record(1, "0"), resources); },
        "border decoding must reject a resource from another line family");
    model::SpreadsheetDocumentFormat border;
    border.properties.set_explicit(model::PropertyId::from_name("LeftBorder"),
        model::LineValue{{"SpreadsheetDocumentCellLineType", "Solid"}, 1, false});
    expect_rejected([&] { (void)codec::encode_spreadsheet_format(border, resources); },
        "border interning must reject a new resource index exceeding uint8");
    resources.lines[255] = model::LineValue{{"SpreadsheetDocumentCellLineType", "Solid"}, 1, false};
    const auto maximum_border = codec::encode_spreadsheet_format(border, resources);
    expect(stream::dump_compact(maximum_border) == "{2,255}" &&
               codec::decode_spreadsheet_format(maximum_border, resources) == border,
        "border index 255 must remain representable and resolve to its typed line resource");
    for (const auto& [name, bit, minimum, maximum] : {
             std::tuple{"Height", 6U, "-32768", "32767"}, {"Width", 7U, "-32768", "32767"},
             {"TextOrientation", 18U, "-2147483648", "2147483647"}}) {
        for (const auto value : {minimum, maximum}) {
            model::SpreadsheetDocumentFormat format;
            format.properties.set_explicit(model::PropertyId::from_name(name), model::DecimalValue{value});
            const auto encoded = codec::encode_spreadsheet_format(format, resources);
            expect(stream::dump_compact(encoded) == stream::dump_compact(record(bit, value)) &&
                       codec::decode_spreadsheet_format(encoded, resources) == format,
                "Format scalar storage boundaries must remain accepted without rescaling");
        }
    }
}

void test_native_absolute_font_hints() {
    const auto font = codec::decode_font(
        "{8,0,959,110,0,0,0,700,0,1,0,0,0,0,0,34,\"Arial\",1,100,0}");
    model::FontValue expected;
    expected.kind = model::FontKind::absolute;
    expected.face_name = "Arial"; expected.height = 11; expected.bold = true;
    expected.italic = false; expected.underline = true; expected.strikeout = false;
    expected.scale_override = true;
    expect(font == expected && codec::encode_font(font) ==
               "{8,0,575,110,0,0,0,700,0,1,0,0,0,0,0,0,\"Arial\",1,100,0}" &&
               codec::decode_font(codec::encode_font(font)) == expected,
        "native Font959 must preserve named semantics while normalizing zero overrides and pitch/family hints");
    for (const auto& [invalid, field] : {
             std::pair{"{8,0,959,110,0,1,0,700,0,1,0,0,0,0,0,34,\"Arial\",1,100,0}", "escapement"},
             {"{8,0,959,110,0,0,0,700,0,1,0,0,0,0,0,34,\"Arial\",1,100,1}", "lineHeight"},
             {"{8,0,959,110,0,0,0,700,0,1,0,0,0,0,0,256,\"Arial\",1,100,0}", "pitch and family"}}) {
        bool rejected = false;
        try { (void)codec::decode_font(invalid); }
        catch (const std::runtime_error& error) { rejected = std::string_view(error.what()).find(field) != std::string_view::npos; }
        expect(rejected, "Font must identify unsupported escapement/lineHeight or overflowing pitch/family precisely");
    }
}

void test_platform_date_codec() {
    expect(codec::date_to_platform("2024-02-29T00:00:00") == "20240229000000",
        "local leap-day date must encode to the canonical platform atom");
    expect(codec::date_from_platform("20311107234510") == "2031-11-07T23:45:10",
        "platform seconds and time fields must decode without loss");
    for (const std::string_view invalid : {
             "2023-02-29T00:00:00", "2024-13-01T00:00:00", "2024-04-31T00:00:00",
             "2024-01-01T24:00:00", "2024-01-01T00:60:00", "2024-01-01T00:00:60",
             "2024-01-01T00:00:00Z", "2024-01-01T00:00:00.1"}) {
        expect_rejected([invalid] { static_cast<void>(codec::date_to_platform(invalid)); },
            "invalid, zoned, or fractional date must be rejected");
    }
}

void test_compact_platform_decimal() {
    const std::string tiny_37 = "0." + std::string(36, '0') + "1";
    const std::string tiny_320 = "0." + std::string(319, '0') + "1";
    const std::string normalized = "0." + std::string(21, '0') + "9999999999999998";
    const std::string boundary = "0." + std::string(4095, '0') + "1";
    const std::array cases{
        std::pair{std::string("0"), std::string("0")},
        std::pair{std::string("140.5"), std::string("140.5")},
        std::pair{tiny_37, std::string("1e-37")},
        std::pair{normalized, std::string("9.999999999999998e-22")},
        std::pair{tiny_320, std::string("1e-320")},
        std::pair{boundary, std::string("1e-4096")},
    };
    for (const auto& [decimal, expected] : cases) {
        const auto atom = codec::compact_platform_decimal(decimal);
        expect(atom == expected, "compact platform decimal must preserve the expected shorter representation");
        expect(codec::canonical_platform_decimal(atom) == decimal,
            "platform atom must expand to the exact input decimal without binary conversion");
    }
    expect(codec::compact_platform_decimal("  -0.0000000000000000000000000000000000001  ") == "-1e-37",
        "compact conversion must normalize whitespace and preserve a negative sign");
    expect(codec::compact_platform_decimal("+000140.5000") == "140.5",
        "shorter ordinary decimals must remain ordinary after normalization");
    for (const std::string_view invalid : {"", ".", "1e3", "1.2.3", "--1", "NaN"}) {
        bool rejected = false;
        try { (void)codec::compact_platform_decimal(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        expect(rejected, "compact conversion must reject invalid xs:decimal input");
    }
    bool rejected = false;
    try { (void)codec::compact_platform_decimal("0." + std::string(4096, '0') + "1"); }
    catch (const std::invalid_argument&) { rejected = true; }
    expect(rejected, "compact conversion must reject exponents outside the reader limit");
}

void test_canonical_decimal() {
    for (const auto& [input, expected] : std::array{
             std::pair{"2.1e2", "210"}, std::pair{"-1.25E+3", "-1250"},
             std::pair{"1e-3", "0.001"}, std::pair{"0.0001e2", "0.01"},
             std::pair{"123456789012345678901234567890.125e-2", "1234567890123456789012345678.90125"}})
        expect(codec::canonical_platform_decimal(input) == expected, "native exponent must expand without decimal precision loss");
    for (const auto input : {"1e", "1e+", "1e+-2", "1e++2", "1e2e3", "1e999999999", "1e4097", "1e-4097", "nan"}) {
        bool rejected = false;
        try { (void)codec::canonical_platform_decimal(input); } catch (const std::invalid_argument&) { rejected = true; }
        expect(rejected, "invalid or excessive native decimal exponent must fail");
    }

    expect(codec::canonical_decimal("  +00012.37500\n") == "12.375",
        "shared decimal canonicalization must preserve arbitrary precision and normalize lexical form");
    expect(codec::canonical_decimal("-0.000") == "0",
        "negative zero must normalize to zero as before");
    expect(codec::canonical_decimal("1.") == "1",
        "existing decimal normalization must continue to accept a trailing decimal point");
    expect(codec::canonical_decimal("123456789012345678901234567890.125") ==
               "123456789012345678901234567890.125",
        "decimal canonicalization must not pass through binary floating point");
    for (const std::string_view invalid : {"", ".", "1.2.3", "--1", "1e3", "NaN"}) {
        bool rejected = false;
        try {
            static_cast<void>(codec::canonical_decimal(invalid));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        expect(rejected, "invalid xs:decimal lexical values must be rejected");
    }
}

}  // namespace

int main() {
    try {
        test_line();
        test_absolute_line_override_flags();
        test_localized_string();
        test_formatted_string();
        test_composite_id();
        test_type_domain();
        test_style_reference();
        test_color();
        test_font();
        test_shortcut();
        test_spreadsheet_format_fields();
        test_spreadsheet_format_enums();
        test_spreadsheet_format_rejections();
        test_native_absolute_font_hints();
        test_platform_date_codec();
        test_canonical_decimal();
        test_compact_platform_decimal();
    } catch (const std::exception& error) {
        std::cerr << "value codec tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "value codec tests: PASS\n";
    return 0;
}
