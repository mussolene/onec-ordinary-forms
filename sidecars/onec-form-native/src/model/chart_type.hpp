#pragma once

#include <array>
#include <string_view>
#include <utility>

namespace oof::model::detail {

inline constexpr std::array chart_type_members{
    std::pair{std::string_view{"Line"}, 0}, std::pair{std::string_view{"Step"}, 1},
    std::pair{std::string_view{"Area"}, 2}, std::pair{std::string_view{"StackedArea"}, 3},
    std::pair{std::string_view{"Column"}, 4}, std::pair{std::string_view{"StackedColumn"}, 5},
    std::pair{std::string_view{"Column3D"}, 6}, std::pair{std::string_view{"StackedColumn3D"}, 7},
    std::pair{std::string_view{"Bar"}, 8}, std::pair{std::string_view{"StackedBar"}, 9},
    std::pair{std::string_view{"Bar3D"}, 10}, std::pair{std::string_view{"StackedBar3D"}, 11},
    std::pair{std::string_view{"Pie"}, 12}, std::pair{std::string_view{"Pie3D"}, 13},
    std::pair{std::string_view{"Stock"}, 14}, std::pair{std::string_view{"OpenHighLowClose"}, 15},
    std::pair{std::string_view{"BarGraph"}, 16}, std::pair{std::string_view{"CeilGraph"}, 17},
    std::pair{std::string_view{"TapeGraph"}, 18}, std::pair{std::string_view{"PyramidGraph"}, 19},
    std::pair{std::string_view{"Waterfall"}, 20}, std::pair{std::string_view{"WireframeSurface"}, 21},
    std::pair{std::string_view{"Surface"}, 22}, std::pair{std::string_view{"ConvexSurface"}, 23},
    std::pair{std::string_view{"ConcaveSurface"}, 24}, std::pair{std::string_view{"ShadedSurface"}, 25},
    std::pair{std::string_view{"StackedLine"}, 26}, std::pair{std::string_view{"NormalizedArea"}, 27},
    std::pair{std::string_view{"NormalizedColumn"}, 28}, std::pair{std::string_view{"NormalizedColumn3D"}, 29},
    std::pair{std::string_view{"NormalizedBar"}, 30}, std::pair{std::string_view{"NormalizedBar3D"}, 31},
    std::pair{std::string_view{"Honeycomb"}, 32}, std::pair{std::string_view{"RadarLine"}, 33},
    std::pair{std::string_view{"RadarArea"}, 34}, std::pair{std::string_view{"RadarStackedLine"}, 35},
    std::pair{std::string_view{"RadarStackedArea"}, 36}, std::pair{std::string_view{"RadarNormalizedArea"}, 37},
    std::pair{std::string_view{"Gauge"}, 38}, std::pair{std::string_view{"Funnel"}, 39},
    std::pair{std::string_view{"Funnel3D"}, 40}, std::pair{std::string_view{"NormalizedFunnel"}, 41},
    std::pair{std::string_view{"NormalizedFunnel3D"}, 42}, std::pair{std::string_view{"Scatter"}, 43},
    std::pair{std::string_view{"Bubble"}, 44}, std::pair{std::string_view{"Donut"}, 45},
    std::pair{std::string_view{"Donut3D"}, 46},
};

}  // namespace oof::model::detail
