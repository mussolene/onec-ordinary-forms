#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "oof/form_bin.hpp"
#include "oof/source/form_xml.hpp"
#include "oof/storage/form_bin_container.hpp"
#include "oof/storage/list_stream.hpp"

namespace {

namespace model = oof::model;
namespace source = oof::source;
namespace formbin = oof::storage::formbin;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

model::OrdinaryFormDocument button_document(
    bool enabled = false,
    std::string_view caption = "Run") {
    std::string xml = R"XML(
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
      <Caption>__BUTTON_CAPTION__</Caption>
      <Events>
        <Click id="3">RunClick</Click>
      </Events>
    </Button>
  </ChildItems>
</Form>
)XML";
    const std::string enabled_xml = enabled
        ? "<Enabled>true</Enabled>"
        : "<Enabled>false</Enabled>";
    xml.replace(xml.find("<Enabled>false</Enabled>"),
                std::string("<Enabled>false</Enabled>").size(),
                enabled_xml);
    xml.replace(xml.find("__BUTTON_CAPTION__"),
                std::string("__BUTTON_CAPTION__").size(),
                caption);
    auto parsed = source::parse_form_xml(xml);
    if (!parsed) {
        throw std::runtime_error(parsed.diagnostics().front().message);
    }
    auto document = parsed.take_value();
    document.set_module(model::FormModule{"procedure RunClick()\nendprocedure\n"});
    return document;
}

void test_label_pictures_roundtrip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "LabelPictures";
    for (std::uint64_t id = 2; id < 14; ++id) form.children.push_back(model::ControlRef{model::ObjectId{id}});
    model::OrdinaryFormDocument document(std::move(form));
    const std::vector<std::uint8_t> gif{'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0, 0, 0, 0, 255, 255, 255,
        0x21, 0xf9, 4, 1, 0, 0, 0, 0, 0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 1, 0x44, 0, 0x3b};
    document.add_asset(model::PictureAsset{model::ObjectId{40}, "Items/Label0/Picture.gif", model::PictureFormat::gif, gif, false});
    const auto property = model::PropertyId::from_name("Picture");
    constexpr std::array sizes{"RealSize", "Stretch", "Proportionally", "Tile", "AutoSize", "ByFontSize"};
    constexpr std::array locations{"Left", "Top", "Right", "Bottom", "Center"};
    for (std::uint64_t id = 2; id < 14; ++id) {
        model::ControlNode control{model::ObjectId{id}, "Label" + std::to_string(id - 2), model::LabelDecorationPayload{}};
        if (id == 2) control.properties().set_explicit(property, model::PictureRef{model::PictureAssetRef{model::ObjectId{40}}});
        else if (id % 3 != 0) control.properties().set_explicit(property, model::PictureRef{model::PictureAssetRef{}, model::QualifiedName{"PictureLib.Write"}});
        control.properties().set_explicit(model::PropertyId::from_name("Format"), std::string("ЧДЦ=") + std::to_string(id));
        control.properties().set_explicit(model::PropertyId::from_name("PictureSize"), model::EnumerationValue{"PictureSize", sizes[(id - 2) % sizes.size()]});
        control.properties().set_explicit(model::PropertyId::from_name("LabelPictureLocation"), model::EnumerationValue{"LabelPictureLocation", locations[(id - 2) % locations.size()]});
        document.add_control(std::move(control));
    }
    const auto encoded = oof::save_form_bin(document);
    expect(encoded.ok(), "label pictures must encode in Form.bin");
    const auto decoded = oof::load_form_bin(encoded.value(), "LabelPictures");
    expect(decoded.ok(), "label pictures must decode from Form.bin");
    expect(decoded.value().assets().size() == 1 && decoded.value().assets().front().bytes == gif,
        "label external picture must retain its bytes");
    for (std::uint64_t id = 2; id < 14; ++id) {
        const auto* label = decoded.value().find_control(model::ObjectId{id});
        expect(label != nullptr, "every label must survive the roundtrip");
        const auto* picture_entry = label->properties().find(property);
        if (id != 2 && id % 3 == 0) expect(!picture_entry, "empty picture must remain empty");
        else {
            expect(picture_entry != nullptr, "label must retain its picture");
            const auto* picture = std::get_if<model::PictureRef>(&picture_entry->value);
            expect(picture && (id == 2 ? picture->asset.id() == decoded.value().assets().front().id :
                picture->standard_name && picture->standard_name->value == "PictureLib.Write"),
                "label pictures must retain the correct named reference");
        }
        const model::OrdinaryFormDocument::ObjectKey owner{model::ObjectCategory::control, model::ObjectId{id}};
        expect(decoded.value().get_prop_val(owner, model::PropertyId::from_name("Format")).value == model::PropertyValue{std::string("ЧДЦ=") + std::to_string(id)},
            "label format must survive the roundtrip");
        expect(decoded.value().get_prop_val(owner, model::PropertyId::from_name("PictureSize")).value ==
                model::PropertyValue{model::EnumerationValue{"PictureSize", sizes[(id - 2) % sizes.size()]}},
            "label picture size must survive the roundtrip");
        expect(decoded.value().get_prop_val(owner, model::PropertyId::from_name("LabelPictureLocation")).value ==
                model::PropertyValue{model::EnumerationValue{"LabelPictureLocation", locations[(id - 2) % locations.size()]}},
            "label picture location must survive the roundtrip");
    }
}

