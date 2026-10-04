#include "oof/source/schema_generator.hpp"

#include <array>
#include <cstddef>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "oof/source/form_xml.hpp"

namespace oof::source {
namespace {

using model::control_kind_count;
using model::ControlKind;
using model::metamodel::ChildPolicy;
using model::metamodel::ControlDescriptor;
using model::metamodel::DescriptorOwner;
using model::metamodel::EventDescriptor;
using model::metamodel::Metamodel;
using model::metamodel::PropertyDescriptor;
using model::metamodel::PropertySurface;
using model::metamodel::ValueCodec;

void append_xml_escaped(std::string& output, std::string_view value) {
    for (const char character : value) {
        switch (character) {
            case '&':
                output += "&amp;";
                break;
            case '<':
                output += "&lt;";
                break;
            case '>':
                output += "&gt;";
                break;
            case '\"':
                output += "&quot;";
                break;
            case '\'':
                output += "&apos;";
                break;
            default:
                output.push_back(character);
                break;
        }
    }
}

void append_attribute(
    std::string& output,
    std::string_view name,
    std::string_view value
) {
    output.push_back(' ');
    output.append(name);
    output += "=\"";
    append_xml_escaped(output, value);
    output.push_back('\"');
}

std::string as_utf8(std::u8string_view value) {
    return {
        reinterpret_cast<const char*>(value.data()),
        value.size(),
    };
}

std::string_view xsd_type(ValueCodec codec) {
    switch (codec) {
        case ValueCodec::command_bar_buttons:
            return "CommandBarButtonsType";
        case ValueCodec::dendrogram_items:
            return "DendrogramItemsType";
        case ValueCodec::dendrogram_links:
            return "DendrogramLinksType";
        case ValueCodec::unclassified:
            return "UnclassifiedValueType";
        case ValueCodec::boolean:
            return "xs:boolean";
        case ValueCodec::integer:
            return "xs:long";
        case ValueCodec::integer32:
            return "xs:int";
        case ValueCodec::decimal:
            return "xs:decimal";
        case ValueCodec::string:
            return "xs:string";
        case ValueCodec::localized_string:
            return "LocalizedStringValueType";
        case ValueCodec::formatted_string:
            return "FormattedStringValueType";
        case ValueCodec::shortcut:
            return "ShortcutValueType";
        case ValueCodec::date:
            return "DateValueType";
        case ValueCodec::uuid:
            return "UuidValueType";
        case ValueCodec::composite_id:
            return "CompositeIdValueType";
        case ValueCodec::type_domain:
            return "TypeDomainValueType";
        case ValueCodec::enumeration:
            return "EnumerationValueType";
        case ValueCodec::color:
            return "ColorValueType";
        case ValueCodec::font:
            return "FontValueType";
        case ValueCodec::picture:
            return "PictureReferenceValueType";
        case ValueCodec::control_reference:
            return "ControlReferenceValueType";
        case ValueCodec::attribute_reference:
            return "AttributeReferenceValueType";
        case ValueCodec::command_reference:
            return "CommandReferenceValueType";
    }
    throw std::logic_error("unknown ordinary-form value codec");
}

std::string_view xsd_type(const PropertyDescriptor& property) {
    if (property.control_kind == model::ControlKind::calendar_field &&
        property.api_name == "BeginOfDisplayPeriod") {
        return "CalendarBeginDateValueType";
    }
    return xsd_type(property.value_codec);
}

std::string_view child_policy_name(ChildPolicy policy) {
    switch (policy) {
        case ChildPolicy::forbidden:
            return "none";
        case ChildPolicy::ordered_controls:
            return "controls";
        case ChildPolicy::ordered_controls_and_pages:
            return "controlsAndPages";
    }
    throw std::logic_error("unknown ordinary-form child policy");
}

bool is_reserved_control_extension(const PropertyDescriptor& descriptor) noexcept {
    return descriptor.api_name == "Name" || descriptor.api_name == "Data";
}

template <typename Descriptor>
void validate_ordered_descriptors(
    std::span<const Descriptor> descriptors,
    std::string_view owner
) {
    std::set<std::string_view> xml_names;
    std::set<std::string_view> api_names;
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto& descriptor = descriptors[index];
        if (descriptor.xml_name.empty() || descriptor.api_name.empty() ||
            descriptor.russian_name.empty()) {
            throw std::logic_error(
                "incomplete ordinary-form descriptor for " + std::string(owner));
        }
        if (index != 0 && descriptors[index - 1].order >= descriptor.order) {
            throw std::logic_error(
                "non-deterministic ordinary-form descriptor order for " +
                std::string(owner));
        }
        if (!xml_names.insert(descriptor.xml_name).second ||
            !api_names.insert(descriptor.api_name).second) {
            throw std::logic_error(
                "duplicate ordinary-form descriptor for " + std::string(owner));
        }
    }
}

void validate_property_group(
    std::span<const PropertyDescriptor> descriptors,
    std::string_view owner,
    DescriptorOwner descriptor_owner,
    PropertySurface surface
) {
    validate_ordered_descriptors(descriptors, owner);
    for (const auto& descriptor : descriptors) {
        if (descriptor.owner != descriptor_owner || descriptor.surface != surface) {
            throw std::logic_error(
                "ordinary-form property surface mismatch for " + std::string(owner));
        }
        static_cast<void>(xsd_type(descriptor.value_codec));
    }
}

void validate_metamodel(const Metamodel& metamodel) {
    const auto controls = metamodel.controls();
    if (controls.size() != 26 || controls.size() != control_kind_count) {
        throw std::logic_error("ordinary-form schema requires exactly 26 controls");
    }

    std::array<bool, control_kind_count> kinds{};
    std::set<std::string_view> guids;
    std::set<std::string_view> public_names;
    std::set<std::string_view> api_names;
    std::set<std::u8string_view> russian_names;
    for (const auto& control : controls) {
        const auto index = static_cast<std::size_t>(control.kind);
        if (index >= kinds.size() || kinds[index]) {
            throw std::logic_error("duplicate ordinary-form control kind");
        }
        kinds[index] = true;
        if (control.guid.empty() || control.public_name.empty() || control.api_name.empty() ||
            control.russian_name.empty() || !guids.insert(control.guid).second ||
            !public_names.insert(control.public_name).second ||
            !api_names.insert(control.api_name).second ||
            !russian_names.insert(control.russian_name).second) {
            throw std::logic_error("duplicate or incomplete ordinary-form control");
        }
        static_cast<void>(child_policy_name(control.child_policy));

        const auto properties = metamodel.properties_for(control.kind);
        validate_property_group(
            properties,
            control.public_name,
            DescriptorOwner::control,
            PropertySurface::control_payload);
        for (const auto& property : properties) {
            if (property.control_kind != control.kind) {
                throw std::logic_error("ordinary-form property owner mismatch");
            }
        }

        const auto events = metamodel.events_for(control.kind);
        validate_ordered_descriptors(events, control.public_name);
        for (const auto& event : events) {
            if (event.owner != DescriptorOwner::control ||
                event.control_kind != control.kind) {
                throw std::logic_error("ordinary-form event owner mismatch");
            }
        }
    }

    validate_property_group(
        metamodel.form_properties(),
        "Form",
        DescriptorOwner::form,
        PropertySurface::form);
    validate_ordered_descriptors(metamodel.form_events(), "Form");
    for (const auto& event : metamodel.form_events()) {
        if (event.owner != DescriptorOwner::form) {
            throw std::logic_error("ordinary-form form-event owner mismatch");
        }
    }
    validate_property_group(
        metamodel.control_extension_properties(),
        "ControlExtension",
        DescriptorOwner::control,
        PropertySurface::control_extension);
    bool has_name = false;
    bool has_data = false;
    for (const auto& descriptor : metamodel.control_extension_properties()) {
        has_name = has_name || descriptor.api_name == "Name";
        has_data = has_data || descriptor.api_name == "Data";
    }
    if (!has_name || !has_data) {
        throw std::logic_error("ordinary-form reserved control extensions are incomplete");
    }
    validate_property_group(
        metamodel.panel_placement_properties(),
        "PanelPlacement",
        DescriptorOwner::control,
        PropertySurface::panel_placement);
}

void append_value_types(std::string& output) {
    output += R"XSD(
  <xs:simpleType name="UnclassifiedValueType">
    <xs:restriction base="xs:string">
      <xs:whiteSpace value="collapse"/>
      <xs:length value="0"/>
      <xs:pattern value="\s"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:simpleType name="ObjectIdType">
    <xs:restriction base="xs:unsignedLong">
      <xs:minInclusive value="1"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:simpleType name="UuidValueType">
    <xs:restriction base="xs:string">
      <xs:pattern value="[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:simpleType name="UndefinedValueType">
    <xs:restriction base="xs:string">
      <xs:enumeration value="undefined"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:simpleType name="LocalDateValueType">
    <xs:restriction base="xs:dateTime">
      <xs:pattern value="[0-9]{4}-[0-9]{2}-[0-9]{2}T([01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:simpleType name="DateValueType">
    <xs:union memberTypes="LocalDateValueType UndefinedValueType"/>
  </xs:simpleType>

  <xs:simpleType name="CalendarBeginLocalDateValueType">
    <xs:restriction base="LocalDateValueType">
      <xs:minExclusive value="0001-01-01T00:00:00"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:simpleType name="CalendarBeginDateValueType">
    <xs:union memberTypes="CalendarBeginLocalDateValueType UndefinedValueType"/>
  </xs:simpleType>

  <xs:simpleType name="NonEmptyTokenType">
    <xs:restriction base="xs:token">
      <xs:minLength value="1"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:complexType name="LocalizedStringItemType">
    <xs:simpleContent>
      <xs:extension base="xs:string">
        <xs:attribute name="language" type="xs:language" use="required"/>
      </xs:extension>
    </xs:simpleContent>
  </xs:complexType>

  <xs:complexType name="LocalizedStringValueType">
    <xs:sequence>
      <xs:element name="Item" type="LocalizedStringItemType" minOccurs="0" maxOccurs="unbounded"/>
    </xs:sequence>
  </xs:complexType>

  <xs:complexType name="FormattedStringValueType">
    <xs:sequence>
      <xs:element name="Value" type="LocalizedStringValueType" minOccurs="1" maxOccurs="1"/>
    </xs:sequence>
    <xs:attribute name="formatted" type="xs:boolean" use="required"/>
  </xs:complexType>

  <xs:complexType name="CompositeIdValueType">
    <xs:attribute name="objectId" type="xs:long" use="required"/>
    <xs:attribute name="uuid" type="UuidValueType" use="required"/>
    <xs:attribute name="isNull" type="xs:boolean" use="required"/>
  </xs:complexType>

  <xs:simpleType name="TypeDomainTermType">
    <xs:restriction base="xs:string">
      <xs:enumeration value="unknown"/>
      <xs:enumeration value="list"/>
      <xs:enumeration value="boolean"/>
      <xs:enumeration value="binary"/>
      <xs:enumeration value="date"/>
      <xs:enumeration value="numeric"/>
      <xs:enumeration value="reference"/>
      <xs:enumeration value="string"/>
      <xs:enumeration value="type"/>
      <xs:enumeration value="valueList"/>
      <xs:enumeration value="valueTable"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:complexType name="TypeDomainEntryType">
    <xs:attribute name="term" type="TypeDomainTermType" use="required"/>
    <xs:attribute name="typeUuid" type="UuidValueType" use="optional"/>
    <xs:attribute name="length" type="xs:nonNegativeInteger" use="optional"/>
    <xs:attribute name="precision" type="xs:nonNegativeInteger" use="optional"/>
    <xs:attribute name="nonNegative" type="xs:boolean" use="optional"/>
    <xs:attribute name="variable" type="xs:boolean" use="optional"/>
    <xs:attribute name="date" type="xs:boolean" use="optional"/>
    <xs:attribute name="time" type="xs:boolean" use="optional"/>
  </xs:complexType>

  <xs:complexType name="TypeDomainValueType">
    <xs:sequence>
      <xs:element name="Entry" type="TypeDomainEntryType" minOccurs="0" maxOccurs="unbounded"/>
    </xs:sequence>
  </xs:complexType>

  <xs:complexType name="EnumerationValueType">
    <xs:attribute name="type" type="NonEmptyTokenType" use="required"/>
    <xs:attribute name="member" type="NonEmptyTokenType" use="required"/>
  </xs:complexType>

  <xs:simpleType name="ColorKindType">
    <xs:restriction base="xs:string">
      <xs:enumeration value="absolute"/>
      <xs:enumeration value="automatic"/>
      <xs:enumeration value="styleReference"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:complexType name="ColorValueType">
    <xs:attribute name="kind" type="ColorKindType" use="required"/>
    <xs:attribute name="red" type="xs:unsignedByte" use="optional"/>
    <xs:attribute name="green" type="xs:unsignedByte" use="optional"/>
    <xs:attribute name="blue" type="xs:unsignedByte" use="optional"/>
    <xs:attribute name="alpha" type="xs:unsignedByte" use="optional"/>
    <xs:attribute name="styleName" type="NonEmptyTokenType" use="optional"/>
    <xs:attribute name="styleObjectId" type="xs:long" use="optional"/>
    <xs:attribute name="styleUuid" type="UuidValueType" use="optional"/>
  </xs:complexType>

  <xs:simpleType name="FontKindType">
    <xs:restriction base="xs:string">
      <xs:enumeration value="absolute"/>
      <xs:enumeration value="windowsFont"/>
      <xs:enumeration value="styleReference"/>
      <xs:enumeration value="automatic"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:complexType name="FontValueType">
    <xs:attribute name="kind" type="FontKindType" use="required"/>
    <xs:attribute name="faceName" type="xs:string" use="optional"/>
    <xs:attribute name="height" type="xs:double" use="optional"/>
    <xs:attribute name="bold" type="xs:boolean" use="optional"/>
    <xs:attribute name="italic" type="xs:boolean" use="optional"/>
    <xs:attribute name="underline" type="xs:boolean" use="optional"/>
    <xs:attribute name="strikeout" type="xs:boolean" use="optional"/>
    <xs:attribute name="scale" type="xs:double" use="optional"/>
    <xs:attribute name="scaleOverride" type="xs:boolean" use="optional"/>
    <xs:attribute name="styleName" type="NonEmptyTokenType" use="optional"/>
    <xs:attribute name="styleObjectId" type="xs:long" use="optional"/>
    <xs:attribute name="styleUuid" type="UuidValueType" use="optional"/>
  </xs:complexType>

  <xs:simpleType name="ControlReferenceValueType">
    <xs:restriction base="ObjectIdType"/>
  </xs:simpleType>

  <xs:simpleType name="AttributeReferenceValueType">
    <xs:restriction base="ObjectIdType"/>
  </xs:simpleType>

  <xs:simpleType name="CommandReferenceValueType">
    <xs:restriction base="ObjectIdType"/>
  </xs:simpleType>

  <xs:complexType name="EventHandlerType">
    <xs:simpleContent>
      <xs:extension base="xs:string">
        <xs:attribute name="id" type="ObjectIdType" use="required"/>
      </xs:extension>
    </xs:simpleContent>
  </xs:complexType>
)XSD";

