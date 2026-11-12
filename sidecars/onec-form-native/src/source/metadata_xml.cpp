#include "oof/source/metadata_xml.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <climits>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <unordered_map>

#include <libxml/parser.h>
#include <libxml/tree.h>

namespace oof::source {
namespace {

constexpr std::string_view md_uri = "http://v8.1c.ru/8.3/MDClasses";
constexpr std::string_view xr_uri = "http://v8.1c.ru/8.3/xcf/readable";
constexpr std::string_view core_uri = "http://v8.1c.ru/8.1/data/core";
constexpr std::string_view cfg_uri = "http://v8.1c.ru/8.1/data/enterprise/current-config";
constexpr std::string_view xs_uri = "http://www.w3.org/2001/XMLSchema";
constexpr std::size_t max_xml_nodes = 1000000;
constexpr std::size_t max_xml_depth = 128;
constexpr std::size_t max_type_set_depth = 32;
constexpr std::size_t max_domain_type_entries = 4096;
constexpr std::size_t max_expanded_type_entries = 100000;

struct XmlDocDeleter { void operator()(xmlDoc* value) const noexcept { if (value) xmlFreeDoc(value); } };
using XmlDoc = std::unique_ptr<xmlDoc, XmlDocDeleter>;

struct ParseFailure final : std::runtime_error {
    Diagnostic diagnostic;
    explicit ParseFailure(Diagnostic value) : std::runtime_error(value.message), diagnostic(std::move(value)) {}
};

[[noreturn]] void fail(std::string path, std::string expected, std::string actual, std::string message) {
    throw ParseFailure({"OOF3002", DiagnosticSeverity::error, {}, std::move(path), {},
        std::move(expected), std::move(actual), std::move(message)});
}

std::string local_name(const xmlNode* node) {
    return node && node->name ? reinterpret_cast<const char*>(node->name) : std::string{};
}

std::string_view namespace_uri(const xmlNode* node) noexcept {
    if (!node || !node->ns || !node->ns->href) return {};
    return reinterpret_cast<const char*>(node->ns->href);
}

bool element(const xmlNode* node, std::string_view name, std::string_view uri) noexcept {
    return node && node->type == XML_ELEMENT_NODE && local_name(node) == name && namespace_uri(node) == uri;
}

std::vector<xmlNodePtr> children(xmlNodePtr parent) {
    std::vector<xmlNodePtr> result;
    if (!parent) return result;
    for (xmlNodePtr child = parent->children; child; child = child->next)
        if (child->type == XML_ELEMENT_NODE) result.push_back(child);
    return result;
}

xmlNodePtr direct_child(xmlNodePtr parent, std::string_view name, std::string_view uri) {
    for (xmlNodePtr child = parent ? parent->children : nullptr; child; child = child->next)
        if (element(child, name, uri)) return child;
    return nullptr;
}

xmlNodePtr direct_child_named(xmlNodePtr parent, std::string_view name) {
    for (xmlNodePtr child = parent ? parent->children : nullptr; child; child = child->next)
        if (child->type == XML_ELEMENT_NODE && local_name(child) == name) return child;
    return nullptr;
}

std::string content(xmlNodePtr node) {
    if (!node) return {};
    xmlChar* raw = xmlNodeGetContent(node);
    if (!raw) return {};
    std::string value(reinterpret_cast<const char*>(raw));
    xmlFree(raw);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::string> attribute(xmlNodePtr node, const char* name) {
    xmlChar* raw = xmlGetProp(node, BAD_CAST name);
    if (!raw) return std::nullopt;
    std::string value(reinterpret_cast<const char*>(raw));
    xmlFree(raw);
    return value;
}

std::string required_attribute(xmlNodePtr node, const char* name, std::string_view path) {
    auto value = attribute(node, name);
    if (!value || value->empty()) fail(std::string(path), std::string("nonempty ") + name, "missing",
        "Required metadata XML attribute is absent");
    return std::move(*value);
}

std::string canonical_uuid(std::string value, std::string_view path) {
    if (value.size() != 36) fail(std::string(path), "UUID", value, "Metadata UUID has invalid length");
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') fail(std::string(path), "hyphenated UUID", value, "Metadata UUID is malformed");
        } else if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
            fail(std::string(path), "hexadecimal UUID", value, "Metadata UUID is malformed");
        }
        value[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[index])));
    }
    if (std::ranges::all_of(value, [](char c) { return c == '0' || c == '-'; }))
        fail(std::string(path), "nonzero UUID", value, "Metadata UUID must be nonzero");
    return value;
}