void test_button_base_state_normalization() {
    constexpr std::string_view utf8_caption = "Проверка";
    for (const bool enabled : {false, true}) {
        const auto canonical_bytes = oof::save_form_bin(button_document(enabled, utf8_caption));
        expect(canonical_bytes.ok(), "Button fixture must encode");

        const auto alter_button_record = [&](std::string_view state, bool alter_neighbor) {
            auto container = formbin::parse_container(canonical_bytes.value());
            auto form_file = std::find_if(
                container.files.begin(), container.files.end(),
                [](const auto& file) { return file.name == "form"; });
            expect(form_file != container.files.end(), "form logical file must exist");
            expect(
                form_file->payload.size() >= 3 && form_file->payload[0] == 0xef &&
                    form_file->payload[1] == 0xbb && form_file->payload[2] == 0xbf,
                "form stream must have the expected UTF-8 BOM");
            const std::string text(
                form_file->payload.begin() + 3,
                form_file->payload.end());
            auto payload = oof::storage::list_stream::parse(text);
            auto& base = payload.items[1].items[2].items[2].items[1]
                             .items[2].items[1].items[0];
            base.items[17] = oof::storage::list_stream::ListValue::raw_atom(std::string(state));
            if (alter_neighbor) {
                base.items[16] = oof::storage::list_stream::ListValue::raw_atom("3");
            }
            const std::string rewritten = oof::storage::list_stream::dump_listout(payload);
            form_file->payload.assign({0xef, 0xbb, 0xbf});
            form_file->payload.insert(form_file->payload.end(), rewritten.begin(), rewritten.end());
            return formbin::serialize_container(container);
        };

        const auto from_state_one = oof::load_form_bin(alter_button_record("1", false), "Main");
        const auto from_state_two = oof::load_form_bin(alter_button_record("2", false), "Main");
        expect(from_state_one.ok() && from_state_two.ok(), "observed Button states 1 and 2 must decode");
        const auto xml_one = source::serialize_form_xml(from_state_one.value());
        const auto xml_two = source::serialize_form_xml(from_state_two.value());
        expect(xml_one.ok() && xml_two.ok(), "both decoded Button models must serialize as XML");
        expect(xml_one.value() == xml_two.value(), "internal Button state must not affect the named model");
        expect(xml_one.value().find(utf8_caption) != std::string::npos, "UTF-8 Button Caption must survive decode");
        const auto& decoded_control = from_state_one.value().collections().controls.front();
        const auto* explicit_enabled = decoded_control.properties().find(
            model::PropertyId::from_name("Enabled"));
        if (enabled) {
            expect(explicit_enabled == nullptr, "default Enabled=true must remain implicit");
        } else {
            expect(
                explicit_enabled != nullptr && !std::get<bool>(explicit_enabled->value),
                "explicit Enabled=false must survive decode");
        }

        const auto canonicalized = oof::save_form_bin(from_state_one.value());
        expect(canonicalized.ok(), "decoded Button must save through the primary writer");
        const auto repeated = oof::save_form_bin(from_state_two.value());
        expect(
            repeated.ok() && repeated.value() == canonicalized.value(),
            "both inputs must build the same stable canonical Form.bin");
        const auto canonical_container = formbin::parse_container(canonicalized.value());
        const auto canonical_form = std::find_if(
            canonical_container.files.begin(), canonical_container.files.end(),
            [](const auto& file) { return file.name == "form"; });
        const std::string canonical_text(canonical_form->payload.begin() + 3, canonical_form->payload.end());
        const auto canonical_payload = oof::storage::list_stream::parse(canonical_text);
        expect(
            canonical_payload.items[1].items[2].items[2].items[1]
                .items[2].items[1].items[0].items[17].atom == "2",
            "writer must emit the observed edited-control state 2");

        for (const std::string_view unsupported_state : {"0", "3"}) {
            const auto partial = oof::load_form_bin(
                alter_button_record(unsupported_state, false), "Main");
            expect(partial.ok() && !partial.value().reconstruction_complete() &&
                       !partial.diagnostics().empty() && partial.diagnostics().front().code == "OOF1140" &&
                       partial.diagnostics().front().severity == oof::DiagnosticSeverity::warning,
                "valid unobserved Button base state must load with OOF1140 and incomplete reconstruction");
            const auto* partial_control = partial.value().find_control(model::ObjectId{2});
            expect(partial_control != nullptr && partial_control->name == "Run",
                "known Button identity must survive an unobserved state");
            const auto* partial_enabled = partial_control->properties().find(
                model::PropertyId::from_name("Enabled"));
            if (enabled) {
                expect(partial_enabled == nullptr, "default Enabled=true must stay implicit after a partial decode");
            } else {
                expect(partial_enabled != nullptr && !std::get<bool>(partial_enabled->value),
                    "known Enabled=false must survive an unobserved state");
            }
            const auto partial_xml = source::serialize_form_xml(partial.value());
            expect(partial_xml.ok(), "partial Button must serialize to named XML");
            const auto partial_parsed = source::parse_form_xml(partial_xml.value());
            expect(partial_parsed.ok() && !partial_parsed.value().reconstruction_complete(),
                "partial Button XML must preserve incomplete reconstruction");
            const auto partial_build = oof::save_form_bin(partial_parsed.value());
            expect(partial_build.ok(), "partial Button XML must remain buildable to Form.bin");
        }
        const auto changed_neighbor = oof::load_form_bin(alter_button_record("2", true), "Main");
        expect(changed_neighbor.ok() && !changed_neighbor.value().reconstruction_complete() &&
                   !changed_neighbor.diagnostics().empty() && changed_neighbor.diagnostics().front().code == "OOF1140",
            "valid unknown Button base profile variation must warn and return an incomplete model");
    }
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

void test_module_newlines_are_canonical_at_container_boundary() {
    const auto with_module = [](std::string text) {
        auto document = button_document();
        document.set_module(model::FormModule{std::move(text)});
        return document;
    };
    const std::string lf = "first\nsecond\nlast";
    const std::string crlf = "first\r\nsecond\r\nlast";
    const std::string mixed = "first\r\nsecond\nlast";
    const auto lf_bytes = oof::save_form_bin(with_module(lf));
    const auto crlf_bytes = oof::save_form_bin(with_module(crlf));
    const auto mixed_bytes = oof::save_form_bin(with_module(mixed));
    expect(lf_bytes.ok() && crlf_bytes.ok() && mixed_bytes.ok(), "module newline variants must encode");
    expect(lf_bytes.value() == crlf_bytes.value() && lf_bytes.value() == mixed_bytes.value(),
        "LF, CRLF, and mixed module input must produce identical Form.bin bytes");

    const auto container = formbin::parse_container(lf_bytes.value());
    const auto module = std::find_if(container.files.begin(), container.files.end(),
        [](const auto& file) { return file.name == "module"; });
    expect(module != container.files.end(), "module stream must exist");
    const std::string stored(module->payload.begin() + 3, module->payload.end());
    expect(stored == crlf, "Form.bin module stream must use canonical CRLF");

    const auto decoded = oof::load_form_bin(lf_bytes.value(), "Main");
    expect(decoded.ok() && decoded.value().module().text == lf,
        "module load must expose LF while preserving a final unterminated line");
    const auto repeated = oof::save_form_bin(decoded.value());
    expect(repeated.ok() && repeated.value() == lf_bytes.value(), "module save/load/save must be idempotent");

    auto empty_document = with_module("");
    const auto empty_bytes = oof::save_form_bin(empty_document);
    expect(empty_bytes.ok(), "empty module must encode");
    const auto empty_decoded = oof::load_form_bin(empty_bytes.value(), "Main");
    expect(empty_decoded.ok() && empty_decoded.value().module().text.empty(), "empty module must remain empty");
}

void test_module_utf8_validation() {
    auto document = button_document();
    const std::string valid = "Процедура Проверка()\n// Ω " "\xf0\x9f\x92\xa1" "\nКонецПроцедуры\n";
    document.set_module(model::FormModule{valid});
    const auto valid_bytes = oof::save_form_bin(document);
    expect(valid_bytes.ok(), "valid Unicode module must encode");
    const auto valid_decoded = oof::load_form_bin(valid_bytes.value(), "Main");
    expect(valid_decoded.ok() && valid_decoded.value().module().text == valid,
        "valid Unicode module and canonical line endings must survive loading");

    const std::vector<std::string> invalid_values{
        std::string("\xff", 1),                 // Invalid leading byte.
        std::string("\xe2\x82", 2),           // Truncated multibyte character.
        std::string("\xc0\xaf", 2),           // Overlong encoding.
        std::string("\xed\xa0\x80", 3),       // UTF-16 surrogate.
        std::string("\xe2\x28\xa1", 3),       // Invalid continuation.
        std::string("\xf4\x90\x80\x80", 4), // Code point outside Unicode.
        std::string("valid\n") + std::string("\x80", 1), // Invalid byte after valid text.
    };
    for (const auto& invalid : invalid_values) {
        document.set_module(model::FormModule{invalid});
        const auto rejected_save = oof::save_form_bin(document);
        expect(!rejected_save && !rejected_save.diagnostics().empty() &&
                   rejected_save.diagnostics().front().code == "OOF1204" &&
                   rejected_save.diagnostics().front().path == "Form.bin/module",
            "saving a malformed UTF-8 module must fail with a module diagnostic");

        for (const bool has_bom : {false, true}) {
            auto container = formbin::parse_container(valid_bytes.value());
            auto module = std::find_if(container.files.begin(), container.files.end(),
                [](const auto& file) { return file.name == "module"; });
            expect(module != container.files.end(), "module fixture must exist");
            module->payload.clear();
            if (has_bom) module->payload.insert(module->payload.end(), {0xef, 0xbb, 0xbf});
            module->payload.insert(module->payload.end(), invalid.begin(), invalid.end());
            const auto rejected_load = oof::load_form_bin(formbin::serialize_container(container), "Main");
            expect(!rejected_load && !rejected_load.diagnostics().empty() &&
                       rejected_load.diagnostics().front().code == "OOF1204" &&
                       rejected_load.diagnostics().front().path == "Form.bin/module",
                "loading malformed UTF-8 module with or without BOM must fail closed");
        }
    }

    auto bom_container = formbin::parse_container(valid_bytes.value());
    auto module = std::find_if(bom_container.files.begin(), bom_container.files.end(),
        [](const auto& file) { return file.name == "module"; });
    expect(module != bom_container.files.end(), "Unicode module fixture must exist");
    const std::string with_crlf = "Строка Ω\r\n";
    module->payload.assign({0xef, 0xbb, 0xbf});
    module->payload.insert(module->payload.end(), with_crlf.begin(), with_crlf.end());
    const auto bom_decoded = oof::load_form_bin(formbin::serialize_container(bom_container), "Main");
    expect(bom_decoded.ok() && bom_decoded.value().module().text == "Строка Ω\n",
        "UTF-8 BOM and CRLF module must load as Unicode with canonical line endings");
    module->payload.assign(with_crlf.begin(), with_crlf.end());
    const auto no_bom_decoded = oof::load_form_bin(formbin::serialize_container(bom_container), "Main");
    expect(no_bom_decoded.ok() && no_bom_decoded.value().module().text == "Строка Ω\n",
        "UTF-8 module without BOM must load with canonical line endings");
}

}  // namespace

int main() {
    try {
        test_product_composition_roundtrip();
        test_label_pictures_roundtrip();
        test_button_base_state_normalization();
        test_unknown_logical_file_is_rejected();
        test_module_newlines_are_canonical_at_container_boundary();
        test_module_utf8_validation();
    } catch (const std::exception& error) {
        std::cerr << "form bin tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form bin tests: PASS\n";
    return 0;
}
