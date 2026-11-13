#include "oof/source/form_xml.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlschemas.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "oof/model/metamodel.hpp"
#include "oof/source/schema_generator.hpp"
#include "oof/source/metadata_xml.hpp"
#include "oof/storage/value_codec.hpp"

namespace oof::source {
namespace {

namespace mm = model::metamodel;

struct XmlDocDeleter {
    void operator()(xmlDocPtr document) const noexcept {
        xmlFreeDoc(document);
    }
};

struct XmlSchemaDeleter {
    void operator()(xmlSchemaPtr schema) const noexcept {
        xmlSchemaFree(schema);
    }
};

struct XmlSchemaParserContextDeleter {
    void operator()(xmlSchemaParserCtxtPtr context) const noexcept {
        xmlSchemaFreeParserCtxt(context);
    }
};

struct XmlSchemaValidationContextDeleter {
    void operator()(xmlSchemaValidCtxtPtr context) const noexcept {
        xmlSchemaFreeValidCtxt(context);
    }
};

using XmlDoc = std::unique_ptr<xmlDoc, XmlDocDeleter>;
using XmlSchema = std::unique_ptr<xmlSchema, XmlSchemaDeleter>;
using XmlSchemaParserContext =
    std::unique_ptr<xmlSchemaParserCtxt, XmlSchemaParserContextDeleter>;
using XmlSchemaValidationContext =
    std::unique_ptr<xmlSchemaValidCtxt, XmlSchemaValidationContextDeleter>;

struct XmlErrorCapture {
    std::string message;
    int line = 0;
};

#if LIBXML_VERSION >= 21200
void capture_xml_error(void* context, const xmlError* error) {
#else
void capture_xml_error(void* context, xmlErrorPtr error) {
#endif
    if (context == nullptr || error == nullptr) {
        return;
    }
    auto& capture = *static_cast<XmlErrorCapture*>(context);
    if (error->message != nullptr) {
        capture.message = error->message;
        while (!capture.message.empty() &&
               (capture.message.back() == '\n' || capture.message.back() == '\r')) {
            capture.message.pop_back();
        }
    }
    capture.line = error->line;
}

class AdapterError final : public std::runtime_error {
public:
    explicit AdapterError(Diagnostic diagnostic)
        : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}

    [[nodiscard]] Diagnostic take_diagnostic() {
        return std::move(diagnostic_);
    }

private:
    Diagnostic diagnostic_;
};

std::string xml_node_path(xmlNodePtr node) {
    if (node == nullptr) {
        return {};
    }
    xmlChar* raw_path = xmlGetNodePath(node);
    if (raw_path == nullptr) {
        return {};
    }
    std::string path(reinterpret_cast<const char*>(raw_path));
    xmlFree(raw_path);
    return path;
}

[[noreturn]] void fail(
    std::string code,
    xmlNodePtr node,
    std::string object_id,
    std::string property,
    std::string expected,
    std::string actual,
    std::string message
) {
    throw AdapterError({
        std::move(code),
        DiagnosticSeverity::error,
        std::move(object_id),
        xml_node_path(node),
        std::move(property),
        std::move(expected),
        std::move(actual),
        std::move(message),
    });
}

std::string_view trim_ascii(std::string_view value) {
    const auto whitespace = [](unsigned char character) {
        return character == ' ' || character == '\t' || character == '\r' ||
               character == '\n';
    };
    while (!value.empty() && whitespace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && whitespace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::string node_name(xmlNodePtr node) {
    return node == nullptr || node->name == nullptr
               ? std::string{}
               : std::string(reinterpret_cast<const char*>(node->name));
}

std::vector<xmlNodePtr> element_children(xmlNodePtr parent) {
    std::vector<xmlNodePtr> children;
    for (xmlNodePtr child = parent == nullptr ? nullptr : parent->children;
         child != nullptr;
         child = child->next) {
        if (child->type == XML_ELEMENT_NODE) {
            children.push_back(child);
        }
    }
    return children;
}

std::string node_text(xmlNodePtr node) {
    xmlChar* raw = xmlNodeGetContent(node);
    if (raw == nullptr) {
        return {};
    }
    std::string value(reinterpret_cast<const char*>(raw));
    xmlFree(raw);
    return value;
}

std::optional<std::string> optional_attribute(xmlNodePtr node, std::string_view name) {
    xmlChar* raw = xmlGetProp(
        node,
        reinterpret_cast<const xmlChar*>(name.data()));
    if (raw == nullptr) {
        return std::nullopt;
    }
    std::string value(reinterpret_cast<const char*>(raw));
    xmlFree(raw);
    return value;
}

std::string required_attribute(
    xmlNodePtr node,
    std::string_view name,
    std::string_view object_id = {}
) {
    auto value = optional_attribute(node, name);
    if (!value.has_value()) {
        fail(
            "OOF2003",
            node,
            std::string(object_id),
            std::string(name),
            "required XML attribute",
            "missing",
            "Required ordinary-form XML attribute is missing");
    }
    return std::move(*value);
}

template <typename Integer>
Integer parse_integer(
    std::string_view text,
    xmlNodePtr node,
    std::string_view property,
    std::string_view object_id = {}
) {
    static_assert(std::is_integral_v<Integer>);
    text = trim_ascii(text);
    Integer value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        fail(
            "OOF2003",
            node,
            std::string(object_id),
            std::string(property),
            "integer in model range",
            std::string(text),
            "Ordinary-form XML integer is outside the typed model range");
    }
    return value;
}

bool parse_boolean(
    std::string_view text,
    xmlNodePtr node,
    std::string_view property,
    std::string_view object_id = {}
) {
    text = trim_ascii(text);
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    fail(
        "OOF2003",
        node,
        std::string(object_id),
        std::string(property),
        "true|false",
        std::string(text),
        "Ordinary-form XML Boolean has an invalid lexical value");
}

std::string canonical_decimal(
    std::string_view text,
    xmlNodePtr node,
    std::string_view property,
    std::string_view object_id = {}
) {
    try {
        return storage::value_codec::canonical_decimal(text);
    } catch (const std::invalid_argument&) {
        fail("OOF2003", node, std::string(object_id), std::string(property), "xs:decimal",
            std::string(text), "Ordinary-form decimal has an invalid lexical value");
    }
}

bool valid_chart_area_placement(std::string_view area, std::string_view placement) {
    const auto kind = area == "PlotArea" ? model::ChartAreaKind::plot :
        area == "LegendArea" ? model::ChartAreaKind::legend : area == "TitleArea" ? model::ChartAreaKind::title : model::ChartAreaKind::none;
    const auto placements = mm::chart_area_placements(kind);
    return std::ranges::find(placements, placement) != placements.end();
}

std::string canonical_uuid(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool nonzero_uuid(std::string_view value) {
    if (value.size() != 36) return false;
    bool nonzero = false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto character = static_cast<unsigned char>(value[index]);
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (character != '-') return false;
        } else {
            if (!std::isxdigit(character)) return false;
            nonzero = nonzero || character != '0';
        }
    }
    return nonzero;
}

model::ObjectId parse_object_id(
    std::string_view text,
    xmlNodePtr node,
    std::string_view property = "id",
    std::string_view object_id = {}
) {
    const std::uint64_t value =
        parse_integer<std::uint64_t>(text, node, property, object_id);
    if (value == 0) {
        fail(
            "OOF2003",
            node,
            std::string(object_id),
            std::string(property),
            "nonzero ObjectId",
            "0",
            "Ordinary-form object IDs must be nonzero");
    }
    return model::ObjectId{value};
}

model::ObjectId parse_attribute_id(
    std::string_view text,
    xmlNodePtr node,
    std::string_view property = "id",
    std::string_view object_id = {}
) {
    return model::ObjectId{parse_integer<std::uint64_t>(text, node, property, object_id)};
}

std::string object_id_text(model::ObjectId id) {
    return std::to_string(id.value());
}

std::string format_double(double value) {
    if (value == 0.0) {
        return "0";
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return output.str();
}

double parse_double(
    std::string_view text,
    xmlNodePtr node,
    std::string_view property,
    std::string_view object_id
) {
    std::istringstream input(std::string(trim_ascii(text)));
    input.imbue(std::locale::classic());
    double value = 0.0;
    input >> value;
    input >> std::ws;
    if (!input.eof() || !std::isfinite(value)) {
        fail(
            "OOF2003",
            node,
            std::string(object_id),
            std::string(property),
            "finite floating-point value",
            std::string(text),
            "Ordinary-form font height is not finite");
    }
    return value;
}

class CompiledOrdinaryFormSchema {
public:
    CompiledOrdinaryFormSchema() {
        const std::string_view runtime_version = xmlParserVersion ? xmlParserVersion : "";
        int version_number = 0;
        const auto parsed = std::from_chars(runtime_version.data(), runtime_version.data() + runtime_version.size(), version_number);
        if (parsed.ec != std::errc{} || parsed.ptr != runtime_version.data() + runtime_version.size() || version_number < 21406) {
            throw std::runtime_error("libxml2 runtime version 2.14.6 or newer is required for xs:decimal validation");
        }
        const GeneratedSchemas generated = generate_schemas(mm::Metamodel::instance());
        if (generated.ordinary_form_xsd.size() > static_cast<std::size_t>(INT_MAX)) {
            throw std::runtime_error("generated ordinary-form schema is too large");
        }
        XmlErrorCapture errors;
        XmlSchemaParserContext parser(xmlSchemaNewMemParserCtxt(
            generated.ordinary_form_xsd.data(),
            static_cast<int>(generated.ordinary_form_xsd.size())));
        if (!parser) {
            throw std::runtime_error("cannot allocate ordinary-form schema parser");
        }
        xmlSchemaSetParserStructuredErrors(parser.get(), capture_xml_error, &errors);
        schema_.reset(xmlSchemaParse(parser.get()));
        if (!schema_) {
            throw std::runtime_error(
                errors.message.empty() ? "cannot compile ordinary-form schema" : errors.message);
        }
    }

    [[nodiscard]] std::optional<Diagnostic> validate(xmlDocPtr document) const {
        XmlErrorCapture errors;
        XmlSchemaValidationContext context(xmlSchemaNewValidCtxt(schema_.get()));
        if (!context) {
            return Diagnostic{
                "OOF2002",
                DiagnosticSeverity::error,
                {},
                {},
                {},
                "compiled OrdinaryForm.xsd validator",
                "allocation failure",
                "Cannot allocate ordinary-form XSD validation context",
            };
        }
        xmlSchemaSetValidStructuredErrors(context.get(), capture_xml_error, &errors);
        const int result = xmlSchemaValidateDoc(context.get(), document);
        if (result == 0) {
            return std::nullopt;
        }
        return Diagnostic{
            "OOF2002",
            DiagnosticSeverity::error,
            {},
            errors.line > 0 ? "line " + std::to_string(errors.line) : std::string{},
            {},
            "OrdinaryForm.xsd 2.1 document",
            result < 0 ? "validation engine failure" : "schema-invalid XML",
            errors.message.empty() ? "Ordinary-form XML failed XSD validation" : errors.message,
        };
    }

private:
    XmlSchema schema_;
};

const CompiledOrdinaryFormSchema& compiled_schema() {
    static const CompiledOrdinaryFormSchema schema;
    return schema;
}

XmlDoc parse_and_validate_xml(std::string_view xml, bool mutable_tree = false) {
    if (xml.size() > static_cast<std::size_t>(INT_MAX)) {
        throw AdapterError({
            "OOF2001",
            DiagnosticSeverity::error,
            {},
            {},
            {},
            "XML document smaller than INT_MAX",
            std::to_string(xml.size()),
            "Ordinary-form XML is too large for libxml2",
        });
    }
    xmlInitParser();
    XmlDoc document(xmlReadMemory(
        xml.data(),
        static_cast<int>(xml.size()),
        "Form.xml",
        nullptr,
        XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOCDATA |
            (mutable_tree ? 0 : XML_PARSE_COMPACT) | XML_PARSE_NOERROR | XML_PARSE_NOWARNING));
    if (!document) {
        throw AdapterError({
            "OOF2001",
            DiagnosticSeverity::error,
            {},
            {},
            {},
            "well-formed XML",
            "malformed XML",
            "Ordinary-form source is not well-formed XML",
        });
    }
    if (document->intSubset != nullptr || document->extSubset != nullptr) {
        throw AdapterError({
            "OOF2001",
            DiagnosticSeverity::error,
            {},
            {},
            {},
            "XML without DTD declarations",
            "DTD declaration",
            "Ordinary-form XML must not contain a DTD",
        });
    }
    if (auto diagnostic = compiled_schema().validate(document.get())) {
        throw AdapterError(std::move(*diagnostic));
    }
    return document;
}

bool apply_configuration_type_names(xmlDocPtr document, const ConfigurationTypeSchema& types,
    Diagnostics& diagnostics) {
    bool changed = false;
    bool missing_identity_warning = false;
    std::set<std::pair<std::string, std::string>> warned_names;
    std::vector<xmlNodePtr> pending{xmlDocGetRootElement(document)};
    while (!pending.empty()) {
        xmlNodePtr node = pending.back();
        pending.pop_back();
        for (xmlNodePtr child : element_children(node)) pending.push_back(child);
        if (node == nullptr || node->ns != nullptr || node_name(node) != "Entry") continue;
        const auto term = optional_attribute(node, "term");
        if (!term || (*term != "object" && *term != "reference" && *term != "list" && *term != "type"))
            continue;
        const auto uuid = optional_attribute(node, "typeUuid");
        if (!uuid || !nonzero_uuid(*uuid)) continue;
        const std::string canonical = canonical_uuid(*uuid);
        const auto explicit_name = optional_attribute(node, "typeName");
        const auto schema_name = types.name_for(model::UuidValue{canonical});
        if (schema_name) {
            if (!explicit_name) {
                const std::string name(*schema_name);
                if (xmlSetProp(node, BAD_CAST "typeName", BAD_CAST name.c_str()) == nullptr)
                    throw std::runtime_error("Cannot set configuration type name in ordinary-form XML");
                changed = true;
            } else if (*explicit_name != *schema_name && warned_names.emplace(canonical, *explicit_name).second) {
                diagnostics.push_back({"OOF1142", DiagnosticSeverity::warning, {}, xml_node_path(node),
                    "typeName", std::string(*schema_name), *explicit_name,
                    "The explicit typeName differs from the supplied type schema; the name and UUID are preserved."});
            }
        } else if (!explicit_name && types.identity_count() == 0 && types.declaration_count() != 0 &&
            !missing_identity_warning) {
            diagnostics.push_back({"OOF1143", DiagnosticSeverity::warning, {}, {}, "typeName", {}, {},
                "The supplied type schema has declarations but no UUID identities; missing type names cannot be mapped from GUIDs."});
            missing_identity_warning = true;
        }
    }
    return changed;
}

model::LocalizedStringValue parse_localized_string(xmlNodePtr node) {
    model::LocalizedStringValue value;
    std::set<std::string> languages;
    for (xmlNodePtr item : element_children(node)) {
        const bool neutral = node_name(item) == "DefaultText";
        if (node_name(item) != "Item" && !neutral)
            fail("OOF2003", item, {}, node_name(item), "DefaultText or Item", node_name(item),
                "Localized string contains an unknown named value");
        if (neutral && (item->properties != nullptr || !element_children(item).empty()))
            fail("OOF2003", item, {}, "DefaultText", "plain default text", "attributes or children",
                "DefaultText cannot contain attributes or child elements");
        const std::string language = neutral ? "#" : required_attribute(item, "language");
        if (!neutral && language == "#")
            fail("OOF2003", item, {}, "language", "DefaultText for neutral text", language,
                "Neutral text must use the named DefaultText element");
        if (value.items.size() >= model::max_localized_text_items)
            fail("OOF2003", item, {}, "LocalizedString", "bounded translation count", "too many entries",
                "Localized string exceeds its resource budget");
        if (!languages.insert(language).second) {
            fail(
                "OOF2003",
                item,
                {},
                "language",
                "one Item per language",
                language,
                "Localized string contains a duplicate language");
        }
        value.items.push_back({language, node_text(item)});
    }
    return value;
}

model::FormattedStringValue parse_formatted_string(xmlNodePtr node, bool runtime_value = false) {
    const auto children = element_children(node);
    if (children.size() != 1 || node_name(children.front()) != "Value") {
        fail("OOF2003", node, {}, {}, "one Value element", std::to_string(children.size()),
            "Formatted string does not contain its typed Value");
    }
    const auto fields = element_children(children.front());
    model::FormattedStringValue result;
    result.formatted = parse_boolean(required_attribute(node, "formatted"), node, "formatted");
    if (fields.size() == 1 && node_name(fields.front()) == "String") {
        if (fields.front()->properties || !element_children(fields.front()).empty())
            fail("OOF2003", fields.front(), {}, "String", "plain string", "structured content",
                "Formatted string text cannot contain attributes or child elements");
        result.value = node_text(fields.front());
    } else {
        if (runtime_value) fail("OOF2003", node, {}, "FormattedString", "plain Value/String", "localized value",
            "Runtime formatted strings require plain string content");
        result.value = parse_localized_string(children.front());
    }
    return result;
}

model::CompositeIdValue parse_composite_id(xmlNodePtr node) {
    model::CompositeIdValue value;
    value.object_id = parse_integer<std::int64_t>(
        required_attribute(node, "objectId"), node, "objectId");
    value.uuid.canonical = canonical_uuid(required_attribute(node, "uuid"));
    value.is_null = parse_boolean(
        required_attribute(node, "isNull"), node, "isNull");
    constexpr std::string_view null_uuid = "00000000-0000-0000-0000-000000000000";
    if (value.is_null &&
        (value.object_id != 0 || value.uuid.canonical != null_uuid)) {
        fail(
            "OOF2003",
            node,
            {},
            "isNull",
            "objectId=0 and zero UUID",
            "inconsistent CompositeId",
            "Null CompositeId fields are inconsistent");
    }
    if (!value.is_null && value.object_id == 0 && value.uuid.canonical == null_uuid) {
        fail(
            "OOF2003",
            node,
            {},
            "isNull",
            "true for zero CompositeId",
            "false",
            "Zero CompositeId must be marked null");
    }
    return value;
}

model::TypeDomainTerm parse_type_domain_term(std::string_view value, xmlNodePtr node) {
    static constexpr std::array<std::pair<std::string_view, model::TypeDomainTerm>, 13> terms{{
        {"unknown", model::TypeDomainTerm::unknown},
        {"object", model::TypeDomainTerm::object},
        {"list", model::TypeDomainTerm::list},
        {"boolean", model::TypeDomainTerm::boolean},
        {"binary", model::TypeDomainTerm::binary},
        {"date", model::TypeDomainTerm::date},
        {"numeric", model::TypeDomainTerm::numeric},
        {"reference", model::TypeDomainTerm::reference},
        {"string", model::TypeDomainTerm::string},
        {"type", model::TypeDomainTerm::type},
        {"valueList", model::TypeDomainTerm::value_list},
        {"valueTable", model::TypeDomainTerm::value_table},
        {"valueTree", model::TypeDomainTerm::value_tree},
    }};
    const auto found = std::ranges::find(terms, value, &decltype(terms)::value_type::first);
    if (found == terms.end()) {
        fail(
            "OOF2003",
            node,
            {},
            "term",
            "known type-domain term",
            std::string(value),
            "Unknown type-domain term");
    }
    return found->second;
}

model::TypeDomainPatternValue parse_type_domain(xmlNodePtr node) {
    model::TypeDomainPatternValue value;
    if (node->properties != nullptr)
        fail("OOF2003", node, {}, node_name(node), "no attributes", "present",
            "Type-domain container does not accept attributes");
    for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
        if ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) &&
            child->content && !trim_ascii(reinterpret_cast<const char*>(child->content)).empty())
            fail("OOF2003", node, {}, node_name(node), "Entry elements", "text",
                "Type-domain container cannot contain direct text");
    }
    for (xmlNodePtr entry_node : element_children(node)) {
        if (node_name(entry_node) != "Entry" || !element_children(entry_node).empty() ||
            !trim_ascii(node_text(entry_node)).empty())
            fail("OOF2003", entry_node, {}, node_name(node), "empty Entry element", "unsupported content",
                "Type-domain entry must contain only named qualifiers");
        static constexpr std::array<std::string_view, 9> allowed{
            "term", "typeUuid", "typeName", "length", "precision", "nonNegative", "variable", "date", "time"};
        for (xmlAttrPtr attribute = entry_node->properties; attribute != nullptr; attribute = attribute->next) {
            if (attribute->ns != nullptr || std::ranges::find(allowed,
                std::string_view(reinterpret_cast<const char*>(attribute->name))) == allowed.end())
                fail("OOF2003", entry_node, {}, node_name(entry_node), "named type qualifier", "unsupported attribute",
                    "Type-domain entry contains an unsupported qualifier");
        }
        model::TypeDomainEntry entry;
        entry.term = parse_type_domain_term(required_attribute(entry_node, "term"), entry_node);
        const auto type_uuid = optional_attribute(entry_node, "typeUuid");
        const auto type_name = optional_attribute(entry_node, "typeName");
        const auto length = optional_attribute(entry_node, "length");
        const auto precision = optional_attribute(entry_node, "precision");
        const auto non_negative = optional_attribute(entry_node, "nonNegative");
        const auto variable = optional_attribute(entry_node, "variable");
        const auto date = optional_attribute(entry_node, "date");
        const auto time = optional_attribute(entry_node, "time");

        const bool reference_like =
            entry.term == model::TypeDomainTerm::object ||
            entry.term == model::TypeDomainTerm::list ||
            entry.term == model::TypeDomainTerm::reference ||
            entry.term == model::TypeDomainTerm::type;
        const bool numeric = entry.term == model::TypeDomainTerm::numeric;
        const bool string = entry.term == model::TypeDomainTerm::string;
        const bool binary = entry.term == model::TypeDomainTerm::binary;
        const bool date_term = entry.term == model::TypeDomainTerm::date;
        const bool invalid_attributes =
            (entry.term == model::TypeDomainTerm::object && !type_uuid.has_value()) ||
            (!reference_like && type_uuid.has_value()) ||
            (type_name && (!reference_like || !type_uuid || !nonzero_uuid(*type_uuid) ||
                trim_ascii(*type_name).empty())) ||
            (!numeric && (precision.has_value() || non_negative.has_value())) ||
            (!(numeric || string || binary) && length.has_value()) ||
            (!(string || binary) && variable.has_value()) ||
            (!date_term && (date.has_value() || time.has_value()));
        if (invalid_attributes) {
            fail(
                "OOF2003",
                entry_node,
                {},
                {},
                "qualifiers applicable to the selected term",
                "inapplicable qualifier",
                "Type-domain entry contains qualifiers for another term");
        }

        if (type_uuid.has_value()) {
            entry.type_uuid = model::UuidValue{canonical_uuid(*type_uuid)};
            if (entry.term == model::TypeDomainTerm::object &&
                entry.type_uuid->canonical == "e603c0f2-92fb-4d47-8f38-a44a381cf235") {
                fail("OOF2003", entry_node, {}, "typeUuid", "named valueTree term", *type_uuid,
                    "ValueTree requires its named type-domain term");
            }
        }
        if (type_name) entry.type_name = model::QualifiedName{*type_name};
        if (numeric) {
            if (length.has_value()) {
                entry.numeric.length = parse_integer<std::uint32_t>(*length, entry_node, "length");
            }
            if (precision.has_value()) {
                entry.numeric.precision =
                    parse_integer<std::uint32_t>(*precision, entry_node, "precision");
            }
            if (non_negative.has_value()) {
                entry.numeric.non_negative =
                    parse_boolean(*non_negative, entry_node, "nonNegative");
            }
            if (entry.numeric.length == 0) entry.numeric.precision = 0;
            if (entry.numeric.precision > entry.numeric.length && entry.numeric.length != 0) {
                fail(
                    "OOF2003",
                    entry_node,
                    {},
                    "precision",
                    "precision <= length",
                    std::to_string(entry.numeric.precision),
                    "Numeric type-domain precision exceeds length");
            }
        } else if (string || binary) {
            model::LengthQualifiers& qualifiers = string ? entry.string : entry.binary;
            if (length.has_value()) {
                qualifiers.length = parse_integer<std::uint32_t>(*length, entry_node, "length");
            }
            if (variable.has_value()) {
                qualifiers.variable = parse_boolean(*variable, entry_node, "variable");
            }
        } else if (date_term) {
            if (date.has_value()) {
                entry.date.date = parse_boolean(*date, entry_node, "date");
            }
            if (time.has_value()) {
                entry.date.time = parse_boolean(*time, entry_node, "time");
            }
        }
        value.entries.push_back(std::move(entry));
    }
    return value;
}

model::EnumerationValue parse_enumeration(xmlNodePtr node) {
    return {
        required_attribute(node, "type"),
        required_attribute(node, "member"),
    };
}

model::StyleReference parse_style_reference(xmlNodePtr node) {
    const auto name = optional_attribute(node, "styleName");
    const auto object_id = optional_attribute(node, "styleObjectId");
    const auto uuid = optional_attribute(node, "styleUuid");
    if (name.has_value()) {
        if (object_id.has_value() || uuid.has_value()) {
            fail(
                "OOF2003",
                node,
                {},
                "styleName",
                "one style reference representation",
                "both name and CompositeId",
                "Style reference representations are mutually exclusive");
        }
        return model::QualifiedName{*name};
    }
    if (object_id.has_value() != uuid.has_value()) {
        fail(
            "OOF2003",
            node,
            {},
            "styleObjectId",
            "styleObjectId together with styleUuid",
            "partial CompositeId",
            "Style CompositeId is incomplete");
    }
    if (object_id.has_value()) {
        const std::int64_t parsed_id =
            parse_integer<std::int64_t>(*object_id, node, "styleObjectId");
        const std::string parsed_uuid = canonical_uuid(*uuid);
        if (parsed_id == 0 &&
            parsed_uuid == "00000000-0000-0000-0000-000000000000") {
            fail("OOF2003", node, {}, "styleObjectId", "non-null style CompositeId", "null", "Style reference cannot be a null CompositeId");
        }
        return model::CompositeIdValue{
            parsed_id,
            model::UuidValue{parsed_uuid},
            false,
        };
    }
    return std::monostate{};
}

constexpr std::array<std::pair<model::ControlBorderType, std::string_view>, 9> border_type_names{{
    {model::ControlBorderType::without_border, "WithoutBorder"},
    {model::ControlBorderType::single, "Single"},
    {model::ControlBorderType::double_line, "Double"},
    {model::ControlBorderType::embossed, "Embossed"},
    {model::ControlBorderType::indented, "Indented"},
    {model::ControlBorderType::underline, "Underline"},
    {model::ControlBorderType::double_underline, "DoubleUnderline"},
    {model::ControlBorderType::overline, "Overline"},
    {model::ControlBorderType::rounded, "Rounded"},
}};

model::LineValue parse_line(xmlNodePtr node) {
    static constexpr std::string_view allowed[] = {"type", "member", "width", "gap"};
    for (xmlAttrPtr a = node->properties; a; a = a->next) {
        const std::string_view name(reinterpret_cast<const char*>(a->name));
        if (a->ns || std::ranges::find(allowed, name) == std::end(allowed))
            fail("OOF2003", node, {}, std::string(name), "named line attribute", "unsupported", "Invalid line attribute");
    }
    for (xmlNodePtr child = node->children; child; child = child->next)
        if (child->type == XML_ELEMENT_NODE || ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) && !trim_ascii(node_text(child)).empty()))
            fail("OOF2003", node, {}, "Line", "attributes only", "content", "Invalid line content");
    model::LineValue value{{required_attribute(node, "type"), required_attribute(node, "member")},
        parse_integer<std::int32_t>(required_attribute(node, "width"), node, "width"),
        parse_boolean(required_attribute(node, "gap"), node, "gap")};
    try { (void)storage::value_codec::encode_line(value); }
    catch (const std::exception& e) { fail("OOF2003", node, {}, "Line", "known line type", e.what(), "Invalid line"); }
    return value;
}

model::BorderValue parse_border(xmlNodePtr node) {
    static constexpr std::string_view allowed_attributes[] = {
        "kind", "borderType", "width", "styleName", "styleObjectId", "styleUuid"};
    std::set<std::string_view> seen;
    for (xmlAttrPtr attribute = node->properties; attribute != nullptr; attribute = attribute->next) {
        const std::string_view name(reinterpret_cast<const char*>(attribute->name));
        if (attribute->ns != nullptr ||
            std::ranges::find(allowed_attributes, name) == std::end(allowed_attributes) ||
            !seen.insert(name).second) {
            fail("OOF2003", node, {}, std::string(name), "one named Border attribute", "unsupported or duplicate",
                "Border contains an unsupported or duplicate attribute");
        }
    }
    for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
        if (child->type == XML_ELEMENT_NODE ||
            ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) &&
             !trim_ascii(node_text(child)).empty())) {
            fail("OOF2003", node, {}, "Border", "attributes only", "content",
                "Border cannot contain elements or text");
        }
    }
    model::BorderValue value;
    const auto kind = required_attribute(node, "kind");
    if (kind == "absolute") {
        if (seen.contains("styleName") || seen.contains("styleObjectId") || seen.contains("styleUuid")) {
            fail("OOF2003", node, {}, "Border", "absolute border without style attributes", "style",
                "Absolute Border must not carry a style reference");
        }
        const auto type_name = required_attribute(node, "borderType");
        const auto type = std::ranges::find(border_type_names, type_name,
            &std::pair<model::ControlBorderType, std::string_view>::second);
        if (type == border_type_names.end()) {
            fail("OOF2003", node, {}, "borderType", "ControlBorderType name", type_name,
                "Border contains an unknown ControlBorderType");
        }
        value.border_type = type->first;
        value.width = parse_integer<std::uint32_t>(required_attribute(node, "width"), node, "width");
    } else if (kind == "styleReference") {
        value.kind = model::BorderKind::style_reference;
        if (seen.contains("borderType") || seen.contains("width")) {
            fail("OOF2003", node, {}, "Border", "style border without absolute attributes", "absolute fields",
                "Style Border must not carry borderType or width");
        }
        value.style = parse_style_reference(node);
    } else {
        fail("OOF2003", node, {}, "kind", "absolute|styleReference", kind, "Unknown Border kind");
    }
    if (!model::valid_border_value(value)) {
        fail("OOF2003", node, {}, "Border", "valid typed Border", "invalid fields",
            "Border type, width, or style reference is invalid");
    }
    return value;
}

model::ColorKind parse_color_kind(std::string_view value, xmlNodePtr node) {
    if (value == "absolute") {
        return model::ColorKind::absolute;
    }
    if (value == "automatic") {
        return model::ColorKind::automatic;
    }
    if (value == "styleReference") {
        return model::ColorKind::style_reference;
    }
    fail(
        "OOF2003",
        node,
        {},
        "kind",
        "absolute|automatic|styleReference",
        std::string(value),
        "Unknown color kind");
}

model::ColorValue parse_color(xmlNodePtr node) {
    model::ColorValue value;
    value.kind = parse_color_kind(required_attribute(node, "kind"), node);
    const auto channel = [&](std::string_view name, std::uint8_t fallback) {
        const auto text = optional_attribute(node, name);
        return text.has_value()
                   ? parse_integer<std::uint8_t>(*text, node, name)
                   : fallback;
    };
    value.red = channel("red", 0);
    value.green = channel("green", 0);
    value.blue = channel("blue", 0);
    value.alpha = channel("alpha", 255);
    value.style = parse_style_reference(node);

    const bool default_channels =
        value.red == 0 && value.green == 0 && value.blue == 0 && value.alpha == 255;
    const bool has_style = !std::holds_alternative<std::monostate>(value.style);
    if (value.kind == model::ColorKind::absolute && has_style) {
        fail("OOF2003", node, {}, "kind", "absolute color without style", "style", "Absolute color must not carry a style reference");
    }
    if (value.kind == model::ColorKind::automatic && (!default_channels || has_style)) {
        fail("OOF2003", node, {}, "kind", "automatic color defaults", "explicit color data", "Automatic color must not carry channels or a style reference");
    }
    if (value.kind == model::ColorKind::style_reference && (!default_channels || !has_style)) {
        fail("OOF2003", node, {}, "kind", "one style reference", "invalid style color", "Style color must carry exactly one style reference");
    }
    return value;
}

model::FontKind parse_font_kind(std::string_view value, xmlNodePtr node) {
    if (value == "absolute") {
        return model::FontKind::absolute;
    }
    if (value == "windowsFont") {
        return model::FontKind::windows_font;
    }
    if (value == "styleReference") {
        return model::FontKind::style_reference;
    }
    if (value == "automatic") {
        return model::FontKind::automatic;
    }
    fail(
        "OOF2003",
        node,
        {},
        "kind",
        "absolute|windowsFont|styleReference|automatic",
        std::string(value),
        "Unknown font kind");
}

constexpr std::array<std::string_view, 5> windows_font_names{
    "DefaultGUIFont", "OEMFixedFont", "ANSIFixedFont", "ANSIVariableFont", "SystemFont"};

model::FontValue parse_font(xmlNodePtr node) {
    static constexpr std::string_view allowed_attributes[] = {
        "kind", "styleName", "styleObjectId", "styleUuid", "faceName", "height",
        "bold", "italic", "underline", "strikeout", "scale", "scaleOverride", "windowsFont", "autoCharSet"};
    for (xmlAttrPtr attribute = node->properties; attribute != nullptr; attribute = attribute->next) {
        const std::string_view name(reinterpret_cast<const char*>(attribute->name));
        if (std::ranges::find(allowed_attributes, name) == std::end(allowed_attributes)) {
            fail("OOF2003", node, {}, std::string(name), "named Font attribute", "unsupported",
                "Font contains an unsupported attribute");
        }
    }
    model::FontValue value;
    value.kind = parse_font_kind(required_attribute(node, "kind"), node);
    value.style = parse_style_reference(node);
    if (const auto selector = optional_attribute(node, "windowsFont")) {
        const auto found = std::ranges::find(windows_font_names, *selector);
        if (found == windows_font_names.end())
            fail("OOF2003", node, {}, "windowsFont", "known Windows font name", *selector, "Unknown Windows font");
        value.windows_font = static_cast<model::WindowsFont>(std::distance(windows_font_names.begin(), found));
    }
    if (auto face_name = optional_attribute(node, "faceName")) {
        value.face_name = std::move(*face_name);
    }
    if (auto height = optional_attribute(node, "height")) {
        value.height = parse_double(*height, node, "height", {});
    }
    if (auto bold = optional_attribute(node, "bold")) {
        value.bold = parse_boolean(*bold, node, "bold");
    }
    if (auto italic = optional_attribute(node, "italic")) {
        value.italic = parse_boolean(*italic, node, "italic");
    }
    if (auto underline = optional_attribute(node, "underline")) {
        value.underline = parse_boolean(*underline, node, "underline");
    }
    if (auto strikeout = optional_attribute(node, "strikeout")) {
        value.strikeout = parse_boolean(*strikeout, node, "strikeout");
    }
    if (auto scale = optional_attribute(node, "scale")) {
        value.scale = parse_double(*scale, node, "scale", {});
    }
    if (auto scale_override = optional_attribute(node, "scaleOverride")) {
        value.scale_override = parse_boolean(*scale_override, node, "scaleOverride");
    }
    if (auto auto_char_set = optional_attribute(node, "autoCharSet")) {
        value.auto_char_set = parse_boolean(*auto_char_set, node, "autoCharSet");
    }
    const bool has_style = !std::holds_alternative<std::monostate>(value.style);
    if ((value.kind == model::FontKind::windows_font) != value.windows_font.has_value())
        fail("OOF2003", node, {}, "windowsFont", "selector required only for Windows font", "invalid selector", "Windows font selector is inconsistent");
    if (value.kind == model::FontKind::style_reference && !has_style) {
        fail("OOF2003", node, {}, "kind", "style reference", "missing", "Style font must carry a style reference");
    }
    if (value.kind != model::FontKind::style_reference && has_style) {
        fail("OOF2003", node, {}, "kind", "font without style reference", "style", "Only a style font may carry a style reference");
    }
    try { (void)storage::value_codec::encode_font(value); }
    catch (const std::exception& error) { fail("OOF2003", node, {}, "Font", "supported Font", {}, error.what()); }
    return value;
}

model::ShortcutValue parse_shortcut(xmlNodePtr node) {
    static constexpr std::string_view allowed_attributes[] = {"Alt", "Ctrl", "Shift"};
    for (xmlAttrPtr attribute = node->properties; attribute != nullptr; attribute = attribute->next) {
        const std::string_view name(reinterpret_cast<const char*>(attribute->name));
        if (std::ranges::find(allowed_attributes, name) == std::end(allowed_attributes)) {
            fail("OOF2003", node, {}, std::string(name), "named Shortcut attribute", "unsupported",
                "Shortcut contains an unsupported attribute");
        }
    }

    model::ShortcutValue value;
    value.alt = parse_boolean(required_attribute(node, "Alt"), node, "Alt");
    value.ctrl = parse_boolean(required_attribute(node, "Ctrl"), node, "Ctrl");
    value.shift = parse_boolean(required_attribute(node, "Shift"), node, "Shift");

    xmlNodePtr key_node = nullptr;
    for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
        if (child->type == XML_ELEMENT_NODE) {
            if (node_name(child) != "Key" || key_node != nullptr) {
                fail("OOF2003", child, {}, "Key", "one named Key child", node_name(child),
                    "Shortcut must contain exactly one Key child");
            }
            key_node = child;
        } else if ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) &&
                   !trim_ascii(reinterpret_cast<const char*>(child->content)).empty()) {
            fail("OOF2003", node, {}, "Shortcut", "Key child only", "text content",
                "Shortcut cannot contain text outside Key");
        }
    }
    if (key_node == nullptr) {
        fail("OOF2003", node, {}, "Key", "required Key child", "missing",
            "Shortcut must contain one Key child");
    }
    if (key_node->properties != nullptr || !element_children(key_node).empty()) {
        fail("OOF2003", key_node, {}, "Key", "text key name only", "attributes or nested elements",
            "Shortcut Key must contain only a key name");
    }
    value.key = node_text(key_node);
    if (trim_ascii(value.key) != value.key || mm::find_shortcut_key(value.key) == nullptr) {
        fail("OOF2003", key_node, {}, "Key", "platform named key", value.key,
            "Shortcut Key is not a supported platform key name");
    }
    return value;
}

