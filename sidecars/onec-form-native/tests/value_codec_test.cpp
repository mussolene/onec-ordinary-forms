#include <exception>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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
    for (const auto token : {"T", "R", "L"}) {
        const auto other_platform_term = codec::decode_type_domain(
            std::string("{\"Pattern\",{\"") + token +
            "\",4772b3b4-f4a3-49c0-a1a5-8cb5961511a3}}");
        expect(other_platform_term.entries.front().term != model::TypeDomainTerm::value_list,
            "ValueList recognition must be limited to the proven # type token");
    }

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
        .entries.front().term == model::TypeDomainTerm::unknown,
        "unrelated unknown type UUID must not normalize to ValueTable");
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
    expect_rejected(
        [] {
            model::FontValue unsupported;
            unsupported.kind = model::FontKind::windows_font;
            static_cast<void>(codec::encode_font(unsupported));
        },
        "unobserved WindowsFont must be rejected");
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
    expect_rejected([&] { static_cast<void>(codec::encode_font(style_override)); },
        "unobserved style font overrides must be rejected");
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

}  // namespace

int main() {
    try {
        test_localized_string();
        test_formatted_string();
        test_composite_id();
        test_type_domain();
        test_style_reference();
        test_color();
        test_font();
        test_shortcut();
        test_platform_date_codec();
    } catch (const std::exception& error) {
        std::cerr << "value codec tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "value codec tests: PASS\n";
    return 0;
}
