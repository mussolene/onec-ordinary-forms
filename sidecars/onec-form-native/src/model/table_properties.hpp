#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace oof::model::detail {

struct TableEnumSpec {
    std::string_view property;
    std::string_view type_name;
    std::span<const std::string_view> members;
    std::size_t slot;
    int default_ordinal;
    std::string_view default_qualified;
};

inline constexpr std::array<std::string_view, 3> table_scroll_bar_use{"DontUse", "UseAlways", "AutoUse"};
inline constexpr std::array<std::string_view, 4> table_row_input_mode{
    "EndOfList", "EndOfWindow", "AfterCurrentRow", "BeforeCurrentRow"};
inline constexpr std::array<std::string_view, 2> table_selection_mode{"SingleLine", "MultiLine"};
inline constexpr std::array<std::string_view, 3> table_initial_list_view{"Beginning", "End", "Auto"};
inline constexpr std::array<std::string_view, 3> table_initial_tree_view{"NoExpand", "ExpandTopLevel", "ExpandAllLevels"};

inline constexpr std::array<TableEnumSpec, 6> table_enum_specs{{
    {"HorizontalScrollBar", "ScrollBarUse", table_scroll_bar_use, 9, 2, "ScrollBarUse.AutoUse"},
    {"VerticalScrollBar", "ScrollBarUse", table_scroll_bar_use, 10, 2, "ScrollBarUse.AutoUse"},
    {"RowInputMode", "TableBoxRowInputMode", table_row_input_mode, 13, 0, "TableBoxRowInputMode.EndOfList"},
    {"SelectionMode", "TableBoxSelectionMode", table_selection_mode, 15, 0, "TableBoxSelectionMode.SingleLine"},
    {"InitialListView", "InitialListView", table_initial_list_view, 20, 0, "InitialListView.Beginning"},
    {"InitialTreeView", "InitialTreeView", table_initial_tree_view, 21, 0, "InitialTreeView.NoExpand"},
}};

}  // namespace oof::model::detail
