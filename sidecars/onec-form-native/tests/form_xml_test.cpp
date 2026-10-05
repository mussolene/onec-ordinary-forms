#include <array>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "oof/model/metamodel.hpp"
#include "oof/source/form_xml.hpp"

namespace {

namespace model = oof::model;
namespace source = oof::source;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
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
                xml += "<Title>Title</Title><Series><ChartSeries id=\"2\"><Text>Series</Text><Color kind=\"absolute\" red=\"1\"/><Marker type=\"ChartMarkerType\" member=\"Auto\"/></ChartSeries></Series>"
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
      <TypeRestriction>
        <Entry term="numeric" length="15" precision="3" nonNegative="true"/>
      </TypeRestriction>
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
    const auto* date = calendar->properties().find(model::PropertyId::from_name("EndOfDisplayPeriod"));
    expect(date != nullptr && std::holds_alternative<model::UndefinedValue>(date->value), "Date|Undefined union must retain Undefined");

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
        return model::SpreadsheetDocumentCell{row, column, {},
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
        restored->cells[0].column == 3 && restored->cells[0].text == " Ω <текст> & \"цитата\" " &&
        restored->cells[1].row == 2 && restored->cells[1].column == 1 && restored->cells[1].text.empty(),
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

    constexpr std::string_view runtime_list_xml = R"XML(<Form id="1" name="ChoiceField" ordinaryFormVersion="2.1">
      <Attributes><Attribute id="3" name="Choice"><TypeDomain><Entry term="string" length="64" variable="false"/></TypeDomain></Attribute></Attributes>
      <ChildItems><ChoiceField id="2" name="ChoiceField"><DataPath attributeId="3"/><Position/><ChoiceList/></ChoiceField></ChildItems>
    </Form>)XML";
    const auto runtime_list = source::parse_form_xml(runtime_list_xml);
    expect_code(runtime_list, "OOF2002",
        "runtime-only ChoiceList must be rejected from the persisted XML object model");
}

