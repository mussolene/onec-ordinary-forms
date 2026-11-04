#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "oof/model/ordinary_form.hpp"
#include "oof/result.hpp"

namespace oof {

struct MetadataTypeDescriptor {
    model::UuidValue type_id;
    model::UuidValue owner_id;
    std::string name;

    friend bool operator==(const MetadataTypeDescriptor&, const MetadataTypeDescriptor&) = default;
};

using MetadataMemberKey = std::variant<model::UuidValue, std::int64_t>;

struct MetadataMemberDescriptor {
    model::UuidValue owner_id;
    std::string name;
    MetadataMemberKey key;
    std::optional<model::TypeDomainPatternValue> type;
    std::optional<model::QualifiedName> unresolved_type;

    friend bool operator==(const MetadataMemberDescriptor&, const MetadataMemberDescriptor&) = default;
};

struct MetadataPath {
    std::vector<std::string> members;
    std::vector<MetadataMemberKey> keys;
    std::optional<model::TypeDomainPatternValue> leaf_type;
    std::optional<model::QualifiedName> unresolved_type;

    friend bool operator==(const MetadataPath&, const MetadataPath&) = default;
};

class MetadataContext {
public:
    static constexpr std::size_t max_types = 100000;
    static constexpr std::size_t max_members = 1000000;
    static constexpr std::size_t max_path_depth = 128;

    [[nodiscard]] static Result<MetadataContext> create(
        std::vector<MetadataTypeDescriptor> types,
        std::vector<MetadataMemberDescriptor> members);

    [[nodiscard]] const MetadataTypeDescriptor* lookup_type(const model::UuidValue& type_id) const;
    [[nodiscard]] const MetadataMemberDescriptor* lookup_member(
        const model::UuidValue& owner_id,
        std::string_view name) const;
    [[nodiscard]] const MetadataMemberDescriptor* lookup_member(
        const model::UuidValue& owner_id,
        const MetadataMemberKey& key) const;

    [[nodiscard]] Result<MetadataPath> resolve_names(
        const model::TypeDomainPatternValue& root,
        std::span<const std::string> members) const;
    [[nodiscard]] Result<MetadataPath> resolve_keys(
        const model::TypeDomainPatternValue& root,
        std::span<const MetadataMemberKey> keys) const;

    [[nodiscard]] std::span<const MetadataTypeDescriptor> types() const noexcept { return types_; }
    [[nodiscard]] std::span<const MetadataMemberDescriptor> members() const noexcept { return members_; }

private:
    MetadataContext(
        std::vector<MetadataTypeDescriptor> types,
        std::vector<MetadataMemberDescriptor> members,
        std::unordered_map<std::string, std::size_t> type_indices,
        std::unordered_map<std::string, std::size_t> member_name_indices,
        std::unordered_map<std::string, std::size_t> member_key_indices)
        : types_(std::move(types)), members_(std::move(members)),
          type_indices_(std::move(type_indices)), member_name_indices_(std::move(member_name_indices)),
          member_key_indices_(std::move(member_key_indices)) {}

    [[nodiscard]] Result<MetadataPath> resolve(
        const model::TypeDomainPatternValue& root,
        std::span<const std::string> names,
        std::span<const MetadataMemberKey> keys,
        bool by_name) const;

    std::vector<MetadataTypeDescriptor> types_;
    std::vector<MetadataMemberDescriptor> members_;
    std::unordered_map<std::string, std::size_t> type_indices_;
    std::unordered_map<std::string, std::size_t> member_name_indices_;
    std::unordered_map<std::string, std::size_t> member_key_indices_;
};

}  // namespace oof