model::PictureRef parse_picture_reference(
    xmlNodePtr node,
    std::string_view property,
    std::string_view object_id
) {
    if (optional_attribute(node, "width") || optional_attribute(node, "height"))
        fail("OOF2003", node, std::string(object_id), std::string(property),
            "transparentX/transparentY coordinates", {}, "Picture width/height attributes are unsupported");
    const auto transparency = [&](model::PictureRef reference) {
        if (const auto x = optional_attribute(node, "transparentX")) reference.transparent_x = parse_integer<std::int64_t>(*x, node, property, object_id);
        if (const auto y = optional_attribute(node, "transparentY")) reference.transparent_y = parse_integer<std::int64_t>(*y, node, property, object_id);
        if (const auto transparent = optional_attribute(node, "transparent")) reference.transparent = parse_boolean(*transparent, node, property, object_id);
        if (reference.transparent_x < -1 || reference.transparent_y < -1)
            fail("OOF2003", node, std::string(object_id), std::string(property), "transparency coordinates at least -1", {}, "Invalid picture transparency coordinates");
        return reference;
    };
    if (const auto uuid = optional_attribute(node, "commonPicture")) {
        const auto canonical = canonical_uuid(*uuid);
        if (optional_attribute(node, "standardName") || !node_text(node).empty() ||
            canonical == "00000000-0000-0000-0000-000000000000")
            fail("OOF2003", node, std::string(object_id), std::string(property),
                "one nonzero CommonPicture UUID", {}, "Picture reference has inconsistent targets");
        return transparency(model::PictureRef{model::PictureAssetRef{}, std::nullopt, model::UuidValue{canonical}});
    }
    if (const auto standard_name = optional_attribute(node, "standardName")) {
        const std::string text = node_text(node);
        if (!text.empty()) {
            fail("OOF2003", node, std::string(object_id), std::string(property),
                "empty standard picture reference", text,
                "A standard picture reference cannot also contain an asset ID");
        }
        if (mm::find_standard_picture(*standard_name) == nullptr) {
            fail("OOF2003", node, std::string(object_id), std::string(property),
                "known PictureLib name", *standard_name, "Unknown standard picture name");
        }
        return transparency(model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
            model::QualifiedName{*standard_name}});
    }
    if (optional_attribute(node, "transparentX") || optional_attribute(node, "transparentY") || optional_attribute(node, "transparent"))
        fail("OOF2003", node, std::string(object_id), std::string(property), "file asset reference without transparency coordinates", {},
            "File picture transparency coordinates belong to the asset");
    return model::PictureRef{model::PictureAssetRef{
        parse_object_id(node_text(node), node, property, object_id)}, std::nullopt};
}

void consume_typed_value_budget(model::TypedValueBudget& budget, std::size_t depth,
    xmlNodePtr node, std::string_view owner) {
    if (depth > model::max_typed_value_depth || budget.nodes >= model::max_typed_value_nodes)
        fail("OOF2003", node, std::string(owner), "Value", "bounded typed value graph", {},
            "Typed values exceed the document resource budget");
    ++budget.nodes;
}

void require_typed_container(xmlNodePtr node, std::string_view owner, std::string_view allowed_attribute = {}) {
    for (auto* attribute = node->properties; attribute != nullptr; attribute = attribute->next) {
        if (allowed_attribute.empty() || std::string_view(reinterpret_cast<const char*>(attribute->name)) != allowed_attribute)
            fail("OOF2003", node, std::string(owner), node_name(node), "named typed value content", "attribute",
                "Typed value container has an unsupported attribute");
    }
    for (auto* child = node->children; child != nullptr; child = child->next) {
        if ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) && child->content &&
            !trim_ascii(reinterpret_cast<const char*>(child->content)).empty())
            fail("OOF2003", node, std::string(owner), node_name(node), "typed child elements", "text",
                "Typed value container cannot contain direct text");
    }
}

xmlNodePtr single_typed_child(xmlNodePtr node, std::string_view owner, std::string_view allowed_attribute = {}) {
    require_typed_container(node, owner, allowed_attribute);
    xmlNodePtr field = nullptr;
    for (auto* child = node->children; child != nullptr; child = child->next) {
        if (child->type != XML_ELEMENT_NODE) continue;
        if (field) fail("OOF2003", node, std::string(owner), node_name(node), "one typed value", "multiple",
            "Typed value wrapper requires exactly one child");
        field = child;
    }
    if (!field) fail("OOF2003", node, std::string(owner), node_name(node), "one typed value", "missing",
        "Typed value wrapper requires exactly one child");
    return field;
}

model::PropertyValue parse_typed_node(xmlNodePtr field, std::string_view owner,
    model::TypedValueBudget& budget, std::size_t depth = 0) {
    consume_typed_value_budget(budget, depth, field, owner);
    const auto kind = node_name(field);
    if (kind == "Color") return parse_color(field);
    if (kind == "Font") return parse_font(field);
    if (kind == "Border") return parse_border(field);
    if (kind == "FormattedString") return parse_formatted_string(field, true);
    if (kind == "TypeDescription") {
        for (auto* child = field->children; child != nullptr; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            if (budget.nodes >= model::max_typed_value_nodes)
                fail("OOF2003", field, std::string(owner), kind, "bounded TypeDescription", {},
                    "TypeDescription exceeds the document resource budget");
            ++budget.nodes;
        }
        return parse_type_domain(field);
    }
    if (kind == "ValueList") {
        require_typed_container(field, owner, "lastAssignedId");
        model::ValueListValue value;
        constexpr auto id_limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (const auto last = optional_attribute(field, "lastAssignedId")) {
            const auto id = parse_integer<std::uint64_t>(*last, field, "lastAssignedId", owner);
            if (id > id_limit) fail("OOF2003", field, std::string(owner), kind, "signed 64-bit history ID", *last,
                "ValueList history ID is out of range");
            value.last_assigned_id = id;
        }
        unsigned stage = 0;
        for (auto* child = field->children; child != nullptr; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            const auto name = node_name(child);
            if (name == "ValueType" && stage == 0) {
                for (auto* entry = child->children; entry != nullptr; entry = entry->next) {
                    if (entry->type != XML_ELEMENT_NODE) continue;
                    if (budget.nodes >= model::max_typed_value_nodes)
                        fail("OOF2003", child, std::string(owner), name, "bounded TypeDomain", {},
                            "ValueList ValueType exceeds the document resource budget");
                    ++budget.nodes;
                }
                value.value_type = parse_type_domain(child);
                stage = 1;
            } else if (name == "AvailableValues" && stage <= 1) {
                auto* nested = single_typed_child(child, owner);
                if (node_name(nested) != "ValueList") fail("OOF2003", nested, std::string(owner), name, "one ValueList", node_name(nested),
                    "AvailableValues requires a named ValueList");
                auto parsed = parse_typed_node(nested, owner, budget, depth + 1);
                value.available_values.push_back(std::get<model::ValueListValue>(std::move(parsed)));
                stage = 2;
            } else if (name == "Items" && stage <= 2) {
                require_typed_container(child, owner);
                std::set<std::uint64_t> ids;
                for (auto* item_node = child->children; item_node != nullptr; item_node = item_node->next) {
                    if (item_node->type != XML_ELEMENT_NODE) continue;
                    consume_typed_value_budget(budget, depth + 1, item_node, owner);
                    if (node_name(item_node) != "Item") fail("OOF2003", item_node, std::string(owner), name, "Item", node_name(item_node),
                        "ValueList Items requires named Item elements");
                    require_typed_container(item_node, owner, "id");
                    model::ValueListItem item;
                    item.id = parse_integer<std::uint64_t>(required_attribute(item_node, "id", owner), item_node, "id", owner);
                    if (item.id > id_limit || !ids.insert(item.id).second)
                        fail("OOF2003", item_node, std::string(owner), "id", "unique signed 64-bit item ID", std::to_string(item.id),
                            "ValueList item ID is duplicated or out of range");
                    bool value_seen = false;
                    bool presentation_seen = false;
                    unsigned item_stage = 0;
                    for (auto* part = item_node->children; part != nullptr; part = part->next) {
                        if (part->type != XML_ELEMENT_NODE) continue;
                        const auto property = node_name(part);
                        if (property == "Value" && item_stage == 0) {
                            item.value = parse_typed_node(single_typed_child(part, owner), owner, budget, depth + 2);
                            value_seen = true;
                            item_stage = 1;
                        } else if (property == "Presentation" && value_seen && item_stage <= 1) {
                            auto* presentation = single_typed_child(part, owner);
                            const auto presentation_kind = node_name(presentation);
                            if (presentation_kind != "String" && presentation_kind != "FormattedString")
                                fail("OOF2003", presentation, std::string(owner), property, "String or FormattedString", presentation_kind,
                                    "ValueList Presentation requires a string value");
                            item.presentation = parse_typed_node(presentation, owner, budget, depth + 2);
                            if (const auto* formatted = std::get_if<model::FormattedStringValue>(&item.presentation);
                                formatted && !formatted->formatted)
                                fail("OOF2003", presentation, std::string(owner), property, "formatted=true", "false",
                                    "ValueList formatted Presentation must retain its formatted kind");
                            presentation_seen = true;
                            item_stage = 2;
                        } else if (property == "Check" && value_seen && item_stage <= 2) {
                            item.checked = parse_boolean(node_text(part), part, property, owner);
                            item_stage = 3;
                        } else if (property == "Picture" && value_seen && item_stage <= 3) {
                            consume_typed_value_budget(budget, depth + 2, part, owner);
                            auto picture = parse_picture_reference(part, property, owner);
                            if (!picture.standard_name && !picture.common_picture)
                                fail("OOF2003", part, std::string(owner), property, "named standard Picture", "file reference",
                                    "ValueList Picture file references are not supported");
                            item.picture = std::move(picture);
                            item_stage = 4;
                        } else fail("OOF2003", part, std::string(owner), property, "ordered Value, Presentation, Check, Picture", property,
                            "ValueList Item has a duplicate, missing, or out-of-order property");
                    }
                    if (!value_seen) fail("OOF2003", item_node, std::string(owner), "Value", "required typed value", "missing",
                        "ValueList Item requires a Value");
                    if (!presentation_seen) consume_typed_value_budget(budget, depth + 2, item_node, owner);
                    value.items.push_back(std::move(item));
                }
                stage = 3;
            } else fail("OOF2003", child, std::string(owner), kind, "ordered ValueType, AvailableValues, Items", name,
                "ValueList has a duplicate or out-of-order field");
        }
        std::uint64_t max_id = 0;
        for (const auto& item : value.items) max_id = std::max(max_id, item.id);
        if (value.last_assigned_id && !value.items.empty() && *value.last_assigned_id < max_id)
            fail("OOF2003", field, std::string(owner), "lastAssignedId", "at least the largest item ID", std::to_string(*value.last_assigned_id),
                "ValueList history cannot precede a live item ID");
        return model::PropertyValue{std::move(value)};
    }
    if (kind == "Array" || kind == "FixedArray") {
        require_typed_container(field, owner);
        model::ArrayValue value;
        value.fixed = kind == "FixedArray";
        for (auto* child = field->children; child != nullptr; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            if (node_name(child) != "Item") fail("OOF2003", child, std::string(owner), kind, "Item", node_name(child),
                "Array requires named Item wrappers");
            value.items.push_back(parse_typed_node(single_typed_child(child, owner), owner, budget, depth + 1));
        }
        return model::PropertyValue{std::move(value)};
    }
    if (kind == "Structure" || kind == "FixedStructure") {
        require_typed_container(field, owner);
        model::StructureValue value;
        value.fixed = kind == "FixedStructure";
        std::set<std::string> names;
        for (auto* child = field->children; child != nullptr; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            if (node_name(child) != "Field") fail("OOF2003", child, std::string(owner), kind, "Field", node_name(child),
                "Structure requires named Field wrappers");
            auto* nested = single_typed_child(child, owner, "name");
            auto parsed = parse_typed_node(nested, owner, budget, depth + 1);
            auto name = required_attribute(child, "name", owner);
            if (name.empty() || !names.insert(name).second)
                fail("OOF2003", child, std::string(owner), kind, "unique nonempty field name", name,
                    "Structure fields require unique nonempty names");
            value.fields.push_back({std::move(name), std::move(parsed)});
        }
        return model::PropertyValue{std::move(value)};
    }
    if (kind == "Map" || kind == "FixedMap") {
        require_typed_container(field, owner);
        model::MapValue value;
        value.fixed = kind == "FixedMap";
        for (auto* child = field->children; child != nullptr; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            if (node_name(child) != "Entry") fail("OOF2003", child, std::string(owner), kind, "Entry", node_name(child),
                "Map requires named Entry wrappers");
            require_typed_container(child, owner);
            xmlNodePtr key = nullptr;
            xmlNodePtr mapped = nullptr;
            for (auto* part = child->children; part != nullptr; part = part->next) {
                if (part->type != XML_ELEMENT_NODE) continue;
                if (!key && node_name(part) == "Key") key = part;
                else if (key && !mapped && node_name(part) == "Value") mapped = part;
                else fail("OOF2003", part, std::string(owner), kind, "one Key followed by one Value", node_name(part),
                    "Map Entry requires an ordered Key and Value pair");
            }
            if (!key || !mapped) fail("OOF2003", child, std::string(owner), kind, "Key and Value", "missing",
                "Map Entry requires both Key and Value");
            auto parsed_key = parse_typed_node(single_typed_child(key, owner), owner, budget, depth + 1);
            auto parsed_value = parse_typed_node(single_typed_child(mapped, owner), owner, budget, depth + 1);
            value.entries.push_back({std::move(parsed_key), std::move(parsed_value)});
        }
        return model::PropertyValue{std::move(value)};
    }
    if (field->properties || !element_children(field).empty())
        fail("OOF2003", field, std::string(owner), kind, "plain typed scalar", "structured content",
            "Primitive typed value cannot contain attributes or children");
    if (kind == "String") return node_text(field);
    if (kind == "Number") return model::DecimalValue{canonical_decimal(node_text(field), field, kind, owner)};
    if (kind == "Boolean") return parse_boolean(node_text(field), field, kind, owner);
    if (kind == "Date") { const auto text = std::string(trim_ascii(node_text(field))); (void)storage::value_codec::date_to_platform(text); return model::DateValue{text}; }
    if (kind == "Undefined" && node_text(field) == "undefined") return model::UndefinedValue{};
    if (kind == "Null" && node_text(field).empty()) return model::NullValue{};
    if (kind == "UUID") return model::UuidValue{canonical_uuid(std::string(trim_ascii(node_text(field))))};
    if (kind == "Type") {
        const auto name = std::string(trim_ascii(node_text(field)));
        const auto descriptors = mm::platform_value_type_descriptors();
        if (std::ranges::find(descriptors, name, &mm::PlatformValueTypeDescriptor::name) == descriptors.end())
            fail("OOF2003", field, std::string(owner), "Type", "known platform type", name, "Unsupported platform type value");
        return model::PlatformTypeValue{name};
    }
    fail("OOF2003", field, std::string(owner), "Value", "supported named typed value", kind,
        "Unsupported typed value kind");
}

model::PropertyValue parse_typed_value(xmlNodePtr node, std::string_view owner, model::TypedValueBudget& budget) {
    return parse_typed_node(single_typed_child(node, owner), owner, budget);
}


model::PropertyValue parse_property_value(
    xmlNodePtr node,
    mm::ValueCodec codec,
    std::string_view object_id,
    model::TypedValueBudget& typed_budget
) {
    const std::string property = node_name(node);
    switch (codec) {
        case mm::ValueCodec::calendar_selected_dates:
            fail("OOF2003", node, std::string(object_id), property, "runtime-only calendar selection", "XML",
                "Runtime calendar selections cannot be parsed from XML");
        case mm::ValueCodec::runtime_value:
            fail("OOF2003", node, std::string(object_id), property, "runtime-only value", "XML",
                "Runtime values cannot be parsed from XML");
        case mm::ValueCodec::table_columns:
            fail("OOF2003", node, std::string(object_id), property, "named Table Columns", "scalar collection",
                "Table columns must use their owned named object model");
        case mm::ValueCodec::value_list_reference:
        case mm::ValueCodec::value_list_item_reference:
            fail("OOF2003", node, std::string(object_id), property, "runtime-only list reference", "XML",
                "Runtime value list references cannot be parsed from XML");
        case mm::ValueCodec::form_controls:
        case mm::ValueCodec::panel_pages:
            fail("OOF2003", node, std::string(object_id), property, "named ChildItems", "scalar collection",
                "Form item collections are represented only by ChildItems");
        case mm::ValueCodec::page_reference:
            fail("OOF2003", node, std::string(object_id), property, "runtime-only page reference", "XML",
                "Runtime page selection cannot be parsed from XML");
        case mm::ValueCodec::owned_panel:
            fail("OOF2003", node, std::string(object_id), property, "typed Form.Panel", "scalar", "Owned Panel is not a scalar property");
        case mm::ValueCodec::typed_value:
            return parse_typed_value(node, object_id, typed_budget);
        case mm::ValueCodec::unclassified:
            fail("OOF2003", node, std::string(object_id), property, "classified value codec", "unclassified", "Unclassified properties are not admitted to the product model");
        case mm::ValueCodec::boolean:
            return parse_boolean(node_text(node), node, property, object_id);
        case mm::ValueCodec::integer:
            return parse_integer<std::int64_t>(node_text(node), node, property, object_id);
        case mm::ValueCodec::integer32:
            return static_cast<std::int64_t>(
                parse_integer<std::int32_t>(node_text(node), node, property, object_id));
        case mm::ValueCodec::decimal:
            return model::DecimalValue{canonical_decimal(node_text(node), node, property, object_id)};
        case mm::ValueCodec::string:
            return node_text(node);
        case mm::ValueCodec::localized_text: {
            const auto children = element_children(node);
            if (children.empty()) return node_text(node);
            if (children.size() > model::max_localized_text_items) {
                fail("OOF2003", node, std::string(object_id), property,
                    "at most 65536 translations", "too many translations",
                    "Localized text exceeds the translation limit");
            }
            for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
                if ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) &&
                    child->content != nullptr &&
                    !trim_ascii(reinterpret_cast<const char*>(child->content)).empty()) {
                    fail("OOF2003", child, std::string(object_id), property,
                        "plain text or localized Item elements", "mixed text and items",
                        "Localized text cannot combine direct content with translations");
                }
            }
            return parse_localized_string(node);
        }
        case mm::ValueCodec::localized_string:
            return parse_localized_string(node);
        case mm::ValueCodec::formatted_string:
            return parse_formatted_string(node);
        case mm::ValueCodec::date: {
            const std::string text(trim_ascii(node_text(node)));
            if (text == "undefined") return model::UndefinedValue{};
            (void)storage::value_codec::date_to_platform(text);
            return model::DateValue{text};
        }
        case mm::ValueCodec::uuid:
            return model::UuidValue{canonical_uuid(std::string(trim_ascii(node_text(node))))};
        case mm::ValueCodec::style: {
            for (xmlAttrPtr attribute = node->properties; attribute != nullptr; attribute = attribute->next) {
                if (attribute->name == nullptr || std::string_view(reinterpret_cast<const char*>(attribute->name)) != "styleId")
                    fail("OOF2003", node, std::string(object_id), property, "styleId attribute only", "unexpected attribute",
                        "Style accepts only the named configuration style reference");
            }
            if (!element_children(node).empty() || !trim_ascii(node_text(node)).empty())
                fail("OOF2003", node, std::string(object_id), property, "empty Style element", "child or text content",
                    "Style cannot contain child elements or text");
            const auto style_id = optional_attribute(node, "styleId");
            if (!style_id) return model::StyleValue{};
            const std::string canonical = canonical_uuid(*style_id);
            if (canonical == "00000000-0000-0000-0000-000000000000")
                fail("OOF2003", node, std::string(object_id), property, "nonzero configuration style UUID", canonical,
                    "Style reference UUID must be nonzero");
            return model::StyleValue{model::UuidValue{canonical}};
        }
        case mm::ValueCodec::composite_id:
            return parse_composite_id(node);
        case mm::ValueCodec::type_domain:
            return parse_type_domain(node);
        case mm::ValueCodec::enumeration:
            return parse_enumeration(node);
        case mm::ValueCodec::color:
            return parse_color(node);
        case mm::ValueCodec::line:
            return parse_line(node);
        case mm::ValueCodec::border:
            return parse_border(node);
        case mm::ValueCodec::font:
            return parse_font(node);
        case mm::ValueCodec::shortcut:
            return parse_shortcut(node);
        case mm::ValueCodec::picture:
            return parse_picture_reference(node, property, object_id);
        case mm::ValueCodec::chart_series_reference:
            return model::ChartSeriesRef{parse_object_id(object_id, node, property, object_id),
                parse_object_id(node_text(node), node, property, object_id)};
        case mm::ValueCodec::chart_point_reference: {
            if (node->properties != nullptr || !element_children(node).empty())
                fail("OOF2003", node, std::string(object_id), property, "plain positive point ID or empty element", "attributes or child elements", "Chart ActivePoint must contain only a point ID");
            const std::string point_id(trim_ascii(node_text(node)));
            if (point_id.empty()) return model::UndefinedValue{};
            return model::ChartPointRef{parse_object_id(object_id, node, property, object_id),
                parse_object_id(point_id, node, property, object_id)};
        }
        case mm::ValueCodec::context_menu_reference: {
            const auto control_id = optional_attribute(node, "controlId");
            const auto children = element_children(node);
            if (!children.empty() && !control_id)
                fail("OOF2003", node, std::string(object_id), property,
                    "controlId for a button path", "missing", "ContextMenu button path needs an owner");
            for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
                if (child->type == XML_COMMENT_NODE || child->type == XML_PI_NODE || child->type == XML_ELEMENT_NODE) continue;
                if ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) &&
                    child->content != nullptr && trim_ascii(reinterpret_cast<const char*>(child->content)).empty()) continue;
                fail("OOF2003", child, std::string(object_id), property,
                    "Button elements only", "text or unsupported content", "ContextMenu has unexpected content");
            }
            if (!control_id) return model::UndefinedValue{};
            const model::ControlRef owner{parse_object_id(*control_id, node, property, object_id)};
            if (children.empty()) return owner;
            model::CommandBarButtonRef reference{owner, {}};
            for (auto* child : children) {
                if (node_name(child) != "Button" || !element_children(child).empty() ||
                    !trim_ascii(node_text(child)).empty())
                    fail("OOF2003", child, std::string(object_id), property,
                        "empty Button with name", node_name(child), "Invalid ContextMenu button path");
                auto name = required_attribute(child, "name", object_id);
                if (name.empty()) fail("OOF2003", child, std::string(object_id), property,
                    "non-empty button name", "empty", "ContextMenu button path is empty");
                reference.path.push_back(std::move(name));
            }
            return reference;
        }
        case mm::ValueCodec::action_source_reference: {
            const auto form_id = optional_attribute(node, "formId");
            const auto control_id = optional_attribute(node, "controlId");
            if (form_id && control_id) {
                fail("OOF2003", node, std::string(object_id), property,
                    "at most one of formId or controlId", "both reference attributes",
                    "ActionSource must reference either the form or one control");
            }
            for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
                if (child->type == XML_COMMENT_NODE || child->type == XML_PI_NODE) {
                    continue;
                }
                if ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) &&
                    child->content != nullptr &&
                    trim_ascii(reinterpret_cast<const char*>(child->content)).empty()) {
                    continue;
                }
                fail("OOF2003", child, std::string(object_id), property,
                    "empty ActionSource element", "text or child content",
                    "ActionSource references are carried only by reference attributes");
            }
            if (form_id) return model::FormRef{parse_object_id(*form_id, node, property, object_id)};
            if (control_id) return model::ControlRef{parse_object_id(*control_id, node, property, object_id)};
            return model::UndefinedValue{};
        }
        case mm::ValueCodec::chart_trendlines:
        case mm::ValueCodec::chart_equation_area:
        case mm::ValueCodec::chart_plot_area:
        case mm::ValueCodec::chart_title_area:
        case mm::ValueCodec::chart_legend_area:
        case mm::ValueCodec::chart_series_collection:
        case mm::ValueCodec::chart_points_collection:
        case mm::ValueCodec::chart_gauge_quality_bands:
            fail("OOF2003", node, std::string(object_id), property, "owned Chart collection", "scalar", "Chart collection is not a scalar property");
        case mm::ValueCodec::command_bar_buttons:
            fail("OOF2003", node, std::string(object_id), property, "owned Buttons collection", "scalar", "Buttons is not a scalar property");
        case mm::ValueCodec::dendrogram_items:
            fail("OOF2003", node, std::string(object_id), property, "owned Items collection", "scalar", "Dendrogram Items is not a scalar property");
        case mm::ValueCodec::dendrogram_links:
            fail("OOF2003", node, std::string(object_id), property, "owned Links collection", "scalar", "Dendrogram Links is not a scalar property");
        case mm::ValueCodec::control_reference:
            return model::ControlRef{parse_object_id(node_text(node), node, property, object_id)};
        case mm::ValueCodec::form_reference:
            fail("OOF2003", node, std::string(object_id), property, "runtime-only form reference", "XML", "Runtime form reference cannot be parsed from XML");
        case mm::ValueCodec::attribute_reference:
            return model::AttributeRef{parse_attribute_id(node_text(node), node, property, object_id)};
        case mm::ValueCodec::command_reference:
            return model::CommandRef{parse_object_id(node_text(node), node, property, object_id)};
    }
    fail("OOF2003", node, std::string(object_id), property, "known value codec", "unknown", "Unknown ordinary-form value codec");
}

bool equals_descriptor_default(
    const mm::PropertyDescriptor& descriptor,
    const model::PropertyValue& value
) {
    const std::string_view canonical = descriptor.default_value.canonical;
    switch (descriptor.default_value.kind) {
        case mm::DefaultKind::unknown:
        case mm::DefaultKind::none:
            return false;
        case mm::DefaultKind::undefined:
            return canonical == "undefined" && std::holds_alternative<model::UndefinedValue>(value);
        case mm::DefaultKind::boolean:
            return std::holds_alternative<bool>(value) &&
                   std::get<bool>(value) == (canonical == "true" || canonical == "1");
        case mm::DefaultKind::integer: {
            std::int64_t parsed{};
            const auto [end, error] =
                std::from_chars(canonical.data(), canonical.data() + canonical.size(), parsed);
            return error == std::errc{} && end == canonical.data() + canonical.size() &&
                   std::holds_alternative<std::int64_t>(value) &&
                   std::get<std::int64_t>(value) == parsed;
        }
        case mm::DefaultKind::decimal:
            return std::holds_alternative<model::DecimalValue>(value) &&
                   std::get<model::DecimalValue>(value).canonical == canonical;
        case mm::DefaultKind::string:
            return (std::holds_alternative<std::string>(value) &&
                    std::get<std::string>(value) == canonical) ||
                   (descriptor.value_codec == mm::ValueCodec::localized_text && canonical.empty() &&
                    std::holds_alternative<model::LocalizedStringValue>(value) &&
                    std::get<model::LocalizedStringValue>(value).items.empty());
        case mm::DefaultKind::enumeration: {
            if (!std::holds_alternative<model::EnumerationValue>(value)) return false;
            const auto& enumeration = std::get<model::EnumerationValue>(value);
            const auto separator = canonical.find('.');
            const auto expected_type = separator == std::string_view::npos
                ? descriptor.api_name : canonical.substr(0, separator);
            const auto expected_member = separator == std::string_view::npos
                ? canonical : canonical.substr(separator + 1);
            return enumeration.type_name == expected_type && enumeration.member == expected_member;
        }
        case mm::DefaultKind::color: {
            if (!std::holds_alternative<model::ColorValue>(value)) return false;
            const auto& color = std::get<model::ColorValue>(value);
            const bool default_channels = color.red == 0 && color.green == 0 &&
                color.blue == 0 && color.alpha == 255;
            if (!default_channels) return false;
            if (canonical == "automatic") {
                return color.kind == model::ColorKind::automatic &&
                    std::holds_alternative<std::monostate>(color.style);
            }
            const auto* style = std::get_if<model::QualifiedName>(&color.style);
            return color.kind == model::ColorKind::style_reference && style != nullptr &&
                style->value == canonical;
        }
        case mm::DefaultKind::font: {
            if (!std::holds_alternative<model::FontValue>(value)) return false;
            model::FontValue expected;
            if (canonical == "automatic,bold=true") expected.bold = true;
            else if (canonical != "automatic") return false;
            return std::get<model::FontValue>(value) == expected;
        }
        case mm::DefaultKind::border: {
            if (!std::holds_alternative<model::BorderValue>(value)) return false;
            model::BorderValue expected;
            if (canonical == "StyleBorders.ControlBorder") {
                expected.kind = model::BorderKind::style_reference;
                expected.style = model::QualifiedName{std::string(canonical)};
            } else if (canonical == "Rounded:1") {
                expected.border_type = model::ControlBorderType::rounded;
                expected.width = 1;
            } else if (canonical != "WithoutBorder:0") return false;
            return std::get<model::BorderValue>(value) == expected;
        }
        case mm::DefaultKind::shortcut:
            return canonical == "None" && std::holds_alternative<model::ShortcutValue>(value) &&
                std::get<model::ShortcutValue>(value) == model::ShortcutValue{};
        case mm::DefaultKind::style:
            return canonical == "auto" && std::holds_alternative<model::StyleValue>(value) &&
                std::get<model::StyleValue>(value) == model::StyleValue{};
    }
    return false;
}

bool valid_table_column_editor_property(
    model::ControlKind kind,
    const mm::PropertyDescriptor& descriptor,
    const model::PropertyValue& value) {
    if (descriptor.api_name == "Enabled") {
        return std::holds_alternative<bool>(value) &&
            (kind == model::ControlKind::input_field || std::get<bool>(value));
    }
    if (kind == model::ControlKind::input_field &&
        std::ranges::find(mm::table_column_input_properties, descriptor.api_name) !=
            mm::table_column_input_properties.end())
        return descriptor.api_name == "BorderColor" ? std::holds_alternative<model::ColorValue>(value) : std::holds_alternative<bool>(value);
    if (kind == model::ControlKind::input_field) return false;
    return equals_descriptor_default(descriptor, value);
}

model::ControlPayload make_payload(model::ControlKind kind) {
    switch (kind) {
        case model::ControlKind::panel: return model::PanelPayload{};
        case model::ControlKind::command_bar: return model::CommandBarPayload{};
        case model::ControlKind::button: return model::ButtonPayload{};
        case model::ControlKind::picture_decoration: return model::PictureDecorationPayload{};
        case model::ControlKind::check_box: return model::CheckBoxPayload{};
        case model::ControlKind::choice_field: return model::ChoiceFieldPayload{};
        case model::ControlKind::radio_button: return model::RadioButtonPayload{};
        case model::ControlKind::input_field: return model::InputFieldPayload{};
        case model::ControlKind::usual_group: return model::UsualGroupPayload{};
        case model::ControlKind::splitter: return model::SplitterPayload{};
        case model::ControlKind::chart: return model::ChartPayload{};
        case model::ControlKind::pivot_chart: return model::PivotChartPayload{};
        case model::ControlKind::gantt_chart: return model::GanttChartPayload{};
        case model::ControlKind::dendrogram: return model::DendrogramPayload{};
        case model::ControlKind::html_document_field: return model::HtmlDocumentFieldPayload{};
        case model::ControlKind::list_box: return model::ListBoxPayload{};
        case model::ControlKind::progress_bar: return model::ProgressBarPayload{};
        case model::ControlKind::track_bar: return model::TrackBarPayload{};
        case model::ControlKind::calendar_field: return model::CalendarFieldPayload{};
        case model::ControlKind::text_document_field: return model::TextDocumentFieldPayload{};
        case model::ControlKind::geographical_schema_field: return model::GeographicalSchemaFieldPayload{};
        case model::ControlKind::graphical_schema_field: return model::GraphicalSchemaFieldPayload{};
        case model::ControlKind::table: return model::TablePayload{};
        case model::ControlKind::spreadsheet_document_field: return model::SpreadsheetDocumentFieldPayload{};
        case model::ControlKind::label_decoration: return model::LabelDecorationPayload{};
        case model::ControlKind::active_x_control: return model::ActiveXControlPayload{};
        case model::ControlKind::count: break;
    }
    throw std::logic_error("unknown ordinary-form control kind");
}

model::BindingCoordinate parse_binding_coordinate(std::string_view value, xmlNodePtr node) {
    static constexpr std::array<std::pair<std::string_view, model::BindingCoordinate>, 6> values{{
        {"left", model::BindingCoordinate::left},
        {"top", model::BindingCoordinate::top},
        {"right", model::BindingCoordinate::right},
        {"bottom", model::BindingCoordinate::bottom},
        {"verticalCenter", model::BindingCoordinate::vertical_center},
        {"horizontalCenter", model::BindingCoordinate::horizontal_center},
    }};
    for (const auto& [name, coordinate] : values) {
        if (name == value) {
            return coordinate;
        }
    }
    fail("OOF2003", node, {}, "coordinate", "known binding coordinate", std::string(value), "Unknown binding coordinate");
}

model::BindingDimension parse_binding_dimension(std::string_view value, xmlNodePtr node) {
    static constexpr std::array<std::pair<std::string_view, model::BindingDimension>, 5> values{{
        {"width", model::BindingDimension::width},
        {"height", model::BindingDimension::height},
        {"minimumWidth", model::BindingDimension::minimum_width},
        {"minimumHeight", model::BindingDimension::minimum_height},
        {"stretch", model::BindingDimension::stretch},
    }};
    for (const auto& [name, dimension] : values) {
        if (name == value) {
            return dimension;
        }
    }
    fail("OOF2003", node, {}, "dimension", "known binding dimension", std::string(value), "Unknown binding dimension");
}

model::PictureFormat parse_picture_format(std::string_view value, xmlNodePtr node) {
    if (value == "gif") return model::PictureFormat::gif;
    if (value == "png") return model::PictureFormat::png;
    if (value == "jpeg") return model::PictureFormat::jpeg;
    if (value == "bmp") return model::PictureFormat::bmp;
    if (value == "ico") return model::PictureFormat::ico;
    fail("OOF2003", node, {}, "format", "gif|png|jpeg|bmp|ico", std::string(value), "Unknown picture format");
}

std::string_view picture_format_name(model::PictureFormat format) {
    switch (format) {
        case model::PictureFormat::gif: return "gif";
        case model::PictureFormat::png: return "png";
        case model::PictureFormat::jpeg: return "jpeg";
        case model::PictureFormat::bmp: return "bmp";
        case model::PictureFormat::ico: return "ico";
    }
    return {};
}

bool valid_picture_path(std::string_view path, model::PictureFormat format) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos) {
        return false;
    }
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto separator = path.find('/', begin);
        const auto part = path.substr(begin, separator == std::string_view::npos ? path.size() - begin : separator - begin);
        if (part.empty() || part == "." || part == "..") return false;
        parts.push_back(part);
        if (separator == std::string_view::npos) break;
        begin = separator + 1;
    }
    if (parts.size() == 1) {
        switch (format) {
            case model::PictureFormat::gif: return path == "CaptionPicture.gif";
            case model::PictureFormat::png: return path == "CaptionPicture.png";
            case model::PictureFormat::jpeg: return path == "CaptionPicture.jpg" || path == "CaptionPicture.jpeg";
            case model::PictureFormat::bmp: return path == "CaptionPicture.bmp";
            case model::PictureFormat::ico: return path == "CaptionPicture.ico";
        }
    }
    if (parts.size() < 3 || parts.size() % 2 == 0 || parts[0] != "Items") return false;
    if (!(parts.size() == 5 && parts[2] == "Document"))
        for (std::size_t index = 2; index + 1 < parts.size(); index += 2) {
            if (parts[index] != "Buttons") return false;
        }
    std::string_view file = parts.back();
    if (parts.size() == 3 && file.starts_with("ChoiceButtonPicture.")) {
        file.remove_prefix(std::string_view("ChoiceButton").size());
    }
    switch (format) {
        case model::PictureFormat::gif: return file == "Picture.gif";
        case model::PictureFormat::png: return file == "Picture.png";
        case model::PictureFormat::jpeg:
            return file == "Picture.jpg" || file == "Picture.jpeg";
        case model::PictureFormat::bmp: return file == "Picture.bmp";
        case model::PictureFormat::ico: return file == "Picture.ico";
    }
    return false;
}

