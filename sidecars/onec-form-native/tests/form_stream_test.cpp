#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <zlib.h>

#include "oof/form_bin.hpp"
#include "oof/model/metamodel.hpp"
#include "oof/source/form_xml.hpp"
#include "oof/storage/form_stream.hpp"
#include "oof/storage/value_codec.hpp"

namespace {

namespace form_stream = oof::storage::form_stream;
namespace list_stream = oof::storage::list_stream;
namespace model = oof::model;
namespace source = oof::source;
namespace value_codec = oof::storage::value_codec;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename T>
void expect_failure(
    const oof::Result<T>& result,
    std::string_view code,
    std::string_view path,
    std::string_view message) {
    expect(!result, message);
    expect(result.diagnostics().size() == 1, "storage failure must have one local diagnostic");
    if (result.diagnostics()[0].code != code) {
        throw std::runtime_error(
            "storage diagnostic code mismatch: expected " + std::string(code) +
            ", got " + result.diagnostics()[0].code + ": " +
            result.diagnostics()[0].message);
    }
    if (result.diagnostics()[0].path != path) {
        throw std::runtime_error(
            "storage diagnostic path mismatch: expected " + std::string(path) +
            ", got " + result.diagnostics()[0].path);
    }
}

model::CompositeIdValue composite(std::int64_t id, std::string uuid) {
    return model::CompositeIdValue{id, model::UuidValue{std::move(uuid)}, false};
}

list_stream::ListValue versioned_record(std::uint32_t version, std::size_t arity) {
    std::vector<list_stream::ListValue> items(
        arity,
        list_stream::ListValue::raw_atom("0"));
    items[0] = list_stream::ListValue::raw_atom(std::to_string(version));
    return list_stream::ListValue::list(std::move(items));
}

std::size_t geometry_tail_start(const list_stream::ListValue& geometry) {
    std::size_t cursor = 12;
    for (std::size_t edge = 0; edge < 6; ++edge) {
        const auto count = static_cast<std::size_t>(std::stoul(geometry.items.at(cursor).atom));
        cursor += 1 + count;
    }
    expect(geometry.items.size() == cursor + 5, "geometry tail must follow the variable anchor records");
    return cursor;
}

list_stream::ListValue layout_fixture(form_stream::LayoutKind kind) {
    std::vector<list_stream::ListValue> root(
        20,
        list_stream::ListValue::raw_atom("0"));
    root[0] = list_stream::ListValue::raw_atom("27");
    if (kind == form_stream::LayoutKind::form_section_16) {
        root[1] = versioned_record(16, 11);
        root[13] = versioned_record(3, 3);
    } else {
        root[1] = versioned_record(18, 14);
        root[13] = versioned_record(10, 11);
    }
    return list_stream::ListValue::list(std::move(root));
}

std::string runtime_envelope_text(const list_stream::ListValue& payload) {
    return list_stream::dump_compact(list_stream::ListValue::list({
        list_stream::ListValue::string_atom("#"),
        list_stream::ListValue::raw_atom(
            "01234567-89ab-cdef-0123-456789abcdef"),
        payload,
    }));
}

list_stream::ListValue* find_usual_group_record(list_stream::ListValue& value) {
    constexpr std::string_view guid = "90db814a-c75f-4b54-bc96-df62e554d67d";
    if (value.is_list && value.items.size() == 6 && !value.items[0].is_list && value.items[0].atom == guid) return &value;
    if (!value.is_list) return nullptr;
    for (auto& item : value.items) {
        if (auto* found = find_usual_group_record(item)) return found;
    }
    return nullptr;
}

list_stream::ListValue* find_chart_record(list_stream::ListValue& value) {
    constexpr std::string_view guid = "a8b97779-1a4b-4059-b09c-807f86d2a461";
    if (value.is_list && value.items.size() == 7 && !value.items[0].is_list && value.items[0].atom == guid) return &value;
    if (!value.is_list) return nullptr;
    for (auto& item : value.items) {
        if (auto* found = find_chart_record(item)) return found;
    }
    return nullptr;
}

list_stream::ListValue* find_record_with_guid(list_stream::ListValue& value, std::string_view guid) {
    if (value.is_list && value.items.size() == 6 && !value.items[0].is_list && value.items[0].atom == guid) {
        return &value;
    }
    if (!value.is_list) return nullptr;
    for (auto& item : value.items) {
        if (auto* found = find_record_with_guid(item, guid)) return found;
    }
    return nullptr;
}

list_stream::ListValue& captured_column_packet(list_stream::ListValue& payload) {
    auto* table = find_record_with_guid(
        payload, model::metamodel::descriptor_for(model::ControlKind::table).guid);
    expect(table != nullptr, "captured full form must contain Table record");
    return table->items[2].items[2].items[1].items[23].items[1].items[1].items[1].items[1].items[39];
}

std::vector<std::uint8_t> test_base64_decode(std::string_view text) {
    const auto value = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };
    std::vector<std::uint8_t> bytes;
    for (std::size_t pos = 0; pos < text.size(); pos += 4) {
        const int a = value(text[pos]);
        const int b = value(text[pos + 1]);
        const int c = text[pos + 2] == '=' ? 0 : value(text[pos + 2]);
        const int d = text[pos + 3] == '=' ? 0 : value(text[pos + 3]);
        expect(a >= 0 && b >= 0 && c >= 0 && d >= 0, "captured packet base64 must be valid");
        const auto bits = (static_cast<std::uint32_t>(a) << 18) | (static_cast<std::uint32_t>(b) << 12) |
            (static_cast<std::uint32_t>(c) << 6) | static_cast<std::uint32_t>(d);
        bytes.push_back(static_cast<std::uint8_t>(bits >> 16));
        if (text[pos + 2] != '=') bytes.push_back(static_cast<std::uint8_t>(bits >> 8));
        if (text[pos + 3] != '=') bytes.push_back(static_cast<std::uint8_t>(bits));
    }
    return bytes;
}

std::string test_base64_encode(const std::vector<std::uint8_t>& bytes) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (std::size_t pos = 0; pos < bytes.size(); pos += 3) {
        const std::uint32_t a = bytes[pos];
        const std::uint32_t b = pos + 1 < bytes.size() ? bytes[pos + 1] : 0;
        const std::uint32_t c = pos + 2 < bytes.size() ? bytes[pos + 2] : 0;
        const std::uint32_t bits = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(bits >> 18) & 63]);
        output.push_back(alphabet[(bits >> 12) & 63]);
        output.push_back(pos + 1 < bytes.size() ? alphabet[(bits >> 6) & 63] : '=');
        output.push_back(pos + 2 < bytes.size() ? alphabet[bits & 63] : '=');
    }
    return output;
}

std::vector<std::uint8_t> test_raw_inflate(const std::vector<std::uint8_t>& packet) {
    z_stream stream{};
    expect(inflateInit2(&stream, -MAX_WBITS) == Z_OK, "test raw inflate must initialize");
    std::vector<std::uint8_t> output(8192);
    stream.next_in = const_cast<Bytef*>(packet.data() + 18);
    stream.avail_in = static_cast<uInt>(packet.size() - 18);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = inflate(&stream, Z_FINISH);
    const auto size = stream.total_out;
    inflateEnd(&stream);
    expect(result == Z_STREAM_END, "captured packet raw DEFLATE must inflate");
    output.resize(size);
    return output;
}

std::vector<std::uint8_t> test_raw_deflate(const std::vector<std::uint8_t>& input, int level) {
    z_stream stream{};
    expect(deflateInit2(&stream, level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK,
        "test raw deflate must initialize");
    std::vector<std::uint8_t> output(compressBound(static_cast<uLong>(input.size())));
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = deflate(&stream, Z_FINISH);
    const auto size = stream.total_out;
    deflateEnd(&stream);
    expect(result == Z_STREAM_END, "test raw DEFLATE must complete");
    output.resize(size);
    return output;
}

void set_captured_packet(list_stream::ListValue& payload, const std::vector<std::uint8_t>& packet) {
    auto& chunks = captured_column_packet(payload).items[0].items;
    chunks.clear();
    const auto encoded = test_base64_encode(packet);
    for (std::size_t pos = 0; pos < encoded.size(); pos += 64) {
        chunks.push_back(list_stream::ListValue::raw_atom(
            (pos == 0 ? "#base64:" : "") + encoded.substr(pos, std::min<std::size_t>(64, encoded.size() - pos))));
    }
}

list_stream::ListValue captured_table_payload() {
    constexpr std::string_view captured_text =
#include "fixtures/table-after-create-columns.inc"
        ;
    const auto envelope = form_stream::decode_runtime_envelope(captured_text);
    expect(envelope.ok(), "captured full form envelope must decode");
    return envelope.value().payload;
}

void test_captured_table_column_record() {
    constexpr std::string_view captured_text =
#include "fixtures/table-after-create-columns.inc"
        ;
    auto envelope = form_stream::decode_runtime_envelope(captured_text);
    expect(envelope.ok(), "independent captured full form envelope must decode");
    auto captured_payload = envelope.value().payload;
    const auto* captured_table_record = find_record_with_guid(
        captured_payload, model::metamodel::descriptor_for(model::ControlKind::table).guid);
    expect(captured_table_record != nullptr, "captured full form must contain a Table record");

    const auto decoded = form_stream::decode_document(envelope.value().payload, "CapturedTable");
    expect(decoded.ok(), decoded ? "captured Table must decode" :
        decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message +
            " expected=" + decoded.diagnostics().front().expected +
            " actual=" + decoded.diagnostics().front().actual);
    const auto table = std::find_if(decoded.value().collections().controls.begin(),
        decoded.value().collections().controls.end(), [](const auto& control) {
            return control.kind() == model::ControlKind::table;
        });
    expect(table != decoded.value().collections().controls.end(), "captured model must expose named Table");
    const auto& payload = std::get<model::TablePayload>(table->payload);
    expect(payload.columns.size() == 1 && payload.columns[0].name == "Code" &&
            payload.columns[0].data_path == "Code" && payload.columns[0].control.kind == model::ControlKind::input_field,
        "captured Column must expose its name, DataPath, and typed InputField");

    const auto encoded = form_stream::encode_document(decoded.value());
    expect(encoded.ok(), encoded ? "captured Table must encode" :
        encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message);
    auto expected = *captured_table_record;
    auto encoded_payload = encoded.value();
    const auto* encoded_table_record = find_record_with_guid(
        encoded_payload,
        model::metamodel::descriptor_for(model::ControlKind::table).guid);
    expect(encoded_table_record != nullptr, "re-encoded form must contain its Table record");
    auto& expected_packet = expected.items[2].items[2].items[1].items[23].items[1].items[1].items[1].items[1].items[39];
    const auto& encoded_packet = encoded_table_record->items[2].items[2].items[1].items[23].items[1].items[1].items[1].items[1].items[39];
    expected_packet = encoded_packet;
    expect(list_stream::dump_compact(expected) == list_stream::dump_compact(*encoded_table_record),
        "independent captured Table/Column record must re-encode exactly apart from recompressed editor bytes");
}

void test_table_read_only_runtime_flags() {
    for (const bool read_only : {true, false}) {
        auto literal = captured_table_payload();
        auto* record = find_record_with_guid(literal,
            model::metamodel::descriptor_for(model::ControlKind::table).guid);
        expect(record != nullptr, "captured fixture must contain Table");
        // Независимый опыт true/false/true меняет только этот флаг Table на 0x400.
        const auto expected_flags = read_only ? "117643809" : "117644833";
        record->items[2].items[2].items[1].items[1] = list_stream::ListValue::raw_atom(expected_flags);
        auto decoded = form_stream::decode_document(literal, "LiteralTableReadOnly");
        expect(decoded.ok(), "independent literal Table ReadOnly flags must decode");
        const auto table = std::find_if(decoded.value().collections().controls.begin(),
            decoded.value().collections().controls.end(), [](const auto& control) {
                return control.kind() == model::ControlKind::table;
            });
        expect(table != decoded.value().collections().controls.end(), "literal Table must retain its named model");
        const auto* property = table->properties().find(model::PropertyId::from_name("ReadOnly"));
        expect(read_only ? property == nullptr : property != nullptr && !std::get<bool>(property->value),
            "Table ReadOnly=true must remain the profile default and false must be explicit");
        auto xml = source::serialize_form_xml(decoded.value());
        expect(xml.ok(), xml ? "named Table ReadOnly must serialize to XML" :
            xml.diagnostics().front().path + ": " + xml.diagnostics().front().message);
        expect((xml.value().find("<ReadOnly>false</ReadOnly>") != std::string::npos) == !read_only,
            "nondefault Table ReadOnly=false must be public XML");
        auto parsed = source::parse_form_xml(xml.value());
        expect(parsed.ok(), "named Table ReadOnly XML must parse");
        auto encoded = form_stream::encode_document(parsed.value());
        expect(encoded.ok(), "Table ReadOnly must survive XML and storage roundtrip");
        const auto* encoded_record = find_record_with_guid(encoded.value(),
            model::metamodel::descriptor_for(model::ControlKind::table).guid);
        expect(encoded_record != nullptr && encoded_record->items[2].items[2].items[1].items[1].atom == expected_flags,
            "writer must use the independent true/false literal flags");
        if (read_only) {
            const_cast<model::ControlNode&>(*table).properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
            auto explicit_true = form_stream::encode_document(decoded.value());
            expect(explicit_true.ok(), "explicit Table ReadOnly=true must be supported");
            const auto* explicit_record = find_record_with_guid(explicit_true.value(),
                model::metamodel::descriptor_for(model::ControlKind::table).guid);
            expect(explicit_record != nullptr &&
                list_stream::dump_compact(*explicit_record) == list_stream::dump_compact(*encoded_record),
                "explicit and omitted ReadOnly=true must have identical Table records");
        }
    }
    auto unknown_flag = captured_table_payload();
    auto* record = find_record_with_guid(unknown_flag,
        model::metamodel::descriptor_for(model::ControlKind::table).guid);
    record->items[2].items[2].items[1].items[1] = list_stream::ListValue::raw_atom("117644835");
    expect(!form_stream::decode_document(unknown_flag, "UnknownTableFlag"),
        "changing any unproven Table flag must fail strict canonical validation");
}

void test_table_first_in_group_observed_metadata() {
    for (const bool first_in_group : {false, true}) {
        auto literal = captured_table_payload();
        auto* record = find_record_with_guid(literal,
            model::metamodel::descriptor_for(model::ControlKind::table).guid);
        expect(record != nullptr, "captured fixture must contain Table");
        // Table setter/getter и снимки 70978: {14,"Rows",4294967295,0,0,0} -> ...0,0,1.
        // Не используем меняющийся между снимками счетчик формы как mapping свойства.
        const auto observed_metadata = list_stream::parse(first_in_group
            ? R"LS({14,"Rows",4294967295,0,0,1})LS"
            : R"LS({14,"Rows",4294967295,0,0,0})LS");
        record->items[4] = observed_metadata;
        auto decoded = form_stream::decode_document(literal, "ObservedTableFirstInGroup");
        expect(decoded.ok(), "independent Table FirstInGroup metadata must decode");
        const auto table = std::find_if(decoded.value().collections().controls.begin(),
            decoded.value().collections().controls.end(), [](const auto& control) {
                return control.kind() == model::ControlKind::table;
            });
        expect(table != decoded.value().collections().controls.end(), "observed Table must be named");
        const auto* property = table->extension_properties.find(model::PropertyId::from_name("FirstInGroup"));
        expect(first_in_group ? property != nullptr && std::get<bool>(property->value) : property == nullptr,
            "Table FirstInGroup=false must be the storage default and true explicit");
        auto xml = source::serialize_form_xml(decoded.value());
        expect(xml.ok(), "observed FirstInGroup must serialize to named XML");
        expect((xml.value().find("<FirstInGroup>true</FirstInGroup>") != std::string::npos) == first_in_group,
            "FirstInGroup must use the existing public property without raw storage");
        auto parsed = source::parse_form_xml(xml.value());
        expect(parsed.ok(), "observed FirstInGroup XML must parse");
        auto encoded = form_stream::encode_document(parsed.value());
        expect(encoded.ok(), "observed FirstInGroup must survive XML and storage");
        const auto* encoded_record = find_record_with_guid(encoded.value(),
            model::metamodel::descriptor_for(model::ControlKind::table).guid);
        expect(encoded_record != nullptr &&
                   list_stream::dump_compact(encoded_record->items[4]) == list_stream::dump_compact(observed_metadata),
            "writer must reproduce the exact independent Table metadata");
    }

    for (const auto& invalid_value : {list_stream::ListValue::raw_atom("2"),
                                      list_stream::ListValue::string_atom("1"),
                                      list_stream::ListValue::list({list_stream::ListValue::raw_atom("1")})}) {
        auto literal = captured_table_payload();
        auto* record = find_record_with_guid(literal,
            model::metamodel::descriptor_for(model::ControlKind::table).guid);
        record->items[4].items[5] = invalid_value;
        expect(!form_stream::decode_document(literal, "MalformedTableFirstInGroup"),
            "unknown FirstInGroup ordinal and non-raw Boolean must fail closed");
    }
    auto reordered = captured_table_payload();
    auto* record = find_record_with_guid(reordered,
        model::metamodel::descriptor_for(model::ControlKind::table).guid);
    record->items[4].items[4] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(reordered, "ReorderedTableFirstInGroup"),
        "FirstInGroup in an unproven neighboring metadata slot must fail closed");
}

void test_table_first_in_group_fresh_model_xml_bin() {
    const auto make_document = [](std::optional<bool> first_in_group) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "TableFirstInGroup";
        form.children = {model::ControlRef{model::ObjectId{3}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::value_table;
        type.entries.push_back(entry);
        document.add_attribute(model::Attribute{model::ObjectId{2}, "Rows", type});
        model::ControlNode table{model::ObjectId{3}, "Rows", model::TablePayload{}};
        table.data_path = model::DataPath{model::AttributeRef{model::ObjectId{2}}, {}};
        table.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), false);
        if (first_in_group) {
            table.extension_properties.set_explicit(model::PropertyId::from_name("FirstInGroup"), *first_in_group);
        }
        auto& columns = std::get<model::TablePayload>(table.payload).columns;
        for (const auto kind : {model::ControlKind::input_field, model::ControlKind::choice_field,
                                model::ControlKind::check_box}) {
            model::TableColumn column;
            column.name = std::string(model::metamodel::descriptor_for(kind).public_name);
            column.data_path = "Code";
            column.header.items.push_back({"en", column.name});
            column.control.kind = kind;
            columns.push_back(std::move(column));
        }
        document.add_control(std::move(table));
        return document;
    };

    std::optional<list_stream::ListValue> omitted_record;
    for (const auto first_in_group : {std::optional<bool>{}, std::optional<bool>{false}, std::optional<bool>{true}}) {
        auto document = make_document(first_in_group);
        auto xml = source::serialize_form_xml(document);
        expect(xml.ok(), "fresh named Table model must serialize to XML");
        if (first_in_group) {
            expect(xml.value().find(*first_in_group ? "<FirstInGroup>true</FirstInGroup>"
                                                    : "<FirstInGroup>false</FirstInGroup>") != std::string::npos,
                "explicit Boolean values must use named FirstInGroup XML");
        }
        auto parsed = source::parse_form_xml(xml.value());
        expect(parsed.ok(), "fresh named XML must parse without a baseline");
        auto binary = oof::save_form_bin(parsed.value());
        expect(binary.ok(), "fresh named XML must produce Form.bin without a source binary");
        auto loaded = oof::load_form_bin(binary.value(), "TableFirstInGroup");
        expect(loaded.ok(), "fresh FirstInGroup Form.bin must load");
        const auto* table = loaded.value().find_control(model::ObjectId{3});
        expect(table != nullptr && table->kind() == model::ControlKind::table, "fresh binary must retain Table");
        const auto* property = table->extension_properties.find(model::PropertyId::from_name("FirstInGroup"));
        expect(first_in_group.value_or(false) ? property != nullptr && std::get<bool>(property->value)
                                              : property == nullptr,
            "fresh binary must retain true and normalize explicit/omitted false");
        expect(!std::get<bool>(table->properties().find(model::PropertyId::from_name("ReadOnly"))->value),
            "ReadOnly=false must coexist with FirstInGroup");
        const auto& columns = std::get<model::TablePayload>(table->payload).columns;
        expect(columns.size() == 3 && columns[0].control.kind == model::ControlKind::input_field &&
                   columns[1].control.kind == model::ControlKind::choice_field &&
                   columns[2].control.kind == model::ControlKind::check_box,
            "FirstInGroup must preserve all three supported editor profiles");
        auto encoded = form_stream::encode_document(loaded.value());
        expect(encoded.ok(), "fresh loaded Table must encode");
        const auto* record = find_record_with_guid(encoded.value(),
            model::metamodel::descriptor_for(model::ControlKind::table).guid);
        expect(record != nullptr && record->items[4].items[5].atom == (first_in_group.value_or(false) ? "1" : "0"),
            "fresh binary must use the observed Table metadata position");
        if (!first_in_group) omitted_record = *record;
        if (first_in_group && !*first_in_group) {
            expect(omitted_record && list_stream::dump_compact(*omitted_record) == list_stream::dump_compact(*record),
                "explicit false and omitted FirstInGroup must have identical Table records");
        }
    }

    auto invalid_type = make_document(true);
    auto& table = const_cast<model::ControlNode&>(*invalid_type.find_control(model::ObjectId{3}));
    table.extension_properties.set_explicit(model::PropertyId::from_name("FirstInGroup"), std::string("true"));
    expect(!form_stream::encode_document(invalid_type), "non-Boolean FirstInGroup must be rejected");
    auto unsupported = make_document(true);
    auto& other = const_cast<model::ControlNode&>(*unsupported.find_control(model::ObjectId{3}));
    other.extension_properties.set_explicit(model::PropertyId::from_name("SkipOnInput"), false);
    expect(!form_stream::encode_document(unsupported),
        "unproven Table extensions must be rejected even at their apparent default");
}

void test_table_column_name_and_data_path_runtime_slots() {
    auto literal = captured_table_payload();
    auto* table_record = find_record_with_guid(
        literal, model::metamodel::descriptor_for(model::ControlKind::table).guid);
    expect(table_record != nullptr, "independent captured fixture must contain Table");
    auto& column_body = table_record->items[2].items[2].items[1].items[23].items[1].items[1];
    auto& column_properties = column_body.items[1].items[1];
    // Независимые чтения свойств платформой дают Name=Code и Data=Choice для этих позиций.
    column_properties.items[30] = list_stream::ListValue::string_atom("Code");
    column_body.items[2] = list_stream::ListValue::string_atom("Choice");
    auto decoded = form_stream::decode_document(literal, "LiteralTableColumnSlots");
    expect(decoded.ok(), "getter-derived literal Name/Data slots must decode independently of the writer");
    auto table = std::find_if(decoded.value().collections().controls.begin(),
        decoded.value().collections().controls.end(), [](const auto& control) {
            return control.kind() == model::ControlKind::table;
        });
    expect(table != decoded.value().collections().controls.end(), "literal model must retain its Table");
    auto& named_column = std::get<model::TablePayload>(const_cast<model::ControlNode&>(*table).payload).columns.front();
    expect(named_column.name == "Code" && named_column.data_path == "Choice",
        "Column Name must decode from property slot 30 and DataPath from outer body slot 2");

    named_column.name = "Choice";
    named_column.data_path = "Code";
    auto encoded = form_stream::encode_document(decoded.value());
    expect(encoded.ok(), "independent named Column alias must serialize");
    table_record = find_record_with_guid(
        encoded.value(), model::metamodel::descriptor_for(model::ControlKind::table).guid);
    expect(table_record != nullptr, "serialized alias must retain its Table");
    const auto& encoded_body = table_record->items[2].items[2].items[1].items[23].items[1].items[1];
    const auto& encoded_name = encoded_body.items[1].items[1].items[30];
    const auto& encoded_data_path = encoded_body.items[2];
    expect(!encoded_name.is_list && encoded_name.atom_kind == list_stream::ListValue::AtomKind::string &&
               encoded_name.atom == "Choice",
        "writer must put the independently named Column in property slot 30");
    expect(!encoded_data_path.is_list && encoded_data_path.atom_kind == list_stream::ListValue::AtomKind::string &&
               encoded_data_path.atom == "Code",
        "writer must put the source DataPath in outer body slot 2 without replacing the alias");
}

void test_table_column_choice_and_check_box_profiles() {
    const auto make_document = [](model::ControlKind invalid_kind = model::ControlKind::input_field,
                                  bool mismatched_property = false) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "TableEditors";
        form.children = {model::ControlRef{model::ObjectId{3}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue value_table_type;
        model::TypeDomainEntry value_table_entry;
        value_table_entry.term = model::TypeDomainTerm::value_table;
        value_table_type.entries.push_back(value_table_entry);
        document.add_attribute(model::Attribute{model::ObjectId{2}, "Rows", value_table_type});
        model::ControlNode table{model::ObjectId{3}, "Rows", model::TablePayload{}};
        table.data_path = model::DataPath{model::AttributeRef{model::ObjectId{2}}, {}};
        auto& columns = std::get<model::TablePayload>(table.payload).columns;
        const auto add_column = [&](std::string name, model::ControlKind kind) {
            model::TableColumn column;
            column.name = std::move(name);
            column.data_path = "Code";
            column.header.items.push_back({"en", column.name});
            column.control.kind = kind;
            columns.push_back(std::move(column));
        };
        add_column("InputCode", model::ControlKind::input_field);
        add_column("ChoiceCode", model::ControlKind::choice_field);
        add_column("CheckCode", model::ControlKind::check_box);
        if (mismatched_property) {
            columns[1].control.properties.set_explicit(model::PropertyId::from_name("ReadOnly"), false);
        }
        if (invalid_kind != model::ControlKind::input_field) columns[0].control.kind = invalid_kind;
        document.add_control(std::move(table));
        return document;
    };

    const auto document = make_document();
    auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "three named default Table editors must encode" :
        encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message);
    auto roundtrip = form_stream::decode_document(encoded.value(), "TableEditorProfiles");
    expect(roundtrip.ok(), roundtrip ? "three named default Table editors must decode" :
        roundtrip.diagnostics().front().path + ": " + roundtrip.diagnostics().front().message);
    const auto table_after = std::find_if(roundtrip.value().collections().controls.begin(),
        roundtrip.value().collections().controls.end(), [](const auto& control) {
            return control.kind() == model::ControlKind::table;
        });
    expect(table_after != roundtrip.value().collections().controls.end(), "roundtrip must retain Table");
    const auto& columns_after = std::get<model::TablePayload>(table_after->payload).columns;
    expect(columns_after.size() == 3 && columns_after[0].control.kind == model::ControlKind::input_field &&
               columns_after[1].control.kind == model::ControlKind::choice_field &&
               columns_after[2].control.kind == model::ControlKind::check_box,
        "Table Column editor GUID and typed payload must roundtrip for all supported kinds");

    auto mismatched_guid = encoded.value();
    auto* table_record = find_record_with_guid(
        mismatched_guid, model::metamodel::descriptor_for(model::ControlKind::table).guid);
    expect(table_record != nullptr, "encoded form must contain Table record for mismatch checks");
    auto& first_column = table_record->items[2].items[2].items[1].items[23].items[1];
    first_column.items[1].items[1].items[1].items[38] = list_stream::ListValue::raw_atom(
        std::string(model::metamodel::descriptor_for(model::ControlKind::check_box).guid));
    expect(!form_stream::decode_document(mismatched_guid, "MismatchedTableEditor").ok(),
        "editor GUID must reject a packet whose nested payload belongs to a different kind");

    auto unknown_kind = encoded.value();
    table_record = find_record_with_guid(
        unknown_kind, model::metamodel::descriptor_for(model::ControlKind::table).guid);
    auto& unknown_column = table_record->items[2].items[2].items[1].items[23].items[1];
    unknown_column.items[1].items[1].items[1].items[38] = list_stream::ListValue::raw_atom(
        "00000000-0000-4000-8000-000000000000");
    expect(!form_stream::decode_document(unknown_kind, "UnknownTableEditor").ok(),
        "unknown Table Column editor GUID must fail closed");

    expect(!form_stream::encode_document(make_document(model::ControlKind::input_field, true)).ok(),
        "ChoiceField must reject a mismatched InputField property even at its default value");
    expect(!form_stream::encode_document(make_document(model::ControlKind::spreadsheet_document_field)).ok(),
        "unsupported Table Column editor kind must fail closed");
}

void expect_captured_table_rejected(list_stream::ListValue payload, std::string_view message) {
    expect(!form_stream::decode_document(payload, "CapturedTable"), message);
}

std::vector<std::uint8_t> compressed_capture_packet(const std::vector<std::uint8_t>& envelope, int level) {
    auto payload = captured_table_payload();
    auto packet = test_base64_decode(captured_column_packet(payload).items[0].items[0].atom.substr(8));
    packet.resize(18);
    const auto compressed = test_raw_deflate(envelope, level);
    packet.insert(packet.end(), compressed.begin(), compressed.end());
    return packet;
}

void test_captured_table_packet_rejections_and_alternate_deflate() {
    auto baseline = captured_table_payload();
    const auto encoded_chunks = captured_column_packet(baseline).items[0].items;
    std::string encoded;
    for (std::size_t index = 0; index < encoded_chunks.size(); ++index) {
        encoded += index == 0 ? encoded_chunks[index].atom.substr(8) : encoded_chunks[index].atom;
    }
    const auto packet = test_base64_decode(encoded);
    const auto envelope = test_raw_inflate(packet);

    auto alternate = captured_table_payload();
    const auto alternate_packet = compressed_capture_packet(envelope, Z_BEST_SPEED);
    expect(alternate_packet != packet, "alternate compressor must produce different captured packet bytes");
    set_captured_packet(alternate, alternate_packet);
    expect(form_stream::decode_document(alternate, "CapturedTable").ok(),
        "same nested InputField semantics with independent valid DEFLATE bytes must decode");

    auto bad_header = captured_table_payload();
    auto changed_packet = packet;
    changed_packet[0] ^= 1;
    set_captured_packet(bad_header, changed_packet);
    expect_captured_table_rejected(std::move(bad_header), "wrong editor packet header must fail");

    auto bad_deflate = captured_table_payload();
    changed_packet = packet;
    changed_packet[18 + 3] ^= 0xff;
    set_captured_packet(bad_deflate, changed_packet);
    expect_captured_table_rejected(std::move(bad_deflate), "corrupt editor DEFLATE data must fail");

    auto truncated = captured_table_payload();
    changed_packet = packet;
    changed_packet.pop_back();
    set_captured_packet(truncated, changed_packet);
    expect_captured_table_rejected(std::move(truncated), "truncated editor DEFLATE data must fail");

    auto trailing = captured_table_payload();
    changed_packet = packet;
    changed_packet.push_back(0);
    set_captured_packet(trailing, changed_packet);
    expect_captured_table_rejected(std::move(trailing), "trailing bytes after editor DEFLATE stream must fail");

    auto wrong_length = envelope;
    wrong_length[0] ^= 1;
    auto bad_length_payload = captured_table_payload();
    set_captured_packet(bad_length_payload, compressed_capture_packet(wrong_length, Z_DEFAULT_COMPRESSION));
    expect_captured_table_rejected(std::move(bad_length_payload), "incorrect editor envelope length must fail");

    auto wrong_bom = envelope;
    wrong_bom[8] ^= 1;
    auto bad_bom_payload = captured_table_payload();
    set_captured_packet(bad_bom_payload, compressed_capture_packet(wrong_bom, Z_DEFAULT_COMPRESSION));
    expect_captured_table_rejected(std::move(bad_bom_payload), "incorrect editor envelope BOM must fail");

    auto unsupported_pair = envelope;
    const std::string_view editor_text(reinterpret_cast<const char*>(unsupported_pair.data() + 11),
        unsupported_pair.size() - 11);
    auto editor = list_stream::parse(editor_text);
    editor.items[3] = list_stream::ListValue::list({list_stream::ListValue::raw_atom("1")});
    const std::string changed_text = list_stream::dump_listout(editor);
    const std::size_t text_size = changed_text.size();
    const std::uint64_t envelope_size = static_cast<std::uint64_t>(text_size) + 3;
    unsupported_pair.resize(sizeof(std::uint64_t));
    for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
        unsupported_pair[i] = static_cast<std::uint8_t>(envelope_size >> (i * 8));
    }
    unsupported_pair.insert(unsupported_pair.end(), {0xef, 0xbb, 0xbf});
    unsupported_pair.insert(unsupported_pair.end(), changed_text.begin(), changed_text.end());
    auto bad_pair_payload = captured_table_payload();
    set_captured_packet(bad_pair_payload, compressed_capture_packet(unsupported_pair, Z_DEFAULT_COMPRESSION));
    expect_captured_table_rejected(std::move(bad_pair_payload), "unsupported paired InputField editor data must fail");

    auto bad_packet_field = captured_table_payload();
    captured_column_packet(bad_packet_field).items[1] = list_stream::ListValue::raw_atom("1");
    expect_captured_table_rejected(std::move(bad_packet_field), "unsupported second packet field must fail");

    auto bad_count = captured_table_payload();
    auto* table = find_record_with_guid(
        bad_count, model::metamodel::descriptor_for(model::ControlKind::table).guid);
    expect(table != nullptr, "captured Table must resolve for count mutation");
    table->items[2].items[2].items[1].items[23].items[0] = list_stream::ListValue::raw_atom("2");
    expect_captured_table_rejected(std::move(bad_count), "Column collection count mismatch must fail");
}

void test_outer_format_probe() {
    const auto format27 = form_stream::probe_outer_format(list_stream::parse("{27}"));
    expect(
        format27 && format27.value() == form_stream::OuterFormat::v27,
        "outer format 27 must probe");
    const auto format26 = form_stream::probe_outer_format(list_stream::parse("{26}"));
    expect(
        format26 && format26.value() == form_stream::OuterFormat::v26,
        "outer format 26 must probe");

    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{28}")),
        "OOF1111",
        "$/0",
        "unknown outer format must be rejected");
    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{}")),
        "OOF1110",
        "$/0",
        "missing outer format must be rejected");
    expect_failure(
        form_stream::probe_outer_format(list_stream::parse("{\"27\"}")),
        "OOF1104",
        "$/0",
        "quoted outer format must be rejected");
}

void test_layout_probe() {
    const auto section16 = form_stream::probe_layout(
        layout_fixture(form_stream::LayoutKind::form_section_16));
    expect(section16.ok(), "form section 16 layout must probe");
    expect(
        section16.value() == form_stream::StorageLayout{
            form_stream::OuterFormat::v27,
            form_stream::LayoutKind::form_section_16,
            20,
            16,
            11,
            3,
            3,
        },
        "form section 16 layout descriptor mismatch");

    const auto section18 = form_stream::probe_layout(
        layout_fixture(form_stream::LayoutKind::form_section_18));
    expect(section18.ok(), "form section 18 layout must probe");
    expect(
        section18.value() == form_stream::StorageLayout{
            form_stream::OuterFormat::v27,
            form_stream::LayoutKind::form_section_18,
            20,
            18,
            14,
            10,
            11,
        },
        "form section 18 layout descriptor mismatch");

    expect_failure(
        form_stream::probe_layout(list_stream::parse("{26}")),
        "OOF1112",
        "$/0",
        "outer format without a proven nested layout must be rejected");

    auto wrong_root_arity = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_root_arity.items.pop_back();
    expect_failure(
        form_stream::probe_layout(wrong_root_arity),
        "OOF1102",
        "$",
        "root arity drift must be rejected");

    auto wrong_version = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_version.items[1].items[0] = list_stream::ListValue::raw_atom("17");
    expect_failure(
        form_stream::probe_layout(wrong_version),
        "OOF1113",
        "$/1/0",
        "unknown form section version must be rejected");

    auto wrong_form_arity = layout_fixture(form_stream::LayoutKind::form_section_16);
    wrong_form_arity.items[1].items.push_back(list_stream::ListValue::raw_atom("0"));
    expect_failure(
        form_stream::probe_layout(wrong_form_arity),
        "OOF1102",
        "$/1",
        "form section arity drift must be rejected");

    auto wrong_page_style_version = layout_fixture(form_stream::LayoutKind::form_section_18);
    wrong_page_style_version.items[13].items[0] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(
        form_stream::probe_layout(wrong_page_style_version),
        "OOF1106",
        "$/13/0",
        "page-style section version drift must be rejected");
}

void test_runtime_envelope() {
    constexpr std::string_view uuid = "01234567-89ab-cdef-0123-456789abcdef";
    const auto payload = layout_fixture(form_stream::LayoutKind::form_section_18);
    const std::string source = runtime_envelope_text(payload);
    const auto decoded = form_stream::decode_runtime_envelope(source);
    expect(decoded.ok(), "runtime envelope must decode");
    expect(decoded.value().runtime_uuid.canonical == uuid, "runtime UUID must decode");
    const auto encoded = form_stream::encode_runtime_envelope(decoded.value());
    expect(encoded && encoded.value() == source, "runtime envelope must encode deterministically");

    const auto with_bom = form_stream::decode_runtime_envelope("\xef\xbb\xbf" + source);
    expect(with_bom.ok(), "runtime envelope must accept a UTF-8 BOM from TextDocument");
    const auto encoded_without_bom = form_stream::encode_runtime_envelope(with_bom.value());
    expect(
        encoded_without_bom && encoded_without_bom.value() == source,
        "runtime envelope encoding must be canonical without a BOM");

    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"!\"," + std::string(uuid) + "," +
            list_stream::dump_compact(payload) + "}"),
        "OOF1106",
        "$/0",
        "wrong runtime marker must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"#\",not-a-guid," + list_stream::dump_compact(payload) + "}"),
        "OOF1105",
        "$/1",
        "malformed runtime UUID must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope("{\"#\",01234567-89ab-cdef-0123-456789abcdef,{99}}"),
        "OOF1111",
        "$/2/0",
        "unknown payload format must be rejected");
    expect_failure(
        form_stream::decode_runtime_envelope(
            "{\"#\",01234567-89ab-cdef-0123-456789abcdef,{26}}"),
        "OOF1112",
        "$/2/0",
        "runtime envelope must reject an outer format without a proven layout");
}

void test_attributes() {
    const auto empty = form_stream::decode_attributes(list_stream::parse("{{-1},3,{0},{0}}"));
    expect(empty.ok(), "empty attribute record must decode");
    expect(empty.value().slot_count == 3, "empty attribute slot count mismatch");
    expect(empty.value().attributes.empty(), "empty attribute table mismatch");
    expect(empty.value().links.empty(), "empty attribute-link table mismatch");

    constexpr std::string_view fixture =
        "{{-1},2,{1,{{1,01234567-89ab-cdef-0123-456789abcdef},1,0,1,\"Value\","
        "{\"Pattern\",{\"T\",d47d59f8-73f0-481c-8b5e-f6384c0a4804}}}},"
        "{1,{42,{1,{1,01234567-89ab-cdef-0123-456789abcdef}}}}}";
    const auto decoded = form_stream::decode_attributes(list_stream::parse(fixture));
    expect(decoded.ok(), "attribute fixture must decode");
    expect(decoded.value().slot_count == 2, "attribute slot count mismatch");
    expect(decoded.value().attributes.size() == 1, "attribute count mismatch");
    expect(decoded.value().attributes[0].name == "Value", "attribute name mismatch");
    expect(decoded.value().attributes[0].main, "attribute main flag mismatch");
    expect(decoded.value().links.size() == 1, "attribute link count mismatch");
    expect(decoded.value().links[0].control_id == 42, "attribute link control mismatch");

    const auto encoded = form_stream::encode_attributes(decoded.value());
    expect(encoded.ok(), "attribute fixture must encode");
    expect(list_stream::dump_compact(encoded.value()) == fixture, "attribute fixture must be canonical");
    const auto rebuilt = form_stream::decode_attributes(encoded.value());
    expect(rebuilt && rebuilt.value() == decoded.value(), "attribute DTO must round-trip");

    constexpr std::string_view one_component_fixture =
        "{{-1},4,{1,{{3},1,0,1,\"Value\",{\"Pattern\",{\"S\",10,1}}}},"
        "{1,{3,{1,{3}}}}}";
    const auto one_component = form_stream::decode_attributes(
        list_stream::parse(one_component_fixture));
    expect(one_component.ok(), "platform one-component attribute ID must decode");
    expect(
        one_component.value().attributes.front().id.object_id == 3 &&
            one_component.value().attributes.front().id.is_null,
        "one-component attribute ID semantics mismatch");
    const auto one_component_encoded = form_stream::encode_attributes(one_component.value());
    expect(one_component_encoded.ok(), "one-component attribute ID must encode");
    expect(
        list_stream::dump_compact(one_component_encoded.value()) == one_component_fixture,
        "one-component attribute record must remain canonical");

    expect_failure(
        form_stream::decode_attributes(list_stream::parse("{{-1},0,{1},{0}}")),
        "OOF1102",
        "$/2/2",
        "attribute count mismatch must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse("{{1},0,{0},{0}}")),
        "OOF1107",
        "$/2/0",
        "dangling main Attribute reference must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse(
            "{{-1},2,{1,{{1,01234567-89ab-cdef-0123-456789abcdef},1,0,1,\"Value\","
            "{\"Other\"}}},{0}}")),
        "OOF1108",
        "$/2/2/1/5",
        "malformed attribute type must be rejected");
    expect_failure(
        form_stream::decode_attributes(list_stream::parse(
            "{{-1},0,{0},{1,{-1,{1,{0}}}}}")),
        "OOF1105",
        "$/2/3/1/0",
        "negative control ID must be rejected");
}

void test_attribute_encode_validation() {
    form_stream::AttributesRecord invalid;
    invalid.attributes.push_back(form_stream::AttributeRecord{
        composite(1, "not-a-guid"),
        false,
        false,
        "Value",
        {},
    });
    expect_failure(
        form_stream::encode_attributes(invalid),
        "OOF1107",
        "$/2/2/1/0",
        "encoder must reject malformed CompositeID UUID");
}

void test_empty_attributes_allocator_header() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children.push_back(model::ControlRef{model::ObjectId{10}});
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{
        model::ObjectId{10},
        "Run",
        model::ButtonPayload{},
    });

    auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "high-ID Button must encode using the current writer allocation count" :
        encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message + " expected=" +
        encoded.diagnostics().front().expected + " actual=" + encoded.diagnostics().front().actual);
    expect(
        encoded.value().items[2].items[1].atom == "11",
        "current writer allocation count must be derived from the high object ID");

    auto fresh_designer = encoded.value();
    fresh_designer.items[2].items[1] = list_stream::ListValue::raw_atom("1");
    const auto decoded = form_stream::decode_document(fresh_designer, "Main");
    expect(decoded.ok(), "empty attribute allocation header from fresh Designer must decode");
    expect(
        decoded.value().collections().controls.front().id == model::ObjectId{10},
        "empty attribute allocation header must not constrain Button IDs");

    auto designer_three_slots = encoded.value();
    designer_three_slots.items[2].items[1] = list_stream::ListValue::raw_atom("3");
    const auto decoded_three_slots = form_stream::decode_document(designer_three_slots, "Main");
    expect(decoded_three_slots.ok(),
        "empty attribute allocation header with three Designer slots must decode above control ID 2");
    expect(form_stream::encode_document(decoded_three_slots.value()).value().items[2].items[1].atom == "11",
        "empty three-slot header must normalize to the writer allocation count");

    const auto normalized = form_stream::encode_document(decoded.value());
    expect(normalized.ok(), "decoded fresh-Designer form must encode");
    expect(
        normalized.value().items[2].items[1].atom == "11",
        "empty attribute allocation header must normalize to the current writer value");

    auto unknown_slot_count = encoded.value();
    unknown_slot_count.items[2].items[1] = list_stream::ListValue::raw_atom("2");
    expect_failure(
        form_stream::decode_document(unknown_slot_count, "Main"),
        "OOF1114",
        "$/2/1",
        "unsupported empty attribute allocation header must be rejected");
}

void test_attribute_allocator_is_separate_from_control_ids() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children.push_back(model::ControlRef{model::ObjectId{4}});
    model::OrdinaryFormDocument document(std::move(form));
    document.add_attribute(model::Attribute{
        model::ObjectId{1},
        "Value",
        {},
    });
    document.add_control(model::ControlNode{
        model::ObjectId{4},
        "Run",
        model::ButtonPayload{},
    });

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "attribute and control ID spaces must encode independently");
    expect(encoded.value().items[1].items[1].items[1].atom == "4",
        "form header max ID must include control ID 4");
    expect(encoded.value().items[2].items[1].atom == "3",
        "attribute slot count must use attribute ID 1, not control ID 4");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "attribute ID 1 and control ID 4 must decode with slot count 3");

    auto wrong_attribute_slots = encoded.value();
    wrong_attribute_slots.items[2].items[1] = list_stream::ListValue::raw_atom("5");
    expect_failure(
        form_stream::decode_document(wrong_attribute_slots, "Main"),
        "OOF1114",
        "$/2/1",
        "nonempty attribute slot count must be checked in its own ID space");
}

void test_usual_group_named_record_round_trip_and_rejections() {
    model::Form fresh_form;
    fresh_form.id = model::ObjectId{1};
    fresh_form.name = "Fresh";
    fresh_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument fresh_document(std::move(fresh_form));
    fresh_document.add_control(model::ControlNode{model::ObjectId{2}, "FreshGroup", model::UsualGroupPayload{}});
    const auto fresh_encoded = form_stream::encode_document(fresh_document);
    expect(fresh_encoded.ok(), "fresh default UsualGroup must encode");
    auto fresh_payload = fresh_encoded.value();
    const auto* fresh_record = find_usual_group_record(fresh_payload);
    const auto independent_add_record = list_stream::parse(
        R"({90db814a-c75f-4b54-bc96-df62e554d67d,2,{0,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,4,700,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},8,{1,0},{3,0,{0},6,1,0,cf48d3ca-5bd4-45b9-bb8f-a0922a8335f2},0}},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,0,1,0,0},{14,"FreshGroup",4294967295,0,0,0},{0}})");
    expect(fresh_record && list_stream::dump_compact(*fresh_record) == list_stream::dump_compact(independent_add_record),
        "full canonical record must match independent fresh runtime Add capture");

    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode defaults{model::ObjectId{2}, "DefaultGroup", model::UsualGroupPayload{}};
    defaults.position.left.set(10);
    defaults.position.top.set(12);
    defaults.position.width.set(140);
    defaults.position.height.set(60);
    document.add_control(std::move(defaults));
    model::ControlNode custom{model::ObjectId{3}, "CustomGroup", model::UsualGroupPayload{}};
    custom.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Группа Ω"));
    custom.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    custom.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::string("Подсказка"));
    custom.position.left.set(20);
    custom.position.top.set(30);
    custom.position.width.set(150);
    custom.position.height.set(70);
    custom.position.visible.set(false);
    document.add_control(std::move(custom));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "named UsualGroup default and Unicode properties must encode");
    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "named UsualGroup records must decode");
    const auto& controls = decoded.value().collections().controls;
    expect(controls.size() == 2 && controls[0].kind() == model::ControlKind::usual_group &&
        controls[1].kind() == model::ControlKind::usual_group, "both records must materialize as UsualGroup");
    expect(controls[0].properties().find(model::PropertyId::from_name("Caption")) == nullptr &&
        controls[0].properties().find(model::PropertyId::from_name("Enabled")) == nullptr &&
        controls[0].properties().find(model::PropertyId::from_name("ToolTip")) == nullptr,
        "default UsualGroup properties must stay implicit");
    const auto* caption = controls[1].properties().find(model::PropertyId::from_name("Caption"));
    const auto* enabled = controls[1].properties().find(model::PropertyId::from_name("Enabled"));
    const auto* tool_tip = controls[1].properties().find(model::PropertyId::from_name("ToolTip"));
    expect(caption && std::get<std::string>(caption->value) == "Группа Ω" && enabled && !std::get<bool>(enabled->value) &&
        tool_tip && std::get<std::string>(tool_tip->value) == "Подсказка", "named properties must survive round-trip");
    expect(controls[1].position.left.value() == 20 && controls[1].position.width.value() == 150 &&
        !controls[1].position.visible.value(), "Position and Visible must survive round-trip");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "full UsualGroup records must round-trip without drift");

    for (const auto mutation : {0, 1, 2, 3, 4, 5}) {
        auto invalid = encoded.value();
        auto* record = find_usual_group_record(invalid);
        expect(record != nullptr, "encoded UsualGroup record must be locatable");
        if (mutation == 0) record->items[2].items[0] = list_stream::ListValue::raw_atom("1");
        if (mutation == 1) record->items[2].items[1].items[1] = list_stream::ListValue::raw_atom("9");
        if (mutation == 2) record->items[2].items[1].items[3] = list_stream::ListValue::list({list_stream::ListValue::raw_atom("0")});
        if (mutation == 3) record->items[5] = list_stream::ListValue::list({list_stream::ListValue::raw_atom("1")});
        if (mutation == 4) record->items[2].items[1].items[0].items[0] = list_stream::ListValue::raw_atom("18");
        if (mutation == 5) record->items[2].items[1].items[4] = list_stream::ListValue::raw_atom("1");
        const auto varied = form_stream::decode_document(invalid, "Main");
        if (mutation == 2 || mutation == 5) {
            expect(varied.ok() && !varied.value().reconstruction_complete() &&
                       !varied.diagnostics().empty() && varied.diagnostics().front().code == "OOF1140" &&
                       varied.diagnostics().front().severity == oof::DiagnosticSeverity::warning,
                "valid unknown UsualGroup profile values must warn and preserve a partial named model");
            expect(varied.value().collections().controls.size() == 2 &&
                       varied.value().collections().controls[0].name == "DefaultGroup",
                "known UsualGroup identity must survive an unknown profile value");
        } else {
            expect(!varied, "malformed UsualGroup structure and invalid known values must still fail, mutation " +
                std::to_string(mutation));
        }
    }

    model::Form nested_form;
    nested_form.id = model::ObjectId{1};
    nested_form.name = "Nested";
    nested_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument nested(std::move(nested_form));
    model::ControlNode group{model::ObjectId{2}, "Group", model::UsualGroupPayload{}};
    group.children.push_back(model::ControlRef{model::ObjectId{3}});
    nested.add_control(std::move(group));
    nested.add_control(model::ControlNode{model::ObjectId{3}, "Child", model::ButtonPayload{}});
    expect(!form_stream::encode_document(nested), "unverified UsualGroup nesting must be rejected");

    model::Form event_form;
    event_form.id = model::ObjectId{1}; event_form.name = "Event";
    event_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument event_document(std::move(event_form));
    model::ControlNode event_group{model::ObjectId{2}, "Group", model::UsualGroupPayload{}};
    event_group.events.push_back(model::EventRef{model::ObjectId{3}});
    event_document.add_control(std::move(event_group));
    event_document.add_event(model::Event{model::ObjectId{3}, "Unknown", "Handler", model::ControlRef{model::ObjectId{2}}});
    expect(!form_stream::encode_document(event_document), "unverified UsualGroup events must be rejected");

    model::Form property_form;
    property_form.id = model::ObjectId{1}; property_form.name = "Property";
    property_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument property_document(std::move(property_form));
    model::ControlNode property_group{model::ObjectId{2}, "Group", model::UsualGroupPayload{}};
    property_group.properties().set_explicit(model::PropertyId::from_name("Transparent"), true);
    property_document.add_control(std::move(property_group));
    expect(!form_stream::encode_document(property_document), "unverified UsualGroup properties must be rejected");

    model::Form id_form;
    id_form.id = model::ObjectId{1}; id_form.name = "InvalidGroupId";
    const auto invalid_id = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
    id_form.children = {model::ControlRef{model::ObjectId{invalid_id}}};
    model::OrdinaryFormDocument id_document(std::move(id_form));
    id_document.add_control(model::ControlNode{model::ObjectId{invalid_id}, "Group", model::UsualGroupPayload{}});
    expect_failure(form_stream::encode_document(id_document), "OOF1122", "$/UsualGroup/ID",
        "UsualGroup IDs above int64 range must be rejected by the control encoder");
}

void test_gantt_chart_named_storage_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Gantt";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::GanttChartPayload payload;
    payload.series.push_back({model::ObjectId{71}, "S-A", "Series A", std::nullopt});
    payload.series.push_back({model::ObjectId{74}, "S-B", "Series B", std::nullopt});
    payload.points.push_back({model::ObjectId{83}, "P-X", "Point X", std::nullopt});
    payload.points.push_back({model::ObjectId{84}, "P-Y", "Point Y", std::nullopt});
    payload.points.push_back({model::ObjectId{85}, "P-Z", "Point Z", std::nullopt});
    payload.intervals.push_back({model::ObjectId{83}, model::ObjectId{71},
        model::DateValue{"2027-01-10T00:00:00"}, model::DateValue{"2027-01-12T00:00:00"}, "First"});
    payload.intervals.push_back({model::ObjectId{83}, model::ObjectId{71},
        model::DateValue{"2027-01-14T00:00:00"}, model::DateValue{"2027-01-16T00:00:00"}, "Second"});
    model::ControlNode gantt{model::ObjectId{2}, "Schedule", std::move(payload)};
    gantt.properties().set_explicit(model::PropertyId::from_name("AutoFullInterval"), false);
    gantt.properties().set_explicit(model::PropertyId::from_name("FullIntervalBegin"), model::DateValue{"2027-01-01T00:00:00"});
    gantt.properties().set_explicit(model::PropertyId::from_name("FullIntervalEnd"), model::DateValue{"2027-03-01T00:00:00"});
    gantt.position.left.set(8);
    gantt.position.top.set(8);
    gantt.position.width.set(400);
    gantt.position.height.set(240);
    document.add_control(std::move(gantt));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message + " expected=" + encoded.diagnostics().front().expected + " actual=" + encoded.diagnostics().front().actual);
    const auto decoded = form_stream::decode_document(encoded.value(), "Gantt");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message + " expected=" + decoded.diagnostics().front().expected + " actual=" + decoded.diagnostics().front().actual);
    const auto* control = decoded.value().find_control(model::ObjectId{2});
    const auto* restored = control ? std::get_if<model::GanttChartPayload>(&control->payload) : nullptr;
    expect(restored && restored->series.size() == 2 && restored->points.size() == 3 && restored->intervals.size() == 2,
        "Gantt storage must retain named dimensions and repeated interval pair");
    expect(restored->series[0].id == model::ObjectId{71} && restored->points[0].id == model::ObjectId{83} &&
               restored->intervals[0].text == "First" && restored->intervals[1].text == "Second",
        "Gantt native keys and ordered interval records must return to their named model objects");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "Gantt native records must roundtrip without private-field drift");

    auto reordered = encoded.value();
    const auto find_gantt_record = [&](auto&& self, list_stream::ListValue& value) -> list_stream::ListValue* {
        if (!value.is_list) return nullptr;
        const auto& descriptor = model::metamodel::descriptor_for(model::ControlKind::gantt_chart);
        if (value.items.size() == 6 && !value.items[0].is_list && value.items[0].atom == descriptor.guid)
            return &value;
        for (auto& child : value.items) {
            if (auto* found = self(self, child)) return found;
        }
        return nullptr;
    };
    auto palette_fixture = encoded.value();
    auto* palette_gantt = find_gantt_record(find_gantt_record, palette_fixture);
    expect(palette_gantt != nullptr, "encoded document must contain its Gantt record for palette validation");
    const auto& point_rows = palette_gantt->items[2].items[2].items[1];
    const auto& point_palette = point_rows.items[11];
    expect(list_stream::dump_compact(point_palette.items[2].items[2]) == "{4,4,{0},4}" &&
               list_stream::dump_compact(point_palette.items[2].items[3]) == "{4,4,{0},4}",
        "Gantt Point palette footer colors must use the proven Auto color defaults");
    const auto& series_rows = palette_gantt->items[2].items[3].items[1];
    const auto& series_palette = series_rows.items[9];
    expect(list_stream::dump_compact(series_palette.items[2].items[2]) == "{4,0,{0},0}",
        "Gantt Series palette footer must retain the proven absolute-black default");
    auto* gantt_record = find_gantt_record(find_gantt_record, reordered);
    expect(gantt_record != nullptr, "encoded document must contain its Gantt record");
    for (const std::size_t table_slot : {2u, 3u}) {
        auto& rows = gantt_record->items[2].items[table_slot].items[1].items;
        const auto dimensions = static_cast<std::size_t>(std::stoul(rows[2].atom)) - 1;
        for (std::size_t left = 0; left < (dimensions + 1) / 2; ++left) {
            const auto right = dimensions - left;
            std::swap(rows[3 + 2 * left], rows[3 + 2 * right]);
            std::swap(rows[4 + 2 * left], rows[4 + 2 * right]);
        }
    }
    const auto reordered_decode = form_stream::decode_document(reordered, "GanttReorderedRows");
    expect(reordered_decode.ok(), reordered_decode ? "" : reordered_decode.diagnostics().front().path + ": " + reordered_decode.diagnostics().front().message);

    auto wrong_default = reordered;
    auto* wrong_default_gantt = find_gantt_record(find_gantt_record, wrong_default);
    wrong_default_gantt->items[2].items[3].items[1].items[4].items[1].items[10] =
        list_stream::ListValue::raw_atom("0");
    expect(!form_stream::decode_document(wrong_default, "GanttCorruptDefaultRow").ok(),
        "Gantt reordered default row must still match its exact sentinel contract");

    auto platform_layout = encoded.value();
    auto* platform_gantt = find_gantt_record(find_gantt_record, platform_layout);
    expect(platform_gantt != nullptr, "platform layout fixture must contain its Gantt record");
    auto& layout_values = platform_gantt->items[2].items[1].items[2].items;
    constexpr std::array<std::pair<std::size_t, std::string_view>, 8> layout_adjustments{{
        {102, "1.4375e-1"}, {104, "8.5625e-1"}, {106, "0"}, {107, "0"},
        {167, "0.14375"}, {169, "0.85625"}, {171, "0"}, {172, "0"}}};
    for (const auto& [slot, value] : layout_adjustments) {
        expect(slot < layout_values.size(), "platform layout fixture must expose every verified runtime-adjusted slot");
        layout_values[slot] = list_stream::ListValue::raw_atom(std::string(value));
    }
    const auto platform_layout_decode = form_stream::decode_document(platform_layout, "GanttPlatformLayout");
    expect(platform_layout_decode.ok(), platform_layout_decode ? "" : platform_layout_decode.diagnostics().front().path + ": " + platform_layout_decode.diagnostics().front().message);
    const auto platform_layout_roundtrip = form_stream::encode_document(platform_layout_decode.value());
    expect(platform_layout_roundtrip.ok(), "named Gantt graph must rebuild after platform layout normalization");

    auto invalid_platform_layout = platform_layout;
    auto* invalid_layout_gantt = find_gantt_record(find_gantt_record, invalid_platform_layout);
    invalid_layout_gantt->items[2].items[1].items[2].items[102] = list_stream::ListValue::raw_atom("1.01");
    expect_failure(form_stream::decode_document(invalid_platform_layout, "GanttInvalidPlatformLayout"),
        "OOF1114", "$/1/2/2/1/2/1/2/102",
        "Gantt runtime-adjusted layout leaves outside the verified range must be rejected");

    model::Form color_form;
    color_form.id = model::ObjectId{1};
    color_form.name = "GanttColor";
    color_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument color_document(std::move(color_form));
    model::GanttChartPayload color_payload;
    color_payload.series.push_back({model::ObjectId{1}, "series", "Series",
        model::ColorValue{model::ColorKind::absolute, 10, 20, 30, 255, std::monostate{}}});
    model::ControlNode color_chart{model::ObjectId{2}, "Colored", std::move(color_payload)};
    color_chart.position.left.set(8);
    color_chart.position.top.set(8);
    color_chart.position.width.set(200);
    color_chart.position.height.set(100);
    color_document.add_control(std::move(color_chart));
    expect_failure(form_stream::encode_document(color_document), "OOF1122", "$/GanttChart/Color",
        "Gantt colors without a proved native palette mapping must be rejected instead of dropped");

    model::Form auto_form;
    auto_form.id = model::ObjectId{1};
    auto_form.name = "GanttAutoBounds";
    auto_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument auto_document(std::move(auto_form));
    model::ControlNode auto_chart{model::ObjectId{2}, "Auto", model::GanttChartPayload{}};
    auto_chart.properties().set_explicit(model::PropertyId::from_name("FullIntervalBegin"), model::DateValue{"2027-01-01T00:00:00"});
    auto_chart.properties().set_explicit(model::PropertyId::from_name("FullIntervalEnd"), model::DateValue{"2027-03-01T00:00:00"});
    auto_chart.position.left.set(8);
    auto_chart.position.top.set(8);
    auto_chart.position.width.set(200);
    auto_chart.position.height.set(100);
    auto_document.add_control(std::move(auto_chart));
    expect_failure(form_stream::encode_document(auto_document), "OOF1122", "$/GanttChart/FullInterval",
        "explicit bounds in automatic mode must not be silently omitted on decode");
}

void test_multiple_top_level_buttons_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.events = {model::EventRef{model::ObjectId{22}}};
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{7}},
        model::ControlRef{model::ObjectId{12}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    document.add_event(model::Event{model::ObjectId{22}, "OnClose", "ПробноеЗакрытие", model::FormRef{model::ObjectId{1}}});

    model::ControlNode first{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    first.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Запуск"));
    first.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    first.position.left.set(11);
    first.position.top.set(12);
    first.position.width.set(120);
    first.position.height.set(24);
    first.events.push_back(model::EventRef{model::ObjectId{20}});
    document.add_event(model::Event{model::ObjectId{20}, "Click", "RunHandler", model::ControlRef{model::ObjectId{2}}});
    document.add_control(std::move(first));

    model::ControlNode second{model::ObjectId{7}, "Cancel", model::ButtonPayload{}};
    second.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Отмена"));
    second.position.left.set(145);
    second.position.top.set(12);
    second.position.width.set(90);
    second.position.height.set(24);
    second.events.push_back(model::EventRef{model::ObjectId{21}});
    document.add_event(model::Event{model::ObjectId{21}, "Click", "CancelHandler", model::ControlRef{model::ObjectId{7}}});
    document.add_control(std::move(second));

    model::ControlNode third{model::ObjectId{12}, "Help", model::ButtonPayload{}};
    third.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Справка"));
    third.position.left.set(250);
    third.position.top.set(12);
    third.position.width.set(90);
    third.position.height.set(24);
    document.add_control(std::move(third));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded.ok() ? "two named top-level Buttons must encode" : encoded.diagnostics().front().message);

    // Independent literal from the single-setter platform oracle, evidence ev_b8c9b9931be1473193bac9c86ee1717d.
    const auto observed = list_stream::parse(
        R"({1,{70003,e1692cc2-605b-4535-84dd-28440238746c,{3,"ПробноеЗакрытие",{1,"",{1,0},{1,0},{1,0},{4,0,{0},"",-1,-1,1,0,""},{0,0,0}}}}})");
    expect(list_stream::dump_compact(encoded.value().items[4]) == list_stream::dump_compact(observed),
        "Form.OnClose must match the entire independently observed action table");
    auto captured = encoded.value();
    captured.items[4] = observed;
    const auto decoded = form_stream::decode_document(captured, "Main");
    expect(decoded.ok(), "three top-level Buttons must decode");
    const auto& controls = decoded.value().collections().controls;
    expect(controls.size() == 3, "all Buttons must materialize as named controls");
    expect(controls[0].id == model::ObjectId{2} && controls[0].name == "Run",
        "first Button identity and order must survive");
    expect(controls[1].id == model::ObjectId{7} && controls[1].name == "Cancel",
        "second Button identity and order must survive");
    expect(controls[2].id == model::ObjectId{12} && controls[2].name == "Help",
        "third Button identity and order must survive");
    expect(std::get<std::string>(controls[0].properties().find(model::PropertyId::from_name("Caption"))->value) == "Запуск",
        "first Button Caption must survive");
    expect(std::get<bool>(controls[0].properties().find(model::PropertyId::from_name("Enabled"))->value) == false,
        "first Button Enabled=false must survive");
    expect(std::get<std::string>(controls[1].properties().find(model::PropertyId::from_name("Caption"))->value) == "Отмена",
        "second Button Caption must survive");
    expect(std::get<std::string>(controls[2].properties().find(model::PropertyId::from_name("Caption"))->value) == "Справка",
        "third Button Caption must survive");
    expect(controls[0].position.left.value() == 11 && controls[1].position.left.value() == 145 &&
        controls[2].position.left.value() == 250,
        "each Button Position must survive independently");
    expect(controls[0].events.front().id() != controls[1].events.front().id() &&
        controls[0].events.front().id() > model::ObjectId{12} && controls[1].events.front().id() > model::ObjectId{12},
        "synthetic event IDs must be unique and above stored object IDs");
    expect(decoded.value().find_event(controls[0].events.front().id())->handler == "RunHandler" &&
        decoded.value().find_event(controls[1].events.front().id())->handler == "CancelHandler",
        "each Button Click handler must remain attached to its owner");
    const auto close_id = decoded.value().form().events.front().id();
    const auto* close_event = decoded.value().find_event(close_id);
    expect(close_id != controls[0].events.front().id() && close_id != controls[1].events.front().id() &&
        close_event->name == "OnClose" && close_event->handler == "ПробноеЗакрытие" &&
        std::get<model::FormRef>(close_event->owner).id() == decoded.value().form().id &&
        std::get<model::ControlRef>(decoded.value().find_event(controls[0].events.front().id())->owner).id() == controls[0].id &&
        std::get<model::ControlRef>(decoded.value().find_event(controls[1].events.front().id())->owner).id() == controls[1].id,
        "Form.OnClose and Button.Click must retain distinct IDs and exact owner references");
    const auto xml = source::serialize_form_xml(decoded.value());
    expect(xml.ok() && xml.value().find("<OnClose") != std::string::npos,
        "Form.OnClose must serialize as a named public XML event");
    const auto parsed = source::parse_form_xml(xml.value());
    expect(parsed.ok(), "named event XML must parse without a baseline");
    const auto rebuilt = form_stream::encode_document(parsed.value());
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(captured),
        "fresh named XML must rebuild the complete stream with both event owners");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded two-Button model must re-encode");
    expect(list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "three-Button storage must round-trip in order without drift");

    auto wrong_sibling_index = encoded.value();
    wrong_sibling_index.items[1].items[2].items[2].items[2].items[3].items[geometry_tail_start(wrong_sibling_index.items[1].items[2].items[2].items[2].items[3]) + 1] =
        list_stream::ListValue::raw_atom("0");
    expect_failure(
        form_stream::decode_document(wrong_sibling_index, "Main"),
        "OOF1114",
        "$/1/2/2/2/3/19",
        "Button geometry with an incorrect sibling index must be rejected");
}

void test_shared_action_metadata_policies_and_rejections() {
    model::Form form;
    form.id = model::ObjectId{1}; form.name = "Actions";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    form.events = {model::EventRef{model::ObjectId{3}}};
    model::OrdinaryFormDocument document(std::move(form));
    document.add_event(model::Event{model::ObjectId{3}, "OnClose", "Handler", model::FormRef{model::ObjectId{1}}});
    model::ControlNode button{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    button.events = {model::EventRef{model::ObjectId{4}}};
    document.add_event(model::Event{model::ObjectId{4}, "Click", "Handler", model::ControlRef{model::ObjectId{2}}});
    button.properties().set_explicit(model::PropertyId::from_name("MenuMode"), model::EnumerationValue{"MenuMode", "UseExtra"});
    model::CommandBarButton command; command.name = "Command"; command.action = model::CommandBarAction{"Handler", "", {}, {}, {}};
    std::get<model::ButtonPayload>(button.payload).buttons = {command};
    document.add_control(std::move(button));
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "coexisting form, button and menu actions must encode");
    const auto derived = list_stream::parse(
        R"({3,"Handler",{1,"Handler",{1,1,{"ru","Handler"}},{1,1,{"ru","Handler"}},{1,1,{"ru","Handler"}},{4,0,{0},"",-1,-1,1,0,""},{0,0,0}}})");
    const auto empty = list_stream::parse(
        R"({3,"Handler",{1,"",{1,0},{1,0},{1,0},{4,0,{0},"",-1,-1,1,0,""},{0,0,0}}})");
    // The independent action literals must stay distinct even for identical handlers.
    for (unsigned owner = 0; owner < 3; ++owner) {
        const auto action_at = [owner](auto& root) -> auto& {
            if (owner == 0) return root.items[4].items[1].items[2];
            auto& payload = root.items[1].items[2].items[2].items[1].items[2];
            if (owner == 1) return payload.items[2].items[1].items[2];
            return payload.items[1].items[12].items[5].items[4];
        };
        expect(list_stream::dump_compact(action_at(encoded.value())) ==
            list_stream::dump_compact(owner == 1 ? derived : empty),
            "each owner must retain its exact independent Action literal");
        auto cross_policy = encoded.value();
        action_at(cross_policy).items[2] = (owner == 1 ? empty : derived).items[2];
        if (owner == 0) {
            expect(!form_stream::decode_document(cross_policy, "Actions"), "Form.OnClose metadata from another owner policy must reject");
        } else if (owner == 1) {
            const auto partial_button = form_stream::decode_document(cross_policy, "Actions");
            expect(partial_button.ok() && !partial_button.value().reconstruction_complete() &&
                       std::any_of(partial_button.diagnostics().begin(), partial_button.diagnostics().end(),
                           [](const auto& diagnostic) {
                               return diagnostic.code == "OOF1140" &&
                                   diagnostic.severity == oof::DiagnosticSeverity::warning;
                           }) &&
                       partial_button.value().find_event(
                           partial_button.value().find_control(model::ObjectId{2})->events.front().id())->handler == "Handler",
                "valid Button action metadata differences must warn while retaining its handler");
        } else {
            const auto independent_menu_metadata = form_stream::decode_document(cross_policy, "Actions");
            const auto* decoded_button = independent_menu_metadata
                ? independent_menu_metadata.value().find_control(model::ObjectId{2}) : nullptr;
            const auto* decoded_payload = decoded_button
                ? std::get_if<model::ButtonPayload>(&decoded_button->payload) : nullptr;
            expect(decoded_payload && decoded_payload->buttons.front().action &&
                decoded_payload->buttons.front().action->name == "Handler" &&
                decoded_payload->buttons.front().action->text.items == std::vector<model::LocalizedStringItem>{{"ru", "Handler"}},
                "menu Action metadata must round-trip independently of event owner policies");
        }
        for (unsigned field = 0; field < 7; ++field) {
            auto bad = encoded.value();
            action_at(bad).items[2].items[field] = list_stream::ListValue::raw_atom("999");
            expect(!form_stream::decode_document(bad, "Actions"), "every metadata field must be checked for every owner");
        }
        for (unsigned variation = 0; variation < 7; ++variation) {
            auto bad = encoded.value(); auto& action = action_at(bad);
            switch (variation) {
                case 0: action.items[0] = list_stream::ListValue::raw_atom("4"); break;
                case 1: action.items[1] = list_stream::ListValue::string_atom(""); break;
                case 2: action.items[1] = list_stream::ListValue::raw_atom("123"); break;
                case 3: action.items.pop_back(); break;
                case 4: action.items.push_back(list_stream::ListValue::raw_atom("0")); break;
                case 5: action.items[2].items.pop_back(); break;
                case 6: action.items[2].items.push_back(list_stream::ListValue::raw_atom("0")); break;
            }
            expect(!form_stream::decode_document(bad, "Actions"), "tag, handler and arity guards must apply to all action owners");
        }
    }
    const auto decoded = form_stream::decode_document(encoded.value(), "Actions");
    expect(decoded.ok(), "all three exact Action profiles must decode together");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "the combined form, button and menu stream must round-trip without drift");
}

void test_button_click_action_metadata_warning_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "ButtonActionMetadata";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument seed(std::move(form));
    model::ControlNode button{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    button.events = {model::EventRef{model::ObjectId{3}}};
    button.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Run"));
    button.position.left.set(17);
    button.position.top.set(23);
    button.position.width.set(111);
    button.position.height.set(29);
    seed.add_event(model::Event{model::ObjectId{3}, "Click", "RunHandler", model::ControlRef{model::ObjectId{2}}});
    seed.add_control(std::move(button));
    const auto encoded = form_stream::encode_document(seed);
    expect(encoded.ok(), "Button.Click metadata fixture must encode");

    auto changed = encoded.value();
    auto* record = find_record_with_guid(changed, model::metamodel::descriptor_for(model::ControlKind::button).guid);
    expect(record != nullptr, "Button.Click fixture must contain its Button record");
    auto& action = record->items.at(2).items.at(2).items.at(1).items.at(2);
    action.items[2].items[1] = list_stream::ListValue::string_atom("OtherActionName");
    action.items[2].items[2] = list_stream::parse(R"({1,1,{"ru","Different presentation"}})");

    const auto decoded = form_stream::decode_document(changed, "ButtonActionMetadata");
    expect(decoded.ok() && !decoded.value().reconstruction_complete() &&
               std::any_of(decoded.diagnostics().begin(), decoded.diagnostics().end(), [](const auto& diagnostic) {
                   return diagnostic.code == "OOF1140" && diagnostic.severity == oof::DiagnosticSeverity::warning;
               }),
        "valid Button action metadata differences must return an incomplete model with OOF1140");
    const auto* decoded_button = decoded.value().find_control(model::ObjectId{2});
    const auto* click = decoded_button && !decoded_button->events.empty()
        ? decoded.value().find_event(decoded_button->events.front().id()) : nullptr;
    const auto* caption = decoded_button
        ? decoded_button->properties().find(model::PropertyId::from_name("Caption")) : nullptr;
    const auto* click_owner = click ? std::get_if<model::ControlRef>(&click->owner) : nullptr;
    expect(decoded_button && decoded_button->name == "Run" && click && click->name == "Click" &&
               click->handler == "RunHandler" && click_owner != nullptr && click_owner->id() == model::ObjectId{2} &&
               decoded_button->position.left.value() == 17 && decoded_button->position.top.value() == 23 &&
               decoded_button->position.width.value() == 111 && decoded_button->position.height.value() == 29 &&
               caption != nullptr && std::get_if<std::string>(&caption->value) != nullptr &&
               *std::get_if<std::string>(&caption->value) == "Run",
        "Button identity, Click handler and owner, geometry, and Caption must survive the profile warning");

    const auto xml = source::serialize_form_xml(decoded.value());
    expect(xml.ok() && xml.value().find("reconstructionComplete=\"false\"") != std::string::npos &&
               xml.value().find("RunHandler") != std::string::npos,
        "partial Button event model must serialize to named XML with completeness metadata");
    const auto parsed = source::parse_form_xml(xml.value());
    expect(parsed.ok() && !parsed.value().reconstruction_complete(),
        "Button event XML parsing must retain incompleteness");
    const auto rebuilt = oof::save_form_bin(parsed.value());
    expect(rebuilt.ok() && !rebuilt.value().empty() &&
               std::any_of(rebuilt.diagnostics().begin(), rebuilt.diagnostics().end(), [](const auto& diagnostic) {
                   return diagnostic.severity == oof::DiagnosticSeverity::warning;
               }),
        "partial Button event XML must build Form.bin while reporting omitted unsupported profile metadata");

    for (unsigned metadata_field = 1; metadata_field <= 4; ++metadata_field) {
        auto varied = encoded.value();
        auto* varied_record = find_record_with_guid(varied,
            model::metamodel::descriptor_for(model::ControlKind::button).guid);
        auto& varied_action = varied_record->items.at(2).items.at(2).items.at(1).items.at(2);
        if (metadata_field == 1) {
            varied_action.items.at(2).items.at(metadata_field) =
                list_stream::ListValue::string_atom("OtherActionName");
        } else {
            const auto localized = std::string("Different field ") + std::to_string(metadata_field);
            varied_action.items.at(2).items.at(metadata_field) = list_stream::ListValue::list({
                list_stream::ListValue::raw_atom("1"), list_stream::ListValue::raw_atom("1"),
                list_stream::ListValue::list({list_stream::ListValue::string_atom("ru"),
                    list_stream::ListValue::string_atom(localized)})});
        }
        const auto partial = form_stream::decode_document(varied, "ButtonActionMetadataVariation");
        const auto* varied_button = partial ? partial.value().find_control(model::ObjectId{2}) : nullptr;
        const auto* varied_event = varied_button && !varied_button->events.empty()
            ? partial.value().find_event(varied_button->events.front().id()) : nullptr;
        expect(partial.ok() && !partial.value().reconstruction_complete() &&
                   std::any_of(partial.diagnostics().begin(), partial.diagnostics().end(), [](const auto& diagnostic) {
                       return diagnostic.code == "OOF1140" &&
                           diagnostic.severity == oof::DiagnosticSeverity::warning;
                   }) && varied_event != nullptr && varied_event->handler == "RunHandler",
            "each valid Button Action name or presentation mismatch must warn and retain its handler");
    }

    auto invalid_version = changed;
    auto* invalid_version_record = find_record_with_guid(invalid_version,
        model::metamodel::descriptor_for(model::ControlKind::button).guid);
    invalid_version_record->items.at(2).items.at(2).items.at(1).items.at(2).items.at(2).items.at(0) =
        list_stream::ListValue::raw_atom("4");
    expect(!form_stream::decode_document(invalid_version, "ButtonActionBadVersion"),
        "Button action metadata version remains a structural rejection");
    auto invalid_arity = changed;
    auto* invalid_arity_record = find_record_with_guid(invalid_arity,
        model::metamodel::descriptor_for(model::ControlKind::button).guid);
    invalid_arity_record->items.at(2).items.at(2).items.at(1).items.at(2).items.at(2).items.pop_back();
    expect(!form_stream::decode_document(invalid_arity, "ButtonActionBadArity"),
        "Button action metadata arity remains a structural rejection");
    auto invalid_localized_type = changed;
    auto* invalid_localized_record = find_record_with_guid(invalid_localized_type,
        model::metamodel::descriptor_for(model::ControlKind::button).guid);
    invalid_localized_record->items.at(2).items.at(2).items.at(1).items.at(2).items.at(2).items.at(2) =
        list_stream::ListValue::raw_atom("123");
    expect(!form_stream::decode_document(invalid_localized_type, "ButtonActionBadLocalizedType"),
        "malformed Button localized Action metadata remains a strict type error");
    auto empty_handler = changed;
    auto* empty_handler_record = find_record_with_guid(empty_handler,
        model::metamodel::descriptor_for(model::ControlKind::button).guid);
    empty_handler_record->items.at(2).items.at(2).items.at(1).items.at(2).items.at(1) =
        list_stream::ListValue::string_atom("");
    expect(!form_stream::decode_document(empty_handler, "ButtonActionEmptyHandler"),
        "empty Button.Click handler remains a strict rejection");
}

void test_menu_action_values_and_optional_overrides_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1}; form.name = "ActionValues";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode button{model::ObjectId{2}, "Menu", model::ButtonPayload{}};
    button.properties().set_explicit(model::PropertyId::from_name("MenuMode"), model::EnumerationValue{"MenuMode", "UseExtra"});

    model::CommandBarButton explicit_empty;
    explicit_empty.name = "EmptyOverrides";
    explicit_empty.action = model::CommandBarAction{
        "RunHandler",
        "DifferentActionName",
        model::LocalizedStringValue{{{"ru", "Action text"}, {"en", "Action title"}}},
        model::LocalizedStringValue{{{"ru", "Action tooltip"}}},
        model::LocalizedStringValue{{{"ru", "Action description"}}},
    };
    explicit_empty.text = std::string{};
    explicit_empty.tooltip = std::string{};

    model::CommandBarButton absent_overrides;
    absent_overrides.name = "AbsentOverrides";
    absent_overrides.action = model::CommandBarAction{
        "AnotherHandler",
        "",
        model::LocalizedStringValue{{{"ru", "Second text"}}},
        model::LocalizedStringValue{{{"en", "Second tooltip"}}},
        model::LocalizedStringValue{{{"ru", "Second description"}, {"en", "Second description EN"}}},
    };
    absent_overrides.explanation = std::string{};

    std::get<model::ButtonPayload>(button.payload).buttons = {explicit_empty, absent_overrides};
    document.add_control(std::move(button));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded.ok() ? "" : "independent menu Action encode failed: " +
        encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "ActionValues");
    expect(decoded.ok(), "independent menu Action metadata and optional overrides must decode");
    const auto* decoded_button = decoded.value().find_control(model::ObjectId{2});
    const auto* payload = decoded_button ? std::get_if<model::ButtonPayload>(&decoded_button->payload) : nullptr;
    expect(payload && payload->buttons.size() == 2, "both menu actions must be restored");
    if (!payload || payload->buttons.size() != 2) return;
    expect(payload->buttons[0].action == explicit_empty.action &&
        payload->buttons[0].text == std::optional<std::string>{""} &&
        payload->buttons[0].tooltip == std::optional<std::string>{""} &&
        !payload->buttons[0].explanation,
        "explicit empty button Text and ToolTip must differ from absent Explanation");
    expect(payload->buttons[1].action == absent_overrides.action &&
        !payload->buttons[1].text && !payload->buttons[1].tooltip &&
        payload->buttons[1].explanation == std::optional<std::string>{""},
        "absent button Text and ToolTip must differ from explicit empty Explanation");
    expect(payload->buttons == std::vector<model::CommandBarButton>{explicit_empty, absent_overrides},
        "handler, independent Action name/localizations, and override presence must round-trip exactly");
}

void test_form_close_strict_action_guards() {
    model::Form form;
    form.id = model::ObjectId{1}; form.name = "CloseProbe";
    model::OrdinaryFormDocument empty(form);
    const auto zero = form_stream::encode_document(empty);
    expect(zero.ok() && list_stream::dump_compact(zero.value().items[4]) == "{0}", "absent form events must preserve the zero table");
    const auto zero_decoded = form_stream::decode_document(zero.value(), "CloseProbe");
    expect(zero_decoded.ok() && zero_decoded.value().form().events.empty(), "zero table must not invent a handler");
    form.events = {model::EventRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(form);
    document.add_event(model::Event{model::ObjectId{2}, "OnClose", "CloseHandler", model::FormRef{model::ObjectId{1}}});
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "single owned OnClose must encode");
    const std::vector<std::string> rejected_tables{
        "{}", "{2}", "{0,{0}}",
        R"({1,{70004,e1692cc2-605b-4535-84dd-28440238746c,{3,"CloseHandler",{0}}}})",
        R"({1,{70003,00000000-0000-0000-0000-000000000000,{3,"CloseHandler",{0}}}})",
        R"({1,{70003,e1692cc2-605b-4535-84dd-28440238746c,{4,"CloseHandler",{0}}}})",
        R"({1,{70003,e1692cc2-605b-4535-84dd-28440238746c,{3,"",{0}}}})",
        R"({1,{70003,e1692cc2-605b-4535-84dd-28440238746c,{3,123,{0}}}})"};
    for (const auto& table : rejected_tables) {
        auto bad = encoded.value(); bad.items[4] = list_stream::parse(table);
        expect(!form_stream::decode_document(bad, "CloseProbe"), "unknown, multiple, malformed or invalid handler records must reject");
    }
    for (const auto index : {0u, 1u, 2u, 3u, 4u, 5u, 6u}) {
        auto bad = encoded.value();
        bad.items[4].items[1].items[2].items[2].items[index] = list_stream::ListValue::raw_atom("999");
        expect(!form_stream::decode_document(bad, "CloseProbe"), "every unsupported action default slot must reject");
    }
    auto overflow = encoded.value();
    overflow.items[1].items[1].items[1] = list_stream::ListValue::raw_atom("18446744073709551615");
    expect_failure(form_stream::decode_document(overflow, "CloseProbe"), "OOF1120", "$/4", "event ID boundary must reject before unsigned allocation wraps");
    for (const auto& event : std::vector<model::Event>{
        {model::ObjectId{2}, "OnOpen", "OpenHandler", model::FormRef{model::ObjectId{1}}},
        {model::ObjectId{2}, "OnClose", "", model::FormRef{model::ObjectId{1}}},
        {model::ObjectId{2}, "OnClose", "CloseHandler", model::FormRef{model::ObjectId{99}}},
        {model::ObjectId{2}, "OnClose", "CloseHandler", model::ControlRef{model::ObjectId{1}}}}) {
        model::OrdinaryFormDocument invalid(form); invalid.add_event(event);
        expect(!form_stream::encode_document(invalid), "unsupported name, empty handler and nonowned events must reject");
    }
    form.events.push_back(model::EventRef{model::ObjectId{3}});
    model::OrdinaryFormDocument multiple(form);
    multiple.add_event(model::Event{model::ObjectId{2}, "OnClose", "CloseHandler", model::FormRef{model::ObjectId{1}}});
    multiple.add_event(model::Event{model::ObjectId{3}, "OnClose", "OtherHandler", model::FormRef{model::ObjectId{1}}});
    expect(!form_stream::encode_document(multiple), "multiple form handlers must reject");
}

void test_button_multiline_round_trip_and_validation() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{7}},
        model::ControlRef{model::ObjectId{12}},
    };
    model::OrdinaryFormDocument document(std::move(form));

    model::ControlNode first{model::ObjectId{2}, "First", model::ButtonPayload{}};
    first.properties().set_explicit(model::PropertyId::from_name("MultiLine"), true);
    document.add_control(std::move(first));
    model::ControlNode second{model::ObjectId{7}, "Second", model::ButtonPayload{}};
    second.properties().set_explicit(model::PropertyId::from_name("MultiLine"), false);
    document.add_control(std::move(second));
    document.add_control(model::ControlNode{
        model::ObjectId{12}, "Third", model::ButtonPayload{}});

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "Button.MultiLine values must encode");
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    expect(records[1].items[2].items[1].items[5].atom == "0" &&
               records[2].items[2].items[1].items[5].atom == "0" &&
               records[3].items[2].items[1].items[5].atom == "0" &&
               records[1].items[2].items[1].items[10].atom == "1" &&
               records[2].items[2].items[1].items[10].atom == "0" &&
               records[3].items[2].items[1].items[10].atom == "0",
        "Button.MultiLine must change only property slot 10 and preserve slot 5");
    for (std::size_t button = 1; button < records.size(); ++button) {
        for (std::size_t slot = 0; slot < records[button].items[2].items[1].items.size(); ++slot) {
            if (slot == 10) {
                continue;
            }
            expect(
                list_stream::dump_compact(records[button].items[2].items[1].items[slot]) ==
                    list_stream::dump_compact(records[1].items[2].items[1].items[slot]),
                "Button.MultiLine must leave other property slots unchanged");
        }
    }

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "Button.MultiLine values must decode");
    const auto property_bool_or = [&](model::ObjectId id, bool fallback) {
        const auto* control = decoded.value().find_control(id);
        expect(control != nullptr, "Button.MultiLine control must exist after decoding");
        const auto* value = control->properties().find(model::PropertyId::from_name("MultiLine"));
        return value == nullptr ? fallback : std::get<bool>(value->value);
    };
    expect(property_bool_or(model::ObjectId{2}, false) &&
               !property_bool_or(model::ObjectId{7}, false) &&
               !property_bool_or(model::ObjectId{12}, false),
        "true and default-false Button.MultiLine values must remain independent");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded Button.MultiLine controls must re-encode");
    expect(
        list_stream::dump_compact(reencoded.value()) ==
            list_stream::dump_compact(encoded.value()),
        "Button.MultiLine storage must round-trip without changing other slots");

    auto invalid_slot_boolean = encoded.value();
    invalid_slot_boolean.items[1].items[2].items[2].items[1].items[2].items[1].items[10] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(
        form_stream::decode_document(invalid_slot_boolean, "Main"),
        "OOF1105",
        "$/1/2/2/1/2/1/10",
        "Button.MultiLine storage values outside Boolean 0 or 1 must be rejected");

    auto invalid_unknown_slot = encoded.value();
    invalid_unknown_slot.items[1].items[2].items[2].items[1].items[2].items[1].items[5] =
        list_stream::ListValue::raw_atom("1");
    const auto partial_unknown_slot = form_stream::decode_document(invalid_unknown_slot, "Main");
    expect(partial_unknown_slot.ok() && !partial_unknown_slot.value().reconstruction_complete() &&
               !partial_unknown_slot.diagnostics().empty() &&
               partial_unknown_slot.diagnostics().front().code == "OOF1140" &&
               partial_unknown_slot.diagnostics().front().severity == oof::DiagnosticSeverity::warning,
        "valid unknown Button property slot variation must warn and return a partial model");
    const auto* partial_button = partial_unknown_slot.value().find_control(model::ObjectId{2});
    expect(partial_button != nullptr && partial_button->properties().find(model::PropertyId::from_name("MultiLine")) != nullptr &&
               std::get<bool>(partial_button->properties().find(
                   model::PropertyId::from_name("MultiLine"))->value),
        "known Button.MultiLine=true must survive unknown property slot variation");

    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "Main";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
    model::ControlNode wrong_type{model::ObjectId{2}, "WrongType", model::ButtonPayload{}};
    wrong_type.properties().set_explicit(
        model::PropertyId::from_name("MultiLine"), std::string("true"));
    invalid_document.add_control(std::move(wrong_type));
    expect_failure(
        form_stream::encode_document(invalid_document),
        "OOF1123",
        "$",
        "Button.MultiLine values with a non-Boolean model type must be rejected");
}

void test_button_alignments_and_tooltip_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{7}},
        model::ControlRef{model::ObjectId{12}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    const auto add_button = [&](std::uint64_t id, std::string name,
                                std::string horizontal, std::string vertical,
                                std::string tooltip) {
        model::ControlNode button{model::ObjectId{id}, std::move(name), model::ButtonPayload{}};
        button.properties().set_explicit(
            model::PropertyId::from_name("HorizontalAlign"),
            model::EnumerationValue{"HorizontalAlign", std::move(horizontal)});
        button.properties().set_explicit(
            model::PropertyId::from_name("VerticalAlign"),
            model::EnumerationValue{"VerticalAlign", std::move(vertical)});
        button.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tooltip));
        document.add_control(std::move(button));
    };
    add_button(2, "First", "Left", "Top", "Проверка Ω\nВторая строка");
    add_button(7, "Center", "Center", "Center", "");
    add_button(12, "Last", "Right", "Bottom", "Подсказка");

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "Button alignment and ToolTip properties must encode");
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    const std::int32_t horizontal[] = {0, 1, 2};
    const std::int32_t vertical[] = {0, 1, 2};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& properties = records[index + 1].items[2].items[1];
        expect(properties.items[3].atom == std::to_string(horizontal[index]) &&
                   properties.items[4].atom == std::to_string(vertical[index]),
            "Button alignment enums must use their observed three-value storage order");
    }
    expect(records[1].items[2].items[1].items[0].items[12].is_list,
        "Button.ToolTip must occupy its localized base slot");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "Button alignment and ToolTip properties must decode");
    const auto* first = decoded.value().find_control(model::ObjectId{2});
    const auto* middle = decoded.value().find_control(model::ObjectId{7});
    const auto* last = decoded.value().find_control(model::ObjectId{12});
    expect(first && middle && last, "all Button controls must survive property decoding");
    expect(!middle->properties().find(model::PropertyId::from_name("HorizontalAlign")) &&
               !middle->properties().find(model::PropertyId::from_name("VerticalAlign")) &&
               !middle->properties().find(model::PropertyId::from_name("ToolTip")),
        "center alignment and empty ToolTip must remain model defaults");
    const auto* first_tooltip = first->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(first_tooltip && std::get<std::string>(first_tooltip->value) == "Проверка Ω\nВторая строка",
        "Unicode multiline Button.ToolTip must survive");
    const auto* last_tooltip = last->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(last_tooltip && std::get<std::string>(last_tooltip->value) == "Подсказка",
        "Button.ToolTip must remain associated with its owner");
    expect(std::get<model::EnumerationValue>(first->properties().find(
               model::PropertyId::from_name("HorizontalAlign"))->value) ==
               model::EnumerationValue{"HorizontalAlign", "Left"} &&
               std::get<model::EnumerationValue>(first->properties().find(
                   model::PropertyId::from_name("VerticalAlign"))->value) ==
               model::EnumerationValue{"VerticalAlign", "Top"} &&
               std::get<model::EnumerationValue>(last->properties().find(
                   model::PropertyId::from_name("HorizontalAlign"))->value) ==
               model::EnumerationValue{"HorizontalAlign", "Right"} &&
               std::get<model::EnumerationValue>(last->properties().find(
                   model::PropertyId::from_name("VerticalAlign"))->value) ==
               model::EnumerationValue{"VerticalAlign", "Bottom"},
        "non-default Button alignment enum names must round-trip");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "Button alignment and ToolTip storage must round-trip without drift");

    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "Invalid";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument wrong_type(std::move(invalid_form));
    model::ControlNode wrong_button{model::ObjectId{2}, "WrongType", model::ButtonPayload{}};
    wrong_button.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"VerticalAlign", "Top"});
    wrong_type.add_control(std::move(wrong_button));
    expect_failure(form_stream::encode_document(wrong_type), "OOF1122", "$/Button/HorizontalAlign",
        "foreign Button alignment enum type must be rejected");

    auto invalid_member_form = model::Form{};
    invalid_member_form.id = model::ObjectId{1};
    invalid_member_form.name = "InvalidMember";
    invalid_member_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument invalid_member(std::move(invalid_member_form));
    model::ControlNode invalid_member_button{model::ObjectId{2}, "InvalidMember", model::ButtonPayload{}};
    invalid_member_button.properties().set_explicit(model::PropertyId::from_name("VerticalAlign"),
        model::EnumerationValue{"VerticalAlign", "Middle"});
    invalid_member.add_control(std::move(invalid_member_button));
    expect_failure(form_stream::encode_document(invalid_member), "OOF1122", "$/Button/VerticalAlign",
        "unknown Button alignment enum member must be rejected");

    auto invalid_storage = encoded.value();
    invalid_storage.items[1].items[2].items[2].items[1].items[2].items[1].items[3] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(invalid_storage, "Main"), "OOF1114",
        "$/1/2/2/1/2/1/3", "unknown Button alignment storage values must be rejected");

    const auto encode_tooltip = [](std::string text) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "LineEndings";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument tooltip_document(std::move(form));
        model::ControlNode button{model::ObjectId{2}, "Tip", model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(text));
        tooltip_document.add_control(std::move(button));
        return form_stream::encode_document(tooltip_document);
    };
    const auto lf = encode_tooltip("first\nsecond");
    const auto crlf = encode_tooltip("first\r\nsecond");
    expect(lf.ok() && crlf.ok() && list_stream::dump_compact(lf.value()) ==
               list_stream::dump_compact(crlf.value()),
        "LF and CRLF ToolTip text must produce identical localized storage");

    const std::string mixed_model_text = "first\r\nsecond\nthird\rlast";
    const auto mixed_storage = encode_tooltip(mixed_model_text);
    expect(mixed_storage.ok(), "mixed ToolTip line endings must encode");
    const auto mixed_decoded = form_stream::decode_document(mixed_storage.value(), "LineEndings");
    expect(mixed_decoded.ok(), "mixed ToolTip line endings must decode");
    const auto* mixed_control = mixed_decoded.value().find_control(model::ObjectId{2});
    expect(mixed_control != nullptr, "mixed ToolTip control must survive decoding");
    const auto* mixed_tooltip = mixed_control->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(mixed_tooltip && std::get<std::string>(mixed_tooltip->value) ==
               "first\nsecond\nthird\rlast",
        "decode must normalize CRLF to LF while preserving lone CR and the final line");
    const auto normalized_storage = encode_tooltip("first\nsecond\nthird\rlast");
    expect(normalized_storage.ok() && list_stream::dump_compact(normalized_storage.value()) ==
               list_stream::dump_compact(mixed_storage.value()),
        "normalizing a mixed-ending ToolTip through the model must preserve its storage record");
}

void test_check_box_tooltip_round_trip_and_validation() {
    const auto make_document = [](std::optional<std::string> tool_tip) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "CheckBoxToolTip";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue boolean_type;
        model::TypeDomainEntry boolean_entry;
        boolean_entry.term = model::TypeDomainTerm::boolean;
        boolean_type.entries.push_back(boolean_entry);
        document.add_attribute(model::Attribute{model::ObjectId{3}, "Flag", boolean_type});
        model::ControlNode check_box{model::ObjectId{2}, "FlagControl", model::CheckBoxPayload{}};
        check_box.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        if (tool_tip.has_value()) {
            check_box.properties().set_explicit(model::PropertyId::from_name("ToolTip"), *tool_tip);
        }
        document.add_control(std::move(check_box));
        return document;
    };
    const auto check_box_base = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        const auto& record = encoded.items[1].items[2].items[2].items[1];
        return record.items[2].items[1].items[0].items[0];
    };

    const auto default_encoded = form_stream::encode_document(make_document(std::nullopt));
    const auto explicit_empty_encoded = form_stream::encode_document(make_document(std::string{}));
    expect(default_encoded.ok() && explicit_empty_encoded.ok(),
        "default and explicit empty CheckBox ToolTip must encode");
    expect(list_stream::dump_compact(default_encoded.value()) ==
               list_stream::dump_compact(explicit_empty_encoded.value()),
        "explicit empty CheckBox ToolTip must normalize to the default storage");
    const auto default_decoded = form_stream::decode_document(explicit_empty_encoded.value(), "CheckBoxToolTip");
    expect(default_decoded.ok(), "explicit empty CheckBox ToolTip must decode");
    const auto* default_check_box = default_decoded.value().find_control(model::ObjectId{2});
    expect(default_check_box && !default_check_box->properties().find(model::PropertyId::from_name("ToolTip")),
        "empty CheckBox ToolTip must normalize to its implicit default");

    const std::string tool_tip = "Подсказка Ω <важно> & \"цитата\"\nВторая строка";
    const auto encoded = form_stream::encode_document(make_document(tool_tip));
    expect(encoded.ok(), "CheckBox ToolTip with Unicode, punctuation, and a newline must encode");
    const auto& stored_tool_tip = check_box_base(encoded.value()).items[12];
    expect(list_stream::dump_compact(stored_tool_tip) == value_codec::encode_localized_string(
               model::LocalizedStringValue{{{"ru", "Подсказка Ω <важно> & \"цитата\"\r\nВторая строка"}}}),
        "CheckBox ToolTip must occupy the observed localized base slot with canonical line endings");
    const auto decoded = form_stream::decode_document(encoded.value(), "CheckBoxToolTip");
    expect(decoded.ok(), "CheckBox ToolTip must decode");
    const auto* check_box = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_tool_tip = check_box == nullptr ? nullptr :
        check_box->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(decoded_tool_tip && std::get<std::string>(decoded_tool_tip->value) == tool_tip,
        "CheckBox ToolTip must round-trip Unicode, punctuation, and newlines");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "CheckBox ToolTip storage must round-trip without drift");

    constexpr std::string_view tooltip_path = "$/1/2/2/1/2/1/0/0/12";
    auto malformed = encoded.value();
    auto& malformed_tool_tip = malformed.items[1].items[2].items[2].items[1]
        .items[2].items[1].items[0].items[0].items[12];
    malformed_tool_tip = list_stream::ListValue::raw_atom("malformed");
    expect_failure(form_stream::decode_document(malformed, "CheckBoxToolTip"), "OOF1108", tooltip_path,
        "malformed CheckBox ToolTip localization must be rejected");

    auto multilingual = encoded.value();
    auto& multilingual_tool_tip = multilingual.items[1].items[2].items[2].items[1]
        .items[2].items[1].items[0].items[0].items[12];
    multilingual_tool_tip = list_stream::parse(value_codec::encode_localized_string(
        model::LocalizedStringValue{{{"ru", "Текст"}, {"en", "Text"}}}));
    expect_failure(form_stream::decode_document(multilingual, "CheckBoxToolTip"), "OOF1115", tooltip_path,
        "multilingual CheckBox ToolTip must be rejected without loss");
}

void test_choice_field_static_profile_round_trip_and_validation() {
    static constexpr std::string_view observed_choice_field_info = R"OOF({2,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},31,0,0,1,0,1,0,0,0,0,1,0,0,255,0,0,4,0,{"U"},{"U"},"",0,1,1,0,0,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},0,0,0,{0,0,0},{1,0},0,0,0,0,0,0,0,16777215,2,0,0},{0}})OOF";
    const auto make_document = [](std::optional<std::string> tool_tip,
                                  bool enabled = true,
                                  bool boolean_attribute = false,
                                  bool with_data_path = true,
                                  bool with_unmapped_property = false) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "ChoiceFieldProfile";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue value_type;
        model::TypeDomainEntry value_entry;
        value_entry.term = boolean_attribute ? model::TypeDomainTerm::boolean : model::TypeDomainTerm::string;
        if (!boolean_attribute) {
            value_entry.string.length = 64;
            value_entry.string.variable = false;
        }
        value_type.entries.push_back(value_entry);
        document.add_attribute(model::Attribute{model::ObjectId{3}, "Choice", value_type});
        model::ControlNode choice_field{model::ObjectId{2}, "ChoiceField", model::ChoiceFieldPayload{}};
        if (with_data_path) {
            choice_field.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        }
        if (!enabled) {
            choice_field.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
        }
        if (tool_tip.has_value()) {
            choice_field.properties().set_explicit(model::PropertyId::from_name("ToolTip"), *tool_tip);
        }
        if (with_unmapped_property) {
            choice_field.properties().set_explicit(
                model::PropertyId::from_name("ReadOnly"), false);
        }
        document.add_control(std::move(choice_field));
        return document;
    };
    const auto choice_record = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };

    const auto default_encoded = form_stream::encode_document(make_document(std::nullopt));
    const auto explicit_empty_encoded = form_stream::encode_document(make_document(std::string{}));
    expect(default_encoded.ok() && explicit_empty_encoded.ok(),
        "default and explicit empty ChoiceField ToolTip must encode");
    expect(list_stream::dump_compact(choice_record(default_encoded.value()).items[2]) == observed_choice_field_info,
        "ChoiceField writer must match the independent Designer-native 46-field info record");
    auto captured_default = default_encoded.value();
    captured_default.items[1].items[2].items[2].items[1].items[2] = list_stream::parse(observed_choice_field_info);
    const auto captured_decoded = form_stream::decode_document(captured_default, "ChoiceFieldNativeInfo");
    expect(captured_decoded.ok(), captured_decoded ? "" :
        "independent Designer-native ChoiceField info must decode: " + captured_decoded.diagnostics().front().path +
        ": " + captured_decoded.diagnostics().front().message);
    expect(list_stream::dump_compact(default_encoded.value()) ==
               list_stream::dump_compact(explicit_empty_encoded.value()),
        "explicit empty ChoiceField ToolTip must normalize to the default storage");
    const auto default_decoded = form_stream::decode_document(default_encoded.value(), "ChoiceFieldProfile");
    expect(default_decoded.ok(), "default ChoiceField storage must decode");
    const auto* default_choice = default_decoded.value().find_control(model::ObjectId{2});
    expect(default_choice && default_choice->data_path &&
               default_choice->data_path->attribute.id() == model::ObjectId{3} &&
               !default_choice->properties().find(model::PropertyId::from_name("ToolTip")),
        "default ChoiceField must preserve direct DataPath and omit empty ToolTip");

    auto unbound_encoded = form_stream::encode_document(make_document(std::nullopt, true, false, false));
    expect(unbound_encoded.ok(), "unbound ChoiceField default profile must encode");
    auto independent_unbound = unbound_encoded.value();
    auto& independent_unbound_record = independent_unbound.items[1].items[2].items[2].items[1];
    independent_unbound_record.items[2] = list_stream::parse(observed_choice_field_info);
    expect(independent_unbound.items[2].items[3].items.size() == 1 &&
               independent_unbound.items[2].items[3].items[0].atom == "0",
        "unbound Designer-native ChoiceField fixture must have an empty attribute-link table");
    const auto independent_unbound_decoded = form_stream::decode_document(
        independent_unbound, "ChoiceFieldUnboundNativeDefault");
    expect(independent_unbound_decoded.ok(), independent_unbound_decoded ? "" :
        "independent unbound ChoiceField default record must decode: " +
            independent_unbound_decoded.diagnostics().front().path + ": " +
            independent_unbound_decoded.diagnostics().front().message);
    const auto* unbound_choice = independent_unbound_decoded.value().find_control(model::ObjectId{2});
    expect(unbound_choice && !unbound_choice->data_path,
        "unbound Designer-native ChoiceField must remain unbound after decode");
    const auto unbound_reencoded = form_stream::encode_document(independent_unbound_decoded.value());
    expect(unbound_reencoded.ok() && unbound_reencoded.value().items[2].items[3].items.size() == 1 &&
               unbound_reencoded.value().items[2].items[3].items[0].atom == "0",
        "independent unbound ChoiceField must re-encode without an invented DataPath");

    const std::string tool_tip = "Выберите Ω <вариант> & \"значение\"\nВторая строка";
    const auto encoded = form_stream::encode_document(make_document(tool_tip, false));
    expect(encoded.ok(), "ChoiceField with disabled state and Unicode multiline ToolTip must encode");
    const auto& record = choice_record(encoded.value());
    const auto& info = record.items[2];
    expect(info.items.size() == 3 && info.items[0].atom == "2" && info.items[1].items.size() == 46 &&
               info.items[1].items[0].items[1].atom == "0" &&
               list_stream::dump_compact(info.items[2]) == "{0}",
        "ChoiceField must use the observed 46-field control-info profile and preserve disabled state");
    const auto& stored_tool_tip = info.items[1].items[0].items[12];
    expect(list_stream::dump_compact(stored_tool_tip) == value_codec::encode_localized_string(
               model::LocalizedStringValue{{{"ru", "Выберите Ω <вариант> & \"значение\"\r\nВторая строка"}}}),
        "ChoiceField ToolTip must use the observed localized base slot and canonical line endings");
    const auto decoded = form_stream::decode_document(encoded.value(), "ChoiceFieldProfile");
    expect(decoded.ok(), "ChoiceField ToolTip and Enabled must decode");
    const auto* choice_field = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_tool_tip = choice_field == nullptr ? nullptr :
        choice_field->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(choice_field && choice_field->data_path &&
               choice_field->data_path->attribute.id() == model::ObjectId{3} &&
               decoded_tool_tip && std::get<std::string>(decoded_tool_tip->value) == tool_tip &&
               std::get<bool>(choice_field->properties().find(model::PropertyId::from_name("Enabled"))->value) == false,
        "ChoiceField named properties and String DataPath must round-trip");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "ChoiceField profile must round-trip without storage drift");

    auto unsupported_property = make_document(std::nullopt, true, false, true, true);
    expect_failure(form_stream::encode_document(unsupported_property), "OOF1122", "$/ChoiceField",
        "unmapped ChoiceField properties must not enter persisted XML or storage");

    auto wrong_type = make_document(std::nullopt, true, true);
    expect_failure(form_stream::encode_document(wrong_type), "OOF1122", "$/ChoiceField/DataPath",
        "ChoiceField must reject non-string DataPath attributes");
    auto wrong_type_unbound = form_stream::encode_document(make_document(std::nullopt, true, true, false));
    expect(wrong_type_unbound.ok(), "unbound ChoiceField can coexist with an unrelated Boolean Attribute");
    auto wrong_type_bound_stream = wrong_type_unbound.value();
    wrong_type_bound_stream.items[2].items[3] = default_encoded.value().items[2].items[3];
    const auto wrong_type_bound = form_stream::decode_document(wrong_type_bound_stream, "ChoiceFieldBooleanLink");
    expect(!wrong_type_bound && wrong_type_bound.diagnostics().front().code == "OOF1122" &&
               wrong_type_bound.diagnostics().front().path == "$/2/3",
        "a linked ChoiceField must reject an independently encoded non-String Attribute link");

    auto malformed = encoded.value();
    malformed.items[1].items[2].items[2].items[1].items[2].items[1].items[13] =
        list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(malformed, "ChoiceFieldProfile"), "OOF1114",
        "$/1/2/2/1/2", "unmapped ChoiceField base flags must fail closed");
}

void test_check_box_font_round_trip_and_validation() {
    const auto make_document = [](std::optional<model::FontValue> font) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "CheckBoxFont";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue boolean_type;
        model::TypeDomainEntry boolean_entry;
        boolean_entry.term = model::TypeDomainTerm::boolean;
        boolean_type.entries.push_back(boolean_entry);
        document.add_attribute(model::Attribute{model::ObjectId{3}, "Flag", boolean_type});
        model::ControlNode check_box{model::ObjectId{2}, "FlagControl", model::CheckBoxPayload{}};
        check_box.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        if (font) check_box.properties().set_explicit(model::PropertyId::from_name("Font"), *font);
        document.add_control(std::move(check_box));
        return document;
    };
    const auto check_box_base = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[0];
    };

    const auto default_encoded = form_stream::encode_document(make_document(std::nullopt));
    model::FontValue automatic;
    const auto explicit_automatic_encoded = form_stream::encode_document(make_document(automatic));
    expect(default_encoded.ok() && explicit_automatic_encoded.ok(),
        "default and explicit automatic CheckBox Font must encode");
    expect(list_stream::dump_compact(default_encoded.value()) ==
               list_stream::dump_compact(explicit_automatic_encoded.value()),
        "explicit automatic CheckBox Font must normalize to the default storage");
    const auto default_decoded = form_stream::decode_document(explicit_automatic_encoded.value(), "CheckBoxFont");
    expect(default_decoded.ok(), "default CheckBox Font must decode");
    const auto* default_check_box = default_decoded.value().find_control(model::ObjectId{2});
    expect(default_check_box && !default_check_box->properties().find(model::PropertyId::from_name("Font")),
        "automatic CheckBox Font must normalize to its implicit default");

    model::FontValue font;
    font.kind = model::FontKind::absolute;
    font.face_name = "Arial";
    font.height = 12;
    font.bold = true;
    const auto encoded = form_stream::encode_document(make_document(font));
    expect(encoded.ok(), "supported absolute CheckBox Font must encode");
    const auto& stored_font = check_box_base(encoded.value()).items[4];
    expect(list_stream::dump_compact(stored_font) == value_codec::encode_font(font),
        "CheckBox Font must use the observed Button base Font record profile");
    const auto decoded = form_stream::decode_document(encoded.value(), "CheckBoxFont");
    expect(decoded.ok(), "supported absolute CheckBox Font must decode");
    const auto* check_box = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_font = check_box == nullptr ? nullptr :
        check_box->properties().find(model::PropertyId::from_name("Font"));
    expect(decoded_font && std::get<model::FontValue>(decoded_font->value) == font,
        "CheckBox Font must round-trip as its named FontValue");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "CheckBox Font storage must round-trip without drift");

    model::FontValue unsupported;
    unsupported.kind = model::FontKind::windows_font;
    const auto unsupported_encoded = form_stream::encode_document(make_document(unsupported));
    expect_failure(unsupported_encoded, "OOF1122", "$/CheckBox/Font",
        "unsupported WindowsFont CheckBox value must be rejected without fallback");
}

void test_button_colors_round_trip_and_validation() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "ButtonColors";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{4}},
        model::ControlRef{model::ObjectId{6}}, model::ControlRef{model::ObjectId{8}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode rgb{model::ObjectId{2}, "RGB", model::ButtonPayload{}};
    model::ColorValue absolute;
    absolute.kind = model::ColorKind::absolute;
    absolute.red = 17;
    absolute.green = 83;
    absolute.blue = 201;
    rgb.properties().set_explicit(model::PropertyId::from_name("BorderColor"), absolute);
    rgb.properties().set_explicit(model::PropertyId::from_name("ButtonBackColor"), absolute);
    rgb.properties().set_explicit(model::PropertyId::from_name("ButtonTextColor"),
        model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
            model::QualifiedName{"StyleColors.ButtonBackColor"}});
    const model::ShortcutValue shortcut{"Enter", false, true, false};
    rgb.properties().set_explicit(model::PropertyId::from_name("Shortcut"), shortcut);
    model::FontValue full_font;
    full_font.kind = model::FontKind::absolute;
    full_font.face_name = "Arial";
    full_font.height = 12.5;
    full_font.bold = false;
    full_font.italic = true;
    full_font.underline = false;
    full_font.strikeout = true;
    full_font.scale = 125;
    rgb.properties().set_explicit(model::PropertyId::from_name("Font"), full_font);
    document.add_control(std::move(rgb));
    model::ControlNode automatic{model::ObjectId{4}, "Automatic", model::ButtonPayload{}};
    automatic.properties().set_explicit(model::PropertyId::from_name("BorderColor"), model::ColorValue{});
    document.add_control(std::move(automatic));
    model::ControlNode named_styles{model::ObjectId{6}, "NamedStyles", model::ButtonPayload{}};
    named_styles.properties().set_explicit(model::PropertyId::from_name("BorderColor"),
        model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
            model::QualifiedName{"StyleColors.ButtonTextColor"}});
    named_styles.properties().set_explicit(model::PropertyId::from_name("ButtonTextColor"),
        model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
            model::QualifiedName{"StyleColors.ButtonBorderColor"}});
    model::FontValue text_font;
    text_font.kind = model::FontKind::style_reference;
    text_font.style = model::QualifiedName{"StyleFonts.TextFont"};
    named_styles.properties().set_explicit(model::PropertyId::from_name("Font"), text_font);
    document.add_control(std::move(named_styles));
    model::ControlNode copied_scale_button{model::ObjectId{8}, "CopiedScale", model::ButtonPayload{}};
    model::FontValue copied_scale_font;
    copied_scale_font.kind = model::FontKind::absolute;
    copied_scale_font.face_name = "Arial";
    copied_scale_font.scale = 125;
    copied_scale_font.scale_override = true;
    copied_scale_button.properties().set_explicit(model::PropertyId::from_name("Font"), copied_scale_font);
    document.add_control(std::move(copied_scale_button));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "Button absolute, automatic, and style colors must encode");
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    const auto& rgb_base = records[1].items[2].items[1].items[0];
    expect(rgb_base.items[6].items[1].atom == "0" && rgb_base.items[6].items[2].items[0].atom == "13194001",
        "Button absolute RGB must use the observed packed BGR integer");
    expect(rgb_base.items[10].items[2].items[0].atom == "-7" &&
               rgb_base.items[9].items[1].atom == "0" &&
               rgb_base.items[9].items[2].items[0].atom == "13194001",
        "Button style colors must use their named platform identifiers independently");
    expect(list_stream::dump_compact(records[1].items[2].items[1].items[9]) == "{0,13,8}",
        "Button.Shortcut must occupy info properties[9] independently of base[9] color");
    expect(list_stream::dump_compact(rgb_base.items[4]) ==
               "{8,0,63,125,0,0,0,400,1,0,1,0,0,0,0,0,\"Arial\",1,125,0}",
        "Button.Font must encode explicit false values and named height/scale data");
    const auto& named_base = records[3].items[2].items[1].items[0];
    expect(named_base.items[6].items[2].items[0].atom == "-21" &&
               named_base.items[10].items[2].items[0].atom == "-34",
        "Button BorderColor and ButtonTextColor must retain explicit -21 and -34 named styles");
    expect(list_stream::dump_compact(named_base.items[4]) == "{8,2,0,{-20},1,100}",
        "Button.Font must encode the observed TextFont style reference");
    const auto& copied_scale_base = records[4].items[2].items[1].items[0];
    expect(list_stream::dump_compact(copied_scale_base.items[4]) ==
               "{8,0,513,0,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,125,0}",
        "Button Font scale override marker must remain independent from the scale");

    const auto decoded = form_stream::decode_document(encoded.value(), "ButtonColors");
    expect(decoded.ok(), "Button color values must decode");
    const auto* decoded_rgb = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_auto = decoded.value().find_control(model::ObjectId{4});
    const auto* decoded_named = decoded.value().find_control(model::ObjectId{6});
    const auto* decoded_copy_scale = decoded.value().find_control(model::ObjectId{8});
    expect(decoded_rgb && decoded_auto && decoded_named && decoded_copy_scale,
        "Button color and Font owners must survive decoding");
    const auto* border = decoded_rgb->properties().find(model::PropertyId::from_name("BorderColor"));
    const auto* back_color = decoded_rgb->properties().find(model::PropertyId::from_name("ButtonBackColor"));
    const auto* decoded_shortcut = decoded_rgb->properties().find(model::PropertyId::from_name("Shortcut"));
    const auto* text = decoded_rgb->properties().find(model::PropertyId::from_name("ButtonTextColor"));
    const auto* decoded_style = text ? std::get_if<model::QualifiedName>(
        &std::get<model::ColorValue>(text->value).style) : nullptr;
    expect(border && std::get<model::ColorValue>(border->value) == absolute && decoded_style &&
               *decoded_style == model::QualifiedName{"StyleColors.ButtonBackColor"},
        "non-default Button RGB and cross-style reference must round-trip");
    expect(back_color && std::get<model::ColorValue>(back_color->value) == absolute &&
               decoded_shortcut && std::get<model::ShortcutValue>(decoded_shortcut->value) == shortcut,
        "Button.Shortcut and ButtonBackColor must decode from their distinct storage records");
    const auto* decoded_font = decoded_rgb->properties().find(model::PropertyId::from_name("Font"));
    expect(decoded_font && std::get<model::FontValue>(decoded_font->value) == full_font,
        "Button Font must preserve explicit false values and each named field");
    expect(!decoded_auto->properties().find(model::PropertyId::from_name("BorderColor")),
        "explicit canonical Button default color must normalize to absent");
    expect(!decoded_auto->properties().find(model::PropertyId::from_name("Font")),
        "automatic Button Font must normalize to the descriptor default");
    const auto* named_border = decoded_named->properties().find(model::PropertyId::from_name("BorderColor"));
    const auto* named_text = decoded_named->properties().find(model::PropertyId::from_name("ButtonTextColor"));
    expect(named_border && std::get<model::ColorValue>(named_border->value).style ==
               model::StyleReference{model::QualifiedName{"StyleColors.ButtonTextColor"}} &&
               named_text && std::get<model::ColorValue>(named_text->value).style ==
               model::StyleReference{model::QualifiedName{"StyleColors.ButtonBorderColor"}},
        "all observed named style identifiers must decode to their public names");
    const auto* decoded_text_font = decoded_named->properties().find(model::PropertyId::from_name("Font"));
    expect(decoded_text_font && std::get<model::FontValue>(decoded_text_font->value) == text_font,
        "Button TextFont style must decode to its named model value");
    const auto* decoded_copy_font = decoded_copy_scale->properties().find(model::PropertyId::from_name("Font"));
    expect(decoded_copy_font && std::get<model::FontValue>(decoded_copy_font->value) == copied_scale_font,
        "Button copy-scale marker and value must both survive decoding");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "Button color storage must round-trip without drift");

    auto invalid_color = absolute;
    invalid_color.kind = static_cast<model::ColorKind>(255);
    invalid_color.style = model::QualifiedName{"StyleColors.ButtonTextColor"};
    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "InvalidColor";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument unknown_kind(std::move(invalid_form));
    model::ControlNode unknown_button{model::ObjectId{2}, "Unknown", model::ButtonPayload{}};
    unknown_button.properties().set_explicit(model::PropertyId::from_name("BorderColor"), invalid_color);
    unknown_kind.add_control(std::move(unknown_button));
    expect_failure(form_stream::encode_document(unknown_kind), "OOF1122", "$/Button/BorderColor",
        "unknown ColorKind values must not be normalized as style references");

    invalid_color = absolute;
    invalid_color.alpha = 254;
    model::Form alpha_form;
    alpha_form.id = model::ObjectId{1};
    alpha_form.name = "InvalidAlpha";
    alpha_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument bad_alpha(std::move(alpha_form));
    model::ControlNode alpha_button{model::ObjectId{2}, "Alpha", model::ButtonPayload{}};
    alpha_button.properties().set_explicit(model::PropertyId::from_name("BorderColor"), invalid_color);
    bad_alpha.add_control(std::move(alpha_button));
    expect_failure(form_stream::encode_document(bad_alpha), "OOF1122", "$/Button/BorderColor",
        "Button absolute colors with alpha must be rejected");

    const auto reject_color = [](model::ColorValue color, std::string_view message) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "InvalidStyle";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument invalid(std::move(form));
        model::ControlNode button{model::ObjectId{2}, "Invalid", model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("ButtonTextColor"), std::move(color));
        invalid.add_control(std::move(button));
        expect_failure(form_stream::encode_document(invalid), "OOF1122", "$/Button/ButtonTextColor", message);
    };
    auto malformed_style = model::ColorValue{model::ColorKind::style_reference, 1, 0, 0, 254,
        model::QualifiedName{"StyleColors.ButtonTextColor"}};
    reject_color(malformed_style, "style colors with explicit channels or alpha must be rejected");
    malformed_style = model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
        model::QualifiedName{"StyleColors.UnknownColor"}};
    reject_color(malformed_style, "unknown qualified style references must be rejected");

    auto malformed_storage = encoded.value();
    malformed_storage.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[6].items[2].items[0] =
        list_stream::ListValue::raw_atom("16777216");
    expect_failure(form_stream::decode_document(malformed_storage, "ButtonColors"), "OOF1114",
        "$/1/2/2/1/2/1/0/6/2/0", "out-of-range packed Button RGB must be rejected");

    auto malformed_font = encoded.value();
    malformed_font.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[4] =
        list_stream::parse("{8,0,1024,125,0,0,0,400,0,0,0,0,0,0,0,0,\"Arial\",1,125,0}");
    expect_failure(form_stream::decode_document(malformed_font, "ButtonColors"), "OOF1114",
        "$/1/2/2/1/2/1/0/4", "unknown Font presence flags must fail through the Button boundary");

    model::Form override_form;
    override_form.id = model::ObjectId{1};
    override_form.name = "FontOverride";
    override_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument font_override(std::move(override_form));
    model::ControlNode style_override{model::ObjectId{2}, "Text", model::ButtonPayload{}};
    auto unsupported_style = text_font;
    unsupported_style.underline = false;
    style_override.properties().set_explicit(model::PropertyId::from_name("Font"), unsupported_style);
    font_override.add_control(std::move(style_override));
    expect_failure(form_stream::encode_document(font_override), "OOF1122", "$/Button/Font",
        "unobserved style-font overrides must be rejected rather than discarded");
}

void test_button_picture_enums_round_trip_and_validation() {
    static constexpr std::string_view size_names[] = {
        "RealSize", "Stretch", "Proportionally", "Tile", "AutoSize", "ByFontSize"};
    static constexpr std::int32_t size_values[] = {0, 1, 2, 3, 4, 7};
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "PictureEnums";
    for (std::size_t index = 0; index < std::size(size_names); ++index) {
        form.children.push_back(model::ControlRef{
            model::ObjectId{static_cast<std::uint64_t>(2 + index * 2)}});
    }
    model::OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < std::size(size_names); ++index) {
        const model::ObjectId id{static_cast<std::uint64_t>(2 + index * 2)};
        model::ControlNode button{id, "Button" + std::to_string(index), model::ButtonPayload{}};
        const bool right = (index % 2) != 0;
        button.properties().set_explicit(model::PropertyId::from_name("PictureLocation"),
            model::EnumerationValue{"PictureLocation", right ? "Right" : "Left"});
        button.properties().set_explicit(model::PropertyId::from_name("PictureSize"),
            model::EnumerationValue{"PictureSize", std::string(size_names[index])});
        document.add_control(std::move(button));
    }

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "all observed Button PictureLocation and PictureSize values must encode");
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    for (std::size_t index = 0; index < std::size(size_names); ++index) {
        const auto& properties = records[index + 1].items[2].items[1];
        expect(properties.items[6].atom == std::to_string(index % 2) &&
                   properties.items[7].atom == std::to_string(size_values[index]),
            "Button picture properties must use their independent storage slots and observed enum codes");
    }

    const auto decoded = form_stream::decode_document(encoded.value(), "PictureEnums");
    expect(decoded.ok(), "all observed Button picture enum values must decode");
    for (std::size_t index = 0; index < std::size(size_names); ++index) {
        const auto* button = decoded.value().find_control(
            model::ObjectId{static_cast<std::uint64_t>(2 + index * 2)});
        expect(button != nullptr, "Button picture enum control must survive decoding");
        const auto* location = button->properties().find(model::PropertyId::from_name("PictureLocation"));
        if (index % 2 == 0) {
            expect(location == nullptr, "default Left picture location must normalize to absent");
        } else {
            expect(location && std::get<model::EnumerationValue>(location->value) ==
                       model::EnumerationValue{"PictureLocation", "Right"},
                "Right picture location must remain explicit");
        }
        const auto* size = button->properties().find(model::PropertyId::from_name("PictureSize"));
        if (index == 0) {
            expect(size == nullptr, "default RealSize must normalize to absent");
        } else {
            expect(size && std::get<model::EnumerationValue>(size->value) ==
                       model::EnumerationValue{"PictureSize", std::string(size_names[index])},
                "each non-default picture size must remain explicit and independent");
        }
    }
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "Button picture enums must re-encode without changing either property");

    model::Form foreign_form;
    foreign_form.id = model::ObjectId{1};
    foreign_form.name = "ForeignEnums";
    foreign_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument foreign_document(std::move(foreign_form));
    model::ControlNode foreign_button{model::ObjectId{2}, "Foreign", model::ButtonPayload{}};
    foreign_button.properties().set_explicit(model::PropertyId::from_name("PictureLocation"),
        model::EnumerationValue{"PictureSize", "Right"});
    foreign_document.add_control(std::move(foreign_button));
    expect_failure(form_stream::encode_document(foreign_document), "OOF1122", "$/Button/PictureLocation",
        "foreign PictureLocation enum type must be rejected");

    model::Form unknown_form;
    unknown_form.id = model::ObjectId{1};
    unknown_form.name = "UnknownEnums";
    unknown_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument unknown_document(std::move(unknown_form));
    model::ControlNode unknown_button{model::ObjectId{2}, "Unknown", model::ButtonPayload{}};
    unknown_button.properties().set_explicit(model::PropertyId::from_name("PictureSize"),
        model::EnumerationValue{"PictureSize", "Unsupported"});
    unknown_document.add_control(std::move(unknown_button));
    expect_failure(form_stream::encode_document(unknown_document), "OOF1122", "$/Button/PictureSize",
        "unsupported PictureSize enum member must be rejected");

    for (const std::int32_t unsupported : {5, 6, 8}) {
        auto invalid = encoded.value();
        invalid.items[1].items[2].items[2].items[1].items[2].items[1].items[7] =
            list_stream::ListValue::raw_atom(std::to_string(unsupported));
        expect_failure(form_stream::decode_document(invalid, "PictureEnums"), "OOF1114",
            "$/1/2/2/1/2/1/7", "unsupported Button.PictureSize storage values must be rejected");
    }
    auto invalid_location = encoded.value();
    invalid_location.items[1].items[2].items[2].items[1].items[2].items[1].items[6] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(invalid_location, "PictureEnums"), "OOF1114",
        "$/1/2/2/1/2/1/6", "unsupported Button.PictureLocation storage values must be rejected");
}

void test_named_button_menu_round_trip_and_invalid_references() {
    model::Form form;
    form.id = model::ObjectId{1}; form.name = "Menu";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode button{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    button.properties().set_explicit(model::PropertyId::from_name("MenuMode"), model::EnumerationValue{"MenuMode", "UseExtra"});
    model::CommandBarButton action;
    action.name = "ActionOne"; action.action = model::CommandBarAction{"RunHandler", "", {}, {}, {}}; action.text = "Первое";
    action.explanation = "Пояснение"; action.tooltip = "Подсказка";
    action.enabled = false; action.checked = true; action.changes_data = true;
    action.representation = model::ButtonRepresentation::picture_text;
    action.shortcut = {"A", false, true, false};
    action.picture = model::PictureRef{model::PictureAssetRef{}, model::QualifiedName{"PictureLib.ActivateTask"}};
    model::CommandBarButton divider; divider.name = "Divider"; divider.type = model::CommandBarButtonKind::separator;
    model::CommandBarButton submenu; submenu.name = "More"; submenu.type = model::CommandBarButtonKind::submenu;
    submenu.order = model::CommandBarButtonOrder::ascending;
    submenu.text = "Еще"; submenu.buttons = {action, divider};
    model::CommandBarButton nested; nested.name = "Nested"; nested.type = model::CommandBarButtonKind::submenu;
    nested.order = model::CommandBarButtonOrder::descending; nested.buttons = {divider};
    submenu.buttons.push_back(nested);
    model::CommandBarButton unordered; unordered.name = "Unordered"; unordered.type = model::CommandBarButtonKind::submenu;
    unordered.buttons = {divider};
    std::get<model::ButtonPayload>(button.payload).buttons = {action, divider, submenu, unordered};
    document.add_control(std::move(button));
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "named menu must encode" : encoded.diagnostics().front().message);
    const auto& menu_record = encoded.value().items[1].items[2].items[2].items[1].items[2].items[1].items[12];
    const auto action_records_end = menu_record.items.begin() + 5 + static_cast<std::ptrdiff_t>(std::stoul(menu_record.items[4].atom));
    const auto complete_action_it = std::find_if(menu_record.items.begin() + 5, action_records_end,
        [](const auto& candidate) { return candidate.items.size() > 5 && candidate.items[5].atom == "15"; });
    expect(complete_action_it != action_records_end, "menu action with all optional flags must exist");
    const auto& complete_action = *complete_action_it;
    expect(complete_action.items[5].atom == "15" && complete_action.items[6].items[0].atom == "1" &&
        complete_action.items[7].items[0].atom == "1" && complete_action.items[8].items[0].atom == "4" &&
        complete_action.items[9].items[0].atom == "0",
        "platform flags 15 store ToolTip, Explanation, Picture, Shortcut in that order, not bit order");
    const auto decoded = form_stream::decode_document(encoded.value(), "Menu");
    expect(decoded.ok(), decoded ? "named menu must decode" : decoded.diagnostics().front().message);
    expect(std::get<model::ButtonPayload>(decoded.value().find_control(model::ObjectId{2})->payload).buttons ==
        std::get<model::ButtonPayload>(document.find_control(model::ObjectId{2})->payload).buttons,
        "named recursive menu properties and actions must survive independent encoding and decoding");
    const auto& decoded_buttons = std::get<model::ButtonPayload>(decoded.value().find_control(model::ObjectId{2})->payload).buttons;
    expect(decoded_buttons[2].order == model::CommandBarButtonOrder::ascending &&
        decoded_buttons[2].buttons[2].order == model::CommandBarButtonOrder::descending,
        "each nested submenu order must survive its own footer entry");
    expect(decoded_buttons[3].order == model::CommandBarButtonOrder::none,
        "DontOrder must remain the default footer value");
    const auto repeated = form_stream::encode_document(decoded.value());
    expect(repeated.ok() && list_stream::dump_compact(repeated.value()) == list_stream::dump_compact(encoded.value()),
        "menu identity must be derived deterministically without preserving a source payload");
    const auto menu_at = [](auto& root) -> auto& { return root.items[1].items[2].items[2].items[1].items[2].items[1].items[12]; };
    auto dangling = encoded.value();
    auto& menu = menu_at(dangling);
    const auto count = static_cast<std::size_t>(std::stoul(menu.items[4].atom));
    menu.items[6 + count].items[5] = list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
    expect(!form_stream::decode_document(dangling, "Menu"), "dangling action references must be rejected");
    auto flags = encoded.value(); menu_at(flags).items[5].items[5] = list_stream::ListValue::raw_atom("16");
    expect(!form_stream::decode_document(flags, "Menu"), "unknown menu action flags must be rejected");
    auto cycle = encoded.value();
    menu_at(cycle).items[6 + count].items[10].items[7] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(cycle, "Menu"), "inconsistent submenu targets must be rejected");
    auto empty_order_footer = encoded.value();
    auto& empty_footer = menu_at(empty_order_footer).items[6 + count].items.back().items[2];
    empty_footer.items.clear();
    expect(!form_stream::decode_document(empty_order_footer, "Menu"), "empty menu order footer must be rejected safely");
    auto huge_order_footer = encoded.value();
    menu_at(huge_order_footer).items[6 + count].items.back().items[2].items[0] =
        list_stream::ListValue::raw_atom("18446744073709551615");
    expect(!form_stream::decode_document(huge_order_footer, "Menu"), "oversized menu order footer count must be rejected");
}

void test_button_menu_client_interface_variant_round_trip_and_validation() {
    model::Form form; form.id = model::ObjectId{1}; form.name = "MenuInterfaceVariant";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode button{model::ObjectId{2}, "Menu", model::ButtonPayload{}};
    button.properties().set_explicit(model::PropertyId::from_name("MenuMode"),
        model::EnumerationValue{"MenuMode", "UseExtra"});
    model::CommandBarButton action; action.name = "Run";
    action.action = model::CommandBarAction{"RunHandler", "", {}, {}, {}};
    action.client_interface_variant = model::ClientInterfaceVariant::version8_0;
    model::CommandBarButton nested_action; nested_action.name = "NestedRun";
    nested_action.action = model::CommandBarAction{"NestedHandler", "", {}, {}, {}};
    model::CommandBarButton nested_separator; nested_separator.name = "NestedSeparator";
    nested_separator.type = model::CommandBarButtonKind::separator;
    nested_separator.client_interface_variant = model::ClientInterfaceVariant::version8_2_ordinary_app;
    model::CommandBarButton submenu; submenu.name = "More";
    submenu.type = model::CommandBarButtonKind::submenu;
    submenu.client_interface_variant = model::ClientInterfaceVariant::version8_2_ordinary_app;
    submenu.buttons = {nested_action, nested_separator};
    model::CommandBarButton separator; separator.name = "Separator";
    separator.type = model::CommandBarButtonKind::separator;
    std::get<model::ButtonPayload>(button.payload).buttons = {action, submenu, separator};
    document.add_control(std::move(button));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "menu interface variants must encode" : encoded.diagnostics().front().message);
    const auto menu_at = [](auto& tree) -> auto& {
        return tree.items[1].items[2].items[2].items[1].items[2].items[1].items[12];
    };
    auto& menu = menu_at(encoded.value());
    const auto count = static_cast<std::size_t>(std::stoul(menu.items[4].atom));
    expect(count == 5, "Action, Submenu, Separator, and nested entries must all have action records");
    const std::vector<std::string> expected_variants{"2", "2", "2", "2", "0"};
    for (std::size_t i = 0; i < expected_variants.size(); ++i) {
        const auto& record = menu.items[5 + i];
        expect(record.items.size() == 8 && record.items[5].atom == "0" &&
                   record.items[6].atom == expected_variants[i] && record.items[7].atom == "0",
            "each reverse-preorder menu record must carry its named variant in the first tail and strict zero second tail");
    }
    const auto decoded = form_stream::decode_document(encoded.value(), "MenuInterfaceVariant");
    expect(decoded.ok(), decoded ? "menu interface variants must decode" : decoded.diagnostics().front().message);
    expect(std::get<model::ButtonPayload>(decoded.value().find_control(model::ObjectId{2})->payload).buttons ==
               std::get<model::ButtonPayload>(document.find_control(model::ObjectId{2})->payload).buttons,
        "Action, Submenu, Separator, and nested interface variants must roundtrip exactly");

    auto changed_form = document.form();
    model::OrdinaryFormDocument changed_document(std::move(changed_form));
    auto changed_button = *document.find_control(model::ObjectId{2});
    auto& changed_entries = std::get<model::ButtonPayload>(changed_button.payload).buttons;
    changed_entries[0].client_interface_variant = model::ClientInterfaceVariant::version8_2_ordinary_app;
    changed_document.add_control(std::move(changed_button));
    auto changed = form_stream::encode_document(changed_document);
    expect(changed.ok(), "editing Version8_0 to Version8_2_OrdinaryApp must encode");
    auto expected_changed = encoded.value();
    menu_at(expected_changed).items[9].items[6] = list_stream::ListValue::raw_atom("2");
    expect(list_stream::dump_compact(changed.value()) == list_stream::dump_compact(expected_changed),
        "editing the client interface variant must change only its first tail atom from 0 to 2");

    for (const auto& invalid : {"1", "3", "-1"}) {
        auto malformed = encoded.value();
        menu_at(malformed).items[5].items[6] = list_stream::ListValue::raw_atom(invalid);
        const auto rejected = form_stream::decode_document(malformed, "InvalidClientInterfaceVariant");
        expect(!rejected && rejected.diagnostics().front().code == "OOF1114" &&
                   rejected.diagnostics().front().path == "$/1/2/2/1/2/1/12/5/6",
            "unknown and negative client interface variants must be rejected at their exact tail slot: " +
                (rejected ? "decoded" : rejected.diagnostics().front().code + ":" + rejected.diagnostics().front().path));
    }
    auto quoted = encoded.value();
    menu_at(quoted).items[5].items[6] = list_stream::ListValue::string_atom("2");
    expect(!form_stream::decode_document(quoted, "QuotedClientInterfaceVariant"),
        "quoted client interface variants must be rejected");
    auto nested_value = encoded.value();
    menu_at(nested_value).items[5].items[6] = list_stream::ListValue::list({list_stream::ListValue::raw_atom("2")});
    expect(!form_stream::decode_document(nested_value, "ListedClientInterfaceVariant"),
        "list-valued client interface variants must be rejected");
    auto wrong_second_tail = encoded.value();
    menu_at(wrong_second_tail).items[5].items[7] = list_stream::ListValue::raw_atom("2");
    const auto partial_second_tail = form_stream::decode_document(wrong_second_tail,
        "WrongClientInterfaceVariantSecondTail");
    expect(partial_second_tail.ok() && !partial_second_tail.value().reconstruction_complete() &&
               std::any_of(partial_second_tail.diagnostics().begin(), partial_second_tail.diagnostics().end(),
                   [](const auto& diagnostic) {
                       return diagnostic.code == "OOF1140" &&
                           diagnostic.severity == oof::DiagnosticSeverity::warning &&
                           diagnostic.path == "$/1/2/2/1/2/1/12/5/7";
                   }),
        "valid unknown second menu tail value must warn at its record slot and preserve the model");
    const auto* partial_menu_button = partial_second_tail.value().find_control(model::ObjectId{2});
    expect(partial_menu_button != nullptr && std::get<model::ButtonPayload>(partial_menu_button->payload).buttons.size() ==
               std::get<model::ButtonPayload>(document.find_control(model::ObjectId{2})->payload).buttons.size(),
        "known menu entries and client interface variants must survive an unknown second tail value");
    const auto partial_menu_reencoded = form_stream::encode_document(partial_second_tail.value());
    expect(partial_menu_reencoded.ok(), "partial menu profile must remain buildable through the primary writer");

    auto invalid_model = document.form();
    model::OrdinaryFormDocument invalid_document(std::move(invalid_model));
    auto invalid_button = *document.find_control(model::ObjectId{2});
    std::get<model::ButtonPayload>(invalid_button.payload).buttons[0].client_interface_variant =
        static_cast<model::ClientInterfaceVariant>(255);
    invalid_document.add_control(std::move(invalid_button));
    expect(!form_stream::encode_document(invalid_document), "writer must reject an invalid enum cast");
}

void test_automatic_button_text_without_explicit_text_round_trip() {
    model::Form form; form.id = model::ObjectId{1}; form.name = "AutomaticMenuText";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode button{model::ObjectId{2}, "Menu", model::ButtonPayload{}};
    button.properties().set_explicit(model::PropertyId::from_name("MenuMode"), model::EnumerationValue{"MenuMode", "UseExtra"});
    model::CommandBarButton action;
    action.name = "RenamedPanelAction";
    action.action = model::CommandBarAction{"ActionCaptionHandler", "",
        model::LocalizedStringValue{{{"ru", "ActionCaption"}}}, {}, {}};
    std::get<model::ButtonPayload>(button.payload).buttons = {action};
    document.add_control(std::move(button));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "automatic menu text fixture must encode" : encoded.diagnostics().front().message);
    const auto menu_at = [](auto& tree) -> auto& {
        return tree.items[1].items[2].items[2].items[1].items[2].items[1].items[12];
    };
    const auto automatic_text_record = [](std::string_view text) {
        return list_stream::ListValue::list({
            list_stream::ListValue::raw_atom("1"),
            list_stream::ListValue::raw_atom("1"),
            list_stream::ListValue::list({
                list_stream::ListValue::string_atom("#"),
                list_stream::ListValue::string_atom(std::string(text)),
            }),
        });
    };
    auto automatic = encoded.value();
    auto& automatic_menu = menu_at(automatic);
    const auto action_count = static_cast<std::size_t>(std::stoul(automatic_menu.items[4].atom));
    auto& properties = automatic_menu.items[6 + action_count].items[6];
    expect(properties.items[5].atom == "0", "fixture Text must remain absent");
    properties.items[4] = automatic_text_record("Renamed panel action");

    const auto decoded = form_stream::decode_document(automatic, "AutomaticMenuText");
    expect(decoded.ok(), decoded ? "single # automatic caption must decode" : decoded.diagnostics().front().message);
    const auto* decoded_control = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_payload = decoded_control ? std::get_if<model::ButtonPayload>(&decoded_control->payload) : nullptr;
    expect(decoded_payload && decoded_payload->buttons == std::vector<model::CommandBarButton>{action},
        "automatic caption must not become explicit Text or replace independent Action.Text");
    const auto rebuilt = form_stream::encode_document(decoded.value());
    expect(rebuilt.ok(), rebuilt ? "automatic caption model must rebuild" : rebuilt.diagnostics().front().message);
    expect(list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
        "rebuild must restore empty computed caption while keeping Text absent");

    const auto rejects_property = [&](const list_stream::ListValue& value, std::string_view label) {
        auto invalid = encoded.value();
        auto& invalid_menu = menu_at(invalid);
        const auto count = static_cast<std::size_t>(std::stoul(invalid_menu.items[4].atom));
        invalid_menu.items[6 + count].items[6].items[4] = value;
        expect(!form_stream::decode_document(invalid, "InvalidAutomaticMenuText"), label);
    };
    rejects_property(list_stream::ListValue::list({
        list_stream::ListValue::raw_atom("1"), list_stream::ListValue::raw_atom("1"),
        list_stream::ListValue::list({list_stream::ListValue::string_atom("#")}),
    }), "malformed automatic localized caption must be rejected");
    rejects_property(list_stream::ListValue::list({
        list_stream::ListValue::raw_atom("1"), list_stream::ListValue::raw_atom("2"),
        list_stream::ListValue::list({list_stream::ListValue::string_atom("#"), list_stream::ListValue::string_atom("Auto")}),
        list_stream::ListValue::list({list_stream::ListValue::string_atom("ru"), list_stream::ListValue::string_atom("Extra")}),
    }), "multilingual absent Text must be rejected");
    rejects_property(list_stream::ListValue::list({
        list_stream::ListValue::raw_atom("1"), list_stream::ListValue::raw_atom("1"),
        list_stream::ListValue::list({list_stream::ListValue::string_atom("ru"), list_stream::ListValue::string_atom("Localized")}),
    }), "ru localized Text with absent flag must be rejected");
}

void test_command_bar_owner_pair_and_strict_profile() {
    model::Form form;
    form.id = model::ObjectId{1}; form.name = "CommandBarOwnerPair";
    form.children = {model::ControlRef{model::ObjectId{1}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode command_bar{model::ObjectId{1}, "Tools", model::CommandBarPayload{}};
    command_bar.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    command_bar.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::string("Run tools"));
    model::CommandBarButton action; action.name = "Run"; action.action = model::CommandBarAction{"RunHandler", "", {}, {}, {}};
    action.picture = model::PictureRef{model::PictureAssetRef{}, model::QualifiedName{"PictureLib.ActivateTask"}};
    model::CommandBarButton submenu; submenu.name = "More";
    submenu.type = model::CommandBarButtonKind::submenu;
    submenu.buttons = {action};
    std::get<model::CommandBarPayload>(command_bar.payload).buttons = {submenu};
    document.add_control(std::move(command_bar));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "CommandBar with owner and submenu ID 1 must encode" :
        encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "CommandBarOwnerPair");
    expect(decoded.ok(), decoded ? "CommandBar root pair and submenu pair must decode" :
        decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* control = decoded.value().find_control(model::ObjectId{1});
    expect(control != nullptr && control->kind() == model::ControlKind::command_bar &&
        std::get<model::CommandBarPayload>(control->payload).buttons ==
            std::get<model::CommandBarPayload>(document.find_control(model::ObjectId{1})->payload).buttons,
        "root (marker,1) and submenu (header owner,1) must remain separate groups");
    expect(!std::get<bool>(control->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
        std::get<std::string>(control->properties().find(model::PropertyId::from_name("ToolTip"))->value) == "Run tools",
        "CommandBar named Enabled and ToolTip values must round-trip independently of Buttons");

    auto unknown_default = encoded.value();
    auto& control_record = unknown_default.items[1].items[2].items[2].items[1];
    control_record.items[2].items[1].items[1] = list_stream::ListValue::raw_atom("1");
    const auto rejected = form_stream::decode_document(unknown_default, "CommandBarUnknownDefault");
    expect(!rejected, "noncanonical unmodeled CommandBar default slot must be rejected");

    auto wrong_root_marker = encoded.value();
    wrong_root_marker.items[1].items[2].items[2].items[1].items[2].items[1].items[8] =
        list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
    expect(!form_stream::decode_document(wrong_root_marker, "CommandBarWrongRootMarker"),
        "unsupported root owner marker must be rejected");
    auto wrong_root_id = encoded.value();
    wrong_root_id.items[1].items[2].items[2].items[1].items[2].items[1].items[9] =
        list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(wrong_root_id, "CommandBarWrongRootId"),
        "root group reference without a matching collection must be rejected");
    auto separate_root_id = encoded.value();
    auto& separate_properties = separate_root_id.items[1].items[2].items[2].items[1].items[2].items[1];
    separate_properties.items[9] = list_stream::ListValue::raw_atom("17");
    auto& separate_menu = separate_properties.items[7];
    const auto action_count = static_cast<std::size_t>(std::stoul(separate_menu.items[4].atom));
    separate_menu.items[6 + action_count].items[2] = list_stream::ListValue::raw_atom("17");
    const auto separate_decoded = form_stream::decode_document(separate_root_id, "CommandBarSeparateRootId");
    expect(separate_decoded.ok(), separate_decoded ? "separate root group identity must decode" :
        separate_decoded.diagnostics().front().message);
    expect(std::get<model::CommandBarPayload>(separate_decoded.value().find_control(model::ObjectId{1})->payload).buttons ==
        std::get<model::CommandBarPayload>(document.find_control(model::ObjectId{1})->payload).buttons,
        "separate root group identity must preserve nested named actions");
    const auto separate_rebuilt = form_stream::encode_document(separate_decoded.value());
    expect(separate_rebuilt.ok() && list_stream::dump_compact(separate_rebuilt.value()) ==
        list_stream::dump_compact(encoded.value()),
        "fresh serializer must derive menu group identity without retaining source records");
    auto unsupported_base_leaf = encoded.value();
    unsupported_base_leaf.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[20] =
        list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(unsupported_base_leaf, "CommandBarUnknownBaseLeaf"),
        "noncanonical unmodeled CommandBar base leaf must be rejected");

    const auto encode_owner_four = [](std::vector<model::CommandBarButton> entries) {
        model::Form owner_form; owner_form.id = model::ObjectId{1}; owner_form.name = "CommandBarOwnerFour";
        owner_form.children = {model::ControlRef{model::ObjectId{4}}};
        model::OrdinaryFormDocument owner_document(std::move(owner_form));
        model::ControlNode owner_bar{model::ObjectId{4}, "Tools", model::CommandBarPayload{}};
        std::get<model::CommandBarPayload>(owner_bar.payload).buttons = std::move(entries);
        owner_document.add_control(std::move(owner_bar));
        return form_stream::encode_document(owner_document);
    };
    const auto menu_for_owner_four = [](const list_stream::ListValue& payload) -> const list_stream::ListValue& {
        const auto& records = payload.items[1].items[2].items[2].items;
        const auto record = std::ranges::find(records, std::string("4"), [](const auto& row) {
            return row.items.size() > 1 ? row.items[1].atom : std::string{};
        });
        if (record == records.end()) throw std::runtime_error("CommandBar owner ID 4 record is absent");
        return record->items[2].items[1].items[7];
    };
    const auto empty_owner_four = encode_owner_four({});
    expect(empty_owner_four.ok() && menu_for_owner_four(empty_owner_four.value()).items[2].atom == "0",
        "empty menu max ID must stay in menu-entry namespace even when root ID is 4");
    auto empty_zero_footer = empty_owner_four.value();
    auto& empty_menu = empty_zero_footer.items[1].items[2].items[2].items[1].items[2].items[1].items[7];
    empty_menu.items.back().items.back().items[0] = list_stream::ListValue::raw_atom("0");
    const auto empty_decoded = form_stream::decode_document(empty_zero_footer, "EmptyCommandBarZeroFooter");
    expect(empty_decoded.ok() && std::get<model::CommandBarPayload>(
        empty_decoded.value().find_control(model::ObjectId{4})->payload).buttons.empty(),
        "native zero footer of an empty CommandBar root must decode into empty named buttons");
    const auto empty_rebuilt = form_stream::encode_document(empty_decoded.value());
    expect(empty_rebuilt.ok() && list_stream::dump_compact(empty_rebuilt.value()) ==
        list_stream::dump_compact(empty_owner_four.value()),
        "empty root footer variant must rebuild from named state without source preservation");
    auto wrong_empty_footer = empty_zero_footer;
    wrong_empty_footer.items[1].items[2].items[2].items[1].items[2].items[1].items[7].items.back().items.back().items[1] =
        list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(wrong_empty_footer, "EmptyCommandBarWrongFooter"),
        "zero footer must not bypass the remaining empty collection contract");
    model::CommandBarButton one_entry; one_entry.name = "Only"; one_entry.action = model::CommandBarAction{"OnlyHandler", "", {}, {}, {}};
    const auto one_entry_owner_four = encode_owner_four({one_entry});
    expect(one_entry_owner_four.ok() && menu_for_owner_four(one_entry_owner_four.value()).items[2].atom == "1",
        "one-entry menu max ID must be 1, independent of root owner ID 4");
    auto nonempty_zero_footer = one_entry_owner_four.value();
    nonempty_zero_footer.items[1].items[2].items[2].items[1].items[2].items[1].items[7].items.back().items.back().items[0] =
        list_stream::ListValue::raw_atom("0");
    expect(!form_stream::decode_document(nonempty_zero_footer, "NonemptyCommandBarZeroFooter"),
        "zero footer must remain unsupported for a nonempty collection");
}

void test_command_bar_five_named_properties_and_invalid_variants() {
    const auto make_document = [](const std::string& orientation, const std::string& alignment,
                                  bool auto_fill, bool transparent, model::ColorValue color) {
        model::Form form; form.id = model::ObjectId{1}; form.name = "CommandBarProperties";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode bar{model::ObjectId{2}, "Tools", model::CommandBarPayload{}};
        bar.properties().set_explicit(model::PropertyId::from_name("Orientation"), model::EnumerationValue{"Orientation", orientation});
        bar.properties().set_explicit(model::PropertyId::from_name("ButtonsAlignment"), model::EnumerationValue{"CommandBarButtonAlignment", alignment});
        bar.properties().set_explicit(model::PropertyId::from_name("AutoFill"), auto_fill);
        bar.properties().set_explicit(model::PropertyId::from_name("Transparent"), transparent);
        bar.properties().set_explicit(model::PropertyId::from_name("ButtonBackColor"), color);
        document.add_control(std::move(bar));
        return document;
    };
    const auto properties_at = [](auto& tree) -> auto& { return tree.items[1].items[2].items[2].items[1].items[2].items[1]; };
    model::ColorValue rgb; rgb.kind = model::ColorKind::absolute; rgb.red = 11; rgb.green = 44; rgb.blue = 77;
    for (const auto& [orientation, orientation_code] : std::array<std::pair<std::string, int>, 3>{{{"Vertical", 0}, {"Horizontal", 1}, {"Auto", 2}}})
        for (const auto& [alignment, alignment_code] : std::array<std::pair<std::string, int>, 3>{{{"Left", 0}, {"Center", 1}, {"Right", 2}}})
            for (const auto auto_fill : {false, true}) for (const auto transparent : {false, true})
                for (const auto color : {model::ColorValue{}, rgb}) {
                    const auto document = make_document(orientation, alignment, auto_fill, transparent, color);
                    const auto encoded = form_stream::encode_document(document);
                    expect(encoded.ok(), "typed CommandBar property combination must encode");
                    const auto& properties = properties_at(encoded.value());
                    expect(properties.items[2].atom == std::to_string(orientation_code) &&
                        properties.items[4].atom == std::to_string(alignment_code) &&
                        properties.items[3].atom == (auto_fill ? "1" : "0") &&
                        properties.items[0].items[5].atom == (transparent ? "1" : "0"),
                        "CommandBar codes must match independent native setters for all combinations");
                    expect(list_stream::dump_compact(properties.items[0].items[9]) ==
                        (color.kind == model::ColorKind::automatic ? "{4,4,{0},4}" : "{4,0,{5057547},0}"),
                        "ButtonBackColor must use its independently captured absolute/automatic records");
                    const auto decoded = form_stream::decode_document(encoded.value(), "CommandBarProperties");
                    expect(decoded.ok(), "typed CommandBar property combination must decode");
                    const auto xml = source::serialize_form_xml(decoded.value());
                    expect(xml.ok(), "typed CommandBar properties must serialize as named XML");
                    const auto parsed = source::parse_form_xml(xml.value());
                    expect(parsed.ok(), "typed CommandBar XML must parse");
                    const auto rebuilt = form_stream::encode_document(parsed.value());
                    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
                        "all five properties must rebuild without a source binary");
                }
    const auto canonical = form_stream::encode_document(make_document("Auto", "Left", false, false, {}));
    expect(canonical.ok(), "default properties fixture must encode");
    for (const auto index : {2u, 4u}) for (const auto atom : {"3", "-1"}) {
        auto invalid = canonical.value(); properties_at(invalid).items[index] = list_stream::ListValue::raw_atom(atom);
        expect(!form_stream::decode_document(invalid, "UnknownEnumCode"), "unknown enum codes must reject");
    }
    auto invalid_fill = canonical.value(); properties_at(invalid_fill).items[3] = list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(invalid_fill, "InvalidAutoFill"), "AutoFill must remain boolean");
    auto invalid_transparency = canonical.value(); properties_at(invalid_transparency).items[0].items[5] = list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(invalid_transparency, "InvalidTransparent"), "Transparent must remain boolean");
    expect(!form_stream::encode_document(make_document("Diagonal", "Left", false, false, {})), "unknown orientation member must reject");
    expect(!form_stream::encode_document(make_document("Auto", "Justify", false, false, {})), "unknown alignment member must reject");
    rgb.alpha = 0;
    expect(!form_stream::encode_document(make_document("Auto", "Left", false, false, rgb)), "ButtonBackColor must retain opaque RGB invariant");
}

void test_command_bar_named_action_source_references() {
    const auto make_document = [](std::optional<model::PropertyValue> source) {
        model::Form form; form.id = model::ObjectId{1}; form.name = "ActionSources";
        form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode bar{model::ObjectId{2}, "Tools", model::CommandBarPayload{}};
        if (source) bar.extension_properties.set_explicit(model::PropertyId::from_name("ActionSource"), *source);
        document.add_control(std::move(bar));
        model::ControlNode html{model::ObjectId{3}, "Browser", model::HtmlDocumentFieldPayload{}};
        document.add_control(std::move(html));
        return document;
    };
    const std::array<std::pair<std::optional<model::PropertyValue>, std::string_view>, 4> sources{{
        {std::nullopt, "4294967295"},
        {model::UndefinedValue{}, "4294967295"},
        {model::FormRef{model::ObjectId{1}}, "0"},
        {model::ControlRef{model::ObjectId{3}}, "3"}
    }};
    for (const auto& [source_value, expected] : sources) {
        const auto document = make_document(source_value);
        const auto xml = source::serialize_form_xml(document);
        expect(xml.ok(), "ActionSource must use named XML references");
        auto loaded = source::parse_form_xml(xml.value());
        expect(loaded.ok(), "named ActionSource XML must parse");
        const auto encoded = form_stream::encode_document(loaded.value());
        expect(encoded.ok(), "fresh ActionSource XML must encode without baseline");
        expect(encoded.value().items[1].items[2].items[2].items[1].items[4].items[2].atom == expected,
            "ActionSource metadata must match independent native mapping");
        const auto decoded = form_stream::decode_document(encoded.value(), "ActionSources");
        expect(decoded.ok(), "ActionSource records must decode");
        const auto repeated = source::serialize_form_xml(decoded.value());
        expect(repeated.ok(), "decoded ActionSource must serialize");
        if (expected == "0") expect(repeated.value().find("formId=\"1\"") != std::string::npos, "form source must remain named");
        if (expected == "3") expect(repeated.value().find("controlId=\"3\"") != std::string::npos, "control source must remain named");
        const auto rebuilt = form_stream::encode_document(decoded.value());
        expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
            "ActionSource canonical stream must round-trip");
    }
    for (const auto invalid : {model::PropertyValue{model::FormRef{model::ObjectId{9}}},
                              model::PropertyValue{model::ControlRef{model::ObjectId{9}}},
                              model::PropertyValue{model::ControlRef{model::ObjectId{2}}},
                              model::PropertyValue{true}})
        expect(!form_stream::encode_document(make_document(invalid)), "invalid ActionSource must be rejected");
    auto tree = form_stream::encode_document(make_document(std::nullopt)).value();
    auto& metadata = tree.items[1].items[2].items[2].items[1].items[4];
    metadata.items[2] = list_stream::ListValue::raw_atom("9");
    expect(!form_stream::decode_document(tree, "ActionSources"), "dangling ActionSource must not decode");
    metadata.items[2] = list_stream::ListValue::raw_atom("4294967296");
    expect(!form_stream::decode_document(tree, "ActionSources"), "out-of-domain ActionSource ID must be rejected");
    metadata.items[2] = list_stream::ListValue::raw_atom("0");
    metadata.items[3] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(tree, "ActionSources"), "neighbor metadata fields remain strict");
}

void test_command_bar_creation_state_and_strict_record_guards() {
    for (const auto nonempty : {false, true}) {
        model::Form form; form.id = model::ObjectId{1}; form.name = "CommandBarCreationState";
        form.children = {model::ControlRef{model::ObjectId{4}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode bar{model::ObjectId{4}, "Tools", model::CommandBarPayload{}};
        bar.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::string("Own state test"));
        if (nonempty) {
            bar.properties().set_explicit(model::PropertyId::from_name("Secondary"), false);
            model::CommandBarButton action; action.name = "Run"; action.action = model::CommandBarAction{"RunHandler", "", {}, {}, {}}; action.default_button = true;
            std::get<model::CommandBarPayload>(bar.payload).buttons = {action};
        }
        document.add_control(std::move(bar));
        const auto canonical = form_stream::encode_document(document);
        expect(canonical.ok(), "own CommandBar fixture must encode");
        auto created = canonical.value();
        const auto base_at = [](auto& tree) -> auto& {
            return tree.items[1].items[2].items[2].items[1].items[2].items[1].items[0];
        };
        base_at(created).items[17] = list_stream::ListValue::raw_atom("1");
        const auto decoded = form_stream::decode_document(created, "CommandBarCreationState");
        expect(decoded.ok(), "observed creation state must decode for empty and default-action menus");
        const auto xml = source::serialize_form_xml(decoded.value());
        const auto canonical_document = form_stream::decode_document(canonical.value(), "CommandBarCreationState");
        const auto canonical_xml = source::serialize_form_xml(canonical_document.value());
        expect(xml.ok() && canonical_xml.ok() && xml.value() == canonical_xml.value(),
            "creation state must not become a public property or alter named menu semantics");
        const auto parsed = source::parse_form_xml(xml.value());
        expect(parsed.ok(), "named CommandBar XML must parse");
        const auto rebuilt = form_stream::encode_document(parsed.value());
        expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(canonical.value()),
            "XML-only rebuild must derive canonical runtime state while retaining default action and tooltip");
        for (const auto state : {"0", "3", "-1", "01"}) {
            auto invalid = created; base_at(invalid).items[17] = list_stream::ListValue::raw_atom(state);
            expect(!form_stream::decode_document(invalid, "UnknownCreationState"), "unproven internal states must reject");
        }
        auto quoted = created; base_at(quoted).items[17] = list_stream::ListValue::string_atom("1");
        expect(!form_stream::decode_document(quoted, "QuotedCreationState"), "state must be a raw integer atom");
        auto unrelated = created; base_at(unrelated).items[16] = list_stream::ListValue::raw_atom("1");
        expect(!form_stream::decode_document(unrelated, "UnprovenAdjacentField"),
            "known state must not hide an unmodeled adjacent variation");
    }
}

void test_command_bar_border_named_round_trip_and_guards() {
    const auto make_document = [](const model::BorderValue& border) {
        model::Form form; form.id = model::ObjectId{1}; form.name = "CommandBarBorder";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode bar{model::ObjectId{2}, "Tools", model::CommandBarPayload{}};
        bar.properties().set_explicit(model::PropertyId::from_name("Border"), border);
        document.add_control(std::move(bar));
        return document;
    };
    const std::array<std::pair<model::ControlBorderType, std::uint32_t>, 8> native_cases{{
        {model::ControlBorderType::without_border, 0}, {model::ControlBorderType::single, 1},
        {model::ControlBorderType::double_line, 200}, {model::ControlBorderType::embossed, 2},
        {model::ControlBorderType::indented, 3}, {model::ControlBorderType::underline, 4},
        {model::ControlBorderType::double_underline, 5}, {model::ControlBorderType::overline, 7}}};
    for (const auto& [type, code] : native_cases) for (const auto width : {0u, 1u, 2u, 5u}) {
        if (type == model::ControlBorderType::without_border && width > 1) continue;
        model::BorderValue border; border.border_type = type; border.width = width;
        const auto document = make_document(border);
        const auto encoded = form_stream::encode_document(document);
        expect(encoded.ok(), encoded ? "typed absolute border must encode" : encoded.diagnostics().front().message);
        const auto& native = encoded.value().items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[11];
        expect(native.items[3].atom == std::to_string(code) && native.items[4].atom == std::to_string(width),
            "absolute Border must use independently observed native type codes and width");
        const auto decoded = form_stream::decode_document(encoded.value(), "CommandBarBorder");
        expect(decoded.ok(), decoded ? "typed border must decode" : decoded.diagnostics().front().message);
        const auto* entry = decoded.value().find_control(model::ObjectId{2})->properties().find(model::PropertyId::from_name("Border"));
        expect(border == model::BorderValue{} ? entry == nullptr : entry != nullptr && std::get<model::BorderValue>(entry->value) == border,
            "default must normalize to implicit while named type and width remain explicit");
        const auto xml = source::serialize_form_xml(decoded.value());
        expect(xml.ok(), "typed border XML must serialize");
        const auto parsed = source::parse_form_xml(xml.value());
        expect(parsed.ok(), "typed border XML must parse");
        const auto rebuilt = form_stream::encode_document(parsed.value());
        expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
            "fresh XML rebuild must retain absolute Border semantics");
    }
    model::BorderValue underline; underline.border_type = model::ControlBorderType::underline; underline.width = 1;
    const auto canonical = form_stream::encode_document(make_document(underline));
    expect(canonical.ok(), "underline must encode");
    const auto border_at = [](auto& stream) -> auto& {
        return stream.items[1].items[2].items[2].items[1].items[2].items[1].items[0].items[11];
    };
    auto created_state = canonical.value(); border_at(created_state).items[5] = list_stream::ListValue::raw_atom("0");
    const auto created = form_stream::decode_document(created_state, "CreatedBorder");
    expect(created.ok(), "observed native creation state must decode into the same named Border");
    const auto created_rebuilt = form_stream::encode_document(created.value());
    expect(created_rebuilt.ok() && list_stream::dump_compact(created_rebuilt.value()) == list_stream::dump_compact(canonical.value()),
        "creation state must be derived without preserving source data");
    for (const auto invalid_state : {1u, 2u, 4u}) {
        auto invalid = canonical.value(); border_at(invalid).items[5] = list_stream::ListValue::raw_atom(std::to_string(invalid_state));
        expect(!form_stream::decode_document(invalid, "UnknownBorderState"), "unproven creation states must be rejected");
    }
    auto invalid_width = canonical.value(); border_at(invalid_width).items[4] = list_stream::ListValue::raw_atom("6");
    expect(!form_stream::decode_document(invalid_width, "UnknownBorderWidth"), "width outside native range must reject");
    for (const auto type : {model::ControlBorderType::underline, model::ControlBorderType::overline}) {
        model::BorderValue named; named.border_type = type; named.width = 1;
        const auto named_stream = form_stream::encode_document(make_document(named));
        auto absent_factory = named_stream.value();
        border_at(absent_factory).items[5] = list_stream::ListValue::raw_atom("0");
        border_at(absent_factory).items[6] = list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
        const auto restored = form_stream::decode_document(absent_factory, "XDTORestoredBorder");
        expect(restored.ok(), "platform XDTO restores the observed named border with an absent factory");
        const auto rebuilt = form_stream::encode_document(restored.value());
        expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(named_stream.value()),
            "absent factory must rebuild from named semantics without source preservation");
    }
    auto unproven_state = canonical.value();
    border_at(unproven_state).items[6] = list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
    expect(!form_stream::decode_document(unproven_state, "UnprovenAbsentFactoryState"),
        "absent factory must not expand beyond the accepted native creation state");
    auto invalid_factory = canonical.value(); border_at(invalid_factory).items[6] = list_stream::ListValue::raw_atom("11111111-1111-4111-8111-111111111111");
    expect(!form_stream::decode_document(invalid_factory, "UnknownBorderFactory"), "unknown nonnull Border factory must reject");
    for (const auto width : {0u, 2u, 5u}) {
        auto unproven_absent = canonical.value();
        border_at(unproven_absent).items[5] = list_stream::ListValue::raw_atom("0");
        border_at(unproven_absent).items[4] = list_stream::ListValue::raw_atom(std::to_string(width));
        border_at(unproven_absent).items[6] = list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
        expect(!form_stream::decode_document(unproven_absent, "UnprovenAbsentFactoryWidth"),
            "absent factory must not expand to unproven widths");
    }
    auto unproven_type = canonical.value();
    border_at(unproven_type).items[5] = list_stream::ListValue::raw_atom("0");
    border_at(unproven_type).items[3] = list_stream::ListValue::raw_atom("1");
    border_at(unproven_type).items[6] = list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
    expect(!form_stream::decode_document(unproven_type, "UnprovenAbsentFactoryType"),
        "absent factory must not expand to unproven named border types");
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto malformed = canonical.value();
        auto& native = border_at(malformed);
        native.items[5] = list_stream::ListValue::raw_atom("0");
        native.items[6] = list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
        if (mutation == 0) native.items[0] = list_stream::ListValue::raw_atom("4");
        if (mutation == 1) native.items[2] = list_stream::parse("{1}");
        if (mutation == 2) native.items[6] = list_stream::ListValue::string_atom("00000000-0000-0000-0000-000000000000");
        if (mutation == 3) native.items[1] = list_stream::ListValue::raw_atom("1");
        expect(!form_stream::decode_document(malformed, "MalformedAbsentFactoryBorder"),
            "restoration must not hide an invalid version, carrier, atom kind or Border kind");
    }
    model::BorderValue style; style.kind = model::BorderKind::style_reference;
    style.style = model::QualifiedName{"StyleBorders.ControlBorder"};
    const auto style_encoded = form_stream::encode_document(make_document(style));
    expect(style_encoded.ok() && list_stream::dump_compact(border_at(style_encoded.value())) == "{3,1,{-18},0,0,0}",
        "named style Border must match its independent native setter record");
    const auto style_decoded = form_stream::decode_document(style_encoded.value(), "StyleBorder");
    expect(style_decoded.ok() && std::get<model::BorderValue>(style_decoded.value().find_control(model::ObjectId{2})->properties().find(
        model::PropertyId::from_name("Border"))->value) == style, "named style reference must survive decode");
    style.style = model::QualifiedName{"StyleBorders.Unknown"};
    expect(!form_stream::encode_document(make_document(style)), "unknown style name must reject");
    model::BorderValue rounded; rounded.border_type = model::ControlBorderType::rounded; rounded.width = 1;
    expect(!form_stream::encode_document(make_document(rounded)), "CommandBar must reject silently ignored Rounded instead of pretending it is applied");
}

void test_command_bar_colors_named_round_trip_and_guards() {
    model::Form form; form.id = model::ObjectId{1}; form.name = "CommandBarColors";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
    const auto make_document = [&](model::ControlNode colored) {
        model::OrdinaryFormDocument result(form);
        result.add_control(std::move(colored));
        result.add_control(model::ControlNode{model::ObjectId{3}, "Default", model::CommandBarPayload{}});
        return result;
    };
    model::ControlNode colored{model::ObjectId{2}, "Colored", model::CommandBarPayload{}};
    const std::array<std::pair<std::string_view, model::ColorValue>, 3> colors{{
        {"BorderColor", model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
            model::QualifiedName{"StyleColors.BorderColor"}}},
        {"ButtonTextColor", model::ColorValue{model::ColorKind::absolute, 44, 55, 66, 255, std::monostate{}}},
        {"BackColor", model::ColorValue{model::ColorKind::absolute, 77, 88, 99, 255, std::monostate{}}}}};
    for (const auto& [name, color] : colors)
        colored.properties().set_explicit(model::PropertyId::from_name(name), color);
    const auto document = make_document(colored);
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "CommandBar colors must encode" : encoded.diagnostics().front().message);
    const auto& base = encoded.value().items[1].items[2].items[2].items[1].items[2].items[1].items[0];
    expect(base.items[6].items[2].items[0].atom == "-22" &&
        base.items[10].items[2].items[0].atom == "4339500" && base.items[2].items[2].items[0].atom == "6510669",
        "independently observed native color slots must contain the matching named values");
    const auto decoded = form_stream::decode_document(encoded.value(), "CommandBarColors");
    expect(decoded.ok(), decoded ? "CommandBar colors must decode" : decoded.diagnostics().front().message);
    for (const auto& [name, color] : colors) {
        const auto* entry = decoded.value().find_control(model::ObjectId{2})->properties().find(model::PropertyId::from_name(name));
        expect(entry != nullptr && std::get<model::ColorValue>(entry->value) == color,
            "each CommandBar color must retain its own value");
        expect(!decoded.value().find_control(model::ObjectId{3})->properties().contains(model::PropertyId::from_name(name)),
            "default sibling colors must stay implicit and independent");
    }
    const auto xml = source::serialize_form_xml(decoded.value());
    expect(xml.ok() && xml.value().find("<ButtonTextColor") != std::string::npos &&
        xml.value().find("<TextColor") == std::string::npos,
        "CommandBar XML must use the platform ButtonTextColor name");
    const auto parsed = source::parse_form_xml(xml.value());
    expect(parsed.ok(), "named CommandBar color XML must parse");
    const auto rebuilt = form_stream::encode_document(parsed.value());
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
        "named XML must independently rebuild all three colors");
    auto automatic_text = colored;
    automatic_text.properties().set_explicit(
        model::PropertyId::from_name("ButtonTextColor"), model::ColorValue{});
    const auto automatic_encoded = form_stream::encode_document(make_document(automatic_text));
    expect(automatic_encoded.ok(), "automatic text is distinct from the style default");
    const auto automatic_decoded = form_stream::decode_document(automatic_encoded.value(), "AutomaticText");
    expect(automatic_decoded.ok() && automatic_decoded.value().find_control(model::ObjectId{2})->properties().contains(
        model::PropertyId::from_name("ButtonTextColor")), "explicit automatic text must not collapse into style default");
    auto alpha = colored; auto transparent = colors[2].second; transparent.alpha = 128;
    alpha.properties().set_explicit(model::PropertyId::from_name("BackColor"), transparent);
    expect(!form_stream::encode_document(make_document(alpha)), "unsupported alpha must be rejected");
    auto unknown = colored; auto style = colors[0].second; style.style = model::QualifiedName{"StyleColors.Unknown"};
    unknown.properties().set_explicit(model::PropertyId::from_name("BorderColor"), style);
    expect(!form_stream::encode_document(make_document(unknown)), "unknown color style must be rejected");
    auto wrong_type = colored;
    wrong_type.properties().set_explicit(model::PropertyId::from_name("BackColor"), false);
    expect(!form_stream::encode_document(make_document(wrong_type)), "color property with Boolean value must be rejected");
}

void test_command_bar_default_button_round_trip_and_guards() {
    model::Form form; form.id = model::ObjectId{1}; form.name = "DefaultAction";
    form.children = {model::ControlRef{model::ObjectId{4}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode bar{model::ObjectId{4}, "Tools", model::CommandBarPayload{}};
    bar.properties().set_explicit(model::PropertyId::from_name("Secondary"), false);
    model::CommandBarButton action; action.name = "Run"; action.action = model::CommandBarAction{"RunHandler", "", {}, {}, {}}; action.default_button = true;
    model::CommandBarButton submenu; submenu.name = "More"; submenu.type = model::CommandBarButtonKind::submenu;
    model::CommandBarButton nested; nested.name = "Nested"; nested.action = model::CommandBarAction{"NestedHandler", "", {}, {}, {}};
    submenu.buttons = {nested};
    std::get<model::CommandBarPayload>(bar.payload).buttons = {submenu, action};
    document.add_control(std::move(bar));
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().message);
    expect(encoded.value().items[1].items[1].items[2].atom == "4", "default Action must select its CommandBar owner in the form header");
    const auto& properties = encoded.value().items[1].items[2].items[2].items[1].items[2].items[1];
    expect(properties.items[11].atom == "3", "selected ID must include preceding submenu and nested items in preorder");
    const auto decoded = form_stream::decode_document(encoded.value(), "DefaultAction");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    expect(std::get<model::CommandBarPayload>(decoded.value().find_control(model::ObjectId{4})->payload).buttons ==
        std::get<model::CommandBarPayload>(document.find_control(model::ObjectId{4})->payload).buttons,
        "default Action and surrounding menu must survive fresh round-trip");
    auto relocated = encoded.value();
    relocated.items[1].items[2].items[2].items[1].items[2].items[1].items[10] =
        list_stream::ListValue::raw_atom("3ffd4fb3-770c-4579-9cbe-44187ca9b300");
    expect(form_stream::decode_document(relocated, "RelocatedDefault").ok(), "platform source UUID relocation must preserve named selection");
    const auto reject_property = [&](std::size_t index, std::string value) {
        auto invalid = encoded.value();
        invalid.items[1].items[2].items[2].items[1].items[2].items[1].items[index] = list_stream::ListValue::raw_atom(std::move(value));
        expect(!form_stream::decode_document(invalid, "InvalidDefault"), "invalid selected action reference must be rejected");
    };
    reject_property(11, "99"); // Missing item.
    reject_property(11, "1"); // Submenu.
    reject_property(11, "2"); // Nested action has unproven default semantics.
    reject_property(11, "0");
    reject_property(10, "9d0a2e40-b978-11d4-84b6-008048da06df");
    reject_property(10, "00000000-0000-0000-0000-000000000000");
    reject_property(5, "1");
    for (const auto value : {"0", "3", "4294967295"}) {
        auto invalid = encoded.value(); invalid.items[1].items[1].items[2] = list_stream::ListValue::raw_atom(value);
        expect(!form_stream::decode_document(invalid, "DanglingOwner"), "missing or mismatched default owner must be rejected");
    }
    auto two_defaults_form = document.form();
    two_defaults_form.children.push_back(model::ControlRef{model::ObjectId{5}});
    model::OrdinaryFormDocument two_defaults(std::move(two_defaults_form));
    two_defaults.add_control(*document.find_control(model::ObjectId{4}));
    auto second_bar = *document.find_control(model::ObjectId{4});
    second_bar.id = model::ObjectId{5}; second_bar.name = "OtherTools";
    two_defaults.add_control(std::move(second_bar));
    const auto multiple_report = two_defaults.validate();
    expect(std::ranges::any_of(multiple_report.violations, [](const auto& violation) {
        return violation.message == "the form may have only one DefaultButton";
    }) && !form_stream::encode_document(two_defaults), "two valid top-level defaults across primary bars must be rejected explicitly");
    auto changed_bar = *document.find_control(model::ObjectId{4});
    auto* payload = std::get_if<model::CommandBarPayload>(&changed_bar.payload);
    const auto changed_document = [&]() {
        model::OrdinaryFormDocument changed(document.form());
        changed.add_control(changed_bar);
        return changed;
    };
    payload->buttons.front().buttons.front().default_button = true;
    expect(!changed_document().validate().ok() && !form_stream::encode_document(changed_document()), "multiple and nested defaults must be rejected in the public model");
    payload->buttons.front().buttons.front().default_button = false;
    payload->buttons.back().default_button = false;
    const auto cleared = form_stream::encode_document(changed_document());
    expect(cleared.ok() && cleared.value().items[1].items[1].items[2].atom == "4294967295", "clearing named default must clear the header reference");
    auto orphan = cleared.value(); orphan.items[1].items[1].items[2] = list_stream::ListValue::raw_atom("4");
    expect(!form_stream::decode_document(orphan, "OrphanOwner"), "a valid CommandBar ID without a selected Action must be rejected");
}

void test_captured_command_bar_control_record_literal() {
    constexpr std::string_view captured_record = R"OOF({e69bf21d-97b2-4f37-86db-675aea9ec2cb,4,{2,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},9,2,0,0,1,1,{5,f6183561-313a-4f39-a1cd-591a0c2b8493,1,1,1,{8,7919d563-1cca-46f4-809c-27d4d06a62a5,1,e1692cc2-605b-4535-84dd-28440238746c,{3,"ProbeHandler",{1,"",{1,0},{1,0},{1,0},{4,0,{0},"",-1,-1,1,0,""},{0,0,0}}},0,0,0},1,{5,b78f2e80-ec68-11d4-9dcf-0050bae2bc79,4,0,1,7919d563-1cca-46f4-809c-27d4d06a62a5,{8,"ProbeAction",0,1,{1,1,{"ru","Probe"}},1,f6183561-313a-4f39-a1cd-591a0c2b8493,1,1e2,0,0,1,0,1,0,0},{-1,0,{0}}}},b78f2e80-ec68-11d4-9dcf-0050bae2bc79,4,9d0a2e40-b978-11d4-84b6-008048da06df,0,0,0}},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,1,2,0,0},{14,"ResearchCommandBar",4294967295,0,0,0},{0}})OOF";
    const auto actual_record = list_stream::parse(captured_record);
    expect(actual_record.is_list && actual_record.items.size() == 6,
        "captured literal CommandBar tuple must retain all six top-level fields");
    expect(actual_record.items[2].items[1].items[7].items[2].atom == "1" &&
        actual_record.items[2].items[1].items[9].atom == "4",
        "captured owner ID 4 tuple confirms menu max ID 1 is not the root ID");

    model::Form form; form.id = model::ObjectId{1}; form.name = "CapturedCommandBar";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{4}}};
    model::OrdinaryFormDocument seeded(std::move(form));
    seeded.add_control(model::ControlNode{model::ObjectId{2}, "Seed", model::ButtonPayload{}});
    seeded.add_control(model::ControlNode{model::ObjectId{4}, "ResearchCommandBar", model::CommandBarPayload{}});
    auto payload = form_stream::encode_document(seeded);
    expect(payload.ok(), "known Form plus control ID 2 seed must encode before literal injection");
    auto& records = payload.value().items[1].items[2].items[2].items;
    const auto target = std::ranges::find(records, std::string("4"), [](const auto& row) {
        return row.items.size() > 1 ? row.items[1].atom : std::string{};
    });
    expect(target != records.end(), "seeded known Form must expose the CommandBar owner record");
    *target = actual_record;
    const auto decoded = form_stream::decode_document(payload.value(), "CapturedCommandBar");
    expect(decoded.ok(), decoded ? "captured full control tuple must decode without rewriting geometry or info" :
        decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* control = decoded.value().find_control(model::ObjectId{4});
    expect(control != nullptr && control->kind() == model::ControlKind::command_bar &&
        control->name == "ResearchCommandBar" &&
        !std::get<model::CommandBarPayload>(control->payload).buttons.empty(),
        "literal actual CommandBar control must be decoded with its named owner and entries");
}

void test_button_menu_mode_round_trip_and_validation() {
    static constexpr std::string_view members[] = {"DontUse", "Use", "UseExtra"};
    const std::string menu_block =
        "{5,53232d71-06b1-4ec1-a94d-77fafadef407,0,1,0,1,"
        "{5,31946946-0a9b-40a2-95cf-82f200778341,0,0,0,{-1,0,{0}}}}";
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "MenuModes";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{4}},
                     model::ControlRef{model::ObjectId{6}}};
    model::OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < std::size(members); ++index) {
        model::ControlNode button{model::ObjectId{static_cast<std::uint64_t>(2 + index * 2)},
            "Button" + std::to_string(index), model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("MenuMode"),
            model::EnumerationValue{"MenuMode", std::string(members[index])});
        document.add_control(std::move(button));
    }

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "all supported Button MenuMode values must encode" :
        encoded.diagnostics().front().message);
    const auto& records = encoded.value().items[1].items[2].items[2].items;
    for (std::size_t index = 0; index < std::size(members); ++index) {
        const auto& properties = records[index + 1].items[2].items[1];
        const auto mode = static_cast<std::int32_t>(index);
        expect(properties.items.size() == (mode == 0 ? 16 : 17) &&
                   properties.items[5].atom == "0" &&
                   properties.items[11].atom == std::to_string(mode),
            "MenuMode must occupy field 11 without disturbing HorizontalAlign or changing the confirmed arity");
        if (mode == 0) {
            expect(properties.items[12].atom == "0" && properties.items[13].atom == "0" &&
                       properties.items[14].atom == "0" && properties.items[15].atom == "1",
                "DontUse must keep the four canonical trailing values in place");
        } else {
            expect(list_stream::dump_compact(properties.items[12]) == menu_block &&
                       properties.items[13].atom == "0" && properties.items[14].atom == "0" &&
                       properties.items[15].atom == "0" && properties.items[16].atom == "1",
                "Use and UseExtra must insert only the confirmed internal menu descriptor before trailing fields");
        }
    }

    const auto decoded = form_stream::decode_document(encoded.value(), "MenuModes");
    expect(decoded.ok(), decoded ? "all supported Button MenuMode values must decode" :
        decoded.diagnostics().front().message);
    expect(decoded.value().find_control(model::ObjectId{2})->properties().find(
               model::PropertyId::from_name("MenuMode")) == nullptr,
        "default DontUse must normalize to an absent explicit property");
    for (const auto& [id, member] : {std::pair{4U, std::string_view("Use")},
                                    std::pair{6U, std::string_view("UseExtra")}}) {
        const auto* value = decoded.value().find_control(model::ObjectId{id})->properties().find(
            model::PropertyId::from_name("MenuMode"));
        expect(value != nullptr && std::get<model::EnumerationValue>(value->value) ==
                   model::EnumerationValue{"MenuMode", std::string(member)},
            "non-default MenuMode member must survive decoding as a named enumeration");
    }
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "all three MenuMode values must encode-decode-encode without storage drift");

    auto wrong_mode = encoded.value();
    wrong_mode.items[1].items[2].items[2].items[1].items[2].items[1].items[11] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(wrong_mode, "MenuModes"), "OOF1114",
        "$/1/2/2/1/2/1/11", "unknown MenuMode storage values must fail closed");
    auto wrong_arity_default = encoded.value();
    wrong_arity_default.items[1].items[2].items[2].items[1].items[2].items[1].items.push_back(
        list_stream::ListValue::raw_atom("0"));
    expect(!form_stream::decode_document(wrong_arity_default, "MenuModes"),
        "DontUse must reject a 17-field record");
    auto wrong_arity_enabled = encoded.value();
    wrong_arity_enabled.items[1].items[2].items[2].items[2].items[2].items[1].items.pop_back();
    expect(!form_stream::decode_document(wrong_arity_enabled, "MenuModes"),
        "Use must reject a 16-field record");
    auto unknown_menu_descriptor = encoded.value();
    unknown_menu_descriptor.items[1].items[2].items[2].items[2].items[2].items[1].items[12].items[0] =
        list_stream::ListValue::raw_atom("6");
    expect(!form_stream::decode_document(unknown_menu_descriptor, "MenuModes"),
        "unknown menu format version must be rejected rather than preserved as raw data");

    model::Form invalid_form;
    invalid_form.id = model::ObjectId{1};
    invalid_form.name = "InvalidMenuMode";
    invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument wrong_type(std::move(invalid_form));
    model::ControlNode wrong_type_button{model::ObjectId{2}, "WrongType", model::ButtonPayload{}};
    wrong_type_button.properties().set_explicit(model::PropertyId::from_name("MenuMode"),
        model::EnumerationValue{"PictureSize", "Use"});
    wrong_type.add_control(std::move(wrong_type_button));
    expect_failure(form_stream::encode_document(wrong_type), "OOF1122", "$/Button/MenuMode",
        "foreign MenuMode enum type must be rejected");

    model::Form unknown_form;
    unknown_form.id = model::ObjectId{1};
    unknown_form.name = "UnknownMenuMode";
    unknown_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument unknown(std::move(unknown_form));
    model::ControlNode unknown_button{model::ObjectId{2}, "Unknown", model::ButtonPayload{}};
    unknown_button.properties().set_explicit(model::PropertyId::from_name("MenuMode"),
        model::EnumerationValue{"MenuMode", "Unknown"});
    unknown.add_control(std::move(unknown_button));
    expect_failure(form_stream::encode_document(unknown), "OOF1122", "$/Button/MenuMode",
        "unknown MenuMode enum members must be rejected");
}

void test_button_external_picture_assets_round_trip() {
    const std::vector<std::vector<std::uint8_t>> bytes{
        {'G','I','F','8','9','a',0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62},
        {137,80,78,71,13,10,26,10,9,8,7,6,5,4},
        {0xff,0xd8,0xff,1,2,3,4,5},
        {'B','M',1,2,3,4,5,6},
    };
    const std::array<model::PictureFormat, 4> formats{
        model::PictureFormat::gif, model::PictureFormat::png,
        model::PictureFormat::jpeg, model::PictureFormat::bmp};
    const std::array<std::string_view, 4> names{"Gif", "Png", "Jpeg", "Bmp"};
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Pictures";
    for (std::size_t index = 0; index < formats.size(); ++index) {
        form.children.push_back(model::ControlRef{model::ObjectId{static_cast<std::uint64_t>(index + 2)}});
    }
    model::OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < formats.size(); ++index) {
        const model::ObjectId button_id{static_cast<std::uint64_t>(index + 2)};
        const model::ObjectId asset_id{static_cast<std::uint64_t>(index + 20)};
        model::PictureAsset asset{asset_id, "Items/" + std::string(names[index]) + "/Picture." +
            std::string(formats[index] == model::PictureFormat::jpeg ? "jpeg" :
                formats[index] == model::PictureFormat::gif ? "gif" :
                formats[index] == model::PictureFormat::png ? "png" : "bmp"), formats[index], bytes[index], index % 2 == 1};
        document.add_asset(std::move(asset));
        model::ControlNode button{button_id, std::string(names[index]), model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{asset_id}});
        document.add_control(std::move(button));
    }
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "named external GIF, PNG, JPEG, and BMP assets must encode");
    const auto decoded = form_stream::decode_document(encoded.value(), "Pictures");
    expect(decoded.ok(), "external Button picture descriptors must decode");
    expect(decoded.value().assets().size() == formats.size(), "each decoded button image must materialize one asset");
    for (std::size_t index = 0; index < formats.size(); ++index) {
        const auto* button = decoded.value().find_control(model::ObjectId{static_cast<std::uint64_t>(index + 2)});
        expect(button != nullptr, "picture button identity must survive");
        const auto* reference = button->properties().find(model::PropertyId::from_name("Picture"));
        expect(reference && std::holds_alternative<model::PictureRef>(reference->value), "picture must remain a named asset reference");
        const auto& asset_ref = std::get<model::PictureRef>(reference->value).asset;
        const auto* asset = decoded.value().find_asset(asset_ref.id());
        expect(asset && asset->format == formats[index] && asset->bytes == bytes[index] && asset->transparent == (index % 2 == 1),
            "picture format, bytes, and transparency must survive independently");
        expect(asset->relative_path == "Items/" + std::string(names[index]) + "/Picture." +
            std::string(formats[index] == model::PictureFormat::jpeg ? "jpeg" :
                formats[index] == model::PictureFormat::gif ? "gif" :
                formats[index] == model::PictureFormat::png ? "png" : "bmp"),
            "decoded asset must receive its named external package path");
    }
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "all four external picture descriptors must re-encode without loss");

    auto missing_bytes = document;
    missing_bytes.set_asset_bytes(model::ObjectId{20}, {});
    expect_failure(form_stream::encode_document(missing_bytes), "OOF1122", "$/Button/Picture",
        "empty asset bytes must be rejected");
    model::Form orphan_form;
    orphan_form.id = model::ObjectId{1};
    orphan_form.name = "Orphan";
    auto orphan = model::OrdinaryFormDocument(std::move(orphan_form));
    orphan.add_asset(model::PictureAsset{model::ObjectId{20}, "Items/Unused/Picture.gif", model::PictureFormat::gif, bytes[0], false});
    expect_failure(form_stream::encode_document(orphan), "OOF1122", "$/PictureAssets",
        "unreferenced external assets must be rejected");

    auto invalid_signature = document;
    invalid_signature.set_asset_bytes(model::ObjectId{20}, {'n','o','t','a','g','i','f'});
    expect_failure(form_stream::encode_document(invalid_signature), "OOF1122", "$/Button/Picture",
        "image signature and declared format must agree");
    auto invalid_transparency = encoded.value();
    invalid_transparency.items[1].items[2].items[2].items[1].items[2].items[1].items[8].items[6] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(invalid_transparency, "Pictures"), "OOF1114", "$/1/2/2/1/2/1/8/6",
        "unknown transparency descriptor value must be rejected");
    auto invalid_base64 = encoded.value();
    invalid_base64.items[1].items[2].items[2].items[1].items[2].items[1].items[8].items[7].items[0].items[0] =
        list_stream::ListValue::raw_atom("#base64:!!!!");
    expect_failure(form_stream::decode_document(invalid_base64, "Pictures"), "OOF1114", "$/1/2/2/1/2/1/8/7/0/0",
        "malformed Button image base64 must be rejected");
}

void test_all_standard_button_pictures_round_trip_without_assets() {
    const auto descriptors = model::metamodel::standard_picture_descriptors();
    expect(descriptors.size() == 294, "the complete sanitized standard picture catalog must be present");
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "StandardPictures";
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        form.children.push_back(model::ControlRef{model::ObjectId{1000 + index}});
    }
    model::OrdinaryFormDocument document(std::move(form));
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto id = model::ObjectId{1000 + index};
        const auto name = "Std" + std::to_string(index);
        model::ControlNode button{id, name, model::ButtonPayload{}};
        button.properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
                model::QualifiedName{std::string(descriptors[index].runtime_name)}});
        document.add_control(std::move(button));
    }
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "all standard-picture descriptors must encode through Button.Picture");
    std::vector<std::string> storage_identities;
    std::function<void(const list_stream::ListValue&)> collect_identities = [&](const auto& value) {
        if (value.is_list && value.items.size() == 9 && value.items[0].atom == "4" &&
            value.items[1].atom == "1" && value.items[2].is_list) {
            storage_identities.push_back(list_stream::dump_compact(value.items[2]));
        }
        for (const auto& item : value.items) collect_identities(item);
    };
    collect_identities(encoded.value());
    expect(storage_identities.size() == descriptors.size(),
        "each standard picture must use the observed list-wrapped identity shape");
    for (const auto& descriptor : descriptors) {
        const std::string identity = descriptor.guid.empty()
            ? "{" + std::to_string(descriptor.storage_id) + "}"
            : "{0," + std::string(descriptor.guid) + "}";
        expect(std::find(storage_identities.begin(), storage_identities.end(), identity) != storage_identities.end(),
            "standard picture descriptor must use its observed GUID or negative-ID list shape");
    }
    const auto decoded = form_stream::decode_document(encoded.value(), "StandardPictures");
    expect(decoded.ok(), "all standard-picture descriptors must decode through Button.Picture");
    expect(decoded.value().assets().empty(), "standard pictures must not create external picture assets");
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto* button = decoded.value().find_control(model::ObjectId{1000 + index});
        const auto* property = button == nullptr ? nullptr : button->properties().find(model::PropertyId::from_name("Picture"));
        expect(property && std::holds_alternative<model::PictureRef>(property->value),
            "standard-picture property must remain a typed reference");
        const auto& reference = std::get<model::PictureRef>(property->value);
        expect(reference.standard_name == model::QualifiedName{std::string(descriptors[index].runtime_name)} &&
                   reference.asset.id().value() == 0,
            "the exact standard descriptor must survive without an asset target");
    }
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "standard-picture streams must re-encode canonically");

    model::Form bad_form;
    bad_form.id = model::ObjectId{1};
    bad_form.name = "UnknownStandard";
    bad_form.children.push_back(model::ControlRef{model::ObjectId{2}});
    model::OrdinaryFormDocument bad(std::move(bad_form));
    model::ControlNode button{model::ObjectId{2}, "Unknown", model::ButtonPayload{}};
    button.properties().set_explicit(model::PropertyId::from_name("Picture"),
        model::PictureRef{model::PictureAssetRef{model::ObjectId{0}}, model::QualifiedName{"PictureLib.Unknown"}});
    bad.add_control(std::move(button));
    expect_failure(form_stream::encode_document(bad), "OOF1123", "$",
        "unknown named standard pictures must be rejected");
}

void test_button_then_label_decoration_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{3}},
        model::ControlRef{model::ObjectId{7}},
        model::ControlRef{model::ObjectId{8}},
        model::ControlRef{model::ObjectId{9}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{
        model::ObjectId{2},
        "Run",
        model::ButtonPayload{},
    });
    model::ControlNode label{
        model::ObjectId{3},
        "Label",
        model::LabelDecorationPayload{},
    };
    label.properties().set_explicit(
        model::PropertyId::from_name("Caption"),
        std::string("Updated caption"));
    label.properties().set_explicit(
        model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"HorizontalAlign", "Auto"});
    label.position.left.set(151);
    label.position.top.set(135);
    label.position.width.set(75);
    label.position.height.set(20);
    label.position.visible.set(false);
    document.add_control(std::move(label));
    model::ControlNode left_label{
        model::ObjectId{7}, "LeftLabel", model::LabelDecorationPayload{}};
    left_label.properties().set_explicit(
        model::PropertyId::from_name("Caption"), std::string("Explicit left"));
    left_label.properties().set_explicit(
        model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"HorizontalAlign", "Left"});
    document.add_control(std::move(left_label));
    model::ControlNode center_label{model::ObjectId{8}, "CenterLabel", model::LabelDecorationPayload{}};
    center_label.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"HorizontalAlign", "Center"});
    document.add_control(std::move(center_label));
    model::ControlNode right_label{model::ObjectId{9}, "RightLabel", model::LabelDecorationPayload{}};
    right_label.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
        model::EnumerationValue{"HorizontalAlign", "Right"});
    document.add_control(std::move(right_label));

    const auto* align_descriptor = model::metamodel::find_property(
        model::ControlKind::label_decoration, "HorizontalAlign");
    expect(align_descriptor != nullptr &&
               align_descriptor->persistence == model::metamodel::PersistenceClass::persisted_editable &&
               align_descriptor->storage_codec == model::metamodel::StorageCodec::control_info &&
               align_descriptor->value_codec == model::metamodel::ValueCodec::enumeration,
        "LabelDecoration.HorizontalAlign must have a typed editable storage descriptor");

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "Button followed by LabelDecoration must encode" :
        encoded.diagnostics().front().code + ":" + encoded.diagnostics().front().path + ":" + encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), "Button followed by LabelDecoration must decode");
    expect(decoded.value().form().children.size() == 5, "Button and four LabelDecorations order must survive");
    expect(std::get<model::ControlRef>(decoded.value().form().children[0]).id() == model::ObjectId{2} &&
               std::get<model::ControlRef>(decoded.value().form().children[1]).id() == model::ObjectId{3} &&
               std::get<model::ControlRef>(decoded.value().form().children[2]).id() == model::ObjectId{7} &&
               std::get<model::ControlRef>(decoded.value().form().children[3]).id() == model::ObjectId{8} &&
               std::get<model::ControlRef>(decoded.value().form().children[4]).id() == model::ObjectId{9},
        "mixed supported control IDs and order must survive");
    const auto* decoded_label = decoded.value().find_control(model::ObjectId{3});
    expect(decoded_label != nullptr && decoded_label->kind() == model::ControlKind::label_decoration,
        "LabelDecoration identity must survive round-trip");
    expect(decoded_label->name == "Label", "LabelDecoration name must survive round-trip");
    expect(decoded_label->properties().find(model::PropertyId::from_name("Caption")) != nullptr &&
               std::get<std::string>(decoded_label->properties().find(model::PropertyId::from_name("Caption"))->value) == "Updated caption",
        "named LabelDecoration Caption must survive round-trip");
    expect(decoded_label->position.left.value() == 151 && decoded_label->position.top.value() == 135 &&
               decoded_label->position.width.value() == 75 && decoded_label->position.height.value() == 20,
        "LabelDecoration Position must survive round-trip");
    expect(!decoded_label->position.visible.value(), "LabelDecoration Visible must survive round-trip");
    const auto* decoded_auto = decoded_label->properties().find(model::PropertyId::from_name("HorizontalAlign"));
    expect(decoded_auto != nullptr &&
               std::get<model::EnumerationValue>(decoded_auto->value) ==
                   model::EnumerationValue{"HorizontalAlign", "Auto"},
        "LabelDecoration HorizontalAlign Auto must round-trip");
    const auto* decoded_left = decoded.value().find_control(model::ObjectId{7});
    const auto* decoded_left_align = decoded_left->properties().find(
        model::PropertyId::from_name("HorizontalAlign"));
    expect(decoded_left_align != nullptr &&
               std::get<model::EnumerationValue>(decoded_left_align->value) ==
                   model::EnumerationValue{"HorizontalAlign", "Left"},
        "explicit LabelDecoration HorizontalAlign Left must round-trip");
    for (const auto& [id, member] : {std::pair{model::ObjectId{8}, std::string_view{"Center"}},
                                     std::pair{model::ObjectId{9}, std::string_view{"Right"}}}) {
        const auto* label_control = decoded.value().find_control(id);
        const auto* alignment = label_control->properties().find(model::PropertyId::from_name("HorizontalAlign"));
        expect(alignment && std::get<model::EnumerationValue>(alignment->value) ==
                   model::EnumerationValue{"HorizontalAlign", std::string(member)},
            "LabelDecoration Center and Right must round-trip through their observed storage values");
    }

    const auto rejects_alignment = [](model::EnumerationValue value) {
        model::Form invalid_form;
        invalid_form.id = model::ObjectId{1};
        invalid_form.name = "Invalid";
        invalid_form.children.push_back(model::ControlRef{model::ObjectId{2}});
        model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
        model::ControlNode invalid_label{
            model::ObjectId{2}, "Label", model::LabelDecorationPayload{}};
        invalid_label.properties().set_explicit(
            model::PropertyId::from_name("HorizontalAlign"), std::move(value));
        invalid_document.add_control(std::move(invalid_label));
        return !form_stream::encode_document(invalid_document);
    };
    expect(rejects_alignment(model::EnumerationValue{"VerticalAlign", "Auto"}),
        "HorizontalAlign must reject an enumeration of another type");
    expect(rejects_alignment(model::EnumerationValue{"HorizontalAlign", "Justify"}),
        "runtime-rejected LabelDecoration Justify must remain unsupported");

    auto unknown_storage_value = encoded.value();
    auto& unknown_label = unknown_storage_value.items[1].items[2].items[2].items[2];
    unknown_label.items[2].items[1].items[3] = list_stream::ListValue::raw_atom("3");
    expect(
        !form_stream::decode_document(unknown_storage_value, "Main"),
        "unknown LabelDecoration.HorizontalAlign storage values must be rejected");

    auto unsupported_leaf = encoded.value();
    auto& label_record = unsupported_leaf.items[1].items[2].items[2].items[2];
    label_record.items[3].items[6].items[2] = list_stream::parse("{2,-1,6,7}");
    expect_failure(
        form_stream::decode_document(unsupported_leaf, "Main"),
        "OOF1114",
        "$/1/2/2/2/3/6/2",
        "unsupported LabelDecoration storage leaves must fail closed");
}

void test_label_border_color_round_trip() {
    const auto make_document = [](std::optional<model::ColorValue> color) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "LabelBorderColor";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode label{model::ObjectId{2}, "Caption", model::LabelDecorationPayload{}};
        if (color) label.properties().set_explicit(model::PropertyId::from_name("BorderColor"), *color);
        document.add_control(std::move(label));
        return document;
    };
    const auto label_record = [](list_stream::ListValue& stream) {
        return find_record_with_guid(stream,
            model::metamodel::descriptor_for(model::ControlKind::label_decoration).guid);
    };

    const auto* descriptor = model::metamodel::find_property(
        model::ControlKind::label_decoration, "BorderColor");
    expect(descriptor != nullptr &&
               descriptor->persistence == model::metamodel::PersistenceClass::persisted_editable &&
               descriptor->storage_codec == model::metamodel::StorageCodec::control_base &&
               descriptor->value_codec == model::metamodel::ValueCodec::color,
        "LabelDecoration.BorderColor must have a typed editable base-property descriptor");

    model::ColorValue absolute;
    absolute.kind = model::ColorKind::absolute;
    absolute.red = 17;
    absolute.green = 83;
    absolute.blue = 201;
    const auto style = model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
        model::QualifiedName{"StyleColors.BorderColor"}};
    for (const auto& color : {absolute, style}) {
        auto encoded = form_stream::encode_document(make_document(color));
        expect(encoded.ok(), "absolute and named-style LabelDecoration.BorderColor must encode");
        const auto* record = label_record(encoded.value());
        expect(record != nullptr &&
                   list_stream::dump_compact(record->items[2].items[1].items[0].items[6]) !=
                       "{4,4,{0},4}",
            "LabelDecoration BorderColor must be written in base slot 6");
        if (color.kind == model::ColorKind::style_reference) {
            const auto& slot = record->items[2].items[1].items[0].items[6];
            expect(slot.items[1].atom == "3" && slot.items[2].items[0].atom == "-22",
                "StyleColors.BorderColor must use the native style identifier in slot 6");
        }
        const auto decoded = form_stream::decode_document(encoded.value(), "LabelBorderColor");
        expect(decoded.ok(), "LabelDecoration.BorderColor storage must decode");
        if (!decoded.ok()) continue;
        const auto* entry = decoded.value().find_control(model::ObjectId{2})->properties().find(
            model::PropertyId::from_name("BorderColor"));
        expect(entry != nullptr && std::get<model::ColorValue>(entry->value) == color,
            "LabelDecoration.BorderColor value must survive native storage round-trip");
        const auto xml = source::serialize_form_xml(decoded.value());
        expect(xml.ok() && xml.value().find("<BorderColor") != std::string::npos,
            "LabelDecoration.BorderColor must serialize as named XML");
        if (!xml.ok()) continue;
        const auto parsed = source::parse_form_xml(xml.value());
        expect(parsed.ok(), "named LabelDecoration.BorderColor XML must parse");
        if (!parsed.ok()) continue;
        const auto rebuilt = form_stream::encode_document(parsed.value());
        expect(rebuilt.ok() &&
                   list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
            "named LabelDecoration XML must rebuild the same fresh Form.bin stream");
    }

    auto default_encoded = form_stream::encode_document(make_document(std::nullopt));
    expect(default_encoded.ok(), "LabelDecoration automatic BorderColor default must encode");
    if (default_encoded.ok()) {
        const auto* record = label_record(default_encoded.value());
        expect(record != nullptr &&
                   list_stream::dump_compact(record->items[2].items[1].items[0].items[6]) ==
                       "{4,4,{0},4}",
            "automatic LabelDecoration.BorderColor must use the default native slot-6 value");
        const auto decoded = form_stream::decode_document(default_encoded.value(), "LabelBorderColorDefault");
        const auto* label = decoded ? decoded.value().find_control(model::ObjectId{2}) : nullptr;
        expect(label != nullptr && !label->properties().contains(model::PropertyId::from_name("BorderColor")),
            "automatic LabelDecoration.BorderColor must remain implicit after decode");
    }

    auto unknown = model::ColorValue{model::ColorKind::style_reference, 0, 0, 0, 255,
        model::QualifiedName{"StyleColors.Unknown"}};
    expect_failure(form_stream::encode_document(make_document(unknown)), "OOF1122",
        "$/LabelDecoration/BorderColor",
        "unknown LabelDecoration color styles must fail without fallback");
}

void test_label_partial_reconstruction_warning_and_build() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "LabelPartialRead";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument seed(std::move(form));
    model::ControlNode label{model::ObjectId{2}, "Caption", model::LabelDecorationPayload{}};
    label.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Keep this caption"));
    model::ColorValue rgb{model::ColorKind::absolute, 12, 34, 56, 255, std::monostate{}};
    label.properties().set_explicit(model::PropertyId::from_name("BorderColor"), rgb);
    seed.add_control(std::move(label));

    auto source_stream = form_stream::encode_document(seed);
    expect(source_stream.ok(), "supported Label seed must encode");
    auto* source_label = find_record_with_guid(source_stream.value(),
        model::metamodel::descriptor_for(model::ControlKind::label_decoration).guid);
    expect(source_label != nullptr, "seed stream must contain LabelDecoration");
    auto& label_properties = source_label->items[2].items[1];
    auto& base_properties = label_properties.items[0];
    base_properties.items[17] = list_stream::ListValue::raw_atom("1");
    label_properties.items[15] = list_stream::ListValue::raw_atom("1");

    const auto decoded = form_stream::decode_document(source_stream.value(), "LabelPartialRead");
    expect(decoded.ok(), "valid Label profile variation must return a partial document with warnings");
    expect(decoded.diagnostics().size() == 1 &&
               decoded.diagnostics().front().severity == oof::DiagnosticSeverity::warning &&
               decoded.diagnostics().front().code == "OOF1140" &&
               decoded.diagnostics().front().actual.empty() &&
               decoded.diagnostics().front().expected.empty(),
        "profile warning must be non-fatal and must not expose stored values");
    expect(!decoded.value().reconstruction_complete(), "unsupported Label fields must mark reconstruction incomplete");
    const auto* decoded_label = decoded.value().find_control(model::ObjectId{2});
    expect(decoded_label != nullptr &&
               std::get<std::string>(decoded_label->properties().find(model::PropertyId::from_name("Caption"))->value) ==
                   "Keep this caption" &&
               std::get<model::ColorValue>(decoded_label->properties().find(
                   model::PropertyId::from_name("BorderColor"))->value) == rgb,
        "supported Caption and RGB BorderColor must survive alongside the warning");

    const auto xml = source::serialize_form_xml(decoded.value());
    expect(xml.ok() && xml.value().find("reconstructionComplete=\"false\"") != std::string::npos &&
               xml.value().find("Keep this caption") != std::string::npos &&
               xml.value().find("<BorderColor") != std::string::npos,
        "partial model XML must retain completeness metadata and named properties");
    const auto parsed = source::parse_form_xml(xml.value());
    expect(parsed.ok() && !parsed.value().reconstruction_complete() &&
               !parsed.diagnostics().empty() &&
               parsed.diagnostics().front().severity == oof::DiagnosticSeverity::warning,
        "XML parsing must preserve incompleteness as a warning");
    const auto built = oof::save_form_bin(parsed.value());
    expect(built.ok() && !built.value().empty() &&
               std::any_of(built.diagnostics().begin(), built.diagnostics().end(), [](const auto& diagnostic) {
                   return diagnostic.severity == oof::DiagnosticSeverity::warning &&
                       diagnostic.message.find("unsupported source properties were not restored") != std::string::npos;
               }),
        "build must write Form.bin and warn that unsupported source properties were not restored");

    auto malformed_arity = source_stream.value();
    auto* malformed_label = find_record_with_guid(malformed_arity,
        model::metamodel::descriptor_for(model::ControlKind::label_decoration).guid);
    malformed_label->items[2].items[1].items.push_back(list_stream::ListValue::raw_atom("0"));
    expect_failure(form_stream::decode_document(malformed_arity, "MalformedLabelArity"), "OOF1102",
        "$/1/2/2/1/2/1", "malformed Label properties arity must remain a structural error");

    auto malformed_color = source_stream.value();
    auto* invalid_color_label = find_record_with_guid(malformed_color,
        model::metamodel::descriptor_for(model::ControlKind::label_decoration).guid);
    invalid_color_label->items[2].items[1].items[0].items[6].items[1] =
        list_stream::ListValue::raw_atom("9");
    expect_failure(form_stream::decode_document(malformed_color, "MalformedLabelColor"), "OOF1114",
        "$/1/2/2/1/2/1/0/6/1", "invalid known BorderColor must remain a strict decode error");

    auto malformed_header = source_stream.value();
    auto* invalid_header_label = find_record_with_guid(malformed_header,
        model::metamodel::descriptor_for(model::ControlKind::label_decoration).guid);
    invalid_header_label->items[2].items[1].items[0].items[0] =
        list_stream::ListValue::raw_atom("18");
    expect_failure(form_stream::decode_document(malformed_header, "MalformedLabelHeader"), "OOF1106",
        "$/1/2/2/1/2/1/0/0", "invalid Label structural header must remain a strict decode error");
}

void test_control_unknown_common_state_warning_and_build() {
    const auto exercise = [](model::ControlKind kind, std::string name) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "UnknownControlState";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument seed(std::move(form));
        model::ControlNode control{model::ObjectId{2}, name, model::ButtonPayload{}};
        if (kind == model::ControlKind::picture_decoration) control.payload = model::PictureDecorationPayload{};
        if (kind == model::ControlKind::check_box) {
            control.payload = model::CheckBoxPayload{};
            model::TypeDomainPatternValue boolean_type;
            model::TypeDomainEntry boolean_entry;
            boolean_entry.term = model::TypeDomainTerm::boolean;
            boolean_type.entries.push_back(boolean_entry);
            seed.add_attribute(model::Attribute{model::ObjectId{3}, "Flag", boolean_type});
            control.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        }
        if (kind == model::ControlKind::button || kind == model::ControlKind::check_box)
            control.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Keep caption"));
        control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
        seed.add_control(std::move(control));

        auto stream = form_stream::encode_document(seed);
        expect(stream.ok(), "canonical control fixture must encode before profile variation");
        auto* record = find_record_with_guid(stream.value(), model::metamodel::descriptor_for(kind).guid);
        expect(record != nullptr, "canonical fixture must contain the requested control");
        auto& base_properties = kind == model::ControlKind::check_box
            ? record->items[2].items[1].items[0].items[0]
            : record->items[2].items[1].items[0];
        base_properties.items[17] = list_stream::ListValue::raw_atom("3");

        const auto decoded = form_stream::decode_document(stream.value(), "UnknownControlState");
        expect(decoded.ok(), "unknown valid common state must produce a partial control with warning");
        expect(!decoded.value().reconstruction_complete() && decoded.diagnostics().size() == 1 &&
                   decoded.diagnostics().front().severity == oof::DiagnosticSeverity::warning &&
                   decoded.diagnostics().front().code == "OOF1140",
            "unknown common state must mark reconstruction incomplete and report OOF1140");
        const auto* restored = decoded.value().find_control(model::ObjectId{2});
        expect(restored != nullptr && restored->properties().find(model::PropertyId::from_name("Enabled")) != nullptr &&
                   !std::get<bool>(restored->properties().find(model::PropertyId::from_name("Enabled"))->value),
            "known Enabled=false must survive unknown common state");
        if (kind == model::ControlKind::button || kind == model::ControlKind::check_box)
            expect(restored->properties().find(model::PropertyId::from_name("Caption")) != nullptr &&
                       std::get<std::string>(restored->properties().find(model::PropertyId::from_name("Caption"))->value) ==
                           "Keep caption",
                "known Caption must survive unknown common state");

        const auto xml = source::serialize_form_xml(decoded.value());
        expect(xml.ok(), "partial control model must serialize to XML");
        const auto parsed = source::parse_form_xml(xml.value());
        expect(parsed.ok() && !parsed.value().reconstruction_complete(),
            "partial control XML must preserve reconstruction completeness");
        const auto built = oof::save_form_bin(parsed.value());
        expect(built.ok() && !built.value().empty(),
            "partial control XML must remain buildable to Form.bin");
    };

    exercise(model::ControlKind::button, "Run");
    exercise(model::ControlKind::picture_decoration, "Picture");
    exercise(model::ControlKind::check_box, "FlagControl");
}

void test_label_decoration_observed_center_right_records() {
    const auto decode_observed = [](std::string_view record, std::string_view expected_member) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "ObservedLabelAlignment";
        form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
        model::ControlNode label{model::ObjectId{3}, "Notice", model::LabelDecorationPayload{}};
        label.position.left.set(10);
        label.position.top.set(45);
        label.position.width.set(160);
        label.position.height.set(65);
        document.add_control(std::move(label));
        auto stream = form_stream::encode_document(document);
        expect(stream.ok(), "seed stream must encode before inserting an observed LabelDecoration record");
        stream.value().items[1].items[2].items[2].items[2] = list_stream::parse(record);
        const auto decoded = form_stream::decode_document(stream.value(), "ObservedLabelAlignment");
        expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().code + ":" +
            decoded.diagnostics().front().path + ":" + decoded.diagnostics().front().message);
        const auto* decoded_label = decoded.value().find_control(model::ObjectId{3});
        const auto* alignment = decoded_label == nullptr ? nullptr : decoded_label->properties().find(
            model::PropertyId::from_name("HorizontalAlign"));
        expect(decoded_label != nullptr && decoded_label->name == "Notice" && alignment != nullptr &&
                   std::get<model::EnumerationValue>(alignment->value) ==
                       model::EnumerationValue{"HorizontalAlign", std::string(expected_member)},
            "complete observed LabelDecoration record must decode its named alignment value");
    };
    decode_observed(R"OOF(
{0fc7e20d-f241-460c-bdf4-5ad88e5474a5,3,
{3,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},11,
{1,1,
{"ru","Notice"}
},1,1,0,0,0,
{0,0,0},0,
{1,0},1,
{10,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,2,0,0,1,2},4,0,0,0,0,0,0,0},
{0}
},
{8,10,45,160,65,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,1,2,0,0},
{14,"Notice",4294967295,0,0,0},
{0}
}
)OOF", "Center");
    decode_observed(R"OOF(
{0fc7e20d-f241-460c-bdf4-5ad88e5474a5,3,
{3,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},11,
{1,1,
{"ru","Notice"}
},2,1,0,0,0,
{0,0,0},0,
{1,0},1,
{10,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,2,0,0,1,2},4,0,0,0,0,0,0,0},
{0}
},
{8,10,45,160,65,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,1,2,0,0},
{14,"Notice",4294967295,0,0,0},
{0}
}
)OOF", "Right");
}

void test_gantt_standard_palette_wraparound() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "GanttPaletteWrap";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::GanttChartPayload payload;
    for (std::uint64_t index = 0; index < 18; ++index)
        payload.series.push_back({model::ObjectId{71 + 3 * index}, "S" + std::to_string(index),
            "Series " + std::to_string(index), std::nullopt});
    for (std::uint64_t index = 0; index < 19; ++index)
        payload.points.push_back({model::ObjectId{83 + 4 * index}, "P" + std::to_string(index),
            "Point " + std::to_string(index), std::nullopt});
    payload.intervals.push_back({model::ObjectId{155}, model::ObjectId{122},
        model::DateValue{"2027-02-01T00:00:00"}, model::DateValue{"2027-02-03T00:00:00"}, "Wrap"});
    model::ControlNode gantt{model::ObjectId{2}, "Schedule", std::move(payload)};
    gantt.properties().set_explicit(model::PropertyId::from_name("AutoFullInterval"), false);
    gantt.properties().set_explicit(model::PropertyId::from_name("FullIntervalBegin"), model::DateValue{"2027-01-01T00:00:00"});
    gantt.properties().set_explicit(model::PropertyId::from_name("FullIntervalEnd"), model::DateValue{"2027-03-01T00:00:00"});
    gantt.position.left.set(8);
    gantt.position.top.set(8);
    gantt.position.width.set(400);
    gantt.position.height.set(240);
    document.add_control(std::move(gantt));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "Gantt palette candidate must encode dimensions beyond the former fixture limits");
    const auto& info = encoded.value().items[1].items[2].items[2].items[1].items[2];
    const auto& series_palette = info.items[3].items[1].items[41];
    const auto& point_palette = info.items[2].items[1].items[43];
    expect(series_palette.items[1].atom == "33" && point_palette.items[1].atom == "33",
        "Gantt factory-derived palette must reuse its 33 distinct records after the 16-color cycle");
    for (const auto* palette : {&series_palette, &point_palette}) {
        expect(list_stream::dump_compact(palette->items[34].items[1]) ==
                   "{0,{4,0,{5263440},0},{4,0,{15700567},0}}" &&
                   list_stream::dump_compact(palette->items[4].items[1]) ==
                   "{0,{4,0,{15700567},0},{4,0,{5410297},0}}",
            "Gantt standard ColorManager cycle must wrap from its final RGB to the first and second RGB");
    }
    for (const auto slot : {2u, 3u}) {
        const auto& rows = info.items[slot].items[1].items;
        expect(rows[4].items[1].items[9].atom == (slot == 2 ? "6" : "4") &&
                   rows[6].items[1].items[9].atom == (slot == 2 ? "4" : "2"),
            "Gantt dimensions after ordinal 16 must reuse independently observed factory cache keys");
    }
    const auto decoded = form_stream::decode_document(encoded.value(), "GanttPaletteWrap");
    expect(decoded.ok(), "Gantt large palette candidate must decode to named dimensions");
    const auto* restored = std::get_if<model::GanttChartPayload>(&decoded.value().find_control(model::ObjectId{2})->payload);
    expect(restored && restored->series.size() == 18 && restored->points.size() == 19 &&
               restored->series.back().id == model::ObjectId{122} && restored->points.back().id == model::ObjectId{155} &&
               restored->intervals.size() == 1 && restored->intervals[0].text == "Wrap",
        "Gantt candidate must retain collection order, final dimension references and interval text");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "Gantt large named graph must rebuild without palette drift");
    auto interior_default = encoded.value();
    auto& interior_info = interior_default.items[1].items[2].items[2].items[1].items[2];
    for (const auto slot : {2u, 3u}) {
        auto& rows = interior_info.items[slot].items[1].items;
        const auto count = static_cast<std::size_t>(std::stoul(rows[2].atom)) - 1;
        std::swap(rows[3 + 2 * count], rows[7]);
        std::swap(rows[4 + 2 * count], rows[8]);
    }
    expect(form_stream::decode_document(interior_default, "GanttInteriorDefault").ok(),
        "Gantt default ID0 may occupy an interior physical position in each dimension table");

    auto malformed = encoded.value();
    malformed.items[1].items[2].items[2].items[1].items[2].items[3].items[1].items[41]
        .items[34].items[1].items[2].items[2].items[0] = list_stream::ListValue::raw_atom("0");
    expect(!form_stream::decode_document(malformed, "GanttWrongWrappedSecondary").ok(),
        "Gantt decoder must reject a changed secondary color rather than silently normalize it");
}

void test_label_enabled_and_tooltip_round_trip() {
    const auto encode_label = [](bool explicit_defaults, bool enabled, std::string tool_tip) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "LabelProperties";
        form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
        model::ControlNode label{model::ObjectId{3}, "Notice", model::LabelDecorationPayload{}};
        if (explicit_defaults || !enabled) {
            label.properties().set_explicit(model::PropertyId::from_name("Enabled"), enabled);
        }
        if (explicit_defaults || !tool_tip.empty()) {
            label.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tool_tip));
        }
        document.add_control(std::move(label));
        return form_stream::encode_document(document);
    };

    const auto implicit_defaults = encode_label(false, true, "");
    const auto explicit_defaults = encode_label(true, true, "");
    expect(implicit_defaults.ok() && explicit_defaults.ok(),
        "LabelDecoration defaults must encode with implicit and explicit model values");
    expect(list_stream::dump_compact(implicit_defaults.value()) ==
               list_stream::dump_compact(explicit_defaults.value()),
        "explicit LabelDecoration Enabled=true and empty ToolTip must omit from storage");

    const std::string tool_tip = "Подсказка Ω & <важно> \"цитата\"\nВторая\rстрока";
    const auto variant = encode_label(false, false, tool_tip);
    expect(variant.ok(), variant ? "LabelDecoration Enabled and ToolTip must encode" :
        variant.diagnostics().front().path + ": " + variant.diagnostics().front().message);
    const auto& label_record = variant.value().items[1].items[2].items[2].items[2];
    const auto& label_base = label_record.items[2].items[1].items[0];
    expect(label_base.items.size() == 21 && label_base.items[1].atom == "0" &&
               label_base.items[12].is_list &&
               list_stream::dump_compact(label_base.items[12]) !=
                   list_stream::dump_compact(implicit_defaults.value().items[1].items[2].items[2].items[2]
                       .items[2].items[1].items[0].items[12]),
        "LabelDecoration Enabled and ToolTip must occupy their observed named storage slots");

    const auto decoded = form_stream::decode_document(variant.value(), "LabelProperties");
    expect(decoded.ok(), "LabelDecoration Enabled and ToolTip must decode");
    const auto* label = decoded.value().find_control(model::ObjectId{3});
    expect(label != nullptr, "LabelDecoration must survive property decoding");
    const auto* enabled = label->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* decoded_tool_tip = label->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(enabled && !std::get<bool>(enabled->value),
        "LabelDecoration Enabled=false must survive decoding");
    expect(decoded_tool_tip && std::get<std::string>(decoded_tool_tip->value) ==
               "Подсказка Ω & <важно> \"цитата\"\nВторая\rстрока",
        "LabelDecoration ToolTip must round-trip Unicode, XML punctuation, and mixed newlines");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(variant.value()),
        "LabelDecoration Enabled and ToolTip storage must round-trip without drift");

    auto unsupported_localization = variant.value();
    auto& tooltip_slot = unsupported_localization.items[1].items[2].items[2].items[2]
        .items[2].items[1].items[0].items[12];
    tooltip_slot = list_stream::parse(value_codec::encode_localized_string(
        model::LocalizedStringValue{{{"en", "Hint"}, {"ru", "Подсказка"}}}));
    expect_failure(
        form_stream::decode_document(unsupported_localization, "LabelProperties"),
        "OOF1115",
        "$/1/2/2/2/2/1/0/12",
        "LabelDecoration ToolTip must reject multiple localized values");
}

void test_picture_decoration_default_enabled_tooltip_round_trip_and_rejections() {
    const auto make_document = [](bool explicit_defaults, bool enabled, std::string tool_tip) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "PictureDecorationCodec";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode picture{
            model::ObjectId{2}, "Picture", model::PictureDecorationPayload{}};
        if (explicit_defaults || !enabled) {
            picture.properties().set_explicit(model::PropertyId::from_name("Enabled"), enabled);
        }
        if (explicit_defaults || !tool_tip.empty()) {
            picture.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tool_tip));
        }
        picture.position.left.set(27);
        picture.position.top.set(18);
        picture.position.width.set(96);
        picture.position.height.set(44);
        picture.position.visible.set(false);
        document.add_control(std::move(picture));
        return document;
    };
    const auto picture_record = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };
    const auto mutable_picture_record = [](list_stream::ListValue& encoded) -> list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };
    const auto picture_base = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1].items[2].items[1].items[0];
    };
    const auto mutable_picture_base = [](list_stream::ListValue& encoded) -> list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1].items[2].items[1].items[0];
    };

    constexpr std::string_view observed_control_record = R"RAW(
{151ef23e-6bb2-4681-83d0-35bc2217230c,2,{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},20,0,0,{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,2,0,0,1,2},{0,0,0},1,1,0,0,{1,0},0,1,1,1},{0}},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,1,2,0,0},{14,"Picture",4294967295,0,0,0},{0}}
)RAW";
    const auto captured_picture_record = list_stream::parse(observed_control_record);
    const auto& captured_picture_info = captured_picture_record.items[2];
    const auto& captured_picture_base = captured_picture_info.items[1].items[0];
    const auto implicit_default = form_stream::encode_document(make_document(false, true, ""));
    const auto explicit_default = form_stream::encode_document(make_document(true, true, ""));
    expect(implicit_default.ok() && explicit_default.ok(),
        "default PictureDecoration must encode from a named model without a Form.bin fixture");
    expect(list_stream::dump_compact(implicit_default.value()) ==
               list_stream::dump_compact(explicit_default.value()),
        "explicit default PictureDecoration Enabled and ToolTip must normalize to omitted defaults");

    const auto& default_record = picture_record(implicit_default.value());
    const auto& default_info = default_record.items[2];
    const auto& default_base = picture_base(implicit_default.value());
    expect(default_record.items[0].atom == "151ef23e-6bb2-4681-83d0-35bc2217230c" &&
               default_info.items[0].atom == "1" && default_base.items.size() == 21 &&
               list_stream::dump_compact(default_base) == list_stream::dump_compact(captured_picture_base) &&
               list_stream::dump_compact(default_info.items[1]) ==
                   list_stream::dump_compact(captured_picture_info.items[1]) &&
               list_stream::dump_compact(default_info.items[2]) == "{0}",
        "PictureDecoration must use the observed v1 info record, exact default base, and empty event table");
    model::Form raw_form;
    raw_form.id = model::ObjectId{1};
    raw_form.name = "PictureDecorationObserved";
    raw_form.children = {model::ControlRef{model::ObjectId{3}}, model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument raw_document(std::move(raw_form));
    raw_document.add_control(model::ControlNode{
        model::ObjectId{3}, "BeforePicture", model::ButtonPayload{}});
    raw_document.add_control(model::ControlNode{
        model::ObjectId{2}, "Picture", model::PictureDecorationPayload{}});
    auto raw_envelope = form_stream::encode_document(raw_document);
    expect(raw_envelope.ok(), "named PictureDecoration envelope must be available for captured record test");
    mutable_picture_record(raw_envelope.value()) = captured_picture_record;
    const auto raw_decoded = form_stream::decode_document(raw_envelope.value(), "ObservedPictureDecoration");
    expect(raw_decoded.ok(), raw_decoded ? "actual PictureDecoration control record must decode" :
        raw_decoded.diagnostics().front().path + ": " + raw_decoded.diagnostics().front().message);
    const auto canonical_raw_roundtrip = form_stream::encode_document(raw_decoded.value());
    expect(canonical_raw_roundtrip.ok() &&
               list_stream::dump_compact(picture_record(canonical_raw_roundtrip.value())) ==
                   list_stream::dump_compact(captured_picture_record),
        "actual captured PictureDecoration control record must encode back canonically");

    const auto default_decoded = form_stream::decode_document(implicit_default.value(), "PictureDecorationCodec");
    expect(default_decoded.ok(), "default PictureDecoration record must decode");
    const auto* default_picture = default_decoded.value().find_control(model::ObjectId{2});
    expect(default_picture && default_picture->kind() == model::ControlKind::picture_decoration &&
               default_picture->name == "Picture" &&
               !default_picture->properties().find(model::PropertyId::from_name("Enabled")) &&
               !default_picture->properties().find(model::PropertyId::from_name("ToolTip")),
        "default PictureDecoration identity and implicit property defaults must decode by name");
    expect(default_picture->position.left.value() == 27 && default_picture->position.top.value() == 18 &&
               default_picture->position.width.value() == 96 && default_picture->position.height.value() == 44 &&
               !default_picture->position.visible.value(),
        "PictureDecoration Position and Visible must use the existing named geometry model");

    const std::string tool_tip = "Подсказка Ω <важно> & \"цитата\"\nВторая\rстрока";
    const auto changed = form_stream::encode_document(make_document(false, false, tool_tip));
    expect(changed.ok(), changed ? "PictureDecoration Enabled and ToolTip must encode" :
        changed.diagnostics().front().path + ": " + changed.diagnostics().front().message);
    const auto& changed_base = picture_base(changed.value());
    expect(changed_base.items[1].atom == "0" &&
               list_stream::dump_compact(changed_base.items[12]) == value_codec::encode_localized_string(
                   model::LocalizedStringValue{{{"ru", "Подсказка Ω <важно> & \"цитата\"\r\nВторая\rстрока"}}}),
        "PictureDecoration Enabled and ToolTip must occupy the observed base slots and canonicalize line endings");
    const auto decoded = form_stream::decode_document(changed.value(), "PictureDecorationCodec");
    expect(decoded.ok(), "PictureDecoration Enabled and ToolTip must decode");
    const auto* picture = decoded.value().find_control(model::ObjectId{2});
    const auto* enabled = picture == nullptr ? nullptr :
        picture->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* decoded_tool_tip = picture == nullptr ? nullptr :
        picture->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(enabled && !std::get<bool>(enabled->value) && decoded_tool_tip &&
               std::get<std::string>(decoded_tool_tip->value) == tool_tip,
        "PictureDecoration named properties must round-trip Unicode and mixed line endings");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(changed.value()),
        "PictureDecoration named properties must re-encode canonically");

    constexpr std::string_view tool_tip_path = "$/1/2/2/1/2/1/0/12";
    auto malformed = changed.value();
    mutable_picture_base(malformed).items[12] = list_stream::ListValue::raw_atom("malformed");
    expect_failure(form_stream::decode_document(malformed, "PictureDecorationCodec"), "OOF1108",
        tool_tip_path, "malformed PictureDecoration ToolTip localization must be rejected");

    auto multilingual = changed.value();
    mutable_picture_base(multilingual).items[12] = list_stream::parse(value_codec::encode_localized_string(
        model::LocalizedStringValue{{{"ru", "Текст"}, {"en", "Text"}}}));
    expect_failure(form_stream::decode_document(multilingual, "PictureDecorationCodec"), "OOF1115",
        tool_tip_path, "multilingual PictureDecoration ToolTip must be rejected without loss");

    auto unsupported_leaf = implicit_default.value();
    mutable_picture_base(unsupported_leaf).items[5] = list_stream::ListValue::raw_atom("1");
    const auto partial_picture = form_stream::decode_document(unsupported_leaf, "PictureDecorationCodec");
    expect(partial_picture.ok() && !partial_picture.value().reconstruction_complete() &&
               std::any_of(partial_picture.diagnostics().begin(), partial_picture.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_picture.value().find_control(model::ObjectId{2}) != nullptr,
        "valid unknown PictureDecoration base property must warn and retain the named control");

    auto unsupported_picture_tail = implicit_default.value();
    unsupported_picture_tail.items[1].items[2].items[2].items[1].items[2].items[1].items[1] =
        list_stream::ListValue::raw_atom("19");
    expect_failure(form_stream::decode_document(unsupported_picture_tail, "PictureDecorationCodec"), "OOF1106",
        "$/1/2/2/1/2/1/1", "PictureDecoration properties version header must remain strict");

    auto picture_property_document = make_document(false, true, "");
    const_cast<model::ControlNode*>(picture_property_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
                model::QualifiedName{"PictureLib.Write"}});
    const auto picture_encoded = form_stream::encode_document(picture_property_document);
    expect(picture_encoded.ok(), "standard PictureDecoration.Picture must encode");
    constexpr std::string_view observed_write_control_record = R"RAW(
{151ef23e-6bb2-4681-83d0-35bc2217230c,2,
{1,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},20,0,0,
{10,0,
{4,1,
{0,894cf65b-4109-4533-a1d7-c87b1fcc80a3},"",-1,-1,0,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,2,0,0,1,2},
{0,0,0},1,1,0,0,
{1,0},0,1,1,1},
{0}
},
{8,20,20,140,90,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,0,1,0,0},
{14,"Picture",4294967295,0,0,0},
{0}
}
)RAW";
    auto actual_picture_stream = picture_encoded.value();
    auto& actual_picture_record = actual_picture_stream.items[1].items[2].items[2].items[1];
    actual_picture_record = list_stream::parse(observed_write_control_record);
    const auto actual_picture_decoded = form_stream::decode_document(actual_picture_stream, "ObservedPictureWrite");
    expect(actual_picture_decoded.ok(), "actual PictureDecoration record from runtime after PictureLib.Write must decode");
    const auto actual_picture_reencoded = form_stream::encode_document(actual_picture_decoded.value());
    expect(actual_picture_reencoded.ok() &&
               list_stream::dump_compact(picture_record(actual_picture_reencoded.value())) ==
                   list_stream::dump_compact(list_stream::parse(observed_write_control_record)),
        "actual runtime PictureDecoration picture record must encode back canonically");
    constexpr std::string_view observed_write_picture =
        "{4,1,{0,894cf65b-4109-4533-a1d7-c87b1fcc80a3},\"\",-1,-1,0,0,\"\"}";
    const auto& picture_properties = picture_record(picture_encoded.value()).items[2].items[1];
    expect(
               list_stream::dump_compact(picture_properties.items[4].items[2]) == observed_write_picture,
        "PictureDecoration.Picture must use the exact observed PictureLib.Write record tuple");
    const auto picture_decoded = form_stream::decode_document(picture_encoded.value(), "PictureDecorationPicture");
    expect(picture_decoded.ok(), "standard PictureDecoration.Picture must decode");
    const auto* decoded_control = picture_decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_picture = decoded_control == nullptr ? nullptr :
        decoded_control->properties().find(model::PropertyId::from_name("Picture"));
    expect(decoded_picture && std::holds_alternative<model::PictureRef>(decoded_picture->value) &&
               std::get<model::PictureRef>(decoded_picture->value).standard_name ==
               model::QualifiedName{"PictureLib.Write"},
        "observed PictureDecoration picture identity must decode to a typed PictureRef");
    const auto picture_reencoded = form_stream::encode_document(picture_decoded.value());
    expect(picture_reencoded.ok() && list_stream::dump_compact(picture_reencoded.value()) ==
               list_stream::dump_compact(picture_encoded.value()),
        "standard PictureDecoration.Picture must re-encode canonically");

    auto unknown_picture_document = make_document(false, true, "");
    const_cast<model::ControlNode*>(unknown_picture_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{0}},
                model::QualifiedName{"PictureLib.Unknown"}});
    expect_failure(form_stream::encode_document(unknown_picture_document), "OOF1123",
        "$", "unknown standard PictureLib names must be rejected before encoding");

    auto external_picture_document = make_document(false, true, "");
    const_cast<model::ControlNode*>(external_picture_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Picture"),
            model::PictureRef{model::PictureAssetRef{model::ObjectId{20}}});
    const std::vector<std::uint8_t> external_gif{
        0x47,0x49,0x46,0x38,0x39,0x61,0x01,0x00,0x01,0x00,0x80,0x00,0x00,0x00,0x00,0x00,
        0xff,0xff,0xff,0x21,0xf9,0x04,0x01,0x00,0x00,0x00,0x00,0x2c,0x00,0x00,0x00,0x00,
        0x01,0x00,0x01,0x00,0x00,0x02,0x01,0x44,0x00,0x3b};
    external_picture_document.add_asset(model::PictureAsset{model::ObjectId{20},
        "Items/Picture/Picture.gif", model::PictureFormat::gif, external_gif, false});
    const auto external_picture_encoded = form_stream::encode_document(external_picture_document);
    expect(external_picture_encoded.ok(), "synthetic external PictureDecoration GIF must encode");
    const auto& external_picture_value = picture_record(external_picture_encoded.value()).items[2].items[1].items[4].items[2];
    expect(external_picture_value.items.size() == 10 && external_picture_value.items[0].atom == "4" &&
               external_picture_value.items[1].atom == "3" && external_picture_value.items[6].atom == "0" &&
               external_picture_value.items[7].items[0].items[0].atom ==
                   "#base64:R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7",
        "PictureDecoration external GIF must use the observed Button picture tuple");
    const auto external_picture_decoded = form_stream::decode_document(
        external_picture_encoded.value(), "PictureDecorationExternalPicture");
    expect(external_picture_decoded.ok(), "synthetic external PictureDecoration GIF must decode");
    const auto* external_decoded_control = external_picture_decoded.value().find_control(model::ObjectId{2});
    const auto* external_decoded_property = external_decoded_control == nullptr ? nullptr :
        external_decoded_control->properties().find(model::PropertyId::from_name("Picture"));
    expect(external_decoded_property && std::holds_alternative<model::PictureRef>(external_decoded_property->value) &&
               external_picture_decoded.value().assets().size() == 1 &&
               external_picture_decoded.value().assets().front().bytes == external_gif &&
               external_picture_decoded.value().assets().front().format == model::PictureFormat::gif &&
               !external_picture_decoded.value().assets().front().transparent,
        "PictureDecoration external GIF must decode as a typed PictureRef and exact PictureAsset bytes");
    const auto external_picture_reencoded = form_stream::encode_document(external_picture_decoded.value());
    expect(external_picture_reencoded.ok() &&
               list_stream::dump_compact(picture_record(external_picture_reencoded.value())) ==
                   list_stream::dump_compact(picture_record(external_picture_encoded.value())),
        "PictureDecoration external GIF must re-encode canonically");

    auto unknown_property_document = make_document(false, true, "");
    const_cast<model::ControlNode*>(unknown_property_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Transparent"), true);
    expect_failure(form_stream::encode_document(unknown_property_document), "OOF1122",
        "$/PictureDecoration", "unimplemented non-default picture formatting must be rejected");

    auto event_document = make_document(false, true, "");
    auto* event_picture = const_cast<model::ControlNode*>(event_document.find_control(model::ObjectId{2}));
    event_picture->events.push_back(model::EventRef{model::ObjectId{3}});
    event_document.add_event(model::Event{
        model::ObjectId{3}, "Click", "PictureClick", model::ControlRef{model::ObjectId{2}}});
    expect_failure(form_stream::encode_document(event_document), "OOF1122", "$/PictureDecoration",
        "unimplemented PictureDecoration events must be rejected");
}

void test_splitter_observed_record_and_named_codec() {
    const auto make_document = [](std::string name) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "SplitterCodec";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{2}, std::move(name), model::SplitterPayload{}});
        return document;
    };
    const auto splitter_record = [](const list_stream::ListValue& stream) -> const list_stream::ListValue& {
        return stream.items[1].items[2].items[2].items[1];
    };
    const auto mutable_splitter_record = [](list_stream::ListValue& stream) -> list_stream::ListValue& {
        return stream.items[1].items[2].items[2].items[1];
    };

    constexpr std::string_view captured_record = R"SPLITTER(
{36e52348-5d60-4770-8e89-a16ed50a2006,2,
{0,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},1,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{-18},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},2,2,0}
},
{8,0,0,0,0,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,0,1,0,0},
{14,"SplitterProbe",4294967295,0,0,0},
{0}
}
)SPLITTER";
    const auto actual_record = list_stream::parse(captured_record);
    const auto defaults = form_stream::encode_document(make_document("SplitterProbe"));
    expect(defaults.ok(), defaults ? "default named Splitter must encode without a source binary" :
        defaults.diagnostics().front().path + ": " + defaults.diagnostics().front().message);
    const auto default_record_decoded = form_stream::decode_document(defaults.value(), "SplitterCodec");
    expect(default_record_decoded.ok(), "default encoded Splitter must decode");
    const auto* default_control = default_record_decoded.value().find_control(model::ObjectId{2});
    expect(default_control && default_control->kind() == model::ControlKind::splitter &&
               default_control->name == "SplitterProbe" &&
               !default_control->properties().find(model::PropertyId::from_name("Orientation")) &&
               !default_control->properties().find(model::PropertyId::from_name("Enabled")) &&
               !default_control->properties().find(model::PropertyId::from_name("ToolTip")),
        "default Splitter must decode to its named identity with implicit property defaults");

    auto observed_stream = form_stream::encode_document(make_document("SplitterProbe"));
    expect(observed_stream.ok(), "named Splitter envelope must encode before inserting the independent platform record");
    mutable_splitter_record(observed_stream.value()) = actual_record;
    const auto decoded_actual = form_stream::decode_document(observed_stream.value(), "CapturedSplitter");
    expect(decoded_actual.ok(), decoded_actual ? "" : decoded_actual.diagnostics().front().path + ": " +
        decoded_actual.diagnostics().front().message);
    const auto* actual_control = decoded_actual.value().find_control(model::ObjectId{2});
    expect(actual_control && actual_control->name == "SplitterProbe" &&
               actual_control->kind() == model::ControlKind::splitter,
        "full captured Splitter record must decode to the named Splitter model");
    const auto captured_roundtrip = form_stream::encode_document(decoded_actual.value());
    expect(captured_roundtrip.ok() && list_stream::dump_compact(splitter_record(captured_roundtrip.value())) ==
               list_stream::dump_compact(actual_record),
        "full captured Splitter record must re-encode without dropping its canonical payload");

    const std::string tool_tip = "Подсказка Ω <важно> & \"цитата\"\nВторая\rстрока";
    auto changed_document = make_document("SplitterChanged");
    auto* changed = const_cast<model::ControlNode*>(changed_document.find_control(model::ObjectId{2}));
    changed->properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    changed->properties().set_explicit(model::PropertyId::from_name("Orientation"),
        model::EnumerationValue{"Orientation", "Horizontal"});
    changed->properties().set_explicit(model::PropertyId::from_name("ToolTip"), tool_tip);
    model::ColorValue back_color;
    back_color.kind = model::ColorKind::absolute;
    back_color.red = 31; back_color.green = 127; back_color.blue = 223;
    changed->properties().set_explicit(model::PropertyId::from_name("BackColor"), back_color);
    model::ColorValue border_color;
    border_color.kind = model::ColorKind::absolute;
    border_color.red = 223; border_color.green = 127; border_color.blue = 31;
    changed->properties().set_explicit(model::PropertyId::from_name("BorderColor"), border_color);
    const auto xml = oof::source::serialize_form_xml(changed_document);
    expect(xml.ok(), "named Splitter model must serialize as public XML");
    const auto parsed = oof::source::parse_form_xml(xml.value());
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto changed_stream = form_stream::encode_document(parsed.value());
    expect(changed_stream.ok(), changed_stream ? "" : changed_stream.diagnostics().front().path + ": " +
        changed_stream.diagnostics().front().message);
    const auto decoded_changed = form_stream::decode_document(changed_stream.value(), "SplitterChanged");
    expect(decoded_changed.ok(), "XML-only changed Splitter must decode after storage encoding");
    const auto* result = decoded_changed.value().find_control(model::ObjectId{2});
    expect(result && !std::get<bool>(result->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
               std::get<model::EnumerationValue>(result->properties().find(model::PropertyId::from_name("Orientation"))->value) ==
                   model::EnumerationValue{"Orientation", "Horizontal"} &&
               std::get<std::string>(result->properties().find(model::PropertyId::from_name("ToolTip"))->value) == tool_tip &&
               std::get<model::ColorValue>(result->properties().find(model::PropertyId::from_name("BackColor"))->value) == back_color &&
               std::get<model::ColorValue>(result->properties().find(model::PropertyId::from_name("BorderColor"))->value) == border_color,
        "named Splitter Enabled, Orientation, ToolTip, and observed RGB colors must survive XML-only round-trip");
    const auto reencoded = form_stream::encode_document(decoded_changed.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(changed_stream.value()),
        "changed Splitter storage must round-trip canonically");

    auto style_document = make_document("SplitterStyleColors");
    auto* style_splitter = const_cast<model::ControlNode*>(style_document.find_control(model::ObjectId{2}));
    const model::ColorValue button_back_style{model::ColorKind::style_reference, 0, 0, 0, 255,
        model::QualifiedName{"StyleColors.ButtonBackColor"}};
    const model::ColorValue border_style{model::ColorKind::style_reference, 0, 0, 0, 255,
        model::QualifiedName{"StyleColors.BorderColor"}};
    style_splitter->properties().set_explicit(model::PropertyId::from_name("BackColor"), button_back_style);
    style_splitter->properties().set_explicit(model::PropertyId::from_name("BorderColor"), border_style);
    const auto style_xml = source::serialize_form_xml(style_document);
    expect(style_xml.ok(), "named Splitter style colors must serialize to XML");
    const auto style_parsed = source::parse_form_xml(style_xml.value());
    expect(style_parsed.ok(), "named Splitter style colors must parse from XML");
    const auto style_stream = form_stream::encode_document(style_parsed.value());
    expect(style_stream.ok(), "named Splitter style colors must encode to Form.bin stream");
    const auto style_decoded = form_stream::decode_document(style_stream.value(), "SplitterStyleColors");
    const auto* decoded_style_splitter = style_decoded
        ? style_decoded.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* decoded_style_back = decoded_style_splitter
        ? decoded_style_splitter->properties().find(model::PropertyId::from_name("BackColor")) : nullptr;
    const auto* decoded_style_border = decoded_style_splitter
        ? decoded_style_splitter->properties().find(model::PropertyId::from_name("BorderColor")) : nullptr;
    const auto* decoded_style_back_value = decoded_style_back
        ? std::get_if<model::ColorValue>(&decoded_style_back->value) : nullptr;
    const auto* decoded_style_border_value = decoded_style_border
        ? std::get_if<model::ColorValue>(&decoded_style_border->value) : nullptr;
    expect(style_decoded.ok() && decoded_style_back_value != nullptr &&
               decoded_style_border_value != nullptr && *decoded_style_back_value == button_back_style &&
               *decoded_style_border_value == border_style,
        "Splitter BackColor and BorderColor style references must survive XML-to-BIN round-trip");
    const auto style_reencoded = form_stream::encode_document(style_decoded.value());
    expect(style_reencoded.ok() && list_stream::dump_compact(style_reencoded.value()) ==
               list_stream::dump_compact(style_stream.value()),
        "Splitter style references must remain stable after decoding and rebuilding");

    auto explicit_auto = make_document("SplitterProbe");
    const_cast<model::ControlNode*>(explicit_auto.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Orientation"),
            model::EnumerationValue{"Orientation", "Auto"});
    const auto auto_encoded = form_stream::encode_document(explicit_auto);
    expect(auto_encoded.ok() && list_stream::dump_compact(auto_encoded.value()) ==
               list_stream::dump_compact(defaults.value()),
        "explicit Splitter Orientation Auto must normalize to its implicit default");
    auto vertical_document = make_document("Vertical");
    const_cast<model::ControlNode*>(vertical_document.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Orientation"),
            model::EnumerationValue{"Orientation", "Vertical"});
    const auto vertical_encoded = form_stream::encode_document(vertical_document);
    expect(vertical_encoded.ok(), "observed Vertical orientation must encode");
    const auto vertical_decoded = form_stream::decode_document(vertical_encoded.value(), "SplitterVertical");
    const auto* vertical_control = vertical_decoded ?
        vertical_decoded.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* vertical_value = vertical_control ? vertical_control->properties().find(
        model::PropertyId::from_name("Orientation")) : nullptr;
    expect(vertical_decoded.ok() && vertical_value &&
               std::get<model::EnumerationValue>(vertical_value->value) ==
                   model::EnumerationValue{"Orientation", "Vertical"},
        "observed Vertical orientation must round-trip by its named enum value");

    auto unsupported_orientation = make_document("BadOrientation");
    const_cast<model::ControlNode*>(unsupported_orientation.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Orientation"),
            model::EnumerationValue{"Orientation", "Diagonal"});
    expect_failure(form_stream::encode_document(unsupported_orientation), "OOF1122", "$/Splitter/Orientation",
        "unknown Orientation members must be rejected");
    auto unsupported_property = make_document("BadProperty");
    const_cast<model::ControlNode*>(unsupported_property.find_control(model::ObjectId{2}))
        ->properties().set_explicit(model::PropertyId::from_name("Border"), std::string("unsupported"));
    expect(!form_stream::encode_document(unsupported_property),
        "unimplemented Border property must be rejected before it is silently discarded");
    auto event_document = make_document("Eventful");
    auto* eventful = const_cast<model::ControlNode*>(event_document.find_control(model::ObjectId{2}));
    eventful->events.push_back(model::EventRef{model::ObjectId{4}});
    event_document.add_event(model::Event{model::ObjectId{4}, "OnChange", "Handler",
        model::ControlRef{model::ObjectId{2}}});
    expect(!form_stream::encode_document(event_document),
        "unimplemented Splitter events must be rejected before they are silently discarded");
    auto unsupported_storage = defaults.value();
    mutable_splitter_record(unsupported_storage).items[2].items[1].items[0].items[5] =
        list_stream::ListValue::raw_atom("0");
    const auto partial_splitter = form_stream::decode_document(unsupported_storage, "SplitterCodec");
    const auto* partial_splitter_control = partial_splitter ?
        partial_splitter.value().find_control(model::ObjectId{2}) : nullptr;
    expect(partial_splitter.ok() && !partial_splitter.value().reconstruction_complete() &&
               std::any_of(partial_splitter.diagnostics().begin(), partial_splitter.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_splitter_control != nullptr && partial_splitter_control->name == "SplitterProbe",
        "valid unknown Splitter profile value must warn and preserve the named control");
}

void test_fresh_checkbox_stream_decode() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Fresh";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
    model::OrdinaryFormDocument seed(std::move(form));
    seed.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
    seed.add_control(model::ControlNode{model::ObjectId{3}, "Placeholder", model::LabelDecorationPayload{}});
    auto stream = form_stream::encode_document(seed);
    expect(stream.ok(), "fresh fixture root must encode before inserting the observed CheckBox record");
    stream.value().items[1].items[2].items[2].items[2] = list_stream::parse(R"OOF(
{35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26,3,{1,{{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},7,{1,1,{"ru","Флажок1"}},1,0,1,0,100,1},4,0,0,0,0,0},{0}},{8,68,82,218,107,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,3,0,25},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,3,2,150},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},1,{0,3,1},0,1,{0,3,3},0,0,0,0,1,2,0,0},{14,"Флажок1",4294967295,0,0,0},{0}}
)OOF");
    stream.value().items[2] = list_stream::parse(R"OOF({{-1},4,{1,{{3},1,0,1,"Флажок1",{"Pattern",{"B"}}}},{1,{3,{1,{3}}}}})OOF");
    const auto decoded = form_stream::decode_document(stream.value(), "Fresh");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* check_box = decoded.value().find_control(model::ObjectId{3});
    expect(check_box != nullptr && check_box->kind() == model::ControlKind::check_box &&
               check_box->name == "Флажок1" && check_box->data_path &&
               check_box->data_path->attribute.id() == model::ObjectId{3},
        "fresh CheckBox record and same-numbered Boolean Attribute must decode by distinct object category");
    expect(check_box->position.left.value() == 68 && check_box->position.top.value() == 82 &&
               check_box->position.width.value() == 150 && check_box->position.height.value() == 25,
        "fresh CheckBox geometry must decode");
    model::TypeDomainPatternValue boolean_type;
    model::TypeDomainEntry boolean_entry;
    boolean_entry.term = model::TypeDomainTerm::boolean;
    boolean_type.entries.push_back(boolean_entry);
    expect(decoded.value().find_attribute(model::ObjectId{3})->type == boolean_type,
        "fresh CheckBox linked Attribute must decode exact Boolean token");
}

void test_html_document_field_output_platform_record_and_guards() {
    const auto make_document = [](std::optional<model::EnumerationValue> output,
                                  const std::function<void(model::ControlNode&)>& configure = {}) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "HtmlOutput";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode field{model::ObjectId{2}, "HtmlProbe", model::HtmlDocumentFieldPayload{}};
        if (output) field.properties().set_explicit(model::PropertyId::from_name("Output"), *output);
        if (configure) configure(field);
        document.add_control(std::move(field));
        return document;
    };

    const auto actual_platform_data = list_stream::parse("{5,0,{0},{4,4,{0},4},{3,1,{-18},0,0,0},1,0}");
    const auto auto_stream = form_stream::encode_document(make_document(std::nullopt));
    expect(auto_stream.ok(), "HTMLDocumentField with default Auto must encode");
    const auto html_record_path = auto_stream.value().items[1].items[2].items[2].items[1];
    expect(list_stream::dump_compact(html_record_path.items[2]) ==
                   list_stream::dump_compact(actual_platform_data),
        "canonical HTMLDocumentField data must match the literal platform Add readback tuple");
    const auto auto_decoded = form_stream::decode_document(auto_stream.value(), "HtmlOutput");
    expect(auto_decoded.ok() &&
               auto_decoded.value().find_control(model::ObjectId{2})->kind() == model::ControlKind::html_document_field,
        "platform-shaped HTMLDocumentField record must decode as the named control");
    expect(!auto_decoded.value().find_control(model::ObjectId{2})->properties().find(
               model::PropertyId::from_name("Output")),
        "platform default Auto must normalize to an implicit model default");

    for (const auto& [member, storage] : std::array<std::pair<std::string_view, std::string_view>, 2>{
             std::pair{"Enable", "1"}, std::pair{"Disable", "2"}}) {
        const auto encoded = form_stream::encode_document(make_document(model::EnumerationValue{"Output", std::string(member)}));
        expect(encoded.ok(), "supported HTMLDocumentField Output values must encode");
        const auto& record = encoded.value().items[1].items[2].items[2].items[1];
        expect(record.items[2].items[6].atom == storage,
            "HTMLDocumentField Output member must occupy only its observed tuple slot");
        const auto decoded = form_stream::decode_document(encoded.value(), "HtmlOutput");
        expect(decoded.ok() && std::get<model::EnumerationValue>(
                   decoded.value().find_control(model::ObjectId{2})->properties().find(
                       model::PropertyId::from_name("Output"))->value) == model::EnumerationValue{"Output", std::string(member)},
            "HTMLDocumentField Output member must decode to its named public enum");
        const auto repeated = form_stream::encode_document(decoded.value());
        expect(repeated.ok() && list_stream::dump_compact(repeated.value()) ==
                   list_stream::dump_compact(encoded.value()),
            "HTMLDocumentField Output must round-trip without changing other storage slots");
    }

    auto invalid_output = auto_stream.value();
    invalid_output.items[1].items[2].items[2].items[1].items[2].items[6] = list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(invalid_output, "HtmlOutput"), "OOF1114",
        "$/1/2/2/1/2/6", "unknown HTMLDocumentField Output storage values must be rejected");
    auto invalid_data_slot = auto_stream.value();
    invalid_data_slot.items[1].items[2].items[2].items[1].items[2].items[1] = list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(invalid_data_slot, "HtmlOutput"), "OOF1114",
        "$/1/2/2/1/2", "unproven HTMLDocumentField data variations must be rejected");
    auto invalid_metadata = auto_stream.value();
    invalid_metadata.items[1].items[2].items[2].items[1].items[4].items[2] = list_stream::ListValue::raw_atom("7");
    expect_failure(form_stream::decode_document(invalid_metadata, "HtmlOutput"), "OOF1114",
        "$/1/2/2/1/4", "unproven HTMLDocumentField metadata variations must be rejected");

    for (const auto property : {"Border", "BorderColor", "Document", "Content"}) {
        auto unsupported = make_document(std::nullopt, [property](model::ControlNode& field) {
            field.properties().set_explicit(model::PropertyId::from_name(property), std::string("unsupported"));
        });
        expect(!form_stream::encode_document(unsupported),
            "unproven HTMLDocumentField properties must fail closed");
    }
    auto unsupported_event = make_document(std::nullopt, [](model::ControlNode& field) {
        field.events.push_back(model::EventRef{model::ObjectId{9}});
    });
    expect(!form_stream::encode_document(unsupported_event),
        "unproven HTMLDocumentField events must fail closed");
    auto unsupported_child = make_document(std::nullopt, [](model::ControlNode& field) {
        field.children.push_back(model::ControlRef{model::ObjectId{3}});
    });
    expect(!form_stream::encode_document(unsupported_child),
        "unproven HTMLDocumentField child storage must fail closed");
    auto wrong_enum_type = make_document(model::EnumerationValue{"UseOutput", "Enable"});
    expect(!form_stream::encode_document(wrong_enum_type),
        "platform enum name must not be accepted as an incidental public XML alias");
    const auto wrong_type_xml = source::parse_form_xml(
        R"XML(<Form id="1" name="Html" ordinaryFormVersion="2.1"><ChildItems><HTMLDocumentField id="2" name="HtmlProbe"><Position/><Output type="UseOutput" member="Enable"/></HTMLDocumentField></ChildItems></Form>)XML");
    expect(wrong_type_xml.ok() && !form_stream::encode_document(wrong_type_xml.value()),
        "XML type UseOutput must not alias the public Output enumeration on build");
    const auto wrong_member_xml = source::parse_form_xml(
        R"XML(<Form id="1" name="Html" ordinaryFormVersion="2.1"><ChildItems><HTMLDocumentField id="2" name="HtmlProbe"><Position/><Output type="Output" member="Allowed"/></HTMLDocumentField></ChildItems></Form>)XML");
    expect(wrong_member_xml.ok() && !form_stream::encode_document(wrong_member_xml.value()),
        "unknown HTMLDocumentField Output enum members must fail at storage encoding");
}

void test_radio_button_basic_observed_record_and_rejections() {
    const auto append_child_ref = [](model::OrdinaryFormDocument& document, model::ObjectId id) {
        auto form = document.form();
        form.children.push_back(model::ControlRef{id});
        document.set_form(std::move(form));
    };
    const auto make_document = [](std::string caption = {}, bool enabled = true, std::string tool_tip = {}) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "RadioButtonCodec";
        for (std::uint64_t id = 100; id <= 103; ++id) form.children.push_back(model::ControlRef{model::ObjectId{id}});
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{100}, "CalendarFieldDefault", model::CalendarFieldPayload{}});
        document.add_control(model::ControlNode{model::ObjectId{101}, "CalendarFieldDisabled", model::CalendarFieldPayload{}});
        document.add_control(model::ControlNode{model::ObjectId{102}, "CalendarFieldHidden", model::CalendarFieldPayload{}});
        model::ControlNode radio{model::ObjectId{103}, "RadioRuntime", model::RadioButtonPayload{}};
        if (!caption.empty()) radio.properties().set_explicit(model::PropertyId::from_name("Caption"), std::move(caption));
        if (!enabled) radio.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
        if (!tool_tip.empty()) radio.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::move(tool_tip));
        document.add_control(std::move(radio));
        return document;
    };
    constexpr std::string_view observed_radio_control_record = R"RAW(
{782e569a-79a7-4a4f-a936-b48d013936ec,103,
{4,{"Pattern"},
{{
{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},7,{1,0},1,0,1,0,100,1},4,0,0,0,0},0,{"U"},{0}},
{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,3,4,0,0},
{14,"RadioRuntime",4294967295,0,0,0},{0}}
)RAW";
    const auto expected_observed_record = list_stream::parse(observed_radio_control_record);
    const auto default_encoded = form_stream::encode_document(make_document());
    expect(default_encoded.ok(), "default RadioButton from the observed unbound profile must encode");
    const auto& default_records = default_encoded.value().items[1].items[2].items[2].items;
    expect(default_records.size() == 5 &&
               list_stream::dump_compact(default_records[4]) == list_stream::dump_compact(expected_observed_record),
        "RadioButton writer must reproduce the full observed default control record");
    const auto default_decoded = form_stream::decode_document(default_encoded.value(), "RadioButtonCodec");
    expect(default_decoded.ok(), "full observed default RadioButton must decode");
    const auto* default_radio = default_decoded.value().find_control(model::ObjectId{103});
    expect(default_radio && default_radio->kind() == model::ControlKind::radio_button &&
               default_radio->name == "RadioRuntime" && default_radio->position.visible.value() &&
               default_radio->properties().find(model::PropertyId::from_name("Enabled")) == nullptr &&
               default_radio->properties().find(model::PropertyId::from_name("Caption")) == nullptr &&
               default_radio->properties().find(model::PropertyId::from_name("ToolTip")) == nullptr,
        "RadioButton identity, Visible, and implicit Caption/Enabled/ToolTip defaults must decode by name");
    const auto default_reencoded = form_stream::encode_document(default_decoded.value());
    expect(default_reencoded.ok() && list_stream::dump_compact(default_reencoded.value()) ==
               list_stream::dump_compact(default_encoded.value()),
        "default RadioButton document must round-trip canonically");

    auto adjacent_defaults = make_document();
    for (const auto [id, name] : {std::pair{104ULL, "RadioDefault2"}, std::pair{105ULL, "RadioDefault3"}}) {
        append_child_ref(adjacent_defaults, model::ObjectId{id});
        adjacent_defaults.add_control(model::ControlNode{model::ObjectId{id}, name, model::RadioButtonPayload{}});
    }
    append_child_ref(adjacent_defaults, model::ObjectId{106});
    adjacent_defaults.add_control(model::ControlNode{model::ObjectId{106}, "Separator", model::LabelDecorationPayload{}});
    append_child_ref(adjacent_defaults, model::ObjectId{107});
    adjacent_defaults.add_control(model::ControlNode{model::ObjectId{107}, "RadioAfterLabel", model::RadioButtonPayload{}});
    const auto adjacent_encoded = form_stream::encode_document(adjacent_defaults);
    expect(adjacent_encoded.ok(), "adjacent default RadioButtons and one after a Label must remain independent controls");
    const auto adjacent_decoded = form_stream::decode_document(adjacent_encoded.value(), "AdjacentDefaultRadios");
    expect(adjacent_decoded.ok() && adjacent_decoded.value().find_control(model::ObjectId{104}) != nullptr &&
               adjacent_decoded.value().find_control(model::ObjectId{105}) != nullptr &&
               adjacent_decoded.value().find_control(model::ObjectId{107}) != nullptr,
        "adjacent ungrouped RadioButtons must decode independently across a Label boundary");

    const std::string caption = "Radio Ω <tag> & текст";
    const std::string tool_tip = "Radio hint Ω <tag> & текст";
    const auto changed_encoded = form_stream::encode_document(make_document(caption, false, tool_tip));
    expect(changed_encoded.ok(), "RadioButton Caption, Enabled, and ToolTip must encode");
    const auto& changed_records = changed_encoded.value().items[1].items[2].items[2].items;
    const auto& changed_info = changed_records[4].items[2];
    const auto& changed_properties = changed_info.items[2].items[0];
    expect(changed_properties.items[0].items[1].atom == "0" &&
               list_stream::dump_compact(changed_properties.items[2]) == value_codec::encode_localized_string(
                   model::LocalizedStringValue{{{"ru", caption}}}) &&
               list_stream::dump_compact(changed_properties.items[0].items[12]) == value_codec::encode_localized_string(
                   model::LocalizedStringValue{{{"ru", tool_tip}}}),
        "RadioButton properties must occupy the observed Enabled, Caption, and ToolTip slots");
    const auto changed_decoded = form_stream::decode_document(changed_encoded.value(), "RadioButtonCodec");
    expect(changed_decoded.ok(), "RadioButton Caption, Enabled, and ToolTip must decode");
    const auto* decoded_radio = changed_decoded.value().find_control(model::ObjectId{103});
    const auto* decoded_enabled = decoded_radio == nullptr ? nullptr :
        decoded_radio->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* decoded_caption = decoded_radio == nullptr ? nullptr :
        decoded_radio->properties().find(model::PropertyId::from_name("Caption"));
    const auto* decoded_tool_tip = decoded_radio == nullptr ? nullptr :
        decoded_radio->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(decoded_radio && decoded_enabled && decoded_caption && decoded_tool_tip &&
               !std::get<bool>(decoded_enabled->value) &&
               std::get<std::string>(decoded_caption->value) == caption &&
               std::get<std::string>(decoded_tool_tip->value) == tool_tip,
        "RadioButton properties must round-trip their runtime-set values");
    const auto changed_reencoded = form_stream::encode_document(changed_decoded.value());
    expect(changed_reencoded.ok() && list_stream::dump_compact(changed_reencoded.value()) ==
               list_stream::dump_compact(changed_encoded.value()),
        "changed RadioButton document must round-trip without drift");

    auto data_path_document = make_document();
    model::TypeDomainPatternValue string_type;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_type.entries.push_back(string_entry);
    data_path_document.add_attribute(model::Attribute{model::ObjectId{200}, "Pattern", string_type});
    auto* radio_with_data = const_cast<model::ControlNode*>(data_path_document.find_control(model::ObjectId{103}));
    radio_with_data->data_path = model::DataPath{model::AttributeRef{model::ObjectId{200}}, {}};
    expect_failure(form_stream::encode_document(data_path_document), "OOF1122", "$/Form/ChildItems/3",
        "RadioButton DataPath must be explicitly rejected outside the proven profile");

    auto numeric_group = make_document();
    auto* numeric_head = const_cast<model::ControlNode*>(numeric_group.find_control(model::ObjectId{103}));
    numeric_head->extension_properties.set_explicit(model::PropertyId::from_name("FirstInGroup"), true);
    model::TypeDomainPatternValue numeric_type;
    model::TypeDomainEntry numeric_entry;
    numeric_entry.term = model::TypeDomainTerm::numeric;
    numeric_entry.numeric = {10, 0, true};
    numeric_type.entries.push_back(numeric_entry);
    numeric_head->extension_properties.set_explicit(model::PropertyId::from_name("ValueType"), numeric_type);
    numeric_head->properties().set_explicit(model::PropertyId::from_name("SelectionValue"), model::DecimalValue{"1"});
    numeric_group.add_attribute(model::Attribute{model::ObjectId{200}, "Choice", numeric_type});
    numeric_head->data_path = model::DataPath{model::AttributeRef{model::ObjectId{200}}, {}};
    auto numeric_form = numeric_group.form();
    numeric_form.children.push_back(model::ControlRef{model::ObjectId{104}});
    numeric_group.set_form(std::move(numeric_form));
    model::ControlNode numeric_member{model::ObjectId{104}, "RadioMember", model::RadioButtonPayload{}};
    numeric_member.properties().set_explicit(model::PropertyId::from_name("SelectionValue"), model::DecimalValue{"0"});
    numeric_group.add_control(std::move(numeric_member));
    append_child_ref(numeric_group, model::ObjectId{105});
    model::ControlNode numeric_member_three{model::ObjectId{105}, "RadioMemberThree", model::RadioButtonPayload{}};
    numeric_member_three.properties().set_explicit(model::PropertyId::from_name("SelectionValue"), model::DecimalValue{"0"});
    numeric_group.add_control(std::move(numeric_member_three));
    append_child_ref(numeric_group, model::ObjectId{106});
    numeric_group.add_control(model::ControlNode{model::ObjectId{106}, "GroupSeparator", model::LabelDecorationPayload{}});
    append_child_ref(numeric_group, model::ObjectId{107});
    model::ControlNode second_head{model::ObjectId{107}, "FractionalGroupHead", model::RadioButtonPayload{}};
    second_head.extension_properties.set_explicit(model::PropertyId::from_name("FirstInGroup"), true);
    model::TypeDomainEntry fractional_numeric_entry;
    fractional_numeric_entry.term = model::TypeDomainTerm::numeric;
    fractional_numeric_entry.numeric = {15, 3, false};
    const model::TypeDomainPatternValue fractional_numeric_type{{fractional_numeric_entry}};
    second_head.extension_properties.set_explicit(model::PropertyId::from_name("ValueType"), fractional_numeric_type);
    second_head.properties().set_explicit(model::PropertyId::from_name("SelectionValue"), model::DecimalValue{"-12.375"});
    numeric_group.add_attribute(model::Attribute{model::ObjectId{201}, "FractionalChoice", fractional_numeric_type});
    second_head.data_path = model::DataPath{model::AttributeRef{model::ObjectId{201}}, {}};
    numeric_group.add_control(std::move(second_head));
    for (const auto [id, name] : {std::pair{108ULL, "FractionalMember2"},
                                  std::pair{109ULL, "FractionalMember3"},
                                  std::pair{110ULL, "FractionalMember4"}}) {
        append_child_ref(numeric_group, model::ObjectId{id});
        model::ControlNode member{model::ObjectId{id}, name, model::RadioButtonPayload{}};
        member.properties().set_explicit(model::PropertyId::from_name("SelectionValue"), model::DecimalValue{"0"});
        numeric_group.add_control(std::move(member));
    }
    const_cast<model::ControlNode*>(numeric_group.find_control(model::ObjectId{103}))->position.tab_order.set(std::optional<std::int32_t>{5});
    const_cast<model::ControlNode*>(numeric_group.find_control(model::ObjectId{104}))->position.tab_order.set(std::optional<std::int32_t>{4});
    const auto numeric_encoded = form_stream::encode_document(numeric_group);
    expect(numeric_encoded.ok(), "named integer and fractional RadioButton groups must encode with independent numeric qualifiers");
    const auto numeric_decoded = form_stream::decode_document(numeric_encoded.value(), "RadioButtonNumericGroup");
    expect(numeric_decoded.ok(), "named integer and fractional RadioButton groups must decode");
    const auto* decoded_head = numeric_decoded.value().find_control(model::ObjectId{103});
    const auto* decoded_member = numeric_decoded.value().find_control(model::ObjectId{104});
    expect(decoded_head && decoded_head->data_path && decoded_head->extension_properties.find(
               model::PropertyId::from_name("FirstInGroup")) && decoded_member &&
               decoded_member->properties().find(model::PropertyId::from_name("SelectionValue")) &&
               std::get<model::DecimalValue>(decoded_member->properties().find(
                   model::PropertyId::from_name("SelectionValue"))->value).canonical == "0",
        "RadioButton group head binding and contextual zero remain named model properties");
    expect(numeric_decoded.value().form().children == numeric_group.form().children &&
        decoded_head->position.tab_order.value() == std::optional<std::int32_t>{5} &&
        decoded_member->position.tab_order.value() == std::optional<std::int32_t>{4},
        "TabOrder swap must preserve numeric RadioButton grouping, FirstInGroup and logical ChildItems");
    const auto numeric_reencoded = form_stream::encode_document(numeric_decoded.value());
    expect(numeric_reencoded.ok() && list_stream::dump_compact(numeric_reencoded.value()) ==
               list_stream::dump_compact(numeric_encoded.value()),
        "named integer and fractional RadioButton groups must round-trip without stream drift");
    auto wrong_group_selection = numeric_group;
    auto* wrong_member = const_cast<model::ControlNode*>(wrong_group_selection.find_control(model::ObjectId{104}));
    wrong_member->properties().set_explicit(model::PropertyId::from_name("SelectionValue"), model::DecimalValue{"-1"});
    expect_failure(form_stream::encode_document(wrong_group_selection), "OOF1122", "$/Form/ChildItems/6",
        "RadioButton member selection outside the inherited nonnegative qualifiers must fail closed");
    auto wrong_group_type = numeric_group;
    auto* wrong_head = const_cast<model::ControlNode*>(wrong_group_type.find_control(model::ObjectId{103}));
    numeric_entry.numeric.precision = 1;
    wrong_head->extension_properties.set_explicit(model::PropertyId::from_name("ValueType"),
        model::TypeDomainPatternValue{{numeric_entry}});
    expect_failure(form_stream::encode_document(wrong_group_type), "OOF1122", "$/Form/ChildItems/6",
        "RadioButton head ValueType must match its linked Attribute qualifiers");

    auto unsupported_default = default_encoded.value();
    auto& unsupported_data_header = unsupported_default.items[1].items[2].items[2].items[4].items[2].items[1];
    unsupported_data_header = list_stream::ListValue::list({list_stream::ListValue::string_atom("Unobserved")});
    expect(!form_stream::decode_document(unsupported_default, "RadioButtonCodec"),
        "unobserved RadioButton data header must fail closed");

    auto multilingual = changed_encoded.value();
    auto& multilingual_caption = multilingual.items[1].items[2].items[2].items[4]
        .items[2].items[2].items[0].items[2];
    multilingual_caption = list_stream::parse(value_codec::encode_localized_string(
        model::LocalizedStringValue{{{"ru", "Текст"}, {"en", "Text"}}}));
    expect(!form_stream::decode_document(multilingual, "RadioButtonCodec"),
        "multilingual RadioButton Caption must be rejected without loss");
}

void test_radio_button_group_order_inherited_decimal_selection_and_boundaries() {
    const auto numeric_type = [](std::uint32_t digits, std::uint32_t fraction, bool non_negative) {
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::numeric;
        entry.numeric = {digits, fraction, non_negative};
        return model::TypeDomainPatternValue{{entry}};
    };
    const auto add_head = [&](model::OrdinaryFormDocument& document, std::uint64_t id,
                              std::uint64_t attribute_id, std::string name, std::string attribute_name,
                              const model::TypeDomainPatternValue& type, std::string selection) {
        document.add_attribute(model::Attribute{model::ObjectId{attribute_id}, std::move(attribute_name), type});
        model::ControlNode head{model::ObjectId{id}, std::move(name), model::RadioButtonPayload{}};
        head.extension_properties.set_explicit(model::PropertyId::from_name("FirstInGroup"), true);
        head.extension_properties.set_explicit(model::PropertyId::from_name("ValueType"), type);
        head.properties().set_explicit(model::PropertyId::from_name("SelectionValue"), model::DecimalValue{std::move(selection)});
        head.data_path = model::DataPath{model::AttributeRef{model::ObjectId{attribute_id}}, {}};
        document.add_control(std::move(head));
    };
    const auto add_member = [](model::OrdinaryFormDocument& document, std::uint64_t id,
                               std::string name, std::string selection) {
        model::ControlNode member{model::ObjectId{id}, std::move(name), model::RadioButtonPayload{}};
        member.properties().set_explicit(model::PropertyId::from_name("SelectionValue"),
            model::DecimalValue{std::move(selection)});
        document.add_control(std::move(member));
    };
    const auto add_label = [](model::OrdinaryFormDocument& document, std::uint64_t id, std::string name) {
        document.add_control(model::ControlNode{model::ObjectId{id}, std::move(name), model::LabelDecorationPayload{}});
    };
    const auto make_form = [](std::vector<model::ChildItemRef> children) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "RadioGroupOrder";
        form.children = std::move(children);
        return form;
    };

    const auto signed_fractional_type = numeric_type(10, 2, false);
    model::OrdinaryFormDocument ordered(make_form({
        model::ControlRef{model::ObjectId{901}},
        model::ControlRef{model::ObjectId{17}},
        model::ControlRef{model::ObjectId{70}},
    }));
    add_head(ordered, 901, 1001, "HeadLaterId", "Choice", signed_fractional_type, "1.25");
    add_member(ordered, 17, "MemberEarlierId", "-12.34");
    add_label(ordered, 70, "BoundaryLabel");

    const auto ordered_encoded = form_stream::encode_document(ordered);
    expect(ordered_encoded.ok(),
        "ChildItems order must bind a lower-ID member to the preceding numeric head and accept an in-range decimal");
    const auto ordered_decoded = form_stream::decode_document(ordered_encoded.value(), "RadioGroupOrder");
    expect(ordered_decoded.ok(), "ordered numeric group with inherited member type must decode");
    const auto& decoded_children = ordered_decoded.value().form().children;
    expect(decoded_children.size() == 3 &&
               std::get<model::ControlRef>(decoded_children[0]).id() == model::ObjectId{901} &&
               std::get<model::ControlRef>(decoded_children[1]).id() == model::ObjectId{17} &&
               std::get<model::ControlRef>(decoded_children[2]).id() == model::ObjectId{70},
        "decoded ChildItems order must remain head then member despite the higher head ObjectId");
    const auto* decoded_member = ordered_decoded.value().find_control(model::ObjectId{17});
    const auto* decoded_selection = decoded_member == nullptr ? nullptr : decoded_member->properties().find(
        model::PropertyId::from_name("SelectionValue"));
    expect(decoded_selection && std::get<model::DecimalValue>(decoded_selection->value).canonical == "-12.34" &&
               decoded_member->extension_properties.find(model::PropertyId::from_name("ValueType")) == nullptr,
        "decoded member must preserve its named DecimalValue without materializing inherited ValueType locally");
    const auto ordered_reencoded = form_stream::encode_document(ordered_decoded.value());
    expect(ordered_reencoded.ok() && list_stream::dump_compact(ordered_reencoded.value()) ==
               list_stream::dump_compact(ordered_encoded.value()),
        "inherited member decimal selection must round-trip without normalization or drift");

    auto reordered = ordered;
    auto reordered_form = reordered.form();
    std::swap(reordered_form.children[0], reordered_form.children[1]);
    reordered.set_form(std::move(reordered_form));
    expect_failure(form_stream::encode_document(reordered), "OOF1122", "$/Form/ChildItems/0",
        "moving the selected member before its head must change its group assignment and reject its unbound nonzero value");

    auto out_of_range = ordered;
    auto* too_precise = const_cast<model::ControlNode*>(out_of_range.find_control(model::ObjectId{17}));
    too_precise->properties().set_explicit(model::PropertyId::from_name("SelectionValue"),
        model::DecimalValue{"-12.345"});
    expect_failure(form_stream::encode_document(out_of_range), "OOF1122", "$/Form/ChildItems/2",
        "inherited member decimal must respect the head precision without coercion");

    auto oversized_stream = ordered_encoded.value();
    auto& member_record = oversized_stream.items[1].items[2].items[2].items[1];
    member_record.items[2].items[4] = list_stream::ListValue::list({
        list_stream::ListValue::string_atom("N"), list_stream::ListValue::raw_atom("123456789")});
    expect(!form_stream::decode_document(oversized_stream, "RadioGroupOrder"),
        "decoder must reject member selection outside the inherited head precision");

    model::Page nested_page;
    nested_page.id = model::ObjectId{400};
    nested_page.name = "NestedPage";
    model::Position page_bounds;
    for (const auto edge : {model::BindingCoordinate::right, model::BindingCoordinate::bottom}) {
        model::AnchorBinding binding;
        binding.coordinate = edge;
        binding.target_coordinate = edge;
        binding.target = model::ControlRef{model::ObjectId{300}};
        page_bounds.bindings.anchors.push_back(std::move(binding));
    }
    nested_page.position.set(page_bounds);
    nested_page.children = {model::ControlRef{model::ObjectId{901}}, model::ControlRef{model::ObjectId{17}}};
    model::ControlNode nested_panel{model::ObjectId{300}, "NestedPanel", model::PanelPayload{}};
    nested_panel.children = {model::PageRef{model::ObjectId{400}}};
    model::OrdinaryFormDocument page_group(make_form({model::ControlRef{model::ObjectId{300}}}));
    add_head(page_group, 901, 1001, "PageHead", "Choice", signed_fractional_type, "1.25");
    add_member(page_group, 17, "PageMember", "-12.34");
    page_group.add_page(std::move(nested_page));
    page_group.add_control(std::move(nested_panel));
    const auto page_group_encoded = form_stream::encode_document(page_group);
    expect(page_group_encoded.ok(),
        "RadioButton group context must be scoped to and preserved within a Panel page" +
            (page_group_encoded ? std::string{} : ": " + page_group_encoded.diagnostics().front().path + " " +
                page_group_encoded.diagnostics().front().message));

    model::Page split_page;
    split_page.id = model::ObjectId{400};
    split_page.name = "SplitPage";
    split_page.position.set(page_bounds);
    split_page.children = {model::ControlRef{model::ObjectId{17}}};
    model::ControlNode boundary_panel{model::ObjectId{300}, "BoundaryPanel", model::PanelPayload{}};
    boundary_panel.children = {model::PageRef{model::ObjectId{400}}};
    model::OrdinaryFormDocument split_by_page(make_form({
        model::ControlRef{model::ObjectId{901}}, model::ControlRef{model::ObjectId{300}}}));
    add_head(split_by_page, 901, 1001, "OuterPageHead", "Choice", signed_fractional_type, "1.25");
    add_member(split_by_page, 17, "InnerPageMember", "-12.34");
    split_by_page.add_page(std::move(split_page));
    split_by_page.add_control(std::move(boundary_panel));
    expect(!form_stream::encode_document(split_by_page),
        "a RadioButton group must not cross from the form into a nested Panel page");
}

void test_text_document_field_persisted_profile_and_rejections() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "TextDocumentForm";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode field{model::ObjectId{2}, "DocumentText", model::TextDocumentFieldPayload{}};
    field.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    model::ColorValue color;
    color.kind = model::ColorKind::absolute;
    color.red = 255;
    field.properties().set_explicit(model::PropertyId::from_name("BorderColor"), color);
    model::FontValue font;
    font.kind = model::FontKind::absolute;
    font.face_name = "Verdana";
    font.height = 20;
    font.bold = true;
    field.properties().set_explicit(model::PropertyId::from_name("Font"), font);
    document.add_control(std::move(field));

    for (const auto name : {"Enabled", "BorderColor", "Font"}) {
        const auto* descriptor = model::metamodel::find_property(model::ControlKind::text_document_field, name);
        expect(descriptor != nullptr && descriptor->persistence == model::metamodel::PersistenceClass::persisted_editable &&
            descriptor->storage_codec == model::metamodel::StorageCodec::control_base,
            "TextDocumentField persisted properties must use their typed base descriptor");
    }
    expect(model::metamodel::find_property(model::ControlKind::text_document_field, "Border")->persistence ==
        model::metamodel::PersistenceClass::unclassified,
        "unverified Border value remains unclassified");

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "TextDocumentField must encode from named properties" :
        encoded.diagnostics().front().code + ":" + encoded.diagnostics().front().path + ":" + encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "TextDocumentForm");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ":" + decoded.diagnostics().front().message);
    if (!decoded.ok()) return;
    const auto* round_trip = decoded.value().find_control(model::ObjectId{2});
    expect(round_trip != nullptr && round_trip->kind() == model::ControlKind::text_document_field && round_trip->name == "DocumentText",
        "TextDocumentField identity must survive native stream round-trip");
    if (round_trip == nullptr) return;
    const auto* enabled = round_trip->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* actual_color = round_trip->properties().find(model::PropertyId::from_name("BorderColor"));
    const auto* actual_font = round_trip->properties().find(model::PropertyId::from_name("Font"));
    expect(enabled != nullptr && !std::get<bool>(enabled->value), "Enabled=false must survive TextDocumentField round-trip");
    expect(actual_color != nullptr && std::get<model::ColorValue>(actual_color->value) == color,
        "BorderColor must survive TextDocumentField round-trip");
    expect(actual_font != nullptr && std::get<model::FontValue>(actual_font->value) == font,
        "Font must survive TextDocumentField round-trip");

    auto changed = encoded.value();
    std::function<list_stream::ListValue*(list_stream::ListValue&)> find_text_document_record;
    find_text_document_record = [&](list_stream::ListValue& value) -> list_stream::ListValue* {
        if (value.is_list && value.items.size() == 6 && !value.items.empty() &&
            !value.items[0].is_list && value.items[0].atom == model::metamodel::descriptor_for(model::ControlKind::text_document_field).guid)
            return &value;
        for (auto& item : value.items) if (auto* found = find_text_document_record(item)) return found;
        return nullptr;
    };
    auto* text_doc_record = find_text_document_record(changed);
    expect(text_doc_record != nullptr, "encoded TextDocumentField record must be discoverable in the stream");
    if (text_doc_record == nullptr) return;
    text_doc_record->items[2].items[1] = list_stream::ListValue::raw_atom("7");
    const auto partial_text_document = form_stream::decode_document(changed, "TextDocumentForm");
    const auto* partial_text_control = partial_text_document ?
        partial_text_document.value().find_control(model::ObjectId{2}) : nullptr;
    expect(partial_text_document.ok() && !partial_text_document.value().reconstruction_complete() &&
               std::any_of(partial_text_document.diagnostics().begin(), partial_text_document.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_text_control != nullptr && partial_text_control->kind() == model::ControlKind::text_document_field,
        "valid unknown TextDocumentField profile value must warn and preserve its named control");

}

void test_calendar_field_enabled_round_trip_and_rejections() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "CalendarForm";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{3}}};
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{model::ObjectId{2}, "BeforeCalendar", model::ButtonPayload{}});
    model::ControlNode calendar{model::ObjectId{3}, "Calendar", model::CalendarFieldPayload{}};
    calendar.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    calendar.position.left.set(24);
    calendar.position.top.set(32);
    calendar.position.width.set(180);
    calendar.position.height.set(140);
    calendar.position.visible.set(false);
    document.add_control(std::move(calendar));

    const auto* enabled_descriptor = model::metamodel::find_property(model::ControlKind::calendar_field, "Enabled");
    expect(enabled_descriptor != nullptr &&
               enabled_descriptor->persistence == model::metamodel::PersistenceClass::persisted_editable &&
               enabled_descriptor->storage_codec == model::metamodel::StorageCodec::control_base,
        "CalendarField Enabled must route through its typed persisted descriptor");
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "mixed Button and CalendarField must encode" :
        encoded.diagnostics().front().code + ":" + encoded.diagnostics().front().path + ":" +
            encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "CalendarForm");
    expect(decoded.ok(), "mixed Button and CalendarField must decode");
    expect(decoded.value().form().children.size() == 2 &&
               std::get<model::ControlRef>(decoded.value().form().children[0]).id() == model::ObjectId{2} &&
               std::get<model::ControlRef>(decoded.value().form().children[1]).id() == model::ObjectId{3},
        "mixed owner child order and ordinals must survive CalendarField round-trip");
    const auto* decoded_calendar = decoded.value().find_control(model::ObjectId{3});
    const auto* enabled = decoded_calendar == nullptr ? nullptr : decoded_calendar->properties().find(
        model::PropertyId::from_name("Enabled"));
    expect(decoded_calendar != nullptr && decoded_calendar->kind() == model::ControlKind::calendar_field &&
               decoded_calendar->name == "Calendar" && enabled != nullptr && !std::get<bool>(enabled->value),
        "CalendarField identity and explicit Enabled=false must survive storage round-trip");
    expect(decoded_calendar->position.left.value() == 24 && decoded_calendar->position.top.value() == 32 &&
               decoded_calendar->position.width.value() == 180 && decoded_calendar->position.height.value() == 140 &&
               !decoded_calendar->position.visible.value(),
        "CalendarField Position and Visible must survive storage round-trip in the sibling owner context");

    model::Form unsupported_form;
    unsupported_form.id = model::ObjectId{1};
    unsupported_form.name = "UnsupportedCalendar";
    unsupported_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument unsupported_document(std::move(unsupported_form));
    model::ControlNode unsupported_calendar{
        model::ObjectId{2}, "Calendar", model::CalendarFieldPayload{}};
    unsupported_calendar.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::string("unproven"));
    unsupported_document.add_control(std::move(unsupported_calendar));
    expect_failure(form_stream::encode_document(unsupported_document), "OOF1122", "$/CalendarField",
        "unproven CalendarField ToolTip must fail closed");

    auto changed_date_atom = encoded.value();
    auto& calendar_record = changed_date_atom.items[1].items[2].items[2].items[2];
    calendar_record.items[2].items[1].items[5] = list_stream::ListValue::raw_atom("20230229000000");
    const auto malformed_calendar_date = form_stream::decode_document(changed_date_atom, "CalendarForm");
    expect(!malformed_calendar_date && malformed_calendar_date.diagnostics().front().code == "OOF1122",
        "malformed CalendarField date atom must fail closed");

    auto changed_flag_atom = encoded.value();
    auto& changed_flag_properties = changed_flag_atom.items[1].items[2].items[2].items[2]
        .items[2].items[1].items[0];
    changed_flag_properties.items[13] = list_stream::ListValue::raw_atom("1");
    const auto partial_calendar = form_stream::decode_document(changed_flag_atom, "CalendarForm");
    const auto* partial_calendar_control = partial_calendar ?
        partial_calendar.value().find_control(model::ObjectId{3}) : nullptr;
    expect(partial_calendar.ok() && !partial_calendar.value().reconstruction_complete() &&
               std::any_of(partial_calendar.diagnostics().begin(), partial_calendar.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_calendar_control != nullptr && !std::get<bool>(partial_calendar_control->properties().find(
                   model::PropertyId::from_name("Enabled"))->value),
        "valid unknown CalendarField profile leaf must warn and preserve Enabled=false");
}

void test_calendar_field_begin_display_period() {
    const auto* descriptor = model::metamodel::find_property(
        model::ControlKind::calendar_field, "BeginOfDisplayPeriod");
    expect(descriptor != nullptr && descriptor->value_codec == model::metamodel::ValueCodec::date &&
               descriptor->storage_codec == model::metamodel::StorageCodec::control_info &&
               descriptor->default_value.kind == model::metamodel::DefaultKind::undefined,
        "BeginOfDisplayPeriod must use the existing Date codec and have an Undefined descriptor default");

    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "CalendarPeriod";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode calendar{model::ObjectId{2}, "Calendar", model::CalendarFieldPayload{}};
    calendar.properties().set_explicit(model::PropertyId::from_name("BeginOfDisplayPeriod"),
        model::DateValue{"2024-02-29T00:00:00"});
    document.add_control(std::move(calendar));

    auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "named calendar date must encode" :
        encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message);
    auto find_calendar = [&](auto&& self, list_stream::ListValue& value) -> list_stream::ListValue* {
        if (!value.is_list) return nullptr;
        if (value.items.size() == 6 && !value.items[0].is_list &&
            value.items[0].atom == "e3c063d8-ef92-41be-9c89-b70290b5368b") return &value;
        for (auto& item : value.items) if (auto* found = self(self, item)) return found;
        return nullptr;
    };
    auto* encoded_calendar = find_calendar(find_calendar, encoded.value());
    expect(encoded_calendar != nullptr && encoded_calendar->items[2].items[1].items[5].atom == "20240229000000",
        "BeginOfDisplayPeriod must occupy observed calendar info slot 5");
    const auto decoded = form_stream::decode_document(encoded.value(), "CalendarPeriod");
    const auto* decoded_calendar = decoded ? decoded.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* date = decoded_calendar ? decoded_calendar->properties().find(
        model::PropertyId::from_name("BeginOfDisplayPeriod")) : nullptr;
    expect(decoded.ok() && date != nullptr && std::get<model::DateValue>(date->value).canonical ==
               "2024-02-29T00:00:00",
        "BeginOfDisplayPeriod Date must survive storage round-trip");

    model::Form late_form;
    late_form.id = model::ObjectId{1}; late_form.name = "CalendarPeriodLate";
    late_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument late_document(std::move(late_form));
    model::ControlNode late_source_calendar{model::ObjectId{2}, "Calendar", model::CalendarFieldPayload{}};
    late_source_calendar.properties().set_explicit(model::PropertyId::from_name("BeginOfDisplayPeriod"),
        model::DateValue{"2031-11-07T23:45:10"});
    late_document.add_control(std::move(late_source_calendar));
    const auto late_encoded = form_stream::encode_document(late_document);
    expect(late_encoded.ok(), "BeginOfDisplayPeriod with a non-midnight time must encode");
    const auto late_decoded = form_stream::decode_document(late_encoded.value(), "CalendarPeriod");
    const auto* late_calendar = late_decoded ? late_decoded.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* late_date = late_calendar ? late_calendar->properties().find(
        model::PropertyId::from_name("BeginOfDisplayPeriod")) : nullptr;
    expect(late_encoded.ok() && late_decoded.ok() && late_date != nullptr &&
               std::get<model::DateValue>(late_date->value).canonical == "2031-11-07T23:45:10",
        "BeginOfDisplayPeriod must preserve a non-midnight time through storage round-trip");

    const auto xml = oof::source::serialize_form_xml(document);
    expect(xml.ok() && xml.value().find("<BeginOfDisplayPeriod>2024-02-29T00:00:00</BeginOfDisplayPeriod>") !=
            std::string::npos, "named BeginOfDisplayPeriod Date must serialize as public XML");
    const auto parsed_date_xml = oof::source::parse_form_xml(xml.value());
    expect(parsed_date_xml.ok(), "serialized BeginOfDisplayPeriod XML must parse as a named date");
    const auto xml_encoded = form_stream::encode_document(parsed_date_xml.value());
    expect(xml_encoded.ok(), "XML-only BeginOfDisplayPeriod source must encode without a baseline");
    const auto xml_decoded = form_stream::decode_document(xml_encoded.value(), "CalendarPeriod");
    const auto* xml_calendar = xml_decoded ? xml_decoded.value().find_control(model::ObjectId{2}) : nullptr;
    const auto* xml_date = xml_calendar ? xml_calendar->properties().find(
        model::PropertyId::from_name("BeginOfDisplayPeriod")) : nullptr;
    expect(xml_decoded.ok() && xml_date != nullptr &&
               std::get<model::DateValue>(xml_date->value).canonical == "2024-02-29T00:00:00",
        "named Date XML-only round-trip must preserve its canonical value");
    const std::string default_xml = R"XML(<Form id="1" name="CalendarDefault" ordinaryFormVersion="2.1"><ChildItems><CalendarField id="2" name="Calendar"><Position/><BeginOfDisplayPeriod>undefined</BeginOfDisplayPeriod></CalendarField></ChildItems></Form>)XML";
    auto default_document = oof::source::parse_form_xml(default_xml);
    expect(default_document.ok(), "Undefined BeginOfDisplayPeriod must parse");
    const auto default_output = oof::source::serialize_form_xml(default_document.value());
    expect(default_output.ok() && default_output.value().find("BeginOfDisplayPeriod") == std::string::npos,
        "Undefined BeginOfDisplayPeriod equal to its descriptor default must be omitted from XML");
    const auto undefined_encoded = form_stream::encode_document(default_document.value());
    expect(undefined_encoded.ok(), "explicit Undefined BeginOfDisplayPeriod must encode to its named default slot");
    const auto undefined_decoded = form_stream::decode_document(undefined_encoded.value(), "CalendarDefault");
    const auto* undefined_calendar = undefined_decoded ? undefined_decoded.value().find_control(model::ObjectId{2}) : nullptr;
    expect(undefined_decoded.ok() && undefined_calendar != nullptr &&
               undefined_calendar->properties().find(model::PropertyId::from_name("BeginOfDisplayPeriod")) == nullptr,
        "Undefined slot round-trip must restore the descriptor's implicit Undefined default");

    for (const std::string_view invalid : {"2023-02-29T00:00:00", "2024-13-01T00:00:00",
             "2024-04-31T00:00:00", "2024-01-01T24:00:00", "2024-01-01T00:60:00",
             "2024-01-01T00:00:60", "2024-01-01T00:00:00Z", "2024-01-01T00:00:00.1"}) {
        const std::string bad_xml = std::string("<Form id=\"1\" name=\"CalendarBad\" ordinaryFormVersion=\"2.1\"><ChildItems><CalendarField id=\"2\" name=\"Calendar\"><Position/><BeginOfDisplayPeriod>") +
            std::string(invalid) + "</BeginOfDisplayPeriod></CalendarField></ChildItems></Form>";
        expect(!oof::source::parse_form_xml(bad_xml), "invalid local calendar date must be rejected");
    }
    model::Form sentinel_form;
    sentinel_form.id = model::ObjectId{1}; sentinel_form.name = "CalendarSentinel";
    sentinel_form.children = {model::ControlRef{model::ObjectId{3}}};
    model::OrdinaryFormDocument sentinel_document(std::move(sentinel_form));
    model::ControlNode sentinel_calendar{model::ObjectId{3}, "Calendar", model::CalendarFieldPayload{}};
    sentinel_calendar.properties().set_explicit(model::PropertyId::from_name("BeginOfDisplayPeriod"),
        model::DateValue{"0001-01-01T00:00:00"});
    sentinel_document.add_control(std::move(sentinel_calendar));
    expect(!form_stream::encode_document(sentinel_document),
        "explicit Date colliding with the Undefined storage sentinel must fail");

    for (const std::string_view unsupported : {"EndOfDisplayPeriod", "CurrentDate"}) {
        model::Form unsupported_form;
        unsupported_form.id = model::ObjectId{1}; unsupported_form.name = "UnsupportedCalendarDate";
        unsupported_form.children = {model::ControlRef{model::ObjectId{4}}};
        model::OrdinaryFormDocument unsupported_document(std::move(unsupported_form));
        model::ControlNode unsupported_calendar{model::ObjectId{4}, "Calendar", model::CalendarFieldPayload{}};
        unsupported_calendar.properties().set_explicit(model::PropertyId::from_name(unsupported),
            model::DateValue{"2024-02-29T00:00:00"});
        unsupported_document.add_control(std::move(unsupported_calendar));
        expect(!form_stream::encode_document(unsupported_document),
            "unsupported CalendarField date properties must remain outside this storage allowlist");
    }
}

void test_fresh_progress_bar_runtime_record_and_rejections() {
    constexpr std::string_view xml =
        R"OOF(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><ChildItems><ProgressBar id="4" name="ProgressResearch"><Position/></ProgressBar></ChildItems></Form>)OOF";
    const auto parsed = oof::source::parse_form_xml(xml);
    expect(parsed.ok(), "ProgressBar XML-only source must parse before native encoding");
    auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), "ProgressBar must encode from its named XML object model");

    constexpr std::string_view explicit_defaults_xml =
        R"OOF(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><ChildItems><ProgressBar id="4" name="ProgressResearch"><Position/><MaxValue>100</MaxValue><MinValue>0</MinValue><Step>1</Step></ProgressBar></ChildItems></Form>)OOF";
    const auto explicit_defaults = oof::source::parse_form_xml(explicit_defaults_xml);
    expect(explicit_defaults.ok(), "explicit ProgressBar numeric defaults must parse");
    if (explicit_defaults) {
        const auto defaults_xml = oof::source::serialize_form_xml(explicit_defaults.value());
        expect(defaults_xml.ok() && defaults_xml.value().find("<MaxValue>") == std::string::npos &&
                   defaults_xml.value().find("<MinValue>") == std::string::npos &&
                   defaults_xml.value().find("<Step>") == std::string::npos,
            "XML writer must omit explicit ProgressBar values equal to DecimalValue defaults");
    }

    constexpr std::string_view runtime_record = R"OOF({b1db1f86-abbb-4cf0-8852-fe6ae21650c2,4,{0,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},3,0,100,1,1,0,2}},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,1,2,0,0},{14,"ProgressResearch",4294967295,0,0,0},{0}})OOF";
    auto observed = list_stream::parse(runtime_record);
    auto* generated_record = static_cast<list_stream::ListValue*>(nullptr);
    const auto find_progress = [&](auto&& self, list_stream::ListValue& value) -> list_stream::ListValue* {
        if (!value.is_list) return nullptr;
        if (value.items.size() == 6 && !value.items[0].is_list &&
            value.items[0].atom == "b1db1f86-abbb-4cf0-8852-fe6ae21650c2") return &value;
        for (auto& item : value.items) if (auto* found = self(self, item)) return found;
        return nullptr;
    };
    generated_record = find_progress(find_progress, encoded.value());
    expect(generated_record != nullptr, "fresh ProgressBar output must contain a named child record");
    observed.items[3] = generated_record->items[3];
    *generated_record = observed;

    const auto decoded = form_stream::decode_document(encoded.value(), "Progress");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* progress = decoded.value().find_control(model::ObjectId{4});
    expect(progress && progress->kind() == model::ControlKind::progress_bar && progress->name == "ProgressResearch" &&
               progress->properties().find(model::PropertyId::from_name("Enabled")) == nullptr,
        "observed native ProgressBar record must decode to named identity and Enabled=true");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "observed native ProgressBar default record must re-encode without drift");

    constexpr std::string_view numeric_xml = R"OOF(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><ChildItems><ProgressBar id="4" name="ProgressResearch"><Position/><MaxValue>321</MaxValue><MinValue>-17</MinValue><Step>25</Step></ProgressBar></ChildItems></Form>)OOF";
    const auto numeric_parsed = oof::source::parse_form_xml(numeric_xml);
    expect(numeric_parsed.ok(), "named ProgressBar numeric properties must parse from XML");
    const auto numeric_encoded = form_stream::encode_document(numeric_parsed.value());
    expect(numeric_encoded.ok(), "named ProgressBar integer values must encode from XML");
    auto numeric_observed = observed;
    numeric_observed.items[2].items[1].items[2] = list_stream::ListValue::raw_atom("-17");
    numeric_observed.items[2].items[1].items[3] = list_stream::ListValue::raw_atom("321");
    numeric_observed.items[2].items[1].items[4] = list_stream::ListValue::raw_atom("25");
    auto numeric_stream = numeric_encoded.value();
    auto* numeric_record = find_progress(find_progress, numeric_stream);
    expect(numeric_record != nullptr, "numeric ProgressBar output must contain a named child record");
    numeric_observed.items[3] = numeric_record->items[3];
    *numeric_record = numeric_observed;
    const auto numeric_decoded = form_stream::decode_document(numeric_stream, "ProgressNumeric");
    expect(numeric_decoded.ok(), "observed numeric ProgressBar record must decode");
    const auto* numeric_control = numeric_decoded.value().find_control(model::ObjectId{4});
    const auto* decoded_max = numeric_control->properties().find(model::PropertyId::from_name("MaxValue"));
    const auto* decoded_min = numeric_control->properties().find(model::PropertyId::from_name("MinValue"));
    const auto* decoded_step = numeric_control->properties().find(model::PropertyId::from_name("Step"));
    expect(decoded_max && std::get<std::int64_t>(decoded_max->value) == 321 &&
               decoded_min && std::get<std::int64_t>(decoded_min->value) == -17 &&
               decoded_step && std::get<std::int64_t>(decoded_step->value) == 25,
        "observed MaxValue, MinValue, and Step must decode as named numeric properties");
    const auto numeric_reencoded = form_stream::encode_document(numeric_decoded.value());
    expect(numeric_reencoded.ok() &&
               list_stream::dump_compact(numeric_reencoded.value()) == list_stream::dump_compact(numeric_stream),
        "observed numeric ProgressBar properties must re-encode to their integer info slots");

    for (const std::string_view property : {"MaxValue", "MinValue", "Step"}) {
        const auto property_xml = [property](std::string_view value) {
            return std::string("<Form id=\"1\" name=\"Progress\" ordinaryFormVersion=\"2.1\"><ChildItems><ProgressBar id=\"4\" name=\"P\"><Position/><") +
                std::string(property) + ">" + std::string(value) + "</" + std::string(property) +
                "></ProgressBar></ChildItems></Form>";
        };
        const auto fractional = oof::source::parse_form_xml(property_xml("12.5"));
        expect(!fractional, "fractional ProgressBar XML must be rejected by the int32 contract");
        const std::size_t storage_slot = property == "MaxValue" ? 3 : property == "MinValue" ? 2 : 4;
        for (const std::string_view endpoint : {"2147483647", "-2147483648"}) {
            const auto endpoint_parsed = oof::source::parse_form_xml(property_xml(endpoint));
            expect(endpoint_parsed.ok(), "ProgressBar int32 endpoint must parse as a named integer");
            const auto endpoint_encoded = form_stream::encode_document(endpoint_parsed.value());
            expect(endpoint_encoded.ok(), "ProgressBar signed int32 endpoint must encode");
            auto endpoint_stream = endpoint_encoded.value();
            auto* endpoint_record = find_progress(find_progress, endpoint_stream);
            expect(endpoint_record && endpoint_record->items[2].items[1].items[storage_slot].atom == endpoint,
                "ProgressBar signed int32 endpoint must occupy its observed numeric info slot");
        }
        for (const std::string_view out_of_range : {"2147483648", "-2147483649"}) {
            const auto parsed = oof::source::parse_form_xml(property_xml(out_of_range));
            expect(!parsed, "ProgressBar XML outside signed int32 must be rejected before storage");
        }
    }

    model::Form changed_form;
    changed_form.id = model::ObjectId{1};
    changed_form.name = "Progress";
    changed_form.children = {model::ControlRef{model::ObjectId{4}}};
    model::OrdinaryFormDocument changed(std::move(changed_form));
    model::ControlNode changed_progress{model::ObjectId{4}, "ProgressResearch", model::ProgressBarPayload{}};
    changed_progress.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    changed_progress.position.visible.set(false);
    changed_progress.properties().set_explicit(model::PropertyId::from_name("ToolTip"), "Прогресс Ω & <тег>");
    changed.add_control(std::move(changed_progress));
    const auto changed_stream = form_stream::encode_document(changed);
    expect(changed_stream.ok(), "ProgressBar Enabled, Visible, and confirmed ToolTip changes must encode");
    const auto changed_decoded = form_stream::decode_document(changed_stream.value(), "Progress");
    expect(changed_decoded.ok(), "changed ProgressBar stream must decode fresh");
    const auto* changed_readback = changed_decoded.value().find_control(model::ObjectId{4});
    expect(changed_readback && !std::get<bool>(changed_readback->properties().find(
               model::PropertyId::from_name("Enabled"))->value) && !changed_readback->position.visible.value() &&
               std::get<std::string>(changed_readback->properties().find(
                   model::PropertyId::from_name("ToolTip"))->value) == "Прогресс Ω & <тег>",
        "named ProgressBar changes must survive fresh decode");

    auto unsupported_leaf = encoded.value();
    auto* unsupported_record = find_progress(find_progress, unsupported_leaf);
    unsupported_record->items[2].items[1].items[0].items[15] = list_stream::ListValue::raw_atom("1");
    const auto unsupported_decode = form_stream::decode_document(unsupported_leaf, "Progress");
    const auto* partial_progress = unsupported_decode ?
        unsupported_decode.value().find_control(model::ObjectId{4}) : nullptr;
    expect(unsupported_decode.ok() && !unsupported_decode.value().reconstruction_complete() &&
               std::any_of(unsupported_decode.diagnostics().begin(), unsupported_decode.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_progress != nullptr && partial_progress->name == "ProgressResearch",
        "valid unknown ProgressBar leaf must warn and preserve the named control");

    constexpr std::string_view data_path_xml = R"OOF(<Form id="1" name="Progress" ordinaryFormVersion="2.1"><Attributes><Attribute id="3" name="Amount"><TypeDomain><Entry term="numeric" length="10" precision="2" nonNegative="true"/></TypeDomain></Attribute></Attributes><ChildItems><ProgressBar id="4" name="P"><DataPath attributeId="3"/><Position/></ProgressBar></ChildItems></Form>)OOF";
    const auto data_path = oof::source::parse_form_xml(data_path_xml);
    expect(data_path.ok(), "ProgressBar direct numeric DataPath with named qualifiers must parse");
    const auto data_path_encoded = form_stream::encode_document(data_path.value());
    expect(data_path_encoded.ok(), data_path_encoded ? "" : data_path_encoded.diagnostics().front().message);
    const auto& progress_links = data_path_encoded.value().items[2].items[3];
    expect(progress_links.items.size() == 2 && progress_links.items[1].items[0].atom == "4" &&
               progress_links.items[1].items[1].items[1].items[0].atom == "3",
        "ProgressBar DataPath must encode as control 4 linked to local Attribute 3");
    const auto data_path_decoded = form_stream::decode_document(data_path_encoded.value(), "Progress");
    expect(data_path_decoded.ok() && data_path_decoded.value().find_control(model::ObjectId{4})->data_path &&
               data_path_decoded.value().find_control(model::ObjectId{4})->data_path->attribute.id() == model::ObjectId{3},
        "ProgressBar direct numeric DataPath must survive decode");
    const auto data_path_reencoded = form_stream::encode_document(data_path_decoded.value());
    expect(data_path_reencoded.ok() && list_stream::dump_compact(data_path_reencoded.value()) ==
               list_stream::dump_compact(data_path_encoded.value()),
        "ProgressBar direct numeric DataPath must survive encode-decode-encode");

    const auto xml_for_domain = [](std::string_view domain, std::string_view target = "3") {
        return std::string("<Form id=\"1\" name=\"Progress\" ordinaryFormVersion=\"2.1\"><Attributes><Attribute id=\"3\" name=\"Amount\"><TypeDomain>") +
            std::string(domain) + "</TypeDomain></Attribute></Attributes><ChildItems><ProgressBar id=\"4\" name=\"P\"><DataPath attributeId=\"" +
            std::string(target) + "\"/><Position/></ProgressBar></ChildItems></Form>";
    };
    for (const auto [domain, label] : std::array<std::pair<std::string_view, std::string_view>, 4>{{
             {"<Entry term=\"string\" length=\"64\"/>", "nonNumeric"},
             {"<Entry term=\"numeric\" length=\"10\"/><Entry term=\"numeric\" length=\"8\"/>", "compound"},
             {"<Entry term=\"object\" typeUuid=\"01234567-89AB-CDEF-0123-456789ABCDEF\"/>", "unknown"},
             {"<Entry term=\"numeric\" length=\"10\"/><Entry term=\"string\" length=\"2\"/>", "variant"},
         }}) {
        const auto invalid = oof::source::parse_form_xml(xml_for_domain(domain));
        expect(invalid.ok() && !form_stream::encode_document(invalid.value()),
            std::string("ProgressBar must reject a ") + std::string(label) + " Attribute domain");
    }
    const auto dangling_xml = oof::source::parse_form_xml(xml_for_domain(
        "<Entry term=\"numeric\" length=\"10\" precision=\"2\"/>", "99"));
    expect(!dangling_xml || !form_stream::encode_document(dangling_xml.value()),
        "ProgressBar must reject a dangling DataPath Attribute ID");
    auto compound_xml_text = xml_for_domain(
        "<Entry term=\"numeric\" length=\"10\" precision=\"2\"/>");
    const auto empty_path_end = compound_xml_text.find("/><Position/>");
    compound_xml_text.replace(empty_path_end, std::string("/><Position/>").size(),
        "><Member>Nested</Member></DataPath><Position/>");
    const auto compound_path_xml = oof::source::parse_form_xml(compound_xml_text);
    expect(compound_path_xml.ok() && !form_stream::encode_document(compound_path_xml.value()),
        "ProgressBar must reject compound DataPath members");
    auto metadata_uuid = data_path_encoded.value();
    metadata_uuid.items[2].items[3].items[1].items[1].items[1] =
        list_stream::ListValue::list({list_stream::ListValue::raw_atom("3"),
            list_stream::ListValue::raw_atom("01234567-89AB-CDEF-0123-456789ABCDEF")});
    expect(!form_stream::decode_document(metadata_uuid, "Progress"),
        "ProgressBar must reject metadata UUID Attribute links");
    auto dangling_link = data_path_encoded.value();
    dangling_link.items[2].items[3].items[1].items[1].items[1].items[0] =
        list_stream::ListValue::raw_atom("99");
    expect(!form_stream::decode_document(dangling_link, "Progress"),
        "ProgressBar decoder must reject dangling Attribute links");
    auto non_numeric_link = data_path_encoded.value();
    model::TypeDomainPatternValue linked_string_type;
    model::TypeDomainEntry linked_string_entry;
    linked_string_entry.term = model::TypeDomainTerm::string;
    linked_string_entry.string = model::LengthQualifiers{64, false};
    linked_string_type.entries.push_back(linked_string_entry);
    non_numeric_link.items[2].items[2].items[1].items[5] =
        list_stream::parse(value_codec::encode_type_domain(linked_string_type));
    expect(!form_stream::decode_document(non_numeric_link, "Progress"),
        "ProgressBar decoder must reject a linked nonNumeric Attribute");

    model::Form overflow_form;
    overflow_form.id = model::ObjectId{1};
    overflow_form.name = "Progress";
    constexpr auto overflow_id = std::numeric_limits<std::uint64_t>::max();
    overflow_form.children = {model::ControlRef{model::ObjectId{overflow_id}}};
    model::OrdinaryFormDocument overflow(std::move(overflow_form));
    overflow.add_control(model::ControlNode{model::ObjectId{overflow_id}, "P", model::ProgressBarPayload{}});
    const auto overflow_result = form_stream::encode_document(overflow);
    expect(!overflow_result && overflow_result.diagnostics().front().code == "OOF1122",
        "ProgressBar ID above int64 must be rejected before encoding");
}

void test_dendrogram_orientation_named_codec() {
    constexpr std::string_view xml =
        R"OOF(<Form id="1" name="DendrogramForm" ordinaryFormVersion="2.1"><ChildItems><Dendrogram id="4" name="Hierarchy"><Position/><Orientation type="DendrogramOrientation" member="Down"/></Dendrogram></ChildItems></Form>)OOF";
    const auto parsed = oof::source::parse_form_xml(xml);
    expect(parsed.ok(), "Dendrogram Orientation XML must parse as a named enumeration");
    auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), "Dendrogram Orientation must encode from named XML");
    auto encoded_stream = encoded.value();
    auto* control_record_pointer = static_cast<list_stream::ListValue*>(nullptr);
    const auto find_dendrogram = [&](const auto& self, auto& value) -> void {
        if (value.is_list && value.items.size() == 6 && !value.items.empty() &&
            !value.items[0].is_list && value.items[0].atom ==
                oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid) {
            control_record_pointer = &value;
            return;
        }
        for (auto& item : value.items) {
            if (control_record_pointer == nullptr) self(self, item);
        }
    };
    find_dendrogram(find_dendrogram, encoded_stream);
    expect(control_record_pointer != nullptr, "Dendrogram record must be present in the encoded stream");
    const auto& control_record = *control_record_pointer;
    expect(control_record.items[2].items[4].atom == "1",
        "Down Orientation must use the observed runtime storage value 1");
    const auto decoded = form_stream::decode_document(encoded_stream, "DendrogramForm");
    expect(decoded.ok(), "Dendrogram Orientation native record must decode");
    const auto* dendrogram = decoded.value().find_control(model::ObjectId{4});
    expect(dendrogram != nullptr &&
               std::get<model::EnumerationValue>(dendrogram->properties().find(
                   model::PropertyId::from_name("Orientation"))->value) ==
                   model::EnumerationValue{"DendrogramOrientation", "Down"},
        "Down Orientation must round-trip as a named model property");
    const auto serialized = oof::source::serialize_form_xml(decoded.value());
    expect(serialized.ok() && serialized.value().find(
               "<Orientation type=\"DendrogramOrientation\" member=\"Down\"/>") != std::string::npos,
        "decoded Orientation must remain visible as named XML");

    constexpr std::string_view explicit_up_xml =
        R"OOF(<Form id="1" name="DendrogramForm" ordinaryFormVersion="2.1"><ChildItems><Dendrogram id="4" name="Hierarchy"><Position/><Orientation type="DendrogramOrientation" member="Up"/></Dendrogram></ChildItems></Form>)OOF";
    const auto explicit_up = oof::source::parse_form_xml(explicit_up_xml);
    expect(explicit_up.ok(), "explicit Up Orientation must parse");
    const auto explicit_up_serialized = oof::source::serialize_form_xml(explicit_up.value());
    expect(explicit_up_serialized.ok() && explicit_up_serialized.value().find("<Orientation") == std::string::npos,
        "explicit default Up Orientation must be omitted by the named XML writer");
    const auto up_record = form_stream::encode_document(explicit_up.value());
    expect(up_record.ok(), "explicit Up Orientation must encode as canonical storage default");
    const auto up_decoded = form_stream::decode_document(up_record.value(), "DendrogramForm");
    expect(up_decoded.ok(), "canonical Up Orientation record must decode");
    const auto up_xml = oof::source::serialize_form_xml(up_decoded.value());
    expect(up_xml.ok() && up_xml.value().find("<Orientation") == std::string::npos,
        "decoded default Up Orientation must remain implicit in the public XML model");

    constexpr std::string_view unsupported_xml =
        R"OOF(<Form id="1" name="DendrogramForm" ordinaryFormVersion="2.1"><ChildItems><Dendrogram id="4" name="Hierarchy"><Position/><Orientation type="DendrogramOrientation" member="Right"/></Dendrogram></ChildItems></Form>)OOF";
    const auto unsupported = oof::source::parse_form_xml(unsupported_xml);
    expect(unsupported.ok(), "syntactically valid but unproven enum member must parse for codec rejection");
    expect_failure(form_stream::encode_document(unsupported.value()), "OOF1122", "$/Dendrogram/Orientation",
        "unproven Dendrogram orientation enum member must fail closed");

    auto changed_tree = encoded_stream;
    control_record_pointer = nullptr;
    find_dendrogram(find_dendrogram, changed_tree);
    expect(control_record_pointer != nullptr, "encoded graph fixture must contain Dendrogram");
    control_record_pointer->items[2].items[1].items[0] =
        list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(changed_tree, "DendrogramForm"), "OOF1114",
        "$/1/2/2/1/2", "nondefault graph data must be rejected by the Orientation-only profile");
}


void test_dendrogram_strict_native_fixture() {
    constexpr std::string_view carrier_xml =
        R"OOF(<Form id="1" name="NativeFixture" ordinaryFormVersion="2.1"><ChildItems><LabelDecoration id="4" name="Carrier"><Position/></LabelDecoration></ChildItems></Form>)OOF";
    const auto carrier = oof::source::parse_form_xml(carrier_xml);
    expect(carrier.ok(), carrier ? "" : carrier.diagnostics().front().path + ": " + carrier.diagnostics().front().message);
    auto stream_result = form_stream::encode_document(carrier.value());
    expect(stream_result.ok(), "native fixture carrier must establish an ordinary-form envelope");
    auto stream = stream_result.value();
    list_stream::ListValue* carrier_record = nullptr;
    std::function<void(list_stream::ListValue&, std::string_view)> find_record = [&](list_stream::ListValue& value, std::string_view guid) {
        if (value.is_list && value.items.size() == 6 && !value.items.empty() &&
            value.items[0].atom == guid) {
            carrier_record = &value;
            return;
        }
        for (auto& item : value.items) if (carrier_record == nullptr) find_record(item, guid);
    };
    find_record(stream, oof::model::metamodel::descriptor_for(model::ControlKind::label_decoration).guid);
    expect(carrier_record != nullptr, "fixture carrier record must be present");
    const auto geometry = carrier_record->items[3];
    // Captured from a strict Designer 8.5.1.1343 dump of a synthetic empty Dendrogram (Form.bin SHA-256 1a510dcc13b1f4f49a8540afb95d1d036c419b5d2936ca962790358ade665fa3). The record ID, name, and geometry are adapted to the test carrier.
    constexpr std::string_view strict_native_record = R"NATIVE(
{984981b1-622d-4ebc-94f7-885f0cdfb59a,4,{0,{0,{11},{75,1,0,1,0,{4,0,{11837108},0},{4,0,{0},1,2,0,e5cabe59-d992-4d31-8086-3116931aff81,0},1,{1,1,{"ru","Сводная"}},0,0,0,1,{"U"},{"U"},0,1,0,-1,0,4,0,", ",4,{1,0},{1,0},{4,3,{-3},3},0,0,{1,0},1,0,{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,3,{-22},3},0,{4,3,{-1},3},1,{4,3,{-1},3},1,{4,3,{-1},3},0,{4,0,{16777215},0},{4,3,{-3},3},{4,3,{-3},3},{4,3,{-3},3},{8,3,0,1,100},{8,3,0,1,100},{8,3,0,1,100},1,1,1,1,1,{1,0},0,{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},{4,4,{0},4},1,1,0,4,30,1,0,0,0,0,1,0,0,0,0,1,1,2,{1,0},1,0,0,0,{4,0,{169},0},0,0,{1,0,0,0},0,180,5,1,0,4,{4,0,{11119017},0},1,0,1,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,{4,3,{-22},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},"",0,1,14,2,{8,3,0,1,100},1,{4,4,{0},4},{3,0,{0},1,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},1,1,1,0,0,95,1e-1,1e-1,3e-2,{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},{4,0,{0},0},2,255,0,0,00000000-0000-0000-0000-000000000000,0,{0,0},0,{0,0,{0,1,0,1,0},0,0},{0,0,{0,1,0,1,0},0,0},0,0,2,-2,1,10,1,20,0,0,{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},0,0,{4,4,{0},4},{4,4,{0},4},0,{{4,4,{0},4},4,0,0,0,"",{1,0},{1,0},{1,0},0},0,0,0,0,0,0,1,1,0,0,1,1,0,6,0,0,0,0.17,0,0.83,0.08,0,0,0.83,0,0,0.92,{0,0},{0,0},{0,0},{0,0},{0,14,{4,4,{0},4},{4,4,{0},4},0,0},{0,14,{4,4,{0},4},{4,4,{0},4},0,0},0,0,{0,0,0,0,0},{0,0,0,0},0,,60,{2,0,0,2,{1,0},{1,4,0.5,0.5,{8,3,0,1,100},{4,4,{0},4},{4,4,{0},4},1,{3,0,{0},0,1,0,48312c09-257f-4b29-b280-284dd89efc1e},{4,4,{0},4},4,2,0},2,0,0,{4,4,{0},4},{8,3,0,1,100},{4,4,{0},4},2,{1,0},0,{4,4,{0},4},0,0,0,0,0,0},{0,0,{0,1,0,1,0},0,0},0,0,0,0,0,0,0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4}}},{0,{3,0,1,0,{0,{8,0,0,0,0,0,{"U"},{1,0},{"U"},0,4294901761}},{0,1,{0,{4,0,{0},0},{4,0,{0},0}}},1,0}},{0,{3,0,1,0,{0,{8,0,0,0,0,0,{"U"},{1,0},{"U"},0,4294901761},0,0,0},{0,1,{0,{4,0,{0},0},{4,0,{0},0}}},1,0}},1,1,6,12,{4,0,{8388608},0},{4,0,{0},1,1,0,e5cabe59-d992-4d31-8086-3116931aff81,0},0},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,0,2,0,0},{14,"DendrogramProduct",4294967295,0,0,0},{0}}
)NATIVE";
    auto native_record = list_stream::parse(strict_native_record);
    native_record.items[1] = list_stream::ListValue::raw_atom("15");
    native_record.items[3] = geometry;
    native_record.items[4].items[1] = list_stream::ListValue::string_atom("NativeDendrogram");
    *carrier_record = std::move(native_record);
    stream.items[1].items[1].items[1] = list_stream::ListValue::raw_atom("15");
    stream.items[2].items[1] = list_stream::ListValue::raw_atom("16");

    const auto decoded = form_stream::decode_document(stream, "NativeFixture");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message + " expected " + decoded.diagnostics().front().expected + " actual " + decoded.diagnostics().front().actual);
    const auto* dendrogram = decoded.value().find_control(model::ObjectId{15});
    expect(dendrogram != nullptr && dendrogram->kind() == model::ControlKind::dendrogram &&
               dendrogram->name == "NativeDendrogram",
        "native fixture must produce its adapted named Dendrogram");
    const auto* orientation = dendrogram->properties().find(model::PropertyId::from_name("Orientation"));
    expect(orientation != nullptr && std::get<model::EnumerationValue>(orientation->value) ==
               model::EnumerationValue{"DendrogramOrientation", "Down"},
        "strict native fixture must retain Down orientation");

    auto malformed = stream;
    carrier_record = nullptr;
    find_record(malformed, oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid);
    expect(carrier_record != nullptr, "malformed fixture record must be present");
    carrier_record->items.pop_back();
    expect_failure(form_stream::decode_document(malformed, "NativeFixture"), "OOF1102",
        "$/1/2/2/1", "malformed Dendrogram control arity must be rejected");

    auto short_payload = stream;
    carrier_record = nullptr;
    find_record(short_payload, oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid);
    carrier_record->items[2].items.pop_back();
    expect_failure(form_stream::decode_document(short_payload, "NativeFixture"), "OOF1102",
        "$/1/2/2/1/2", "short Dendrogram data record must be rejected");

    auto invalid_id = stream;
    carrier_record = nullptr;
    find_record(invalid_id, oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid);
    carrier_record->items[1] = list_stream::ListValue::raw_atom("0");
    expect_failure(form_stream::decode_document(invalid_id, "NativeFixture"), "OOF1122",
        "$/1/2/2/1/1", "invalid Dendrogram ID must be rejected");

    auto unknown_orientation = stream;
    carrier_record = nullptr;
    find_record(unknown_orientation, oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid);
    carrier_record->items[2].items[4] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(unknown_orientation, "NativeFixture"), "OOF1122",
        "$/1/2/2/1/2/4", "unknown Dendrogram orientation must be rejected");

    constexpr std::string_view linked_carrier_xml = R"OOF(<Form id="1" name="LinkedFixture" ordinaryFormVersion="2.1"><Attributes><Attribute id="3" name="Text"><TypeDomain><Entry term="string" length="20"/></TypeDomain></Attribute></Attributes><ChildItems><InputField id="4" name="Carrier"><DataPath attributeId="3"/><Position/></InputField></ChildItems></Form>)OOF";
    const auto linked_carrier = oof::source::parse_form_xml(linked_carrier_xml);
    expect(linked_carrier.ok(), linked_carrier ? "" : linked_carrier.diagnostics().front().message);
    auto linked_stream_result = form_stream::encode_document(linked_carrier.value());
    expect(linked_stream_result.ok(), "linked InputField carrier must establish a real DataPath link");
    auto linked_stream = linked_stream_result.value();
    carrier_record = nullptr;
    find_record(linked_stream, oof::model::metamodel::descriptor_for(model::ControlKind::input_field).guid);
    expect(carrier_record != nullptr, "linked carrier control record must be present");
    auto linked_native_record = list_stream::parse(strict_native_record);
    linked_native_record.items[1] = list_stream::ListValue::raw_atom("4");
    linked_native_record.items[3] = carrier_record->items[3];
    linked_native_record.items[4].items[1] = list_stream::ListValue::string_atom("LinkedDendrogram");
    *carrier_record = std::move(linked_native_record);
    expect_failure(form_stream::decode_document(linked_stream, "LinkedFixture"), "OOF1122", "$/2/3",
        "Dendrogram must reject a linked DataPath instead of dropping it");
}

void test_dendrogram_named_graph_candidate_roundtrip() {
    constexpr std::string_view xml = R"OOF(<Form id="1" name="DendrogramGraph" ordinaryFormVersion="2.1"><ChildItems><Dendrogram id="2" name="Tree"><Position/><Items><Item><Value>node-C</Value><Text><Item language="ru">Узел C</Item></Text></Item><Item><Value>node-A</Value><Text><Item language="ru">Узел A</Item></Text></Item><Item><Value>node-B</Value><Text><Item language="ru">Узел B</Item></Text></Item></Items><Links><Link><FirstItem>node-C</FirstItem><SecondItem>node-A</SecondItem><Title><Item language="ru">Связь C-A</Item></Title><Distance>1.5</Distance></Link><Link><FirstItem>node-B</FirstItem><SecondItem>node-A</SecondItem><Title><Item language="ru">Связь B-A</Item></Title></Link></Links></Dendrogram></ChildItems></Form>)OOF";
    const auto parsed = oof::source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    if (!parsed.ok()) return;
    const auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message);
    if (!encoded.ok()) return;
    auto stream = encoded.value();
    const auto decoded = form_stream::decode_document(stream, "DendrogramGraph");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    if (!decoded.ok()) return;
    const auto* dendrogram = decoded.value().find_control(model::ObjectId{2});
    const auto* graph = dendrogram == nullptr ? nullptr : std::get_if<model::DendrogramPayload>(&dendrogram->payload);
    expect(graph != nullptr && graph->items.size() == 3 && graph->links.size() == 2,
        "named Items and Links must round-trip through typed model records");
    if (graph == nullptr) return;
    expect(graph->items[0].value == "node-C" && graph->items[0].text.items.front().text == "Узел C" &&
           graph->items[2].value == "node-B" && graph->links[0].first_item == "node-C" &&
           graph->links[0].second_item == "node-A" && graph->links[0].distance.canonical == "1.5" &&
           graph->links[1].first_item == "node-B" && graph->links[1].second_item == "node-A",
        "named values, localized text, distance, and endpoint references must survive the codec");
    const auto xml_roundtrip = oof::source::serialize_form_xml(decoded.value());
    expect(xml_roundtrip.ok() && xml_roundtrip.value().find("<Items>") != std::string::npos &&
           xml_roundtrip.value().find("<FirstItem>node-C</FirstItem>") != std::string::npos,
        "source writer must emit named Dendrogram concepts");
    const auto encoded_again = form_stream::encode_document(decoded.value());
    expect(encoded_again.ok() && list_stream::dump_compact(encoded_again.value()) == list_stream::dump_compact(encoded.value()),
        "bounded default-cache candidate must round-trip deterministically");

    auto find_dendrogram_record = [](list_stream::ListValue& root) -> list_stream::ListValue* {
        list_stream::ListValue* found = nullptr;
        std::function<void(list_stream::ListValue&)> visit = [&](list_stream::ListValue& value) {
            if (!found && value.is_list && value.items.size() == 6 &&
                value.items[0].atom == oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid) {
                found = &value;
                return;
            }
            for (auto& child : value.items) if (!found) visit(child);
        };
        visit(root);
        return found;
    };
    auto unknown_cache = encoded.value();
    auto* cache_control = find_dendrogram_record(unknown_cache);
    expect(cache_control != nullptr, "encoded Dendrogram must be locatable for strict negative checks");
    if (cache_control != nullptr) {
        auto& elements = cache_control->items[2].items[2].items[1];
        elements.items[elements.items.size() - 3] = list_stream::parse("{0,1,{0,{4,0,{204},0},{4,0,{0},0}}}");
        expect(!form_stream::decode_document(unknown_cache, "DendrogramGraph"),
            "unproven appearance cache data must fail closed");
    }
    auto unsupported_details = encoded.value();
    auto* details_control = find_dendrogram_record(unsupported_details);
    if (details_control != nullptr) {
        auto& row = details_control->items[2].items[2].items[1].items[4].items[1];
        row.items[8] = list_stream::parse("{\"S\",\"unsupported details\"}");
        expect(!form_stream::decode_document(unsupported_details, "DendrogramGraph"),
            "unmodeled node Details must fail closed");
    }
    auto malformed_row = encoded.value();
    auto* arity_control = find_dendrogram_record(malformed_row);
    if (arity_control != nullptr) {
        arity_control->items[2].items[2].items[1].items[4].items[1].items.pop_back();
        expect(!form_stream::decode_document(malformed_row, "DendrogramGraph"),
            "malformed Dendrogram node record arity must fail closed");
    }

    const auto make_document = [](model::DendrogramPayload payload) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "DendrogramGraph";
        form.children.push_back(model::ControlRef{model::ObjectId{2}});
        model::OrdinaryFormDocument document(std::move(form));
        document.add_control(model::ControlNode{model::ObjectId{2}, "Tree", std::move(payload)});
        return document;
    };
    auto invalid_graph = *graph;
    invalid_graph.links[1].second_item = "missing";
    expect(!form_stream::encode_document(make_document(invalid_graph)), "unresolved graph references must fail closed");
    invalid_graph = *graph;
    invalid_graph.links[1] = invalid_graph.links[0];
    expect(!form_stream::encode_document(make_document(invalid_graph)), "duplicate graph edges must fail closed");
    invalid_graph = *graph;
    invalid_graph.links[0].distance.canonical = "NaN";
    expect(!form_stream::encode_document(make_document(invalid_graph)), "non-finite distance must fail closed");
}

void test_dendrogram_native_three_item_two_link_cursor_fixture() {
    constexpr std::string_view carrier_xml =
        R"OOF(<Form id="1" name="NativeGraphFixture" ordinaryFormVersion="2.1"><ChildItems><Dendrogram id="2" name="Tree"><Position/></Dendrogram></ChildItems></Form>)OOF";
    const auto parsed = oof::source::parse_form_xml(carrier_xml);
    expect(parsed.ok(), "native graph fixture carrier must parse");
    if (!parsed.ok()) return;
    auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), "native graph fixture carrier must encode");
    if (!encoded.ok()) return;
    auto stream = encoded.value();
    list_stream::ListValue* record = nullptr;
    std::function<void(list_stream::ListValue&)> find_record = [&](list_stream::ListValue& value) {
        if (!record && value.is_list && value.items.size() == 6 && !value.items[0].is_list &&
            value.items[0].atom == oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid) {
            record = &value;
            return;
        }
        for (auto& child : value.items) if (!record && child.is_list) find_record(child);
    };
    find_record(stream);
    expect(record != nullptr, "native graph fixture must locate the Dendrogram record");
    if (!record) return;

    // Extracted from strict Designer 8.5.1.1343 run11 Form.bin SHA-256
    // d869b334e2fd31bdd31306dac11b09218f1455bcc3e9f3b994d72b3ef2eef40b.
    // Only cache keys and appearance-cache rows are normalized below because
    // this test covers the independently observed collection cursor grammar.
    auto native_items = list_stream::parse(R"NATIVE({0,{3,0,4,1,{0,{8,1,0,0,2,0,{"S","node-C"},{1,1,{"ru","Узел C"}},{"U"},2,0}},2,{0,{8,2,0,0,3,0,{"S","node-A"},{1,1,{"ru","Узел A"}},{"U"},4,0}},3,{0,{8,3,0,0,0,0,{"S","node-B"},{1,1,{"ru","Узел B"}},{"U"},6,0}},0,{0,{8,0,0,1,0,3,{"U"},{1,0},{"U"},0,4294901761}},{0,7,{0,{4,0,{0},0},{4,0,{0},0}},{0,{4,0,{204},0},{4,0,{0},0}},{0,{4,0,{204},0},{4,0,{6723840},0}},{0,{4,0,{10053120},0},{4,0,{0},0}},{0,{4,0,{10053120},0},{4,0,{52479},0}},{0,{4,0,{13434624},0},{4,0,{0},0}},{0,{4,0,{13434624},0},{4,0,{10053375},0}}},1,0}})NATIVE");
    auto native_links = list_stream::parse(R"NATIVE({0,{3,0,3,1,{0,{8,1,0,0,2,0,{"U"},{1,1,{"ru","Связь 1"}},{"U"},2,0},1,2,0},2,{0,{8,2,0,0,0,0,{"U"},{1,1,{"ru","Связь 2"}},{"U"},4,0},3,2,0},0,{0,{8,0,0,1,0,2,{"U"},{1,0},{"U"},0,4294901761},0,0,0},{0,5,{0,{4,0,{0},0},{4,0,{0},0}},{0,{4,0,{204},0},{4,0,{0},0}},{0,{4,0,{204},0},{4,0,{6723840},0}},{0,{4,0,{10053120},0},{4,0,{0},0}},{0,{4,0,{10053120},0},{4,0,{52479},0}}},1,0}})NATIVE");

    expect(native_items.items[1].items.size() == 14 && native_items.items[1].items[3].atom == "1" &&
               native_items.items[1].items[4].is_list && native_items.items[1].items[5].atom == "2" &&
               native_items.items[1].items[6].is_list && native_items.items[1].items[7].atom == "3" &&
               native_items.items[1].items[8].is_list,
        "independent native Items fixture must use first key in header and subsequent key-row pairs");
    expect(native_links.items[1].items.size() == 12 && native_links.items[1].items[3].atom == "1" &&
               native_links.items[1].items[4].items.size() == 5 && native_links.items[1].items[4].items[1].items.size() == 11 &&
               native_links.items[1].items[4].items[2].atom == "1" && native_links.items[1].items[4].items[3].atom == "2" &&
               native_links.items[1].items[6].items[2].atom == "3" && native_links.items[1].items[6].items[3].atom == "2",
        "independent native Links fixture must keep endpoints and Distance outside each 11-field row");

    auto normalize_cache_profile = [](list_stream::ListValue& wrapper,
                                      const list_stream::ListValue& default_cache) {
        auto& sequence = wrapper.items[1];
        const std::size_t row_count = static_cast<std::size_t>(std::stoul(sequence.items[2].atom)) - 1;
        for (std::size_t index = 0; index < row_count; ++index) {
            auto& row = sequence.items[4 + index * 2].items[1];
            row.items[9] = list_stream::ListValue::raw_atom("0");
        }
        sequence.items[sequence.items.size() - 3] = default_cache;
    };
    const auto item_default_cache = record->items[2].items[2].items[1].items[5];
    const auto link_default_cache = record->items[2].items[3].items[1].items[5];
    normalize_cache_profile(native_items, item_default_cache);
    normalize_cache_profile(native_links, link_default_cache);
    record->items[2].items[2] = std::move(native_items);
    record->items[2].items[3] = std::move(native_links);

    const auto decoded = form_stream::decode_document(stream, "NativeGraphFixture");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    if (!decoded.ok()) return;
    const auto* dendrogram = decoded.value().find_control(model::ObjectId{2});
    const auto* graph = dendrogram == nullptr ? nullptr : std::get_if<model::DendrogramPayload>(&dendrogram->payload);
    expect(graph != nullptr && graph->items.size() == 3 && graph->links.size() == 2,
        "native collection cursor must decode three items and two links");
    if (graph == nullptr) return;
    expect(graph->items[0].value == "node-C" && graph->items[1].value == "node-A" && graph->items[2].value == "node-B" &&
               graph->links[0].first_item == "node-C" && graph->links[0].second_item == "node-A" &&
               graph->links[0].distance.canonical == "0" && graph->links[1].first_item == "node-B" &&
               graph->links[1].second_item == "node-A" && graph->links[1].distance.canonical == "0",
        "native keys and outer link endpoint/distance fields must map to named graph semantics");

    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded native graph must encode through the named writer");
    if (reencoded.ok()) {
        list_stream::ListValue* reencoded_record = nullptr;
        std::function<void(list_stream::ListValue&)> find_reencoded = [&](list_stream::ListValue& value) {
            if (!reencoded_record && value.is_list && value.items.size() == 6 && !value.items[0].is_list &&
                value.items[0].atom == oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid) {
                reencoded_record = &value;
                return;
            }
            for (auto& child : value.items) if (!reencoded_record && child.is_list) find_reencoded(child);
        };
        auto reencoded_stream = reencoded.value();
        find_reencoded(reencoded_stream);
        expect(reencoded_record != nullptr &&
                   list_stream::dump_compact(reencoded_record->items[2].items[2]) == list_stream::dump_compact(record->items[2].items[2]) &&
                   list_stream::dump_compact(reencoded_record->items[2].items[3]) == list_stream::dump_compact(record->items[2].items[3]),
            "writer must reproduce the normalized independent native Items and Links cursor structures");
    }

    // Exact collection slices from strict run13, whose Form.bin SHA-256 is
    // d6bc32f901213b881c13a955ed7d969d1b574854873cea8491bec38cd2bcfbdb.
    // Designer persisted physical rows in descending key order while each
    // sentinel and next-key chain retained the logical source order.
    auto strict_native_stream = encoded.value();
    record = nullptr;
    find_record(strict_native_stream);
    expect(record != nullptr, "run13 fixture carrier must locate the Dendrogram record");
    if (!record) return;
    auto strict_items = list_stream::parse(R"NATIVE({0,{3,0,4,3,{0,{8,3,0,0,0,0,{"S","xml-B"},{1,1,{"ru","Из XML B"}},{"U"},0,0}},2,{0,{8,2,0,0,3,0,{"S","xml-A"},{1,1,{"ru","Из XML A"}},{"U"},0,0}},1,{0,{8,1,0,0,2,0,{"S","xml-C"},{1,1,{"ru","Из XML C"}},{"U"},0,0}},0,{0,{8,0,0,1,0,3,{"U"},{1,0},{"U"},0,4294901761}},{0,1,{0,{4,0,{0},0},{4,0,{0},0}}},1,0}})NATIVE");
    auto strict_links = list_stream::parse(R"NATIVE({0,{3,0,3,2,{0,{8,2,0,0,0,0,{"U"},{1,1,{"ru","Связь A-B из XML"}},{"U"},0,0},2,3,0},1,{0,{8,1,0,0,2,0,{"U"},{1,1,{"ru","Связь C-B из XML"}},{"U"},0,0},1,3,1.5},0,{0,{8,0,0,1,0,2,{"U"},{1,0},{"U"},0,4294901761},0,0,0},{0,1,{0,{4,0,{0},0},{4,0,{0},0}}},1,0}})NATIVE");
    expect(strict_items.items[1].items[3].atom == "3" && strict_items.items[1].items[5].atom == "2" &&
               strict_items.items[1].items[7].atom == "1" && strict_links.items[1].items[3].atom == "2" &&
               strict_links.items[1].items[5].atom == "1",
        "run13 fixture must retain the actual strict Designer physical key order");
    record->items[2].items[2] = std::move(strict_items);
    record->items[2].items[3] = std::move(strict_links);
    const auto strict_native_decoded = form_stream::decode_document(strict_native_stream, "NativeGraphFixture");
    expect(strict_native_decoded.ok(), strict_native_decoded ? "" : strict_native_decoded.diagnostics().front().path + ": " + strict_native_decoded.diagnostics().front().message);
    if (!strict_native_decoded.ok()) return;
    const auto* strict_native_control = strict_native_decoded.value().find_control(model::ObjectId{2});
    const auto* strict_graph = strict_native_control == nullptr ? nullptr : std::get_if<model::DendrogramPayload>(&strict_native_control->payload);
    expect(strict_graph != nullptr && strict_graph->items.size() == 3 && strict_graph->links.size() == 2 &&
               strict_graph->items[0].value == "xml-C" && strict_graph->items[1].value == "xml-A" && strict_graph->items[2].value == "xml-B" &&
               strict_graph->links[0].first_item == "xml-C" && strict_graph->links[0].second_item == "xml-B" &&
               strict_graph->links[0].distance.canonical == "1.5" && strict_graph->links[1].first_item == "xml-A" &&
               strict_graph->links[1].second_item == "xml-B" && strict_graph->links[1].distance.canonical == "0",
        "strict run13 physical rows must decode in sentinel/next-key logical order with endpoints resolved by key");
    if (!strict_graph) return;
    const auto strict_public_xml = oof::source::serialize_form_xml(strict_native_decoded.value());
    expect(strict_public_xml.ok() && strict_public_xml.value().find("<Value>xml-C</Value>") < strict_public_xml.value().find("<Value>xml-A</Value>") &&
               strict_public_xml.value().find("<FirstItem>xml-C</FirstItem>") != std::string::npos,
        "strict run13 native data must become logical named public XML");
    if (!strict_public_xml.ok()) return;
    const auto strict_reparsed = oof::source::parse_form_xml(strict_public_xml.value());
    expect(strict_reparsed.ok(), "public XML from strict run13 must parse for fresh encoding");
    if (!strict_reparsed.ok()) return;
    auto strict_fresh_stream = form_stream::encode_document(strict_reparsed.value());
    expect(strict_fresh_stream.ok(), "public XML from strict run13 must fresh-encode");
    if (!strict_fresh_stream.ok()) return;
    const auto strict_fresh_decoded = form_stream::decode_document(strict_fresh_stream.value(), "NativeGraphFixture");
    expect(strict_fresh_decoded.ok(), "freshly encoded strict run13 graph must decode");
    if (!strict_fresh_decoded.ok()) return;
    const auto* fresh_control = strict_fresh_decoded.value().find_control(model::ObjectId{2});
    const auto* fresh_graph = fresh_control == nullptr ? nullptr : std::get_if<model::DendrogramPayload>(&fresh_control->payload);
    expect(fresh_graph != nullptr && fresh_graph->items.size() == strict_graph->items.size() &&
               fresh_graph->links.size() == strict_graph->links.size() && fresh_graph->items[0].value == "xml-C" &&
               fresh_graph->links[0].first_item == "xml-C" && fresh_graph->links[0].second_item == "xml-B" &&
               fresh_graph->links[0].distance.canonical == "1.5" && fresh_graph->links[1].first_item == "xml-A" &&
               fresh_graph->links[1].second_item == "xml-B" && fresh_graph->links[1].distance.canonical == "0",
        "strict native to named XML to fresh Form.bin must preserve graph semantics");

    auto duplicate_key = strict_native_stream;
    record = nullptr;
    find_record(duplicate_key);
    auto& duplicate_items = record->items[2].items[2].items[1];
    duplicate_items.items[5] = list_stream::ListValue::raw_atom("3");
    duplicate_items.items[6].items[1].items[1] = list_stream::ListValue::raw_atom("3");
    expect(!form_stream::decode_document(duplicate_key, "NativeGraphFixture"),
        "duplicate native physical item keys must fail closed");

    auto missing_key = strict_native_stream;
    record = nullptr;
    find_record(missing_key);
    auto& missing_items = record->items[2].items[2].items[1];
    missing_items.items[5] = list_stream::ListValue::raw_atom("4");
    missing_items.items[6].items[1].items[1] = list_stream::ListValue::raw_atom("4");
    expect(!form_stream::decode_document(missing_key, "NativeGraphFixture"),
        "out-of-range physical item key leaving a missing key must fail closed");

    auto cyclic_chain = strict_native_stream;
    record = nullptr;
    find_record(cyclic_chain);
    record->items[2].items[2].items[1].items[4].items[1].items[4] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(cyclic_chain, "NativeGraphFixture"),
        "cycle in native item next-key chain must fail closed");

    auto bad_sentinel_marker = strict_native_stream;
    record = nullptr;
    find_record(bad_sentinel_marker);
    auto& marker_items = record->items[2].items[2].items[1];
    marker_items.items[marker_items.items.size() - 4].items[0] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(bad_sentinel_marker, "NativeGraphFixture"),
        "nonzero Items sentinel wrapper marker must fail closed");

    auto bad_tail = strict_native_stream;
    record = nullptr;
    find_record(bad_tail);
    auto& tail_items = record->items[2].items[2].items[1];
    tail_items.items[tail_items.items.size() - 4].items[1].items[5] = list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(bad_tail, "NativeGraphFixture"),
        "native sentinel tail key mismatch must fail closed");

    auto bad_head = strict_native_stream;
    record = nullptr;
    find_record(bad_head);
    auto& head_items = record->items[2].items[2].items[1];
    head_items.items[head_items.items.size() - 4].items[1].items[3] = list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(bad_head, "NativeGraphFixture"),
        "native sentinel head key mismatch must fail closed");

    auto missing_chain_key = strict_native_stream;
    record = nullptr;
    find_record(missing_chain_key);
    auto& broken_chain_items = record->items[2].items[2].items[1];
    broken_chain_items.items[8].items[1].items[4] = list_stream::ListValue::raw_atom("0");
    expect(!form_stream::decode_document(missing_chain_key, "NativeGraphFixture"),
        "native chain that skips stored item rows must fail closed");

    auto malformed_value_tag = strict_native_stream;
    record = nullptr;
    find_record(malformed_value_tag);
    auto& value_row = record->items[2].items[2].items[1].items[4].items[1];
    value_row.items[6].items[0] = list_stream::ListValue::raw_atom("S");
    expect(!form_stream::decode_document(malformed_value_tag, "NativeGraphFixture"),
        "raw atom in typed Dendrogram string Value must fail closed");

    auto unresolved_native_link = strict_native_stream;
    record = nullptr;
    find_record(unresolved_native_link);
    record->items[2].items[3].items[1].items[6].items[3] = list_stream::ListValue::raw_atom("9");
    expect(!form_stream::decode_document(unresolved_native_link, "NativeGraphFixture"),
        "native link endpoint key absent from the item key map must fail closed");

    auto wrong_external_key = stream;
    record = nullptr;
    find_record(wrong_external_key);
    record->items[2].items[2].items[1].items[5] = list_stream::ListValue::raw_atom("7");
    expect_failure(form_stream::decode_document(wrong_external_key, "NativeGraphFixture"), "OOF1114",
        "$/1/2/2/1/2/2/6", "nonascending external Dendrogram item key must fail closed");

    auto wrong_link_key = stream;
    record = nullptr;
    find_record(wrong_link_key);
    record->items[2].items[3].items[1].items[5] = list_stream::ListValue::raw_atom("7");
    expect_failure(form_stream::decode_document(wrong_link_key, "NativeGraphFixture"), "OOF1114",
        "$/1/2/2/1/2/3/6", "nonascending external Dendrogram link key must fail closed");

    constexpr std::string_view two_one_xml =
        R"OOF(<Form id="1" name="TwoOneGraph" ordinaryFormVersion="2.1"><ChildItems><Dendrogram id="2" name="Tree"><Position/><Items><Item><Value>A</Value><Text><Item language="ru">А</Item></Text></Item><Item><Value>B</Value><Text><Item language="ru">Б</Item></Text></Item></Items><Links><Link><FirstItem>A</FirstItem><SecondItem>B</SecondItem><Title><Item language="ru">A-B</Item></Title></Link></Links></Dendrogram></ChildItems></Form>)OOF";
    const auto two_one_document = oof::source::parse_form_xml(two_one_xml);
    expect(two_one_document.ok(), "two-item one-link graph must parse");
    if (!two_one_document.ok()) return;
    auto two_one_stream = form_stream::encode_document(two_one_document.value());
    expect(two_one_stream.ok(), "two-item one-link graph must encode");
    if (!two_one_stream.ok()) return;
    record = nullptr;
    list_stream::ListValue* two_one_record = nullptr;
    find_record(two_one_stream.value());
    two_one_record = record;
    expect(two_one_record != nullptr, "two-item one-link record must be present");
    if (!two_one_record) return;
    const auto& two_items = two_one_record->items[2].items[2].items[1];
    const auto& one_link = two_one_record->items[2].items[3].items[1];
    const auto item_sentinel_index = two_items.items.size() - 5;
    const auto link_sentinel_index = one_link.items.size() - 5;
    expect(two_items.items[item_sentinel_index + 1].items[1].items[5].atom == "2" &&
               one_link.items[link_sentinel_index + 1].items[1].items[5].atom == "1",
        "nonempty sentinels must reference the last allocated key for their own collection size");
    expect(form_stream::decode_document(two_one_stream.value(), "TwoOneGraph").ok(),
        "two-item one-link candidate must decode with cardinality-specific sentinels");
}

void test_dendrogram_unbounded_branching_graph_round_trip() {
    constexpr std::string_view xml =
        R"OOF(<Form id="1" name="BranchingGraph" ordinaryFormVersion="2.1"><ChildItems><Dendrogram id="2" name="Tree"><Position/><Items><Item><Value>node-A</Value><Text><Item language="ru">Узел А</Item></Text></Item><Item><Value>node-B</Value><Text><Item language="ru">Узел Б</Item></Text></Item><Item><Value>node-C</Value><Text><Item language="ru">Узел В</Item></Text></Item><Item><Value>node-D</Value><Text><Item language="ru">Узел Г</Item></Text></Item><Item><Value>node-E</Value><Text><Item language="ru">Узел Д</Item></Text></Item></Items><Links><Link><FirstItem>node-A</FirstItem><SecondItem>node-C</SecondItem><Title><Item language="ru">А-В</Item></Title><Distance>1.25</Distance></Link><Link><FirstItem>node-A</FirstItem><SecondItem>node-B</SecondItem><Title><Item language="ru">А-Б</Item></Title></Link><Link><FirstItem>node-B</FirstItem><SecondItem>node-E</SecondItem><Title><Item language="ru">Б-Д</Item></Title><Distance>2</Distance></Link><Link><FirstItem>node-B</FirstItem><SecondItem>node-D</SecondItem><Title><Item language="ru">Б-Г</Item></Title><Distance>3.5</Distance></Link></Links></Dendrogram></ChildItems></Form>)OOF";
    const auto parsed = oof::source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    if (!parsed.ok()) return;
    const auto* source_control = parsed.value().find_control(model::ObjectId{2});
    const auto* source_graph = source_control == nullptr ? nullptr : std::get_if<model::DendrogramPayload>(&source_control->payload);
    const std::vector<model::DendrogramItem> expected_items{
        {"node-A", {{{"ru", "Узел А"}}}}, {"node-B", {{{"ru", "Узел Б"}}}},
        {"node-C", {{{"ru", "Узел В"}}}}, {"node-D", {{{"ru", "Узел Г"}}}},
        {"node-E", {{{"ru", "Узел Д"}}}},
    };
    const std::vector<model::DendrogramLink> expected_links{
        {"node-A", "node-C", {{{"ru", "А-В"}}}, {"1.25"}},
        {"node-A", "node-B", {{{"ru", "А-Б"}}}, {"0"}},
        {"node-B", "node-E", {{{"ru", "Б-Д"}}}, {"2"}},
        {"node-B", "node-D", {{{"ru", "Б-Г"}}}, {"3.5"}},
    };
    expect(source_graph != nullptr && source_graph->items == expected_items && source_graph->links == expected_links,
        "named XML parser must retain all five values/texts and four branching links");

    const auto public_xml = oof::source::serialize_form_xml(parsed.value());
    expect(public_xml.ok(), "five-item four-link graph must serialize to named XML");
    if (!public_xml.ok()) return;
    const auto reparsed = oof::source::parse_form_xml(public_xml.value());
    expect(reparsed.ok(), "unbounded named graph XML must parse after source serialization");
    if (!reparsed.ok()) return;
    auto encoded = form_stream::encode_document(reparsed.value());
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().message);
    if (!encoded.ok()) return;

    auto find_record = [](list_stream::ListValue& root) -> list_stream::ListValue* {
        list_stream::ListValue* found = nullptr;
        std::function<void(list_stream::ListValue&)> visit = [&](list_stream::ListValue& value) {
            if (!found && value.is_list && value.items.size() == 6 && !value.items[0].is_list &&
                value.items[0].atom == oof::model::metamodel::descriptor_for(model::ControlKind::dendrogram).guid) {
                found = &value;
                return;
            }
            for (auto& child : value.items) if (!found && child.is_list) visit(child);
        };
        visit(root);
        return found;
    };
    auto physical_stream = encoded.value();
    auto* physical_record = find_record(physical_stream);
    expect(physical_record != nullptr, "encoded branching graph must have a Dendrogram control record");
    if (!physical_record) return;

    const auto reorder_collection = [](list_stream::ListValue& wrapper, const std::vector<std::uint32_t>& key_order) {
        auto& sequence = wrapper.items.at(1);
        const auto row_count = static_cast<std::size_t>(std::stoul(sequence.items.at(2).atom)) - 1;
        expect(key_order.size() == row_count, "physical key permutation must cover every graph row");
        std::map<std::uint32_t, std::pair<list_stream::ListValue, list_stream::ListValue>> rows_by_key;
        for (std::size_t index = 0; index < row_count; ++index) {
            const auto key = static_cast<std::uint32_t>(std::stoul(sequence.items.at(3 + index * 2).atom));
            rows_by_key.emplace(key, std::make_pair(sequence.items.at(3 + index * 2), sequence.items.at(4 + index * 2)));
        }
        std::vector<list_stream::ListValue> fields(sequence.items.begin(), sequence.items.begin() + 3);
        for (const auto key : key_order) {
            const auto row = rows_by_key.find(key);
            expect(row != rows_by_key.end(), "physical key permutation must reference an existing row");
            fields.push_back(row->second.first);
            fields.push_back(row->second.second);
        }
        const auto suffix = sequence.items.size() - 5;
        fields.insert(fields.end(), sequence.items.begin() + static_cast<std::ptrdiff_t>(suffix), sequence.items.end());
        sequence = list_stream::ListValue::list(std::move(fields));
    };
    reorder_collection(physical_record->items[2].items[2], {3, 1, 5, 2, 4});
    reorder_collection(physical_record->items[2].items[3], {3, 1, 4, 2});

    auto oversized_count = physical_stream;
    auto* oversized_record = find_record(oversized_count);
    oversized_record->items[2].items[2].items[1].items[2] = list_stream::ListValue::raw_atom("4294967295");
    expect(!form_stream::decode_document(oversized_count, "BranchingGraph"),
        "oversized declared collection count must fail before indexing or allocation");

    auto exponent_distance = physical_stream;
    auto* exponent_record = find_record(exponent_distance);
    exponent_record->items[2].items[3].items[1].items[4].items[4] = list_stream::ListValue::raw_atom("1e2");
    expect_failure(form_stream::decode_document(exponent_distance, "BranchingGraph"), "OOF1122",
        "$/1/2/2/1/2/3/8", "native exponent Distance must be rejected because public xs:decimal cannot serialize it");

    const auto decoded = form_stream::decode_document(physical_stream, "BranchingGraph");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    if (!decoded.ok()) return;
    const auto* decoded_control = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_graph = decoded_control == nullptr ? nullptr : std::get_if<model::DendrogramPayload>(&decoded_control->payload);
    expect(decoded_graph != nullptr && decoded_graph->items == expected_items && decoded_graph->links == expected_links,
        "decoder must reconstruct named branching semantics from shuffled physical keys and chains");

    const auto decoded_xml = oof::source::serialize_form_xml(decoded.value());
    expect(decoded_xml.ok(), "shuffled physical graph must serialize to named public XML");
    if (!decoded_xml.ok()) return;
    const auto final_document = oof::source::parse_form_xml(decoded_xml.value());
    expect(final_document.ok(), "named graph reconstructed from physical rows must parse");
    if (!final_document.ok()) return;
    const auto final_stream = form_stream::encode_document(final_document.value());
    expect(final_stream.ok(), "named graph reconstructed from physical rows must re-encode");
    if (!final_stream.ok()) return;
    const auto final_decoded = form_stream::decode_document(final_stream.value(), "BranchingGraph");
    expect(final_decoded.ok(), "freshly encoded branching graph must decode");
    if (!final_decoded.ok()) return;
    const auto* final_control = final_decoded.value().find_control(model::ObjectId{2});
    const auto* final_graph = final_control == nullptr ? nullptr : std::get_if<model::DendrogramPayload>(&final_control->payload);
    expect(final_graph != nullptr && final_graph->items == expected_items && final_graph->links == expected_links,
        "shuffled native-shaped stream to named XML to fresh stream must preserve every item, label, endpoint and distance");
}


void test_track_bar_observed_record_and_named_round_trip() {
    constexpr std::string_view default_form_xml =
        R"OOF(<Form id="1" name="TrackBarForm" ordinaryFormVersion="2.1"><ChildItems><TrackBar id="4" name="TrackBarResearch"><Position/></TrackBar></ChildItems></Form>)OOF";
    const auto parsed = oof::source::parse_form_xml(default_form_xml);
    expect(parsed.ok(), "TrackBar default named XML must parse");
    auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), "TrackBar default named XML must encode");

    constexpr std::string_view runtime_record =
        R"OOF({6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef,2,{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},5,0,100,1,10,2,2,5,100},{0}},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,0,1,0,0},{14,"TrackBarProbe",4294967295,0,0,0},{0}})OOF";
    auto observed = list_stream::parse(runtime_record);
    observed.items[1] = list_stream::ListValue::raw_atom("4");
    observed.items[4].items[1] = list_stream::ListValue::string_atom("TrackBarResearch");
    const auto find_track_bar = [&](auto&& self, list_stream::ListValue& value) -> list_stream::ListValue* {
        if (!value.is_list) return nullptr;
        if (value.items.size() == 6 && !value.items[0].is_list &&
            value.items[0].atom == "6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef") return &value;
        for (auto& item : value.items) if (auto* found = self(self, item)) return found;
        return nullptr;
    };
    auto* default_record = find_track_bar(find_track_bar, encoded.value());
    expect(default_record != nullptr, "TrackBar writer must emit a named control record");
    observed.items[3] = default_record->items[3];
    *default_record = observed;

    const auto decoded = form_stream::decode_document(encoded.value(), "TrackBarForm");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* track_bar = decoded.value().find_control(model::ObjectId{4});
    expect(track_bar && track_bar->kind() == model::ControlKind::track_bar &&
               track_bar->name == "TrackBarResearch" &&
               track_bar->properties().find(model::PropertyId::from_name("Enabled")) == nullptr &&
               track_bar->properties().find(model::PropertyId::from_name("MaxValue")) == nullptr &&
               track_bar->properties().find(model::PropertyId::from_name("MinValue")) == nullptr &&
               track_bar->properties().find(model::PropertyId::from_name("Step")) == nullptr,
        "fresh TrackBar Add record must decode to the named control with omitted defaults");
    const auto default_reencoded = form_stream::encode_document(decoded.value());
    expect(default_reencoded.ok() &&
               list_stream::dump_compact(default_reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "fresh TrackBar default record must re-encode without drift");
    const auto default_serialized_xml = oof::source::serialize_form_xml(decoded.value());
    expect(default_serialized_xml.ok() && default_serialized_xml.value().find("<MaxValue>") == std::string::npos &&
               default_serialized_xml.value().find("<MinValue>") == std::string::npos &&
               default_serialized_xml.value().find("<Step>") == std::string::npos,
        "TrackBar XML writer must omit explicit properties equal to descriptor defaults");
    constexpr std::string_view explicit_defaults_xml =
        R"OOF(<Form id="1" name="TrackBarForm" ordinaryFormVersion="2.1"><ChildItems><TrackBar id="4" name="TrackBarResearch"><Position/><MaxValue>100</MaxValue><MinValue>0</MinValue><Step>1</Step></TrackBar></ChildItems></Form>)OOF";
    const auto explicit_defaults = oof::source::parse_form_xml(explicit_defaults_xml);
    expect(explicit_defaults.ok(), "explicit TrackBar defaults must parse");
    const auto explicit_defaults_serialized = oof::source::serialize_form_xml(explicit_defaults.value());
    expect(explicit_defaults.ok() && explicit_defaults_serialized.ok() &&
               explicit_defaults_serialized.value().find("<MaxValue>") == std::string::npos &&
               explicit_defaults_serialized.value().find("<MinValue>") == std::string::npos &&
               explicit_defaults_serialized.value().find("<Step>") == std::string::npos,
        "TrackBar XML writer must omit explicitly authored numeric values equal to defaults");

    constexpr std::string_view numeric_xml =
        R"OOF(<Form id="1" name="TrackBarForm" ordinaryFormVersion="2.1"><ChildItems><TrackBar id="4" name="TrackBarResearch"><Position><Top>12</Top><Visible>false</Visible><Height>45</Height><Left>23</Left><Width>234</Width></Position><Enabled>false</Enabled><MaxValue>321</MaxValue><MinValue>17</MinValue><ToolTip>TrackBar runtime probe</ToolTip><Step>25</Step></TrackBar></ChildItems></Form>)OOF";
    const auto numeric_parsed = oof::source::parse_form_xml(numeric_xml);
    expect(numeric_parsed.ok(), numeric_parsed ? "" : numeric_parsed.diagnostics().front().path + ": " +
        numeric_parsed.diagnostics().front().message);
    auto numeric_encoded = form_stream::encode_document(numeric_parsed.value());
    expect(numeric_encoded.ok(), "named TrackBar numeric and base properties must encode");
    auto numeric_observed = observed;
    auto numeric_stream = numeric_encoded.value();
    auto* numeric_record = find_track_bar(find_track_bar, numeric_stream);
    expect(numeric_record != nullptr, "TrackBar numeric writer must emit a named control record");
    numeric_observed.items[3] = numeric_record->items[3];
    numeric_observed.items[2].items[1].items[0].items[1] = list_stream::ListValue::raw_atom("0");
    numeric_observed.items[2].items[1].items[0].items[12] =
        list_stream::parse("{1,1,{\"ru\",\"TrackBar runtime probe\"}}");
    numeric_observed.items[2].items[1].items[2] = list_stream::ListValue::raw_atom("17");
    numeric_observed.items[2].items[1].items[3] = list_stream::ListValue::raw_atom("321");
    numeric_observed.items[2].items[1].items[4] = list_stream::ListValue::raw_atom("25");
    *numeric_record = numeric_observed;
    const auto numeric_decoded = form_stream::decode_document(numeric_stream, "TrackBarNumeric");
    expect(numeric_decoded.ok(), "observed TrackBar MaxValue, MinValue, Step and base changes must decode");
    const auto* numeric_control = numeric_decoded.value().find_control(model::ObjectId{4});
    const auto* max_value = numeric_control->properties().find(model::PropertyId::from_name("MaxValue"));
    const auto* min_value = numeric_control->properties().find(model::PropertyId::from_name("MinValue"));
    const auto* step = numeric_control->properties().find(model::PropertyId::from_name("Step"));
    const auto* enabled = numeric_control->properties().find(model::PropertyId::from_name("Enabled"));
    const auto* tool_tip = numeric_control->properties().find(model::PropertyId::from_name("ToolTip"));
    expect(max_value && std::get<std::int64_t>(max_value->value) == 321 &&
               min_value && std::get<std::int64_t>(min_value->value) == 17 &&
               step && std::get<std::int64_t>(step->value) == 25 &&
               enabled && !std::get<bool>(enabled->value) &&
               tool_tip && std::get<std::string>(tool_tip->value) == "TrackBar runtime probe" &&
               numeric_control->position.top.value() == 12 && numeric_control->position.left.value() == 23 &&
               numeric_control->position.width.value() == 234 && numeric_control->position.height.value() == 45 &&
               !numeric_control->position.visible.value(),
        "TrackBar named properties and Position including Visible must decode from the observed record");
    const auto numeric_reencoded = form_stream::encode_document(numeric_decoded.value());
    expect(numeric_reencoded.ok() &&
               list_stream::dump_compact(numeric_reencoded.value()) == list_stream::dump_compact(numeric_encoded.value()),
        "TrackBar named properties must re-encode to the observed info and position slots");

    constexpr std::string_view inverted_range_xml =
        R"OOF(<Form id="1" name="TrackBarForm" ordinaryFormVersion="2.1"><ChildItems><TrackBar id="4" name="P"><Position/><MaxValue>16</MaxValue><MinValue>17</MinValue></TrackBar></ChildItems></Form>)OOF";
    const auto inverted_range = oof::source::parse_form_xml(inverted_range_xml);
    expect(inverted_range.ok() && form_stream::encode_document(inverted_range.value()),
        "TrackBar MaxValue below MinValue must be accepted as observed at runtime");

    constexpr std::string_view int32_max_xml =
        R"OOF(<Form id="1" name="TrackBarForm" ordinaryFormVersion="2.1"><ChildItems><TrackBar id="4" name="P"><Position/><MaxValue>2147483647</MaxValue></TrackBar></ChildItems></Form>)OOF";
    const auto int32_max_input = oof::source::parse_form_xml(int32_max_xml);
    expect(int32_max_input.ok(), "TrackBar int32 maximum input must parse");
    const auto int32_max_stream = form_stream::encode_document(int32_max_input.value());
    expect(int32_max_stream.ok(), "TrackBar int32 maximum must remain serializable independent of GUI runtime limits");
    const auto int32_max_decoded = form_stream::decode_document(int32_max_stream.value(), "TrackBarInt32Max");
    const auto* int32_max_control = int32_max_decoded ? int32_max_decoded.value().find_control(model::ObjectId{4}) : nullptr;
    const auto* int32_max_value = int32_max_control ?
        int32_max_control->properties().find(model::PropertyId::from_name("MaxValue")) : nullptr;
    expect(int32_max_value && std::get<std::int64_t>(int32_max_value->value) == 2147483647,
        "TrackBar int32 maximum must decode exactly without claiming GUI runtime acceptance");

    for (const std::string_view value : {"2147483648", "1.5"}) {
        const std::string invalid_xml =
            "<Form id=\"1\" name=\"TrackBarForm\" ordinaryFormVersion=\"2.1\"><ChildItems><TrackBar id=\"4\" name=\"P\"><Position/><MaxValue>" +
            std::string(value) + "</MaxValue></TrackBar></ChildItems></Form>";
        const auto invalid_input = oof::source::parse_form_xml(invalid_xml);
        expect(!invalid_input || !form_stream::encode_document(invalid_input.value()),
            "TrackBar int32 overflow and fractional values must fail parsing or serialization");
    }

    for (const auto [property, value] : {std::pair<std::string_view, std::string_view>{"MaxValue", "-1"},
             {"MinValue", "-1"}, {"Step", "0"}, {"Step", "-1"}}) {
        const std::string rejected_xml =
            "<Form id=\"1\" name=\"TrackBarForm\" ordinaryFormVersion=\"2.1\"><ChildItems><TrackBar id=\"4\" name=\"P\"><Position/><" +
            std::string(property) + ">" + std::string(value) + "</" + std::string(property) +
            "></TrackBar></ChildItems></Form>";
        const auto rejected = oof::source::parse_form_xml(rejected_xml);
        expect(rejected.ok(), "TrackBar integer property inputs must parse before runtime-evidence validation");
        const auto rejected_encoding = form_stream::encode_document(rejected.value());
        const std::string expected_path = property == "MaxValue" ? "$/TrackBar/MaxValue" :
            property == "MinValue" ? "$/TrackBar/MinValue" : "$/TrackBar/Step";
        expect_failure(rejected_encoding, "OOF1122", expected_path,
            "TrackBar MinValue below zero and Step at or below zero must fail closed");
    }

    auto unsupported = encoded.value();
    auto* unsupported_record = find_track_bar(find_track_bar, unsupported);
    unsupported_record->items[2].items[1].items[5] = list_stream::ListValue::raw_atom("11");
    const auto unsupported_decoded = form_stream::decode_document(unsupported, "TrackBarUnsupported");
    const auto* partial_track_bar = unsupported_decoded ?
        unsupported_decoded.value().find_control(model::ObjectId{4}) : nullptr;
    expect(unsupported_decoded.ok() && !unsupported_decoded.value().reconstruction_complete() &&
               std::any_of(unsupported_decoded.diagnostics().begin(), unsupported_decoded.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_track_bar != nullptr && partial_track_bar->name == "TrackBarResearch",
        "valid unknown TrackBar marking/detail profile value must warn and preserve its name");

    for (const auto [path_slot, value, path] : {
             std::tuple<std::size_t, std::string_view, std::string_view>{2, "-1", "$/1/2/2/1/2/1/2"},
             {3, "-1", "$/1/2/2/1/2/1/3"}, {4, "0", "$/1/2/2/1/2/1/4"},
             {4, "-1", "$/1/2/2/1/2/1/4"}}) {
        auto invalid = encoded.value();
        auto* invalid_record = find_track_bar(find_track_bar, invalid);
        invalid_record->items[2].items[1].items[path_slot] = list_stream::ListValue::raw_atom(std::string(value));
        const auto invalid_decoded = form_stream::decode_document(invalid, "TrackBarInvalidNumeric");
        expect_failure(invalid_decoded, "OOF1122", path,
            "observed TrackBar numeric constraints must reject unsupported stored values");
    }

    constexpr std::string_view unsupported_property_xml =
        R"OOF(<Form id="1" name="TrackBarForm" ordinaryFormVersion="2.1"><ChildItems><TrackBar id="4" name="P"><Position/><LargeStep>5</LargeStep></TrackBar></ChildItems></Form>)OOF";
    const auto unsupported_property = oof::source::parse_form_xml(unsupported_property_xml);
    expect(unsupported_property.ok() && !form_stream::encode_document(unsupported_property.value()),
        "TrackBar LargeStep remains unsupported until its storage slot is observed");

    constexpr std::string_view data_path_xml =
        R"OOF(<Form id="1" name="TrackBarForm" ordinaryFormVersion="2.1"><Attributes><Attribute id="3" name="Amount"><TypeDomain><Entry term="numeric" length="10" precision="2"/></TypeDomain></Attribute></Attributes><ChildItems><TrackBar id="4" name="P"><DataPath attributeId="3"/><Position/></TrackBar></ChildItems></Form>)OOF";
    const auto data_path = oof::source::parse_form_xml(data_path_xml);
    expect(data_path.ok() && !form_stream::encode_document(data_path.value()),
        "unobserved TrackBar DataPath must stay unsupported by the primary codec");

    constexpr std::string_view mixed_xml =
        R"OOF(<Form id="1" name="TrackBarMixed" ordinaryFormVersion="2.1"><ChildItems><Button id="2" name="Run"><Position/><Caption>Run</Caption></Button><TrackBar id="4" name="Range"><Position/><MinValue>17</MinValue></TrackBar></ChildItems></Form>)OOF";
    const auto mixed = oof::source::parse_form_xml(mixed_xml);
    expect(mixed.ok(), "mixed Button and TrackBar XML must parse");
    const auto mixed_encoded = form_stream::encode_document(mixed.value());
    expect(mixed_encoded.ok(), "TrackBar must encode beside an existing Button control");
    const auto mixed_decoded = form_stream::decode_document(mixed_encoded.value(), "TrackBarMixed");
    expect(mixed_decoded.ok() && mixed_decoded.value().find_control(model::ObjectId{2})->kind() == model::ControlKind::button &&
               mixed_decoded.value().find_control(model::ObjectId{4})->kind() == model::ControlKind::track_bar,
        "mixed Button and TrackBar records must decode in named control order");
}

void test_list_box_value_list_data_path_and_supported_properties() {
    constexpr std::string_view xml = R"OOF(<Form id="1" name="ListBoxForm" ordinaryFormVersion="2.1"><Attributes><Attribute id="5" name="Values"><TypeDomain><Entry term="valueList"/></TypeDomain></Attribute></Attributes><ChildItems><ListBox id="7" name="Choices"><DataPath attributeId="5"/><Position><Top>12</Top><Visible>false</Visible><Height>45</Height><Left>23</Left><Width>234</Width></Position><Enabled>false</Enabled><ShowPicture>true</ShowPicture><ShowCheckBox>true</ShowCheckBox><ToolTip>Список проверки</ToolTip><ReadOnly>false</ReadOnly></ListBox></ChildItems></Form>)OOF";
    auto parsed = oof::source::parse_form_xml(xml);
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().path + ": " + parsed.diagnostics().front().message);
    const auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), "named ListBox with ValueList DataPath and proven properties must encode");
    const auto decoded = form_stream::decode_document(encoded.value(), "ListBoxRoundTrip");
    expect(decoded.ok(), "named ListBox with ValueList DataPath must decode");
    const auto* control = decoded.value().find_control(model::ObjectId{7});
    expect(control && control->kind() == model::ControlKind::list_box && control->data_path &&
               control->data_path->attribute.id() == model::ObjectId{5},
        "ListBox DataPath must resolve to the named ValueList attribute");
    const auto* attribute = decoded.value().find_attribute(model::ObjectId{5});
    expect(attribute && attribute->type.entries.size() == 1 &&
               attribute->type.entries.front().term == model::TypeDomainTerm::value_list,
        "ListBox source type must survive stream decoding as named ValueList");
    const auto property = [&](std::string_view name) { return control->properties().find(model::PropertyId::from_name(name)); };
    expect(property("Enabled") && !std::get<bool>(property("Enabled")->value) &&
               property("ShowPicture") && std::get<bool>(property("ShowPicture")->value) &&
               property("ShowCheckBox") && std::get<bool>(property("ShowCheckBox")->value) &&
               property("ToolTip") && std::get<std::string>(property("ToolTip")->value) == "Список проверки" &&
               property("ReadOnly") && !std::get<bool>(property("ReadOnly")->value),
        "ListBox supported base and display properties must decode by their named descriptors");
    expect(control->position.top.value() == 12 && control->position.left.value() == 23 &&
               control->position.width.value() == 234 && control->position.height.value() == 45 &&
               !control->position.visible.value(),
        "ListBox Position and Visible must survive stream decoding");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
               list_stream::dump_compact(encoded.value()),
        "ListBox ValueList link, properties, and geometry must round-trip exactly");

    const auto rejects = [](std::string_view source) {
        const auto candidate = oof::source::parse_form_xml(source);
        return !candidate || !form_stream::encode_document(candidate.value());
    };
    constexpr std::string_view wrong_type = R"OOF(<Form id="1" name="WrongType" ordinaryFormVersion="2.1"><Attributes><Attribute id="5" name="Values"><TypeDomain><Entry term="string"/></TypeDomain></Attribute></Attributes><ChildItems><ListBox id="7" name="Choices"><DataPath attributeId="5"/><Position/></ListBox></ChildItems></Form>)OOF";
    expect(rejects(wrong_type), "ListBox DataPath must reject a non-ValueList attribute");
    constexpr std::string_view dangling = R"OOF(<Form id="1" name="Dangling" ordinaryFormVersion="2.1"><Attributes><Attribute id="5" name="Values"><TypeDomain><Entry term="valueList"/></TypeDomain></Attribute></Attributes><ChildItems><ListBox id="7" name="Choices"><DataPath attributeId="9"/><Position/></ListBox></ChildItems></Form>)OOF";
    expect(rejects(dangling), "ListBox DataPath must reject a dangling Attribute reference");
    constexpr std::string_view nested = R"OOF(<Form id="1" name="Nested" ordinaryFormVersion="2.1"><Attributes><Attribute id="5" name="Values"><TypeDomain><Entry term="valueList"/></TypeDomain></Attribute></Attributes><ChildItems><ListBox id="7" name="Choices"><DataPath attributeId="5"><Member>Nested</Member></DataPath><Position/></ListBox></ChildItems></Form>)OOF";
    expect(rejects(nested), "ListBox DataPath must reject nested members outside the tested profile");
    constexpr std::string_view events = R"OOF(<Form id="1" name="Events" ordinaryFormVersion="2.1"><Attributes><Attribute id="5" name="Values"><TypeDomain><Entry term="valueList"/></TypeDomain></Attribute></Attributes><ChildItems><ListBox id="7" name="Choices"><DataPath attributeId="5"/><Position/><Events><OnActivateRow>HandleRow</OnActivateRow></Events></ListBox></ChildItems></Form>)OOF";
    expect(rejects(events), "ListBox event storage must remain rejected until its record is proven");
    constexpr std::string_view persisted_value = R"OOF(<Form id="1" name="PersistedValue" ordinaryFormVersion="2.1"><Attributes><Attribute id="5" name="Values"><TypeDomain><Entry term="valueList"/></TypeDomain></Attribute></Attributes><ChildItems><ListBox id="7" name="Choices"><DataPath attributeId="5"/><Position/><Value>alpha</Value></ListBox></ChildItems></Form>)OOF";
    expect(rejects(persisted_value), "runtime Value items must not enter persisted ListBox XML");
}

void test_list_box_captured_runtime_record_roundtrip() {
    constexpr std::string_view observed_record = R"CAPTURED(
{19f8b798-314e-4b4e-8121-905b2a7a03f5,201,{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,1,{-18},0,0,0},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},{23,100743712,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,0,{12741203},0},{4,3,{-15},3},{4,3,{-13},3},2,2,0,0,0,1,0,1,1,{8,2,0,{-20},1,100},{8,2,0,{-20},1,100},0,0,1,0,0,0,0,0,0,0,100,1,2,2,2,0,0,2},6,0,0,0,0},{0}},{8,0,0,0,0,0,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,4,5,0,0},{14,"ПолеСпискаТипизированнаяСвязь",4294967295,0,0,0},{0}}
)CAPTURED";
    const auto captured_control = list_stream::parse(observed_record);
    constexpr std::string_view linked_form_xml =
        R"OOF(<Form id="1" name="CapturedListBox" ordinaryFormVersion="2.1"><Attributes><Attribute id="200" name="ЗначенияСписка"><TypeDomain><Entry term="valueList"/></TypeDomain></Attribute></Attributes><ChildItems><Button id="100" name="Подготовка1"><Position/></Button><Button id="101" name="Подготовка2"><Position/></Button><Button id="102" name="Подготовка3"><Position/></Button><Button id="103" name="Подготовка4"><Position/></Button><ListBox id="201" name="ПолеСпискаТипизированнаяСвязь"><DataPath attributeId="200"/><Position/></ListBox></ChildItems></Form>)OOF";
    const auto parsed = oof::source::parse_form_xml(linked_form_xml);
    expect(parsed.ok(), "captured ListBox fixture model with ValueList link must parse");
    auto stream = form_stream::encode_document(parsed.value());
    expect(stream.ok(), "captured ListBox fixture carrier must encode with a distinct AttributeLink");
    auto& records = stream.value().items[1].items[2].items[2].items;
    const auto record = std::find_if(records.begin(), records.end(), [](const auto& candidate) {
        return candidate.is_list && candidate.items.size() > 1 && !candidate.items[1].is_list &&
            candidate.items[1].atom == "201";
    });
    expect(record != records.end(), "captured ListBox control slot must be present in its stream carrier");
    *record = captured_control;

    const auto decoded = form_stream::decode_document(stream.value(), "CapturedListBox");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* control = decoded.value().find_control(model::ObjectId{201});
    const auto* attribute = decoded.value().find_attribute(model::ObjectId{200});
    expect(control && control->kind() == model::ControlKind::list_box && control->name == "ПолеСпискаТипизированнаяСвязь" &&
               control->data_path && control->data_path->attribute.id() == model::ObjectId{200},
        "captured ListBox record must decode with the separate AttributeLink to the named source");
    expect(attribute && attribute->type.entries.size() == 1 &&
               attribute->type.entries.front().term == model::TypeDomainTerm::value_list &&
               !attribute->type.entries.front().type_uuid,
        "captured ListBox fixture must keep its independently typed ValueList Attribute");

    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "captured ListBox control must re-encode in the supported named profile");
    const auto& reencoded_records = reencoded.value().items[1].items[2].items[2].items;
    const auto reencoded_record = std::find_if(reencoded_records.begin(), reencoded_records.end(), [](const auto& candidate) {
        return candidate.is_list && candidate.items.size() > 1 && !candidate.items[1].is_list &&
            candidate.items[1].atom == "201";
    });
    expect(reencoded_record != reencoded_records.end() &&
               list_stream::dump_compact(*reencoded_record) == list_stream::dump_compact(captured_control),
        "ListBox writer must reproduce the independently captured runtime control record exactly");
}

void test_calendar_field_captured_begin_period_record_decode() {
    constexpr std::string_view captured = R"CAPTURED({"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,
{27,
{18,
{
{1,0},102,4294967295},
{09ccdc77-ea1a-4a6d-ab1c-3435eada2433,
{1,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,0,
{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},26,0,0,0,0,0,0,
{10,1,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,0,0,0,0,0},0,1,
{1,1,
{6,
{1,1,
{"ru","Страница1"}
},
{10,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,0,0,0,0,0},-1,1,1,"Страница1",1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},1}
},1,1,0,4,
{2,8,1,1,1,0,0,0,0},
{2,8,0,1,2,0,0,0,0},
{2,422,1,1,3,0,0,8,0},
{2,252,0,1,4,0,0,8,0},0,4294967295,5,64,0,
{4,4,
{0},4},0,0,57,0,0},
{0}
},
{3,
{e3c063d8-ef92-41be-9c89-b70290b5368b,100,
{1,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},9,
{4,3,
{-16},3},
{4,3,
{-14},3},
{4,3,
{-15},3},20240229000000,00010101000000,1,1,0,0,0,0,1},
{0}
},
{8,10,15,360,55,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,0,1,0,0},
{14,"CalendarFieldDefault",4294967295,0,0,0},
{0}
},
{e3c063d8-ef92-41be-9c89-b70290b5368b,101,
{1,
{
{19,0,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},9,
{4,3,
{-16},3},
{4,3,
{-14},3},
{4,3,
{-15},3},00010101000000,00010101000000,1,1,0,0,0,0,1},
{0}
},
{8,10,70,360,110,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,1,2,0,0},
{14,"CalendarFieldDisabled",4294967295,0,0,0},
{0}
},
{e3c063d8-ef92-41be-9c89-b70290b5368b,102,
{1,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},9,
{4,3,
{-16},3},
{4,3,
{-14},3},
{4,3,
{-15},3},00010101000000,00010101000000,1,1,0,0,0,0,1},
{0}
},
{8,10,125,360,165,0,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,2,3,0,0},
{14,"CalendarFieldHidden",4294967295,0,0,0},
{0}
}
}
},430,260,1,0,1,4,4,24,430,260,96},
{
{-1},103,
{0},
{0}
},
{00000000-0000-0000-0000-000000000000,0},
{0},1,4,1,0,0,0,
{0},
{0},
{10,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},100,0,0,0,0,0},1,2,0,0,1,1}
})CAPTURED";
    auto parsed = list_stream::parse(captured);
    expect(parsed.is_list && parsed.items.size() == 3,
        "captured native snapshot must contain the storage envelope and payload");
    const auto decoded = form_stream::decode_document(parsed.items[2], "CapturedBeginPeriod");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* calendar = decoded.value().find_control(model::ObjectId{100});
    const auto* begin = calendar == nullptr ? nullptr : calendar->properties().find(
        model::PropertyId::from_name("BeginOfDisplayPeriod"));
    expect(calendar != nullptr, "captured native changed1 record must retain CalendarField ID 100");
    expect(calendar->name == "CalendarFieldDefault", "captured native changed1 record must retain its actual name");
    expect(begin != nullptr, "captured native changed1 record must decode its BeginOfDisplayPeriod");
    expect(std::get<model::DateValue>(begin->value).canonical == "2024-02-29T00:00:00",
        "captured native changed1 record must decode its actual slot value");
}

void test_progress_data_path_mixed_with_existing_links() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "MixedProgressLinks";
    form.children = {model::ControlRef{model::ObjectId{42}}, model::ControlRef{model::ObjectId{5}},
        model::ControlRef{model::ObjectId{9}}, model::ControlRef{model::ObjectId{10}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::TypeDomainEntry numeric_entry;
    numeric_entry.term = model::TypeDomainTerm::numeric;
    numeric_entry.numeric = model::NumericQualifiers{10, 2, true};
    model::TypeDomainPatternValue numeric_type;
    numeric_type.entries.push_back(numeric_entry);
    model::TypeDomainPatternValue string_type;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{64, false};
    string_type.entries.push_back(string_entry);
    model::TypeDomainPatternValue boolean_type;
    model::TypeDomainEntry boolean_entry;
    boolean_entry.term = model::TypeDomainTerm::boolean;
    boolean_type.entries.push_back(boolean_entry);
    document.add_attribute(model::Attribute{model::ObjectId{17}, "Amount", numeric_type});
    document.add_attribute(model::Attribute{model::ObjectId{6}, "Text", string_type});
    document.add_attribute(model::Attribute{model::ObjectId{7}, "Flag", boolean_type});
    model::ControlNode bound{model::ObjectId{42}, "BoundProgress", model::ProgressBarPayload{}};
    bound.data_path = model::DataPath{model::AttributeRef{model::ObjectId{17}}, {}};
    document.add_control(std::move(bound));
    document.add_control(model::ControlNode{model::ObjectId{5}, "UnboundProgress", model::ProgressBarPayload{}});
    model::ControlNode input{model::ObjectId{9}, "Input", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{6}}, {}};
    document.add_control(std::move(input));
    model::ControlNode checkbox{model::ObjectId{10}, "Check", model::CheckBoxPayload{}};
    checkbox.data_path = model::DataPath{model::AttributeRef{model::ObjectId{7}}, {}};
    document.add_control(std::move(checkbox));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().message);
    const auto& links = encoded.value().items[2].items[3];
    expect(links.items.size() == 4 && links.items[0].atom == "3" &&
               links.items[1].items[0].atom == "9" && links.items[2].items[0].atom == "10" &&
               links.items[3].items[0].atom == "42",
        "bound ProgressBar, InputField, and CheckBox must produce exactly three correctly counted links");
    const auto decoded = form_stream::decode_document(encoded.value(), "MixedProgressLinks");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    expect(decoded.value().find_control(model::ObjectId{42})->data_path.has_value() &&
               !decoded.value().find_control(model::ObjectId{5})->data_path.has_value() &&
               decoded.value().find_control(model::ObjectId{9})->data_path.has_value() &&
               decoded.value().find_control(model::ObjectId{10})->data_path.has_value(),
        "bound and unbound ProgressBars must coexist with required InputField and CheckBox links");
}

void test_calendar_field_observed_record_decode() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "ObservedCalendar";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{4}}};
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{model::ObjectId{2}, "BeforeCalendar", model::ButtonPayload{}});
    document.add_control(model::ControlNode{model::ObjectId{4}, "CalendarResearch", model::CalendarFieldPayload{}});
    auto stream = form_stream::encode_document(document);
    expect(stream.ok(), "seed stream must encode before inserting the complete observed CalendarField record");
    stream.value().items[1].items[2].items[2].items[2] = list_stream::parse(R"OOF(
{e3c063d8-ef92-41be-9c89-b70290b5368b,4,
{1,
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},9,
{4,3,
{-16},3},
{4,3,
{-14},3},
{4,3,
{-15},3},20240229000000,00010101000000,1,1,0,0,0,0,1},
{0}
},
{8,0,0,0,0,1,
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},
{0,
{2,-1,6,0},
{2,-1,6,0}
},0,0,0,0,0,0,0,1,2,0,0},
{14,"CalendarResearch",4294967295,0,0,0},
{0}
}
)OOF");

    const auto decoded = form_stream::decode_document(stream.value(), "ObservedCalendar");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    const auto* calendar = decoded.value().find_control(model::ObjectId{4});
    expect(calendar != nullptr && calendar->kind() == model::ControlKind::calendar_field &&
               calendar->name == "CalendarResearch",
        "observed CalendarField identity, ID, and metadata name must decode");
    expect(calendar->properties().find(model::PropertyId::from_name("Enabled")) == nullptr,
        "observed default Enabled=true must remain implicit");

    auto changed_enabled = stream.value();
    auto& enabled_slot = changed_enabled.items[1].items[2].items[2].items[2]
        .items[2].items[1].items[0].items[1];
    enabled_slot = list_stream::ListValue::raw_atom("0");
    const auto disabled = form_stream::decode_document(changed_enabled, "ObservedCalendar");
    expect(disabled.ok(), "observed CalendarField Enabled=false variation must decode");
    const auto* disabled_calendar = disabled.value().find_control(model::ObjectId{4});
    const auto* enabled = disabled_calendar == nullptr ? nullptr : disabled_calendar->properties().find(
        model::PropertyId::from_name("Enabled"));
    expect(enabled != nullptr && !std::get<bool>(enabled->value),
        "observed Enabled=false must become an explicit named property");

    auto changed_date = stream.value();
    auto& date_slot = changed_date.items[1].items[2].items[2].items[2].items[2].items[1].items[5];
    date_slot = list_stream::ListValue::raw_atom("20230229000000");
    const auto invalid_date = form_stream::decode_document(changed_date, "ObservedCalendar");
    expect(!invalid_date && invalid_date.diagnostics().front().code == "OOF1122",
        "malformed leap-day date in the observed CalendarField record must fail closed");

    auto changed_events = stream.value();
    auto& events_slot = changed_events.items[1].items[2].items[2].items[2].items[2].items[2];
    events_slot = list_stream::parse("{1}");
    const auto partial_events = form_stream::decode_document(changed_events, "ObservedCalendar");
    expect(partial_events.ok() && !partial_events.value().reconstruction_complete() &&
               std::any_of(partial_events.diagnostics().begin(), partial_events.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_events.value().find_control(model::ObjectId{4}) != nullptr,
        "valid unknown CalendarField event profile must warn and preserve its control");
}

void test_button_label_input_field_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{3}},
        model::ControlRef{model::ObjectId{9}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    model::TypeDomainPatternValue string10;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{10, true};
    string10.entries.push_back(string_entry);
    document.add_attribute(model::Attribute{model::ObjectId{1}, "SyntheticValue", string10});
    document.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
    document.add_control(model::ControlNode{model::ObjectId{3}, "Label", model::LabelDecorationPayload{}});
    model::ControlNode input{model::ObjectId{9}, "InputSynthetic", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    input.position.left.set(231);
    input.position.top.set(135);
    input.position.width.set(70);
    input.position.height.set(30);
    input.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    input.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
    document.add_control(std::move(input));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "Button-Label-InputField profile must encode" :
        encoded.diagnostics().front().code + ":" + encoded.diagnostics().front().path + ":" + encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    const auto* decoded_input = decoded.value().find_control(model::ObjectId{9});
    expect(decoded_input && decoded_input->kind() == model::ControlKind::input_field,
        "InputField identity must survive round-trip");
    expect(decoded_input->name == "InputSynthetic" && decoded_input->data_path &&
               decoded_input->data_path->attribute.id() == model::ObjectId{1},
        "InputField name and named DataPath link must survive round-trip");
    expect(decoded_input->position.left.value() == 231 && decoded_input->position.top.value() == 135 &&
               decoded_input->position.width.value() == 70 && decoded_input->position.height.value() == 30,
        "InputField Position must survive round-trip");
    expect(decoded_input->properties().find(model::PropertyId::from_name("Enabled")) != nullptr &&
               !std::get<bool>(decoded_input->properties().find(model::PropertyId::from_name("Enabled"))->value),
        "InputField Enabled must survive round-trip");
    expect(decoded_input->properties().find(model::PropertyId::from_name("ReadOnly")) != nullptr &&
               std::get<bool>(decoded_input->properties().find(model::PropertyId::from_name("ReadOnly"))->value),
        "InputField ReadOnly must survive round-trip");
    expect(decoded.value().find_attribute(model::ObjectId{1})->type == string10,
        "linked Attribute String(10) type must survive round-trip");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded InputField document must re-encode");
    const auto redecode = form_stream::decode_document(reencoded.value(), "Main");
    expect(redecode.ok() && redecode.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1},
        "InputField DataPath must survive a second decode");
    expect(redecode.ok() && std::get<bool>(redecode.value().find_control(model::ObjectId{9})
        ->properties().find(model::PropertyId::from_name("ReadOnly"))->value),
        "InputField ReadOnly must survive two round-trips");
    const auto& read_only_payload = reencoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[13];
    expect(read_only_payload.atom == "1", "InputField ReadOnly must encode at the proven payload slot");

    const auto make_input_document = [](std::uint32_t length, bool variable, bool non_string = false, bool mixed = false,
        bool explicit_read_only_false = false, bool auto_choice_incomplete = false, bool auto_mark_incomplete = false,
        const std::vector<std::pair<std::string_view, bool>>& extra_flags = {}) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "Main";
        form.children = {
            model::ControlRef{model::ObjectId{2}},
            model::ControlRef{model::ObjectId{3}},
            model::ControlRef{model::ObjectId{9}},
        };
        model::OrdinaryFormDocument input_document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = non_string ? model::TypeDomainTerm::numeric : model::TypeDomainTerm::string;
        entry.string = model::LengthQualifiers{length, variable};
        type.entries.push_back(entry);
        if (mixed) {
            model::TypeDomainEntry numeric_entry;
            numeric_entry.term = model::TypeDomainTerm::numeric;
            type.entries.push_back(numeric_entry);
        }
        input_document.add_attribute(model::Attribute{model::ObjectId{1}, "SyntheticValue", type});
        input_document.add_control(model::ControlNode{model::ObjectId{2}, "Run", model::ButtonPayload{}});
        input_document.add_control(model::ControlNode{model::ObjectId{3}, "Label", model::LabelDecorationPayload{}});
        model::ControlNode input_field{model::ObjectId{9}, "InputSynthetic", model::InputFieldPayload{}};
        input_field.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        if (explicit_read_only_false) {
            input_field.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), false);
        }
        if (auto_choice_incomplete) {
            input_field.properties().set_explicit(model::PropertyId::from_name("AutoChoiceIncomplete"), true);
        }
        if (auto_mark_incomplete) {
            input_field.properties().set_explicit(model::PropertyId::from_name("AutoMarkIncomplete"), true);
        }
        for (const auto& [name, value] : extra_flags) {
            input_field.properties().set_explicit(model::PropertyId::from_name(name), value);
        }
        input_document.add_control(std::move(input_field));
        return input_document;
    };

    for (const auto length : {0U, 17U, 20U, 37U, 64U}) {
        for (const bool variable : {false, true}) {
            const auto length_encoded = form_stream::encode_document(make_input_document(length, variable));
            expect(length_encoded.ok(), "single-string InputField qualifiers must encode");
            const auto& input_payload = length_encoded.value().items[1].items[2].items[2]
                .items[3].items[2].items[2].items[0];
            expect(input_payload.items[14].atom == std::to_string(length),
                "InputField String qualifier length must encode at payload slot 14");
            expect(input_payload.items[13].atom == "0",
                "InputField ReadOnly must remain unchanged at payload slot 13");
            const auto& default_read_only_payload = length_encoded.value().items[1].items[2].items[2].items[3]
                .items[2].items[2].items[0].items[13];
            expect(default_read_only_payload.atom == "0", "default InputField ReadOnly must encode as false");
            const auto length_decoded = form_stream::decode_document(length_encoded.value(), "Main");
            expect(length_decoded.ok(), "single-string InputField qualifiers must decode");
            const auto* round_trip_attribute = length_decoded.value().find_attribute(model::ObjectId{1});
            expect(round_trip_attribute->type.entries.front().string.length == length,
                "InputField string length must survive round-trip");
            expect(round_trip_attribute->type.entries.front().string.variable == (length == 0 ? true : variable),
                "InputField variable length must survive canonical round-trip");
            expect(length_decoded.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1},
                "InputField DataPath must survive string qualifier round-trip");
        }
    }

    auto length_mismatch = form_stream::encode_document(make_input_document(17, true));
    expect(length_mismatch.ok(), "InputField qualifier mismatch fixture must encode");
    length_mismatch.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[14] = list_stream::ListValue::raw_atom("64");
    expect_failure(
        form_stream::decode_document(length_mismatch.value(), "Main"),
        "OOF1114",
        "$/1/2/2/3/2",
        "InputField payload length that disagrees with its named String type must be rejected");

    const auto explicit_false = form_stream::encode_document(make_input_document(10, true, false, false, true));
    expect(explicit_false.ok(), "explicit InputField ReadOnly=false must be accepted");
    const auto& explicit_false_payload = explicit_false.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[13];
    expect(explicit_false_payload.atom == "0", "explicit InputField ReadOnly=false must encode as false");

    auto auto_choice_document = make_input_document(10, true, false, false, false, true);
    const auto auto_choice_encoded = form_stream::encode_document(auto_choice_document);
    expect(auto_choice_encoded.ok(), "InputField AutoChoiceIncomplete=true must encode");
    const auto& auto_choice_payload = auto_choice_encoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0];
    expect(auto_choice_payload.items[36].atom == "1", "AutoChoiceIncomplete must encode at payload slot 36");
    const auto auto_choice_decoded = form_stream::decode_document(auto_choice_encoded.value(), "Main");
    expect(auto_choice_decoded.ok(), "InputField AutoChoiceIncomplete=true must decode");
    const auto* decoded_auto_choice = auto_choice_decoded.value().find_control(model::ObjectId{9});
    const auto* auto_choice_property = decoded_auto_choice == nullptr ? nullptr :
        decoded_auto_choice->properties().find(model::PropertyId::from_name("AutoChoiceIncomplete"));
    expect(auto_choice_property != nullptr && std::get<bool>(auto_choice_property->value),
        "InputField AutoChoiceIncomplete=true must survive round-trip");
    const auto auto_choice_reencoded = form_stream::encode_document(auto_choice_decoded.value());
    expect(auto_choice_reencoded.ok(), "decoded AutoChoiceIncomplete document must re-encode");
    expect(auto_choice_reencoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[36].atom == "1",
        "AutoChoiceIncomplete=true must remain stable after re-encoding");

    auto unknown_auto_choice_flag = auto_choice_encoded.value();
    unknown_auto_choice_flag.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[36] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(unknown_auto_choice_flag, "Main"), "OOF1105",
        "$/1/2/2/3/2/2/0/36", "unknown AutoChoiceIncomplete flag value must be rejected");

    auto auto_mark_document = make_input_document(10, true, false, false, false, false, true);
    const auto auto_mark_encoded = form_stream::encode_document(auto_mark_document);
    expect(auto_mark_encoded.ok(), "InputField AutoMarkIncomplete=true must encode");
    const auto& auto_mark_payload = auto_mark_encoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0];
    expect(auto_mark_payload.items[35].atom == "1", "AutoMarkIncomplete must encode at payload slot 35");
    const auto auto_mark_decoded = form_stream::decode_document(auto_mark_encoded.value(), "Main");
    expect(auto_mark_decoded.ok(), "InputField AutoMarkIncomplete=true must decode");
    const auto* decoded_auto_mark = auto_mark_decoded.value().find_control(model::ObjectId{9});
    const auto* auto_mark_property = decoded_auto_mark == nullptr ? nullptr :
        decoded_auto_mark->properties().find(model::PropertyId::from_name("AutoMarkIncomplete"));
    expect(auto_mark_property != nullptr && std::get<bool>(auto_mark_property->value),
        "InputField AutoMarkIncomplete=true must survive round-trip");
    const auto auto_mark_reencoded = form_stream::encode_document(auto_mark_decoded.value());
    expect(auto_mark_reencoded.ok() && auto_mark_reencoded.value().items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[35].atom == "1",
        "AutoMarkIncomplete=true must remain stable after re-encoding");
    auto unknown_auto_mark_flag = auto_mark_encoded.value();
    unknown_auto_mark_flag.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[35] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(unknown_auto_mark_flag, "Main"), "OOF1105",
        "$/1/2/2/3/2/2/0/35", "unknown AutoMarkIncomplete flag value must be rejected");

    constexpr std::array<std::pair<std::string_view, bool>, 14> input_field_flags{{
        {"Wrap", true}, {"ChooseType", true}, {"MarkNegatives", false}, {"ChoiceButton", false},
        {"OpenButton", false}, {"ClearButton", false}, {"SpinButton", false},
        {"ChoiceListButton", false}, {"Transparent", false},
        {"MultiLine", false}, {"ExtendedEdit", false}, {"PasswordMode", false},
        {"AutoMarkIncomplete", false}, {"AutoChoiceIncomplete", false},
    }};
    const auto default_flags_encoded = form_stream::encode_document(make_input_document(10, true));
    expect(default_flags_encoded.ok(), "InputField Boolean defaults must encode");
    const auto default_flags_decoded = form_stream::decode_document(default_flags_encoded.value(), "Main");
    expect(default_flags_decoded.ok(), "InputField Boolean defaults must decode");
    for (const auto& [name, default_value] : input_field_flags) {
        const auto* default_control = default_flags_decoded.value().find_control(model::ObjectId{9});
        expect(default_control->properties().find(model::PropertyId::from_name(name)) == nullptr,
            "InputField Boolean default must decode as implicit");
        auto toggled_document = make_input_document(10, true, false, false, false, false, false, {{name, !default_value}});
        const auto toggled_encoded = form_stream::encode_document(toggled_document);
        expect(toggled_encoded.ok(), "individual InputField Boolean toggle must encode");
        const auto toggled_decoded = form_stream::decode_document(toggled_encoded.value(), "Main");
        expect(toggled_decoded.ok(), "individual InputField Boolean toggle must decode");
        const auto* decoded_control = toggled_decoded.value().find_control(model::ObjectId{9});
        const auto* property = decoded_control->properties().find(model::PropertyId::from_name(name));
        expect(property != nullptr && std::get<bool>(property->value) == !default_value,
            "individual InputField Boolean toggle must survive round-trip");
        const auto toggled_reencoded = form_stream::encode_document(toggled_decoded.value());
        expect(toggled_reencoded.ok() &&
                list_stream::dump_compact(toggled_reencoded.value().items[1].items[2].items[2].items[3]) ==
                    list_stream::dump_compact(toggled_encoded.value().items[1].items[2].items[2].items[3]),
            "individual InputField Boolean toggle must preserve its exact control stream");
    }

    auto mismatched_paired_flag = default_flags_encoded.value();
    mismatched_paired_flag.items[1].items[2].items[2].items[3].items[2].items[2].items[0].items[26] =
        list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(mismatched_paired_flag, "Main"), "OOF1122",
        "$/1/2/2/3/2/2/0/26", "mismatched paired InputField MultiLine flags must be rejected");

    auto malformed_paired_flag = default_flags_encoded.value();
    malformed_paired_flag.items[1].items[2].items[2].items[3].items[2].items[3] = list_stream::ListValue::raw_atom("0");
    expect_failure(form_stream::decode_document(malformed_paired_flag, "Main"), "OOF1101",
        "$/1/2/2/3/2/3", "malformed paired InputField flag structure must be rejected");

    std::vector<std::pair<std::string_view, bool>> mixed_flag_values;
    for (const auto& [name, default_value] : input_field_flags) {
        mixed_flag_values.emplace_back(name, !default_value);
    }
    auto mixed_flags_document = make_input_document(10, true, false, false, false, false, false, mixed_flag_values);
    const auto mixed_flags_encoded = form_stream::encode_document(mixed_flags_document);
    expect(mixed_flags_encoded.ok(), "mixed persisted InputField Boolean flags must encode");
    const auto mixed_flags_decoded = form_stream::decode_document(mixed_flags_encoded.value(), "Main");
    expect(mixed_flags_decoded.ok(), "mixed persisted InputField Boolean flags must decode");
    const auto mixed_flags_reencoded = form_stream::encode_document(mixed_flags_decoded.value());
    expect(mixed_flags_reencoded.ok() &&
            list_stream::dump_compact(mixed_flags_reencoded.value().items[1].items[2].items[2].items[3]) ==
                list_stream::dump_compact(mixed_flags_encoded.value().items[1].items[2].items[2].items[3]),
        "mixed persisted InputField Boolean flags must preserve the exact control stream");

    auto mismatch = encoded.value();
    auto& mismatch_info = mismatch.items[1].items[2].items[2].items[3].items[2];
    mismatch_info.items[1] = list_stream::parse("{\"Pattern\",{\"S\",20,1}}");
    expect_failure(
        form_stream::decode_document(mismatch, "Main"),
        "OOF1122",
        "$/1/2/2/3/2/1",
        "InputField type must match its linked Attribute");

    expect_failure(
        form_stream::encode_document(make_input_document(10, true, true)),
        "OOF1122",
        "$/InputField/DataPath",
        "non-string InputField attribute types must remain unsupported");
    expect_failure(
        form_stream::encode_document(make_input_document(10, true, false, true)),
        "OOF1122",
        "$/InputField/DataPath",
        "mixed InputField attribute types must remain unsupported");

    auto unsupported_leaf = encoded.value();
    auto& input_record = unsupported_leaf.items[1].items[2].items[2].items[3];
    input_record.items[2].items[2].items[0].items[45] = list_stream::ListValue::raw_atom("9");
    const auto partial_input = form_stream::decode_document(unsupported_leaf, "Main");
    expect(partial_input.ok() && !partial_input.value().reconstruction_complete() &&
               std::any_of(partial_input.diagnostics().begin(), partial_input.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }),
        "valid unknown InputField info leaf must warn and preserve a partial named model");
    const auto* partial_input_control = partial_input.value().find_control(model::ObjectId{9});
    expect(partial_input_control != nullptr && partial_input_control->data_path.has_value(),
        "known InputField identity and DataPath must survive an unknown info leaf");

    auto malformed_text_edit = encoded.value();
    auto& payload = malformed_text_edit.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0];
    payload.items[12] = list_stream::ListValue::raw_atom("9");
    const auto partial_text_edit = form_stream::decode_document(malformed_text_edit, "Main");
    expect(partial_text_edit.ok() && !partial_text_edit.value().reconstruction_complete() &&
               std::any_of(partial_text_edit.diagnostics().begin(), partial_text_edit.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_text_edit.value().find_control(model::ObjectId{9})->data_path.has_value(),
        "valid unknown InputField TextEdit slot must warn and preserve known DataPath");
    auto unsupported_text_edit_flag = encoded.value();
    unsupported_text_edit_flag.items[1].items[2].items[2].items[3]
        .items[2].items[2].items[0].items[12] = list_stream::ListValue::raw_atom("1");
    const auto partial_text_edit_flag = form_stream::decode_document(unsupported_text_edit_flag, "Main");
    expect(partial_text_edit_flag.ok() && !partial_text_edit_flag.value().reconstruction_complete() &&
               std::any_of(partial_text_edit_flag.diagnostics().begin(), partial_text_edit_flag.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }),
        "valid unknown InputField TextEdit flag must warn without rejecting the named model");
    expect_failure(form_stream::encode_document(make_input_document(
        10, true, false, false, false, false, false, {{"TextEdit", false}})),
        "OOF1122", "$/InputField", "unsupported explicit TextEdit=false must fail encoding");
}

void test_input_field_tooltip_and_format_round_trip() {
    struct TextProperties {
        std::optional<std::string> tool_tip;
        std::optional<std::string> format;
        bool with_flags = false;
    };
    const auto make_document = [](const TextProperties& text) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "InputStrings";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry type_entry;
        type_entry.term = model::TypeDomainTerm::string;
        type_entry.string = model::LengthQualifiers{64, false};
        type.entries.push_back(type_entry);
        document.add_attribute(model::Attribute{model::ObjectId{1}, "Value", type});
        model::ControlNode input{model::ObjectId{2}, "Input", model::InputFieldPayload{}};
        input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        if (text.tool_tip.has_value()) {
            input.properties().set_explicit(model::PropertyId::from_name("ToolTip"), *text.tool_tip);
        }
        if (text.format.has_value()) {
            input.properties().set_explicit(model::PropertyId::from_name("Format"), *text.format);
        }
        if (text.with_flags) {
            input.properties().set_explicit(model::PropertyId::from_name("AutoMarkIncomplete"), true);
            input.properties().set_explicit(model::PropertyId::from_name("MultiLine"), true);
            input.properties().set_explicit(model::PropertyId::from_name("PasswordMode"), true);
        }
        document.add_control(std::move(input));
        return document;
    };
    const auto input_record = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };
    const auto expect_round_trip = [&](const TextProperties& values, std::string_view case_name) {
        const auto encoded = form_stream::encode_document(make_document(values));
        expect(encoded.ok(), "InputField localized string case must encode");
        const auto decoded = form_stream::decode_document(encoded.value(), "InputStrings");
        expect(decoded.ok(), "InputField localized string case must decode");
        const auto* input = decoded.value().find_control(model::ObjectId{2});
        expect(input != nullptr, "InputField localized string case must resolve");
        const auto* tool_tip = input->properties().find(model::PropertyId::from_name("ToolTip"));
        const auto* format = input->properties().find(model::PropertyId::from_name("Format"));
        if (values.tool_tip.has_value() && !values.tool_tip->empty()) {
            expect(tool_tip && std::get<std::string>(tool_tip->value) == *values.tool_tip,
                "InputField ToolTip must round-trip Unicode and XML-sensitive text");
        } else {
            expect(tool_tip == nullptr, "empty InputField ToolTip must normalize to its default");
        }
        if (values.format.has_value() && !values.format->empty()) {
            expect(format && std::get<std::string>(format->value) == *values.format,
                "InputField Format must round-trip Unicode and punctuation");
        } else {
            expect(format == nullptr, "empty InputField Format must normalize to its default");
        }
        if (values.with_flags) {
            expect(input->properties().find(model::PropertyId::from_name("AutoMarkIncomplete")) != nullptr &&
                    input->properties().find(model::PropertyId::from_name("MultiLine")) != nullptr &&
                    input->properties().find(model::PropertyId::from_name("PasswordMode")) != nullptr,
                "InputField localized strings must coexist with independent and paired flags");
        }
        const auto reencoded = form_stream::encode_document(decoded.value());
        expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
                list_stream::dump_compact(encoded.value()),
            case_name);
        return encoded.value();
    };

    const auto empty = expect_round_trip(TextProperties{std::string{}, std::string{}, false},
        "empty InputField ToolTip and Format must retain canonical storage");
    expect(input_record(empty).items[2].items[2].items[0].items[34].is_list &&
            input_record(empty).items[2].items[2].items[0].items[0].items[12].is_list,
        "empty InputField localized properties must use typed empty localization records");

    const std::string tool_tip_text = "Подсказка Ω <важно> & \"цитата\"";
    const std::string format_text = "Л=ru_RU; NFD=2; ЧРГ='Ω & <>'";
    const auto tool_tip_only = expect_round_trip(TextProperties{tool_tip_text, std::nullopt, false},
        "InputField ToolTip-only storage must be stable");
    expect(list_stream::dump_compact(input_record(tool_tip_only).items[2].items[2].items[0].items[0].items[12]) ==
            value_codec::encode_localized_string(model::LocalizedStringValue{{{"ru", tool_tip_text}}}),
        "InputField ToolTip must occupy its proven localized base-info slot");
    const auto format_only = expect_round_trip(TextProperties{std::nullopt, format_text, false},
        "InputField Format-only storage must be stable");
    expect(list_stream::dump_compact(input_record(format_only).items[2].items[2].items[0].items[34]) ==
            value_codec::encode_localized_string(model::LocalizedStringValue{{{"ru", format_text}}}),
        "InputField Format must occupy its proven localized control-info slot");
    const auto combined = expect_round_trip(TextProperties{tool_tip_text, format_text, true},
        "both InputField strings and Boolean flags must preserve exact storage");

    const auto set_localized_record = [](list_stream::ListValue& encoded, bool tool_tip, list_stream::ListValue value) {
        auto& payload = encoded.items[1].items[2].items[2].items[1].items[2].items[2].items[0];
        if (tool_tip) payload.items[0].items[12] = std::move(value);
        else payload.items[34] = std::move(value);
    };
    for (const bool tool_tip : {true, false}) {
        const auto property_name = tool_tip ? "ToolTip" : "Format";
        const auto property_path = tool_tip ? "$/1/2/2/1/2/2/0/0/12" : "$/1/2/2/1/2/2/0/34";
        auto malformed = combined;
        set_localized_record(malformed, tool_tip, list_stream::ListValue::raw_atom("malformed"));
        expect_failure(form_stream::decode_document(malformed, "InputStrings"), "OOF1108", property_path,
            std::string("malformed InputField ") + property_name + " localization must be rejected");
        auto multilingual = combined;
        set_localized_record(multilingual, tool_tip, list_stream::parse(value_codec::encode_localized_string(
            model::LocalizedStringValue{{{"ru", "Текст"}, {"en", "Text"}}})));
        expect_failure(form_stream::decode_document(multilingual, "InputStrings"), "OOF1115", property_path,
            std::string("multilingual InputField ") + property_name + " must be rejected without loss");
    }
}

void test_input_field_alignment_and_choice_list_height_round_trip() {
    struct LayoutProperties {
        std::optional<std::string> horizontal;
        std::optional<std::string> vertical;
        std::optional<std::int64_t> height;
        bool include_existing_properties = false;
        std::optional<model::DecimalValue> decimal_height;
    };
    const auto make_document = [](const LayoutProperties& values) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "InputLayout";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::string;
        entry.string = model::LengthQualifiers{64, false};
        type.entries.push_back(entry);
        document.add_attribute(model::Attribute{model::ObjectId{1}, "Value", type});
        model::ControlNode input{model::ObjectId{2}, "Entry", model::InputFieldPayload{}};
        input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        if (values.horizontal) {
            input.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"),
                model::EnumerationValue{"HorizontalAlign", *values.horizontal});
        }
        if (values.vertical) {
            input.properties().set_explicit(model::PropertyId::from_name("VerticalAlign"),
                model::EnumerationValue{"VerticalAlign", *values.vertical});
        }
        if (values.height) {
            input.properties().set_explicit(model::PropertyId::from_name("ChoiceListHeight"), *values.height);
        }
        if (values.decimal_height) {
            input.properties().set_explicit(model::PropertyId::from_name("ChoiceListHeight"), *values.decimal_height);
        }
        if (values.include_existing_properties) {
            input.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
            input.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
            input.properties().set_explicit(model::PropertyId::from_name("ToolTip"), std::string("Hint"));
            input.properties().set_explicit(model::PropertyId::from_name("Format"), std::string("N=2"));
            const std::array<std::pair<std::string_view, bool>, 14> flags{{
                {"AutoChoiceIncomplete", true}, {"Wrap", false}, {"ChooseType", false},
                {"MarkNegatives", true}, {"ChoiceButton", true}, {"OpenButton", true},
                {"ClearButton", true}, {"SpinButton", true}, {"ChoiceListButton", true},
                {"Transparent", true}, {"MultiLine", true}, {"ExtendedEdit", true},
                {"PasswordMode", true}, {"AutoMarkIncomplete", true},
            }};
            for (const auto& [name, value] : flags) {
                input.properties().set_explicit(model::PropertyId::from_name(name), value);
            }
        }
        document.add_control(std::move(input));
        return document;
    };
    const auto input_record = [](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return encoded.items[1].items[2].items[2].items[1];
    };
    const auto payload = [&](const list_stream::ListValue& encoded) -> const list_stream::ListValue& {
        return input_record(encoded).items[2].items[2].items[0];
    };
    const auto round_trip = [&](const LayoutProperties& values, std::int32_t horizontal,
                                std::int32_t vertical, std::int32_t height) {
        const auto encoded = form_stream::encode_document(make_document(values));
        expect(encoded.ok(), "InputField layout values must encode");
        const auto& encoded_payload = payload(encoded.value());
        expect(encoded_payload.items[17].atom == std::to_string(horizontal) &&
                encoded_payload.items[18].atom == std::to_string(vertical) &&
                encoded_payload.items[31].atom == std::to_string(height),
            "InputField alignment and height must occupy their proven control-info slots");
        const auto decoded = form_stream::decode_document(encoded.value(), "InputLayout");
        expect(decoded.ok(), "InputField layout values must decode");
        const auto* input = decoded.value().find_control(model::ObjectId{2});
        expect(input != nullptr, "InputField layout control must resolve");
        const auto* decoded_horizontal = input->properties().find(model::PropertyId::from_name("HorizontalAlign"));
        const auto* decoded_vertical = input->properties().find(model::PropertyId::from_name("VerticalAlign"));
        const auto* decoded_height = input->properties().find(model::PropertyId::from_name("ChoiceListHeight"));
        constexpr std::array<std::string_view, 5> horizontal_members{"Left", "Center", "Right", "Justify", "Auto"};
        constexpr std::array<std::string_view, 3> vertical_members{"Top", "Center", "Bottom"};
        expect((horizontal == 4 && decoded_horizontal == nullptr) ||
                (decoded_horizontal && std::get<model::EnumerationValue>(decoded_horizontal->value) ==
                    model::EnumerationValue{"HorizontalAlign", std::string(horizontal_members[horizontal])}),
            "InputField HorizontalAlign must round-trip and omit Auto default");
        expect((vertical == 0 && decoded_vertical == nullptr) ||
                (decoded_vertical && std::get<model::EnumerationValue>(decoded_vertical->value) ==
                    model::EnumerationValue{"VerticalAlign", std::string(vertical_members[vertical])}),
            "InputField VerticalAlign must round-trip and omit Top default");
        expect((height == 0 && decoded_height == nullptr) ||
                (decoded_height && std::get<std::int64_t>(decoded_height->value) == height),
            "InputField ChoiceListHeight must round-trip and omit zero default");
        if (values.include_existing_properties) {
            std::size_t explicit_count = 0;
            input->properties().for_each_explicit([&](const model::PropertyEntry&) { ++explicit_count; });
            expect(explicit_count == 21,
                "new InputField layout properties must coexist with all 18 prior properties");
        }
        const auto reencoded = form_stream::encode_document(decoded.value());
        expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) ==
                list_stream::dump_compact(encoded.value()),
            "InputField layout properties must preserve exact storage on reencode");
        return encoded.value();
    };

    constexpr std::array<std::pair<std::string_view, std::int32_t>, 5> horizontal_values{{
        {"Left", 0}, {"Center", 1}, {"Right", 2}, {"Justify", 3}, {"Auto", 4},
    }};
    for (const auto& [member, slot] : horizontal_values) {
        round_trip(LayoutProperties{std::string(member), std::nullopt, std::nullopt, false, std::nullopt}, slot, 0, 0);
    }
    constexpr std::array<std::pair<std::string_view, std::int32_t>, 3> vertical_values{{
        {"Top", 0}, {"Center", 1}, {"Bottom", 2},
    }};
    for (const auto& [member, slot] : vertical_values) {
        round_trip(LayoutProperties{std::nullopt, std::string(member), std::nullopt, false, std::nullopt}, 4, slot, 0);
    }
    for (const std::int32_t value : {0, 7, -7, std::numeric_limits<std::int32_t>::min(),
             std::numeric_limits<std::int32_t>::max()}) {
        round_trip(LayoutProperties{std::nullopt, std::nullopt, value, false, std::nullopt}, 4, 0, value);
    }
    round_trip(LayoutProperties{"Right", "Bottom", 7, true, std::nullopt}, 2, 2, 7);

    const auto defaults = form_stream::encode_document(make_document({}));
    expect(defaults.ok(), "default InputField layout must encode");
    auto invalid_horizontal = defaults.value();
    invalid_horizontal.items[1].items[2].items[2].items[1].items[2].items[2].items[0].items[17] =
        list_stream::ListValue::raw_atom("5");
    expect_failure(form_stream::decode_document(invalid_horizontal, "InputLayout"), "OOF1114",
        "$/1/2/2/1/2/2/0/17", "unknown InputField HorizontalAlign storage values must fail closed");
    auto invalid_vertical = defaults.value();
    invalid_vertical.items[1].items[2].items[2].items[1].items[2].items[2].items[0].items[18] =
        list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(invalid_vertical, "InputLayout"), "OOF1114",
        "$/1/2/2/1/2/2/0/18", "unknown InputField VerticalAlign storage values must fail closed");
    auto out_of_range_storage_height = defaults.value();
    out_of_range_storage_height.items[1].items[2].items[2].items[1].items[2].items[2].items[0].items[31] =
        list_stream::ListValue::raw_atom("2147483648");
    expect_failure(form_stream::decode_document(out_of_range_storage_height, "InputLayout"), "OOF1105",
        "$/1/2/2/1/2/2/0/31", "out-of-int32 InputField ChoiceListHeight storage must fail closed");

    const auto expect_invalid_property = [&](std::optional<model::EnumerationValue> horizontal,
                                             std::optional<model::EnumerationValue> vertical,
                                             std::string_view property_path) {
        auto form = model::Form{};
        form.id = model::ObjectId{1};
        form.name = "InvalidInputLayout";
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::TypeDomainPatternValue type;
        model::TypeDomainEntry entry;
        entry.term = model::TypeDomainTerm::string;
        entry.string = model::LengthQualifiers{64, false};
        type.entries.push_back(entry);
        document.add_attribute(model::Attribute{model::ObjectId{1}, "Value", type});
        model::ControlNode input{model::ObjectId{2}, "Entry", model::InputFieldPayload{}};
        input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
        if (horizontal) input.properties().set_explicit(model::PropertyId::from_name("HorizontalAlign"), *horizontal);
        if (vertical) input.properties().set_explicit(model::PropertyId::from_name("VerticalAlign"), *vertical);
        document.add_control(std::move(input));
        expect_failure(form_stream::encode_document(document), "OOF1122", property_path,
            "InputField must reject invalid enum type or member");
    };
    expect_invalid_property(model::EnumerationValue{"VerticalAlign", "Top"}, std::nullopt,
        "$/InputField/HorizontalAlign");
    expect_invalid_property(model::EnumerationValue{"HorizontalAlign", "Middle"}, std::nullopt,
        "$/InputField/HorizontalAlign");
    expect_invalid_property(std::nullopt, model::EnumerationValue{"HorizontalAlign", "Center"},
        "$/InputField/VerticalAlign");
    expect_invalid_property(std::nullopt, model::EnumerationValue{"VerticalAlign", "Middle"},
        "$/InputField/VerticalAlign");

    auto fractional_document = make_document({std::nullopt, std::nullopt, std::nullopt, false, model::DecimalValue{"7.5"}});
    expect_failure(form_stream::encode_document(fractional_document), "OOF1123", "$",
        "fractional ChoiceListHeight must be rejected as invalid for the integer32 model property");
    auto overflow_document = make_document({std::nullopt, std::nullopt, std::nullopt, false, model::DecimalValue{"2147483648"}});
    expect_failure(form_stream::encode_document(overflow_document), "OOF1123", "$",
        "ChoiceListHeight outside int32 must be rejected by model validation");
}

void test_single_input_field_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {model::ControlRef{model::ObjectId{9}}};
    model::OrdinaryFormDocument document(std::move(form));

    model::TypeDomainPatternValue string64;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{64, false};
    string64.entries.push_back(string_entry);
    document.add_attribute(model::Attribute{model::ObjectId{1}, "SyntheticValue", string64});

    model::ControlNode input{model::ObjectId{9}, "InputSynthetic", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    input.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    input.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
    document.add_control(std::move(input));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "single InputField profile must encode" : encoded.diagnostics().front().message);
    auto encoded_input = encoded.value();
    auto& input_record = encoded_input.items[1].items[2].items[2].items[1];
    expect(input_record.items[3].items[geometry_tail_start(input_record.items[3]) + 1].atom == "0" && input_record.items[3].items[geometry_tail_start(input_record.items[3]) + 2].atom == "1",
        "single InputField geometry must use sibling references 0 and 1");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    expect(decoded.value().form().children.size() == 1, "single InputField composition must survive round-trip");
    const auto* decoded_input = decoded.value().find_control(model::ObjectId{9});
    expect(decoded_input && decoded_input->data_path &&
               decoded_input->data_path->attribute.id() == model::ObjectId{1},
        "single InputField DataPath must survive round-trip");
    expect(decoded.value().find_attribute(model::ObjectId{1})->type == string64,
        "single InputField String(64) type must survive round-trip");
    expect(decoded_input->properties().find(model::PropertyId::from_name("Enabled")) != nullptr &&
               !std::get<bool>(decoded_input->properties().find(model::PropertyId::from_name("Enabled"))->value) &&
               std::get<bool>(decoded_input->properties().find(model::PropertyId::from_name("ReadOnly"))->value),
        "single InputField Enabled and ReadOnly must survive round-trip");

    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "single InputField document must re-encode");
    const auto redecode = form_stream::decode_document(reencoded.value(), "Main");
    expect(redecode.ok() && redecode.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1},
        "single InputField DataPath must survive two round-trips");

    auto wrong_sibling_reference = encoded.value();
    wrong_sibling_reference.items[1].items[2].items[2].items[1].items[3].items[geometry_tail_start(wrong_sibling_reference.items[1].items[2].items[2].items[1].items[3]) + 1] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(
        form_stream::decode_document(wrong_sibling_reference, "Main"),
        "OOF1114",
        "$/1/2/2/1/3/19",
        "single InputField must reject triple-profile geometry references");
}

void test_spreadsheet_cell_controls() {
    // Independent literals from synthetic platform scalar editors, not the writer under test.
    const std::array<std::string_view, 4> captured = {
R"CELL({2,1,381ed624-9217-4e63-85db-c4c3cb87daae,
{
{9,
{"Pattern",
{"S",100,1}
},
{
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},31,0,0,1,0,0,0,0,0,0,1,0,0,100,0,0,4,0,
{"U"},
{"U"},"",0,1,0,0,0,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},0,0,0,
{0,0,0},
{1,0},0,0,0,0,0,0,0,16777215,2,0,0}
},
{1,
{9a7643d2-19e9-45e2-8893-280bc9195a97,
{4,
{"U"},
{"U"},0,"",0,0}
}
},
{0},0,1,0,
{1,0},0}
},0})CELL",
R"CELL({2,1,381ed624-9217-4e63-85db-c4c3cb87daae,
{
{9,
{"Pattern",
{"N",15,3,0}
},
{
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},31,0,0,0,0,0,0,0,0,0,1,0,0,15,3,0,4,0,
{"U"},
{"U"},"",1,1,0,0,0,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},0,0,0,
{0,0,0},
{1,0},0,0,0,0,0,0,0,16777215,2,0,0}
},
{1,
{9a7643d2-19e9-45e2-8893-280bc9195a97,
{4,
{"U"},
{"U"},0,"",0,0}
}
},
{0},0,1,0,
{1,0},0}
},0})CELL",
R"CELL({2,1,381ed624-9217-4e63-85db-c4c3cb87daae,
{
{9,
{"Pattern",
{"B"}
},
{
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},31,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,4,0,
{"U"},
{"U"},"",0,1,0,0,0,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},0,0,0,
{0,0,0},
{1,0},0,0,0,0,0,0,0,16777215,2,0,0}
},
{1,
{9a7643d2-19e9-45e2-8893-280bc9195a97,
{4,
{"U"},
{"U"},0,"",0,0}
}
},
{0},0,1,0,
{1,0},0}
},0})CELL",
R"CELL({2,1,381ed624-9217-4e63-85db-c4c3cb87daae,
{
{9,
{"Pattern",
{"D"}
},
{
{
{19,1,
{4,4,
{0},4},
{4,4,
{0},4},
{8,3,0,1,100},0,
{4,4,
{0},4},
{4,4,
{0},4},
{4,4,
{0},4},
{4,3,
{-7},3},
{4,3,
{-21},3},
{3,1,
{-18},0,0,0},
{1,0},0,0,100,2,2,1,2,
{4,4,
{0},4}
},31,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,4,0,
{"U"},
{"U"},"",2,1,0,0,0,0,
{4,0,
{0},"",-1,-1,1,0,""},
{4,0,
{0},"",-1,-1,1,0,""},2,0,0,
{0,0,0},
{1,0},0,0,0,0,0,0,0,16777215,2,0,0}
},
{1,
{9a7643d2-19e9-45e2-8893-280bc9195a97,
{4,
{"U"},
{"U"},0,"",0,0}
}
},
{0},0,1,0,
{1,0},0}
},0})CELL"};
    const auto chunks_for_text = [](std::string_view text) {
        const std::string packet = std::string("\xef\xbb\xbf") + std::string(text);
        const std::vector<std::uint8_t> bytes(packet.begin(), packet.end());
        const auto encoded = test_base64_encode(bytes);
        std::vector<list_stream::ListValue> chunks;
        for (std::size_t pos = 0; pos < encoded.size(); pos += 64)
            chunks.push_back(list_stream::ListValue::raw_atom((pos == 0 ? "#base64:" : "") + encoded.substr(pos, 64)));
        return list_stream::ListValue::list(std::move(chunks));
    };
    const auto chunks_for_envelope = [&](const list_stream::ListValue& envelope) {
        const auto text = list_stream::dump_compact(envelope);
        return chunks_for_text(std::string_view(text).substr(1, text.size() - 2));
    };
    model::Form form;
    form.id = model::ObjectId{1}; form.name = "Cells";
    form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode field{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    auto& cells = std::get<model::SpreadsheetDocumentFieldPayload>(field.payload).cells;
    const std::array terms{model::TypeDomainTerm::string, model::TypeDomainTerm::numeric,
        model::TypeDomainTerm::boolean, model::TypeDomainTerm::date};
    const std::array<model::PropertyValue, 4> values{std::string("Unicode Привет 世界 🌍"),
        model::DecimalValue{"-12.375"}, false, model::DateValue{"2026-10-04T12:30:45"}};
    for (std::size_t i = 0; i < terms.size(); ++i) {
        model::TypeDomainEntry type; type.term = terms[i];
        if (i == 0) type.string = {100, true};
        if (i == 1) type.numeric = {15, 3, false};
        if (i == 3) type.date = {true, true};
        model::SpreadsheetDocumentCell cell{1, static_cast<std::uint32_t>(i + 1), {},
            model::SpreadsheetDocumentCellValue{model::TypeDomainPatternValue{{type}}, values[i]}};
        cell.control.emplace();
        cell.control->properties.set_explicit(model::PropertyId::from_name("ReadOnly"), false);
        cells.push_back(std::move(cell));
    }
    document.add_control(std::move(field));
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded.ok() ? "" : encoded.diagnostics().front().message);
    auto fixture = encoded.value();
    auto& info = fixture.items[1].items[2].items[2].items[1].items[2].items[11];
    for (std::size_t i = 0; i < captured.size(); ++i) {
        auto& cell = info.items[20 + 2 * i];
        expect(cell.items.size() == (i == 2 ? 4 : 5) && cell.items[0].atom == (i == 2 ? "1" : "3"),
            "Cell editor flags must preserve absent Boolean default values independently of Control");
        std::string joined;
        for (std::size_t part = 0; part < cell.items[3].items.size(); ++part)
            joined += part == 0 ? cell.items[3].items[part].atom.substr(8) : cell.items[3].items[part].atom;
        const auto bytes = test_base64_decode(joined);
        const std::string text(bytes.begin(), bytes.end());
        const auto produced = list_stream::parse("{" + text.substr(3) + "}");
        expect(list_stream::dump_compact(produced) == list_stream::dump_compact(list_stream::parse(captured[i])),
            "Cell editor writer must match the independent scalar editor literal");
        cell.items[3] = chunks_for_text(captured[i].substr(1, captured[i].size() - 2));
    }
    for (bool read_only : {false, true}) {
        auto changed = fixture;
        auto& changed_info = changed.items[1].items[2].items[2].items[1].items[2].items[11];
        for (std::size_t i = 0; i < captured.size(); ++i) {
            auto packet = list_stream::parse(captured[i]);
            packet.items[3].items[0].items[2].items[0].items[13] = list_stream::ListValue::raw_atom(read_only ? "1" : "0");
            changed_info.items[20 + 2 * i].items[3] = chunks_for_envelope(packet);
        }
        const auto decoded = form_stream::decode_document(changed, "Cells");
        expect(decoded.ok(), decoded.ok() ? "" : decoded.diagnostics().front().message);
        const auto& restored = std::get<model::SpreadsheetDocumentFieldPayload>(decoded.value().find_control(model::ObjectId{2})->payload).cells;
        for (std::size_t i = 0; i < restored.size(); ++i) {
            expect(restored[i].control.has_value() && restored[i].control->kind == model::ControlKind::input_field &&
                std::get<bool>(restored[i].control->properties.find(model::PropertyId::from_name("ReadOnly"))->value) == read_only &&
                restored[i].typed_value->value == values[i], "Independent Cell editor must decode into named ReadOnly and unchanged scalar value");
        }
    }
    model::OrdinaryFormDocument qualified(document.form());
    auto qualified_field = *document.find_control(model::ObjectId{2});
    auto& qualified_cells = std::get<model::SpreadsheetDocumentFieldPayload>(qualified_field.payload).cells;
    qualified_cells[0].typed_value->type.entries.front().string = {37, true};
    qualified_cells[1].typed_value->type.entries.front().numeric = {12, 4, false};
    qualified.add_control(qualified_field);
    const auto qualified_encoded = form_stream::encode_document(qualified);
    expect(qualified_encoded.ok(), "Cell.Control must derive qualifiers without a hardcoded String100 restriction");
    const auto qualified_decoded = form_stream::decode_document(qualified_encoded.value(), "Cells");
    expect(qualified_decoded.ok() && std::get<model::SpreadsheetDocumentFieldPayload>(qualified_decoded.value().find_control(model::ObjectId{2})->payload).cells == qualified_cells,
        "Named Cell.Control must round-trip supported String and Number qualifiers");
    model::OrdinaryFormDocument unsupported(document.form());
    auto unsupported_field = *document.find_control(model::ObjectId{2});
    std::get<model::SpreadsheetDocumentFieldPayload>(unsupported_field.payload).cells[0].control->kind = model::ControlKind::check_box;
    unsupported.add_control(std::move(unsupported_field));
    expect(!form_stream::encode_document(unsupported), "Model must reject unsupported Cell editor kinds before serialization");
    for (int mutation = 0; mutation < 12; ++mutation) {
        auto broken = fixture;
        auto& cell = broken.items[1].items[2].items[2].items[1].items[2].items[11].items[20];
        auto packet = list_stream::parse(captured[0]);
        if (mutation == 0) packet.items[0] = list_stream::ListValue::raw_atom("3");
        if (mutation == 1) packet.items[3].items[0].items[0] = list_stream::ListValue::raw_atom("8");
        if (mutation == 2) packet.items[3].items[0].items[2].items[0].items[13] = list_stream::ListValue::raw_atom("2");
        if (mutation == 3) packet.items[3].items[0].items[2].items[0].items[5] = list_stream::ListValue::raw_atom("1");
        if (mutation == 4) packet.items.push_back(list_stream::ListValue::raw_atom("0"));
        if (mutation == 8) packet.items[2] = list_stream::ListValue::raw_atom("00000000-0000-0000-0000-000000000000");
        if (mutation == 9) packet.items[3].items[0].items[1].items[1].items[1] = list_stream::ListValue::raw_atom("37");
        cell.items[3] = chunks_for_envelope(packet);
        if (mutation == 5) cell.items[3].items[0] = list_stream::ListValue::raw_atom("#base64:!!!!");
        if (mutation == 6) cell.items[0] = list_stream::ListValue::raw_atom("7");
        if (mutation == 7) cell.items[1] = list_stream::ListValue::raw_atom("999");
        if (mutation == 10) cell.items[3].items.pop_back();
        if (mutation == 11) cell.items[2] = list_stream::ListValue::raw_atom("2");
        expect(!form_stream::decode_document(broken, "Cells"), "Cell decoder must reject corrupted kind, field, Boolean, trailing data, base64, flags, and references");
    }
}

void test_spreadsheet_document_field_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {model::ControlRef{model::ObjectId{9}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode field{model::ObjectId{9}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    auto& field_payload = std::get<model::SpreadsheetDocumentFieldPayload>(field.payload);
    field_payload.cells = {
        {1, 1, "Документ", std::nullopt}, {2, 1, "", std::nullopt}, {2, 3, " Ω & текст ", std::nullopt}};
    const auto typed_cell = [](std::uint32_t row, std::uint32_t column, model::TypeDomainTerm term,
                               model::PropertyValue value) {
        model::TypeDomainEntry entry;
        entry.term = term;
        if (term == model::TypeDomainTerm::string) entry.string = {100, true};
        if (term == model::TypeDomainTerm::numeric) entry.numeric = {15, 3, false};
        if (term == model::TypeDomainTerm::date) entry.date = {true, true};
        model::TypeDomainPatternValue type{{entry}};
        return model::SpreadsheetDocumentCell{row, column, {},
            model::SpreadsheetDocumentCellValue{std::move(type), std::move(value)}};
    };
    field_payload.cells.push_back(typed_cell(4, 1, model::TypeDomainTerm::string, std::string("Unicode Привет 世界")));
    field_payload.cells.push_back(typed_cell(4, 2, model::TypeDomainTerm::numeric, model::DecimalValue{"-12.375"}));
    field_payload.cells.push_back(typed_cell(4, 3, model::TypeDomainTerm::boolean, false));
    field_payload.cells.push_back(typed_cell(4, 4, model::TypeDomainTerm::date, model::DateValue{"2026-10-04T12:30:45"}));
    field_payload.cells.push_back(typed_cell(6, 1, model::TypeDomainTerm::string, std::string{}));
    field_payload.cells.push_back(typed_cell(6, 2, model::TypeDomainTerm::numeric, model::DecimalValue{"0"}));
    field_payload.cells.push_back(typed_cell(6, 3, model::TypeDomainTerm::date, model::DateValue{"0001-01-01T00:00:00"}));
    auto generic_string = typed_cell(7, 1, model::TypeDomainTerm::string, std::string("x"));
    generic_string.typed_value->type.entries.front().string.length = 37;
    field_payload.cells.push_back(std::move(generic_string));
    auto generic_number = typed_cell(7, 2, model::TypeDomainTerm::numeric, model::DecimalValue{"1.2345"});
    generic_number.typed_value->type.entries.front().numeric = {12, 4, false};
    field_payload.cells.push_back(std::move(generic_number));
    document.add_control(std::move(field));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded.ok() ? "" : encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), decoded.ok() ? "" : decoded.diagnostics().front().message);
    const auto* restored_control = decoded.value().find_control(model::ObjectId{9});
    expect(restored_control != nullptr, "SpreadsheetDocumentField must resolve after storage decode");
    const auto* restored = std::get_if<model::SpreadsheetDocumentFieldPayload>(&restored_control->payload);
    expect(restored && restored->cells.size() == 12 && restored->cells[0].row == 1 &&
        restored->cells[0].column == 1 && restored->cells[0].text == "Документ" &&
        restored->cells[1].row == 2 && restored->cells[1].column == 1 && restored->cells[1].text.empty() &&
        restored->cells[2].row == 2 && restored->cells[2].column == 3 && restored->cells[2].text == " Ω & текст ",
        "SpreadsheetDocumentField cells must preserve sparse coordinates, Unicode, whitespace, and empty content");
    expect(restored && restored->cells[3].typed_value &&
        std::get<std::string>(restored->cells[3].typed_value->value) == "Unicode Привет 世界" &&
        restored->cells[3].typed_value->type.entries.front().string.length == 100 &&
        restored->cells[4].typed_value &&
        std::get<model::DecimalValue>(restored->cells[4].typed_value->value).canonical == "-12.375" &&
        restored->cells[5].typed_value && !std::get<bool>(restored->cells[5].typed_value->value) &&
        restored->cells[6].typed_value &&
        std::get<model::DateValue>(restored->cells[6].typed_value->value).canonical == "2026-10-04T12:30:45",
        "SpreadsheetDocumentField must preserve typed String, Number, Boolean, and Date values and qualifiers");
    const auto& document_info = encoded.value().items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(11);
    expect(list_stream::dump_compact(document_info.items.at(32)) ==
            list_stream::dump_compact(list_stream::parse(R"LS({2,1,{"S","Unicode Привет 世界"}})LS")) &&
        list_stream::dump_compact(document_info.items.at(34)) ==
            list_stream::dump_compact(list_stream::parse(R"LS({2,2,{"N",-12.375}})LS")) &&
        list_stream::dump_compact(document_info.items.at(36)) == "{0,3}" &&
        list_stream::dump_compact(document_info.items.at(38)) ==
            list_stream::dump_compact(list_stream::parse(R"LS({2,4,{"D",20261004123045}})LS")) &&
        list_stream::dump_compact(document_info.items.at(83)) == "{46137344,1,0,0}" &&
        list_stream::dump_compact(document_info.items.at(84)) == "{46137344,1,1,0}" &&
        list_stream::dump_compact(document_info.items.at(85)) == "{46137344,1,2,0}" &&
        list_stream::dump_compact(document_info.items.at(86)) == "{46137344,1,3,0}",
        "typed cell records and fresh TypeDomain references must match the observed named wire profile");

    constexpr std::string_view observed_typed_document_info = R"LS({8,1,12,{"ru","ru",1,1,"ru","Русский","Русский",1},{128,72},{0},0,{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},0,2,3,0,0,2,0,{16,0,{1,1,{"ru","Первый"}},0},2,{16,0,{1,0},0},1,0,2,0,{16,0,{1,1,{"ru","二"}},0},2,{16,0,{1,1,{"ru","Four"}},0},3,0,4,0,{2,1,{"S","Unicode Привет 世界 🌍"}},1,{2,2,{"N",-12.375}},2,{0,3},3,{2,4,{"D",20261004123045}},{4,0,00000000-0000-0000-0000-000000000000,0},4,0,0,0,0,0,0,0,0,{0},{0},{0},{0},"",{{0,6,6,{"N",1000},7,{"N",1000},8,{"N",1000},9,{"N",1000},10,{"N",1000},11,{"N",1000}}},{0,-1,-1,-1,-1,00000000-0000-0000-0000-000000000000},0,0,0,0,0,0,0,1,0,1,4,{46137344,1,0,0},{46137344,1,1,0},{46137344,1,2,0},{46137344,1,3,0},0,0,4,{"Pattern",{"S",100,1}},{"Pattern",{"N",15,3,0}},{"Pattern",{"B"}},{"Pattern",{"D"}},1,381ed624-9217-4e63-85db-c4c3cb87daae,2,{4,3,{-1},3},{4,3,{-3},3},0,0,0,"",0,{3,0,0,100,1,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,"",0,0,0,0,0,0,0},{0},0,0,0,1,0,0,0})LS";
    model::Form observed_form;
    observed_form.id = model::ObjectId{1};
    observed_form.name = "Main";
    observed_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument observed_document(std::move(observed_form));
    model::ControlNode observed_field{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    auto& observed_cells = std::get<model::SpreadsheetDocumentFieldPayload>(observed_field.payload).cells;
    observed_cells = {{1, 1, "Первый", std::nullopt}, {1, 3, "", std::nullopt},
        {2, 1, "二", std::nullopt}, {2, 3, "Four", std::nullopt}};
    observed_cells.push_back(typed_cell(4, 1, model::TypeDomainTerm::string, std::string("Unicode Привет 世界 🌍")));
    observed_cells.push_back(typed_cell(4, 2, model::TypeDomainTerm::numeric, model::DecimalValue{"-12.375"}));
    observed_cells.push_back(typed_cell(4, 3, model::TypeDomainTerm::boolean, false));
    observed_cells.push_back(typed_cell(4, 4, model::TypeDomainTerm::date, model::DateValue{"2026-10-04T12:30:45"}));
    observed_document.add_control(std::move(observed_field));
    auto observed_encoded = form_stream::encode_document(observed_document);
    expect(observed_encoded.ok(), observed_encoded.ok() ? "" : observed_encoded.diagnostics().front().message);
    auto observed_fixture = list_stream::parse(observed_typed_document_info);
    auto& observed_control_record = observed_encoded.value().items.at(1).items.at(2).items.at(2).items.at(1);
    const auto& produced_document_info = observed_control_record.items.at(2).items.at(11);
    const auto produced_observed_info = list_stream::dump_compact(produced_document_info);
    const auto expected_observed_info = list_stream::dump_compact(observed_fixture);
    expect(produced_observed_info == expected_observed_info,
        std::string("typed-cell writer must match the independent platform-observed complete DocumentInfo; got ") +
            produced_observed_info + " expected " + expected_observed_info);
    observed_control_record.items.at(2).items.at(11) = std::move(observed_fixture);
    const auto observed_decoded = form_stream::decode_document(observed_encoded.value(), "Main");
    expect(observed_decoded.ok(), observed_decoded.ok() ? "" : observed_decoded.diagnostics().front().message);
    const auto* observed_control = observed_decoded.value().find_control(model::ObjectId{2});
    const auto* observed_payload = observed_control == nullptr ? nullptr :
        std::get_if<model::SpreadsheetDocumentFieldPayload>(&observed_control->payload);
    expect(observed_payload && observed_payload->cells.size() == 8 &&
        observed_payload->cells[4].typed_value &&
        std::get<std::string>(observed_payload->cells[4].typed_value->value) == "Unicode Привет 世界 🌍" &&
        observed_payload->cells[5].typed_value &&
        std::get<model::DecimalValue>(observed_payload->cells[5].typed_value->value).canonical == "-12.375" &&
        observed_payload->cells[6].typed_value && !std::get<bool>(observed_payload->cells[6].typed_value->value) &&
        observed_payload->cells[7].typed_value &&
        std::get<model::DateValue>(observed_payload->cells[7].typed_value->value).canonical == "2026-10-04T12:30:45",
        "independent full DocumentInfo must decode into named typed cells and exact values");

    model::Form shared_form;
    shared_form.id = model::ObjectId{1};
    shared_form.name = "Main";
    shared_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument shared_document(std::move(shared_form));
    model::ControlNode shared_field{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    std::get<model::SpreadsheetDocumentFieldPayload>(shared_field.payload).cells = {
        typed_cell(1, 1, model::TypeDomainTerm::string, std::string("first")),
        typed_cell(1, 3, model::TypeDomainTerm::string, std::string("second"))};
    shared_document.add_control(std::move(shared_field));
    auto shared_encoded = form_stream::encode_document(shared_document);
    expect(shared_encoded.ok(), shared_encoded.ok() ? "" : shared_encoded.diagnostics().front().message);
    auto& shared_info = shared_encoded.value().items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(11);
    shared_info.items.at(22).items.at(1) = list_stream::ListValue::raw_atom("1");
    shared_info.items.erase(shared_info.items.begin() + 52);
    shared_info.items.at(50) = list_stream::ListValue::raw_atom("1");
    shared_info.items.at(54) = list_stream::ListValue::raw_atom("1");
    shared_info.items.erase(shared_info.items.begin() + 56);
    const auto shared_decoded = form_stream::decode_document(shared_encoded.value(), "Main");
    expect(shared_decoded.ok(), shared_decoded.ok() ? "" : shared_decoded.diagnostics().front().message);
    const auto* shared_control = shared_decoded.value().find_control(model::ObjectId{2});
    const auto* shared_payload = shared_control == nullptr ? nullptr :
        std::get_if<model::SpreadsheetDocumentFieldPayload>(&shared_control->payload);
    expect(shared_payload && shared_payload->cells.size() == 2 &&
        std::get<std::string>(shared_payload->cells[0].typed_value->value) == "first" &&
        std::get<std::string>(shared_payload->cells[1].typed_value->value) == "second" &&
        shared_payload->cells[0].typed_value->type == shared_payload->cells[1].typed_value->type,
        "typed cells may resolve a shared format and ValueType reference by their table mappings");
    expect(restored && restored->cells[7].typed_value &&
        std::get<std::string>(restored->cells[7].typed_value->value).empty() &&
        restored->cells[8].typed_value &&
        std::get<model::DecimalValue>(restored->cells[8].typed_value->value).canonical == "0" &&
        restored->cells[9].typed_value &&
        std::get<model::DateValue>(restored->cells[9].typed_value->value).canonical == "0001-01-01T00:00:00",
        "default typed String, Number, and Date values must survive storage normalization");
    expect(restored && restored->cells[10].typed_value &&
        restored->cells[10].typed_value->type.entries.front().string.length == 37 &&
        restored->cells[11].typed_value &&
        restored->cells[11].typed_value->type.entries.front().numeric == model::NumericQualifiers{12, 4, false},
        "non-default String and Number qualifiers must survive typed cell storage");

    constexpr std::string_view fresh_add_control = R"LS({236a17b3-7f44-46d9-a907-75f9cdc61ab5,2,{18,0,0,0,0,5,5,1,1,{4,4,{0},4},{3,1,{-18},0,0,0},{8,1,12,{"ru","ru",1,1,"ru","Русский","Русский",1},{128,72},{0},0,{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},0,2,2,0,0,1,0,{16,0,{1,1,{"ru","Первый"}},0},1,0,1,2,{16,0,{1,1,{"ru","Второй"}},0},{3,0,00000000-0000-0000-0000-000000000000,0},2,0,0,0,0,0,0,0,0,{0},{0},{0},{0},"",{{0,6,6,{"N",1000},7,{"N",1000},8,{"N",1000},9,{"N",1000},10,{"N",1000},11,{"N",1000}}},{0,-1,-1,-1,-1,00000000-0000-0000-0000-000000000000},0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,{4,3,{-1},3},{4,3,{-3},3},0,0,0,"",0,{3,0,0,100,1,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,"",0,0,0,0,0,0,0},{0},0,0,0,1,0,0,0},0,1,{3,0,0,100,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,"ru",0,0,0,0,0,0,0},1,1,{0},0,0,0,0,0,1,0,1,1,0,0,0,0,1,1},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,0,1,0,0},{14,"Sheet",4294967295,0,0,0},{0}})LS";
    const auto platform_record = list_stream::parse(fresh_add_control);
    model::Form platform_form;
    platform_form.id = model::ObjectId{1};
    platform_form.name = "Main";
    platform_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument platform_document(std::move(platform_form));
    model::ControlNode platform_field{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    std::get<model::SpreadsheetDocumentFieldPayload>(platform_field.payload).cells = {
        {1, 1, "Первый", std::nullopt}, {2, 3, "Второй", std::nullopt}};
    platform_document.add_control(std::move(platform_field));
    const auto platform_encode = form_stream::encode_document(platform_document);
    expect(platform_encode.ok(), platform_encode.ok() ? "" : platform_encode.diagnostics().front().message);
    auto fresh_add_tree = platform_encode.value();
    auto& fresh_add_record = fresh_add_tree.items.at(1).items.at(2).items.at(2).items.at(1);
    fresh_add_record = platform_record;
    const auto fresh_add_decoded = form_stream::decode_document(fresh_add_tree, "Main");
    expect(fresh_add_decoded.ok(), fresh_add_decoded.ok() ? "" : fresh_add_decoded.diagnostics().front().message);
    const auto* fresh_add_control_node = fresh_add_decoded.value().find_control(model::ObjectId{2});
    expect(fresh_add_control_node != nullptr, "fresh platform Add record must decode to a named control");
    const auto* fresh_add_payload = fresh_add_control_node == nullptr ? nullptr :
        std::get_if<model::SpreadsheetDocumentFieldPayload>(&fresh_add_control_node->payload);
    expect(fresh_add_payload && fresh_add_payload->cells.size() == 2 &&
        fresh_add_payload->cells[0].text == "Первый" && fresh_add_payload->cells[1].row == 2 &&
        fresh_add_payload->cells[1].column == 3 && fresh_add_payload->cells[1].text == "Второй",
        "independent fresh Add record must normalize its unexposed fresh view envelope");

    constexpr std::string_view normalized_default_record = R"LS({236a17b3-7f44-46d9-a907-75f9cdc61ab5,2,{18,0,0,0,0,5,5,1,1,{4,4,{0},4},{3,1,{-18},0,0,0},{8,1,12,{"ru","ru",1,1,"ru","Русский","Русский",1},{128,72},{0},0,{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},0,2,1,0,0,1,0,{16,0,{1,1,{"ru","Текст"}},0},{1,0,00000000-0000-0000-0000-000000000000,0},1,0,0,0,0,0,0,0,0,{0},{0},{0},{0},"",{{0,6,6,{"N",1000},7,{"N",1000},8,{"N",1000},9,{"N",1000},10,{"N",1000},11,{"N",1000}}},{0,-1,-1,-1,-1,00000000-0000-0000-0000-000000000000},0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,{4,3,{-1},3},{4,3,{-3},3},0,0,0,"",0,{3,0,0,100,1,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,"",0,0,0,0,0,0,0},{0},0,0,0,1,0,0,0},0,1,{3,0,0,100,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,"ru",0,1,{3,0,0,0,0,00000000-0000-0000-0000-000000000000},0,0,0,0,0},1,1,{0},0,0,0,0,0,1,0,1,1,0,0,0,0,1,1},{8,0,0,0,0,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},0,0,0,0,0,0,0,0,1,0,0},{14,"Sheet",4294967295,0,0,0},{0}})LS";
    model::Form normalized_form;
    normalized_form.id = model::ObjectId{1};
    normalized_form.name = "Main";
    normalized_form.children = {model::ControlRef{model::ObjectId{2}}};
    model::OrdinaryFormDocument normalized_document(std::move(normalized_form));
    model::ControlNode normalized_field{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    std::get<model::SpreadsheetDocumentFieldPayload>(normalized_field.payload).cells = {{1, 1, "Текст", std::nullopt}};
    normalized_document.add_control(std::move(normalized_field));
    const auto normalized_encoded = form_stream::encode_document(normalized_document);
    expect(normalized_encoded.ok(), normalized_encoded.ok() ? "" : normalized_encoded.diagnostics().front().message);
    const auto normalized_record = list_stream::parse(normalized_default_record);
    const auto& normalized_writer_record = normalized_encoded.value().items.at(1).items.at(2).items.at(2).items.at(1);
    expect(list_stream::dump_compact(normalized_writer_record) == list_stream::dump_compact(normalized_record),
        "writer must match the independently captured Designer-normalized R1C1 selection record exactly");
    auto normalized_tree = normalized_encoded.value();
    normalized_tree.items.at(1).items.at(2).items.at(2).items.at(1) = normalized_record;
    const auto normalized_decoded = form_stream::decode_document(normalized_tree, "Main");
    expect(normalized_decoded.ok(), normalized_decoded.ok() ? "" : normalized_decoded.diagnostics().front().message);
    const auto* normalized_control = normalized_decoded.value().find_control(model::ObjectId{2});
    const auto* normalized_payload = normalized_control == nullptr ? nullptr :
        std::get_if<model::SpreadsheetDocumentFieldPayload>(&normalized_control->payload);
    expect(normalized_payload && normalized_payload->cells == std::vector<model::SpreadsheetDocumentCell>{{1, 1, "Текст", std::nullopt}},
        "independent normalized R1C1 record must decode to the same named cells as fresh Add");

    const auto expect_partial_view_profile = [&](const list_stream::ListValue& changed,
                                                  std::string_view explanation) {
        const auto partial = form_stream::decode_document(changed, "Main");
        const auto* partial_control = partial ? partial.value().find_control(model::ObjectId{9}) : nullptr;
        const auto* partial_payload = partial_control == nullptr ? nullptr :
            std::get_if<model::SpreadsheetDocumentFieldPayload>(&partial_control->payload);
        expect(partial.ok() && !partial.value().reconstruction_complete() &&
                   std::any_of(partial.diagnostics().begin(), partial.diagnostics().end(),
                       [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
                   partial_payload != nullptr && partial_payload->cells == restored->cells,
            explanation);
    };

    auto unsupported_row_flags = encoded.value();
    auto& field_record = unsupported_row_flags.items.at(1).items.at(2).items.at(2).items.at(1);
    auto& field_info = field_record.items.at(2).items.at(11);
    expect(field_info.items.size() > 17 && !field_info.items.at(17).is_list,
        "Spreadsheet Document row fixture must expose flat named row flags");
    field_info.items.at(17) = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(unsupported_row_flags, "Main").ok(),
        "SpreadsheetDocumentField must reject nondefault row flags");

    auto excessive_row_count = encoded.value();
    auto& excessive_row_record = excessive_row_count.items.at(1).items.at(2).items.at(2).items.at(1);
    excessive_row_record.items.at(2).items.at(11).items.at(15) =
        list_stream::ListValue::raw_atom("4294967295");
    expect(!form_stream::decode_document(excessive_row_count, "Main").ok(),
        "SpreadsheetDocumentField must reject an unrepresentable storage row count before iterating");

    auto excessive_cell_count = encoded.value();
    auto& excessive_cell_record = excessive_cell_count.items.at(1).items.at(2).items.at(2).items.at(1);
    excessive_cell_record.items.at(2).items.at(11).items.at(18) =
        list_stream::ListValue::raw_atom("4294967295");
    expect(!form_stream::decode_document(excessive_cell_count, "Main").ok(),
        "SpreadsheetDocumentField must reject an unrepresentable storage cell count before iterating");

    auto bad_typed_reference = encoded.value();
    auto& bad_reference_info = bad_typed_reference.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(11);
    bad_reference_info.items.at(32).items.at(1) = list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(bad_typed_reference, "Main").ok(),
        "SpreadsheetDocumentField must reject a reference conflicting with its typed value");
    for (const std::string invalid_decimal : {"1e3", "bad", "0.00", "-0"}) {
        auto malformed_number = encoded.value();
        auto& malformed_info = malformed_number.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(11);
        malformed_info.items.at(34).items.at(2).items.at(1) = list_stream::ListValue::raw_atom(invalid_decimal);
        const auto rejected_number = form_stream::decode_document(malformed_number, "Main");
        expect(!rejected_number.ok() &&
            rejected_number.diagnostics().front().message.find("canonical decimal") != std::string::npos,
            "typed Number decode must reject noncanonical atoms before producing an unserializable DecimalValue");
        model::Form invalid_form;
        invalid_form.id = model::ObjectId{1};
        invalid_form.name = "Main";
        invalid_form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
        model::ControlNode invalid_field{model::ObjectId{2}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
        std::get<model::SpreadsheetDocumentFieldPayload>(invalid_field.payload).cells = {
            typed_cell(1, 1, model::TypeDomainTerm::numeric, model::DecimalValue{invalid_decimal})};
        invalid_document.add_control(std::move(invalid_field));
        expect(!form_stream::encode_document(invalid_document).ok(),
            "typed Number encode must reject a noncanonical DecimalValue without rounding");
    }
    auto unknown_type_reference = encoded.value();
    auto& unknown_type_info = unknown_type_reference.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(11);
    unknown_type_info.items.at(83).items.at(0) = list_stream::ListValue::raw_atom("46137345");
    expect(!form_stream::decode_document(unknown_type_reference, "Main").ok(),
        "SpreadsheetDocumentField must reject an unknown typed-value reference marker");
    auto unknown_document_tail = encoded.value();
    auto& unknown_tail_info = unknown_document_tail.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(11);
    unknown_tail_info.items.at(57) = list_stream::ListValue::raw_atom("9");
    expect_partial_view_profile(unknown_document_tail,
        "SpreadsheetDocumentField unknown document-tail profile value must warn and preserve known cells");
    auto excessive_typed_count = encoded.value();
    auto& excessive_typed_info = excessive_typed_count.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(11);
    excessive_typed_info.items.at(82) = list_stream::ListValue::raw_atom("4294967295");
    expect(!form_stream::decode_document(excessive_typed_count, "Main").ok(),
        "SpreadsheetDocumentField must reject an excessive typed-value count");
    auto excessive_domain_count = encoded.value();
    auto& excessive_domain_info = excessive_domain_count.items.at(1).items.at(2).items.at(2).items.at(1).items.at(2).items.at(11);
    excessive_domain_info.items.at(94) = list_stream::ListValue::raw_atom("4294967295");
    expect(!form_stream::decode_document(excessive_domain_count, "Main").ok(),
        "SpreadsheetDocumentField must reject an excessive ValueType count");

    auto unsupported_area_marker = encoded.value();
    auto& marker_record = unsupported_area_marker.items.at(1).items.at(2).items.at(2).items.at(1);
    marker_record.items.at(2).items.at(14).items.at(24).items.at(0) = list_stream::ListValue::raw_atom("4");
    expect_partial_view_profile(unsupported_area_marker,
        "SpreadsheetDocumentField unknown view marker must warn and preserve known cells");
    auto unsupported_columns_id = encoded.value();
    auto& columns_id_record = unsupported_columns_id.items.at(1).items.at(2).items.at(2).items.at(1);
    columns_id_record.items.at(2).items.at(14).items.at(24).items.at(5) =
        list_stream::ListValue::raw_atom("11111111-1111-1111-1111-111111111111");
    expect_partial_view_profile(unsupported_columns_id,
        "SpreadsheetDocumentField unknown view columns ID must warn and preserve known cells");
    auto nondefault_view = encoded.value();
    auto& nondefault_record = nondefault_view.items.at(1).items.at(2).items.at(2).items.at(1);
    nondefault_record.items.at(2).items.at(14).items.at(1) = list_stream::ListValue::raw_atom("1");
    expect_partial_view_profile(nondefault_view,
        "SpreadsheetDocumentField unknown current-cell view data must warn and preserve known cells");
    auto unsupported_view_setting = encoded.value();
    auto& setting_record = unsupported_view_setting.items.at(1).items.at(2).items.at(2).items.at(1);
    setting_record.items.at(2).items.at(14).items.at(22) = list_stream::ListValue::raw_atom("1");
    expect_partial_view_profile(unsupported_view_setting,
        "SpreadsheetDocumentField unknown view setting must warn and preserve known cells");
    auto truncated_areas = encoded.value();
    auto& truncated_record = truncated_areas.items.at(1).items.at(2).items.at(2).items.at(1);
    truncated_record.items.at(2).items.at(14).items.pop_back();
    expect(!form_stream::decode_document(truncated_areas, "Main").ok(),
        "SpreadsheetDocumentField must reject a selection count without complete rectangle records");
}

void test_two_input_fields_round_trip() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{9}},
        model::ControlRef{model::ObjectId{12}},
    };
    model::OrdinaryFormDocument document(std::move(form));

    model::TypeDomainPatternValue string64;
    model::TypeDomainEntry string64_entry;
    string64_entry.term = model::TypeDomainTerm::string;
    string64_entry.string = model::LengthQualifiers{64, false};
    string64.entries.push_back(string64_entry);
    model::TypeDomainPatternValue string20;
    model::TypeDomainEntry string20_entry;
    string20_entry.term = model::TypeDomainTerm::string;
    string20_entry.string = model::LengthQualifiers{20, true};
    string20.entries.push_back(string20_entry);
    model::Attribute second_attribute{model::ObjectId{7}, "SecondValue", string20};
    second_attribute.main.set(true);
    document.add_attribute(std::move(second_attribute));
    model::Attribute first_attribute{model::ObjectId{1}, "FirstValue", string64};
    first_attribute.main.set(false);
    document.add_attribute(std::move(first_attribute));

    model::ControlNode second{model::ObjectId{12}, "SecondInput", model::InputFieldPayload{}};
    second.data_path = model::DataPath{model::AttributeRef{model::ObjectId{7}}, {}};
    second.properties().set_explicit(model::PropertyId::from_name("Enabled"), true);
    second.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), false);
    document.add_control(std::move(second));
    model::ControlNode first{model::ObjectId{9}, "FirstInput", model::InputFieldPayload{}};
    first.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    first.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    first.properties().set_explicit(model::PropertyId::from_name("ReadOnly"), true);
    first.position.left.set(242);
    first.position.top.set(146);
    document.add_control(std::move(first));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "two InputFields with two linked Attributes must encode" : encoded.diagnostics().front().message);
    const auto& children = encoded.value().items[1].items[2].items[2];
    expect(children.items[1].items[1].atom == "9" && children.items[2].items[1].atom == "12",
        "two InputFields must preserve Form.children order");
    expect(children.items[1].items[3].items[geometry_tail_start(children.items[1].items[3]) + 1].atom == "0" && children.items[1].items[3].items[geometry_tail_start(children.items[1].items[3]) + 2].atom == "1" &&
               children.items[2].items[3].items[geometry_tail_start(children.items[2].items[3]) + 1].atom == "1" && children.items[2].items[3].items[geometry_tail_start(children.items[2].items[3]) + 2].atom == "2",
        "two InputFields must use sequential sibling geometry references");
    const auto& links = encoded.value().items[2].items[3];
    expect(links.items[1].items[0].atom == "9" && links.items[2].items[0].atom == "12",
        "InputField links must follow Form.children order instead of collection insertion order");
    expect(encoded.value().items[2].items[1].atom == "8",
        "sparse Attribute IDs must determine the Attribute slot count");

    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    expect(decoded.value().form().children.size() == 2 &&
               std::get<model::ControlRef>(decoded.value().form().children[0]).id() == model::ObjectId{9} &&
               std::get<model::ControlRef>(decoded.value().form().children[1]).id() == model::ObjectId{12},
        "two InputFields must decode in child order");
    const auto* decoded_first = decoded.value().find_control(model::ObjectId{9});
    const auto* decoded_second = decoded.value().find_control(model::ObjectId{12});
    expect(decoded_first->data_path->attribute.id() == model::ObjectId{1} &&
               decoded_second->data_path->attribute.id() == model::ObjectId{7},
        "each InputField must resolve its own attribute link");
    expect(decoded.value().find_attribute(model::ObjectId{1})->type == string64 &&
               decoded.value().find_attribute(model::ObjectId{7})->type == string20 &&
               !decoded.value().find_attribute(model::ObjectId{1})->main.value() &&
               decoded.value().find_attribute(model::ObjectId{7})->main.value(),
        "sparse Attribute IDs, types, and Main flags must survive decode");
    const auto property_bool_or = [](const model::ControlNode& control, std::string_view name, bool fallback) {
        const auto* property = control.properties().find(model::PropertyId::from_name(name));
        return property == nullptr ? fallback : std::get<bool>(property->value);
    };
    expect(!property_bool_or(*decoded_first, "Enabled", true) &&
               property_bool_or(*decoded_first, "ReadOnly", false) &&
               property_bool_or(*decoded_second, "Enabled", true) &&
               !property_bool_or(*decoded_second, "ReadOnly", false),
        "each InputField must preserve independent Enabled and ReadOnly values");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok(), "decoded two-InputField document must re-encode");
    const auto redecode = form_stream::decode_document(reencoded.value(), "Main");
    expect(redecode.ok() && redecode.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1} &&
               redecode.value().find_control(model::ObjectId{12})->data_path->attribute.id() == model::ObjectId{7},
        "both InputField links must survive two round-trips");

    auto duplicate_link = encoded.value();
    duplicate_link.items[2].items[3].items[2] = duplicate_link.items[2].items[3].items[1];
    expect_failure(form_stream::decode_document(duplicate_link, "Main"), "OOF1122", "$/2/3",
        "duplicate control links must be rejected as ambiguous");

    auto missing_link = encoded.value();
    missing_link.items[2].items[3].items.pop_back();
    missing_link.items[2].items[3].items[0] = list_stream::ListValue::raw_atom("1");
    expect_failure(form_stream::decode_document(missing_link, "Main"), "OOF1122", "$/2/3",
        "an InputField without a DataPath link must be rejected");

    auto leftover_link = encoded.value();
    auto extra_link = leftover_link.items[2].items[3].items[1];
    extra_link.items[0] = list_stream::ListValue::raw_atom("99");
    leftover_link.items[2].items[3].items.push_back(std::move(extra_link));
    leftover_link.items[2].items[3].items[0] = list_stream::ListValue::raw_atom("3");
    expect_failure(form_stream::decode_document(leftover_link, "Main"), "OOF1122", "$/2/3",
        "links without a matching InputField must be rejected");

    auto dangling_target = encoded.value();
    dangling_target.items[2].items[3].items[1].items[1].items[1].items[0] =
        list_stream::ListValue::raw_atom("99");
    expect_failure(form_stream::decode_document(dangling_target, "Main"), "OOF1122", "$/2/3",
        "links to missing Attributes must be rejected");

    auto swapped_links = encoded.value();
    std::swap(swapped_links.items[2].items[3].items[1], swapped_links.items[2].items[3].items[2]);
    const auto decoded_swapped_links = form_stream::decode_document(swapped_links, "Main");
    expect(decoded_swapped_links.ok(), "reordered InputField links must decode");
    const auto* swapped_first = decoded_swapped_links.value().find_control(model::ObjectId{9});
    const auto* swapped_second = decoded_swapped_links.value().find_control(model::ObjectId{12});
    expect(swapped_first != nullptr && swapped_second != nullptr &&
               swapped_first->data_path.has_value() && swapped_second->data_path.has_value() &&
               swapped_first->data_path->attribute.id() == model::ObjectId{1} &&
               swapped_second->data_path->attribute.id() == model::ObjectId{7},
        "InputField links must resolve by control ID regardless of table order");

    auto wrong_geometry = encoded.value();
    wrong_geometry.items[1].items[2].items[2].items[2].items[3].items[geometry_tail_start(wrong_geometry.items[1].items[2].items[2].items[2].items[3]) + 2] =
        list_stream::ListValue::raw_atom("4");
    expect_failure(form_stream::decode_document(wrong_geometry, "Main"), "OOF1114", "$/1/2/2/2/3/20",
        "unknown two-InputField sibling geometry must fail closed");

    auto truncated_input = encoded.value();
    const auto input_guid = truncated_input.items[1].items[2].items[2].items[1].items[0];
    truncated_input.items[1].items[2].items[2].items[1] =
        list_stream::ListValue::list({input_guid});
    expect_failure(form_stream::decode_document(truncated_input, "Main"), "OOF1103", "$/1/2/2/1/1",
        "truncated InputField record must report the missing ID slot");

    model::Form mixed_form;
    mixed_form.id = model::ObjectId{1};
    mixed_form.name = "Mixed";
    mixed_form.children = {
        model::ControlRef{model::ObjectId{4}}, model::ControlRef{model::ObjectId{5}},
        model::ControlRef{model::ObjectId{6}}, model::ControlRef{model::ObjectId{18}},
        model::ControlRef{model::ObjectId{21}}, model::ControlRef{model::ObjectId{8}},
        model::ControlRef{model::ObjectId{13}}, model::ControlRef{model::ObjectId{24}},
    };
    model::OrdinaryFormDocument mixed_document(std::move(mixed_form));
    mixed_document.add_attribute(model::Attribute{model::ObjectId{1}, "ValueA", string64});
    mixed_document.add_attribute(model::Attribute{model::ObjectId{3}, "ValueB", string64});
    model::TypeDomainPatternValue boolean_type;
    model::TypeDomainEntry boolean_entry;
    boolean_entry.term = model::TypeDomainTerm::boolean;
    boolean_type.entries.push_back(boolean_entry);
    mixed_document.add_attribute(model::Attribute{model::ObjectId{5}, "SharedFlag", boolean_type});
    mixed_document.add_attribute(model::Attribute{model::ObjectId{21}, "SeparateFlag", boolean_type});
    model::ControlNode first_input{model::ObjectId{4}, "InputA", model::InputFieldPayload{}};
    first_input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    mixed_document.add_control(std::move(first_input));
    model::ControlNode first_button{model::ObjectId{5}, "Run", model::ButtonPayload{}};
    first_button.events.push_back(model::EventRef{model::ObjectId{30}});
    mixed_document.add_event(model::Event{model::ObjectId{30}, "Click", "RunProbe", model::ControlRef{model::ObjectId{5}}});
    mixed_document.add_control(std::move(first_button));
    mixed_document.add_control(model::ControlNode{model::ObjectId{6}, "Label", model::LabelDecorationPayload{}});
    model::ControlNode second_input{model::ObjectId{18}, "InputB", model::InputFieldPayload{}};
    second_input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
    mixed_document.add_control(std::move(second_input));
    model::ControlNode second_button{model::ObjectId{21}, "Cancel", model::ButtonPayload{}};
    second_button.events.push_back(model::EventRef{model::ObjectId{31}});
    mixed_document.add_event(model::Event{model::ObjectId{31}, "Click", "CancelProbe", model::ControlRef{model::ObjectId{21}}});
    mixed_document.add_control(std::move(second_button));
    model::ControlNode shared_flag_a{model::ObjectId{8}, "SharedFlagA", model::CheckBoxPayload{}};
    shared_flag_a.data_path = model::DataPath{model::AttributeRef{model::ObjectId{5}}, {}};
    shared_flag_a.position.left.set(68);
    shared_flag_a.position.top.set(82);
    shared_flag_a.position.width.set(150);
    shared_flag_a.position.height.set(25);
    shared_flag_a.properties().set_explicit(model::PropertyId::from_name("Caption"), std::string("Flag A"));
    mixed_document.add_control(std::move(shared_flag_a));
    model::ControlNode shared_flag_b{model::ObjectId{13}, "SharedFlagB", model::CheckBoxPayload{}};
    shared_flag_b.data_path = model::DataPath{model::AttributeRef{model::ObjectId{5}}, {}};
    shared_flag_b.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
    mixed_document.add_control(std::move(shared_flag_b));
    model::ControlNode separate_flag{model::ObjectId{24}, "SeparateFlag", model::CheckBoxPayload{}};
    separate_flag.data_path = model::DataPath{model::AttributeRef{model::ObjectId{21}}, {}};
    mixed_document.add_control(std::move(separate_flag));

    const std::vector<model::ObjectId> expected_order{
        model::ObjectId{4}, model::ObjectId{5}, model::ObjectId{6}, model::ObjectId{18},
        model::ObjectId{21}, model::ObjectId{8}, model::ObjectId{13}, model::ObjectId{24}};
    const auto mixed_encoded = form_stream::encode_document(mixed_document);
    expect(mixed_encoded.ok(), "mixed supported controls must encode in ChildItems order");
    const auto& mixed_child_records = mixed_encoded.value().items[1].items[2].items[2];
    for (std::size_t index = 0; index < expected_order.size(); ++index) {
        const auto& record = mixed_child_records.items[index + 1];
        const auto& geometry = record.items[3];
        const auto control_id = record.items[1].atom;
        expect(geometry.items.size() == 23,
            "unbound controls must use the 23-field geometry form without implicit self-links");
        for (std::size_t slot = 6; slot < 12; ++slot) {
            expect(list_stream::dump_compact(geometry.items[slot]) == "{0,{2,-1,6,0},{2,-1,6,0}}",
                "unbound geometry slots must use the explicit empty-binding sentinel");
        }
        const auto logical_position = std::ranges::find(
            expected_order, model::ObjectId{std::stoull(control_id)});
        expect(logical_position != expected_order.end(),
            "every stored record must map to a logical ChildItems position");
        const auto logical_index = static_cast<std::size_t>(
            std::distance(expected_order.begin(), logical_position));
        expect(geometry.items[geometry_tail_start(geometry) + 1].atom == std::to_string(logical_index) &&
                   geometry.items[geometry_tail_start(geometry) + 2].atom == std::to_string(logical_index + 1),
            "mixed control geometry sibling indexes must derive from ChildItems order");
    }
    const auto mixed_decoded = form_stream::decode_document(mixed_encoded.value(), "Mixed");
    expect(mixed_decoded.ok(), "mixed supported controls and DataPaths must decode");
    expect(mixed_decoded.value().form().children.size() == expected_order.size(), "all mixed ChildItems must survive");
    for (std::size_t index = 0; index < expected_order.size(); ++index) {
        expect(std::get<model::ControlRef>(mixed_decoded.value().form().children[index]).id() == expected_order[index],
            "mixed ChildItems order must survive round-trip");
    }
    expect(mixed_decoded.value().find_control(model::ObjectId{4})->data_path->attribute.id() == model::ObjectId{1} &&
               mixed_decoded.value().find_control(model::ObjectId{18})->data_path->attribute.id() == model::ObjectId{3},
        "InputField DataPaths must resolve by control ID in mixed order");
    expect(mixed_decoded.value().find_control(model::ObjectId{8})->data_path->attribute.id() == model::ObjectId{5} &&
               mixed_decoded.value().find_control(model::ObjectId{13})->data_path->attribute.id() == model::ObjectId{5} &&
               mixed_decoded.value().find_control(model::ObjectId{24})->data_path->attribute.id() == model::ObjectId{21},
        "CheckBox links must resolve shared and separate Boolean attributes even when IDs overlap controls");
    expect(mixed_decoded.value().find_attribute(model::ObjectId{5})->type == boolean_type &&
               mixed_decoded.value().find_attribute(model::ObjectId{21})->type == boolean_type,
        "CheckBox Boolean attribute TypeDomain must survive round-trip");
    expect(mixed_decoded.value().find_control(model::ObjectId{13})->properties().find(
               model::PropertyId::from_name("Enabled")) != nullptr &&
               !std::get<bool>(mixed_decoded.value().find_control(model::ObjectId{13})->properties().find(
                   model::PropertyId::from_name("Enabled"))->value),
        "CheckBox Enabled must use its proven base property");
    expect(mixed_decoded.value().find_event(mixed_decoded.value().find_control(model::ObjectId{5})->events.front().id())->handler == "RunProbe" &&
               mixed_decoded.value().find_event(mixed_decoded.value().find_control(model::ObjectId{21})->events.front().id())->handler == "CancelProbe",
        "each mixed-order Button handler must remain attached to its owner");

    auto wrong_checkbox_target = mixed_encoded.value();
    wrong_checkbox_target.items[2].items[3].items[2].items[1].items[1].items[0] =
        list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(wrong_checkbox_target, "Mixed"),
        "CheckBox linked to a non-Boolean Attribute must be rejected");

    const auto rejects_checkbox_model = [](model::TypeDomainPatternValue type,
                                           std::vector<std::string> members = {},
                                           bool add_unsupported_property = false) {
        model::Form invalid_form;
        invalid_form.id = model::ObjectId{1};
        invalid_form.name = "InvalidCheckBox";
        invalid_form.children.push_back(model::ControlRef{model::ObjectId{8}});
        model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
        invalid_document.add_attribute(model::Attribute{model::ObjectId{8}, "Flag", std::move(type)});
        model::ControlNode invalid_checkbox{model::ObjectId{8}, "Flag", model::CheckBoxPayload{}};
        invalid_checkbox.data_path = model::DataPath{model::AttributeRef{model::ObjectId{8}}, std::move(members)};
        if (add_unsupported_property) {
            invalid_checkbox.properties().set_explicit(
                model::PropertyId::from_name("ThreeState"), true);
        }
        invalid_document.add_control(std::move(invalid_checkbox));
        return !form_stream::encode_document(invalid_document);
    };
    expect(rejects_checkbox_model(string64),
        "CheckBox must reject a string attribute target");
    expect(rejects_checkbox_model(boolean_type, {"Nested"}),
        "CheckBox must reject nested DataPath members");
    expect(rejects_checkbox_model(boolean_type, {}, true),
        "CheckBox must reject an unproven property");

    auto unsupported_checkbox_geometry = mixed_encoded.value();
    const auto check_box_record = std::ranges::find(
        unsupported_checkbox_geometry.items[1].items[2].items[2].items, std::string("8"),
        [](const list_stream::ListValue& item) { return item.items.size() > 1 ? item.items[1].atom : std::string{}; });
    expect(check_box_record != unsupported_checkbox_geometry.items[1].items[2].items[2].items.end(),
        "mixed stream must contain the CheckBox geometry record");
    check_box_record->items[3].items[6].items[2] = list_stream::parse("{2,-1,6,7}");
    expect_failure(form_stream::decode_document(unsupported_checkbox_geometry, "Mixed"), "OOF1114",
        "$/1/2/2/4/3/6/2", "CheckBox must reject a non-sentinel secondary tuple");

    auto changed_button_index = mixed_encoded.value();
    changed_button_index.items[1].items[2].items[2].items[2].items[3].items[geometry_tail_start(changed_button_index.items[1].items[2].items[2].items[2].items[3]) + 2] =
        list_stream::ListValue::raw_atom("9");
    expect_failure(form_stream::decode_document(changed_button_index, "Mixed"), "OOF1114", "$/1/2/2/2/3/20",
        "Button must reject geometry sibling indexes that differ from ChildItems");

    auto unsupported_action_text = mixed_encoded.value();
    auto& first_action = unsupported_action_text.items[1].items[2].items[2].items[2]
        .items[2].items[2].items[1].items[2].items[2];
    first_action.items[2] = list_stream::parse("{1,1,{\"ru\",\"different presentation\"}}");
    const auto partial_action_text = form_stream::decode_document(unsupported_action_text, "Mixed");
    const auto* partial_action_button = partial_action_text
        ? partial_action_text.value().find_control(model::ObjectId{5}) : nullptr;
    const auto* partial_action_event = partial_action_button && !partial_action_button->events.empty()
        ? partial_action_text.value().find_event(partial_action_button->events.front().id()) : nullptr;
    expect(partial_action_text.ok() && !partial_action_text.value().reconstruction_complete() &&
               std::any_of(partial_action_text.diagnostics().begin(), partial_action_text.diagnostics().end(),
                   [](const auto& diagnostic) {
                       return diagnostic.code == "OOF1140" &&
                           diagnostic.severity == oof::DiagnosticSeverity::warning;
                   }) && partial_action_event != nullptr && partial_action_event->handler == "RunProbe",
        "valid noncanonical Button Action presentation must warn while retaining its handler");
}

void test_two_button_sibling_index() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Main";
    form.children = {
        model::ControlRef{model::ObjectId{2}},
        model::ControlRef{model::ObjectId{4}},
    };
    model::OrdinaryFormDocument document(std::move(form));
    document.add_control(model::ControlNode{model::ObjectId{2}, "First", model::ButtonPayload{}});
    document.add_control(model::ControlNode{model::ObjectId{4}, "Second", model::ButtonPayload{}});

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "two Buttons must encode with derived sibling indexes");
    const auto decoded = form_stream::decode_document(encoded.value(), "Main");
    expect(decoded.ok() && decoded.value().collections().controls.size() == 2,
        "two Buttons must decode with matching sibling indexes");

    auto wrong_sibling_index = encoded.value();
    wrong_sibling_index.items[1].items[2].items[2].items[2].items[3].items[geometry_tail_start(wrong_sibling_index.items[1].items[2].items[2].items[2].items[3]) + 1] =
        list_stream::ListValue::raw_atom("0");
    expect_failure(
        form_stream::decode_document(wrong_sibling_index, "Main"),
        "OOF1114",
        "$/1/2/2/2/3/19",
        "second Button must reject a sibling index of zero");
}

void test_six_reordered_controls_use_logical_geometry_ordinals() {
    model::TypeDomainPatternValue string64;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{64, false};
    string64.entries.push_back(string_entry);

    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Reordered";
    const std::vector<model::ObjectId> logical_order{
        model::ObjectId{27}, model::ObjectId{18}, model::ObjectId{21},
        model::ObjectId{15}, model::ObjectId{9}, model::ObjectId{12}};
    for (const auto id : logical_order) form.children.push_back(model::ControlRef{id});
    model::OrdinaryFormDocument document(std::move(form));
    document.add_attribute(model::Attribute{model::ObjectId{3}, "ValueB", string64});
    document.add_attribute(model::Attribute{model::ObjectId{1}, "ValueA", string64});

    model::ControlNode input18{model::ObjectId{18}, "InputB", model::InputFieldPayload{}};
    input18.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
    document.add_control(std::move(input18));
    model::ControlNode button21{model::ObjectId{21}, "Cancel", model::ButtonPayload{}};
    button21.events.push_back(model::EventRef{model::ObjectId{41}});
    document.add_event(model::Event{model::ObjectId{41}, "Click", "CancelProbe", model::ControlRef{model::ObjectId{21}}});
    document.add_control(std::move(button21));
    document.add_control(model::ControlNode{model::ObjectId{27}, "LabelTop", model::LabelDecorationPayload{}});
    model::ControlNode input9{model::ObjectId{9}, "InputA", model::InputFieldPayload{}};
    input9.data_path = model::DataPath{model::AttributeRef{model::ObjectId{1}}, {}};
    document.add_control(std::move(input9));
    model::ControlNode button12{model::ObjectId{12}, "Run", model::ButtonPayload{}};
    button12.events.push_back(model::EventRef{model::ObjectId{42}});
    document.add_event(model::Event{model::ObjectId{42}, "Click", "RunProbe", model::ControlRef{model::ObjectId{12}}});
    document.add_control(std::move(button12));
    document.add_control(model::ControlNode{model::ObjectId{15}, "LabelBottom", model::LabelDecorationPayload{}});

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), "six reordered controls must encode");
    const auto& children = encoded.value().items[1].items[2].items[2];
    const std::vector<std::uint64_t> physical_ids{9, 12, 15, 18, 21, 27};
    const std::vector<std::uint32_t> physical_ordinals{4, 5, 3, 1, 2, 0};
    for (std::size_t index = 0; index < physical_ids.size(); ++index) {
        const auto& record = children.items[index + 1];
        expect(std::stoull(record.items[1].atom) == physical_ids[index],
            "writer must sort physical control records by numeric ID");
        expect(record.items[3].items[geometry_tail_start(record.items[3]) + 1].atom == std::to_string(physical_ordinals[index]) &&
                   record.items[3].items[geometry_tail_start(record.items[3]) + 2].atom == std::to_string(physical_ordinals[index] + 1),
            "physical control records must retain logical geometry ordinals");
    }
    const auto& links = encoded.value().items[2].items[3];
    expect(links.items[1].items[0].atom == "9" && links.items[2].items[0].atom == "18",
        "writer must sort InputField links by numeric control ID");

    const auto verify_logical = [&](const auto& storage, std::string_view message) {
        const auto decoded = form_stream::decode_document(storage, "Reordered");
        expect(decoded.ok(), decoded ? message : decoded.diagnostics().front().message);
        expect(decoded.value().form().children.size() == logical_order.size(), message);
        for (std::size_t index = 0; index < logical_order.size(); ++index) {
            expect(std::get<model::ControlRef>(decoded.value().form().children[index]).id() == logical_order[index], message);
        }
        expect(decoded.value().find_control(model::ObjectId{18})->data_path->attribute.id() == model::ObjectId{3} &&
                   decoded.value().find_control(model::ObjectId{9})->data_path->attribute.id() == model::ObjectId{1},
            "sorted InputField links must still resolve by control ID");
        expect(decoded.value().find_event(decoded.value().find_control(model::ObjectId{21})->events.front().id())->handler == "CancelProbe" &&
                   decoded.value().find_event(decoded.value().find_control(model::ObjectId{12})->events.front().id())->handler == "RunProbe",
            "reordered Button handlers must remain with their owners");
    };
    verify_logical(encoded.value(), "sorted physical records must decode in logical ChildItems order");

    auto permuted = encoded.value();
    std::swap(permuted.items[1].items[2].items[2].items[1], permuted.items[1].items[2].items[2].items[6]);
    verify_logical(permuted, "noncanonical physical record permutation must preserve logical ChildItems order");

    auto duplicate_ordinal = encoded.value();
    auto& duplicate_geometry = duplicate_ordinal.items[1].items[2].items[2].items[6].items[3];
    const auto duplicate_tail = geometry_tail_start(duplicate_geometry);
    duplicate_geometry.items[duplicate_tail + 1] = list_stream::ListValue::raw_atom("4");
    duplicate_geometry.items[duplicate_tail + 2] = list_stream::ListValue::raw_atom("5");
    expect_failure(form_stream::decode_document(duplicate_ordinal, "Reordered"), "OOF1114", "$/1/2/2/6/3/19",
        "duplicate geometry ordinals must be rejected");

    auto out_of_range_ordinal = encoded.value();
    auto& out_of_range_geometry = out_of_range_ordinal.items[1].items[2].items[2].items[6].items[3];
    const auto out_of_range_tail = geometry_tail_start(out_of_range_geometry);
    out_of_range_geometry.items[out_of_range_tail + 1] = list_stream::ListValue::raw_atom("6");
    out_of_range_geometry.items[out_of_range_tail + 2] = list_stream::ListValue::raw_atom("7");
    expect_failure(form_stream::decode_document(out_of_range_ordinal, "Reordered"), "OOF1114", "$/1/2/2/6/3/19",
        "out-of-range geometry ordinals must be rejected");

    auto wrong_next_index = encoded.value();
    wrong_next_index.items[1].items[2].items[2].items[6].items[3].items[geometry_tail_start(wrong_next_index.items[1].items[2].items[2].items[6].items[3]) + 2] =
        list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(wrong_next_index, "Reordered"), "OOF1114", "$/1/2/2/4/3/20",
        "duplicate page-local TabOrder must be rejected");
}

void test_root_pages_round_trip_with_page_local_control_order() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Paged";
    form.children = {model::PageRef{model::ObjectId{30}}, model::PageRef{model::ObjectId{31}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::Page first;
    const auto page_position = [](std::int32_t left, std::int32_t top, std::int32_t width,
                                  std::int32_t height, std::int32_t right_margin,
                                  std::int32_t bottom_margin) {
        model::Position position;
        position.left.set(left);
        position.top.set(top);
        position.width.set(width);
        position.height.set(height);
        for (const auto edge : {model::BindingCoordinate::right, model::BindingCoordinate::bottom}) {
            model::AnchorBinding binding;
            binding.coordinate = edge;
            binding.target_coordinate = edge;
            binding.offset.set(edge == model::BindingCoordinate::right ? -right_margin : -bottom_margin);
            position.bindings.anchors.push_back(std::move(binding));
        }
        return position;
    };
    first.id = model::ObjectId{30};
    first.name = "Overview";
    first.title.set(model::LocalizedStringValue{{{"ru", "Обзор"}}});
    first.visible.set(false);
    first.position.set(page_position(0, 0, 400, 300, 12, 14));
    first.children = {model::ControlRef{model::ObjectId{20}}, model::ControlRef{model::ObjectId{9}}};
    model::Page second;
    second.id = model::ObjectId{31};
    second.name = "Details";
    second.title.set(model::LocalizedStringValue{{{"en", "Details"}, {"ru", "Подробности"}}});
    second.position.set(page_position(4, 5, 350, 260, 8, 9));
    second.children = {model::ControlRef{model::ObjectId{4}}, model::ControlRef{model::ObjectId{5}}};
    document.add_page(first);
    document.add_page(second);

    model::ControlNode input{model::ObjectId{4}, "Value", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{2}}, {}};
    input.position.left.set(24);
    model::AnchorBinding input_binding;
    input_binding.coordinate = model::BindingCoordinate::left;
    input_binding.target_coordinate = model::BindingCoordinate::right;
    input_binding.target = model::ControlRef{model::ObjectId{20}};
    input_binding.offset.set(3);
    input.position.bindings.anchors.push_back(input_binding);
    document.add_control(std::move(input));
    model::ControlNode detail_button{model::ObjectId{5}, "Apply", model::ButtonPayload{}};
    detail_button.events.push_back(model::EventRef{model::ObjectId{40}});
    document.add_event(model::Event{model::ObjectId{40}, "Click", "ApplyHandler", model::ControlRef{model::ObjectId{5}}});
    document.add_control(std::move(detail_button));
    model::ControlNode label{model::ObjectId{9}, "Summary", model::LabelDecorationPayload{}};
    document.add_control(std::move(label));
    model::ControlNode overview_button{model::ObjectId{20}, "Open", model::ButtonPayload{}};
    model::AnchorBinding form_binding;
    form_binding.coordinate = model::BindingCoordinate::right;
    form_binding.target_coordinate = model::BindingCoordinate::right;
    form_binding.offset.set(5);
    overview_button.position.bindings.anchors.push_back(form_binding);
    overview_button.events.push_back(model::EventRef{model::ObjectId{41}});
    document.add_event(model::Event{model::ObjectId{41}, "Click", "OpenHandler", model::ControlRef{model::ObjectId{20}}});
    document.add_control(std::move(overview_button));
    model::TypeDomainPatternValue string_type;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{32, false};
    string_type.entries.push_back(string_entry);
    document.add_attribute(model::Attribute{model::ObjectId{2}, "Value", string_type});

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "named root Pages must encode" : encoded.diagnostics().front().message);
    auto payload = encoded.value().items[1].items[2].items[1].items[1];
    std::size_t incoming_end = 2;
    for (std::size_t edge = 0; edge < 6; ++edge) {
        const auto count = static_cast<std::size_t>(std::stoul(payload.items[incoming_end].atom));
        incoming_end += 1 + count;
    }
    payload.items.erase(payload.items.begin() + 2,
        payload.items.begin() + static_cast<std::ptrdiff_t>(incoming_end));
    expect(payload.items[3].atom == "1" && payload.items[9].atom == "8",
        "multi-page root payload must select its confirmed page-table and boundary-count variant");
    const auto& physical = encoded.value().items[1].items[2].items[2].items;
    expect(physical[1].items[1].atom == "4" && physical[2].items[1].atom == "5" &&
           physical[3].items[1].atom == "9" && physical[4].items[1].atom == "20",
        "records inside root Pages must remain physically sorted by control ID");
    const auto first_tail = geometry_tail_start(physical[1].items[3]);
    const auto second_tail = geometry_tail_start(physical[2].items[3]);
    const auto third_tail = geometry_tail_start(physical[3].items[3]);
    const auto fourth_tail = geometry_tail_start(physical[4].items[3]);
    expect(physical[1].items[3].items[first_tail].atom == "1" && physical[1].items[3].items[first_tail + 1].atom == "0" &&
           physical[2].items[3].items[second_tail].atom == "1" && physical[2].items[3].items[second_tail + 1].atom == "1" &&
           physical[3].items[3].items[third_tail].atom == "0" && physical[3].items[3].items[third_tail + 1].atom == "1" &&
           physical[4].items[3].items[fourth_tail].atom == "0" && physical[4].items[3].items[fourth_tail + 1].atom == "0",
        "geometry page indexes and ordinals must follow each Page ChildItems order");

    const auto decoded = form_stream::decode_document(encoded.value(), "Paged");
    expect(decoded.ok(), decoded ? "multi-page root stream must decode" : decoded.diagnostics().front().message);
    expect(decoded.value().collections().pages.size() == 2 && decoded.value().form().children.size() == 2,
        "decoded root Page table and Form order must remain named");
    const auto* decoded_first = decoded.value().find_page(model::ObjectId{21});
    const auto* decoded_second = decoded.value().find_page(model::ObjectId{22});
    expect(decoded_first && decoded_second && decoded_first->name == "Overview" && decoded_second->name == "Details",
        "decoded Page metadata must follow table order and receive fresh nonconflicting IDs");
    expect(!decoded_first->visible.value() && decoded_second->enabled.value() &&
           decoded_second->title.value() == second.title.value(),
        "Page visibility, enabled state, and multilingual title must survive");
    expect(decoded_first->position.value().left.value() == 0 && decoded_first->position.value().width.value() == 400 &&
           decoded_second->position.value().left.value() == 4 && decoded_second->position.value().width.value() == 350,
        "each Page Position must survive independently");
    expect(decoded_first->children.size() == 2 &&
           std::get<model::ControlRef>(decoded_first->children[0]).id() == model::ObjectId{20} &&
           std::get<model::ControlRef>(decoded_first->children[1]).id() == model::ObjectId{9} &&
           std::get<model::ControlRef>(decoded_second->children[0]).id() == model::ObjectId{4} &&
           std::get<model::ControlRef>(decoded_second->children[1]).id() == model::ObjectId{5},
        "page-local logical control order must survive physical ID sorting");
    expect(decoded.value().find_control(model::ObjectId{4})->data_path->attribute.id() == model::ObjectId{2} &&
           decoded.value().find_event(decoded.value().find_control(model::ObjectId{5})->events.front().id())->handler == "ApplyHandler" &&
           decoded.value().find_event(decoded.value().find_control(model::ObjectId{20})->events.front().id())->handler == "OpenHandler",
        "DataPath and Button event links must remain attached across root Pages");
    const auto& input_anchors = decoded.value().find_control(model::ObjectId{4})->position.bindings.anchors;
    const auto& form_anchors = decoded.value().find_control(model::ObjectId{20})->position.bindings.anchors;
    expect(input_anchors.size() == 1 && input_anchors.front().target == model::ControlRef{model::ObjectId{20}} &&
           form_anchors.size() == 1 && !form_anchors.front().target,
        "incoming geometry must resolve both cross-page control targets and Form target zero");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "multi-page root document must encode-decode-encode without storage drift");

    auto bad_page = encoded.value();
    auto& bad_page_geometry = bad_page.items[1].items[2].items[2].items[1].items[3];
    bad_page_geometry.items[geometry_tail_start(bad_page_geometry)] = list_stream::ListValue::raw_atom("2");
    expect(!form_stream::decode_document(bad_page, "Paged"), "out-of-range root Page indexes must fail");
    auto bad_ordinal = encoded.value();
    auto& bad_ordinal_geometry = bad_ordinal.items[1].items[2].items[2].items[1].items[3];
    const auto tail = geometry_tail_start(bad_ordinal_geometry);
    bad_ordinal_geometry.items[tail + 1] = list_stream::ListValue::raw_atom("2");
    bad_ordinal_geometry.items[tail + 2] = list_stream::ListValue::raw_atom("3");
    expect(!form_stream::decode_document(bad_ordinal, "Paged"), "out-of-range page-local ordinals must fail");

    model::Form single_form;
    single_form.id = model::ObjectId{1};
    single_form.name = "SingleNamedPage";
    single_form.children = {model::PageRef{model::ObjectId{30}}};
    model::OrdinaryFormDocument single(std::move(single_form));
    model::Page customized;
    customized.id = model::ObjectId{30};
    customized.name = "Custom";
    customized.title.set(model::LocalizedStringValue{{{"ru", "Своя страница"}}});
    customized.enabled.set(false);
    customized.position.set(page_position(7, 9, 320, 210, 6, 8));
    customized.children = {model::ControlRef{model::ObjectId{8}}};
    single.add_page(customized);
    single.add_control(model::ControlNode{model::ObjectId{8}, "Only", model::ButtonPayload{}});
    const auto single_encoded = form_stream::encode_document(single);
    expect(single_encoded.ok(), "custom single root Page must encode");
    const auto single_decoded = form_stream::decode_document(single_encoded.value(), "SingleNamedPage");
    expect(single_decoded.ok() && single_decoded.value().collections().pages.size() == 1,
        "custom single root Page must remain explicitly represented");
    const auto* retained = single_decoded.value().find_page(model::ObjectId{9});
    expect(retained && retained->name == "Custom" && !retained->enabled.value() &&
           retained->title.value() == customized.title.value() && retained->position.value().left.value() == 7,
        "single Page metadata and changed geometry must not collapse into the implicit default");
    for (const auto [id, order] : {std::pair{20ULL,2},std::pair{9ULL,1},std::pair{4ULL,2},std::pair{5ULL,1}})
        const_cast<model::ControlNode*>(document.find_control(model::ObjectId{id}))->position.tab_order.set(std::optional<std::int32_t>{order});
    const auto independent_pages = form_stream::encode_document(document);
    expect(independent_pages.ok(), "the same TabOrder values may repeat in different Pages");
    const auto decoded_pages = form_stream::decode_document(independent_pages.value(), "Paged");
    expect(decoded_pages.ok() && decoded_pages.value().collections().pages.size() == 2,
        "independent page-local TabOrder permutations must decode");
    const auto page_rebuilt = form_stream::encode_document(decoded_pages.value());
    expect(page_rebuilt.ok() && list_stream::dump_compact(page_rebuilt.value()) == list_stream::dump_compact(independent_pages.value()),
        "Page-local traversal order must round-trip without moving ChildItems across Pages");
    const_cast<model::ControlNode*>(document.find_control(model::ObjectId{4}))->position.tab_order.set(std::optional<std::int32_t>{3});
    expect(!form_stream::encode_document(document).ok(), "TabOrder must be bounded by its own Page rather than total owner count");
}

void test_owned_panel_colors_and_repeated_background() {
    const auto parsed = oof::source::parse_form_xml(R"XML(<Form id="1" name="PanelColors" ordinaryFormVersion="2.1">
      <Panel><AutoTabOrder>false</AutoTabOrder><BorderColor kind="absolute" red="11" green="22" blue="33"/>
        <TextColor kind="absolute" red="44" green="55" blue="66"/><BackColor kind="absolute" red="77" green="88" blue="99"/></Panel>
      <ChildItems><Panel id="2" name="Nested"><Position/><AutoTabOrder>false</AutoTabOrder>
        <BorderColor kind="absolute" red="101" green="102" blue="103"/>
        <TextColor kind="absolute" red="104" green="105" blue="106"/><BackColor kind="absolute" red="107" green="108" blue="109"/>
        <ChildItems><Page name="NestedPage"><Position><Width>100</Width><Bindings>
          <AnchorBinding coordinate="right" targetCoordinate="right" targetId="2" offset="0"/>
          <AnchorBinding coordinate="bottom" targetCoordinate="bottom" targetId="2" offset="0"/>
        </Bindings></Position></Page></ChildItems></Panel></ChildItems></Form>)XML");
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().message);
    const auto decoded = form_stream::decode_document(encoded.value(), "PanelColors");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    for (const auto name : {"BorderColor", "TextColor", "BackColor"}) {
        const auto id = model::PropertyId::from_name(name);
        expect(decoded.value().form().panel.properties.find(id)->value == parsed.value().form().panel.properties.find(id)->value,
            "main Panel colors must round-trip through named properties");
        expect(decoded.value().find_control(model::ObjectId{2})->properties().find(id)->value ==
            parsed.value().find_control(model::ObjectId{2})->properties().find(id)->value,
            "nested Panel colors must remain independent from main Panel colors");
    }
    auto styled_form = parsed.value().form();
    model::ColorValue border_style; border_style.kind = model::ColorKind::style_reference;
    border_style.style = model::QualifiedName{"StyleColors.BorderColor"};
    styled_form.panel.properties.set_explicit(model::PropertyId::from_name("BorderColor"), border_style);
    model::OrdinaryFormDocument styled_document(std::move(styled_form));
    for (const auto& page : parsed.value().collections().pages) styled_document.add_page(page);
    for (const auto& control : parsed.value().collections().controls) styled_document.add_control(control);
    const auto styled_encoded = form_stream::encode_document(styled_document);
    expect(styled_encoded.ok() && styled_encoded.value().items[1].items[2].items[1].items[1].items[0].items[6].items[2].items[0].atom == "-22",
        "named BorderColor style must use the native observed style descriptor");
    const auto styled_decoded = form_stream::decode_document(styled_encoded.value(), "StyledPanel");
    expect(styled_decoded.ok() && styled_decoded.value().form().panel.properties.find(model::PropertyId::from_name("BorderColor"))->value == model::PropertyValue{border_style},
        "style identity must survive without exposing a numeric style ID");
    auto paged_form = parsed.value().form();
    paged_form.children = {model::PageRef{model::ObjectId{10}}, model::PageRef{model::ObjectId{11}}};
    model::OrdinaryFormDocument paged_document(std::move(paged_form));
    for (const auto& page : parsed.value().collections().pages) paged_document.add_page(page);
    for (const auto& control : parsed.value().collections().controls) {
        auto bound_control = control;
        model::AnchorBinding binding; binding.coordinate = model::BindingCoordinate::right;
        binding.target_coordinate = model::BindingCoordinate::right;
        bound_control.position.bindings.anchors.push_back(std::move(binding));
        paged_document.add_control(std::move(bound_control));
    }
    for (const auto id : {10, 11}) {
        model::Page page; page.id = model::ObjectId{static_cast<std::uint64_t>(id)}; page.name = "RootPage" + std::to_string(id);
        model::Position position; position.width.set(300); position.height.set(200);
        for (const auto edge : {model::BindingCoordinate::right, model::BindingCoordinate::bottom}) {
            model::AnchorBinding binding; binding.coordinate = edge; binding.target_coordinate = edge;
            position.bindings.anchors.push_back(std::move(binding));
        }
        page.position.set(std::move(position));
        if (id == 10) page.children = {model::ControlRef{model::ObjectId{2}}};
        paged_document.add_page(std::move(page));
    }
    const auto paged_encoded = form_stream::encode_document(paged_document);
    expect(paged_encoded.ok(), paged_encoded ? "" : paged_encoded.diagnostics().front().message);
    const auto paged_decoded = form_stream::decode_document(paged_encoded.value(), "PagedColors");
    expect(paged_decoded.ok() && paged_decoded.value().form().panel.properties.find(model::PropertyId::from_name("BackColor"))->value ==
        parsed.value().form().panel.properties.find(model::PropertyId::from_name("BackColor"))->value,
        "repeated background must remain stable when page boundaries and markers grow");
    auto invalid = encoded.value();
    auto& owner = invalid.items[1].items[2].items[1].items[1];
    owner.items[owner.items.size() - 6] = list_stream::parse("{4,4,{0},4}");
    expect(!form_stream::decode_document(invalid, "InconsistentBackground"),
        "background copy must agree with the named BackColor rather than being normalized away");
    auto invalid_form = parsed.value().form();
    model::ColorValue transparent{model::ColorKind::absolute, 11, 22, 33, 128, std::monostate{}};
    invalid_form.panel.properties.set_explicit(model::PropertyId::from_name("BorderColor"), transparent);
    model::OrdinaryFormDocument invalid_document(std::move(invalid_form));
    for (const auto& page : parsed.value().collections().pages) invalid_document.add_page(page);
    for (const auto& control : parsed.value().collections().controls) invalid_document.add_control(control);
    expect(!form_stream::encode_document(invalid_document), "unsupported alpha must be rejected instead of losing transparency");
}

void test_owned_panel_auto_tab_order_round_trip() {
    for (const auto root_auto : {true, false}) for (const auto nested_auto : {true, false}) {
        model::Form form; form.id = model::ObjectId{1}; form.name = "AutoTraversal";
        if (!root_auto) form.panel.properties.set_explicit(model::PropertyId::from_name("AutoTabOrder"), false);
        form.children = {model::ControlRef{model::ObjectId{2}}};
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode panel{model::ObjectId{2}, "Nested", model::PanelPayload{}};
        panel.children = {model::PageRef{model::ObjectId{3}}};
        if (!nested_auto) panel.properties().set_explicit(model::PropertyId::from_name("AutoTabOrder"), false);
        model::Page page; page.id = model::ObjectId{3}; page.name = "NestedPage";
        model::Position page_position;
        page_position.width.set(100); page_position.height.set(80);
        for (const auto edge : {model::BindingCoordinate::right, model::BindingCoordinate::bottom}) {
            model::AnchorBinding binding; binding.coordinate = edge; binding.target_coordinate = edge;
            binding.target = model::ControlRef{model::ObjectId{2}};
            page_position.bindings.anchors.push_back(std::move(binding));
        }
        page.position.set(std::move(page_position));
        document.add_control(std::move(panel)); document.add_page(std::move(page));
        const auto encoded = form_stream::encode_document(document);
        expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().message);
        expect(encoded.value().items[1].items[2].items[1].items[1].items[13].atom == (root_auto ? "1" : "0"),
            "main Panel traversal must be written independently");
        const auto decoded = form_stream::decode_document(encoded.value(), "AutoTraversal");
        expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
        const auto* root_entry = decoded.value().form().panel.properties.find(model::PropertyId::from_name("AutoTabOrder"));
        expect(root_auto ? root_entry == nullptr : root_entry != nullptr && !std::get<bool>(root_entry->value),
            "main Panel traversal default must be implicit, false explicit");
        const auto* nested_entry = decoded.value().find_control(model::ObjectId{2})->properties().find(model::PropertyId::from_name("AutoTabOrder"));
        expect(nested_auto ? nested_entry == nullptr : nested_entry != nullptr && !std::get<bool>(nested_entry->value),
            "nested Panel traversal must not leak into the main Panel");
        const auto rebuilt = form_stream::encode_document(decoded.value());
        expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
            "traversal flags and ownership must survive fresh named-model rebuild");
        auto invalid = encoded.value();
        invalid.items[1].items[2].items[1].items[1].items[13] = list_stream::ListValue::raw_atom("2");
        const auto invalid_result = form_stream::decode_document(invalid, "InvalidTraversal");
        expect(!invalid_result && invalid_result.diagnostics().front().path == "$/1/2/1/1/13",
            "nonboolean traversal must identify the original field path");
    }
}

void test_recursive_panel_pages_keep_owner_geometry_separate() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "NestedPanels";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{30}}};
    model::OrdinaryFormDocument document(std::move(form));
    const auto make_page_position = [](std::int32_t left, std::int32_t top, std::int32_t width,
                                       std::int32_t height, model::ControlRef owner) {
        model::Position position;
        position.left.set(left);
        position.top.set(top);
        position.width.set(width);
        position.height.set(height);
        for (const auto edge : {model::BindingCoordinate::right, model::BindingCoordinate::bottom}) {
            model::AnchorBinding binding;
            binding.coordinate = edge;
            binding.target_coordinate = edge;
            binding.target = owner;
            position.bindings.anchors.push_back(std::move(binding));
        }
        return position;
    };
    const auto add_anchor = [](model::ControlNode& control, model::BindingCoordinate source,
                               model::BindingCoordinate target_edge,
                               std::optional<model::ControlRef> target, std::int32_t offset) {
        model::AnchorBinding binding;
        binding.coordinate = source;
        binding.target_coordinate = target_edge;
        binding.target = target;
        binding.offset.set(offset);
        control.position.bindings.anchors.push_back(std::move(binding));
    };

    model::ControlNode outer_panel{model::ObjectId{2}, "Outer", model::PanelPayload{}};
    outer_panel.children = {model::PageRef{model::ObjectId{40}}};
    model::Page outer_page;
    outer_page.id = model::ObjectId{40};
    outer_page.name = "OuterPage";
    outer_page.title.set(model::LocalizedStringValue{{{"en", "Outer page"}}});
    outer_page.position.set(make_page_position(0, 0, 500, 400, model::ControlRef{model::ObjectId{2}}));
    outer_page.children = {model::ControlRef{model::ObjectId{3}}, model::ControlRef{model::ObjectId{6}}};
    document.add_page(outer_page);

    model::ControlNode inner_panel{model::ObjectId{3}, "Inner", model::PanelPayload{}};
    inner_panel.children = {model::PageRef{model::ObjectId{41}}};
    model::Page inner_page;
    inner_page.id = model::ObjectId{41};
    inner_page.name = "InnerPage";
    inner_page.title.set(model::LocalizedStringValue{{{"ru", "Внутренняя"}}});
    inner_page.position.set(make_page_position(5, 7, 300, 200, model::ControlRef{model::ObjectId{3}}));
    inner_page.children = {model::ControlRef{model::ObjectId{4}}, model::ControlRef{model::ObjectId{5}}};
    document.add_page(inner_page);

    model::ControlNode input{model::ObjectId{4}, "Value", model::InputFieldPayload{}};
    input.data_path = model::DataPath{model::AttributeRef{model::ObjectId{7}}, {}};
    add_anchor(input, model::BindingCoordinate::right, model::BindingCoordinate::right,
        model::ControlRef{model::ObjectId{3}}, 2);
    document.add_control(std::move(input));
    model::ControlNode inner_button{model::ObjectId{5}, "InnerRun", model::ButtonPayload{}};
    add_anchor(inner_button, model::BindingCoordinate::left, model::BindingCoordinate::right,
        model::ControlRef{model::ObjectId{4}}, 3);
    inner_button.events.push_back(model::EventRef{model::ObjectId{60}});
    document.add_event(model::Event{model::ObjectId{60}, "Click", "InnerHandler", model::ControlRef{model::ObjectId{5}}});
    document.add_control(std::move(inner_button));
    model::ControlNode outer_button{model::ObjectId{6}, "OuterRun", model::ButtonPayload{}};
    add_anchor(outer_button, model::BindingCoordinate::left, model::BindingCoordinate::right,
        model::ControlRef{model::ObjectId{3}}, 4);
    add_anchor(outer_button, model::BindingCoordinate::top, model::BindingCoordinate::top,
        model::ControlRef{model::ObjectId{2}}, 1);
    document.add_control(std::move(outer_button));
    model::ControlNode root_button{model::ObjectId{30}, "RootRun", model::ButtonPayload{}};
    add_anchor(root_button, model::BindingCoordinate::left, model::BindingCoordinate::right,
        model::ControlRef{model::ObjectId{2}}, 5);
    add_anchor(root_button, model::BindingCoordinate::top, model::BindingCoordinate::top, std::nullopt, 6);
    document.add_control(std::move(outer_panel));
    document.add_control(std::move(inner_panel));
    document.add_control(std::move(root_button));
    model::TypeDomainPatternValue string_type;
    model::TypeDomainEntry string_entry;
    string_entry.term = model::TypeDomainTerm::string;
    string_entry.string = model::LengthQualifiers{32, false};
    string_type.entries.push_back(string_entry);
    document.add_attribute(model::Attribute{model::ObjectId{7}, "Value", string_type});

    const auto preflight = document.validate();
    expect(preflight.ok(), "recursive Panel fixture must satisfy model invariants");
    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "recursive Panel graph must encode" : encoded.diagnostics().front().message);
    auto find_record = [](auto&& self, list_stream::ListValue& table, std::uint64_t id) -> list_stream::ListValue* {
        if (!table.is_list || table.items.empty()) return nullptr;
        for (std::size_t index = 1; index < table.items.size(); ++index) {
            auto& record = table.items[index];
            if (!record.is_list || record.items.size() < 2 || record.items[1].is_list) continue;
            if (std::stoull(record.items[1].atom) == id) return &record;
            if (record.items.size() == 6 && record.items[5].is_list) {
                if (auto* found = self(self, record.items[5], id)) return found;
            }
        }
        return nullptr;
    };
    auto storage = encoded.value();
    auto& root_children = storage.items[1].items[2].items[2];
    auto* outer_record = find_record(find_record, root_children, 2);
    auto* inner_record = find_record(find_record, root_children, 3);
    expect(outer_record && inner_record, "both nested Panel records must be present");

    const auto has_source = [](const list_stream::ListValue& node, std::uint64_t source_id, std::size_t cursor) {
        if (node.items.size() < cursor) return false;
        for (std::size_t edge = 0; edge < 6; ++edge) {
            if (cursor >= node.items.size() || node.items[cursor].is_list) return false;
            const auto count = static_cast<std::size_t>(std::stoul(node.items[cursor].atom));
            for (std::size_t index = 0; index < count; ++index) {
                const auto& tuple = node.items.at(cursor + index + 1);
                if (tuple.is_list && tuple.items.size() == 3 && tuple.items[1].atom == std::to_string(source_id)) return true;
            }
            cursor += count + 1;
        }
        return false;
    };
    const auto& outer_payload = outer_record->items[2].items[1];
    const auto& inner_payload = inner_record->items[2].items[1];
    expect(has_source(outer_payload, 6, 2) && has_source(outer_record->items[3], 30, 12) &&
           has_source(inner_payload, 4, 2) && has_source(inner_record->items[3], 6, 12),
        "Panel owner fanout must live in Panel properties while sibling fanout stays in outer geometry");

    const auto decoded = form_stream::decode_document(encoded.value(), "NestedPanels");
    expect(decoded.ok(), decoded ? "two-level Panel graph must decode" : decoded.diagnostics().front().message);
    const auto* decoded_outer = decoded.value().find_control(model::ObjectId{2});
    const auto* decoded_inner = decoded.value().find_control(model::ObjectId{3});
    const auto* decoded_input = decoded.value().find_control(model::ObjectId{4});
    expect(decoded_outer && decoded_inner && decoded_input && decoded_outer->kind() == model::ControlKind::panel &&
           decoded_inner->kind() == model::ControlKind::panel && decoded_outer->children.size() == 1 &&
           decoded_inner->children.size() == 1,
        "two Panel levels and their named Page references must materialize");
    expect(decoded_input->data_path->attribute.id() == model::ObjectId{7} &&
           decoded.value().collections().events.size() == 1 &&
           decoded.value().collections().events.front().handler == "InnerHandler",
        "global DataPath and nested Click links must survive");
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "two-level Panel graph must round-trip without storage drift");

    auto moved_incoming = encoded.value();
    auto& moved_root_children = moved_incoming.items[1].items[2].items[2];
    auto* moved_outer = find_record(find_record, moved_root_children, 2);
    auto& panel_payload = moved_outer->items[2].items[1];
    std::size_t cursor = 2;
    bool removed = false;
    for (std::size_t edge = 0; edge < 6 && !removed; ++edge) {
        const auto count = static_cast<std::size_t>(std::stoul(panel_payload.items[cursor].atom));
        for (std::size_t index = 0; index < count; ++index) {
            const auto tuple_index = cursor + index + 1;
            if (panel_payload.items[tuple_index].items[1].atom == "6") {
                panel_payload.items[cursor] = list_stream::ListValue::raw_atom(std::to_string(count - 1));
                panel_payload.items.erase(panel_payload.items.begin() + static_cast<std::ptrdiff_t>(tuple_index));
                auto& outer_geometry = moved_outer->items[3];
                std::size_t geometry_cursor = 12;
                for (std::size_t group = 0; group < edge; ++group) {
                    geometry_cursor += 1 + static_cast<std::size_t>(std::stoul(outer_geometry.items[geometry_cursor].atom));
                }
                const auto geometry_count = static_cast<std::size_t>(std::stoul(outer_geometry.items[geometry_cursor].atom));
                outer_geometry.items[geometry_cursor] = list_stream::ListValue::raw_atom(std::to_string(geometry_count + 1));
                outer_geometry.items.insert(outer_geometry.items.begin() + static_cast<std::ptrdiff_t>(geometry_cursor + geometry_count + 1),
                    list_stream::ListValue::list({list_stream::ListValue::raw_atom("0"),
                        list_stream::ListValue::raw_atom("6"), list_stream::ListValue::raw_atom("3")}));
                removed = true;
                break;
            }
        }
        cursor += count + 1;
    }
    expect(removed, "Panel owner incoming tuple must be locatable for the forged-transfer negative case");
    expect(!form_stream::decode_document(moved_incoming, "NestedPanels"),
        "moving a Panel owner tuple into outer geometry must be rejected");

    auto unknown_property = encoded.value();
    auto& unknown_root_children = unknown_property.items[1].items[2].items[2];
    auto* unknown_panel = find_record(find_record, unknown_root_children, 2);
    unknown_panel->items[4].items[3].atom = "1";
    expect(!form_stream::decode_document(unknown_property, "NestedPanels"),
        "unknown explicit Panel property variation must fail closed");

    model::OrdinaryFormDocument changed_panel_property{document.form()};
    for (const auto& page : document.collections().pages) changed_panel_property.add_page(page);
    for (const auto& attribute : document.collections().attributes) changed_panel_property.add_attribute(attribute);
    for (const auto& event : document.collections().events) changed_panel_property.add_event(event);
    for (auto control : document.collections().controls) {
        if (control.id == model::ObjectId{2}) {
            control.properties().set_explicit(model::PropertyId::from_name("Enabled"), false);
        }
        changed_panel_property.add_control(std::move(control));
    }
    expect(!form_stream::encode_document(changed_panel_property),
        "unsupported explicit Panel Enabled variation must fail closed");
}

void test_manual_bindings_are_not_silently_discarded() {
    for (const auto kind : {model::ControlKind::button, model::ControlKind::label_decoration,
                           model::ControlKind::input_field, model::ControlKind::check_box}) {
        model::Form form;
        form.id = model::ObjectId{1};
        form.name = "ManualBindings";
        form.children.push_back(model::ControlRef{model::ObjectId{2}});
        model::OrdinaryFormDocument document(std::move(form));
        model::ControlNode control{model::ObjectId{2}, "Item", model::ButtonPayload{}};
        if (kind == model::ControlKind::label_decoration) control.payload = model::LabelDecorationPayload{};
        if (kind == model::ControlKind::input_field) control.payload = model::InputFieldPayload{};
        if (kind == model::ControlKind::check_box) control.payload = model::CheckBoxPayload{};
        if (kind == model::ControlKind::input_field || kind == model::ControlKind::check_box) {
            model::TypeDomainPatternValue type;
            model::TypeDomainEntry entry;
            entry.term = kind == model::ControlKind::check_box
                ? model::TypeDomainTerm::boolean : model::TypeDomainTerm::string;
            type.entries.push_back(entry);
            document.add_attribute(model::Attribute{model::ObjectId{3}, "Value", std::move(type)});
            control.data_path = model::DataPath{model::AttributeRef{model::ObjectId{3}}, {}};
        }
        document.add_control(control);
        for (const bool horizontal : {true, false}) {
            model::OrdinaryFormDocument manual(document.form());
            for (const auto& attribute : document.collections().attributes) manual.add_attribute(attribute);
            auto manual_control = control;
            manual_control.position.bindings.manual_horizontal.set(horizontal);
            manual_control.position.bindings.manual_vertical.set(!horizontal);
            manual.add_control(std::move(manual_control));
            const auto encoded = form_stream::encode_document(manual);
            expect(encoded.ok(), "both manual axes must encode for each supported control type");
            const auto decoded = form_stream::decode_document(encoded.value(), "ManualBindings");
            expect(decoded.ok(), "both manual axes must decode for each supported control type");
            const auto* roundtrip = decoded.value().find_control(model::ObjectId{2});
            expect(roundtrip != nullptr && roundtrip->position.bindings.manual_horizontal.value() == horizontal &&
                       roundtrip->position.bindings.manual_vertical.value() == !horizontal,
                "manual axes must survive Form.bin round-trip");
        }
    }
}

void test_anchor_bindings_round_trip_and_fanout() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "Bindings";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{9}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode run{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    model::AnchorBinding right_to_form;
    right_to_form.coordinate = model::BindingCoordinate::right;
    right_to_form.target_coordinate = model::BindingCoordinate::right;
    right_to_form.offset.set(-380);
    model::AnchorBindingTarget form_left;
    form_left.coordinate = model::BindingCoordinate::left;
    form_left.offset.set(120);
    right_to_form.proportional = form_left;
    run.position.bindings.anchors.push_back(right_to_form);

    model::AnchorBinding top_to_control;
    top_to_control.coordinate = model::BindingCoordinate::top;
    top_to_control.target = model::ControlRef{model::ObjectId{9}};
    top_to_control.target_coordinate = model::BindingCoordinate::bottom;
    top_to_control.offset.set(15);
    model::AnchorBindingTarget control_left;
    control_left.target = model::ControlRef{model::ObjectId{9}};
    control_left.coordinate = model::BindingCoordinate::left;
    control_left.offset.set(-5);
    top_to_control.proportional = control_left;
    run.position.bindings.anchors.push_back(top_to_control);
    run.position.bindings.manual_horizontal.set(true);
    run.position.bindings.manual_vertical.set(true);

    model::ControlNode text{model::ObjectId{9}, "Text", model::ButtonPayload{}};
    model::AnchorBinding bottom_to_form;
    bottom_to_form.coordinate = model::BindingCoordinate::bottom;
    bottom_to_form.target_coordinate = model::BindingCoordinate::bottom;
    bottom_to_form.offset.set(40);
    text.position.bindings.anchors.push_back(bottom_to_form);
    model::AnchorBinding left_to_self;
    left_to_self.coordinate = model::BindingCoordinate::left;
    left_to_self.target = model::ControlRef{model::ObjectId{9}};
    left_to_self.target_coordinate = model::BindingCoordinate::right;
    text.position.bindings.anchors.push_back(left_to_self);
    document.add_control(std::move(run));
    document.add_control(std::move(text));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "primary and proportional bindings must encode" : encoded.diagnostics().front().message);
    const auto& root_body = encoded.value().items[1].items[2].items[1].items[1];
    expect(root_body.items[3].atom == "1" &&
               list_stream::dump_compact(root_body.items[4]) == "{0,9,1}" &&
               root_body.items[5].atom == "1" &&
               list_stream::dump_compact(root_body.items[6]) == "{0,2,3}" &&
               root_body.items[7].atom == "1" &&
               list_stream::dump_compact(root_body.items[8]) == "{0,2,3}",
        "root Bottom, Left, and Right fanout counts and tuples must be derived from outgoing links: " +
            list_stream::dump_compact(root_body));

    const auto& children = encoded.value().items[1].items[2].items[2];
    const auto find_record = [&](std::uint64_t id) -> const list_stream::ListValue& {
        for (std::size_t index = 1; index < children.items.size(); ++index) {
            const auto& record = children.items[index];
            if (record.is_list && record.items.size() > 1 && !record.items[1].is_list &&
                record.items[1].atom == std::to_string(id)) return record;
        }
        throw std::runtime_error("encoded control record is missing");
    };
    const auto& run_geometry = find_record(2).items[3];
    expect(list_stream::dump_compact(run_geometry.items[9]) == "{0,{2,0,3,-380},{2,0,2,120}}" &&
               list_stream::dump_compact(run_geometry.items[6]) == "{0,{2,9,1,15},{2,9,2,-5}}",
        "primary and proportional target IDs, edge codes, and offsets must map into shared geometry slots");
    const auto& text_geometry = find_record(9).items[3];
    expect(list_stream::dump_compact(text_geometry.items[7]) == "{0,{2,0,1,40},{2,-1,6,0}}" &&
               list_stream::dump_compact(text_geometry.items[8]) == "{0,{2,9,3,0},{2,-1,6,0}}",
        "Form and self-control primary targets must encode in their source-edge slots");

    const auto decoded = form_stream::decode_document(encoded.value(), "Bindings");
    expect(decoded.ok(), decoded ? "anchor binding graph must decode" : decoded.diagnostics().front().message);
    const auto* roundtrip_run = decoded.value().find_control(model::ObjectId{2});
    const auto* roundtrip_text = decoded.value().find_control(model::ObjectId{9});
    expect(roundtrip_run != nullptr && roundtrip_text != nullptr &&
               roundtrip_run->position.bindings.anchors.size() == 2 &&
               roundtrip_run->position.bindings.anchors[1].target_coordinate == model::BindingCoordinate::right &&
               !roundtrip_run->position.bindings.anchors[1].target.has_value() &&
               roundtrip_run->position.bindings.anchors[1].proportional.has_value() &&
               !roundtrip_run->position.bindings.anchors[1].proportional->target.has_value() &&
               roundtrip_run->position.bindings.anchors[1].proportional->coordinate == model::BindingCoordinate::left &&
               roundtrip_run->position.bindings.anchors[1].proportional->offset.value() == 120 &&
               roundtrip_run->position.bindings.anchors[0].target == model::ControlRef{model::ObjectId{9}} &&
               roundtrip_run->position.bindings.anchors[0].proportional->target == model::ControlRef{model::ObjectId{9}} &&
               roundtrip_run->position.bindings.manual_horizontal.value() &&
               roundtrip_run->position.bindings.manual_vertical.value() &&
               roundtrip_text->position.bindings.anchors.size() == 2 &&
               roundtrip_text->position.bindings.anchors[1].target == model::ControlRef{model::ObjectId{9}},
        "Form, cross-control, self, proportional, and manual bindings must survive decode; Run anchors=" +
            (roundtrip_run == nullptr ? std::string("missing") :
             std::to_string(roundtrip_run->position.bindings.anchors.size())) +
            ", Text anchors=" + (roundtrip_text == nullptr ? std::string("missing") :
             std::to_string(roundtrip_text->position.bindings.anchors.size())));
    const auto reencoded = form_stream::encode_document(decoded.value());
    expect(reencoded.ok() && list_stream::dump_compact(reencoded.value()) == list_stream::dump_compact(encoded.value()),
        "all binding tuples and fanout counts must be stable across decode and encode");

    auto stale_root = encoded.value();
    stale_root.items[1].items[2].items[1].items[1].items[6].items[1] = list_stream::ListValue::raw_atom("7");
    expect(!form_stream::decode_document(stale_root, "Bindings"),
        "root incoming tuples must be checked against the outgoing graph");
    auto unsupported_root_marker = encoded.value();
    unsupported_root_marker.items[1].items[2].items[1].items[0] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_document(unsupported_root_marker, "Bindings"), "OOF1114", "$/1/2/1/0",
        "unsupported root-panel envelope marker must be rejected");
    auto unsupported_root_trailer = encoded.value();
    unsupported_root_trailer.items[1].items[2].items[1].items[2] = list_stream::parse("{1}");
    expect_failure(form_stream::decode_document(unsupported_root_trailer, "Bindings"), "OOF1114", "$/1/2/1/2",
        "unsupported root-panel envelope trailer must be rejected");
    auto dangling_target = encoded.value();
    dangling_target.items[1].items[2].items[2].items[1].items[3].items[9].items[1].items[1] =
        list_stream::ListValue::raw_atom("77");
    expect(!form_stream::decode_document(dangling_target, "Bindings"),
        "primary target IDs absent from the form graph must be rejected");
    auto unknown_target_edge = encoded.value();
    unknown_target_edge.items[1].items[2].items[2].items[1].items[3].items[9].items[1].items[2] =
        list_stream::ListValue::raw_atom("6");
    expect(!form_stream::decode_document(unknown_target_edge, "Bindings"),
        "unknown target edge 6 must be rejected");
    model::OrdinaryFormDocument centered_source(document.form());
    for (const auto& source_control : document.collections().controls) {
        auto centered_control = source_control;
        if (centered_control.id == model::ObjectId{2}) {
            centered_control.position.bindings.anchors.front().coordinate = model::BindingCoordinate::horizontal_center;
        }
        centered_source.add_control(std::move(centered_control));
    }
    expect(!form_stream::encode_document(centered_source), "center coordinates must remain unsupported as sources");
    auto orphan_proportional = encoded.value();
    orphan_proportional.items[1].items[2].items[2].items[1].items[3].items[9].items[1] =
        list_stream::parse("{2,-1,6,0}");
    expect(!form_stream::decode_document(orphan_proportional, "Bindings"),
        "proportional tuple without a primary tuple must be rejected");
}

void test_chart_named_dense_roundtrip_with_sibling_geometry() {
    std::string xml = R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems>
      <Chart id="2" name="Metrics"><Position><Top>0</Top><Height>40</Height><Left>0</Left><Width>120</Width></Position><Title>Новая диаграмма</Title><Series>)XML";
    constexpr std::array<std::uint64_t, 5> series_ids{2, 8, 9, 10, 11};
    constexpr std::array<std::string_view, 5> series_texts{"Повтор", "Повтор", "", "Series 4", "Series 5"};
    constexpr std::array<std::string_view, 5> markers{"Auto", "Circle", "Rhomb", "Rect", "Alternation"};
    for (std::size_t index = 0; index < series_ids.size(); ++index) {
        xml += "<ChartSeries id=\"" + std::to_string(series_ids[index]) + "\"><Text>" +
            std::string(series_texts[index]) + "</Text><Color kind=\"absolute\" red=\"" +
            std::to_string(40 + index * 30) + "\" green=\"" + std::to_string(90 + index * 20) +
            "\" blue=\"" + std::to_string(120 + index * 10) + "\"/><Marker type=\"ChartMarkerType\" member=\"" +
            std::string(markers[index]) + "\"/></ChartSeries>";
    }
    xml += "</Series><Points>";
    constexpr std::array<std::uint64_t, 3> point_ids{1, 7, 12};
    constexpr std::array<std::string_view, 3> point_texts{"", "Ось X", "Ось X"};
    for (std::size_t index = 0; index < point_ids.size(); ++index) {
        xml += "<ChartPoint id=\"" + std::to_string(point_ids[index]) + "\"><Text>" +
            std::string(point_texts[index]) + "</Text><Color kind=\"absolute\" red=\"" +
            std::to_string(20 + index * 50) + "\" green=\"" + std::to_string(30 + index * 40) +
            "\" blue=\"" + std::to_string(40 + index * 30) + "\"/></ChartPoint>";
    }
    xml += "</Points><Values>";
    for (std::size_t reverse_series = series_ids.size(); reverse_series > 0; --reverse_series) {
        const std::size_t series = reverse_series - 1;
        for (std::size_t reverse_point = point_ids.size(); reverse_point > 0; --reverse_point) {
            const std::size_t point = reverse_point - 1;
            xml += "<ChartValue seriesRef=\"" + std::to_string(series_ids[series]) + "\" pointRef=\"" +
                std::to_string(point_ids[point]) + "\">";
            if (series == 0 && point == 0) xml += "<Undefined>undefined</Undefined>";
            else if (series == 0 && point == 1) xml += "<Number>0</Number>";
            else if (series == 4 && point == 2) xml += "<Number>-23.75</Number>";
            else xml += "<Number>" + std::to_string(series * 3 + point + 1) + ".125</Number>";
            xml += "</ChartValue>";
        }
    }
    xml += R"XML(</Values></Chart>
      <LabelDecoration id="3" name="Caption"><Position><Top>50</Top><Height>20</Height><Left>4</Left><Width>80</Width></Position></LabelDecoration>
    </ChildItems></Form>)XML";
    auto parsed = source::parse_form_xml(xml);
    if (!parsed.ok()) throw std::runtime_error("Chart parse: " + parsed.diagnostics().front().code + " " + parsed.diagnostics().front().message + " " + parsed.diagnostics().front().path);
    expect(parsed.ok(), "named dense Chart with sibling must parse");
    auto encoded = form_stream::encode_document(parsed.value());
    if (!encoded.ok()) throw std::runtime_error("Chart encode: " + encoded.diagnostics().front().code + " " + encoded.diagnostics().front().message + " expected=" + encoded.diagnostics().front().expected + " actual=" + encoded.diagnostics().front().actual + " " + encoded.diagnostics().front().path);
    expect(encoded.ok(), "named dense Chart must encode in a full document");
    auto decoded = form_stream::decode_document(encoded.value());
    expect(decoded.ok(), "named dense Chart must decode from a full document");
    const auto* chart_control = decoded.value().find_control(model::ObjectId{2});
    expect(chart_control != nullptr && chart_control->kind() == model::ControlKind::chart, "Chart must remain the first control");
    const auto* chart = std::get_if<model::ChartPayload>(&chart_control->payload);
    expect(chart != nullptr && chart->series.size() == 5 && chart->points.size() == 3 && chart->values.size() == 15,
        "Chart dimensions and dense cell count must survive the stream");
    expect(chart->series[0].color.red == 40 && chart->series[0].color.green == 90 && chart->series[0].color.blue == 120 &&
        chart->points[0].color.red == 20 && chart->points[0].color.green == 30 && chart->points[0].color.blue == 40,
        "Chart RGB channels must preserve platform blue-green-red packing order");
    expect(chart->series[0].text == "Повтор" && chart->series[1].text == "Повтор" && chart->series[2].text.empty() && chart->points[0].text.empty(),
        "duplicate and empty Chart captions must survive the stream");
    const auto* title = chart_control->properties().find(model::PropertyId::from_name("Title"));
    expect(title != nullptr && std::get<std::string>(title->value) == "Новая диаграмма", "Chart Title must use its property descriptor surface");
    const auto cell = std::find_if(chart->values.begin(), chart->values.end(), [](const auto& value) {
        return value.series_ref == model::ObjectId{11} && value.point_ref == model::ObjectId{12};
    });
    expect(cell != chart->values.end() && std::get<model::DecimalValue>(cell->value).canonical == "-23.75",
        "Chart matrix must preserve named references and negative decimal values independent of source order");
    const auto zero = std::find_if(chart->values.begin(), chart->values.end(), [](const auto& value) {
        return value.series_ref == model::ObjectId{2} && value.point_ref == model::ObjectId{7};
    });
    expect(zero != chart->values.end() && std::get<model::DecimalValue>(zero->value).canonical == "0",
        "explicit Chart zero must remain a numeric cell");
    const auto undefined = std::find_if(chart->values.begin(), chart->values.end(), [](const auto& value) {
        return value.series_ref == model::ObjectId{2} && value.point_ref == model::ObjectId{1};
    });
    expect(undefined != chart->values.end() && std::holds_alternative<model::UndefinedValue>(undefined->value),
        "explicit Chart Undefined must remain distinct from numeric zero");
    const auto* sibling = decoded.value().find_control(model::ObjectId{3});
    expect(sibling != nullptr && sibling->position.top.value() == 50 && sibling->position.left.value() == 4,
        "Chart geometry slot must not overwrite its sibling Position");

    auto designer_normalized_cache = encoded.value();
    auto* normalized_chart_record = find_chart_record(designer_normalized_cache);
    expect(normalized_chart_record != nullptr, "encoded document must expose its Chart record for cache normalization testing");
    const auto chart_middle_start = std::size_t{5} + (series_ids.size() + 1) * 11 + 2 + point_ids.size() * 11;
    normalized_chart_record->items[3].items[chart_middle_start + 84] = list_stream::ListValue::raw_atom("0.25");
    const auto chart_cells_end = chart_middle_start + 96 + series_ids.size() * point_ids.size() * 3;
    const auto chart_render_start = chart_cells_end + 28 + series_ids.size() + 1 + 21 + point_ids.size() + series_ids.size() + 1;
    normalized_chart_record->items[3].items[chart_render_start + 2] = list_stream::ListValue::raw_atom("0.25");
    expect(form_stream::decode_document(designer_normalized_cache).ok(),
        "proven render-cache scalar may be recomputed without relaxing adjacent named Chart fields");

    auto strict_alternation_cache = encoded.value();
    auto* strict_alternation_record = find_chart_record(strict_alternation_cache);
    expect(strict_alternation_record != nullptr, "encoded document must expose its Chart record for derived marker testing");
    strict_alternation_record->items[3].items[51] = list_stream::ListValue::raw_atom("1");
    expect(form_stream::decode_document(strict_alternation_cache).ok(),
        "Designer-resolved Alternation cache may differ while its named style remains intact");

    auto wrong_marker_cache_type = encoded.value();
    auto* wrong_marker_type_record = find_chart_record(wrong_marker_cache_type);
    expect(wrong_marker_type_record != nullptr, "encoded document must expose its Chart record for marker type testing");
    wrong_marker_type_record->items[3].items[51] = list_stream::ListValue::string_atom("1");
    expect(!form_stream::decode_document(wrong_marker_cache_type),
        "derived Marker cache must remain a raw integer value");

    auto out_of_range_marker_cache = encoded.value();
    auto* out_of_range_marker_record = find_chart_record(out_of_range_marker_cache);
    expect(out_of_range_marker_record != nullptr, "encoded document must expose its Chart record for marker range testing");
    out_of_range_marker_record->items[3].items[51] = list_stream::ListValue::raw_atom("6");
    expect(!form_stream::decode_document(out_of_range_marker_cache),
        "derived Marker cache outside the proven enum range must be rejected");

    auto concrete_marker_mismatch = encoded.value();
    auto* concrete_marker_record = find_chart_record(concrete_marker_mismatch);
    expect(concrete_marker_record != nullptr, "encoded document must expose its Chart record for concrete marker testing");
    concrete_marker_record->items[3].items[18] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(concrete_marker_mismatch),
        "concrete Marker cache must match the named enum exactly");

    auto unknown_chart_style = encoded.value();
    auto* unknown_chart_record = find_chart_record(unknown_chart_style);
    expect(unknown_chart_record != nullptr, "encoded document must expose its Chart record for negative testing");
    unknown_chart_record->items[3].items[6] = list_stream::ListValue::raw_atom("777");
    expect(!form_stream::decode_document(unknown_chart_style),
        "Chart decoder must reject an unrecognized style-row value instead of dropping it");

    constexpr std::string_view empty_xml = R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Empty"><Position/><Series/><Points/><Values/></Chart></ChildItems></Form>)XML";
    auto empty = source::parse_form_xml(empty_xml);
    expect(empty.ok(), "empty platform Chart must remain a valid named model");
    auto empty_stream = form_stream::encode_document(empty.value());
    expect(empty_stream.ok(), "empty platform Chart must encode without synthetic data");
    auto empty_decoded = form_stream::decode_document(empty_stream.value());
    expect(empty_decoded.ok(), "empty platform Chart must cold decode");
    const auto* empty_chart_control = empty_decoded.value().find_control(model::ObjectId{2});
    const auto* empty_chart = empty_chart_control == nullptr ? nullptr : std::get_if<model::ChartPayload>(&empty_chart_control->payload);
    expect(empty_chart != nullptr && empty_chart->series.empty() && empty_chart->points.empty() && empty_chart->values.empty(),
        "empty native Chart dimensions must not be rewritten as synthetic values");

    constexpr std::string_view sparse_xml = R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Sparse"><Position/><Series><ChartSeries id="2"><Text>S</Text><Color kind="absolute" red="1"/><Marker type="ChartMarkerType" member="Auto"/></ChartSeries></Series><Points><ChartPoint id="1"><Text>P</Text><Color kind="absolute" red="2"/></ChartPoint><ChartPoint id="3"><Text>Q</Text><Color kind="absolute" red="3"/></ChartPoint></Points><Values><ChartValue seriesRef="2" pointRef="1"><Number>1</Number></ChartValue></Values></Chart></ChildItems></Form>)XML";
    expect(!source::parse_form_xml(sparse_xml).ok(), "incomplete Chart matrix must fail instead of filling zero");
    constexpr std::string_view invalid_number_xml = R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="BadNumber"><Position/><Series><ChartSeries id="2"><Text>S</Text><Color kind="absolute" red="1"/><Marker type="ChartMarkerType" member="Auto"/></ChartSeries></Series><Points><ChartPoint id="1"><Text>P</Text><Color kind="absolute" red="2"/></ChartPoint></Points><Values><ChartValue seriesRef="2" pointRef="1"><Number>NaN</Number></ChartValue></Values></Chart></ChildItems></Form>)XML";
    expect(!source::parse_form_xml(invalid_number_xml).ok(), "non-decimal Chart Number must fail closed");

    auto unsupported_auto_marker_xml = xml;
    const auto second_series_marker = unsupported_auto_marker_xml.find("member=\"Circle\"");
    expect(second_series_marker != std::string::npos, "test fixture must contain the second Series marker");
    unsupported_auto_marker_xml.replace(second_series_marker, std::string("member=\"Circle\"").size(), "member=\"Auto\"");
    const auto unsupported_auto_marker = source::parse_form_xml(unsupported_auto_marker_xml);
    expect(unsupported_auto_marker.ok(), "unsupported Auto ordinal fixture must remain well-formed named XML");
    expect(form_stream::encode_document(unsupported_auto_marker.value()).ok(),
        "Auto is a named Marker value at any Series ordinal; its resolved row cache is derived");
}

void test_chart_empty_render_cache_normalization_and_guards() {
    constexpr std::string_view empty_xml = R"XML(<Form id="1" name="Main" ordinaryFormVersion="2.1"><ChildItems><Chart id="2" name="Empty"><Position><Height>64</Height><Width>110</Width></Position><Series/><Points/><Values/></Chart></ChildItems></Form>)XML";
    const auto parsed = source::parse_form_xml(empty_xml);
    expect(parsed.ok(), "empty Chart XML must parse for render-cache testing");
    const auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), "empty Chart model must encode before platform cache mutation");

    constexpr std::size_t middle_start = 18;
    constexpr std::size_t render_start = 165;
    constexpr std::array<std::size_t, 10> platform_cache_indices{
        105, 107, 108, 110, 111, 170, 172, 173, 175, 176};
    auto platform_normalized = encoded.value();
    auto* chart_record = find_chart_record(platform_normalized);
    expect(chart_record != nullptr, "encoded empty document must expose its Chart record");
    for (const auto index : platform_cache_indices)
        chart_record->items[3].items[index] = list_stream::ListValue::raw_atom("1");

    const auto decoded = form_stream::decode_document(platform_normalized, "Main");
    expect(decoded.ok(), "proven empty-Chart render caches must decode as computed values");
    const auto* control = decoded.value().find_control(model::ObjectId{2});
    const auto* chart = control == nullptr ? nullptr : std::get_if<model::ChartPayload>(&control->payload);
    expect(control != nullptr && control->kind() == model::ControlKind::chart && control->name == "Empty" &&
        control->position.height.value() == 64 && control->position.width.value() == 110 &&
        chart != nullptr && chart->series.empty() && chart->points.empty() && chart->values.empty(),
        "render-cache normalization must preserve the exact zero-Series/zero-Point named model");
    const auto original_xml = source::serialize_form_xml(parsed.value());
    const auto decoded_xml = source::serialize_form_xml(decoded.value());
    expect(original_xml.ok() && decoded_xml.ok() && decoded_xml.value() == original_xml.value(),
        "empty Chart model XML must remain identical across render-cache decode");
    const auto rebuilt = form_stream::encode_document(decoded.value());
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
        "fresh serializer must rebuild canonical empty-Chart caches from the named model");

    constexpr std::array<std::size_t, 4> newly_proven_indices{
        middle_start + 92, middle_start + 93, render_start + 10, render_start + 11};
    const std::array<list_stream::ListValue, 4> invalid_cache_values{
        list_stream::ListValue::raw_atom("NaN"),
        list_stream::ListValue::raw_atom("inf"),
        list_stream::ListValue::list({list_stream::ListValue::raw_atom("1")}),
        list_stream::ListValue::string_atom("1"),
    };
    for (const auto index : newly_proven_indices) {
        for (const auto& value : invalid_cache_values) {
            auto invalid = encoded.value();
            auto* invalid_record = find_chart_record(invalid);
            expect(invalid_record != nullptr, "invalid-cache fixture must expose its Chart record");
            invalid_record->items[3].items[index] = value;
            expect(!form_stream::decode_document(invalid, "InvalidEmptyChartRenderCache"),
                "nonfinite, list-shaped, or quoted render-cache value must fail closed");
        }
    }
    auto adjacent_unproven_cache = encoded.value();
    auto* adjacent_record = find_chart_record(adjacent_unproven_cache);
    expect(adjacent_record != nullptr, "adjacent-cache fixture must expose its Chart record");
    adjacent_record->items[3].items[middle_start + 91] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_document(adjacent_unproven_cache, "AdjacentUnprovenEmptyChartCache"),
        "neighboring unproven Chart cache slot must remain strict");
}

void test_platform_empty_document_fixture() {
    constexpr std::string_view fixture = R"OOF(
{27,{18,{{1,1,{"ru","Form"}},1,4294967295},{09ccdc77-ea1a-4a6d-ab1c-3435eada2433,{1,{{19,1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},0,{4,4,{0},4},{4,4,{0},4},{4,4,{0},4},{4,3,{-7},3},{4,3,{-21},3},{3,0,{0},0,0,0,48312c09-257f-4b29-b280-284dd89efc1e},{1,0},0,0,100,2,2,1,2,{4,4,{0},4}},26,0,0,0,0,0,0,{10,1,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},0,1,{1,1,{6,{1,1,{"ru","Страница1"}},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},-1,1,1,"Страница1",1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},1}},1,1,0,4,{2,8,1,1,1,0,0,0,0},{2,8,0,1,2,0,0,0,0},{2,392,1,1,3,0,0,8,0},{2,292,0,1,4,0,0,8,0},0,4294967295,5,64,0,{4,4,{0},4},0,0,57,0,0},{0}},{0}},400,300,1,0,1,4,4,3,400,300,96},{{-1},3,{0},{0}},{00000000-0000-0000-0000-000000000000,0},{0},1,4,1,0,0,0,{0},{0},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},1,2,0,0,1,1}
)OOF";
    const auto payload = list_stream::parse(fixture);
    const auto decoded = form_stream::decode_document(payload, "Empty");
    expect(decoded.ok(), decoded ? "platform empty-form fixture must decode into the product model" :
        decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    expect(decoded.value().form().name == "Empty", "external form name must be retained");
    expect(decoded.value().collections().controls.empty(), "empty fixture must have no controls");
    const auto* caption = decoded.value().form().properties.find(
        model::PropertyId::from_name("Caption"));
    expect(caption != nullptr, "platform form caption must materialize");
    expect(std::get<std::string>(caption->value) == "Form", "platform form caption mismatch");

    const auto encoded = form_stream::encode_document(decoded.value());
    expect(encoded.ok(), "decoded platform empty form must encode");
    expect(
        list_stream::dump_compact(encoded.value()) == list_stream::dump_compact(payload),
        "platform empty-form storage must canonicalize without semantic drift");
}

void test_center_target_coordinates() {
    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "CenterTargets";
    form.children = {model::ControlRef{model::ObjectId{2}}, model::ControlRef{model::ObjectId{9}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode run{model::ObjectId{2}, "Run", model::ButtonPayload{}};
    model::AnchorBinding binding;
    binding.coordinate = model::BindingCoordinate::right;
    binding.target_coordinate = model::BindingCoordinate::horizontal_center;
    model::AnchorBindingTarget proportional;
    proportional.coordinate = model::BindingCoordinate::vertical_center;
    binding.proportional = proportional;
    run.position.bindings.anchors.push_back(binding);
    document.add_control(std::move(run));
    model::ControlNode label{model::ObjectId{9}, "Label", model::ButtonPayload{}};
    model::AnchorBinding control_binding;
    control_binding.coordinate = model::BindingCoordinate::left;
    control_binding.target = model::ControlRef{model::ObjectId{2}};
    control_binding.target_coordinate = model::BindingCoordinate::vertical_center;
    model::AnchorBindingTarget control_proportional;
    control_proportional.target = model::ControlRef{model::ObjectId{2}};
    control_proportional.coordinate = model::BindingCoordinate::horizontal_center;
    control_binding.proportional = control_proportional;
    label.position.bindings.anchors.push_back(control_binding);
    document.add_control(std::move(label));

    const auto encoded = form_stream::encode_document(document);
    expect(encoded.ok(), encoded ? "center targets must encode" : encoded.diagnostics().front().message);
    const auto& children = encoded.value().items[1].items[2].items[2];
    const auto& geometry = children.items[1].items[3];
    expect(list_stream::dump_compact(geometry.items[9]) == "{0,{2,0,5,0},{2,0,4,0}}",
        "horizontal and vertical center targets must encode as platform edges 5 and 4");
    expect(list_stream::dump_compact(children.items[2].items[3].items[8]) == "{0,{2,2,4,0},{2,2,5,0}}",
        "control targets and proportional control targets must encode center edges 4 and 5");
    const auto& root_body = encoded.value().items[1].items[2].items[1].items[1];
    const auto root_text = list_stream::dump_compact(root_body);
    expect(root_text.find("},26,0,1,{0,2,3},0,1,{0,2,3},0,0,") != std::string::npos,
        "vertical center edge 4 and horizontal center edge 5 must populate incoming buckets 1 and 3: " + root_text);
    const auto decoded = form_stream::decode_document(encoded.value(), "CenterTargets");
    expect(decoded.ok(), decoded ? "center targets must decode" : decoded.diagnostics().front().message);
    const auto* roundtrip = decoded.value().find_control(model::ObjectId{2});
    expect(roundtrip != nullptr && roundtrip->position.bindings.anchors.size() == 1 &&
               roundtrip->position.bindings.anchors.front().target_coordinate == model::BindingCoordinate::horizontal_center &&
               roundtrip->position.bindings.anchors.front().proportional.has_value() &&
               roundtrip->position.bindings.anchors.front().proportional->coordinate == model::BindingCoordinate::vertical_center,
        "both center target enum values must round-trip independently");
    const auto* roundtrip_label = decoded.value().find_control(model::ObjectId{9});
    expect(roundtrip_label != nullptr && roundtrip_label->position.bindings.anchors.front().target_coordinate == model::BindingCoordinate::vertical_center &&
               roundtrip_label->position.bindings.anchors.front().proportional->coordinate == model::BindingCoordinate::horizontal_center,
        "center targets on controls must round-trip independently");

    model::OrdinaryFormDocument centered_source(document.form());
    for (const auto& source_control : document.collections().controls) {
        auto centered_control = source_control;
        if (centered_control.id == model::ObjectId{2}) {
            centered_control.position.bindings.anchors.front().coordinate = model::BindingCoordinate::vertical_center;
        }
        centered_source.add_control(std::move(centered_control));
    }
    expect_failure(form_stream::encode_document(centered_source), "OOF1122", "$/Position/Bindings/coordinate",
        "center coordinates must remain unsupported as sources");

    auto encoded_center_source = encoded.value();
    auto& center_source_geometry = encoded_center_source.items[1].items[2].items[2].items[1].items[3];
    center_source_geometry.items[11] = center_source_geometry.items[9];
    center_source_geometry.items[9] = list_stream::parse("{0,{2,-1,6,0},{2,-1,6,0}}");
    expect_failure(form_stream::decode_document(encoded_center_source, "CenterTargets"), "OOF1122",
        "$/Position/Bindings/coordinate", "center coordinates must remain unsupported as decoded sources");

    auto invalid_incoming = encoded.value();
    bool changed = false;
    std::string bad_source_edge;
    const auto corrupt_source_edge = [&](auto&& self, list_stream::ListValue& value) -> void {
        if (value.is_list && value.items.size() == 3 && !value.items[0].is_list && value.items[0].atom == "0" &&
            !value.items[1].is_list && value.items[1].atom == "2" && !value.items[2].is_list && value.items[2].atom == "3") {
            value.items[2] = list_stream::ListValue::raw_atom(bad_source_edge);
            changed = true;
            return;
        }
        for (auto& item : value.items) self(self, item);
    };
    for (const auto source_edge : {"4", "5"}) {
        changed = false;
        bad_source_edge = source_edge;
        invalid_incoming = encoded.value();
        auto& root_body = invalid_incoming.items[1].items[2].items[1].items[1];
        corrupt_source_edge(corrupt_source_edge, root_body);
        expect(changed, "test fixture must contain an incoming source edge tuple");
        expect(!form_stream::decode_document(invalid_incoming, "CenterTargets"),
            std::string("incoming source edge ") + source_edge + " must be rejected");
    }
}

void test_page_boundary_position_codec() {
    const auto boundaries = list_stream::parse(
        "{{2,6,1,1,1,0,0,0,0},{2,6,0,1,2,0,0,0,0},"
        "{2,296,1,1,3,0,0,40,0},{2,197,0,1,4,0,0,2,0}}");
    const model::ControlRef owner{model::ObjectId{2}};
    auto decoded = form_stream::decode_page_position(boundaries, 0, owner);
    expect(decoded.ok(), "proven Page boundary shape must decode to named Position");
    const auto& position = decoded.value();
    expect(position.left.value() == 6 && position.top.value() == 6 &&
               position.width.value() == 290 && position.height.value() == 191,
        "Page right/bottom coordinates must become dimensions, not be mislabeled as width/height");
    expect(position.bindings.anchors.size() == 2 &&
               position.bindings.anchors[0].target == owner &&
               position.bindings.anchors[0].coordinate == model::BindingCoordinate::right &&
               position.bindings.anchors[0].offset.value() == -40 &&
               position.bindings.anchors[1].coordinate == model::BindingCoordinate::bottom &&
               position.bindings.anchors[1].offset.value() == -2,
        "Page runtime constraints must be distinct from static rectangle coordinates");
    auto rebuilt = form_stream::encode_page_position(position, 0, owner);
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(boundaries),
        "named Page Position must rebuild the proven boundary records without retained source bytes");

    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "PageBoundary";
    form.children.emplace_back(owner);
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode panel{owner.id(), "Tabs", model::PanelPayload{}};
    panel.children.emplace_back(model::PageRef{model::ObjectId{1}});
    document.add_control(std::move(panel));
    model::Page page;
    page.id = model::ObjectId{1};
    page.name = "First";
    page.position.set(position);
    document.add_page(std::move(page));
    auto xml = oof::source::serialize_form_xml(document);
    expect(xml.ok(), "named Page boundary model must serialize as public XML");
    auto reparsed = oof::source::parse_form_xml(xml.value());
    expect(reparsed.ok(), "named Page boundary XML must parse independently");
    auto xml_rebuilt = form_stream::encode_page_position(
        reparsed.value().find_page(model::ObjectId{1})->position.value(), 0, owner);
    expect(xml_rebuilt.ok() && list_stream::dump_compact(xml_rebuilt.value()) == list_stream::dump_compact(boundaries),
        "Page boundaries must survive model -> XML -> model -> codec without source storage");
    const std::string original_offset = "offset=\"-40\"";
    const auto offset_at = xml.value().find(original_offset);
    expect(offset_at != std::string::npos, "Page runtime constraint must be a named editable XML offset");
    auto edited_xml = xml.value();
    edited_xml.replace(offset_at, original_offset.size(), "offset=\"-4\"");
    auto xml_edit = oof::source::parse_form_xml(edited_xml);
    expect(xml_edit.ok(), "editing the named Page offset must remain valid XML");
    auto edited_boundaries = form_stream::encode_page_position(
        xml_edit.value().find_page(model::ObjectId{1})->position.value(), 0, owner);
    expect(edited_boundaries.ok() && edited_boundaries.value().items[2].items[7].atom == "4" &&
               edited_boundaries.value().items[2].items[1].atom == "296",
        "named Page offset editing must preserve independent static coordinates");
    expect(!form_stream::encode_page_position(position, 0),
        "nested Page bindings must not silently become Form bindings");
    expect(!form_stream::decode_page_position(boundaries, 1, owner),
        "Page index mismatch must be rejected");

    auto invalid = boundaries;
    invalid.items[2].items[1] = list_stream::ListValue::raw_atom("5");
    expect(!form_stream::decode_page_position(invalid, 0, owner),
        "inverted Page rectangle must be rejected");
    invalid = boundaries;
    invalid.items[0].items[7] = list_stream::ListValue::raw_atom("1");
    expect(!form_stream::decode_page_position(invalid, 0, owner),
        "unproven Left constraint must be rejected");
    invalid = boundaries;
    invalid.items[2].items[7] = list_stream::ListValue::raw_atom("-2147483648");
    expect(!form_stream::decode_page_position(invalid, 0, owner),
        "Page offset negation overflow must be rejected");

    auto edited = position;
    edited.width.set(330);
    edited.bindings.anchors[0].offset.set(-4);
    auto encoded_edit = form_stream::encode_page_position(edited, 0, owner);
    expect(encoded_edit.ok() && encoded_edit.value().items[2].items[1].atom == "336" &&
               encoded_edit.value().items[2].items[7].atom == "4",
        "editing named static width and runtime offset must affect independent boundary values");
    edited.bindings.anchors.clear();
    expect(!form_stream::encode_page_position(edited, 0, owner),
        "missing Page constraints must not be inferred from coordinates");
}

void test_page_table_codec() {
    model::Page page;
    page.id = model::ObjectId{1};
    page.name = "Страница1";
    model::LocalizedStringValue default_title;
    default_title.items.push_back({"ru", "Страница1"});
    page.title.set(default_title);

    const auto table = form_stream::encode_page_table({page});
    expect(table.ok(), table ? "default Page table must encode" : table.diagnostics().front().message);
    const auto expected = list_stream::parse(
        R"({1,1,{6,{1,1,{"ru","Страница1"}},{10,0,{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},{4,0,{0},"",-1,-1,1,0,""},100,0,0,0,0,0},-1,1,1,"Страница1",1,{4,4,{0},4},{4,4,{0},4},{8,3,0,1,100},1}})");
    expect(list_stream::dump_compact(table.value()) == list_stream::dump_compact(expected),
        "Page table writer must match the confirmed default row exactly");

    const auto decoded = form_stream::decode_page_table(expected, 40);
    expect(decoded.ok() && decoded.value().size() == 1 &&
               decoded.value()[0].id == model::ObjectId{40} && decoded.value()[0].name == "Страница1" &&
               decoded.value()[0].title.value() == default_title &&
               decoded.value()[0].visible.value() && decoded.value()[0].enabled.value(),
        "default Page table must decode named properties and assign caller-selected IDs");

    auto edited = page;
    edited.id = model::ObjectId{8};
    edited.name = "Operations";
    edited.title.set(model::LocalizedStringValue{{{"ru", "Операции"}, {"en", "Operations"}}});
    edited.visible.set(false);
    edited.enabled.set(false);
    const auto edited_table = form_stream::encode_page_table({edited});
    expect(edited_table.ok(), "named Page Name, multilingual Title, Visible, and Enabled must encode");
    const auto edited_roundtrip = form_stream::decode_page_table(edited_table.value(), 80);
    expect(edited_roundtrip.ok() && edited_roundtrip.value()[0].id == model::ObjectId{80} &&
               edited_roundtrip.value()[0].name == "Operations" &&
               edited_roundtrip.value()[0].title.value() == edited.title.value() &&
               !edited_roundtrip.value()[0].visible.value() && !edited_roundtrip.value()[0].enabled.value(),
        "Page table roundtrip must preserve all localizations and supported edits");

    const auto two_pages = form_stream::encode_page_table({page, edited});
    expect(two_pages.ok(), "multiple Page rows must encode");
    const auto two_pages_decoded = form_stream::decode_page_table(two_pages.value(), 100);
    expect(two_pages_decoded.ok() && two_pages_decoded.value().size() == 2 &&
               two_pages_decoded.value()[0].id == model::ObjectId{100} &&
               two_pages_decoded.value()[1].id == model::ObjectId{101},
        "Page table must preserve row order and allocate consecutive internal IDs");

    auto malformed = expected;
    malformed.items[1] = list_stream::ListValue::raw_atom("2");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1102", "$/Pages",
        "Page table count that disagrees with row arity must fail");
    malformed = expected;
    malformed.items[2].items.pop_back();
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1102", "$/Pages/2",
        "Page rows with wrong arity must fail");
    malformed = expected;
    malformed.items[2].items[0] = list_stream::ListValue::raw_atom("7");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1106", "$/Pages/2/0",
        "unknown Page record versions must fail");
    malformed = expected;
    malformed.items[2].items[2].items[0] = list_stream::ListValue::raw_atom("11");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1114", "$/Pages/2/2",
        "unproven Page style values must fail");

    auto duplicate_title = page;
    duplicate_title.title.set(model::LocalizedStringValue{{{"ru", "Первый"}, {"ru", "Второй"}}});
    expect_failure(form_stream::encode_page_table({duplicate_title}), "OOF1122", "$/Pages/0/1/1",
        "duplicate Title language keys must be rejected by the writer");
    malformed = expected;
    malformed.items[2].items[1] = list_stream::parse("{1,2,{\"ru\",\"Первый\"},{\"ru\",\"Второй\"}}");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1122", "$/Pages/2/1/1",
        "duplicate Title language keys must be rejected by the reader");

    model::Page empty_language = page;
    empty_language.title.set(model::LocalizedStringValue{{{"", "Title without language tag"}}});
    const auto empty_language_table = form_stream::encode_page_table({empty_language});
    expect(empty_language_table.ok(), "a single empty language tag remains accepted like the XML parser");
    const auto empty_language_decoded = form_stream::decode_page_table(empty_language_table.value(), 1);
    expect(empty_language_decoded.ok() &&
               empty_language_decoded.value()[0].title.value() == empty_language.title.value(),
        "an empty language tag must survive the Page table codec");

    auto empty_name = page;
    empty_name.name.clear();
    expect_failure(form_stream::encode_page_table({empty_name}), "OOF1122", "$/Pages/0",
        "empty Page names must fail on encode");
    malformed = expected;
    malformed.items[2].items[6] = list_stream::ListValue::string_atom("");
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1115", "$/Pages/2/6",
        "empty Page names must fail on decode");

    auto duplicate_id = edited;
    duplicate_id.id = page.id;
    expect_failure(form_stream::encode_page_table({page, duplicate_id}), "OOF1122", "$/Pages/1",
        "duplicate Page IDs must fail on encode");
    const auto names_table = form_stream::encode_page_table({page, edited});
    expect(names_table.ok(), "distinct Page names must encode");
    malformed = names_table.value();
    malformed.items[3].items[6] = list_stream::ListValue::string_atom(page.name);
    expect_failure(form_stream::decode_page_table(malformed, 1), "OOF1122", "$/Pages/3/6",
        "duplicate Page names within one owner table must fail on decode");
    auto duplicate_name = page;
    duplicate_name.id = model::ObjectId{9};
    expect_failure(form_stream::encode_page_table({page, duplicate_name}), "OOF1122", "$/Pages/1",
        "duplicate Page names within one owner table must fail on encode");

    expect_failure(form_stream::decode_page_table(expected, 0), "OOF1122", "$/Pages",
        "zero starting Page IDs must fail");
    const auto overflow = form_stream::decode_page_table(
        two_pages.value(), std::numeric_limits<std::uint64_t>::max());
    expect_failure(overflow, "OOF1122", "$/Pages",
        "Page ID assignment overflow must fail before allocating rows");
}



void test_chart_value_tooltip_named_pair_and_xml_text() {
    const std::string tooltip_one = R"(Север: "Первая" & <10>)";
    const std::string tooltip_two = R"(Вторая: Ёжик, 東京 "20")" + std::string{"\r\n"} + "Строка 2";
    const std::string xml = R"XML(<Form id="1" name="Hints" ordinaryFormVersion="2.1"><ChildItems>
      <Chart id="2" name="Metrics"><Position/>
        <Series><ChartSeries id="2"><Text>Series</Text><Color kind="absolute" red="20"/><Marker type="ChartMarkerType" member="Auto"/></ChartSeries></Series>
        <Points><ChartPoint id="1"><Text>First</Text><Color kind="absolute" red="30"/></ChartPoint><ChartPoint id="3"><Text>Second</Text><Color kind="absolute" red="40"/></ChartPoint></Points>
        <Values><ChartValue seriesRef="2" pointRef="1"><Number>10</Number><ToolTip>Север: "Первая" &amp; &lt;10&gt;</ToolTip></ChartValue>
          <ChartValue seriesRef="2" pointRef="3"><Number>20</Number><ToolTip>Вторая: Ёжик, 東京 "20"&#xD;&#xA;Строка 2</ToolTip></ChartValue></Values>
      </Chart></ChildItems></Form>)XML";
    const auto parsed=source::parse_form_xml(xml);
    expect(parsed.ok(), "named ChartValue ToolTip must parse after its typed value");
    const auto& model_chart=std::get<model::ChartPayload>(parsed.value().find_control(model::ObjectId{2})->payload);
    expect(model_chart.values[0].tooltip==tooltip_one && model_chart.values[1].tooltip==tooltip_two,
        "XML character references must preserve Unicode, quotes and logical CRLF without transport conversion");
    const auto encoded=form_stream::encode_document(parsed.value());
    expect(encoded.ok(), "named Tooltip strings must encode both representations");
    const auto& info=encoded.value().items[1].items[2].items[2].items[1].items[3];
    // Независимая пара из опыта 19327, первая подсказка без преобразования переводов строк.
    const auto observed=list_stream::parse(R"({{1,{1,1,{"#","Север: ""Первая"" & <10>"}},0},0})");
    expect(info.items[153].atom==tooltip_one && list_stream::dump_compact(info.items[246])==list_stream::dump_compact(observed),
        "writer must match both atom and independent observed single-fragment representation");
    const auto decoded=form_stream::decode_document(encoded.value(),"Hints");
    expect(decoded.ok(), "paired plain string ToolTip must decode");
    const auto canonical=source::serialize_form_xml(decoded.value());
    expect(canonical.ok() && canonical.value().find("&#xD;")!=std::string::npos && canonical.value().find("&amp;")!=std::string::npos,
        "XML writer must escape carriage returns and markup in Tooltip text");
    const auto reparsed=source::parse_form_xml(canonical.value());
    expect(reparsed.ok(), "canonical ToolTip XML must parse again");
    const auto& re_chart=std::get<model::ChartPayload>(reparsed.value().find_control(model::ObjectId{2})->payload);
    expect(re_chart.values[0].tooltip==tooltip_one && re_chart.values[1].tooltip==tooltip_two,
        "ToolTip XML roundtrip must preserve exact logical string bytes");
    const auto rebuilt=form_stream::encode_document(reparsed.value());
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value())==list_stream::dump_compact(encoded.value()),
        "named Tooltip XML must rebuild the complete native Chart stream without a baseline");
    auto undefined_xml=xml;
    undefined_xml.replace(undefined_xml.find("<Number>10</Number>"),19,"<Undefined>undefined</Undefined>");
    const auto undefined_source=source::parse_form_xml(undefined_xml);
    expect(undefined_source.ok(), undefined_source.ok() ? "" : "Undefined with ToolTip: " + undefined_source.diagnostics().front().message + " actual=" + undefined_source.diagnostics().front().actual);
    const auto undefined_native=form_stream::encode_document(undefined_source.value());
    expect(undefined_native.ok(), "Undefined with ToolTip must encode without a numeric value");
    const auto undefined_decoded=form_stream::decode_document(undefined_native.value(),"UndefinedHint");
    expect(undefined_decoded.ok(), "Undefined with ToolTip must decode");
    const auto& undefined_chart=std::get<model::ChartPayload>(undefined_decoded.value().find_control(model::ObjectId{2})->payload);
    expect(std::holds_alternative<model::UndefinedValue>(undefined_chart.values[0].value)
        && undefined_chart.values[0].tooltip==tooltip_one,
        "ToolTip must preserve the Undefined variant rather than supplying a numeric value");
    const auto undefined_canonical=source::serialize_form_xml(undefined_decoded.value());
    expect(undefined_canonical.ok(), "Undefined with ToolTip must serialize to named XML");
    const auto undefined_reparsed=source::parse_form_xml(undefined_canonical.value());
    expect(undefined_reparsed.ok(), "Undefined ToolTip canonical XML must parse");
    const auto undefined_rebuilt=form_stream::encode_document(undefined_reparsed.value());
    expect(undefined_rebuilt.ok() && list_stream::dump_compact(undefined_rebuilt.value())==list_stream::dump_compact(undefined_native.value()),
        "Undefined ToolTip must rebuild the complete native stream through XML");
    auto duplicate_xml=xml;
    duplicate_xml.insert(duplicate_xml.find("</ToolTip>")+10,"<ToolTip>duplicate</ToolTip>");
    expect(!source::parse_form_xml(duplicate_xml).ok(), "duplicate ToolTip must fail instead of losing a value");
    auto reordered_xml=xml;
    reordered_xml.erase(reordered_xml.find("<Number>10</Number>"),19);
    reordered_xml.insert(reordered_xml.find("</ToolTip>")+10,"<Number>10</Number>");
    expect(!source::parse_form_xml(reordered_xml).ok(), "ToolTip before the typed value must fail the public XML contract");
    auto mismatched=encoded.value();
    mismatched.items[1].items[2].items[2].items[1].items[3].items[153]=list_stream::ListValue::string_atom("different");
    expect(!form_stream::decode_document(mismatched,"Mismatch").ok(), "Tooltip atom and fragment mismatch must fail");
    auto multiple=encoded.value();
    multiple.items[1].items[2].items[2].items[1].items[3].items[246].items[0].items[1]=
        list_stream::parse(R"({1,2,{"#","Север: ""Первая"" & <10>"},{"#","extra"}})");
    expect(!form_stream::decode_document(multiple,"Formatted").ok(), "multifragment Tooltip must not be silently flattened");
    auto unknown_tag=encoded.value();
    unknown_tag.items[1].items[2].items[2].items[1].items[3].items[246].items[0].items[1].items[2].items[0]=list_stream::ListValue::string_atom("unknown");
    expect(!form_stream::decode_document(unknown_tag,"UnknownText").ok(), "unsupported text fragment kinds must fail");
    auto empty_source=parsed.value();
    auto& empty_chart=std::get<model::ChartPayload>(const_cast<model::ControlNode*>(empty_source.find_control(model::ObjectId{2}))->payload);
    for(auto& cell:empty_chart.values)cell.tooltip.clear();
    const auto empty_encoded=form_stream::encode_document(empty_source);
    expect(empty_encoded.ok(), "empty named Tooltip must use proven default representation");
    const auto& empty_info=empty_encoded.value().items[1].items[2].items[2].items[1].items[3];
    expect(empty_info.items[153].atom.empty() && list_stream::dump_compact(empty_info.items[246])=="{{1,{1,0},0},0}",
        "omitted and empty model Tooltip must emit default empty atom and no_text companion");
    auto alternative=empty_encoded.value();
    alternative.items[1].items[2].items[2].items[1].items[3].items[246]=list_stream::parse(R"({{1,{1,1,{"#",""}},0},0})");
    expect(!form_stream::decode_document(alternative,"UnknownEmpty").ok(), "unproven explicit-empty alternative companion must fail");
    const auto empty_xml=source::serialize_form_xml(empty_source);
    expect(empty_xml.ok() && empty_xml.value().find("<ToolTip>")==std::string::npos, "default empty ToolTip must be omitted in canonical XML");
    auto explicit_empty_xml=xml;const auto tip_at=explicit_empty_xml.find("<ToolTip>");const auto tip_end=explicit_empty_xml.find("</ToolTip>",tip_at);
    explicit_empty_xml.replace(tip_at,tip_end+10-tip_at,"<ToolTip/>");
    const auto explicit_empty=source::parse_form_xml(explicit_empty_xml);
    expect(explicit_empty.ok() && std::get<model::ChartPayload>(explicit_empty.value().find_control(model::ObjectId{2})->payload).values[0].tooltip.empty(),
        "explicit empty XML must mean the canonical proven empty default");
}

void test_independent_tab_order_observed_geometry_and_guards() {
    // Независимые снимки опыта 67445: изменено только OrderSecond.ПорядокОбхода.
    for (const auto& [literal, ordinal, tab] : std::vector<std::tuple<std::string, std::uint32_t, std::int32_t>>{
        {R"({8,120,12,220,37,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,3,0,25},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,3,2,100},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},1,{0,3,1},0,1,{0,3,3},0,0,0,0,1,1,0,0})",1,1},
        {R"({8,10,12,110,37,1,{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,90,0,25},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,90,2,100},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},{0,{2,-1,6,0},{2,-1,6,0}},1,{0,90,1},0,1,{0,90,3},0,0,0,0,0,2,0,0})",0,2}}) {
        const auto geometry = list_stream::parse(literal);
        const form_stream::GeometryContext context{model::ControlRef{model::ObjectId{42}},0,ordinal};
        const auto decoded = form_stream::decode_control_geometry(geometry, context);
        expect(decoded.ok() && decoded.value().position.tab_order.value() == std::optional<std::int32_t>{tab},
            "observed geometry must decode named TabOrder independently of unchanged child ordinal");
        const auto rebuilt = form_stream::encode_control_geometry(decoded.value(), context);
        expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(geometry),
            "observed TabOrder geometry must round-trip all coordinates and bindings exactly");
    }
    model::Form form; form.id=model::ObjectId{1}; form.name="IndependentOrder";
    form.children={model::ControlRef{model::ObjectId{90}},model::ControlRef{model::ObjectId{3}},model::ControlRef{model::ObjectId{40}}};
    model::OrdinaryFormDocument document(std::move(form));
    for (const auto id : {90ULL,3ULL,40ULL})
        document.add_control(model::ControlNode{model::ObjectId{id},"Button"+std::to_string(id),model::ButtonPayload{}});
    auto* first=const_cast<model::ControlNode*>(document.find_control(model::ObjectId{90}));
    auto* second=const_cast<model::ControlNode*>(document.find_control(model::ObjectId{3}));
    first->position.tab_order.set(std::optional<std::int32_t>{2});
    second->position.tab_order.set(std::optional<std::int32_t>{1});
    const auto encoded=form_stream::encode_document(document);
    expect(encoded.ok(), "independent TabOrder permutation with implicit third value must encode");
    const auto decoded=form_stream::decode_document(encoded.value(),"IndependentOrder");
    expect(decoded.ok() && decoded.value().form().children==document.form().children &&
        decoded.value().find_control(model::ObjectId{90})->position.tab_order.value()==std::optional<std::int32_t>{2} &&
        !decoded.value().find_control(model::ObjectId{40})->position.tab_order.is_explicit(),
        "unsorted IDs and explicit TabOrder must preserve ChildItems and omit default orders");
    const auto xml=oof::source::serialize_form_xml(decoded.value());
    const auto parsed=oof::source::parse_form_xml(xml.value());
    const auto rebuilt=form_stream::encode_document(parsed.value());
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value())==list_stream::dump_compact(encoded.value()),
        "named XML TabOrder permutation must rebuild without a baseline");
    for (const auto invalid : {0,-1,4,2,3}) {
        second->position.tab_order.set(std::optional<std::int32_t>{invalid});
        expect(!form_stream::encode_document(document).ok(),"invalid or duplicate TabOrder including implicit defaults must fail");
    }
    second->position.tab_order.set(std::nullopt);
    expect(!form_stream::encode_document(document).ok(),"explicit undefined TabOrder must fail");
    for (const auto invalid : {"0","4","2","1.5"}) {
        auto corrupt=encoded.value(); auto& geometry=corrupt.items[1].items[2].items[2].items[1].items[3];
        geometry.items[geometry_tail_start(geometry)+2]=list_stream::ListValue::raw_atom(invalid);
        expect(!form_stream::decode_document(corrupt,"InvalidOrder").ok(),"invalid native TabOrder must not be normalized");
    }
}

void test_owner_aware_control_geometry_codec() {
    const model::ControlRef owner{model::ObjectId{42}};
    const form_stream::GeometryContext context{owner, 3, 7};
    form_stream::ControlGeometry source;
    source.position.width.set(120);
    source.position.height.set(24);
    model::AnchorBinding binding;
    binding.coordinate = model::BindingCoordinate::right;
    binding.target_coordinate = model::BindingCoordinate::right;
    binding.target = owner;
    model::AnchorBindingTarget proportional;
    proportional.coordinate = model::BindingCoordinate::left;
    proportional.target = owner;
    binding.proportional = proportional;
    source.position.bindings.anchors.push_back(binding);
    source.incoming[0].push_back({9, 1});

    const auto encoded = form_stream::encode_control_geometry(source, context);
    expect(encoded.ok(), encoded ? "nested geometry must encode" : encoded.diagnostics().front().message);
    const auto cursor = geometry_tail_start(encoded.value());
    expect(encoded.value().items[cursor].atom == "3" && encoded.value().items[cursor + 1].atom == "7" &&
               encoded.value().items[cursor + 2].atom == "8",
        "geometry must encode the explicit page and local sibling ordinal");
    expect(list_stream::dump_compact(encoded.value().items[9]) == "{0,{2,0,3,0},{2,0,2,0}}",
        "primary and proportional references to the owning Panel must encode as target zero");

    const auto decoded = form_stream::decode_control_geometry(encoded.value(), context);
    expect(decoded.ok(), decoded ? "nested geometry must decode" : decoded.diagnostics().front().message);
    const auto& roundtrip = decoded.value();
    expect(roundtrip.position.bindings.anchors.size() == 1 &&
               roundtrip.position.bindings.anchors[0].target == owner &&
               roundtrip.position.bindings.anchors[0].proportional.has_value() &&
               roundtrip.position.bindings.anchors[0].proportional->target == owner,
        "target-zero primary and proportional bindings must resolve to the owning Panel");
    expect(roundtrip.incoming == source.incoming,
        "owner-aware geometry roundtrip must preserve named incoming dependencies");
    expect(roundtrip.position.width.value() == 120 && roundtrip.position.height.value() == 24,
        "nested geometry must preserve named Position dimensions");

    auto bad = encoded.value();
    bad.items[cursor] = list_stream::ListValue::raw_atom("4");
    expect_failure(form_stream::decode_control_geometry(bad, context), "OOF1114",
        "$/" + std::to_string(cursor),
        "geometry with a different page index must fail against its owner context");
    bad = encoded.value();
    bad.items[cursor + 2] = list_stream::ListValue::raw_atom("9");
    const auto changed_tab = form_stream::decode_control_geometry(bad, context);
    expect(changed_tab.ok() && changed_tab.value().position.tab_order.value() == std::optional<std::int32_t>{9},
        "standalone geometry must decode independent TabOrder without assuming sibling count");
    bad = encoded.value();
    bad.items[cursor + 1] = list_stream::ListValue::raw_atom("6");
    expect_failure(form_stream::decode_control_geometry(bad, context), "OOF1114",
        "$/" + std::to_string(cursor + 1),
        "geometry with a different local ordinal must be rejected");

    auto form_target = source;
    form_target.position.bindings.anchors[0].target.reset();
    expect_failure(form_stream::encode_control_geometry(form_target, context), "OOF1122",
        "$/Position/Bindings/targetId", "Form target cannot be encoded in nested geometry");
    auto invalid_target = source;
    invalid_target.position.bindings.anchors[0].target = model::ControlRef{model::ObjectId{0}};
    expect_failure(form_stream::encode_control_geometry(invalid_target, context), "OOF1122",
        "$/Position/Bindings/targetId", "explicit primary target ID zero must not collapse to the owner sentinel");
    invalid_target = source;
    invalid_target.position.bindings.anchors[0].proportional->target =
        model::ControlRef{model::ObjectId{0}};
    expect_failure(form_stream::encode_control_geometry(invalid_target, context), "OOF1122",
        "$/Position/Bindings/ProportionalBinding/targetId",
        "explicit proportional target ID zero must not collapse to the owner sentinel");

    auto unsupported_position = source;
    unsupported_position.position.default_control.set(std::optional<bool>{true});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "explicit DefaultControl must be rejected by standalone geometry encoding");
    unsupported_position = source;
    unsupported_position.position.tab_order.set(std::optional<std::int32_t>{2});
    const auto explicit_tab = form_stream::encode_control_geometry(unsupported_position, context);
    expect(explicit_tab.ok() && explicit_tab.value().items[cursor + 2].atom == "2",
        "explicit TabOrder must encode independently of the child ordinal");
    unsupported_position = source;
    unsupported_position.position.z_order.set(std::optional<std::int32_t>{3});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "explicit ZOrder must be rejected by standalone geometry encoding");
    unsupported_position = source;
    unsupported_position.position.collapse.set(
        std::optional<model::EnumerationValue>{model::EnumerationValue{"Collapse", "None"}});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "explicit Collapse must be rejected by standalone geometry encoding");
    unsupported_position = source;
    unsupported_position.position.bindings.dimensions.push_back(
        model::DimensionBinding{model::BindingDimension::width, {}});
    expect_failure(form_stream::encode_control_geometry(unsupported_position, context), "OOF1122",
        "$/Position", "dimension bindings must be rejected by standalone geometry encoding");

    auto invalid_incoming = source;
    invalid_incoming.incoming[0][0].source_control_id = 0;
    expect_failure(form_stream::encode_control_geometry(invalid_incoming, context), "OOF1122",
        "$/Position/Incoming/0/0", "incoming source ID zero must be rejected by the writer");
    invalid_incoming = source;
    invalid_incoming.incoming[0][0].source_control_id =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
    expect_failure(form_stream::encode_control_geometry(invalid_incoming, context), "OOF1122",
        "$/Position/Incoming/0/0", "incoming source IDs outside int64 must be rejected by the writer");
    invalid_incoming = source;
    invalid_incoming.incoming[0][0].source_edge = 4;
    expect_failure(form_stream::encode_control_geometry(invalid_incoming, context), "OOF1122",
        "$/Position/Incoming/0/0", "unsupported incoming source edges must be rejected by the writer");
    const form_stream::GeometryContext invalid_owner{model::ControlRef{model::ObjectId{0}}, 3, 7};
    expect_failure(form_stream::encode_control_geometry(source, invalid_owner), "OOF1122",
        "$/Position/0", "a zero Panel owner ID must be rejected");
    const form_stream::GeometryContext root_context{form_stream::FormGeometryOwner{}, 0, 2};
    form_target.position.bindings.anchors.clear();
    form_target.position.bindings.anchors.push_back(binding);
    form_target.position.bindings.anchors[0].target.reset();
    form_target.position.bindings.anchors[0].proportional->target.reset();
    const auto root_geometry = form_stream::encode_control_geometry(form_target, root_context);
    expect(root_geometry.ok(), "Form-target primary and proportional references must remain supported at root");
    const auto root_roundtrip = form_stream::decode_control_geometry(root_geometry.value(), root_context);
    expect(root_roundtrip.ok() &&
               !root_roundtrip.value().position.bindings.anchors[0].target.has_value() &&
               !root_roundtrip.value().position.bindings.anchors[0].proportional->target.has_value(),
        "root target-zero references must retain Form semantics");

    auto foreign = source;
    foreign.position.bindings.anchors[0].target = model::ControlRef{model::ObjectId{77}};
    foreign.position.bindings.anchors[0].proportional->target = model::ControlRef{model::ObjectId{77}};
    const auto foreign_encoded = form_stream::encode_control_geometry(foreign, context);
    expect(foreign_encoded.ok() && foreign_encoded.value().items[9].items[1].items[1].atom == "77" &&
               foreign_encoded.value().items[9].items[2].items[1].atom == "77",
        "references to controls other than the owner must retain their explicit IDs");
    const auto foreign_decoded = form_stream::decode_control_geometry(foreign_encoded.value(), context);
    expect(foreign_decoded.ok() &&
               foreign_decoded.value().position.bindings.anchors[0].target == model::ControlRef{model::ObjectId{77}} &&
               foreign_decoded.value().position.bindings.anchors[0].proportional->target ==
                   model::ControlRef{model::ObjectId{77}},
        "references other than target zero must decode as their explicit control IDs");
}


void test_data_processor_form_extension_named_round_trip_and_guards() {
    auto parsed = oof::source::parse_form_xml(R"XML(<Form id="1" name="Processor" ordinaryFormVersion="2.1"><Height>120</Height><Width>240</Width><MainAttribute attributeId="2"/><DataProcessorFormExtension/><Attributes><Attribute id="2" name="ProcessorObject"><TypeDomain><Entry term="object" typeUuid="11111111-1111-1111-1111-111111111111"/></TypeDomain></Attribute></Attributes></Form>)XML");
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    expect(parsed.value().form().extension == model::FormExtensionKind::data_processor,
        "Named XML must select the processing form controller");
    const auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().message);
    expect(list_stream::dump_compact(encoded.value().items[2].items[0]) == "{2}",
        "MainAttribute must persist as a named reference independent of attribute flags");
    const auto& extension = encoded.value().items[3];
    expect(extension.items.size() == 3 && extension.items[0].atom == "59d6c227-97d3-46f6-84a0-584c5a2807e1" &&
        list_stream::dump_compact(extension.items[2]) == "{2,0,{0,0},{0},1}",
        "Fresh encoding must instantiate the proven processing controller default");
    const auto decoded = form_stream::decode_document(encoded.value(), "Processor");
    expect(decoded.ok() && decoded.value().form().extension == model::FormExtensionKind::data_processor &&
        decoded.value().form().main_attribute.id() == model::ObjectId{2},
        "The controller must survive the binary object model round-trip");
    const auto xml = oof::source::serialize_form_xml(decoded.value());
    expect(xml.ok() && xml.value().find("<DataProcessorFormExtension/>") != std::string::npos &&
        xml.value().find("59d6c227") == std::string::npos,
        "Public XML must retain the named extension without exposing its stream or CLSID");
    const auto rebuilt = form_stream::encode_document(oof::source::parse_form_xml(xml.value()).value());
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
        "Named XML must rebuild the extension without any input binary");
    for (const auto slot : {1u, 4u}) {
        auto corrupt = encoded.value();
        corrupt.items[3].items[2].items[slot] = list_stream::ListValue::raw_atom(slot == 1 ? "1" : "0");
        expect_failure(form_stream::decode_document(corrupt, "Processor"), "OOF1114", "$/3/2",
            "Unmapped controller settings must not be dropped");
    }
    auto unsupported = encoded.value();
    unsupported.items[2].items[0] = list_stream::ListValue::list({list_stream::ListValue::raw_atom("-1")});
    expect_failure(form_stream::decode_document(unsupported, "Processor"), "OOF1122", "$/2/0",
        "A present extension requires a selected main object during decoding");
    unsupported = encoded.value();
    unsupported.items[3] = list_stream::parse("{00000000-0000-0000-0000-000000000000,0}");
    expect_failure(form_stream::decode_document(unsupported, "Processor"), "OOF1122", "$/2/0",
        "A main object context requires a supported extension during decoding");
    for (const auto flag : {1u, 2u}) {
        unsupported = encoded.value();
        unsupported.items[2].items[2].items[1].items[flag] = list_stream::ListValue::raw_atom("1");
        expect_failure(form_stream::decode_document(unsupported, "Processor"), "OOF1122", "$/2/0",
            "Nondefault main object flags must not yield unrebuildable XML");
    }
    unsupported = encoded.value();
    unsupported.items[2].items[2].items[1].items[5] = list_stream::parse("{\"Pattern\",{\"R\",11111111-1111-1111-1111-111111111111}}");
    expect_failure(form_stream::decode_document(unsupported, "Processor"), "OOF1122", "$/2/0",
        "A reference type cannot replace the concrete main object context");
    auto unknown = encoded.value();
    unknown.items[3].items[0] = list_stream::ListValue::raw_atom("11111111-1111-1111-1111-111111111111");
    expect_failure(form_stream::decode_document(unknown, "Processor"), "OOF1106", "$/3/0",
        "An unknown extension must not be interpreted as a processing form");
    auto malformed = encoded.value();
    malformed.items[3].items[1] = list_stream::ListValue::raw_atom("0");
    expect_failure(form_stream::decode_document(malformed, "Processor"), "OOF1106", "$/3/1",
        "Present controller must have a consistent presence marker");
    auto form = parsed.value().form();
    form.extension.reset();
    parsed.value().set_form(std::move(form));
    expect_failure(form_stream::encode_document(parsed.value()), "OOF1122", "$/MainAttribute",
        "A main object context must not silently use an absent extension");
    form = parsed.value().form();
    form.main_attribute = model::AttributeRef{};
    parsed.value().set_form(std::move(form));
    const auto removed = form_stream::encode_document(parsed.value());
    expect(removed.ok() && list_stream::dump_compact(removed.value().items[3]) ==
        "{00000000-0000-0000-0000-000000000000,0}", "Removing the named extension must remove its controller");
}

void test_default_schema_fields_fresh_xml_geometry_and_rejections() {
    const auto parsed = oof::source::parse_form_xml(R"XML(<Form id="1" name="DefaultSchemas" ordinaryFormVersion="2.1"><ChildItems>
      <GraphicalSchemaField id="19" name="Flow"><Position><Top>20</Top><Height>90</Height><Left>10</Left><TabOrder>2</TabOrder><Width>140</Width></Position></GraphicalSchemaField>
      <GeographicalSchemaField id="2" name="Map"><Position><Top>25</Top><Height>120</Height><Left>170</Left><TabOrder>1</TabOrder><Width>180</Width><Bindings><AnchorBinding coordinate="right" targetCoordinate="right" offset="-20"/></Bindings></Position></GeographicalSchemaField>
    </ChildItems></Form>)XML");
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().path + ": " + encoded.diagnostics().front().message);
    const auto& records = encoded.value().items[1].items[2].items[2];
    expect(records.items.size() == 3 && records.items[1].items[0].atom == "ad37194e-555e-4305-b718-5dca84baf145" &&
        records.items[1].items.size() == 7 && records.items[2].items[0].atom == "42248403-7748-49da-b782-e4438fd7bff3" &&
        records.items[2].items.size() == 6, "Map separate document must shift its geometry slot without shifting Flow geometry");
    const auto decoded = form_stream::decode_document(encoded.value(), "DefaultSchemas");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().path + ": " + decoded.diagnostics().front().message);
    expect(decoded.value().form().children == parsed.value().form().children,
        "Physical ID sorting must preserve logical order for both schema fields");
    const auto* map = decoded.value().find_control(model::ObjectId{2});
    const auto* flow = decoded.value().find_control(model::ObjectId{19});
    expect(map && flow && map->name == "Map" && flow->name == "Flow" && map->position.left.value() == 170 &&
        map->position.width.value() == 180 && flow->position.top.value() == 20 &&
        map->position.tab_order.value() == std::optional<std::int32_t>{1} &&
        flow->position.tab_order.value() == std::optional<std::int32_t>{2} &&
        map->position.bindings.anchors.size() == 1,
        "Names, rectangles, independent TabOrder and owner binding must survive fresh serialization");
    const auto rebuilt = form_stream::encode_document(decoded.value());
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
        "Default schema fields must rebuild byte exact without a baseline");
    const auto xml = oof::source::serialize_form_xml(decoded.value());
    expect(xml.ok() && xml.value().find("<GraphicalSchemaField") != std::string::npos &&
        xml.value().find("<GeographicalSchemaField") != std::string::npos &&
        xml.value().find("ListStream") == std::string::npos,
        "Both default controls must use named public XML");
    auto sibling_bound = parsed.value();
    auto* sibling_flow = const_cast<model::ControlNode*>(sibling_bound.find_control(model::ObjectId{19}));
    model::AnchorBinding sibling_anchor;
    sibling_anchor.coordinate = model::BindingCoordinate::left;
    sibling_anchor.target = model::ControlRef{model::ObjectId{2}};
    sibling_anchor.target_coordinate = model::BindingCoordinate::right;
    sibling_anchor.offset.set(-5);
    sibling_flow->position.bindings.anchors.push_back(sibling_anchor);
    const auto sibling_encoded = form_stream::encode_document(sibling_bound);
    expect(sibling_encoded.ok(), "New schema fields must participate in sibling bindings");
    auto missing_incoming = sibling_encoded.value();
    auto& map_geometry = missing_incoming.items[1].items[2].items[2].items[1].items[4];
    expect(map_geometry.items[15].atom == "1" && map_geometry.items[16].is_list,
        "Map geometry must contain the Flow incoming right-edge tuple");
    map_geometry.items[15] = list_stream::ListValue::raw_atom("0");
    map_geometry.items.erase(map_geometry.items.begin() + 16);
    const auto missing_result = form_stream::decode_document(missing_incoming, "DefaultSchemas");
    expect(!missing_result && missing_result.diagnostics().front().path == "$/1/2/2/1/4/3",
        "Map incoming graph error must point to geometry slot4, not its separate document slot3");
    for (const auto record_index : {1u, 2u}) {
        auto nondefault = encoded.value();
        auto& record = nondefault.items[1].items[2].items[2].items[record_index];
        if (record_index == 1) record.items[3].items[4] = list_stream::ListValue::raw_atom("1");
        else record.items[2].items[2].items[0].items[2] = list_stream::ListValue::raw_atom("1");
        const auto partial_schema = form_stream::decode_document(nondefault, "DefaultSchemas");
        const auto expected_id = record_index == 1 ? model::ObjectId{2} : model::ObjectId{19};
        const auto expected_name = record_index == 1 ? "Map" : "Flow";
        const auto* partial_schema_control = partial_schema ?
            partial_schema.value().find_control(expected_id) : nullptr;
        expect(partial_schema.ok() && !partial_schema.value().reconstruction_complete() &&
                   std::any_of(partial_schema.diagnostics().begin(), partial_schema.diagnostics().end(),
                       [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
                   partial_schema_control != nullptr && partial_schema_control->name == expected_name &&
                   partial_schema_control->position.left.value() ==
                       (record_index == 1 ? std::optional<std::int32_t>{170} : std::optional<std::int32_t>{10}),
            "valid unknown schema profile content must warn and preserve identity and position");
        auto unknown_property = parsed.value();
        auto* control = const_cast<model::ControlNode*>(unknown_property.find_control(
            model::ObjectId{record_index == 1 ? 2u : 19u}));
        control->properties().set_explicit(model::PropertyId::from_name("BorderColor"), model::ColorValue{});
        expect(!form_stream::encode_document(unknown_property),
            "Unverified explicit property must not silently become a default");
    }
}


void test_partial_complex_and_spreadsheet_profiles_rebuild_from_xml() {
    const auto check_partial_xml_build = [](const model::OrdinaryFormDocument& partial,
                                            model::ObjectId control_id,
                                            std::string_view control_name) {
        const auto xml = source::serialize_form_xml(partial);
        expect(xml.ok() && xml.value().find("reconstructionComplete=\"false\"") != std::string::npos,
            "partial named model must serialize with reconstruction completeness metadata");
        const auto parsed = source::parse_form_xml(xml.value());
        expect(parsed.ok() && !parsed.value().reconstruction_complete() && !parsed.diagnostics().empty() &&
                   parsed.diagnostics().front().severity == oof::DiagnosticSeverity::warning,
            "partial XML must parse back with an incompleteness warning");
        const auto* restored = parsed.value().find_control(control_id);
        expect(restored != nullptr && restored->name == control_name,
            "partial XML must retain the known control identity");
        const auto encoded = form_stream::encode_document(parsed.value());
        expect(encoded.ok(), "partial XML model must remain encodable through the primary form writer");
        const auto built = oof::save_form_bin(parsed.value());
        expect(built.ok() && std::any_of(built.diagnostics().begin(), built.diagnostics().end(),
                   [](const auto& diagnostic) {
                       return diagnostic.severity == oof::DiagnosticSeverity::warning &&
                           diagnostic.message.find("unsupported source properties were not restored") != std::string::npos;
                   }),
            "partial XML model must build Form.bin with the source-property warning");
    };

    const auto pivot_xml = source::parse_form_xml(R"XML(<Form id="1" name="PartialPivot" ordinaryFormVersion="2.1"><ChildItems>
      <PivotChart id="77" name="Pivot"><Position><Top>25</Top><Height>140</Height><Left>40</Left><Width>200</Width></Position></PivotChart>
    </ChildItems></Form>)XML");
    expect(pivot_xml.ok(), "Pivot fixture must parse from named XML");
    auto pivot_stream = form_stream::encode_document(pivot_xml.value());
    expect(pivot_stream.ok(), "Pivot fixture must encode before profile mutation");
    pivot_stream.value().items[1].items[2].items[2].items[1].items[2].items[2].items[3] =
        list_stream::ListValue::raw_atom("99");
    const auto partial_pivot = form_stream::decode_document(pivot_stream.value(), "PartialPivot");
    const auto* pivot = partial_pivot ? partial_pivot.value().find_control(model::ObjectId{77}) : nullptr;
    expect(partial_pivot.ok() && !partial_pivot.value().reconstruction_complete() &&
               std::any_of(partial_pivot.diagnostics().begin(), partial_pivot.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               pivot != nullptr && pivot->position.left.value() == 40,
        "unknown Pivot profile must warn while retaining known geometry");
    check_partial_xml_build(partial_pivot.value(), model::ObjectId{77}, "Pivot");

    model::Form form;
    form.id = model::ObjectId{1};
    form.name = "PartialSheet";
    form.children = {model::ControlRef{model::ObjectId{9}}};
    model::OrdinaryFormDocument document(std::move(form));
    model::ControlNode sheet{model::ObjectId{9}, "Sheet", model::SpreadsheetDocumentFieldPayload{}};
    std::get<model::SpreadsheetDocumentFieldPayload>(sheet.payload).cells = {
        {1, 1, "Kept cell", std::nullopt}};
    sheet.position.left.set(18);
    document.add_control(std::move(sheet));
    auto sheet_stream = form_stream::encode_document(document);
    expect(sheet_stream.ok(), "SpreadsheetDocumentField fixture must encode before profile mutation");
    sheet_stream.value().items[1].items[2].items[2].items[1].items[2].items[14].items[22] =
        list_stream::ListValue::raw_atom("1");
    const auto partial_sheet = form_stream::decode_document(sheet_stream.value(), "PartialSheet");
    const auto* sheet_control = partial_sheet ? partial_sheet.value().find_control(model::ObjectId{9}) : nullptr;
    const auto* sheet_payload = sheet_control == nullptr ? nullptr :
        std::get_if<model::SpreadsheetDocumentFieldPayload>(&sheet_control->payload);
    expect(partial_sheet.ok() && !partial_sheet.value().reconstruction_complete() &&
               std::any_of(partial_sheet.diagnostics().begin(), partial_sheet.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               sheet_payload != nullptr && sheet_payload->cells ==
                   std::vector<model::SpreadsheetDocumentCell>{{1, 1, "Kept cell", std::nullopt}} &&
               sheet_control->position.left.value() == 18,
        "unknown SpreadsheetDocumentField view profile must warn while retaining known cells and geometry");
    check_partial_xml_build(partial_sheet.value(), model::ObjectId{9}, "Sheet");
}


void test_pivot_chart_default_factory_round_trip_and_rejections() {
    const auto parsed = oof::source::parse_form_xml(R"XML(<Form id="1" name="DefaultPivot" ordinaryFormVersion="2.1"><ChildItems>
      <PivotChart id="77" name="Pivot"><Position><Top>25</Top><Height>140</Height><Left>40</Left><Width>200</Width></Position></PivotChart>
    </ChildItems></Form>)XML");
    expect(parsed.ok(), parsed ? "" : parsed.diagnostics().front().message);
    const auto encoded = form_stream::encode_document(parsed.value());
    expect(encoded.ok(), encoded ? "" : encoded.diagnostics().front().message);
    const auto& record = encoded.value().items[1].items[2].items[2].items[1];
    const auto& info = record.items[2];
    expect(record.items[0].atom == "a26da99e-184a-4823-b0d6-62816d38dc4e" && record.items.size() == 6 &&
        info.items.size() == 13 && info.items[0].atom == "3" && info.items[1].items[2].items.size() == 386 &&
        info.items[1].items[2].items[4].atom == "4" && info.items[2].items[3].items[1].atom == "0",
        "Default Pivot includes the native sample renderer and no source values, not an empty ordinary Chart wrapper");
    const auto decoded = form_stream::decode_document(encoded.value(), "DefaultPivot");
    expect(decoded.ok(), decoded ? "" : decoded.diagnostics().front().message);
    const auto* pivot = decoded.value().find_control(model::ObjectId{77});
    expect(pivot && pivot->kind() == model::ControlKind::pivot_chart && pivot->name == "Pivot" &&
        pivot->position.left.value() == 40 && pivot->position.height.value() == 140,
        "Pivot identity and position must be independent of the factory sample renderer");
    const auto rebuilt = form_stream::encode_document(decoded.value());
    expect(rebuilt.ok() && list_stream::dump_compact(rebuilt.value()) == list_stream::dump_compact(encoded.value()),
        "Named default Pivot must rebuild exactly without a baseline");
    for (const auto slot : {3u, 4u, 5u, 6u, 7u, 8u, 9u, 12u}) {
        auto changed = encoded.value();
        changed.items[1].items[2].items[2].items[1].items[2].items[slot] = list_stream::ListValue::raw_atom("99");
        const auto partial = form_stream::decode_document(changed, "DefaultPivot");
        const auto* partial_pivot = partial ? partial.value().find_control(model::ObjectId{77}) : nullptr;
        expect(partial.ok() && !partial.value().reconstruction_complete() &&
                   std::any_of(partial.diagnostics().begin(), partial.diagnostics().end(),
                       [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
                   partial_pivot != nullptr && partial_pivot->name == "Pivot" &&
                   partial_pivot->position.left.value() == 40,
            "valid unknown Pivot profile data must warn and preserve identity and position");
    }
    auto changed_renderer = encoded.value();
    changed_renderer.items[1].items[2].items[2].items[1].items[2].items[1].items[2].items[4] = list_stream::ListValue::raw_atom("0");
    const auto partial_renderer = form_stream::decode_document(changed_renderer, "DefaultPivot");
    const auto* partial_pivot = partial_renderer ?
        partial_renderer.value().find_control(model::ObjectId{77}) : nullptr;
    expect(partial_renderer.ok() && !partial_renderer.value().reconstruction_complete() &&
               std::any_of(partial_renderer.diagnostics().begin(), partial_renderer.diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "OOF1140"; }) &&
               partial_pivot != nullptr && partial_pivot->name == "Pivot",
        "changed Pivot renderer must warn and preserve the named Pivot control");
}

}  // namespace

int main() {
    try {
        test_data_processor_form_extension_named_round_trip_and_guards();
        test_pivot_chart_default_factory_round_trip_and_rejections();
        test_default_schema_fields_fresh_xml_geometry_and_rejections();
        test_partial_complex_and_spreadsheet_profiles_rebuild_from_xml();
        test_command_bar_owner_pair_and_strict_profile();
        test_command_bar_five_named_properties_and_invalid_variants();
        test_command_bar_named_action_source_references();
        test_command_bar_creation_state_and_strict_record_guards();
        test_command_bar_border_named_round_trip_and_guards();
        test_command_bar_colors_named_round_trip_and_guards();
        test_command_bar_default_button_round_trip_and_guards();
        test_captured_command_bar_control_record_literal();
        test_outer_format_probe();
        test_layout_probe();
        test_runtime_envelope();
        test_attributes();
        test_attribute_encode_validation();
        test_empty_attributes_allocator_header();
        test_attribute_allocator_is_separate_from_control_ids();
        test_usual_group_named_record_round_trip_and_rejections();
        test_gantt_chart_named_storage_round_trip();
        test_gantt_standard_palette_wraparound();
        test_captured_table_column_record();
        test_table_read_only_runtime_flags();
        test_table_first_in_group_observed_metadata();
        test_table_first_in_group_fresh_model_xml_bin();
        test_table_column_name_and_data_path_runtime_slots();
        test_table_column_choice_and_check_box_profiles();
        test_captured_table_packet_rejections_and_alternate_deflate();
        test_two_button_sibling_index();
        test_multiple_top_level_buttons_round_trip();
        test_form_close_strict_action_guards();
        test_shared_action_metadata_policies_and_rejections();
        test_button_click_action_metadata_warning_round_trip();
        test_menu_action_values_and_optional_overrides_round_trip();
        test_button_multiline_round_trip_and_validation();
        test_button_alignments_and_tooltip_round_trip();
        test_check_box_tooltip_round_trip_and_validation();
        test_choice_field_static_profile_round_trip_and_validation();
        test_check_box_font_round_trip_and_validation();
        test_button_colors_round_trip_and_validation();
        test_button_picture_enums_round_trip_and_validation();
        test_button_menu_mode_round_trip_and_validation();
        test_named_button_menu_round_trip_and_invalid_references();
        test_button_menu_client_interface_variant_round_trip_and_validation();
        test_automatic_button_text_without_explicit_text_round_trip();
        test_all_standard_button_pictures_round_trip_without_assets();
        test_button_external_picture_assets_round_trip();
        test_button_then_label_decoration_round_trip();
        test_label_border_color_round_trip();
        test_label_partial_reconstruction_warning_and_build();
        test_control_unknown_common_state_warning_and_build();
        test_label_decoration_observed_center_right_records();
        test_label_enabled_and_tooltip_round_trip();
        test_picture_decoration_default_enabled_tooltip_round_trip_and_rejections();
        test_splitter_observed_record_and_named_codec();
        test_fresh_checkbox_stream_decode();
        test_radio_button_basic_observed_record_and_rejections();
        test_radio_button_group_order_inherited_decimal_selection_and_boundaries();
        test_html_document_field_output_platform_record_and_guards();
        test_calendar_field_enabled_round_trip_and_rejections();
        test_text_document_field_persisted_profile_and_rejections();
        test_calendar_field_begin_display_period();
        test_calendar_field_captured_begin_period_record_decode();
        test_calendar_field_observed_record_decode();
        test_fresh_progress_bar_runtime_record_and_rejections();
        test_dendrogram_orientation_named_codec();
        test_dendrogram_strict_native_fixture();
        test_dendrogram_named_graph_candidate_roundtrip();
        test_dendrogram_native_three_item_two_link_cursor_fixture();
        test_dendrogram_unbounded_branching_graph_round_trip();
        test_track_bar_observed_record_and_named_round_trip();
        test_list_box_value_list_data_path_and_supported_properties();
        test_list_box_captured_runtime_record_roundtrip();
        test_progress_data_path_mixed_with_existing_links();
        test_button_label_input_field_round_trip();
        test_input_field_tooltip_and_format_round_trip();
        test_input_field_alignment_and_choice_list_height_round_trip();
        test_single_input_field_round_trip();
        test_spreadsheet_cell_controls();
        test_spreadsheet_document_field_round_trip();
        test_two_input_fields_round_trip();
        test_six_reordered_controls_use_logical_geometry_ordinals();
        test_root_pages_round_trip_with_page_local_control_order();
        test_owned_panel_colors_and_repeated_background();
        test_owned_panel_auto_tab_order_round_trip();
        test_recursive_panel_pages_keep_owner_geometry_separate();
        test_manual_bindings_are_not_silently_discarded();
        test_anchor_bindings_round_trip_and_fanout();
        test_center_target_coordinates();
        test_page_boundary_position_codec();
        test_page_table_codec();
        test_owner_aware_control_geometry_codec();
        test_independent_tab_order_observed_geometry_and_guards();
        test_chart_value_tooltip_named_pair_and_xml_text();
        test_chart_named_dense_roundtrip_with_sibling_geometry();
        test_chart_empty_render_cache_normalization_and_guards();
        test_platform_empty_document_fixture();
    } catch (const std::exception& error) {
        std::cerr << "form stream tests: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "form stream tests: PASS\n";
    return 0;
}