    output += "\n  <xs:simpleType name=\"ShortcutKeyType\">\n    <xs:restriction base=\"xs:string\">\n";
    for (const auto& key : model::metamodel::shortcut_key_descriptors()) {
        output += "      <xs:enumeration value=\"";
        append_xml_escaped(output, key.name);
        output += "\"/>\n";
    }
    output +=
        "    </xs:restriction>\n  </xs:simpleType>\n"
        "  <xs:complexType name=\"ShortcutValueType\">\n"
        "    <xs:sequence><xs:element name=\"Key\" type=\"ShortcutKeyType\" minOccurs=\"1\" maxOccurs=\"1\"/></xs:sequence>\n"
        "    <xs:attribute name=\"Alt\" type=\"xs:boolean\" use=\"required\"/>\n"
        "    <xs:attribute name=\"Ctrl\" type=\"xs:boolean\" use=\"required\"/>\n"
        "    <xs:attribute name=\"Shift\" type=\"xs:boolean\" use=\"required\"/>\n"
        "  </xs:complexType>\n";

    const auto pictures = model::metamodel::standard_picture_descriptors();
    output += "\n  <xs:simpleType name=\"StandardPictureNameType\">\n    <xs:restriction base=\"xs:string\">\n";
    for (const auto& picture : pictures) {
        output += "      <xs:enumeration value=\"";
        append_xml_escaped(output, picture.runtime_name);
        output += "\">\n        <xs:annotation><xs:appinfo><StandardPicture";
        append_attribute(output, "russianName", as_utf8(picture.russian_name));
        if (!picture.guid.empty()) append_attribute(output, "guid", picture.guid);
        if (picture.storage_id < 0) append_attribute(output, "storageId", std::to_string(picture.storage_id));
        output += "/></xs:appinfo></xs:annotation>\n      </xs:enumeration>\n";
    }
    output +=
        "    </xs:restriction>\n  </xs:simpleType>\n"
        "  <xs:simpleType name=\"PictureReferenceContentType\"><xs:union memberTypes=\"ObjectIdType\"><xs:simpleType><xs:restriction base=\"xs:string\"><xs:enumeration value=\"\"/></xs:restriction></xs:simpleType></xs:union></xs:simpleType>\n"
        "  <xs:complexType name=\"PictureReferenceValueType\"><xs:simpleContent><xs:extension base=\"PictureReferenceContentType\"><xs:attribute name=\"standardName\" type=\"StandardPictureNameType\" use=\"optional\"/></xs:extension></xs:simpleContent></xs:complexType>\n";
}