model::ChartTrendline parse_chart_trendline(xmlNodePtr node, model::ObjectId id, std::string_view owner, model::TypedValueBudget& typed_budget) {
    if (node->properties) fail("OOF2003", node, std::string(owner), "Trendline", "ordered named Trendline without attributes", {}, "Trendline does not expose runtime identity attributes");
    model::ChartTrendline trend; trend.id = id;
    std::set<std::string> seen;
    for (auto* child : element_children(node)) {
        const auto name = node_name(child);
        const auto fields = mm::chart_trendline_property_descriptors();
        const auto field = std::ranges::find(fields, name, &mm::ChartSeriesPropertyDescriptor::name);
        if (field == fields.end() || !seen.insert(name).second) fail("OOF2003", child, std::string(owner), name, "unique named Trendline property", {}, "Invalid Trendline property");
        if (field->value_codec == mm::ValueCodec::chart_equation_area) {
            if (child->properties) fail("OOF2003", child, std::string(owner), name, "EquationArea without attributes", {}, "EquationArea has no identity attributes");
            std::set<std::string> equation_seen;
            for (auto* property : element_children(child)) {
                const auto property_name = node_name(property);
                const auto equation_fields = mm::chart_label_area_property_descriptors();
                const auto definition = std::ranges::find(equation_fields, property_name, &mm::ChartScalePropertyDescriptor::name);
                if (definition == equation_fields.end() || !equation_seen.insert(property_name).second) fail("OOF2003", property, std::string(owner), property_name, "unique named EquationArea property", {}, "Invalid EquationArea property");
                trend.equation_area.set_explicit(definition->id, parse_property_value(property, definition->value_codec, owner, typed_budget));
            }
        } else trend.properties.set_explicit(field->id, parse_property_value(child, field->value_codec, owner, typed_budget));
    }
    return trend;
}
void consume_chart_budget(std::size_t& budget, std::size_t count, xmlNodePtr node, std::string_view owner) {
    if (count > model::max_chart_collection_items - budget)
        fail("OOF2003", node, std::string(owner), "Chart", "bounded document Chart collections", {}, "Chart collections exceed the document resource budget");
    budget += count;
}
void parse_chart_series_fields(xmlNodePtr node, mm::ChartSeriesObject object, std::string_view owner, std::size_t& budget, model::TypedValueBudget& typed_budget) {
    std::set<std::string> seen;
    for (auto* child : element_children(node)) {
        const auto name = node_name(child);
        const auto fields = mm::chart_series_property_descriptors();
        const auto field = std::ranges::find(fields, name, &mm::ChartSeriesPropertyDescriptor::name);
        if (field == fields.end() || !seen.insert(name).second) fail("OOF2003", child, std::string(owner), name, "unique named Series property", {}, "Invalid Series property");
        if (field->normal_runtime_only && std::holds_alternative<model::ChartSeries*>(object))
            fail("OOF2003", child, std::string(owner), name, "persisted Series property", {}, "Runtime-only ordinary Series property cannot appear in source XML");
        if (field->value_codec == mm::ValueCodec::chart_trendlines) {
            if (child->properties) fail("OOF2003", child, std::string(owner), name, "Trendlines without attributes", {}, "Trendlines has no identity attributes");
            std::visit([&](auto* item) {
                for (auto* element : element_children(child)) {
                    if (node_name(element) != "Trendline") fail("OOF2003", element, std::string(owner), "Trendlines", "named Trendline", {}, "Invalid Trendlines element");
                    if (item->trendlines.size() >= model::max_chart_collection_items) fail("OOF2003", element, std::string(owner), name, "bounded Trendlines collection", {}, "Trendlines exceeds supported collection size");
                    consume_chart_budget(budget, 1, element, owner);
                    const auto id = model::ObjectId{item->next_trendline_id++};
                    item->trendlines.push_back(parse_chart_trendline(element, id, owner, typed_budget));
                }
            }, object);
        } else {
            if (field->storage == mm::ChartSeriesStorage::computed) fail("OOF2003", child, std::string(owner), name, "derived Series property", {}, "Computed Series properties are reconstructed from the named owner and GraphicalRepresentationType");
            mm::set_chart_series_property_value(object, *field, parse_property_value(child, field->value_codec, owner, typed_budget));
        }
    }
}

struct ParsedObjects {
    std::vector<model::ControlNode> controls;
    std::vector<model::Page> pages;
    std::vector<model::Attribute> attributes;
    std::vector<model::Command> commands;
    std::vector<model::Event> events;
    std::vector<model::PictureAsset> assets;
};

class DocumentParser {
public:
    explicit DocumentParser(const mm::Metamodel& metamodel,
        std::shared_ptr<const MetadataContext> metadata_context)
        : metamodel_(metamodel), metadata_context_(std::move(metadata_context)) {}

    model::OrdinaryFormDocument parse(xmlNodePtr root) {
        model::Form form;
        form.id = parse_object_id(required_attribute(root, "id"), root);
        form.name = required_attribute(root, "name", object_id_text(form.id));
        bool reconstruction_complete = true;
        if (const auto value = optional_attribute(root, "reconstructionComplete")) {
            reconstruction_complete = parse_boolean(
                *value, root, "reconstructionComplete", object_id_text(form.id));
        }
        const std::string form_id = object_id_text(form.id);
        bool form_extension_seen = false;

        for (xmlNodePtr child : element_children(root)) {
            const std::string name = node_name(child);
            if (name == "MainAttribute") {
                form.main_attribute = model::AttributeRef{parse_attribute_id(required_attribute(child, "attributeId"), child)};
            } else if (const auto extension = std::ranges::find(mm::form_extensions, name,
                &mm::FormExtensionDescriptor::xml_name); extension != mm::form_extensions.end()) {
                if (form_extension_seen)
                    fail("OOF2003", child, form_id, name, "one form extension", "duplicate extension",
                        "Form extension tags are mutually exclusive");
                form_extension_seen = true;
                form.extension = extension->kind;
                std::set<model::PropertyId> seen_extension_properties;
                bool saved_values_seen = false;
                for (xmlNodePtr property_node : element_children(child)) {
                    const auto property_name = node_name(property_node);
                    if (property_name == "SavedValues" &&
                        (extension->kind == model::FormExtensionKind::data_processor ||
                         extension->kind == model::FormExtensionKind::report)) {
                        if (saved_values_seen)
                            fail("OOF2003", property_node, form_id, property_name, "one SavedValues collection",
                                "duplicate", "Duplicate SavedValues collection");
                        saved_values_seen = true;
                        for (auto* binding_node : element_children(property_node)) {
                            if (form.saved_values.size() >= model::max_saved_value_bindings)
                                fail("OOF2003", binding_node, form_id, property_name, "bounded SavedValues collection",
                                    {}, "SavedValues exceeds the document resource budget");
                            model::SavedValueBinding binding;
                            binding.source_id = model::UuidValue{canonical_uuid(required_attribute(binding_node, "sourceId"))};
                            binding.name = required_attribute(binding_node, "name");
                            if (const auto attribute = optional_attribute(binding_node, "attributeId"))
                                binding.attribute = model::AttributeRef{parse_attribute_id(*attribute, binding_node)};
                            form.saved_values.push_back(std::move(binding));
                        }
                        continue;
                    }
                    const auto* descriptor = metamodel_.form_extension_property(*form.extension, property_name);
                    if (descriptor == nullptr || descriptor->extension_kind != form.extension)
                        fail("OOF2003", property_node, form_id, property_name,
                            "declared form extension property", property_name,
                            "Form extension property is not declared by the executable metamodel");
                    if (!seen_extension_properties.insert(descriptor->id).second)
                        fail("OOF2003", property_node, form_id, property_name,
                            "one form extension property", property_name, "Duplicate form extension property");
                    auto value = parse_property_value(property_node, descriptor->value_codec, form_id, typed_budget_);
                    if (!equals_descriptor_default(*descriptor, value))
                        form.extension_properties.set_explicit(descriptor->id, std::move(value));
                }
            } else if (name == "Panel") {
                for (xmlNodePtr property : element_children(child)) {
                    const auto property_name = node_name(property);
                    const auto* descriptor = metamodel_.root_panel_property(property_name);
                    if (descriptor == nullptr || descriptor->surface != mm::PropertySurface::control_payload ||
                        descriptor->persistence != mm::PersistenceClass::persisted_editable)
                        fail("OOF2003", property, form_id, property_name, "supported main Panel property", property_name, "Unsupported main Panel property");
                    auto value = parse_property_value(property, descriptor->value_codec, form_id, typed_budget_);
                    if (!equals_descriptor_default(*descriptor, value))
                        form.panel.properties.set_explicit(descriptor->id, std::move(value));
                }
            } else if (name == "Events") {
                form.events = parse_form_events(child, form.id);
            } else if (name == "Attributes") {
                parse_attributes(child);
            } else if (name == "Commands") {
                parse_commands(child);
            } else if (name == "PictureAssets") {
                parse_picture_assets(child);
            } else if (name == "ChildItems") {
                form.children = parse_child_items(child);
            } else {
                const mm::PropertyDescriptor* descriptor = metamodel_.form_property(name);
                if (descriptor == nullptr || descriptor->surface != mm::PropertySurface::form) {
                    fail("OOF2003", child, form_id, name, "declared Form property", name, "Form property is not declared by the executable metamodel");
                }
                if (descriptor->persistence == mm::PersistenceClass::runtime_only) {
                    fail("OOF2003", child, form_id, name, "persisted Form property", name,
                        "Runtime-only Form properties cannot appear in public Form.xml");
                }
                model::PropertyValue value =
                    parse_property_value(child, descriptor->value_codec, form_id, typed_budget_);
                if (!equals_descriptor_default(*descriptor, value)) {
                    form.properties.set_explicit(descriptor->id, std::move(value));
                }
            }
        }

        model::OrdinaryFormDocument document(std::move(form), metadata_context_);
        document.set_reconstruction_complete(reconstruction_complete);
        for (auto& asset : objects_.assets) document.add_asset(std::move(asset));
        for (auto& attribute : objects_.attributes) document.add_attribute(std::move(attribute));
        for (auto& command : objects_.commands) document.add_command(std::move(command));
        for (auto& event : objects_.events) document.add_event(std::move(event));
        for (auto& control : objects_.controls) document.add_control(std::move(control));
        for (auto& page : objects_.pages) document.add_page(std::move(page));
        return document;
    }

private:
    std::uint64_t next_page_id_{1};

