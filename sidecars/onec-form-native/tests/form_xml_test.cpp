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
          <AnchorBinding coordinate="left" offset="0"/>
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

void test_typed_values_and_canonicalization() {
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
      <Font kind="styleReference" mask="0" styleName="ui:TextFont"/>
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
    expect(serialized.value().find("styleName=\"ui:TextFont\"") != std::string::npos, "lexical style name must not require an XML namespace binding");
    expect(source::parse_form_xml(serialized.value()).ok(), "canonical typed values must validate and reparse");
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
        test_all_control_variants();
        test_typed_values_and_canonicalization();
        test_inherited_property_has_one_surface();
        test_xml_character_normalization_is_lossless();
        test_strict_rejections();
        test_event_owner_invariant();
    } catch (const std::exception& error) {
        std::cerr << "form XML tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form XML tests: PASS\n";
    return 0;
}
