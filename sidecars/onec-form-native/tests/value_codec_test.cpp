#include <array>
#include <exception>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <tuple>
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
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,3,8,0,1,100}")); },
        "automatic Font non-bold presence flags must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,3,4,500,1,100}")); },
        "automatic Font unobserved bold weights must be rejected");
    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{8,3,4,700,1,100},0")); },
        "Font must reject trailing values outside its own record");
    model::FontValue automatic_italic = automatic;
    automatic_italic.italic = false;
    expect_rejected([&] { static_cast<void>(codec::encode_font(automatic_italic)); },
        "automatic Font must reject unobserved italic overrides");

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
        test_localized_string();
        test_formatted_string();
        test_composite_id();
        test_type_domain();
        test_style_reference();
        test_color();
        test_font();
        test_shortcut();
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
