#include "oof/metadata_context.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace oof {
namespace {

Diagnostic error(std::string path, std::string expected, std::string actual, std::string message) {
    return {"OOF3001", DiagnosticSeverity::error, {}, std::move(path), {},
        std::move(expected), std::move(actual), std::move(message)};
}

bool valid_uuid(std::string_view text) noexcept {
    if (text.size() != 36) return false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (text[index] != '-') return false;
        } else if (!((text[index] >= '0' && text[index] <= '9') ||
                     (text[index] >= 'a' && text[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool nonzero_uuid(std::string_view text) noexcept {
    return std::ranges::any_of(text, [](char value) { return value != '0' && value != '-'; });
}

std::string member_key(const model::UuidValue& owner, std::string_view name) {
    std::string key = owner.canonical;
    key.push_back('\0');
    key.append(name);
    return key;
}

std::string member_key(const model::UuidValue& owner, const MetadataMemberKey& member) {
    std::string key = owner.canonical;
    key.push_back('\0');
    if (const auto* uuid = std::get_if<model::UuidValue>(&member)) key += "u:" + uuid->canonical;
    else key += "n:" + std::to_string(std::get<std::int64_t>(member));
    return key;
}

std::string key_description(const MetadataMemberKey& key) {
    if (const auto* id = std::get_if<model::UuidValue>(&key)) return id->canonical;
    return std::to_string(std::get<std::int64_t>(key));
}

std::optional<model::UuidValue> singular_object_uuid(const model::TypeDomainPatternValue& type) {
    if (type.entries.size() != 1) return std::nullopt;
    const auto& entry = type.entries.front();
    if ((entry.term != model::TypeDomainTerm::object && entry.term != model::TypeDomainTerm::reference) ||
        !entry.type_uuid) return std::nullopt;
    return entry.type_uuid;
}

Result<MetadataPath> path_failure(std::string path, std::string expected, std::string actual, std::string message) {
    return Result<MetadataPath>::failure({error(std::move(path), std::move(expected), std::move(actual), std::move(message))});
}

}  // namespace

Result<MetadataContext> MetadataContext::create(
    std::vector<MetadataTypeDescriptor> types,
    std::vector<MetadataMemberDescriptor> members) {
    if (types.size() > max_types || members.size() > max_members) {
        return Result<MetadataContext>::failure({error("$", "metadata within configured limits",
            std::to_string(types.size()) + " types, " + std::to_string(members.size()) + " members",
            "Metadata context exceeds its bounded descriptor limits")});
    }

    std::unordered_map<std::string, std::size_t> type_indices;
    std::unordered_set<std::string> type_names;
    std::unordered_set<std::string> type_owners;
    type_indices.reserve(types.size());
    type_names.reserve(types.size());
    for (std::size_t index = 0; index < types.size(); ++index) {
        const auto& type = types[index];
        if (!valid_uuid(type.type_id.canonical) || !nonzero_uuid(type.type_id.canonical) ||
            !valid_uuid(type.owner_id.canonical) || !nonzero_uuid(type.owner_id.canonical) || type.name.empty()) {
            return Result<MetadataContext>::failure({error("$/Types/" + std::to_string(index),
                "canonical nonzero UUIDs and nonempty name", type.name,
                "Metadata type descriptor is invalid")});
        }
        if (!type_indices.emplace(type.type_id.canonical, index).second) {
            return Result<MetadataContext>::failure({error("$/Types/" + std::to_string(index),
                "unique type UUID", type.type_id.canonical, "Metadata type UUID is duplicated")});
        }
        if (!type_names.insert(type.name).second)
            return Result<MetadataContext>::failure({error("$/Types/" + std::to_string(index),
                "unique generated type name", type.name, "Metadata type name is duplicated")});
        type_owners.insert(type.owner_id.canonical);
    }

    std::unordered_set<std::string> member_names;
    std::unordered_set<std::string> member_keys;
    for (std::size_t index = 0; index < members.size(); ++index) {
        const auto& member = members[index];
        if (!valid_uuid(member.owner_id.canonical) || !type_owners.contains(member.owner_id.canonical)) {
            return Result<MetadataContext>::failure({error("$/Members/" + std::to_string(index),
                "metadata object UUID declared as a type owner", member.owner_id.canonical,
                "Metadata member owner is unknown")});
        }
        if (member.name.empty()) {
            return Result<MetadataContext>::failure({error("$/Members/" + std::to_string(index),
                "nonempty member name", "empty", "Metadata member name is empty")});
        }
        const std::string scoped_name = member_key(member.owner_id, member.name);
        if (!member_names.insert(scoped_name).second) {
            return Result<MetadataContext>::failure({error("$/Members/" + std::to_string(index),
                "unique member name per owner", member.name, "Metadata member name is duplicated")});
        }
        if (const auto* uuid = std::get_if<model::UuidValue>(&member.key)) {
            if (!valid_uuid(uuid->canonical) || !nonzero_uuid(uuid->canonical)) {
                return Result<MetadataContext>::failure({error("$/Members/" + std::to_string(index),
                    "canonical nonzero UUID member key", uuid->canonical, "Metadata member UUID key is invalid")});
            }
        }
        const std::string scoped_key = member_key(member.owner_id, member.key);
        if (!member_keys.insert(scoped_key).second) {
            return Result<MetadataContext>::failure({error("$/Members/" + std::to_string(index),
                "unique member key per owner", key_description(member.key), "Metadata member key is duplicated")});
        }
        if (member.type && member.unresolved_type) {
            return Result<MetadataContext>::failure({error("$/Members/" + std::to_string(index),
                "either resolved or unresolved type", "both supplied", "Metadata member type is ambiguous")});
        }
        if ((member.type && member.type->entries.empty()) ||
            (member.unresolved_type && member.unresolved_type->value.empty()))
            return Result<MetadataContext>::failure({error("$/Members/" + std::to_string(index),
                "nonempty resolved type or unresolved qualified name", "empty", "Metadata member type is empty")});
        if (member.type) {
            for (const auto& entry : member.type->entries) {
                if (entry.type_uuid && (!valid_uuid(entry.type_uuid->canonical) || !nonzero_uuid(entry.type_uuid->canonical)))
                    return Result<MetadataContext>::failure({error("$/Members/" + std::to_string(index),
                        "canonical nonzero type UUID", entry.type_uuid->canonical, "Metadata member type UUID is invalid")});
            }
        }
    }

    std::unordered_map<std::string, std::size_t> member_name_indices;
    std::unordered_map<std::string, std::size_t> member_key_indices;
    member_name_indices.reserve(members.size());
    member_key_indices.reserve(members.size());
    for (std::size_t index = 0; index < members.size(); ++index) {
        member_name_indices.emplace(member_key(members[index].owner_id, members[index].name), index);
        member_key_indices.emplace(member_key(members[index].owner_id, members[index].key), index);
    }
    return Result<MetadataContext>::success(MetadataContext{
        std::move(types), std::move(members), std::move(type_indices), std::move(member_name_indices),
        std::move(member_key_indices)});
}

const MetadataTypeDescriptor* MetadataContext::lookup_type(const model::UuidValue& type_id) const {
    const auto found = type_indices_.find(type_id.canonical);
    return found == type_indices_.end() ? nullptr : &types_[found->second];
}

const MetadataMemberDescriptor* MetadataContext::lookup_member(
    const model::UuidValue& owner_id,
    std::string_view name) const {
    const auto found = member_name_indices_.find(member_key(owner_id, name));
    return found == member_name_indices_.end() ? nullptr : &members_[found->second];
}

const MetadataMemberDescriptor* MetadataContext::lookup_member(
    const model::UuidValue& owner_id,
    const MetadataMemberKey& key) const {
    const auto found = member_key_indices_.find(member_key(owner_id, key));
    return found == member_key_indices_.end() ? nullptr : &members_[found->second];
}

Result<MetadataPath> MetadataContext::resolve_names(
    const model::TypeDomainPatternValue& root,
    std::span<const std::string> names) const {
    return resolve(root, names, {}, true);
}

Result<MetadataPath> MetadataContext::resolve_keys(
    const model::TypeDomainPatternValue& root,
    std::span<const MetadataMemberKey> keys) const {
    return resolve(root, {}, keys, false);
}

Result<MetadataPath> MetadataContext::resolve(
    const model::TypeDomainPatternValue& root,
    std::span<const std::string> names,
    std::span<const MetadataMemberKey> keys,
    bool by_name) const {
    const std::size_t count = by_name ? names.size() : keys.size();
    if (count > max_path_depth)
        return path_failure("$/Path", "path depth within configured limit", std::to_string(count),
            "Metadata path exceeds its bounded depth");
    MetadataPath result;
    if (by_name) result.members.assign(names.begin(), names.end());
    if (!by_name) result.keys.assign(keys.begin(), keys.end());
    if (count == 0) {
        result.leaf_type = root;
        return Result<MetadataPath>::success(std::move(result));
    }
    auto root_type = singular_object_uuid(root);
    if (!root_type)
        return path_failure("$/Path", "one unambiguous object or reference root type", std::to_string(root.entries.size()),
            "Metadata path root is not a singular resolvable object type");
    const auto* root_descriptor = lookup_type(*root_type);
    if (!root_descriptor)
        return path_failure("$/Path", "registered generated TypeId for root object", root_type->canonical,
            "Metadata path root type is not present in this context");
    model::UuidValue owner = root_descriptor->owner_id;
    for (std::size_t index = 0; index < count; ++index) {
        const MetadataMemberDescriptor* member = by_name
            ? lookup_member(owner, names[index]) : lookup_member(owner, keys[index]);
        const std::string path = "$/Path/" + std::to_string(index);
        if (member == nullptr)
            return path_failure(path, "member declared by current owner", by_name ? names[index] : key_description(keys[index]),
                "Metadata member does not resolve in the current owner context");
        if (by_name) result.keys.push_back(member->key);
        else result.members.push_back(member->name);
        if (index + 1 == count) {
            result.leaf_type = member->type;
            result.unresolved_type = member->unresolved_type;
            return Result<MetadataPath>::success(std::move(result));
        }
        if (!member->type)
            return path_failure(path, "resolved object or reference type for intermediate member",
                member->unresolved_type ? member->unresolved_type->value : "untyped",
                "Metadata path cannot continue through an unresolved or untyped member");
        auto next_type = singular_object_uuid(*member->type);
        if (!next_type)
            return path_failure(path, "one object or reference UUID for intermediate member type",
                std::to_string(member->type->entries.size()),
                "Metadata path intermediate type is unknown or ambiguous");
        const auto* next_descriptor = lookup_type(*next_type);
        if (!next_descriptor)
            return path_failure(path, "registered generated TypeId for intermediate object", next_type->canonical,
                "Metadata path intermediate type is not present in this context");
        owner = next_descriptor->owner_id;
    }
    return path_failure("$/Path", "resolvable metadata path", "unreachable", "Metadata path resolution failed");
}

}  // namespace oof
