#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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
    std::string xml = "<Form id=\"1\" name=\"All\" ordinaryFormVersion=\"2.1\"><ChildItems>";
    std::uint64_t id = 2;
    for (const auto& descriptor : model::metamodel::control_descriptors()) {
        xml += "<" + std::string(descriptor.public_name) + " id=\"" +
               std::to_string(id) + "\" name=\"C" + std::to_string(id) +
               "\"><Position/></" + std::string(descriptor.public_name) + ">";
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
      <VerticalAlign type="VerticalAlign" member="Top"/>
      <ChoiceListHeight>0012.3400</ChoiceListHeight>
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
    expect(height != nullptr, "decimal property must materialize");
    expect(std::get<model::DecimalValue>(height->value).canonical == "12.34", "decimal lexical form must canonicalize");
    const model::ControlNode* calendar = parsed.value().find_control(model::ObjectId{5});
    const auto* date = calendar->properties().find(model::PropertyId::from_name("EndOfDisplayPeriod"));
    expect(date != nullptr && std::holds_alternative<model::UndefinedValue>(date->value), "Date|Undefined union must retain Undefined");

    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "typed values must serialize");
    expect(serialized.value().find(">12.34</ChoiceListHeight>") != std::string::npos, "decimal output must be canonical");
    expect(serialized.value().find("01234567-89ab-cdef-0123-456789abcdef") != std::string::npos, "UUID output must be lowercase canonical");
    expect(serialized.value().find("styleName=\"StyleFonts.TextFont\"") != std::string::npos &&
               serialized.value().find("mask=") == std::string::npos,
        "named Font style must serialize without the removed mask attribute");
    expect(source::parse_form_xml(serialized.value()).ok(), "canonical typed values must validate and reparse");
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
</ChildItems></Form>
)XML";
    auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), "LabelDecoration HorizontalAlign values must parse as typed enumerations");
    auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok(), "LabelDecoration HorizontalAlign values must serialize");
    expect(serialized.value().find("type=\"HorizontalAlign\" member=\"Auto\"") != std::string::npos &&
               serialized.value().find("type=\"HorizontalAlign\" member=\"Left\"") != std::string::npos,
        "LabelDecoration HorizontalAlign type and members must survive XML round-trip");
    auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok() && reparsed.value().collections().controls.size() == 2,
        "canonical LabelDecoration alignment XML must reparse");
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
    constexpr std::string_view xml = R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="run" type="Action"><Text>Run</Text><Shortcut Alt="false" Ctrl="true" Shift="false"><Key>R</Key></Shortcut><Action>RunHandler</Action></CommandBarButton><CommandBarButton name="more" type="Submenu"><Text>More</Text><Order>Ascending</Order><Buttons><CommandBarButton name="sep" type="Separator"/></Buttons></CommandBarButton></Buttons></Button></ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(xml);
    expect(parsed.ok(), parsed.diagnostics().empty() ? "typed Button.Buttons tree must parse" : parsed.diagnostics().front().message);
    const auto* button = parsed.value().find_control(model::ObjectId{2});
    const auto* payload = button ? std::get_if<model::ButtonPayload>(&button->payload) : nullptr;
    expect(payload && payload->buttons.size() == 2 && payload->buttons[1].buttons.size() == 1,
        "button menu and recursive submenu must live in ButtonPayload");
    expect(payload->buttons[0].action == "RunHandler" && payload->buttons[0].shortcut.key == "R",
        "handler and typed shortcut must be retained");
    const auto serialized = source::serialize_form_xml(parsed.value());
    expect(serialized.ok() && serialized.value().find("<CommandBarButton name=\"sep\" type=\"Separator\">") != std::string::npos,
        "button menu must serialize in named XML");
    const auto reparsed = source::parse_form_xml(serialized.value());
    expect(reparsed.ok(), "serialized button menu must parse again");
    const auto* restored = std::get_if<model::ButtonPayload>(&reparsed.value().find_control(model::ObjectId{2})->payload);
    expect(restored && restored->buttons == payload->buttons, "recursive button menu must roundtrip exactly");
    expect(serialized.value().find("<Order>Ascending</Order>") != std::string::npos, "submenu order must serialize by its named XML value");
    constexpr std::string_view picture_xml = R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><PictureAssets><PictureAsset id="4" relativePath="Items/Run/Buttons/More/Buttons/Item/Picture.gif" format="gif"/></PictureAssets><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Picture>4</Picture><Action>RunHandler</Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML";
    expect(source::parse_form_xml(picture_xml).ok(), "named menu picture paths must parse within source package");
    auto unsafe = std::string(picture_xml);
    unsafe.replace(unsafe.find("Buttons/More"), 12, "Buttons/../More");
    expect(!source::parse_form_xml(unsafe).ok(), "menu picture paths must reject traversal");
    auto malformed_path = std::string(picture_xml);
    malformed_path.replace(malformed_path.find("Buttons/More"), 12, "Other/More");
    expect(!source::parse_form_xml(malformed_path).ok(), "menu paths may only use named Buttons ownership segments");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"/></Buttons></Button></ChildItems></Form>)XML").ok(), "Action requires handler");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Action>One</Action></CommandBarButton><CommandBarButton name="x" type="Action"><Action>Two</Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "duplicate names in a collection must be rejected");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Submenu"><Action>Bad</Action></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "non-Action handler must be rejected");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Action"><Action>Run</Action><Order>DontOrder</Order></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "Order must be rejected on Action even when it is the default");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Separator"><Order>Ascending</Order></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "Order must be rejected on Separator");
    expect(!source::parse_form_xml(R"XML(<Form id="1" name="Menu" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Buttons><CommandBarButton name="x" type="Submenu"><Order>Random</Order></CommandBarButton></Buttons></Button></ChildItems></Form>)XML").ok(), "unknown submenu order must be rejected");
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

}  // namespace

int main() {
    try {
        test_complete_document_roundtrip();
        test_root_page_tree_xml_roundtrip();
        test_page_internal_ids_do_not_change_xml();
        test_page_boolean_defaults_and_rejections();
        test_page_position_roundtrip_and_rejections();
        test_all_control_variants();
        test_binding_target_and_manual_roundtrip();
        test_typed_values_and_canonicalization();
        test_button_shortcut_xml();
        test_boolean_type_domain_xml_roundtrip();
        test_inherited_property_has_one_surface();
        test_xml_character_normalization_is_lossless();
        test_strict_rejections();
        test_event_owner_invariant();
        test_button_foreign_enum_default_is_retained();
        test_standard_picture_xml_reference_roundtrip();
        test_button_menu_model_roundtrip_and_rejections();
        test_label_horizontal_align_xml_roundtrip();
    } catch (const std::exception& error) {
        std::cerr << "form XML tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form XML tests: PASS\n";
    return 0;
}