void test_check_box_font_xml_round_trip() {
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

void test_table_columns_named_profile() {
    constexpr std::string_view valid = R"XML(
<Form id="1" name="RowsForm" ordinaryFormVersion="2.1">
  <Attributes><Attribute id="2" name="Rows"><TypeDomain><Entry term="valueTable"/></TypeDomain></Attribute></Attributes>
  <ChildItems><Table id="3" name="Rows"><DataPath attributeId="2"/><Position/><Columns>
    <Column name="Code"><DataPath>Code</DataPath><Header><Item language="ru">Код</Item></Header>
      <Control type="InputField"><Enabled>true</Enabled><ReadOnly>false</ReadOnly></Control>
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
    expect(columns[0].data_path == columns[1].data_path && columns[0].data_path == columns[2].data_path &&
               columns[2].control.kind == model::ControlKind::choice_field &&
               columns[3].control.kind == model::ControlKind::check_box &&
               columns[0].control.properties.empty() && columns[2].control.properties.empty() &&
               columns[3].control.properties.empty(),
        "duplicate DataPath is allowed and typed editor defaults normalize away");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<Columns>") != std::string::npos &&
               serialized.value().find("type=\"ChoiceField\"") != std::string::npos &&
               serialized.value().find("type=\"CheckBox\"") != std::string::npos,
        "named Table Columns must serialize as editable XML");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && reparsed.value().collections().controls.size() == 1 &&
               std::get<model::TablePayload>(reparsed.value().find_control(model::ObjectId{3})->payload)
                       .columns[2].control.kind == model::ControlKind::choice_field &&
               std::get<model::TablePayload>(reparsed.value().find_control(model::ObjectId{3})->payload)
                       .columns[3].control.kind == model::ControlKind::check_box,
        "Table Column XML must survive a source roundtrip");

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

    std::string nondefault_editor(valid);
    const auto enabled_pos = nondefault_editor.find("<Enabled>true</Enabled>");
    nondefault_editor.replace(enabled_pos, std::string("<Enabled>true</Enabled>").size(), "<Enabled>false</Enabled>");
    expect(!source::parse_form_xml(nondefault_editor).ok(), "unverified persisted editor Enabled=false must fail closed");

    std::string wrong_source(valid);
    const auto type_pos = wrong_source.find("term=\"valueTable\"");
    wrong_source.replace(type_pos, std::string("term=\"valueTable\"").size(), "term=\"string\"");
    expect(!source::parse_form_xml(wrong_source).ok(), "Table source must reject a non-ValueTable type");

    std::string dangling_source(valid);
    const auto attribute_pos = dangling_source.find("attributeId=\"2\"");
    dangling_source.replace(attribute_pos, std::string("attributeId=\"2\"").size(), "attributeId=\"99\"");
    expect(!source::parse_form_xml(dangling_source).ok(), "Table source must reject a dangling attribute link");
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
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"/></Buttons></Button></ChildItems></Form>)XML").ok(), "Action requires handler");
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
    const auto parsed = source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="zero" type="Action"><ClientInterfaceVariant>Version8_0</ClientInterfaceVariant><Action handler="Run" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton><CommandBarButton name="default" type="Action"><Action handler="Default" name=""><Text/><ToolTip/><Description/></Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML");
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto& buttons = std::get<model::ButtonPayload>(parsed.value().find_control(model::ObjectId{2})->payload).buttons;
    expect(buttons[0].client_interface_variant == model::ClientInterfaceVariant::version8_0 &&
               buttons[1].client_interface_variant == model::ClientInterfaceVariant::version8_2_ordinary_app,
        "explicit Version8_0 and omitted Version8_2_OrdinaryApp must map to named variants");
    const auto xml = source::serialize_form_xml(parsed.value());
    expect(xml.ok() && xml.value().find("<ClientInterfaceVariant>Version8_0</ClientInterfaceVariant>") != std::string::npos &&
               xml.value().find("<ClientInterfaceVariant>Version8_2_OrdinaryApp</ClientInterfaceVariant>") == std::string::npos,
        "XML must write Version8_0 and omit the Version8_2_OrdinaryApp default");
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
    auto invalid_form = parsed.value().form();
    invalid_form.panel.properties.set_explicit(model::PropertyId::from_name("Enabled"), false);
    model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
    expect(!invalid_document.validate().ok() && !source::serialize_form_xml(invalid_document),
        "unimplemented main Panel properties must not be silently lost");
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
    expect(!source::parse_form_xml(replace_once(default_control, "context=\"Default\"", "context=\"Form\"")),
        "unproven Form context must reject");
    for (const auto source_name : {"Form", "AllSources"})
        expect(!source::parse_form_xml(replace_once(default_control, "source=\"Control\"",
            std::string("source=\"") + source_name + "\"")), "non-Control source rejects sourceControlId");
    expect(!source::parse_form_xml(replace_once(default_control, " context=\"Default\"", "")),
        "StandardAction requires an explicit named context");

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

}  // namespace

int main() {
    try {
        test_reconstruction_completeness_xml_metadata();
        test_data_processor_form_extension_xml_contract();
        test_chart_summary_series_color_xml();
        test_complete_document_roundtrip();
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
        test_spreadsheet_document_cells_xml_round_trip();
        test_check_box_tooltip_xml_round_trip();
        test_choice_field_static_xml_profile_and_runtime_list_rejection();
        test_check_box_font_xml_round_trip();
        test_input_field_layout_xml_round_trip();
        test_button_shortcut_xml();
        test_boolean_type_domain_xml_roundtrip();
        test_value_list_type_domain_xml_roundtrip();
        test_value_table_type_domain_xml_roundtrip();
        test_table_columns_named_profile();
        test_inherited_property_has_one_surface();
        test_xml_character_normalization_is_lossless();
        test_strict_rejections();
        test_event_owner_invariant();
        test_button_foreign_enum_default_is_retained();
        test_standard_picture_xml_reference_roundtrip();
        test_button_menu_model_roundtrip_and_rejections();
        test_client_interface_variant_xml_contract();
        test_gantt_named_collections_roundtrip_and_rejections();
        test_label_horizontal_align_xml_roundtrip();
        test_label_enabled_tooltip_xml_roundtrip();
        test_progress_bar_xml_only_contract();
        test_command_bar_buttons_xml_only_contract();
        test_standard_menu_action_xml_contract();
        test_command_bar_action_source_xml_contract();
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