void check_tree_limits(xmlNodePtr node, std::size_t depth, std::size_t& count) {
    if (!node) return;
    if (depth > max_xml_depth || ++count > max_xml_nodes)
        fail("$", "metadata XML within node/depth limits", std::to_string(count), "Metadata XML exceeds parser limits");
    for (xmlNodePtr child = node->children; child; child = child->next) check_tree_limits(child, depth + 1, count);
}

struct GeneratedType {
    model::UuidValue type_id;
    model::UuidValue owner_id;
    std::string name;
    std::string category;
};

struct PendingMember {
    model::UuidValue owner_id;
    std::string name;
    MetadataMemberKey key;
    xmlNodePtr type_node = nullptr;
    std::string path;
};

struct TypeSet {
    xmlNodePtr type_node = nullptr;
    std::string path;
};

std::vector<xmlNodePtr> descendants(xmlNodePtr root, std::string_view name, std::string_view uri) {
    std::vector<xmlNodePtr> result;
    std::vector<xmlNodePtr> pending{root};
    std::size_t count = 0;
    while (!pending.empty()) {
        xmlNodePtr node = pending.back(); pending.pop_back();
        if (++count > max_xml_nodes) fail("$", "bounded XML traversal", "node limit", "Metadata XML traversal exceeded limits");
        if (element(node, name, uri)) result.push_back(node);
        for (xmlNodePtr child = node ? node->children : nullptr; child; child = child->next)
            if (child->type == XML_ELEMENT_NODE) pending.push_back(child);
    }
    return result;
}

std::string qname_local(xmlNodePtr node, std::string_view value, std::string_view path, std::string_view* uri_out = nullptr) {
    const auto colon = value.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == value.size())
        fail(std::string(path), "qualified type name", std::string(value), "Metadata type name is not namespace-qualified");
    const std::string prefix(value.substr(0, colon));
    xmlNsPtr ns = xmlSearchNs(node->doc, node, BAD_CAST prefix.c_str());
    if (!ns || !ns->href) fail(std::string(path), "declared namespace prefix", prefix, "Metadata QName prefix is undeclared");
    if (uri_out) *uri_out = reinterpret_cast<const char*>(ns->href);
    return std::string(value.substr(colon + 1));
}

std::optional<std::uint32_t> numeric_child(xmlNodePtr parent, std::string_view name, std::string_view uri) {
    xmlNodePtr node = direct_child(parent, name, uri);
    if (!node) return std::nullopt;
    const std::string value = content(node);
    std::uint32_t parsed{};
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || end != value.data() + value.size())
        fail("$/Type/Qualifiers", "unsigned 32-bit integer", value, "Metadata type qualifier is invalid");
    return parsed;
}

bool text_is(xmlNodePtr parent, std::string_view name, std::string_view uri, std::string_view expected) {
    xmlNodePtr node = direct_child(parent, name, uri);
    return node && content(node) == expected;
}

std::vector<std::pair<xmlNodePtr, bool>> type_items(xmlNodePtr root) {
    std::vector<std::pair<xmlNodePtr, bool>> result;
    std::vector<xmlNodePtr> pending{root};
    std::size_t visited = 0;
    while (!pending.empty()) {
        xmlNodePtr node = pending.back();
        pending.pop_back();
        if (++visited > max_xml_nodes) fail("$/Type", "bounded TypeDescription traversal", "node limit",
            "Metadata TypeDescription traversal exceeded limits");
        if (node != root && (local_name(node) == "Type" || local_name(node) == "TypeSet")) {
            if (namespace_uri(node) != core_uri)
                fail("$/Type", "v8 core Type or TypeSet element namespace", local_name(node),
                    "Metadata type element uses an unexpected namespace");
            result.emplace_back(node, local_name(node) == "TypeSet");
            continue;
        }
        std::vector<xmlNodePtr> element_children;
        for (xmlNodePtr child = node ? node->children : nullptr; child; child = child->next)
            if (child->type == XML_ELEMENT_NODE) element_children.push_back(child);
        for (auto child = element_children.rbegin(); child != element_children.rend(); ++child) pending.push_back(*child);
    }
    return result;
}