    model::StandardMenuAction parse_standard_action(xmlNodePtr child, std::string_view owner) {
        for (xmlAttrPtr attr = child->properties; attr != nullptr; attr = attr->next) {
            const std::string_view attr_name(reinterpret_cast<const char*>(attr->name));
            if (attr_name != "command" && attr_name != "context" && attr_name != "commandBarId" && attr_name != "source" && attr_name != "sourceControlId") {
                fail("OOF2003", child, std::string(owner), std::string(attr_name),
                    "command, context, commandBarId, source, and sourceControlId attributes", std::string(attr_name), "Unsupported StandardAction attribute");
            }
        }
        model::StandardMenuAction action;
        const auto command = required_attribute(child, "command", owner);
        const auto* descriptor = mm::find_standard_menu_command(command);
        if (descriptor == nullptr)
            fail("OOF2003", child, std::string(owner), "command", "registered standard menu command", command, "Unknown standard menu command");
        action.command = descriptor->kind;
        const auto context = required_attribute(child, "context", owner);
        const auto command_bar_id = optional_attribute(child, "commandBarId");
        if (context == "CommandBar") {
            action.context = model::StandardMenuActionContext::command_bar;
            action.command_bar = model::ControlRef{parse_object_id(required_attribute(child, "commandBarId", owner), child, "commandBarId", owner)};
        } else if (context == "Default" || context == "Form") {
            action.context = context == "Form" ? model::StandardMenuActionContext::form : model::StandardMenuActionContext::default_context;
            if (command_bar_id)
                fail("OOF2003", child, std::string(owner), "commandBarId", "absent for Default context", *command_bar_id, "Default context cannot have commandBarId");
        } else fail("OOF2003", child, std::string(owner), "context", "Default or CommandBar", context, "Unknown standard menu action context");
        const auto source = required_attribute(child, "source", owner);
        const auto source_control_id = optional_attribute(child, "sourceControlId");
        if (source == "Form") action.source = model::StandardMenuActionSource::form;
        else if (source == "AllSources") action.source = model::StandardMenuActionSource::all_sources;
        else if (source == "Control") {
            action.source = model::StandardMenuActionSource::control;
            const auto id = parse_object_id(required_attribute(child, "sourceControlId", owner), child, "sourceControlId", owner);
            if (id.value() == 0 || id.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
                fail("OOF2003", child, std::string(owner), "sourceControlId", "positive int32 control reference", std::to_string(id.value()), "Standard action source reference is invalid");
            action.source_control = model::ControlRef{id};
        } else fail("OOF2003", child, std::string(owner), "source", "Form, AllSources, or Control", source, "Unknown standard menu action source");
        if (source != "Control" && source_control_id)
            fail("OOF2003", child, std::string(owner), "sourceControlId", "absent unless source is Control", *source_control_id, "Standard action source attributes are inconsistent");
        return action;
    }

    std::tuple<std::string, std::optional<model::EventActionMetadata>, std::optional<model::StandardMenuAction>> parse_event_content(xmlNodePtr node) {
        std::string handler;
        std::optional<model::EventActionMetadata> metadata;
        std::optional<model::StandardMenuAction> standard;
        for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
            if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) {
                const std::string text = node_text(child);
                if (!metadata && !standard) {
                    handler += text;
                } else if (text.find_first_not_of(" \t\r\n") != std::string::npos) {
                    fail("OOF2003", child, {}, "handler", "only XML whitespace after Action", text,
                        "Event handler text cannot follow Action");
                }
            } else if (child->type == XML_ELEMENT_NODE) {
                if (node_name(child) == "StandardAction" && !metadata && !standard &&
                    handler.find_first_not_of(" \t\r\n") == std::string::npos) {
                    handler.clear();
                    standard = parse_standard_action(child, "Event");
                    continue;
                }
                if (node_name(child) != "Action" || metadata || standard) {
                    fail("OOF2003", child, {}, node_name(child), "at most one Action", node_name(child), "Invalid event child");
                }
                model::EventActionMetadata action;
                action.name = required_attribute(child, "name");
                std::set<std::string> fields;
                for (xmlNodePtr field : element_children(child)) {
                    const std::string name = node_name(field);
                    if (!fields.insert(name).second) {
                        fail("OOF2003", field, {}, name, "one Action field", name, "Duplicate Action field");
                    }
                    std::size_t item_count = 0;
                    for (xmlNodePtr item = field->children; item != nullptr; item = item->next) {
                        if (item->type == XML_ELEMENT_NODE && ++item_count > model::max_localized_text_items) {
                            fail("OOF2003", field, {}, name, "at most 65536 translations", "too many translations",
                                "Event action exceeds the translation limit");
                        }
                    }
                    if (name == "Text") action.text = parse_localized_string(field);
                    else if (name == "ToolTip") action.tooltip = parse_localized_string(field);
                    else if (name == "Description") action.description = parse_localized_string(field);
                    else fail("OOF2003", field, {}, name, "Text, ToolTip, or Description", name, "Unknown Action field");
                }
                if (fields.size() != 3) {
                    fail("OOF2003", child, {}, "Action", "Text, ToolTip, and Description", "incomplete", "Action requires all localized values");
                }
                metadata = std::move(action);
            }
        }
        if (handler.empty() && !standard) {
            fail("OOF2003", node, {}, "handler", "nonempty handler", "empty", "Event handler must not be empty");
        }
        return {std::move(handler), std::move(metadata), std::move(standard)};
    }

    std::vector<model::EventRef> parse_form_events(xmlNodePtr node, model::ObjectId owner) {
        std::vector<model::EventRef> references;
        for (xmlNodePtr event_node : element_children(node)) {
            const std::string name = node_name(event_node);
            const mm::EventDescriptor* descriptor = metamodel_.form_event(name);
            if (descriptor == nullptr) {
                fail("OOF2003", event_node, object_id_text(owner), name, "declared Form event", name, "Form event is not declared by the executable metamodel");
            }
            const model::ObjectId id = parse_object_id(
                required_attribute(event_node, "id"),
                event_node,
                "id",
                object_id_text(owner));
            auto [handler, metadata, standard] = parse_event_content(event_node);
            objects_.events.push_back({id, std::string(descriptor->api_name), std::move(handler),
                model::FormRef{owner}, std::move(metadata), std::move(standard)});
            references.push_back(model::EventRef{id});
        }
        return references;
    }

    std::vector<model::EventRef> parse_control_events(
        xmlNodePtr node,
        model::ObjectId owner,
        std::span<const mm::EventDescriptor> descriptors,
        std::optional<std::string_view> column = std::nullopt
    ) {
        std::vector<model::EventRef> references;
        for (xmlNodePtr event_node : element_children(node)) {
            const std::string name = node_name(event_node);
            const auto found = std::ranges::find(descriptors, name, &mm::EventDescriptor::xml_name);
            const mm::EventDescriptor* descriptor = found == descriptors.end() ? nullptr : &*found;
            if (descriptor == nullptr) {
                fail("OOF2003", event_node, object_id_text(owner), name, "declared control event", name, "Control event is not declared by the executable metamodel");
            }
            const model::ObjectId id = parse_object_id(
                required_attribute(event_node, "id"),
                event_node,
                "id",
                object_id_text(owner));
            auto [handler, metadata, standard] = parse_event_content(event_node);
            const model::EventOwner event_owner = column
                ? model::EventOwner{model::TableColumnControlRef{model::ControlRef{owner}, std::string(*column)}}
                : model::EventOwner{model::ControlRef{owner}};
            objects_.events.push_back({id, std::string(descriptor->api_name), std::move(handler),
                event_owner, std::move(metadata), std::move(standard)});
            references.push_back(model::EventRef{id});
        }
        return references;
    }

    void parse_attributes(xmlNodePtr node) {
        for (xmlNodePtr attribute_node : element_children(node)) {
            model::Attribute attribute;
            attribute.id = parse_attribute_id(required_attribute(attribute_node, "id"), attribute_node);
            const std::string id = object_id_text(attribute.id);
            attribute.name = required_attribute(attribute_node, "name", id);
            for (xmlNodePtr child : element_children(attribute_node)) {
                const std::string name = node_name(child);
                if (name == "TypeDomain") {
                    attribute.type = parse_type_domain(child);
                } else if (name == "Main") {
                    const bool value = parse_boolean(node_text(child), child, name, id);
                    if (value) attribute.main.set(true);
                } else if (name == "StoredData") {
                    const bool value = parse_boolean(node_text(child), child, name, id);
                    if (value) attribute.stored_data.set(true);
                }
            }
            objects_.attributes.push_back(std::move(attribute));
        }
    }

    void parse_commands(xmlNodePtr node) {
        for (xmlNodePtr command_node : element_children(node)) {
            model::Command command;
            command.id = parse_object_id(required_attribute(command_node, "id"), command_node);
            const std::string id = object_id_text(command.id);
            command.name = required_attribute(command_node, "name", id);
            command.handler = required_attribute(command_node, "handler", id);
            for (xmlNodePtr child : element_children(command_node)) {
                const std::string name = node_name(child);
                if (name == "Title") {
                    model::LocalizedStringValue value = parse_localized_string(child);
                    if (!value.items.empty()) command.title.set(std::move(value));
                } else if (name == "ChangesData") {
                    const bool value = parse_boolean(node_text(child), child, name, id);
                    if (value) command.changes_data.set(true);
                } else if (name == "Picture") {
                    command.picture.set(parse_picture_reference(child, name, id));
                }
            }
            objects_.commands.push_back(std::move(command));
        }
    }

    void parse_picture_assets(xmlNodePtr node) {
        for (xmlNodePtr asset_node : element_children(node)) {
            model::PictureAsset asset;
            asset.id = parse_object_id(required_attribute(asset_node, "id"), asset_node);
            const std::string id = object_id_text(asset.id);
            asset.relative_path = required_attribute(asset_node, "relativePath", id);
            asset.format = parse_picture_format(required_attribute(asset_node, "format", id), asset_node);
            if (const auto transparent = optional_attribute(asset_node, "transparent")) {
                asset.transparent = parse_boolean(*transparent, asset_node, "transparent", id);
            }
            if (const auto x = optional_attribute(asset_node, "transparentX"))
                asset.transparent_x = parse_integer<std::int64_t>(*x, asset_node, "transparentX", id);
            if (const auto y = optional_attribute(asset_node, "transparentY"))
                asset.transparent_y = parse_integer<std::int64_t>(*y, asset_node, "transparentY", id);
            if (asset.transparent_x < -1 || asset.transparent_y < -1)
                fail("OOF2003", asset_node, id, "transparentX/transparentY", "coordinates at least -1", {}, "Invalid picture transparency coordinates");
            if (const auto uuid = optional_attribute(asset_node, "uuid"))
                asset.uuid = model::UuidValue{canonical_uuid(*uuid)};
            if (!valid_picture_path(asset.relative_path, asset.format)) {
                fail(
                    "OOF2003",
                    asset_node,
                    id,
                    "relativePath",
                    "CaptionPicture.<matching format> or Items/<ElementName>[/Buttons/<ItemName>]/Picture.<matching format> or Items/<ElementName>/ChoiceButtonPicture.<matching format> or Items/<ElementName>/Document/<PictureName>/Picture.<matching format>",
                    asset.relative_path,
                    "Picture asset path is outside the ordinary-form source package contract");
            }
            objects_.assets.push_back(std::move(asset));
        }
    }

    model::DataPathMember parse_metadata_member(xmlNodePtr member, std::string_view owner_id) {
        model::DataPathMember descriptor;
        for (xmlAttrPtr attribute = member->properties; attribute != nullptr; attribute = attribute->next) {
            const std::string_view name(reinterpret_cast<const char*>(attribute->name));
            if (attribute->ns != nullptr || (name != "name" && name != "uuid"))
                fail("OOF2003", member, std::string(owner_id), "Member", "name or uuid", std::string(name),
                    "Data path member contains an unsupported attribute");
        }
        const auto name = optional_attribute(member, "name");
        const auto uuid = optional_attribute(member, "uuid");
        std::string text;
        for (xmlNodePtr child = member->children; child != nullptr; child = child->next) {
            if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE)
                text += node_text(child);
            else if (child->type == XML_ELEMENT_NODE) {
                if (node_name(child) != "TypeDomain" || descriptor.type)
                    fail("OOF2003", child, std::string(owner_id), "Member", "one TypeDomain", node_name(child),
                        "Data path member contains an unsupported or duplicate type descriptor");
                descriptor.type = parse_type_domain(child);
            }
        }
        const std::string_view text_name = trim_ascii(text);
        if (name && !text_name.empty() && *name != text_name)
            fail("OOF2003", member, std::string(owner_id), "Member", "matching text and attribute names", text,
                "Data path member names conflict");
        descriptor.name = name ? *name : std::string(text_name);
        if (name && trim_ascii(*name).empty())
            fail("OOF2003", member, std::string(owner_id), "name", "nonempty member name", *name,
                "Data path member name is empty");
        if (uuid) {
            if (!nonzero_uuid(*uuid))
                fail("OOF2003", member, std::string(owner_id), "uuid", "nonzero UUID", *uuid,
                    "Data path member UUID is malformed or zero");
            descriptor.uuid = model::UuidValue{canonical_uuid(*uuid)};
        }
        if (descriptor.name.empty() && !descriptor.uuid)
            fail("OOF2003", member, std::string(owner_id), "Member", "member name or UUID", "missing",
                "Data path member descriptor has no identity");
        return descriptor;
    }

    model::DataPath parse_data_path(xmlNodePtr node, std::string_view owner_id) {
        model::DataPath path;
        path.attribute = model::AttributeRef{parse_attribute_id(
            required_attribute(node, "attributeId", owner_id),
            node,
            "attributeId",
            owner_id)};
        require_typed_container(node, owner_id, "attributeId");
        for (xmlNodePtr member : element_children(node)) {
            if (node_name(member) != "Member" || path.members.size() >= 128)
                fail("OOF2003", member, std::string(owner_id), "DataPath", "at most 128 Member elements",
                    node_name(member), "Data path contains unsupported content or exceeds its depth limit");
            path.members.push_back(parse_metadata_member(member, owner_id));
        }
        return path;
    }

    model::Bindings parse_bindings(xmlNodePtr node, std::string_view owner_id) {
        model::Bindings bindings;
        if (auto manual = optional_attribute(node, "manualHorizontal")) {
            if (parse_boolean(*manual, node, "manualHorizontal", owner_id)) bindings.manual_horizontal.set(true);
        }
        if (auto manual = optional_attribute(node, "manualVertical")) {
            if (parse_boolean(*manual, node, "manualVertical", owner_id)) bindings.manual_vertical.set(true);
        }
        for (xmlNodePtr child : element_children(node)) {
            if (node_name(child) == "AnchorBinding") {
                model::AnchorBinding binding;
                binding.coordinate = parse_binding_coordinate(
                    required_attribute(child, "coordinate", owner_id), child);
                binding.target_coordinate = parse_binding_coordinate(
                    required_attribute(child, "targetCoordinate", owner_id), child);
                if (auto target = optional_attribute(child, "targetId")) {
                    binding.target = model::ControlRef{
                        parse_object_id(*target, child, "targetId", owner_id)};
                }
                const std::int32_t offset = parse_integer<std::int32_t>(
                    required_attribute(child, "offset", owner_id),
                    child,
                    "offset",
                    owner_id);
                if (offset != 0) binding.offset.set(offset);
                xmlNodePtr proportional = nullptr;
                for (xmlNodePtr nested : element_children(child)) {
                    if (node_name(nested) == "ProportionalBinding") proportional = nested;
                }
                if (proportional != nullptr) {
                    model::AnchorBindingTarget target;
                    target.coordinate = parse_binding_coordinate(
                        required_attribute(proportional, "targetCoordinate", owner_id), proportional);
                    if (auto id = optional_attribute(proportional, "targetId")) {
                        target.target = model::ControlRef{
                            parse_object_id(*id, proportional, "targetId", owner_id)};
                    }
                    const auto target_offset = parse_integer<std::int32_t>(
                        required_attribute(proportional, "offset", owner_id),
                        proportional,
                        "offset",
                        owner_id);
                    if (target_offset != 0) target.offset.set(target_offset);
                    binding.proportional = std::move(target);
                }
                bindings.anchors.push_back(std::move(binding));
            } else if (node_name(child) == "DimensionBinding") {
                model::DimensionBinding binding;
                binding.dimension = parse_binding_dimension(
                    required_attribute(child, "dimension", owner_id), child);
                const std::int32_t value = parse_integer<std::int32_t>(
                    required_attribute(child, "value", owner_id),
                    child,
                    "value",
                    owner_id);
                if (value != 0) binding.value.set(value);
                bindings.dimensions.push_back(std::move(binding));
            }
        }
        return bindings;
    }

    model::Position parse_position(xmlNodePtr node, std::string_view owner_id) {
        model::Position position;
        for (xmlNodePtr child : element_children(node)) {
            const std::string name = node_name(child);
            if (name == "DefaultControl") {
                position.default_control.set(parse_boolean(node_text(child), child, name, owner_id));
            } else if (name == "Top") {
                const auto value = parse_integer<std::int32_t>(node_text(child), child, name, owner_id);
                if (value != 0) position.top.set(value);
            } else if (name == "Visible") {
                const bool value = parse_boolean(node_text(child), child, name, owner_id);
                if (!value) position.visible.set(false);
            } else if (name == "Height") {
                const auto value = parse_integer<std::int32_t>(node_text(child), child, name, owner_id);
                if (value != 0) position.height.set(value);
            } else if (name == "Left") {
                const auto value = parse_integer<std::int32_t>(node_text(child), child, name, owner_id);
                if (value != 0) position.left.set(value);
            } else if (name == "TabOrder") {
                position.tab_order.set(parse_integer<std::int32_t>(node_text(child), child, name, owner_id));
            } else if (name == "ZOrder") {
                position.z_order.set(parse_integer<std::int32_t>(node_text(child), child, name, owner_id));
            } else if (name == "Width") {
                const auto value = parse_integer<std::int32_t>(node_text(child), child, name, owner_id);
                if (value != 0) position.width.set(value);
            } else if (name == "Bindings") {
                position.bindings = parse_bindings(child, owner_id);
            }
        }
        return position;
    }

    model::PageRef parse_page(xmlNodePtr node) {
        model::Page page;
        if (optional_attribute(node, "id").has_value()) {
            fail(
                "OOF2003",
                node,
                {},
                "id",
                "Page without an id attribute",
                "id attribute present",
                "Page identity is assigned internally and must not appear in Form.xml");
        }
        page.id = model::ObjectId{next_page_id_++};
        const std::string id = object_id_text(page.id);
        page.name = required_attribute(node, "name", id);
        for (xmlNodePtr child : element_children(node)) {
            if (node_name(child) == "Title") {
                model::LocalizedStringValue title = parse_localized_string(child);
                if (!title.items.empty()) page.title.set(std::move(title));
            } else if (node_name(child) == "Visible") {
                if (!parse_boolean(node_text(child), child, "Visible", id)) page.visible.set(false);
            } else if (node_name(child) == "Enabled") {
                if (!parse_boolean(node_text(child), child, "Enabled", id)) page.enabled.set(false);
            } else if (node_name(child) == "Position") {
                page.position.set(parse_position(child, id));
            } else if (node_name(child) == "ChildItems") {
                page.children = parse_child_items(child);
            }
        }
        const model::PageRef reference{page.id};
        objects_.pages.push_back(std::move(page));
        return reference;
    }

    struct ChartContentState {
        bool chart_summary_series_seen = false;
        bool chart_values_seen = false;
        bool chart_gauge_quality_bands_seen = false;
    };

    bool parse_chart_content_item(xmlNodePtr child, model::ChartPayload& chart,
        const std::string& id_text, ChartContentState& state) {
        const std::string name = node_name(child);
        if (name == "GaugeChartQualityBands") {
            if (state.chart_gauge_quality_bands_seen)
                fail("OOF2003", child, id_text, name, "one GaugeChartQualityBands collection", "duplicate", "Duplicate GaugeChartQualityBands");
            state.chart_gauge_quality_bands_seen = true;
            if (child->properties != nullptr)
                fail("OOF2003", child, id_text, name, "collection without attributes", "present", "GaugeChartQualityBands does not accept attributes");
            auto& bands = chart.gauge_quality_bands;
            for (xmlNodePtr item : element_children(child)) {
                if (node_name(item) != "GaugeChartQualityBand")
                    fail("OOF2003", item, id_text, node_name(item), "GaugeChartQualityBand", node_name(item), "Unknown GaugeChartQualityBands item");
                if (item->properties != nullptr)
                    fail("OOF2003", item, id_text, "GaugeChartQualityBand", "item without attributes", "present", "GaugeChartQualityBand does not accept attributes");
                consume_chart_budget(chart_budget_, 1, item, id_text);
                const auto fields = element_children(item);
                if (fields.size() != 5 || node_name(fields[0]) != "Begin" || node_name(fields[1]) != "End" ||
                    node_name(fields[2]) != "BackColor" || node_name(fields[3]) != "Text" || node_name(fields[4]) != "ToolTip")
                    fail("OOF2003", item, id_text, "GaugeChartQualityBand", "Begin, End, BackColor, Text, ToolTip in order", "invalid fields", "Invalid GaugeChartQualityBand fields");
                for (xmlNodePtr decimal_node : {fields[0], fields[1]}) {
                    if (decimal_node->properties != nullptr || !element_children(decimal_node).empty())
                        fail("OOF2003", decimal_node, id_text, node_name(decimal_node), "plain decimal", "attributes or child elements", "Gauge quality band boundaries must be plain decimals");
                }
                model::ChartGaugeQualityBand band;
                band.begin = model::DecimalValue{canonical_decimal(node_text(fields[0]), fields[0], "Begin", id_text)};
                band.end = model::DecimalValue{canonical_decimal(node_text(fields[1]), fields[1], "End", id_text)};
                if (!model::valid_chart_gauge_quality_band_range(band.begin, band.end))
                    fail("OOF2003", item, id_text, "Begin/End", "Begin less than or equal to End", band.begin.canonical + ">" + band.end.canonical, "Invalid GaugeChartQualityBand range");
                band.back_color = parse_color(fields[2]);
                band.text = parse_property_value(fields[3], mm::ValueCodec::localized_text, id_text, typed_budget_);
                band.tool_tip = parse_property_value(fields[4], mm::ValueCodec::localized_text, id_text, typed_budget_);
                bands.push_back(std::move(band));
            }
            return true;
        }
        if ((name == "PlotArea" || name == "LegendArea" || name == "TitleArea")) {
            const auto kind = name == "PlotArea" ? model::ChartAreaKind::plot : name == "TitleArea" ? model::ChartAreaKind::title : model::ChartAreaKind::legend;
            auto& area = name == "PlotArea" ? chart.plot_area : name == "TitleArea" ? chart.title_area : chart.legend_area;
            if (area) fail("OOF2003", child, id_text, name, "one chart area", name, "Duplicate Chart area");
            model::ChartArea parsed;
            for (const auto& definition : mm::chart_area_property_descriptors()) {
                if (definition.coordinate != nullptr)
                    parsed.*(definition.coordinate) = model::DecimalValue{std::string(
                        kind == model::ChartAreaKind::plot ? definition.plot_default :
                        kind == model::ChartAreaKind::title ? definition.title_default : definition.legend_default)};
            }
            std::set<std::string> seen;
            for (xmlNodePtr field : element_children(child)) {
                const auto field_name = node_name(field);
                if (!seen.insert(field_name).second)
                    fail("OOF2003", field, id_text, field_name, "one area property", field_name, "Duplicate Chart area property");
                const auto owners = mm::chart_scale_owner_descriptors();
                const auto scale_owner = std::ranges::find(owners, field_name, &mm::ChartScaleOwnerDescriptor::name);
                if (kind == model::ChartAreaKind::plot && scale_owner != owners.end()) {
                    auto& scale = parsed.*(scale_owner->member);
                    std::set<std::string> scale_seen;
                    for (xmlNodePtr value_node : element_children(field)) {
                        const auto value_name = node_name(value_node);
                        if (value_name == "TitleArea") {
                            if (!scale_seen.insert(value_name).second) fail("OOF2003", value_node, id_text, value_name, "one owned TitleArea", {}, "Duplicate ChartScale TitleArea");
                            std::set<std::string> title_seen;
                            for (xmlNodePtr title_node : element_children(value_node)) {
                                const auto title_name = node_name(title_node);
                                const auto fields = mm::chart_label_area_property_descriptors();
                                const auto definition = std::ranges::find(fields, title_name, &mm::ChartScalePropertyDescriptor::name);
                                if (definition == fields.end() || !title_seen.insert(title_name).second)
                                    fail("OOF2003", title_node, id_text, title_name, "unique named ChartLabelArea property", {}, "Invalid ChartLabelArea property");
                                auto value = parse_property_value(title_node, definition->value_codec, id_text, typed_budget_);
                                mm::set_chart_scale_property_value(scale, *definition, std::move(value));
                            }
                            continue;
                        }

                        const auto fields = mm::chart_scale_property_descriptors();
                        const auto definition = std::ranges::find(fields, value_name, &mm::ChartScalePropertyDescriptor::name);
                        if (definition == fields.end() || !scale_seen.insert(value_name).second)
                            fail("OOF2003", value_node, id_text, value_name, "unique named scale field", value_name, "Invalid Chart scale field");
                        model::PropertyValue value;
                        if (definition->value_codec == mm::ValueCodec::line) {
                            const auto children = element_children(value_node);
                            if (children.size() != 1) fail("OOF2003", value_node, id_text, value_name, "one Line or Undefined", {}, "Invalid nullable Line");
                            if (node_name(children[0]) == "Line") value = parse_line(children[0]);
                            else if (node_name(children[0]) == "Undefined" && node_text(children[0]) == "undefined") value = model::UndefinedValue{};
                            else fail("OOF2003", value_node, id_text, value_name, "Line or Undefined", {}, "Invalid nullable Line");
                        } else value = parse_property_value(value_node, definition->value_codec, id_text, typed_budget_);
                        if (const auto* n = std::get_if<model::DecimalValue>(&value); n && definition->numeric_policy == mm::ChartScaleNumericPolicy::nonnegative_decimal && n->canonical.starts_with('-'))
                            fail("OOF2003", value_node, id_text, value_name, "nonnegative decimal", n->canonical, "Negative ScaleStep");
                        if (definition->value_codec == mm::ValueCodec::enumeration) {
                            const auto* enumeration = std::get_if<model::EnumerationValue>(&value);
                            if (!enumeration || enumeration->type_name != definition->enumeration_type ||
                                std::ranges::find(definition->members, enumeration->member) == definition->members.end())
                                fail("OOF2003", value_node, id_text, value_name, "valid scale enumeration", {}, "Invalid Chart scale enumeration");
                        }
                        mm::set_chart_scale_property_value(scale, *definition, std::move(value));
                    }
                } else if (kind == model::ChartAreaKind::title && field_name == "Text") {
                    auto text = parse_property_value(field, mm::ValueCodec::localized_text, id_text, typed_budget_);
                    chart.properties.set_explicit(model::PropertyId::from_name("Title"), std::move(text));
                } else if (field_name == "Placement") {
                    parsed.placement = node_text(field);
                    parsed.explicit_geometry[0] = true;
                    if (!valid_chart_area_placement(name, parsed.placement))
                        fail("OOF2003", field, id_text, field_name, "valid area placement", parsed.placement, "Invalid Chart area placement");
                } else {
                    const auto id = model::PropertyId::from_name(field_name);
                    if (const auto* boolean = mm::find_chart_area_boolean(kind, id)) {
                        parsed.*(boolean->member) = parse_boolean(node_text(field), field, field_name, id_text);
                    } else if (const auto* color = mm::find_chart_area_color(kind, id)) {
                        parsed.*(color->member) = parse_color(field);
                    } else if (const auto* style = mm::find_chart_area_style(kind, id)) {
                        if (style->border) parsed.*(style->border) = parse_border(field);
                        else if (style->font) parsed.*(style->font) = parse_font(field);
                        else parsed.*(style->line) = parse_line(field);
                    } else if (const auto* descriptor = mm::find_chart_area_property(id); descriptor && descriptor->coordinate) {
                        (parsed.*(descriptor->coordinate)).canonical = canonical_decimal(node_text(field), field, field_name, id_text);
                        parsed.explicit_geometry[1 + descriptor->coordinate_offset] = true;
                    } else if (const auto* descriptor = kind == model::ChartAreaKind::plot ? mm::find_chart_plot_format(id) : nullptr) {
                        auto value = parse_property_value(field, descriptor->value_codec, id_text, typed_budget_);
                        if (descriptor->text) parsed.*(descriptor->text) = std::move(value);
                        else {
                            const auto& enumeration = std::get<model::EnumerationValue>(value);
                            if (enumeration.type_name != descriptor->enumeration_type ||
                                std::ranges::find(descriptor->members, enumeration.member) == descriptor->members.end())
                                fail("OOF2003", field, id_text, field_name, "valid PlotArea enumeration", enumeration.member, "Invalid PlotArea enumeration");
                            parsed.*(descriptor->enumeration) = enumeration;
                        }
                    } else fail("OOF2003", field, id_text, field_name, "named Chart area property", field_name, "Unknown Chart area property");
                }
            }
            for (const auto* value : {&parsed.left, &parsed.top, &parsed.right, &parsed.bottom}) {
                if (value->canonical != "0" && value->canonical != "1" && !value->canonical.starts_with("0."))
                    fail("OOF2003", child, id_text, name, "bounds between 0 and 1", value->canonical, "Chart area bound is out of range");
            }
            area = std::move(parsed);
        } else if (name == "SummarySeries") {
            if (state.chart_summary_series_seen) fail("OOF2003", child, id_text, name, "one SummarySeries", name, "Duplicate SummarySeries");
            state.chart_summary_series_seen = true;
            auto& summary = chart.summary_series;
            parse_chart_series_fields(child, mm::ChartSeriesObject{&summary}, id_text, chart_budget_, typed_budget_);
        } else if (name == "Series" || name == "Points" || name == "Values") {
            if (name == "Values") {
                if (state.chart_values_seen)
                    fail("OOF2003", child, id_text, name, "one Values collection", "duplicate", "Duplicate Chart Values");
                state.chart_values_seen = true;
                for (xmlAttrPtr attribute = child->properties; attribute != nullptr; attribute = attribute->next) {
                    const std::string_view attribute_name(reinterpret_cast<const char*>(attribute->name));
                    if (attribute_name != "dataMode" && attribute_name != "sourceDescription")
                        fail("OOF2003", child, id_text, std::string(attribute_name), "dataMode and sourceDescription attributes", std::string(attribute_name), "Unknown Chart Values attribute");
                }
                if (const auto mode = optional_attribute(child, "dataMode")) {
                    if (*mode == "Sample") chart.data_mode = model::ChartDataMode::sample;
                    else if (*mode == "Explicit") chart.data_mode = model::ChartDataMode::explicit_values;
                    else if (*mode == "Source") chart.data_mode = model::ChartDataMode::source;
                    else fail("OOF2003", child, id_text, "dataMode", "Sample, Explicit, or Source", *mode, "Invalid Chart data mode");
                }
                if (const auto description = optional_attribute(child, "sourceDescription")) {
                    if (chart.data_mode != model::ChartDataMode::source)
                        fail("OOF2003", child, id_text, "sourceDescription", "attribute only in Source mode", *description, "Chart sourceDescription requires Source mode");
                    chart.data_source_description = *description;
                }
            }
            if (name == "Series" || name == "Points") {
                if (const auto last = optional_attribute(child, "lastAssignedId")) {
                    const auto id = parse_integer<std::uint64_t>(*last, child, "lastAssignedId", id_text);
                    if (name == "Series") chart.last_series_id = id;
                    else chart.last_point_id = id;
                }
            }
            for (xmlNodePtr item : element_children(child)) {
                if (name == "Series" && node_name(item) == "ChartSeries") {
                    consume_chart_budget(chart_budget_, 1, item, id_text);
                    model::ChartSeries series;
                    series.id = parse_object_id(required_attribute(item, "id"), item);
                    parse_chart_series_fields(item, mm::ChartSeriesObject{&series}, id_text, chart_budget_, typed_budget_);
                    chart.series.push_back(std::move(series));
                } else if (name == "Points" && node_name(item) == "ChartPoint") {
                    consume_chart_budget(chart_budget_, 1, item, id_text);
                    model::ChartPoint point;
                    point.id = parse_object_id(required_attribute(item, "id"), item);
                    for (xmlNodePtr field : element_children(item)) {
                        const std::string field_name = node_name(field);
                        if (field_name == "Text") point.text = node_text(field);
                        else if (field_name == "Color") point.color = parse_color(field);
                        else if (field_name == "Value") point.value = parse_typed_value(field, id_text, typed_budget_);
                        else fail("OOF2003", field, id_text, field_name, "Text or Color", field_name, "Unknown ChartPoint field");
                    }
                    chart.points.push_back(std::move(point));
                } else if (name == "Values" && node_name(item) == "ChartValue") {
                    consume_chart_budget(chart_budget_, 1, item, id_text);
                    if (chart.values.size() >= chart.series.size() * chart.points.size())
                        fail("OOF2003", item, id_text, "ChartValue", "bounded dense Chart matrix", {}, "Chart Values exceed the declared Series by Point matrix");
                    model::ChartValue value;
                    value.series_ref = parse_object_id(required_attribute(item, "seriesRef"), item);
                    value.point_ref = parse_object_id(required_attribute(item, "pointRef"), item);
                    const auto fields = element_children(item);
                    if (fields.empty())
                        fail("OOF2003", item, id_text, "ChartValue", "typed value", "empty", "ChartValue requires a typed value");
                    value.value = parse_typed_node(fields.front(), id_text, typed_budget_);
                    for (std::size_t i = 1; i < fields.size(); ++i) {
                        const auto field_name = node_name(fields[i]);
                        if (field_name == "Details") value.details = parse_typed_value(fields[i], id_text, typed_budget_);
                        else if (field_name == "ToolTip") value.tooltip = node_text(fields[i]);
                        else fail("OOF2003", fields[i], id_text, field_name, "Details or ToolTip", field_name, "Unknown ChartValue field");
                    }
                    chart.values.push_back(std::move(value));
                } else {
                    fail("OOF2003", item, id_text, node_name(item), name == "Series" ? "ChartSeries" : name == "Points" ? "ChartPoint" : "ChartValue", node_name(item), "Unknown Chart collection item");
                }
            }
            if (name == "Values" && chart.data_mode == model::ChartDataMode::source && !chart.values.empty())
                fail("OOF2003", child, id_text, "Values", "no ChartValue cells in Source mode", std::to_string(chart.values.size()), "Source-mode Chart cannot contain dense Values cells");
        } else {
            const auto* property = metamodel_.property(model::ControlKind::chart, name);
            if (!property || property->surface != mm::PropertySurface::control_payload) return false;
            if (name == "Title")
                fail("OOF2003", child, id_text, name, "TitleArea/Text", name, "Chart title text belongs to TitleArea");
            if (property->persistence == mm::PersistenceClass::runtime_only ||
                property->persistence == mm::PersistenceClass::unsupported_by_platform)
                fail("OOF2003", child, id_text, name, "persisted Chart property", name, "Chart property cannot appear in public Form.xml");
            auto value = parse_property_value(child, property->value_codec, id_text, typed_budget_);
            if (!equals_descriptor_default(*property, value)) chart.properties.set_explicit(property->id, std::move(value));
        }
        return true;
    }

    void finish_chart_content(model::ChartPayload& chart) {
        for (const auto [area, property] : {std::pair{&chart.legend_area, "ShowLegend"}, std::pair{&chart.title_area, "ShowTitle"}}) {
            if (!*area || (*area)->explicit_geometry[0]) continue;
            const auto* shown = chart.properties.find(model::PropertyId::from_name(property));
            if (shown != nullptr && std::holds_alternative<bool>(shown->value) && !std::get<bool>(shown->value))
                (*area)->placement = "None";
        }
    }

    model::ChartPayload parse_owned_chart(xmlNodePtr node, const std::string& owner) {
        if (node->properties != nullptr)
            fail("OOF2003", node, owner, "Chart", "aggregate without identity attributes", "attributes", "Owned Chart has no control identity");
        model::ChartPayload chart;
        ChartContentState state;
        for (auto* child : element_children(node)) {
            const auto name = node_name(child);
            if (name == "BackColor" || !parse_chart_content_item(child, chart, owner, state))
                fail("OOF2003", child, owner, name, "owned Chart content", name, "Property does not belong to owned Chart content");
        }
        finish_chart_content(chart);
        return chart;
    }

    model::ControlRef parse_control(xmlNodePtr node) {
        const std::string type_name = node_name(node);
        const mm::ControlDescriptor* descriptor =
            metamodel_.control_by_public_name(type_name);
        if (descriptor == nullptr) {
            fail("OOF2003", node, {}, {}, "one of 26 control types", type_name, "Unknown ordinary-form control type");
        }
        const model::ObjectId id = parse_object_id(required_attribute(node, "id"), node);
        const std::string id_text = object_id_text(id);
        model::ControlNode control{
            id,
            required_attribute(node, "name", id_text),
            make_payload(descriptor->kind),
        };
        if (const auto version = optional_attribute(node, "serializationVersion")) {
            auto* spreadsheet = std::get_if<model::SpreadsheetDocumentFieldPayload>(&control.payload);
            if (spreadsheet == nullptr)
                fail("OOF2003", node, id_text, "serializationVersion", "SpreadsheetDocumentField", type_name,
                    "Версия сериализации допустима только у SpreadsheetDocumentField");
            if (*version == "14") spreadsheet->serialization_version = model::SpreadsheetDocumentFieldSerializationVersion::v14;
            else if (*version == "16") spreadsheet->serialization_version = model::SpreadsheetDocumentFieldSerializationVersion::v16;
            else if (*version == "18") spreadsheet->serialization_version = model::SpreadsheetDocumentFieldSerializationVersion::v18;
            else fail("OOF2003", node, id_text, "serializationVersion", "14, 16 or 18", *version,
                "Версия сериализации SpreadsheetDocumentField не поддерживается");
        }
        auto* dendrogram = std::get_if<model::DendrogramPayload>(&control.payload);
        auto* input_field = std::get_if<model::InputFieldPayload>(&control.payload);
        bool type_link_seen = false;
        bool control_events_seen = false;
        bool position_seen = false;
        ChartContentState chart_state;
        bool gantt_series_seen = false;
        bool gantt_points_seen = false;
        bool gantt_intervals_seen = false;
        bool dendrogram_items_seen = false;
        bool dendrogram_links_seen = false;
        bool spreadsheet_document_seen = false;
        for (xmlNodePtr child : element_children(node)) {
            const std::string name = node_name(child);
            if (auto* chart = std::get_if<model::ChartPayload>(&control.payload);
                chart && parse_chart_content_item(child, *chart, id_text, chart_state)) continue;
            if (name == "Chart" && (descriptor->kind == model::ControlKind::gantt_chart || dendrogram != nullptr)) {
                auto& owned = dendrogram ? dendrogram->chart : std::get<model::GanttChartPayload>(control.payload).chart;
                if (owned) fail("OOF2003", child, id_text, name, "one owned Chart", "duplicate", "Duplicate owned Chart");
                owned = parse_owned_chart(child, id_text);
            }
            else if (name == "Document" && descriptor->kind == model::ControlKind::spreadsheet_document_field) {
                if (spreadsheet_document_seen)
                    fail("OOF2003", child, id_text, "Document", "at most one Document element", "duplicate", "SpreadsheetDocumentField has duplicate Document elements");
                spreadsheet_document_seen = true;
                auto& spreadsheet = std::get<model::SpreadsheetDocumentFieldPayload>(control.payload);
                for (xmlAttrPtr attribute = child->properties; attribute; attribute = attribute->next) {
                    const std::string_view name(reinterpret_cast<const char*>(attribute->name));
                    if (attribute->ns != nullptr || (name != "height" && name != "width"))
                        fail("OOF2003", child, id_text, "Document", "height and width attributes", std::string(name), "Unsupported Spreadsheet Document attribute");
                }
                if (const auto height = optional_attribute(child, "height"))
                    spreadsheet.height = parse_integer<std::uint32_t>(*height, child, "height", id_text);
                if (const auto width = optional_attribute(child, "width"))
                    spreadsheet.width = parse_integer<std::uint32_t>(*width, child, "width", id_text);
                auto& cells = spreadsheet.cells;
                const auto parse_format = [&](xmlNodePtr format_node) {
                    require_typed_container(format_node, id_text);
                    model::SpreadsheetDocumentFormat format;
                    const auto descriptors = mm::spreadsheet_format_properties();
                    for (xmlNodePtr property_node : element_children(format_node)) {
                        const auto name = node_name(property_node);
                        const auto property = std::ranges::find(descriptors, name, &mm::PropertyDescriptor::xml_name);
                        if (property == descriptors.end() || format.properties.find(property->id) != nullptr)
                            fail("OOF2003", property_node, id_text, name, "unique named spreadsheet format property", "unknown or duplicate", "Unsupported spreadsheet format property");
                        if (property->value_codec == mm::ValueCodec::picture && optional_attribute(property_node, "empty")) {
                            if (required_attribute(property_node, "empty", id_text) != "true" ||
                                property_node->properties->next != nullptr || property_node->properties->ns != nullptr ||
                                !element_children(property_node).empty() || !trim_ascii(node_text(property_node)).empty())
                                fail("OOF2003", property_node, id_text, name, "empty=true without another target or content", "inconsistent empty picture", "Explicit empty spreadsheet picture cannot contain another reference");
                            format.properties.set_explicit(property->id, model::PictureRef{});
                            continue;
                        }
                        std::string_view allowed;
                        switch (property->value_codec) {
                            case mm::ValueCodec::color: allowed = "|kind|red|green|blue|alpha|styleName|styleObjectId|styleUuid|"; break;
                            case mm::ValueCodec::font: allowed = "|kind|styleName|styleObjectId|styleUuid|faceName|height|bold|italic|underline|strikeout|scale|scaleOverride|windowsFont|autoCharSet|"; break;
                            case mm::ValueCodec::line: allowed = "|type|member|width|gap|"; break;
                            case mm::ValueCodec::border: allowed = "|kind|borderType|width|styleName|styleObjectId|styleUuid|"; break;
                            case mm::ValueCodec::enumeration: allowed = "|type|member|"; break;
                            case mm::ValueCodec::picture: allowed = "|standardName|commonPicture|transparent|transparentX|transparentY|"; break;
                            default: break;
                        }
                        for (xmlAttrPtr attribute = property_node->properties; attribute; attribute = attribute->next) {
                            const std::string attribute_name(reinterpret_cast<const char*>(attribute->name));
                            if (attribute->ns != nullptr || allowed.find("|" + attribute_name + "|") == std::string_view::npos)
                                fail("OOF2003", property_node, id_text, name, "named property attributes", attribute_name, "Unsupported spreadsheet format attribute");
                        }
                        const bool nested = property->value_codec == mm::ValueCodec::localized_string ||
                            property->value_codec == mm::ValueCodec::type_domain;
                        if (nested) {
                            require_typed_container(property_node, id_text);
                            if (property->value_codec == mm::ValueCodec::localized_string)
                                for (xmlNodePtr item : element_children(property_node)) {
                                    for (xmlAttrPtr attribute = item->properties; attribute; attribute = attribute->next)
                                        if (attribute->ns != nullptr || std::string_view(reinterpret_cast<const char*>(attribute->name)) != "language")
                                            fail("OOF2003", item, id_text, name, "language attribute only", "unsupported", "Unsupported localized spreadsheet format attribute");
                                    if (!element_children(item).empty())
                                        fail("OOF2003", item, id_text, name, "plain localized text", "nested content", "Unsupported localized spreadsheet format content");
                                }
                        } else if (!element_children(property_node).empty() ||
                            (!allowed.empty() && property->value_codec != mm::ValueCodec::picture && !trim_ascii(node_text(property_node)).empty()))
                            fail("OOF2003", property_node, id_text, name, "typed property content", "nested or mixed content", "Unsupported spreadsheet format content");
                        format.properties.set_explicit(property->id,
                            parse_property_value(property_node, property->value_codec, id_text, typed_budget_));
                    }
                    return format;
                };
                std::set<std::pair<std::uint32_t, std::uint32_t>> coordinates;
                std::set<std::uint32_t> row_indices, column_indices;
                bool language_settings_seen = false;
                bool default_format_seen = false, default_column_format_seen = false;
                unsigned document_stage = 0;
                for (xmlNodePtr cell_node : element_children(child)) {
                    const auto item_name = node_name(cell_node);
                    const unsigned item_stage = item_name == "LanguageSettings" ? 0 : item_name == "DefaultFormat" ? 1 :
                        item_name == "DefaultColumnFormat" ? 2 : item_name == "Row" ? 3 : item_name == "Column" ? 4 : item_name == "Cell" ? 5 : 6;
                    if (item_stage == 6 || item_stage < document_stage)
                        fail("OOF2003", cell_node, id_text, item_name, "LanguageSettings, DefaultFormat, DefaultColumnFormat, Row, Column, Cell sequence", "unknown or out of order", "Invalid spreadsheet Document item");
                    document_stage = item_stage;
                    if (item_name == "LanguageSettings") {
                        if (language_settings_seen)
                            fail("OOF2003", cell_node, id_text, item_name, "one LanguageSettings", "duplicate", "Duplicate spreadsheet LanguageSettings");
                        language_settings_seen = true;
                        for (xmlAttrPtr attribute = cell_node->properties; attribute; attribute = attribute->next) {
                            const std::string_view name(reinterpret_cast<const char*>(attribute->name));
                            if (attribute->ns != nullptr || (name != "currentLanguage" && name != "defaultLanguage"))
                                fail("OOF2003", cell_node, id_text, item_name, "currentLanguage and defaultLanguage attributes", std::string(name), "Unsupported spreadsheet LanguageSettings attribute");
                        }
                        model::SpreadsheetLanguageSettings settings;
                        settings.current_language = required_attribute(cell_node, "currentLanguage", id_text);
                        settings.default_language = required_attribute(cell_node, "defaultLanguage", id_text);
                        settings.languages.clear();
                        for (xmlNodePtr entry : element_children(cell_node)) {
                            const auto name = node_name(entry);
                            if (name == "Language") {
                                for (xmlAttrPtr attribute = entry->properties; attribute; attribute = attribute->next) {
                                    const std::string_view name(reinterpret_cast<const char*>(attribute->name));
                                    if (attribute->ns != nullptr || (name != "id" && name != "code" && name != "description"))
                                        fail("OOF2003", entry, id_text, std::string(name), "id, code and description attributes", "unsupported", "Unsupported spreadsheet Language attribute");
                                }
                                if (!element_children(entry).empty() || !trim_ascii(node_text(entry)).empty())
                                    fail("OOF2003", entry, id_text, name, "attributes only", "content", "Spreadsheet Language cannot contain text or children");
                                settings.languages.push_back({required_attribute(entry, "id", id_text),
                                    required_attribute(entry, "code", id_text), optional_attribute(entry, "description").value_or("")});
                            } else fail("OOF2003", entry, id_text, name, "Language", "unknown", "Unknown spreadsheet LanguageSettings item");
                        }
                        for (xmlNodePtr text = cell_node->children; text; text = text->next)
                            if ((text->type == XML_TEXT_NODE || text->type == XML_CDATA_SECTION_NODE) && text->content &&
                                !trim_ascii(reinterpret_cast<const char*>(text->content)).empty())
                                fail("OOF2003", cell_node, id_text, item_name, "named language children", "text", "Spreadsheet LanguageSettings cannot contain direct text");
                        spreadsheet.language_settings = std::move(settings);
                        continue;
                    }
                    if (item_name == "DefaultFormat" || item_name == "DefaultColumnFormat") {
                        auto& seen = item_name == "DefaultFormat" ? default_format_seen : default_column_format_seen;
                        if (seen) fail("OOF2003", cell_node, id_text, item_name, "at most one format", "duplicate", "Duplicate spreadsheet default format");
                        seen = true;
                        (item_name == "DefaultFormat" ? spreadsheet.default_format : spreadsheet.default_column_format) = parse_format(cell_node);
                        continue;
                    }
                    if (item_name == "Row" || item_name == "Column") {
                        require_typed_container(cell_node, id_text, "index");
                        for (xmlAttrPtr attribute = cell_node->properties; attribute; attribute = attribute->next)
                            if (attribute->ns != nullptr || std::string_view(reinterpret_cast<const char*>(attribute->name)) != "index")
                                fail("OOF2003", cell_node, id_text, item_name, "index attribute only", "unsupported attribute", "Unsupported spreadsheet row or column attribute");
                        const auto index = parse_integer<std::uint32_t>(required_attribute(cell_node, "index", id_text), cell_node, "index", id_text);
                        auto& indices = item_name == "Row" ? row_indices : column_indices;
                        if (index == 0 || !indices.insert(index).second)
                            fail("OOF2003", cell_node, id_text, "index", "unique positive uint32", "zero or duplicate", "Invalid spreadsheet row or column index");
                        const auto children = element_children(cell_node);
                        if (children.size() != 1 || node_name(children.front()) != "Format")
                            fail("OOF2003", cell_node, id_text, item_name, "one Format child", "missing or unsupported child", "Spreadsheet row or column requires Format");
                        auto format = parse_format(children.front());
                        if (item_name == "Row") spreadsheet.rows.push_back({index, std::move(format)});
                        else spreadsheet.columns.push_back({index, std::move(format)});
                        continue;
                    }
                    for (xmlAttrPtr attr = cell_node->properties; attr != nullptr; attr = attr->next) {
                        const std::string_view attr_name(reinterpret_cast<const char*>(attr->name));
                        if (attr->ns != nullptr || (attr_name != "row" && attr_name != "column"))
                            fail("OOF2003", cell_node, id_text, std::string(attr_name), "row and column attributes", std::string(attr_name), "Unsupported spreadsheet Cell attribute");
                    }
                    const auto row = parse_integer<std::uint32_t>(required_attribute(cell_node, "row", id_text), cell_node, "row", id_text);
                    const auto column = parse_integer<std::uint32_t>(required_attribute(cell_node, "column", id_text), cell_node, "column", id_text);
                    if (row == 0 || column == 0)
                        fail("OOF2003", cell_node, id_text, "coordinate", "positive uint32 row and column", "zero", "Spreadsheet Cell coordinates are one-based");
                    if (!coordinates.emplace(row, column).second)
                        fail("OOF2003", cell_node, id_text, "coordinate", "unique row and column pair", "duplicate", "Spreadsheet Document contains a duplicate Cell coordinate");
                    bool text_seen = false;
                    bool contains_value_seen = false;
                    bool value_type_seen = false;
                    bool value_seen = false;
                    bool detail_parameter_seen = false;
                    model::PropertyValue text{std::string{}};
                    std::optional<model::TypeDomainPatternValue> value_type;
                    std::optional<model::PropertyValue> typed_value;
                    std::optional<std::string> detail_parameter;
                    std::optional<model::SpreadsheetDocumentCellControl> cell_control;
                    model::SpreadsheetDocumentFormat format;
                    bool format_seen = false;
                    unsigned int content_stage = 0;
                    for (xmlNodePtr value_node : element_children(cell_node)) {
                        const auto name = node_name(value_node);
                        if (name == "Format") {
                            if (format_seen || (!text_seen && !value_type_seen && !detail_parameter_seen))
                                fail("OOF2003", value_node, id_text, name, "one Format after cell content", "duplicate or out of order", "Spreadsheet Cell Format must follow its content");
                            format = parse_format(value_node);
                            format_seen = true;
                            content_stage = 6;
                            continue;
                        }
                        if (name == "Text") {
                            if (text_seen || content_stage != 0)
                                fail("OOF2003", value_node, id_text, "Text", "one standalone Text child", "duplicate or mixed content", "Spreadsheet Cell Text cannot be combined with a typed value");
                            if (value_node->properties != nullptr)
                                fail("OOF2003", value_node, id_text, "Text", "plain or localized text without attributes", "attributes", "Spreadsheet Cell Text does not accept attributes");
                            text_seen = true;
                            content_stage = 1;
                            text = parse_property_value(value_node, mm::ValueCodec::localized_text, id_text, typed_budget_);
                            continue;
                        }
                        if (name == "ContainsValue") {
                            if (contains_value_seen || content_stage != 0)
                                fail("OOF2003", value_node, id_text, "ContainsValue", "first typed-cell child", "duplicate or out of order", "Spreadsheet Cell typed children are out of order");
                            if (value_node->properties != nullptr || !element_children(value_node).empty())
                                fail("OOF2003", value_node, id_text, "ContainsValue", "plain Boolean", "structured content", "Spreadsheet Cell ContainsValue must be plain text");
                            if (!parse_boolean(node_text(value_node), value_node, "ContainsValue", id_text))
                                fail("OOF2003", value_node, id_text, "ContainsValue", "true for typed cells", "false", "Text cells omit ContainsValue");
                            contains_value_seen = true;
                            content_stage = 2;
                            continue;
                        }
                        if (name == "ValueType") {
                            if (!contains_value_seen || value_type_seen || content_stage != 2)
                                fail("OOF2003", value_node, id_text, "ValueType", "after ContainsValue", "duplicate or out of order", "Spreadsheet Cell typed children are out of order");
                            if (value_node->properties != nullptr)
                                fail("OOF2003", value_node, id_text, "ValueType", "no attributes", "present", "Spreadsheet Cell ValueType does not accept attributes");
                            for (xmlNodePtr entry_node : element_children(value_node)) {
                                if (node_name(entry_node) != "Entry")
                                    fail("OOF2003", entry_node, id_text, node_name(entry_node), "Entry", node_name(entry_node), "Unknown Spreadsheet Cell ValueType item");
                                static constexpr std::array<std::string_view, 9> allowed{
                                    "term", "typeUuid", "typeName", "length", "precision", "nonNegative", "variable", "date", "time"};
                                for (xmlAttrPtr attribute = entry_node->properties; attribute != nullptr; attribute = attribute->next) {
                                    if (std::ranges::find(allowed, std::string_view(reinterpret_cast<const char*>(attribute->name))) == allowed.end())
                                        fail("OOF2003", entry_node, id_text, std::string(reinterpret_cast<const char*>(attribute->name)), "named type qualifier", "unsupported", "Spreadsheet Cell ValueType contains an unsupported qualifier");
                                }
                            }
                            value_type = parse_type_domain(value_node);
                            value_type_seen = true;
                            content_stage = 3;
                            continue;
                        }
                        if (name == "Value") {
                            if (!contains_value_seen || !value_type_seen || value_seen || content_stage != 3)
                                fail("OOF2003", value_node, id_text, "Value", "after ContainsValue and ValueType", "duplicate or out of order", "Spreadsheet Cell typed children are out of order");
                            if (value_node->properties != nullptr || !element_children(value_node).empty())
                                fail("OOF2003", value_node, id_text, "Value", "plain scalar text", "structured content", "Spreadsheet Cell Value must be plain text");
                            if (value_type->entries.size() != 1)
                                fail("OOF2003", value_node, id_text, "ValueType", "one supported type entry", std::to_string(value_type->entries.size()), "Spreadsheet Cell ValueType must contain exactly one type");
                            const auto term = value_type->entries.front().term;
                            const auto codec = term == model::TypeDomainTerm::string ? mm::ValueCodec::string :
                                term == model::TypeDomainTerm::numeric ? mm::ValueCodec::decimal :
                                term == model::TypeDomainTerm::boolean ? mm::ValueCodec::boolean :
                                term == model::TypeDomainTerm::date ? mm::ValueCodec::date : mm::ValueCodec::unclassified;
                            if (codec == mm::ValueCodec::unclassified)
                                fail("OOF2003", value_node, id_text, "ValueType", "String, Number, Boolean, or Date", "unsupported type entry", "Spreadsheet Cell typed value kind is unsupported");
                            consume_typed_value_budget(typed_budget_, 0, value_node, id_text);
                            typed_value = parse_property_value(value_node, codec, id_text, typed_budget_);
                            if (std::holds_alternative<model::UndefinedValue>(*typed_value))
                                fail("OOF2003", value_node, id_text, "Value", "defined String, Number, Boolean, or Date", "undefined", "Undefined Spreadsheet Cell values are unsupported");
                            value_seen = true;
                            content_stage = 4;
                            continue;
                        }
                        if (name == "Control") {
                            if (cell_control.has_value() || !value_type_seen || (content_stage != 3 && content_stage != 4))
                                fail("OOF2003", value_node, id_text, name, "one Control after typed value", "duplicate or out of order", "Cell.Control requires a typed cell");
                            for (xmlAttrPtr attr = value_node->properties; attr != nullptr; attr = attr->next) {
                                if (std::string_view(reinterpret_cast<const char*>(attr->name)) != "type")
                                    fail("OOF2003", value_node, id_text, name, "type attribute only", "unsupported attribute", "Unsupported Cell.Control attribute");
                            }
                            if (required_attribute(value_node, "type", id_text) != "InputField")
                                fail("OOF2003", value_node, id_text, name, "InputField", "unsupported type", "Unsupported Cell.Control kind");
                            cell_control.emplace();
                            bool read_only_seen = false;
                            for (xmlNodePtr property_node : element_children(value_node)) {
                                if (node_name(property_node) != "ReadOnly" || read_only_seen || property_node->properties != nullptr ||
                                    !element_children(property_node).empty())
                                    fail("OOF2003", property_node, id_text, "ReadOnly", "one plain Boolean ReadOnly", "unsupported property", "Unsupported Cell.Control property");
                                const auto* property = metamodel_.property(model::ControlKind::input_field, "ReadOnly");
                                cell_control->properties.set_explicit(property->id,
                                    parse_property_value(property_node, property->value_codec, id_text, typed_budget_));
                                read_only_seen = true;
                            }
                            content_stage = 5;
                            continue;
                        }
                        if (name == "DetailParameter") {
                            if (detail_parameter_seen || (content_stage != 0 && content_stage != 1 &&
                                content_stage != 3 && content_stage != 4 && content_stage != 5) ||
                                value_node->properties != nullptr || !element_children(value_node).empty())
                                fail("OOF2003", value_node, id_text, name, "one plain DetailParameter after cell content",
                                    "duplicate or out of order", "Spreadsheet Cell DetailParameter is invalid");
                            detail_parameter = node_text(value_node);
                            detail_parameter_seen = true;
                            content_stage = 7;
                            continue;
                        }
                        fail("OOF2003", value_node, id_text, name, "Text or typed-cell children", name, "Unknown Spreadsheet Cell item");
                    }
                    std::optional<model::SpreadsheetDocumentCellValue> typed;
                    if (contains_value_seen) {
                        if (!value_type_seen || !value_type.has_value())
                            fail("OOF2003", cell_node, id_text, "ValueType", "one type after ContainsValue", "missing", "Typed Spreadsheet Cell requires ValueType");
                        if (value_type->entries.size() != 1)
                            fail("OOF2003", cell_node, id_text, "ValueType", "one supported type entry", std::to_string(value_type->entries.size()), "Spreadsheet Cell ValueType must contain exactly one type");
                        model::PropertyValue effective_value = model::UndefinedValue{};
                        if (value_seen) {
                            effective_value = std::move(*typed_value);
                        } else {
                            switch (value_type->entries.front().term) {
                                case model::TypeDomainTerm::string:
                                    effective_value = std::string{};
                                    break;
                                case model::TypeDomainTerm::numeric:
                                    effective_value = model::DecimalValue{"0"};
                                    break;
                                case model::TypeDomainTerm::boolean:
                                    effective_value = false;
                                    break;
                                case model::TypeDomainTerm::date:
                                    if (value_type->entries.front().date != model::DateQualifiers{true, true})
                                        fail("OOF2003", cell_node, id_text, "Value", "known DateTime default", "unverified date qualifiers", "Spreadsheet Cell omitted Value has an unsupported Date default");
                                    effective_value = model::DateValue{"0001-01-01T00:00:00"};
                                    break;
                                default:
                                    fail("OOF2003", cell_node, id_text, "Value", "known String, Number, Boolean, or Date default", "unsupported type entry", "Spreadsheet Cell omitted Value has an unsupported default");
                            }
                        }
                        typed = model::SpreadsheetDocumentCellValue{std::move(*value_type), std::move(effective_value)};
                    } else if (!text_seen && !detail_parameter_seen) {
                        fail("OOF2003", cell_node, id_text, "Text", "Text, typed value, or DetailParameter", "missing",
                            "Spreadsheet Cell requires named content");
                    }
                    model::SpreadsheetDocumentCell cell{row, column, std::move(text), std::move(typed),
                        std::move(cell_control), std::move(format)};
                    cell.text_present = text_seen || cell.typed_value.has_value();
                    cell.detail_parameter = std::move(detail_parameter);
                    cells.push_back(std::move(cell));
                }
                std::ranges::sort(spreadsheet.rows, {}, &model::SpreadsheetDocumentRow::index);
                std::ranges::sort(spreadsheet.columns, {}, &model::SpreadsheetDocumentColumn::index);
                std::sort(cells.begin(), cells.end(), [](const auto& left, const auto& right) {
                    return std::tie(left.row, left.column) < std::tie(right.row, right.column);
                });
                continue;
            }
            else if (name == "Buttons" && (descriptor->kind == model::ControlKind::button || descriptor->kind == model::ControlKind::command_bar)) {
                if (auto* payload = std::get_if<model::ButtonPayload>(&control.payload)) payload->buttons = parse_command_bar_buttons(child, id_text, payload->buttons_state);
                else {
                    auto& bar_payload = std::get<model::CommandBarPayload>(control.payload);
                    bar_payload.buttons = parse_command_bar_buttons(child, id_text, bar_payload.buttons_state);
                }
            } else if (name == "Columns" && descriptor->kind == model::ControlKind::table) {
                std::get<model::TablePayload>(control.payload).columns = parse_table_columns(child, id_text);
            } else if (descriptor->kind == model::ControlKind::table && std::ranges::any_of(mm::table_source_extension_descriptors(),
                [&](const auto& extension) { return extension.xml_name == name; })) {
                auto& table = std::get<model::TablePayload>(control.payload);
                if (table.source_extension)
                    fail("OOF2003", child, id_text, name, "one named Table source extension", "duplicate or content",
                        "Table source extension is duplicated or malformed");
                require_typed_container(child, id_text);
                table.source_extension = std::ranges::find(mm::table_source_extension_descriptors(), name,
                    &mm::TableSourceExtensionDescriptor::xml_name)->kind;
                std::set<std::string> seen;
                const auto properties = mm::table_source_property_descriptors(*table.source_extension);
                for (auto* property_node : element_children(child)) {
                    const auto property_name = node_name(property_node);
                    if (!seen.insert(property_name).second)
                        fail("OOF2003", property_node, id_text, property_name, "one extension member", "duplicate",
                            "Table source extension member is duplicated");
                    if (property_name == "Bindings" && table.source_extension == model::TableSourceExtensionKind::catalog_list) {
                        require_typed_container(property_node, id_text);
                        std::set<model::TableSourceBindingRole> roles;
                        constexpr std::array<std::string_view, 3> names{"Owner", "Parent", "ActiveRow"};
                        for (auto* role_node : element_children(property_node)) {
                            const auto role = std::ranges::find(names, node_name(role_node));
                            if (role == names.end()) fail("OOF2003", role_node, id_text, "Bindings", "named source binding role",
                                node_name(role_node), "Table source binding role is unsupported");
                            const auto kind = static_cast<model::TableSourceBindingRole>(role - names.begin());
                            if (!roles.insert(kind).second)
                                fail("OOF2003", role_node, id_text, "Bindings", "unique source binding role", "duplicate",
                                    "Table source binding role is duplicated");
                            require_typed_container(role_node, id_text);
                            const auto targets = element_children(role_node);
                            if (targets.size() != 1) fail("OOF2003", role_node, id_text, "Bindings", "one DataPath or EventSource",
                                "missing or multiple", "Table source binding requires one typed source");
                            auto* target = targets.front();
                            if (node_name(target) == "DataPath") {
                                table.source_bindings.push_back({kind, parse_data_path(target, id_text)});
                            } else if (node_name(target) == "EventSource") {
                                require_typed_container(target, id_text, "event");
                                model::TableEventSource source;
                                source.event = required_attribute(target, "event", id_text);
                                const auto owners = element_children(target);
                                if (owners.size() != 1) fail("OOF2003", target, id_text, "EventSource", "one FormRef or ControlRef",
                                    "missing or multiple", "Event source requires one typed owner");
                                auto* owner = owners.front();
                                require_typed_container(owner, id_text, "id");
                                if (!element_children(owner).empty()) fail("OOF2003", owner, id_text, "EventSource", "empty reference",
                                    "content", "Event source reference cannot contain children");
                                const auto reference = parse_object_id(required_attribute(owner, "id", id_text), owner, "id", id_text);
                                if (node_name(owner) == "FormRef") source.owner = model::FormRef{reference};
                                else if (node_name(owner) == "ControlRef") source.owner = model::ControlRef{reference};
                                else fail("OOF2003", owner, id_text, "EventSource", "FormRef or ControlRef", node_name(owner),
                                    "Event source owner kind is unsupported");
                                table.source_bindings.push_back({kind, std::move(source)});
                            } else fail("OOF2003", target, id_text, "Bindings", "DataPath or EventSource", node_name(target),
                                "Table source binding target is unsupported");
                        }
                        continue;
                    }
                    if (property_name == "Events") {
                        auto events = parse_control_events(property_node, id,
                            mm::table_source_event_descriptors(*table.source_extension));
                        control.events.insert(control.events.end(), events.begin(), events.end());
                        continue;
                    }
                    const auto found = std::ranges::find(properties, property_name, &mm::PropertyDescriptor::xml_name);
                    if (found == properties.end())
                        fail("OOF2003", property_node, id_text, property_name, "declared Table source property", property_name,
                            "Table source property is not declared");
                    auto value = parse_property_value(property_node, found->value_codec, id_text, typed_budget_);
                    if (!equals_descriptor_default(*found, value)) control.properties().set_explicit(found->id, std::move(value));
                }
            } else if (name == "DataPath") {
                control.data_path = parse_data_path(child, id_text);
            } else if (input_field != nullptr && std::ranges::any_of(mm::input_field_extensions,
                [&](const auto& extension) { return extension.xml_name == name; })) {
                if (input_field->extension || child->properties != nullptr ||
                    !element_children(child).empty() || !trim_ascii(node_text(child)).empty())
                    fail("OOF2003", child, id_text, name, "one empty named InputField extension", "settings or duplicate",
                        "InputField extensions must be empty and mutually exclusive");
                input_field->extension = std::ranges::find(mm::input_field_extensions, name,
                    &mm::InputFieldExtensionDescriptor::xml_name)->kind;
            } else if (name == "TypeLink" && input_field != nullptr) {
                if (type_link_seen)
                    fail("OOF2003", child, id_text, name, "one TypeLink", "duplicate", "Duplicate InputField TypeLink");
                type_link_seen = true;
                for (xmlAttrPtr attribute = child->properties; attribute != nullptr; attribute = attribute->next) {
                    if (attribute->name == nullptr || std::string_view(reinterpret_cast<const char*>(attribute->name)) != "attributeId")
                        fail("OOF2003", child, id_text, std::string(reinterpret_cast<const char*>(attribute->name)),
                            "attributeId only", std::string(reinterpret_cast<const char*>(attribute->name)), "Unknown TypeLink attribute");
                }
                if (!element_children(child).empty() || !trim_ascii(node_text(child)).empty())
                    fail("OOF2003", child, id_text, name, "empty TypeLink element", "content", "TypeLink cannot contain text or child elements");
                input_field->type_link = model::AttributeRef{parse_attribute_id(
                    required_attribute(child, "attributeId", id_text), child, "attributeId", id_text)};
            } else if (dendrogram != nullptr && name == "Items") {
                if (dendrogram_items_seen) {
                    fail("OOF2003", child, id_text, name, "one owned Items collection", "duplicate", "Dendrogram Items is duplicated");
                }
                dendrogram_items_seen = true;
                dendrogram->items = parse_dendrogram_items(child, id_text);
            } else if (dendrogram != nullptr && name == "Links") {
                if (dendrogram_links_seen) {
                    fail("OOF2003", child, id_text, name, "one owned Links collection", "duplicate", "Dendrogram Links is duplicated");
                }
                dendrogram_links_seen = true;
                dendrogram->links = parse_dendrogram_links(child, id_text);
            } else if (name == "Position") {
                control.position = parse_position(child, id_text);
                position_seen = true;
            } else if (name == "Events") {
                if (control_events_seen)
                    fail("OOF2003", child, id_text, name, "one Events collection", "duplicate", "Control Events is duplicated");
                control_events_seen = true;
                auto events = parse_control_events(child, id, metamodel_.events_for(descriptor->kind));
                control.events.insert(control.events.end(), events.begin(), events.end());
            } else if (name == "ChildItems") {
                control.children = parse_child_items(child);
            } else if (descriptor->kind == model::ControlKind::gantt_chart &&
                       (name == "Series" || name == "Points" || name == "Intervals")) {
                auto& gantt = std::get<model::GanttChartPayload>(control.payload);
                {
                    bool* seen = name == "Series" ? &gantt_series_seen :
                        name == "Points" ? &gantt_points_seen : &gantt_intervals_seen;
                    if (*seen) fail("OOF2003", child, id_text, name, "one collection", "duplicate", "Gantt collection is duplicated");
                    *seen = true;
                    if (name == "Series") {
                        for (xmlNodePtr item : element_children(child)) {
                            if (node_name(item) != "GanttSeries") fail("OOF2003", item, id_text, name, "GanttSeries", node_name(item), "Unexpected Series member");
                            model::GanttSeries value;
                            value.id = parse_object_id(required_attribute(item, "id", id_text), item, "id", id_text);
                            bool value_seen = false;
                            bool text_seen = false;
                            for (xmlNodePtr field : element_children(item)) {
                                if (node_name(field) == "Value" && !value_seen) { value.value = node_text(field); value_seen = true; }
                                else if (node_name(field) == "Text" && !text_seen) { value.text = node_text(field); text_seen = true; }
                                else if (node_name(field) == "Color" && !value.color) value.color = parse_color(field);
                                else fail("OOF2003", field, id_text, node_name(field), "Value, Text, and optional Color", node_name(field), "Unexpected or duplicate Gantt Series property");
                            }
                            if (!value_seen || !text_seen) fail("OOF2003", item, id_text, "Value/Text", "required properties", "missing", "Gantt Series requires Value and Text");
                            gantt.series.push_back(std::move(value));
                        }
                    } else if (name == "Points") {
                        for (xmlNodePtr item : element_children(child)) {
                            if (node_name(item) != "GanttPoint") fail("OOF2003", item, id_text, name, "GanttPoint", node_name(item), "Unexpected Points member");
                            model::GanttPoint value;
                            value.id = parse_object_id(required_attribute(item, "id", id_text), item, "id", id_text);
                            bool value_seen = false;
                            bool text_seen = false;
                            for (xmlNodePtr field : element_children(item)) {
                                if (node_name(field) == "Value" && !value_seen) { value.value = node_text(field); value_seen = true; }
                                else if (node_name(field) == "Text" && !text_seen) { value.text = node_text(field); text_seen = true; }
                                else if (node_name(field) == "Color" && !value.color) value.color = parse_color(field);
                                else fail("OOF2003", field, id_text, node_name(field), "Value, Text, and optional Color", node_name(field), "Unexpected or duplicate Gantt Point property");
                            }
                            if (!value_seen || !text_seen) fail("OOF2003", item, id_text, "Value/Text", "required properties", "missing", "Gantt Point requires Value and Text");
                            gantt.points.push_back(std::move(value));
                        }
                    } else {
                        for (xmlNodePtr item : element_children(child)) {
                            if (node_name(item) != "GanttInterval") fail("OOF2003", item, id_text, name, "GanttInterval", node_name(item), "Unexpected Intervals member");
                            model::GanttInterval value;
                            value.point_ref = parse_object_id(required_attribute(item, "pointRef", id_text), item, "pointRef", id_text);
                            value.series_ref = parse_object_id(required_attribute(item, "seriesRef", id_text), item, "seriesRef", id_text);
                            bool begin_seen = false;
                            bool end_seen = false;
                            bool text_seen = false;
                            for (xmlNodePtr field : element_children(item)) {
                                const std::string field_name = node_name(field);
                                if (field_name == "StartDate" && !begin_seen) {
                                    value.start_date = parse_gantt_date(field, id_text, field_name); begin_seen = true;
                                } else if (field_name == "EndDate" && !end_seen) {
                                    value.end_date = parse_gantt_date(field, id_text, field_name); end_seen = true;
                                } else if (field_name == "Text" && !text_seen) {
                                    value.text = node_text(field); text_seen = true;
                                } else fail("OOF2003", field, id_text, field_name, "one StartDate, EndDate, and Text", field_name, "Unexpected or duplicate Gantt Interval property");
                            }
                            if (!begin_seen || !end_seen || !text_seen) fail("OOF2003", item, id_text, "StartDate/EndDate/Text", "required properties", "missing", "Gantt Interval requires both dates and Text");
                            gantt.intervals.push_back(std::move(value));
                        }
                    }
                }
            } else {
                if ((descriptor->kind == model::ControlKind::input_field ||
                     descriptor->kind == model::ControlKind::choice_field) && name == "SelButtonPicture") {
                    fail("OOF2003", child, id_text, name, "ChoiceButtonPicture", name,
                        "Use the canonical XML property for the picture alias");
                }
                if (descriptor->kind == model::ControlKind::chart && name == "Title")
                    fail("OOF2003", child, id_text, name, "TitleArea/Text", name, "Chart title text belongs to TitleArea");
                const mm::PropertyDescriptor* property =
                    metamodel_.property(descriptor->kind, name);
                if (property == nullptr) {
                    fail("OOF2003", child, id_text, name, "declared control property", name, "Control property is not declared by the executable metamodel");
                }
                if (property->persistence == mm::PersistenceClass::runtime_only) {
                    fail("OOF2003", child, id_text, name, "persisted control property", name,
                        "Runtime-only control properties cannot appear in public Form.xml");
                }
                if (property->persistence == mm::PersistenceClass::unsupported_by_platform) {
                    fail("OOF2003", child, id_text, name, "available ordinary-form property", name,
                        "Property is unavailable on the ordinary-form platform owner");
                }
                model::PropertyValue value =
                    parse_property_value(child, property->value_codec, id_text, typed_budget_);
                if (equals_descriptor_default(*property, value)) {
                    continue;
                }
                if (property->surface == mm::PropertySurface::control_extension &&
                    property->api_name != "Name" && property->api_name != "Data") {
                    control.extension_properties.set_explicit(property->id, std::move(value));
                } else if (property->surface == mm::PropertySurface::control_payload) {
                    control.properties().set_explicit(property->id, std::move(value));
                } else {
                    fail("OOF2003", child, id_text, name, "property on its typed XML surface", name, "Control property appears on the wrong object-model surface");
                }
            }
        }
        if (const auto* table = std::get_if<model::TablePayload>(&control.payload);
            table && table->source_extension == model::TableSourceExtensionKind::catalog_list) {
            for (auto* child : element_children(node))
                if (node_name(child) == "Tree")
                    fail("OOF2003", child, id_text, "Tree", "CatalogListTableExtension/Tree", "Table/Tree",
                        "CatalogList Tree belongs to its source extension");
        }
        if (auto* chart = std::get_if<model::ChartPayload>(&control.payload)) finish_chart_content(*chart);
        if (dendrogram != nullptr) validate_dendrogram_graph(*dendrogram, node, id_text);
        if (!position_seen) {
            fail("OOF2003", node, id_text, "Position", "required Position", "missing", "Control has no typed Position");
        }
        const model::ControlRef reference{id};
        objects_.controls.push_back(std::move(control));
        return reference;
    }

    model::DateValue parse_gantt_date(xmlNodePtr node, std::string_view owner_id, std::string_view name) {
        const std::string text(trim_ascii(node_text(node)));
        try {
            (void)storage::value_codec::date_to_platform(text);
        } catch (const std::exception& error) {
            fail("OOF2003", node, std::string(owner_id), std::string(name),
                "local date-time YYYY-MM-DDTHH:MM:SS", text,
                std::string("Gantt date value is invalid: ") + error.what());
        }
        return model::DateValue{text};
    }

    std::vector<model::TableColumn> parse_table_columns(xmlNodePtr node, std::string_view owner) {
        std::vector<model::TableColumn> columns;
        std::set<std::string> names;
        for (xmlNodePtr column_node : element_children(node)) {
            if (node_name(column_node) != "Column") {
                fail("OOF2003", column_node, std::string(owner), node_name(column_node),
                    "Column", node_name(column_node), "Unknown Table column object");
            }
            for (xmlAttrPtr attr = column_node->properties; attr != nullptr; attr = attr->next) {
                const std::string_view attr_name(reinterpret_cast<const char*>(attr->name));
                if (attr_name != "name") {
                    fail("OOF2003", column_node, std::string(owner), std::string(attr_name),
                        "name attribute", std::string(attr_name), "Unsupported Table Column attribute");
                }
            }
            model::TableColumn column;
            column.control.reset();
            column.name = required_attribute(column_node, "name", owner);
            if (column.name.empty() || !names.insert(column.name).second) {
                fail("OOF2003", column_node, std::string(owner), "name",
                    "non-empty unique Column name", column.name, "Table Column name is empty or duplicated");
            }
            std::set<std::string> seen;
            bool data_path_seen = false;
            bool header_seen = false;
            for (xmlNodePtr field : element_children(column_node)) {
                const std::string field_name = node_name(field);
                if (!seen.insert(field_name).second) {
                    fail("OOF2003", field, std::string(owner), field_name,
                        "Column field at most once", field_name, "Duplicate Table Column field");
                }
                if (field_name == "DataPath") {
                    column.data_path = node_text(field);
                    data_path_seen = true;
                } else if (field_name == "Source") {
                    require_typed_container(field, owner);
                    const auto children = element_children(field);
                    if (children.size() != 1)
                        fail("OOF2003", field, std::string(owner), "Source", "one named column source", "invalid count",
                            "Table Column Source must contain one named source");
                    auto* source_node = children.front();
                    if (node_name(source_node) == "MetadataField") {
                        auto member = parse_metadata_member(source_node, owner);
                        if (member.type)
                            fail("OOF2003", source_node, std::string(owner), "Source", "field identity only", "TypeDomain",
                                "Column value type belongs to Column or Control");
                        column.source = model::TableMetadataColumnField{std::move(member)};
                    } else if (node_name(source_node) == "StandardPicture") {
                        require_typed_container(source_node, owner);
                        if (!element_children(source_node).empty())
                            fail("OOF2003", source_node, std::string(owner), "StandardPicture", "empty element", "children",
                                "StandardPicture cannot contain child elements");
                        column.source = model::TableStandardPictureColumn{};
                    } else fail("OOF2003", source_node, std::string(owner), "Source", "MetadataField or StandardPicture",
                        node_name(source_node), "Unknown Table Column source");
                } else if (field_name == "Header") {
                    column.header = parse_localized_string(field);
                    header_seen = true;
                } else if (field_name == "Width") {
                    for (xmlAttrPtr attr = field->properties; attr != nullptr; attr = attr->next) {
                        const std::string_view attr_name(reinterpret_cast<const char*>(attr->name));
                        if (attr_name != "unit") {
                            fail("OOF2003", field, std::string(owner), std::string(attr_name),
                                "unit attribute", std::string(attr_name), "Unsupported Table Column Width attribute");
                        }
                    }
                    const std::string unit = required_attribute(field, "unit", owner);
                    if (unit != "form") {
                        fail("OOF2003", field, std::string(owner), "unit", "form", unit,
                            "Table Column Width unit must be form");
                    }
                    for (xmlNodePtr child : element_children(field)) {
                        fail("OOF2003", child, std::string(owner), "Width", "decimal text", node_name(child),
                            "Table Column Width cannot contain child elements");
                    }
                    std::string width = canonical_decimal(node_text(field), field, "Width", owner);
                    if (width.front() == '-') {
                        fail("OOF2003", field, std::string(owner), "Width", "non-negative decimal", width,
                            "Table Column Width cannot be negative");
                    }
                    column.layout_width = model::DecimalValue{std::move(width)};
                } else if (field_name == "TextFont") {
                    column.text_font = parse_font(field);
                } else if (const auto property = std::ranges::find(mm::table_column_boolean_descriptors(), field_name,
                    &mm::TableColumnBooleanDescriptor::name); property != mm::table_column_boolean_descriptors().end()) {
                    column.*property->member = parse_boolean(node_text(field), field, field_name, owner);
                } else if (const auto property = std::ranges::find(mm::table_column_enumeration_descriptors(), field_name,
                    &mm::TableColumnEnumerationDescriptor::name); property != mm::table_column_enumeration_descriptors().end()) {
                    column.*property->member = parse_enumeration(field);
                } else if (field_name == "ValueType") {
                    column.value_type = parse_type_domain(field);
                } else if (field_name == "Control") {
                    column.control.emplace();
                    for (xmlAttrPtr attr = field->properties; attr != nullptr; attr = attr->next) {
                        const std::string_view attr_name(reinterpret_cast<const char*>(attr->name));
                        if (attr_name != "type") {
                            fail("OOF2003", field, std::string(owner), std::string(attr_name),
                                "type attribute", std::string(attr_name), "Unsupported Table Column Control attribute");
                        }
                    }
                    const std::string type = required_attribute(field, "type", owner);
                    if (type == "InputField") {
                        column.control->kind = model::ControlKind::input_field;
                    } else if (type == "ChoiceField") {
                        column.control->kind = model::ControlKind::choice_field;
                    } else if (type == "CheckBox") {
                        column.control->kind = model::ControlKind::check_box;
                    } else {
                        fail("OOF2003", field, std::string(owner), "type",
                            "InputField, ChoiceField, or CheckBox", type, "Unsupported Table Column Control type");
                    }
                    std::set<std::string> control_properties;
                    for (xmlNodePtr property_node : element_children(field)) {
                        const std::string property_name = node_name(property_node);
                        const auto extension = std::ranges::find(mm::input_field_extensions, property_name,
                            &mm::InputFieldExtensionDescriptor::xml_name);
                        if (extension != std::end(mm::input_field_extensions)) {
                            if (column.control->kind != model::ControlKind::input_field || column.control->extension ||
                                property_node->properties || !element_children(property_node).empty() ||
                                !trim_ascii(node_text(property_node)).empty())
                                fail("OOF2003", property_node, std::string(owner), property_name,
                                    "one empty named InputField extension", "invalid owner, settings or duplicate",
                                    "Invalid Table Column editor extension");
                            column.control->extension = extension->kind;
                            continue;
                        }
                        if (property_name == "Events") {
                            if (column.control->kind != model::ControlKind::input_field ||
                                !control_properties.insert(property_name).second)
                                fail("OOF2003", property_node, std::string(owner), property_name,
                                    "one InputField Events collection", "invalid owner or duplicate", "Invalid editor Events");
                            column.control->events = parse_control_events(property_node,
                                parse_object_id(std::string(owner), property_node, "ownerId"),
                                mm::event_descriptors(column.control->kind), column.name);
                            continue;
                        }
                        if (property_name == "ValueType") {
                            if (column.control->kind != model::ControlKind::input_field) {
                                fail("OOF2003", property_node, std::string(owner), property_name,
                                    "ValueType on an InputField editor", property_name,
                                    "Table Column editor ValueType is supported only for InputField");
                            }
                            for (xmlAttrPtr attr = property_node->properties; attr != nullptr; attr = attr->next) {
                                fail("OOF2003", property_node, std::string(owner),
                                    std::string(reinterpret_cast<const char*>(attr->name)),
                                    "ValueType without attributes", "attribute present",
                                    "Table Column ValueType does not accept attributes");
                            }
                            if (!control_properties.insert(property_name).second) {
                                fail("OOF2003", property_node, std::string(owner), property_name,
                                    "ValueType at most once", "duplicate",
                                    "Duplicate Table Column editor ValueType");
                            }
                            auto value_type = parse_type_domain(property_node);
                            if (!value_type.entries.empty()) column.control->value_type = std::move(value_type);
                            continue;
                        }
                        const bool supported_property =
                            (column.control->kind == model::ControlKind::input_field &&
                                (std::ranges::find(mm::table_column_input_properties, property_name) !=
                                    mm::table_column_input_properties.end())) ||
                            (column.control->kind == model::ControlKind::choice_field &&
                                (property_name == "Enabled" || property_name == "ToolTip")) ||
                            (column.control->kind == model::ControlKind::check_box &&
                                (property_name == "Enabled" || property_name == "Caption" ||
                                    property_name == "ToolTip" || property_name == "Font"));
                        if (!control_properties.insert(property_name).second || !supported_property) {
                            fail("OOF2003", property_node, std::string(owner), property_name,
                                "a compatible named default property, at most once", property_name,
                                "Unsupported Table Column editor property");
                        }
                        const auto* descriptor = metamodel_.property(column.control->kind, property_name);
                        if (descriptor == nullptr || descriptor->surface != mm::PropertySurface::control_payload) {
                            fail("OOF2003", property_node, std::string(owner), property_name,
                                "declared payload property for the selected editor", property_name,
                                "Table Column editor property is not declared by its typed owner");
                        }
                        const model::PropertyValue value = parse_property_value(
                            property_node, descriptor->value_codec, owner, typed_budget_);
                        if (!valid_table_column_editor_property(column.control->kind, *descriptor, value)) {
                            fail("OOF2003", property_node, std::string(owner), property_name,
                                "a supported typed property value", node_text(property_node),
                                "Table Column editor property value is unsupported");
                        }
                        if (!equals_descriptor_default(*descriptor, value)) {
                            column.control->properties.set_explicit(descriptor->id, value);
                        }
                    }
                } else {
                    fail("OOF2003", field, std::string(owner), field_name,
                        "DataPath, Header, Width, TextFont, Visible, ValueType, and Control", field_name, "Unsupported Table Column field");
                }
            }
            if (column.control && column.value_type) {
                fail("OOF2003", column_node, std::string(owner), "ValueType",
                    "Column ValueType only without Control", "both present",
                    "Table Column stored type cannot have two owners");
            }
            if (column.value_type && column.value_type->entries.empty()) column.value_type.reset();
            if ((!column.source && (!data_path_seen || column.data_path.empty())) ||
                (column.source && data_path_seen) || !header_seen || (!column.source && column.header.items.empty())) {
                fail("OOF2003", column_node, std::string(owner), "Column fields",
                    "DataPath and localized Header", "incomplete", "Table Column is incomplete");
            }
            columns.push_back(std::move(column));
        }
        return columns;
    }

    model::CommandBarButton parse_command_bar_button(xmlNodePtr node, std::string_view owner) {
        if (node_name(node) != "CommandBarButton") {
            fail("OOF2003", node, std::string(owner), node_name(node), "CommandBarButton", node_name(node), "Unknown button menu item");
        }
        for (xmlAttrPtr attr = node->properties; attr != nullptr; attr = attr->next) {
            const std::string_view name(reinterpret_cast<const char*>(attr->name));
            if (name != "name" && name != "type")
                fail("OOF2003", node, std::string(owner), std::string(name), "name and type attributes", std::string(name), "Unsupported button menu attribute");
        }
        model::CommandBarButton item;
        item.name = required_attribute(node, "name", owner);
        const auto type = required_attribute(node, "type", owner);
        if (type == "Action") item.type = model::CommandBarButtonKind::action;
        else if (type == "Submenu") item.type = model::CommandBarButtonKind::submenu;
        else if (type == "Separator") item.type = model::CommandBarButtonKind::separator;
        else fail("OOF2003", node, std::string(owner), "type", "Action, Submenu, or Separator", type, "Unknown button menu item type");
        std::set<std::string> seen;
        bool has_order = false;
        for (xmlNodePtr child : element_children(node)) {
            const std::string name = node_name(child);
            if (!seen.insert(name).second) fail("OOF2003", child, std::string(owner), name, "field at most once", name, "Duplicate button menu field");
            if (name == "AutomaticInsertionLocks") item.automatic_insertion_locks = parse_integer<std::uint64_t>(node_text(child), child, name, owner);
            else if (name == "Text") item.text = node_text(child);
            else if (name == "Explanation") item.explanation = node_text(child);
            else if (name == "ToolTip") item.tooltip = node_text(child);
            else if (name == "Enabled") item.enabled = parse_boolean(node_text(child), child, "Enabled", owner);
            else if (name == "Checked") item.checked = parse_boolean(node_text(child), child, "Checked", owner);
            else if (name == "DefaultButton") item.default_button = parse_boolean(node_text(child), child, "DefaultButton", owner);
            else if (name == "ChangesData") item.changes_data = parse_boolean(node_text(child), child, "ChangesData", owner);
            else if (name == "Representation") {
                const auto rep = node_text(child);
                if (rep == "Auto") item.representation = model::ButtonRepresentation::automatic;
                else if (rep == "Picture") item.representation = model::ButtonRepresentation::picture;
                else if (rep == "Text") item.representation = model::ButtonRepresentation::text;
                else if (rep == "PictureText") item.representation = model::ButtonRepresentation::picture_text;
                else fail("OOF2003", child, std::string(owner), name, "Auto, Picture, Text, or PictureText", rep, "Unknown button representation");
            } else if (name == "ClientInterfaceVariant") {
                const auto variant = node_text(child);
                if (variant == "Version8_0") item.client_interface_variant = model::ClientInterfaceVariant::version8_0;
                else if (variant == "Version8_2") item.client_interface_variant = model::ClientInterfaceVariant::version8_2;
                else if (variant == "Version8_2_OrdinaryApp") item.client_interface_variant = model::ClientInterfaceVariant::version8_2_ordinary_app;
                else fail("OOF2003", child, std::string(owner), name, "Version8_0, Version8_2, or Version8_2_OrdinaryApp", variant, "Unknown client interface variant");
            } else if (name == "ClientInterfaceTheme") {
                const auto theme = node_text(child);
                if (theme == "Light") item.client_interface_theme = model::ClientInterfaceTheme::light;
                else if (theme == "Dark") item.client_interface_theme = model::ClientInterfaceTheme::dark;
                else fail("OOF2003", child, std::string(owner), name, "Light or Dark", theme, "Unknown client interface theme");
            } else if (name == "CommandControlKind") {
                const auto kind = node_text(child);
                if (kind == "FromSource") item.command_control_kind = model::CommandControlKind::from_source;
                else if (kind == "Button") item.command_control_kind = model::CommandControlKind::button;
                else fail("OOF2003", child, std::string(owner), name, "FromSource or Button", kind, "Unknown command control kind");
            } else if (name == "Shortcut") item.shortcut = parse_shortcut(child);
            else if (name == "Picture") {
                if (child->properties == nullptr && element_children(child).empty() && node_text(child).empty())
                    item.clear_picture = true;
                else item.picture = parse_picture_reference(child, "Picture", owner);
            }
            else if (name == "Action") {
                for (xmlAttrPtr attr = child->properties; attr != nullptr; attr = attr->next) {
                    const std::string_view attr_name(reinterpret_cast<const char*>(attr->name));
                    if (attr_name != "handler" && attr_name != "name") {
                        fail("OOF2003", child, std::string(owner), std::string(attr_name),
                            "handler and name attributes", std::string(attr_name), "Unsupported Action attribute");
                    }
                }
                model::CommandBarAction action;
                action.handler = required_attribute(child, "handler", owner);
                action.name = required_attribute(child, "name", owner);
                std::set<std::string> action_fields;
                bool has_text = false;
                bool has_tooltip = false;
                bool has_description = false;
                for (xmlNodePtr action_field : element_children(child)) {
                    const std::string field_name = node_name(action_field);
                    if (!action_fields.insert(field_name).second) {
                        fail("OOF2003", action_field, std::string(owner), field_name,
                            "Action field at most once", field_name, "Duplicate Action field");
                    }
                    if (field_name == "Text") {
                        action.text = parse_localized_string(action_field);
                        has_text = true;
                    } else if (field_name == "ToolTip") {
                        action.tooltip = parse_localized_string(action_field);
                        has_tooltip = true;
                    } else if (field_name == "Description") {
                        action.description = parse_localized_string(action_field);
                        has_description = true;
                    } else {
                        fail("OOF2003", action_field, std::string(owner), field_name,
                            "Text, ToolTip, and Description", field_name, "Unknown Action field");
                    }
                }
                if (!has_text || !has_tooltip || !has_description) {
                    fail("OOF2003", child, std::string(owner), "Action fields",
                        "Text, ToolTip, and Description", "incomplete", "Action requires all three localized values");
                }
                item.action = std::move(action);
            }
            else if (name == "StandardAction") {
                item.standard_action = parse_standard_action(child, owner);
            }
            else if (name == "Order") {
                has_order = true;
                const auto order = node_text(child);
                if (order == "DontOrder") item.order = model::CommandBarButtonOrder::none;
                else if (order == "Ascending") item.order = model::CommandBarButtonOrder::ascending;
                else if (order == "Descending") item.order = model::CommandBarButtonOrder::descending;
                else fail("OOF2003", child, std::string(owner), name, "DontOrder, Ascending, or Descending", order, "Unknown submenu order");
            }
            else if (name == "Buttons") item.buttons = parse_command_bar_buttons(child, owner, item.buttons_state);
            else fail("OOF2003", child, std::string(owner), name, "declared button menu field", name, "Unknown button menu field");
        }
        if (item.action && item.standard_action)
            fail("OOF2003", node, std::string(owner), "Action", "at most one Action or StandardAction", "duplicated", "Action item cannot combine action values");
        if (item.action && item.action->handler.empty())
            fail("OOF2003", node, std::string(owner), "Action", "non-empty action handler", "empty", "Action requires a handler");
        if (item.type != model::CommandBarButtonKind::action && (item.action || item.standard_action))
            fail("OOF2003", node, std::string(owner), "Action", "Action item only", "present", "Only Action items may declare an action value");
        if (item.type != model::CommandBarButtonKind::submenu && has_order)
            fail("OOF2003", node, std::string(owner), "Order", "Submenu only", "present", "Only Submenu items may declare an order");
        if (item.type != model::CommandBarButtonKind::submenu && (!item.buttons.empty() || item.buttons_state != model::CommandBarButtonsState{}))
            fail("OOF2003", node, std::string(owner), "Buttons", "Submenu only", "present", "Only Submenu items may contain buttons");
        if (item.type == model::CommandBarButtonKind::separator &&
            std::any_of(seen.begin(), seen.end(), [](const auto& field) {
                return field != "ClientInterfaceVariant" && field != "ClientInterfaceTheme" && field != "CommandControlKind" && field != "AutomaticInsertionLocks";
            }))
            fail("OOF2003", node, std::string(owner), "fields", "command interface settings or AutomaticInsertionLocks only", "unsupported field",
                "Separator contains an unsupported field");
        return item;
    }

    std::vector<model::DendrogramItem> parse_dendrogram_items(xmlNodePtr node, std::string_view owner) {
        std::vector<model::DendrogramItem> result;
        std::set<std::string> values;
        for (xmlNodePtr child : element_children(node)) {
            if (node_name(child) != "Item") {
                fail("OOF2003", child, std::string(owner), node_name(child), "Item", node_name(child), "Unknown Dendrogram item");
            }
            model::DendrogramItem item;
            bool value_seen = false;
            bool text_seen = false;
            for (xmlNodePtr field : element_children(child)) {
                const auto name = node_name(field);
                if (name == "Value" && !value_seen) {
                    item.value = node_text(field);
                    value_seen = true;
                } else if (name == "Text" && !text_seen) {
                    item.text = parse_localized_string(field);
                    text_seen = true;
                } else {
                    fail("OOF2003", field, std::string(owner), name, "Value and Text", name, "Unsupported or duplicate Dendrogram item field");
                }
            }
            if (!value_seen || item.value.empty() || !values.insert(item.value).second) {
                fail("OOF2003", child, std::string(owner), "Value", "unique non-empty string", item.value, "Dendrogram item value is missing, empty, or duplicated");
            }
            if (item.text.items.empty()) {
                fail("OOF2003", child, std::string(owner), "Text", "at least one localized Item", "empty", "Dendrogram item Text requires localized text");
            }
            const auto max_rows = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 1);
            if (result.size() >= max_rows)
                fail("OOF1122", child, std::string(owner), "Items", "collection size representable by uint32 keys", std::to_string(result.size() + 1), "Dendrogram Items collection is too large");
            result.push_back(std::move(item));
        }
        return result;
    }

    std::vector<model::DendrogramLink> parse_dendrogram_links(xmlNodePtr node, std::string_view owner) {
        std::vector<model::DendrogramLink> result;
        for (xmlNodePtr child : element_children(node)) {
            if (node_name(child) != "Link") fail("OOF2003", child, std::string(owner), node_name(child), "Link", node_name(child), "Unknown Dendrogram link");
            model::DendrogramLink link;
            bool first_seen = false, second_seen = false, title_seen = false, distance_seen = false;
            for (xmlNodePtr field : element_children(child)) {
                const auto name = node_name(field);
                if (name == "FirstItem" && !first_seen) { link.first_item = node_text(field); first_seen = true; }
                else if (name == "SecondItem" && !second_seen) { link.second_item = node_text(field); second_seen = true; }
                else if (name == "Title" && !title_seen) { link.title = parse_localized_string(field); title_seen = true; }
                else if (name == "Distance" && !distance_seen) { link.distance = model::DecimalValue{canonical_decimal(node_text(field), field, name, owner)}; distance_seen = true; }
                else fail("OOF2003", field, std::string(owner), name, "FirstItem, SecondItem, Title, optional Distance", name, "Unsupported or duplicate Dendrogram link field");
            }
            if (!first_seen || !second_seen || !title_seen || link.title.items.empty())
                fail("OOF2003", child, std::string(owner), "Link", "named endpoints and localized Title", "incomplete", "Dendrogram link is incomplete");
            const auto max_rows = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 1);
            if (result.size() >= max_rows)
                fail("OOF1122", child, std::string(owner), "Links", "collection size representable by uint32 keys", std::to_string(result.size() + 1), "Dendrogram Links collection is too large");
            result.push_back(std::move(link));
        }
        return result;
    }

    void validate_dendrogram_graph(const model::DendrogramPayload& graph, xmlNodePtr node, std::string_view owner) {
        std::set<std::string> values;
        for (const auto& item : graph.items) {
            if (item.value.empty() || !values.insert(item.value).second || item.text.items.empty())
                fail("OOF2003", node, std::string(owner), "Items", "unique non-empty values with localized Text", item.value, "Invalid Dendrogram items");
        }
        std::set<std::pair<std::string, std::string>> edges;
        for (const auto& link : graph.links) {
            if (link.first_item == link.second_item || !values.contains(link.first_item) || !values.contains(link.second_item))
                fail("OOF2003", node, std::string(owner), "Links", "distinct endpoints resolving to Items", link.first_item + "->" + link.second_item, "Dendrogram link endpoint is invalid");
            auto edge = std::minmax(link.first_item, link.second_item);
            if (!edges.emplace(edge.first, edge.second).second)
                fail("OOF2003", node, std::string(owner), "Links", "unique graph edges", link.first_item + "->" + link.second_item, "Duplicate Dendrogram edge");
            if (link.title.items.empty())
                fail("OOF2003", node, std::string(owner), "Title", "localized value", "empty", "Dendrogram link title is invalid");
            (void)canonical_decimal(link.distance.canonical, node, "Distance", owner);
        }
    }

    std::vector<model::CommandBarButton> parse_command_bar_buttons(xmlNodePtr node, std::string_view owner, model::CommandBarButtonsState& state) {
        state = {};
        for (xmlAttrPtr attr = node->properties; attr != nullptr; attr = attr->next) {
            const std::string name(reinterpret_cast<const char*>(attr->name));
            if (name != "autoFill")
                fail("OOF2003", node, std::string(owner), name, "autoFill attribute", name, "Unknown Buttons attribute");
            state.auto_fill = parse_boolean(required_attribute(node, "autoFill", owner), node, "autoFill", owner);
        }
        std::vector<model::CommandBarButton> result;
        std::set<std::string> names;
        std::map<std::string, model::CommandBarButtonKind> first_named;
        std::set<std::string> duplicate_names;
        bool has_automatic = false;
        for (xmlNodePtr child : element_children(node)) {
            if (node_name(child) == "AutomaticButtons") {
                if (has_automatic || !result.empty())
                    fail("OOF2003", child, std::string(owner), "AutomaticButtons", "at most once before buttons", "out of order or duplicate", "AutomaticButtons must be the first Buttons element");
                has_automatic = true;
                if (child->properties != nullptr)
                    fail("OOF2003", child, std::string(owner), "AutomaticButtons", "no attributes", "attribute", "Unknown AutomaticButtons attribute");
                std::set<std::string> included;
                for (xmlNodePtr automatic_child : element_children(child)) {
                    const auto name = node_name(automatic_child);
                    if (name == "Placement") {
                        if (state.placement || !state.included_buttons.empty() || automatic_child->properties != nullptr)
                            fail("OOF2003", automatic_child, std::string(owner), name, "one Placement before ButtonRef without attributes", "invalid Placement", "Invalid AutomaticButtons Placement");
                        const auto choices = element_children(automatic_child);
                        if (choices.size() != 1 || (node_name(choices.front()) != "BeforeButton" && node_name(choices.front()) != "AfterLastButton"))
                            fail("OOF2003", automatic_child, std::string(owner), name, "exactly one BeforeButton or AfterLastButton", "invalid choice", "Invalid AutomaticButtons Placement choice");
                        const auto choice = choices.front();
                        model::AutomaticButtonsPlacement placement;
                        if (node_name(choice) == "BeforeButton") placement.before_button = required_attribute(choice, "name", owner);
                        for (xmlAttrPtr attr = choice->properties; attr != nullptr; attr = attr->next)
                            if (node_name(choice) != "BeforeButton" || std::string_view(reinterpret_cast<const char*>(attr->name)) != "name")
                                fail("OOF2003", choice, std::string(owner), "Placement", "declared placement attributes", "unknown attribute", "Unknown placement attribute");
                        if (!element_children(choice).empty() || !node_text(choice).empty())
                            fail("OOF2003", choice, std::string(owner), "Placement", "empty placement target", "content", "Placement target cannot contain content");
                        state.placement = std::move(placement);
                    } else if (name == "ButtonRef") {
                        const auto target = required_attribute(automatic_child, "name", owner);
                        for (xmlAttrPtr attr = automatic_child->properties; attr != nullptr; attr = attr->next)
                            if (std::string_view(reinterpret_cast<const char*>(attr->name)) != "name")
                                fail("OOF2003", automatic_child, std::string(owner), name, "name attribute", "unknown attribute", "Unknown ButtonRef attribute");
                        if (target.empty() || !included.insert(target).second || !element_children(automatic_child).empty() || !node_text(automatic_child).empty())
                            fail("OOF2003", automatic_child, std::string(owner), name, "unique non-empty name without content", "invalid reference", "Invalid AutomaticButtons ButtonRef");
                        state.included_buttons.push_back(target);
                    } else fail("OOF2003", automatic_child, std::string(owner), name, "Placement or ButtonRef", name, "Unknown AutomaticButtons field");
                }
                continue;
            }
            auto item = parse_command_bar_button(child, owner);
            if (item.name.empty()) fail("OOF2003", child, std::string(owner), item.name, "non-empty item name", item.name, "Empty button menu item name");
            const auto [first, inserted] = first_named.try_emplace(item.name, item.type);
            if (!inserted && (first->second != model::CommandBarButtonKind::separator ||
                item.type != model::CommandBarButtonKind::action || !duplicate_names.insert(item.name).second))
                fail("OOF2003", child, std::string(owner), item.name, "unique name or Separator followed by one Action",
                    item.name, "Unsupported duplicate button menu item name");
            names.insert(item.name);
            result.push_back(std::move(item));
        }
        if (state.placement && state.placement->before_button && !names.contains(*state.placement->before_button))
            fail("OOF2003", node, std::string(owner), "BeforeButton", "existing sibling name", *state.placement->before_button, "Dangling AutomaticButtons placement");
        for (const auto& name : state.included_buttons)
            if (!names.contains(name)) fail("OOF2003", node, std::string(owner), "ButtonRef", "existing sibling name", name, "Dangling AutomaticButtons reference");
        return result;
    }

    std::vector<model::ChildItemRef> parse_child_items(xmlNodePtr node) {
        std::vector<model::ChildItemRef> children;
        for (xmlNodePtr child : element_children(node)) {
            if (node_name(child) == "Page") {
                children.emplace_back(parse_page(child));
            } else {
                children.emplace_back(parse_control(child));
            }
        }
        const auto control_for = [&](model::ObjectId id) -> model::ControlNode* {
            const auto found = std::ranges::find(objects_.controls, id, &model::ControlNode::id);
            return found == objects_.controls.end() ? nullptr : &*found;
        };
        bool has_z_order = false;
        for (const auto& child : children) {
            const auto* ref = std::get_if<model::ControlRef>(&child);
            const auto* control = ref ? control_for(ref->id()) : nullptr;
            if (control && control->position.z_order.is_explicit()) has_z_order = true;
        }
        if (has_z_order) {
            struct OrderedChild {
                model::ChildItemRef child;
                model::ControlNode* control;
                std::int32_t order;
                std::int32_t tab_order;
            };
            std::vector<OrderedChild> ordered;
            std::set<std::int32_t> orders;
            std::set<std::int32_t> tab_orders;
            if (children.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
                fail("OOF2003", node, {}, "ZOrder", "bounded sibling count", "too many", "Invalid display order");
            for (const auto& child : children) {
                const auto* ref = std::get_if<model::ControlRef>(&child);
                auto* control = ref ? control_for(ref->id()) : nullptr;
                if (!control)
                    fail("OOF2003", node, {}, "ZOrder", "control siblings", "Page or unresolved control", "Display order requires control siblings");
                if (!control->position.z_order.is_explicit()) continue;
                const auto z = control->position.z_order.value();
                if (!z || *z < 1 || static_cast<std::size_t>(*z) > children.size() || !orders.insert(*z).second)
                    fail("OOF2003", node, object_id_text(control->id), "ZOrder", "unique sibling position", z ? std::to_string(*z) : "undefined", "Display order conflicts with another sibling or is out of range");
            }
            std::int64_t next_free = 1;
            for (std::size_t index = 0; index < children.size(); ++index) {
                const auto* ref = std::get_if<model::ControlRef>(&children[index]);
                auto* control = ref ? control_for(ref->id()) : nullptr;
                const auto implicit = static_cast<std::int32_t>(index + 1);
                std::int32_t z = 0;
                if (control->position.z_order.is_explicit()) z = *control->position.z_order.value();
                else {
                    while (orders.contains(static_cast<std::int32_t>(next_free))) ++next_free;
                    z = static_cast<std::int32_t>(next_free++);
                }
                const auto tab = control->position.tab_order.is_explicit() ? control->position.tab_order.value() : std::optional<std::int32_t>{implicit};
                if (!tab || *tab < 1 || static_cast<std::size_t>(*tab) > children.size() || !tab_orders.insert(*tab).second)
                    fail("OOF2003", node, object_id_text(control->id), "TabOrder", "unique sibling position", tab ? std::to_string(*tab) : "undefined", "Tab order conflicts with another sibling or is out of range");
                ordered.push_back({children[index], control, z, *tab});
            }
            std::ranges::sort(ordered, {}, &OrderedChild::order);
            for (std::size_t index = 0; index < ordered.size(); ++index) {
                children[index] = ordered[index].child;
                auto& position = ordered[index].control->position;
                position.z_order.reset();
                if (ordered[index].tab_order == static_cast<std::int32_t>(index + 1)) position.tab_order.reset();
                else position.tab_order.set(std::optional<std::int32_t>{ordered[index].tab_order});
            }
        }
        return children;
    }

    const mm::Metamodel& metamodel_;
    std::shared_ptr<const MetadataContext> metadata_context_;
    ParsedObjects objects_;
    std::size_t chart_budget_ = 0;
    model::TypedValueBudget typed_budget_;
};

