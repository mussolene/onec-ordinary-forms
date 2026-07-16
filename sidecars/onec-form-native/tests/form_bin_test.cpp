#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "oof/form_bin.hpp"
#include "oof/source/form_xml.hpp"
#include "oof/storage/form_bin_container.hpp"

namespace {

namespace model = oof::model;
namespace source = oof::source;
namespace formbin = oof::storage::formbin;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

model::OrdinaryFormDocument button_document() {
    constexpr std::string_view xml = R"XML(
<Form id="1" name="Main" ordinaryFormVersion="2.1">
  <Caption>Sample</Caption>
  <ChildItems>
    <Button name="Run" id="2">
      <Position>
        <Top>20</Top>
        <Visible>false</Visible>
        <Height>30</Height>
        <Left>10</Left>
        <Width>100</Width>
      </Position>
      <Enabled>false</Enabled>
      <Caption>Run</Caption>
      <Events>
        <Click id="3">RunClick</Click>
      </Events>
    </Button>
  </ChildItems>
</Form>
)XML";
    auto parsed = source::parse_form_xml(xml);
    if (!parsed) {
        throw std::runtime_error(parsed.diagnostics().front().message);
    }
    auto document = parsed.take_value();
    document.set_module(model::FormModule{"procedure RunClick()\nendprocedure\n"});
    return document;
}

void test_product_composition_roundtrip() {
    const auto document = button_document();
    const auto encoded = oof::save_form_bin(document);
    expect(encoded.ok(), "typed Button document must encode to Form.bin");
    expect(oof::save_form_bin(document).value() == encoded.value(), "Form.bin build must be deterministic");

    const auto container = formbin::parse_container(encoded.value());
    expect(container.files.size() == 2, "product Form.bin must contain two logical files");
    expect(container.files[0].name == "form", "form stream must be first");
    expect(container.files[1].name == "module", "module stream must be second");
    expect(container.files[0].created == 0 && container.files[0].modified == 0, "container timestamps must be deterministic");

    const auto decoded = oof::load_form_bin(encoded.value(), "Main");
    expect(decoded.ok(), "product Form.bin must decode into the typed model");
    expect(decoded.value().module().text == document.module().text, "module text must round-trip");
    expect(decoded.value().collections().controls.size() == 1, "Button must round-trip");
    const auto& control = decoded.value().collections().controls.front();
    expect(control.kind() == model::ControlKind::button, "control kind must remain Button");
    expect(control.name == "Run", "control name must round-trip");
    expect(control.position.left.value() == 10, "left position must round-trip");
    expect(control.position.top.value() == 20, "top position must round-trip");
    expect(control.position.width.value() == 100, "width must round-trip");
    expect(control.position.height.value() == 30, "height must round-trip");
    expect(!control.position.visible.value(), "visibility must round-trip");
    const auto* enabled = control.properties().find(model::PropertyId::from_name("Enabled"));
    expect(enabled != nullptr && !std::get<bool>(enabled->value), "Enabled=false must round-trip");
    expect(decoded.value().collections().events.size() == 1, "Button.Click must round-trip");
    expect(decoded.value().collections().events.front().handler == "RunClick", "event handler must round-trip");
    expect(oof::save_form_bin(decoded.value()).value() == encoded.value(), "load-save Form.bin must be byte-stable for the canonical slice");
}

void test_unknown_logical_file_is_rejected() {
    const auto encoded = oof::save_form_bin(button_document());
    expect(encoded.ok(), "fixture must encode");
    auto container = formbin::parse_container(encoded.value());
    container.files.push_back({"unknown", 0, 0, {0x01}});
    const auto decoded = oof::load_form_bin(formbin::serialize_container(container));
    expect(!decoded, "unknown logical file must fail closed");
    expect(decoded.diagnostics().front().code == "OOF1202", "unknown logical file diagnostic mismatch");
}

}  // namespace

int main() {
    try {
        test_product_composition_roundtrip();
        test_unknown_logical_file_is_rejected();
    } catch (const std::exception& error) {
        std::cerr << "form bin tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form bin tests: PASS\n";
    return 0;
}