struct TypeParser {
    const std::unordered_map<std::string, GeneratedType>& generated;
    const std::unordered_map<std::string, TypeSet>& type_sets;
    std::vector<std::string> stack;
    std::unordered_map<std::string,
        std::pair<std::optional<model::TypeDomainPatternValue>, std::optional<model::QualifiedName>>> memo;
    std::size_t expanded_entries = 0;

    void charge(std::size_t count, std::string_view path) {
        if (count > max_expanded_type_entries - std::min(expanded_entries, max_expanded_type_entries))
            fail(std::string(path), "expanded type entries within configured budget", std::to_string(expanded_entries + count),
                "Metadata type-set expansion exceeds the shared entry budget");
        expanded_entries += count;
    }

    void append_entries(model::TypeDomainPatternValue& domain,
        const model::TypeDomainPatternValue& addition, std::string_view path) {
        const std::size_t count = addition.entries.size();
        if (count > max_domain_type_entries - std::min(domain.entries.size(), max_domain_type_entries))
            fail(std::string(path), "type domain within configured entry limit", std::to_string(domain.entries.size() + count),
                "Metadata type domain exceeds its per-domain entry limit");
        charge(count, path);
        domain.entries.insert(domain.entries.end(), addition.entries.begin(), addition.entries.end());
    }

    void append_entry(model::TypeDomainPatternValue& domain, model::TypeDomainEntry entry, std::string_view path) {
        if (domain.entries.size() >= max_domain_type_entries)
            fail(std::string(path), "type domain within configured entry limit", std::to_string(domain.entries.size() + 1),
                "Metadata type domain exceeds its per-domain entry limit");
        charge(1, path);
        domain.entries.push_back(std::move(entry));
    }

    std::pair<std::optional<model::TypeDomainPatternValue>, std::optional<model::QualifiedName>> parse_type_set(
        const std::string& name, std::size_t depth) {
        const auto found = type_sets.find(name);
        if (found == type_sets.end()) return {std::nullopt, model::QualifiedName{name}};
        if (const auto cached = memo.find(name); cached != memo.end()) {
            if (cached->second.first) charge(cached->second.first->entries.size(), found->second.path);
            return cached->second;
        }
        if (depth > max_type_set_depth || std::ranges::find(stack, name) != stack.end())
            fail(found->second.path, "acyclic bounded type-set references", name, "Metadata type-set reference cycle detected");
        stack.push_back(name);
        auto result = parse_type_node(found->second.type_node, found->second.path, depth + 1);
        stack.pop_back();
        if (result.first) charge(result.first->entries.size(), found->second.path);
        memo.emplace(name, result);
        return result;
    }

