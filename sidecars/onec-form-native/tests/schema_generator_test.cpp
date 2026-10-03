#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlschemas.h>

#include "oof/model/metamodel.hpp"
#include "oof/source/schema_generator.hpp"

namespace {

using oof::model::metamodel::ChildPolicy;
using oof::model::metamodel::ControlDescriptor;
using oof::model::metamodel::EventDescriptor;
using oof::model::metamodel::Metamodel;
using oof::model::metamodel::PropertyDescriptor;
using oof::model::metamodel::ValueCodec;
using oof::source::GeneratedSchemas;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct XmlDocumentDeleter {
    void operator()(xmlDocPtr document) const noexcept {
        xmlFreeDoc(document);
    }
};

struct SchemaParserContextDeleter {
    void operator()(xmlSchemaParserCtxtPtr context) const noexcept {
        xmlSchemaFreeParserCtxt(context);
    }
};

struct SchemaDeleter {
    void operator()(xmlSchemaPtr schema) const noexcept {
        xmlSchemaFree(schema);
    }
};

struct SchemaValidationContextDeleter {
    void operator()(xmlSchemaValidCtxtPtr context) const noexcept {
        xmlSchemaFreeValidCtxt(context);
    }
};

using XmlDocument = std::unique_ptr<xmlDoc, XmlDocumentDeleter>;
using SchemaParserContext =
    std::unique_ptr<xmlSchemaParserCtxt, SchemaParserContextDeleter>;
using Schema = std::unique_ptr<xmlSchema, SchemaDeleter>;
using SchemaValidationContext =
    std::unique_ptr<xmlSchemaValidCtxt, SchemaValidationContextDeleter>;

void discard_xml_error(void*, const char*, ...) {}

XmlDocument parse_xml(std::string_view xml, std::string_view name) {
    xmlDocPtr document = xmlReadMemory(
        xml.data(),
        static_cast<int>(xml.size()),
        std::string(name).c_str(),
        nullptr,
        XML_PARSE_NONET | XML_PARSE_NOBLANKS);
    if (document == nullptr) {
        throw std::runtime_error("cannot parse " + std::string(name));
    }
    return XmlDocument(document);
}

Schema compile_schema(std::string_view xml, std::string_view name) {
    XmlDocument document = parse_xml(xml, name);
    SchemaParserContext context(xmlSchemaNewDocParserCtxt(document.get()));
    expect(context != nullptr, "cannot create XSD parser context");
    Schema schema(xmlSchemaParse(context.get()));
    expect(schema != nullptr, "generated XSD must compile");
    return schema;
}

int validate_document(xmlSchemaPtr schema, std::string_view xml) {
    XmlDocument document = parse_xml(xml, "instance.xml");
    SchemaValidationContext context(xmlSchemaNewValidCtxt(schema));
    expect(context != nullptr, "cannot create XSD validation context");
    xmlSchemaSetValidErrors(
        context.get(),
        discard_xml_error,
        discard_xml_error,
        nullptr);
    return xmlSchemaValidateDoc(context.get(), document.get());
}

bool is_element(xmlNodePtr node, std::string_view name) {
    return node != nullptr && node->type == XML_ELEMENT_NODE &&
           std::string_view(reinterpret_cast<const char*>(node->name)) == name;
}

std::string attribute(xmlNodePtr node, std::string_view name) {
    const std::string owned_name(name);
    xmlChar* value = xmlGetProp(node, BAD_CAST owned_name.c_str());
    if (value == nullptr) {
        return {};
    }
    std::string result(reinterpret_cast<const char*>(value));
    xmlFree(value);
    return result;
}

xmlNodePtr direct_child(xmlNodePtr parent, std::string_view name) {
    for (xmlNodePtr node = parent == nullptr ? nullptr : parent->children;
         node != nullptr;
         node = node->next) {
        if (is_element(node, name)) {
            return node;
        }
    }
    return nullptr;
}

std::vector<xmlNodePtr> direct_children(xmlNodePtr parent, std::string_view name) {
    std::vector<xmlNodePtr> result;
    for (xmlNodePtr node = parent == nullptr ? nullptr : parent->children;
         node != nullptr;
         node = node->next) {
        if (is_element(node, name)) {
            result.push_back(node);
        }
    }
    return result;
}

xmlNodePtr direct_child_with_attribute(
    xmlNodePtr parent,
    std::string_view element_name,
    std::string_view attribute_name,
    std::string_view expected_value
) {
    for (xmlNodePtr node : direct_children(parent, element_name)) {
        if (attribute(node, attribute_name) == expected_value) {
            return node;
        }
    }
    return nullptr;
}

xmlNodePtr schema_component(
    xmlNodePtr schema,
    std::string_view component,
    std::string_view name
) {
    return direct_child_with_attribute(schema, component, "name", name);
}

xmlNodePtr sequence_for_type(xmlNodePtr schema, std::string_view type_name) {
    xmlNodePtr type = schema_component(schema, "complexType", type_name);
    expect(type != nullptr, "expected named complex type");
    xmlNodePtr sequence = direct_child(type, "sequence");
    expect(sequence != nullptr, "expected complex type sequence");
    return sequence;
}

void expect_element_shape(
    xmlNodePtr element,
    std::string_view name,
    std::string_view type,
    std::string_view min_occurs,
    std::string_view max_occurs
) {
    expect(element != nullptr, "expected schema element");
    expect(attribute(element, "name") == name, "schema element name drift");
    expect(attribute(element, "type") == type, "schema element type drift");
    expect(attribute(element, "minOccurs") == min_occurs, "schema element minimum drift");
    expect(attribute(element, "maxOccurs") == max_occurs, "schema element maximum drift");
}

void expect_type_attribute(
    xmlNodePtr schema,
    std::string_view type_name,
    std::string_view attribute_name,
    std::string_view attribute_type,
    std::string_view use
) {
    xmlNodePtr type = schema_component(schema, "complexType", type_name);
    expect(type != nullptr, "expected attribute owner type");
    xmlNodePtr schema_attribute =
        direct_child_with_attribute(type, "attribute", "name", attribute_name);
    expect(schema_attribute != nullptr, "expected domain attribute");
    expect(attribute(schema_attribute, "type") == attribute_type, "domain attribute type drift");
    expect(attribute(schema_attribute, "use") == use, "domain attribute use drift");
}

std::vector<std::string> enumeration_values(
    xmlNodePtr schema,
    std::string_view type_name
) {
    xmlNodePtr type = schema_component(schema, "simpleType", type_name);
    expect(type != nullptr, "expected enumeration type");
    xmlNodePtr restriction = direct_child(type, "restriction");
    expect(restriction != nullptr, "expected enumeration restriction");
    std::vector<std::string> values;
    for (xmlNodePtr value : direct_children(restriction, "enumeration")) {
        values.push_back(attribute(value, "value"));
    }
    return values;
}

std::size_t count_named_descendants(
    xmlNodePtr parent,
    std::string_view element_name,
    std::string_view name
) {
    std::size_t count = 0;
    for (xmlNodePtr node = parent == nullptr ? nullptr : parent->children;
         node != nullptr;
         node = node->next) {
        if (is_element(node, element_name) && attribute(node, "name") == name) {
            ++count;
        }
        count += count_named_descendants(node, element_name, name);
    }
    return count;
}

std::string as_utf8(std::u8string_view value) {
    return {
        reinterpret_cast<const char*>(value.data()),
        value.size(),
    };
}

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::filesystem::path find_repository_root() {
    const auto find_from = [](std::filesystem::path path) {
        if (!path.empty() && !std::filesystem::is_directory(path)) {
            path = path.parent_path();
        }
        while (!path.empty()) {
            if (std::filesystem::exists(path / "schemas" / "OrdinaryForm.xsd") &&
                std::filesystem::exists(
                    path / "sidecars" / "onec-form-native" / "tests" /
                    "schema_generator_test.cpp")) {
                return path;
            }
            const auto parent = path.parent_path();
            if (parent == path) {
                break;
            }
            path = parent;
        }
        return std::filesystem::path{};
    };

    if (auto root = find_from(std::filesystem::current_path()); !root.empty()) {
        return root;
    }
    const std::filesystem::path source_path = __FILE__;
    if (auto root = find_from(
            source_path.is_absolute()
                ? source_path
                : std::filesystem::current_path() / source_path);
        !root.empty()) {
        return root;
    }
    throw std::runtime_error("cannot locate repository root");
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read tracked schema");
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

template <typename Descriptor>
void expect_unique_descriptors(
    std::span<const Descriptor> descriptors,
    std::string_view message
) {
    std::set<std::string_view> xml_names;
    std::set<std::string_view> api_names;
    for (const auto& descriptor : descriptors) {
        expect(xml_names.insert(descriptor.xml_name).second, message);
        expect(api_names.insert(descriptor.api_name).second, message);
    }
}

void test_determinism(const Metamodel& metamodel, const GeneratedSchemas& schemas) {
    const GeneratedSchemas repeated = oof::source::generate_schemas(metamodel);
    expect(
        schemas.ordinary_form_xsd == repeated.ordinary_form_xsd,
        "ordinary-form schema generation must be deterministic");
    expect(
        schemas.ordinary_form_palette_xsd == repeated.ordinary_form_palette_xsd,
        "palette schema generation must be deterministic");
    expect(!schemas.ordinary_form_xsd.empty(), "ordinary-form schema must not be empty");
    expect(
        !schemas.ordinary_form_palette_xsd.empty(),
        "palette schema must not be empty");
}

void test_registry_is_duplicate_free(const Metamodel& metamodel) {
    const auto controls = metamodel.controls();
    expect(controls.size() == 26, "registry must contain exactly 26 controls");

    std::set<std::string_view> public_names;
    std::set<std::string_view> api_names;
    std::set<std::string_view> guids;
    std::set<std::u8string_view> russian_names;
    for (const auto& control : controls) {
        expect(
            public_names.insert(control.public_name).second,
            "control public names must be unique");
        expect(
            api_names.insert(control.api_name).second,
            "control API names must be unique");
        expect(guids.insert(control.guid).second, "control GUIDs must be unique");
        expect(
            russian_names.insert(control.russian_name).second,
            "control Russian names must be unique");
        expect_unique_descriptors(
            metamodel.properties_for(control.kind),
            "control properties must be duplicate-free");
        expect_unique_descriptors(
            metamodel.events_for(control.kind),
            "control events must be duplicate-free");
    }
    expect_unique_descriptors(
        metamodel.form_properties(),
        "form properties must be duplicate-free");
    expect_unique_descriptors(
        metamodel.form_events(),
        "form events must be duplicate-free");
    expect_unique_descriptors(
        metamodel.control_extension_properties(),
        "control extension properties must be duplicate-free");
    expect_unique_descriptors(
        metamodel.panel_placement_properties(),
        "panel placement properties must be duplicate-free");
}

void test_schema_version_and_controls(
    const Metamodel& metamodel,
    xmlNodePtr schema
) {
    expect(
        enumeration_values(schema, "TypeDomainTermType") ==
            std::vector<std::string>({
                "unknown", "list", "boolean", "binary", "date", "numeric",
                "reference", "string", "type"}),
        "type-domain term vocabulary drift");

    xmlNodePtr form_element = schema_component(schema, "element", "Form");
    expect(form_element != nullptr, "Form must be the schema root element");
    expect(attribute(form_element, "type") == "FormType", "Form must use FormType");

    xmlNodePtr form_type = schema_component(schema, "complexType", "FormType");
    expect(form_type != nullptr, "FormType must exist");
    xmlNodePtr version =
        direct_child_with_attribute(form_type, "attribute", "name", "ordinaryFormVersion");
    expect(version != nullptr, "ordinaryFormVersion must exist");
    expect(attribute(version, "use") == "required", "2.1 must be required");
    expect(attribute(version, "fixed") == "2.1", "2.1 must be fixed");
    expect_type_attribute(schema, "FormType", "id", "ObjectIdType", "required");
    expect_type_attribute(schema, "FormType", "name", "xs:string", "required");
    expect(
        count_named_descendants(schema, "element", "Module") == 0,
        "external module text must not be embedded in Form XML");
    expect(
        count_named_descendants(schema, "complexType", "ModuleType") == 0,
        "external module type must not be embedded in Form XML");

    std::size_t control_types = 0;
    std::size_t control_elements = 0;
    for (const auto& control : metamodel.controls()) {
        const std::string type_name = std::string(control.public_name) + "Type";
        if (schema_component(schema, "complexType", type_name) != nullptr) {
            ++control_types;
        }
        xmlNodePtr element = schema_component(schema, "element", control.public_name);
        if (element != nullptr) {
            ++control_elements;
            expect(attribute(element, "type") == type_name, "control element type mismatch");
        }
        expect_type_attribute(schema, type_name, "id", "ObjectIdType", "required");
        expect_type_attribute(schema, type_name, "name", "xs:string", "required");
    }
    expect(control_types == 26, "schema must define 26 named control complex types");
    expect(control_elements == 26, "schema must define 26 named control elements");
    expect(
        schema_component(schema, "complexType", "ControlType") == nullptr,
        "generic ControlType is forbidden");
}

void expect_property_element(
    const PropertyDescriptor& descriptor,
    xmlNodePtr element
) {
    expect(attribute(element, "name") == descriptor.xml_name, "property order drift");
    expect(attribute(element, "minOccurs") == "0", "property must be optional");
    expect(attribute(element, "maxOccurs") == "1", "property must occur at most once");

    const std::string type = attribute(element, "type");
    expect(!type.empty(), "property must have an explicit XSD type");
    expect(type != "xs:anyType", "property must not use xs:anyType");
    if (descriptor.value_codec == ValueCodec::unclassified) {
        expect(
            type == "UnclassifiedValueType",
            "unclassified property must use the rejecting type");
    } else {
        expect(
            type != "UnclassifiedValueType",
            "classified property must use its codec type");
        expect(
            descriptor.value_codec == ValueCodec::string || type != "xs:string",
            "only the exact string codec may use xs:string");
    }
}

std::size_t expect_property_sequence(
    std::span<const PropertyDescriptor> properties,
    const std::vector<xmlNodePtr>& elements,
    std::size_t offset = 0
) {
    expect(
        elements.size() >= offset + properties.size(),
        "property sequence is incomplete");
    for (std::size_t index = 0; index < properties.size(); ++index) {
        expect_property_element(properties[index], elements[offset + index]);
    }
    return offset + properties.size();
}

void test_document_package_types(
    const Metamodel& metamodel,
    xmlNodePtr schema
) {
    const auto form_elements =
        direct_children(sequence_for_type(schema, "FormType"), "element");
    std::size_t cursor = expect_property_sequence(
        metamodel.form_properties(),
        form_elements);
    constexpr std::array form_surface_names{
        "Events",
        "Attributes",
        "Commands",
        "PictureAssets",
        "ChildItems",
    };
    constexpr std::array form_surface_types{
        "FormEventsType",
        "AttributesType",
        "CommandsType",
        "PictureAssetsType",
        "PanelChildItemsType",
    };
    for (std::size_t index = 0; index < form_surface_names.size(); ++index) {
        expect_element_shape(
            form_elements[cursor++],
            form_surface_names[index],
            form_surface_types[index],
            "0",
            "1");
    }
    expect(cursor == form_elements.size(), "FormType package sequence drift");

    xmlNodePtr object_id = schema_component(schema, "simpleType", "ObjectIdType");
    xmlNodePtr id_restriction = direct_child(object_id, "restriction");
    expect(attribute(id_restriction, "base") == "xs:unsignedLong", "ObjectId base drift");
    xmlNodePtr minimum = direct_child(id_restriction, "minInclusive");
    expect(attribute(minimum, "value") == "1", "ObjectId must reject zero");

    const auto attribute_elements =
        direct_children(sequence_for_type(schema, "AttributeType"), "element");
    expect(attribute_elements.size() == 3, "AttributeType surface drift");
    expect_element_shape(
        attribute_elements[0], "TypeDomain", "TypeDomainValueType", "1", "1");
    expect_element_shape(attribute_elements[1], "Main", "xs:boolean", "0", "1");
    expect_element_shape(attribute_elements[2], "StoredData", "xs:boolean", "0", "1");
    expect_type_attribute(schema, "AttributeType", "id", "ObjectIdType", "required");
    expect_type_attribute(schema, "AttributeType", "name", "xs:string", "required");
    const auto attributes =
        direct_children(sequence_for_type(schema, "AttributesType"), "element");
    expect(attributes.size() == 1, "Attributes collection drift");
    expect_element_shape(attributes[0], "Attribute", "AttributeType", "0", "unbounded");

    const auto command_elements =
        direct_children(sequence_for_type(schema, "CommandType"), "element");
    expect(command_elements.size() == 3, "CommandType surface drift");
    expect_element_shape(
        command_elements[0], "Title", "LocalizedStringValueType", "0", "1");
    expect_element_shape(command_elements[1], "ChangesData", "xs:boolean", "0", "1");
    expect_element_shape(
        command_elements[2], "Picture", "PictureReferenceValueType", "0", "1");
    expect_type_attribute(schema, "CommandType", "id", "ObjectIdType", "required");
    expect_type_attribute(schema, "CommandType", "name", "xs:string", "required");
    expect_type_attribute(schema, "CommandType", "handler", "xs:string", "required");
    const auto commands =
        direct_children(sequence_for_type(schema, "CommandsType"), "element");
    expect(commands.size() == 1, "Commands collection drift");
    expect_element_shape(commands[0], "Command", "CommandType", "0", "unbounded");

    expect_type_attribute(schema, "PictureAssetType", "id", "ObjectIdType", "required");
    expect_type_attribute(
        schema,
        "PictureAssetType",
        "relativePath",
        "xs:string",
        "required");
    expect_type_attribute(
        schema,
        "PictureAssetType",
        "format",
        "PictureFormatType",
        "required");
    expect(
        enumeration_values(schema, "PictureFormatType") ==
            std::vector<std::string>({"gif", "png", "jpeg", "bmp"}),
        "picture format domain drift");
    const auto assets =
        direct_children(sequence_for_type(schema, "PictureAssetsType"), "element");
    expect(assets.size() == 1, "PictureAssets collection drift");
    expect_element_shape(
        assets[0], "PictureAsset", "PictureAssetType", "0", "unbounded");

    const auto page_elements =
        direct_children(sequence_for_type(schema, "PageType"), "element");
    expect(page_elements.size() == 5, "PageType surface drift");
    expect_element_shape(
        page_elements[0], "Title", "LocalizedStringValueType", "0", "1");
    expect_element_shape(
        page_elements[1], "Visible", "xs:boolean", "0", "1");
    expect_element_shape(
        page_elements[2], "Enabled", "xs:boolean", "0", "1");
    expect_element_shape(
        page_elements[3], "Position", "PositionType", "0", "1");
    expect_element_shape(
        page_elements[4], "ChildItems", "ControlChildItemsType", "0", "1");
    expect_type_attribute(schema, "PageType", "name", "xs:string", "required");
}

void test_data_path_position_and_bindings(
    const Metamodel& metamodel,
    xmlNodePtr schema
) {
    const auto members =
        direct_children(sequence_for_type(schema, "DataPathType"), "element");
    expect(members.size() == 1, "DataPath member sequence drift");
    expect_element_shape(members[0], "Member", "xs:string", "0", "unbounded");
    expect_type_attribute(
        schema,
        "DataPathType",
        "attributeId",
        "ObjectIdType",
        "required");

    expect(
        enumeration_values(schema, "BindingCoordinateType") ==
            std::vector<std::string>({
                "left",
                "top",
                "right",
                "bottom",
                "verticalCenter",
                "horizontalCenter",
            }),
        "anchor coordinate domain drift");
    expect(
        enumeration_values(schema, "BindingDimensionType") ==
            std::vector<std::string>({
                "width",
                "height",
                "minimumWidth",
                "minimumHeight",
                "stretch",
            }),
        "binding dimension domain drift");
    expect_type_attribute(
        schema,
        "AnchorBindingType",
        "coordinate",
        "BindingCoordinateType",
        "required");
    expect_type_attribute(
        schema,
        "AnchorBindingType",
        "targetCoordinate",
        "BindingCoordinateType",
        "required");
    expect_type_attribute(
        schema,
        "AnchorBindingType",
        "targetId",
        "ObjectIdType",
        "optional");
    expect_type_attribute(schema, "AnchorBindingType", "offset", "xs:int", "required");
    expect_type_attribute(schema, "ProportionalBindingType", "targetCoordinate", "BindingCoordinateType", "required");
    expect_type_attribute(schema, "ProportionalBindingType", "targetId", "ObjectIdType", "optional");
    expect_type_attribute(schema, "ProportionalBindingType", "offset", "xs:int", "required");
    const auto proportional_children = direct_children(sequence_for_type(schema, "AnchorBindingType"), "element");
    expect(proportional_children.size() == 1, "AnchorBindingType child sequence drift");
    expect_element_shape(
        proportional_children[0], "ProportionalBinding", "ProportionalBindingType", "0", "1");
    expect_type_attribute(
        schema,
        "DimensionBindingType",
        "dimension",
        "BindingDimensionType",
        "required");
    expect_type_attribute(
        schema,
        "DimensionBindingType",
        "value",
        "xs:int",
        "required");
    const auto bindings =
        direct_children(sequence_for_type(schema, "BindingsType"), "element");
    expect(bindings.size() == 2, "Bindings sequence drift");
    expect_element_shape(
        bindings[0], "AnchorBinding", "AnchorBindingType", "0", "unbounded");
    expect_element_shape(
        bindings[1], "DimensionBinding", "DimensionBindingType", "0", "unbounded");
    expect_type_attribute(schema, "BindingsType", "manualHorizontal", "xs:boolean", "optional");
    expect_type_attribute(schema, "BindingsType", "manualVertical", "xs:boolean", "optional");

    const auto position =
        direct_children(sequence_for_type(schema, "PositionType"), "element");
    std::size_t cursor = expect_property_sequence(
        metamodel.panel_placement_properties(),
        position);
    expect_element_shape(position[cursor++], "Bindings", "BindingsType", "0", "1");
    expect(cursor == position.size(), "PositionType descriptor sequence drift");
}

void test_control_surfaces_and_property_order(
    const Metamodel& metamodel,
    xmlNodePtr schema
) {
    const auto extension_properties = metamodel.control_extension_properties();

    for (const auto& control : metamodel.controls()) {
        const std::string type_name = std::string(control.public_name) + "Type";
        const auto elements = direct_children(sequence_for_type(schema, type_name), "element");
        std::size_t cursor = 0;
        expect_element_shape(elements[cursor++], "DataPath", "DataPathType", "0", "1");

        for (const auto& descriptor : extension_properties) {
            if (descriptor.api_name != "Name" && descriptor.api_name != "Data") {
                expect_property_element(descriptor, elements[cursor++]);
            }
        }
        expect_element_shape(elements[cursor++], "Position", "PositionType", "1", "1");
        cursor = expect_property_sequence(
            metamodel.properties_for(control.kind),
            elements,
            cursor);
        expect_element_shape(
            elements[cursor++],
            "Events",
            std::string(control.public_name) + "EventsType",
            "0",
            "1");
        if (control.child_policy != ChildPolicy::forbidden) {
            const std::string child_type =
                control.child_policy == ChildPolicy::ordered_controls_and_pages
                    ? "PanelChildItemsType"
                    : "ControlChildItemsType";
            expect_element_shape(
                elements[cursor++],
                "ChildItems",
                child_type,
                "0",
                "1");
        }
        expect(cursor == elements.size(), "control canonical surface sequence drift");
        expect(
            std::ranges::none_of(elements, [](xmlNodePtr element) {
                const std::string name = attribute(element, "name");
                return name == "Name" || name == "Data";
            }),
            "reserved Name/Data must not be control property elements");
    }
}

void test_event_surfaces(const Metamodel& metamodel, xmlNodePtr schema) {
    xmlNodePtr handler = schema_component(schema, "complexType", "EventHandlerType");
    xmlNodePtr simple_content = direct_child(handler, "simpleContent");
    xmlNodePtr extension = direct_child(simple_content, "extension");
    expect(attribute(extension, "base") == "xs:string", "event handler content drift");
    xmlNodePtr id = direct_child_with_attribute(extension, "attribute", "name", "id");
    expect(attribute(id, "type") == "ObjectIdType", "event ID type drift");
    expect(attribute(id, "use") == "required", "event ID must be required");

    const auto expect_events = [&](std::string_view type_name, auto events) {
        const auto elements =
            direct_children(sequence_for_type(schema, type_name), "element");
        expect(elements.size() == events.size(), "event descriptor count drift");
        for (std::size_t index = 0; index < events.size(); ++index) {
            expect_element_shape(
                elements[index],
                events[index].xml_name,
                "EventHandlerType",
                "0",
                "1");
        }
    };

    expect_events("FormEventsType", metamodel.form_events());
    for (const auto& control : metamodel.controls()) {
        expect_events(
            std::string(control.public_name) + "EventsType",
            metamodel.events_for(control.kind));
    }
}

std::vector<std::string> child_refs(xmlNodePtr schema, std::string_view type_name) {
    xmlNodePtr type = schema_component(schema, "complexType", type_name);
    expect(type != nullptr, "child-items type must exist");
    xmlNodePtr choice = direct_child(type, "choice");
    expect(choice != nullptr, "child-items type must use a choice");
    std::vector<std::string> result;
    for (xmlNodePtr element : direct_children(choice, "element")) {
        result.push_back(attribute(element, "ref"));
    }
    return result;
}

void test_child_policy(const Metamodel& metamodel, xmlNodePtr schema) {
    std::vector<std::string> expected_controls;
    for (const auto& control : metamodel.controls()) {
        expected_controls.emplace_back(control.public_name);
    }
    expect(
        child_refs(schema, "ControlChildItemsType") == expected_controls,
        "control child choices must follow registry order");

    auto expected_panel_children = expected_controls;
    expected_panel_children.emplace_back("Page");
    expect(
        child_refs(schema, "PanelChildItemsType") == expected_panel_children,
        "Panel must allow Page and registered controls");

    std::size_t containers = 0;
    for (const auto& control : metamodel.controls()) {
        const std::string type_name = std::string(control.public_name) + "Type";
        const auto elements = direct_children(sequence_for_type(schema, type_name), "element");
        xmlNodePtr child_items = nullptr;
        for (xmlNodePtr element : elements) {
            if (attribute(element, "name") == "ChildItems") {
                child_items = element;
            }
        }
        if (control.child_policy == ChildPolicy::forbidden) {
            expect(child_items == nullptr, "leaf control must not expose ChildItems");
            continue;
        }
        ++containers;
        expect(child_items != nullptr, "container control must expose ChildItems");
        const std::string expected_type =
            control.child_policy == ChildPolicy::ordered_controls_and_pages
                ? "PanelChildItemsType"
                : "ControlChildItemsType";
        expect(attribute(child_items, "type") == expected_type, "child policy type drift");
    }
    expect(containers == 2, "only Panel and UsualGroup may contain children");
}

void expect_palette_properties(
    xmlNodePtr properties,
    std::span<const PropertyDescriptor> descriptors
) {
    const auto nodes = direct_children(properties, "Property");
    expect(nodes.size() == descriptors.size(), "palette property count drift");
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto& descriptor = descriptors[index];
        expect(attribute(nodes[index], "name") == descriptor.xml_name, "palette property drift");
        expect(
            attribute(nodes[index], "apiName") == descriptor.api_name,
            "palette property API annotation drift");
        expect(
            attribute(nodes[index], "russianName") == as_utf8(descriptor.russian_name),
            "palette property Russian annotation drift");
        expect(
            attribute(nodes[index], "russianType") == as_utf8(descriptor.platform_type),
            "palette property Russian type drift");
        expect(
            attribute(nodes[index], "valueCodec") ==
                oof::model::metamodel::value_codec_name(descriptor.value_codec),
            "palette property value-codec drift");
    }
}

void expect_palette_events(
    xmlNodePtr events,
    std::span<const EventDescriptor> descriptors
) {
    const auto nodes = direct_children(events, "Event");
    expect(nodes.size() == descriptors.size(), "palette event count drift");
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto& descriptor = descriptors[index];
        expect(attribute(nodes[index], "name") == descriptor.xml_name, "palette event drift");
        expect(
            attribute(nodes[index], "apiName") == descriptor.api_name,
            "palette event API annotation drift");
        expect(
            attribute(nodes[index], "russianName") == as_utf8(descriptor.russian_name),
            "palette event Russian annotation drift");
    }
}

