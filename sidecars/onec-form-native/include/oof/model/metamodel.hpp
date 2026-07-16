#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "oof/model/ordinary_form.hpp"

namespace oof::model::metamodel {

enum class VersionMask : std::uint32_t {
    none = 0,
    platform_8_2 = 1U << 0,
    platform_8_3 = 1U << 1,
    platform_8_5 = 1U << 2,
    all_supported = (1U << 0) | (1U << 1) | (1U << 2),
};

[[nodiscard]] constexpr VersionMask operator|(VersionMask lhs, VersionMask rhs) noexcept {
    return static_cast<VersionMask>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool includes(VersionMask mask, VersionMask version) noexcept {
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(version)) ==
           static_cast<std::uint32_t>(version);
}

enum class ClassificationStatus : std::uint8_t {
    platform_resource_backed,
    corpus_xsd_resource_correlated,
    corpus_xsd_correlated,
    platform_ui_guid_table_backed,
    platform_resource_backed_windows_oracle_pending,
    binary_guid_corpus_xsd_correlated,
    windows_harness_required,
};

enum class ChildPolicy : std::uint8_t {
    forbidden,
    ordered_controls,
};

struct ControlDescriptor {
    ControlKind kind = ControlKind::panel;
    std::string_view guid;
    std::string_view storage_tag;
    std::string_view public_name;
    std::string_view api_name;
    std::u8string_view russian_name;
    VersionMask version_mask = VersionMask::none;
    ClassificationStatus classification = ClassificationStatus::corpus_xsd_correlated;
    ChildPolicy child_policy = ChildPolicy::forbidden;
};

[[nodiscard]] std::span<const ControlDescriptor> control_descriptors() noexcept;
[[nodiscard]] const ControlDescriptor& descriptor_for(ControlKind kind);
[[nodiscard]] const ControlDescriptor* find_by_guid(std::string_view guid) noexcept;
[[nodiscard]] const ControlDescriptor* find_by_public_name(std::string_view name) noexcept;
[[nodiscard]] const ControlDescriptor* find_by_api_name(std::string_view name) noexcept;
[[nodiscard]] const ControlDescriptor* find_by_russian_name(std::u8string_view name) noexcept;
[[nodiscard]] std::string_view classification_name(ClassificationStatus status) noexcept;

}  // namespace oof::model::metamodel