using XmlAttributes = std::vector<std::pair<std::string, std::string>>;

void append_xml_escaped(std::string& output, std::string_view value, bool attribute) {
    for (const char character : value) {
        switch (character) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '\r': output += "&#xD;"; break;
            case '\n':
                if (attribute) output += "&#xA;";
                else output.push_back(character);
                break;
            case '\t':
                if (attribute) output += "&#x9;";
                else output.push_back(character);
                break;
            case '"':
                if (attribute) output += "&quot;";
                else output.push_back(character);
                break;
            case '\'':
                if (attribute) output += "&apos;";
                else output.push_back(character);
                break;
            default: output.push_back(character); break;
        }
    }
}

class CanonicalXmlWriter {
public:
    CanonicalXmlWriter() {
        output_ = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    }

    void open(std::string_view name, const XmlAttributes& attributes = {}) {
        indent();
        output_.push_back('<');
        output_.append(name);
        append_attributes(attributes);
        output_.push_back('>');
        if (!mixed_) output_.push_back('\n');
        ++depth_;
    }

    void close(std::string_view name) {
        --depth_;
        indent();
        output_ += "</";
        output_.append(name);
        output_.push_back('>');
        if (!mixed_) output_.push_back('\n');
    }

    void empty(std::string_view name, const XmlAttributes& attributes = {}) {
        indent();
        output_.push_back('<');
        output_.append(name);
        append_attributes(attributes);
        output_ += "/>";
        if (!mixed_) output_.push_back('\n');
    }