void test_palette(const Metamodel& metamodel, xmlNodePtr schema) {
    xmlNodePtr annotation = direct_child(schema, "annotation");
    xmlNodePtr appinfo = direct_child(annotation, "appinfo");
    xmlNodePtr palette = direct_child(appinfo, "Palette");
    expect(palette != nullptr, "palette appinfo must exist");
    expect(
        attribute(palette, "ordinaryFormVersion") == "2.1",
        "palette version must be 2.1");

    xmlNodePtr form = direct_child(palette, "Form");
    expect_palette_properties(direct_child(form, "Properties"), metamodel.form_properties());
    expect_palette_events(direct_child(form, "Events"), metamodel.form_events());

    xmlNodePtr control_extension = direct_child_with_attribute(
        palette,
        "SharedProperties",
        "surface",
        "controlExtension");
    xmlNodePtr panel_placement = direct_child_with_attribute(
        palette,
        "SharedProperties",
        "surface",
        "panelPlacement");
    expect_palette_properties(
        control_extension,
        metamodel.control_extension_properties());
    expect_palette_properties(
        panel_placement,
        metamodel.panel_placement_properties());

    xmlNodePtr controls = direct_child(palette, "Controls");
    const auto control_nodes = direct_children(controls, "Control");
    expect(control_nodes.size() == 26, "palette must contain 26 controls");
    for (std::size_t index = 0; index < metamodel.controls().size(); ++index) {
        const auto& descriptor = metamodel.controls()[index];
        xmlNodePtr control = control_nodes[index];
        expect(attribute(control, "name") == descriptor.public_name, "palette control drift");
        expect(
            attribute(control, "apiName") == descriptor.api_name,
            "palette control API annotation drift");
        expect(
            attribute(control, "russianName") == as_utf8(descriptor.russian_name),
            "palette control Russian annotation drift");
        std::string_view expected_children;
        switch (descriptor.child_policy) {
            case ChildPolicy::forbidden:
                expected_children = "none";
                break;
            case ChildPolicy::ordered_controls:
                expected_children = "controls";
                break;
            case ChildPolicy::ordered_controls_and_pages:
                expected_children = "controlsAndPages";
                break;
        }
        expect(
            attribute(control, "children") == expected_children,
            "palette control child-policy drift");
        expect_palette_properties(
            direct_child(control, "Properties"),
            metamodel.properties_for(descriptor.kind));
        expect_palette_events(
            direct_child(control, "Events"),
            metamodel.events_for(descriptor.kind));
    }
    xmlNodePtr standard_pictures = direct_child(palette, "StandardPictures");
    const auto standard_picture_nodes = direct_children(standard_pictures, "Picture");
    const auto standard_picture_descriptors = oof::model::metamodel::standard_picture_descriptors();
    expect(standard_picture_nodes.size() == standard_picture_descriptors.size(),
        "palette must expose every canonical standard picture descriptor");
    for (std::size_t index = 0; index < standard_picture_nodes.size(); ++index) {
        expect(attribute(standard_picture_nodes[index], "name") == standard_picture_descriptors[index].runtime_name,
            "standard picture palette order and names must match the descriptor registry");
        expect(attribute(standard_picture_nodes[index], "russianName") ==
                   as_utf8(standard_picture_descriptors[index].russian_name),
            "standard picture Russian annotations must match the descriptor registry");
    }
}

