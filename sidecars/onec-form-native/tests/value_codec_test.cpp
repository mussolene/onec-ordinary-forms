#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "oof/storage/value_codec.hpp"

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
    font.mask = 15;
    font.face_name = "Arial";
    font.height = 10.0;
    font.bold = true;
    expect(
        codec::encode_font(font) == "{6,0,15,{\"none\"},\"Arial\",10,{1,0,0,0}}",
        "absolute font must match the proven platform fixture");
    expect(codec::decode_font(codec::encode_font(font)) == font, "font must round-trip");

    model::FontValue style;
    style.kind = model::FontKind::style_reference;
    style.style = model::QualifiedName{"ui:TextFont"};
    expect(codec::decode_font(codec::encode_font(style)) == style, "style font must round-trip");

    expect_rejected(
        [] { static_cast<void>(codec::decode_font("{5,3,0,{\"none\"},\"\",0,{0,0,0,0}}")); },
        "unknown font field count must be rejected");
    expect_rejected(
        [] {
            static_cast<void>(
                codec::decode_font("{6,2,0,{\"none\"},\"\",0,{0,0,0,0}}"));
        },
        "style font without a reference must be rejected");
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
    } catch (const std::exception& error) {
        std::cerr << "value codec tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "value codec tests: PASS\n";
    return 0;
}
