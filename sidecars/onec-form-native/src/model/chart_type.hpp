#pragma once

#include <array>
#include <cstddef>
#include <span>
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

struct ChartEnumSpec {
    std::string_view property;
    std::string_view type_name;
    std::span<const std::string_view> members;
    std::size_t offset;
    int default_ordinal;
    std::string_view default_qualified;
};

inline constexpr std::array<std::string_view, 4> chart_auto_series_separation{
    "None", "All", "Maximum", "Minimum"};
inline constexpr std::array<std::string_view, 23> chart_label_type{
    "None", "Series", "Value", "Percent", "SeriesPercent", "SeriesValue", "SeriesValuePercent",
    "ValuePercent", "Point", "PointPercent", "PointValue", "PointValuePercent", "SeriesPoint",
    "SeriesPointPercent", "SeriesPointValue", "SeriesPointValuePercent", "ValueSize", "SeriesSize",
    "SeriesValueSize", "SeriesPointSize", "SeriesPointValueSize", "PointSize", "PointValueSize"};
inline constexpr std::array<std::string_view, 3> chart_max_series{"NotDefined", "Limited", "Percent"};
inline constexpr std::array<std::string_view, 2> chart_orientation{"SouthWest", "SouthEast"};
inline constexpr std::array<std::string_view, 2> chart_gauge_value_representation{"Needle", "Sector"};
inline constexpr std::array<std::string_view, 11> chart_label_location{
    "Edge", "Center", "EdgeInside", "EdgeAuto", "Auto", "TopLeft", "BottomLeft",
    "TopRight", "BottomRight", "TopAndLeftSpecified", "EmptySpace"};
inline constexpr std::array<std::string_view, 2> chart_gauge_labels_location{"AtScale", "InsideScale"};
inline constexpr std::array<std::string_view, 3> chart_space_mode{"None", "Half", "Full"};

inline constexpr std::array<ChartEnumSpec, 8> chart_enum_specs{{
    {"AutoSeriesSeparation", "AutoSeriesSeparation", chart_auto_series_separation, 9, 0, "AutoSeriesSeparation.None"},
    {"LabelType", "ChartLabelType", chart_label_type, 3, 0, "ChartLabelType.None"},
    {"MaxSeries", "MaxSeries", chart_max_series, 45, 0, "MaxSeries.NotDefined"},
    {"Orientation", "ChartOrientation", chart_orientation, 10, 0, "ChartOrientation.SouthWest"},
    {"GaugeChartValueRepresentation", "GaugeChartValueRepresentation", chart_gauge_value_representation, 68, 0, "GaugeChartValueRepresentation.Needle"},
    {"LabelLocation", "ChartLabelLocation", chart_label_location, 5, 4, "ChartLabelLocation.Auto"},
    {"GaugeChartValuesScaleLabelsLocation", "GaugeChartValuesScaleLabelsLocation", chart_gauge_labels_location, 73, 1, "GaugeChartValuesScaleLabelsLocation.InsideScale"},
    {"SpaceMode", "ChartSpaceMode", chart_space_mode, 48, 1, "ChartSpaceMode.Half"},
}};

}  // namespace oof::model::detail