void test_forbidden_vocabulary(const GeneratedSchemas& schemas) {
    const std::string generated =
        lowercase(schemas.ordinary_form_xsd + schemas.ordinary_form_palette_xsd);
    constexpr std::array forbidden{
        "objectmodel",
        "liststream",
        "bracketstream",
        "formbin",
        "logicalstream",
        "rawbracket",
        "platformrecords",
        "raw",
        "profile",
        "slot",
        "platform",
        "list-stream",
        "field kind=",
        "xs:any",
        "xs:anytype",
        "2.0",
    };
    for (std::string_view word : forbidden) {
        expect(generated.find(word) == std::string::npos, "forbidden schema vocabulary");
    }
}

void test_document_instances(xmlSchemaPtr schema) {
    constexpr std::string_view complete_document = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1">
  <Events>
    <OnOpen id="7">OnOpen</OnOpen>
  </Events>
  <Attributes>
    <Attribute id="2" name="Value">
      <TypeDomain/>
      <Main>true</Main>
      <StoredData>false</StoredData>
    </Attribute>
  </Attributes>
  <Commands>
    <Command id="3" name="Run" handler="Run">
      <Title><Item language="en">Run</Item></Title>
      <ChangesData>true</ChangesData>
      <Picture>4</Picture>
    </Command>
  </Commands>
  <PictureAssets>
    <PictureAsset id="4" relativePath="Items/Icon/Picture.gif" format="gif"/>
  </PictureAssets>
  <ChildItems>
    <Button id="5" name="RunButton">
      <DataPath attributeId="2"><Member>Nested</Member></DataPath>
      <AutoContextMenu>true</AutoContextMenu>
      <Position>
        <Bindings>
          <AnchorBinding coordinate="left" targetCoordinate="left" offset="0"/>
          <DimensionBinding dimension="width" value="120"/>
        </Bindings>
      </Position>
      <Caption>Run</Caption>
      <Events><Click id="6">Run</Click></Events>
    </Button>
  </ChildItems>
</Form>
)XML";
    expect(
        validate_document(schema, complete_document) == 0,
        "full current public document package must validate");

    constexpr std::string_view panel_page = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1">
  <ChildItems>
    <Page name="RootPage">
      <Position><Top>2</Top><Height>80</Height><Left>1</Left><Width>120</Width><Bindings><DimensionBinding dimension="width" value="120"/></Bindings></Position>
    </Page>
    <Panel id="2" name="Pages">
      <Position/>
      <ChildItems>
        <Page name="MainPage">
          <Title><Item language="en">Main</Item></Title>
          <Position><Top>4</Top><Height>60</Height><Left>3</Left><Width>90</Width></Position>
        </Page>
      </ChildItems>
    </Panel>
  </ChildItems>