void append_document_types(std::string& output) {
    output += R"XSD(
  <xs:complexType name="DataPathType">
    <xs:sequence>
      <xs:element name="Member" type="xs:string" minOccurs="0" maxOccurs="unbounded"/>
    </xs:sequence>
    <xs:attribute name="attributeId" type="ObjectIdType" use="required"/>
  </xs:complexType>

  <xs:simpleType name="BindingCoordinateType">
    <xs:restriction base="xs:string">
      <xs:enumeration value="left"/>
      <xs:enumeration value="top"/>
      <xs:enumeration value="right"/>
      <xs:enumeration value="bottom"/>
      <xs:enumeration value="verticalCenter"/>
      <xs:enumeration value="horizontalCenter"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:simpleType name="BindingDimensionType">
    <xs:restriction base="xs:string">
      <xs:enumeration value="width"/>
      <xs:enumeration value="height"/>
      <xs:enumeration value="minimumWidth"/>
      <xs:enumeration value="minimumHeight"/>
      <xs:enumeration value="stretch"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:complexType name="AnchorBindingType">
    <xs:sequence>
      <xs:element name="ProportionalBinding" type="ProportionalBindingType" minOccurs="0" maxOccurs="1"/>
    </xs:sequence>
    <xs:attribute name="coordinate" type="BindingCoordinateType" use="required"/>
    <xs:attribute name="targetCoordinate" type="BindingCoordinateType" use="required"/>
    <xs:attribute name="targetId" type="ObjectIdType" use="optional"/>
    <xs:attribute name="offset" type="xs:int" use="required"/>
  </xs:complexType>

  <xs:complexType name="ProportionalBindingType">
    <xs:attribute name="targetCoordinate" type="BindingCoordinateType" use="required"/>
    <xs:attribute name="targetId" type="ObjectIdType" use="optional"/>
    <xs:attribute name="offset" type="xs:int" use="required"/>
  </xs:complexType>

  <xs:complexType name="DimensionBindingType">
    <xs:attribute name="dimension" type="BindingDimensionType" use="required"/>
    <xs:attribute name="value" type="xs:int" use="required"/>
  </xs:complexType>

  <xs:complexType name="BindingsType">
    <xs:sequence>
      <xs:element name="AnchorBinding" type="AnchorBindingType" minOccurs="0" maxOccurs="unbounded"/>
      <xs:element name="DimensionBinding" type="DimensionBindingType" minOccurs="0" maxOccurs="unbounded"/>
    </xs:sequence>
    <xs:attribute name="manualHorizontal" type="xs:boolean" use="optional"/>
    <xs:attribute name="manualVertical" type="xs:boolean" use="optional"/>
  </xs:complexType>

  <xs:complexType name="AttributeType">
    <xs:sequence>
      <xs:element name="TypeDomain" type="TypeDomainValueType" minOccurs="1" maxOccurs="1"/>
      <xs:element name="Main" type="xs:boolean" minOccurs="0" maxOccurs="1"/>
      <xs:element name="StoredData" type="xs:boolean" minOccurs="0" maxOccurs="1"/>
    </xs:sequence>
    <xs:attribute name="id" type="ObjectIdType" use="required"/>
    <xs:attribute name="name" type="xs:string" use="required"/>
  </xs:complexType>

  <xs:complexType name="AttributesType">
    <xs:sequence>
      <xs:element name="Attribute" type="AttributeType" minOccurs="0" maxOccurs="unbounded"/>
    </xs:sequence>
  </xs:complexType>

  <xs:complexType name="CommandType">
    <xs:sequence>
      <xs:element name="Title" type="LocalizedStringValueType" minOccurs="0" maxOccurs="1"/>
      <xs:element name="ChangesData" type="xs:boolean" minOccurs="0" maxOccurs="1"/>
      <xs:element name="Picture" type="PictureReferenceValueType" minOccurs="0" maxOccurs="1"/>
    </xs:sequence>
    <xs:attribute name="id" type="ObjectIdType" use="required"/>
    <xs:attribute name="name" type="xs:string" use="required"/>
    <xs:attribute name="handler" type="xs:string" use="required"/>
  </xs:complexType>

  <xs:complexType name="CommandsType">
    <xs:sequence>
      <xs:element name="Command" type="CommandType" minOccurs="0" maxOccurs="unbounded"/>
    </xs:sequence>
  </xs:complexType>

  <xs:simpleType name="PictureFormatType">
    <xs:restriction base="xs:string">
      <xs:enumeration value="gif"/>
      <xs:enumeration value="png"/>
      <xs:enumeration value="jpeg"/>
      <xs:enumeration value="bmp"/>
    </xs:restriction>
  </xs:simpleType>

  <xs:complexType name="PictureAssetType">
    <xs:attribute name="id" type="ObjectIdType" use="required"/>
    <xs:attribute name="relativePath" type="xs:string" use="required"/>
    <xs:attribute name="format" type="PictureFormatType" use="required"/>
    <xs:attribute name="transparent" type="xs:boolean" use="optional" default="false"/>
  </xs:complexType>

  <xs:complexType name="PictureAssetsType">
    <xs:sequence>
      <xs:element name="PictureAsset" type="PictureAssetType" minOccurs="0" maxOccurs="unbounded"/>
    </xs:sequence>
  </xs:complexType>
)XSD";
}