    void text(
        std::string_view name,
        std::string_view value,
        const XmlAttributes& attributes = {}
    ) {
        indent();
        output_.push_back('<');
        output_.append(name);
        append_attributes(attributes);
        output_.push_back('>');
        append_xml_escaped(output_, value, false);
        output_ += "</";
        output_.append(name);
        output_.push_back('>');
        if (!mixed_) output_.push_back('\n');
    }

    void open_mixed(std::string_view name, std::string_view value, const XmlAttributes& attributes) {
        indent();
        output_.push_back('<');
        output_.append(name);
        append_attributes(attributes);
        output_.push_back('>');
        append_xml_escaped(output_, value, false);
        ++depth_;
        mixed_ = true;
    }

    void close_mixed(std::string_view name) {
        --depth_;
        output_ += "</";
        output_.append(name);
        output_ += ">\n";
        mixed_ = false;
    }

    [[nodiscard]] std::string take() {
        return std::move(output_);
    }

private:
    void indent() {
        if (!mixed_) output_.append(depth_ * 2, ' ');
    }

    void append_attributes(const XmlAttributes& attributes) {
        for (const auto& [name, value] : attributes) {
            output_.push_back(' ');
            output_.append(name);
            output_ += "=\"";
            append_xml_escaped(output_, value, true);
            output_.push_back('"');
        }
    }

    std::string output_;
    std::size_t depth_ = 0;
    bool mixed_ = false;
};

[[noreturn]] void serialization_fail(
    std::string object_id,
    std::string property,
    std::string expected,
    std::string actual,
    std::string message
) {
    throw AdapterError({
        "OOF2005",
        DiagnosticSeverity::error,
        std::move(object_id),
        "/Form",
        std::move(property),
        std::move(expected),
        std::move(actual),
        std::move(message),
    });
}

std::string_view type_domain_term_name(model::TypeDomainTerm term) {
    switch (term) {
        case model::TypeDomainTerm::unknown: return "unknown";
        case model::TypeDomainTerm::object: return "object";
        case model::TypeDomainTerm::list: return "list";
        case model::TypeDomainTerm::boolean: return "boolean";
        case model::TypeDomainTerm::binary: return "binary";
        case model::TypeDomainTerm::date: return "date";
        case model::TypeDomainTerm::numeric: return "numeric";
        case model::TypeDomainTerm::reference: return "reference";
        case model::TypeDomainTerm::string: return "string";
        case model::TypeDomainTerm::type: return "type";
        case model::TypeDomainTerm::value_list: return "valueList";
        case model::TypeDomainTerm::value_table: return "valueTable";
        case model::TypeDomainTerm::value_tree: return "valueTree";
    }
    return "unknown";
}

std::string_view color_kind_name(model::ColorKind kind) {
    switch (kind) {
        case model::ColorKind::absolute: return "absolute";
        case model::ColorKind::automatic: return "automatic";
        case model::ColorKind::style_reference: return "styleReference";
    }
    return {};
}

std::string_view font_kind_name(model::FontKind kind) {
    switch (kind) {
        case model::FontKind::absolute: return "absolute";
        case model::FontKind::windows_font: return "windowsFont";
        case model::FontKind::style_reference: return "styleReference";
        case model::FontKind::automatic: return "automatic";
    }
    return {};
}

std::string_view binding_coordinate_name(model::BindingCoordinate coordinate) {
    switch (coordinate) {
        case model::BindingCoordinate::left: return "left";
        case model::BindingCoordinate::top: return "top";
        case model::BindingCoordinate::right: return "right";
        case model::BindingCoordinate::bottom: return "bottom";
        case model::BindingCoordinate::vertical_center: return "verticalCenter";
        case model::BindingCoordinate::horizontal_center: return "horizontalCenter";
    }
    return {};
}

std::string_view binding_dimension_name(model::BindingDimension dimension) {
    switch (dimension) {
        case model::BindingDimension::width: return "width";
        case model::BindingDimension::height: return "height";
        case model::BindingDimension::minimum_width: return "minimumWidth";
        case model::BindingDimension::minimum_height: return "minimumHeight";
        case model::BindingDimension::stretch: return "stretch";
    }
    return {};
}

class DocumentSerializer {
public:
    DocumentSerializer(
        const model::OrdinaryFormDocument& document,
        const mm::Metamodel& metamodel
    ) : document_(document), metamodel_(metamodel) {}

    std::string serialize() {
        const model::Form& form = document_.form();
        const std::string form_id = object_id_text(form.id);
        std::vector<std::pair<std::string, std::string>> attributes{
            {"id", form_id},
            {"name", form.name},
            {"ordinaryFormVersion", std::string(ordinary_form_xml_version)},
        };
        if (!document_.reconstruction_complete()) {
            attributes.emplace_back("reconstructionComplete", "false");
        }
        writer_.open("Form", attributes);
        write_property_set(form.properties, metamodel_.form_properties(), form_id);
        if (form.main_attribute.has_value())
            writer_.empty("MainAttribute", {{"attributeId", object_id_text(form.main_attribute.id())}});
        if (form.extension) {
            const auto extension = std::ranges::find(mm::form_extensions, *form.extension,
                &mm::FormExtensionDescriptor::kind);
            if (extension == mm::form_extensions.end())
                serialization_fail(form_id, "FormExtension", "registered form extension", {},
                    "Form extension is not registered in the executable metamodel");
            const auto properties = metamodel_.form_extension_properties(*form.extension);
            const bool has_properties = std::ranges::any_of(properties, [&](const auto& property) {
                const auto* entry = form.extension_properties.find(property.id);
                return entry != nullptr && !equals_descriptor_default(property, entry->value);
            });
            if (!has_properties && form.saved_values.empty() && *form.extension != model::FormExtensionKind::catalog_item) {
                writer_.empty(extension->xml_name);
            } else {
                writer_.open(extension->xml_name);
                write_property_set(form.extension_properties, properties, form_id);
                if (!form.saved_values.empty()) {
                    writer_.open("SavedValues");
                    for (const auto& binding : form.saved_values) {
                        std::vector<std::pair<std::string, std::string>> binding_attributes{
                            {"sourceId", binding.source_id.canonical}, {"name", binding.name}};
                        if (binding.attribute.has_value())
                            binding_attributes.emplace_back("attributeId", object_id_text(binding.attribute.id()));
                        writer_.empty("SavedValue", binding_attributes);
                    }
                    writer_.close("SavedValues");
                }
                writer_.close(extension->xml_name);
            }
        }
        write_events("Events", form.events, metamodel_.form_events(), form_id);
        write_attributes();
        write_commands();
        write_assets();
        write_child_items(form.children);
        writer_.close("Form");
        return writer_.take();
    }

private:
    template <typename Value>
    const Value& require_value(
        const model::PropertyValue& value,
        std::string_view object_id,
        std::string_view property,
        std::string_view expected
    ) const {
        const Value* typed = std::get_if<Value>(&value);
        if (typed == nullptr) {
            serialization_fail(
                std::string(object_id),
                std::string(property),
                std::string(expected),
                "different model variant",
                "Property value does not match its metamodel codec");
        }
        return *typed;
    }

    void write_localized(
        std::string_view name,
        const model::LocalizedStringValue& value,
        std::string_view object_id
    ) {
        std::set<std::string> languages;
        for (const auto& item : value.items) {
            if (!languages.insert(item.language).second) {
                serialization_fail(
                    std::string(object_id),
                    std::string(name),
                    "one Item per language",
                    item.language,
                    "Localized string contains a duplicate language");
            }
        }
        if (value.items.empty()) {
            writer_.empty(name);
            return;
        }
        writer_.open(name);
        for (const auto& item : value.items)
            if (item.language == "#") writer_.text("DefaultText", item.text);
        for (const auto& item : value.items)
            if (item.language != "#") writer_.text("Item", item.text, {{"language", item.language}});
        writer_.close(name);
    }

    void append_style_attributes(
        XmlAttributes& attributes,
        const model::StyleReference& style,
        std::string_view object_id,
        std::string_view property
    ) const {
        if (const auto* name = std::get_if<model::QualifiedName>(&style)) {
            if (name->value.empty()) {
                serialization_fail(std::string(object_id), std::string(property), "nonempty styleName", "empty", "Style qualified name is empty");
            }
            attributes.emplace_back("styleName", name->value);
        } else if (const auto* composite = std::get_if<model::CompositeIdValue>(&style)) {
            if (composite->is_null ||
                (composite->object_id == 0 &&
                 canonical_uuid(composite->uuid.canonical) ==
                     "00000000-0000-0000-0000-000000000000")) {
                serialization_fail(std::string(object_id), std::string(property), "non-null style CompositeId", "null", "Style reference cannot be a null CompositeId");
            }
            attributes.emplace_back("styleObjectId", std::to_string(composite->object_id));
            attributes.emplace_back("styleUuid", canonical_uuid(composite->uuid.canonical));
        }
    }

    void write_line(std::string_view name, const model::LineValue& value, std::string_view object_id) {
        try { (void)storage::value_codec::encode_line(value); }
        catch (const std::exception& e) { serialization_fail(std::string(object_id), std::string(name), "known line", e.what(), "Invalid line"); }
        XmlAttributes attributes;
        attributes.emplace_back("type", value.type.type_name);
        attributes.emplace_back("member", value.type.member);
        attributes.emplace_back("width", std::to_string(value.width));
        attributes.emplace_back("gap", value.gap ? "true" : "false");
        writer_.empty(name, attributes);
    }

    void write_border(
        std::string_view name,
        const model::BorderValue& value,
        std::string_view object_id
    ) {
        if (!model::valid_border_value(value)) {
            serialization_fail(std::string(object_id), std::string(name), "valid typed Border", "invalid fields",
                "Border type, width, or style reference is invalid");
        }
        XmlAttributes attributes;
        if (value.kind == model::BorderKind::absolute) {
            const auto type = std::ranges::find(border_type_names, value.border_type,
                &std::pair<model::ControlBorderType, std::string_view>::first);
            attributes.emplace_back("kind", "absolute");
            attributes.emplace_back("borderType", std::string(type->second));
            attributes.emplace_back("width", std::to_string(value.width));
        } else {
            attributes.emplace_back("kind", "styleReference");
            append_style_attributes(attributes, value.style, object_id, name);
        }
        writer_.empty(name, attributes);
    }

    void write_type_domain(
        std::string_view name,
        const model::TypeDomainPatternValue& value,
        std::string_view object_id
    ) {
        if (value.entries.empty()) {
            writer_.empty(name);
            return;
        }
        writer_.open(name);
        for (const auto& entry : value.entries) {
            XmlAttributes attributes{{"term", std::string(type_domain_term_name(entry.term))}};
            switch (entry.term) {
                case model::TypeDomainTerm::unknown:
                    if (entry.type_uuid)
                        serialization_fail(std::string(object_id), "TypeDomain", "encoding unknown type", "object UUID",
                            "A concrete object UUID requires the named object term");
                    break;
                case model::TypeDomainTerm::object:
                    if (!entry.type_uuid)
                        serialization_fail(std::string(object_id), "TypeDomain", "encoding object type", "missing UUID",
                            "A concrete object type requires its UUID");
                    if (canonical_uuid(entry.type_uuid->canonical) == "e603c0f2-92fb-4d47-8f38-a44a381cf235")
                        serialization_fail(std::string(object_id), "TypeDomain", "named valueTree term", "object UUID",
                            "ValueTree requires its named type-domain term");
                    [[fallthrough]];
                case model::TypeDomainTerm::list:
                case model::TypeDomainTerm::reference:
                case model::TypeDomainTerm::type:
                    if (entry.type_uuid.has_value()) {
                        attributes.emplace_back("typeUuid", canonical_uuid(entry.type_uuid->canonical));
                    }
                    break;
                case model::TypeDomainTerm::value_list:
                    if (entry.type_uuid.has_value() ||
                        entry.numeric != model::NumericQualifiers{} ||
                        entry.string != model::LengthQualifiers{} ||
                        entry.binary != model::LengthQualifiers{} ||
                        entry.date != model::DateQualifiers{}) {
                        serialization_fail(std::string(object_id), std::string(name),
                            "unqualified ValueList descriptor", "UUID or qualifiers",
                            "ValueList type-domain entry cannot carry UUID or qualifiers");
                    }
                    break;
                case model::TypeDomainTerm::value_table:
                    if (entry.type_uuid.has_value() ||
                        entry.numeric != model::NumericQualifiers{} ||
                        entry.string != model::LengthQualifiers{} ||
                        entry.binary != model::LengthQualifiers{} ||
                        entry.date != model::DateQualifiers{}) {
                        serialization_fail(std::string(object_id), std::string(name),
                            "unqualified ValueTable descriptor", "UUID or qualifiers",
                            "ValueTable type-domain entry cannot carry UUID or qualifiers");
                    }
                    break;
                case model::TypeDomainTerm::value_tree:
                    if (entry.type_uuid.has_value() ||
                        entry.numeric != model::NumericQualifiers{} ||
                        entry.string != model::LengthQualifiers{} ||
                        entry.binary != model::LengthQualifiers{} ||
                        entry.date != model::DateQualifiers{}) {
                        serialization_fail(std::string(object_id), std::string(name),
                            "unqualified ValueTree descriptor", "UUID or qualifiers",
                            "ValueTree type-domain entry cannot carry UUID or qualifiers");
                    }
                    break;
                case model::TypeDomainTerm::boolean:
                    break;
                case model::TypeDomainTerm::numeric:
                    if (entry.numeric.length == 0 && entry.numeric.precision != 0) {
                        serialization_fail(std::string(object_id), std::string(name), "zero precision for zero length", std::to_string(entry.numeric.precision), "Numeric type-domain precision is not normalized");
                    }
                    if (entry.numeric.precision > entry.numeric.length && entry.numeric.length != 0) {
                        serialization_fail(std::string(object_id), std::string(name), "precision <= length", std::to_string(entry.numeric.precision), "Numeric type-domain precision exceeds length");
                    }
                    if (entry.numeric.length != 0) attributes.emplace_back("length", std::to_string(entry.numeric.length));
                    if (entry.numeric.precision != 0) attributes.emplace_back("precision", std::to_string(entry.numeric.precision));
                    if (entry.numeric.non_negative) attributes.emplace_back("nonNegative", "true");
                    break;
                case model::TypeDomainTerm::string:
                    if (entry.string.length != 0) attributes.emplace_back("length", std::to_string(entry.string.length));
                    if (!entry.string.variable) attributes.emplace_back("variable", "false");
                    break;
                case model::TypeDomainTerm::binary:
                    if (entry.binary.length != 0) attributes.emplace_back("length", std::to_string(entry.binary.length));
                    if (!entry.binary.variable) attributes.emplace_back("variable", "false");
                    break;
                case model::TypeDomainTerm::date:
                    if (!entry.date.date) attributes.emplace_back("date", "false");
                    if (!entry.date.time) attributes.emplace_back("time", "false");
                    break;
            }
            if (entry.type_name) {
                const bool reference_like = entry.term == model::TypeDomainTerm::object ||
                    entry.term == model::TypeDomainTerm::reference || entry.term == model::TypeDomainTerm::list ||
                    entry.term == model::TypeDomainTerm::type;
                if (!reference_like || !entry.type_uuid || !nonzero_uuid(entry.type_uuid->canonical) ||
                    trim_ascii(entry.type_name->value).empty())
                    serialization_fail(std::string(object_id), std::string(name), "named concrete type UUID",
                        "invalid type name", "Type name requires a concrete object, reference, list, or type UUID");
                attributes.emplace_back("typeName", entry.type_name->value);
            }
            writer_.empty("Entry", attributes);
        }
        writer_.close(name);
    }

    void write_composite_id(
        std::string_view name,
        const model::CompositeIdValue& value,
        std::string_view object_id
    ) {
        constexpr std::string_view null_uuid = "00000000-0000-0000-0000-000000000000";
        const std::string uuid = canonical_uuid(value.uuid.canonical);
        if ((value.is_null && (value.object_id != 0 || uuid != null_uuid)) ||
            (!value.is_null && value.object_id == 0 && uuid == null_uuid)) {
            serialization_fail(std::string(object_id), std::string(name), "coherent CompositeId", "inconsistent fields", "CompositeId null state is inconsistent");
        }
        writer_.empty(name, {
            {"objectId", std::to_string(value.object_id)},
            {"uuid", uuid},
            {"isNull", value.is_null ? "true" : "false"},
        });
    }

    void write_color(
        std::string_view name,
        const model::ColorValue& value,
        std::string_view object_id
    ) {
        XmlAttributes attributes{{"kind", std::string(color_kind_name(value.kind))}};
        const bool default_channels =
            value.red == 0 && value.green == 0 && value.blue == 0 && value.alpha == 255;
        const bool has_style = !std::holds_alternative<std::monostate>(value.style);
        if (value.kind == model::ColorKind::absolute) {
            if (has_style) {
                serialization_fail(std::string(object_id), std::string(name), "absolute color without style", "style", "Absolute color carries a style reference");
            }
            attributes.emplace_back("red", std::to_string(value.red));
            attributes.emplace_back("green", std::to_string(value.green));
            attributes.emplace_back("blue", std::to_string(value.blue));
            attributes.emplace_back("alpha", std::to_string(value.alpha));
        } else if (value.kind == model::ColorKind::automatic) {
            if (!default_channels || has_style) {
                serialization_fail(std::string(object_id), std::string(name), "automatic color defaults", "explicit color data", "Automatic color carries non-default data");
            }
        } else {
            if (!default_channels || !has_style) {
                serialization_fail(std::string(object_id), std::string(name), "one style reference", "invalid style color", "Style color is incomplete or carries channels");
            }
            append_style_attributes(attributes, value.style, object_id, name);
        }
        writer_.empty(name, attributes);
    }

    void write_font(
        std::string_view name,
        const model::FontValue& value,
        std::string_view object_id
    ) {
        XmlAttributes attributes{
            {"kind", std::string(font_kind_name(value.kind))},
        };
        const bool has_style = !std::holds_alternative<std::monostate>(value.style);
        if ((value.kind == model::FontKind::style_reference) != has_style) {
            serialization_fail(std::string(object_id), std::string(name), "style only for styleReference font", has_style ? "unexpected style" : "missing style", "Font style representation is inconsistent");
        }
        if (has_style) append_style_attributes(attributes, value.style, object_id, name);
        if ((value.kind == model::FontKind::windows_font) != value.windows_font.has_value())
            serialization_fail(std::string(object_id), std::string(name), "selector required only for Windows font", "invalid selector", "Windows font selector is inconsistent");
        if (value.windows_font) {
            const auto index = static_cast<std::size_t>(*value.windows_font);
            if (index >= windows_font_names.size())
                serialization_fail(std::string(object_id), std::string(name), "unmodified Windows font", "unsupported font data", "Windows font is invalid");
            attributes.emplace_back("windowsFont", std::string(windows_font_names[index]));
        }
        try { (void)storage::value_codec::encode_font(value); }
        catch (const std::exception& error) { serialization_fail(std::string(object_id), std::string(name), "supported Font", {}, error.what()); }
        if (value.face_name) attributes.emplace_back("faceName", *value.face_name);
        if (value.height) {
            if (!std::isfinite(*value.height)) {
                serialization_fail(std::string(object_id), std::string(name), "finite height", "non-finite", "Font height is not finite");
            }
            attributes.emplace_back("height", format_double(*value.height));
        }
        if (value.bold) attributes.emplace_back("bold", *value.bold ? "true" : "false");
        if (value.italic) attributes.emplace_back("italic", *value.italic ? "true" : "false");
        if (value.underline) attributes.emplace_back("underline", *value.underline ? "true" : "false");
        if (value.strikeout) attributes.emplace_back("strikeout", *value.strikeout ? "true" : "false");
        if (value.scale != 100.0 || value.scale_override) {
            if (!std::isfinite(value.scale)) {
                serialization_fail(std::string(object_id), std::string(name), "finite scale", "non-finite", "Font scale is not finite");
            }
            attributes.emplace_back("scale", format_double(value.scale));
        }
        if (value.scale_override) attributes.emplace_back("scaleOverride", "true");
        if (!value.auto_char_set) attributes.emplace_back("autoCharSet", "false");
        writer_.empty(name, attributes);
    }

    void write_picture_reference(
        std::string_view element_name,
        const model::PictureRef& reference,
        std::string_view object_id
    ) {
        XmlAttributes attributes;
        if (reference.transparent_x < -1 || reference.transparent_y < -1)
            serialization_fail(std::string(object_id), std::string(element_name), "transparency coordinates at least -1", {}, "Invalid picture transparency coordinates");
        if (reference.transparent_x != -1) attributes.emplace_back("transparentX", std::to_string(reference.transparent_x));
        if (reference.transparent_y != -1) attributes.emplace_back("transparentY", std::to_string(reference.transparent_y));
        if (reference.transparent) attributes.emplace_back("transparent", "true");
        if (reference.common_picture) {
            const auto uuid = canonical_uuid(reference.common_picture->canonical);
            if (reference.standard_name || reference.asset.id() || uuid == "00000000-0000-0000-0000-000000000000")
                serialization_fail(std::string(object_id), std::string(element_name),
                    "one nonzero CommonPicture UUID", {}, "Picture reference has inconsistent targets");
            attributes.emplace_back("commonPicture", uuid);
            writer_.empty(element_name, attributes);
            return;
        }
        if (reference.standard_name) {
            if (reference.asset.id().value() != 0 ||
                mm::find_standard_picture(reference.standard_name->value) == nullptr) {
                serialization_fail(std::string(object_id), std::string(element_name),
                    "known standard picture name with empty asset target", "invalid target",
                    "Picture reference has inconsistent targets");
            }
            attributes.emplace_back("standardName", reference.standard_name->value);
            writer_.empty(element_name, attributes);
            return;
        }
        if (!attributes.empty())
            serialization_fail(std::string(object_id), std::string(element_name), "file asset without transparency coordinates", {},
                "File picture transparency coordinates belong to the asset");
        if (reference.asset.id().value() == 0) {
            serialization_fail(std::string(object_id), std::string(element_name),
                "positive picture asset ID", "0", "Picture asset reference is empty");
        }
        writer_.text(element_name, object_id_text(reference.asset.id()));
    }

    void write_formatted_string(std::string_view name, const model::FormattedStringValue& value, std::string_view owner) {
        writer_.open(name, {{"formatted", value.formatted ? "true" : "false"}});
        std::visit([&](const auto& content) {
            using Content = std::decay_t<decltype(content)>;
            if constexpr (std::is_same_v<Content, std::string>) {
                writer_.open("Value");
                writer_.text("String", content);
                writer_.close("Value");
            } else write_localized("Value", content, owner);
        }, value.value);
        writer_.close(name);
    }

    void write_value_list(const model::ValueListValue& value, std::string_view owner) {
        std::int64_t default_last = -1;
        for (const auto& entry : value.items) default_last = std::max(default_last, static_cast<std::int64_t>(entry.id));
        XmlAttributes attributes;
        const auto effective_last = value.effective_last_assigned_id();
        if (effective_last != default_last) attributes.emplace_back("lastAssignedId", std::to_string(effective_last));
        if (value.items.empty() && value.value_type.entries.empty() && value.available_values.empty()) {
            writer_.empty("ValueList", attributes);
        } else {
            writer_.open("ValueList", attributes);
            if (!value.value_type.entries.empty()) write_type_domain("ValueType", value.value_type, owner);
            if (!value.available_values.empty()) {
                writer_.open("AvailableValues");
                write_value_list(value.available_values.front(), owner);
                writer_.close("AvailableValues");
            }
            if (!value.items.empty()) {
                writer_.open("Items");
                for (const auto& entry : value.items) {
                    writer_.open("Item", {{"id", std::to_string(entry.id)}});
                    writer_.open("Value");
                    write_typed_value_node(entry.value, owner);
                    writer_.close("Value");
                    const auto* plain = std::get_if<std::string>(&entry.presentation);
                    if (!plain || !plain->empty()) {
                        writer_.open("Presentation");
                        write_typed_value_node(entry.presentation, owner);
                        writer_.close("Presentation");
                    }
                    if (entry.checked) writer_.text("Check", "true");
                    if (entry.picture) write_picture_reference("Picture", *entry.picture, owner);
                    writer_.close("Item");
                }
                writer_.close("Items");
            }
            writer_.close("ValueList");
        }
    }

    void write_typed_value_node(const model::PropertyValue& value, std::string_view owner) {
        if (std::holds_alternative<model::UndefinedValue>(value)) writer_.text("Undefined", "undefined");
        else if (std::holds_alternative<model::NullValue>(value)) writer_.empty("Null");
        else if (const auto* item = std::get_if<model::UuidValue>(&value)) writer_.text("UUID", canonical_uuid(item->canonical));
        else if (const auto* item = std::get_if<model::PlatformTypeValue>(&value)) {
            const auto descriptors = mm::platform_value_type_descriptors();
            if (std::ranges::find(descriptors, item->name, &mm::PlatformValueTypeDescriptor::name) == descriptors.end())
                serialization_fail(std::string(owner), "Type", "known platform type", item->name, "Unsupported platform type value");
            writer_.text("Type", item->name);
        }
        else if (const auto* item = std::get_if<std::string>(&value)) writer_.text("String", *item);
        else if (const auto* item = std::get_if<model::DecimalValue>(&value)) writer_.text("Number", canonical_decimal(item->canonical, nullptr, "Value", owner));
        else if (const auto* item = std::get_if<std::int64_t>(&value)) writer_.text("Number", std::to_string(*item));
        else if (const auto* item = std::get_if<bool>(&value)) writer_.text("Boolean", *item ? "true" : "false");
        else if (const auto* item = std::get_if<model::DateValue>(&value)) { (void)storage::value_codec::date_to_platform(item->canonical); writer_.text("Date", item->canonical); }
        else if (const auto* item = std::get_if<model::FormattedStringValue>(&value)) write_formatted_string("FormattedString", *item, owner);
        else if (const auto* item = std::get_if<model::ColorValue>(&value)) write_color("Color", *item, owner);
        else if (const auto* item = std::get_if<model::FontValue>(&value)) write_font("Font", *item, owner);
        else if (const auto* item = std::get_if<model::BorderValue>(&value)) write_border("Border", *item, owner);
        else if (const auto* item = std::get_if<model::TypeDomainPatternValue>(&value)) write_type_domain("TypeDescription", *item, owner);
        else if (const auto* item = std::get_if<model::ValueListValue>(&value)) write_value_list(*item, owner);
        else if (const auto* item = std::get_if<model::ArrayValue>(&value)) {
            const std::string_view name = item->fixed ? "FixedArray" : "Array";
            writer_.open(name);
            for (const auto& nested : item->items) {
                writer_.open("Item");
                write_typed_value_node(nested, owner);
                writer_.close("Item");
            }
            writer_.close(name);
        } else if (const auto* item = std::get_if<model::StructureValue>(&value)) {
            const std::string_view name = item->fixed ? "FixedStructure" : "Structure";
            writer_.open(name);
            for (const auto& field : item->fields) {
                writer_.open("Field", {{"name", field.name}});
                write_typed_value_node(field.value, owner);
                writer_.close("Field");
            }
            writer_.close(name);
        } else if (const auto* item = std::get_if<model::MapValue>(&value)) {
            struct OrderedEntry {
                std::string key;
                const model::MapEntry* entry;
            };
            std::vector<OrderedEntry> ordered;
            ordered.reserve(item->entries.size());
            for (const auto& entry : item->entries) {
                auto key = model::typed_map_key(entry.key);
                if (!key) serialization_fail(std::string(owner), "Key", "supported canonical Map key", "unsupported",
                    "Map key cannot be written in canonical XML");
                ordered.push_back({std::move(*key), &entry});
            }
            std::ranges::sort(ordered, {}, &OrderedEntry::key);
            const std::string_view name = item->fixed ? "FixedMap" : "Map";
            writer_.open(name);
            for (const auto& view : ordered) {
                writer_.open("Entry");
                writer_.open("Key");
                write_typed_value_node(view.entry->key, owner);
                writer_.close("Key");
                writer_.open("Value");
                write_typed_value_node(view.entry->value, owner);
                writer_.close("Value");
                writer_.close("Entry");
            }
            writer_.close(name);
        } else serialization_fail(std::string(owner), "Value", "supported named typed value", "unsupported", "Unsupported typed value kind");
    }

