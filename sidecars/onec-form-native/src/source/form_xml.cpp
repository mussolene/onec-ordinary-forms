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

void capture_xml_error(void* context, xmlErrorPtr error) {
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

std::string canonical_uuid(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
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

XmlDoc parse_and_validate_xml(std::string_view xml) {
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
            XML_PARSE_COMPACT | XML_PARSE_NOERROR | XML_PARSE_NOWARNING));
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

model::LocalizedStringValue parse_localized_string(xmlNodePtr node) {
    model::LocalizedStringValue value;
    std::set<std::string> languages;
    for (xmlNodePtr item : element_children(node)) {
        const std::string language = required_attribute(item, "language");
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

model::FormattedStringValue parse_formatted_string(xmlNodePtr node) {
    const auto children = element_children(node);
    if (children.size() != 1 || node_name(children.front()) != "Value") {
        fail(
            "OOF2003",
            node,
            {},
            {},
            "one Value element",
            std::to_string(children.size()),
            "Formatted string does not contain its typed Value");
    }
    return {
        parse_localized_string(children.front()),
        parse_boolean(required_attribute(node, "formatted"), node, "formatted"),
    };
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
    static constexpr std::array<std::pair<std::string_view, model::TypeDomainTerm>, 11> terms{{
        {"unknown", model::TypeDomainTerm::unknown},
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
    for (xmlNodePtr entry_node : element_children(node)) {
        model::TypeDomainEntry entry;
        entry.term = parse_type_domain_term(required_attribute(entry_node, "term"), entry_node);
        const auto type_uuid = optional_attribute(entry_node, "typeUuid");
        const auto length = optional_attribute(entry_node, "length");
        const auto precision = optional_attribute(entry_node, "precision");
        const auto non_negative = optional_attribute(entry_node, "nonNegative");
        const auto variable = optional_attribute(entry_node, "variable");
        const auto date = optional_attribute(entry_node, "date");
        const auto time = optional_attribute(entry_node, "time");

        const bool reference_like =
            entry.term == model::TypeDomainTerm::unknown ||
            entry.term == model::TypeDomainTerm::list ||
            entry.term == model::TypeDomainTerm::reference ||
            entry.term == model::TypeDomainTerm::type;
        const bool numeric = entry.term == model::TypeDomainTerm::numeric;
        const bool string = entry.term == model::TypeDomainTerm::string;
        const bool binary = entry.term == model::TypeDomainTerm::binary;
        const bool date_term = entry.term == model::TypeDomainTerm::date;
        const bool invalid_attributes =
            (!reference_like && type_uuid.has_value()) ||
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
        }
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

model::FontValue parse_font(xmlNodePtr node) {
    static constexpr std::string_view allowed_attributes[] = {
        "kind", "styleName", "styleObjectId", "styleUuid", "faceName", "height",
        "bold", "italic", "underline", "strikeout", "scale", "scaleOverride"};
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
    const bool has_style = !std::holds_alternative<std::monostate>(value.style);
    if (value.kind == model::FontKind::style_reference && !has_style) {
        fail("OOF2003", node, {}, "kind", "style reference", "missing", "Style font must carry a style reference");
    }
    if (value.kind != model::FontKind::style_reference && has_style) {
        fail("OOF2003", node, {}, "kind", "font without style reference", "style", "Only a style font may carry a style reference");
    }
    if (value.kind == model::FontKind::automatic &&
        (value.face_name || value.height || value.bold || value.italic ||
         value.underline || value.strikeout || value.scale != 100.0 || value.scale_override)) {
        fail("OOF2003", node, {}, "kind", "automatic font defaults", "explicit font fields", "Automatic font must not carry absolute font fields");
    }
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
        return model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
            model::QualifiedName{*standard_name}};
    }
    return model::PictureRef{model::PictureAssetRef{
        parse_object_id(node_text(node), node, property, object_id)}, std::nullopt};
}

model::PropertyValue parse_property_value(
    xmlNodePtr node,
    mm::ValueCodec codec,
    std::string_view object_id
) {
    const std::string property = node_name(node);
    switch (codec) {
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
        case mm::ValueCodec::composite_id:
            return parse_composite_id(node);
        case mm::ValueCodec::type_domain:
            return parse_type_domain(node);
        case mm::ValueCodec::enumeration:
            return parse_enumeration(node);
        case mm::ValueCodec::color:
            return parse_color(node);
        case mm::ValueCodec::font:
            return parse_font(node);
        case mm::ValueCodec::shortcut:
            return parse_shortcut(node);
        case mm::ValueCodec::picture:
            return parse_picture_reference(node, property, object_id);
        case mm::ValueCodec::command_bar_buttons:
            fail("OOF2003", node, std::string(object_id), property, "owned Buttons collection", "scalar", "Buttons is not a scalar property");
        case mm::ValueCodec::dendrogram_items:
            fail("OOF2003", node, std::string(object_id), property, "owned Items collection", "scalar", "Dendrogram Items is not a scalar property");
        case mm::ValueCodec::dendrogram_links:
            fail("OOF2003", node, std::string(object_id), property, "owned Links collection", "scalar", "Dendrogram Links is not a scalar property");
        case mm::ValueCodec::control_reference:
            return model::ControlRef{parse_object_id(node_text(node), node, property, object_id)};
        case mm::ValueCodec::attribute_reference:
            return model::AttributeRef{parse_object_id(node_text(node), node, property, object_id)};
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
            return std::holds_alternative<std::string>(value) &&
                   std::get<std::string>(value) == canonical;
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
        case mm::DefaultKind::font:
            return canonical == "automatic" && std::holds_alternative<model::FontValue>(value) &&
                std::get<model::FontValue>(value) == model::FontValue{};
        case mm::DefaultKind::shortcut:
            return canonical == "None" && std::holds_alternative<model::ShortcutValue>(value) &&
                std::get<model::ShortcutValue>(value) == model::ShortcutValue{};
    }
    return false;
}

bool equals_table_column_editor_default(
    model::ControlKind kind,
    const mm::PropertyDescriptor& descriptor,
    const model::PropertyValue& value) {
    if (kind == model::ControlKind::input_field) {
        if (descriptor.api_name == "Enabled") return std::holds_alternative<bool>(value) && std::get<bool>(value);
        if (descriptor.api_name == "ReadOnly") return std::holds_alternative<bool>(value) && !std::get<bool>(value);
        return false;
    }
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
    fail("OOF2003", node, {}, "format", "gif|png|jpeg|bmp", std::string(value), "Unknown picture format");
}

std::string_view picture_format_name(model::PictureFormat format) {
    switch (format) {
        case model::PictureFormat::gif: return "gif";
        case model::PictureFormat::png: return "png";
        case model::PictureFormat::jpeg: return "jpeg";
        case model::PictureFormat::bmp: return "bmp";
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
    if (parts.size() < 3 || parts.size() % 2 == 0 || parts[0] != "Items") return false;
    for (std::size_t index = 2; index + 1 < parts.size(); index += 2) {
        if (parts[index] != "Buttons") return false;
    }
    const std::string_view file = parts.back();
    switch (format) {
        case model::PictureFormat::gif: return file == "Picture.gif";
        case model::PictureFormat::png: return file == "Picture.png";
        case model::PictureFormat::jpeg:
            return file == "Picture.jpg" || file == "Picture.jpeg";
        case model::PictureFormat::bmp: return file == "Picture.bmp";
    }
    return false;
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
    explicit DocumentParser(const mm::Metamodel& metamodel) : metamodel_(metamodel) {}

    model::OrdinaryFormDocument parse(xmlNodePtr root) {
        model::Form form;
        form.id = parse_object_id(required_attribute(root, "id"), root);
        form.name = required_attribute(root, "name", object_id_text(form.id));
        const std::string form_id = object_id_text(form.id);

        for (xmlNodePtr child : element_children(root)) {
            const std::string name = node_name(child);
            if (name == "Events") {
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
                model::PropertyValue value =
                    parse_property_value(child, descriptor->value_codec, form_id);
                if (!equals_descriptor_default(*descriptor, value)) {
                    form.properties.set_explicit(descriptor->id, std::move(value));
                }
            }
        }

        model::OrdinaryFormDocument document(std::move(form));
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
            objects_.events.push_back({
                id,
                std::string(descriptor->api_name),
                node_text(event_node),
                model::FormRef{owner},
            });
            references.push_back(model::EventRef{id});
        }
        return references;
    }

    std::vector<model::EventRef> parse_control_events(
        xmlNodePtr node,
        model::ObjectId owner,
        model::ControlKind kind
    ) {
        std::vector<model::EventRef> references;
        for (xmlNodePtr event_node : element_children(node)) {
            const std::string name = node_name(event_node);
            const mm::EventDescriptor* descriptor = metamodel_.event(kind, name);
            if (descriptor == nullptr) {
                fail("OOF2003", event_node, object_id_text(owner), name, "declared control event", name, "Control event is not declared by the executable metamodel");
            }
            const model::ObjectId id = parse_object_id(
                required_attribute(event_node, "id"),
                event_node,
                "id",
                object_id_text(owner));
            objects_.events.push_back({
                id,
                std::string(descriptor->api_name),
                node_text(event_node),
                model::ControlRef{owner},
            });
            references.push_back(model::EventRef{id});
        }
        return references;
    }

    void parse_attributes(xmlNodePtr node) {
        for (xmlNodePtr attribute_node : element_children(node)) {
            model::Attribute attribute;
            attribute.id = parse_object_id(required_attribute(attribute_node, "id"), attribute_node);
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
            if (!valid_picture_path(asset.relative_path, asset.format)) {
                fail(
                    "OOF2003",
                    asset_node,
                    id,
                    "relativePath",
                    "Items/<ElementName>[/Buttons/<ItemName>]/Picture.<matching format>",
                    asset.relative_path,
                    "Picture asset path is outside the ordinary-form source package contract");
            }
            objects_.assets.push_back(std::move(asset));
        }
    }

    model::DataPath parse_data_path(xmlNodePtr node, std::string_view owner_id) {
        model::DataPath path;
        path.attribute = model::AttributeRef{parse_object_id(
            required_attribute(node, "attributeId", owner_id),
            node,
            "attributeId",
            owner_id)};
        for (xmlNodePtr member : element_children(node)) {
            path.members.push_back(node_text(member));
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
            } else if (name == "Collapse") {
                position.collapse.set(parse_enumeration(child));
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
        auto* dendrogram = std::get_if<model::DendrogramPayload>(&control.payload);
        bool position_seen = false;
        bool dendrogram_items_seen = false;
        bool dendrogram_links_seen = false;
        bool spreadsheet_document_seen = false;
        for (xmlNodePtr child : element_children(node)) {
            const std::string name = node_name(child);
            if (descriptor->kind == model::ControlKind::chart &&
                       (name == "Series" || name == "Points" || name == "Values")) {
                auto& chart = std::get<model::ChartPayload>(control.payload);
                for (xmlNodePtr item : element_children(child)) {
                    if (name == "Series" && node_name(item) == "ChartSeries") {
                        model::ChartSeries series;
                        series.id = parse_object_id(required_attribute(item, "id"), item);
                        for (xmlNodePtr field : element_children(item)) {
                            const std::string field_name = node_name(field);
                            if (field_name == "Text") series.text = node_text(field);
                            else if (field_name == "Color") series.color = parse_color(field);
                            else if (field_name == "Marker") series.marker = parse_enumeration(field);
                            else fail("OOF2003", field, id_text, field_name, "Text, Color, or Marker", field_name, "Unknown ChartSeries field");
                        }
                        chart.series.push_back(std::move(series));
                    } else if (name == "Points" && node_name(item) == "ChartPoint") {
                        model::ChartPoint point;
                        point.id = parse_object_id(required_attribute(item, "id"), item);
                        for (xmlNodePtr field : element_children(item)) {
                            const std::string field_name = node_name(field);
                            if (field_name == "Text") point.text = node_text(field);
                            else if (field_name == "Color") point.color = parse_color(field);
                            else fail("OOF2003", field, id_text, field_name, "Text or Color", field_name, "Unknown ChartPoint field");
                        }
                        chart.points.push_back(std::move(point));
                    } else if (name == "Values" && node_name(item) == "ChartValue") {
                        model::ChartValue value;
                        value.series_ref = parse_object_id(required_attribute(item, "seriesRef"), item);
                        value.point_ref = parse_object_id(required_attribute(item, "pointRef"), item);
                        const auto fields = element_children(item);
                        if (fields.size() != 1) fail("OOF2003", item, id_text, "ChartValue", "one Number or Undefined", std::to_string(fields.size()), "ChartValue requires one typed value");
                        if (node_name(fields.front()) == "Number") value.value = model::DecimalValue{canonical_decimal(node_text(fields.front()), fields.front(), "Number", id_text)};
                        else if (node_name(fields.front()) == "Undefined") {
                            if (node_text(fields.front()) != "undefined") fail("OOF2003", fields.front(), id_text, "Undefined", "undefined", node_text(fields.front()), "Invalid Undefined value");
                            value.value = model::UndefinedValue{};
                        } else fail("OOF2003", fields.front(), id_text, node_name(fields.front()), "Number or Undefined", node_name(fields.front()), "Unknown ChartValue type");
                        chart.values.push_back(std::move(value));
                    } else {
                        fail("OOF2003", item, id_text, node_name(item), name == "Series" ? "ChartSeries" : name == "Points" ? "ChartPoint" : "ChartValue", node_name(item), "Unknown Chart collection item");
                    }
                }
            }
            else if (name == "Document" && descriptor->kind == model::ControlKind::spreadsheet_document_field) {
                if (spreadsheet_document_seen)
                    fail("OOF2003", child, id_text, "Document", "at most one Document element", "duplicate", "SpreadsheetDocumentField has duplicate Document elements");
                spreadsheet_document_seen = true;
                if (child->properties != nullptr)
                    fail("OOF2003", child, id_text, "Document", "no attributes", "present", "Spreadsheet Document does not accept attributes");
                auto& cells = std::get<model::SpreadsheetDocumentFieldPayload>(control.payload).cells;
                std::set<std::pair<std::uint32_t, std::uint32_t>> coordinates;
                for (xmlNodePtr cell_node : element_children(child)) {
                    if (node_name(cell_node) != "Cell")
                        fail("OOF2003", cell_node, id_text, node_name(cell_node), "Cell", node_name(cell_node), "Unknown spreadsheet Document item");
                    for (xmlAttrPtr attr = cell_node->properties; attr != nullptr; attr = attr->next) {
                        const std::string_view attr_name(reinterpret_cast<const char*>(attr->name));
                        if (attr_name != "row" && attr_name != "column")
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
                    std::string text;
                    std::optional<model::TypeDomainPatternValue> value_type;
                    std::optional<model::PropertyValue> typed_value;
                    unsigned int content_stage = 0;
                    for (xmlNodePtr value_node : element_children(cell_node)) {
                        const auto name = node_name(value_node);
                        if (name == "Text") {
                            if (text_seen || content_stage != 0)
                                fail("OOF2003", value_node, id_text, "Text", "one standalone Text child", "duplicate or mixed content", "Spreadsheet Cell Text cannot be combined with a typed value");
                            if (value_node->properties != nullptr || !element_children(value_node).empty())
                                fail("OOF2003", value_node, id_text, "Text", "text content without attributes or nested elements", "structured content", "Spreadsheet Cell Text must be plain text");
                            text_seen = true;
                            content_stage = 1;
                            text = node_text(value_node);
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
                                static constexpr std::array<std::string_view, 8> allowed{
                                    "term", "typeUuid", "length", "precision", "nonNegative", "variable", "date", "time"};
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
                            typed_value = parse_property_value(value_node, codec, id_text);
                            if (std::holds_alternative<model::UndefinedValue>(*typed_value))
                                fail("OOF2003", value_node, id_text, "Value", "defined String, Number, Boolean, or Date", "undefined", "Undefined Spreadsheet Cell values are unsupported");
                            value_seen = true;
                            content_stage = 4;
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
                    } else if (!text_seen) {
                        fail("OOF2003", cell_node, id_text, "Text", "required Text child", "missing", "Spreadsheet Cell requires Text or a typed value");
                    }
                    cells.push_back({row, column, std::move(text), std::move(typed)});
                }
                std::sort(cells.begin(), cells.end(), [](const auto& left, const auto& right) {
                    return std::tie(left.row, left.column) < std::tie(right.row, right.column);
                });
                continue;
            }
            else if (name == "Buttons" && (descriptor->kind == model::ControlKind::button || descriptor->kind == model::ControlKind::command_bar)) {
                if (auto* payload = std::get_if<model::ButtonPayload>(&control.payload)) payload->buttons = parse_command_bar_buttons(child, id_text);
                else std::get<model::CommandBarPayload>(control.payload).buttons = parse_command_bar_buttons(child, id_text);
            } else if (name == "Columns" && descriptor->kind == model::ControlKind::table) {
                std::get<model::TablePayload>(control.payload).columns = parse_table_columns(child, id_text);
            } else if (name == "DataPath") {
                control.data_path = parse_data_path(child, id_text);
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
                control.events = parse_control_events(child, id, descriptor->kind);
            } else if (name == "ChildItems") {
                control.children = parse_child_items(child);
            } else {
                const mm::PropertyDescriptor* property =
                    metamodel_.property(descriptor->kind, name);
                if (property == nullptr) {
                    fail("OOF2003", child, id_text, name, "declared control property", name, "Control property is not declared by the executable metamodel");
                }
                model::PropertyValue value =
                    parse_property_value(child, property->value_codec, id_text);
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
        if (dendrogram != nullptr) validate_dendrogram_graph(*dendrogram, node, id_text);
        if (!position_seen) {
            fail("OOF2003", node, id_text, "Position", "required Position", "missing", "Control has no typed Position");
        }
        const model::ControlRef reference{id};
        objects_.controls.push_back(std::move(control));
        return reference;
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
            column.name = required_attribute(column_node, "name", owner);
            if (column.name.empty() || !names.insert(column.name).second) {
                fail("OOF2003", column_node, std::string(owner), "name",
                    "non-empty unique Column name", column.name, "Table Column name is empty or duplicated");
            }
            std::set<std::string> seen;
            bool data_path_seen = false;
            bool header_seen = false;
            bool control_seen = false;
            for (xmlNodePtr field : element_children(column_node)) {
                const std::string field_name = node_name(field);
                if (!seen.insert(field_name).second) {
                    fail("OOF2003", field, std::string(owner), field_name,
                        "Column field at most once", field_name, "Duplicate Table Column field");
                }
                if (field_name == "DataPath") {
                    column.data_path = node_text(field);
                    data_path_seen = true;
                } else if (field_name == "Header") {
                    column.header = parse_localized_string(field);
                    header_seen = true;
                } else if (field_name == "Control") {
                    for (xmlAttrPtr attr = field->properties; attr != nullptr; attr = attr->next) {
                        const std::string_view attr_name(reinterpret_cast<const char*>(attr->name));
                        if (attr_name != "type") {
                            fail("OOF2003", field, std::string(owner), std::string(attr_name),
                                "type attribute", std::string(attr_name), "Unsupported Table Column Control attribute");
                        }
                    }
                    const std::string type = required_attribute(field, "type", owner);
                    if (type == "InputField") {
                        column.control.kind = model::ControlKind::input_field;
                    } else if (type == "ChoiceField") {
                        column.control.kind = model::ControlKind::choice_field;
                    } else if (type == "CheckBox") {
                        column.control.kind = model::ControlKind::check_box;
                    } else {
                        fail("OOF2003", field, std::string(owner), "type",
                            "InputField, ChoiceField, or CheckBox", type, "Unsupported Table Column Control type");
                    }
                    std::set<std::string> control_properties;
                    for (xmlNodePtr property_node : element_children(field)) {
                        const std::string property_name = node_name(property_node);
                        const bool supported_property =
                            (column.control.kind == model::ControlKind::input_field &&
                                (property_name == "Enabled" || property_name == "ReadOnly")) ||
                            (column.control.kind == model::ControlKind::choice_field &&
                                (property_name == "Enabled" || property_name == "ToolTip")) ||
                            (column.control.kind == model::ControlKind::check_box &&
                                (property_name == "Enabled" || property_name == "Caption" ||
                                    property_name == "ToolTip" || property_name == "Font"));
                        if (!control_properties.insert(property_name).second || !supported_property) {
                            fail("OOF2003", property_node, std::string(owner), property_name,
                                "a compatible named default property, at most once", property_name,
                                "Unsupported Table Column editor property");
                        }
                        const auto* descriptor = metamodel_.property(column.control.kind, property_name);
                        if (descriptor == nullptr || descriptor->surface != mm::PropertySurface::control_payload) {
                            fail("OOF2003", property_node, std::string(owner), property_name,
                                "declared payload property for the selected editor", property_name,
                                "Table Column editor property is not declared by its typed owner");
                        }
                        const model::PropertyValue value = parse_property_value(
                            property_node, descriptor->value_codec, owner);
                        if (!equals_table_column_editor_default(column.control.kind, *descriptor, value)) {
                            fail("OOF2003", property_node, std::string(owner), property_name,
                                "the observed default value", node_text(property_node),
                                "Table Column editor only supports observed default property values");
                        }
                    }
                    control_seen = true;
                } else {
                    fail("OOF2003", field, std::string(owner), field_name,
                        "DataPath, Header, and Control", field_name, "Unsupported Table Column field");
                }
            }
            if (!data_path_seen || !header_seen || !control_seen || column.data_path.empty() || column.header.items.empty()) {
                fail("OOF2003", column_node, std::string(owner), "Column fields",
                    "DataPath, localized Header, and typed Control", "incomplete", "Table Column is incomplete");
            }
            columns.push_back(std::move(column));
        }
        if (columns.empty()) {
            fail("OOF2003", node, std::string(owner), "Column",
                "at least one Column", "empty", "Table Columns collection is empty");
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
            if (name == "Text") item.text = node_text(child);
            else if (name == "Explanation") item.explanation = node_text(child);
            else if (name == "ToolTip") item.tooltip = node_text(child);
            else if (name == "Enabled") item.enabled = parse_boolean(node_text(child), child, "Enabled", owner);
            else if (name == "Checked") item.checked = parse_boolean(node_text(child), child, "Checked", owner);
            else if (name == "ChangesData") item.changes_data = parse_boolean(node_text(child), child, "ChangesData", owner);
            else if (name == "Representation") {
                const auto rep = node_text(child);
                if (rep == "Auto") item.representation = model::ButtonRepresentation::automatic;
                else if (rep == "Picture") item.representation = model::ButtonRepresentation::picture;
                else if (rep == "Text") item.representation = model::ButtonRepresentation::text;
                else if (rep == "PictureText") item.representation = model::ButtonRepresentation::picture_text;
                else fail("OOF2003", child, std::string(owner), name, "Auto, Picture, Text, or PictureText", rep, "Unknown button representation");
            } else if (name == "Shortcut") item.shortcut = parse_shortcut(child);
            else if (name == "Picture") item.picture = parse_picture_reference(child, "Picture", owner);
            else if (name == "Action") item.action = node_text(child);
            else if (name == "Order") {
                has_order = true;
                const auto order = node_text(child);
                if (order == "DontOrder") item.order = model::CommandBarButtonOrder::none;
                else if (order == "Ascending") item.order = model::CommandBarButtonOrder::ascending;
                else if (order == "Descending") item.order = model::CommandBarButtonOrder::descending;
                else fail("OOF2003", child, std::string(owner), name, "DontOrder, Ascending, or Descending", order, "Unknown submenu order");
            }
            else if (name == "Buttons") item.buttons = parse_command_bar_buttons(child, owner);
            else fail("OOF2003", child, std::string(owner), name, "declared button menu field", name, "Unknown button menu field");
        }
        if (item.type == model::CommandBarButtonKind::action && (!item.action || item.action->empty()))
            fail("OOF2003", node, std::string(owner), "Action", "non-empty action handler", "missing", "Action item requires a handler");
        if (item.type != model::CommandBarButtonKind::action && item.action)
            fail("OOF2003", node, std::string(owner), "Action", "Action item only", "present", "Only Action items may declare a handler");
        if (item.type != model::CommandBarButtonKind::submenu && has_order)
            fail("OOF2003", node, std::string(owner), "Order", "Submenu only", "present", "Only Submenu items may declare an order");
        if (item.type != model::CommandBarButtonKind::submenu && !item.buttons.empty())
            fail("OOF2003", node, std::string(owner), "Buttons", "Submenu only", "present", "Only Submenu items may contain buttons");
        if (item.type == model::CommandBarButtonKind::separator && seen.size() != 0)
            fail("OOF2003", node, std::string(owner), "fields", "no separator fields", "present", "Separator cannot have fields");
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

    std::vector<model::CommandBarButton> parse_command_bar_buttons(xmlNodePtr node, std::string_view owner) {
        std::vector<model::CommandBarButton> result;
        std::set<std::string> names;
        for (xmlNodePtr child : element_children(node)) {
            auto item = parse_command_bar_button(child, owner);
            if (!names.insert(item.name).second) fail("OOF2003", child, std::string(owner), item.name, "unique item name in collection", item.name, "Duplicate button menu item name");
            result.push_back(std::move(item));
        }
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
        return children;
    }

    const mm::Metamodel& metamodel_;
    ParsedObjects objects_;
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
        output_ += ">\n";
        ++depth_;
    }

    void close(std::string_view name) {
        --depth_;
        indent();
        output_ += "</";
        output_.append(name);
        output_ += ">\n";
    }

    void empty(std::string_view name, const XmlAttributes& attributes = {}) {
        indent();
        output_.push_back('<');
        output_.append(name);
        append_attributes(attributes);
        output_ += "/>\n";
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
        output_ += ">\n";
    }

    [[nodiscard]] std::string take() {
        return std::move(output_);
    }

private:
    void indent() {
        output_.append(depth_ * 2, ' ');
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
        writer_.open("Form", {
            {"id", form_id},
            {"name", form.name},
            {"ordinaryFormVersion", std::string(ordinary_form_xml_version)},
        });
        write_property_set(form.properties, metamodel_.form_properties(), form_id);
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
        for (const auto& item : value.items) {
            writer_.text("Item", item.text, {{"language", item.language}});
        }
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
                case model::TypeDomainTerm::boolean:
                    break;
                case model::TypeDomainTerm::numeric:
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
        if (value.kind == model::FontKind::automatic &&
            (value.face_name || value.height || value.bold || value.italic ||
             value.underline || value.strikeout || value.scale != 100.0 || value.scale_override)) {
            serialization_fail(std::string(object_id), std::string(name), "automatic font defaults", "explicit font data", "Automatic font carries absolute fields");
        }
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
        writer_.empty(name, attributes);
    }

    void write_picture_reference(
        std::string_view element_name,
        const model::PictureRef& reference,
        std::string_view object_id
    ) {
        if (reference.standard_name) {
            if (reference.asset.id().value() != 0 ||
                mm::find_standard_picture(reference.standard_name->value) == nullptr) {
                serialization_fail(std::string(object_id), std::string(element_name),
                    "known standard picture name with empty asset target", "invalid target",
                    "Picture reference has inconsistent targets");
            }
            writer_.empty(element_name, {{"standardName", reference.standard_name->value}});
            return;
        }
        if (reference.asset.id().value() == 0) {
            serialization_fail(std::string(object_id), std::string(element_name),
                "positive picture asset ID", "0", "Picture asset reference is empty");
        }
        writer_.text(element_name, object_id_text(reference.asset.id()));
    }

    void write_property(
        const mm::PropertyDescriptor& descriptor,
        const model::PropertyValue& value,
        std::string_view object_id
    ) {
        const std::string_view name = descriptor.xml_name;
        switch (descriptor.value_codec) {
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
            case mm::ValueCodec::localized_string:
                write_localized(name, require_value<model::LocalizedStringValue>(value, object_id, name, "localized string"), object_id);
                return;
            case mm::ValueCodec::formatted_string: {
                const auto& formatted = require_value<model::FormattedStringValue>(value, object_id, name, "formatted string");
                writer_.open(name, {{"formatted", formatted.formatted ? "true" : "false"}});
                write_localized("Value", formatted.value, object_id);
                writer_.close(name);
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
            case mm::ValueCodec::command_bar_buttons:
                serialization_fail(std::string(object_id), std::string(name), "owned Buttons collection", "scalar", "Buttons is not a scalar property");
            case mm::ValueCodec::dendrogram_items:
                serialization_fail(std::string(object_id), std::string(name), "owned Items collection", "scalar", "Dendrogram Items is not a scalar property");
            case mm::ValueCodec::dendrogram_links:
                serialization_fail(std::string(object_id), std::string(name), "owned Links collection", "scalar", "Dendrogram Links is not a scalar property");
            case mm::ValueCodec::control_reference:
                writer_.text(name, object_id_text(require_value<model::ControlRef>(value, object_id, name, "control reference").id()));
                return;
            case mm::ValueCodec::attribute_reference:
                writer_.text(name, object_id_text(require_value<model::AttributeRef>(value, object_id, name, "attribute reference").id()));
                return;
            case mm::ValueCodec::command_reference:
                writer_.text(name, object_id_text(require_value<model::CommandRef>(value, object_id, name, "command reference").id()));
                return;
        }
    }

    void write_property_set(
        const model::PropertySet& properties,
        std::span<const mm::PropertyDescriptor> descriptors,
        std::string_view object_id,
        bool skip_reserved_extensions = false
    ) {
        for (const auto& descriptor : descriptors) {
            if (skip_reserved_extensions &&
                (descriptor.api_name == "Name" || descriptor.api_name == "Data")) {
                continue;
            }
            const model::PropertyEntry* entry = properties.find(descriptor.id);
            if (entry != nullptr && !equals_descriptor_default(descriptor, entry->value)) {
                write_property(descriptor, entry->value, object_id);
            }
        }
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
                    const mm::EventDescriptor* actual =
                        std::holds_alternative<model::FormRef>(event->owner)
                            ? metamodel_.form_event(event->name)
                            : metamodel_.event(
                                  document_.find_control(std::get<model::ControlRef>(event->owner).id())->kind(),
                                  event->name);
                    if (actual == &descriptor) ordered.emplace_back(&descriptor, event);
                }
            }
        }
        if (ordered.size() != references.size()) {
            serialization_fail(std::string(owner_id), "Events", "all events declared for this owner", std::to_string(ordered.size()), "Event sequence cannot be projected without loss");
        }
        writer_.open(container_name);
        for (const auto& [descriptor, event] : ordered) {
            writer_.text(
                descriptor->xml_name,
                event->handler,
                {{"id", object_id_text(event->id)}});
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
                serialization_fail(id, "relativePath", "Items/<ElementName>[/Buttons/<ItemName>]/Picture.<matching format>", asset.relative_path, "Picture asset path is outside the source package contract");
            }
            writer_.empty("PictureAsset", {
                {"id", id},
                {"relativePath", asset.relative_path},
                {"format", std::string(picture_format_name(asset.format))},
                {"transparent", asset.transparent ? "true" : "false"},
            });
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
        for (const std::string& member : path.members) writer_.text("Member", member);
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

    void write_position(const model::Position& position) {
        const bool default_control =
            position.default_control.is_explicit() && position.default_control.value().has_value();
        const bool top = position.top.is_explicit() && position.top.value() != 0;
        const bool visible = position.visible.is_explicit() && !position.visible.value();
        const bool height = position.height.is_explicit() && position.height.value() != 0;
        const bool left = position.left.is_explicit() && position.left.value() != 0;
        const bool tab_order = position.tab_order.is_explicit() && position.tab_order.value().has_value();
        const bool z_order = position.z_order.is_explicit() && position.z_order.value().has_value();
        const bool collapse = position.collapse.is_explicit() && position.collapse.value().has_value();
        const bool width = position.width.is_explicit() && position.width.value() != 0;
        const bool bindings = !position.bindings.anchors.empty() || !position.bindings.dimensions.empty() ||
            position.bindings.manual_horizontal.value() || position.bindings.manual_vertical.value();
        if (!(default_control || top || visible || height || left || tab_order || z_order ||
              collapse || width || bindings)) {
            writer_.empty("Position");
            return;
        }
        writer_.open("Position");
        if (default_control) writer_.text("DefaultControl", *position.default_control.value() ? "true" : "false");
        if (top) writer_.text("Top", std::to_string(position.top.value()));
        if (visible) writer_.text("Visible", "false");
        if (height) writer_.text("Height", std::to_string(position.height.value()));
        if (left) writer_.text("Left", std::to_string(position.left.value()));
        if (tab_order) writer_.text("TabOrder", std::to_string(*position.tab_order.value()));
        if (z_order) writer_.text("ZOrder", std::to_string(*position.z_order.value()));
        if (collapse) write_enumeration("Collapse", *position.collapse.value());
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
        if (page.position.is_explicit()) write_position(page.position.value());
        write_child_items(page.children);
        writer_.close("Page");
    }

    void write_command_bar_buttons(const std::vector<model::CommandBarButton>& buttons, std::string_view owner) {
        if (buttons.empty()) return;
        writer_.open("Buttons");
        for (const auto& item : buttons) {
            const char* type = item.type == model::CommandBarButtonKind::action ? "Action" :
                item.type == model::CommandBarButtonKind::submenu ? "Submenu" : "Separator";
            writer_.open("CommandBarButton", {{"name", item.name}, {"type", type}});
            if (!item.text.empty()) writer_.text("Text", item.text);
            if (!item.explanation.empty()) writer_.text("Explanation", item.explanation);
            if (!item.tooltip.empty()) writer_.text("ToolTip", item.tooltip);
            if (!item.enabled) writer_.text("Enabled", "false");
            if (item.checked) writer_.text("Checked", "true");
            if (item.changes_data) writer_.text("ChangesData", "true");
            const char* representation = item.representation == model::ButtonRepresentation::automatic ? "Auto" :
                item.representation == model::ButtonRepresentation::picture ? "Picture" :
                item.representation == model::ButtonRepresentation::text ? "Text" : "PictureText";
            if (item.representation != model::ButtonRepresentation::automatic)
                writer_.text("Representation", representation);
            if (item.shortcut != model::ShortcutValue{}) {
                writer_.open("Shortcut", {{"Alt", item.shortcut.alt ? "true" : "false"},
                    {"Ctrl", item.shortcut.ctrl ? "true" : "false"}, {"Shift", item.shortcut.shift ? "true" : "false"}});
                writer_.text("Key", item.shortcut.key);
                writer_.close("Shortcut");
            }
            if (item.picture) write_picture_reference("Picture", *item.picture, owner);
            if (item.action) writer_.text("Action", *item.action);
            if (item.type == model::CommandBarButtonKind::submenu && item.order != model::CommandBarButtonOrder::none) {
                writer_.text("Order", item.order == model::CommandBarButtonOrder::ascending ? "Ascending" : "Descending");
            }
            write_command_bar_buttons(item.buttons, owner);
            writer_.close("CommandBarButton");
        }
        writer_.close("Buttons");
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

    void write_control(const model::ControlNode& control) {
        const auto& descriptor = metamodel_.control(control.kind());
        const std::string id = object_id_text(control.id);
        writer_.open(descriptor.public_name, {{"name", control.name}, {"id", id}});
        if (control.data_path.has_value()) write_data_path(*control.data_path);
        write_property_set(
            control.extension_properties,
            metamodel_.control_extension_properties(),
            id,
            true);
        write_position(control.position);
        if (const auto* dendrogram = std::get_if<model::DendrogramPayload>(&control.payload))
            write_dendrogram_payload(*dendrogram, id);
        if (const auto* spreadsheet = std::get_if<model::SpreadsheetDocumentFieldPayload>(&control.payload);
            spreadsheet != nullptr && !spreadsheet->cells.empty()) {
            writer_.open("Document");
            for (const auto& cell : spreadsheet->cells) {
                writer_.open("Cell", {{"row", std::to_string(cell.row)}, {"column", std::to_string(cell.column)}});
                if (!cell.typed_value.has_value()) {
                    writer_.text("Text", cell.text);
                } else {
                    if (!cell.text.empty())
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
                writer_.close("Cell");
            }
            writer_.close("Document");
        }
        const std::vector<model::CommandBarButton>* owned_buttons = nullptr;
        if (const auto* button = std::get_if<model::ButtonPayload>(&control.payload)) owned_buttons = &button->buttons;
        if (const auto* command_bar = std::get_if<model::CommandBarPayload>(&control.payload)) owned_buttons = &command_bar->buttons;
        if (owned_buttons != nullptr) {
            for (const auto& property : metamodel_.properties_for(control.kind())) {
                if (property.api_name == "Buttons") write_command_bar_buttons(*owned_buttons, id);
                else if (const auto* entry = control.properties().find(property.id);
                    entry != nullptr && !equals_descriptor_default(property, entry->value))
                    write_property(property, entry->value, id);
            }
        } else if (const auto* table = std::get_if<model::TablePayload>(&control.payload)) {
            for (const auto& property : metamodel_.properties_for(control.kind())) {
                if (property.api_name == "Columns") write_table_columns(table->columns, id);
                else if (const auto* entry = control.properties().find(property.id);
                    entry != nullptr && !equals_descriptor_default(property, entry->value))
                    write_property(property, entry->value, id);
            }
        } else {
            write_property_set(control.properties(), metamodel_.properties_for(control.kind()), id);
        }
        if (const auto* chart = std::get_if<model::ChartPayload>(&control.payload)) {
            writer_.open("Series");
            for (const auto& item : chart->series) {
                writer_.open("ChartSeries", {{"id", object_id_text(item.id)}});
                writer_.text("Text", item.text);
                write_color("Color", item.color, object_id_text(item.id));
                write_enumeration("Marker", item.marker);
                writer_.close("ChartSeries");
            }
            writer_.close("Series");
            writer_.open("Points");
            for (const auto& item : chart->points) {
                writer_.open("ChartPoint", {{"id", object_id_text(item.id)}});
                writer_.text("Text", item.text);
                write_color("Color", item.color, object_id_text(item.id));
                writer_.close("ChartPoint");
            }
            writer_.close("Points");
            writer_.open("Values");
            for (const auto& item : chart->values) {
                writer_.open("ChartValue", {{"seriesRef", object_id_text(item.series_ref)}, {"pointRef", object_id_text(item.point_ref)}});
                if (const auto* number = std::get_if<model::DecimalValue>(&item.value)) writer_.text("Number", number->canonical);
                else writer_.text("Undefined", "undefined");
                writer_.close("ChartValue");
            }
            writer_.close("Values");
        }
        write_events("Events", control.events, metamodel_.events_for(control.kind()), id);
        write_child_items(control.children);
        writer_.close(descriptor.public_name);
    }

    void write_table_columns(const std::vector<model::TableColumn>& columns, std::string_view owner) {
        if (columns.empty()) {
            serialization_fail(std::string(owner), "Columns", "at least one typed Column", "empty",
                "Table Columns collection is empty");
        }
        writer_.open("Columns");
        for (const auto& column : columns) {
            std::string_view control_type;
            switch (column.control.kind) {
                case model::ControlKind::input_field: control_type = "InputField"; break;
                case model::ControlKind::choice_field: control_type = "ChoiceField"; break;
                case model::ControlKind::check_box: control_type = "CheckBox"; break;
                default:
                    serialization_fail(std::string(owner), "Column/Control", "InputField, ChoiceField, or CheckBox",
                        "unsupported control", "Table Column Control kind is unsupported");
            }
            column.control.properties.for_each_explicit([&](const model::PropertyEntry& entry) {
                const auto* descriptor = metamodel_.property(column.control.kind, entry.id);
                const bool supported = descriptor != nullptr &&
                    ((column.control.kind == model::ControlKind::input_field &&
                        (descriptor->api_name == "Enabled" || descriptor->api_name == "ReadOnly")) ||
                     (column.control.kind == model::ControlKind::choice_field &&
                        (descriptor->api_name == "Enabled" || descriptor->api_name == "ToolTip")) ||
                     (column.control.kind == model::ControlKind::check_box &&
                        (descriptor->api_name == "Enabled" || descriptor->api_name == "Caption" ||
                            descriptor->api_name == "ToolTip" || descriptor->api_name == "Font")));
                if (!supported || !equals_table_column_editor_default(column.control.kind, *descriptor, entry.value)) {
                    serialization_fail(std::string(owner), "Column/Control/" +
                        (descriptor == nullptr ? std::string("unknown") : std::string(descriptor->xml_name)),
                        "compatible observed default property", "unsupported or nondefault value",
                        "Table Column editor property is outside its typed default profile");
                }
            });
            writer_.open("Column", {{"name", column.name}});
            writer_.text("DataPath", column.data_path);
            write_localized("Header", column.header, owner);
            writer_.open("Control", {{"type", std::string(control_type)}});
            writer_.close("Control");
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

Result<model::OrdinaryFormDocument> parse_form_xml(std::string_view xml) {
    try {
        XmlDoc document = parse_and_validate_xml(xml);
        xmlNodePtr root = xmlDocGetRootElement(document.get());
        DocumentParser parser(mm::Metamodel::instance());
        model::OrdinaryFormDocument parsed = parser.parse(root);
        const model::ValidationReport report = parsed.validate();
        if (!report.ok()) {
            return Result<model::OrdinaryFormDocument>::failure(
                invariant_diagnostics(report));
        }
        return Result<model::OrdinaryFormDocument>::success(std::move(parsed));
    } catch (AdapterError& error) {
        Diagnostics diagnostics;
        diagnostics.push_back(error.take_diagnostic());
        return Result<model::OrdinaryFormDocument>::failure(std::move(diagnostics));
    } catch (const std::exception& error) {
        return Result<model::OrdinaryFormDocument>::failure(
            Diagnostics{internal_diagnostic(error.what())});
    }
}

Result<std::string> serialize_form_xml(const model::OrdinaryFormDocument& document) {
    const model::ValidationReport report = document.validate();
    if (!report.ok()) {
        return Result<std::string>::failure(invariant_diagnostics(report));
    }
    try {
        DocumentSerializer serializer(document, mm::Metamodel::instance());
        std::string xml = serializer.serialize();
        try {
            static_cast<void>(parse_and_validate_xml(xml));
        } catch (AdapterError& validation_error) {
            Diagnostic diagnostic = validation_error.take_diagnostic();
            diagnostic.code = "OOF2005";
            diagnostic.message =
                "Canonical XML produced by the adapter is invalid: " + diagnostic.message;
            throw AdapterError(std::move(diagnostic));
        }
        return Result<std::string>::success(std::move(xml));
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