void append_property_element(
    std::string& output,
    const PropertyDescriptor& property,
    std::string_view indent
) {
    output.append(indent);
    output += "<xs:element";
    append_attribute(output, "name", property.xml_name);
    append_attribute(output, "type", xsd_type(property));
    output += " minOccurs=\"0\" maxOccurs=\"1\"/>\n";
}

void append_property_elements(
    std::string& output,
    std::span<const PropertyDescriptor> properties,
    std::string_view indent = "      ",
    bool include_runtime_only = true
) {
    for (const auto& property : properties) {
        if (!include_runtime_only &&
            property.persistence == model::metamodel::PersistenceClass::runtime_only) {
            continue;
        }
        append_property_element(output, property, indent);
    }
}

void append_control_extension_elements(
    std::string& output,
    std::span<const PropertyDescriptor> properties
) {
    for (const auto& property : properties) {
        if (!is_reserved_control_extension(property)) {
            append_property_element(output, property, "      ");
        }
    }
}

void append_position_type(
    std::string& output,
    std::span<const PropertyDescriptor> properties
) {
    output += "  <xs:complexType name=\"PositionType\">\n    <xs:sequence>\n";
    append_property_elements(output, properties);
    output +=
        "      <xs:element name=\"Bindings\" type=\"BindingsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "    </xs:sequence>\n"
        "  </xs:complexType>\n\n";
}