    void write_property(
        const mm::PropertyDescriptor& descriptor,
        const model::PropertyValue& value,
        std::string_view object_id,
        const model::ChartPayload* chart_context = nullptr
    ) {
        const std::string_view name = descriptor.xml_name;
        switch (descriptor.value_codec) {
            case mm::ValueCodec::calendar_selected_dates:
                serialization_fail(std::string(object_id), std::string(name), "runtime-only calendar selection", "XML",
                    "Runtime calendar selections cannot be serialized");
            case mm::ValueCodec::runtime_value:
                serialization_fail(std::string(object_id), std::string(name), "runtime-only value", "XML",
                    "Runtime values cannot be serialized");
            case mm::ValueCodec::table_columns:
                serialization_fail(std::string(object_id), std::string(name), "named Table Columns", "scalar collection",
                    "Table columns must use their owned named object model");
            case mm::ValueCodec::value_list_reference:
            case mm::ValueCodec::value_list_item_reference:
                serialization_fail(std::string(object_id), std::string(name), "runtime-only list reference", "XML",
                    "Runtime value list references cannot be serialized");
            case mm::ValueCodec::owned_panel:
                serialization_fail(std::string(object_id), std::string(name), "typed Form.Panel", "scalar", "Owned Panel is not a scalar property");
            case mm::ValueCodec::typed_value:
                writer_.open(name);
                write_typed_value_node(value, object_id);
                writer_.close(name);
                return;
            case mm::ValueCodec::unclassified:
                serialization_fail(std::string(object_id), std::string(name), "classified codec", "unclassified", "Unclassified property cannot be serialized");
            case mm::ValueCodec::boolean:
                writer_.text(name, require_value<bool>(value, object_id, name, "Boolean") ? "true" : "false");
                return;
            case mm::ValueCodec::integer:
                writer_.text(name, std::to_string(require_value<std::int64_t>(value, object_id, name, "integer")));
                return;
            case mm::ValueCodec::integer32: {
                const std::int64_t integer =
                    require_value<std::int64_t>(value, object_id, name, "32-bit integer");
                if (integer < std::numeric_limits<std::int32_t>::min() ||
                    integer > std::numeric_limits<std::int32_t>::max()) {
                    serialization_fail(std::string(object_id), std::string(name), "32-bit integer", std::to_string(integer), "Property integer is outside its metamodel range");
                }
                writer_.text(name, std::to_string(integer));
                return;
            }
            case mm::ValueCodec::decimal:
                if (const auto* integer = std::get_if<std::int64_t>(&value)) {
                    writer_.text(name, std::to_string(*integer));
                } else {
                    const auto& decimal = require_value<model::DecimalValue>(value, object_id, name, "decimal");
                    writer_.text(name, canonical_decimal(decimal.canonical, nullptr, name, object_id));
                }
                return;
            case mm::ValueCodec::string:
                writer_.text(name, require_value<std::string>(value, object_id, name, "string"));
                return;
            case mm::ValueCodec::localized_text:
                if (const auto* plain = std::get_if<std::string>(&value)) {
                    writer_.text(name, *plain);
                } else {
                    const auto& translations = require_value<model::LocalizedStringValue>(
                        value, object_id, name, "localized text");
                    if (translations.items.size() == 1 && translations.items.front().language == "ru" &&
                        !translations.items.front().text.empty()) {
                        writer_.text(name, translations.items.front().text);
                    } else {
                        write_localized(name, translations, object_id);
                    }
                }
                return;
            case mm::ValueCodec::localized_string:
                write_localized(name, require_value<model::LocalizedStringValue>(value, object_id, name, "localized string"), object_id);
                return;
            case mm::ValueCodec::formatted_string: {
                const auto& formatted = require_value<model::FormattedStringValue>(value, object_id, name, "formatted string");
                write_formatted_string(name, formatted, object_id);
                return;
            }
            case mm::ValueCodec::date:
                if (std::holds_alternative<model::UndefinedValue>(value)) {
                    writer_.text(name, "undefined");
                } else {
                    const auto& date = require_value<model::DateValue>(value, object_id, name, "Date or Undefined");
                    (void)storage::value_codec::date_to_platform(date.canonical);
                    writer_.text(name, date.canonical);
                }
                return;
            case mm::ValueCodec::uuid:
                writer_.text(name, canonical_uuid(require_value<model::UuidValue>(value, object_id, name, "UUID").canonical));
                return;
            case mm::ValueCodec::style: {
                const auto& style = require_value<model::StyleValue>(value, object_id, name, "Style value");
                if (style.configuration_style) {
                    const std::string uuid = canonical_uuid(style.configuration_style->canonical);
                    if (uuid == "00000000-0000-0000-0000-000000000000")
                        serialization_fail(std::string(object_id), std::string(name), "nonzero configuration style UUID", uuid,
                            "Style reference UUID must be nonzero");
                    writer_.empty(name, {{"styleId", uuid}});
                } else writer_.empty(name);
                return;
            }
            case mm::ValueCodec::composite_id:
                write_composite_id(name, require_value<model::CompositeIdValue>(value, object_id, name, "CompositeId"), object_id);
                return;
            case mm::ValueCodec::type_domain:
                write_type_domain(name, require_value<model::TypeDomainPatternValue>(value, object_id, name, "type domain"), object_id);
                return;
            case mm::ValueCodec::enumeration: {
                const auto& enumeration = require_value<model::EnumerationValue>(value, object_id, name, "enumeration");
                writer_.empty(name, {{"type", enumeration.type_name}, {"member", enumeration.member}});
                return;
            }
            case mm::ValueCodec::color:
                write_color(name, require_value<model::ColorValue>(value, object_id, name, "color"), object_id);
                return;
            case mm::ValueCodec::line:
                write_line(name, require_value<model::LineValue>(value, object_id, name, "line"), object_id);
                return;
            case mm::ValueCodec::border:
                write_border(name, require_value<model::BorderValue>(value, object_id, name, "border"), object_id);
                return;
            case mm::ValueCodec::font:
                write_font(name, require_value<model::FontValue>(value, object_id, name, "font"), object_id);
                return;
            case mm::ValueCodec::shortcut: {
                const auto& shortcut = require_value<model::ShortcutValue>(
                    value, object_id, name, "Shortcut");
                if (mm::find_shortcut_key(shortcut.key) == nullptr) {
                    serialization_fail(std::string(object_id), std::string(name),
                        "platform named key", shortcut.key, "Shortcut Key is not supported");
                }
                writer_.open(name, {
                    {"Alt", shortcut.alt ? "true" : "false"},
                    {"Ctrl", shortcut.ctrl ? "true" : "false"},
                    {"Shift", shortcut.shift ? "true" : "false"},
                });
                writer_.text("Key", shortcut.key);
                writer_.close(name);
                return;
            }
            case mm::ValueCodec::picture:
            {
                const auto& reference = require_value<model::PictureRef>(value, object_id, name, "picture reference");
                write_picture_reference(name, reference, object_id);
                return;
            }
            case mm::ValueCodec::chart_series_reference: {
                const auto& reference = require_value<model::ChartSeriesRef>(value, object_id, name, "Chart series reference");
                if (object_id_text(reference.owner) != object_id)
                    serialization_fail(std::string(object_id), std::string(name), "series owned by Chart", "different owner", "Chart series reference has a different owner");
                const auto* control = document_.find_control(reference.owner);
                const auto* chart = chart_context ? chart_context : control ? std::get_if<model::ChartPayload>(&control->payload) : nullptr;
                if (!chart || std::ranges::none_of(chart->series, [&](const auto& series) { return series.id == reference.series; }))
                    serialization_fail(std::string(object_id), std::string(name), "existing Chart series", "missing", "Chart series reference does not exist");
                writer_.text(name, object_id_text(reference.series));
                return;
            }
            case mm::ValueCodec::chart_point_reference: {
                if (std::holds_alternative<model::UndefinedValue>(value)) {
                    writer_.empty(name);
                    return;
                }
                const auto& reference = require_value<model::ChartPointRef>(value, object_id, name, "Chart point reference");
                if (object_id_text(reference.owner) != object_id)
                    serialization_fail(std::string(object_id), std::string(name), "point owned by Chart", "different owner", "Chart point reference has a different owner");
                const auto* control = document_.find_control(reference.owner);
                const auto* chart = chart_context ? chart_context : control ? std::get_if<model::ChartPayload>(&control->payload) : nullptr;
                if (!chart || std::ranges::none_of(chart->points, [&](const auto& point) { return point.id == reference.point; }))
                    serialization_fail(std::string(object_id), std::string(name), "existing Chart point", "missing", "Chart point reference does not exist");
                writer_.text(name, object_id_text(reference.point));
                return;
            }
            case mm::ValueCodec::context_menu_reference:
                if (std::holds_alternative<model::UndefinedValue>(value)) {
                    writer_.empty(name);
                } else if (const auto* control = std::get_if<model::ControlRef>(&value)) {
                    writer_.empty(name, {{"controlId", object_id_text(control->id())}});
                } else {
                    const auto& reference = require_value<model::CommandBarButtonRef>(value, object_id, name, "context menu reference");
                    if (reference.path.empty()) serialization_fail(std::string(object_id), std::string(name),
                        "non-empty button path", "empty", "ContextMenu button path is empty");
                    writer_.open(name, {{"controlId", object_id_text(reference.owner.id())}});
                    for (const auto& segment : reference.path) {
                        if (segment.empty()) serialization_fail(std::string(object_id), std::string(name),
                            "non-empty button name", "empty", "ContextMenu button path is empty");
                        writer_.empty("Button", {{"name", segment}});
                    }
                    writer_.close(name);
                }
                return;
            case mm::ValueCodec::action_source_reference:
                if (std::holds_alternative<model::UndefinedValue>(value)) {
                    writer_.empty(name);
                } else if (const auto* form = std::get_if<model::FormRef>(&value)) {
                    writer_.empty(name, {{"formId", object_id_text(form->id())}});
                } else {
                    writer_.empty(name, {{"controlId", object_id_text(
                        require_value<model::ControlRef>(value, object_id, name, "form or control reference").id())}});
                }
                return;
            case mm::ValueCodec::command_bar_buttons:
                serialization_fail(std::string(object_id), std::string(name), "owned Buttons collection", "scalar", "Buttons is not a scalar property");
            case mm::ValueCodec::form_controls:
            case mm::ValueCodec::panel_pages:
                serialization_fail(std::string(object_id), std::string(name), "named ChildItems", "scalar collection",
                    "Form item collections cannot be serialized as scalar properties");
            case mm::ValueCodec::page_reference:
                serialization_fail(std::string(object_id), std::string(name), "runtime-only page reference", "XML",
                    "Runtime page selection cannot be serialized");
            case mm::ValueCodec::chart_trendlines:
            case mm::ValueCodec::chart_equation_area:
            case mm::ValueCodec::chart_plot_area:
            case mm::ValueCodec::chart_title_area:
            case mm::ValueCodec::chart_legend_area:
            case mm::ValueCodec::chart_series_collection:
            case mm::ValueCodec::chart_points_collection:
            case mm::ValueCodec::chart_gauge_quality_bands:
                serialization_fail(std::string(object_id), std::string(name), "owned Chart collection", "scalar", "Chart collection cannot be serialized as a scalar property");
            case mm::ValueCodec::dendrogram_items:
                serialization_fail(std::string(object_id), std::string(name), "owned Items collection", "scalar", "Dendrogram Items is not a scalar property");
            case mm::ValueCodec::dendrogram_links:
                serialization_fail(std::string(object_id), std::string(name), "owned Links collection", "scalar", "Dendrogram Links is not a scalar property");
            case mm::ValueCodec::control_reference:
                writer_.text(name, object_id_text(require_value<model::ControlRef>(value, object_id, name, "control reference").id()));
                return;
            case mm::ValueCodec::form_reference:
                serialization_fail(std::string(object_id), std::string(name), "runtime-only form reference", "XML", "Runtime form reference cannot be serialized");
            case mm::ValueCodec::attribute_reference:
                writer_.text(name, object_id_text(require_value<model::AttributeRef>(value, object_id, name, "attribute reference").id()));
                return;
            case mm::ValueCodec::command_reference:
                writer_.text(name, object_id_text(require_value<model::CommandRef>(value, object_id, name, "command reference").id()));
                return;
        }
    }

    void write_chart_family_value(const mm::ChartSeriesPropertyDescriptor& field, const model::PropertyValue& value, std::string_view owner) {
        mm::PropertyDescriptor property;
        property.xml_name = field.name;
        property.value_codec = field.value_codec;
        write_property(property, value, owner);
    }
    void write_chart_series_fields(mm::ConstChartSeriesObject object, std::string_view owner) {
        const bool summary = std::holds_alternative<const model::ChartSummarySeries*>(object);
        const auto fields = mm::chart_series_property_descriptors();
        for (const auto name : summary ? std::array<std::string_view, 4>{"Text", "Value", "Color", "Marker"} : std::array<std::string_view, 4>{"Text", "Color", "Marker", "Value"}) {
            const auto field = std::ranges::find(fields, name, &mm::ChartSeriesPropertyDescriptor::name);
            const auto value = mm::chart_series_property_value(object, *field);
            if (!value || (name == "Value" && std::holds_alternative<model::UndefinedValue>(*value)) ||
                (summary && (name == "Color" || name == "Marker") && *value == field->default_value)) continue;
            write_chart_family_value(*field, *value, owner);
        }
        for (const auto& field : fields) {
            if (field.value_codec == mm::ValueCodec::chart_trendlines) {
                std::visit([&](auto* item) {
                    if (item->trendlines.empty()) return;
                    writer_.open("Trendlines");
                    for (const auto& trend : item->trendlines) {
                        writer_.open("Trendline");
                        for (const auto& definition : mm::chart_trendline_property_descriptors()) {
                            if (definition.value_codec == mm::ValueCodec::chart_equation_area) {
                                if (trend.equation_area.empty()) continue;
                                writer_.open("EquationArea");
                                for (const auto& property : mm::chart_label_area_property_descriptors()) if (const auto* entry = trend.equation_area.find(property.id)) {
                                    mm::PropertyDescriptor descriptor;
                                    descriptor.xml_name = property.name;
                                    descriptor.value_codec = property.value_codec;
                                    write_property(descriptor, entry->value, owner);
                                }
                                writer_.close("EquationArea");
                            } else if (const auto* entry = trend.properties.find(definition.id)) write_chart_family_value(definition, entry->value, owner);
                        }
                        writer_.close("Trendline");
                    }
                    writer_.close("Trendlines");
                }, object);
            } else if (field.fixed == mm::ChartSeriesFixed::none && field.storage != mm::ChartSeriesStorage::computed && (!field.normal_runtime_only || summary))
                if (const auto value = mm::chart_series_property_value(object, field)) write_chart_family_value(field, *value, owner);
        }
    }

    void write_property_set(
        const model::PropertySet& properties,
        std::span<const mm::PropertyDescriptor> descriptors,
        std::string_view object_id,
        bool skip_reserved_extensions = false,
        std::optional<model::ControlKind> control_kind = std::nullopt,
        const model::ChartPayload* chart_context = nullptr
    ) {
        for (const auto& descriptor : descriptors) {
            if ((descriptor.control_kind == model::ControlKind::input_field ||
                 descriptor.control_kind == model::ControlKind::choice_field) &&
                descriptor.api_name == "SelButtonPicture") {
                if (properties.find(descriptor.id) != nullptr) {
                    serialization_fail(std::string(object_id), std::string(descriptor.xml_name),
                        "canonical ChoiceButtonPicture property", "noncanonical alias",
                        "Picture aliases cannot be stored as separate XML properties");
                }
                continue;
            }
            if (control_kind && descriptor.control_kind != model::ControlKind::panel &&
                descriptor.control_kind != *control_kind) {
                continue;
            }
            if (descriptor.value_codec == mm::ValueCodec::owned_panel) {
                const auto& properties = document_.form().panel.properties;
                bool explicit_panel = false;
                properties.for_each_explicit([&](const model::PropertyEntry& entry) {
                    const auto* property = metamodel_.root_panel_property(entry.id);
                    if (property == nullptr)
                        serialization_fail(std::string(object_id), "Panel",
                            "supported main Panel property", "unsupported property",
                            "Form.Panel property is not declared by the executable metamodel");
                    if (!equals_descriptor_default(*property, entry.value)) explicit_panel = true;
                });
                if (explicit_panel) {
                    writer_.open(descriptor.xml_name);
                    write_property_set(properties, metamodel_.root_panel_properties(), object_id);
                    writer_.close(descriptor.xml_name);
                }
                continue;
            }
            if (skip_reserved_extensions &&
                (descriptor.api_name == "Name" || descriptor.api_name == "Data")) {
                continue;
            }
            const model::PropertyEntry* entry = properties.find(descriptor.id);
            if (entry != nullptr && descriptor.persistence == mm::PersistenceClass::runtime_only) {
                serialization_fail(std::string(object_id), std::string(descriptor.xml_name),
                    "persisted property", "runtime-only value",
                    "Runtime-only properties cannot be serialized into public Form.xml");
            }
            if (entry != nullptr && !equals_descriptor_default(descriptor, entry->value)) {
                write_property(descriptor, entry->value, object_id, chart_context);
            }
        }
    }

    void write_standard_action(const model::StandardMenuAction& action) {
        const auto* descriptor = mm::find_standard_menu_command(action.command);
        const char* command = descriptor == nullptr ? nullptr : descriptor->public_name.data();
        const char* source = action.source == model::StandardMenuActionSource::form ? "Form" :
            action.source == model::StandardMenuActionSource::all_sources ? "AllSources" :
            action.source == model::StandardMenuActionSource::control ? "Control" : nullptr;
        const char* context = action.context == model::StandardMenuActionContext::default_context ? "Default" :
            action.context == model::StandardMenuActionContext::command_bar ? "CommandBar" :
            action.context == model::StandardMenuActionContext::form ? "Form" : nullptr;
        if (command == nullptr || source == nullptr || context == nullptr)
            throw std::invalid_argument("Unknown StandardAction command, context, or source");
        XmlAttributes attributes{{"command", command}, {"context", context}};
        if (action.context == model::StandardMenuActionContext::command_bar)
            attributes.emplace_back("commandBarId", object_id_text(action.command_bar.id()));
        attributes.emplace_back("source", source);
        if (action.source == model::StandardMenuActionSource::control)
            attributes.emplace_back("sourceControlId", object_id_text(action.source_control.id()));
        writer_.empty("StandardAction", attributes);
    }

    void write_events(
        std::string_view container_name,
        const std::vector<model::EventRef>& references,
        std::span<const mm::EventDescriptor> descriptors,
        std::string_view owner_id
    ) {
        if (references.empty()) return;
        std::vector<std::pair<const mm::EventDescriptor*, const model::Event*>> ordered;
        for (const auto& descriptor : descriptors) {
            for (const model::EventRef reference : references) {
                const model::Event* event = document_.find_event(reference.id());
                if (event != nullptr) {
                    if (event->name == descriptor.api_name) ordered.emplace_back(&descriptor, event);
                }
            }
        }
        if (ordered.size() != references.size()) {
            serialization_fail(std::string(owner_id), "Events", "all events declared for this owner", std::to_string(ordered.size()), "Event sequence cannot be projected without loss");
        }
        writer_.open(container_name);
        for (const auto& [descriptor, event] : ordered) {
            if (event->standard_action) {
                writer_.open(descriptor->xml_name, {{"id", object_id_text(event->id)}});
                write_standard_action(*event->standard_action);
                writer_.close(descriptor->xml_name);
                continue;
            }
            if (!event->action_metadata) {
                writer_.text(descriptor->xml_name, event->handler, {{"id", object_id_text(event->id)}});
                continue;
            }
            const auto& action = *event->action_metadata;
            for (const auto* value : {&action.text, &action.tooltip, &action.description}) {
                if (value->items.size() > model::max_localized_text_items) {
                    serialization_fail(std::string(owner_id), "Action", "at most 65536 translations",
                        "too many translations", "Event action exceeds the translation limit");
                }
            }
            writer_.open_mixed(descriptor->xml_name, event->handler, {{"id", object_id_text(event->id)}});
            writer_.open("Action", {{"name", action.name}});
            write_localized("Text", action.text, owner_id);
            write_localized("ToolTip", action.tooltip, owner_id);
            write_localized("Description", action.description, owner_id);
            writer_.close("Action");
            writer_.close_mixed(descriptor->xml_name);
        }
        writer_.close(container_name);
    }

    void write_attributes() {
        const auto& attributes = document_.collections().attributes;
        if (attributes.empty()) return;
        writer_.open("Attributes");
        for (const auto& attribute : attributes) {
            const std::string id = object_id_text(attribute.id);
            writer_.open("Attribute", {{"id", id}, {"name", attribute.name}});
            write_type_domain("TypeDomain", attribute.type, id);
            if (attribute.main.is_explicit() && attribute.main.value()) writer_.text("Main", "true");
            if (attribute.stored_data.is_explicit() && attribute.stored_data.value()) writer_.text("StoredData", "true");
            writer_.close("Attribute");
        }
        writer_.close("Attributes");
    }

    void write_commands() {
        const auto& commands = document_.collections().commands;
        if (commands.empty()) return;
        writer_.open("Commands");
        for (const auto& command : commands) {
            const std::string id = object_id_text(command.id);
            writer_.open("Command", {
                {"id", id},
                {"name", command.name},
                {"handler", command.handler},
            });
            if (command.title.is_explicit() && !command.title.value().items.empty()) {
                write_localized("Title", command.title.value(), id);
            }
            if (command.changes_data.is_explicit() && command.changes_data.value()) {
                writer_.text("ChangesData", "true");
            }
            if (command.picture.value().has_value()) {
                write_picture_reference("Picture", *command.picture.value(), object_id_text(command.id));
            }
            writer_.close("Command");
        }
        writer_.close("Commands");
    }

    void write_assets() {
        if (document_.assets().empty()) return;
        writer_.open("PictureAssets");
        for (const auto& asset : document_.assets()) {
            const std::string id = object_id_text(asset.id);
            if (!valid_picture_path(asset.relative_path, asset.format)) {
                serialization_fail(id, "relativePath", "CaptionPicture.<matching format> or Items/<ElementName>[/Buttons/<ItemName>]/Picture.<matching format> or Items/<ElementName>/ChoiceButtonPicture.<matching format> or Items/<ElementName>/Document/<PictureName>/Picture.<matching format>", asset.relative_path, "Picture asset path is outside the source package contract");
            }
            if (asset.transparent_x < -1 || asset.transparent_y < -1)
                serialization_fail(id, "transparentX/transparentY", "coordinates at least -1", {}, "Invalid picture transparency coordinates");
            XmlAttributes attributes{
                {"id", id},
                {"relativePath", asset.relative_path},
                {"format", std::string(picture_format_name(asset.format))},
                {"transparent", asset.transparent ? "true" : "false"},
            };
            if (asset.transparent_x != -1) attributes.emplace_back("transparentX", std::to_string(asset.transparent_x));
            if (asset.transparent_y != -1) attributes.emplace_back("transparentY", std::to_string(asset.transparent_y));
            if (asset.uuid) attributes.emplace_back("uuid", canonical_uuid(asset.uuid->canonical));
            writer_.empty("PictureAsset", attributes);
        }
        writer_.close("PictureAssets");
    }

    void write_data_path(const model::DataPath& path) {
        XmlAttributes attributes{{"attributeId", object_id_text(path.attribute.id())}};
        if (path.members.empty()) {
            writer_.empty("DataPath", attributes);
            return;
        }
        writer_.open("DataPath", attributes);
        if (path.members.size() > 128)
            serialization_fail({}, "DataPath", "at most 128 members", "excessive depth",
                "Data path exceeds its depth limit");
        for (const auto& member : path.members) {
            if ((trim_ascii(member.name).empty() && !member.uuid) ||
                (!member.name.empty() && trim_ascii(member.name).empty()) ||
                (member.uuid && !nonzero_uuid(member.uuid->canonical)))
                serialization_fail({}, "Member", "member name or nonzero UUID", "invalid descriptor",
                    "Data path member identity is missing or malformed");
            if (!member.uuid && !member.type) {
                writer_.text("Member", member.name);
                continue;
            }
            XmlAttributes member_attributes;
            if (member.uuid) member_attributes.emplace_back("uuid", canonical_uuid(member.uuid->canonical));
            if (!member.name.empty()) member_attributes.emplace_back("name", member.name);
            if (member.type) {
                writer_.open("Member", member_attributes);
                write_type_domain("TypeDomain", *member.type, {});
                writer_.close("Member");
            } else writer_.empty("Member", member_attributes);
        }
        writer_.close("DataPath");
    }

    void write_enumeration(std::string_view name, const model::EnumerationValue& value) {
        writer_.empty(name, {{"type", value.type_name}, {"member", value.member}});
    }

    void write_bindings(const model::Bindings& bindings) {
        XmlAttributes binding_attributes;
        if (bindings.manual_horizontal.value()) binding_attributes.emplace_back("manualHorizontal", "true");
        if (bindings.manual_vertical.value()) binding_attributes.emplace_back("manualVertical", "true");
        writer_.open("Bindings", binding_attributes);
        for (const auto& binding : bindings.anchors) {
            XmlAttributes attributes{
                {"coordinate", std::string(binding_coordinate_name(binding.coordinate))},
                {"targetCoordinate", std::string(binding_coordinate_name(binding.target_coordinate))},
            };
            if (binding.target.has_value()) {
                attributes.emplace_back("targetId", object_id_text(binding.target->id()));
            }
            attributes.emplace_back("offset", std::to_string(binding.offset.value()));
            if (!binding.proportional.has_value()) {
                writer_.empty("AnchorBinding", attributes);
            } else {
                writer_.open("AnchorBinding", attributes);
                const auto& target = *binding.proportional;
                XmlAttributes proportional_attributes{
                    {"targetCoordinate", std::string(binding_coordinate_name(target.coordinate))},
                };
                if (target.target.has_value()) {
                    proportional_attributes.emplace_back("targetId", object_id_text(target.target->id()));
                }
                proportional_attributes.emplace_back("offset", std::to_string(target.offset.value()));
                writer_.empty("ProportionalBinding", proportional_attributes);
                writer_.close("AnchorBinding");
            }
        }
        for (const auto& binding : bindings.dimensions) {
            writer_.empty("DimensionBinding", {
                {"dimension", std::string(binding_dimension_name(binding.dimension))},
                {"value", std::to_string(binding.value.value())},
            });
        }
        writer_.close("Bindings");
    }

    void write_position(const model::Position& position, std::string_view object_id) {
        if (position.collapse.is_explicit()) {
            serialization_fail(std::string(object_id), "Collapse", "persisted property", "runtime-only value",
                "Runtime-only properties cannot be serialized into public Form.xml");
        }
        const bool default_control =
            position.default_control.is_explicit() && position.default_control.value().value_or(false);
        const bool top = position.top.is_explicit() && position.top.value() != 0;
        const bool visible = position.visible.is_explicit() && !position.visible.value();
        const bool height = position.height.is_explicit() && position.height.value() != 0;
        const bool left = position.left.is_explicit() && position.left.value() != 0;
        const bool tab_order = position.tab_order.is_explicit() && position.tab_order.value().has_value();
        const bool z_order = position.z_order.is_explicit() && position.z_order.value().has_value();
        const bool width = position.width.is_explicit() && position.width.value() != 0;
        const bool bindings = !position.bindings.anchors.empty() || !position.bindings.dimensions.empty() ||
            position.bindings.manual_horizontal.value() || position.bindings.manual_vertical.value();
        if (!(default_control || top || visible || height || left || tab_order || z_order ||
              width || bindings)) {
            writer_.empty("Position");
            return;
        }
        writer_.open("Position");
        if (default_control) writer_.text("DefaultControl", "true");
        if (top) writer_.text("Top", std::to_string(position.top.value()));
        if (visible) writer_.text("Visible", "false");
        if (height) writer_.text("Height", std::to_string(position.height.value()));
        if (left) writer_.text("Left", std::to_string(position.left.value()));
        if (tab_order) writer_.text("TabOrder", std::to_string(*position.tab_order.value()));
        if (z_order) writer_.text("ZOrder", std::to_string(*position.z_order.value()));
        if (width) writer_.text("Width", std::to_string(position.width.value()));
        if (bindings) write_bindings(position.bindings);
        writer_.close("Position");
    }

    void write_page(const model::Page& page) {
        const std::string id = object_id_text(page.id);
        writer_.open("Page", {{"name", page.name}});
        if (page.title.is_explicit() && !page.title.value().items.empty()) {
            write_localized("Title", page.title.value(), id);
        }
        if (!page.visible.value()) writer_.text("Visible", "false");
        if (!page.enabled.value()) writer_.text("Enabled", "false");
        if (page.position.is_explicit()) write_position(page.position.value(), id);
        write_child_items(page.children);
        writer_.close("Page");
    }

    void write_command_bar_buttons(const std::vector<model::CommandBarButton>& buttons, std::string_view owner, const model::CommandBarButtonsState& state) {
        if (buttons.empty() && state == model::CommandBarButtonsState{}) return;
        if (state.auto_fill) writer_.open("Buttons", {{"autoFill", "true"}});
        else writer_.open("Buttons");
        if (state.placement || !state.included_buttons.empty()) {
            writer_.open("AutomaticButtons");
            if (state.placement) {
                writer_.open("Placement");
                if (state.placement->before_button) writer_.empty("BeforeButton", {{"name", *state.placement->before_button}});
                else writer_.empty("AfterLastButton");
                writer_.close("Placement");
            }
            for (const auto& name : state.included_buttons) writer_.empty("ButtonRef", {{"name", name}});
            writer_.close("AutomaticButtons");
        }
        for (const auto& item : buttons) {
            const char* type = item.type == model::CommandBarButtonKind::action ? "Action" :
                item.type == model::CommandBarButtonKind::submenu ? "Submenu" : "Separator";
            writer_.open("CommandBarButton", {{"name", item.name}, {"type", type}});
            if (item.automatic_insertion_locks != 1)
                writer_.text("AutomaticInsertionLocks", std::to_string(item.automatic_insertion_locks));
            if (item.text) writer_.text("Text", *item.text);
            if (item.explanation) writer_.text("Explanation", *item.explanation);
            if (item.tooltip) writer_.text("ToolTip", *item.tooltip);
            if (!item.enabled) writer_.text("Enabled", "false");
            if (item.checked) writer_.text("Checked", "true");
            if (item.changes_data) writer_.text("ChangesData", "true");
            if (item.default_button) writer_.text("DefaultButton", "true");
            const char* representation = item.representation == model::ButtonRepresentation::automatic ? "Auto" :
                item.representation == model::ButtonRepresentation::picture ? "Picture" :
                item.representation == model::ButtonRepresentation::text ? "Text" : "PictureText";
            if (item.representation != model::ButtonRepresentation::automatic)
                writer_.text("Representation", representation);
            switch (item.client_interface_variant) {
                case model::ClientInterfaceVariant::version8_0:
                    writer_.text("ClientInterfaceVariant", "Version8_0");
                    break;
                case model::ClientInterfaceVariant::version8_2:
                    writer_.text("ClientInterfaceVariant", "Version8_2");
                    break;
                case model::ClientInterfaceVariant::version8_2_ordinary_app:
                    break;
                default:
                    throw std::invalid_argument("Unknown CommandBarButton ClientInterfaceVariant");
            }
            if (item.client_interface_theme) {
                switch (*item.client_interface_theme) {
                    case model::ClientInterfaceTheme::light:
                        writer_.text("ClientInterfaceTheme", "Light");
                        break;
                    case model::ClientInterfaceTheme::dark:
                        writer_.text("ClientInterfaceTheme", "Dark");
                        break;
                    default:
                        throw std::invalid_argument("Unknown CommandBarButton ClientInterfaceTheme");
                }
            }
            if (item.command_control_kind == model::CommandControlKind::button)
                writer_.text("CommandControlKind", "Button");
            else if (item.command_control_kind != model::CommandControlKind::from_source)
                throw std::invalid_argument("Unknown CommandBarButton CommandControlKind");
            if (item.shortcut != model::ShortcutValue{}) {
                writer_.open("Shortcut", {{"Alt", item.shortcut.alt ? "true" : "false"},
                    {"Ctrl", item.shortcut.ctrl ? "true" : "false"}, {"Shift", item.shortcut.shift ? "true" : "false"}});
                writer_.text("Key", item.shortcut.key);
                writer_.close("Shortcut");
            }
            if (item.clear_picture) writer_.empty("Picture");
            else if (item.picture) write_picture_reference("Picture", *item.picture, owner);
            if (item.action) {
                writer_.open("Action", {{"handler", item.action->handler}, {"name", item.action->name}});
                write_localized("Text", item.action->text, owner);
                write_localized("ToolTip", item.action->tooltip, owner);
                write_localized("Description", item.action->description, owner);
                writer_.close("Action");
            }
            if (item.standard_action) {
                write_standard_action(*item.standard_action);
            }
            if (item.type == model::CommandBarButtonKind::submenu && item.order != model::CommandBarButtonOrder::none) {
                writer_.text("Order", item.order == model::CommandBarButtonOrder::ascending ? "Ascending" : "Descending");
            }
            write_command_bar_buttons(item.buttons, owner, item.buttons_state);
            writer_.close("CommandBarButton");
        }
        writer_.close("Buttons");
    }

    void write_gantt_data(const model::GanttChartPayload& gantt) {
        if (!gantt.series.empty()) {
            writer_.open("Series");
            for (const auto& item : gantt.series) {
                writer_.open("GanttSeries", {{"id", object_id_text(item.id)}});
                writer_.text("Value", item.value);
                writer_.text("Text", item.text);
                if (item.color) write_color("Color", *item.color, object_id_text(item.id));
                writer_.close("GanttSeries");
            }
            writer_.close("Series");
        }
        if (!gantt.points.empty()) {
            writer_.open("Points");
            for (const auto& item : gantt.points) {
                writer_.open("GanttPoint", {{"id", object_id_text(item.id)}});
                writer_.text("Value", item.value);
                writer_.text("Text", item.text);
                if (item.color) write_color("Color", *item.color, object_id_text(item.id));
                writer_.close("GanttPoint");
            }
            writer_.close("Points");
        }
        if (!gantt.intervals.empty()) {
            writer_.open("Intervals");
            for (const auto& item : gantt.intervals) {
                writer_.open("GanttInterval", {{"pointRef", object_id_text(item.point_ref)}, {"seriesRef", object_id_text(item.series_ref)}});
                writer_.text("StartDate", item.start_date.canonical);
                writer_.text("EndDate", item.end_date.canonical);
                writer_.text("Text", item.text);
                writer_.close("GanttInterval");
            }
            writer_.close("Intervals");
        }
    }