    std::pair<std::optional<model::TypeDomainPatternValue>, std::optional<model::QualifiedName>> parse_type_node(
        xmlNodePtr type_node, std::string_view path, std::size_t depth = 0) {
        if (!type_node) return {std::nullopt, std::nullopt};
        model::TypeDomainPatternValue domain;
        std::optional<model::QualifiedName> unresolved;
        for (const auto& [type_value, is_type_set] : type_items(type_node)) {
            const std::string name = content(type_value);
            std::string_view qname_uri;
            const std::string local = qname_local(type_value, name, std::string(path) + "/Type", &qname_uri);
            if (is_type_set) {
                if (qname_uri != cfg_uri) {
                    unresolved = model::QualifiedName{name};
                    continue;
                }
                auto resolved_set = parse_type_set(local, depth + 1);
                if (resolved_set.first) append_entries(domain, *resolved_set.first, path);
                else unresolved = model::QualifiedName{name};
                continue;
            }
            model::TypeDomainEntry entry;
            if (qname_uri == xs_uri) {
                if (local == "boolean") entry.term = model::TypeDomainTerm::boolean;
                else if (local == "string") entry.term = model::TypeDomainTerm::string;
                else if (local == "decimal") entry.term = model::TypeDomainTerm::numeric;
                else if (local == "dateTime") entry.term = model::TypeDomainTerm::date;
                else if (local == "base64Binary") entry.term = model::TypeDomainTerm::binary;
                else { unresolved = model::QualifiedName{name}; continue; }
                if (entry.term == model::TypeDomainTerm::numeric) {
                    if (auto* q = direct_child(type_node, "NumberQualifiers", core_uri)) {
                        entry.numeric.length = numeric_child(q, "Digits", core_uri).value_or(0);
                        entry.numeric.precision = numeric_child(q, "FractionDigits", core_uri).value_or(0);
                        entry.numeric.non_negative = text_is(q, "AllowedSign", core_uri, "NonNegative");
                    }
                } else if (entry.term == model::TypeDomainTerm::string || entry.term == model::TypeDomainTerm::binary) {
                    const auto qname = entry.term == model::TypeDomainTerm::string ? "StringQualifiers" : "BinaryDataQualifiers";
                    if (auto* q = direct_child(type_node, qname, core_uri)) {
                        const auto length = numeric_child(q, "Length", core_uri);
                        if (entry.term == model::TypeDomainTerm::string) entry.string = {length.value_or(0), !text_is(q, "AllowedLength", core_uri, "Fixed")};
                        else entry.binary = {length.value_or(0), !text_is(q, "AllowedLength", core_uri, "Fixed")};
                    }
                } else if (entry.term == model::TypeDomainTerm::date) {
                    if (auto* q = direct_child(type_node, "DateQualifiers", core_uri)) {
                        const std::string fractions = content(direct_child(q, "DateFractions", core_uri));
                        entry.date.date = fractions == "Date" || fractions == "DateTime";
                        entry.date.time = fractions == "Time" || fractions == "DateTime";
                    }
                }
                append_entry(domain, std::move(entry), path);
                continue;
            }
            if (qname_uri == core_uri) {
                if (local == "ValueList") entry.term = model::TypeDomainTerm::value_list;
                else if (local == "ValueTable") entry.term = model::TypeDomainTerm::value_table;
                else if (local == "ValueTree") entry.term = model::TypeDomainTerm::value_tree;
                else { unresolved = model::QualifiedName{name}; continue; }
                append_entry(domain, std::move(entry), path);
                continue;
            }
            if (qname_uri != cfg_uri) {
                unresolved = model::QualifiedName{name};
                continue;
            }
            if (auto known = generated.find(local); known != generated.end()) {
                // GeneratedType category describes its metadata interface. The
                // serialized type domain identifies every nominal type by UUID.
                entry.term = model::TypeDomainTerm::object;
                entry.type_uuid = known->second.type_id;
                append_entry(domain, std::move(entry), path);
                continue;
            }
            unresolved = model::QualifiedName{name};
        }
        if (unresolved) return {std::nullopt, std::move(unresolved)};
        if (domain.entries.empty()) return {std::nullopt, std::nullopt};
        return {std::move(domain), std::nullopt};
    }
};

}  // namespace

std::optional<std::string_view> ConfigurationTypeSchema::name_for(const model::UuidValue& type_id) const {
    try {
        const auto found = identities_.find(canonical_uuid(type_id.canonical, "$/TypeIdentity/@uuid"));
        if (found != identities_.end()) return found->second;
    } catch (const ParseFailure&) {
        // Invalid and unknown identities have no declaration name.
    }
    return std::nullopt;
}

bool ConfigurationTypeSchema::declares(std::string_view name) const {
    return declarations_.contains(name);
}