void append_event_type(
    std::string& output,
    std::string_view type_name,
    std::span<const EventDescriptor> events
) {
    output += "  <xs:complexType";
    append_attribute(output, "name", type_name);
    output += ">\n    <xs:sequence>\n";
    for (const auto& event : events) {
        output += "      <xs:element";
        append_attribute(output, "name", event.xml_name);
        output +=
            " type=\"EventHandlerType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n";
    }
    output += "    </xs:sequence>\n  </xs:complexType>\n\n";
}

void append_child_item_type(
    std::string& output,
    std::string_view type_name,
    std::span<const ControlDescriptor> controls,
    bool include_page
) {
    output += "  <xs:complexType";
    append_attribute(output, "name", type_name);
    output += ">\n    <xs:choice minOccurs=\"0\" maxOccurs=\"unbounded\">\n";
    for (const auto& control : controls) {
        output += "      <xs:element";
        append_attribute(output, "ref", control.public_name);
        output += "/>\n";
    }
    if (include_page) {
        output += "      <xs:element ref=\"Page\"/>\n";
    }
    output += "    </xs:choice>\n  </xs:complexType>\n\n";
}

std::string generate_ordinary_form_xsd(const Metamodel& metamodel) {
    std::string output;
    output.reserve(64 * 1024);
    output +=
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "
        "elementFormDefault=\"unqualified\" attributeFormDefault=\"unqualified\">\n"
        "  <xs:annotation>\n"
        "    <xs:documentation>Generated from the executable ordinary-form metamodel.</xs:documentation>\n"
        "  </xs:annotation>\n";

    append_value_types(output);
    append_document_types(output);
    append_position_type(output, metamodel.panel_placement_properties());

    output += R"XSD(  <xs:simpleType name="CommandBarButtonKindType"><xs:restriction base="xs:string"><xs:enumeration value="Action"/><xs:enumeration value="Submenu"/><xs:enumeration value="Separator"/></xs:restriction></xs:simpleType>
  <xs:simpleType name="ButtonRepresentationType"><xs:restriction base="xs:string"><xs:enumeration value="Auto"/><xs:enumeration value="Picture"/><xs:enumeration value="Text"/><xs:enumeration value="PictureText"/></xs:restriction></xs:simpleType>
  <xs:simpleType name="CommandBarButtonOrderType"><xs:restriction base="xs:string"><xs:enumeration value="DontOrder"/><xs:enumeration value="Ascending"/><xs:enumeration value="Descending"/></xs:restriction></xs:simpleType>
  <xs:complexType name="CommandBarButtonsType"><xs:sequence><xs:element ref="CommandBarButton" minOccurs="0" maxOccurs="unbounded"/></xs:sequence></xs:complexType>
  <xs:complexType name="CommandBarButtonType"><xs:sequence>
    <xs:element name="Text" type="xs:string" minOccurs="0"/><xs:element name="Explanation" type="xs:string" minOccurs="0"/><xs:element name="ToolTip" type="xs:string" minOccurs="0"/>
    <xs:element name="Enabled" type="xs:boolean" minOccurs="0"/><xs:element name="Checked" type="xs:boolean" minOccurs="0"/><xs:element name="ChangesData" type="xs:boolean" minOccurs="0"/>
    <xs:element name="Representation" type="ButtonRepresentationType" minOccurs="0"/><xs:element name="Shortcut" type="ShortcutValueType" minOccurs="0"/><xs:element name="Picture" type="PictureReferenceValueType" minOccurs="0"/><xs:element name="Action" type="xs:string" minOccurs="0"/><xs:element name="Order" type="CommandBarButtonOrderType" minOccurs="0"/><xs:element name="Buttons" type="CommandBarButtonsType" minOccurs="0"/>
  </xs:sequence><xs:attribute name="name" type="xs:string" use="required"/><xs:attribute name="type" type="CommandBarButtonKindType" use="required"/></xs:complexType>
  <xs:element name="CommandBarButton" type="CommandBarButtonType"/>

  <xs:complexType name="DendrogramItemsType"><xs:sequence><xs:element name="Item" type="DendrogramItemType" minOccurs="0" maxOccurs="unbounded"/></xs:sequence></xs:complexType>
  <xs:complexType name="DendrogramItemType"><xs:sequence><xs:element name="Value" type="xs:string"/><xs:element name="Text" type="LocalizedStringValueType"/></xs:sequence></xs:complexType>
  <xs:complexType name="DendrogramLinksType"><xs:sequence><xs:element name="Link" type="DendrogramLinkType" minOccurs="0" maxOccurs="unbounded"/></xs:sequence></xs:complexType>
  <xs:complexType name="DendrogramLinkType"><xs:sequence><xs:element name="FirstItem" type="xs:string"/><xs:element name="SecondItem" type="xs:string"/><xs:element name="Title" type="LocalizedStringValueType"/><xs:element name="Distance" type="xs:decimal" minOccurs="0"/></xs:sequence></xs:complexType>

)XSD";

    output += R"XSD(  <xs:simpleType name="SpreadsheetCoordinateType"><xs:restriction base="xs:positiveInteger"><xs:maxInclusive value="4294967295"/></xs:restriction></xs:simpleType>
  <xs:complexType name="SpreadsheetDocumentCellType"><xs:sequence><xs:element name="Text" type="xs:string" minOccurs="1" maxOccurs="1"/></xs:sequence><xs:attribute name="row" type="SpreadsheetCoordinateType" use="required"/><xs:attribute name="column" type="SpreadsheetCoordinateType" use="required"/></xs:complexType>
  <xs:complexType name="SpreadsheetDocumentType"><xs:sequence><xs:element name="Cell" type="SpreadsheetDocumentCellType" minOccurs="0" maxOccurs="unbounded"/></xs:sequence></xs:complexType>

)XSD";

    const auto controls = metamodel.controls();
    append_child_item_type(output, "ControlChildItemsType", controls, false);
    append_child_item_type(output, "PanelChildItemsType", controls, true);

    output +=
        "  <xs:complexType name=\"PageType\">\n"
        "    <xs:sequence>\n"
        "      <xs:element name=\"Title\" type=\"LocalizedStringValueType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Visible\" type=\"xs:boolean\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Enabled\" type=\"xs:boolean\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Position\" type=\"PositionType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"ChildItems\" type=\"ControlChildItemsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "    </xs:sequence>\n"
        "    <xs:attribute name=\"name\" type=\"xs:string\" use=\"required\"/>\n"
        "  </xs:complexType>\n"
        "  <xs:element name=\"Page\" type=\"PageType\"/>\n\n";

    append_event_type(output, "FormEventsType", metamodel.form_events());
    for (const auto& control : controls) {
        append_event_type(
            output,
            std::string(control.public_name) + "EventsType",
            metamodel.events_for(control.kind));
    }

    output += "  <xs:complexType name=\"FormType\">\n    <xs:sequence>\n";
    append_property_elements(output, metamodel.form_properties());
    output +=
        "      <xs:element name=\"Events\" type=\"FormEventsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Attributes\" type=\"AttributesType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Commands\" type=\"CommandsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"PictureAssets\" type=\"PictureAssetsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"ChildItems\" type=\"PanelChildItemsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "    </xs:sequence>\n"
        "    <xs:attribute name=\"id\" type=\"ObjectIdType\" use=\"required\"/>\n"
        "    <xs:attribute name=\"name\" type=\"xs:string\" use=\"required\"/>\n"
        "    <xs:attribute name=\"ordinaryFormVersion\" type=\"xs:string\" use=\"required\" fixed=\"";
    append_xml_escaped(output, ordinary_form_xml_version);
    output +=
        "\"/>\n"
        "  </xs:complexType>\n"
        "  <xs:element name=\"Form\" type=\"FormType\"/>\n\n";

    output +=
        "  <xs:simpleType name=\"TableColumnEditorKindType\">\n"
        "    <xs:restriction base=\"xs:string\">\n"
        "      <xs:enumeration value=\"InputField\"/>\n"
        "      <xs:enumeration value=\"ChoiceField\"/>\n"
        "      <xs:enumeration value=\"CheckBox\"/>\n"
        "    </xs:restriction>\n"
        "  </xs:simpleType>\n"
        "  <xs:complexType name=\"TableColumnControlType\">\n"
        "    <xs:sequence>\n"
        "      <xs:element name=\"Enabled\" type=\"xs:boolean\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"ReadOnly\" type=\"xs:boolean\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Caption\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"ToolTip\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Font\" type=\"FontValueType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n"
        "    </xs:sequence>\n"
        "    <xs:attribute name=\"type\" type=\"TableColumnEditorKindType\" use=\"required\"/>\n"
        "  </xs:complexType>\n"
        "  <xs:complexType name=\"TableColumnType\">\n"
        "    <xs:sequence>\n"
        "      <xs:element name=\"DataPath\" type=\"xs:string\" minOccurs=\"1\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Header\" type=\"LocalizedStringValueType\" minOccurs=\"1\" maxOccurs=\"1\"/>\n"
        "      <xs:element name=\"Control\" type=\"TableColumnControlType\" minOccurs=\"1\" maxOccurs=\"1\"/>\n"
        "    </xs:sequence>\n"
        "    <xs:attribute name=\"name\" type=\"xs:string\" use=\"required\"/>\n"
        "  </xs:complexType>\n"
        "  <xs:complexType name=\"TableColumnsType\">\n"
        "    <xs:sequence>\n"
        "      <xs:element name=\"Column\" type=\"TableColumnType\" minOccurs=\"1\" maxOccurs=\"unbounded\"/>\n"
        "    </xs:sequence>\n"
        "  </xs:complexType>\n\n";

    for (const auto& control : controls) {
        const std::string type_name = std::string(control.public_name) + "Type";
        output += "  <xs:complexType";
        append_attribute(output, "name", type_name);
        output += ">\n    <xs:sequence>\n";
        output +=
            "      <xs:element name=\"DataPath\" type=\"DataPathType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n";
        append_control_extension_elements(
            output,
            metamodel.control_extension_properties());
        output +=
            "      <xs:element name=\"Position\" type=\"PositionType\" minOccurs=\"1\" maxOccurs=\"1\"/>\n";
        if (control.kind == model::ControlKind::spreadsheet_document_field) {
            output +=
                "      <xs:element name=\"Document\" type=\"SpreadsheetDocumentType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n";
        }
        for (const auto& property : metamodel.properties_for(control.kind)) {
            if (control.kind == ControlKind::table && property.api_name == "Columns") {
                output +=
                    "      <xs:element name=\"Columns\" type=\"TableColumnsType\" minOccurs=\"1\" maxOccurs=\"1\"/>\n";
            } else if (control.kind == ControlKind::choice_field &&
                property.persistence == model::metamodel::PersistenceClass::runtime_only) {
                continue;
            } else {
                append_property_element(output, property, "      ");
            }
        }

        output += "      <xs:element name=\"Events\" type=\"";
        append_xml_escaped(output, control.public_name);
        output += "EventsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n";
        switch (control.child_policy) {
            case ChildPolicy::forbidden:
                break;
            case ChildPolicy::ordered_controls:
                output +=
                    "      <xs:element name=\"ChildItems\" type=\"ControlChildItemsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n";
                break;
            case ChildPolicy::ordered_controls_and_pages:
                output +=
                    "      <xs:element name=\"ChildItems\" type=\"PanelChildItemsType\" minOccurs=\"0\" maxOccurs=\"1\"/>\n";
                break;
        }
        output +=
            "    </xs:sequence>\n"
            "    <xs:attribute name=\"name\" type=\"xs:string\" use=\"required\"/>\n"
            "    <xs:attribute name=\"id\" type=\"ObjectIdType\" use=\"required\"/>\n"
            "  </xs:complexType>\n";
        output += "  <xs:element";
        append_attribute(output, "name", control.public_name);
        append_attribute(output, "type", type_name);
        output += "/>\n\n";
    }

    output += "</xs:schema>\n";
    return output;
}