</Form>
)XML";
    expect(
        validate_document(schema, panel_page) == 0,
        "Panel must accept Page with localized Title");

    expect(
        validate_document(
            schema,
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\">"
            "<Events><OnOpen>OnOpen</OnOpen></Events></Form>") != 0,
        "event handler without ObjectId must be rejected");
    expect(
        validate_document(
            schema,
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\">"
            "<ChildItems><Page name=\"RootPage\"/></ChildItems></Form>") == 0,
        "Form root must accept Page");
    expect(
        validate_document(
            schema,
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<Page id=\"2\" name=\"RootPage\"/></ChildItems></Form>") != 0,
        "Page id must be rejected by the public schema");
}

void test_schema_structure_coverage_does_not_imply_codec_coverage(
    const Metamodel& metamodel,
    xmlSchemaPtr schema
) {
    expect(
        validate_document(
            schema,
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"/>") == 0,
        "version 2.1 root must validate");
    expect(
        validate_document(
            schema,
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.0\"/>") != 0,
        "version 2.0 must be rejected");
    expect(
        validate_document(schema, "<Form id=\"1\" name=\"Main\"/>") != 0,
        "ordinaryFormVersion must be required");
    expect(
        validate_document(schema, "<Form name=\"Main\" ordinaryFormVersion=\"2.1\"/>") != 0,
        "Form ObjectId must be required");
    expect(
        validate_document(schema, "<Form id=\"1\" ordinaryFormVersion=\"2.1\"/>") != 0,
        "Form name must be required");
    expect(
        !metamodel.coverage().release_ready &&
            metamodel.coverage().unclassified_value_codecs != 0,
        "schema structure coverage must not imply codec release readiness");

    const auto unclassified = std::ranges::find_if(
        metamodel.form_properties(),
        [](const PropertyDescriptor& descriptor) {
            return descriptor.value_codec == ValueCodec::unclassified;
        });
    expect(
        unclassified != metamodel.form_properties().end(),
        "current form metamodel must exercise the rejecting value type");

    const std::string prefix =
        "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><" +
        std::string(unclassified->xml_name);
    const std::string value_instance =
        prefix + ">value</" + std::string(unclassified->xml_name) + "></Form>";
    const std::string empty_instance = prefix + "/></Form>";
    expect(
        validate_document(schema, value_instance) != 0,
        "unclassified text values must be rejected");
    expect(
        validate_document(schema, empty_instance) != 0,
        "unclassified empty values must be rejected");

    const auto boolean_property = std::ranges::find_if(
        metamodel.form_properties(),
        [](const PropertyDescriptor& descriptor) {
            return descriptor.value_codec == ValueCodec::boolean;
        });
    expect(
        boolean_property != metamodel.form_properties().end(),
        "current form metamodel must contain a Boolean property");
    const std::string valid_instance =
        "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><" +
        std::string(boolean_property->xml_name) + ">true</" +
        std::string(boolean_property->xml_name) + "></Form>";
    expect(
        validate_document(schema, valid_instance) == 0,
        "classified Boolean value must validate");
}