    void write_dendrogram_payload(const model::DendrogramPayload& graph, std::string_view owner) {
        const auto max_rows = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 1);
        if (graph.items.size() > max_rows || graph.links.size() > max_rows)
            serialization_fail(std::string(owner), "Items/Links", "collection size representable by uint32 keys",
                std::to_string(graph.items.size()) + "/" + std::to_string(graph.links.size()), "Dendrogram graph exceeds the encodable collection range");
        std::set<std::string> values;
        for (const auto& item : graph.items) {
            if (item.value.empty() || !values.insert(item.value).second || item.text.items.empty())
                serialization_fail(std::string(owner), "Items", "unique non-empty values with localized Text", item.value, "Dendrogram item is invalid");
        }
        std::set<std::pair<std::string, std::string>> edges;
        std::map<std::string, std::size_t> keys;
        for (std::size_t index = 0; index < graph.items.size(); ++index) keys.emplace(graph.items[index].value, index);
        std::vector<std::size_t> parents(graph.items.size());
        for (std::size_t index = 0; index < parents.size(); ++index) parents[index] = index;
        for (const auto& link : graph.links) {
            const auto first = keys.find(link.first_item), second = keys.find(link.second_item);
            if (first == keys.end() || second == keys.end() || first == second || link.title.items.empty())
                serialization_fail(std::string(owner), "Links", "distinct resolved endpoints with localized Title", link.first_item + "->" + link.second_item, "Dendrogram link is invalid");
            const auto edge = std::minmax(link.first_item, link.second_item);
            if (!edges.emplace(edge.first, edge.second).second)
                serialization_fail(std::string(owner), "Links", "unique acyclic edges", link.first_item + "->" + link.second_item, "Duplicate Dendrogram edge");
            const auto root = [&](std::size_t node) { while (parents[node] != node) node = parents[node]; return node; };
            const auto first_root = root(first->second), second_root = root(second->second);
            if (first_root == second_root)
                serialization_fail(std::string(owner), "Links", "acyclic graph", link.first_item + "->" + link.second_item, "Dendrogram cycle is unsupported");
            parents[first_root] = second_root;
            (void)canonical_decimal(link.distance.canonical, nullptr, "Distance", owner);
        }
        if (!graph.items.empty()) {
            writer_.open("Items");
            for (const auto& item : graph.items) {
                writer_.open("Item");
                writer_.text("Value", item.value);
                write_localized("Text", item.text, owner);
                writer_.close("Item");
            }
            writer_.close("Items");
        }
        if (!graph.links.empty()) {
            writer_.open("Links");
            for (const auto& link : graph.links) {
                writer_.open("Link");
                writer_.text("FirstItem", link.first_item);
                writer_.text("SecondItem", link.second_item);
                write_localized("Title", link.title, owner);
                if (link.distance.canonical != "0")
                    writer_.text("Distance", canonical_decimal(link.distance.canonical, nullptr, "Distance", owner));
                writer_.close("Link");
            }
            writer_.close("Links");
        }
    }

    void write_chart_content(const model::ChartPayload& payload, const std::string& id, bool owned = false) {
        const auto* chart = &payload;
        if (owned && chart->properties.contains(model::PropertyId::from_name("BackColor")))
            serialization_fail(id, "Chart/BackColor", "BackColor on the owning control", "duplicate alias", "Owned Chart BackColor belongs to its owner");
        for (const auto& descriptor : metamodel_.properties_for(model::ControlKind::chart)) {
            if (descriptor.xml_name == "Title" || (owned && descriptor.xml_name == "BackColor")) continue;
            if (descriptor.api_name == "GaugeChartQualityBands") {
                if (!chart->gauge_quality_bands.empty()) {
                    writer_.open("GaugeChartQualityBands");
                    for (const auto& band : chart->gauge_quality_bands) {
                        if (!model::valid_chart_gauge_quality_band_range(band.begin, band.end))
                            serialization_fail(id, "GaugeChartQualityBands", "Begin less than or equal to End", band.begin.canonical + ">" + band.end.canonical, "Invalid GaugeChartQualityBand range");
                        writer_.open("GaugeChartQualityBand");
                        writer_.text("Begin", canonical_decimal(band.begin.canonical, nullptr, "Begin", id));
                        writer_.text("End", canonical_decimal(band.end.canonical, nullptr, "End", id));
                        write_color("BackColor", band.back_color, id);
                        for (const auto& [name, value] : {std::pair<std::string_view, const model::PropertyValue*>{"Text", &band.text},
                                                          {"ToolTip", &band.tool_tip}}) {
                            if (const auto* translations = std::get_if<model::LocalizedStringValue>(value))
                                write_localized(name, *translations, id);
                            else writer_.text(name, std::get<std::string>(*value));
                        }
                        writer_.close("GaugeChartQualityBand");
                    }
                    writer_.close("GaugeChartQualityBands");
                }
                continue;
            }
            write_property_set(chart->properties, std::span{&descriptor, 1}, id, false, std::nullopt, chart);
        }
        const auto write_area = [&](std::string_view name, const std::optional<model::ChartArea>& area) {
            if (!area) return;
            if (!valid_chart_area_placement(name, area->placement))
                serialization_fail(id, std::string(name), "valid area placement", area->placement, "Invalid Chart area placement");
            for (const auto* value : {&area->left, &area->top, &area->right, &area->bottom}) {
                const auto bound = canonical_decimal(value->canonical, nullptr, name, id);
                if (bound != "0" && bound != "1" && !bound.starts_with("0."))
                    serialization_fail(id, std::string(name), "bounds between 0 and 1", bound, "Chart area bound is out of range");
            }
            writer_.open(name);
            if (name == "TitleArea") {
                const auto* text = chart->properties.find(model::PropertyId::from_name("Title"));
                if (text != nullptr) {
                    mm::PropertyDescriptor property;
                    property.xml_name = "Text";
                    property.value_codec = mm::ValueCodec::localized_text;
                    write_property(property, text->value, id);
                }
            }
            const auto kind = name == "PlotArea" ? model::ChartAreaKind::plot : name == "TitleArea" ? model::ChartAreaKind::title : model::ChartAreaKind::legend;
            const auto* shown = kind == model::ChartAreaKind::plot ? nullptr : chart->properties.find(
                model::PropertyId::from_name(kind == model::ChartAreaKind::title ? "ShowTitle" : "ShowLegend"));
            const bool visible = shown == nullptr || !std::holds_alternative<bool>(shown->value) || std::get<bool>(shown->value);
            const std::string_view default_placement = kind != model::ChartAreaKind::plot && !visible ? "None" : "Auto";
            if (area->explicit_geometry[0] || area->placement != default_placement)
                writer_.text("Placement", area->placement);
            for (const auto& descriptor : mm::chart_area_property_descriptors()) {
                if (descriptor.coordinate == nullptr) continue;
                const auto value = canonical_decimal(((*area).*(descriptor.coordinate)).canonical, nullptr, descriptor.name, id);
                const auto default_value = kind == model::ChartAreaKind::plot ? descriptor.plot_default :
                    kind == model::ChartAreaKind::title ? descriptor.title_default : descriptor.legend_default;
                if (area->explicit_geometry[1 + descriptor.coordinate_offset] || value != default_value)
                    writer_.text(descriptor.name, value);
            }
            for (const auto& descriptor : mm::chart_area_boolean_descriptors()) {
                const auto& value = (*area).*(descriptor.member);
                if (descriptor.kind == kind && value) writer_.text(descriptor.name, *value ? "true" : "false");
                else if (descriptor.kind != kind && value && mm::find_chart_area_boolean(kind, descriptor.id) == nullptr)
                    serialization_fail(id, std::string(name), "property supported by this area", std::string(descriptor.name), "Chart area property belongs to another area");
            }
            for (const auto& descriptor : mm::chart_area_color_descriptors()) {
                const auto& value = (*area).*(descriptor.member);
                if (descriptor.kind == kind && value) write_color(descriptor.name, *value, id);
                else if (descriptor.kind != kind && value && mm::find_chart_area_color(kind, descriptor.id) == nullptr)
                    serialization_fail(id, std::string(name), "property supported by this area", std::string(descriptor.name), "Chart area property belongs to another area");
            }
            for (const auto& descriptor : mm::chart_area_style_descriptors()) {
                if (descriptor.kind != kind) continue;
                if (descriptor.border && ((*area).*(descriptor.border)))
                    write_border(descriptor.name, *((*area).*(descriptor.border)), id);
                if (descriptor.font && ((*area).*(descriptor.font)))
                    write_font(descriptor.name, *((*area).*(descriptor.font)), id);
                if (descriptor.line && ((*area).*(descriptor.line)) && *((*area).*(descriptor.line)) != model::LineValue{})
                    write_line(descriptor.name, *((*area).*(descriptor.line)), id);
            }
            for (const auto& descriptor : mm::chart_plot_format_descriptors()) {
                if (descriptor.text) {
                    const auto& value = (*area).*(descriptor.text);
                    if (!value) continue;
                    if (kind != model::ChartAreaKind::plot)
                        serialization_fail(id, std::string(name), "PlotArea property", std::string(descriptor.name), "Chart area property belongs to another area");
                    mm::PropertyDescriptor property;
                    property.xml_name = descriptor.name;
                    property.value_codec = descriptor.value_codec;
                    write_property(property, *value, id);
                } else {
                    const auto& value = (*area).*(descriptor.enumeration);
                    if (!value) continue;
                    if (kind != model::ChartAreaKind::plot || value->type_name != descriptor.enumeration_type ||
                        std::ranges::find(descriptor.members, value->member) == descriptor.members.end())
                        serialization_fail(id, std::string(name), "valid PlotArea enumeration", value->member, "Invalid PlotArea enumeration");
                    write_enumeration(descriptor.name, *value);
                }
            }
            if (name == "PlotArea") for (const auto& owner : mm::chart_scale_owner_descriptors()) {
                const auto& scale = (*area).*(owner.member);
                bool present = false;
                for (const auto& field : mm::chart_scale_property_descriptors()) present = present || mm::chart_scale_property_value(scale, field) != nullptr;
                for (const auto& field : mm::chart_label_area_property_descriptors()) present = present || mm::chart_scale_property_value(scale, field) != nullptr;
                if (!present) continue;
                writer_.open(owner.name);
                for (const auto& field : mm::chart_scale_property_descriptors()) if (const auto* value = mm::chart_scale_property_value(scale, field)) {
                    mm::PropertyDescriptor property;
                    property.xml_name = field.name;
                    property.value_codec = field.value_codec;
                    if (field.value_codec == mm::ValueCodec::line) {
                        writer_.open(field.name);
                        if (std::holds_alternative<model::UndefinedValue>(*value)) writer_.text("Undefined", "undefined");
                        else write_line("Line", require_value<model::LineValue>(*value, id, field.name, "Line or Undefined"), id);
                        writer_.close(field.name);
                    } else write_property(property, *value, id);
                }
                bool title_present = false;
                for (const auto& field : mm::chart_label_area_property_descriptors()) title_present = title_present || mm::chart_scale_property_value(scale, field) != nullptr;
                if (title_present) {
                    writer_.open("TitleArea");
                    for (const auto& field : mm::chart_label_area_property_descriptors()) if (const auto* value = mm::chart_scale_property_value(scale, field)) {
                        mm::PropertyDescriptor property;
                        property.xml_name = field.name;
                        property.value_codec = field.value_codec;
                        write_property(property, *value, id);
                    }
                    writer_.close("TitleArea");
                }
                writer_.close(owner.name);
            }
            writer_.close(name);
        };
        write_area("PlotArea", chart->plot_area);
        write_area("LegendArea", chart->legend_area);
        auto title_area = chart->title_area;
        if (!title_area && chart->properties.find(model::PropertyId::from_name("Title")) != nullptr) {
            model::ChartArea defaults;
            for (const auto& descriptor : mm::chart_area_property_descriptors())
                if (descriptor.coordinate != nullptr)
                    defaults.*(descriptor.coordinate) = model::DecimalValue{std::string(descriptor.title_default)};
            const auto* show = chart->properties.find(model::PropertyId::from_name("ShowTitle"));
            if (show != nullptr && std::holds_alternative<bool>(show->value) && !std::get<bool>(show->value))
                defaults.placement = "None";
            title_area = std::move(defaults);
        }
        write_area("TitleArea", title_area);
        if (!chart->summary_series.properties.empty() || !chart->summary_series.trendlines.empty() || chart->summary_series.text || !std::holds_alternative<model::UndefinedValue>(chart->summary_series.value) ||
            chart->summary_series.color != model::ColorValue{} ||
            chart->summary_series.marker != model::EnumerationValue{"ChartMarkerType", "Auto"}) {
            writer_.open("SummarySeries");
            write_chart_series_fields(mm::ConstChartSeriesObject{&chart->summary_series}, id);
            writer_.close("SummarySeries");
        }
        const auto write_chart_key = [&](std::string_view name, const model::PropertyValue& value, std::string_view owner) {
            if (std::holds_alternative<model::UndefinedValue>(value)) return;
            writer_.open(name);
            write_typed_value_node(value, owner);
            writer_.close(name);
        };
        std::uint64_t max_series_id = 1;
        for (const auto& item : chart->series) max_series_id = std::max(max_series_id, item.id.value());
        std::uint64_t max_point_id = 0;
        for (const auto& item : chart->points) max_point_id = std::max(max_point_id, item.id.value());
        constexpr auto chart_id_limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (max_series_id > chart_id_limit || max_point_id > chart_id_limit ||
            (chart->last_series_id && *chart->last_series_id > chart_id_limit) ||
            (chart->last_point_id && *chart->last_point_id > chart_id_limit))
            serialization_fail(id, "lastAssignedId", "signed 64-bit allocator IDs", "out of range", "Invalid Chart allocator history");
        if (chart->last_series_id && *chart->last_series_id < max_series_id)
            serialization_fail(id, "Series", "lastAssignedId at least the largest series ID", std::to_string(*chart->last_series_id), "Invalid Chart allocator history");
        if (chart->last_point_id && *chart->last_point_id < max_point_id)
            serialization_fail(id, "Points", "lastAssignedId at least the largest point ID", std::to_string(*chart->last_point_id), "Invalid Chart allocator history");
        if (chart->last_series_id && *chart->last_series_id != max_series_id)
            writer_.open("Series", {{"lastAssignedId", std::to_string(*chart->last_series_id)}});
        else writer_.open("Series");
        for (const auto& item : chart->series) {
            writer_.open("ChartSeries", {{"id", object_id_text(item.id)}});
            write_chart_series_fields(mm::ConstChartSeriesObject{&item}, object_id_text(item.id));
            writer_.close("ChartSeries");
        }
        writer_.close("Series");
        if (chart->last_point_id && *chart->last_point_id != max_point_id)
            writer_.open("Points", {{"lastAssignedId", std::to_string(*chart->last_point_id)}});
        else writer_.open("Points");
        for (const auto& item : chart->points) {
            writer_.open("ChartPoint", {{"id", object_id_text(item.id)}});
            writer_.text("Text", item.text);
            write_color("Color", item.color, object_id_text(item.id));
            write_chart_key("Value", item.value, object_id_text(item.id));
            writer_.close("ChartPoint");
        }
        writer_.close("Points");
        const auto default_mode = chart->series.empty() || chart->points.empty() ? model::ChartDataMode::sample : model::ChartDataMode::explicit_values;
        if (chart->data_mode && *chart->data_mode != model::ChartDataMode::sample && *chart->data_mode != model::ChartDataMode::explicit_values &&
            *chart->data_mode != model::ChartDataMode::source)
            serialization_fail(id, "dataMode", "Sample, Explicit, or Source", "invalid enum", "Invalid Chart data mode");
        if (chart->data_mode == model::ChartDataMode::source) {
            if (!chart->values.empty()) serialization_fail(id, "Values", "no ChartValue cells in Source mode", std::to_string(chart->values.size()), "Source-mode Chart cannot contain dense Values cells");
            XmlAttributes attributes{{"dataMode", "Source"}};
            if (!chart->data_source_description.empty()) attributes.emplace_back("sourceDescription", chart->data_source_description);
            writer_.open("Values", attributes);
        } else {
            if (!chart->data_source_description.empty())
                serialization_fail(id, "sourceDescription", "empty outside Source mode", chart->data_source_description, "Chart sourceDescription requires Source mode");
            if (chart->data_mode && *chart->data_mode != default_mode)
                writer_.open("Values", {{"dataMode", *chart->data_mode == model::ChartDataMode::sample ? "Sample" : "Explicit"}});
            else writer_.open("Values");
        }
        for (const auto& item : chart->values) {
            writer_.open("ChartValue", {{"seriesRef", object_id_text(item.series_ref)}, {"pointRef", object_id_text(item.point_ref)}});
            write_typed_value_node(item.value, object_id_text(item.series_ref));
            write_chart_key("Details", item.details, object_id_text(item.series_ref));
            if (!item.tooltip.empty()) writer_.text("ToolTip", item.tooltip);
            writer_.close("ChartValue");
        }
        writer_.close("Values");
    }

    void write_control(const model::ControlNode& control) {
        const auto& descriptor = metamodel_.control(control.kind());
        const std::string id = object_id_text(control.id);
        XmlAttributes attributes{{"name", control.name}, {"id", id}};
        if (const auto* spreadsheet = std::get_if<model::SpreadsheetDocumentFieldPayload>(&control.payload);
            spreadsheet != nullptr && spreadsheet->serialization_version != model::SpreadsheetDocumentFieldSerializationVersion::v18)
            attributes.emplace_back("serializationVersion", std::to_string(static_cast<unsigned>(spreadsheet->serialization_version)));
        writer_.open(descriptor.public_name, attributes);
        if (control.data_path.has_value()) write_data_path(*control.data_path);
        if (const auto* input_field = std::get_if<model::InputFieldPayload>(&control.payload);
            input_field != nullptr && input_field->type_link.has_value())
            writer_.empty("TypeLink", {{"attributeId", object_id_text(input_field->type_link->id())}});
        if (const auto* input_field = std::get_if<model::InputFieldPayload>(&control.payload);
            input_field != nullptr && input_field->extension) {
            const auto descriptor = std::ranges::find(mm::input_field_extensions, *input_field->extension,
                &mm::InputFieldExtensionDescriptor::kind);
            if (descriptor == std::end(mm::input_field_extensions))
                serialization_fail(id, "InputFieldExtension", "declared extension", "unknown kind",
                    "InputField extension is not declared");
            writer_.empty(descriptor->xml_name);
        }
        write_property_set(
            control.extension_properties,
            metamodel_.control_extension_properties(),
            id,
            true,
            control.kind());
        write_position(control.position, id);
        if (const auto* spreadsheet = std::get_if<model::SpreadsheetDocumentFieldPayload>(&control.payload);
            spreadsheet != nullptr && (!spreadsheet->cells.empty() || !spreadsheet->rows.empty() ||
                !spreadsheet->columns.empty() || spreadsheet->default_format.properties.size() != 0 ||
                spreadsheet->default_column_format.properties.size() != 0 || spreadsheet->height || spreadsheet->width ||
                spreadsheet->language_settings != model::SpreadsheetLanguageSettings{})) {
            const auto write_format = [&](std::string_view name, const model::SpreadsheetDocumentFormat& format) {
                writer_.open(name);
                for (const auto& property : mm::spreadsheet_format_properties()) {
                    if (const auto* entry = format.properties.find(property.id)) {
                        const auto* picture = std::get_if<model::PictureRef>(&entry->value);
                        if (property.value_codec == mm::ValueCodec::picture && picture && *picture == model::PictureRef{})
                            writer_.empty(property.xml_name, {{"empty", "true"}});
                        else write_property(property, entry->value, id);
                    }
                }
                writer_.close(name);
            };
            XmlAttributes document_attributes;
            if (spreadsheet->height) document_attributes.emplace_back("height", std::to_string(*spreadsheet->height));
            if (spreadsheet->width) document_attributes.emplace_back("width", std::to_string(*spreadsheet->width));
            writer_.open("Document", document_attributes);
            if (spreadsheet->language_settings != model::SpreadsheetLanguageSettings{}) {
                const auto& settings = spreadsheet->language_settings;
                writer_.open("LanguageSettings", {{"currentLanguage", settings.current_language}, {"defaultLanguage", settings.default_language}});
                for (const auto& language : settings.languages)
                    writer_.empty("Language", {{"id", language.id}, {"code", language.code}, {"description", language.description}});
                writer_.close("LanguageSettings");
            }
            if (spreadsheet->default_format.properties.size() != 0) write_format("DefaultFormat", spreadsheet->default_format);
            if (spreadsheet->default_column_format.properties.size() != 0) write_format("DefaultColumnFormat", spreadsheet->default_column_format);
            const auto write_indexed_formats = [&](std::string_view name, const auto& items) {
                std::vector<std::size_t> order;
                for (std::size_t index = 0; index < items.size(); ++index) order.push_back(index);
                std::ranges::sort(order, {}, [&](std::size_t index) { return items[index].index; });
                for (const auto index : order) {
                    writer_.open(name, {{"index", std::to_string(items[index].index)}});
                    write_format("Format", items[index].format);
                    writer_.close(name);
                }
            };
            write_indexed_formats("Row", spreadsheet->rows);
            write_indexed_formats("Column", spreadsheet->columns);
            for (const auto& cell : spreadsheet->cells) {
                writer_.open("Cell", {{"row", std::to_string(cell.row)}, {"column", std::to_string(cell.column)}});
                if (!cell.typed_value.has_value() && cell.text_present) {
                    const mm::PropertyDescriptor text_descriptor{.xml_name = "Text", .value_codec = mm::ValueCodec::localized_text};
                    write_property(text_descriptor, cell.text, id);
                } else if (cell.typed_value.has_value()) {
                    const auto* text = std::get_if<std::string>(&cell.text);
                    if (!text || !text->empty())
                        serialization_fail(id, "Document/Cell", "Text-only or typed value", "both", "Spreadsheet Cell cannot persist Text and a typed Value together");
                    const auto& typed = *cell.typed_value;
                    writer_.text("ContainsValue", "true");
                    write_type_domain("ValueType", typed.type, id);
                    {
                        const auto& type = typed.type.entries.front();
                        if (const auto* value = std::get_if<std::string>(&typed.value)) {
                            if (type.term != model::TypeDomainTerm::string)
                                serialization_fail(id, "Document/Cell/Value", "String matching ValueType", "mismatched value", "Spreadsheet Cell Value does not match ValueType");
                            writer_.text("Value", *value);
                        } else if (const auto* value = std::get_if<model::DecimalValue>(&typed.value)) {
                            if (type.term != model::TypeDomainTerm::numeric)
                                serialization_fail(id, "Document/Cell/Value", "Number matching ValueType", "mismatched value", "Spreadsheet Cell Value does not match ValueType");
                            writer_.text("Value", canonical_decimal(value->canonical, nullptr, "Value", id));
                        } else if (const auto* value = std::get_if<bool>(&typed.value)) {
                            if (type.term != model::TypeDomainTerm::boolean)
                                serialization_fail(id, "Document/Cell/Value", "Boolean matching ValueType", "mismatched value", "Spreadsheet Cell Value does not match ValueType");
                            writer_.text("Value", *value ? "true" : "false");
                        } else if (const auto* value = std::get_if<model::DateValue>(&typed.value)) {
                            if (type.term != model::TypeDomainTerm::date)
                                serialization_fail(id, "Document/Cell/Value", "Date matching ValueType", "mismatched value", "Spreadsheet Cell Value does not match ValueType");
                            (void)storage::value_codec::date_to_platform(value->canonical);
                            writer_.text("Value", value->canonical);
                        } else {
                            serialization_fail(id, "Document/Cell/Value", "String, Number, Boolean, or Date", "unsupported value", "Spreadsheet Cell Value kind is unsupported");
                        }
                    }
                }
                if (cell.control.has_value()) {
                    writer_.open("Control", {{"type", "InputField"}});
                    const auto* read_only = cell.control->properties.find(model::PropertyId::from_name("ReadOnly"));
                    writer_.text("ReadOnly", read_only != nullptr && std::get<bool>(read_only->value) ? "true" : "false");
                    writer_.close("Control");
                }
                if (cell.detail_parameter) writer_.text("DetailParameter", *cell.detail_parameter);
                if (cell.format.properties.size() != 0) write_format("Format", cell.format);
                writer_.close("Cell");
            }
            writer_.close("Document");
        }
        const std::vector<model::CommandBarButton>* owned_buttons = nullptr;
        const model::CommandBarButtonsState* buttons_state = nullptr;
        if (const auto* button = std::get_if<model::ButtonPayload>(&control.payload)) {
            owned_buttons = &button->buttons;
            buttons_state = &button->buttons_state;
        }
        if (const auto* command_bar = std::get_if<model::CommandBarPayload>(&control.payload)) {
            owned_buttons = &command_bar->buttons;
            buttons_state = &command_bar->buttons_state;
        }
        if (owned_buttons != nullptr) {
            for (const auto& property : metamodel_.properties_for(control.kind())) {
                if (property.api_name == "Buttons") write_command_bar_buttons(*owned_buttons, id, *buttons_state);
                else if (const auto* entry = control.properties().find(property.id);
                    entry != nullptr && !equals_descriptor_default(property, entry->value))
                    write_property(property, entry->value, id);
            }
        } else if (const auto* table = std::get_if<model::TablePayload>(&control.payload)) {
            for (const auto& property : metamodel_.properties_for(control.kind())) {
                if (property.api_name == "Columns") write_table_columns(table->columns, id);
                else if (table->source_extension && std::ranges::any_of(mm::table_source_property_descriptors(*table->source_extension),
                    [&](const auto& source_property) { return source_property.id == property.id; })) continue;
                else if (const auto* entry = control.properties().find(property.id);
                    entry != nullptr && !equals_descriptor_default(property, entry->value))
                    write_property(property, entry->value, id);
            }
        } else if (const auto* dendrogram = std::get_if<model::DendrogramPayload>(&control.payload)) {
            const auto properties = metamodel_.properties_for(control.kind());
            const auto items = std::ranges::find(properties, "Items", &mm::PropertyDescriptor::api_name);
            if (items == properties.end() || items + 1 == properties.end() || (items + 1)->api_name != "Links")
                throw std::logic_error("Dendrogram Items and Links descriptors are not adjacent");
            const auto offset = static_cast<std::size_t>(items - properties.begin());
            write_property_set(control.properties(), properties.first(offset), id);
            write_property_set(control.properties(), properties.subspan(offset, 2), id);
            write_dendrogram_payload(*dendrogram, id);
            write_property_set(control.properties(), properties.subspan(offset + 2), id);
        } else if (control.kind() != model::ControlKind::chart) {
            write_property_set(control.properties(), metamodel_.properties_for(control.kind()), id);
        }
        if (const auto* gantt = std::get_if<model::GanttChartPayload>(&control.payload)) {
            write_gantt_data(*gantt);
        }
        if (const auto* chart = std::get_if<model::ChartPayload>(&control.payload)) write_chart_content(*chart, id);
        const auto* gantt = std::get_if<model::GanttChartPayload>(&control.payload);
        const auto* dendrogram = std::get_if<model::DendrogramPayload>(&control.payload);
        const auto* owned_chart = gantt && gantt->chart ? &*gantt->chart :
            dendrogram && dendrogram->chart ? &*dendrogram->chart : nullptr;
        if (owned_chart) {
            writer_.open("Chart");
            write_chart_content(*owned_chart, id, true);
            writer_.close("Chart");
        }
        auto common_events = control.events;
        if (const auto* table = std::get_if<model::TablePayload>(&control.payload); table && table->source_extension) {
            const auto extensions = mm::table_source_extension_descriptors();
            const auto extension = std::ranges::find(extensions, *table->source_extension, &mm::TableSourceExtensionDescriptor::kind);
            if (extension == extensions.end())
                serialization_fail(id, "TableSourceExtension", "registered source interface", "unknown kind",
                    "Table source extension is unsupported");
            const auto descriptors = mm::table_source_event_descriptors(*table->source_extension);
            std::vector<model::EventRef> source_events;
            std::erase_if(common_events, [&](const auto& reference) {
                const auto* event = document_.find_event(reference.id());
                const bool belongs = event && std::ranges::any_of(descriptors,
                    [&](const auto& descriptor) { return descriptor.api_name == event->name; });
                if (belongs) source_events.push_back(reference);
                return belongs;
            });
            writer_.open(extension->xml_name);
            write_property_set(control.properties(), mm::table_source_property_descriptors(*table->source_extension), id);
            if (!table->source_bindings.empty()) {
                writer_.open("Bindings");
                constexpr std::array<std::string_view, 3> names{"Owner", "Parent", "ActiveRow"};
                for (std::size_t role = 0; role < names.size(); ++role) {
                    const auto found = std::ranges::find(table->source_bindings,
                        static_cast<model::TableSourceBindingRole>(role), &model::TableSourceBinding::role);
                    if (found == table->source_bindings.end()) continue;
                    writer_.open(names[role]);
                    if (const auto* path = std::get_if<model::DataPath>(&found->source)) write_data_path(*path);
                    else {
                        const auto& source = std::get<model::TableEventSource>(found->source);
                        writer_.open("EventSource", {{"event", source.event}});
                        if (const auto* form = std::get_if<model::FormRef>(&source.owner))
                            writer_.empty("FormRef", {{"id", object_id_text(form->id())}});
                        else writer_.empty("ControlRef", {{"id", object_id_text(std::get<model::ControlRef>(source.owner).id())}});
                        writer_.close("EventSource");
                    }
                    writer_.close(names[role]);
                }
                writer_.close("Bindings");
            }
            write_events("Events", source_events, descriptors, id);
            writer_.close(extension->xml_name);
        }
        write_events("Events", common_events, metamodel_.events_for(control.kind()), id);
        write_child_items(control.children);
        writer_.close(descriptor.public_name);
    }

    void write_table_columns(const std::vector<model::TableColumn>& columns, std::string_view owner) {
        writer_.open("Columns");
        for (const auto& column : columns) {
            if (column.control && column.value_type) {
                serialization_fail(std::string(owner), "Column/ValueType",
                    "Column ValueType only without Control", "both present",
                    "Table Column stored type cannot have two owners");
            }
            std::string_view control_type;
            if (column.control) {
                switch (column.control->kind) {
                    case model::ControlKind::input_field: control_type = "InputField"; break;
                    case model::ControlKind::choice_field: control_type = "ChoiceField"; break;
                    case model::ControlKind::check_box: control_type = "CheckBox"; break;
                    default:
                        serialization_fail(std::string(owner), "Column/Control", "InputField, ChoiceField, or CheckBox",
                            "unsupported control", "Table Column Control kind is unsupported");
                }
                column.control->properties.for_each_explicit([&](const model::PropertyEntry& entry) {
                    const auto* descriptor = metamodel_.property(column.control->kind, entry.id);
                    const bool supported = descriptor != nullptr &&
                        ((column.control->kind == model::ControlKind::input_field &&
                            (std::ranges::find(mm::table_column_input_properties, descriptor->api_name) !=
                                mm::table_column_input_properties.end())) ||
                         (column.control->kind == model::ControlKind::choice_field &&
                            (descriptor->api_name == "Enabled" || descriptor->api_name == "ToolTip")) ||
                         (column.control->kind == model::ControlKind::check_box &&
                            (descriptor->api_name == "Enabled" || descriptor->api_name == "Caption" ||
                                descriptor->api_name == "ToolTip" || descriptor->api_name == "Font")));
                    if (!supported || !valid_table_column_editor_property(column.control->kind, *descriptor, entry.value)) {
                        serialization_fail(std::string(owner), "Column/Control/" +
                            (descriptor == nullptr ? std::string("unknown") : std::string(descriptor->xml_name)),
                            "compatible typed property", "unsupported value",
                            "Table Column editor property is outside its supported typed profile");
                    }
                });
            }
            writer_.open("Column", {{"name", column.name}});
            if (column.source) {
                writer_.open("Source");
                if (const auto* field = std::get_if<model::TableMetadataColumnField>(&*column.source)) {
                    XmlAttributes attributes;
                    if (!field->field.name.empty()) attributes.emplace_back("name", field->field.name);
                    if (field->field.uuid) attributes.emplace_back("uuid", canonical_uuid(field->field.uuid->canonical));
                    writer_.empty("MetadataField", attributes);
                } else writer_.empty("StandardPicture");
                writer_.close("Source");
            } else writer_.text("DataPath", column.data_path);
            write_localized("Header", column.header, owner);
            if (column.layout_width) {
                const std::string width = canonical_decimal(column.layout_width->canonical, nullptr, "Width", owner);
                if (width.front() == '-') {
                    serialization_fail(std::string(owner), "Width", "non-negative decimal", width,
                        "Table Column Width cannot be negative");
                }
                writer_.text("Width", width, {{"unit", "form"}});
            }
            if (column.text_font != model::FontValue{}) write_font("TextFont", column.text_font, owner);
            for (const auto& property : mm::table_column_boolean_descriptors())
                if ((column.*property.member) != property.default_value)
                    writer_.text(property.name, (column.*property.member) ? "true" : "false");
            for (const auto& property : mm::table_column_enumeration_descriptors())
                if ((column.*property.member).member != property.default_member)
                    write_enumeration(property.name, column.*property.member);
            if (column.value_type && !column.value_type->entries.empty())
                write_type_domain("ValueType", *column.value_type, owner);
            if (column.control) {
                writer_.open("Control", {{"type", std::string(control_type)}});
                if (column.control->value_type.has_value()) {
                    if (column.control->kind != model::ControlKind::input_field) {
                        serialization_fail(std::string(owner), "Column/Control/ValueType",
                            "InputField ValueType", "other editor kind",
                            "Table Column ValueType is supported only for InputField");
                    }
                    if (!column.control->value_type->entries.empty())
                        write_type_domain("ValueType", *column.control->value_type, owner);
                }
                if (column.control->extension) {
                    const auto extension = std::ranges::find(mm::input_field_extensions, *column.control->extension,
                        &mm::InputFieldExtensionDescriptor::kind);
                    if (extension == std::end(mm::input_field_extensions))
                        serialization_fail(std::string(owner), "InputFieldExtension", "declared extension", "unknown",
                            "Table Column editor extension is not declared");
                    writer_.empty(extension->xml_name);
                }
                const auto write_editor_property = [&](std::string_view name) {
                    const auto* descriptor = metamodel_.property(column.control->kind, name);
                    if (descriptor == nullptr) return;
                    const auto* entry = column.control->properties.find(descriptor->id);
                    if (entry == nullptr || equals_descriptor_default(*descriptor, entry->value)) return;
                    if (!valid_table_column_editor_property(column.control->kind, *descriptor, entry->value)) {
                        serialization_fail(std::string(owner), "Column/Control/" + std::string(descriptor->xml_name),
                            "supported typed property value", "unsupported value",
                            "Table Column editor property value is unsupported");
                    }
                    write_property(*descriptor, entry->value, owner);
                };
                if (column.control->kind == model::ControlKind::input_field) {
                    for (const auto name : mm::table_column_input_properties) write_editor_property(name);
                } else if (column.control->kind == model::ControlKind::choice_field) {
                    write_editor_property("Enabled");
                    write_editor_property("ToolTip");
                } else {
                    write_editor_property("Enabled");
                    write_editor_property("Caption");
                    write_editor_property("ToolTip");
                    write_editor_property("Font");
                }
                write_events("Events", column.control->events, mm::event_descriptors(column.control->kind), owner);
                writer_.close("Control");
            }
            writer_.close("Column");
        }
        writer_.close("Columns");
    }

    void write_child_items(const std::vector<model::ChildItemRef>& children) {
        if (children.empty()) return;
        writer_.open("ChildItems");
        for (const model::ChildItemRef& child : children) {
            std::visit(
                [&](const auto& reference) {
                    using Reference = std::remove_cvref_t<decltype(reference)>;
                    if constexpr (std::is_same_v<Reference, model::ControlRef>) {
                        const model::ControlNode* control = document_.find_control(reference.id());
                        if (control == nullptr) {
                            serialization_fail({}, "ChildItems", "resolving control", object_id_text(reference.id()), "Child control reference is dangling");
                        }
                        write_control(*control);
                    } else {
                        const model::Page* page = document_.find_page(reference.id());
                        if (page == nullptr) {
                            serialization_fail({}, "ChildItems", "resolving page", object_id_text(reference.id()), "Child page reference is dangling");
                        }
                        write_page(*page);
                    }
                },
                child);
        }
        writer_.close("ChildItems");
    }

    const model::OrdinaryFormDocument& document_;
    const mm::Metamodel& metamodel_;
    CanonicalXmlWriter writer_;
};

Diagnostics invariant_diagnostics(const model::ValidationReport& report) {
    Diagnostics diagnostics;
    diagnostics.reserve(report.violations.size());
    for (const auto& violation : report.violations) {
        diagnostics.push_back({
            "OOF2004",
            DiagnosticSeverity::error,
            object_id_text(violation.source),
            "/Form",
            {},
            "valid OrdinaryFormDocument invariant",
            violation.target ? object_id_text(violation.target) : std::string{},
            violation.message,
        });
    }
    return diagnostics;
}

Diagnostic internal_diagnostic(std::string message) {
    return {
        "OOF2099",
        DiagnosticSeverity::error,
        {},
        {},
        {},
        "successful ordinary-form XML adapter operation",
        "internal failure",
        std::move(message),
    };
}

}  // namespace

Result<model::OrdinaryFormDocument> parse_form_xml(
    std::string_view xml, std::shared_ptr<const MetadataContext> metadata_context,
    const ConfigurationTypeSchema* types) {
    try {
        XmlDoc document = parse_and_validate_xml(xml, types != nullptr);
        Diagnostics diagnostics;
        if (types != nullptr) apply_configuration_type_names(document.get(), *types, diagnostics);
        xmlNodePtr root = xmlDocGetRootElement(document.get());
        DocumentParser parser(mm::Metamodel::instance(), std::move(metadata_context));
        model::OrdinaryFormDocument parsed = parser.parse(root);
        const model::ValidationReport report = parsed.validate();
        if (!report.ok()) {
            return Result<model::OrdinaryFormDocument>::failure(
                invariant_diagnostics(report));
        }
        if (!parsed.reconstruction_complete()) {
            diagnostics.push_back({"OOF1140", DiagnosticSeverity::warning, {}, {}, {}, {}, {},
                "The source Form.xml is marked incomplete; unsupported source properties are not available."});
        }
        return Result<model::OrdinaryFormDocument>::success(std::move(parsed), std::move(diagnostics));
    } catch (AdapterError& error) {
        Diagnostics diagnostics;
        diagnostics.push_back(error.take_diagnostic());
        return Result<model::OrdinaryFormDocument>::failure(std::move(diagnostics));
    } catch (const std::exception& error) {
        return Result<model::OrdinaryFormDocument>::failure(
            Diagnostics{internal_diagnostic(error.what())});
    }
}

Result<std::string> serialize_form_xml(const model::OrdinaryFormDocument& document,
    const ConfigurationTypeSchema* types) {
    const model::ValidationReport report = document.validate();
    if (!report.ok()) {
        return Result<std::string>::failure(invariant_diagnostics(report));
    }
    try {
        DocumentSerializer serializer(document, mm::Metamodel::instance());
        std::string xml = serializer.serialize();
        Diagnostics diagnostics;
        try {
            XmlDoc validated = parse_and_validate_xml(xml, types != nullptr);
            if (types != nullptr && apply_configuration_type_names(validated.get(), *types, diagnostics)) {
                xmlChar* raw = nullptr;
                int size = 0;
                xmlDocDumpFormatMemoryEnc(validated.get(), &raw, &size, "UTF-8", 1);
                std::unique_ptr<xmlChar, decltype(xmlFree)> buffer(raw, xmlFree);
                if (raw == nullptr || size <= 0)
                    throw std::runtime_error("Cannot serialize configuration type names in ordinary-form XML");
                xml.assign(reinterpret_cast<const char*>(raw), static_cast<std::size_t>(size));
            }
        } catch (AdapterError& validation_error) {
            Diagnostic diagnostic = validation_error.take_diagnostic();
            diagnostic.code = "OOF2005";
            diagnostic.message =
                "Canonical XML produced by the adapter is invalid: " + diagnostic.message;
            throw AdapterError(std::move(diagnostic));
        }
        if (!document.reconstruction_complete()) {
            diagnostics.push_back({"OOF1140", DiagnosticSeverity::warning, {}, {}, {}, {}, {},
                "The serialized Form.xml remains incomplete; unsupported source properties are not available."});
        }
        return Result<std::string>::success(std::move(xml), std::move(diagnostics));
    } catch (AdapterError& error) {
        Diagnostics diagnostics;
        diagnostics.push_back(error.take_diagnostic());
        return Result<std::string>::failure(std::move(diagnostics));
    } catch (const std::exception& error) {
        return Result<std::string>::failure(
            Diagnostics{internal_diagnostic(error.what())});
    }
}

}  // namespace oof::source