void append_palette_properties(
    std::string& output,
    std::span<const PropertyDescriptor> properties,
    std::string_view indent
) {
    for (const auto& property : properties) {
        output.append(indent);
        output += "<Property";
        append_attribute(output, "name", property.xml_name);
        append_attribute(output, "apiName", property.api_name);
        append_attribute(output, "russianName", as_utf8(property.russian_name));
        append_attribute(output, "russianType", as_utf8(property.platform_type));
        append_attribute(
            output,
            "valueCodec",
            model::metamodel::value_codec_name(property.value_codec));
        output += "/>\n";
    }
}

void append_palette_events(
    std::string& output,
    std::span<const EventDescriptor> events,
    std::string_view indent
) {
    for (const auto& event : events) {
        output.append(indent);
        output += "<Event";
        append_attribute(output, "name", event.xml_name);
        append_attribute(output, "apiName", event.api_name);
        append_attribute(output, "russianName", as_utf8(event.russian_name));
        output += "/>\n";
    }
}

std::string generate_palette_xsd(const Metamodel& metamodel) {
    std::string output;
    output.reserve(96 * 1024);
    output +=
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "
        "elementFormDefault=\"unqualified\" attributeFormDefault=\"unqualified\">\n"
        "  <xs:annotation>\n"
        "    <xs:documentation>Generated bilingual annotations for the ordinary-form metamodel.</xs:documentation>\n"
        "    <xs:appinfo>\n"
        "      <Palette ordinaryFormVersion=\"";
    append_xml_escaped(output, ordinary_form_xml_version);
    output +=
        "\">\n"
        "        <Form>\n"
        "          <Properties>\n";
    append_palette_properties(output, metamodel.form_properties(), "            ");
    output += "          </Properties>\n          <Events>\n";
    append_palette_events(output, metamodel.form_events(), "            ");
    output +=
        "          </Events>\n"
        "        </Form>\n"
        "        <SharedProperties surface=\"controlExtension\">\n";
    append_palette_properties(
        output,
        metamodel.control_extension_properties(),
        "          ");
    output +=
        "        </SharedProperties>\n"
        "        <SharedProperties surface=\"panelPlacement\">\n";
    append_palette_properties(
        output,
        metamodel.panel_placement_properties(),
        "          ");
    output += "        </SharedProperties>\n        <Controls>\n";

    for (const auto& control : metamodel.controls()) {
        output += "          <Control";
        append_attribute(output, "name", control.public_name);
        append_attribute(output, "apiName", control.api_name);
        append_attribute(output, "russianName", as_utf8(control.russian_name));
        append_attribute(output, "children", child_policy_name(control.child_policy));
        output += ">\n            <Properties>\n";
        append_palette_properties(
            output,
            metamodel.properties_for(control.kind),
            "              ");
        output += "            </Properties>\n            <Events>\n";
        append_palette_events(
            output,
            metamodel.events_for(control.kind),
            "              ");
        output += "            </Events>\n          </Control>\n";
    }

    output += "        </Controls>\n        <StandardPictures>\n";
    for (const auto& picture : model::metamodel::standard_picture_descriptors()) {
        output += "          <Picture";
        append_attribute(output, "name", picture.runtime_name);
        append_attribute(output, "russianName", as_utf8(picture.russian_name));
        if (!picture.guid.empty()) append_attribute(output, "guid", picture.guid);
        if (picture.storage_id < 0) append_attribute(output, "storageId", std::to_string(picture.storage_id));
        output += "/>\n";
    }
    output +=
        "        </StandardPictures>\n"
        "        <NamedConcept name=\"CommandBarButton\" russianName=\"КнопкаКоманднойПанели\"><Properties>\n"
        "          <Property name=\"Name\" russianName=\"Имя\"/><Property name=\"Type\" russianName=\"ТипКнопки\"/>\n"
        "          <Property name=\"Text\" russianName=\"Текст\"/><Property name=\"Explanation\" russianName=\"Пояснение\"/><Property name=\"ToolTip\" russianName=\"Подсказка\"/>\n"
        "          <Property name=\"Enabled\" russianName=\"Доступность\"/><Property name=\"Checked\" russianName=\"Пометка\"/><Property name=\"ChangesData\" russianName=\"ИзменяетДанные\"/>\n"
        "          <Property name=\"Representation\" russianName=\"Отображение\"/><Property name=\"Shortcut\" russianName=\"СочетаниеКлавиш\"/><Property name=\"Picture\" russianName=\"Картинка\"/><Property name=\"Action\" russianName=\"Действие\"/><Property name=\"Order\" russianName=\"ПорядокКнопок\"/><Property name=\"Buttons\" russianName=\"Кнопки\"/>\n"
        "        </Properties></NamedConcept>\n"
        "        <NamedConcept name=\"TableColumn\" russianName=\"КолонкаТабличногоПоля\"><Properties>\n"
        "          <Property name=\"Name\" russianName=\"Имя\"/><Property name=\"DataPath\" russianName=\"Данные\"/>\n"
        "          <Property name=\"Header\" russianName=\"ТекстШапки\"/><Property name=\"Control\" russianName=\"ЭлементУправления\"/>\n"
        "        </Properties></NamedConcept>\n"
        "        <NamedConcept name=\"TableColumnControl\" russianName=\"ЭлементУправленияКолонкиТабличногоПоля\"><Properties>\n"
        "          <Property name=\"Type\" russianName=\"ВидРедактора\"/><Property name=\"Enabled\" russianName=\"Доступность\"/>\n"
        "          <Property name=\"ReadOnly\" russianName=\"ТолькоПросмотр\"/><Property name=\"Caption\" russianName=\"Заголовок\"/>\n"
        "          <Property name=\"ToolTip\" russianName=\"Подсказка\"/><Property name=\"Font\" russianName=\"Шрифт\"/>\n"
        "        </Properties></NamedConcept>\n"
        "      </Palette>\n"
        "    </xs:appinfo>\n"
        "  </xs:annotation>\n"
        "</xs:schema>\n";
    return output;
}

}  // namespace

GeneratedSchemas generate_schemas(const Metamodel& metamodel) {
    validate_metamodel(metamodel);
    return {
        generate_ordinary_form_xsd(metamodel),
        generate_palette_xsd(metamodel),
    };
}

}  // namespace oof::source