Result<ConfigurationTypeSchema> parse_configuration_type_schema(std::string_view xsd) {
    constexpr std::string_view identity_uri = "urn:oof:configuration-types:1";
    try {
        if (xsd.empty() || xsd.size() > metadata_xml_max_document_bytes)
            fail("$", "nonempty XSD within byte limit", std::to_string(xsd.size()),
                "Configuration type schema input exceeds configured limits");
        xmlInitParser();
        // No schema compilation, external loading, DTD loading, or entity substitution.
        XmlDoc doc(xmlReadMemory(xsd.data(), static_cast<int>(xsd.size()), "configuration-types.xsd", nullptr,
            XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOCDATA | XML_PARSE_COMPACT | XML_PARSE_NOERROR | XML_PARSE_NOWARNING));
        if (!doc) fail("$", "well-formed XSD", "malformed", "Configuration type schema cannot be parsed");
        if (doc->intSubset || doc->extSubset)
            fail("$", "XML without DTD declarations", "DTD", "Configuration type schema DTDs are forbidden");
        xmlNodePtr root = xmlDocGetRootElement(doc.get());
        if (!element(root, "schema", xs_uri) || !xmlHasNsProp(root, BAD_CAST "targetNamespace", nullptr) ||
            attribute(root, "targetNamespace") != cfg_uri)
            fail("$", "XSD schema in the current-config target namespace", local_name(root),
                "Configuration type schema root or target namespace is unsupported");
        std::size_t nodes = 0;
        check_tree_limits(root, 0, nodes);
        std::set<std::string, std::less<>> declarations;
        std::map<std::string, std::string, std::less<>> identities;
        std::set<xmlNodePtr> annotated_declarations;
        std::vector<xmlNodePtr> pending{root};
        while (!pending.empty()) {
            xmlNodePtr node = pending.back();
            pending.pop_back();
            const std::string local = local_name(node);
            const bool type = local == "simpleType" || local == "complexType";
            if (type) {
                if (namespace_uri(node) != xs_uri)
                    fail("$/schema", "XSD type declaration namespace", local,
                        "Configuration type declaration uses an unexpected namespace");
                const auto name = attribute(node, "name");
                if (node->parent == root) {
                    if (!name || name->empty() || xmlValidateNCName(BAD_CAST name->c_str(), 0) != 0)
                        fail("$/schema/type/@name", "nonempty NCName", name.value_or("missing"),
                            "Configuration type declaration name is invalid");
                    if (declarations.size() >= ConfigurationTypeSchema::max_declarations)
                        fail("$/schema", "bounded type declaration count", std::to_string(declarations.size() + 1),
                            "Configuration type schema exceeds declaration limit");
                    if (!declarations.insert(*name).second)
                        fail("$/schema/type/@name", "unique type name", *name,
                            "Configuration type declaration name is duplicated");
                } else if (name || namespace_uri(node->parent) != xs_uri ||
                    (local_name(node->parent) != "element" &&
                     (local != "simpleType" || (local_name(node->parent) != "restriction" &&
                      local_name(node->parent) != "list" && local_name(node->parent) != "union")))) {
                    fail("$/schema/type", "anonymous nested type in XSD content", name.value_or("invalid location"),
                        "Configuration nested type declaration is malformed");
                }
                for (xmlAttrPtr property = node->properties; property; property = property->next) {
                    if (std::string_view(reinterpret_cast<const char*>(property->name)) == "name" && property->ns)
                        fail("$/schema/type/@name", "unqualified name attribute", "qualified name",
                            "Configuration type declaration name attribute is malformed");
                }
                for (xmlNodePtr child = node->children; child; child = child->next) {
                    if (child->type == XML_TEXT_NODE && !content(child).empty())
                        fail("$/schema/type", "XSD element content without free text", "text",
                            "Configuration type declaration contains unexpected text");
                }
                std::size_t annotations = 0;
                std::size_t bodies = 0;
                bool saw_body = false;
                std::size_t complex_bodies = 0;
                bool content_body = false;
                for (xmlNodePtr child : children(node)) {
                    const std::string child_name = local_name(child);
                    if (element(child, "annotation", xs_uri)) {
                        if (++annotations > 1 || saw_body)
                            fail("$/schema/type", "one leading annotation", child_name,
                                "Configuration type declaration annotation is malformed");
                        continue;
                    }
                    saw_body = true;
                    if (namespace_uri(child) != xs_uri ||
                        (local == "simpleType" && child_name != "restriction" && child_name != "list" && child_name != "union") ||
                        (local == "complexType" && child_name != "simpleContent" && child_name != "complexContent" &&
                         child_name != "group" && child_name != "all" && child_name != "choice" && child_name != "sequence" &&
                         child_name != "attribute" && child_name != "attributeGroup" && child_name != "anyAttribute"))
                        fail("$/schema/type", "XSD type content", child_name,
                            "Configuration type declaration content is malformed");
                    if (local == "complexType") {
                        const bool is_content = child_name == "simpleContent" || child_name == "complexContent";
                        const bool is_particle = is_content || child_name == "group" || child_name == "all" ||
                            child_name == "choice" || child_name == "sequence";
                        if ((is_particle && ++complex_bodies > 1) || content_body)
                            fail("$/schema/type", "one complex type body", child_name,
                                "Configuration complex type has conflicting bodies");
                        content_body = is_content;
                    }
                    if (local == "simpleType" && ++bodies > 1)
                        fail("$/schema/type", "one simple type body", child_name,
                            "Configuration simple type has multiple bodies");
                }
                if (local == "simpleType" && bodies != 1)
                    fail("$/schema/type", "one simple type body", "missing",
                        "Configuration simple type body is absent");
            }
            if (local == "TypeIdentity" || namespace_uri(node) == identity_uri) {
                xmlNodePtr appinfo = node->parent;
                xmlNodePtr annotation = appinfo ? appinfo->parent : nullptr;
                xmlNodePtr declaration = annotation ? annotation->parent : nullptr;
                if (!element(node, "TypeIdentity", identity_uri) || !element(appinfo, "appinfo", xs_uri) ||
                    !element(annotation, "annotation", xs_uri) || !declaration || declaration->parent != root ||
                    (!element(declaration, "simpleType", xs_uri) && !element(declaration, "complexType", xs_uri)))
                    fail("$/schema/TypeIdentity", "TypeIdentity inside a declared type annotation/appinfo", local,
                        "Configuration type identity namespace or location is invalid");
                if (!annotated_declarations.insert(declaration).second)
                    fail("$/schema/TypeIdentity", "one identity per declaration", "duplicate",
                        "Configuration type declaration has multiple identities");
                if (!node->properties || node->properties->next || node->properties->ns ||
                    std::string_view(reinterpret_cast<const char*>(node->properties->name)) != "uuid" ||
                    !children(node).empty() || !content(node).empty())
                    fail("$/schema/TypeIdentity", "only an unqualified uuid attribute and no content", "invalid identity",
                        "Configuration type identity attributes or content are invalid");
                const std::string uuid = canonical_uuid(required_attribute(node, "uuid", "$/schema/TypeIdentity"),
                    "$/schema/TypeIdentity/@uuid");
                const std::string name = required_attribute(declaration, "name", "$/schema/type");
                if (!identities.emplace(uuid, name).second)
                    fail("$/schema/TypeIdentity/@uuid", "unique type UUID", uuid,
                        "Configuration type identity is duplicated or conflicting");
            }
            for (xmlNodePtr child : children(node)) pending.push_back(child);
        }
        return Result<ConfigurationTypeSchema>::success(ConfigurationTypeSchema{std::move(declarations), std::move(identities)});
    } catch (const ParseFailure& failure) {
        return Result<ConfigurationTypeSchema>::failure({failure.diagnostic});
    } catch (const std::exception& exception) {
        return Result<ConfigurationTypeSchema>::failure({{"OOF3002", DiagnosticSeverity::error, {}, "$", {},
            "valid bounded configuration type XSD", "parser error", exception.what()}});
    }
}

