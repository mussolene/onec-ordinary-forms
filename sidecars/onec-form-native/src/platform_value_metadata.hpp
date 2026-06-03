#pragma once

#include <array>
#include <string_view>

namespace oof::platform::value {

struct PlatformSymbol {
    std::string_view name;
    std::string_view provider;
    std::string_view address;
    std::string_view evidence;
};

constexpr std::array<PlatformSymbol, 38> localized_value_symbols{{
    {"core::ListInStream::ListInStream(IReader*)", "core85.so", "0x56fd70", "container 8.5.1.1343"},
    {"core::ListInStream::ListInStream(IFile*)", "core85.so", "0x56fe90", "container 8.5.1.1343"},
    {"core::ListInStream::ListInStream(IListInStream*)", "core85.so", "0x56fff0", "container 8.5.1.1343"},
    {"core::ListOutStream::ListOutStream(IWriter*)", "core85.so", "0x5702f0", "container 8.5.1.1343"},
    {"core::ListOutStream::ListOutStream(IFile*)", "core85.so", "0x570410", "container 8.5.1.1343"},
    {"core::ListOutStream::ListOutStream(IListOutStream*)", "core85.so", "0x570570", "container 8.5.1.1343"},
    {"core::ListOutStream::close", "core85.so", "0x5705d0", "container 8.5.1.1343"},
    {"core::CompositeID::serialize", "core85.so", "0x4f3780", "container 8.5.1.1343"},
    {"core::CompositeID::deserialize", "core85.so", "0x4f37d0", "container 8.5.1.1343"},
    {"core::kNullCompositeID", "core85.so", "0xb0a618", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::serialize", "core85.so", "0x6d5ac0", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::deserialize", "core85.so", "0x6d5ec0", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::addType", "core85.so", "0x6d50f0", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::setNumericQualifiers", "core85.so", "0x6d5320", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::setStringQualifiers", "core85.so", "0x6d5410", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::setDateQualifiers", "core85.so", "0x6d5500", "container 8.5.1.1343"},
    {"core::TypeDomainPattern::setBinaryQualifiers", "core85.so", "0x6d55e0", "container 8.5.1.1343"},
    {"core::GenericValue::serialize", "core85.so", "0x6d1fb0", "container 8.5.1.1343"},
    {"core::GenericValue::deserialize", "core85.so", "0x6d23d0", "container 8.5.1.1343"},
    {"core::GenericValue::fromString", "core85.so", "0x6d2f70", "container 8.5.1.1343"},
    {"core::LocalWString::serialize", "core85.so", "0x57c520", "container 8.5.1.1343"},
    {"core::LocalWString::deserialize", "core85.so", "0x57c610", "container 8.5.1.1343"},
    {"core::LocalWString::addItem", "core85.so", "0x579d40", "container 8.5.1.1343"},
    {"core::FormattedString::serialize", "core85.so", "0x5347b0", "container 8.5.1.1343"},
    {"core::FormattedString::deserialize", "core85.so", "0x534800", "container 8.5.1.1343"},
    {"core::create_local_str_val", "core85.so", "0x579ae0", "container 8.5.1.1343"},
    {"core::load_wstring", "core85.so", "0x61f000", "container 8.5.1.1343"},
    {"core::Thread::getResourceLocale", "core85.so", "0x8d6f60", "container 8.5.1.1343"},
    {"core::Color::serialize", "core85.so", "0x6bcf10", "container 8.5.1.1343"},
    {"core::Color::deserialize", "core85.so", "0x6bcf70", "container 8.5.1.1343"},
    {"core::Font::serialize", "core85.so", "0x6bda70", "container 8.5.1.1343"},
    {"core::Font::deserialize", "core85.so", "0x6bdd00", "container 8.5.1.1343"},
    {"core::V8Border::serialize", "core85.so", "0x6be670", "container 8.5.1.1343"},
    {"core::V8Border::deserialize", "core85.so", "0x6be700", "container 8.5.1.1343"},
    {"core::V8Picture::to_storage", "core85.so", "0x5df650", "container 8.5.1.1343"},
    {"core::V8Picture::from_storage", "core85.so", "0x5df850", "container 8.5.1.1343"},
    {"core::ShortCut::to_stream", "core85.so", "0x4c2790", "container 8.5.1.1343"},
    {"core::ShortCut::from_stream", "core85.so", "0x4c27e0", "container 8.5.1.1343"},
}};

struct ValueSurfaceEntry {
    std::string_view type_name;
    std::string_view platform_symbol;
    std::string_view native_role;
};

constexpr std::array<ValueSurfaceEntry, 15> value_surface{{
    {"CompositeID", "core::CompositeID", "typed metadata identity"},
    {"TypeDomainPattern", "core::TypeDomainPattern", "type-domain descriptor"},
    {"GenericValue", "core::GenericValue", "ValueFromStringInternal/ValueToStringInternal scalar"},
    {"LocalWString", "core::LocalWString", "localized string payload"},
    {"FormattedString", "core::FormattedString", "localized string with formatting flag"},
    {"Color", "core::Color", "UI color value"},
    {"Font", "core::Font", "UI font value"},
    {"V8Border", "core::V8Border", "UI border value"},
    {"V8Picture", "core::V8Picture", "picture value"},
    {"ShortCut", "core::ShortCut", "keyboard shortcut value"},
    {"Date", "core::Date", "date scalar value"},
    {"Numeric", "core::Numeric", "numeric scalar value"},
    {"PersistenceStorage", "core::IInPersistenceStorage/core::IOutPersistenceStorage", "nested persisted object"},
    {"ListInStream", "core::ListInStream", "platform list reader"},
    {"ListOutStream", "core::ListOutStream", "platform list writer"},
}};

struct SchemaValueSurfaceEntry {
    std::string_view type_name;
    std::string_view schema_source;
    std::string_view platform_evidence;
    std::string_view native_role;
    std::string_view layout_status;
};

constexpr std::array<SchemaValueSurfaceEntry, 10> schema_value_surface{{
    {"AbstractRef", "xdto_root.res:data_ui.xsd AbstractRef", "CompositeID/QName union", "schema-level reference wrapper", "schema-backed"},
    {"StyleRef", "xdto_root.res:data_ui.xsd StyleRef", "CompositeID/QName union", "style item reference", "schema-backed"},
    {"PictureRef", "xdto_root.res:data_ui.xsd PictureRef", "CompositeID/QName union", "picture reference", "schema-backed"},
    {"Color", "xdto_root.res:data_ui.xsd Color", "core82 Color::serialize/deserialize list arity 3; 8.5 schema adds CustomColor", "AbsoluteColor/AutoColor/ref UI color", "value-layer schema backed; ordinary object-slot semantics pending"},
    {"Font", "xdto_root.res:data_ui.xsd Font", "core82 Font::serialize/deserialize list arity 6; 8.5 schema adds scale/lineHeight", "absolute/windows/style/auto font", "value-layer schema backed; ordinary object-slot semantics pending"},
    {"V8Border", "xdto_root.res:data_ui.xsd Border", "core82 V8Border::serialize/deserialize list arity 3", "control border with style/ref/color", "trace-backed list skeleton; real payload correlation pending"},
    {"V8Picture", "xdto_root.res:data_ui.xsd Picture/PictureRef", "core82/core85 V8Picture to_storage/from_storage entrypoints; Picture is base64/url/ref XSD value", "picture ref/storage object", "value storage boundary known; ordinary object-slot semantics pending"},
    {"GenericValue", "core value serializer", "core85 GenericValue serialize/fromString", "typed scalar envelope", "diagnostic stream envelope"},
    {"ShortCut", "core value serializer", "core85/core82 ShortCut to_stream/from_stream", "keyboard shortcut value", "diagnostic stream envelope"},
    {"PersistenceStorage", "core persistence interfaces", "IInPersistenceStorage/IOutPersistenceStorage", "nested persisted object bytes", "opaque payload boundary"},
}};

}  // namespace oof::platform::value