void test_tracked_schema_drift(const GeneratedSchemas& schemas) {
    const std::filesystem::path root = find_repository_root();
    expect(
        read_file(root / "schemas" / "OrdinaryForm.xsd") ==
            schemas.ordinary_form_xsd,
        "schemas/OrdinaryForm.xsd has generator drift");
    expect(
        read_file(root / "schemas" / "OrdinaryFormPalette.xsd") ==
            schemas.ordinary_form_palette_xsd,
        "schemas/OrdinaryFormPalette.xsd has generator drift");
}

}  // namespace

int main() {
    xmlInitParser();
    try {
        const Metamodel& metamodel = Metamodel::instance();
        const GeneratedSchemas schemas = oof::source::generate_schemas(metamodel);

        test_determinism(metamodel, schemas);
        test_registry_is_duplicate_free(metamodel);
        test_forbidden_vocabulary(schemas);

        XmlDocument form_document =
            parse_xml(schemas.ordinary_form_xsd, "OrdinaryForm.xsd");
        XmlDocument palette_document =
            parse_xml(schemas.ordinary_form_palette_xsd, "OrdinaryFormPalette.xsd");
        xmlNodePtr form_schema = xmlDocGetRootElement(form_document.get());
        xmlNodePtr palette_schema = xmlDocGetRootElement(palette_document.get());
        expect(is_element(form_schema, "schema"), "ordinary-form XSD root must be schema");
        expect(is_element(palette_schema, "schema"), "palette XSD root must be schema");

        Schema compiled_form =
            compile_schema(schemas.ordinary_form_xsd, "OrdinaryForm.xsd");
        Schema compiled_palette =
            compile_schema(schemas.ordinary_form_palette_xsd, "OrdinaryFormPalette.xsd");
        expect(compiled_palette != nullptr, "palette XSD must compile");

        test_schema_version_and_controls(metamodel, form_schema);
        test_document_package_types(metamodel, form_schema);
        test_data_path_position_and_bindings(metamodel, form_schema);
        test_control_surfaces_and_property_order(metamodel, form_schema);
        test_event_surfaces(metamodel, form_schema);
        test_child_policy(metamodel, form_schema);
        test_palette(metamodel, palette_schema);
        test_document_instances(compiled_form.get());
        test_schema_structure_coverage_does_not_imply_codec_coverage(
            metamodel,
            compiled_form.get());
        test_tracked_schema_drift(schemas);
    } catch (const std::exception& error) {
        std::cerr << "schema generator tests: FAIL: " << error.what() << '\n';
        xmlCleanupParser();
        return 1;
    }

    xmlCleanupParser();
    std::cout << "schema generator tests: PASS\n";
    return 0;
}
