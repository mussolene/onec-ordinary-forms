#include "oof/source/metadata_xml.hpp"
#include "oof/storage/value_codec.hpp"

#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace {

using oof::MetadataContext;
using oof::MetadataMemberDescriptor;
using oof::MetadataMemberKey;
using oof::MetadataTypeDescriptor;
using oof::model::TypeDomainEntry;
using oof::model::TypeDomainPatternValue;
using oof::model::TypeDomainTerm;
using oof::model::UuidValue;

constexpr std::string_view md = "http://v8.1c.ru/8.3/MDClasses";
constexpr std::string_view xr = "http://v8.1c.ru/8.3/xcf/readable";
constexpr std::string_view core = "http://v8.1c.ru/8.1/data/core";

bool expect(bool condition, std::string_view message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

std::string metadata_document(
    std::string_view object_kind,
    std::string_view object_name,
    std::string_view object_uuid,
    std::string_view generated_name,
    std::string_view type_id,
    std::string_view attributes,
    std::string_view category = "Object") {
    return "<MetaDataObject xmlns=\"" + std::string(md) + "\" xmlns:xr=\"" + std::string(xr) +
        "\" xmlns:v8=\"" + std::string(core) + "\" xmlns:cfg=\"http://v8.1c.ru/8.1/data/enterprise/current-config\" "
        "xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"><" + std::string(object_kind) + " uuid=\"" +
        std::string(object_uuid) + "\"><InternalInfo><xr:GeneratedType name=\"" + std::string(generated_name) +
        "\" category=\"" + std::string(category) + "\"><xr:TypeId>" + std::string(type_id) +
        "</xr:TypeId><xr:ValueId>aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa</xr:ValueId></xr:GeneratedType></InternalInfo>" +
        "<Properties><Name>" + std::string(object_name) + "</Name></Properties>" +
        std::string(attributes) + "</" + std::string(object_kind) + "></MetaDataObject>";
}

std::string attribute_xml(std::string_view name, std::string_view id, std::string_view type) {
    return "<Attribute uuid=\"" + std::string(id) + "\"><Properties><Name>" + std::string(name) +
        "</Name><Type>" + std::string(type) +
        "</Type></Properties></Attribute>";
}

TypeDomainPatternValue object_type(std::string uuid) {
    TypeDomainEntry entry;
    entry.term = TypeDomainTerm::object;
    entry.type_uuid = UuidValue{std::move(uuid)};
    return TypeDomainPatternValue{{entry}};
}

bool test_context_lookup_and_path() {
    const UuidValue root_id{"11111111-1111-4111-8111-111111111111"};
    const UuidValue child_id{"22222222-2222-4222-8222-222222222222"};
    const UuidValue owner_id{"99999999-9999-4999-8999-999999999999"};
    const UuidValue unrelated_owner{"77777777-7777-4777-8777-777777777777"};
    const UuidValue unrelated_type{"66666666-6666-4666-8666-666666666666"};
    std::vector<MetadataTypeDescriptor> types{
        {root_id, owner_id, "CatalogObject.Root"}, {child_id, UuidValue{"88888888-8888-4888-8888-888888888888"}, "CatalogObject.Child"},
        {unrelated_type, unrelated_owner, "CatalogObject.Unrelated"}};
    std::vector<MetadataMemberDescriptor> members{
        {owner_id, "Child", MetadataMemberKey{UuidValue{"33333333-3333-4333-8333-333333333333"}}, object_type(child_id.canonical), std::nullopt},
        {UuidValue{"88888888-8888-4888-8888-888888888888"}, "Value", MetadataMemberKey{UuidValue{"44444444-4444-4444-8444-444444444444"}},
            TypeDomainPatternValue{{TypeDomainEntry{.term = TypeDomainTerm::boolean}}}, std::nullopt},
        {owner_id, "Alias", MetadataMemberKey{std::int64_t{7}}, object_type(child_id.canonical), std::nullopt}};
    auto context_result = MetadataContext::create(std::move(types), std::move(members));
    if (!expect(context_result.ok(), "MetadataContext::create rejected a valid synthetic descriptor set")) return false;
    const auto context = context_result.value();
    const std::array names{std::string{"Child"}, std::string{"Value"}};
    auto path = context.resolve_names(object_type(root_id.canonical), names);
    if (!expect(path.ok(), "cross-type name path did not resolve")) return false;
    if (!expect(path.value().keys.size() == 2 && path.value().members == std::vector<std::string>(names.begin(), names.end()) &&
            path.value().leaf_type && path.value().leaf_type->entries.front().term == TypeDomainTerm::boolean,
            "cross-type path returned the wrong keys or leaf type")) return false;
    const std::array keys{MetadataMemberKey{std::int64_t{7}}};
    const auto by_key = context.resolve_keys(object_type(root_id.canonical), keys);
    if (!expect(by_key.ok() && by_key.value().members == std::vector<std::string>{"Alias"}, "numeric registered key lookup failed")) return false;
    const std::array wrong_root_names{std::string{"Child"}};
    if (!expect(!context.resolve_names(object_type(unrelated_type.canonical), wrong_root_names), "wrong-owner member lookup was accepted")) return false;
    const std::array missing_names{std::string{"Missing"}};
    if (!expect(!context.resolve_names(object_type(root_id.canonical), missing_names), "unknown member lookup was accepted")) return false;
    const auto empty = context.resolve_names(object_type(root_id.canonical), {});
    return expect(empty.ok() && empty.value().leaf_type == object_type(root_id.canonical), "empty path did not preserve its root type");
}

bool test_xml_cross_document_and_shared_owner() {
    const std::string root_attrs = "<ChildObjects>" + attribute_xml("Child", "33333333-3333-4333-8333-333333333333",
        "<v8:Type>cfg:CatalogObject.Child</v8:Type>") + "</ChildObjects>";
    const std::string child_attrs = "<ChildObjects>" +
        attribute_xml("Value", "44444444-4444-4444-8444-444444444444", "<v8:Type>xs:boolean</v8:Type>") +
        attribute_xml("Other", "55555555-5555-4555-8555-555555555555", "<v8:Type>xs:string</v8:Type>") +
        "</ChildObjects>";
    const std::array xml{
        metadata_document("Catalog", "Root", "99999999-9999-4999-8999-999999999999", "CatalogObject.Root",
            "11111111-1111-4111-8111-111111111111", root_attrs),
        metadata_document("Catalog", "Child", "88888888-8888-4888-8888-888888888888", "CatalogObject.Child",
            "22222222-2222-4222-8222-222222222222", child_attrs),
        metadata_document("Register", "Ledger", "77777777-7777-4777-8777-777777777777", "RegisterRecordSet.Ledger",
            "66666666-6666-4666-8666-666666666666",
            "<ChildObjects>" + attribute_xml("Measure", "55555555-5555-4555-8555-555555555555",
                "<v8:Type xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">xs:decimal</v8:Type>") + "</ChildObjects>",
            "RecordSet")};
    auto context = oof::source::parse_metadata_context(xml);
    if (!expect(context.ok(), "metadata XML documents failed to load")) return false;
    const std::array names{std::string{"Child"}, std::string{"Value"}};
    auto path = context.value().resolve_names(object_type("11111111-1111-4111-8111-111111111111"), names);
    if (!expect(path.ok(), "XML cross-document member path did not resolve")) return false;
    const auto* owner_type = context.value().lookup_type(UuidValue{"22222222-2222-4222-8222-222222222222"});
    if (!expect(owner_type && context.value().lookup_member(owner_type->owner_id, "Other") != nullptr,
        "shared generated owner did not expose all of its direct members")) return false;
    return expect(context.value().lookup_member(UuidValue{"77777777-7777-4777-8777-777777777777"}, "Measure") != nullptr,
        "RecordSet direct Attribute was not indexed by its metadata object UUID");
}

bool test_characteristic_type_set_alias() {
    const std::string chart = "<MetaDataObject xmlns=\"" + std::string(md) + "\" xmlns:xr=\"" + std::string(xr) +
        "\" xmlns:v8=\"" + std::string(core) + "\"><ChartOfCharacteristicTypes uuid=\""
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa1\"><InternalInfo><xr:GeneratedType name=\"ChartOfCharacteristicTypesRef.Kinds\" category=\"Ref\">"
        "<xr:TypeId>22222222-2222-4222-8222-222222222222</xr:TypeId></xr:GeneratedType></InternalInfo>"
        "<Properties><Name>TypeLinkKinds</Name><Type><v8:Type xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">xs:string</v8:Type>"
        "<v8:StringQualifiers><v8:Length>10</v8:Length><v8:AllowedLength>Variable</v8:AllowedLength></v8:StringQualifiers></Type></Properties>"
        "<ChildObjects/></ChartOfCharacteristicTypes></MetaDataObject>";
    const auto root = metadata_document("Catalog", "Root", "99999999-9999-4999-8999-999999999999",
        "CatalogObject.Root", "11111111-1111-4111-8111-111111111111",
        "<ChildObjects>" + attribute_xml("Kind", "33333333-3333-4333-8333-333333333333",
            "<v8:TypeSet>cfg:Characteristic.TypeLinkKinds</v8:TypeSet>") + "</ChildObjects>");
    const std::array xml{chart, root};
    auto context = oof::source::parse_metadata_context(xml);
    if (!expect(context.ok(), "Characteristic.TypeLinkKinds metadata alias did not load")) return false;
    const std::array name{std::string{"Kind"}};
    auto path = context.value().resolve_names(object_type("11111111-1111-4111-8111-111111111111"), name);
    return expect(path.ok() && path.value().leaf_type && path.value().leaf_type->entries.size() == 1 &&
        path.value().leaf_type->entries.front().term == TypeDomainTerm::string &&
        path.value().leaf_type->entries.front().string == oof::model::LengthQualifiers{10, true},
        "Characteristic.TypeLinkKinds did not resolve through ChartOfCharacteristicTypes.Properties/Type");
}

bool test_generated_type_categories_use_nominal_identity() {
    constexpr std::array categories{"Object", "Ref", "List", "RecordSet"};
    for (const std::string category : categories) {
        const std::string generated_name = "Catalog" + category + ".Target";
        const std::string target_id = "22222222-2222-4222-8222-222222222222";
        const std::array xml{
            metadata_document("Catalog", "Root", "99999999-9999-4999-8999-999999999999",
                "CatalogObject.Root", "11111111-1111-4111-8111-111111111111",
                "<ChildObjects>" + attribute_xml("Target", "33333333-3333-4333-8333-333333333333",
                    "<v8:Type>cfg:" + generated_name + "</v8:Type>") + "</ChildObjects>"),
            metadata_document("Catalog", "Target", "88888888-8888-4888-8888-888888888888",
                generated_name, target_id, "", category)};
        const auto context = oof::source::parse_metadata_context(xml);
        if (!expect(context.ok(), "generated nominal type metadata must parse")) return false;
        const auto path = context.value().resolve_names(object_type("11111111-1111-4111-8111-111111111111"),
            std::array{std::string{"Target"}});
        const auto stored = oof::storage::value_codec::decode_type_domain(
            "{\"Pattern\",{\"#\"," + target_id + "}}");
        if (!expect(path.ok() && path.value().leaf_type == stored,
            "metadata interface category must not change the nominal type token")) return false;
    }
    return true;
}

bool test_rejections() {
    const std::string root_attrs = "<ChildObjects>" + attribute_xml("A", "33333333-3333-4333-8333-333333333333",
        "<v8:Type>xs:boolean</v8:Type>") + attribute_xml("A", "44444444-4444-4444-8444-444444444444",
        "<v8:Type>xs:string</v8:Type>") + "</ChildObjects>";
    const std::array duplicate_xml{metadata_document("Catalog", "Root", "99999999-9999-4999-8999-999999999999",
        "CatalogObject.Root", "11111111-1111-4111-8111-111111111111", root_attrs)};
    if (!expect(!oof::source::parse_metadata_context(duplicate_xml), "duplicate metadata member names were accepted")) return false;

    const std::string type_a = "<MetaDataObject xmlns=\"" + std::string(md) + "\" xmlns:xr=\"" + std::string(xr) +
        "\" xmlns:v8=\"" + std::string(core) + "\" xmlns:cfg=\"http://v8.1c.ru/8.1/data/enterprise/current-config\">"
        "<DefinedType uuid=\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa1\"><Properties><Name>A</Name><Type><v8:TypeSet>cfg:DefinedType.B</v8:TypeSet></Type></Properties></DefinedType></MetaDataObject>";
    const std::string type_b = "<MetaDataObject xmlns=\"" + std::string(md) + "\" xmlns:xr=\"" + std::string(xr) +
        "\" xmlns:v8=\"" + std::string(core) + "\" xmlns:cfg=\"http://v8.1c.ru/8.1/data/enterprise/current-config\">"
        "<DefinedType uuid=\"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb2\"><Properties><Name>B</Name><Type><v8:TypeSet>cfg:DefinedType.A</v8:TypeSet></Type></Properties></DefinedType></MetaDataObject>";
    const std::string trigger = metadata_document("Catalog", "Root", "99999999-9999-4999-8999-999999999999",
        "CatalogObject.Root", "11111111-1111-4111-8111-111111111111",
        "<ChildObjects>" + attribute_xml("Cycle", "33333333-3333-4333-8333-333333333333", "<v8:TypeSet>cfg:DefinedType.A</v8:TypeSet>") + "</ChildObjects>");
    const std::array cycle_xml{type_a, type_b, trigger};
    if (!expect(!oof::source::parse_metadata_context(cycle_xml), "type-set cycle was accepted")) return false;

    const std::string dtd = "<!DOCTYPE MetaDataObject [<!ENTITY x 'bad'>]>" + metadata_document("Catalog", "Root",
        "99999999-9999-4999-8999-999999999999", "CatalogObject.Root", "11111111-1111-4111-8111-111111111111", "");
    const std::array dtd_xml{dtd};
    if (!expect(!oof::source::parse_metadata_context(dtd_xml), "DTD-bearing metadata XML was accepted")) return false;

    std::vector<std::string> too_many_documents(oof::source::metadata_xml_max_documents + 1);
    if (!expect(!oof::source::parse_metadata_context(too_many_documents), "document-count limit was not enforced")) return false;

    const auto type_set_doc = [](std::string_view name, std::size_t index, std::string body) {
        std::string serial = std::to_string(index);
        serial.insert(serial.begin(), 12 - serial.size(), '0');
        const std::string id = "a0000000-0000-4000-8000-" + serial;
        return "<MetaDataObject xmlns=\"" + std::string(md) + "\" xmlns:v8=\"" + std::string(core) +
            "\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:cfg=\"http://v8.1c.ru/8.1/data/enterprise/current-config\">"
            "<DefinedType uuid=\"" + id + "\"><Properties><Name>" + std::string(name) + "</Name><Type>" +
            std::move(body) + "</Type></Properties></DefinedType></MetaDataObject>";
    };
    std::vector<std::string> branching;
    branching.push_back(type_set_doc("S0", 1, "<v8:Type xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">xs:boolean</v8:Type>"));
    for (std::size_t index = 1; index <= 13; ++index) {
        const std::string previous = "S" + std::to_string(index - 1);
        const std::string ref = "<v8:TypeSet>cfg:DefinedType." + previous + "</v8:TypeSet>";
        branching.push_back(type_set_doc("S" + std::to_string(index), index + 1, ref + ref));
    }
    branching.push_back(metadata_document("Catalog", "Root", "99999999-9999-4999-8999-999999999999",
        "CatalogObject.Root", "11111111-1111-4111-8111-111111111111",
        "<ChildObjects>" + attribute_xml("Branch", "33333333-3333-4333-8333-333333333333",
            "<v8:TypeSet>cfg:DefinedType.S13</v8:TypeSet>") + "</ChildObjects>"));
    if (!expect(!oof::source::parse_metadata_context(branching), "acyclic type-set expansion budget was not enforced")) return false;

    std::vector<std::string> deep_chain;
    for (std::size_t index = 0; index <= 34; ++index) {
        const std::string body = index == 0
            ? "<v8:Type xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">xs:boolean</v8:Type>"
            : "<v8:TypeSet>cfg:DefinedType.D" + std::to_string(index - 1) + "</v8:TypeSet>";
        deep_chain.push_back(type_set_doc("D" + std::to_string(index), index + 50, body));
    }
    deep_chain.push_back(metadata_document("Catalog", "Root", "99999999-9999-4999-8999-999999999999",
        "CatalogObject.Root", "11111111-1111-4111-8111-111111111111",
        "<ChildObjects>" + attribute_xml("Deep", "33333333-3333-4333-8333-333333333333",
            "<v8:TypeSet>cfg:DefinedType.D34</v8:TypeSet>") + "</ChildObjects>"));
    return expect(!oof::source::parse_metadata_context(deep_chain), "type-set depth limit was not enforced");
}

bool test_factory_validation_and_unresolved_type() {
    const UuidValue type_id{"11111111-1111-4111-8111-111111111111"};
    const UuidValue owner_id{"99999999-9999-4999-8999-999999999999"};
    auto duplicate = MetadataContext::create(
        {{type_id, owner_id, "A"}, {type_id, owner_id, "B"}}, {});
    if (!expect(!duplicate, "duplicate type UUID was accepted")) return false;
    auto bad_owner = MetadataContext::create({{type_id, owner_id, "A"}},
        {{UuidValue{"22222222-2222-4222-8222-222222222222"}, "B",
            MetadataMemberKey{UuidValue{"33333333-3333-4333-8333-333333333333"}}, std::nullopt, std::nullopt}});
    if (!expect(!bad_owner, "unknown member owner was accepted")) return false;
    auto unresolved = MetadataContext::create({{type_id, owner_id, "A"}},
        {{owner_id, "Unknown", MetadataMemberKey{UuidValue{"33333333-3333-4333-8333-333333333333"}},
            std::nullopt, oof::model::QualifiedName{"cfg:DefinedType.Missing"}}});
    if (!expect(unresolved.ok(), "explicit unresolved named type was rejected")) return false;
    const std::array names{std::string{"Unknown"}, std::string{"Next"}};
    return expect(!unresolved.value().resolve_names(object_type(type_id.canonical), names),
        "path continued through unresolved named type");
}

std::string configuration_schema(std::string_view declarations) {
    return "<s:schema xmlns:s=\"http://www.w3.org/2001/XMLSchema\" "
        "xmlns:i=\"urn:oof:configuration-types:1\" "
        "targetNamespace=\"http://v8.1c.ru/8.1/data/enterprise/current-config\">" +
        std::string(declarations) + "</s:schema>";
}

std::string configuration_type(std::string_view name, std::string_view uuid) {
    return "<s:complexType name=\"" + std::string(name) + "\"><s:annotation><s:appinfo>"
        "<i:TypeIdentity uuid=\"" + std::string(uuid) +
        "\"/></s:appinfo></s:annotation></s:complexType>";
}

bool test_configuration_type_schema() {
    const UuidValue first{"11111111-1111-4111-8111-111111111111"};
    const UuidValue second{"22222222-2222-4222-8222-222222222222"};
    const auto xml = configuration_schema(configuration_type("CatalogRef.Test", first.canonical) +
        configuration_type("DocumentObject.Test", second.canonical) +
        "<s:import namespace=\"urn:unavailable\" schemaLocation=\"https://invalid.example/never-read.xsd\"/>"
        "<s:simpleType name=\"DefinedType.Name\"><s:restriction base=\"s:string\"/></s:simpleType>");
    auto parsed = oof::source::parse_configuration_type_schema(xml);
    if (!expect(parsed.ok(), "configuration type XSD with namespace aliases failed")) return false;
    auto copy = parsed.value();
    auto moved = std::move(copy);
    if (!expect(moved.declaration_count() == 3 && moved.identity_count() == 2 &&
        moved.declares("CatalogRef.Test") && moved.declares("DefinedType.Name") &&
        !moved.declares("Missing") && moved.name_for(first) == "CatalogRef.Test" &&
        moved.name_for(second) == "DocumentObject.Test" &&
        !moved.name_for(UuidValue{"11111111-1111-4111-8111-111111111112"}),
        "configuration type declaration lookup or copy/move lost ownership")) return false;
    const auto plain = oof::source::parse_configuration_type_schema(configuration_schema(
        "<s:complexType name=\"CatalogRef.Test\"/>"));
    if (!expect(plain && plain.value().declaration_count() == 1 && plain.value().identity_count() == 0 &&
        !plain.value().name_for(first), "declarations-only XSD inferred a UUID")) return false;
    const auto uppercase = oof::source::parse_configuration_type_schema(configuration_schema(
        configuration_type("CatalogRef.Upper", "11111111-1111-4111-8111-1111111111AA")));
    return expect(uppercase && uppercase.value().name_for(UuidValue{"11111111-1111-4111-8111-1111111111aa"}) ==
        "CatalogRef.Upper", "configuration type UUID was not canonicalized");
}

bool test_configuration_type_schema_rejections() {
    const std::string first = configuration_type("CatalogRef.Test", "11111111-1111-4111-8111-111111111111");
    const std::vector<std::string> invalid{
        configuration_schema(first + first),
        configuration_schema(first + configuration_type("CatalogRef.Other", "11111111-1111-4111-8111-111111111111")),
        configuration_schema(configuration_type("CatalogRef.Zero", "00000000-0000-0000-0000-000000000000")),
        configuration_schema(configuration_type("CatalogRef.Bad", "not-a-uuid")),
        "<!DOCTYPE schema [<!ENTITY x 'value'>]>" + configuration_schema(first),
        "<!DOCTYPE schema SYSTEM 'https://invalid.example/never-read.dtd'>" + configuration_schema(first),
        "<s:schema xmlns:s=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:foreign\"/>",
        "<s:schema xmlns:s=\"urn:foreign\" targetNamespace=\"http://v8.1c.ru/8.1/data/enterprise/current-config\"/>",
        configuration_schema("<s:complexType/>"),
        configuration_schema("<s:complexType name=\"C\">&undefined;</s:complexType>"),
        configuration_schema("<s:complexType name=\"bad name\"/>"),
        configuration_schema("<s:simpleType name=\"MissingBody\"/>"),
        configuration_schema("<s:simpleType name=\"TwoBodies\"><s:list itemType=\"s:string\"/><s:union/></s:simpleType>"),
        configuration_schema("<s:complexType name=\"C\"><s:sequence/><s:choice/></s:complexType>"),
        configuration_schema("<s:complexType xmlns:s=\"urn:foreign\" name=\"WrongNamespace\"/>"),
        configuration_schema("<i:TypeIdentity uuid=\"11111111-1111-4111-8111-111111111111\"/>"),
        configuration_schema("<s:complexType name=\"C\"><s:annotation><s:appinfo>"
            "<TypeIdentity xmlns=\"urn:foreign\" uuid=\"11111111-1111-4111-8111-111111111111\"/>"
            "</s:appinfo></s:annotation></s:complexType>"),
        configuration_schema("<s:complexType name=\"C\"><s:annotation><s:appinfo>"
            "<i:TypeIdentity uuid=\"11111111-1111-4111-8111-111111111111\" extra=\"bad\"/>"
            "</s:appinfo></s:annotation></s:complexType>"),
        configuration_schema("<s:complexType name=\"C\"><s:annotation><s:appinfo>"
            "<i:TypeIdentity i:uuid=\"11111111-1111-4111-8111-111111111111\"/>"
            "</s:appinfo></s:annotation></s:complexType>"),
        configuration_schema("<s:complexType name=\"C\"><s:annotation><s:appinfo>"
            "<i:TypeIdentity uuid=\"11111111-1111-4111-8111-111111111111\"/>"
            "<i:TypeIdentity uuid=\"22222222-2222-4222-8222-222222222222\"/>"
            "</s:appinfo></s:annotation></s:complexType>")};
    for (std::size_t index = 0; index < invalid.size(); ++index)
        if (!expect(!oof::source::parse_configuration_type_schema(invalid[index]),
            "configuration type XSD rejection case " + std::to_string(index) + " was accepted")) return false;
    if (!expect(!oof::source::parse_configuration_type_schema(std::string(
        oof::source::metadata_xml_max_document_bytes + 1, ' ')), "configuration XSD byte limit was not enforced")) return false;
    std::string declarations;
    for (std::size_t index = 0; index <= oof::source::ConfigurationTypeSchema::max_declarations; ++index)
        declarations += "<s:complexType name=\"C" + std::to_string(index) + "\"/>";
    if (!expect(!oof::source::parse_configuration_type_schema(configuration_schema(declarations)),
        "configuration XSD declaration limit was not enforced")) return false;
    std::string deep;
    for (std::size_t index = 0; index < 130; ++index) deep += "<s:annotation>";
    for (std::size_t index = 0; index < 130; ++index) deep += "</s:annotation>";
    if (!expect(!oof::source::parse_configuration_type_schema(configuration_schema(deep)),
        "configuration XSD depth limit was not enforced")) return false;
    // Compact comments keep this below the byte cap while exercising the shared node cap.
    std::string many_nodes;
    many_nodes.reserve(8000010);
    for (std::size_t index = 0; index < 1000001; ++index) many_nodes += "<!--x-->";
    return expect(!oof::source::parse_configuration_type_schema(configuration_schema(many_nodes)),
        "configuration XSD node limit was not enforced");
}

}  // namespace

int main() {
    return test_configuration_type_schema() && test_configuration_type_schema_rejections() &&
        test_context_lookup_and_path() && test_xml_cross_document_and_shared_owner() &&
        test_generated_type_categories_use_nominal_identity() &&
        test_characteristic_type_set_alias() &&
        test_rejections() && test_factory_validation_and_unresolved_type() ? 0 : 1;
}