Result<MetadataContext> parse_metadata_context(std::span<const std::string> xml_documents) {
    if (xml_documents.empty() || xml_documents.size() > metadata_xml_max_documents)
        return Result<MetadataContext>::failure({{"OOF3002", DiagnosticSeverity::error, {}, "$", {},
            "one or more bounded metadata XML documents", std::to_string(xml_documents.size()),
            "Metadata XML document count is outside configured limits"}});
    try {
        std::size_t total_bytes = 0;
        std::vector<XmlDoc> documents;
        documents.reserve(xml_documents.size());
        xmlInitParser();
        for (std::size_t index = 0; index < xml_documents.size(); ++index) {
            const auto& xml = xml_documents[index];
            if (xml.size() > metadata_xml_max_document_bytes || total_bytes > metadata_xml_max_total_bytes - std::min(xml.size(), metadata_xml_max_total_bytes))
                fail("$/Documents/" + std::to_string(index), "document and total input within byte limits",
                    std::to_string(xml.size()), "Metadata XML input exceeds configured limits");
            total_bytes += xml.size();
            if (total_bytes > metadata_xml_max_total_bytes)
                fail("$/Documents", "total input within byte limit", std::to_string(total_bytes), "Metadata XML input exceeds configured limits");
            XmlDoc doc(xmlReadMemory(xml.data(), static_cast<int>(xml.size()), "metadata.xml", nullptr,
                XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOCDATA | XML_PARSE_COMPACT | XML_PARSE_NOERROR | XML_PARSE_NOWARNING));
            if (!doc) fail("$/Documents/" + std::to_string(index), "well-formed metadata XML", "malformed", "Metadata XML cannot be parsed");
            if (doc->intSubset || doc->extSubset)
                fail("$/Documents/" + std::to_string(index), "XML without DTD declarations", "DTD", "Metadata XML DTDs are forbidden");
            xmlNodePtr root = xmlDocGetRootElement(doc.get());
            if (!element(root, "MetaDataObject", md_uri))
                fail("$/Documents/" + std::to_string(index), "MDClasses MetaDataObject root", local_name(root), "Metadata XML root namespace or name is unsupported");
            std::size_t nodes = 0;
            check_tree_limits(root, 0, nodes);
            documents.push_back(std::move(doc));
        }

        std::vector<MetadataTypeDescriptor> types;
        std::vector<PendingMember> pending_members;
        std::unordered_map<std::string, GeneratedType> generated;
        std::unordered_map<std::string, TypeSet> type_sets;
        for (std::size_t doc_index = 0; doc_index < documents.size(); ++doc_index) {
            xmlDocPtr doc = documents[doc_index].get();
            xmlNodePtr root = xmlDocGetRootElement(doc);
            auto md_objects = children(root);
            xmlNodePtr metadata_object = nullptr;
            for (xmlNodePtr child : md_objects) if (namespace_uri(child) == md_uri && local_name(child) != "InternalInfo") {
                metadata_object = child; break;
            }
            if (!metadata_object) fail("$/Documents/" + std::to_string(doc_index), "one MDClasses object", "missing", "Metadata XML has no metadata object");
            const std::string object_name = required_attribute(metadata_object, "uuid", "$/Documents/" + std::to_string(doc_index));
            model::UuidValue object_uuid{canonical_uuid(object_name, "$/MetadataObject/@uuid")};
            xmlNodePtr object_properties = direct_child(metadata_object, "Properties", md_uri);
            const std::string metadata_name = content(direct_child(object_properties, "Name", md_uri));
            auto generated_nodes = descendants(metadata_object, "GeneratedType", xr_uri);
            std::optional<model::UuidValue> member_owner;
            for (xmlNodePtr generated_node : generated_nodes) {
                const std::string generated_name = required_attribute(generated_node, "name", "$/GeneratedType");
                const std::string category = attribute(generated_node, "category").value_or("");
                const auto id_nodes = descendants(generated_node, "TypeId", xr_uri);
                if (id_nodes.empty()) fail("$/GeneratedType/TypeId", "GeneratedType.TypeId", "missing", "Generated type UUID is absent");
                model::UuidValue type_id{canonical_uuid(content(id_nodes.front()), "$/GeneratedType/TypeId")};
                GeneratedType entry{type_id, object_uuid, generated_name, category};
                const auto [known, inserted] = generated.emplace(generated_name, entry);
                if (!inserted && (known->second.type_id != type_id || known->second.category != category || known->second.owner_id != object_uuid))
                    fail("$/GeneratedType", "consistent shared generated type identity", generated_name,
                        "Generated type name has conflicting TypeId or category");
                if (inserted) types.push_back({type_id, object_uuid, generated_name});
                if (!member_owner) member_owner = object_uuid;
            }
            if (local_name(metadata_object) == "DefinedType" || local_name(metadata_object) == "Characteristic") {
                xmlNodePtr type_node = direct_child(object_properties, "Type", md_uri);
                if (!type_node && direct_child_named(object_properties, "Type"))
                    fail("$/" + object_name + "/Properties/Type", "MDClasses Type element namespace", "wrong namespace",
                        "Metadata Type element uses an unexpected namespace");
                if (type_node) {
                    const std::string qname = local_name(metadata_object) + "." + metadata_name;
                    if (!type_sets.emplace(qname, TypeSet{type_node, "$/" + qname + "/Properties/Type"}).second)
                        fail("$/" + qname, "unique type-set name", qname, "Metadata type-set name is duplicated");
                }
            }
            if (local_name(metadata_object) == "ChartOfCharacteristicTypes") {
                xmlNodePtr type_node = direct_child(object_properties, "Type", md_uri);
                if (!type_node && direct_child_named(object_properties, "Type"))
                    fail("$/" + metadata_name + "/Properties/Type", "MDClasses Type element namespace", "wrong namespace",
                        "Metadata Type element uses an unexpected namespace");
                if (type_node) {
                    const std::string qname = "Characteristic." + metadata_name;
                    if (!type_sets.emplace(qname, TypeSet{type_node, "$/" + qname + "/Properties/Type"}).second)
                        fail("$/" + qname, "unique characteristic type-set name", qname,
                            "Characteristic type-set name is duplicated");
                }
            }
            xmlNodePtr child_objects = direct_child(metadata_object, "ChildObjects", md_uri);
            if (!child_objects) continue;
            const auto child_nodes = children(child_objects);
            const bool has_attributes = std::ranges::any_of(child_nodes, [](xmlNodePtr node) {
                return namespace_uri(node) == md_uri && local_name(node) == "Attribute";
            });
            if (!has_attributes) continue;
            if (!member_owner) fail("$/ChildObjects", "GeneratedType for object owner", local_name(metadata_object), "Metadata members have no generated object owner");
            for (xmlNodePtr member : child_nodes) {
                if (namespace_uri(member) != md_uri || local_name(member) != "Attribute") continue;
                const std::string member_id = required_attribute(member, "uuid", "$/ChildObjects/Attribute");
                const std::string name = content(direct_child(direct_child(member, "Properties", md_uri), "Name", md_uri));
                if (name.empty()) fail("$/ChildObjects/Attribute/Properties/Name", "nonempty metadata attribute name", "missing", "Metadata Attribute has no name");
                xmlNodePtr properties = direct_child(member, "Properties", md_uri);
                xmlNodePtr type_node = direct_child(properties, "Type", md_uri);
                if (!type_node && direct_child_named(properties, "Type"))
                    fail("$/ChildObjects/Attribute/" + name + "/Properties/Type", "MDClasses Type element namespace",
                        "wrong namespace", "Metadata Type element uses an unexpected namespace");
                PendingMember pending{*member_owner, name,
                    MetadataMemberKey{model::UuidValue{canonical_uuid(member_id, "$/ChildObjects/Attribute/@uuid")}},
                    type_node, "$/ChildObjects/Attribute/" + name + "/Properties/Type"};
                pending_members.push_back(std::move(pending));
            }
        }

        TypeParser type_parser{generated, type_sets, {}, {}, 0};
        std::vector<MetadataMemberDescriptor> members;
        members.reserve(pending_members.size());
        for (const auto& pending : pending_members) {
            auto parsed = type_parser.parse_type_node(pending.type_node, pending.path);
            members.push_back({pending.owner_id, pending.name, pending.key, std::move(parsed.first), std::move(parsed.second)});
        }
        auto context = MetadataContext::create(std::move(types), std::move(members));
        return context;
    } catch (const ParseFailure& failure) {
        return Result<MetadataContext>::failure({failure.diagnostic});
    } catch (const std::exception& exception) {
        return Result<MetadataContext>::failure({{"OOF3002", DiagnosticSeverity::error, {}, "$", {},
            "valid bounded metadata XML", "parser error", exception.what()}});
    }
}

}  // namespace oof::source
