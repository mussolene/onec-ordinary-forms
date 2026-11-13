#include "../src/model/chart_type.hpp"
#include "../src/model/table_properties.hpp"
#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "oof/model/metamodel.hpp"
#include "oof/source/form_xml.hpp"
#include "oof/source/metadata_xml.hpp"
#include "oof/storage/form_stream.hpp"
#include "oof/storage/list_stream.hpp"

namespace {

namespace model = oof::model;
namespace source = oof::source;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void test_owned_chart_xml_contract() {
    const std::string chart = R"XML(<Chart><TitleArea><Text>Owned title</Text></TitleArea><Series><ChartSeries id="2"><Text>Series</Text><Color kind="automatic"/><Marker type="ChartMarkerType" member="Auto"/></ChartSeries></Series><Points><ChartPoint id="1"><Text>Point</Text><Color kind="automatic"/></ChartPoint></Points><Values><ChartValue seriesRef="2" pointRef="1"><Number>7.5</Number></ChartValue></Values></Chart>)XML";
    const std::string prefix = R"XML(<Form id="1" name="OwnedCharts" ordinaryFormVersion="2.1"><ChildItems>)XML";
    const std::string suffix = "</ChildItems></Form>";
    const std::string xml = prefix + "<GanttChart id=\"2\" name=\"Gantt\"><Position/>" + chart +
        "</GanttChart><Dendrogram id=\"3\" name=\"Tree\"><Position/>" + chart + "</Dendrogram>" + suffix;
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.ok() ? "owned Chart values must parse" : parsed.diagnostics().front().message);
    const auto& gantt = std::get<model::GanttChartPayload>(parsed.value().find_control(model::ObjectId{2})->payload);
    const auto& tree = std::get<model::DendrogramPayload>(parsed.value().find_control(model::ObjectId{3})->payload);
    expect(gantt.chart && tree.chart && gantt.chart->series.size() == 1 && tree.chart->points.size() == 1 &&
        gantt.chart->values.front().value == model::PropertyValue{model::DecimalValue{"7.5"}},
        "both owners must retain the typed Chart aggregate and its local collection identities");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), serialized.ok() ? "owned Chart values must serialize" : serialized.diagnostics().front().message);
    auto restored = source::parse_form_xml(serialized.value());
    expect(restored.ok(), restored.ok() ? "owned Chart values must reparse" : restored.diagnostics().front().message);
    auto repeated = source::serialize_form_xml(restored.value());
    expect(repeated.ok() && repeated.value() == serialized.value(), "owned Chart XML must be canonical and stable");

    for (const auto& content : {
        std::string{R"XML(<Chart id="9"><Series/><Points/><Values/></Chart>)XML"},
        std::string{R"XML(<Chart name="Nested"><Series/><Points/><Values/></Chart>)XML"},
        std::string{"<Chart><Position/><Series/><Points/><Values/></Chart>"},
        std::string{"<Chart><Series/><Points/><Values/><Events/></Chart>"},
        std::string{R"XML(<Chart><BackColor kind="automatic"/><Series/><Points/><Values/></Chart>)XML"},
        chart + chart}) {
        expect(!source::parse_form_xml(prefix + "<GanttChart id=\"2\" name=\"Gantt\"><Position/>" + content +
            "</GanttChart>" + suffix), "owned Chart must reject identity, geometry, events, BackColor aliases, and duplicates");
    }
    auto legacy = source::parse_form_xml(prefix + "<GanttChart id=\"2\" name=\"Gantt\"><Position/></GanttChart>" + suffix);
    expect(legacy.ok() && !std::get<model::GanttChartPayload>(legacy.value().find_control(model::ObjectId{2})->payload).chart,
        "an omitted owned Chart must retain the constructor-default contract");
}

void test_value_list_xml_contract() {
    const std::string first = R"XML(<Item id="0"><Value><String>Первый</String></Value><Presentation><String>Строка</String></Presentation><Check>true</Check></Item>)XML";
    const std::string second = R"XML(<Item id="1"><Value><Number>42.5</Number></Value><Presentation><String>Число</String></Presentation></Item>)XML";
    const std::string third = R"XML(<Item id="2"><Value><Boolean>true</Boolean></Value><Presentation><String>Булево</String></Presentation></Item>)XML";
    const std::string numeric_type = R"XML(<ValueType><Entry term="numeric" length="12" precision="3"/></ValueType>)XML";
    const std::array<std::string, 12> values{
        "<ValueList/>",
        "<ValueList><Items>" + first + second + third + "</Items></ValueList>",
        "<ValueList><Items>" + second + third + "</Items></ValueList>",
        R"XML(<ValueList lastAssignedId="2"/>)XML",
        std::string(R"XML(<ValueList><Items><Item id="3"><Value><String>Новый</String></Value><Presentation><String>Вставленный</String></Presentation><Check>true</Check></Item>)XML") + second + third + "</Items></ValueList>",
        "<ValueList><Items>" + third + first + second + "</Items></ValueList>",
        "<ValueList>" + numeric_type + R"XML(<Items><Item id="0"><Value><Number>-1.25</Number></Value><Check>true</Check></Item><Item id="1"><Value><Number>20.5</Number></Value></Item></Items></ValueList>)XML",
        "<ValueList>" + numeric_type + "<AvailableValues><ValueList>" + numeric_type + R"XML(<Items><Item id="0"><Value><Number>10</Number></Value></Item><Item id="1"><Value><Number>20</Number></Value></Item></Items></ValueList></AvailableValues><Items><Item id="0"><Value><Number>10</Number></Value></Item><Item id="1"><Value><Number>20</Number></Value></Item></Items></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="0"><Value><String>Строка</String></Value><Presentation><FormattedString formatted="true"><Value><String>До картинки &lt;img 0:448d6f55-d885-496c-870d-d1bd78374745/&gt; после картинки</String></Value></FormattedString></Presentation><Check>true</Check></Item></Items></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="0"><Value><String>Картинка</String></Value><Presentation><String>Библиотечная картинка</String></Presentation><Check>true</Check><Picture standardName="PictureLib.CloneListItem"/></Item></Items></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="0"><Value><Array><Item><String>Вложено</String></Item><Item><Number>17.25</Number></Item></Array></Value><Presentation><String>Измененное представление</String></Presentation><Check>true</Check></Item><Item id="1"><Value><Structure><Field name="Caption"><String>Заголовок</String></Field><Field name="Items"><Array><Item><String>Вложено</String></Item></Array></Field></Structure></Value><Check>true</Check></Item></Items></ValueList>)XML",
        R"XML(<ValueList><ValueType><Entry term="string" length="30"/></ValueType><AvailableValues><ValueList/></AvailableValues></ValueList>)XML"
    };
    const std::array<std::vector<std::uint64_t>, 12> ids{
        std::vector<std::uint64_t>{}, {0, 1, 2}, {1, 2}, {}, {3, 1, 2}, {2, 0, 1}, {0, 1}, {0, 1}, {0}, {0}, {0, 1}, {}};
    const std::array<std::int64_t, 12> history{-1, 2, 2, 2, 3, 2, 1, 1, 0, 0, 1, -1};
    std::string xml = R"XML(<Form id="1" name="ValueLists" ordinaryFormVersion="2.1"><Attributes>)XML";
    for (std::size_t index = 0; index < values.size(); ++index)
        xml += "<Attribute id=\"" + std::to_string(100 + index) + "\" name=\"Numeric" + std::to_string(index) +
            "\"><TypeDomain><Entry term=\"numeric\" length=\"12\" precision=\"3\"/></TypeDomain></Attribute>";
    xml += "</Attributes><ChildItems>";
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto& value = values[index];
        xml += "<InputField id=\"" + std::to_string(200 + index) + "\" name=\"Input" + std::to_string(index) +
            "\"><DataPath attributeId=\"" + std::to_string(100 + index) + "\"/><Position/><MaxValue>" + value +
            "</MaxValue><MinValue>" + value + "</MinValue></InputField>";
        xml += "<Chart id=\"" + std::to_string(300 + index) + "\" name=\"Chart" + std::to_string(index) +
            "\"><Position/><Series><ChartSeries id=\"2\"><Text/><Color kind=\"automatic\"/><Marker type=\"ChartMarkerType\" member=\"Auto\"/>"
            "<Value>" + value + "</Value><Details>" + value + "</Details></ChartSeries></Series><Points><ChartPoint id=\"1\"><Text/><Color kind=\"automatic\"/><Value>" + value +
            "</Value></ChartPoint></Points><Values><ChartValue seriesRef=\"2\" pointRef=\"1\">" + value + "<Details>" + value +
            "</Details></ChartValue></Values></Chart>";
    }
    xml += "</ChildItems></Form>";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "12 native ValueList variants must parse on 12 numeric fields and 12 Charts");
    const auto inspect = [&](const model::OrdinaryFormDocument& document) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto* input = document.find_control(model::ObjectId{200 + index});
            const auto* chart = document.find_control(model::ObjectId{300 + index});
            expect(input && chart, "ValueList owners must remain named controls");
            const auto* minimum = input->properties().find(model::PropertyId::from_name("MinValue"));
            const auto* maximum = input->properties().find(model::PropertyId::from_name("MaxValue"));
            expect(minimum && maximum && minimum->value == maximum->value, "ValueList must roundtrip through both bounds");
            const auto& list = std::get<model::ValueListValue>(minimum->value);
            expect(list.items.size() == ids[index].size() && list.effective_last_assigned_id() == history[index],
                "GetID history and cardinality must survive named XML independently of item position");
            for (std::size_t position = 0; position < list.items.size(); ++position)
                expect(list.items[position].id == ids[index][position], "ValueList Item id must preserve actual GetID order");
            const auto& payload = std::get<model::ChartPayload>(chart->payload);
            expect(payload.series.front().value == minimum->value && payload.points.front().value == minimum->value &&
                payload.values.front().value == minimum->value && payload.values.front().details == minimum->value,
                "all direct Chart typed-value surfaces must preserve the ValueList graph");
            const auto* details = payload.series.front().properties.find(model::PropertyId::from_name("Details"));
            expect(details && details->value == minimum->value, "ChartSeries Details must preserve ValueList through the shared codec");
        }
    };
    inspect(parsed.value());
    const auto written = source::serialize_form_xml(parsed.value());
    expect(written.ok(), "ValueList named XML must serialize with existing typed, formatted and picture codecs");
    const auto restored = source::parse_form_xml(written.value());
    expect(restored.ok(), "canonical ValueList XML must parse again");
    inspect(restored.value());
    const auto canonical = source::serialize_form_xml(restored.value());
    expect(canonical.ok() && canonical.value() == written.value(), "ValueList canonical XML must be stable");
    const auto& no_available = std::get<model::ValueListValue>(parsed.value().find_control(model::ObjectId{200})->properties().find(model::PropertyId::from_name("MinValue"))->value);
    const auto& empty_available = std::get<model::ValueListValue>(parsed.value().find_control(model::ObjectId{211})->properties().find(model::PropertyId::from_name("MinValue"))->value);
    expect(no_available.available_values.empty() && empty_available.available_values.size() == 1 &&
        empty_available.available_values.front().items.empty(), "absent AvailableValues and an explicit empty ValueList must remain distinct");
    const auto& nested = std::get<model::ValueListValue>(parsed.value().find_control(model::ObjectId{210})->properties().find(model::PropertyId::from_name("MinValue"))->value);
    auto copied = nested;
    std::get<model::ArrayValue>(copied.items.front().value).items.front() = std::string{"Changed"};
    expect(std::get<std::string>(std::get<model::ArrayValue>(nested.items.front().value).items.front()) == "Вложено",
        "copying a ValueList must not share mutable nested item values");
    const auto& formatted_list = std::get<model::ValueListValue>(parsed.value().find_control(model::ObjectId{208})->properties().find(model::PropertyId::from_name("MinValue"))->value);
    const auto& formatted = std::get<model::FormattedStringValue>(formatted_list.items.front().presentation);
    expect(formatted.formatted && std::get<std::string>(formatted.value).find("<img 0:448d6f55") != std::string::npos &&
        written.value().find("&lt;img 0:448d6f55") != std::string::npos,
        "formatted presentation keeps plain runtime markup as escaped XML text without invented languages");
    const auto wrap = [](std::string_view value) {
        return std::string(R"XML(<Form id="1" name="List" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><Series><ChartSeries id="2"><Text/><Color kind="automatic"/><Marker type="ChartMarkerType" member="Auto"/><Value>)XML") +
            std::string(value) + "</Value></ChartSeries></Series><Points/><Values/></Chart></ChildItems></Form>";
    };
    const auto implicit = source::parse_form_xml(wrap(values[1]));
    const auto explicit_default = source::parse_form_xml(wrap("<ValueList lastAssignedId=\"2\"><Items>" + first + second + third + "</Items></ValueList>"));
    expect(implicit.ok() && explicit_default.ok(), "explicit default history must remain admissible");
    const auto implicit_xml = source::serialize_form_xml(implicit.value());
    const auto explicit_xml = source::serialize_form_xml(explicit_default.value());
    expect(implicit_xml.ok() && explicit_xml.ok() && implicit_xml.value() == explicit_xml.value(),
        "default lastAssignedId must be omitted while nondefault cleared history is preserved");
    for (const auto invalid : {
        R"XML(<ValueList lastAssignedId="-1"/>)XML",
        R"XML(<ValueList lastAssignedId="9223372036854775808"/>)XML",
        R"XML(<ValueList><Items><Item><Value><String>x</String></Value></Item></Items></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="0"><Value><String>x</String></Value></Item><Item id="0"><Value><Number>1</Number></Value></Item></Items></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="9223372036854775808"><Value><String>x</String></Value></Item></Items></ValueList>)XML",
        R"XML(<ValueList lastAssignedId="1"><Items><Item id="2"><Value><String>x</String></Value></Item></Items></ValueList>)XML",
        R"XML(<ValueList><AvailableValues><ValueList/><ValueList/></AvailableValues></ValueList>)XML",
        R"XML(<ValueList><Items/><ValueType/></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="0"><Presentation><String>x</String></Presentation></Item></Items></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="0"><Value><Number>1</Number></Value><Presentation><Number>2</Number></Presentation></Item></Items></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="0"><Value><String>x</String></Value><Presentation><FormattedString formatted="false"><Value><String>x</String></Value></FormattedString></Presentation></Item></Items></ValueList>)XML",
        R"XML(<ValueList><Items><Item id="0"><Value><String>x</String></Value><Picture>7</Picture></Item></Items></ValueList>)XML",
        R"XML(<FormattedString formatted="true"><Value><Item language="ru">x</Item></Value></FormattedString>)XML"})
        expect(!source::parse_form_xml(wrap(invalid)).ok(), "ValueList must reject invalid ID history, cardinality, presentations and unsupported file pictures");
    const auto plain_empty = source::parse_form_xml(wrap(R"XML(<FormattedString formatted="true"><Value><String/></Value></FormattedString>)XML"));
    expect(plain_empty.ok(), "empty plain FormattedString must remain distinct from empty localized content");
    std::string available = "<ValueList/>";
    for (std::size_t depth = 0; depth < model::max_typed_value_depth; ++depth)
        available = "<ValueList><AvailableValues>" + available + "</AvailableValues></ValueList>";
    expect(source::parse_form_xml(wrap(available)).ok(), "AvailableValues recursion must admit a leaf at depth64");
    const auto too_deep = source::parse_form_xml(wrap("<ValueList><AvailableValues>" + available + "</AvailableValues></ValueList>"));
    expect(!too_deep.ok(), "AvailableValues must not bypass the shared typed-value depth limit");
    model::ValueListValue one;
    one.items.push_back({0, model::PropertyValue{std::string{"value"}}, model::PropertyValue{std::string{}}, false, std::nullopt});
    model::TypedValueBudget budget{model::max_typed_value_nodes - 4};
    expect(model::validate_typed_value(model::PropertyValue{one}, budget) && budget.nodes == model::max_typed_value_nodes,
        "ValueList root, item, value and omitted presentation must all consume the shared node budget");
}

void test_recursive_typed_value_xml_contract() {
    const std::array<std::string_view, 12> values{
        R"XML(<Color kind="absolute" red="23" green="67" blue="101"/>)XML",
        R"XML(<Color kind="styleReference" styleName="WebColors.Red"/>)XML",
        R"XML(<Font kind="absolute" faceName="Arial" height="13" bold="true" italic="true" scale="120"/>)XML",
        R"XML(<Border kind="absolute" borderType="Double" width="2"/>)XML",
        R"XML(<Array><Item><String>Вложенный текст</String></Item><Item><Number>-12.75</Number></Item><Item><Null/></Item></Array>)XML",
        R"XML(<FixedArray><Item><Array><Item><Boolean>true</Boolean></Item></Array></Item></FixedArray>)XML",
        R"XML(<Structure><Field name="Заголовок"><String>Значение</String></Field><Field name="Nested"><Array><Item><Undefined>undefined</Undefined></Item></Array></Field></Structure>)XML",
        R"XML(<FixedStructure><Field name="Nested"><Structure><Field name="Дата"><Date>2024-01-02T03:04:05</Date></Field></Structure></Field></FixedStructure>)XML",
        R"XML(<Map><Entry><Key><Number>17</Number></Key><Value><Array><Item><String>Вложено</String></Item></Array></Value></Entry><Entry><Key><Boolean>false</Boolean></Key><Value><Number>123.45</Number></Value></Entry></Map>)XML",
        R"XML(<FixedMap><Entry><Key><String>Ключ</String></Key><Value><FixedArray><Item><Array/></Item></FixedArray></Value></Entry></FixedMap>)XML",
        R"XML(<TypeDescription><Entry term="numeric" length="12" precision="3"/><Entry term="string" length="40"/></TypeDescription>)XML",
        R"XML(<Array><Item><FixedStructure><Field name="Описание"><TypeDescription><Entry term="boolean"/></TypeDescription></Field><Field name="Карта"><Map><Entry><Key><UUID>00112233-4455-6677-8899-aabbccddeeff</UUID></Key><Value><Type>String</Type></Value></Entry></Map></Field></FixedStructure></Item></Array>)XML"
    };
    std::string xml = R"XML(<Form id="1" name="TypedValues" ordinaryFormVersion="2.1"><Attributes>)XML";
    for (std::size_t index = 0; index < values.size(); ++index)
        xml += "<Attribute id=\"" + std::to_string(100 + index) + "\" name=\"Numeric" + std::to_string(index) +
            "\"><TypeDomain><Entry term=\"numeric\" length=\"12\" precision=\"3\"/></TypeDomain></Attribute>";
    xml += "</Attributes><ChildItems>";
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::string value(values[index]);
        xml += "<InputField id=\"" + std::to_string(200 + index) + "\" name=\"Input" + std::to_string(index) +
            "\"><DataPath attributeId=\"" + std::to_string(100 + index) + "\"/><Position/><MaxValue>" + value +
            "</MaxValue><MinValue>" + value + "</MinValue></InputField>";
        xml += "<Chart id=\"" + std::to_string(300 + index) + "\" name=\"Chart" + std::to_string(index) +
            "\"><Position/><SummarySeries><Value>" + value + "</Value></SummarySeries><Series><ChartSeries id=\"2\"><Text/>"
            "<Color kind=\"automatic\"/><Marker type=\"ChartMarkerType\" member=\"Auto\"/><Value>" + value +
            "</Value><Trendlines><Trendline><Details>" + value + "</Details></Trendline></Trendlines><Details>" + value +
            "</Details></ChartSeries></Series><Points><ChartPoint id=\"1\"><Text/><Color kind=\"automatic\"/><Value>" +
            value + "</Value></ChartPoint></Points><Values><ChartValue seriesRef=\"2\" pointRef=\"1\">" + value +
            "<Details>" + value + "</Details></ChartValue></Values></Chart>";
    }
    xml += "</ChildItems></Form>";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "12 numeric fields and 12 Charts must share the recursive typed-value XML contract");
    const auto inspect = [&](const model::OrdinaryFormDocument& document) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto* input = document.find_control(model::ObjectId{200 + index});
            const auto* chart = document.find_control(model::ObjectId{300 + index});
            expect(input && chart, "typed-value fixtures must retain both control families");
            const auto* minimum = input->properties().find(model::PropertyId::from_name("MinValue"));
            const auto* maximum = input->properties().find(model::PropertyId::from_name("MaxValue"));
            expect(minimum && maximum && minimum->value == maximum->value, "both bounds must preserve the same named value");
            const auto& payload = std::get<model::ChartPayload>(chart->payload);
            expect(payload.series.size() == 1 && payload.points.size() == 1 && payload.values.size() == 1,
                "typed Chart fixture must retain its declared matrix");
            const auto& expected = minimum->value;
            expect(payload.summary_series.value == expected && payload.series.front().value == expected &&
                payload.points.front().value == expected && payload.values.front().value == expected &&
                payload.values.front().details == expected, "all fixed Chart value surfaces must use the same recursive value graph");
            const auto* details = payload.series.front().properties.find(model::PropertyId::from_name("Details"));
            const auto& trends = payload.series.front().trendlines;
            expect(details && details->value == expected && trends.size() == 1, "series Details and Trendlines must preserve the shared value");
            const auto* trend_details = trends.front().properties.find(model::PropertyId::from_name("Details"));
            expect(trend_details && trend_details->value == expected, "trendline Details must use the recursive value parser");
        }
    };
    inspect(parsed.value());
    const auto written = source::serialize_form_xml(parsed.value());
    expect(written.ok(), "recursive named values must serialize through existing Color, Font, Border and TypeDomain writers");
    const auto reparsed = source::parse_form_xml(written.value());
    expect(reparsed.ok(), "recursive typed XML must parse after canonical serialization");
    inspect(reparsed.value());
    const auto rewritten = source::serialize_form_xml(reparsed.value());
    expect(rewritten.ok() && rewritten.value() == written.value(), "typed graphs must preserve each fixed or mutable node kind in canonical XML");
    const auto& fixed = std::get<model::ArrayValue>(parsed.value().find_control(model::ObjectId{205})->properties().find(
        model::PropertyId::from_name("MinValue"))->value);
    expect(fixed.fixed && !std::get<model::ArrayValue>(fixed.items.front()).fixed,
        "FixedArray XML must preserve an explicitly mutable descendant rather than emulate a platform constructor");
    const auto chart_xml = [](std::string_view value) {
        return std::string(R"XML(<Form id="1" name="InvalidValue" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><Series><ChartSeries id="2"><Text/><Color kind="automatic"/><Marker type="ChartMarkerType" member="Auto"/><Value>)XML") +
            std::string(value) + "</Value></ChartSeries></Series><Points/><Values/></Chart></ChildItems></Form>";
    };
    for (const auto invalid : {
        R"XML(<Array><Item/></Array>)XML",
        R"XML(<Array><Item><String>a</String><Number>1</Number></Item></Array>)XML",
        R"XML(<Structure><Field><String>x</String></Field></Structure>)XML",
        R"XML(<Structure><Field name=""><String>x</String></Field></Structure>)XML",
        R"XML(<Structure><Field name="Same"><String>x</String></Field><Field name="Same"><String>y</String></Field></Structure>)XML",
        R"XML(<Map><Entry><Key><String>x</String></Key></Entry></Map>)XML",
        R"XML(<Map><Entry><Value><String>x</String></Value><Key><Number>1</Number></Key></Entry></Map>)XML",
        R"XML(<Map><Entry><Key><String>x</String></Key><Key><String>y</String></Key><Value><Number>1</Number></Value></Entry></Map>)XML",
        R"XML(<Map><Entry><Key><String>x</String></Key><Value><Number>1</Number><Number>2</Number></Value></Entry></Map>)XML",
        R"XML(<Array><Item><ControlRef>2</ControlRef></Item></Array>)XML",
        R"XML(<ValueList><Items><Item id="0"/></Items></ValueList>)XML"}) {
        expect(!source::parse_form_xml(chart_xml(invalid)).ok(), "malformed typed pairs, wrappers, references and incomplete ValueList must be rejected");
    }
    std::string nested = "<String>leaf</String>";
    for (std::size_t depth = 0; depth < model::max_typed_value_depth; ++depth)
        nested = "<Array><Item>" + nested + "</Item></Array>";
    expect(source::parse_form_xml(chart_xml(nested)).ok(), "typed root depth zero must admit a leaf exactly at depth 64");
    const auto overflow = source::parse_form_xml(chart_xml("<Array><Item>" + nested + "</Item></Array>"));
    expect(!overflow.ok() && !overflow.diagnostics().empty() &&
        overflow.diagnostics().front().message == "Typed values exceed the document resource budget",
        "XML must reject excess recursive depth before appending a nested value");
    model::TypedValueBudget shared_budget{model::max_typed_value_nodes - 1};
    expect(model::validate_typed_value(model::PropertyValue{std::string{"first"}}, shared_budget) &&
        !model::validate_typed_value(model::PropertyValue{std::string{"second"}}, shared_budget),
        "one pre-used document node budget must reject a later typed value without allocating a million nodes");
}

void test_map_canonical_xml_order() {
    const std::string numeric = R"XML(<Entry><Key><Number>17</Number></Key><Value><Array><Item><String>Вложенное значение</String></Item></Array></Value></Entry>)XML";
    const std::string boolean = R"XML(<Entry><Key><Boolean>false</Boolean></Key><Value><Structure><Field name="Caption"><String>Заголовок</String></Field></Structure></Value></Entry>)XML";
    const std::string text = R"XML(<Entry><Key><String>Title</String></Key><Value><String>Значение</String></Value></Entry>)XML";
    for (const auto kind : {"Map", "FixedMap"}) {
        const auto make_xml = [&](const std::string& entries) {
            return std::string(R"XML(<Form id="1" name="CanonicalMap" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Chart"><Position/><Series><ChartSeries id="2"><Text/><Color kind="automatic"/><Marker type="ChartMarkerType" member="Auto"/><Value><Array><Item><)XML") +
                kind + ">" + entries + "</" + kind + "></Item></Array></Value></ChartSeries></Series><Points/><Values/></Chart></ChildItems></Form>";
        };
        const auto first = source::parse_form_xml(make_xml(numeric + boolean + text));
        const auto second = source::parse_form_xml(make_xml(text + numeric + boolean));
        expect(first.ok() && second.ok(), "both native Map enumeration orders must parse");
        const auto map_value = [](const model::OrdinaryFormDocument& document) -> const model::MapValue& {
            const auto& chart = std::get<model::ChartPayload>(document.find_control(model::ObjectId{2})->payload);
            return std::get<model::MapValue>(std::get<model::ArrayValue>(chart.series.front().value).items.front());
        };
        const auto& first_map = map_value(first.value());
        const auto& second_map = map_value(second.value());
        const auto& first_chart = std::get<model::ChartPayload>(first.value().find_control(model::ObjectId{2})->payload);
        const auto& second_chart = std::get<model::ChartPayload>(second.value().find_control(model::ObjectId{2})->payload);
        expect(first_map == second_map && first_chart.series.front().value == second_chart.series.front().value,
            "Map deep equality must compare key/value associations independently of enumeration order");
        expect(std::holds_alternative<model::DecimalValue>(first_map.entries.front().key) &&
            std::holds_alternative<std::string>(second_map.entries.front().key), "parser must preserve each supplied wire order");
        const auto first_xml = source::serialize_form_xml(first.value());
        const auto second_xml = source::serialize_form_xml(second.value());
        expect(first_xml.ok() && second_xml.ok() && first_xml.value() == second_xml.value(),
            "Map and FixedMap must emit the same canonical XML for equivalent entry orders, including nested values");
        expect(std::holds_alternative<model::DecimalValue>(first_map.entries.front().key) &&
            std::holds_alternative<std::string>(second_map.entries.front().key), "canonical XML writer must not mutate either model entry order");
        const auto reparsed = source::parse_form_xml(first_xml.value());
        expect(reparsed.ok() && map_value(reparsed.value()) == first_map,
            "canonical Map XML must preserve the complete key/value graph");
    }
}

void test_form_item_collection_xml_contract() {
    using namespace model;
    const FormItemCollectionRef controls{ObjectId{1}, FormItemCollectionKind::controls};
    std::string xml = R"XML(<Form id="1" name="Collections" ordinaryFormVersion="2.1"><ChildItems>)XML";
    std::vector<ChildItemRef> expected;
    for (std::uint64_t index = 0; index < 12; ++index) {
        const auto panel = std::to_string(100 + index);
        const auto label = std::to_string(200 + index);
        xml += "<Panel id=\"" + panel + "\" name=\"Panel" + std::to_string(index) +
            "\"><Position/><ChildItems><Page name=\"First\"><ChildItems><LabelDecoration id=\"" + label +
            "\" name=\"Label" + std::to_string(index) + "\"><Position/></LabelDecoration></ChildItems></Page></ChildItems></Panel>";
        expected.emplace_back(ControlRef{ObjectId{100 + index}});
        expected.emplace_back(ControlRef{ObjectId{200 + index}});
    }
    xml += "</ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok() && parsed.value().form_item_refs(controls) == expected,
        "XML parsing must expose native Controls preorder independently of object insertion order");
    const auto written = source::serialize_form_xml(parsed.value());
    expect(written.ok() && written.value().find("<Controls") == std::string::npos &&
        written.value().find("<Pages") == std::string::npos && written.value().find("<CurrentPage") == std::string::npos,
        "collection handles and runtime selection must not duplicate named ChildItems in XML");
    const auto reparsed = source::parse_form_xml(written.value());
    expect(reparsed.ok() && reparsed.value().form_item_refs(controls) == expected,
        "Controls references and preorder must survive named XML roundtrip");
    for (const auto invalid : {
        R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><Controls/></Form>)XML",
        R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><ChildItems><Panel id="2" name="Tabs"><Position/><Pages/></Panel></ChildItems></Form>)XML",
        R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><ChildItems><Panel id="2" name="Tabs"><Position/><CurrentPage>3</CurrentPage></Panel></ChildItems></Form>)XML"}) {
        expect(!source::parse_form_xml(invalid).ok(), "duplicate collections and runtime page references must be rejected");
    }
}

template <typename T>
void expect_code(const oof::Result<T>& result, std::string_view code, std::string_view message) {
    expect(!result.ok(), message);
    expect(!result.diagnostics().empty(), "failed result must contain a diagnostic");
    expect(result.diagnostics().front().code == code, message);
}

void test_complete_document_roundtrip() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1">
  <Events>
    <OnOpen id="7">OnOpen</OnOpen>
  </Events>
  <Attributes>
    <Attribute id="2" name="Value">
      <TypeDomain/>
      <Main>true</Main>
      <StoredData>false</StoredData>
    </Attribute>
  </Attributes>
  <Commands>
    <Command id="3" name="Run" handler="Run">
      <Title><Item language="en">Run &amp; validate</Item></Title>
      <ChangesData>true</ChangesData>
      <Picture>4</Picture>
    </Command>
  </Commands>
  <PictureAssets>
    <PictureAsset id="4" relativePath="Items/Icon/Picture.gif" format="gif"/>
  </PictureAssets>
  <ChildItems>
    <Button id="5" name="RunButton">
      <DataPath attributeId="2"><Member>Nested</Member></DataPath>
      <AutoContextMenu>true</AutoContextMenu>
      <Position>
        <Top>0</Top>
        <Visible>true</Visible>
        <Bindings>
          <AnchorBinding coordinate="left" targetCoordinate="left" offset="0"/>
          <DimensionBinding dimension="width" value="120"/>
        </Bindings>
      </Position>
      <Caption>Run</Caption>
      <Events><Click id="6">Run</Click></Events>
    </Button>
  </ChildItems>
</Form>
)XML";

    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "complete Form.xml must parse");
    expect(parsed.value().validate().ok(), "parsed Form.xml must satisfy model invariants");
    expect(parsed.value().collections().controls.size() == 1, "one control must be materialized");
    expect(parsed.value().collections().events.size() == 2, "nested events must be materialized");
    expect(parsed.value().collections().attributes.front().main.is_explicit(), "true attribute flag must remain explicit");
    expect(!parsed.value().collections().attributes.front().stored_data.is_explicit(), "explicit default attribute flag must normalize to implicit");

    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "parsed document must serialize");
    expect(serialized.value().find("ordinaryFormVersion=\"2.1\"") != std::string::npos, "writer must emit fixed version 2.1");
    expect(serialized.value().find("<StoredData>") == std::string::npos, "default command/attribute values must be omitted");
    expect(serialized.value().find("<Top>") == std::string::npos, "default Position values must be omitted");
    expect(serialized.value().find("<Visible>") == std::string::npos, "default Visible=true must normalize to implicit");

    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "canonical Form.xml must reparse");
    auto repeated = source::serialize_form_xml(reparsed.value());
    expect(repeated.ok(), "reparsed Form.xml must serialize");
    expect(repeated.value() == serialized.value(), "Form.xml serialization must be deterministic");
}

void test_xml_explicit_z_order_normalization() {
    std::string xml = R"XML(<Form id="1" name="ZOrder" ordinaryFormVersion="2.1"><ChildItems>)XML";
    for (int index = 0; index < 12; ++index) {
        xml += "<LabelDecoration id=\"" + std::to_string(index + 10) + "\" name=\"Label" + std::to_string(index) + "\"><Position>";
        if (index == 0) xml += "<ZOrder>12</ZOrder>";
        if (index == 11) xml += "<ZOrder>1</ZOrder>";
        xml += "</Position></LabelDecoration>";
    }
    xml += "</ChildItems></Form>";
    auto document = source::parse_form_xml(xml);
    expect(document.ok(), "a complete explicit display permutation must parse");
    const auto& children = document.value().form().children;
    expect(std::get<model::ControlRef>(children.front()).id() == model::ObjectId{21} &&
               std::get<model::ControlRef>(children.back()).id() == model::ObjectId{10},
        "ZOrder must define canonical ChildItems order");
    for (int index = 0; index < 12; ++index) {
        const auto id = model::ObjectId{static_cast<std::uint64_t>(index + 10)};
        const auto tab = document.value().get_prop_val({model::ObjectCategory::control, id}, model::PropertyId::from_name("TabOrder"));
        expect(tab.value == model::PropertyValue{std::int64_t{index + 1}}, "normalization must preserve the initial tab order");
        expect(!document.value().find_control(id)->position.z_order.is_explicit(), "normalization must not leave a separate stored ZOrder");
    }
    const auto out = source::serialize_form_xml(document.value());
    expect(out.ok() && out.value().find("<ZOrder>") == std::string::npos, "canonical XML uses ChildItems for display order");
    const auto reparsed = source::parse_form_xml(out.value());
    expect(reparsed.ok() && reparsed.value().form().children == children, "canonical XML must preserve the display permutation");
    const auto encoded = oof::storage::form_stream::encode_document(reparsed.value());
    expect(encoded.ok(), "normalized display order must build the form stream");
    const auto invalid = [](std::string_view first, std::string_view second) {
        return "<Form id=\"1\" name=\"Z\" ordinaryFormVersion=\"2.1\"><ChildItems><LabelDecoration id=\"2\" name=\"A\"><Position>" +
            std::string(first) + "</Position></LabelDecoration><LabelDecoration id=\"3\" name=\"B\"><Position>" +
            std::string(second) + "</Position></LabelDecoration></ChildItems></Form>";
    };
    const auto partial = source::parse_form_xml(invalid("<ZOrder>2</ZOrder>", ""));
    expect(partial.ok() && std::get<model::ControlRef>(partial.value().form().children.front()).id() == model::ObjectId{3},
        "partial display order must leave unspecified siblings in their original relative order");
    const auto second_first = source::parse_form_xml(invalid("", "<ZOrder>1</ZOrder>"));
    expect(second_first.ok() && std::get<model::ControlRef>(second_first.value().form().children.front()).id() == model::ObjectId{3},
        "a single explicit first position must move the second sibling to the front");
    expect_code(source::parse_form_xml(invalid("<ZOrder>1</ZOrder>", "<ZOrder>1</ZOrder>")), "OOF2003", "duplicate display order must be rejected");
    expect_code(source::parse_form_xml(invalid("<ZOrder>3</ZOrder>", "")), "OOF2003", "out-of-range display order must be rejected");
}

void test_reconstruction_completeness_xml_metadata() {
    constexpr std::string_view incomplete_xml =
        R"XML(<Form id="1" name="Partial" ordinaryFormVersion="2.1" reconstructionComplete="false"/>)XML";
    const auto parsed = source::parse_form_xml(incomplete_xml);
    expect(parsed.ok() && !parsed.value().reconstruction_complete(),
        "reconstructionComplete=false must parse into model metadata");
    expect(parsed.diagnostics().size() == 1 &&
               parsed.diagnostics().front().severity == oof::DiagnosticSeverity::warning,
        "incomplete XML parse must succeed with a warning");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("reconstructionComplete=\"false\"") != std::string::npos,
        "XML serialization must preserve incomplete reconstruction metadata");
    expect(serialized.diagnostics().size() == 1 &&
               serialized.diagnostics().front().severity == oof::DiagnosticSeverity::warning,
        "incomplete XML serialization must return a warning with success");

    constexpr std::string_view complete_xml =
        R"XML(<Form id="1" name="Complete" ordinaryFormVersion="2.1"/>)XML";
    const auto complete = source::parse_form_xml(complete_xml);
    expect(complete.ok() && complete.value().reconstruction_complete() && complete.diagnostics().empty(),
        "missing completeness metadata must retain the complete default without warning");
    const auto complete_serialized = source::serialize_form_xml(complete.value());
    expect(complete_serialized.ok() &&
               complete_serialized.value().find("reconstructionComplete") == std::string::npos,
        "complete XML must omit the default completeness attribute");
}

void test_usual_group_named_xml_round_trip() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Groups" ordinaryFormVersion="2.1"><ChildItems>
      <UsualGroup id="2" name="DefaultGroup"><Position/></UsualGroup>
      <UsualGroup id="3" name="CustomGroup"><Position><Top>30</Top><Visible>false</Visible><Height>70</Height><Left>20</Left><Width>150</Width></Position><Enabled>false</Enabled><Caption>Группа Ω</Caption><ToolTip>Подсказка</ToolTip></UsualGroup>
    </ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.ok() ? "UsualGroup named XML must parse" :
        parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto* custom = parsed.value().find_control(model::ObjectId{3});
    expect(custom && custom->kind() == model::ControlKind::usual_group && custom->position.left.value() == 20 &&
        custom->position.width.value() == 150 && !custom->position.visible.value(),
        "UsualGroup Position and Visible must be typed");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<UsualGroup") != std::string::npos &&
        serialized.value().find("Группа Ω") != std::string::npos && serialized.value().find("<Visible>false</Visible>") != std::string::npos,
        "UsualGroup named properties must serialize in public XML");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && source::serialize_form_xml(reparsed.value()).value() == serialized.value(),
        "UsualGroup XML must reach a deterministic named round-trip");
}

void test_root_page_tree_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1">
  <ChildItems>
    <Page name="RootPage">
      <Title><Item language="en">Root tab</Item></Title>
      <Visible>false</Visible>
      <ChildItems>
        <Button id="3" name="PageButton"><Position/><Caption>Run</Caption></Button>
      </ChildItems>
    </Page>
    <Panel id="4" name="TabPanel">
      <Position/>
      <ChildItems>
        <Page name="PanelPage">
          <Title><Item language="en">Panel tab</Item></Title>
          <Enabled>false</Enabled>
          <ChildItems>
            <Button id="6" name="PanelPageButton"><Position/><Caption>Open</Caption></Button>
          </ChildItems>
        </Page>
        <Button id="7" name="PanelButton"><Position/><Caption>Close</Caption></Button>
      </ChildItems>
    </Panel>
    <Button id="8" name="RootButton"><Position/><Caption>Cancel</Caption></Button>
  </ChildItems>
</Form>
)XML";

    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "Form root Page with control children must parse");
    expect(parsed.value().validate().ok(), "root Page tree must satisfy model invariants");
    expect(parsed.value().form().children.size() == 3 &&
               std::holds_alternative<model::PageRef>(parsed.value().form().children[0]) &&
               std::get<model::PageRef>(parsed.value().form().children[0]).id() == model::ObjectId{1} &&
               std::get<model::ControlRef>(parsed.value().form().children[1]).id() == model::ObjectId{4} &&
               std::get<model::ControlRef>(parsed.value().form().children[2]).id() == model::ObjectId{8},
        "root Page and control order must be retained");
    const auto* page = parsed.value().find_page(model::ObjectId{1});
    expect(page != nullptr && page->title.value().items.front().text == "Root tab" &&
               !page->visible.value() && page->enabled.value() &&
               page->children.size() == 1 &&
               std::get<model::ControlRef>(page->children.front()).id() == model::ObjectId{3},
        "root Page title and control child must be retained");
    const auto* panel = parsed.value().find_control(model::ObjectId{4});
    const auto* panel_page = parsed.value().find_page(model::ObjectId{2});
    expect(panel != nullptr && panel->kind() == model::ControlKind::panel && panel->children.size() == 2 &&
               std::holds_alternative<model::PageRef>(panel->children[0]) &&
               std::get<model::PageRef>(panel->children[0]).id() == model::ObjectId{2} &&
               std::get<model::ControlRef>(panel->children[1]).id() == model::ObjectId{7},
        "Panel Page and control order must be retained");
    expect(panel_page != nullptr && panel_page->visible.value() && !panel_page->enabled.value(),
        "Page visibility and enabled state must be independent");

    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "root Page tree must serialize to Form.xml");
    expect(serialized.value().find("<Page id=") == std::string::npos,
           "Page identity must not be emitted in public XML");
    expect(serialized.value().find("<Button name=\"PageButton\" id=\"3\">") != std::string::npos,
           "control IDs must remain in public XML");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "serialized root Page tree must parse again");
    auto repeated = source::serialize_form_xml(reparsed.value());
    expect(repeated.ok() && repeated.value() == serialized.value(),
           "Page tree XML must reach a byte-canonical round-trip");
    const auto* reparsed_panel = reparsed.value().find_control(model::ObjectId{4});
    const auto* reparsed_panel_page = reparsed.value().find_page(model::ObjectId{2});
    expect(reparsed.value().form().children == parsed.value().form().children &&
               reparsed.value().find_page(model::ObjectId{1}) != nullptr &&
               reparsed.value().find_page(model::ObjectId{1})->children == page->children &&
               reparsed_panel != nullptr && reparsed_panel->children == panel->children &&
               reparsed_panel_page != nullptr &&
               panel_page != nullptr &&
               !reparsed.value().find_page(model::ObjectId{1})->visible.value() &&
               !reparsed_panel_page->enabled.value() &&
               reparsed_panel_page->children == panel_page->children,
        "Form, Page, and Panel references must survive XML round-trip");
}

void test_page_internal_ids_do_not_change_xml() {
    const auto serialize_page = [](std::uint64_t page_id) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "Main";
        form.children.emplace_back(model::PageRef{model::ObjectId{page_id}});
        model::OrdinaryFormDocument document(std::move(form));
        model::Page page;
        page.id = model::ObjectId{page_id};
        page.name = "Settings";
        document.add_page(std::move(page));
        auto serialized = source::serialize_form_xml(document);
        expect(serialized.ok(), "internally identified Page must serialize");
        return serialized.value();
    };

    expect(serialize_page(2) == serialize_page(92),
           "internal Page IDs must not affect public XML");
}

void test_page_boolean_defaults_and_rejections() {
    const auto parse_page_properties = [](std::string_view properties) {
        return source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<Page name=\"Settings\">" + std::string(properties) +
            "</Page></ChildItems></Form>");
    };
    auto defaults = parse_page_properties("");
    auto explicit_defaults = parse_page_properties("<Visible>true</Visible><Enabled>1</Enabled>");
    expect(defaults.ok() && explicit_defaults.ok(), "Page true defaults must parse");
    const auto* page = explicit_defaults.value().find_page(model::ObjectId{1});
    expect(page != nullptr && page->visible.value() && page->enabled.value() &&
               !page->visible.is_explicit() && !page->enabled.is_explicit(),
        "Page true defaults must normalize to implicit values");
    auto default_xml = source::serialize_form_xml(defaults.value());
    auto normalized_xml = source::serialize_form_xml(explicit_defaults.value());
    expect(default_xml.ok() && normalized_xml.ok() && default_xml.value() == normalized_xml.value(),
        "Page default values must not produce Git differences");
    expect_code(parse_page_properties("<Visible>yes</Visible>"), "OOF2002",
        "invalid Page visibility must be rejected by XSD");
    expect_code(parse_page_properties("<Enabled>false</Enabled><Enabled>true</Enabled>"), "OOF2002",
        "duplicate Page enabled state must be rejected by XSD");
}

void test_page_position_roundtrip_and_rejections() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <Page name="RootPage">
    <Position><Top>6</Top><Height>80</Height><Left>5</Left><Width>120</Width>
      <Bindings manualHorizontal="true">
        <AnchorBinding coordinate="right" targetCoordinate="right" targetId="2" offset="3"/>
        <DimensionBinding dimension="width" value="120"/>
      </Bindings>
    </Position>
    <ChildItems><Button id="2" name="RootButton"><Position/><Caption>Run</Caption></Button></ChildItems>
  </Page>
  <Panel id="3" name="Tabs"><Position/><ChildItems>
    <Page name="NestedPage">
      <Position><Top>8</Top><Height>60</Height><Left>7</Left><Width>90</Width>
        <Bindings><AnchorBinding coordinate="left" targetCoordinate="left" targetId="4" offset="-1"/></Bindings>
      </Position>
      <ChildItems><Button id="4" name="NestedButton"><Position/><Caption>Open</Caption></Button></ChildItems>
    </Page>
  </ChildItems></Panel>
</ChildItems></Form>
)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "root and nested Page.Position must parse");
    const auto* root_page = parsed.value().find_page(model::ObjectId{1});
    const auto* nested_page = parsed.value().find_page(model::ObjectId{2});
    expect(root_page != nullptr && root_page->position.is_explicit() &&
               root_page->position.value().left.value() == 5 &&
               root_page->position.value().top.value() == 6 &&
               root_page->position.value().width.value() == 120 &&
               root_page->position.value().height.value() == 80 &&
               root_page->position.value().bindings.manual_horizontal.value() &&
               root_page->position.value().bindings.anchors.size() == 1 &&
               root_page->position.value().bindings.anchors.front().target == model::ControlRef{model::ObjectId{2}} &&
               root_page->position.value().bindings.dimensions.size() == 1,
        "root Page Position coordinates and bindings must materialize");
    expect(nested_page != nullptr && nested_page->position.is_explicit() &&
               nested_page->position.value().left.value() == 7 &&
               nested_page->position.value().top.value() == 8 &&
               nested_page->position.value().width.value() == 90 &&
               nested_page->position.value().height.value() == 60 &&
               nested_page->position.value().bindings.anchors.size() == 1 &&
               nested_page->position.value().bindings.anchors.front().target == model::ControlRef{model::ObjectId{4}},
        "nested Page Position coordinates and bindings must materialize");

    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Width>120</Width>") != std::string::npos,
        "explicit root and nested Page Position must serialize");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "serialized Page Position must validate and parse");
    auto repeated = source::serialize_form_xml(reparsed.value());
    expect(repeated.ok() && repeated.value() == serialized.value(),
        "Page Position and Bindings must round-trip canonically");

    auto implicit = source::parse_form_xml(
        "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Page name=\"Default\"/></ChildItems></Form>");
    expect(implicit.ok(), "Page without Position must remain valid");
    const auto* implicit_page = implicit.value().find_page(model::ObjectId{1});
    auto implicit_xml = source::serialize_form_xml(implicit.value());
    expect(implicit_page != nullptr && !implicit_page->position.is_explicit() &&
               implicit_xml.ok() && implicit_xml.value().find("<Position") == std::string::npos,
        "implicit Page Position must not add XML output");

    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Page name=\"BadTarget\"><Position><Bindings><AnchorBinding coordinate=\"left\" targetCoordinate=\"left\" targetId=\"99\" offset=\"0\"/>"
        "</Bindings></Position></Page></ChildItems></Form>"),
        "OOF2004", "Page Position binding must reject a dangling control target");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Page name=\"BadSize\"><Position><Width>-1</Width></Position></Page>"
        "</ChildItems></Form>"),
        "OOF2004", "Page Position must reject negative dimensions");
}

void test_all_control_variants() {
    std::string xml = "<Form id=\"1\" name=\"All\" ordinaryFormVersion=\"2.1\"><Attributes>"
        "<Attribute id=\"100\" name=\"Rows\"><TypeDomain><Entry term=\"valueTable\"/>"
        "</TypeDomain></Attribute></Attributes><ChildItems>";
    std::uint64_t id = 2;
    for (const auto& descriptor : model::metamodel::control_descriptors()) {
        xml += "<" + std::string(descriptor.public_name) + " id=\"" +
               std::to_string(id) + "\" name=\"C" + std::to_string(id) + "\">";
        if (descriptor.kind == model::ControlKind::table) {
            xml += "<DataPath attributeId=\"100\"/><Position/><Columns><Column name=\"Code\">"
                   "<DataPath>Code</DataPath><Header><Item language=\"en\">Code</Item></Header>"
                   "<Control type=\"InputField\"/></Column></Columns>";
        } else {
            xml += "<Position/>";
            if (descriptor.kind == model::ControlKind::chart) {
                xml += "<TitleArea><Text>Title</Text><Placement>Auto</Placement><Left>0.83</Left><Top>0</Top><Right>1</Right><Bottom>0.08</Bottom></TitleArea><Series><ChartSeries id=\"2\"><Text>Series</Text><Color kind=\"absolute\" red=\"1\"/><Marker type=\"ChartMarkerType\" member=\"Auto\"/></ChartSeries></Series>"
                       "<Points><ChartPoint id=\"1\"><Text>Point</Text><Color kind=\"absolute\" red=\"2\"/></ChartPoint></Points>"
                       "<Values><ChartValue seriesRef=\"2\" pointRef=\"1\"><Number>1</Number></ChartValue></Values>";
            }
        }
        xml += "</" + std::string(descriptor.public_name) + ">";
        ++id;
    }
    xml += "</ChildItems></Form>";

    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "all 26 typed controls must parse in one document");
    expect(parsed.value().collections().controls.size() == 26, "all 26 payload variants must materialize");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "all 26 typed controls must serialize");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "all-control canonical XML must reparse");
    expect(reparsed.value().collections().controls.size() == 26, "all control kinds must survive source roundtrip");

    std::string edited_xml = serialized.value();
    for (const auto& control : reparsed.value().collections().controls) {
        const std::string old_name = "name=\"" + control.name + "\"";
        const auto offset = edited_xml.find(old_name);
        expect(offset != std::string::npos, "editable control name must appear in XML");
        edited_xml.replace(offset, old_name.size(), "name=\"Edited" + control.name + "\"");
    }
    auto edited = source::parse_form_xml(edited_xml);
    expect(edited.ok(), "renamed controls must parse without changing their IDs");
    expect(edited.value().form().children == reparsed.value().form().children,
           "XML rename must preserve ordered child references");
    for (const auto& original : reparsed.value().collections().controls) {
        const auto* renamed = edited.value().find_control(original.id);
        expect(renamed != nullptr && renamed->kind() == original.kind(),
               "XML rename must preserve control identity and type");
        expect(renamed->name == "Edited" + original.name,
               "edited XML name must reach the typed model");
    }
    auto edited_source = source::serialize_form_xml(edited.value());
    expect(edited_source.ok() && edited_source.value() == edited_xml,
           "renamed XML must remain canonical without losing the edits");
}

void test_calendar_field_enabled_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="CalendarForm" ordinaryFormVersion="2.1">
  <ChildItems>
    <CalendarField id="2" name="Calendar">
      <Position><Top>32</Top><Visible>false</Visible><Left>24</Left></Position>
      <Enabled>false</Enabled>
    </CalendarField>
  </ChildItems>
</Form>
)XML";

    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.ok() ? "CalendarField parse result must be inspected" :
        parsed.diagnostics().front().code + ":" + parsed.diagnostics().front().path + ":" +
            parsed.diagnostics().front().message);
    const auto* calendar = parsed.value().find_control(model::ObjectId{2});
    expect(calendar != nullptr && calendar->kind() == model::ControlKind::calendar_field &&
               calendar->name == "Calendar",
        "CalendarField identity and public XML name must remain typed");
    const auto* enabled = calendar->properties().find(model::PropertyId::from_name("Enabled"));
    expect(enabled != nullptr && std::get<bool>(enabled->value) == false,
        "CalendarField Enabled=false must materialize as a Boolean property");
    expect(calendar->position.left.value() == 24 && calendar->position.top.value() == 32 &&
               !calendar->position.visible.value(),
        "CalendarField Position and Visible must materialize through the shared placement model");

    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<CalendarField") != std::string::npos &&
               serialized.value().find("<Enabled>false</Enabled>") != std::string::npos &&
               serialized.value().find("<Visible>false</Visible>") != std::string::npos,
        "CalendarField Enabled and Visible must serialize under their public names");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "canonical CalendarField XML must reparse");
    const auto* round_trip = reparsed.value().find_control(model::ObjectId{2});
    expect(round_trip != nullptr && round_trip->kind() == model::ControlKind::calendar_field &&
               std::get<bool>(round_trip->properties().find(model::PropertyId::from_name("Enabled"))->value) == false &&
               !round_trip->position.visible.value() && round_trip->position.left.value() == 24,
        "CalendarField named values must survive XML source round-trip");
}

void test_html_document_field_output_xml_roundtrip() {
    for (const std::string_view member : {"Auto", "Enable", "Disable"}) {
        const std::string xml =
            "<Form id=\"1\" name=\"HtmlForm\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<HTMLDocumentField id=\"2\" name=\"Html\"><Position/>"
            "<Output type=\"Output\" member=\"" + std::string(member) + "\"/>"
            "</HTMLDocumentField></ChildItems></Form>";
        auto parsed = source::parse_form_xml(xml);
        expect(parsed.ok(), "supported HTMLDocumentField.Output enum must parse");
        const auto* field = parsed.value().find_control(model::ObjectId{2});
        expect(field != nullptr && field->kind() == model::ControlKind::html_document_field,
            "HTMLDocumentField public name must materialize the typed control payload");
        const auto* output = field->properties().find(model::PropertyId::from_name("Output"));
        if (member == "Auto") {
            expect(output == nullptr, "HTMLDocumentField Output Auto must normalize to its implicit default");
        } else {
            expect(output != nullptr &&
                       std::get<model::EnumerationValue>(output->value) == model::EnumerationValue{"Output", std::string(member)},
                "HTMLDocumentField.Output must materialize a named enumeration");
        }
        auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok(), "HTMLDocumentField.Output source must serialize");
        if (member == "Auto") {
            expect(serialized.value().find("<Output") == std::string::npos,
                "implicit Auto default must be omitted from canonical XML");
        } else {
            expect(serialized.value().find("<Output type=\"Output\" member=\"" + std::string(member) + "\"/>") != std::string::npos,
                "non-default Output must serialize with the public property name and enum member");
        }
        auto reparsed = source::parse_form_xml(serialized.value());
        expect(reparsed.ok(), "canonical HTMLDocumentField.Output XML must reparse");
    }
}

void test_binding_target_and_manual_roundtrip() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <Button id="2" name="Run"><Position><Bindings manualHorizontal="true" manualVertical="true">
    <AnchorBinding coordinate="right" targetCoordinate="left" targetId="3" offset="7">
      <ProportionalBinding targetCoordinate="bottom" offset="-2"/>
    </AnchorBinding>
  </Bindings></Position><Caption>Run</Caption></Button>
  <LabelDecoration id="3" name="Target"><Position/><Caption>Target</Caption></LabelDecoration>
</ChildItems></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "primary and proportional anchor targets must parse");
    const auto* run = parsed.value().find_control(model::ObjectId{2});
    expect(run != nullptr && run->position.bindings.anchors.size() == 1,
        "primary anchor must be materialized");
    const auto& binding = run->position.bindings.anchors.front();
    expect(binding.coordinate == model::BindingCoordinate::right &&
               binding.target_coordinate == model::BindingCoordinate::left &&
               binding.target == model::ControlRef{model::ObjectId{3}} &&
               binding.offset.value() == 7 && binding.proportional.has_value() &&
               !binding.proportional->target.has_value() &&
               binding.proportional->coordinate == model::BindingCoordinate::bottom &&
               binding.proportional->offset.value() == -2,
        "anchor edges, offsets, and Form proportional target must be retained");
    expect(run->position.bindings.manual_horizontal.value() &&
               run->position.bindings.manual_vertical.value(),
        "manual flags must be retained");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "anchor bindings must serialize");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "serialized anchor bindings must reparse");
    const auto* roundtrip = reparsed.value().find_control(model::ObjectId{2});
    expect(roundtrip != nullptr && roundtrip->position.bindings.manual_horizontal.value() &&
               roundtrip->position.bindings.manual_vertical.value() &&
               roundtrip->position.bindings.anchors.size() == 1 &&
               roundtrip->position.bindings.anchors.front().coordinate == binding.coordinate &&
               roundtrip->position.bindings.anchors.front().target_coordinate == binding.target_coordinate &&
               roundtrip->position.bindings.anchors.front().target == binding.target &&
               roundtrip->position.bindings.anchors.front().offset.value() == binding.offset.value() &&
               roundtrip->position.bindings.anchors.front().proportional.has_value() &&
               roundtrip->position.bindings.anchors.front().proportional->target == binding.proportional->target &&
               roundtrip->position.bindings.anchors.front().proportional->coordinate == binding.proportional->coordinate &&
               roundtrip->position.bindings.anchors.front().proportional->offset.value() == binding.proportional->offset.value(),
        "all binding targets and manual flags must survive XML round-trip");

    constexpr std::string_view manual_only = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <Button id="2" name="Run"><Position><Bindings manualHorizontal="true"/></Position><Caption>Run</Caption></Button>
</ChildItems></Form>)XML";
    auto manual_parsed = source::parse_form_xml(manual_only);
    expect(manual_parsed.ok(), "manual-only bindings must parse");
    auto manual_xml = source::serialize_form_xml(manual_parsed.value());
    expect(manual_xml.ok() && manual_xml.value().find("<Bindings manualHorizontal=\"true\"") != std::string::npos,
        "manual-only Bindings must be emitted");

    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Button id=\"2\" name=\"Run\"><Position><Bindings><AnchorBinding coordinate=\"right\" offset=\"0\"/>"
        "</Bindings></Position><Caption>Run</Caption></Button></ChildItems></Form>"),
        "OOF2002", "missing targetCoordinate must be rejected by XSD");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Button id=\"2\" name=\"Run\"><Position><Bindings><AnchorBinding coordinate=\"right\" targetCoordinate=\"left\" offset=\"0\">"
        "<ProportionalBinding targetCoordinate=\"bottom\" targetId=\"99\" offset=\"0\"/>"
        "</AnchorBinding></Bindings></Position><Caption>Run</Caption></Button></ChildItems></Form>"),
        "OOF2004", "dangling proportional target must fail model validation");
}

void test_typed_values_and_canonicalization() {
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"Restriction\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<InputField id=\"2\" name=\"Input\"><ValueType><Entry term=\"string\" length=\"64\"/></ValueType>"
        "<Position/><TypeRestriction><Entry term=\"string\" length=\"8\"/></TypeRestriction>"
        "</InputField></ChildItems></Form>"), "OOF2002",
        "runtime TypeRestriction must not be accepted as persisted XML state");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"Mask\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Button id=\"2\" name=\"Legacy\"><Position/><Font kind=\"automatic\" mask=\"0\"/>"
        "</Button></ChildItems></Form>"), "OOF2002",
        "removed Font mask must be rejected rather than parsed as an alias");
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Values" ordinaryFormVersion="2.1">
  <Attributes>
    <Attribute id="2" name="Value">
      <TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain>
    </Attribute>
  </Attributes>
  <PictureAssets>
    <PictureAsset id="3" relativePath="Items/Input/Picture.png" format="png"/>
  </PictureAssets>
  <ChildItems>
    <InputField id="4" name="Input">
      <DataPath attributeId="2"/>
      <Position/>
      <VerticalAlign type="VerticalAlign" member="Bottom"/>
      <ChoiceListHeight>0007</ChoiceListHeight>
      <HorizontalAlign type="HorizontalAlign" member="Right"/>
      <Picture>3</Picture>
      <BorderColor kind="absolute" red="18" green="52" blue="86" alpha="255"/>
      <Font kind="styleReference" styleName="StyleFonts.TextFont"/>
    </InputField>
    <CalendarField id="5" name="Calendar">
      <Position/>
      <EndOfDisplayPeriod>undefined</EndOfDisplayPeriod>
    </CalendarField>
    <ActiveXControl id="6" name="ActiveX">
      <Position/>
      <CLSID>01234567-89AB-CDEF-0123-456789ABCDEF</CLSID>
    </ActiveXControl>
  </ChildItems>
</Form>
)XML";

    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "classified scalar and structured values must parse");
    const model::ControlNode* input = parsed.value().find_control(model::ObjectId{4});
    expect(input != nullptr, "InputField must resolve");
    const auto* height = input->properties().find(model::PropertyId::from_name("ChoiceListHeight"));
    expect(height != nullptr, "InputField ChoiceListHeight must materialize");
    expect(std::get<std::int64_t>(height->value) == 7, "integer lexical form must canonicalize");
    expect(std::get<model::EnumerationValue>(input->properties().find(
               model::PropertyId::from_name("HorizontalAlign"))->value) ==
               model::EnumerationValue{"HorizontalAlign", "Right"} &&
           std::get<model::EnumerationValue>(input->properties().find(
               model::PropertyId::from_name("VerticalAlign"))->value) ==
               model::EnumerationValue{"VerticalAlign", "Bottom"},
        "InputField alignment enum members must parse as named values");
    expect(input->properties().find(model::PropertyId::from_name("AutoChoiceIncomplete")) == nullptr,
        "omitted AutoChoiceIncomplete must retain the false default");
    std::string auto_choice_true_source(xml);
    const auto input_position = auto_choice_true_source.find("<Position/>");
    auto_choice_true_source.insert(input_position + std::string_view{"<Position/>"}.size(),
        "<AutoChoiceIncomplete>true</AutoChoiceIncomplete>");
    parsed = source::parse_form_xml(auto_choice_true_source);
    expect(parsed.ok(), parsed.ok() ? "named AutoChoiceIncomplete=true must parse" :
        parsed.diagnostics().front().code + ":" + parsed.diagnostics().front().message);
    input = parsed.value().find_control(model::ObjectId{4});
    const model::ControlNode* calendar = parsed.value().find_control(model::ObjectId{5});
    const auto date = parsed.value().get_prop_val(
        {model::ObjectCategory::control, calendar->id}, model::PropertyId::from_name("EndOfDisplayPeriod"));
    expect(date.status == model::PropertyReadStatus::proven_default && date.value &&
            std::holds_alternative<model::UndefinedValue>(*date.value),
        "Date|Undefined union must expose the proven Undefined default");

    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "typed values must serialize");
    expect(serialized.value().find(">7</ChoiceListHeight>") != std::string::npos, "integer output must be canonical");
    expect(serialized.value().find("type=\"HorizontalAlign\" member=\"Right\"") != std::string::npos &&
            serialized.value().find("type=\"VerticalAlign\" member=\"Bottom\"") != std::string::npos,
        "InputField alignment enums must serialize with their named type and member");
    expect(serialized.value().find("<AutoChoiceIncomplete>true</AutoChoiceIncomplete>") != std::string::npos,
        "named AutoChoiceIncomplete=true must serialize");
    auto reparsed_input = source::parse_form_xml(serialized.value());
    expect(reparsed_input.ok(), "AutoChoiceIncomplete XML must parse after serialization");
    const auto* reparsed_input_control = reparsed_input.value().find_control(model::ObjectId{4});
    expect(reparsed_input_control != nullptr &&
        std::get<bool>(reparsed_input_control->properties().find(
            model::PropertyId::from_name("AutoChoiceIncomplete"))->value),
        "named AutoChoiceIncomplete=true must survive XML round-trip");
    std::string auto_choice_false_source(xml);
    const auto false_input_position = auto_choice_false_source.find("<Position/>");
    auto_choice_false_source.insert(false_input_position + std::string_view{"<Position/>"}.size(),
        "<AutoChoiceIncomplete>false</AutoChoiceIncomplete>");
    auto explicit_false_document = source::parse_form_xml(auto_choice_false_source);
    expect(explicit_false_document.ok(), "named AutoChoiceIncomplete=false must parse");
    auto explicit_false_xml = source::serialize_form_xml(explicit_false_document.value());
    expect(explicit_false_xml.ok() &&
        explicit_false_xml.value().find("<AutoChoiceIncomplete>") == std::string::npos,
        "default AutoChoiceIncomplete=false must serialize as omitted");
    auto reparsed_false = source::parse_form_xml(explicit_false_xml.value());
    expect(reparsed_false.ok(), "AutoChoiceIncomplete=false XML must parse");
    const auto* reparsed_false_control = reparsed_false.value().find_control(model::ObjectId{4});
    expect(reparsed_false_control != nullptr &&
        reparsed_false_control->properties().find(model::PropertyId::from_name("AutoChoiceIncomplete")) == nullptr,
        "named AutoChoiceIncomplete=false must survive XML round-trip");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Bad" ordinaryFormVersion="2.1"><ChildItems><InputField id="2" name="I"><Position/><AutoChoiceIncomplete>maybe</AutoChoiceIncomplete></InputField></ChildItems></Form>)XML").ok(),
        "invalid AutoChoiceIncomplete Boolean must be rejected");
    auto auto_mark_true_source = auto_choice_true_source;
    auto_mark_true_source.replace(auto_mark_true_source.find("<AutoChoiceIncomplete>true</AutoChoiceIncomplete>"),
        std::string_view{"<AutoChoiceIncomplete>true</AutoChoiceIncomplete>"}.size(),
        "<AutoMarkIncomplete>true</AutoMarkIncomplete>");
    auto auto_mark_document = source::parse_form_xml(auto_mark_true_source);
    expect(auto_mark_document.ok(), "named AutoMarkIncomplete=true must parse");
    auto auto_mark_xml = source::serialize_form_xml(auto_mark_document.value());
    expect(auto_mark_xml.ok() && auto_mark_xml.value().find("<AutoMarkIncomplete>true</AutoMarkIncomplete>") != std::string::npos,
        "named AutoMarkIncomplete=true must serialize");
    expect(source::parse_form_xml(auto_mark_xml.value()).ok(), "AutoMarkIncomplete=true XML must round-trip");
    auto auto_mark_false_source = auto_mark_true_source;
    auto_mark_false_source.replace(auto_mark_false_source.find("<AutoMarkIncomplete>true</AutoMarkIncomplete>"),
        std::string_view{"<AutoMarkIncomplete>true</AutoMarkIncomplete>"}.size(),
        "<AutoMarkIncomplete>false</AutoMarkIncomplete>");
    auto auto_mark_false = source::parse_form_xml(auto_mark_false_source);
    expect(auto_mark_false.ok(), "named AutoMarkIncomplete=false must parse");
    auto auto_mark_false_xml = source::serialize_form_xml(auto_mark_false.value());
    expect(auto_mark_false_xml.ok() && auto_mark_false_xml.value().find("<AutoMarkIncomplete>") == std::string::npos,
        "default AutoMarkIncomplete=false must serialize as omitted");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Bad" ordinaryFormVersion="2.1"><ChildItems><InputField id="2" name="I"><Position/><AutoMarkIncomplete>maybe</AutoMarkIncomplete></InputField></ChildItems></Form>)XML").ok(),
        "invalid AutoMarkIncomplete Boolean must be rejected");
    constexpr std::array<std::pair<std::string_view, bool>, 14> input_field_boolean_defaults{{
        {"Wrap", true}, {"ChooseType", true}, {"MarkNegatives", false}, {"ChoiceButton", false},
        {"OpenButton", false}, {"ClearButton", false}, {"SpinButton", false},
        {"ChoiceListButton", false}, {"Transparent", false},
        {"MultiLine", false}, {"ExtendedEdit", false}, {"PasswordMode", false},
        {"AutoMarkIncomplete", false}, {"AutoChoiceIncomplete", false},
    }};
    for (const auto& [name, default_value] : input_field_boolean_defaults) {
        const auto make_property_xml = [&](bool value) {
            return "<Form id=\"1\" name=\"InputFlags\" ordinaryFormVersion=\"2.1\"><Attributes>"
                "<Attribute id=\"2\" name=\"Value\"><TypeDomain><Entry term=\"string\" length=\"64\" variable=\"false\"/>"
                "</TypeDomain></Attribute></Attributes><ChildItems><InputField id=\"3\" name=\"Input\">"
                "<DataPath attributeId=\"2\"/><Position/><" + std::string(name) + ">" + (value ? "true" : "false") +
                "</" + std::string(name) + "></InputField></ChildItems></Form>";
        };
        const auto property_xml = make_property_xml(!default_value);
        auto property_document = source::parse_form_xml(property_xml);
        expect(property_document.ok(), property_document.ok() ? "individual InputField Boolean XML value must parse" :
            std::string(name) + " XML value rejected: " + property_document.diagnostics().front().code + ": " +
                property_document.diagnostics().front().message);
        auto property_serialized = source::serialize_form_xml(property_document.value());
        expect(property_serialized.ok() && property_serialized.value().find("<" + std::string(name) + ">") != std::string::npos,
            "individual non-default InputField Boolean XML value must serialize");
        expect(source::parse_form_xml(property_serialized.value()).ok(),
            "individual InputField Boolean XML value must round-trip");
        const auto default_xml = make_property_xml(default_value);
        auto explicit_default_document = source::parse_form_xml(default_xml);
        expect(explicit_default_document.ok(), "explicit InputField Boolean default must parse");
        auto explicit_default_serialized = source::serialize_form_xml(explicit_default_document.value());
        expect(explicit_default_serialized.ok() &&
                explicit_default_serialized.value().find("<" + std::string(name) + ">") == std::string::npos,
            "explicit InputField Boolean default must normalize to omission");
    }
    expect(serialized.value().find("01234567-89ab-cdef-0123-456789abcdef") != std::string::npos, "UUID output must be lowercase canonical");
    expect(serialized.value().find("styleName=\"StyleFonts.TextFont\"") != std::string::npos &&
               serialized.value().find("mask=") == std::string::npos,
        "named Font style must serialize without the removed mask attribute");
    expect(source::parse_form_xml(serialized.value()).ok(), "canonical typed values must validate and reparse");
}

void test_input_field_tooltip_and_format_xml_round_trip() {
    const auto make_document = [](std::string tool_tip, std::string format, bool with_flags) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "InputStrings";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::string;
        entry.string = model::LengthQualifiers{64, false};
        type.entries.push_back(entry);
        document.add_attribute(model::Attribute{model::ObjectId{1}, "Value", type});
        model::ControlNode input{model::ObjectId{2}, "Input", model::InputFieldPayload{}};
        input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        input.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tool_tip));
        input.properties().set_explicit(model::PropertyId::from_name("Format"), std::move(format));
        if (with_flags) {
            input.properties().set_explicit(model::PropertyId::from_name("AutoMarkIncomplete"), true);
            input.properties().set_explicit(model::PropertyId::from_name("MultiLine"), true);
        }
        document.add_control(std::move(input));
        return document;
    };
    const auto serialized_empty = source::serialize_form_xml(make_document("", "", false));
    expect(serialized_empty.ok() && serialized_empty.value().find("<ToolTip>") == std::string::npos &&
            serialized_empty.value().find("<Format>") == std::string::npos,
        "empty InputField ToolTip and Format must serialize as omitted defaults");
    const auto empty_reparsed = source::parse_form_xml(serialized_empty.value());
    expect(empty_reparsed.ok(), "empty InputField string defaults must parse after serialization");

    const std::string tool_tip = "Подсказка Ω <важно> & \"цитата\"";
    const std::string format = "Л=en_US; NFD=2; ЧРГ='Ω & <>'";
    for (const auto& [tool_tip_value, format_value, with_flags] : {
             std::tuple<std::string, std::string, bool>{tool_tip, "", false},
             std::tuple<std::string, std::string, bool>{"", format, false},
             std::tuple<std::string, std::string, bool>{tool_tip, format, true}}) {
        const auto serialized = source::serialize_form_xml(make_document(tool_tip_value, format_value, with_flags));
        expect(serialized.ok(), "InputField ToolTip and Format must serialize");
        if (!tool_tip_value.empty()) {
            expect(serialized.value().find("<ToolTip>Подсказка Ω &lt;важно&gt; &amp; \"цитата\"</ToolTip>") != std::string::npos,
                "InputField ToolTip XML must escape markup and preserve Unicode");
        }
        if (!format_value.empty()) {
            expect(serialized.value().find("<Format>Л=en_US; NFD=2; ЧРГ='Ω &amp; &lt;&gt;'</Format>") != std::string::npos,
                "InputField Format XML must escape markup and preserve Unicode");
        }
        const auto reparsed = source::parse_form_xml(serialized.value());
        expect(reparsed.ok(), "InputField ToolTip and Format XML must parse after serialization");
        const auto* input = reparsed.value().find_control(model::ObjectId{2});
        expect(input != nullptr, "InputField string XML control must resolve");
        const auto* parsed_tool_tip = input->properties().find(model::PropertyId::from_name("ToolTip"));
        const auto* parsed_format = input->properties().find(model::PropertyId::from_name("Format"));
        expect((tool_tip_value.empty() && parsed_tool_tip == nullptr) ||
                (parsed_tool_tip && std::get<std::string>(parsed_tool_tip->value) == tool_tip_value),
            "InputField ToolTip XML value must round-trip independently");
        expect((format_value.empty() && parsed_format == nullptr) ||
                (parsed_format && std::get<std::string>(parsed_format->value) == format_value),
            "InputField Format XML value must round-trip independently");
        if (with_flags) {
            expect(input->properties().find(model::PropertyId::from_name("AutoMarkIncomplete")) != nullptr &&
                    input->properties().find(model::PropertyId::from_name("MultiLine")) != nullptr,
                "InputField strings must coexist with persisted Boolean flags in XML");
        }
    }
}

void test_input_field_type_link_xml_contract() {
    std::string xml = R"XML(<Form id="1" name="TypeLinks" ordinaryFormVersion="2.1"><Attributes>)XML";
    for (int index = 0; index < 12; ++index) {
        xml += "<Attribute id=\"" + std::to_string(100 + index) + "\" name=\"Object" + std::to_string(index) +
            "\"><TypeDomain><Entry term=\"object\" typeUuid=\"12345678-1234-1234-1234-123456789abc\"/></TypeDomain></Attribute>";
    }
    xml += "</Attributes><ChildItems>";
    for (int index = 0; index < 12; ++index) {
        xml += "<InputField id=\"" + std::to_string(200 + index) + "\" name=\"Input" + std::to_string(index) +
            "\"><DataPath attributeId=\"" + std::to_string(100 + index) + "\"/><TypeLink attributeId=\"" +
            std::to_string(100 + index) + "\"/><Position/><TypeLinkItem>2</TypeLinkItem></InputField>";
    }
    xml += "</ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "twelve typed TypeLinks must parse" : parsed.diagnostics().front().message);
    for (int index = 0; index < 12; ++index) {
        const auto* input = std::get_if<model::InputFieldPayload>(&parsed.value().find_control(model::ObjectId{200 + static_cast<std::uint64_t>(index)})->payload);
        expect(input && input->type_link == model::AttributeRef{model::ObjectId{100 + static_cast<std::uint64_t>(index)}},
            "TypeLink must resolve as a named local Attribute reference");
    }
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<TypeLink attributeId=\"100\"/>") != std::string::npos &&
        serialized.value().find("<DataPath attributeId=\"100\"/>") < serialized.value().find("<TypeLink attributeId=\"100\"/>") &&
        serialized.value().find("<TypeLinkItem>2</TypeLinkItem>") != std::string::npos,
        "InputField TypeLink must serialize after DataPath while TypeLinkItem stays a named property");
    expect(serialized && source::parse_form_xml(serialized.value()).ok(), "TypeLink and TypeLinkItem survive public XML roundtrip");

    const auto invalid = [](std::string_view tail) {
        return source::parse_form_xml("<Form id=\"1\" name=\"Invalid\" ordinaryFormVersion=\"2.1\"><Attributes>"
            "<Attribute id=\"2\" name=\"Source\"><TypeDomain><Entry term=\"string\" length=\"10\" variable=\"false\"/></TypeDomain></Attribute>"
            "</Attributes><ChildItems><InputField id=\"3\" name=\"Input\"><TypeLink attributeId=\"" + std::string(tail) +
            "\"/><Position/></InputField></ChildItems></Form>");
    };
    expect(!invalid("999").ok() && !invalid("2").ok(), "missing and scalar TypeLink sources must be rejected");
    expect(!source::parse_form_xml("<Form id=\"1\" name=\"Unbound\" ordinaryFormVersion=\"2.1\"><ChildItems><InputField id=\"2\" name=\"Input\"><TypeLinkItem>2</TypeLinkItem><Position/></InputField></ChildItems></Form>").ok(),
        "nondefault TypeLinkItem without TypeLink must be rejected");
    expect(!source::parse_form_xml("<Form id=\"1\" name=\"WrongControl\" ordinaryFormVersion=\"2.1\"><ChildItems><CheckBox id=\"2\" name=\"Check\"><TypeLink attributeId=\"3\"/><Position/></CheckBox></ChildItems></Form>").ok(),
        "TypeLink is valid only on InputField");
}

void test_attribute_id_zero_xml_contract() {
    constexpr std::string_view absent_main_xml = R"XML(<Form id="1" name="AttributeZero" ordinaryFormVersion="2.1"><Attributes><Attribute id="0" name="ObjectZero"><TypeDomain><Entry term="object" typeUuid="11111111-1111-1111-1111-111111111111"/></TypeDomain></Attribute><Attribute id="2" name="Positive"><TypeDomain><Entry term="string" length="20"/></TypeDomain></Attribute></Attributes><ChildItems><InputField id="3" name="InputZero"><DataPath attributeId="0"/><TypeLink attributeId="0"/><Position/></InputField><InputField id="4" name="InputPositive"><DataPath attributeId="2"/><Position/></InputField><InputField id="5" name="Unbound"><Position/></InputField></ChildItems></Form>)XML";
    const auto absent_main = source::parse_form_xml(absent_main_xml);
    expect(absent_main.ok(), absent_main ? "" : absent_main.diagnostics().front().message);
    expect(!absent_main.value().form().main_attribute.has_value() &&
        absent_main.value().find_attribute(model::ObjectId{0}) != nullptr,
        "declaring Attribute 0 must not infer a MainAttribute reference");
    const auto* zero_input = absent_main.value().find_control(model::ObjectId{3});
    const auto* zero_payload = zero_input ? std::get_if<model::InputFieldPayload>(&zero_input->payload) : nullptr;
    expect(zero_payload && zero_input->data_path &&
        zero_input->data_path->attribute == model::AttributeRef{model::ObjectId{0}} &&
        zero_payload->type_link == model::AttributeRef{model::ObjectId{0}},
        "DataPath and TypeLink must retain explicit Attribute ID 0");
    expect(absent_main.value().find_control(model::ObjectId{5}) &&
        !absent_main.value().find_control(model::ObjectId{5})->data_path,
        "empty root DataPath must stay absent instead of binding to Attribute ID 0");
    const auto serialized_absent = source::serialize_form_xml(absent_main.value());
    expect(serialized_absent.ok() && serialized_absent.value().find("<MainAttribute") == std::string::npos &&
        serialized_absent.value().find("<Attribute id=\"0\"") != std::string::npos &&
        serialized_absent.value().find("<DataPath attributeId=\"0\"/>") != std::string::npos &&
        serialized_absent.value().find("<TypeLink attributeId=\"0\"/>") != std::string::npos,
        "XML writer must preserve ID 0 references and preserve absent MainAttribute");
    const auto reparsed_absent = source::parse_form_xml(serialized_absent.value());
    expect(reparsed_absent.ok() && !reparsed_absent.value().form().main_attribute.has_value(),
        "absent MainAttribute must remain absent after named XML round trip");

    auto explicit_main_xml = std::string(absent_main_xml);
    explicit_main_xml.replace(explicit_main_xml.find("<Attributes>"), std::string("<Attributes>").size(),
        "<MainAttribute attributeId=\"0\"/><Attributes>");
    const auto explicit_main = source::parse_form_xml(explicit_main_xml);
    expect(explicit_main.ok() && explicit_main.value().form().main_attribute ==
        model::AttributeRef{model::ObjectId{0}},
        "explicit MainAttribute ID 0 must remain present");
    const auto explicit_main_serialized = source::serialize_form_xml(explicit_main.value());
    expect(explicit_main_serialized.ok() && explicit_main_serialized.value().find(
        "<MainAttribute attributeId=\"0\"/>") != std::string::npos,
        "explicit MainAttribute ID 0 must serialize as a named reference");

    const auto duplicate_zero = source::parse_form_xml(
        "<Form id=\"1\" name=\"DuplicateZero\" ordinaryFormVersion=\"2.1\"><Attributes>"
        "<Attribute id=\"0\" name=\"First\"><TypeDomain><Entry term=\"string\" length=\"1\"/></TypeDomain></Attribute>"
        "<Attribute id=\"0\" name=\"Second\"><TypeDomain><Entry term=\"string\" length=\"1\"/></TypeDomain></Attribute>"
        "</Attributes></Form>");
    expect(!duplicate_zero.ok(), "duplicate Attribute ID 0 must be rejected");
    expect(!source::parse_form_xml(
        "<Form id=\"1\" name=\"DanglingZero\" ordinaryFormVersion=\"2.1\"><MainAttribute attributeId=\"0\"/></Form>").ok(),
        "MainAttribute ID 0 without a declared Attribute 0 must be rejected");
    expect(!source::parse_form_xml(
        "<Form id=\"1\" name=\"DanglingDataPathZero\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<LabelDecoration id=\"2\" name=\"Label\"><DataPath attributeId=\"0\"/><Position/></LabelDecoration>"
        "</ChildItems></Form>").ok(),
        "DataPath Attribute ID 0 without a declared Attribute 0 must be rejected");
    expect(!source::parse_form_xml(
        "<Form id=\"1\" name=\"ControlZero\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Button id=\"0\" name=\"Invalid\"><Position/></Button>"
        "</ChildItems></Form>").ok(),
        "control ID 0 must remain invalid in public XML");
}

void test_spreadsheet_serialization_version_xml() {
    using Version = model::SpreadsheetDocumentFieldSerializationVersion;
    const auto xml_for = [](std::string_view attributes, std::string_view kind = "SpreadsheetDocumentField") {
        return std::string("<Form id=\"1\" name=\"SheetVersion\" ordinaryFormVersion=\"2.1\"><ChildItems><") +
            std::string(kind) + " id=\"2\" name=\"Sheet\"" + std::string(attributes) + "><Position/></" +
            std::string(kind) + "></ChildItems></Form>";
    };
    for (const auto& [attribute, version] : std::array{
        std::pair{std::string_view{}, Version::v18},
        std::pair{std::string_view{" serializationVersion=\"14\""}, Version::v14},
        std::pair{std::string_view{" serializationVersion=\"16\""}, Version::v16},
        std::pair{std::string_view{" serializationVersion=\"18\""}, Version::v18}}) {
        const auto parsed = source::parse_form_xml(xml_for(attribute));
        expect(parsed.ok() && std::get<model::SpreadsheetDocumentFieldPayload>(parsed.value().find_control(model::ObjectId{2})->payload).
            serialization_version == version, "XML должен читать версию компонента и default18 прежнего XML");
        const auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok() && (version == Version::v18
            ? serialized.value().find("serializationVersion=") == std::string::npos
            : serialized.value().find("serializationVersion=\"" + std::to_string(static_cast<unsigned>(version)) + "\"") != std::string::npos),
            "XML должен сохранять старое поколение и опускать default18");
    }
    for (const auto version : {"0", "15", "17", "19", "14.0", "014", ""})
        expect(!source::parse_form_xml(xml_for(" serializationVersion=\"" + std::string(version) + "\"")),
            "Атрибут должен принимать только точные версии 14, 16 и 18");
    expect(!source::parse_form_xml(xml_for(" serializationVersion=\"14\"", "Button")),
        "Версию оболочки SSF нельзя задавать другому контролу");
    expect(!source::parse_form_xml(xml_for(" serializationVersion=\"14\" serializationVersion=\"16\"")),
        "Повторенный атрибут версии должен отклоняться");
}

void test_spreadsheet_document_cells_xml_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Spreadsheet";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode field{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    auto& payload = std::get<model::SpreadsheetDocumentFieldPayload>(field.payload);
    auto& cells = payload.cells;
    cells = {{1, 3, " Ω <текст> & \"цитата\" ", std::nullopt}, {2, 1, "", std::nullopt}};
    const auto typed_cell = [](std::uint32_t row, std::uint32_t column, model::TypeDomainTerm term,
                               model::PropertyValue value) {
        model::TypeDomainEntry entry;
        entry.term = term;
        if (term == model::TypeDomainTerm::string) entry.string = {100, true};
        if (term == model::TypeDomainTerm::numeric) entry.numeric = {15, 3, false};
        if (term == model::TypeDomainTerm::date) entry.date = {true, true};
        return model::SpreadsheetDocumentCell{row, column, std::string{},
            model::SpreadsheetDocumentCellValue{model::TypeDomainPatternValue{{entry}}, std::move(value)}};
    };
    cells.push_back(typed_cell(4, 1, model::TypeDomainTerm::string, std::string("Unicode Привет 世界")));
    cells.push_back(typed_cell(4, 2, model::TypeDomainTerm::numeric, model::DecimalValue{"-12.375"}));
    cells.push_back(typed_cell(4, 3, model::TypeDomainTerm::boolean, false));
    cells.push_back(typed_cell(4, 4, model::TypeDomainTerm::date, model::DateValue{"2026-10-04T12:30:45"}));
    auto generic_string = typed_cell(6, 1, model::TypeDomainTerm::string, std::string("x"));
    generic_string.typed_value->type.entries.front().string.length = 37;
    cells.push_back(std::move(generic_string));
    auto generic_number = typed_cell(6, 2, model::TypeDomainTerm::numeric, model::DecimalValue{"1.2345"});
    generic_number.typed_value->type.entries.front().numeric = {12, 4, false};
    cells.push_back(std::move(generic_number));
    for (std::size_t index = 2; index < cells.size(); ++index) {
        cells[index].control.emplace();
        cells[index].control->properties.set_explicit(model::PropertyId::from_name("ReadOnly"), index % 2 == 0);
    }
    document.add_control(std::move(field));
    const auto serialized = source::serialize_form_xml(document);
    expect(serialized.ok(), serialized.ok() ? "" : serialized.diagnostics().front().message);
    expect(serialized.value().find("<Cell row=\"1\" column=\"3\">") != std::string::npos &&
        serialized.value().find("Ω &lt;текст&gt; &amp; \"цитата\"") != std::string::npos,
        "named Spreadsheet Document cell must preserve Unicode, whitespace, and XML escaping");
    expect(serialized.value().find("<Cell row=\"2\" column=\"1\">") != std::string::npos &&
        serialized.value().find("<Text></Text>") != std::string::npos,
        "explicitly empty Spreadsheet Document cell must remain present");
    expect(serialized.value().find("<ContainsValue>true</ContainsValue>") != std::string::npos &&
        serialized.value().find("<Entry term=\"string\" length=\"100\"/>") != std::string::npos &&
        serialized.value().find("<Value>Unicode Привет 世界</Value>") != std::string::npos &&
        serialized.value().find("<Value>-12.375</Value>") != std::string::npos &&
        serialized.value().find("<Value>false</Value>") != std::string::npos &&
        serialized.value().find("<Value>2026-10-04T12:30:45</Value>") != std::string::npos,
        "typed Spreadsheet Document cells must serialize named values and qualifiers");
    expect(serialized.value().find("<Control type=\"InputField\">") != std::string::npos &&
        serialized.value().find("<ReadOnly>false</ReadOnly>") != std::string::npos,
        "Cell.Control must preserve the named editor and explicit false property");
    for (const std::string_view bad : {"<Control type=\"CheckBox\"><ReadOnly>true</ReadOnly></Control>",
        "<Control type=\"InputField\"><Enabled>false</Enabled></Control>",
        "<Control type=\"InputField\"><ReadOnly>true</ReadOnly><ReadOnly>false</ReadOnly></Control>",
        "<Control type=\"InputField\"><ReadOnly>2</ReadOnly></Control>",
        "<Control type=\"InputField\" raw=\"x\"/>"}) {
        std::string invalid = serialized.value();
        const auto begin = invalid.find("<Control type=\"InputField\">");
        const auto end = invalid.find("</Control>", begin) + std::string_view("</Control>").size();
        invalid.replace(begin, end - begin, bad);
        expect(!source::parse_form_xml(invalid), "XML parser must reject unsupported Cell.Control kind, property, Boolean, duplicates, and attributes");
    }
    const auto parsed = source::parse_form_xml(serialized.value());
    expect(parsed.ok(), parsed.ok() ? "" : parsed.diagnostics().front().message);
    const auto* restored_control = parsed.value().find_control(model::ObjectId{2});
    expect(restored_control != nullptr, "SpreadsheetDocumentField must resolve after XML parsing");
    const auto* restored = std::get_if<model::SpreadsheetDocumentFieldPayload>(&restored_control->payload);
    expect(restored && restored->cells.size() == 8 && restored->cells[0].row == 1 &&
        restored->cells[0].column == 3 && std::get<std::string>(restored->cells[0].text) == " Ω <текст> & \"цитата\" " &&
        restored->cells[1].row == 2 && restored->cells[1].column == 1 && std::get<std::string>(restored->cells[1].text).empty(),
        "named Spreadsheet Document cells and explicit empty text must round-trip");
    expect(restored && restored->cells[2].typed_value &&
        std::get<std::string>(restored->cells[2].typed_value->value) == "Unicode Привет 世界" &&
        restored->cells[2].typed_value->type.entries.front().string.length == 100 &&
        restored->cells[3].typed_value && std::get<model::DecimalValue>(restored->cells[3].typed_value->value).canonical == "-12.375" &&
        restored->cells[4].typed_value && !std::get<bool>(restored->cells[4].typed_value->value) &&
        restored->cells[5].typed_value &&
        std::get<model::DateValue>(restored->cells[5].typed_value->value).canonical == "2026-10-04T12:30:45",
        "typed Spreadsheet Document values and qualifiers must survive XML round-trip");
    expect(restored && restored->cells[6].typed_value &&
        restored->cells[6].typed_value->type.entries.front().string.length == 37 &&
        restored->cells[7].typed_value &&
        restored->cells[7].typed_value->type.entries.front().numeric == model::NumericQualifiers{12, 4, false},
        "Spreadsheet Document must retain supported non-default String and Number qualifiers");

    const auto wrap_cells = [](std::string_view items) {
        return std::string("<Form id=\"1\" name=\"Spreadsheet\" ordinaryFormVersion=\"2.1\"><ChildItems><SpreadsheetDocumentField id=\"2\" name=\"Sheet\"><Position/><Document>") +
            std::string(items) + "</Document></SpreadsheetDocumentField></ChildItems></Form>";
    };
    for (const auto invalid : {
        "<Cell row=\"0\" column=\"1\"><Text/></Cell>",
        "<Cell row=\"1\" column=\"4294967296\"><Text/></Cell>",
        "<Cell row=\"1\" column=\"1\"><Text>A</Text></Cell><Cell row=\"1\" column=\"1\"><Text>B</Text></Cell>",
        "<Cell row=\"1\" column=\"1\"/>",
        "<Cell row=\"1\" column=\"1\"><Text><Nested/></Text></Cell>",
        "<Cell row=\"1\" column=\"1\"><ContainsValue>false</ContainsValue><ValueType><Entry term=\"boolean\"/></ValueType><Value>false</Value></Cell>",
        "<Cell row=\"1\" column=\"1\"><Text>x</Text><ContainsValue>true</ContainsValue><ValueType><Entry term=\"boolean\"/></ValueType><Value>false</Value></Cell>",
        "<Cell row=\"1\" column=\"1\"><ContainsValue>true</ContainsValue><ValueType><Entry term=\"binary\"/></ValueType><Value>x</Value></Cell>"}) {
        expect(!source::parse_form_xml(wrap_cells(invalid)).ok(),
            "invalid Spreadsheet Document coordinates or Cell content must be rejected");
    }
    const auto unordered = source::parse_form_xml(wrap_cells(
        "<Cell row=\"2\" column=\"1\"><Text>B</Text></Cell><Cell row=\"1\" column=\"3\"><Text>A</Text></Cell>"));
    expect(unordered.ok(), "unordered Spreadsheet XML cells must parse");
    const auto normalized = source::serialize_form_xml(unordered.value());
    expect(normalized.ok(), "unordered Spreadsheet XML cells must serialize");
    expect(normalized.value().find("<Cell row=\"1\" column=\"3\">") <
        normalized.value().find("<Cell row=\"2\" column=\"1\">"),
        "Spreadsheet XML cells must serialize in row and column order");

    const auto authored_view_settings = source::parse_form_xml(
        "<Form id=\"1\" name=\"Spreadsheet\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<SpreadsheetDocumentField id=\"2\" name=\"Sheet\"><Position/>"
        "<ViewSettings currentRow=\"2\" currentColumn=\"3\"><SelectionArea row=\"1\" "
        "column=\"1\" endRow=\"1\" endColumn=\"1\"/></ViewSettings>"
        "</SpreadsheetDocumentField></ChildItems></Form>");
    expect(!authored_view_settings.ok(),
        "unpersisted SpreadsheetDocumentField ViewSettings must be rejected as authored XML");
}

void test_spreadsheet_document_localized_text_xml_round_trip() {
    const auto wrap = [](std::string_view cells) {
        return std::string("<Form id=\"1\" name=\"LocalizedSpreadsheet\" ordinaryFormVersion=\"2.1\"><ChildItems><SpreadsheetDocumentField id=\"2\" name=\"Sheet\"><Position/><Document>") +
            std::string(cells) + "</Document></SpreadsheetDocumentField></ChildItems></Form>";
    };
    std::string cells;
    for (unsigned row = 1; row <= 16; ++row) {
        cells += "<Cell row=\"" + std::to_string(row) + "\" column=\"1\"><Text>";
        if (row % 4 == 1) cells += " Plain Ω &amp; &lt;text&gt; ";
        else if (row % 4 == 2) cells += "<DefaultText>Reference &amp; neutral</DefaultText>";
        else if (row % 4 == 3) cells += "<DefaultText>Reference</DefaultText><Item language=\"ru\">Ссылка</Item><Item language=\"en\">Link 世界</Item>";
        else cells += "<DefaultText>Hash neutral</DefaultText><Item language=\"\">Empty language</Item><Item language=\"ru\">Ссылка</Item>";
        cells += "</Text></Cell>";
    }
    auto parsed = source::parse_form_xml(wrap(cells));
    expect(parsed.ok(), parsed.ok() ? "plain, neutral and multilingual spreadsheet text must parse" : parsed.diagnostics().front().message);
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Text> Plain Ω &amp; &lt;text&gt; </Text>") != std::string::npos &&
        serialized.value().find("<DefaultText>Reference &amp; neutral</DefaultText>") != std::string::npos &&
        serialized.value().find("language=\"#\"") == std::string::npos,
        "plain XML contract and named neutral text must survive serialization without platform language markers");
    auto restored = source::parse_form_xml(serialized.value());
    expect(restored.ok(), "localized Spreadsheet Cell Text must reparse");
    const auto& sheet = std::get<model::SpreadsheetDocumentFieldPayload>(restored.value().find_control(model::ObjectId{2})->payload);
    expect(sheet.cells.size() == 16, "all sixteen plain, neutral, empty-language and multilingual spreadsheet cells must survive");
    for (unsigned index = 0; index < 16; ++index) {
        const auto& text = sheet.cells[index].text;
        if (index % 4 == 0)
            expect(std::get<std::string>(text) == " Plain Ω & <text> ", "plain spreadsheet text must retain whitespace and escaped Unicode");
        else if (index % 4 == 1)
            expect(std::get<model::LocalizedStringValue>(text) == model::LocalizedStringValue{{{"#", "Reference & neutral"}}},
                "neutral spreadsheet text must retain its localized value identity");
        else if (index % 4 == 2) expect(std::get<model::LocalizedStringValue>(text) ==
            model::LocalizedStringValue{{{"#", "Reference"}, {"ru", "Ссылка"}, {"en", "Link 世界"}}},
            "multilingual spreadsheet text must retain every named language and the neutral value");
        else expect(std::get<model::LocalizedStringValue>(text) ==
            model::LocalizedStringValue{{{"#", "Hash neutral"}, {"", "Empty language"}, {"ru", "Ссылка"}}},
            "empty native language must remain distinct from the hash neutral entry and Russian text");
    }
    auto repeated = source::serialize_form_xml(restored.value());
    expect(repeated.ok() && repeated.value() == serialized.value(), "localized spreadsheet text must have a canonical XML round-trip");
    for (const std::string_view text : {
        "<Text>mixed<Item language=\"en\">Text</Item></Text>",
        "<Text><Item language=\"en\">A</Item><Item language=\"en\">B</Item></Text>",
        "<Text><Item language=\"\">A</Item><Item language=\"\">B</Item></Text>",
        "<Text><DefaultText>A</DefaultText><DefaultText>B</DefaultText></Text>",
        "<Text><Item language=\"#\">A</Item></Text>", "<Text><Item>A</Item></Text>",
        "<Text raw=\"1\">A</Text>", "<Text><Item language=\"en\" raw=\"1\">A</Item></Text>",
        "<Text><Item language=\"en\"><Nested/></Item></Text>", "<Text><Unknown/></Text>",
        "<Text><DefaultText language=\"en\">A</DefaultText></Text>"})
        expect(!source::parse_form_xml(wrap("<Cell row=\"1\" column=\"1\">" + std::string(text) + "</Cell>")),
            "malformed localized spreadsheet text must reject mixed content, duplicates, unknown attributes and children");
    for (unsigned invalid_kind = 0; invalid_kind < 2; ++invalid_kind) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "InvalidSpreadsheetText";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument invalid(std::move(form));
        model::ControlNode control{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
        auto& payload = std::get<model::SpreadsheetDocumentFieldPayload>(control.payload);
        model::SpreadsheetDocumentCell cell{1, 1, model::DecimalValue{"1"}, std::nullopt};
        if (invalid_kind == 1) {
            cell.text = model::LocalizedStringValue{};
            cell.typed_value = model::SpreadsheetDocumentCellValue{
                model::TypeDomainPatternValue{{model::TypeDomainEntry{.term = model::TypeDomainTerm::boolean}}}, false};
        }
        payload.cells.push_back(std::move(cell));
        invalid.add_control(std::move(control));
        expect(!invalid.validate().ok(), "model must reject non-text Cell.Text and localized values on typed cells");
    }
}

void test_spreadsheet_document_picture_paths() {
    const auto xml_for = [](std::string_view path, std::string_view format) {
        return std::string("<Form id=\"1\" name=\"SpreadsheetPictures\" ordinaryFormVersion=\"2.1\"><PictureAssets><PictureAsset id=\"3\" relativePath=\"") +
            std::string(path) + "\" format=\"" + std::string(format) + "\"/></PictureAssets><ChildItems><SpreadsheetDocumentField id=\"2\" name=\"Sheet\"><Position/><Document><Cell row=\"1\" column=\"1\"><Text>Picture</Text><Format><Picture>3</Picture></Format></Cell></Document></SpreadsheetDocumentField></ChildItems></Form>";
    };
    for (const auto& [extension, format] : {std::pair{"gif", "gif"}, std::pair{"png", "png"},
        std::pair{"jpg", "jpeg"}, std::pair{"jpeg", "jpeg"}, std::pair{"bmp", "bmp"}, std::pair{"ico", "ico"}}) {
        const std::string path = "Items/Sheet/Document/Image1/Picture." + std::string(extension);
        auto parsed = source::parse_form_xml(xml_for(path, format));
        expect(parsed.ok(), "named Spreadsheet Document picture asset paths must parse for every existing picture format");
        auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok() && serialized.value().find(path) != std::string::npos,
            "named Spreadsheet Document picture asset paths must survive serialization");
    }
    for (const std::string_view path : {"Items/Sheet/Document/../Picture.gif", "Items/Sheet/Document/Image1/../Picture.gif",
        "Items/Sheet/Document/Image1/Extra/Picture.gif", "Items/Sheet/Document/Image1/Buttons/Nested/Picture.gif",
        "Items/Sheet/Buttons/Nested/Document/Image1/Picture.gif", "Items/Sheet/Document/Picture.gif",
        "Items/Sheet/Other/Image1/Picture.gif", "Items/Sheet/Document//Picture.gif", "/Items/Sheet/Document/Image1/Picture.gif",
        "Items/Sheet/Document/Image1/Picture.png", "Items\\Sheet\\Document\\Image1\\Picture.gif"})
        expect(!source::parse_form_xml(xml_for(path, "gif")),
            "Spreadsheet Document picture paths must reject traversal, extra nesting, unknown collections and mismatched filenames");
}

void test_spreadsheet_language_settings_xml_round_trip() {
    const auto wrap = [](std::string_view body) {
        return std::string("<Form id=\"1\" name=\"SpreadsheetLanguages\" ordinaryFormVersion=\"2.1\"><ChildItems><SpreadsheetDocumentField id=\"2\" name=\"Sheet\"><Position/><Document>") +
            std::string(body) + "</Document></SpreadsheetDocumentField></ChildItems></Form>";
    };
    const model::SpreadsheetLanguageSettings factory;
    expect(factory.current_language == "ru" && factory.default_language == "ru" &&
        factory.languages == std::vector<model::SpreadsheetLanguageInfo>{{"ru", "Русский", "Русский"}},
        "spreadsheet language factory defaults must reflect the named native language settings");
    const std::string factory_languages = "<Language id=\"ru\" code=\"Русский\" description=\"Русский\"/>";
    auto defaults = source::parse_form_xml(wrap("<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\">" + factory_languages + "</LanguageSettings>"));
    expect(defaults.ok(), "explicit factory LanguageSettings must parse");
    auto default_output = source::serialize_form_xml(defaults.value());
    expect(default_output.ok() && default_output.value().find("<LanguageSettings") == std::string::npos,
        "factory language settings must be omitted from canonical public XML");
    std::string xml = "<Form id=\"1\" name=\"SpreadsheetLanguages\" ordinaryFormVersion=\"2.1\"><ChildItems>";
    std::vector<model::SpreadsheetLanguageSettings> expected;
    for (unsigned index = 0; index < 12; ++index) {
        model::SpreadsheetLanguageSettings settings;
        settings.current_language = "current" + std::to_string(index);
        settings.default_language = index == 7 ? "" : "default" + std::to_string(index);
        settings.languages = {{"", "Default code", ""}, {"ru", "Русский", "Описание Ω & <текст>"}};
        expected.push_back(settings);
        const auto id = std::to_string(index + 2);
        xml += "<SpreadsheetDocumentField id=\"" + id + "\" name=\"Sheet" + id + "\"><Position/><Document><LanguageSettings currentLanguage=\"" +
            settings.current_language + "\" defaultLanguage=\"" + settings.default_language + "\">";
        xml += "<Language id=\"\" code=\"Default code\"/><Language id=\"ru\" code=\"Русский\" description=\"Описание Ω &amp; &lt;текст&gt;\"/>"
            "</LanguageSettings></Document></SpreadsheetDocumentField>";
    }
    xml += "</ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.ok() ? "all twelve LanguageSettings variants must parse" : parsed.diagnostics().front().message);
    auto output = source::serialize_form_xml(parsed.value());
    expect(output.ok() && output.value().find("<LanguageSettings") != std::string::npos &&
        output.value().find("<DesignAlias>") == std::string::npos,
        "public LanguageSettings must contain only platform XDTO properties");
    auto restored = source::parse_form_xml(output.value());
    expect(restored.ok(), "LanguageSettings must reparse after canonical serialization");
    for (unsigned index = 0; index < 12; ++index) {
        const auto& settings = std::get<model::SpreadsheetDocumentFieldPayload>(restored.value().find_control(model::ObjectId{index + 2})->payload).language_settings;
        expect(settings == expected[index], "all twelve language settings must preserve strings and ordered language triplets");
    }
    auto repeated = source::serialize_form_xml(restored.value());
    expect(repeated.ok() && repeated.value() == output.value(), "LanguageSettings canonical XML must be stable");
    for (const std::string_view body : {
        "<LanguageSettings currentLanguage=\"ru\"/>", "<LanguageSettings defaultLanguage=\"ru\"/>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\" raw=\"1\"/>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\"><DesignAlias>1</DesignAlias></LanguageSettings>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\"><Language id=\"ru\" code=\"Russian\"/><DesignAlias>1</DesignAlias></LanguageSettings>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\"><Language code=\"Russian\"/></LanguageSettings>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\"><Language id=\"ru\"/></LanguageSettings>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\"><Language id=\"ru\" code=\"Russian\" raw=\"1\"/></LanguageSettings>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\"><Language id=\"ru\" code=\"Russian\">text</Language></LanguageSettings>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\">text</LanguageSettings>",
        "<LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\"/><LanguageSettings currentLanguage=\"en\" defaultLanguage=\"en\"/>",
        "<DefaultFormat/><LanguageSettings currentLanguage=\"ru\" defaultLanguage=\"ru\"/>"})
        expect(!source::parse_form_xml(wrap(body)), "LanguageSettings must reject missing attributes, unknown fields and invalid order");
    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "InvalidLanguageSettings";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument invalid(std::move(invalid_form));
    model::ControlNode invalid_control{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    auto& invalid_settings = std::get<model::SpreadsheetDocumentFieldPayload>(invalid_control.payload).language_settings;
    invalid_settings.languages[0].description = std::string(1, static_cast<char>(1));
    invalid.add_control(std::move(invalid_control));
    expect(!invalid.validate().ok(), "LanguageSettings model must reject strings that cannot be represented in public XML");
}

void test_spreadsheet_document_formats_xml_round_trip() {
    namespace mm = model::metamodel;
    std::string format_xml;
    std::size_t property_count = 0;
    std::string boolean_name;
    for (const auto& property : mm::spreadsheet_format_properties()) {
        std::string value;
        switch (property.value_codec) {
            case mm::ValueCodec::boolean: value = "false"; boolean_name = property.xml_name; break;
            case mm::ValueCodec::decimal: value = "2.5"; break;
            case mm::ValueCodec::integer:
            case mm::ValueCodec::integer32: value = "2"; break;
            case mm::ValueCodec::string: value = "Named format"; break;
            case mm::ValueCodec::localized_string: value = "<DefaultText>Neutral</DefaultText><Item language=\"ru\">Формат</Item>"; break;
            case mm::ValueCodec::type_domain: value = "<Entry term=\"boolean\"/>"; break;
            case mm::ValueCodec::uuid: value = "12345678-1234-1234-1234-123456789abc"; break;
            case mm::ValueCodec::picture: value = "standardName=\"PictureLib.Write\""; break;
            case mm::ValueCodec::enumeration:
                value = "type=\"" + std::string(property.enumeration_type) + "\" member=\"" + std::string(property.enumeration_members.front()) + "\"";
                break;
            case mm::ValueCodec::line: value = "type=\"SpreadsheetDocumentCellLineType\" member=\"Solid\" width=\"1\" gap=\"false\""; break;
            case mm::ValueCodec::color: value = "kind=\"absolute\" red=\"31\" green=\"127\" blue=\"223\""; break;
            case mm::ValueCodec::font: value = "kind=\"absolute\" faceName=\"Arial\" height=\"11\" bold=\"true\""; break;
            default: continue;
        }
        const std::string name(property.xml_name);
        format_xml += "<" + name;
        if (property.value_codec == mm::ValueCodec::color || property.value_codec == mm::ValueCodec::font ||
            property.value_codec == mm::ValueCodec::enumeration || property.value_codec == mm::ValueCodec::line ||
            property.value_codec == mm::ValueCodec::picture)
            format_xml += " " + value + "/>";
        else format_xml += ">" + value + "</" + name + ">";
        ++property_count;
    }
    expect(property_count == mm::spreadsheet_format_properties().size() && property_count == 48 && !boolean_name.empty(),
        "all 48 spreadsheet format descriptors must have typed XML round-trip coverage");
    std::string typed_format_xml = format_xml;
    const std::string contains_value_false = "<ContainsValue>false</ContainsValue>";
    typed_format_xml.replace(typed_format_xml.find(contains_value_false), contains_value_false.size(), "<ContainsValue>true</ContainsValue>");
    const auto wrap = [](std::string_view items) {
        return std::string("<Form id=\"1\" name=\"SpreadsheetFormats\" ordinaryFormVersion=\"2.1\"><ChildItems><SpreadsheetDocumentField id=\"2\" name=\"Sheet\"><Position/><Document>") +
            std::string(items) + "</Document></SpreadsheetDocumentField></ChildItems></Form>";
    };
    std::string xml = "<Form id=\"1\" name=\"SpreadsheetFormats\" ordinaryFormVersion=\"2.1\"><ChildItems>";
    for (unsigned owner = 0; owner < 12; ++owner) {
        const auto id = std::to_string(owner + 2);
        xml += "<SpreadsheetDocumentField id=\"" + id + "\" name=\"Sheet" + id + "\"><Position/><Document>" +
            "<DefaultFormat>" + format_xml + "</DefaultFormat><DefaultColumnFormat>" + format_xml + "</DefaultColumnFormat>" +
            "<Row index=\"7\"><Format>" + format_xml + "</Format></Row><Row index=\"2\"><Format/></Row>" +
            "<Column index=\"9\"><Format>" + format_xml + "</Format></Column><Column index=\"3\"><Format/></Column>" +
            "<Cell row=\"7\" column=\"9\"><Text>Styled</Text><Format>" + format_xml + "</Format></Cell>" +
            "<Cell row=\"8\" column=\"9\"><ContainsValue>true</ContainsValue><ValueType><Entry term=\"boolean\"/></ValueType><Value>false</Value><Control type=\"InputField\"><ReadOnly>false</ReadOnly></Control><Format>" + typed_format_xml + "</Format></Cell>" +
            "<Cell row=\"9\" column=\"9\"><Text>Inherited</Text></Cell></Document></SpreadsheetDocumentField>";
    }
    xml += "</ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.ok() ? "named spreadsheet formats must parse" : parsed.diagnostics().front().message);
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), serialized.ok() ? "named spreadsheet formats must serialize" : serialized.diagnostics().front().message);
    expect(serialized.value().find("<" + boolean_name + ">false</" + boolean_name + ">") != std::string::npos,
        "explicit false format properties must not be omitted as defaults");
    expect(serialized.value().find("<Row index=\"2\">") < serialized.value().find("<Row index=\"7\">") &&
        serialized.value().find("<Column index=\"3\">") < serialized.value().find("<Column index=\"9\">"),
        "row and column format records must serialize in index order");
    auto restored = source::parse_form_xml(serialized.value());
    expect(restored.ok(), restored.ok() ? "named spreadsheet formats must reparse" : restored.diagnostics().front().message);
    for (unsigned owner = 0; owner < 12; ++owner) {
        const auto* control = restored.value().find_control(model::ObjectId{owner + 2});
        const auto& sheet = std::get<model::SpreadsheetDocumentFieldPayload>(control->payload);
        expect(sheet.rows.size() == 2 && sheet.columns.size() == 2 && sheet.cells.size() == 3 &&
            sheet.rows[0].index == 2 && sheet.rows[0].format.properties.size() == 0 &&
            sheet.columns[0].index == 3 && sheet.columns[0].format.properties.size() == 0,
            "explicit empty row and column format records must survive across all twelve control owners");
        expect(sheet.default_format.properties.size() == property_count &&
            sheet.default_column_format == sheet.default_format && sheet.rows[1].format == sheet.default_format &&
            sheet.columns[1].format == sheet.default_format && sheet.cells[0].format == sheet.default_format &&
            sheet.cells[2].format.properties.size() == 0,
            "default, row, column and text-cell formats must preserve exact typed explicit properties and inheritance");
        auto typed_format = sheet.default_format;
        typed_format.properties.set_explicit(model::PropertyId::from_name("ContainsValue"), true);
        expect(sheet.cells[1].format == typed_format,
            "typed-cell format must retain matching explicit ContainsValue and ValueType with every other typed property");
    }
    auto repeated = source::serialize_form_xml(restored.value());
    expect(repeated.ok() && repeated.value() == serialized.value(), "spreadsheet format XML must be canonical after round-trip");
    for (unsigned invalid_kind = 0; invalid_kind < 3; ++invalid_kind) {
        model::Form invalid_form;
        invalid_form.id = model::ObjectId{1};
        invalid_form.name = "InvalidSpreadsheetFormat";
        invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
        model::ControlNode invalid_control = *restored.value().find_control(model::ObjectId{2});
        auto& invalid_sheet = std::get<model::SpreadsheetDocumentFieldPayload>(invalid_control.payload);
        if (invalid_kind == 0) invalid_sheet.rows[1].index = invalid_sheet.rows[0].index;
        else if (invalid_kind == 1) invalid_sheet.columns[0].index = 0;
        else invalid_sheet.default_format.properties.set_explicit(model::PropertyId::from_name("Font"), false);
        invalid_document.add_control(std::move(invalid_control));
        expect(!invalid_document.validate().ok(),
            "model must independently reject duplicate row indices, zero column indices and wrong format value codecs");
    }
    const std::string boolean = "<" + boolean_name + ">false</" + boolean_name + ">";
    const std::vector<std::string> invalid{
        "<Row index=\"0\"><Format/></Row>", "<Column index=\"0\"><Format/></Column>",
        "<Row index=\"1\"><Format/></Row><Row index=\"1\"><Format/></Row>",
        "<Column index=\"1\"><Format/></Column><Column index=\"1\"><Format/></Column>",
        "<Row index=\"1\" raw=\"1\"><Format/></Row>", "<Row index=\"1\"/>",
        "<Row index=\"1\"><Format/><Format/></Row>", "<Column index=\"1\"><Format raw=\"1\"/></Column>",
        "<DefaultFormat><Unknown>1</Unknown></DefaultFormat>",
        "<DefaultFormat>" + boolean + boolean + "</DefaultFormat>",
        "<DefaultFormat><" + boolean_name + " raw=\"1\">false</" + boolean_name + "></DefaultFormat>",
        "<DefaultFormat>unsupported</DefaultFormat>",
        "<DefaultFormat><HorizontalAlignment type=\"HorizontalAlign\" member=\"Invalid\"/></DefaultFormat>",
        "<DefaultFormat><HorizontalAlignment type=\"WrongType\" member=\"Left\"/></DefaultFormat>",
        "<DefaultFormat><TextColor kind=\"absolute\" raw=\"1\"/></DefaultFormat>",
        "<DefaultFormat><Picture>999</Picture></DefaultFormat>",
        "<DefaultFormat><Format><Item language=\"ru\" raw=\"1\">x</Item></Format></DefaultFormat>",
        "<Cell row=\"1\" column=\"1\"><Text>x</Text><Format><ContainsValue>true</ContainsValue></Format></Cell>",
        "<Cell row=\"1\" column=\"1\"><ContainsValue>true</ContainsValue><ValueType><Entry term=\"boolean\"/></ValueType><Format><ContainsValue>false</ContainsValue></Format></Cell>",
        "<Cell row=\"1\" column=\"1\"><ContainsValue>true</ContainsValue><ValueType><Entry term=\"boolean\"/></ValueType><Format><ValueType><Entry term=\"string\"/></ValueType></Format></Cell>",
        "<DefaultFormat/><DefaultFormat/>", "<DefaultColumnFormat/><DefaultFormat/>",
        "<Column index=\"1\"><Format/></Column><Row index=\"1\"><Format/></Row>",
        "<Cell row=\"1\" column=\"1\"><Format/><Text>x</Text></Cell>",
        "<Cell row=\"1\" column=\"1\"><Text>x</Text><Format/><Format/></Cell>",
        "<Cell row=\"1\" column=\"1\"><ContainsValue>true</ContainsValue><ValueType><Entry term=\"boolean\"/></ValueType><Format/><Value>true</Value></Cell>"};
    for (const auto& item : invalid)
        expect(!source::parse_form_xml(wrap(item)), "unsupported format properties, attributes, duplicate indices and invalid sequence must be rejected");
    auto cleared = source::parse_form_xml(wrap(
        "<Row index=\"1\"><Format><Picture standardName=\"PictureLib.Write\"/></Format></Row>"
        "<Cell row=\"1\" column=\"1\"><Text>x</Text><Format><Picture empty=\"true\"/></Format></Cell>"));
    expect(cleared.ok(), "cell format must allow an explicit empty picture that clears an inherited row picture");
    const auto& cleared_sheet = std::get<model::SpreadsheetDocumentFieldPayload>(cleared.value().find_control(model::ObjectId{2})->payload);
    const auto* empty_picture = cleared_sheet.cells[0].format.properties.find(model::PropertyId::from_name("Picture"));
    expect(empty_picture && std::get<model::PictureRef>(empty_picture->value) == model::PictureRef{} &&
        cleared_sheet.rows[0].format.properties.find(model::PropertyId::from_name("Picture")),
        "explicit empty picture and inherited picture must retain separate explicit states");
    auto cleared_output = source::serialize_form_xml(cleared.value());
    expect(cleared_output.ok() && cleared_output.value().find("<Picture empty=\"true\"/>") != std::string::npos,
        "explicit empty format pictures must serialize using the named empty attribute");
    for (const std::string_view picture : {"<Picture empty=\"true\" standardName=\"PictureLib.Write\"/>",
        "<Picture empty=\"true\">2</Picture>", "<Picture empty=\"false\"/>",
        "<Picture empty=\"true\" transparent=\"true\"/>", "<Picture/>"})
        expect(!source::parse_form_xml(wrap("<DefaultFormat>" + std::string(picture) + "</DefaultFormat>")),
            "empty format picture must reject another target, content, attributes, false, or an implicit clear");
    for (const std::string_view attributes : {"height=\"0\" width=\"0\"", "height=\"4294967295\" width=\"17\""}) {
        std::string extent_xml = wrap("");
        extent_xml.replace(extent_xml.find("<Document>"), 10, "<Document " + std::string(attributes) + ">");
        auto extent = source::parse_form_xml(extent_xml);
        expect(extent.ok(), "explicit Spreadsheet Document dimensions, including zero, must parse without cells");
        auto extent_output = source::serialize_form_xml(extent.value());
        expect(extent_output.ok() && extent_output.value().find("<Document " + std::string(attributes) + ">") != std::string::npos,
            "explicit Spreadsheet Document dimensions must survive XML round-trip without inferred content");
    }
    for (const std::string_view attributes : {"height=\"-1\"", "width=\"4294967296\"", "raw=\"1\""}) {
        std::string extent_xml = wrap("");
        extent_xml.replace(extent_xml.find("<Document>"), 10, "<Document " + std::string(attributes) + ">");
        expect(!source::parse_form_xml(extent_xml), "negative or overflowing dimensions and unsupported Document attributes must be rejected");
    }
}

void test_check_box_tooltip_xml_round_trip() {
    const auto make_document = [](std::string tool_tip) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "CheckBoxToolTip";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::boolean;
        type.entries.push_back(entry);
        document.add_attribute(model::Attribute{model::ObjectId{3}, "Flag", type});
        model::ControlNode check_box{model::ObjectId{2}, "FlagControl", model::CheckBoxPayload{}};
        check_box.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        check_box.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tool_tip));
        document.add_control(std::move(check_box));
        return document;
    };

    const auto empty_serialized = source::serialize_form_xml(make_document(""));
    expect(empty_serialized.ok() && empty_serialized.value().find("<ToolTip>") == std::string::npos,
        "empty CheckBox ToolTip must serialize as an omitted default");
    const auto empty_reparsed = source::parse_form_xml(empty_serialized.value());
    expect(empty_reparsed.ok(), "empty CheckBox ToolTip XML must parse after serialization");
    const auto* empty_check_box = empty_reparsed.value().find_control(model::ObjectId{2});
    expect(empty_check_box && !empty_check_box->properties().find(model::PropertyId::from_name("ToolTip")),
        "empty CheckBox ToolTip must normalize to its implicit default in the model");

    const std::string tool_tip = "Подсказка Ω <важно> & \"цитата\"\nВторая строка";
    const auto serialized = source::serialize_form_xml(make_document(tool_tip));
    expect(serialized.ok(), "CheckBox ToolTip must serialize");
    expect(serialized.value().find("<ToolTip>Подсказка Ω &lt;важно&gt; &amp; \"цитата\"\nВторая строка</ToolTip>") !=
            std::string::npos,
        "CheckBox ToolTip XML must escape markup and preserve Unicode, punctuation, and newlines");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "CheckBox ToolTip XML must parse after serialization");
    const auto* check_box = reparsed.value().find_control(model::ObjectId{2});
    const auto* parsed_tool_tip = check_box == nullptr ? nullptr :
        check_box->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(parsed_tool_tip && std::get<std::string>(parsed_tool_tip->value) == tool_tip,
        "CheckBox ToolTip XML value must round-trip independently");
    const auto reserialized = source::serialize_form_xml(reparsed.value());
    expect(reserialized.ok() && reserialized.value() == serialized.value(),
        "CheckBox ToolTip XML must serialize canonically after parsing");
}

void test_choice_field_static_xml_profile_and_runtime_list_rejection() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="ChoiceField" ordinaryFormVersion="2.1">
  <Attributes>
    <Attribute id="3" name="Choice">
      <TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain>
    </Attribute>
  </Attributes>
  <ChildItems>
    <ChoiceField id="2" name="ChoiceField">
      <DataPath attributeId="3"/>
      <Position/>
      <Enabled>false</Enabled>
      <ToolTip>Выберите Ω &amp; &lt;значение&gt;</ToolTip>
    </ChoiceField>
  </ChildItems>
</Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto* choice = parsed.value().find_control(model::ObjectId{2});
    expect(choice && choice->kind() == model::ControlKind::choice_field && choice->data_path &&
               choice->data_path->attribute.id() == model::ObjectId{3} &&
               !std::get<bool>(choice->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
               std::get<std::string>(choice->properties().find(model::PropertyId::from_name("ToolTip"))->value) ==
                   "Выберите Ω & <значение>",
        "ChoiceField XML must expose a named string DataPath and observed static properties");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<DataPath attributeId=\"3\"/>") != std::string::npos &&
               serialized.value().find("<ChoiceList") == std::string::npos,
        "ChoiceField XML must serialize the typed binding without pretending to persist ChoiceList");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && reparsed.value().find_control(model::ObjectId{2}) != nullptr,
        "ChoiceField named static XML must round-trip");

    auto runtime = parsed.value();
    const auto list_property = model::PropertyId::from_name("ChoiceList");
    const model::OrdinaryFormDocument::ObjectKey owner{model::ObjectCategory::control, model::ObjectId{2}};
    model::ValueListValue content;
    content.items.push_back({0, std::string{"runtime"}, std::string{"Runtime"}, true, {}});
    const auto reference = model::ValueListRef::create(content);
    expect(reference && runtime.set_prop_val(owner, list_property, *reference) == model::PropertyMutationStatus::applied_runtime_only,
        "ChoiceField accepts an external live list");
    const auto populated_xml = source::serialize_form_xml(runtime);
    expect(populated_xml.ok() && populated_xml.value() == serialized.value(),
        "assigning and mutating a runtime list must leave persisted XML unchanged");
    const auto cold = source::parse_form_xml(populated_xml.value());
    const auto cold_list = cold.value().get_prop_val(owner, list_property);
    expect(cold_list.value && std::get<model::ValueListRef>(*cold_list.value).get()->items.empty() &&
        std::get<model::ValueListRef>(*cold_list.value) != *reference,
        "cold XML reconstruction creates its own empty runtime list");

    constexpr std::string_view runtime_list_xml = R"XML(<Form id="1" name="ChoiceField" ordinaryFormVersion="2.1">
      <Attributes><Attribute id="3" name="Choice"><TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain></Attribute></Attributes>
      <ChildItems><ChoiceField id="2" name="ChoiceField"><DataPath attributeId="3"/><Position/><ChoiceList/></ChoiceField></ChildItems>
    </Form>)XML";
    const auto runtime_list = source::parse_form_xml(runtime_list_xml);
    expect_code(runtime_list, "OOF2002",
        "runtime-only ChoiceList must be rejected from the persisted XML object model");
    constexpr std::string_view input_runtime_list_xml = R"XML(<Form id="1" name="InputField" ordinaryFormVersion="2.1">
      <Attributes><Attribute id="3" name="Choice"><TypeDomain><Entry term="string" length="20" variable="false"/></TypeDomain></Attribute></Attributes>
      <ChildItems><InputField id="2" name="Input"><DataPath attributeId="3"/><Position/><ChoiceList/></InputField></ChildItems>
    </Form>)XML";
    expect_code(source::parse_form_xml(input_runtime_list_xml), "OOF2002",
        "runtime-only InputField ChoiceList must not be accepted as stored XML state");
}

void test_control_font_xml_round_trip() {
    const auto make_document = [](std::optional<model::FontValue> font) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "CheckBoxFont";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::boolean;
        type.entries.push_back(entry);
        document.add_attribute(model::Attribute{model::ObjectId{3}, "Flag", type});
        model::ControlNode check_box{model::ObjectId{2}, "FlagControl", model::CheckBoxPayload{}};
        check_box.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        if (font) check_box.properties().set_explicit(model::PropertyId::from_name("Font"), *font);
        document.add_control(std::move(check_box));
        return document;
    };

    model::FontValue automatic;
    const auto empty_xml = source::serialize_form_xml(make_document(automatic));
    expect(empty_xml.ok() && empty_xml.value().find("<Font") == std::string::npos,
        "automatic CheckBox Font must serialize as an omitted default");
    const auto empty_parsed = source::parse_form_xml(empty_xml.value());
    const auto* empty_control = empty_parsed.ok() ? empty_parsed.value().find_control(model::ObjectId{2}) : nullptr;
    expect(empty_control && !empty_control->properties().find(model::PropertyId::from_name("Font")),
        "omitted CheckBox Font must parse as its implicit default");

    model::FontValue automatic_bold_false;
    automatic_bold_false.bold = false;
    const auto false_serialized = source::serialize_form_xml(make_document(automatic_bold_false));
    expect(false_serialized.ok() &&
               false_serialized.value().find("<Font kind=\"automatic\" bold=\"false\"/>") != std::string::npos,
        "automatic Font must serialize an explicit false bold override");
    std::string automatic_bold_absent_xml = false_serialized.value();
    const auto false_font_node = automatic_bold_absent_xml.find("<Font kind=\"automatic\" bold=\"false\"/>");
    automatic_bold_absent_xml.replace(false_font_node, std::string("<Font kind=\"automatic\" bold=\"false\"/>").size(),
        "<Font kind=\"automatic\"/>");
    const auto absent_bold_parsed = source::parse_form_xml(automatic_bold_absent_xml);
    const auto* absent_bold_control = absent_bold_parsed.ok() ?
        absent_bold_parsed.value().find_control(model::ObjectId{2}) : nullptr;
    expect(absent_bold_control &&
               !absent_bold_control->properties().find(model::PropertyId::from_name("Font")),
        "automatic Font with absent bold must parse as its implicit default");
    const auto false_parsed = source::parse_form_xml(false_serialized.value());
    const auto* false_control = false_parsed.ok() ? false_parsed.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* false_font = false_control == nullptr ? nullptr :
        false_control->properties().find(model::PropertyId::from_name("Font"));
    expect(false_font && std::get<model::FontValue>(false_font->value) == automatic_bold_false,
        "automatic Font false bold override must round-trip through XML");

    model::FontValue automatic_bold_true;
    automatic_bold_true.bold = true;
    const auto true_serialized = source::serialize_form_xml(make_document(automatic_bold_true));
    expect(true_serialized.ok() &&
               true_serialized.value().find("<Font kind=\"automatic\" bold=\"true\"/>") != std::string::npos,
        "automatic Font must serialize an explicit true bold override");
    const auto true_parsed = source::parse_form_xml(true_serialized.value());
    const auto* true_control = true_parsed.ok() ? true_parsed.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* true_font = true_control == nullptr ? nullptr :
        true_control->properties().find(model::PropertyId::from_name("Font"));
    expect(true_font && std::get<model::FontValue>(true_font->value) == automatic_bold_true,
        "automatic Font true bold override must round-trip through XML");

    model::FontValue automatic_italic;
    automatic_italic.italic = false;
    const auto italic_serialized = source::serialize_form_xml(make_document(automatic_italic));
    expect(italic_serialized.ok(), "automatic Font must serialize native italic overrides");
    std::string unsupported_automatic_xml = false_serialized.value();
    const auto bold_attribute = unsupported_automatic_xml.find("bold=\"false\"");
    expect(bold_attribute != std::string::npos, "automatic false bold test XML must contain its named attribute");
    unsupported_automatic_xml.replace(bold_attribute, std::string("bold=\"false\"").size(),
        "bold=\"false\" italic=\"false\"");
    expect(source::parse_form_xml(unsupported_automatic_xml).ok(),
        "automatic Font must accept native explicit false italic attributes");

    model::FontValue font;
    font.kind = model::FontKind::absolute;
    font.face_name = "Arial";
    font.height = 12;
    font.bold = true;
    const auto serialized = source::serialize_form_xml(make_document(font));
    expect(serialized.ok() && serialized.value().find("<Font kind=\"absolute\"") != std::string::npos,
        "absolute CheckBox Font must serialize through the existing named Font XML surface");
    const auto parsed = source::parse_form_xml(serialized.value());
    expect(parsed.ok(), "absolute CheckBox Font XML must parse");
    const auto* control = parsed.value().find_control(model::ObjectId{2});
    const auto* parsed_font = control == nullptr ? nullptr :
        control->properties().find(model::PropertyId::from_name("Font"));
    expect(parsed_font && std::get<model::FontValue>(parsed_font->value) == font,
        "absolute CheckBox Font XML must round-trip as FontValue");

    constexpr std::array kinds{"Button", "LabelDecoration", "PictureDecoration", "InputField", "CheckBox",
        "ChoiceField", "CommandBar", "RadioButton", "CalendarField", "UsualGroup", "ProgressBar",
        "TextDocumentField", "Button", "LabelDecoration", "InputField"};
    const auto styles = model::metamodel::named_fonts();
    expect(styles.size() == kinds.size(), "the native batch must cover every standard Font style");
    std::string batch = R"XML(<Form id="1" name="ControlFonts" ordinaryFormVersion="2.1"><Attributes><Attribute id="17" name="Flag"><TypeDomain><Entry term="boolean"/></TypeDomain></Attribute></Attributes><ChildItems>)XML";
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto kind = std::string(kinds[index]);
        batch += "<" + kind + " id=\"" + std::to_string(index + 2) + "\" name=\"Item" + std::to_string(index) + "\">";
        if (kind == "CheckBox") batch += R"XML(<DataPath attributeId="17"/>)XML";
        if (kind == "InputField") batch += R"XML(<ValueType><Entry term="string" length="20"/></ValueType>)XML";
        batch += "<Position/><Font kind=\"styleReference\" styleName=\"" + std::string(styles[index].name) +
            "\" height=\"12\" bold=\"true\" italic=\"false\" underline=\"false\" strikeout=\"false\" autoCharSet=\"false\"/></" + kind + ">";
    }
    batch += "</ChildItems></Form>";
    const auto mixed = source::parse_form_xml(batch);
    expect(mixed.ok(), "fifteen standard Font styles across twelve control types must parse as named XML");
    const auto encoded = oof::storage::form_stream::encode_document(mixed.value());
    expect(encoded.ok(), "the shared Font codec must encode every standard style across twelve control types");
    const auto decoded = oof::storage::form_stream::decode_document(encoded.value(), "ControlFonts");
    expect(decoded.ok() && decoded.value().reconstruction_complete(),
        "the standard style batch must decode completely without baseline data");
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto* item = decoded.value().find_control(model::ObjectId{index + 2});
        const auto* entry = item ? item->properties().find(model::PropertyId::from_name("Font")) : nullptr;
        auto expected = font;
        expected.kind = model::FontKind::style_reference; expected.face_name.reset();
        expected.style = model::QualifiedName{std::string(styles[index].name)};
        expected.italic = false; expected.underline = false; expected.strikeout = false;
        expected.auto_char_set = false;
        expect(entry && std::get<model::FontValue>(entry->value) == expected,
            "each control must retain its named style, size, bold and explicit false overrides");
    }
    const auto named = source::serialize_form_xml(decoded.value());
    expect(named.ok(), "standard styles must serialize using the existing Font XML attributes");
    const auto reopened = source::parse_form_xml(named.value());
    expect(reopened.ok(), "the standard style batch must reopen from named XML");
    const auto rebuilt = oof::storage::form_stream::encode_document(reopened.value());
    expect(rebuilt.ok() && oof::storage::list_stream::dump_compact(rebuilt.value()) ==
        oof::storage::list_stream::dump_compact(encoded.value()),
        "all fifteen styles across twelve control types must rebuild the canonical stream only from XML");
}

void test_input_field_layout_xml_round_trip() {
    const auto make_xml = [](std::optional<std::string> horizontal,
                             std::optional<std::string> vertical,
                             std::string height) {
        std::string xml = R"XML(<Form id="1" name="InputLayout" ordinaryFormVersion="2.1"><Attributes>
<Attribute id="1" name="Value"><TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain></Attribute>
</Attributes><ChildItems><InputField id="2" name="Entry"><DataPath attributeId="1"/><Position/>)XML";
        if (vertical) xml += "<VerticalAlign type=\"VerticalAlign\" member=\"" + *vertical + "\"/>";
        if (!height.empty()) xml += "<ChoiceListHeight>" + height + "</ChoiceListHeight>";
        if (horizontal) xml += "<HorizontalAlign type=\"HorizontalAlign\" member=\"" + *horizontal + "\"/>";
        xml += "</InputField></ChildItems></Form>";
        return xml;
    };
    const auto round_trip_enum = [&](std::string_view property, std::string_view type,
                                     std::string_view member) {
        const auto xml = property == "HorizontalAlign"
            ? make_xml(std::string(member), std::nullopt, {})
            : make_xml(std::nullopt, std::string(member), {});
        const auto parsed = source::parse_form_xml(xml);
        expect(parsed.ok(), "InputField named alignment XML must parse");
        const auto serialized = source::serialize_form_xml(parsed.value());
        const bool is_default = (property == "HorizontalAlign" && member == "Auto") ||
            (property == "VerticalAlign" && member == "Top");
        const auto* parsed_input = parsed.value().find_control(model::ObjectId{2});
        const auto* parsed_property = parsed_input->properties().find(model::PropertyId::from_name(property));
        expect((is_default && parsed_property == nullptr) ||
                (parsed_property && std::get<model::EnumerationValue>(parsed_property->value) ==
                    model::EnumerationValue{std::string(type), std::string(member)}),
            std::string("InputField ") + std::string(property) + " must parse enum member " + std::string(member));
        const auto expected_member = "type=\"" + std::string(type) + "\" member=\"" + std::string(member) + "\"";
        expect(serialized.ok() && (is_default
                ? serialized.value().find("<" + std::string(property)) == std::string::npos
                : serialized.value().find(expected_member) != std::string::npos),
            std::string("InputField ") + std::string(property) + " must preserve enum member " + std::string(member));
        const auto reparsed = source::parse_form_xml(serialized.value());
        expect(reparsed.ok(), "InputField alignment XML must reparse");
    };
    for (const std::string_view member : {"Left", "Center", "Right", "Justify", "Auto"}) {
        round_trip_enum("HorizontalAlign", "HorizontalAlign", member);
    }
    for (const std::string_view member : {"Top", "Center", "Bottom"}) {
        round_trip_enum("VerticalAlign", "VerticalAlign", member);
    }

    const auto defaults = source::parse_form_xml(make_xml("Auto", "Top", "0"));
    expect(defaults.ok(), "explicit InputField layout defaults must parse");
    const auto defaults_xml = source::serialize_form_xml(defaults.value());
    expect(defaults_xml.ok() && defaults_xml.value().find("<HorizontalAlign") == std::string::npos &&
            defaults_xml.value().find("<VerticalAlign") == std::string::npos &&
            defaults_xml.value().find("<ChoiceListHeight>") == std::string::npos,
        "InputField layout defaults must normalize to omitted XML properties");

    for (const std::string_view value : {"0", "7", "-7", "-2147483648", "2147483647"}) {
        const auto parsed = source::parse_form_xml(make_xml(std::nullopt, std::nullopt, std::string(value)));
        expect(parsed.ok(), "int32 InputField ChoiceListHeight values must parse");
        const auto serialized = source::serialize_form_xml(parsed.value());
        const auto expected_property = value == "0"
            ? std::string("<ChoiceListHeight>")
            : "<ChoiceListHeight>" + std::string(value) + "</ChoiceListHeight>";
        expect(serialized.ok() && (value == "0"
                ? serialized.value().find(expected_property) == std::string::npos
                : serialized.value().find(expected_property) != std::string::npos),
            "int32 InputField ChoiceListHeight values must serialize without normalization loss");
    }
    expect_code(source::parse_form_xml(make_xml(std::nullopt, std::nullopt, "7.5")), "OOF2002",
        "fractional InputField ChoiceListHeight XML must be rejected by the int32 schema type");
    expect_code(source::parse_form_xml(make_xml(std::nullopt, std::nullopt, "2147483648")), "OOF2002",
        "out-of-int32 InputField ChoiceListHeight XML must be rejected");
}

void test_button_shortcut_xml() {
    constexpr std::string_view xml =
        "<Form id=\"1\" name=\"Shortcuts\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Button id=\"2\" name=\"Run\"><Position/><Shortcut Alt=\"true\" Ctrl=\"false\" Shift=\"true\">"
        "<Key>PageDown</Key></Shortcut></Button></ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "named Button Shortcut must parse");
    const auto* button = parsed.value().find_control(model::ObjectId{2});
    expect(button != nullptr, "Shortcut Button must resolve");
    const auto* entry = button->properties().find(model::PropertyId::from_name("Shortcut"));
    expect(entry != nullptr && std::get<model::ShortcutValue>(entry->value) ==
               model::ShortcutValue{"PageDown", true, false, true},
        "named key and modifiers must enter the object model without platform codes");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("Alt=\"true\"") != std::string::npos &&
               serialized.value().find("Ctrl=\"false\"") != std::string::npos &&
               serialized.value().find("Shift=\"true\"") != std::string::npos &&
               serialized.value().find("<Key>PageDown</Key>") != std::string::npos,
        "Shortcut serialization must retain named key and flags");
    const auto reparsed = source::parse_form_xml(serialized.value());
    const auto* reparsed_button = reparsed.ok()
        ? reparsed.value().find_control(model::ObjectId{2})
        : nullptr;
    const auto* reparsed_shortcut = reparsed_button
        ? reparsed_button->properties().find(model::PropertyId::from_name("Shortcut"))
        : nullptr;
    expect(reparsed_shortcut && std::get<model::ShortcutValue>(reparsed_shortcut->value) ==
               model::ShortcutValue{"PageDown", true, false, true},
        "serialized Shortcut must parse back to the same named model value");

    auto default_shortcut = source::parse_form_xml(
        "<Form id=\"1\" name=\"Default\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Button id=\"2\" name=\"Run\"><Position/><Shortcut Alt=\"false\" Ctrl=\"false\" Shift=\"false\">"
        "<Key>None</Key></Shortcut></Button></ChildItems></Form>");
    expect(default_shortcut.ok(), "explicit empty Shortcut must parse");
    auto default_xml = source::serialize_form_xml(default_shortcut.value());
    expect(default_xml.ok() && default_xml.value().find("<Shortcut") == std::string::npos,
        "None without modifiers must serialize as the default absence");

    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"Bad\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Button id=\"2\" name=\"Run\"><Position/><Shortcut Alt=\"false\" Ctrl=\"false\" Shift=\"false\" keycode=\"65\">"
        "<Key>A</Key></Shortcut></Button></ChildItems></Form>"), "OOF2002",
        "Shortcut must reject raw numeric key attributes");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"Bad\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<Button id=\"2\" name=\"Run\"><Position/><Shortcut Alt=\"false\" Ctrl=\"false\" Shift=\"false\">"
        "<Key>Unknown</Key></Shortcut></Button></ChildItems></Form>"), "OOF2002",
        "Shortcut must reject unknown named keys");
}

void test_form_style_xml_roundtrip_and_rejections() {
    constexpr std::string_view automatic_xml =
        "<Form id=\"1\" name=\"Styled\" ordinaryFormVersion=\"2.1\"><Style/></Form>";
    auto automatic = source::parse_form_xml(automatic_xml);
    expect(automatic.ok() && automatic.value().get_prop_val(
        {model::ObjectCategory::form, model::ObjectId{1}}, model::PropertyId::from_name("Style")).value ==
            model::PropertyValue{model::StyleValue{}},
        "empty Form.Style means Auto");
    auto automatic_output = source::serialize_form_xml(automatic.value());
    expect(automatic_output.ok() && automatic_output.value().find("<Style") == std::string::npos,
        "Auto Form.Style is omitted as the descriptor default");

    constexpr std::string_view custom_xml =
        "<Form id=\"1\" name=\"Styled\" ordinaryFormVersion=\"2.1\"><Style styleId=\"21436500-0000-4000-8000-000000000001\"/></Form>";
    auto custom = source::parse_form_xml(custom_xml);
    const auto* entry = custom.ok() ? custom.value().form().properties.find(model::PropertyId::from_name("Style")) : nullptr;
    expect(entry && entry->value == model::PropertyValue{model::StyleValue{
        model::UuidValue{"21436500-0000-4000-8000-000000000001"}}},
        "Form.Style parses a named configuration style UUID");
    auto serialized = source::serialize_form_xml(custom.value());
    expect(serialized.ok() && serialized.value().find("<Style styleId=\"21436500-0000-4000-8000-000000000001\"/>") != std::string::npos,
        "configured Form.Style serializes as a named reference");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && reparsed.value().form().properties.find(model::PropertyId::from_name("Style"))->value == entry->value,
        "configured Form.Style roundtrips by UUID identity");

    for (const auto invalid : {
             "<Style styleId=\"00000000-0000-0000-0000-000000000000\"/>",
             "<Style styleId=\"broken\"/>",
             "<Style unexpected=\"value\"/>",
             "<Style><Unknown/></Style>",
             "<Style>text</Style>"}) {
        const std::string xml = "<Form id=\"1\" name=\"Bad\" ordinaryFormVersion=\"2.1\">" +
            std::string(invalid) + "</Form>";
        expect(!source::parse_form_xml(xml).ok(), "Form.Style rejects invalid UUIDs and non-reference XML content");
    }
}

void test_boolean_type_domain_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Boolean" ordinaryFormVersion="2.1">
  <Attributes><Attribute id="2" name="Flag"><TypeDomain><Entry term="boolean"/></TypeDomain></Attribute></Attributes>
</Form>
)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "Boolean TypeDomain entry must parse");
    const auto& attributes = parsed.value().collections().attributes;
    expect(attributes.size() == 1, "Boolean attribute must materialize");
    expect(
        attributes.front().type.entries.size() == 1 &&
            attributes.front().type.entries.front().term == model::TypeDomainTerm::boolean,
        "Boolean TypeDomain entry must materialize as boolean");

    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "Boolean TypeDomain entry must serialize");
    expect(
        serialized.value().find("<Entry term=\"boolean\"/>") != std::string::npos,
        "Boolean TypeDomain entry must retain its named XML term");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "serialized Boolean TypeDomain must reparse");
    expect(
        reparsed.value().collections().attributes.front().type.entries.front().term ==
            model::TypeDomainTerm::boolean,
        "Boolean TypeDomain must survive XML writer/parser round-trip");

    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Boolean\" ordinaryFormVersion=\"2.1\"><Attributes>"
            "<Attribute id=\"2\" name=\"Flag\"><TypeDomain><Entry term=\"boolean\" length=\"1\"/>"
            "</TypeDomain></Attribute></Attributes></Form>"),
        "OOF2003",
        "Boolean TypeDomain must reject length qualifier");
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Boolean\" ordinaryFormVersion=\"2.1\"><Attributes>"
            "<Attribute id=\"2\" name=\"Flag\"><TypeDomain><Entry term=\"boolean\" variable=\"false\"/>"
            "</TypeDomain></Attribute></Attributes></Form>"),
        "OOF2003",
        "Boolean TypeDomain must reject variable qualifier");
}

void test_value_list_type_domain_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="ValueList" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Values"><TypeDomain><Entry term="valueList"/></TypeDomain></Attribute></Attributes></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "named ValueList TypeDomain must parse");
    const auto& entry = parsed.value().collections().attributes.front().type.entries.front();
    expect(entry.term == model::TypeDomainTerm::value_list && !entry.type_uuid,
        "ValueList XML term must not expose its platform UUID");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Entry term=\"valueList\"/>") != std::string::npos,
        "ValueList TypeDomain must retain its named XML term");
    expect(source::parse_form_xml(serialized.value()).ok(),
        "serialized ValueList TypeDomain must parse again");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"ValueList\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"2\" name=\"Values\"><TypeDomain><Entry term=\"valueList\" typeUuid=\"d47d59f8-73f0-481c-8b5e-f6384c0a4804\"/></TypeDomain></Attribute></Attributes></Form>"),
        "OOF2003", "ValueList term must reject caller-supplied UUIDs");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"ValueList\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"2\" name=\"Values\"><TypeDomain><Entry term=\"valueList\" length=\"64\"/></TypeDomain></Attribute></Attributes></Form>"),
        "OOF2003", "ValueList term must reject string qualifiers");
}

void test_value_table_type_domain_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="ValueTable" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "named ValueTable TypeDomain must parse");
    const auto& entry = parsed.value().collections().attributes.front().type.entries.front();
    expect(entry.term == model::TypeDomainTerm::value_table && !entry.type_uuid,
        "ValueTable XML term must not expose its platform UUID");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Entry term=\"valueTable\"/>") != std::string::npos,
        "ValueTable TypeDomain must retain its named XML term");
    expect(source::parse_form_xml(serialized.value()).ok(),
        "serialized ValueTable TypeDomain must parse again");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"ValueTable\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"2\" name=\"Rows\"><TypeDomain><Entry term=\"valueTable\" typeUuid=\"d47d59f8-73f0-481c-8b5e-f6384c0a4804\"/></TypeDomain></Attribute></Attributes></Form>"),
        "OOF2003", "ValueTable term must reject caller-supplied UUIDs");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"ValueTable\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"2\" name=\"Rows\"><TypeDomain><Entry term=\"valueTable\" length=\"64\"/></TypeDomain></Attribute></Attributes></Form>"),
        "OOF2003", "ValueTable term must reject qualifiers");
}

void test_value_tree_type_domain_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="ValueTree" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTree"/></TypeDomain></Attribute></Attributes></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "named ValueTree TypeDomain must parse");
    const auto& entry = parsed.value().collections().attributes.front().type.entries.front();
    expect(entry.term == model::TypeDomainTerm::value_tree && !entry.type_uuid,
        "ValueTree XML must not expose its platform UUID");
    const auto native = oof::storage::form_stream::encode_document(parsed.value());
    expect(native.ok(), "ValueTree Attribute must encode without a baseline");
    const auto decoded = oof::storage::form_stream::decode_document(native.value());
    expect(decoded.ok() && decoded.value().reconstruction_complete(), "ValueTree Attribute must decode completely");
    const auto serialized = source::serialize_form_xml(decoded.value());
    expect(serialized.ok() && serialized.value().find("<Entry term=\"valueTree\"/>") != std::string::npos,
        "ValueTree must remain named through XML and native roundtrip");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "rebuilt ValueTree XML must parse");
    const auto rebuilt = oof::storage::form_stream::encode_document(reparsed.value());
    expect(rebuilt.ok() && oof::storage::list_stream::dump_compact(rebuilt.value()) ==
        oof::storage::list_stream::dump_compact(native.value()), "ValueTree native roundtrip must be exact");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"ValueTree\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"2\" name=\"Rows\"><TypeDomain><Entry term=\"valueTree\" length=\"64\"/></TypeDomain></Attribute></Attributes></Form>"),
        "OOF2003", "ValueTree must reject qualifiers");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"ValueTree\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"2\" name=\"Rows\"><TypeDomain><Entry term=\"object\" typeUuid=\"e603c0f2-92fb-4d47-8f38-a44a381cf235\"/></TypeDomain></Attribute></Attributes></Form>"),
        "OOF2003", "ValueTree UUID must not be accepted as an object alias");
    auto disguised = parsed.value();
    auto& disguised_entry = const_cast<model::Attribute&>(disguised.collections().attributes.front()).type.entries.front();
    disguised_entry.term = model::TypeDomainTerm::object;
    disguised_entry.type_uuid = model::UuidValue{"e603c0f2-92fb-4d47-8f38-a44a381cf235"};
    expect(!source::serialize_form_xml(disguised).ok(), "ValueTree UUID must not serialize as an object alias");
}

void test_table_enum_properties_xml() {
    const auto xml = [](std::string_view property_name, std::string_view property) {
        const bool before_columns = property_name == "HorizontalScrollBar" || property_name == "VerticalScrollBar" || property_name == "Output";
        const auto prefix = std::string("<Form id=\"1\" name=\"RowsForm\" ordinaryFormVersion=\"2.1\">") +
            "<Attributes><Attribute id=\"2\" name=\"Rows\"><TypeDomain><Entry term=\"valueTable\"/>" +
            "</TypeDomain></Attribute></Attributes><ChildItems><Table id=\"3\" name=\"Rows\">" +
            "<DataPath attributeId=\"2\"/><Position/>";
        constexpr std::string_view columns =
            "<Columns><Column name=\"Code\"><DataPath>Code</DataPath><Header>"
            "<Item language=\"ru\">Код</Item></Header><Control type=\"InputField\"/>"
            "</Column></Columns>";
        return prefix + (before_columns ? std::string(property) : std::string{}) + std::string(columns) +
            (before_columns ? std::string{} : std::string(property)) + "</Table></ChildItems></Form>";
    };
    const auto table = model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::control, model::ObjectId{3}};
    for (const auto& spec : model::detail::table_enum_specs) {
        const auto property_id = model::PropertyId::from_name(spec.property);
        const auto element = [&](std::string_view type, std::string_view member) {
            return "<" + std::string(spec.property) + " type=\"" + std::string(type) +
                "\" member=\"" + std::string(member) + "\"/>";
        };
        const auto omitted = source::parse_form_xml(xml(spec.property, ""));
        expect(omitted.ok(), "Table enum property may be omitted");
        expect(spec.default_ordinal >= 0 && static_cast<std::size_t>(spec.default_ordinal) < spec.members.size(),
            "Table enum default must belong to its domain");
        const auto default_value = omitted.value().get_prop_val(table, property_id);
        expect(default_value.status == model::PropertyReadStatus::proven_default && default_value.value &&
            *default_value.value == model::PropertyValue{model::EnumerationValue{
                std::string(spec.type_name), std::string(spec.members[static_cast<std::size_t>(spec.default_ordinal)])}},
            "omitted Table enum property must use its named default");
        for (const auto member : spec.members) {
            const model::PropertyValue expected = model::EnumerationValue{std::string(spec.type_name), std::string(member)};
            const auto parsed = source::parse_form_xml(xml(spec.property, element(spec.type_name, member)));
            expect(parsed.ok(), "known Table enum value must parse from XML");
            const auto read = parsed.value().get_prop_val(table, property_id);
            expect(read.value && *read.value == expected, "Table enum XML must expose the named value");
            const auto written = source::serialize_form_xml(parsed.value());
            expect(written.ok(), "known Table enum value must serialize to XML");
            const auto reparsed = source::parse_form_xml(written.value());
            expect(reparsed.ok(), "serialized Table enum must parse again");
            const auto roundtrip = reparsed.value().get_prop_val(table, property_id);
            expect(roundtrip.value && *roundtrip.value == expected, "Table enum value must survive XML roundtrip");
        }
        expect(!source::parse_form_xml(xml(spec.property, element(spec.type_name, "Unknown"))).ok(),
            "unknown Table enum member must fail");
        expect(!source::parse_form_xml(xml(spec.property, element("OtherType", spec.members.front()))).ok(),
            "foreign Table enum type must fail");
        const auto default_member = spec.members[static_cast<std::size_t>(spec.default_ordinal)];
        expect(!source::parse_form_xml(xml(spec.property, element("OtherType", default_member))).ok(),
            "foreign enum type must fail even when its member matches the Table default");
        expect(!source::parse_form_xml(xml(spec.property, element(spec.type_name, spec.members.front()) +
            element(spec.type_name, spec.members.front()))).ok(), "duplicate Table enum property must fail");
    }
}

void test_table_choice_mode_runtime_only() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="ChoiceForm" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes><ChildItems><Table id="3" name="Table"><DataPath attributeId="2"/><Position/><Columns><Column name="Code"><DataPath>Code</DataPath><Header><Item language="en">Code</Item></Header><Control type="InputField"/></Column></Columns></Table></ChildItems></Form>)XML";
    auto document = source::parse_form_xml(xml);
    expect(document.ok(), "Table ChoiceMode fixture must parse");
    const model::OrdinaryFormDocument::ObjectKey table{model::ObjectCategory::control, model::ObjectId{3}};
    const auto property = model::PropertyId::from_name("ChoiceMode");
    expect(document.value().get_prop_val(table, property).status == model::PropertyReadStatus::no_default,
        "context dependent ChoiceMode must not assume a default");
    expect(document.value().set_prop_val(table, property, true) == model::PropertyMutationStatus::applied_runtime_only,
        "Table ChoiceMode must be a runtime-only value");
    expect(!source::serialize_form_xml(document.value()).ok(), "runtime ChoiceMode must not be saved in XML");
    expect(document.value().reset_prop_val(table, property) == model::PropertyMutationStatus::reset_runtime_only &&
        source::serialize_form_xml(document.value()).ok(), "Table must serialize after runtime reset");
    std::string bad(xml); const auto end = bad.find("</Table>"); bad.insert(end, "<ChoiceMode>true</ChoiceMode>");
    expect(!source::parse_form_xml(bad).ok(), "runtime ChoiceMode must not be accepted in XML");
    for (const auto name : {"CurrentRow", "CurrentData", "SelectedRows", "CurrentColumn"}) {
        std::string runtime_xml(xml);
        runtime_xml.insert(runtime_xml.find("</Table>"), "<" + std::string(name) + "/>" );
        expect(!source::parse_form_xml(runtime_xml).ok(),
            std::string(name) + " must not be accepted as stored XML state");
    }
}

void test_table_row_selection_mode_xml() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Modes" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTree"/></TypeDomain></Attribute></Attributes><ChildItems><Table id="3" name="Table"><DataPath attributeId="2"/><Position/><Columns><Column name="Code"><DataPath>Code</DataPath><Header><Item language="en">Code</Item></Header><Control type="InputField"/></Column></Columns><SelectionMode type="TableBoxSelectionMode" member="MultiLine"/><RowSelectionMode type="TableBoxRowSelectionMode" member="Row"/></Table></ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "both named Table selection modes must parse together");
    const model::OrdinaryFormDocument::ObjectKey owner{model::ObjectCategory::control, model::ObjectId{3}};
    const auto row = parsed.value().get_prop_val(owner, model::PropertyId::from_name("RowSelectionMode"));
    const auto selection = parsed.value().get_prop_val(owner, model::PropertyId::from_name("SelectionMode"));
    expect(row.value && *row.value == model::PropertyValue{model::EnumerationValue{"TableBoxRowSelectionMode", "Row"}} &&
           selection.value && *selection.value == model::PropertyValue{model::EnumerationValue{"TableBoxSelectionMode", "MultiLine"}},
        "different Table selection modes must remain distinct");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<RowSelectionMode type=\"TableBoxRowSelectionMode\" member=\"Row\"") != std::string::npos,
        "Row mode must retain its named type in public XML");
    std::string wrong_type(xml);
    const auto start = wrong_type.find("TableBoxRowSelectionMode");
    expect(start != std::string::npos, "row mode fixture must contain its enum type");
    wrong_type.replace(start, std::string("TableBoxRowSelectionMode").size(), "TableBoxSelectionMode");
    expect(!source::parse_form_xml(wrong_type).ok(), "SelectionMode enum type must not be accepted for RowSelectionMode");
}

void test_value_tree_table_xml_contract() {
    const auto xml = [](std::string_view source_type, std::string_view property, std::string_view events) {
        return std::string("<Form id=\"1\" name=\"TreeForm\" ordinaryFormVersion=\"2.1\"><Attributes>") +
            "<Attribute id=\"2\" name=\"Rows\"><TypeDomain><Entry term=\"" + std::string(source_type) +
            "\"/></TypeDomain></Attribute></Attributes><ChildItems><Table id=\"3\" name=\"Table\">" +
            "<DataPath attributeId=\"2\"/><Position/>" +
            "<Columns><Column name=\"Code\"><DataPath>Code</DataPath><Header><Item language=\"en\">Code</Item>"
            "</Header><Control type=\"InputField\"/></Column></Columns>" + std::string(property) + std::string(events) + "</Table></ChildItems></Form>";
    };
    constexpr std::string_view action = "<Events><OnCurrentParentChange id=\"4\">HandleParent</OnCurrentParentChange></Events>";
    const model::OrdinaryFormDocument::ObjectKey table{model::ObjectCategory::control, model::ObjectId{3}};
    const auto tree_id = model::PropertyId::from_name("Tree");
    for (const bool tree : {false, true}) {
        const auto parsed = source::parse_form_xml(xml("valueTree", tree ? "<Tree>true</Tree>" : "<Tree>false</Tree>", action));
        expect(parsed.ok(), "ValueTree Table with Tree mode and parent-change action must parse");
        const auto value = parsed.value().get_prop_val(table, tree_id);
        expect(value.value && std::get<bool>(*value.value) == tree, "XML must expose the selected Tree mode");
        const auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok() && serialized.value().find("OnCurrentParentChange") != std::string::npos,
            "ValueTree action must remain named in XML");
        const auto again = source::parse_form_xml(serialized.value());
        expect(again.ok(), "serialized ValueTree Table must parse again");
        const auto repeated = again.value().get_prop_val(table, tree_id);
        expect(repeated.value && std::get<bool>(*repeated.value) == tree, "Tree mode must survive XML roundtrip");
    }
    const auto implicit = source::parse_form_xml(xml("valueTree", "", ""));
    expect(implicit.ok(), "ValueTree Table must parse without an explicit Tree property");
    const auto default_tree = implicit.value().get_prop_val(table, tree_id);
    expect(default_tree.value && std::get<bool>(*default_tree.value), "implicit ValueTree mode must be Tree");
    expect(!source::parse_form_xml(xml("valueTree", "<Tree>unknown</Tree>", "")).ok(),
        "Tree must reject a non-Boolean value");
    expect(!source::parse_form_xml(xml("valueTable", "<Tree>false</Tree>", "")).ok(),
        "ValueTable must reject an explicit Tree property");
    auto value_table = source::parse_form_xml(xml("valueTable", "", ""));
    expect(value_table.ok(), "ValueTable fixture must parse");
    expect(value_table.value().get_prop_val(table, tree_id).status == model::PropertyReadStatus::unsupported_surface,
        "ValueTable must not report a Tree default");
    expect(value_table.value().set_prop_val(table, tree_id, false) == model::PropertyMutationStatus::unsupported_surface &&
        value_table.value().reset_prop_val(table, tree_id) == model::PropertyMutationStatus::unsupported_surface,
        "ValueTable must reject Tree mutation and reset");
    expect(!source::parse_form_xml(xml("valueTable", "", action)).ok(),
        "ValueTable must reject the parent-change action");
}

void test_table_column_visible_and_empty_tree_xml() {
    const auto xml = [](std::string_view type, std::string_view columns) {
        return std::string(R"XML(<Form id="1" name="ColumnVisibility" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term=")XML") +
            std::string(type) + R"XML("/></TypeDomain></Attribute></Attributes><ChildItems><Table id="3" name="Table"><DataPath attributeId="2"/><Position/>)XML" +
            std::string(columns) + "</Table></ChildItems></Form>";
    };
    const auto column_xml = [](std::string_view visible) {
        return std::string(R"XML(<Columns><Column name="Code"><DataPath>Code</DataPath><Header><Item language="en">Code</Item></Header>)XML") +
            std::string(visible) + R"XML(<Control type="InputField"/></Column></Columns>)XML";
    };
    for (const auto& [input, expected] : std::array{
             std::pair<std::string_view, bool>{"", true},
             std::pair<std::string_view, bool>{"<Visible>true</Visible>", true},
             std::pair<std::string_view, bool>{"<Visible>false</Visible>", false}}) {
        const auto parsed = source::parse_form_xml(xml("valueTable", column_xml(input)));
        expect(parsed.ok(), "named Table Column visibility must parse");
        const auto* table = parsed.value().find_control(model::ObjectId{3});
        expect(table && std::get<model::TablePayload>(table->payload).columns.front().visible == expected,
            "named Table Column visibility must retain the chosen value");
        const auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok() &&
            (serialized.value().find("<Visible>false</Visible>") != std::string::npos) == !expected &&
            serialized.value().find("<Visible>true</Visible>") == std::string::npos,
            "default visibility must be omitted and false must be named in XML");
        const auto parsed_again = source::parse_form_xml(serialized.value());
        expect(parsed_again.ok() && std::get<model::TablePayload>(parsed_again.value()
            .find_control(model::ObjectId{3})->payload).columns.front().visible == expected,
            "Table Column visibility must survive XML roundtrip");
    }
    expect(!source::parse_form_xml(xml("valueTable", column_xml("<Visible>unknown</Visible>"))).ok(),
        "Column Visible must reject a non-Boolean value");

    auto empty_tree = source::parse_form_xml(xml("valueTree", "<Columns/>"));
    expect(empty_tree.ok(), "ValueTree Table must accept a named empty Columns collection");
    const auto* tree = empty_tree.value().find_control(model::ObjectId{3});
    expect(tree && std::get<model::TablePayload>(tree->payload).columns.empty(),
        "empty ValueTree Columns must remain empty in the model");
    const auto native = oof::storage::form_stream::encode_document(empty_tree.value());
    expect(native.ok(), "empty ValueTree Columns must encode without a baseline");
    const auto decoded = oof::storage::form_stream::decode_document(native.value(), "EmptyTreeTable");
    expect(decoded.ok() && decoded.value().reconstruction_complete(),
        "empty ValueTree Columns must decode completely");
    const auto serialized = source::serialize_form_xml(decoded.value());
    expect(serialized.ok(), "empty ValueTree Columns must serialize into named XML");
    const auto again = source::parse_form_xml(serialized.value());
    expect(again.ok() && std::get<model::TablePayload>(again.value()
        .find_control(model::ObjectId{3})->payload).columns.empty(),
        "empty ValueTree Columns must survive XML and native roundtrip");
    expect(!source::parse_form_xml(xml("valueTable", "<Columns/>")),
        "empty ValueTable Columns must be rejected by the current model contract");
}

void test_table_fixed_column_xml() {
    const auto xml = [](std::string_view type, std::string_view left, std::string_view right) {
        return std::string(R"XML(<Form id="1" name="FixedColumns" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term=")XML") +
            std::string(type) + R"XML("/></TypeDomain></Attribute></Attributes><ChildItems><Table id="3" name="Table"><DataPath attributeId="2"/><Position/><Columns><Column name="A"><DataPath>A</DataPath><Header><Item language="en">A</Item></Header><Control type="InputField"/></Column><Column name="B"><DataPath>B</DataPath><Header><Item language="en">B</Item></Header><Control type="InputField"/></Column><Column name="C"><DataPath>C</DataPath><Header><Item language="en">C</Item></Header><Visible>false</Visible><Control type="InputField"/></Column></Columns><FixedLeft>)XML" +
            std::string(left) + "</FixedLeft><FixedRight>" + std::string(right) +
            "</FixedRight></Table></ChildItems></Form>";
    };
    for (const auto type : {"valueTable", "valueTree"}) {
        const auto parsed = source::parse_form_xml(xml(type, "1", "1"));
        expect(parsed.ok(), "valid fixed counts must parse from XML");
        const auto written = source::serialize_form_xml(parsed.value());
        expect(written.ok() && written.value().find("<FixedLeft>1</FixedLeft>") != std::string::npos &&
            written.value().find("<FixedRight>1</FixedRight>") != std::string::npos,
            "fixed counts must be retained as named XML properties");
        expect(source::parse_form_xml(written.value()).ok(), "fixed counts must survive XML roundtrip");
        expect(!source::parse_form_xml(xml(type, "-1", "0")).ok(), "negative fixed count must be rejected");
        const auto hidden_after_fix = source::parse_form_xml(xml(type, "2", "1"));
        expect(hidden_after_fix.ok(), "saved fixed counts must survive hiding a previously visible column");
        const auto hidden_xml = source::serialize_form_xml(hidden_after_fix.value());
        expect(hidden_xml.ok() && source::parse_form_xml(hidden_xml.value()).ok(),
            "fixed counts with a hidden column must survive XML roundtrip");
        expect(!source::parse_form_xml(xml(type, "1.5", "0")).ok(), "XML fixed counts must be integral");
    }
}

void test_table_columns_named_profile() {
    constexpr std::string_view valid = R"XML(
<Form id="1" name="RowsForm" ordinaryFormVersion="2.1">
  <Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes>
  <ChildItems><Table id="3" name="Rows"><DataPath attributeId="2"/><Position/><Columns>
    <Column name="Code"><DataPath>Code</DataPath><Header><Item language="ru">Код</Item></Header>
      <Control type="InputField"><ValueType><Entry term="date" date="true" time="false"/></ValueType><Enabled>false</Enabled><ReadOnly>true</ReadOnly></Control>
    </Column>
    <Column name="CodeCopy"><DataPath>Code</DataPath><Header><Item language="en">Code copy</Item></Header>
      <Control type="InputField"/>
    </Column>
    <Column name="ChoiceCopy"><DataPath>Code</DataPath><Header><Item language="en">Choice copy</Item></Header>
      <Control type="ChoiceField"><Enabled>true</Enabled><ToolTip/></Control>
    </Column>
    <Column name="CheckCopy"><DataPath>Code</DataPath><Header><Item language="en">Check copy</Item></Header>
      <Control type="CheckBox"><Enabled>true</Enabled><Caption/><ToolTip/><Font kind="automatic"/></Control>
    </Column>
  </Columns></Table></ChildItems>
</Form>)XML";
    auto parsed = source::parse_form_xml(valid);
    expect(parsed.ok(), parsed ? "named Table with repeated source DataPath and typed editors must parse" :
        parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto* table = parsed.value().find_control(model::ObjectId{3});
    expect(table != nullptr && std::get<model::TablePayload>(table->payload).columns.size() == 4,
        "Table must own its ordered typed Columns");
    const auto& columns = std::get<model::TablePayload>(table->payload).columns;
    expect(std::ranges::all_of(columns, [](const auto& column) { return column.control.has_value(); }),
        "explicit Column Control nodes must materialize editors");
    expect(columns[0].control->value_type.has_value() && columns[0].control->value_type->entries.size() == 1 &&
               columns[0].control->value_type->entries[0].term == model::TypeDomainTerm::date &&
               columns[0].control->value_type->entries[0].date == model::DateQualifiers{true, false} &&
               std::get<bool>(columns[0].control->properties.find(model::PropertyId::from_name("Enabled"))->value) == false &&
               std::get<bool>(columns[0].control->properties.find(model::PropertyId::from_name("ReadOnly"))->value) == true &&
               columns[0].data_path == columns[1].data_path && columns[0].data_path == columns[2].data_path &&
               columns[2].control->kind == model::ControlKind::choice_field &&
               columns[3].control->kind == model::ControlKind::check_box &&
               columns[2].control->properties.empty() &&
               columns[3].control->properties.empty(),
        "duplicate DataPath is allowed and typed editor defaults normalize away");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Columns>") != std::string::npos &&
               serialized.value().find("type=\"ChoiceField\"") != std::string::npos &&
               serialized.value().find("type=\"CheckBox\"") != std::string::npos,
        "named Table Columns must serialize as editable XML");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && reparsed.value().collections().controls.size() == 1 &&
               std::get<model::TablePayload>(reparsed.value().find_control(model::ObjectId{3})->payload)
                       .columns[2].control->kind == model::ControlKind::choice_field &&
               std::get<model::TablePayload>(reparsed.value().find_control(model::ObjectId{3})->payload)
                       .columns[3].control->kind == model::ControlKind::check_box,
        "Table Column XML must survive a source roundtrip");
    expect(serialized.value().find("<ValueType>") != std::string::npos &&
               std::get<model::TablePayload>(reparsed.value().find_control(model::ObjectId{3})->payload)
                   .columns[0].control->value_type == columns[0].control->value_type &&
               serialized.value().find("<Enabled>false</Enabled>") != std::string::npos &&
               serialized.value().find("<ReadOnly>true</ReadOnly>") != std::string::npos,
        "Table InputField TypeDomain, Enabled, and ReadOnly must remain named in XML");

    std::string duplicate_name(valid);
    const auto duplicate_pos = duplicate_name.find("name=\"CodeCopy\"");
    duplicate_name.replace(duplicate_pos, std::string("name=\"CodeCopy\"").size(), "name=\"Code\"");
    expect(!source::parse_form_xml(duplicate_name).ok(), "duplicate Table Column names must fail");

    std::string unsupported_editor(valid);
    const auto editor_pos = unsupported_editor.find("type=\"InputField\"");
    unsupported_editor.replace(editor_pos, std::string("type=\"InputField\"").size(),
        "type=\"SpreadsheetDocumentField\"");
    expect(!source::parse_form_xml(unsupported_editor).ok(), "unsupported Table Column editor kind must fail");

    std::string incompatible_default(valid);
    const auto choice_pos = incompatible_default.find("<Control type=\"ChoiceField\">");
    incompatible_default.insert(choice_pos + std::string("<Control type=\"ChoiceField\">").size(),
        "<ReadOnly>false</ReadOnly>");
    expect(!source::parse_form_xml(incompatible_default).ok(),
        "ChoiceField must reject an InputField property even when it carries that property's default");

    std::string check_caption(valid);
    const auto check_pos = check_caption.find("<Control type=\"CheckBox\">");
    check_caption.insert(check_pos + std::string("<Control type=\"CheckBox\">").size(), "<ChoiceList/>");
    expect(!source::parse_form_xml(check_caption).ok(),
        "CheckBox must reject a ChoiceField property even when it carries the platform default");

    std::string choice_value_type(valid);
    const auto choice_type_pos = choice_value_type.find("<Control type=\"ChoiceField\">");
    choice_value_type.insert(choice_type_pos + std::string("<Control type=\"ChoiceField\">").size(),
        "<ValueType><Entry term=\"string\" length=\"64\" variable=\"false\"/></ValueType>");
    expect(!source::parse_form_xml(choice_value_type).ok(),
        "Table editor ValueType must remain restricted to the proven InputField owner");

    std::string choice_disabled(valid);
    const auto choice_enabled_pos = choice_disabled.find("<Control type=\"ChoiceField\">");
    choice_disabled.insert(choice_enabled_pos + std::string("<Control type=\"ChoiceField\">").size(),
        "<Enabled>false</Enabled>");
    expect(!source::parse_form_xml(choice_disabled).ok(),
        "Table ChoiceField Enabled=false must remain unsupported until separately proven");

    std::string wrong_source(valid);
    const auto type_pos = wrong_source.find("term=\"valueTable\"");
    wrong_source.replace(type_pos, std::string("term=\"valueTable\"").size(), "term=\"string\"");
    expect(!source::parse_form_xml(wrong_source).ok(), "Table source must reject a non-ValueTable type");

    std::string dangling_source(valid);
    const auto attribute_pos = dangling_source.find("attributeId=\"2\"");
    dangling_source.replace(attribute_pos, std::string("attributeId=\"2\"").size(), "attributeId=\"99\"");
    expect(!source::parse_form_xml(dangling_source).ok(), "Table source must reject a dangling attribute link");
}

void test_neutral_localized_text_xml() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Neutral" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes><ChildItems><Table id="3" name="Rows"><DataPath attributeId="2"/><Position/><Columns><Column name="Code"><DataPath>Code</DataPath><Header><DefaultText>Neutral &amp; literal</DefaultText><Item language="en">English</Item></Header></Column></Columns></Table><PictureDecoration id="4" name="Picture"><Position/><NonselectedPictureText><DefaultText>Picture text</DefaultText><Item language="en">Picture</Item></NonselectedPictureText></PictureDecoration></ChildItems></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "neutral and translated text must share the existing localized value model" : parsed.diagnostics().front().message);
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<DefaultText>Neutral &amp; literal</DefaultText>") != std::string::npos &&
        serialized.value().find("language=\"#\"") == std::string::npos,
        "neutral text must have a named public representation rather than a platform language marker");
    const auto native = oof::storage::form_stream::encode_document(parsed.value());
    expect(native.ok(), "neutral localized values must encode through the existing string codec");
    const auto decoded = oof::storage::form_stream::decode_document(native.value(), "Neutral");
    expect(decoded.ok(), "neutral localized values must decode from the native string representation");
    const auto public_xml = source::serialize_form_xml(decoded.value());
    expect(public_xml.ok() && public_xml.value().find("<DefaultText>Neutral &amp; literal</DefaultText>") != std::string::npos,
        "neutral default text must survive XML-only native roundtrip");
    for (const auto value : {"<DefaultText>A</DefaultText><DefaultText>B</DefaultText>",
             "<Item language=\"#\">A</Item>", "<DefaultText language=\"en\">A</DefaultText>"}) {
        const auto invalid = std::string("<Form id=\"1\" name=\"Neutral\" ordinaryFormVersion=\"2.1\"><ChildItems><PictureDecoration id=\"4\" name=\"P\"><Position/><NonselectedPictureText>") + value + "</NonselectedPictureText></PictureDecoration></ChildItems></Form>";
        expect(!source::parse_form_xml(invalid), "neutral text must reject duplicate defaults, wire markers and unsupported attributes");
    }
}

void test_catalog_list_table_source_matrix() {
    const std::string prefix = R"XML(<Form id="1" name="CatalogRows" ordinaryFormVersion="2.1"><Attributes><Attribute id="0" name="Rows"><TypeDomain><Entry term="object" typeUuid="11111111-1111-1111-1111-111111111111"/></TypeDomain></Attribute></Attributes><ChildItems><Table id="3" name="Rows"><DataPath attributeId="0"/><Position/><Columns>
<Column name="Picture"><Source><StandardPicture/></Source><Header/></Column>
<Column name="Code"><Source><MetadataField name="Code"/></Source><Header/><ValueType><Entry term="string" length="9" variable="true"/></ValueType></Column>
<Column name="Description"><Source><MetadataField name="Description"/></Source><Header/><Control type="InputField"><ValueType><Entry term="string" length="40" variable="true"/></ValueType></Control></Column>
<Column name="Flag"><Source><MetadataField uuid="44444444-4444-4444-4444-444444444444"/></Source><Header/><ValueType><Entry term="boolean"/></ValueType></Column>
</Columns><CatalogListTableExtension>)XML";
    const std::string suffix = "</CatalogListTableExtension></Table></ChildItems></Form>";
    const std::array<std::string, 12> variants{
        "", "<Tree>true</Tree>", "<EditType type=\"ListEditMode\" member=\"InList\"/>",
        "<HierarchicalView>false</HierarchicalView>", "<ChangeHierarchicalView>false</ChangeHierarchicalView>",
        "<ViewFoldersAndItems type=\"FoldersAndItemsUse\" member=\"Folders\"/>",
        "<ChangeCurrentParent>false</ChangeCurrentParent>", "<ChangeEditingMode>true</ChangeEditingMode>",
        "<NewRowShowCheck type=\"NewRowShowCheckVariant\" member=\"DontCheck\"/>",
        "<Autonumeration type=\"AutonumerationInForm\" member=\"DontUse\"/>",
        "<Tree>true</Tree><Events><BeforeParentChange id=\"5\">ParentHandler</BeforeParentChange><BeforeSetDeletionMark id=\"6\">DeleteHandler</BeforeSetDeletionMark><BeforeExpand id=\"7\">ExpandHandler</BeforeExpand><BeforeCollapse id=\"8\">CollapseHandler</BeforeCollapse></Events>",
        "<Tree>false</Tree><EditType type=\"ListEditMode\" member=\"InDialog\"/><Autonumeration type=\"AutonumerationInForm\" member=\"Auto\"/>"};
    for (std::size_t index = 0; index < variants.size(); ++index) {
        const auto parsed = source::parse_form_xml(prefix + variants[index] + suffix);
        expect(parsed.ok(), parsed ? "CatalogList source matrix parses" : parsed.diagnostics().front().message);
        const auto xml = source::serialize_form_xml(parsed.value());
        expect(xml.ok(), "CatalogList matrix serializes named XML");
        const auto reparsed = source::parse_form_xml(xml.value());
        expect(reparsed.ok(), "CatalogList matrix reparses without metadata context");
        const auto encoded = oof::storage::form_stream::encode_document(reparsed.value());
        expect(encoded.ok(), encoded ? "CatalogList matrix encodes" : encoded.diagnostics().front().message);
        const auto decoded = oof::storage::form_stream::decode_document(encoded.value(), "CatalogRows");
        expect(decoded.ok() && decoded.value().reconstruction_complete(), decoded ? "CatalogList matrix is complete" : decoded.diagnostics().front().message);
        const auto rebuilt_xml = source::serialize_form_xml(decoded.value());
        expect(rebuilt_xml.ok() && rebuilt_xml.value() == xml.value(), "CatalogList properties, events and family column identities survive XML-only roundtrip");
        const auto& columns = std::get<model::TablePayload>(decoded.value().find_control(model::ObjectId{3})->payload).columns;
        expect(std::get<model::TableMetadataColumnField>(*columns[1].source).field.name == "Code" &&
            std::get<model::TableMetadataColumnField>(*columns[2].source).field.name == "Description" &&
            std::get<model::TableMetadataColumnField>(*columns[3].source).field.uuid.has_value(),
            "Catalog standard and UUID columns keep their named families");
        if (index == 0) {
            auto legacy = encoded.value();
            auto& body = legacy.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(3).items.at(1);
            body.items[0].atom = "11"; body.items.pop_back();
            const auto old = oof::storage::form_stream::decode_document(legacy, "CatalogRows");
            expect(old.ok() && old.value().reconstruction_complete(), "CatalogList source v11 with omitted Autonumeration decodes completely");
            const auto old_xml = source::serialize_form_xml(old.value());
            expect(old_xml.ok() && old_xml.value() == xml.value(), "CatalogList v11 uses proven Auto default");
            for (unsigned bad = 0; bad < 6; ++bad) {
                auto changed = encoded.value();
                auto& source = changed.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(3).items.at(1);
                using LV = oof::storage::list_stream::ListValue;
                if (bad == 0) source.items[0].atom = "10";
                if (bad == 1) source.items.push_back(LV::raw_atom("0"));
                if (bad == 2) source.items[1].atom = "2";
                if (bad == 3) source.items[2].atom = "99";
                if (bad == 4) source.items[9] = oof::storage::list_stream::parse("{1,1,{0,55555555-5555-5555-5555-555555555555,{1,0,70005}}}");
                if (bad == 5) source.items[10].items[0].atom = "4";
                const auto rejected = oof::storage::form_stream::decode_document(changed, "CatalogRows");
                expect(!rejected, "CatalogList unknown versions, arity, values and binding factories remain explicit rejects");
                if (bad == 4) expect(rejected.diagnostics().front().message.find("binding factory") != std::string::npos,
                    "unknown source bindings have an exact diagnostic and are not dropped");
            }
            auto wrong = reparsed.value();
            auto* attribute = const_cast<model::Attribute*>(wrong.find_attribute(model::ObjectId{0}));
            attribute->type.entries.front().type_name = model::QualifiedName{"DocumentList.Other"};
            expect(!oof::storage::form_stream::encode_document(wrong), "CatalogList source rejects a conflicting generated type family");
            attribute->type.entries.front().type_name = model::QualifiedName{"CatalogList.Items"};
            expect(oof::storage::form_stream::encode_document(wrong).ok(), "CatalogList explicit type UUID and family name build without external metadata");
        }
    }
    expect(!source::parse_form_xml(prefix + "<EditType type=\"ListEditMode\" member=\"Unknown\"/>" + suffix),
        "CatalogList unknown enumeration member is rejected");
    auto wrong_column = prefix;
    const auto name = wrong_column.find("MetadataField name=\"Code\"");
    wrong_column.replace(name, std::string("MetadataField name=\"Code\"").size(), "MetadataField name=\"Number\"");
    expect(!source::parse_form_xml(wrong_column + suffix), "CatalogList does not reinterpret DocumentList standard columns");
}

void test_catalog_source_bindings_matrix() {
    const std::string prefix = R"XML(<Form id="1" name="CatalogBindings" ordinaryFormVersion="2.1"><Attributes>
<Attribute id="0" name="Rows"><TypeDomain><Entry term="object" typeUuid="11111111-1111-1111-1111-111111111111"/></TypeDomain></Attribute>
<Attribute id="2" name="Current"><TypeDomain><Entry term="object" typeUuid="22222222-2222-2222-2222-222222222222"/></TypeDomain></Attribute>
</Attributes><ChildItems><Table id="3" name="Rows"><DataPath attributeId="0"/><Position/><Columns><Column name="Code"><Source><MetadataField name="Code"/></Source><Header/><ValueType><Entry term="string" length="9" variable="true"/></ValueType></Column></Columns><CatalogListTableExtension><Bindings>)XML";
    const std::string suffix = R"XML(</Bindings></CatalogListTableExtension></Table>
<Table id="8" name="EventOwner"><DataPath attributeId="0"/><Position/><Columns><Column name="Code"><Source><MetadataField name="Code"/></Source><Header/><ValueType><Entry term="string" length="9" variable="true"/></ValueType></Column></Columns><CatalogListTableExtension/></Table>
</ChildItems></Form>)XML";
    constexpr std::array<std::string_view, 3> roles{"Owner", "Parent", "ActiveRow"};
    std::vector<std::string> variants;
    for (const auto role : roles) {
        for (const unsigned depth : {0U, 1U, 3U}) {
            std::string value = "<" + std::string(role) + "><DataPath attributeId=\"2\">";
            for (unsigned index = 0; index < depth; ++index)
                value += "<Member uuid=\"33333333-3333-3333-3333-333333333333\"/>";
            variants.push_back(value + "</DataPath></" + std::string(role) + ">");
        }
        variants.push_back("<" + std::string(role) + "><EventSource event=\"ObjectActivationProcessing\"><FormRef id=\"1\"/></EventSource></" + std::string(role) + ">");
        variants.push_back("<" + std::string(role) + "><EventSource event=\"BeforeExpand\"><ControlRef id=\"8\"/></EventSource></" + std::string(role) + ">");
    }
    variants.push_back("<ActiveRow><EventSource event=\"BeforeExpand\"><ControlRef id=\"8\"/></EventSource></ActiveRow><Owner><DataPath attributeId=\"2\"/></Owner><Parent><EventSource event=\"ObjectActivationProcessing\"><FormRef id=\"1\"/></EventSource></Parent>");
    for (const auto& variant : variants) {
        const auto parsed = source::parse_form_xml(prefix + variant + suffix);
        expect(parsed.ok(), parsed ? "Catalog source binding matrix parses" : parsed.diagnostics().front().message);
        expect(parsed.value().collections().events.empty(), "source event references do not require handler objects");
        const auto copy = parsed.value();
        const auto xml = source::serialize_form_xml(copy);
        expect(xml.ok(), "typed binding graph copy serializes");
        const auto encoded = oof::storage::form_stream::encode_document(copy);
        expect(encoded.ok(), encoded ? "Catalog bindings encode" : encoded.diagnostics().front().message);
        const auto decoded = oof::storage::form_stream::decode_document(encoded.value(), "CatalogBindings");
        expect(decoded.ok() && decoded.value().reconstruction_complete(), decoded ? "Catalog binding graph is complete" : decoded.diagnostics().front().message);
        const auto repeated = source::serialize_form_xml(decoded.value());
        expect(repeated.ok() && repeated.value() == xml.value(), "direct, nested and forward event references survive named XML-only roundtrip");
        const auto& bindings = std::get<model::TablePayload>(decoded.value().find_control(model::ObjectId{3})->payload).source_bindings;
        expect(!bindings.empty(), "nonempty Catalog bindings are retained as named objects");
    }
    std::string remapped = prefix + "<Parent><EventSource event=\"BeforeExpand\"><ControlRef id=\"88\"/></EventSource></Parent><Owner><DataPath attributeId=\"22\"/></Owner>" + suffix;
    remapped.replace(remapped.find("Attribute id=\"2\""), std::string("Attribute id=\"2\"").size(), "Attribute id=\"22\"");
    remapped.replace(remapped.find("Table id=\"8\""), std::string("Table id=\"8\"").size(), "Table id=\"88\"");
    const auto renamed = source::parse_form_xml(remapped);
    expect(renamed.ok(), "renumbered binding owner and Attribute references parse");
    const auto encoded = oof::storage::form_stream::encode_document(renamed.value());
    expect(encoded.ok(), "renumbered typed references encode without stale persisted IDs");
    const auto decoded = oof::storage::form_stream::decode_document(encoded.value(), "CatalogBindings");
    expect(decoded.ok(), "renumbered binding graph decodes");
    const auto xml = source::serialize_form_xml(decoded.value());
    expect(xml.ok() && xml.value().find("<ControlRef id=\"88\"") != std::string::npos &&
        xml.value().find("<DataPath attributeId=\"22\"") != std::string::npos,
        "control and Attribute identities are taken from current typed references");
    for (const auto& invalid : std::array<std::string, 7>{
        "<Owner><DataPath attributeId=\"2\"/></Owner><Owner><DataPath attributeId=\"2\"/></Owner>",
        "<Unknown><DataPath attributeId=\"2\"/></Unknown>",
        "<Owner><DataPath attributeId=\"99\"/></Owner>",
        "<Parent><EventSource event=\"BeforeExpand\"><ControlRef id=\"99\"/></EventSource></Parent>",
        "<Parent><EventSource event=\"Unknown\"><ControlRef id=\"8\"/></EventSource></Parent>",
        "<Parent><EventSource event=\"ObjectActivationProcessing\"><FormRef id=\"99\"/></EventSource></Parent>",
        "<Parent><EventSource event=\"ObjectActivationProcessing\"><FormRef id=\"1\"/><ControlRef id=\"8\"/></EventSource></Parent>"})
        expect(!source::parse_form_xml(prefix + invalid + suffix), "duplicate, dangling, unknown and ambiguous source bindings are rejected");
    for (const unsigned bad : {0U, 1U, 2U}) {
        auto changed = encoded.value();
        auto& links = changed.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(3).items.at(1).items.at(9);
        if (bad == 0) links.items[2].items[0].atom = "99";
        if (bad == 1) links.items[3].items[2].items[2].atom = "69999";
        if (bad == 2) links.items[2].items[2].items[1].atom = "99";
        expect(!oof::storage::form_stream::decode_document(changed, "CatalogBindings"),
            "unknown storage role, event ID and DataPath arity are rejected without dropping references");
    }
    for (const bool document_scope : {false, true}) {
        auto scoped_prefix = prefix;
        const std::string extension = document_scope
            ? "<MainAttribute attributeId=\"4\"/><DocumentFormExtension/>"
            : "<MainAttribute attributeId=\"4\"/><DataProcessorFormExtension/>";
        scoped_prefix.insert(scoped_prefix.find("<Attributes>"), extension);
        scoped_prefix.insert(scoped_prefix.find("</Attributes>"),
            "<Attribute id=\"4\" name=\"Object\"><TypeDomain><Entry term=\"object\" typeUuid=\"99999999-9999-9999-9999-999999999999\"/></TypeDomain></Attribute>");
        const std::string event = document_scope ? "BeforeWrite" : "BeforeSaveValues";
        const auto scoped = source::parse_form_xml(scoped_prefix + "<Parent><EventSource event=\"" + event +
            "\"><FormRef id=\"1\"/></EventSource></Parent>" + suffix);
        expect(scoped.ok(), scoped ? "extension-scoped Form binding parses" : scoped.diagnostics().front().message);
        const auto binary = oof::storage::form_stream::encode_document(scoped.value());
        expect(binary.ok(), binary ? "extension-scoped Form binding encodes" : binary.diagnostics().front().message);
        const auto restored = oof::storage::form_stream::decode_document(binary.value(), "CatalogBindings");
        expect(restored.ok(), restored ? "extension-scoped Form binding decodes" : restored.diagnostics().front().message);
        const auto& binding = std::get<model::TablePayload>(restored.value().find_control(model::ObjectId{3})->payload).source_bindings.front();
        expect(std::get<model::TableEventSource>(binding.source).event == event,
            "storage event ID 80000 resolves through the actual Form extension scope");
        const auto wrong_event = document_scope ? "BeforeSaveValues" : "BeforeWrite";
        expect(!source::parse_form_xml(scoped_prefix + "<Parent><EventSource event=\"" + wrong_event +
            "\"><FormRef id=\"1\"/></EventSource></Parent>" + suffix),
            "Form event source names cannot escape their declared extension scope");
    }
    auto catalog_prefix = prefix;
    catalog_prefix.insert(catalog_prefix.find("<Attributes>"),
        "<MainAttribute attributeId=\"2\"/><CatalogItemFormExtension/>");
    const std::string parent_binding =
        "<Parent><DataPath attributeId=\"2\"><Member name=\"Parent\"/></DataPath></Parent>";
    const auto parent = source::parse_form_xml(catalog_prefix + parent_binding + suffix);
    expect(parent.ok(), "one standard Parent source member parses");
    const auto parent_stream = oof::storage::form_stream::encode_document(parent.value());
    expect(parent_stream.ok(), "one standard Parent source member encodes");
    auto deep_stream = parent_stream.value();
    auto& path = deep_stream.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2)
        .items.at(3).items.at(1).items.at(9).items.at(2).items.at(2);
    path.items[1].atom = "3";
    path.items.push_back(path.items.back());
    expect(!oof::storage::form_stream::decode_document(deep_stream, "CatalogBindings"),
        "native Save drops a continuation through standard Parent, so its input cannot be declared complete");
    expect(!source::parse_form_xml(catalog_prefix +
        "<Parent><DataPath attributeId=\"2\"><Member name=\"Parent\"/><Member name=\"Parent\"/></DataPath></Parent>" + suffix),
        "unrepresentable repeated Parent source paths must fail before writing a BIN");
}

void test_document_list_table_source_xml_and_native() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="List" ordinaryFormVersion="2.1">
<Attributes><Attribute id="0" name="Rows"><TypeDomain><Entry term="object" typeUuid="11111111-1111-1111-1111-111111111111"/></TypeDomain></Attribute></Attributes>
<ChildItems><Table id="3" name="Rows"><DataPath attributeId="0"/><Position/><Columns>
<Column name="Picture"><Source><StandardPicture/></Source><Header/><Width unit="form">24</Width><ValueType><Entry term="string" length="10" variable="true"/></ValueType></Column>
<Column name="Date"><Source><MetadataField name="Date"/></Source><Header><Item language="en">Date</Item></Header><Width unit="form">140</Width><Control type="InputField"><ValueType><Entry term="date" date="true" time="true"/></ValueType></Control></Column>
<Column name="Number"><Source><MetadataField name="Number"/></Source><Header><Item language="en">Number</Item></Header><ValueType><Entry term="string" length="9" variable="true"/></ValueType></Column>
<Column name="Custom"><Source><MetadataField uuid="44444444-4444-4444-4444-444444444444"/></Source><Header/><ValueType><Entry term="boolean"/></ValueType></Column>
</Columns><DocumentListTableExtension><EnablePeriodSetting>false</EnablePeriodSetting><Events>
<BeforeSetDeletionMark id="20">HandleDeletion</BeforeSetDeletionMark><BeforePosting id="21">HandlePosting</BeforePosting><BeforeUndoPosting id="22">HandleUndo</BeforeUndoPosting><BeforeDateChange id="23">HandleDate</BeforeDateChange>
</Events></DocumentListTableExtension><Events><Selection id="24">HandleSelection</Selection></Events></Table></ChildItems></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "named DocumentList source must parse" : parsed.diagnostics().front().message);
    const auto* table_control = parsed.value().find_control(model::ObjectId{3});
    const auto& table = std::get<model::TablePayload>(table_control->payload);
    expect(table.source_extension == model::TableSourceExtensionKind::document_list && table.columns.size() == 4 &&
        table.columns.front().header.items.empty() && !table.columns.front().control,
        "source role, absent picture editor and empty header must be independent");
    const auto public_xml = source::serialize_form_xml(parsed.value());
    expect(public_xml.ok() && public_xml.value().find("DocumentListTableExtension") != std::string::npos &&
        public_xml.value().find("uuid=\"44444444-4444-4444-4444-444444444444\"") != std::string::npos,
        "source identity and extension events must serialize by name without configuration metadata");
    const auto repeated = source::parse_form_xml(public_xml.value());
    expect(repeated.ok(), "named source XML must parse after serialization");
    const auto native = oof::storage::form_stream::encode_document(parsed.value());
    expect(native.ok(), native ? "named source must encode without a baseline" : native.diagnostics().front().message);
    const auto has_guid_reference = [&](auto&& self, const oof::storage::list_stream::ListValue& node) -> bool {
        if (!node.is_list) return false;
        if (node.items.size() == 2 && node.items[0].atom == "48a6ebc3-fcc8-4f8f-b399-459fb32aa46b") {
            const auto& key = node.items[1].items[1].items[2];
            if (key.is_list && key.items.size() == 2 && key.items[0].atom == "0" &&
                key.items[1].atom == "44444444-4444-4444-4444-444444444444") return true;
        }
        return std::ranges::any_of(node.items, [&](const auto& child) { return self(self, child); });
    };
    expect(has_guid_reference(has_guid_reference, native.value()),
        "metadata GUID source must use the observed CompositeID prefix, not a symmetric invented GUID record");
    const auto decoded = oof::storage::form_stream::decode_document(native.value(), "List");
    expect(decoded.ok(), decoded ? "named source must decode" : decoded.diagnostics().front().message);
    expect(decoded.value().reconstruction_complete(), "the constructed named source must rebuild completely");
    auto small_allocator = native.value();
    small_allocator.items[2].items[1] = oof::storage::list_stream::ListValue::raw_atom("2");
    const auto small_decoded = oof::storage::form_stream::decode_document(small_allocator, "List");
    expect(small_decoded.ok() && small_decoded.value().reconstruction_complete(),
        "native-confirmed two-slot allocator with Attribute zero must decode without configuration metadata");
    small_allocator.items[2].items[1] = oof::storage::list_stream::ListValue::raw_atom("1");
    expect(!oof::storage::form_stream::decode_document(small_allocator, "List"),
        "the smaller unproven allocator layout must remain rejected");
    const auto* decoded_control = decoded.value().find_control(model::ObjectId{3});
    const auto& decoded_table = std::get<model::TablePayload>(decoded_control->payload);
    expect(decoded_table.source_extension == table.source_extension && decoded_table.columns.size() == 4 &&
        decoded_table.columns[1].source == table.columns[1].source &&
        decoded_table.columns[3].source == table.columns[3].source &&
        decoded_table.columns[1].layout_width == table.columns[1].layout_width,
        "named field GUID, builtin source and layout width must survive native serialization");
    expect(decoded.value().get_prop_val({model::ObjectCategory::control, model::ObjectId{3}},
        model::PropertyId::from_name("EnablePeriodSetting")).value == model::PropertyValue{false},
        "the source Boolean must survive native serialization");
    expect(decoded_control->events.size() == 5, "common and four extension events must survive separate event tables");
    const auto decoded_xml = source::serialize_form_xml(decoded.value());
    expect(decoded_xml.ok() && decoded_xml.value().find("<BeforePosting") != std::string::npos &&
        decoded_xml.value().find("<Selection") != std::string::npos,
        "source and common event scopes must both remain editable XML");
    for (const auto& property : model::metamodel::table_column_boolean_descriptors()) {
        auto changed = parsed.value();
        auto& column = std::get<model::TablePayload>(const_cast<model::ControlNode*>(
            changed.find_control(model::ObjectId{3}))->payload).columns[1];
        column.*property.member = !property.default_value;
        const auto changed_xml = source::serialize_form_xml(changed);
        expect(changed_xml.ok() && changed_xml.value().find("<" + std::string(property.name) + ">") != std::string::npos,
            "each common Column Boolean must have one editable named XML property");
        const auto xml_model = source::parse_form_xml(changed_xml.value());
        expect(xml_model.ok(), "each common Column Boolean XML must parse");
        const auto stream = oof::storage::form_stream::encode_document(xml_model.value());
        expect(stream.ok(), "each common Column Boolean must encode through its shared descriptor");
        const auto model = oof::storage::form_stream::decode_document(stream.value(), "BooleanColumn");
        expect(model.ok() && model.value().reconstruction_complete(), "each common Column Boolean must decode completely");
        const auto& result = std::get<model::TablePayload>(model.value().find_control(model::ObjectId{3})->payload).columns[1];
        expect((result.*property.member) == !property.default_value, "Column Boolean must retain its typed value");
    }
    for (const auto& property : model::metamodel::table_column_enumeration_descriptors()) {
        for (const auto& member : property.members) {
            auto changed = parsed.value();
            auto& column = std::get<model::TablePayload>(const_cast<model::ControlNode*>(
                changed.find_control(model::ObjectId{3}))->payload).columns[1];
            column.*property.member = {std::string(property.enumeration_type), std::string(member.name)};
            const auto xml_value = source::serialize_form_xml(changed);
            expect(xml_value.ok(), "named Column alignment must serialize");
            const auto xml_model = source::parse_form_xml(xml_value.value());
            expect(xml_model.ok(), "named Column alignment must parse");
            const auto stream = oof::storage::form_stream::encode_document(xml_model.value());
            expect(stream.ok(), "named Column alignment must encode");
            const auto model = oof::storage::form_stream::decode_document(stream.value(), "AlignedColumn");
            expect(model.ok() && model.value().reconstruction_complete(), "named Column alignment must decode completely");
            const auto& result = std::get<model::TablePayload>(model.value().find_control(model::ObjectId{3})->payload).columns[1];
            expect((result.*property.member) == (column.*property.member), "named Column alignment must retain its enum type and member");
        }
    }
    auto mutate = [&](std::string_view before, std::string_view after) {
        std::string changed(xml); const auto at = changed.find(before);
        expect(at != std::string::npos, "negative source test must replace existing content");
        changed.replace(at, before.size(), after); return changed;
    };
    for (const auto& bad : {
        mutate("<MetadataField name=\"Date\"/>", "<MetadataField/>"),
        mutate("<MetadataField name=\"Date\"/>", "<MetadataField name=\"UnknownBuiltin\"/>"),
        mutate("<Source><StandardPicture/></Source>", "<DataPath>Picture</DataPath><Source><StandardPicture/></Source>"),
        mutate("<StandardPicture/>", "<StandardPicture index=\"1\"/>"),
        mutate("<EnablePeriodSetting>false</EnablePeriodSetting>", "<AutoRefresh>true</AutoRefresh>"),
        mutate("<BeforePosting id=\"21\">HandlePosting</BeforePosting>", "<Selection id=\"21\">HandlePosting</Selection>"),
        mutate("<Entry term=\"object\" typeUuid=\"11111111-1111-1111-1111-111111111111\"/>", "<Entry term=\"valueTable\"/>")}) {
        expect(!source::parse_form_xml(bad), "source XML must reject unknown identity, wrong scope, mixed sources and invalid values");
    }
}

void test_table_columns_without_editors_xml() {
    const auto xml = [](std::string_view type) {
        return std::string(R"XML(<Form id="1" name="NoEditors" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes><ChildItems><Table id="3" name="Rows"><DataPath attributeId="2"/><Position/><Columns><Column name="Code"><DataPath>Code</DataPath><Header><Item language="en">Code</Item></Header><Width unit="form">140</Width><TextFont kind="absolute" faceName="Arial" height="10"/><Visible>false</Visible>)XML") +
            std::string(type) + "</Column></Columns></Table></ChildItems></Form>";
    };
    for (const auto value_type : {std::string_view{},
             std::string_view{R"XML(<ValueType><Entry term="string" length="20" variable="true"/></ValueType>)XML"},
             std::string_view{R"XML(<ValueType><Entry term="boolean"/></ValueType>)XML"}}) {
        auto parsed = source::parse_form_xml(xml(value_type));
        expect(parsed.ok(), "Column without Control must parse with an independent stored type");
        const auto& column = std::get<model::TablePayload>(parsed.value()
            .find_control(model::ObjectId{3})->payload).columns.front();
        expect(!column.control && column.value_type.has_value() == !value_type.empty(),
            "omitting Control must preserve an absent editor and the optional stored type owner");
        expect(column.data_path == "Code" && column.header.items.front().text == "Code" &&
            column.layout_width == model::DecimalValue{"140"} && !column.visible &&
            column.text_font.kind == model::FontKind::absolute && column.text_font.face_name == "Arial",
            "Column data, header, layout, font, and visibility must be independent of an editor");
        const auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok() && serialized.value().find("<Control") == std::string::npos &&
            serialized.value().find("InputField") == std::string::npos &&
            (serialized.value().find("<ValueType>") != std::string::npos) == !value_type.empty(),
            "Column XML must retain its stored type without inventing an editor");
        const auto again = source::parse_form_xml(serialized.value());
        expect(again.ok(), "Column without Control must parse again");
        const auto& repeated = std::get<model::TablePayload>(again.value()
            .find_control(model::ObjectId{3})->payload).columns.front();
        expect(!repeated.control && repeated.value_type == column.value_type &&
            repeated.name == column.name && repeated.data_path == column.data_path &&
            repeated.header == column.header && repeated.layout_width == column.layout_width &&
            repeated.text_font == column.text_font && repeated.visible == column.visible,
            "editor absence, stored type, and independent properties must survive XML roundtrip");

        const auto native = oof::storage::form_stream::encode_document(parsed.value());
        expect(native.ok(), "Column without Control must encode without a baseline");
        const auto decoded = oof::storage::form_stream::decode_document(native.value(), "NoEditors");
        expect(decoded.ok() && decoded.value().reconstruction_complete(),
            "Column without Control must decode as a complete named model");
        const auto& native_column = std::get<model::TablePayload>(decoded.value()
            .find_control(model::ObjectId{3})->payload).columns.front();
        expect(!native_column.control && native_column.value_type == column.value_type,
            "native roundtrip must retain the absent editor and its independently stored type");
        const auto native_xml = source::serialize_form_xml(decoded.value());
        expect(native_xml.ok() && native_xml.value().find("<Control") == std::string::npos,
            "native Column without an editor must not acquire a phantom XML Control");
    }
    for (const auto type : {std::string_view{"<ValueType/>"},
             std::string_view{R"XML(<ValueType><Entry term="boolean"/></ValueType>)XML"}}) {
        expect(!source::parse_form_xml(xml(std::string(type) + "<Control type=\"InputField\"/>")).ok() &&
            !source::parse_form_xml(xml(std::string("<Control type=\"InputField\"/>") + std::string(type))).ok(),
            "Column ValueType must reject any Control owner, including an empty stored type and either order");
    }
    auto ambiguous = source::parse_form_xml(xml("<ValueType/>"));
    expect(ambiguous.ok(), "an explicit empty Column stored type must normalize to the default");
    const auto empty_xml = source::serialize_form_xml(ambiguous.value());
    expect(empty_xml.ok() && empty_xml.value().find("<ValueType") == std::string::npos,
        "empty Column ValueType must be omitted consistently with native default decoding");
    auto& column = std::get<model::TablePayload>(const_cast<model::ControlNode*>(ambiguous.value()
        .find_control(model::ObjectId{3}))->payload).columns.front();
    expect(!column.value_type, "empty Column ValueType must parse as an absent default");
    column.value_type.emplace();
    column.control.emplace();
    expect(!source::serialize_form_xml(ambiguous.value()).ok(),
        "writer must reject a Column with both Control and even an empty ValueType");
    expect(!source::parse_form_xml(xml("<Control/>")).ok(),
        "a present Column Control must retain its required type attribute");
}

void test_table_column_layout_xml() {
    const auto make_xml = [](std::string_view width, std::string_view font) {
        return std::string(R"XML(<Form id="1" name="LayoutForm" ordinaryFormVersion="2.1"><Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes><ChildItems><Table id="3" name="Rows"><DataPath attributeId="2"/><Position/><Columns><Column name="Code"><DataPath>Code</DataPath><Header><Item language="en">Code</Item></Header>)XML") +
            std::string(width) + std::string(font) +
            R"XML(<Control type="InputField"/></Column></Columns></Table></ChildItems></Form>)XML";
    };

    const std::string layout_xml = make_xml(
        R"XML(<Width unit="form">140</Width>)XML",
        R"XML(<TextFont kind="absolute" faceName="Arial" height="10"/>)XML");
    const auto parsed = source::parse_form_xml(layout_xml);
    expect(parsed.ok(), parsed ? "Table Column layout width and TextFont must parse" :
        parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto* table = parsed.value().find_control(model::ObjectId{3});
    expect(table != nullptr, "layout XML must expose the named Table");
    const auto& column = std::get<model::TablePayload>(table->payload).columns.front();
    expect(column.layout_width == model::DecimalValue{"140"} &&
               column.text_font.kind == model::FontKind::absolute &&
               column.text_font.face_name == "Arial" && column.text_font.height == 10.0,
        "Table Column layout width and named font data must remain typed values");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Width unit=\"form\">140</Width>") != std::string::npos &&
               serialized.value().find("<TextFont kind=\"absolute\" faceName=\"Arial\" height=\"10\"/>") != std::string::npos,
        "Table Column layout values must serialize with the form unit and named font properties");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && reparsed.value().find_control(model::ObjectId{3}) != nullptr &&
               std::get<model::TablePayload>(reparsed.value().find_control(model::ObjectId{3})->payload)
                       .columns.front().layout_width == column.layout_width &&
               std::get<model::TablePayload>(reparsed.value().find_control(model::ObjectId{3})->payload)
                       .columns.front().text_font == column.text_font,
        "Table Column layout values must survive XML roundtrip");

    const auto default_xml = source::parse_form_xml(make_xml("", R"XML(<TextFont kind="automatic"/>)XML"));
    expect(default_xml.ok(), "automatic TextFont may be represented explicitly in XML");
    const auto default_serialized = source::serialize_form_xml(default_xml.value());
    expect(default_serialized.ok() && default_serialized.value().find("TextFont") == std::string::npos,
        "explicit automatic TextFont must normalize to the omitted default");

    for (const auto& [width, font, label] : std::array{
             std::tuple<std::string_view, std::string_view, std::string_view>{"<Width>100</Width>", "", "missing Width unit"},
             std::tuple<std::string_view, std::string_view, std::string_view>{"<Width unit=\"pixels\">100</Width>", "", "wrong Width unit"},
             std::tuple<std::string_view, std::string_view, std::string_view>{"<Width unit=\"form\">-1</Width>", "", "negative Width"},
             std::tuple<std::string_view, std::string_view, std::string_view>{"<Width unit=\"form\">not-a-number</Width>", "", "nonnumeric Width"},
             std::tuple<std::string_view, std::string_view, std::string_view>{"", "<TextFont kind=\"automatic\" unexpected=\"true\"/>", "unknown TextFont property"}}) {
        expect(!source::parse_form_xml(make_xml(width, font)).ok(),
            std::string("Table Column must reject ") + std::string(label));
    }
}

void test_inherited_property_has_one_surface() {
    constexpr std::string_view xml =
        "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<RadioButton id=\"2\" name=\"Choice\"><FirstInGroup>true</FirstInGroup>"
        "<Position/></RadioButton></ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "inherited FirstInGroup must parse on the shared extension surface");
    const model::ControlNode* control = parsed.value().find_control(model::ObjectId{2});
    expect(control != nullptr, "RadioButton must resolve");
    expect(
        control->extension_properties.contains(model::PropertyId::from_name("FirstInGroup")),
        "FirstInGroup must materialize exactly once in extension properties");
    expect(
        !control->properties().contains(model::PropertyId::from_name("FirstInGroup")),
        "FirstInGroup must not be duplicated in RadioButton payload");
    expect(source::serialize_form_xml(parsed.value()).ok(), "canonical inherited property must serialize");

    constexpr std::string_view numeric_group_xml = R"XML(
<Form id="1" name="RadioGroup" ordinaryFormVersion="2.1">
  <Attributes><Attribute id="10" name="Choice"><TypeDomain><Entry term="numeric" length="10" precision="0" nonNegative="true"/></TypeDomain></Attribute></Attributes>
  <ChildItems>
    <RadioButton id="20" name="Head"><DataPath attributeId="10"/><FirstInGroup>true</FirstInGroup>
      <ValueType><Entry term="numeric" length="10" precision="0" nonNegative="true"/></ValueType>
      <Position/><SelectionValue>1</SelectionValue>
    </RadioButton>
    <RadioButton id="21" name="Member"><Position/><SelectionValue>0</SelectionValue></RadioButton>
  </ChildItems>
</Form>)XML";
    const auto numeric_group = source::parse_form_xml(numeric_group_xml);
    expect(numeric_group.ok(), "numeric SelectionValue and linked RadioButton group must parse as named properties");
    const auto* numeric_head = numeric_group.value().find_control(model::ObjectId{20});
    const auto* selected_value = numeric_head == nullptr ? nullptr :
        numeric_head->properties().find(model::PropertyId::from_name("SelectionValue"));
    expect(selected_value && std::get<model::DecimalValue>(selected_value->value).canonical == "1",
        "RadioButton SelectionValue must use the existing DecimalValue model");
    const auto canonical_group = source::serialize_form_xml(numeric_group.value());
    expect(canonical_group.ok() && canonical_group.value().find("<SelectionValue>1</SelectionValue>") != std::string::npos,
        "RadioButton SelectionValue must serialize as named xs:decimal text");

    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<RadioButton id=\"2\" name=\"Choice\"><Position/>"
            "<FirstInGroup>true</FirstInGroup></RadioButton></ChildItems></Form>"),
        "OOF2002",
        "inherited property must not have a second payload position");
}

void test_xml_character_normalization_is_lossless() {
    constexpr std::string_view xml =
        "<Form id=\"1\" name=\"A&#xA;B&#x9;C&#xD;D\" ordinaryFormVersion=\"2.1\">"
        "<ChildItems><Button id=\"2\" name=\"B\"><Position/>"
        "<Caption>A&#xD;B&#xA;C&#x9;D</Caption></Button></ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "XML character references must parse");
    const std::string expected_name = "A\nB\tC\rD";
    expect(parsed.value().form().name == expected_name, "attribute character references must reach the model unchanged");

    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "model containing XML-normalized characters must serialize");
    expect(
        serialized.value().find("name=\"A&#xA;B&#x9;C&#xD;D\"") != std::string::npos,
        "attribute whitespace must use character references in canonical XML");
    expect(
        serialized.value().find("A&#xD;B\nC\tD</Caption>") != std::string::npos,
        "text CR must use a character reference while LF/TAB remain literal text");

    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "canonical character references must reparse");
    expect(reparsed.value().form().name == expected_name, "attribute whitespace must survive source roundtrip");
    auto repeated = source::serialize_form_xml(reparsed.value());
    expect(repeated.ok() && repeated.value() == serialized.value(), "character-normalized XML must reach a fixed point");
}

void test_strict_rejections() {
    expect_code(
        source::parse_form_xml("<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.0\"/>"),
        "OOF2002",
        "legacy 2.0 must be rejected by XSD");
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<Page id=\"2\" name=\"LegacyPage\"/></ChildItems></Form>"),
        "OOF2002",
        "legacy Page id must be rejected");
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<Button id=\"2\" name=\"B\"><Caption>X</Caption><Position/></Button>"
            "</ChildItems></Form>"),
        "OOF2002",
        "wrong property order must be rejected by XSD");
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<Button id=\"2\" name=\"B\"><Position/><Caption>A</Caption><Caption>B</Caption></Button>"
            "</ChildItems></Form>"),
        "OOF2002",
        "duplicate property must be rejected by XSD");
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><Unknown/></Form>"),
        "OOF2002",
        "unknown concept must be rejected by XSD");
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<Button id=\"2\" name=\"B\"><Position/><RawBracket>value</RawBracket></Button>"
            "</ChildItems></Form>"),
        "OOF2002",
        "raw storage vocabulary must be rejected by XSD");
    expect_code(
        source::parse_form_xml(
            "<!DOCTYPE Form [<!ELEMENT Form ANY>]><Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"/>"),
        "OOF2001",
        "DTD must be rejected before semantic parsing");
    expect_code(
        source::parse_form_xml(
            "<!DOCTYPE Form [<!ENTITY xxe SYSTEM \"file:///definitely-not-an-oof-source\">]>"
            "<Form id=\"1\" name=\"&xxe;\" ordinaryFormVersion=\"2.1\"/>"),
        "OOF2001",
        "external entities must be rejected before expansion");
    for (const auto& [path, format] : {std::pair{"CaptionPicture.gif", "gif"}, std::pair{"CaptionPicture.png", "png"},
            std::pair{"CaptionPicture.jpeg", "jpeg"}, std::pair{"CaptionPicture.bmp", "bmp"}}) {
        const std::string xml = "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><CaptionPicture>2</CaptionPicture><PictureAssets><PictureAsset id=\"2\" relativePath=\"" +
            std::string(path) + "\" format=\"" + format + "\"/></PictureAssets></Form>";
        expect(source::parse_form_xml(xml).ok(), "form caption picture path must be accepted");
    }
    for (const auto path : {"Other.gif", "../CaptionPicture.gif", "CaptionPicture.png"}) {
        const std::string xml = "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><CaptionPicture>2</CaptionPicture><PictureAssets><PictureAsset id=\"2\" relativePath=\"" +
            std::string(path) + "\" format=\"gif\"/></PictureAssets></Form>";
        expect_code(source::parse_form_xml(xml), "OOF2003", "invalid form picture path must be rejected");
    }
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><PictureAssets>"
            "<PictureAsset id=\"2\" relativePath=\"../Picture.gif\" format=\"gif\"/>"
            "</PictureAssets></Form>"),
        "OOF2003",
        "picture path traversal must be rejected semantically");
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<Button id=\"2\" name=\"B\"><Position/>"
            "<BorderColor kind=\"styleReference\" styleObjectId=\"0\" "
            "styleUuid=\"00000000-0000-0000-0000-000000000000\"/>"
            "</Button></ChildItems></Form>"),
        "OOF2003",
        "null style CompositeId must be rejected semantically");
    expect_code(
        source::parse_form_xml(
            "<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><Commands>"
            "<Command id=\"2\" name=\"Run\" handler=\"Run\"><Picture>99</Picture></Command>"
            "</Commands></Form>"),
        "OOF2004",
        "dangling picture references must fail model validation");
}

void test_label_horizontal_align_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <LabelDecoration id="8" name="AutoLabel"><Position/><HorizontalAlign type="HorizontalAlign" member="Auto"/><Caption>Automatic</Caption></LabelDecoration>
  <LabelDecoration id="4" name="LeftLabel"><Position/><HorizontalAlign type="HorizontalAlign" member="Left"/><Caption>Left aligned</Caption></LabelDecoration>
  <LabelDecoration id="9" name="CenterLabel"><Position/><HorizontalAlign type="HorizontalAlign" member="Center"/><Caption>Centered</Caption></LabelDecoration>
  <LabelDecoration id="10" name="RightLabel"><Position/><HorizontalAlign type="HorizontalAlign" member="Right"/><Caption>Right aligned</Caption></LabelDecoration>
</ChildItems></Form>
)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "LabelDecoration HorizontalAlign values must parse as typed enumerations");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "LabelDecoration HorizontalAlign values must serialize");
    expect(serialized.value().find("type=\"HorizontalAlign\" member=\"Auto\"") != std::string::npos &&
               serialized.value().find("type=\"HorizontalAlign\" member=\"Left\"") != std::string::npos &&
               serialized.value().find("type=\"HorizontalAlign\" member=\"Center\"") != std::string::npos &&
               serialized.value().find("type=\"HorizontalAlign\" member=\"Right\"") != std::string::npos,
        "LabelDecoration Auto, Left, Center, and Right XML values must survive named serialization");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && reparsed.value().collections().controls.size() == 4,
        "canonical LabelDecoration alignment XML must reparse");
}

void test_label_enabled_tooltip_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <LabelDecoration id="3" name="Notice"><Position/><Enabled>false</Enabled><ToolTip>Подсказка Ω &amp; &lt;важно&gt; "цитата"
Вторая строка</ToolTip></LabelDecoration>
</ChildItems></Form>
)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "LabelDecoration Enabled and ToolTip XML must parse");
    const auto* label = parsed.value().find_control(model::ObjectId{3});
    expect(label != nullptr, "LabelDecoration must resolve after XML parsing");
    const auto* enabled = label->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* tool_tip = label->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(enabled && !std::get<bool>(enabled->value), "LabelDecoration Enabled=false must parse as Boolean");
    expect(tool_tip && std::get<std::string>(tool_tip->value) ==
               "Подсказка Ω & <важно> \"цитата\"\nВторая строка",
        "LabelDecoration ToolTip must preserve Unicode, XML punctuation, and newline text");

    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Enabled>false</Enabled>") != std::string::npos &&
               serialized.value().find("&amp;") != std::string::npos &&
               serialized.value().find("&lt;важно&gt;") != std::string::npos,
        "LabelDecoration properties must serialize as named, escaped XML values");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "serialized LabelDecoration properties must reparse");
    const auto* reparsed_label = reparsed.value().find_control(model::ObjectId{3});
    const auto* reparsed_tool_tip = reparsed_label == nullptr ? nullptr : reparsed_label->properties().find(
        model::PropertyId::from_name("ToolTip"));
    expect(reparsed_tool_tip && std::get<std::string>(reparsed_tool_tip->value) ==
               "Подсказка Ω & <важно> \"цитата\"\nВторая строка",
        "LabelDecoration ToolTip text must survive XML serialization and reparsing");

    constexpr std::string_view explicit_defaults_xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <LabelDecoration id="3" name="Notice"><Position/><Enabled>true</Enabled><ToolTip></ToolTip></LabelDecoration>
</ChildItems></Form>
)XML";
    const auto explicit_defaults = source::parse_form_xml(explicit_defaults_xml);
    expect(explicit_defaults.ok(), "explicit LabelDecoration defaults must parse");
    const auto defaults_serialized = source::serialize_form_xml(explicit_defaults.value());
    expect(defaults_serialized.ok() &&
               defaults_serialized.value().find("<Enabled>") == std::string::npos &&
               defaults_serialized.value().find("<ToolTip>") == std::string::npos,
        "explicit LabelDecoration Enabled=true and empty ToolTip must be omitted from public XML");
}

void test_picture_decoration_enabled_tooltip_xml_roundtrip() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <PictureDecoration id="3" name="Illustration"><Position/><Enabled>false</Enabled><ToolTip>Подсказка Ω &amp; &lt;важно&gt; "цитата"
Вторая строка</ToolTip></PictureDecoration>
</ChildItems></Form>
)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "PictureDecoration Enabled and ToolTip XML must parse");
    const auto* picture = parsed.value().find_control(model::ObjectId{3});
    expect(picture && picture->kind() == model::ControlKind::picture_decoration,
        "PictureDecoration must resolve by its named XML type");
    const auto* enabled = picture->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* tool_tip = picture->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(enabled && !std::get<bool>(enabled->value), "PictureDecoration Enabled=false must parse as Boolean");
    expect(tool_tip && std::get<std::string>(tool_tip->value) ==
               "Подсказка Ω & <важно> \"цитата\"\nВторая строка",
        "PictureDecoration ToolTip must preserve Unicode, escaped punctuation, and newline text");

    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Enabled>false</Enabled>") != std::string::npos &&
               serialized.value().find("&amp;") != std::string::npos &&
               serialized.value().find("&lt;важно&gt;") != std::string::npos,
        "PictureDecoration properties must serialize as named escaped XML values");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "serialized PictureDecoration XML must reparse");
    const auto* reparsed_picture = reparsed.value().find_control(model::ObjectId{3});
    const auto* reparsed_tool_tip = reparsed_picture == nullptr ? nullptr : reparsed_picture->properties().find(
        model::PropertyId::from_name("ToolTip"));
    expect(reparsed_tool_tip && std::get<std::string>(reparsed_tool_tip->value) ==
               "Подсказка Ω & <важно> \"цитата\"\nВторая строка",
        "PictureDecoration ToolTip must survive XML serialization and reparsing");

    constexpr std::string_view defaults_xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <PictureDecoration id="3" name="Illustration"><Position/><Enabled>true</Enabled><ToolTip></ToolTip></PictureDecoration>
</ChildItems></Form>
)XML";
    const auto defaults = source::parse_form_xml(defaults_xml);
    expect(defaults.ok(), "explicit PictureDecoration defaults must parse");
    const auto defaults_serialized = source::serialize_form_xml(defaults.value());
    expect(defaults_serialized.ok() && defaults_serialized.value().find("<Enabled>") == std::string::npos &&
               defaults_serialized.value().find("<ToolTip>") == std::string::npos,
        "explicit PictureDecoration Enabled=true and empty ToolTip must be omitted from public XML");
}

void test_picture_decoration_standard_picture_xml_roundtrip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "PictureReference";
    form.children.push_back(model::ControlRef{model::ObjectId{2}});
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode picture{model::ObjectId{2}, "Illustration", model::PictureDecorationPayload{}};
    picture.properties().set_explicit(model::PropertyId::from_name("Picture"),
        model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
            model::QualifiedName{"PictureLib.Write"}});
    document.add_control(std::move(picture));

    const auto xml = source::serialize_form_xml(document);
    expect(xml.ok() && xml.value().find("<Picture standardName=\"PictureLib.Write\"/>") !=
               std::string::npos,
        "PictureDecoration.Picture must serialize as a named standard-picture reference");
    const auto parsed = source::parse_form_xml(xml.value());
    const auto* restored = parsed.ok() ? parsed.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* property = restored == nullptr ? nullptr : restored->properties().find(
        model::PropertyId::from_name("Picture"));
    expect(property && std::get<model::PictureRef>(property->value).standard_name ==
               model::QualifiedName{"PictureLib.Write"},
        "PictureDecoration standard picture reference must survive XML-only roundtrip");

    std::string unknown = xml.value();
    const auto at = unknown.find("PictureLib.Write");
    unknown.replace(at, std::string("PictureLib.Write").size(), "PictureLib.Unknown");
    expect(!source::parse_form_xml(unknown).ok(),
        "unknown PictureDecoration standard-picture names must be rejected");
}

void test_button_foreign_enum_default_is_retained() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
  <Button id="2" name="ForeignDefault"><Position/><HorizontalAlign type="VerticalAlign" member="Center"/></Button>
</ChildItems></Form>
)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "foreign typed Button default must remain representable for validation");
    const auto* button = parsed.value().find_control(model::ObjectId{2});
    const auto* alignment = button == nullptr ? nullptr : button->properties().find(
        model::PropertyId::from_name("HorizontalAlign"));
    expect(alignment && std::get<model::EnumerationValue>(alignment->value) ==
               model::EnumerationValue{"VerticalAlign", "Center"},
        "a matching member with the wrong enum type must remain explicit");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find(
               "<HorizontalAlign type=\"VerticalAlign\" member=\"Center\"/>") != std::string::npos,
        "XML serialization must not silently normalize a foreign typed enum default");
}

void test_standard_picture_xml_reference_roundtrip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Pictures";
    form.children.push_back(model::ControlRef{model::ObjectId{2}});
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode button{model::ObjectId{2}, "Write", model::ButtonPayload{}};
    button.properties().set_explicit(model::PropertyId::from_name("Picture"),
        model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
            model::QualifiedName{"PictureLib.Write"}});
    document.add_control(std::move(button));

    const auto xml = source::serialize_form_xml(document);
    expect(xml.ok() && xml.value().find("<Picture standardName=\"PictureLib.Write\"/>") != std::string::npos,
        "standard Button.Picture must use a named XML reference without an internal identity");
    const auto parsed = source::parse_form_xml(xml.value());
    expect(parsed.ok(), "known standard picture XML must parse");
    const auto* restored = parsed.value().find_control(model::ObjectId{2});
    const auto* picture = restored == nullptr ? nullptr : restored->properties().find(model::PropertyId::from_name("Picture"));
    expect(picture && std::get<model::PictureRef>(picture->value).standard_name ==
               model::QualifiedName{"PictureLib.Write"},
        "standard picture name must survive XML roundtrip");

    std::string unknown = xml.value();
    const auto at = unknown.find("PictureLib.Write");
    unknown.replace(at, std::string("PictureLib.Write").size(), "PictureLib.Unknown");
    expect(!source::parse_form_xml(unknown).ok(), "unknown standard picture names must be rejected");
}

void test_text_document_selected_text_runtime_only() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="TextSelection" ordinaryFormVersion="2.1"><ChildItems><TextDocumentField id="2" name="Text"><Position/></TextDocumentField></ChildItems></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "TextDocumentField fixture must parse");
    const model::OrdinaryFormDocument::ObjectKey owner{model::ObjectCategory::control, model::ObjectId{2}};
    const auto property = model::PropertyId::from_name("SelectedText");
    expect(parsed.value().set_prop_val(owner, property, std::string("abc")) == model::PropertyMutationStatus::applied_runtime_only,
        "selected text must be identified as runtime-only");
    expect(!source::serialize_form_xml(parsed.value()).ok(), "runtime selection must not serialize into Form.xml");
    expect(parsed.value().reset_prop_val(owner, property) == model::PropertyMutationStatus::reset_runtime_only,
        "runtime selection must be resettable");
    expect(source::serialize_form_xml(parsed.value()).ok(), "form must serialize after runtime selection is reset");
    constexpr std::string_view bad = R"XML(<Form id="1" name="TextSelection" ordinaryFormVersion="2.1"><ChildItems><TextDocumentField id="2" name="Text"><Position/><SelectedText>abc</SelectedText></TextDocumentField></ChildItems></Form>)XML";
    expect(!source::parse_form_xml(bad).ok(), "runtime selection must not be accepted in public XML");
    constexpr std::string_view input_xml = R"XML(<Form id="1" name="InputSelection" ordinaryFormVersion="2.1"><Attributes><Attribute id="3" name="Value"><TypeDomain><Entry term="string" length="20"/></TypeDomain></Attribute></Attributes><ChildItems><InputField id="2" name="Input"><DataPath attributeId="3"/><Position/></InputField></ChildItems></Form>)XML";
    auto input = source::parse_form_xml(input_xml);
    expect(input.ok(), "InputField selection fixture must parse");
    expect(input.value().set_prop_val(owner, property, std::string("abc")) == model::PropertyMutationStatus::applied_runtime_only,
        "InputField selection must be runtime-only");
    expect(!source::serialize_form_xml(input.value()).ok(), "InputField runtime selection must not serialize");
    expect(input.value().reset_prop_val(owner, property) == model::PropertyMutationStatus::reset_runtime_only &&
        source::serialize_form_xml(input.value()).ok(), "InputField must serialize after selection reset");
}

void test_choice_and_input_field_picture_references_xml() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="FieldPictures" ordinaryFormVersion="2.1">
  <Attributes>
    <Attribute id="3" name="Choice"><TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain></Attribute>
  </Attributes>
  <PictureAssets>
    <PictureAsset id="10" relativePath="Items/First/Picture.gif" format="gif"/>
    <PictureAsset id="11" relativePath="Items/Second/Picture.png" format="png"/>
  </PictureAssets>
  <ChildItems>
    <ChoiceField id="2" name="Choice">
      <DataPath attributeId="3"/><Position/>
      <Picture standardName="PictureLib.Write"/><ChoiceButtonPicture>10</ChoiceButtonPicture>
    </ChoiceField>
    <InputField id="4" name="Input">
      <DataPath attributeId="3"/><Position/>
      <Picture>10</Picture><ChoiceButtonPicture>11</ChoiceButtonPicture>
    </InputField>
  </ChildItems>
</Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.ok() ? "ChoiceField and InputField picture references must parse" :
        parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);

    const auto check_picture = [&](model::ObjectId control_id, std::string_view property_name,
                                   model::ObjectId asset_id,
                                   std::optional<model::QualifiedName> standard_name = std::nullopt) {
        const auto* control = parsed.value().find_control(control_id);
        const auto* property = control == nullptr ? nullptr : control->properties().find(
            model::PropertyId::from_name(property_name));
        expect(property && std::holds_alternative<model::PictureRef>(property->value),
            "field picture properties must materialize as PictureRef values");
        const auto& reference = std::get<model::PictureRef>(property->value);
        expect(reference.asset.id() == asset_id && reference.standard_name == standard_name,
            "each field picture property must retain its own asset or standard resource reference");
    };
    check_picture(model::ObjectId{2}, "Picture", model::ObjectId{0},
        model::QualifiedName{"PictureLib.Write"});
    check_picture(model::ObjectId{2}, "ChoiceButtonPicture", model::ObjectId{10});
    check_picture(model::ObjectId{4}, "Picture", model::ObjectId{10});
    check_picture(model::ObjectId{4}, "ChoiceButtonPicture", model::ObjectId{11});

    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() &&
               serialized.value().find("<Picture standardName=\"PictureLib.Write\"/>") != std::string::npos &&
               serialized.value().find("<ChoiceButtonPicture>10</ChoiceButtonPicture>") != std::string::npos &&
               serialized.value().find("<Picture>10</Picture>") != std::string::npos &&
               serialized.value().find("<ChoiceButtonPicture>11</ChoiceButtonPicture>") != std::string::npos,
        "ChoiceField and InputField pictures must serialize together as distinct named references");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && source::serialize_form_xml(reparsed.value()).value() == serialized.value(),
        "field picture references must survive a deterministic XML-only roundtrip");

    auto alias_document = parsed.value();
    const auto alias = model::PropertyId::from_name("SelButtonPicture");
    for (const auto id : {model::ObjectId{2}, model::ObjectId{4}}) {
        const model::OrdinaryFormDocument::ObjectKey owner{model::ObjectCategory::control, id};
        const model::PictureRef picture{model::PictureAssetRef{}, model::QualifiedName{"PictureLib.Write"}};
        expect(alias_document.set_prop_val(owner, alias, picture) == model::PropertyMutationStatus::applied,
            "picture API alias must set the canonical property before XML serialization");
    }
    const auto alias_serialized = source::serialize_form_xml(alias_document);
    expect(alias_serialized.ok() && alias_serialized.value().find("SelButtonPicture") == std::string::npos,
        "XML must not emit the API alias as a separate property");
    const auto first_canonical = alias_serialized.value().find("<ChoiceButtonPicture");
    const auto second_canonical = alias_serialized.value().find("<ChoiceButtonPicture", first_canonical + 1);
    expect(first_canonical != std::string::npos && second_canonical != std::string::npos &&
            alias_serialized.value().find("<ChoiceButtonPicture", second_canonical + 1) == std::string::npos,
        "each field must serialize one canonical picture property");
    for (const std::string value : {"10", "11"}) {
        std::string alias_xml(xml);
        const auto open = alias_xml.find("<ChoiceButtonPicture>" + value + "</ChoiceButtonPicture>");
        expect(open != std::string::npos, "canonical picture fixture must contain the target property");
        const auto original = "<ChoiceButtonPicture>" + value + "</ChoiceButtonPicture>";
        const auto replacement = "<SelButtonPicture>" + value + "</SelButtonPicture>";
        alias_xml.replace(open, original.size(), replacement);
        expect(!source::parse_form_xml(alias_xml).ok(),
            "XML must reject the API alias instead of introducing a second public property");
    }
    std::string duplicate_alias(xml);
    const auto choice_at = duplicate_alias.find("<ChoiceButtonPicture>10</ChoiceButtonPicture>");
    duplicate_alias.insert(choice_at, "<SelButtonPicture>11</SelButtonPicture>");
    expect(!source::parse_form_xml(duplicate_alias).ok(),
        "XML must reject simultaneous canonical and alias properties");

    constexpr std::string_view empty_xml = R"XML(<Form id="1" name="EmptyPictures" ordinaryFormVersion="2.1">
  <Attributes><Attribute id="3" name="Value"><TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain></Attribute></Attributes>
  <ChildItems>
    <ChoiceField id="2" name="Choice"><DataPath attributeId="3"/><Position/><Picture/><ChoiceButtonPicture/></ChoiceField>
    <InputField id="4" name="Input"><DataPath attributeId="3"/><Position/><Picture/><ChoiceButtonPicture/></InputField>
  </ChildItems>
</Form>)XML";
    expect(!source::parse_form_xml(empty_xml).ok(),
        "empty field picture references must be rejected; omitted properties express absence");

    constexpr std::string_view duplicate_xml = R"XML(<Form id="1" name="DuplicatePictures" ordinaryFormVersion="2.1">
  <Attributes><Attribute id="3" name="Value"><TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain></Attribute></Attributes>
  <ChildItems><ChoiceField id="2" name="Choice"><DataPath attributeId="3"/><Position/><Picture/><Picture/></ChoiceField></ChildItems>
</Form>)XML";
    expect(!source::parse_form_xml(duplicate_xml).ok(),
        "duplicate field picture properties must be rejected");

    constexpr std::string_view dangling_xml = R"XML(<Form id="1" name="DanglingPicture" ordinaryFormVersion="2.1">
  <Attributes><Attribute id="3" name="Value"><TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain></Attribute></Attributes>
  <ChildItems><InputField id="4" name="Input"><DataPath attributeId="3"/><Position/><ChoiceButtonPicture>99</ChoiceButtonPicture></InputField></ChildItems>
</Form>)XML";
    expect_code(source::parse_form_xml(dangling_xml), "OOF2004",
        "field picture references to unknown assets must be rejected");

    std::string unknown_standard(xml);
    const auto standard_at = unknown_standard.find("PictureLib.Write");
    unknown_standard.replace(standard_at, std::string("PictureLib.Write").size(), "PictureLib.Unknown");
    expect(!source::parse_form_xml(unknown_standard).ok(),
        "unknown standard field picture references must be rejected");
}

void test_button_menu_model_roundtrip_and_rejections() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="run" type="Action"><Text>Run</Text><Explanation/><ToolTip/><Shortcut Alt="false" Ctrl="true" Shift="false"><Key>R</Key></Shortcut><Action handler="RunHandler" name="ActionName"><Text><Item language="ru">Action caption</Item><Item language="en">Action title</Item></Text><ToolTip><Item language="ru">Action tip</Item></ToolTip><Description><Item language="ru">Action description</Item></Description></Action></CommandBarButton><CommandBarButton name="more" type="Submenu"><Text>More</Text><Order>Ascending</Order><Buttons><CommandBarButton name="sep" type="Separator"/><CommandBarButton name="inherited" type="Action"><Action handler="NestedHandler" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></CommandBarButton></Buttons></Button></ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.diagnostics().empty() ? "typed Button.Buttons tree must parse" : parsed.diagnostics().front().message);
    const auto* button = parsed.value().find_control(model::ObjectId{2});
    const auto* payload = button ? std::get_if<model::ButtonPayload>(&button->payload) : nullptr;
    expect(payload && payload->buttons.size() == 2 && payload->buttons[1].buttons.size() == 2,
        "button menu and recursive submenu must live in ButtonPayload");
    expect(payload->buttons[0].action && payload->buttons[0].action->handler == "RunHandler" &&
        payload->buttons[0].action->name == "ActionName" &&
        payload->buttons[0].action->text.items == std::vector<model::LocalizedStringItem>{{"ru", "Action caption"}, {"en", "Action title"}} &&
        payload->buttons[0].action->tooltip.items == std::vector<model::LocalizedStringItem>{{"ru", "Action tip"}} &&
        payload->buttons[0].action->description.items == std::vector<model::LocalizedStringItem>{{"ru", "Action description"}} &&
        payload->buttons[0].text == std::optional<std::string>{"Run"} && payload->buttons[0].shortcut.key == "R",
        "action metadata, explicit button Text and typed shortcut must be retained independently");
    expect(payload->buttons[0].explanation == std::optional<std::string>{""} &&
        payload->buttons[0].tooltip == std::optional<std::string>{""} &&
        !payload->buttons[1].buttons[1].text && !payload->buttons[1].buttons[1].explanation &&
        !payload->buttons[1].buttons[1].tooltip,
        "XML must distinguish explicit empty button overrides from absent overrides");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<CommandBarButton name=\"sep\" type=\"Separator\">") != std::string::npos,
        "button menu must serialize in named XML");
    expect(serialized.value().find("<Action handler=\"RunHandler\" name=\"ActionName\">") != std::string::npos &&
        serialized.value().find("<Description>") != std::string::npos &&
        serialized.value().find("Action description") != std::string::npos,
        "Action metadata must serialize as named fields independently from button Text");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "serialized button menu must parse again");
    const auto* restored = std::get_if<model::ButtonPayload>(&reparsed.value().find_control(model::ObjectId{2})->payload);
    expect(restored && restored->buttons == payload->buttons, "recursive button menu must roundtrip exactly");
    expect(serialized.value().find("<Order>Ascending</Order>") != std::string::npos, "submenu order must serialize by its named XML value");
    constexpr std::string_view picture_xml = R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><PictureAssets><PictureAsset id="4" relativePath="Items/Run/Buttons/More/Buttons/Item/Picture.gif" format="gif"/></PictureAssets><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Picture>4</Picture><Action handler="RunHandler" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML";
    expect(source::parse_form_xml(picture_xml).ok(), "named menu picture paths must parse within source package");
    auto unsafe = std::string(picture_xml);
    unsafe.replace(unsafe.find("Buttons/More"), 12, "Buttons/../More");
    expect(!source::parse_form_xml(unsafe).ok(), "menu picture paths must reject traversal");
    auto malformed_path = std::string(picture_xml);
    malformed_path.replace(malformed_path.find("Buttons/More"), 12, "Other/More");
    expect(!source::parse_form_xml(malformed_path).ok(), "menu paths may only use named Buttons ownership segments");
    expect(source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"/></Buttons></Button></ChildItems></Form>)XML").ok(), "a new menu Action may have no assigned handler");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Action>RunHandler</Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "legacy text-only Action must be rejected");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Action handler="RunHandler" name=""><Text/><ToolTip/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "Action requires all localized metadata fields");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Action handler="RunHandler" name="">unexpected<Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "Action mixed text content must be rejected by schema validation");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Action handler="One" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton><CommandBarButton name="x" type="Action"><Action handler="Two" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "duplicate names in a collection must be rejected");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Submenu"><Action handler="Bad" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "non-Action handler must be rejected");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action><Order>DontOrder</Order></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "Order must be rejected on Action even when it is the default");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Separator"><Order>Ascending</Order></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "Order must be rejected on Separator");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Submenu"><Order>Random</Order></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "unknown submenu order must be rejected");
}

void test_client_interface_variant_xml_contract() {
    const auto parsed = source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="zero" type="Action"><ClientInterfaceVariant>Version8_0</ClientInterfaceVariant><ClientInterfaceTheme>Dark</ClientInterfaceTheme><CommandControlKind>Button</CommandControlKind><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton><CommandBarButton name="version8_2" type="Action"><ClientInterfaceVariant>Version8_2</ClientInterfaceVariant><ClientInterfaceTheme>Light</ClientInterfaceTheme><Action handler="Version82" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton><CommandBarButton name="default" type="Action"><Action handler="Default" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML");
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto& buttons = std::get<model::ButtonPayload>(parsed.value().find_control(model::ObjectId{2})->payload).buttons;
    expect(buttons[0].client_interface_variant == model::ClientInterfaceVariant::version8_0 &&
               buttons[1].client_interface_variant == model::ClientInterfaceVariant::version8_2 &&
               buttons[2].client_interface_variant == model::ClientInterfaceVariant::version8_2_ordinary_app,
        "explicit Version8_0 and Version8_2, and the omitted Version8_2_OrdinaryApp default must map to named variants");
    expect(buttons[0].client_interface_theme == model::ClientInterfaceTheme::dark &&
               buttons[1].client_interface_theme == model::ClientInterfaceTheme::light &&
               !buttons[2].client_interface_theme,
        "XML должен различать две именованные темы и отсутствие настройки");
    expect(buttons[0].command_control_kind == model::CommandControlKind::button &&
               buttons[1].command_control_kind == model::CommandControlKind::from_source,
        "вид визуального элемента команды должен быть независимым");
    const auto xml = source::serialize_form_xml(parsed.value());
    expect(xml.ok() && xml.value().find("<ClientInterfaceVariant>Version8_0</ClientInterfaceVariant>") != std::string::npos &&
               xml.value().find("<ClientInterfaceVariant>Version8_2</ClientInterfaceVariant>") != std::string::npos &&
               xml.value().find("<ClientInterfaceVariant>Version8_2_OrdinaryApp</ClientInterfaceVariant>") == std::string::npos,
        "XML must write explicit Version8_0 and Version8_2, and omit the Version8_2_OrdinaryApp default");
    const auto repeated = xml ? source::parse_form_xml(xml.value()) : source::parse_form_xml("");
    expect(repeated.ok() && std::get<model::ButtonPayload>(repeated.value().find_control(model::ObjectId{2})->payload).buttons == buttons,
        "независимые тема и вариант должны сохраняться в XML");
    for (const auto& invalid_theme : {"Auto", "0", "Unknown", "Light</ClientInterfaceTheme><ClientInterfaceTheme>Dark"}) {
        auto invalid_xml = xml.value();
        const auto theme_start = invalid_xml.find("<ClientInterfaceTheme>Dark</ClientInterfaceTheme>");
        invalid_xml.replace(theme_start, std::string("<ClientInterfaceTheme>Dark</ClientInterfaceTheme>").size(),
            std::string("<ClientInterfaceTheme>") + invalid_theme + "</ClientInterfaceTheme>");
        expect(!source::parse_form_xml(invalid_xml), "неизвестные и повторные темы должны отклоняться");
    }
    expect(source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><ClientInterfaceVariant>Unsupported</ClientInterfaceVariant><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok() == false,
        "unsupported client interface variant names must be rejected");
    expect(source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><ClientInterfaceVariant>Version8_0</ClientInterfaceVariant><ClientInterfaceVariant>Version8_2_OrdinaryApp</ClientInterfaceVariant><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok() == false,
        "duplicate client interface variant fields must be rejected");
    auto invalid = parsed.value();
    auto changed = *invalid.find_control(model::ObjectId{2});
    std::get<model::ButtonPayload>(changed.payload).buttons[0].client_interface_variant =
        static_cast<model::ClientInterfaceVariant>(3);
    invalid.add_control(std::move(changed));
    expect(!source::serialize_form_xml(invalid), "XML writer must reject an invalid enum cast");
}

void test_gantt_named_collections_roundtrip_and_rejections() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Gantt" ordinaryFormVersion="2.1"><ChildItems>
      <GanttChart id="2" name="Schedule"><Position/><AutoFullInterval>false</AutoFullInterval><FullIntervalBegin>2027-01-01T00:00:00</FullIntervalBegin><FullIntervalEnd>2027-03-01T00:00:00</FullIntervalEnd>
        <Series><GanttSeries id="7"><Value>series-value</Value><Text>Series</Text></GanttSeries></Series>
        <Points><GanttPoint id="7"><Value>point-value</Value><Text>Point</Text></GanttPoint></Points>
        <Intervals><GanttInterval pointRef="7" seriesRef="7"><StartDate>2027-01-10T00:00:00</StartDate><EndDate>2027-01-12T00:00:00</EndDate><Text>First</Text></GanttInterval><GanttInterval pointRef="7" seriesRef="7"><StartDate>2027-01-14T00:00:00</StartDate><EndDate>2027-01-16T00:00:00</EndDate><Text>Second</Text></GanttInterval></Intervals>
      </GanttChart>
    </ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto* control = parsed.value().find_control(model::ObjectId{2});
    const auto* gantt = control ? std::get_if<model::GanttChartPayload>(&control->payload) : nullptr;
    expect(gantt && gantt->series.size() == 1 && gantt->points.size() == 1 && gantt->intervals.size() == 2,
        "named Gantt collections must retain independently scoped IDs and repeated intervals");
    expect(gantt->series[0].id == model::ObjectId{7} && gantt->points[0].id == model::ObjectId{7} &&
               gantt->intervals[0].point_ref == model::ObjectId{7} && gantt->intervals[0].series_ref == model::ObjectId{7} &&
               gantt->intervals[1].text == "Second",
        "Gantt intervals must preserve typed point and series references and order");
    const auto* automatic = control->properties().find(model::PropertyId::from_name("AutoFullInterval"));
    expect(automatic && !std::get<bool>(automatic->value), "Gantt manual-window mode must be a typed descriptor property");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "named Gantt payload must serialize");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "named Gantt payload must parse after serialization");
    const auto* roundtripped = std::get_if<model::GanttChartPayload>(
        &reparsed.value().find_control(model::ObjectId{2})->payload);
    expect(roundtripped && roundtripped->intervals == gantt->intervals,
        "Gantt intervals, including repeated references, must roundtrip exactly");

    expect(source::parse_form_xml(
        R"XML(<Form id="1" name="Gantt" ordinaryFormVersion="2.1"><ChildItems><GanttChart id="2" name="Schedule"><Position/><Series/><Series/></GanttChart></ChildItems></Form>)XML").ok() == false,
        "duplicate Gantt collection singletons must be rejected");
    expect(source::parse_form_xml(
        R"XML(<Form id="1" name="Gantt" ordinaryFormVersion="2.1"><ChildItems><GanttChart id="2" name="Schedule"><Position/><Intervals><GanttInterval pointRef="missing" seriesRef="missing"><StartDate>2027-01-10T00:00:00</StartDate><EndDate>2027-01-12T00:00:00</EndDate><Text>Orphan</Text></GanttInterval></Intervals></GanttChart></ChildItems></Form>)XML").ok() == false,
        "unresolved Gantt domain references must be rejected");
    expect(source::parse_form_xml(
        R"XML(<Form id="1" name="Gantt" ordinaryFormVersion="2.1"><ChildItems><GanttChart id="2" name="Schedule"><Position/><AutoFullInterval>true</AutoFullInterval></GanttChart></ChildItems></Form>)XML").ok(),
        "automatic Gantt range must permit omitted explicit bounds");
    expect(source::parse_form_xml(
        R"XML(<Form id="1" name="Gantt" ordinaryFormVersion="2.1"><ChildItems><GanttChart id="2" name="Schedule"><Position/><FullIntervalBegin>ABCD-01-01T00:00:00</FullIntervalBegin></GanttChart></ChildItems></Form>)XML").ok() == false,
        "Gantt dates must reject nonnumeric years");
}

void test_event_action_metadata_xml() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><Events><OnOpen id="3">  Обработчик &amp; Ω  <Action name="Open"><Text><Item language="ru">Заголовок</Item><Item language="en">Title</Item></Text><ToolTip/><Description><Item language="ru"></Item></Description></Action></OnOpen></Events><ChildItems><Button id="2" name="Button"><Position/><Events><Click id="4">Click<Action name="Run"><Text/><ToolTip/><Description/></Action></Click></Events></Button></ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "event action metadata must parse for form and control");
    const auto* event = parsed.value().find_event(model::ObjectId{3});
    const auto* click = parsed.value().find_event(model::ObjectId{4});
    expect(event && event->handler == "  Обработчик & Ω  " && event->action_metadata &&
               event->action_metadata->name == "Open" && event->action_metadata->text.items ==
                   std::vector<model::LocalizedStringItem>{{"ru", "Заголовок"}, {"en", "Title"}} &&
               event->action_metadata->tooltip.items.empty() &&
               event->action_metadata->description.items == std::vector<model::LocalizedStringItem>{{"ru", ""}},
        "handler text, translations, empty collection and empty translation must remain distinct");
    expect(click && click->handler == "Click" && click->action_metadata && click->action_metadata->name == "Run",
        "control event metadata must parse");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "event action metadata must serialize");
    const auto restored = source::parse_form_xml(serialized.value());
    expect(restored.ok(), "serialized event action metadata must parse");
    const auto* roundtrip = restored.value().find_event(model::ObjectId{3});
    expect(roundtrip && roundtrip->handler == event->handler && roundtrip->action_metadata == event->action_metadata,
        "mixed content must preserve exact direct handler text and metadata");
    const auto legacy = source::parse_form_xml(R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><Events><OnOpen id="3">Handler</OnOpen></Events></Form>)XML");
    expect(legacy.ok() && !legacy.value().find_event(model::ObjectId{3})->action_metadata,
        "legacy event must remain supported without metadata");
    const auto legacy_xml = source::serialize_form_xml(legacy.value());
    expect(legacy_xml.ok() && legacy_xml.value().find("<OnOpen id=\"3\">Handler</OnOpen>") != std::string::npos,
        "legacy event XML must remain unchanged");
    const auto wrap = [](std::string_view value) {
        return std::string("<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><Events><OnOpen id=\"3\">Handler") +
            std::string(value) + "</OnOpen></Events></Form>";
    };
    const auto pretty = source::parse_form_xml(wrap("<Action name=\"A\"><Text/><ToolTip/><Description/></Action>\n  "));
    expect(pretty.ok() && pretty.value().find_event(model::ObjectId{3})->handler == "Handler",
        "formatting whitespace after Action must not become handler text");
    expect(!source::parse_form_xml(wrap("<Action name=\"A\"><Text/><ToolTip/><Description/></Action>Bad")),
        "non-whitespace text after Action must be rejected");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><Events><OnOpen id="3"/></Events></Form>)XML"),
        "empty legacy handler must be rejected");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><Events><OnOpen id="3"><Action name="A"><Text/><ToolTip/><Description/></Action></OnOpen></Events></Form>)XML"),
        "empty handler with Action must be rejected");
    const auto spaces = source::parse_form_xml(R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><Events><OnOpen id="3">  </OnOpen></Events></Form>)XML");
    expect(spaces.ok() && spaces.value().find_event(model::ObjectId{3})->handler == "  ",
        "legacy handler whitespace must be preserved");
    std::string too_many = "<Action name=\"A\"><Text>";
    for (std::size_t i = 0; i <= model::max_localized_text_items; ++i) {
        too_many += "<Item language=\"ru\"/>";
    }
    too_many += "</Text><ToolTip/><Description/></Action>";
    expect(!source::parse_form_xml(wrap(too_many)), "event action must reject excessive translations before accumulation");
    model::Form oversized_form;
    oversized_form.id = model::ObjectId{1};
    oversized_form.name = "Main";
    oversized_form.events.push_back(model::EventRef{model::ObjectId{3}});
    model::OrdinaryFormDocument oversized_document(std::move(oversized_form));
    model::EventActionMetadata oversized_action;
    oversized_action.name = "A";
    oversized_action.text.items.resize(model::max_localized_text_items + 1);
    oversized_document.add_event(model::Event{model::ObjectId{3}, "OnOpen", "Handler", model::FormRef{model::ObjectId{1}}, std::move(oversized_action)});
    expect(!source::serialize_form_xml(oversized_document), "event action serialization must reject excessive translations");
    expect(!source::parse_form_xml(wrap("<Unknown/>")), "unknown event child must be rejected");
    expect(!source::parse_form_xml(wrap("<Action name=\"A\"><Text/><ToolTip/><Description/><Description/></Action>")),
        "duplicate action field must be rejected");
    expect(!source::parse_form_xml(wrap("<Action name=\"A\"><Text/><ToolTip/><Description/></Action><Action name=\"B\"><Text/><ToolTip/><Description/></Action>")),
        "duplicate action must be rejected");
    expect(!source::parse_form_xml(wrap("<Action name=\"A\">unexpected<Text/><ToolTip/><Description/></Action>")),
        "unexpected action text must be rejected");
    expect(!source::parse_form_xml(wrap("<Action name=\"A\"><Text><Item language=\"ru\">1</Item><Item language=\"ru\">2</Item></Text><ToolTip/><Description/></Action>")),
        "duplicate action translation must be rejected");
}

void test_event_owner_invariant() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.events.push_back(model::EventRef{model::ObjectId{3}});
    form.children.push_back(model::ControlRef{model::ObjectId{2}});
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{model::ObjectId{2}, "Button", model::ButtonPayload{}});
    document.add_event(model::Event{
        model::ObjectId{3},
        "Click",
        "Click",
        model::ControlRef{model::ObjectId{2}},
    });
    expect_code(
        source::serialize_form_xml(document),
        "OOF2004",
        "event listed by a different owner must not be silently projected");
}

void test_unsupported_control_events_rejected() {
    for (const auto& [control, event] : std::array{
             std::pair{"PivotChart", "Selection"},
             std::pair{"PivotChart", "DetailProcessing"},
             std::pair{"SpreadsheetDocumentField", "OnActivateArea"}}) {
        const auto xml = std::string("<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems><") +
            control + " id=\"2\" name=\"Target\"><Position/><Events><" + event +
            " id=\"3\">Handler</" + event + "></Events></" + control + "></ChildItems></Form>";
        expect(!source::parse_form_xml(xml),
            std::string("explicit unsupported event ") + control + "." + event + " must fail XML parsing");
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "Main";
        form.children.push_back(model::ControlRef{model::ObjectId{2}});
        model::OrdinaryFormDocument invalid(std::move(form));
        model::ControlPayload payload = std::string_view(control) == "PivotChart"
            ? model::ControlPayload{model::PivotChartPayload{}}
            : model::ControlPayload{model::SpreadsheetDocumentFieldPayload{}};
        model::ControlNode target{model::ObjectId{2}, "Target", std::move(payload)};
        target.events.push_back(model::EventRef{model::ObjectId{3}});
        invalid.add_control(std::move(target));
        invalid.add_event(model::Event{model::ObjectId{3}, event, "Handler", model::ControlRef{model::ObjectId{2}}});
        expect(!invalid.validate().ok(), "unsupported event with a matching owner must invalidate the model");
        expect(!source::serialize_form_xml(invalid), "public XML writer must reject unavailable event references");
        expect(!oof::storage::form_stream::encode_document(invalid), "native writer must reject unavailable event references");
    }

    const auto supported = source::parse_form_xml(
        R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems><SpreadsheetDocumentField id="2" name="Sheet"><Position/><Events><Selection id="3">Handler</Selection></Events></SpreadsheetDocumentField></ChildItems></Form>)XML");
    expect(supported.ok(), "supported SpreadsheetDocumentField Selection event must remain valid");
    const auto supported_xml = source::serialize_form_xml(supported.value());
    expect(supported_xml.ok() && supported_xml.value().find("<Selection id=\"3\">Handler</Selection>") != std::string::npos,
        "supported SpreadsheetDocumentField Selection event must serialize");

}

void test_progress_bar_xml_only_contract() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><ChildItems>
      <ProgressBar id="4" name="ProgressResearch"><Position><Top>12</Top><Visible>false</Visible><Height>20</Height><Left>8</Left><Width>180</Width></Position><Enabled>false</Enabled><ToolTip>Прогресс Ω &amp; &lt;тег&gt;</ToolTip></ProgressBar>
    </ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto* progress = parsed.value().find_control(model::ObjectId{4});
    expect(progress && progress->kind() == model::ControlKind::progress_bar &&
               progress->name == "ProgressResearch" && !progress->position.visible.value(),
        "ProgressBar identity and Visible must become named object-model fields");
    expect(!std::get<bool>(progress->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
               std::get<std::string>(progress->properties().find(model::PropertyId::from_name("ToolTip"))->value) ==
                   "Прогресс Ω & <тег>",
        "ProgressBar Enabled and ToolTip must become named properties");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<ProgressBar name=\"ProgressResearch\" id=\"4\">") !=
               std::string::npos && serialized.value().find("<Enabled>false</Enabled>") != std::string::npos,
        "ProgressBar XML must serialize its named identity and changed property");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && reparsed.value().find_control(model::ObjectId{4}) != nullptr,
        "ProgressBar XML-only object model must round-trip");
}

void test_data_processor_form_extension_xml_contract() {
    const auto parsed = source::parse_form_xml(
        "<Form id=\"1\" name=\"Processor\" ordinaryFormVersion=\"2.1\"><DataProcessorFormExtension/></Form>");
    expect(parsed.ok() && parsed.value().form().extension == model::FormExtensionKind::data_processor,
        "The platform form extension must be a typed named concept");
    expect(!source::parse_form_xml("<Form id=\"1\" name=\"Processor\" ordinaryFormVersion=\"2.1\"><MainAttribute attributeId=\"42\"/></Form>").ok(),
        "A dangling main Attribute reference must fail the object model invariant");
    const auto xml = source::serialize_form_xml(parsed.value());
    expect(xml.ok() && source::parse_form_xml(xml.value()).value().form().extension == parsed.value().form().extension,
        "XML-only round-trip must retain the extension kind");
    for (const auto domain : {"<Entry term=\"object\"/>", "<Entry term=\"unknown\" typeUuid=\"11111111-1111-1111-1111-111111111111\"/>"})
        expect(!source::parse_form_xml(std::string("<Form id=\"1\" name=\"P\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"2\" name=\"Value\"><TypeDomain>") + domain + "</TypeDomain></Attribute></Attributes></Form>").ok(),
            "Concrete object types require UUID and cannot use the old unknown term");
    for (const auto invalid : {"<DataProcessorFormExtension/><DataProcessorFormExtension/>",
        "<DataProcessorFormExtension raw=\"x\"/>", "<DataProcessorFormExtension><Field>0</Field></DataProcessorFormExtension>",
        "<DataProcessorFormExtension>0</DataProcessorFormExtension>"}) {
        const auto rejected = source::parse_form_xml(std::string("<Form id=\"1\" name=\"Processor\" ordinaryFormVersion=\"2.1\">") + invalid + "</Form>");
        expect(!rejected.ok(), "XSD must reject duplicate, indexed, raw or unnamed extension values");
    }
    auto document = parsed.value();
    auto form = document.form();
    form.extension = static_cast<model::FormExtensionKind>(255);
    document.set_form(std::move(form));
    expect(!document.validate().ok(), "Unknown extension kinds must violate the object model invariant");
    expect(!source::serialize_form_xml(document).ok(), "Unknown extension kinds must fail explicit serialization");
}

void test_saved_values_form_extension_xml_contract() {
    for (const auto& extension : {model::metamodel::data_processor_form_extension, model::metamodel::report_form_extension}) {
        const auto prefix = "<Form id=\"1\" name=\"Saved\" ordinaryFormVersion=\"2.1\"><" + std::string(extension.xml_name) + ">";
        const auto suffix = "</" + std::string(extension.xml_name) + ">"
            "<Attributes><Attribute id=\"0\" name=\"Настройка\"><TypeDomain><Entry term=\"string\"/></TypeDomain></Attribute></Attributes></Form>";
        const auto parsed = source::parse_form_xml(prefix +
            "<SaveValues>true</SaveValues><RestoreValuesOnOpen>false</RestoreValuesOnOpen>"
            "<SavedValues><SavedValue sourceId=\"AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE\" name=\"Настройка\" attributeId=\"0\"/>"
            "<SavedValue sourceId=\"11111111-2222-3333-4444-555555555555\" name=\"ПараметрБезРеквизита\"/></SavedValues>" + suffix);
        expect(parsed.ok(), "both extension XML contracts must accept named booleans and saved-value bindings");
        const auto& form = parsed.value().form();
        expect(form.extension == extension.kind && form.saved_values.size() == 2 &&
            form.saved_values[0].source_id.canonical == "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee" &&
            form.saved_values[0].name == "Настройка" && form.saved_values[0].attribute.has_value() &&
            form.saved_values[0].attribute.id() == model::ObjectId{0} && !form.saved_values[1].attribute.has_value(),
            "saved bindings must preserve source UUID, UTF-8 name and attribute zero without resolving source metadata");
        expect(std::get<bool>(form.extension_properties.find(model::PropertyId::from_name("SaveValues"))->value) &&
            !std::get<bool>(form.extension_properties.find(model::PropertyId::from_name("RestoreValuesOnOpen"))->value),
            "extension booleans must use their descriptor types and defaults");
        const auto xml = source::serialize_form_xml(parsed.value());
        expect(xml.ok() && xml.value().find("<SavedValues>") != std::string::npos &&
            xml.value().find("attributeId=\"0\"") != std::string::npos,
            "saved-value XML must retain named collection and optional zero identity");
        const auto reparsed = source::parse_form_xml(xml.value());
        expect(reparsed.ok() && reparsed.value().form().saved_values == form.saved_values,
            "saved source UUIDs and independent names must survive XML roundtrip");
        const auto canonical = source::serialize_form_xml(reparsed.value());
        expect(canonical.ok() && canonical.value() == xml.value(),
            "saved values and extension booleans must have stable canonical serialization");
        const auto empty = source::parse_form_xml(prefix + "<SavedValues/>" + suffix);
        expect(empty.ok() && empty.value().form().saved_values.empty() && empty.value().form().extension_properties.empty(),
            "empty saved values and omitted booleans must use descriptor defaults");
        const auto empty_xml = source::serialize_form_xml(empty.value());
        expect(empty_xml.ok() && empty_xml.value().find("<" + std::string(extension.xml_name) + "/>") != std::string::npos &&
            empty_xml.value().find("SavedValues") == std::string::npos &&
            empty_xml.value().find("<SaveValues>") == std::string::npos && empty_xml.value().find("<RestoreValuesOnOpen>") == std::string::npos,
            "empty collection and default extension values must be omitted canonically");
        const auto independent = source::parse_form_xml(prefix +
            "<SaveValues>false</SaveValues><SavedValues><SavedValue sourceId=\"11111111-2222-3333-4444-555555555555\" name=\"Stored\"/></SavedValues>" + suffix);
        expect(independent.ok() && independent.value().form().saved_values.size() == 1 && independent.value().form().extension_properties.empty(),
            "SaveValues false must retain independently stored saved bindings");
        for (const auto body : {"<SavedValues/><SavedValues/>", "<RestoreValuesOnOpen>false</RestoreValuesOnOpen><RestoreValuesOnOpen>true</RestoreValuesOnOpen>",
            "<SavedValues version=\"1\"/>", "<SavedValues><Unknown/></SavedValues>",
            "<SavedValues><SavedValue sourceId=\"bad\" name=\"Setting\"/></SavedValues>",
            "<SavedValues><SavedValue sourceId=\"11111111-2222-3333-4444-555555555555\"/></SavedValues>",
            "<SavedValues><SavedValue sourceId=\"11111111-2222-3333-4444-555555555555\" name=\" \"/></SavedValues>",
            "<SavedValues><SavedValue sourceId=\"11111111-2222-3333-4444-555555555555\" name=\"Setting\" attributeId=\"99\"/></SavedValues>",
            "<SavedValues><SavedValue sourceId=\"11111111-2222-3333-4444-555555555555\" name=\"Setting\" raw=\"x\"/></SavedValues>",
            "<SavedValues><SavedValue sourceId=\"11111111-2222-3333-4444-555555555555\" name=\"Setting\"><Raw/></SavedValue></SavedValues>"})
            expect(!source::parse_form_xml(prefix + body + suffix),
                "saved-value XML must reject duplicate, unknown, malformed and dangling values");
    }
    const auto report = source::parse_form_xml(R"XML(<Form id="1" name="Report" ordinaryFormVersion="2.1"><ReportFormExtension><SaveValues>true</SaveValues><RestoreValuesOnOpen>false</RestoreValuesOnOpen><ReportResult>2</ReportResult></ReportFormExtension><ChildItems><SpreadsheetDocumentField id="2" name="Result"><Position/></SpreadsheetDocumentField></ChildItems></Form>)XML");
    expect(report.ok(), "ReportFormExtension must parse ReportResult through the typed control-reference codec");
    const auto* result = report.value().form().extension_properties.find(model::PropertyId::from_name("ReportResult"));
    expect(result && std::get<model::ControlRef>(result->value).id() == model::ObjectId{2}, "ReportResult must retain its concrete control identity");
    const auto report_xml = source::serialize_form_xml(report.value());
    expect(report_xml.ok() && report_xml.value().find("<ReportResult>2</ReportResult>") != std::string::npos && source::parse_form_xml(report_xml.value()).ok(),
        "ReportResult and named booleans must survive canonical XML serialization");
    for (const auto extension : {"CatalogItemFormExtension", "CatalogListFormExtension"})
        expect(!source::parse_form_xml("<Form id=\"1\" name=\"Catalog\" ordinaryFormVersion=\"2.1\"><" + std::string(extension) +
            "><SavedValues/></" + extension + "></Form>"), "unrelated extensions cannot own SavedValues");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Processor" ordinaryFormVersion="2.1"><DataProcessorFormExtension><ReportResult>2</ReportResult></DataProcessorFormExtension></Form>)XML"),
        "ReportResult must remain specific to report forms");
    for (const auto value : {"", "0", "Undefined"})
        expect(!source::parse_form_xml("<Form id=\"1\" name=\"Report\" ordinaryFormVersion=\"2.1\"><ReportFormExtension><ReportResult>" +
            std::string(value) + "</ReportResult></ReportFormExtension></Form>"),
            "ReportResult Undefined default is omitted and cannot create an empty ControlRef");
}

void test_input_field_reference_extension_xml_contract() {
    const std::string begin = R"XML(<Form id="1" name="Ref" ordinaryFormVersion="2.1"><ChildItems><InputField id="2" name="Ref">)XML";
    const std::string end = R"XML(<ValueType><Entry term="object" typeUuid="22222222-2222-2222-2222-222222222222"/></ValueType><Position/></InputField></ChildItems></Form>)XML";
    for (const auto& extension : model::metamodel::input_field_extensions) {
        const auto parsed = source::parse_form_xml(begin + "<" + std::string(extension.xml_name) + "/>" + end);
        expect(parsed.ok() && std::get<model::InputFieldPayload>(parsed.value().find_control(model::ObjectId{2})->payload).extension == extension.kind,
            "Each reference InputField family must have a named model identity");
    }
    const auto configured = source::parse_form_xml(begin +
        R"XML(<CatalogInputFieldExtension><QuickChoice>true</QuickChoice><ChoiceFoldersAndItems type="FoldersAndItemsUse" member="Folders"/></CatalogInputFieldExtension>)XML" + end);
    expect(configured.ok(), configured ? "" : configured.diagnostics().front().message);
    const auto* field = configured.value().find_control(model::ObjectId{2});
    const auto* quick = field->properties().find(model::PropertyId::from_name("QuickChoice"));
    const auto* folders = field->properties().find(model::PropertyId::from_name("ChoiceFoldersAndItems"));
    expect(quick && std::get<bool>(quick->value) && folders &&
        std::get<model::EnumerationValue>(folders->value) == model::EnumerationValue{"FoldersAndItemsUse", "Folders"},
        "CatalogInputFieldExtension must retain its two named nondefault settings");
    const auto serialized = source::serialize_form_xml(configured.value());
    expect(serialized.ok() &&
        serialized.value().find("<CatalogInputFieldExtension>") < serialized.value().find("<QuickChoice>true</QuickChoice>") &&
        serialized.value().find("<QuickChoice>true</QuickChoice>") < serialized.value().find(
            R"XML(<ChoiceFoldersAndItems type="FoldersAndItemsUse" member="Folders"/>)XML") &&
        serialized.value().find(R"XML(<ChoiceFoldersAndItems type="FoldersAndItemsUse" member="Folders"/>)XML") <
            serialized.value().find("</CatalogInputFieldExtension>"),
        "CatalogInputFieldExtension settings must remain nested and ordered in public XML");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), reparsed ? "" : reparsed.diagnostics().front().message);
    const auto serialized_again = source::serialize_form_xml(reparsed.value());
    expect(serialized_again.ok() && serialized_again.value() == serialized.value(),
        "CatalogInputFieldExtension settings must survive XML serialization and parsing");
    const auto defaulted = source::parse_form_xml(begin +
        R"XML(<CatalogInputFieldExtension><QuickChoice>false</QuickChoice><ChoiceFoldersAndItems type="FoldersAndItemsUse" member="FoldersAndItems"/></CatalogInputFieldExtension>)XML" + end);
    expect(defaulted.ok(), defaulted ? "" : defaulted.diagnostics().front().message);
    const auto default_xml = source::serialize_form_xml(defaulted.value());
    expect(defaulted.ok() && default_xml.ok() && default_xml.value().find("<CatalogInputFieldExtension/>") != std::string::npos,
        "CatalogInputFieldExtension default settings must canonicalize to the empty extension");
    const auto table = source::parse_form_xml(R"XML(<Form id="1" name="Rows" ordinaryFormVersion="2.1"><Attributes><Attribute id="3" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes><ChildItems><Table id="2" name="Rows"><DataPath attributeId="3"/><Position/><Columns><Column name="Ref"><DataPath>Ref</DataPath><Header><Item language="en">Ref</Item></Header><Control type="InputField"><ValueType><Entry term="object" typeUuid="22222222-2222-2222-2222-222222222222"/></ValueType><CatalogInputFieldExtension><QuickChoice>true</QuickChoice><ChoiceFoldersAndItems type="FoldersAndItemsUse" member="Items"/></CatalogInputFieldExtension></Control></Column></Columns></Table></ChildItems></Form>)XML");
    expect(table.ok(), table ? "" : table.diagnostics().front().message);
    const auto table_xml = source::serialize_form_xml(table.value());
    expect(table_xml.ok() && table_xml.value().find(R"XML(<Control type="InputField">)XML") != std::string::npos &&
        table_xml.value().find(R"XML(<Control type="InputField">)XML") < table_xml.value().find("<CatalogInputFieldExtension>") &&
        table_xml.value().find("<CatalogInputFieldExtension>") < table_xml.value().find("<QuickChoice>true</QuickChoice>") &&
        table_xml.value().find("<QuickChoice>true</QuickChoice>") < table_xml.value().find(
            R"XML(<ChoiceFoldersAndItems type="FoldersAndItemsUse" member="Items"/>)XML") &&
        table_xml.value().find(R"XML(<ChoiceFoldersAndItems type="FoldersAndItemsUse" member="Items"/>)XML") <
            table_xml.value().find("</CatalogInputFieldExtension>"),
        "Table Column InputField must use the same nested Catalog extension settings");
    expect(!source::parse_form_xml(begin + "<CatalogInputFieldExtension/>" +
        R"XML(<ValueType><Entry term="object" typeUuid="22222222-2222-2222-2222-222222222222"/></ValueType><Position/><ListChoiceMode>true</ListChoiceMode></InputField></ChildItems></Form>)XML"),
        "XML must reject an authored reference family that is inactive under list-choice mode");
    for (const auto invalid : {"<CatalogInputFieldExtension/><ChartOfAccountsInputFieldExtension/>",
        "<CatalogInputFieldExtension/><CatalogInputFieldExtension/>",
        "<CatalogInputFieldExtension setting=\"0\"/>",
        "<CatalogInputFieldExtension><QuickChoice>true</QuickChoice><QuickChoice>false</QuickChoice></CatalogInputFieldExtension>",
        "<CatalogInputFieldExtension><Unknown>true</Unknown></CatalogInputFieldExtension>",
        "<CatalogInputFieldExtension><ChoiceFoldersAndItems type=\"FoldersAndItemsUse\" member=\"Unknown\"/></CatalogInputFieldExtension>",
        "<ChartOfCharacteristicTypesInputFieldExtension><Settings/></ChartOfCharacteristicTypesInputFieldExtension>"})
        expect(!source::parse_form_xml(begin + invalid + end),
            "InputField extension XML must reject unknown settings and mutually exclusive identities");
    expect(!source::parse_form_xml(begin + "<CatalogInputFieldExtension/><QuickChoice>true</QuickChoice>" + end),
        "CatalogInputFieldExtension settings must not appear outside the named extension");
}

void test_catalog_list_form_extension_xml_contract() {
    const std::array empty_extensions{
        model::metamodel::catalog_list_form_extension,
        model::metamodel::document_list_form_extension,
        model::metamodel::information_register_record_list_form_extension,
        model::metamodel::accumulation_register_record_list_form_extension,
        model::metamodel::characteristic_type_list_form_extension,
        model::metamodel::enumeration_list_form_extension,
    };
    for (const auto& extension : empty_extensions) {
        const auto open = "<" + std::string(extension.xml_name);
        const auto empty = open + "/>";
        const std::string prefix = R"XML(<Form id="1" name="List" ordinaryFormVersion="2.1"><MainAttribute attributeId="0"/>)XML";
        const std::string suffix = R"XML(<Attributes><Attribute id="0" name="ListObject"><TypeDomain><Entry term="object" typeUuid="22222222-2222-2222-2222-222222222222"/></TypeDomain></Attribute></Attributes></Form>)XML";
        const auto parsed = source::parse_form_xml(prefix + empty + suffix);
        expect(parsed.ok() && parsed.value().form().extension == extension.kind &&
            parsed.value().form().main_attribute.id() == model::ObjectId{0},
            "Each empty list extension must preserve its named identity and selected object context");
        const auto xml = source::serialize_form_xml(parsed.value());
        expect(xml.ok() && xml.value().find(empty) != std::string::npos &&
            xml.value().find(extension.guid.substr(0, 8)) == std::string::npos,
            "Empty list extension XML must serialize only its named identity");
        if (xml.ok()) {
            const auto reparsed = source::parse_form_xml(xml.value());
            expect(reparsed.ok() && reparsed.value().form().extension == extension.kind &&
                reparsed.value().form().main_attribute.id() == model::ObjectId{0} &&
                reparsed.value().find_attribute(model::ObjectId{0})->type.entries.front().type_uuid ==
                    parsed.value().find_attribute(model::ObjectId{0})->type.entries.front().type_uuid,
                "Empty list extension XML must preserve its identity and concrete main type on round-trip");
        }

        const std::array invalid{
            empty + empty,
            empty + "<CatalogItemFormExtension/>",
            empty + "<DataProcessorFormExtension/>",
            open + " settings=\"0\"/>",
            open + "><Settings>0</Settings></" + std::string(extension.xml_name) + ">",
            open + "><SaveValues>false</SaveValues></" + std::string(extension.xml_name) + ">",
            open + "><Autonumeration type=\"AutonumerationInForm\" member=\"Auto\"/></" + std::string(extension.xml_name) + ">",
            open + ">0</" + std::string(extension.xml_name) + ">",
            open + "><Unexpected/></" + std::string(extension.xml_name) + ">",
        };
        for (const auto& body : invalid)
            expect(!source::parse_form_xml(prefix + body + suffix),
                "Empty list extensions must reject duplicate, foreign, and unnamed settings");
    }
}

void test_catalog_item_form_extension_xml_contract() {
    for (const auto autonumeration : {std::string_view{"Auto"}, std::string_view{"DontUse"}}) {
        for (const auto folders : {std::string_view{"Items"}, std::string_view{"Folders"},
                 std::string_view{"FoldersAndItems"}}) {
            const auto xml = std::string(R"XML(<Form id="1" name="Catalog" ordinaryFormVersion="2.1"><CatalogItemFormExtension><Autonumeration type="AutonumerationInForm" member=")XML") +
                std::string(autonumeration) + R"XML("/><UseForFoldersAndItems type="FoldersAndItemsUse" member=")XML" +
                std::string(folders) + R"XML("/></CatalogItemFormExtension></Form>)XML";
            const auto parsed = source::parse_form_xml(xml);
            expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
            expect(parsed.value().form().extension == model::FormExtensionKind::catalog_item &&
                       parsed.value().get_prop_val(
                           model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::form, model::ObjectId{1}},
                           model::PropertyId::from_name("Autonumeration")).value ==
                           model::PropertyValue{model::EnumerationValue{"AutonumerationInForm", std::string(autonumeration)}} &&
                       parsed.value().get_prop_val(
                           model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::form, model::ObjectId{1}},
                           model::PropertyId::from_name("UseForFoldersAndItems")).value ==
                           model::PropertyValue{model::EnumerationValue{"FoldersAndItemsUse", std::string(folders)}},
                "CatalogItemFormExtension XML must populate its named typed properties");
            const auto serialized = source::serialize_form_xml(parsed.value());
            expect(serialized.ok() && serialized.value().find("CatalogItemFormExtension") != std::string::npos &&
                       serialized.value().find("c5251418") == std::string::npos,
                "CatalogItemFormExtension XML must serialize named values without platform identifiers");
            const auto reparsed = source::parse_form_xml(serialized.value());
            expect(reparsed.ok() && reparsed.value().get_prop_val(
                       model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::form, model::ObjectId{1}},
                       model::PropertyId::from_name("Autonumeration")).value ==
                           parsed.value().get_prop_val(
                               model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::form, model::ObjectId{1}},
                               model::PropertyId::from_name("Autonumeration")).value &&
                       reparsed.value().get_prop_val(
                           model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::form, model::ObjectId{1}},
                           model::PropertyId::from_name("UseForFoldersAndItems")).value ==
                           parsed.value().get_prop_val(
                               model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::form, model::ObjectId{1}},
                               model::PropertyId::from_name("UseForFoldersAndItems")).value,
                "CatalogItemFormExtension XML must preserve all six enum combinations");
        }
    }
    const auto defaults = source::parse_form_xml(
        R"XML(<Form id="1" name="Catalog" ordinaryFormVersion="2.1"><CatalogItemFormExtension/></Form>)XML");
    expect(defaults.ok() && defaults.value().form().extension == model::FormExtensionKind::catalog_item &&
               defaults.value().form().extension_properties.empty(),
        "omitted CatalogItemFormExtension properties must use typed descriptor defaults");
    for (const auto body : {
             "<Autonumeration type=\"Unknown\" member=\"Auto\"/>",
             "<Autonumeration type=\"AutonumerationInForm\" member=\"Unknown\"/>",
             "<Unknown>0</Unknown>",
             "<Autonumeration type=\"AutonumerationInForm\" member=\"Auto\"/><Autonumeration type=\"AutonumerationInForm\" member=\"DontUse\"/>"}) {
        const auto invalid = std::string(R"XML(<Form id="1" name="Catalog" ordinaryFormVersion="2.1"><CatalogItemFormExtension>)XML") +
            body + "</CatalogItemFormExtension></Form>";
        expect(!source::parse_form_xml(invalid), "unknown, duplicate, or malformed CatalogItemFormExtension fields must fail closed");
    }
    expect(!source::parse_form_xml(
               R"XML(<Form id="1" name="Both" ordinaryFormVersion="2.1"><CatalogItemFormExtension/><DataProcessorFormExtension/></Form>)XML"),
        "form extension XML choices must be mutually exclusive");
}

void test_document_form_extension_xml_contract() {
    const auto owner = model::OrdinaryFormDocument::ObjectKey{
        model::ObjectCategory::form, model::ObjectId{1}};
    const std::string xml = R"XML(<Form id="1" name="Document" ordinaryFormVersion="2.1"><DocumentFormExtension><RepostOnWrite>true</RepostOnWrite><UsePostingMode type="PostingModeUse" member="RealTime"/><AutoTime type="AutoTimeMode" member="First"/><Autonumeration type="AutonumerationInForm" member="Auto"/></DocumentFormExtension></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    expect(parsed.value().form().extension == model::FormExtensionKind::document &&
               parsed.value().get_prop_val(owner, model::PropertyId::from_name("RepostOnWrite")).value ==
                   model::PropertyValue{true} &&
               parsed.value().get_prop_val(owner, model::PropertyId::from_name("UsePostingMode")).value ==
                   model::PropertyValue{model::EnumerationValue{"PostingModeUse", "RealTime"}} &&
               parsed.value().get_prop_val(owner, model::PropertyId::from_name("AutoTime")).value ==
                   model::PropertyValue{model::EnumerationValue{"AutoTimeMode", "First"}} &&
               parsed.value().get_prop_val(owner, model::PropertyId::from_name("Autonumeration")).value ==
                   model::PropertyValue{model::EnumerationValue{"AutonumerationInForm", "Auto"}},
        "DocumentFormExtension XML must populate its four named typed properties");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("DocumentFormExtension") != std::string::npos &&
               serialized.value().find("<Field") == std::string::npos,
        "DocumentFormExtension XML must serialize named values without raw fields");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), reparsed ? "" : reparsed.diagnostics().front().message);
    for (const auto name : {"RepostOnWrite", "UsePostingMode", "AutoTime", "Autonumeration"})
        expect(reparsed.value().get_prop_val(owner, model::PropertyId::from_name(name)).value ==
                   parsed.value().get_prop_val(owner, model::PropertyId::from_name(name)).value,
            "DocumentFormExtension named properties must survive XML serialize and reparse");

    for (const auto& [name, type, members] : {
             std::tuple<std::string_view, std::string_view, std::vector<std::string_view>>{
                 "UsePostingMode", "PostingModeUse", {"Regular", "RealTime", "Ask", "Auto"}},
             {"AutoTime", "AutoTimeMode", {"DontUse", "First", "Last", "CurrentOrFirst", "CurrentOrLast"}},
             {"Autonumeration", "AutonumerationInForm", {"Auto", "DontUse"}},
         }) {
        for (const auto member : members) {
            const auto candidate = "<Form id=\"1\" name=\"Document\" ordinaryFormVersion=\"2.1\"><DocumentFormExtension><" +
                std::string(name) + " type=\"" + std::string(type) + "\" member=\"" + std::string(member) +
                "\"/></DocumentFormExtension></Form>";
            const auto value = source::parse_form_xml(candidate);
            expect(value.ok(), "DocumentFormExtension XML must accept every documented enum member");
        }
    }
    const auto defaults = source::parse_form_xml(
        R"XML(<Form id="1" name="Document" ordinaryFormVersion="2.1"><DocumentFormExtension/></Form>)XML");
    expect(defaults.ok() && defaults.value().form().extension == model::FormExtensionKind::document,
        "empty DocumentFormExtension must parse without asserting undocumented defaults");
    for (const auto body : {
             "<RepostOnWrite>yes</RepostOnWrite>",
             "<UsePostingMode type=\"Wrong\" member=\"Auto\"/>",
             "<UsePostingMode type=\"PostingModeUse\" member=\"Unknown\"/>",
             "<AutoTime type=\"AutoTimeMode\" member=\"Unknown\"/>",
             "<Autonumeration type=\"AutonumerationInForm\" member=\"Unknown\"/>",
             "<Unknown>0</Unknown>",
             "<RepostOnWrite>true</RepostOnWrite><RepostOnWrite>false</RepostOnWrite>",
         }) {
        const auto invalid = std::string(R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><DocumentFormExtension>)XML") +
            body + "</DocumentFormExtension></Form>";
        expect(!source::parse_form_xml(invalid),
            "DocumentFormExtension XML must reject wrong types, members, unknown fields, and duplicates");
    }
    expect(!source::parse_form_xml(
               R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><DocumentFormExtension settings="0"/></Form>)XML"),
        "DocumentFormExtension XML must reject unnamed settings attributes");
    expect(!source::parse_form_xml(
               R"XML(<Form id="1" name="Both" ordinaryFormVersion="2.1"><DocumentFormExtension/><CatalogItemFormExtension/></Form>)XML"),
        "document and other form extensions must remain mutually exclusive");
}

void test_main_panel_typed_xml_contract() {
    const std::string xml = R"XML(<Form id="1" name="Traversal" ordinaryFormVersion="2.1"><Panel><AutoTabOrder>false</AutoTabOrder></Panel></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    expect(!std::get<bool>(parsed.value().form().panel.properties.find(model::PropertyId::from_name("AutoTabOrder"))->value),
        "main Panel must be a typed object containing its explicit traversal property");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Panel>") != std::string::npos && source::parse_form_xml(serialized.value()).ok(),
        "typed main Panel must round-trip without another control ID or child collection");
    for (const auto body : {"<AutoTabOrder>2</AutoTabOrder>", "<AutoTabOrder>false</AutoTabOrder><AutoTabOrder>true</AutoTabOrder>",
        "<ChildItems/>", "<Unknown>false</Unknown>"}) {
        const auto invalid = std::string(R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><Panel>)XML") + body + "</Panel></Form>";
        expect(!source::parse_form_xml(invalid), "unsupported main Panel content must be rejected by its schema");
    }
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><Panel id="2"/></Form>)XML"),
        "main Panel must not declare a separate control ID");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><Panel/><Panel/></Form>)XML"),
        "duplicate main Panel must be rejected");
    const auto defaults = source::parse_form_xml(R"XML(<Form id="1" name="Default" ordinaryFormVersion="2.1"><Panel><AutoTabOrder>true</AutoTabOrder></Panel></Form>)XML");
    expect(defaults.ok(), defaults ? "" : defaults.diagnostics().front().message);
    const auto implicit = source::serialize_form_xml(defaults.value());
    expect(implicit.ok() && implicit.value().find("<Panel>") == std::string::npos, "default traversal must not create an explicit panel value");
    model::FontValue font; font.kind = model::FontKind::absolute; font.face_name = "Arial"; font.height = 12;
    const std::array<std::pair<std::string_view, model::PropertyValue>, 5> unsupported{{
        {"Enabled", false}, {"ToolTip", std::string("Tip")}, {"Font", font}, {"Transparent", true},
        {"ShowTabs", model::EnumerationValue{"ShowTabs", "Bottom"}},
    }};
    for (const auto& [name, value] : unsupported) {
        auto invalid_form = parsed.value().form();
        invalid_form.panel.properties.set_explicit(model::PropertyId::from_name(name), value);
        model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
        expect(!invalid_document.validate().ok() && !source::serialize_form_xml(invalid_document),
            "unimplemented main Panel properties must not be silently lost");
    }
    for (const auto body : {"<Enabled>false</Enabled>", "<Transparent>true</Transparent>"}) {
        const auto invalid = std::string(R"XML(<Form id="1" name="Invalid" ordinaryFormVersion="2.1"><Panel>)XML") + body + "</Panel></Form>";
        expect(!source::parse_form_xml(invalid), "unsupported main Panel property must not parse");
    }
}

void test_command_bar_default_button_xml_contract() {
    const std::string xml = R"XML(<Form id="1" name="DefaultAction" ordinaryFormVersion="2.1"><ChildItems>
      <CommandBar id="4" name="Tools"><Position/><Secondary>false</Secondary><Buttons>
        <CommandBarButton name="Run" type="Action"><DefaultButton>true</DefaultButton><Action handler="RunHandler" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton>
      </Buttons></CommandBar></ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<DefaultButton>true</DefaultButton>") != std::string::npos,
        "default Action must have a named editable boolean property");
    expect(source::parse_form_xml(serialized.value()).ok(), "default Action must survive XML serialization");
    auto invalid = xml; invalid.replace(invalid.find("<Secondary>false</Secondary>"), std::string("<Secondary>false</Secondary>").size(), "<Secondary>true</Secondary>");
    expect(!source::parse_form_xml(invalid), "secondary panel default is outside the verified public contract");
    invalid = xml; const auto pos = invalid.find("<DefaultButton>true</DefaultButton>");
    invalid.insert(pos, "<DefaultButton>false</DefaultButton>");
    expect(!source::parse_form_xml(invalid), "duplicate DefaultButton must be rejected");
    invalid = xml; invalid.replace(invalid.find("<DefaultButton>true"), std::string("<DefaultButton>true").size(), "<DefaultButton>bad");
    expect(!source::parse_form_xml(invalid), "invalid boolean must be rejected");
}

void test_command_bar_buttons_xml_only_contract() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="CommandBar" ordinaryFormVersion="2.1"><ChildItems>
      <CommandBar id="4" name="Tools"><Position/><Enabled>false</Enabled><Buttons>
        <CommandBarButton name="Run" type="Action"><Text>Start</Text><Action handler="RunHandler" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton>
        <CommandBarButton name="More" type="Submenu"><Buttons><CommandBarButton name="Stop" type="Action"><Action handler="StopHandler" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></CommandBarButton>
      </Buttons><ToolTip>Actions</ToolTip></CommandBar>
    </ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto* bar = parsed.value().find_control(model::ObjectId{4});
    expect(bar != nullptr && bar->kind() == model::ControlKind::command_bar &&
        !std::get<bool>(bar->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
        std::get<std::string>(bar->properties().find(model::PropertyId::from_name("ToolTip"))->value) == "Actions" &&
        std::get<model::CommandBarPayload>(bar->payload).buttons.size() == 2,
        "CommandBar named properties and entry-owned Actions must parse");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<CommandBar name=\"Tools\" id=\"4\">") != std::string::npos,
        "CommandBar XML-only fresh serialization must use named control properties");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() &&
        std::get<model::CommandBarPayload>(reparsed.value().find_control(model::ObjectId{4})->payload).buttons ==
            std::get<model::CommandBarPayload>(bar->payload).buttons,
        "CommandBar typed Buttons and Actions must survive XML-only round-trip");
}

void test_automatic_buttons_xml_contract() {
    const auto parse_buttons = [](std::string_view buttons, bool bar_auto_fill = false) {
        return source::parse_form_xml(std::string(R"XML(<Form id="1" name="AutomaticButtons" ordinaryFormVersion="2.1"><ChildItems><CommandBar id="2" name="Tools"><Position/>)XML") +
            (bar_auto_fill ? "<AutoFill>true</AutoFill>" : "") + std::string(buttons) + "</CommandBar></ChildItems></Form>");
    };
    const std::string items = R"XML(<CommandBarButton name="Manual" type="Separator"/><CommandBarButton name="Automatic" type="Submenu"><AutomaticInsertionLocks>0</AutomaticInsertionLocks><Buttons autoFill="true"><AutomaticButtons><Placement><AfterLastButton/></Placement></AutomaticButtons></Buttons></CommandBarButton>)XML";
    const std::vector<std::string> variants{
        "<Buttons autoFill=\"true\"/>",
        "<Buttons><AutomaticButtons><Placement><AfterLastButton/></Placement></AutomaticButtons></Buttons>",
        "<Buttons autoFill=\"true\"><AutomaticButtons><Placement><AfterLastButton/></Placement></AutomaticButtons></Buttons>",
        "<Buttons>" + items + "</Buttons>",
        "<Buttons><AutomaticButtons><ButtonRef name=\"Manual\"/></AutomaticButtons>" + items + "</Buttons>",
        "<Buttons><AutomaticButtons><Placement><BeforeButton name=\"Automatic\"/></Placement></AutomaticButtons>" + items + "</Buttons>",
        "<Buttons autoFill=\"true\"><AutomaticButtons><Placement><BeforeButton name=\"Manual\"/></Placement><ButtonRef name=\"Manual\"/><ButtonRef name=\"Automatic\"/></AutomaticButtons>" + items + "</Buttons>"};
    for (const auto& buttons : variants) {
        const auto parsed = parse_buttons(buttons);
        expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
        if (!parsed) continue;
        const auto& expected = std::get<model::CommandBarPayload>(parsed.value().find_control(model::ObjectId{2})->payload);
        const auto xml = source::serialize_form_xml(parsed.value());
        expect(xml.ok(), "automatic collection state must serialize without a baseline");
        if (!xml) continue;
        const auto roundtrip = source::parse_form_xml(xml.value());
        expect(roundtrip.ok(), "automatic collection state must parse from canonical XML");
        if (!roundtrip) continue;
        const auto& actual = std::get<model::CommandBarPayload>(roundtrip.value().find_control(model::ObjectId{2})->payload);
        expect(actual.buttons_state == expected.buttons_state && actual.buttons == expected.buttons,
            "placement, membership, nested state and individual insertion lock counters must roundtrip independently");
        expect(xml.value().find("<AutomaticInsertionLocks>1</AutomaticInsertionLocks>") == std::string::npos && xml.value().find("autoFill=\"false\"") == std::string::npos,
            "default insertion lock counter and false collection default must be omitted");
    }
    for (const auto locks : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{2}, std::numeric_limits<std::uint64_t>::max()}) {
        for (const auto kind : {"Separator", "Submenu", "Action"}) {
            const auto parsed = parse_buttons(std::string("<Buttons><CommandBarButton name=\"Counted\" type=\"") + kind +
                "\"><AutomaticInsertionLocks>" + std::to_string(locks) + "</AutomaticInsertionLocks>" +
                (std::string_view(kind) == "Action" ? "<Action handler=\"Run\" name=\"\"><Text/><ToolTip/><Description/></Action>" : "") +
                "</CommandBarButton></Buttons>");
            expect(parsed.ok(), "all menu item kinds must accept the full uint64 insertion lock counter");
            if (!parsed) continue;
            const auto xml = source::serialize_form_xml(parsed.value());
            expect(xml.ok(), "insertion lock counter must serialize without deriving a boolean");
            if (!xml) continue;
            expect((xml.value().find("<AutomaticInsertionLocks>") == std::string::npos) == (locks == 1),
                "only the default counter value 1 may be omitted");
            const auto roundtrip = source::parse_form_xml(xml.value());
            expect(roundtrip.ok() && std::get<model::CommandBarPayload>(roundtrip.value().find_control(model::ObjectId{2})->payload).buttons.front().automatic_insertion_locks == locks,
                "zero, multiple holds and UINT64_MAX must survive XML roundtrip exactly");
        }
    }
    const auto independent = parse_buttons("<Buttons autoFill=\"true\"/>");
    expect(independent.ok() && !independent.value().find_control(model::ObjectId{2})->properties().find(model::PropertyId::from_name("AutoFill")),
        "Buttons autoFill must not assign CommandBar AutoFill");
    const auto reverse = parse_buttons("<Buttons/>", true);
    expect(reverse.ok() && !std::get<model::CommandBarPayload>(reverse.value().find_control(model::ObjectId{2})->payload).buttons_state.auto_fill,
        "CommandBar AutoFill must not assign Buttons autoFill");
    const std::vector<std::string> invalid{
        "<Buttons><AutomaticButtons><ButtonRef name=\"Missing\"/></AutomaticButtons>" + items + "</Buttons>",
        "<Buttons><AutomaticButtons><ButtonRef name=\"Manual\"/><ButtonRef name=\"Manual\"/></AutomaticButtons>" + items + "</Buttons>",
        "<Buttons><AutomaticButtons><ButtonRef name=\"\"/></AutomaticButtons>" + items + "</Buttons>",
        "<Buttons><AutomaticButtons><Placement><BeforeButton name=\"Missing\"/></Placement></AutomaticButtons>" + items + "</Buttons>",
        "<Buttons><AutomaticButtons><Placement><AfterLastButton/><AfterLastButton/></Placement></AutomaticButtons></Buttons>",
        "<Buttons><AutomaticButtons><ButtonRef name=\"Manual\"/><Placement><AfterLastButton/></Placement></AutomaticButtons>" + items + "</Buttons>",
        "<Buttons>" + items + "<AutomaticButtons/></Buttons>",
        "<Buttons><AutomaticButtons/><AutomaticButtons/></Buttons>",
        "<Buttons><AutomaticButtons unknown=\"true\"/></Buttons>",
        "<Buttons><AutomaticButtons><Unknown/></AutomaticButtons></Buttons>",
        "<Buttons><CommandBarButton name=\"Bad\" type=\"Separator\"><Buttons autoFill=\"true\"/></CommandBarButton></Buttons>",
        "<Buttons><CommandBarButton name=\"Bad\" type=\"Action\"><Action handler=\"Run\" name=\"\"><Text/><ToolTip/><Description/></Action><Buttons><AutomaticButtons><Placement><AfterLastButton/></Placement></AutomaticButtons></Buttons></CommandBarButton></Buttons>",
        "<Buttons><CommandBarButton name=\"Bad\" type=\"Separator\" automatic=\"true\"/></Buttons>",
        "<Buttons><CommandBarButton name=\"Bad\" type=\"Separator\"><AutomaticInsertionLocks>-1</AutomaticInsertionLocks></CommandBarButton></Buttons>",
        "<Buttons><CommandBarButton name=\"Bad\" type=\"Separator\"><AutomaticInsertionLocks>18446744073709551616</AutomaticInsertionLocks></CommandBarButton></Buttons>",
        "<Buttons><CommandBarButton name=\"Bad\" type=\"Separator\"><AutomaticInsertionLocks>true</AutomaticInsertionLocks></CommandBarButton></Buttons>",
        "<Buttons><CommandBarButton name=\"Bad\" type=\"Separator\"><AutomaticInsertionLocks/></CommandBarButton></Buttons>",
        "<Buttons><CommandBarButton name=\"Bad\" type=\"Separator\"><AutomaticInsertionLocks>0</AutomaticInsertionLocks><AutomaticInsertionLocks>2</AutomaticInsertionLocks></CommandBarButton></Buttons>"};
    const auto before = source::serialize_form_xml(independent.value());
    for (const auto& buttons : invalid) expect(!parse_buttons(buttons), "invalid automatic collection relationships must reject atomically");
    const auto after = source::serialize_form_xml(independent.value());
    expect(before.ok() && after.ok() && before.value() == after.value(), "failed XML candidates must not change an existing document");
    const auto button = source::parse_form_xml(R"XML(<Form id="1" name="ButtonMenu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons autoFill="true"><AutomaticButtons><Placement><AfterLastButton/></Placement></AutomaticButtons></Buttons><MenuMode type="MenuMode" member="Use"/></Button></ChildItems></Form>)XML");
    expect(button.ok() && source::serialize_form_xml(button.value()).ok(), "Button payload must share automatic empty collection state");
}

void test_standard_menu_action_xml_contract() {
    const auto make_xml = [](std::string_view source_name) {
        return std::string("<Form id=\"1\" name=\"StandardAction\" ordinaryFormVersion=\"2.1\"><ChildItems>") +
            "<Button id=\"2\" name=\"Other\"><Position/></Button>" +
            "<CommandBar id=\"4\" name=\"Main\"><Position/><Buttons/></CommandBar>" +
            "<CommandBar id=\"5\" name=\"Secondary\"><Position/><Buttons>" +
            "<CommandBarButton name=\"Close\" type=\"Action\"><StandardAction command=\"Close\" context=\"CommandBar\" commandBarId=\"4\" source=\"" +
            std::string(source_name) + "\"/></CommandBarButton></Buttons></CommandBar>" +
            "</ChildItems></Form>";
    };
    for (const auto [source_name, source_value] : {
             std::pair{"Form", model::StandardMenuActionSource::form},
             std::pair{"AllSources", model::StandardMenuActionSource::all_sources}}) {
        const auto xml = make_xml(source_name);
        const auto parsed = source::parse_form_xml(xml);
        expect(parsed.ok(), parsed ? "StandardAction XML must parse" : parsed.diagnostics().front().message);
        const auto* secondary = parsed.value().find_control(model::ObjectId{5});
        const auto* payload = secondary ? std::get_if<model::CommandBarPayload>(&secondary->payload) : nullptr;
        expect(payload && payload->buttons.size() == 1 && payload->buttons.front().name == "Close" &&
                   payload->buttons.front().standard_action == model::StandardMenuAction{
                       model::StandardMenuCommand::close, model::ControlRef{model::ObjectId{4}}, source_value, model::StandardMenuActionContext::command_bar, {}} &&
                   !payload->buttons.front().action,
            "StandardAction XML must produce a named command and CommandBar reference without a handler");
        const auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok() && serialized.value().find(
                   std::string("<StandardAction command=\"Close\" context=\"CommandBar\" commandBarId=\"4\" source=\"") +
                       source_name + "\"/>") != std::string::npos &&
                   serialized.value().find("handler=\"") == std::string::npos,
            "StandardAction serialization must retain its named source and avoid a synthetic handler");
        const auto reparsed = source::parse_form_xml(serialized.value());
        const auto* reparsed_bar = reparsed ? reparsed.value().find_control(model::ObjectId{5}) : nullptr;
        const auto* reparsed_payload = reparsed_bar ? std::get_if<model::CommandBarPayload>(&reparsed_bar->payload) : nullptr;
        expect(reparsed.ok() && reparsed_payload && reparsed_payload->buttons == payload->buttons,
            "StandardAction XML must round-trip as the same named menu item");
    }

    const auto valid_xml = make_xml("Form");
    const auto replace_once = [](std::string text, std::string_view from, std::string_view to) {
        const auto position = text.find(from);
        if (position == std::string::npos) throw std::runtime_error("StandardAction XML marker is missing");
        text.replace(position, from.size(), to);
        return text;
    };
    expect(!source::parse_form_xml(replace_once(valid_xml, "command=\"Close\"", "command=\"Unknown\"")),
        "unknown named StandardAction command must be rejected");
    expect(!source::parse_form_xml(replace_once(valid_xml, "source=\"Form\"", "source=\"Other\"")),
        "unknown named StandardAction source must be rejected");
    expect(!source::parse_form_xml(replace_once(valid_xml, "commandBarId=\"4\"", "commandBarId=\"999\"")),
        "dangling StandardAction CommandBar reference must be rejected");
    expect(!source::parse_form_xml(replace_once(valid_xml, "commandBarId=\"4\"", "commandBarId=\"2\"")),
        "StandardAction reference to a non-CommandBar control must be rejected");
    expect(!source::parse_form_xml(replace_once(valid_xml,
        "<StandardAction command=\"Close\" context=\"CommandBar\" commandBarId=\"4\" source=\"Form\"/>",
        "<Action handler=\"FakeHandler\" name=\"\"><Text/><ToolTip/><Description/></Action>"
        "<StandardAction command=\"Close\" context=\"CommandBar\" commandBarId=\"4\" source=\"Form\"/>")),
        "an item cannot combine handler Action and StandardAction");
    const auto default_control = replace_once(replace_once(valid_xml, "context=\"CommandBar\" commandBarId=\"4\"", "context=\"Default\""),
        "source=\"Form\"", "source=\"Control\" sourceControlId=\"2\"");
    const auto parsed_control = source::parse_form_xml(default_control);
    expect(parsed_control.ok(), "Default context and generic Button control source must parse");
    const auto& control_action = *std::get<model::CommandBarPayload>(parsed_control.value().find_control(model::ObjectId{5})->payload)
        .buttons.front().standard_action;
    expect(control_action.context == model::StandardMenuActionContext::default_context && !control_action.command_bar &&
        control_action.source == model::StandardMenuActionSource::control && control_action.source_control == model::ControlRef{model::ObjectId{2}},
        "Default must have no CommandBar context reference and Control must have a typed source reference");
    for (const auto bad_id : {"0", "-1", "2147483648", "999", "1"})
        expect(!source::parse_form_xml(replace_once(default_control, "sourceControlId=\"2\"",
            std::string("sourceControlId=\"") + bad_id + "\"")), "invalid, dangling, or form source control ID must reject");
    expect(!source::parse_form_xml(replace_once(default_control, " sourceControlId=\"2\"", "")),
        "Control source requires sourceControlId");
    expect(!source::parse_form_xml(replace_once(default_control, "context=\"Default\"", "context=\"Default\" commandBarId=\"4\"")),
        "Default context rejects mutually exclusive commandBarId");
    expect(!source::parse_form_xml(replace_once(default_control, "context=\"Default\"", "context=\"CommandBar\"")),
        "CommandBar context requires commandBarId");
    const auto form_context = source::parse_form_xml(replace_once(default_control,
        "context=\"Default\"", "context=\"Form\""));
    expect(form_context.ok(), "native-confirmed Form context must use the existing named XML contract");
    expect(!source::parse_form_xml(replace_once(default_control, "context=\"Default\"",
        "context=\"Form\" commandBarId=\"4\"")),
        "Form context must reject a conflicting CommandBar reference");
    for (const auto source_name : {"Form", "AllSources"})
        expect(!source::parse_form_xml(replace_once(default_control, "source=\"Control\"",
            std::string("source=\"") + source_name + "\"")), "non-Control source rejects sourceControlId");
    expect(!source::parse_form_xml(replace_once(default_control, " context=\"Default\"", "")),
        "StandardAction requires an explicit named context");

}

void test_standard_save_restore_action_xml_contract() {
    for (const auto [command, expected] : {
             std::pair{"SaveValues", model::StandardMenuCommand::save_values},
             std::pair{"RestoreValues", model::StandardMenuCommand::restore_values},
             std::pair{"Help", model::StandardMenuCommand::help}}) {
        const std::string xml = std::string("<Form id=\"1\" name=\"RuntimeCommands\" ordinaryFormVersion=\"2.1\"><ChildItems>") +
            "<CommandBar id=\"42\" name=\"Tools\"><Position/><Buttons><CommandBarButton name=\"Runtime\" type=\"Action\">"
            "<StandardAction command=\"" + command + "\" context=\"Default\" source=\"Form\"/>"
            "</CommandBarButton></Buttons></CommandBar></ChildItems></Form>";
        const auto parsed = source::parse_form_xml(xml);
        expect(parsed.ok(), parsed ? "SaveValues and RestoreValues named XML must parse" : parsed.diagnostics().front().message);
        const auto* bar = parsed.value().find_control(model::ObjectId{42});
        const auto* payload = bar ? std::get_if<model::CommandBarPayload>(&bar->payload) : nullptr;
        expect(payload && payload->buttons.size() == 1 && payload->buttons.front().standard_action ==
            model::StandardMenuAction{expected, {}, model::StandardMenuActionSource::form,
                model::StandardMenuActionContext::default_context, {}},
            "runtime standard command XML must use named command, Form source, and Default context");
        const auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok() && serialized.value().find(std::string("<StandardAction command=\"") + command +
            "\" context=\"Default\" source=\"Form\"/>") != std::string::npos &&
            serialized.value().find("commandBarId=") == std::string::npos &&
            serialized.value().find("sourceControlId=") == std::string::npos,
            "runtime standard command writer must not expose storage IDs or reference fields");
        const auto reparsed = source::parse_form_xml(serialized.value());
        expect(reparsed.ok() && std::get<model::CommandBarPayload>(reparsed.value().find_control(model::ObjectId{42})->payload)
            .buttons == payload->buttons,
            "runtime standard command must round-trip through named XML");
        const auto invalid_context = xml;
        auto command_bar_context = invalid_context;
        command_bar_context.replace(command_bar_context.find("context=\"Default\" source"),
            std::string("context=\"Default\" source").size(), "context=\"CommandBar\" commandBarId=\"42\" source");
        const auto explicit_context = source::parse_form_xml(command_bar_context);
        expect(explicit_context.ok() &&
            std::get<model::CommandBarPayload>(explicit_context.value().find_control(model::ObjectId{42})->payload)
                .buttons.front().standard_action->command_bar.id().value() == 42,
            "registered standard commands must preserve an explicit CommandBar context");
        auto all_sources = command_bar_context;
        all_sources.replace(all_sources.find("source=\"Form\""), std::string("source=\"Form\"").size(), "source=\"AllSources\"");
        expect(source::parse_form_xml(all_sources).ok(), "explicit command context must support AllSources independently");
        auto control_source = command_bar_context;
        control_source.replace(control_source.find("source=\"Form\""), std::string("source=\"Form\"").size(),
            "source=\"Control\" sourceControlId=\"42\"");
        const auto explicit_source = source::parse_form_xml(control_source);
        expect(explicit_source.ok() &&
            std::get<model::CommandBarPayload>(explicit_source.value().find_control(model::ObjectId{42})->payload)
                .buttons.front().standard_action->source_control.id().value() == 42,
            "explicit command context and source references must remain independent");
        auto default_source = invalid_context;
        default_source.replace(default_source.find("source=\"Form\""), std::string("source=\"Form\"").size(), "source=\"AllSources\"");
        const auto default_all_sources = source::parse_form_xml(default_source);
        expect(default_all_sources.ok() &&
            std::get<model::CommandBarPayload>(default_all_sources.value().find_control(model::ObjectId{42})->payload)
                .buttons.front().standard_action->source == model::StandardMenuActionSource::all_sources,
            "runtime standard command must retain AllSources in Default context");
    }
}

void test_command_bar_action_source_xml_contract() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="ActionSource" ordinaryFormVersion="2.1">
  <Attributes><Attribute id="10" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes>
  <ChildItems>
    <CommandBar id="2" name="FormSource"><ActionSource formId="1"/><Position/><Buttons><CommandBarButton name="Run" type="Action"><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></CommandBar>
    <CommandBar id="3" name="TableSource"><ActionSource controlId="6"/><Position/><Buttons><CommandBarButton name="Run" type="Action"><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></CommandBar>
    <CommandBar id="4" name="HtmlSource"><ActionSource controlId="7"/><Position/><Buttons><CommandBarButton name="Run" type="Action"><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></CommandBar>
    <CommandBar id="5" name="UndefinedSource"><ActionSource/><Position/><Buttons><CommandBarButton name="Run" type="Action"><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></CommandBar>
    <Table id="6" name="Rows"><DataPath attributeId="10"/><Position/><Columns><Column name="Code"><DataPath>Code</DataPath><Header><Item language="en">Code</Item></Header><Control type="InputField"/></Column></Columns></Table>
    <HTMLDocumentField id="7" name="Html"><Position/></HTMLDocumentField>
    <InputField id="8" name="Input"><Position/></InputField>
  </ChildItems>
</Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto action_source_id = model::PropertyId::from_name("ActionSource");
    expect(std::get<model::FormRef>(parsed.value().find_control(model::ObjectId{2})->extension_properties.find(action_source_id)->value) ==
               model::FormRef{model::ObjectId{1}},
        "ActionSource formId must parse as a FormRef");
    expect(std::get<model::ControlRef>(parsed.value().find_control(model::ObjectId{3})->extension_properties.find(action_source_id)->value) ==
               model::ControlRef{model::ObjectId{6}} &&
               std::get<model::ControlRef>(parsed.value().find_control(model::ObjectId{4})->extension_properties.find(action_source_id)->value) ==
               model::ControlRef{model::ObjectId{7}},
        "ActionSource controlId must resolve to Table and HTMLDocumentField controls");
    expect(parsed.value().find_control(model::ObjectId{5})->extension_properties.find(action_source_id) == nullptr,
        "empty ActionSource must normalize to the omitted Undefined default");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<ActionSource formId=\"1\"/>") != std::string::npos &&
               serialized.value().find("<ActionSource controlId=\"6\"/>") != std::string::npos &&
               serialized.value().find("<ActionSource controlId=\"7\"/>") != std::string::npos &&
               serialized.value().find("<ActionSource/>") == std::string::npos,
        "ActionSource references must use named XML attributes and omit Undefined");
    expect(source::parse_form_xml(serialized.value()).ok(), "serialized ActionSource references must reparse");

    const auto replace_once = [](std::string text, std::string_view from, std::string_view to) {
        const std::size_t position = text.find(from);
        if (position == std::string::npos) throw std::runtime_error("ActionSource test XML marker is missing");
        text.replace(position, from.size(), to);
        return text;
    };
    expect_code(source::parse_form_xml(replace_once(std::string(xml), "formId=\"1\"", "formId=\"99\"")),
        "OOF2004", "ActionSource must reject a FormRef to another form");
    expect_code(source::parse_form_xml(replace_once(std::string(xml), "controlId=\"6\"", "controlId=\"999\"")),
        "OOF2004", "ActionSource must reject a dangling control reference");
    expect_code(source::parse_form_xml(replace_once(std::string(xml), "controlId=\"6\"", "controlId=\"8\"")),
        "OOF2004", "ActionSource must reject an unsupported control source kind");
    expect_code(source::parse_form_xml(replace_once(std::string(xml), "formId=\"1\"", "formId=\"1\" controlId=\"6\"")),
        "OOF2003", "ActionSource must reject two reference attributes");
    expect_code(source::parse_form_xml(replace_once(std::string(xml), "<ActionSource/>", "<ActionSource>text</ActionSource>")),
        "OOF2002", "ActionSource must reject text content");
    expect_code(source::parse_form_xml(replace_once(std::string(xml), "<ActionSource/>", "<ActionSource otherId=\"6\"/>")),
        "OOF2002", "ActionSource must reject unrecognized raw attributes");
    expect_code(source::parse_form_xml(
        "<Form id=\"1\" name=\"NoExtension\" ordinaryFormVersion=\"2.1\"><ChildItems>"
        "<InputField id=\"2\" name=\"Input\"><ActionSource formId=\"1\"/><Position/></InputField>"
        "</ChildItems></Form>"),
        "OOF2002", "ActionSource must not be declared on other control types");
}

void test_context_menu_named_xml_contract() {
    const std::string xml = R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
      <CommandBar id="2" name="Menu"><Position/><Buttons><CommandBarButton name="Parent" type="Submenu"><Buttons><CommandBarButton name="Child" type="Submenu"/></Buttons></CommandBarButton></Buttons></CommandBar>
      <InputField id="3" name="PanelMenu"><ContextMenu controlId="2"/><Position/></InputField>
      <InputField id="4" name="NestedMenu"><ContextMenu controlId="2"><Button name="Parent"/><Button name="Child"/></ContextMenu><Position/></InputField>
      <InputField id="5" name="NoMenu"><ContextMenu/><Position/></InputField>
    </ChildItems></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto id = model::PropertyId::from_name("ContextMenu");
    const auto& panel = parsed.value().find_control(model::ObjectId{3})->extension_properties.find(id)->value;
    expect(std::get<model::ControlRef>(panel).id() == model::ObjectId{2}, "ContextMenu must reference CommandBar");
    const auto& nested = parsed.value().find_control(model::ObjectId{4})->extension_properties.find(id)->value;
    const auto& ref = std::get<model::CommandBarButtonRef>(nested);
    expect(ref.owner.id() == model::ObjectId{2} && ref.path == std::vector<std::string>{"Parent", "Child"},
        "ContextMenu must preserve the complete named button path");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "ContextMenu XML must serialize");
    expect(serialized.value().find("<ContextMenu controlId=\"2\">") != std::string::npos &&
           serialized.value().find("<Button name=\"Parent\"/>") != std::string::npos &&
           serialized.value().find("<Button name=\"Child\"/>") != std::string::npos,
        "ContextMenu XML must contain the named button path");
    expect(source::parse_form_xml(serialized.value()).ok(), "serialized ContextMenu XML must reparse");
    const auto replace_once = [](std::string text, std::string_view from, std::string_view to) {
        const auto pos = text.find(from);
        if (pos == std::string::npos) throw std::runtime_error("ContextMenu XML marker is missing");
        text.replace(pos, from.size(), to);
        return text;
    };
    for (const auto invalid : {"<ContextMenu><Button name=\"Parent\"/></ContextMenu>",
             "<ContextMenu controlId=\"2\"><Button name=\"\"/></ContextMenu>",
             "<ContextMenu controlId=\"2\"><Button name=\"Parent\">text</Button></ContextMenu>",
             "<ContextMenu controlId=\"2\"><Index>1</Index></ContextMenu>"})
        expect(!source::parse_form_xml(replace_once(xml, "<ContextMenu/>", invalid)),
            "ContextMenu must reject malformed button paths");
}

void test_form_context_menu_named_xml_contract() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
      <CommandBar id="2" name="Menu"><Position/><Buttons><CommandBarButton name="Parent" type="Submenu"><Buttons><CommandBarButton name="Child" type="Submenu"/></Buttons></CommandBarButton></Buttons></CommandBar>
      <InputField id="3" name="Input"><Position/></InputField>
    </ChildItems></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    auto& form = const_cast<model::Form&>(parsed.value().form());
    const auto auto_id = model::PropertyId::from_name("AutoContextMenu");
    const auto menu_id = model::PropertyId::from_name("ContextMenu");
    form.properties.set_explicit(auto_id, false);
    form.properties.set_explicit(menu_id, model::CommandBarButtonRef{model::ControlRef{model::ObjectId{2}}, {"Parent", "Child"}});
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), serialized ? "" : serialized.diagnostics().front().message);
    expect(serialized.value().find("<AutoContextMenu>false</AutoContextMenu>") != std::string::npos &&
        serialized.value().find("<ContextMenu controlId=\"2\">") != std::string::npos &&
        serialized.value().find("<Button name=\"Parent\"/>") != std::string::npos &&
        serialized.value().find("<Button name=\"Child\"/>") != std::string::npos,
        "Form ContextMenu must use named XML and preserve AutoContextMenu=false");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), reparsed ? "" : reparsed.diagnostics().front().message);
    const auto* auto_value = reparsed.value().form().properties.find(auto_id);
    const auto* menu_value = reparsed.value().form().properties.find(menu_id);
    expect(auto_value && std::get_if<bool>(&auto_value->value) && !std::get<bool>(auto_value->value),
        "Form AutoContextMenu=false must survive XML");
    const auto* reference = menu_value ? std::get_if<model::CommandBarButtonRef>(&menu_value->value) : nullptr;
    expect(reference && reference->owner.id() == model::ObjectId{2} &&
        reference->path == std::vector<std::string>{"Parent", "Child"},
        "Form ContextMenu must preserve the nested button reference");
}

void test_command_bar_border_xml_contract() {
    const auto xml_for = [](std::string_view border) {
        return std::string{"<Form id=\"1\" name=\"Border\" ordinaryFormVersion=\"2.1\"><ChildItems>"
            "<CommandBar id=\"2\" name=\"Tools\"><Position/>"} + std::string(border) +
            "</CommandBar></ChildItems></Form>";
    };
    const auto round_trip = [&](std::string_view border, const model::BorderValue& expected) {
        auto parsed = source::parse_form_xml(xml_for(border));
        expect(parsed.ok(), "valid Border XML must parse and pass generated schema");
        const auto* property = parsed.value().find_control(model::ObjectId{2})->properties().find(
            model::PropertyId::from_name("Border"));
        expect(property && std::get<model::BorderValue>(property->value) == expected,
            "Border XML must retain typed fields");
        const auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok(), "typed Border must serialize");
        auto reparsed = source::parse_form_xml(serialized.value());
        expect(reparsed.ok(), "serialized Border must parse");
        const auto* repeated = reparsed.value().find_control(model::ObjectId{2})->properties().find(
            model::PropertyId::from_name("Border"));
        expect(repeated && std::get<model::BorderValue>(repeated->value) == expected,
            "typed Border must round-trip symmetrically");
    };
    constexpr std::array<std::pair<model::ControlBorderType, std::string_view>, 9> types{{
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
    for (const auto& [type, name] : types) {
        if (type == model::ControlBorderType::rounded) {
            expect(!source::parse_form_xml(xml_for("<Border kind=\"absolute\" borderType=\"Rounded\" width=\"1\"/>")),
                "Rounded is not a CommandBar border type");
            continue;
        }
        model::BorderValue expected; expected.border_type = type; expected.width = 1;
        round_trip("<Border kind=\"absolute\" borderType=\"" + std::string(name) + "\" width=\"1\"/>", expected);
    }
    for (std::uint32_t width = 0; width <= 5; ++width) {
        model::BorderValue expected; expected.border_type = model::ControlBorderType::double_line; expected.width = width;
        round_trip("<Border kind=\"absolute\" borderType=\"Double\" width=\"" + std::to_string(width) + "\"/>", expected);
    }
    model::BorderValue style; style.kind = model::BorderKind::style_reference;
    style.style = model::QualifiedName{"StyleBorders.ControlBorder"};
    round_trip("<Border kind=\"styleReference\" styleName=\"StyleBorders.ControlBorder\"/>", style);
    style.style = model::CompositeIdValue{5, model::UuidValue{"12345678-1234-1234-1234-123456789abc"}, false};
    round_trip("<Border kind=\"styleReference\" styleObjectId=\"5\" styleUuid=\"12345678-1234-1234-1234-123456789abc\"/>", style);
    auto default_document = source::parse_form_xml(xml_for("<Border kind=\"absolute\" borderType=\"WithoutBorder\" width=\"0\"/>"));
    expect(default_document.ok() && !default_document.value().find_control(model::ObjectId{2})->properties().find(
        model::PropertyId::from_name("Border")), "default Border must be implicit after parsing");
    const auto document_with_border = [&](const model::BorderValue& border) {
        model::OrdinaryFormDocument document(default_document.value().form());
        auto bar = *default_document.value().find_control(model::ObjectId{2});
        bar.properties().set_explicit(model::PropertyId::from_name("Border"), border);
        document.add_control(std::move(bar));
        return document;
    };
    const auto omitted = source::serialize_form_xml(document_with_border(model::BorderValue{}));
    expect(omitted.ok() && omitted.value().find("<Border ") == std::string::npos,
        "explicit default Border must serialize as omitted");

    for (const auto border : {
        "<Border kind=\"automatic\"/>",
        "<Border kind=\"absolute\" width=\"1\"/>",
        "<Border kind=\"absolute\" borderType=\"Single\"/>",
        "<Border kind=\"absolute\" borderType=\"Unknown\" width=\"1\"/>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"6\"/>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"-1\"/>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"1.5\"/>",
        "<Border kind=\"absolute\" borderType=\"WithoutBorder\" width=\"2\"/>",
        "<Border kind=\"absolute\" borderType=\"Rounded\" width=\"0\"/>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"1\" styleName=\"StyleBorders.ControlBorder\"/>",
        "<Border kind=\"styleReference\" styleName=\"StyleBorders.ControlBorder\" borderType=\"WithoutBorder\"/>",
        "<Border kind=\"styleReference\" styleName=\"StyleBorders.ControlBorder\" width=\"0\"/>",
        "<Border kind=\"styleReference\"/>",
        "<Border kind=\"styleReference\" styleName=\"\"/>",
        "<Border kind=\"styleReference\" styleObjectId=\"5\"/>",
        "<Border kind=\"styleReference\" styleObjectId=\"0\" styleUuid=\"00000000-0000-0000-0000-000000000000\"/>",
        "<Border kind=\"styleReference\" styleName=\"StyleBorders.ControlBorder\" styleObjectId=\"5\" styleUuid=\"12345678-1234-1234-1234-123456789abc\"/>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"1\" extra=\"1\"/>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"1\"><Width>1</Width></Border>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"1\">1</Border>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"1\" width=\"2\"/>",
        "<Border kind=\"absolute\" borderType=\"Single\" width=\"1\"/><Border kind=\"absolute\" borderType=\"Single\" width=\"2\"/>"}) {
        expect(!source::parse_form_xml(xml_for(border)).ok(), "invalid Border XML must be rejected");
    }
    model::BorderValue invalid; invalid.width = 6;
    expect(!source::serialize_form_xml(document_with_border(invalid)).ok(), "writer must reject invalid model Border");
    invalid = model::BorderValue{}; invalid.kind = model::BorderKind::style_reference;
    expect(!source::serialize_form_xml(document_with_border(invalid)).ok(), "writer must reject style Border without a reference");
}

void test_chart_base_value_xml() {
    const auto xml = [](std::string_view property) {
        return std::string("<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems><Chart id=\"2\" name=\"Chart\"><Position/>") +
            std::string(property) + "<Series/><Points/><Values/></Chart></ChildItems></Form>";
    };
    const auto property = model::PropertyId::from_name("BaseValue");
    const auto chart = model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::control, model::ObjectId{2}};
    for (const auto* text : {"12345678901234567890123456789012345678", "0.00000000000000000000000000000000000001",
             "-123456789012345.12345678901234567890123", "0"}) {
        const auto parsed = source::parse_form_xml(xml("<BaseValue>" + std::string(text) + "</BaseValue>"));
        expect(parsed.ok(), "valid Chart BaseValue must parse from XML");
        const auto read = parsed.value().get_prop_val(chart, property);
        expect(read.value && *read.value == model::PropertyValue{model::DecimalValue{text}},
            "Chart BaseValue XML must preserve decimal precision");
        const auto written = source::serialize_form_xml(parsed.value());
        expect(written.ok(), "valid Chart BaseValue must serialize");
        const auto reparsed = source::parse_form_xml(written.value());
        expect(reparsed.ok(), "serialized Chart BaseValue must parse again");
        const auto reread = reparsed.value().get_prop_val(chart, property);
        expect(reread.value && *reread.value == *read.value, "Chart BaseValue must survive XML roundtrip");
    }
    for (const auto* invalid : {"bad", "1.2.3", "1e3", ""})
        expect(!source::parse_form_xml(xml("<BaseValue>" + std::string(invalid) + "</BaseValue>")).ok(),
            "invalid decimal Chart BaseValue XML must be rejected");
    expect(!source::parse_form_xml(xml("<BaseValue>1</BaseValue><BaseValue>2</BaseValue>")).ok(),
        "duplicate Chart BaseValue must be rejected");
    auto document = source::parse_form_xml(xml(""));
    expect(document.ok(), "Chart BaseValue can be omitted");
    expect(document.value().set_prop_val(chart, property, model::DecimalValue{"bad"}) == model::PropertyMutationStatus::applied,
        "property API accepts the DecimalValue type");
    expect(!source::serialize_form_xml(document.value()).ok(), "writer must reject malformed BaseValue decimal");
}

void test_chart_enum_properties_xml() {
    const auto xml = [](std::string_view property) {
        return std::string("<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems><Chart id=\"2\" name=\"Chart\"><Position/>") +
            std::string(property) + "<Series/><Points/><Values/></Chart></ChildItems></Form>";
    };
    const auto chart = model::OrdinaryFormDocument::ObjectKey{model::ObjectCategory::control, model::ObjectId{2}};
    for (const auto& spec : model::detail::chart_enum_specs) {
        const auto property_id = model::PropertyId::from_name(spec.property);
        const auto element = [&](std::string_view type, std::string_view member) {
            return "<" + std::string(spec.property) + " type=\"" + std::string(type) +
                "\" member=\"" + std::string(member) + "\"/>";
        };
        const auto omitted = source::parse_form_xml(xml(""));
        expect(omitted.ok(), "Chart enum property may be omitted");
        expect(spec.default_ordinal >= 0 && static_cast<std::size_t>(spec.default_ordinal) < spec.members.size(),
            "Chart enum default must belong to its domain");
        const auto default_value = omitted.value().get_prop_val(chart, property_id);
        expect(default_value.status == model::PropertyReadStatus::proven_default && default_value.value &&
            *default_value.value == model::PropertyValue{model::EnumerationValue{
                std::string(spec.type_name), std::string(spec.members[static_cast<std::size_t>(spec.default_ordinal)])}},
            "omitted Chart enum property must use its named default");
        for (const auto member : spec.members) {
            const model::PropertyValue expected = model::EnumerationValue{std::string(spec.type_name), std::string(member)};
            const auto parsed = source::parse_form_xml(xml(element(spec.type_name, member)));
            expect(parsed.ok(), "known Chart enum value must parse from XML");
            const auto read = parsed.value().get_prop_val(chart, property_id);
            expect(read.value && *read.value == expected, "Chart enum XML must expose the named value");
            const auto written = source::serialize_form_xml(parsed.value());
            expect(written.ok(), "known Chart enum value must serialize to XML");
            const auto reparsed = source::parse_form_xml(written.value());
            expect(reparsed.ok(), "serialized Chart enum must parse again");
            const auto roundtrip = reparsed.value().get_prop_val(chart, property_id);
            expect(roundtrip.value && *roundtrip.value == expected, "Chart enum value must survive XML roundtrip");
        }
        expect(!source::parse_form_xml(xml(element(spec.type_name, "Unknown"))).ok(),
            "unknown Chart enum member must fail");
        expect(!source::parse_form_xml(xml(element("OtherType", spec.members.front()))).ok(),
            "foreign Chart enum type must fail");
        expect(!source::parse_form_xml(xml(element(spec.type_name, spec.members.front()) +
            element(spec.type_name, spec.members.front()))).ok(), "duplicate Chart enum property must fail");
    }
}

void test_chart_type_xml() {
    const auto xml = [](std::string_view property) {
        return std::string("<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems><Chart id=\"2\" name=\"Chart\"><Position/>") +
            std::string(property) + "<Series/><Points/><Values/></Chart></ChildItems></Form>";
    };
    const auto property_id = model::PropertyId::from_name("ChartType");
    for (const auto& [name, ordinal] : model::detail::chart_type_members) {
        static_cast<void>(ordinal);
        const auto input = xml("<ChartType type=\"ChartType\" member=\"" + std::string(name) + "\"/>");
        const auto parsed = source::parse_form_xml(input);
        expect(parsed.ok(), "every known ChartType must parse from XML");
        const auto value = parsed.value().get_prop_val({model::ObjectCategory::control, model::ObjectId{2}}, property_id);
        expect(value.value && *value.value == model::PropertyValue{model::EnumerationValue{"ChartType", std::string(name)}},
            "ChartType XML must expose its named value");
        const auto written = source::serialize_form_xml(parsed.value());
        expect(written.ok(), "every known ChartType must serialize to XML");
        const auto reparsed = source::parse_form_xml(written.value());
        expect(reparsed.ok(), "serialized ChartType must parse again");
        const auto roundtrip = reparsed.value().get_prop_val({model::ObjectCategory::control, model::ObjectId{2}}, property_id);
        expect(roundtrip.value && *roundtrip.value == *value.value, "ChartType value must survive XML roundtrip");
    }
    for (const auto invalid : {"<ChartType type=\"ChartType\" member=\"Unknown\"/>",
             "<ChartType type=\"OtherType\" member=\"Column\"/>", "<ChartType type=\"ChartType\"/>",
             "<ChartType type=\"ChartType\" member=\"Column\"/><ChartType type=\"ChartType\" member=\"Line\"/>"}) {
        expect(!source::parse_form_xml(xml(invalid)).ok(), "invalid or duplicate ChartType XML must fail");
    }
    const auto omitted = source::parse_form_xml(xml(""));
    expect(omitted.ok(), "ChartType can be omitted");
    const auto default_value = omitted.value().get_prop_val({model::ObjectCategory::control, model::ObjectId{2}}, property_id);
    expect(default_value.status == model::PropertyReadStatus::proven_default && default_value.value &&
        *default_value.value == model::PropertyValue{model::EnumerationValue{"ChartType", "Column"}},
        "omitted ChartType must use Column");
}

void test_chart_summary_series_color_xml() {
    const auto xml = [](std::string_view summary) {
        return std::string("<Form id=\"1\" name=\"Main\" ordinaryFormVersion=\"2.1\"><ChildItems><Chart id=\"2\" name=\"Chart\"><Position/>") +
            std::string(summary) + "<Series/><Points/><Values/></Chart></ChildItems></Form>";
    };
    auto parsed = source::parse_form_xml(xml("<SummarySeries><Color kind=\"absolute\" red=\"153\" green=\"25\" blue=\"25\"/></SummarySeries>"));
    expect(parsed.ok(), "named SummarySeries Color must parse");
    const auto& color = std::get<model::ChartPayload>(parsed.value().collections().controls.front().payload).summary_series.color;
    expect(color.kind == model::ColorKind::absolute && color.red == 153 && color.green == 25 && color.blue == 25,
        "SummarySeries Color must retain actual RGB");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<SummarySeries>") != std::string::npos,
        "nondefault SummarySeries must serialize");
    auto roundtrip = source::parse_form_xml(serialized.value());
    expect(roundtrip.ok() && std::get<model::ChartPayload>(roundtrip.value().collections().controls.front().payload).summary_series.color == color,
        "SummarySeries Color must roundtrip");
    auto marker = source::parse_form_xml(xml("<SummarySeries><Marker type=\"ChartMarkerType\" member=\"Rhomb\"/></SummarySeries>"));
    expect(marker.ok(), "proven SummarySeries Rhomb must parse");
    auto marker_xml = source::serialize_form_xml(marker.value());
    expect(marker_xml.ok() && marker_xml.value().find("member=\"Rhomb\"") != std::string::npos &&
        marker_xml.value().find("<Color ") == std::string::npos, "Marker-only SummarySeries must omit default Color");
    auto marker_roundtrip = source::parse_form_xml(marker_xml.value());
    expect(marker_roundtrip.ok() && std::get<model::ChartPayload>(marker_roundtrip.value().collections().controls.front().payload).summary_series.marker.member == "Rhomb",
        "SummarySeries Marker must roundtrip");
    auto default_marker = source::parse_form_xml(xml("<SummarySeries><Marker type=\"ChartMarkerType\" member=\"Auto\"/></SummarySeries>"));
    expect(default_marker.ok() && source::serialize_form_xml(default_marker.value()).value().find("<SummarySeries>") == std::string::npos,
        "default SummarySeries Auto Marker must be omitted");
    for (const auto member : {"None", "Rect", "Circle", "Rhomb", "Auto", "Alternation"})
        expect(source::parse_form_xml(xml(std::string{"<SummarySeries><Marker type=\"ChartMarkerType\" member=\""} + member + "\"/></SummarySeries>")).ok(), "all six native SummarySeries Marker values parse");
    expect(!source::parse_form_xml(xml("<SummarySeries><Marker type=\"ChartMarkerType\" member=\"Rhomb\"/><Marker type=\"ChartMarkerType\" member=\"Rhomb\"/></SummarySeries>")).ok(),
        "duplicate SummarySeries Marker must fail");
    auto empty = source::parse_form_xml(xml("<SummarySeries/>"));
    expect(empty.ok(), "empty SummarySeries must mean default automatic Color");
    auto empty_serialized = source::serialize_form_xml(empty.value());
    expect(empty_serialized.ok() && empty_serialized.value().find("<SummarySeries>") == std::string::npos,
        "empty default SummarySeries must be omitted");
    auto automatic = source::parse_form_xml(xml("<SummarySeries><Color kind=\"automatic\"/></SummarySeries>"));
    expect(automatic.ok(), "automatic SummarySeries Color must parse");
    auto omitted = source::serialize_form_xml(automatic.value());
    expect(omitted.ok() && omitted.value().find("<SummarySeries>") == std::string::npos,
        "default automatic SummarySeries must be omitted");
    expect(!source::parse_form_xml(xml("<SummarySeries><Color kind=\"absolute\" alpha=\"1\"/></SummarySeries>")).ok(),
        "SummarySeries Color must reject nonopaque RGB");
    expect(!source::parse_form_xml(xml("<SummarySeries><Color kind=\"styleReference\" styleName=\"Accent\"/></SummarySeries>")).ok(),
        "SummarySeries Color must reject unproven style references");
    expect(!source::parse_form_xml(xml("<SummarySeries><Color kind=\"automatic\"/></SummarySeries><SummarySeries><Color kind=\"automatic\"/></SummarySeries>")).ok(),
        "duplicate SummarySeries must fail");
}

void test_runtime_properties_are_not_public_xml() {
    for (const auto fragment : {"<ListBox id=\"2\" name=\"List\"><Position/><CurrentRow/></ListBox>",
                                "<HTMLDocumentField id=\"2\" name=\"Html\"><Position/><Document/></HTMLDocumentField>"}) {
        expect(!source::parse_form_xml(std::string{"<Form id=\"1\" name=\"Runtime\" ordinaryFormVersion=\"2.1\"><ChildItems>"} + fragment + "</ChildItems></Form>"),
            "live selected rows and DOM must not enter public XML as empty placeholders");
    }
    for (const auto name : {"DataSource", "Fields"}) {
        expect(!source::parse_form_xml(std::string{"<Form id=\"1\" name=\"Runtime\" ordinaryFormVersion=\"2.1\"><ChildItems><PivotChart id=\"2\" name=\"Pivot\"><Position/><"} + name + "/></PivotChart></ChildItems></Form>"),
            "live Pivot source and field references cannot enter XML as empty placeholders");
    }
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Runtime" ordinaryFormVersion="2.1"><FormOwner/></Form>)XML"),
        "runtime FormOwner must not enter public XML as an empty or raw reference placeholder");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Runtime" ordinaryFormVersion="2.1"><CurrentControl/></Form>)XML"),
        "runtime CurrentControl must be excluded from public XML before its reference API is implemented");
    for (const auto fragment : {
             R"XML(<CalendarField id="2" name="C"><Position/><CurrentDate>2026-12-25T00:00:00</CurrentDate></CalendarField>)XML",
             R"XML(<CalendarField id="2" name="C"><Position/><SelectedDates/></CalendarField>)XML",
             R"XML(<InputField id="2" name="I"><Position/><ChoiceIncomplete>false</ChoiceIncomplete></InputField>)XML",
             R"XML(<InputField id="2" name="I"><Position/><MarkIncomplete>false</MarkIncomplete></InputField>)XML",
             R"XML(<Chart id="2" name="C"><Position/><RefreshEnabled>false</RefreshEnabled><Series/><Points/><Values/></Chart>)XML"}) {
        const std::string xml = std::string(R"XML(<Form id="1" name="Runtime" ordinaryFormVersion="2.1"><ChildItems>)XML") +
            fragment + "</ChildItems></Form>";
        expect(!source::parse_form_xml(xml), "runtime-only properties must fail at the public XML boundary");
    }
    expect(source::parse_form_xml(R"XML(<Form id="1" name="Runtime" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><Series/><Points/><Values/></Chart></ChildItems></Form>)XML").ok(),
        "runtime Chart rejection fixture must otherwise parse");
    const auto bands_xml = R"XML(<Form id="1" name="Bands" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Gauge"><Position/><GaugeChartQualityBands><GaugeChartQualityBand><Begin>-5.25</Begin><End>0</End><BackColor kind="absolute" red="12" green="34" blue="56"/><Text>Low</Text><ToolTip><Item language="ru">Низкое значение</Item></ToolTip></GaugeChartQualityBand><GaugeChartQualityBand><Begin>0.125</Begin><End>10.875</End><BackColor kind="absolute" red="200" green="100" blue="50"/><Text><Item language="en">High</Item></Text><ToolTip>Details</ToolTip></GaugeChartQualityBand></GaugeChartQualityBands><Series/><Points/><Values/></Chart></ChildItems></Form>)XML";
    auto bands_document = source::parse_form_xml(bands_xml);
    expect(bands_document.ok(), "named gauge quality bands parse as chart objects");
    if (bands_document) {
        const auto& chart = std::get<model::ChartPayload>(bands_document.value().find_control(model::ObjectId{2})->payload);
        expect(chart.gauge_quality_bands.size() == 2 && chart.gauge_quality_bands[0].begin.canonical == "-5.25" &&
            chart.gauge_quality_bands[1].end.canonical == "10.875" && std::holds_alternative<model::LocalizedStringValue>(chart.gauge_quality_bands[0].tool_tip) &&
            std::holds_alternative<model::LocalizedStringValue>(chart.gauge_quality_bands[1].text),
            "GaugeChartQualityBand retains decimal boundaries and localized text values");
        const auto serialized_bands = source::serialize_form_xml(bands_document.value());
        expect(serialized_bands.ok(), "gauge quality bands serialize through their named object model");
        if (serialized_bands) {
            const auto parsed_again = source::parse_form_xml(serialized_bands.value());
            expect(parsed_again.ok() && std::get<model::ChartPayload>(parsed_again.value().find_control(model::ObjectId{2})->payload).gauge_quality_bands == chart.gauge_quality_bands,
                "gauge quality bands round trip with their typed values");
        }
    }
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Bands" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Gauge"><Position/><GaugeChartQualityBands><GaugeChartQualityBand><Begin>2</Begin><End>1</End><BackColor kind="automatic"/><Text/><ToolTip/></GaugeChartQualityBand></GaugeChartQualityBands><Series/><Points/><Values/></Chart></ChildItems></Form>)XML"),
        "gauge quality bands reject inverted ranges");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Bands" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Gauge"><Position/><GaugeChartQualityBands/><GaugeChartQualityBands/><Series/><Points/><Values/></Chart></ChildItems></Form>)XML"),
        "gauge quality bands reject duplicate collections");
    auto empty_bands = source::parse_form_xml(R"XML(<Form id="1" name="Bands" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Gauge"><Position/><Series/><Points/><Values/></Chart></ChildItems></Form>)XML");
    bool empty_bands_omitted = false;
    if (empty_bands) {
        const auto empty_bands_xml = source::serialize_form_xml(empty_bands.value());
        empty_bands_omitted = empty_bands_xml.ok() && empty_bands_xml.value().find("GaugeChartQualityBands") == std::string::npos;
    }
    expect(empty_bands_omitted, "empty gauge quality bands are omitted canonically");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Bands" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Gauge"><Position/><GaugeChartQualityBands><GaugeChartQualityBand><Begin>not-a-number</Begin><End>1</End><BackColor kind="automatic"/><Text/><ToolTip/></GaugeChartQualityBand></GaugeChartQualityBands><Series/><Points/><Values/></Chart></ChildItems></Form>)XML"),
        "gauge quality bands reject non-decimal boundaries");
    auto parsed = source::parse_form_xml(R"XML(<Form id="1" name="Runtime" ordinaryFormVersion="2.1"><ChildItems><CalendarField id="2" name="C"><Position/></CalendarField></ChildItems></Form>)XML");
    expect(parsed.ok(), "runtime boundary fixture must parse");
    if (!parsed) return;
    const auto before_runtime = source::serialize_form_xml(parsed.value());
    expect(parsed.value().set_prop_val({model::ObjectCategory::control, model::ObjectId{2}},
               model::PropertyId::from_name("CurrentDate"), model::DateValue{"2026-12-25T00:00:00"}) ==
               model::PropertyMutationStatus::applied_runtime_only,
        "runtime-only properties remain available in the in-memory object model");
    const auto after_runtime = source::serialize_form_xml(parsed.value());
    expect(before_runtime.ok() && after_runtime.ok() && after_runtime.value() == before_runtime.value(),
        "separate calendar runtime state must not change public XML");
    model::OrdinaryFormDocument shadow(parsed.value().form());
    auto shadow_calendar = *parsed.value().find_control(model::ObjectId{2});
    shadow_calendar.properties().set_explicit(model::PropertyId::from_name("CurrentDate"),
        model::DateValue{"2026-12-25T00:00:00"});
    shadow.add_control(std::move(shadow_calendar));
    expect(!source::serialize_form_xml(shadow),
        "XML writer must still reject manually persisted runtime shadows");
}

void test_chart_source_mode_xml() {
    std::string xml = R"XML(<Form id="1" name="SourceCharts" ordinaryFormVersion="2.1"><ChildItems>)XML";
    for (std::uint64_t i = 0; i < 12; ++i) {
        const auto series_count = i % 4;
        const auto point_count = i % 5;
        xml += "<Chart id=\"" + std::to_string(100 + i) + "\" name=\"Source" + std::to_string(i) + "\"><Position/><Series>";
        for (std::uint64_t j = 0; j < series_count; ++j)
            xml += "<ChartSeries id=\"" + std::to_string(j + 2) + "\"><Text/><Color kind=\"automatic\"/><Marker type=\"ChartMarkerType\" member=\"Auto\"/></ChartSeries>";
        xml += "</Series><Points>";
        for (std::uint64_t j = 0; j < point_count; ++j)
            xml += "<ChartPoint id=\"" + std::to_string(j + 1) + "\"><Text/><Color kind=\"automatic\"/></ChartPoint>";
        xml += "</Points><Values dataMode=\"Source\" sourceDescription=\"Description &amp; chart " + std::to_string(i) + "\"/></Chart>";
    }
    xml += "</ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "Source-mode XML accepts twelve charts with varied dimensions and no dense Values");
    if (parsed) {
        for (std::uint64_t i = 0; i < 12; ++i) {
            const auto* control = parsed.value().find_control(model::ObjectId{100 + i});
            const auto* chart = control ? std::get_if<model::ChartPayload>(&control->payload) : nullptr;
            expect(chart && chart->data_mode == model::ChartDataMode::source && chart->values.empty() &&
                chart->series.size() == i % 4 && chart->points.size() == i % 5 &&
                chart->data_source_description == "Description & chart " + std::to_string(i),
                "Source mode preserves collection dimensions and decoded descriptions");
        }
        const auto serialized = source::serialize_form_xml(parsed.value());
        expect(serialized.ok(), "Source-mode XML serializes without dense values");
        if (serialized) {
            const auto restored = source::parse_form_xml(serialized.value());
            expect(restored.ok() && std::get<model::ChartPayload>(restored.value().find_control(model::ObjectId{105})->payload).data_source_description == "Description & chart 5",
                "Source-mode description survives serialization");
        }
    }
    for (const auto bad_xml : {
        R"XML(<Form id="1" name="Source" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><Series/><Points/><Values dataMode="Source"/><DataSource><Undefined/></DataSource></Chart></ChildItems></Form>)XML",
        R"XML(<Form id="1" name="Source" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><Series/><Points/><Values dataMode="Source" sourceDescription="source"><ChartValue seriesRef="1" pointRef="1"><Number>1</Number></ChartValue></Values></Chart></ChildItems></Form>)XML",
        R"XML(<Form id="1" name="Source" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><Series/><Points/><Values dataMode="Source" unexpected="x"/></Chart></ChildItems></Form>)XML",
        R"XML(<Form id="1" name="Source" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><Series/><Points/><Values sourceDescription="outside"/></Chart></ChildItems></Form>)XML",
        R"XML(<Form id="1" name="Source" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><Series/><Points/><Values dataMode="Explicit" sourceDescription="outside"/></Chart></ChildItems></Form>)XML"})
        expect(!source::parse_form_xml(bad_xml), "Source-mode XML rejects cells, unknown attributes, and out-of-mode descriptions");
}

void test_control_value_is_not_persisted_xml() {
    for (const auto kind : {"InputField", "CheckBox", "ChoiceField", "RadioButton"}) {
        const auto xml = std::string("<Form id=\"1\" name=\"Values\" ordinaryFormVersion=\"2.1\"><ChildItems><") + kind +
            " id=\"2\" name=\"C\"><Value><Undefined/></Value><Position/></" + kind + "></ChildItems></Form>";
        expect(!source::parse_form_xml(xml), "runtime Value cannot enter public persisted XML");
    }
}

void test_table_current_parent_is_not_persisted_xml() {
    const auto result = source::parse_form_xml(R"XML(<Form id="1" name="Parents" ordinaryFormVersion="2.1"><ChildItems><Table id="2" name="T"><Position/><CurrentParent/><Columns/></Table></ChildItems></Form>)XML");
    expect(!result, "runtime CurrentParent must not enter the persisted Table XML");
}

void test_graphical_current_item_is_not_persisted_xml() {
    const auto result = source::parse_form_xml(R"XML(<Form id="1" name="Graph" ordinaryFormVersion="2.1"><ChildItems><GraphicalSchemaField id="2" name="G"><Position/><CurrentItem><Undefined/></CurrentItem></GraphicalSchemaField></ChildItems></Form>)XML");
    expect(!result, "runtime CurrentItem must not enter the public persisted GraphicalSchemaField XML");
}

void test_geographical_showed_area_is_not_persisted_xml() {
    const auto result = source::parse_form_xml(R"XML(<Form id="1" name="Map" ordinaryFormVersion="2.1"><ChildItems><GeographicalSchemaField id="2" name="Geo"><Position/><ShowedArea/></GeographicalSchemaField></ChildItems></Form>)XML");
    expect(!result, "runtime ShowedArea must not enter the persisted geographical XML");
}

void test_chart_active_point_xml_reference() {
    using namespace oof::model;
    std::string xml = R"XML(<Form id="1" name="ActivePoints" ordinaryFormVersion="2.1"><ChildItems>)XML";
    for (std::uint64_t i = 0; i < 12; ++i) {
        xml += "<Chart id=\"" + std::to_string(100 + i) + "\" name=\"C" + std::to_string(i) + "\"><Position/>";
        if (i % 3 == 1) xml += "<ActivePoint>20</ActivePoint>";
        else if (i % 3 == 2) xml += "<ActivePoint/>";
        xml += "<Series/><Points>";
        if (i % 3 != 0)
            xml += R"XML(<ChartPoint id="10"><Text>First</Text><Color kind="automatic"/></ChartPoint><ChartPoint id="20"><Text>Second</Text><Color kind="automatic"/></ChartPoint>)XML";
        xml += "</Points><Values/></Chart>";
    }
    xml += "</ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.ok() ? "Chart ActivePoint references and Undefined must parse" :
        parsed.diagnostics().front().code + ":" + parsed.diagnostics().front().message);
    if (!parsed) return;
    const auto active_id = PropertyId::from_name("ActivePoint");
    for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        const auto active = parsed.value().get_prop_val(owner, active_id);
        if (i % 3 == 0) {
            expect(active.status == PropertyReadStatus::computed_value && active.value == PropertyValue{UndefinedValue{}},
                "empty Chart computes Undefined ActivePoint");
        } else if (i % 3 == 1) {
            expect(active.status == PropertyReadStatus::explicit_value && active.value == PropertyValue{ChartPointRef{owner.id, ObjectId{20}}},
                "Chart ActivePoint XML resolves an owner-local point ID");
        } else {
            expect(active.status == PropertyReadStatus::explicit_value && active.value == PropertyValue{UndefinedValue{}},
                "empty ActivePoint element represents explicit Undefined");
        }
    }
    const auto written = source::serialize_form_xml(parsed.value());
    expect(written.ok(), "Chart ActivePoint references and explicit Undefined must serialize");
    if (!written) return;
    expect(written.value().find("<ActivePoint/>") != std::string::npos,
        "explicit Undefined must serialize as an empty ActivePoint element");
    const auto restored = source::parse_form_xml(written.value());
    expect(restored.ok(), "serialized Chart ActivePoint values must reparse");
    if (restored) for (std::uint64_t i = 0; i < 12; ++i) {
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::control, ObjectId{100 + i}};
        expect(restored.value().get_prop_val(owner, active_id).value == parsed.value().get_prop_val(owner, active_id).value,
            "Chart ActivePoint identities and Undefined must survive XML roundtrip");
    }
    const std::string invalid = R"XML(<Form id="1" name="BadPoint" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><ActivePoint>99</ActivePoint><Series/><Points><ChartPoint id="10"><Text>Only</Text><Color kind="automatic"/></ChartPoint></Points><Values/></Chart></ChildItems></Form>)XML";
    const std::string invalid_shape = R"XML(<Form id="1" name="BadPoint" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="C"><Position/><ActivePoint><Unexpected/></ActivePoint><Series/><Points/><Values/></Chart></ChildItems></Form>)XML";
    expect(!source::parse_form_xml(invalid).ok(), "ActivePoint XML must reject a missing point ID");
    expect(!source::parse_form_xml(invalid_shape).ok(), "ActivePoint XML must reject nested content");
}

void test_default_control_focus_groups_xml() {
    constexpr std::array<std::size_t, 12> page_indexes{0, 0, 0, 1, 1, 1, 2, 2, 3, 4, 4, 5};
    constexpr std::array<bool, 12> selected{false, false, true, false, false, true,
        false, true, true, false, false, true};
    std::array<std::string, 6> pages;
    for (std::size_t index = 0; index < page_indexes.size(); ++index)
        pages[page_indexes[index]] += "<InputField id=\"" + std::to_string(100 + index) + "\" name=\"Input" +
            std::to_string(index) + "\"><Position><DefaultControl>" + (selected[index] ? "true" : "false") +
            "</DefaultControl></Position></InputField>";
    for (std::size_t index = 0; index < pages.size(); ++index)
        pages[index] = "<Page name=\"Page" + std::to_string(index) + "\"><ChildItems>" + pages[index] + "</ChildItems></Page>";
    const std::string xml = R"XML(<Form id="1" name="FocusGroups" ordinaryFormVersion="2.1"><ChildItems>)XML" +
        pages[0] + pages[1] + R"XML(<Panel id="30" name="Normal"><Position/><ChildItems>)XML" + pages[2] + pages[3] +
        R"XML(</ChildItems></Panel><Panel id="31" name="Shared"><Position/><SeparateByPage>false</SeparateByPage><ChildItems>)XML" +
        pages[4] + pages[5] + "</ChildItems></Panel></ChildItems></Form>";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "independent root, normal panel, and shared panel focus groups must parse");
    if (!parsed) return;
    const auto property = model::PropertyId::from_name("DefaultControl");
    for (std::size_t index = 0; index < selected.size(); ++index)
        expect(parsed.value().get_prop_val({model::ObjectCategory::control, model::ObjectId{100 + index}}, property).value ==
                   model::PropertyValue{selected[index]},
            "public XML DefaultControl must populate the same named Position state as the model API");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<DefaultControl>false</DefaultControl>") == std::string::npos &&
               serialized.value().find("<DefaultControl>true</DefaultControl>") != std::string::npos,
        "XML writer must omit known false defaults and preserve true focus selections");
    if (!serialized) return;
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && source::serialize_form_xml(reparsed.value()).value() == serialized.value(),
        "focus groups must reach a deterministic public XML round trip");
    for (const auto duplicate : {0u, 6u, 9u}) {
        auto invalid = xml;
        const std::string needle = "name=\"Input" + std::to_string(duplicate) + "\"><Position><DefaultControl>false";
        const auto offset = invalid.find(needle);
        expect(offset != std::string::npos, "duplicate DefaultControl fixture must identify an unselected control");
        invalid.replace(offset, needle.size(), "name=\"Input" + std::to_string(duplicate) + "\"><Position><DefaultControl>true");
        expect(!source::parse_form_xml(invalid).ok(),
            "public XML must reject duplicate defaults within root pages, normal pages, and shared panels");
    }
    const model::OrdinaryFormDocument::ObjectKey shared_owner{model::ObjectCategory::control, model::ObjectId{109}};
    expect(parsed.value().set_prop_val(shared_owner, property, true) == model::PropertyMutationStatus::applied &&
               parsed.value().get_prop_val({model::ObjectCategory::control, model::ObjectId{111}}, property).value == model::PropertyValue{false},
        "model mutation must clear a shared panel default on another page");
    const auto updated = source::serialize_form_xml(parsed.value());
    expect(updated.ok(), "updated focus groups must serialize");
    if (!updated) return;
    const auto updated_model = source::parse_form_xml(updated.value());
    expect(updated_model.ok() && updated_model.value().get_prop_val(shared_owner, property).value == model::PropertyValue{true} &&
               updated_model.value().get_prop_val({model::ObjectCategory::control, model::ObjectId{111}}, property).value == model::PropertyValue{false},
        "updated shared panel focus selections must survive source serialization");
    expect(parsed.value().reset_prop_val(shared_owner, property) == model::PropertyMutationStatus::reset &&
               source::serialize_form_xml(parsed.value()).ok(),
        "clearing a shared focus selection must remain serializable without reassignment");
}

void test_collapse_runtime_source_boundary() {
    constexpr std::string_view xml = R"XML(<Form id="1" name="Collapse" ordinaryFormVersion="2.1"><ChildItems><InputField id="2" name="Input"><Position><Top>17</Top><Visible>false</Visible><Height>45</Height><Left>13</Left><Width>120</Width></Position></InputField></ChildItems></Form>)XML";
    const model::OrdinaryFormDocument::ObjectKey owner{model::ObjectCategory::control, model::ObjectId{2}};
    const auto property = model::PropertyId::from_name("Collapse");
    for (const auto member : {"None", "Left", "Right", "Top", "Bottom"}) {
        const std::string with_collapse = std::string(R"XML(<Form id="1" name="Collapse" ordinaryFormVersion="2.1"><ChildItems><InputField id="2" name="Input"><Position><Collapse type="ControlCollapseMode" member=")XML") + member +
            R"XML("/></Position></InputField></ChildItems></Form>)XML";
        expect(!source::parse_form_xml(with_collapse).ok(),
            "public XML must reject Collapse, including the explicit None runtime state");
        auto parsed = source::parse_form_xml(xml);
        expect(parsed.ok(), "Collapse runtime source boundary fixture must parse");
        if (!parsed) continue;
        const auto baseline = source::serialize_form_xml(parsed.value());
        expect(baseline.ok(), "Collapse fixture must serialize before runtime assignment");
        expect(parsed.value().set_prop_val(owner, property, model::EnumerationValue{"ControlCollapseMode", member}) ==
                   model::PropertyMutationStatus::applied_runtime_only,
            "Collapse must remain writable through the in-memory model");
        expect(!source::serialize_form_xml(parsed.value()).ok(),
            "XML writer must reject explicit runtime Collapse instead of silently omitting it");
        expect(parsed.value().reset_prop_val(owner, property) == model::PropertyMutationStatus::reset_runtime_only,
            "Collapse reset must remove runtime state");
        const auto reset = source::serialize_form_xml(parsed.value());
        expect(reset.ok() && baseline.ok() && reset.value() == baseline.value(),
            "reset Collapse must restore source serialization without changing other Position fields");
    }
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "ExplicitRuntime";
    form.children.push_back(model::ControlRef{model::ObjectId{2}});
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode input{model::ObjectId{2}, "Input", model::InputFieldPayload{}};
    input.position.collapse.set(std::nullopt);
    document.add_control(std::move(input));
    expect(!source::serialize_form_xml(document).ok(),
        "even an explicit empty runtime Collapse must be rejected rather than lost");
}

void test_unavailable_ordinary_geometry_properties_are_not_public_xml() {
    struct Case { std::string_view element; std::string_view property; };
    const std::array cases{
        Case{"Dendrogram", "AutoMaxHeight"}, Case{"Dendrogram", "AutoMaxWidth"},
        Case{"Dendrogram", "MaxHeight"}, Case{"Dendrogram", "MaxWidth"},
        Case{"Dendrogram", "VerticalStretch"}, Case{"Dendrogram", "HorizontalStretch"},
        Case{"GeographicalSchemaField", "AutoMaxHeight"}, Case{"GeographicalSchemaField", "AutoMaxWidth"},
        Case{"GeographicalSchemaField", "MaxHeight"}, Case{"GeographicalSchemaField", "MaxWidth"},
        Case{"GeographicalSchemaField", "VerticalStretch"}, Case{"GeographicalSchemaField", "HorizontalStretch"},
        Case{"GraphicalSchemaField", "VerticalStretch"}, Case{"GraphicalSchemaField", "HorizontalStretch"},
        Case{"GanttChart", "AutoMaxWidth"}, Case{"GanttChart", "MaxWidth"},
    };
    for (const auto& test : cases) {
        const std::string prefix = std::string(R"XML(<Form id="1" name="Unavailable" ordinaryFormVersion="2.1"><ChildItems><)XML") +
            std::string(test.element) + R"XML( id="2" name="Control"><Position/>)XML";
        const std::string suffix = "</" + std::string(test.element) + "></ChildItems></Form>";
        expect(source::parse_form_xml(prefix + suffix).ok(),
            "XML rejection fixture must be valid before inserting an unavailable property");
        const std::string xml = prefix + "<" + std::string(test.property) + ">" +
            (test.property.starts_with("Max") ? "137" : "true") + "</" + std::string(test.property) + ">" + suffix;
        expect(!source::parse_form_xml(xml),
            "explicit platform-unavailable ordinary geometry property must fail at the public XML boundary");
    }

    auto parsed = source::parse_form_xml(R"XML(<Form id="1" name="Ordinary" ordinaryFormVersion="2.1"><ChildItems>
      <Dendrogram id="2" name="D"><Position><Height>45</Height><Width>120</Width></Position></Dendrogram>
      <GeographicalSchemaField id="3" name="Geo"><Position><Height>46</Height><Width>121</Width></Position></GeographicalSchemaField>
      <GraphicalSchemaField id="4" name="Graph"><Position><Height>47</Height><Width>122</Width></Position></GraphicalSchemaField>
      <GanttChart id="5" name="Gantt"><Position><Height>48</Height><Width>123</Width></Position></GanttChart>
    </ChildItems></Form>)XML");
    expect(parsed.ok(), "ordinary-form Position Width and Height must remain valid after unavailable-property filtering");
    if (!parsed) return;
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Width>120</Width>") != std::string::npos &&
               serialized.value().find("<Height>48</Height>") != std::string::npos,
        "ordinary-form Position dimensions must serialize normally");

    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "RawUnavailable";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
    model::ControlNode dendrogram{model::ObjectId{2}, "D", model::DendrogramPayload{}};
    dendrogram.properties().set_explicit(model::PropertyId::from_name("AutoMaxHeight"), true);
    invalid_document.add_control(std::move(dendrogram));
    expect(!source::serialize_form_xml(invalid_document),
        "XML writer must reject a platform-unavailable property inserted through the raw PropertySet");

    auto empty = source::parse_form_xml(R"XML(<Form id="1" name="Default" ordinaryFormVersion="2.1"/>)XML");
    expect(empty.ok() && source::serialize_form_xml(empty.value()).ok(),
        "a default ordinary Form.xml must remain parseable and serializable");
}

void test_typed_defaults_for_existing_codecs() {
    auto parsed = source::parse_form_xml(R"XML(<Form id="1" name="TypedDefaults" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="B"><Position/></Button><CommandBar id="3" name="Bar"><Position/></CommandBar><InputField id="4" name="I"><Position/></InputField><HTMLDocumentField id="5" name="H"><Position/></HTMLDocumentField><Splitter id="6" name="S"><Position/></Splitter><RadioButton id="7" name="R"><Position/></RadioButton></ChildItems></Form>)XML");
    expect(parsed.ok(), "typed-default fixture must parse");
    if (!parsed) return;
    struct Expected {std::uint64_t owner; std::string_view name; model::PropertyValue value;};
    const Expected cases[]{
        {2, "HorizontalAlign", model::EnumerationValue{"HorizontalAlign", "Center"}},
        {2, "VerticalAlign", model::EnumerationValue{"VerticalAlign", "Center"}},
        {2, "PictureLocation", model::EnumerationValue{"PictureLocation", "Left"}},
        {2, "PictureSize", model::EnumerationValue{"PictureSize", "RealSize"}},
        {2, "MenuMode", model::EnumerationValue{"MenuMode", "DontUse"}},
        {2, "Shortcut", model::ShortcutValue{}},
        {3, "Border", model::BorderValue{}},
        {4, "HorizontalAlign", model::EnumerationValue{"HorizontalAlign", "Auto"}},
        {4, "VerticalAlign", model::EnumerationValue{"VerticalAlign", "Top"}},
        {5, "Output", model::EnumerationValue{"Output", "Auto"}},
        {6, "Orientation", model::EnumerationValue{"Orientation", "Auto"}},
        {7, "SelectionValue", model::UndefinedValue{}},
    };
    for (const auto& test : cases) {
        const model::OrdinaryFormDocument::ObjectKey owner{model::ObjectCategory::control, model::ObjectId{test.owner}};
        const auto property = model::PropertyId::from_name(test.name);
        const auto before = parsed.value().get_prop_val(owner, property);
        expect(before.status == model::PropertyReadStatus::proven_default && before.value && *before.value == test.value,
            "declared typed defaults must be materialized by GetPropVal");
        expect(parsed.value().set_prop_val(owner, property, test.value) == model::PropertyMutationStatus::applied,
            "the same typed default must be accepted by SetPropVal");
        expect(parsed.value().reset_prop_val(owner, property) == model::PropertyMutationStatus::reset,
            "reset must reveal the original typed default");
        const auto after = parsed.value().get_prop_val(owner, property);
        expect(after.status == model::PropertyReadStatus::proven_default && after.value && *after.value == test.value,
            "GetPropVal must return the same typed default after reset");
    }
    expect(parsed.value().set_prop_val({model::ObjectCategory::control, model::ObjectId{2}},
               model::PropertyId::from_name("PictureSize"), model::EnumerationValue{"PictureSize", "Unknown"}) ==
               model::PropertyMutationStatus::invalid_value,
        "typed enum defaults must not widen the property domain to unknown members");
}

void test_border_and_calendar_selection_xml_contract() {
    const auto parse = [](std::string_view control, std::string_view properties) {
        return source::parse_form_xml(std::string(R"XML(<Form id="1" name="Borders" ordinaryFormVersion="2.1"><ChildItems><)XML") +
            std::string(control) + R"XML( id="2" name="C"><Position/>)XML" + std::string(properties) +
            "</" + std::string(control) + "></ChildItems></Form>");
    };
    const auto group = parse("UsualGroup", R"XML(<Border kind="absolute" borderType="Rounded" width="1"/>)XML");
    expect(group.ok(), "Group rounded default must be readable from named XML");
    if (group) {
        const auto xml = source::serialize_form_xml(group.value());
        expect(xml.ok() && xml.value().find("<Border ") == std::string::npos,
            "explicit Group rounded default must remain implicit after normalization");
    }
    expect(!parse("CalendarField", R"XML(<Border kind="absolute" borderType="Rounded" width="1"/>)XML"),
        "rounded border must be rejected outside its declared Group owner");
    expect(!parse("UsualGroup", R"XML(<Border kind="absolute" borderType="Rounded" width="2"/>)XML"),
        "rounded border width must stay at the platform-supported value");
    for (const auto member : {"Single", "Multiple", "Interval"}) {
        const auto value = parse("CalendarField", std::string(R"XML(<SelectionMode type="DateSelectionMode" member=")XML") +
            member + R"XML("/>)XML");
        expect(value.ok(), "Calendar selection-mode members must parse as typed values");
    }
    expect(!parse("CalendarField", R"XML(<SelectionMode type="SelectionMode" member="Single"/>)XML") &&
               !parse("CalendarField", R"XML(<SelectionMode type="DateSelectionMode" member="Unknown"/>)XML"),
        "Calendar selection mode must fail at the semantic XML boundary for unknown domains");
}

void test_named_style_color_defaults() {
    const auto parsed = oof::source::parse_form_xml(
        R"XML(<Form id="1" name="ColorDefaults" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="OwnButton"><Position/></Button><CalendarField id="3" name="OwnCalendar"><Position/></CalendarField></ChildItems></Form>)XML");
    expect(parsed.ok(), "style-color default fixture must parse");
    if (!parsed) return;
    auto document = parsed.value();
    const oof::model::OrdinaryFormDocument::ObjectKey owner{oof::model::ObjectCategory::control, oof::model::ObjectId{2}};
    for (const auto name : {"ButtonTextColor", "ButtonBackColor"}) {
        const auto property = oof::model::PropertyId::from_name(name);
        const auto value = document.get_prop_val(owner, property);
        expect(value.status == oof::model::PropertyReadStatus::proven_default && value.value,
            "named style-color defaults must be materialized, not reported as unknown");
        if (!value.value) continue;
        const auto* color = std::get_if<oof::model::ColorValue>(&*value.value);
        expect(color && color->kind == oof::model::ColorKind::style_reference &&
                std::get<oof::model::QualifiedName>(color->style).value == std::string("StyleColors.") + name,
            "style-color default must retain its qualified public name");
        expect(document.set_prop_val(owner, property, *value.value) == oof::model::PropertyMutationStatus::applied,
            "style-color default must remain editable through the typed property API");
    }
    const oof::model::OrdinaryFormDocument::ObjectKey calendar{oof::model::ObjectCategory::control, oof::model::ObjectId{3}};
    for (const auto& [property_name, style_name] : std::array{
             std::pair{"HolidayColor", "StyleColors.SpecialTextColor"},
             std::pair{"SelectionBackColor", "StyleColors.FieldSelectionBackColor"},
             std::pair{"SelectionTextColor", "StyleColors.FieldSelectedTextColor"}}) {
        const auto property = oof::model::PropertyId::from_name(property_name);
        const auto value = document.get_prop_val(calendar, property);
        expect(value.status == oof::model::PropertyReadStatus::proven_default && value.value,
            "Calendar style defaults must be returned as typed named colors");
        if (!value.value) continue;
        const auto& color = std::get<oof::model::ColorValue>(*value.value);
        expect(color.kind == oof::model::ColorKind::style_reference &&
                std::get<oof::model::QualifiedName>(color.style).value == style_name,
            "Calendar style default must use the independently verified StyleColors property");
        expect(document.set_prop_val(calendar, property, *value.value) == oof::model::PropertyMutationStatus::applied,
            "Calendar style default must be accepted by SetPropVal");
    }
    const auto xml = oof::source::serialize_form_xml(document);
    expect(xml.ok() && xml.value().find("<ButtonTextColor") == std::string::npos &&
            xml.value().find("<ButtonBackColor") == std::string::npos,
        "explicit values equal to proven style-color defaults must be omitted from XML");
    expect(xml.ok() && xml.value().find("<HolidayColor") == std::string::npos &&
            xml.value().find("<SelectionBackColor") == std::string::npos &&
            xml.value().find("<SelectionTextColor") == std::string::npos,
        "Calendar named style defaults must also remain implicit in XML");
    oof::model::ColorValue alternate;
    alternate.kind = oof::model::ColorKind::style_reference;
    alternate.style = oof::model::QualifiedName{"StyleColors.FieldSelectedTextColor"};
    expect(document.set_prop_val(calendar, oof::model::PropertyId::from_name("HolidayColor"), alternate) ==
               oof::model::PropertyMutationStatus::applied,
        "Calendar colors must accept a non-default named style");
    const auto changed_xml = oof::source::serialize_form_xml(document);
    expect(changed_xml.ok() && changed_xml.value().find("<HolidayColor") != std::string::npos,
        "non-default Calendar style must remain visible in source XML");
    if (changed_xml) {
        const auto restored = oof::source::parse_form_xml(changed_xml.value());
        expect(restored.ok(), "non-default Calendar style XML must parse");
        if (restored) {
            const auto value = restored.value().get_prop_val(calendar, oof::model::PropertyId::from_name("HolidayColor"));
            expect(value.value && std::get<oof::model::ColorValue>(*value.value) == alternate,
                "non-default Calendar style must survive public XML");
        }
    }
}

void test_chart_xml_document_resource_budget() {
    const auto extra = source::parse_form_xml(R"XML(<Form id="1" name="Extra" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Chart"><Position/><Series/><Points/><Values><ChartValue seriesRef="2" pointRef="1"><Number>1</Number></ChartValue></Values></Chart></ChildItems></Form>)XML");
    expect(!extra && !extra.diagnostics().empty() &&
        extra.diagnostics().front().message == "Chart Values exceed the declared Series by Point matrix",
        "XML rejects excess Values before allocating cells outside its declared matrix");
    std::string xml = R"XML(<Form id="1" name="Budget" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Chart"><Position/><Series>)XML";
    for (std::size_t i = 0; i < 999; ++i) {
        xml += "<ChartSeries id=\"" + std::to_string(i + 2) + "\"><Text/><Color kind=\"automatic\"/><Marker type=\"ChartMarkerType\" member=\"Auto\"/>";
        if (i == 0) xml += "<Trendlines><Trendline/><Trendline/></Trendlines>";
        xml += "</ChartSeries>";
    }
    xml += "</Series><Points>";
    for (std::size_t i = 0; i < 999; ++i)
        xml += "<ChartPoint id=\"" + std::to_string(i + 1) + "\"><Text/><Color kind=\"automatic\"/></ChartPoint>";
    xml += "</Points><Values/></Chart></ChildItems></Form>";
    const auto parsed = source::parse_form_xml(xml);
    expect(!parsed && !parsed.diagnostics().empty() &&
        parsed.diagnostics().front().message == "Chart collections exceed the document resource budget",
        "XML dimensions and Trendlines must share one budget before dense matrix allocation");
}


void test_self_contained_field_descriptors_xml() {
    const std::string xml = R"XML(<Form id="1" name="NamedFields" ordinaryFormVersion="2.1"><MainAttribute attributeId="0"/><CatalogItemFormExtension/><Attributes><Attribute id="0" name="Object"><TypeDomain><Entry term="object" typeUuid="11111111-1111-1111-1111-111111111111" typeName="CatalogObject.Root"/></TypeDomain></Attribute></Attributes><ChildItems><InputField id="2" name="Text"><DataPath attributeId="0"><Member name="CustomText" uuid="22222222-2222-2222-2222-222222222222"><TypeDomain><Entry term="string" length="64"/></TypeDomain></Member></DataPath><Position/></InputField><LabelDecoration id="3" name="Untyped"><DataPath attributeId="0"><Member uuid="33333333-3333-3333-3333-333333333333"/></DataPath><Position/></LabelDecoration></ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto& member = parsed.value().find_control(model::ObjectId{2})->data_path->members.front();
    expect(member.name == "CustomText" && member.uuid == model::UuidValue{"22222222-2222-2222-2222-222222222222"} &&
        member.type && member.type->entries.size() == 1 && member.type->entries.front().term == model::TypeDomainTerm::string &&
        member.type->entries.front().string.length == 64,
        "a named field must retain its semantic identity and exact type qualifiers");
    const auto& untyped = parsed.value().find_control(model::ObjectId{3})->data_path->members.front();
    expect(untyped.name.empty() && untyped.uuid == model::UuidValue{"33333333-3333-3333-3333-333333333333"} && !untyped.type,
        "a field reference can retain only its UUID when its source has no name or type");
    expect(parsed.value().find_attribute(model::ObjectId{0})->type.entries.front().type_name ==
        model::QualifiedName{"CatalogObject.Root"}, "configured type names must be explicit XML annotations");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), serialized ? "" : serialized.diagnostics().front().message);
    const auto again = source::parse_form_xml(serialized.value());
    expect(again.ok() && again.value().find_control(model::ObjectId{2})->data_path ==
        parsed.value().find_control(model::ObjectId{2})->data_path &&
        again.value().find_control(model::ObjectId{3})->data_path == parsed.value().find_control(model::ObjectId{3})->data_path,
        "field UUIDs, supplied labels, and type descriptors must survive public XML round-trip");
    const std::array<std::pair<std::string_view, std::string_view>, 8> invalid{{
        {"uuid=\"22222222-2222-2222-2222-222222222222\"", "uuid=\"bad\""},
        {"uuid=\"22222222-2222-2222-2222-222222222222\"", "uuid=\"00000000-0000-0000-0000-000000000000\""},
        {"name=\"CustomText\"", "name=\"\""},
        {"<Member uuid=\"33333333-3333-3333-3333-333333333333\"/>", "<Member/>"},
        {"term=\"string\" length=\"64\"", "term=\"string\" length=\"64\" typeName=\"CatalogRef.Root\""},
        {" typeUuid=\"11111111-1111-1111-1111-111111111111\"", ""},
        {"<TypeDomain><Entry term=\"string\" length=\"64\"/></TypeDomain>", "<TypeDomain><Entry term=\"string\"/></TypeDomain><TypeDomain><Entry term=\"string\"/></TypeDomain>"},
        {"<Member uuid=\"33333333-3333-3333-3333-333333333333\"/>", "<Member name=\"One\">Other</Member>"},
    }};
    for (const auto& [from, to] : invalid) {
        auto bad = xml;
        const auto offset = bad.find(from);
        expect(offset != std::string::npos, "negative fixture must change its intended descriptor");
        bad.replace(offset, from.size(), to);
        expect(!source::parse_form_xml(bad), "malformed field identities or configured type annotations must be rejected");
    }
    auto legacy = xml;
    const auto offset = legacy.find("<Member uuid=\"33333333-3333-3333-3333-333333333333\"/>");
    legacy.replace(offset, std::string("<Member uuid=\"33333333-3333-3333-3333-333333333333\"/>").size(),
        "<Member>LegacyField</Member>");
    const auto legacy_parsed = source::parse_form_xml(legacy);
    expect(legacy_parsed.ok() && legacy_parsed.value().find_control(model::ObjectId{3})->data_path->members.front().name ==
        "LegacyField", "legacy named member text must remain available for source inspection");
}


void test_configuration_type_schema_xml_enrichment() {
    constexpr std::string_view annotated_xsd = R"XSD(<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" targetNamespace="http://v8.1c.ru/8.1/data/enterprise/current-config">
      <xs:complexType name="CatalogObject.Root"><xs:annotation><xs:appinfo><TypeIdentity xmlns="urn:oof:configuration-types:1" uuid="11111111-1111-1111-1111-111111111111"/></xs:appinfo></xs:annotation></xs:complexType>
      <xs:simpleType name="CatalogRef.Item"><xs:annotation><xs:appinfo><TypeIdentity xmlns="urn:oof:configuration-types:1" uuid="22222222-2222-2222-2222-222222222222"/></xs:appinfo></xs:annotation><xs:restriction base="xs:string"/></xs:simpleType>
      <xs:complexType name="CatalogList.Root"><xs:annotation><xs:appinfo><TypeIdentity xmlns="urn:oof:configuration-types:1" uuid="33333333-3333-3333-3333-333333333333"/></xs:appinfo></xs:annotation></xs:complexType>
      <xs:complexType name="Type.Custom"><xs:annotation><xs:appinfo><TypeIdentity xmlns="urn:oof:configuration-types:1" uuid="44444444-4444-4444-4444-444444444444"/></xs:appinfo></xs:annotation></xs:complexType>
    </xs:schema>)XSD";
    const auto types = source::parse_configuration_type_schema(annotated_xsd);
    expect(types.ok(), types ? "" : types.diagnostics().front().message);
    std::string xml = R"XML(<Form id="1" name="SchemaNames" ordinaryFormVersion="2.1"><Attributes>
      <Attribute id="0" name="Root"><TypeDomain><Entry term="object" typeUuid="11111111-1111-1111-1111-111111111111"/></TypeDomain></Attribute>
      <Attribute id="2" name="Reference"><TypeDomain><Entry term="object" typeUuid="22222222-2222-2222-2222-222222222222"/></TypeDomain></Attribute>
      <Attribute id="3" name="List"><TypeDomain><Entry term="object" typeUuid="33333333-3333-3333-3333-333333333333"/></TypeDomain></Attribute>
      <Attribute id="4" name="Type"><TypeDomain><Entry term="object" typeUuid="44444444-4444-4444-4444-444444444444"/></TypeDomain></Attribute>
      <Attribute id="5" name="Primitive"><TypeDomain><Entry term="string" length="40"/></TypeDomain></Attribute>
      <Attribute id="6" name="Unknown"><TypeDomain><Entry term="object" typeUuid="55555555-5555-5555-5555-555555555555"/></TypeDomain></Attribute>
      </Attributes><ChildItems>)XML";
    for (std::size_t index = 0; index < 12; ++index) {
        xml += "<LabelDecoration id=\"" + std::to_string(20 + index) + "\" name=\"Label" + std::to_string(index) +
            "\"><DataPath attributeId=\"0\"><Member uuid=\"22222222-2222-2222-2222-222222222222\"";
        if (index % 2 == 0)
            xml += "><TypeDomain><Entry term=\"object\" typeUuid=\"11111111-1111-1111-1111-111111111111\"/></TypeDomain></Member>";
        else xml += "/>";
        xml += "</DataPath><Position/></LabelDecoration>";
    }
    xml += "</ChildItems></Form>";
    const auto plain = source::parse_form_xml(xml);
    expect(plain.ok(), plain ? "" : plain.diagnostics().front().message);
    expect(plain.value().collections().controls.size() == 12 && !plain.value().metadata_context(),
        "the annotation test must cover twelve controls without MetadataContext");
    const auto unchanged = source::serialize_form_xml(plain.value());
    expect(unchanged.ok(), unchanged ? "" : unchanged.diagnostics().front().message);
    const auto enriched = source::serialize_form_xml(plain.value(), &types.value());
    expect(enriched.ok(), enriched ? "" : enriched.diagnostics().front().message);
    expect(enriched.diagnostics().empty(), "unambiguous schema type identities must not produce warnings");
    const auto enriched_model = source::parse_form_xml(enriched.value());
    expect(enriched_model.ok(), enriched_model ? "" : enriched_model.diagnostics().front().message);
    const auto from_input = source::parse_form_xml(xml, {}, &types.value());
    expect(from_input.ok(), from_input ? "" : from_input.diagnostics().front().message);
    const std::array<std::pair<std::uint64_t, std::string_view>, 4> configured{{
        {0, "CatalogObject.Root"}, {2, "CatalogRef.Item"}, {3, "CatalogList.Root"}, {4, "Type.Custom"},
    }};
    for (const auto& [id, name] : configured) {
        expect(enriched_model.value().find_attribute(model::ObjectId{id})->type.entries.front().type_name ==
            model::QualifiedName{std::string(name)} &&
            from_input.value().find_attribute(model::ObjectId{id})->type.entries.front().type_name ==
            model::QualifiedName{std::string(name)},
            "parse and serialize must enrich only the configured type matching its GUID");
    }
    expect(!from_input.value().find_attribute(model::ObjectId{5})->type.entries.front().type_name &&
        !from_input.value().find_attribute(model::ObjectId{6})->type.entries.front().type_name,
        "primitive types and unknown configured GUIDs must not acquire invented names");
    for (std::size_t index = 0; index < 12; ++index) {
        const auto& member = from_input.value().find_control(model::ObjectId{20 + index})->data_path->members.front();
        expect(member.name.empty() && member.uuid == model::UuidValue{"22222222-2222-2222-2222-222222222222"},
            "a known type GUID used as Member.uuid must not become a field name");
        if (index % 2 == 0)
            expect(member.type && member.type->entries.front().type_name == model::QualifiedName{"CatalogObject.Root"},
                "an explicitly present member value type may receive its own configured type name");
        else expect(!member.type, "a UUID-only member must not acquire an invented value type");
    }
    const auto default_again = source::serialize_form_xml(plain.value());
    expect(default_again.ok() && default_again.value() == unchanged.value() && default_again.diagnostics().empty(),
        "optional schema enrichment must not mutate the input model or default serialization");
    const std::string conflict_name = "CatalogObject.UserSupplied";
    auto conflicting_xml = xml;
    const std::string root_identity = "typeUuid=\"11111111-1111-1111-1111-111111111111\"";
    std::size_t cursor = 0;
    while ((cursor = conflicting_xml.find(root_identity, cursor)) != std::string::npos) {
        cursor += root_identity.size();
        const std::string annotation = " typeName=\"" + conflict_name + "\"";
        conflicting_xml.insert(cursor, annotation);
        cursor += annotation.size();
    }
    const auto conflict = source::parse_form_xml(conflicting_xml, {}, &types.value());
    expect(conflict.ok(), conflict ? "" : conflict.diagnostics().front().message);
    expect(std::count_if(conflict.diagnostics().begin(), conflict.diagnostics().end(), [](const auto& diagnostic) {
        return diagnostic.code == "OOF1142";
    }) == 1, "repeated conflicting annotations for the same type and explicit name must warn once");
    expect(conflict.value().find_attribute(model::ObjectId{0})->type.entries.front().type_name ==
        model::QualifiedName{conflict_name}, "an explicit configured type name must remain authoritative source text");
    const auto conflict_output = source::serialize_form_xml(conflict.value(), &types.value());
    expect(conflict_output.ok() && std::count_if(conflict_output.diagnostics().begin(), conflict_output.diagnostics().end(),
        [](const auto& diagnostic) { return diagnostic.code == "OOF1142"; }) == 1,
        "serialize must also deduplicate its explicit-name conflict warning");
    auto escaped_xml = conflicting_xml;
    const auto explicit_position = escaped_xml.find(conflict_name);
    escaped_xml.replace(explicit_position, conflict_name.size(), "Safe&amp;&quot;&lt;Injected/&gt;");
    const auto escaped = source::parse_form_xml(escaped_xml, {}, &types.value());
    expect(escaped.ok(), escaped ? "" : escaped.diagnostics().front().message);
    const auto escaped_output = source::serialize_form_xml(escaped.value(), &types.value());
    expect(escaped_output.ok() && escaped_output.value().find("<Injected/>") == std::string::npos,
        "explicit type names must be escaped as attribute data rather than executable XML");
    const auto escaped_again = source::parse_form_xml(escaped_output.value());
    expect(escaped_again.ok() && escaped_again.value().find_attribute(model::ObjectId{0})->type.entries.front().type_name ==
        model::QualifiedName{"Safe&\"<Injected/>"}, "escaping must preserve the supplied type name value");
    constexpr std::string_view declarations_xsd = R"XSD(<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" targetNamespace="http://v8.1c.ru/8.1/data/enterprise/current-config"><xs:complexType name="CatalogObject.Root"/><xs:simpleType name="CatalogRef.Item"><xs:restriction base="xs:string"/></xs:simpleType></xs:schema>)XSD";
    const auto declarations = source::parse_configuration_type_schema(declarations_xsd);
    expect(declarations.ok(), declarations ? "" : declarations.diagnostics().front().message);
    const auto unnamed = source::parse_form_xml(xml, {}, &declarations.value());
    expect(unnamed.ok(), unnamed ? "" : unnamed.diagnostics().front().message);
    expect(std::count_if(unnamed.diagnostics().begin(), unnamed.diagnostics().end(), [](const auto& diagnostic) {
        return diagnostic.code == "OOF1143";
    }) == 1 && !unnamed.value().find_attribute(model::ObjectId{0})->type.entries.front().type_name,
        "a declarations-only schema must warn once without guessing UUID associations");
    const auto unnamed_output = source::serialize_form_xml(plain.value(), &declarations.value());
    expect(unnamed_output.ok() && unnamed_output.value() == unchanged.value() &&
        std::count_if(unnamed_output.diagnostics().begin(), unnamed_output.diagnostics().end(), [](const auto& diagnostic) {
            return diagnostic.code == "OOF1143";
        }) == 1, "a declarations-only schema must leave serialized XML unchanged and warn once");
    auto invalid_name_xsd = std::string(declarations_xsd);
    const auto declaration_name = invalid_name_xsd.find("CatalogObject.Root");
    invalid_name_xsd.replace(declaration_name, std::string("CatalogObject.Root").size(), "Name&quot;&lt;Injected/&gt;");
    expect(!source::parse_configuration_type_schema(invalid_name_xsd),
        "type declaration names must be valid NCNames and cannot inject source markup");
}

}  // namespace

int main() {
    try {
        test_configuration_type_schema_xml_enrichment();
        test_self_contained_field_descriptors_xml();
        test_value_list_xml_contract();
        test_map_canonical_xml_order();
        test_recursive_typed_value_xml_contract();
        test_form_item_collection_xml_contract();
        test_named_style_color_defaults();
        test_border_and_calendar_selection_xml_contract();
        test_typed_defaults_for_existing_codecs();
        test_runtime_properties_are_not_public_xml();
        test_collapse_runtime_source_boundary();
        test_default_control_focus_groups_xml();
        test_chart_source_mode_xml();
        test_control_value_is_not_persisted_xml();
        test_table_current_parent_is_not_persisted_xml();
        test_graphical_current_item_is_not_persisted_xml();
        test_geographical_showed_area_is_not_persisted_xml();
        test_chart_active_point_xml_reference();
        test_unavailable_ordinary_geometry_properties_are_not_public_xml();
        test_reconstruction_completeness_xml_metadata();
        test_data_processor_form_extension_xml_contract();
        test_saved_values_form_extension_xml_contract();
        test_catalog_item_form_extension_xml_contract();
        test_document_form_extension_xml_contract();
        test_catalog_list_form_extension_xml_contract();
        test_input_field_reference_extension_xml_contract();
        test_chart_base_value_xml();
        test_chart_enum_properties_xml();
        test_chart_type_xml();
        test_chart_summary_series_color_xml();
        test_complete_document_roundtrip();
        test_chart_xml_document_resource_budget();
        test_xml_explicit_z_order_normalization();
        test_usual_group_named_xml_round_trip();
        test_root_page_tree_xml_roundtrip();
        test_page_internal_ids_do_not_change_xml();
        test_page_boolean_defaults_and_rejections();
        test_page_position_roundtrip_and_rejections();
        test_all_control_variants();
        test_calendar_field_enabled_xml_roundtrip();
        test_html_document_field_output_xml_roundtrip();
        test_binding_target_and_manual_roundtrip();
        test_typed_values_and_canonicalization();
        test_input_field_tooltip_and_format_xml_round_trip();
        test_input_field_type_link_xml_contract();
        test_attribute_id_zero_xml_contract();
        test_spreadsheet_serialization_version_xml();
        test_spreadsheet_document_cells_xml_round_trip();
        test_spreadsheet_document_formats_xml_round_trip();
        test_spreadsheet_document_localized_text_xml_round_trip();
        test_spreadsheet_document_picture_paths();
        test_spreadsheet_language_settings_xml_round_trip();
        test_check_box_tooltip_xml_round_trip();
        test_choice_field_static_xml_profile_and_runtime_list_rejection();
        test_control_font_xml_round_trip();
        test_input_field_layout_xml_round_trip();
        test_button_shortcut_xml();
        test_form_style_xml_roundtrip_and_rejections();
        test_boolean_type_domain_xml_roundtrip();
        test_value_list_type_domain_xml_roundtrip();
        test_value_table_type_domain_xml_roundtrip();
        test_value_tree_type_domain_xml_roundtrip();
        test_table_enum_properties_xml();
        test_table_choice_mode_runtime_only();
        test_table_row_selection_mode_xml();
        test_value_tree_table_xml_contract();
        test_table_column_visible_and_empty_tree_xml();
        test_table_fixed_column_xml();
        test_table_columns_named_profile();
        test_neutral_localized_text_xml();
        test_catalog_list_table_source_matrix();
        test_catalog_source_bindings_matrix();
        test_document_list_table_source_xml_and_native();
        test_table_columns_without_editors_xml();
        test_table_column_layout_xml();
        test_inherited_property_has_one_surface();
        test_xml_character_normalization_is_lossless();
        test_strict_rejections();
        test_event_action_metadata_xml();
        test_event_owner_invariant();
        test_unsupported_control_events_rejected();
        test_button_foreign_enum_default_is_retained();
        test_standard_picture_xml_reference_roundtrip();
        test_text_document_selected_text_runtime_only();
        test_choice_and_input_field_picture_references_xml();
        test_button_menu_model_roundtrip_and_rejections();
        test_client_interface_variant_xml_contract();
        test_gantt_named_collections_roundtrip_and_rejections();
        test_owned_chart_xml_contract();
        test_label_horizontal_align_xml_roundtrip();
        test_label_enabled_tooltip_xml_roundtrip();
        test_progress_bar_xml_only_contract();
        test_command_bar_buttons_xml_only_contract();
        test_automatic_buttons_xml_contract();
        test_standard_menu_action_xml_contract();
        test_standard_save_restore_action_xml_contract();
        test_command_bar_action_source_xml_contract();
        test_context_menu_named_xml_contract();
        test_form_context_menu_named_xml_contract();
        test_command_bar_default_button_xml_contract();
        test_command_bar_border_xml_contract();
        test_main_panel_typed_xml_contract();
        test_picture_decoration_enabled_tooltip_xml_roundtrip();
        test_picture_decoration_standard_picture_xml_roundtrip();
    } catch (const std::exception& error) {
        std::cerr << "form XML tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form XML tests: PASS\n";
    return 0;
}
