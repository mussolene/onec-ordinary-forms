#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oof/form_bin.hpp"
#include "oof/model/ordinary_form.hpp"
#include "oof/source/form_xml.hpp"

namespace {
using namespace oof::model;
constexpr std::array kinds{RuntimeCollectionKind::array, RuntimeCollectionKind::structure, RuntimeCollectionKind::map};

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error("runtime collection: " + std::string(message));
}

RuntimeCollectionRef collection(RuntimeCollectionKind kind) {
    auto value = RuntimeCollectionRef::create(kind);
    require(value.has_value() && value->valid(), "collection creation must return a live handle");
    return std::move(*value);
}

RuntimeCollectionRef reference(const std::optional<PropertyValue>& value) {
    const auto* ref = value ? std::get_if<RuntimeCollectionRef>(&*value) : nullptr;
    require(ref && ref->valid(), "collection getter must retain a live collection reference");
    return *ref;
}

bool write_slot(const RuntimeCollectionRef& value, PropertyValue item) {
    switch (*value.kind()) {
        case RuntimeCollectionKind::array:
            return *value.size() == 0 ? value.append(std::move(item)) : value.set_at(0, std::move(item));
        case RuntimeCollectionKind::structure: return value.set_field("Slot", std::move(item));
        case RuntimeCollectionKind::map: return value.set_key(std::string{"slot"}, std::move(item));
    }
    return false;
}

std::optional<PropertyValue> read_slot(const RuntimeCollectionRef& value) {
    switch (*value.kind()) {
        case RuntimeCollectionKind::array: return value.at(0);
        case RuntimeCollectionKind::structure: return value.field("Slot");
        case RuntimeCollectionKind::map: return value.find_key(std::string{"slot"});
    }
    return std::nullopt;
}

OrdinaryFormDocument form(std::string name) {
    Form value;
    value.id = ObjectId{1};
    value.name = std::move(name);
    return OrdinaryFormDocument{std::move(value)};
}

void test_document_matrix() {
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    const std::array properties{PropertyId::from_name("UniqueKey"), PropertyId::from_name("ChoiceInitialValue")};
    for (const auto kind : kinds) for (unsigned scenario = 0; scenario != 4; ++scenario) {
        auto value = collection(kind);
        require(write_slot(value, std::int64_t{1}), "matrix source must accept its first value");
        auto first = form("First");
        auto second = form("Second");
        if (scenario == 0) {
            for (const auto property : properties) {
                require(first.set_prop_val(owner, property, value) == PropertyMutationStatus::applied_runtime_only &&
                    second.set_prop_val(owner, property, value) == PropertyMutationStatus::applied_runtime_only,
                    "both forms must accept the same runtime handle");
                auto from_first = reference(first.get_prop_val(owner, property).value);
                auto from_second = reference(second.get_prop_val(owner, property).value);
                require(from_first == value && from_second == value, "property assignment must preserve object identity");
                require(write_slot(from_first, std::int64_t{2}) && read_slot(from_second) == PropertyValue{std::int64_t{2}},
                    "mutation through a property getter must be visible through the other form");
            }
        } else if (scenario == 1) {
            auto equal_contents = collection(kind);
            require(write_slot(equal_contents, std::int64_t{1}) && equal_contents != value,
                "equal collection contents must not collapse distinct identities");
            require(first.set_prop_val(owner, properties[0], value) == PropertyMutationStatus::applied_runtime_only &&
                second.set_prop_val(owner, properties[0], equal_contents) == PropertyMutationStatus::applied_runtime_only,
                "forms must accept independent equal collections");
            require(reference(first.get_prop_val(owner, properties[0]).value) !=
                reference(second.get_prop_val(owner, properties[0]).value), "form storage must not intern collections by contents");
        } else if (scenario == 2) {
            const auto lifetime = value.observe_lifetime();
            for (const auto property : properties) {
                require(first.set_prop_val(owner, property, value) == PropertyMutationStatus::applied_runtime_only &&
                    second.set_prop_val(owner, property, value) == PropertyMutationStatus::applied_runtime_only,
                    "reset scenario bindings must be installed");
            }
            value = {};
            for (const auto property : properties)
                require(first.reset_prop_val(owner, property) == PropertyMutationStatus::reset_runtime_only &&
                    first.get_prop_val(owner, property).value == PropertyValue{UndefinedValue{}},
                    "reset must drop the first form binding and restore Undefined");
            require(!lifetime.expired(), "reset must preserve another form's ownership");
            for (const auto property : properties)
                require(second.reset_prop_val(owner, property) == PropertyMutationStatus::reset_runtime_only,
                    "the final form binding must be removable");
            require(lifetime.expired(), "last reset must release the referenced collection");
        } else {
            const auto xml_before = oof::source::serialize_form_xml(first);
            const auto bin_before = oof::save_form_bin(first);
            require(xml_before.ok() && bin_before.ok(), "baseline form must serialize");
            require(write_slot(value, value), "runtime graph must accept a self cycle");
            for (const auto property : properties)
                require(first.set_prop_val(owner, property, value) == PropertyMutationStatus::applied_runtime_only,
                    "runtime properties must accept cyclic collection handles");
            const auto xml_after = oof::source::serialize_form_xml(first);
            const auto bin_after = oof::save_form_bin(first);
            require(xml_after.ok() && bin_after.ok() && xml_after.value() == xml_before.value() &&
                bin_after.value() == bin_before.value(), "runtime bindings must not change public XML or Form.bin bytes");
            TypedValueBudget budget;
            require(!validate_typed_value(PropertyValue{value}, budget), "source typed values must reject runtime graph references");
            ArrayValue owned;
            owned.items.emplace_back(value);
            budget = {};
            require(!validate_typed_value(PropertyValue{owned}, budget),
                "an owned source snapshot must not conceal a runtime graph reference");
            const auto loaded = oof::load_form_bin(bin_after.value());
            require(loaded.ok() && loaded.value().get_prop_val(owner, properties[0]).value == PropertyValue{UndefinedValue{}} &&
                loaded.value().get_prop_val(owner, properties[1]).value == PropertyValue{UndefinedValue{}},
                "binary loading must not recreate runtime graph bindings");
        }
    }
}

void test_invalid_operations_and_handle_lifecycle() {
    PropertyValue interrupted{true};
    try {
        interrupted.emplace<std::string>(std::numeric_limits<std::size_t>::max(), 'x');
    } catch (const std::length_error&) {
        if (interrupted.valueless_by_exception()) {
            require(validate_runtime_value(interrupted) == RuntimeValueValidation::invalid,
                "a variant left without a value after construction failure must be rejected without termination");
            auto document = form("InterruptedValue");
            const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
            require(document.set_prop_val(owner, PropertyId::from_name("UniqueKey"), interrupted) ==
                PropertyMutationStatus::invalid_value,
                "runtime assignment must reject an interrupted variant construction");
        }
    }
    RuntimeCollectionRef invalid;
    require(!invalid.valid() && !invalid.kind() && !invalid.size() && !invalid.fixed() &&
        invalid.observe_lifetime().expired() && !invalid.fixed_copy(), "default handle must be invalid");
    require(!invalid.append(true) && !invalid.set_at(0, true) && !invalid.set_field("A", true) &&
        !invalid.set_key(true, true) && !invalid.erase_at(0) && !invalid.erase_field("A") &&
        !invalid.erase_key(true) && !invalid.clear() && !invalid.at(0) && !invalid.field("A") && !invalid.find_key(true),
        "invalid handles must reject access and mutation safely");
    require(!RuntimeCollectionRef::create(static_cast<RuntimeCollectionKind>(255)), "unknown collection kind must fail");
    const std::array<PropertyValue, 6> rejected{ArrayValue{}, StructureValue{}, MapValue{}, ValueListValue{}, RuntimeCollectionRef{},
        std::string(1, static_cast<char>(0xff))};
    for (const auto kind : kinds) {
        auto original = collection(kind);
        require(write_slot(original, std::int64_t{7}), "rejection fixture must contain a stable scalar");
        auto copy = original;
        require(copy == original, "copy construction must preserve identity");
        auto moved = std::move(copy);
        require(moved == original && !copy.valid(), "move construction must transfer the handle");
        auto target = collection(kind);
        const auto replaced = target.observe_lifetime();
        target = original;
        require(target == original && replaced.expired(), "copy assignment must release the previous collection");
        copy = std::move(target);
        require(copy == original && !target.valid(), "move assignment must transfer ownership");
        auto& original_alias = original;
        auto& copy_alias = copy;
        original = original_alias;
        copy = std::move(copy_alias);
        require(original.valid() && copy == original, "self copy and move assignment must retain a valid handle");
        for (const auto& item : rejected)
            require(!write_slot(original, item) && read_slot(original) == PropertyValue{std::int64_t{7}} && original.size() == 1,
                "invalid UTF-8, invalid references and owned snapshots must fail without modifying contents");
        if (kind == RuntimeCollectionKind::array) {
            require(!original.at(1) && !original.set_at(1, true) && !original.erase_at(1) &&
                !original.field("Slot") && !original.set_field("Slot", true) && !original.set_key(true, true),
                "array indices and foreign-kind operations must fail unchanged");
        } else if (kind == RuntimeCollectionKind::structure) {
            require(!original.at(0) && !original.append(true) && !original.set_at(0, true) &&
                !original.set_key(true, true) && !original.find_key(std::string{"Slot"}),
                "structure must reject array and map operations");
        } else {
            require(!original.at(0) && !original.append(true) && !original.set_at(0, true) &&
                !original.set_field("Slot", true) && !original.field("Slot") &&
                !original.set_key(std::string(1, static_cast<char>(0xff)), true) &&
                !original.set_key(ArrayValue{}, true) && !original.set_key(StructureValue{}, true) &&
                !original.set_key(MapValue{}, true),
                "map must reject wrong-kind operations and invalid UTF-8 keys");
        }
        require(original.size() == 1 && read_slot(original) == PropertyValue{std::int64_t{7}},
            "rejected operations must leave all collection state unchanged");
        auto document = form("RejectInvalid");
        const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
        require(document.set_prop_val(owner, PropertyId::from_name("UniqueKey"), RuntimeCollectionRef{}) ==
            PropertyMutationStatus::invalid_value && document.get_prop_val(owner, PropertyId::from_name("UniqueKey")).value ==
            PropertyValue{UndefinedValue{}}, "invalid collection assignment must preserve the runtime property");
    }
}

void test_cycles_and_node_collection() {
    for (const auto kind : kinds) {
        std::weak_ptr<const void> last_root;
        {
            auto cycle = collection(kind);
            last_root = cycle.observe_lifetime();
            require(write_slot(cycle, cycle), "all three kinds must support self cycles");
            require(reference(read_slot(cycle)) == cycle, "self-cycle getter must retain the same identity");
        }
        require(last_root.expired(), "self cycle must be freed after its last external root disappears");
        auto keeper = collection(RuntimeCollectionKind::array);
        auto cycle = collection(kind);
        const auto detached = cycle.observe_lifetime();
        require(write_slot(cycle, cycle) && keeper.append(cycle), "keeper must retain a child cycle across regions");
        cycle = {};
        require(!detached.expired(), "a reachable child cycle must remain alive");
        require(keeper.clear() && detached.expired() && keeper.valid() && keeper.size() == 0,
            "a detached child cycle must be collected while an unrelated keeper in the merged region stays alive");
    }
    auto keeper = collection(RuntimeCollectionKind::array);
    auto first = collection(RuntimeCollectionKind::array);
    auto second = collection(RuntimeCollectionKind::structure);
    auto third = collection(RuntimeCollectionKind::map);
    const auto a = first.observe_lifetime(), b = second.observe_lifetime(), c = third.observe_lifetime();
    require(first.append(second) && second.set_field("Next", third) && third.set_key(std::string{"next"}, first) &&
        keeper.append(first), "mutual cycles must connect three initially separate collection regions");
    first = {}; second = {}; third = {};
    require(!a.expired() && !b.expired() && !c.expired(), "reachable mutual cycle must survive local handle release");
    require(keeper.clear() && a.expired() && b.expired() && c.expired() && keeper.valid(),
        "detaching a mutual cycle must collect every node independently of its region keeper");
    auto child = collection(RuntimeCollectionKind::structure);
    const auto child_lifetime = child.observe_lifetime();
    require(keeper.append(child), "cross-region edge must be installable");
    child = {};
    auto borrowed = reference(keeper.at(0));
    require(keeper.clear() && !child_lifetime.expired(), "a getter-created handle must independently root the child");
    borrowed = {};
    require(child_lifetime.expired(), "releasing the final getter handle must collect the detached node");
    auto key = collection(RuntimeCollectionKind::array);
    const auto key_lifetime = key.observe_lifetime();
    auto map = collection(RuntimeCollectionKind::map);
    require(key.append(key) && map.set_key(key, std::int64_t{1}), "map key edges must retain collection identity");
    key = {};
    require(!key_lifetime.expired() && map.clear() && key_lifetime.expired() && map.valid(),
        "map-key-only cycles must be collected when the key entry is removed");
}

void test_map_identity_keys_and_structure_names() {
    auto map = collection(RuntimeCollectionKind::map);
    for (const auto kind : kinds) {
        auto key = collection(kind), equal_contents = collection(kind);
        require(write_slot(key, std::int64_t{1}) && write_slot(equal_contents, std::int64_t{1}) &&
            map.set_key(key, std::string{"found"}), "object key must be accepted by identity");
        require(map.find_key(key) == PropertyValue{std::string{"found"}} && !map.find_key(equal_contents),
            "a distinct equal collection must not match an object key");
        require(write_slot(key, std::int64_t{2}) && map.find_key(key) == PropertyValue{std::string{"found"}},
            "object key identity must remain stable after key mutation");
        require(map.erase_key(key) && !map.find_key(key), "identity key removal must use the same object handle");
    }
    auto structure = collection(RuntimeCollectionKind::structure);
    require(structure.set_field("Latin", std::int64_t{1}) && structure.field("lATIN") == PropertyValue{std::int64_t{1}} &&
        structure.set_field("ПОЛЕ", std::int64_t{2}) && structure.field("поле") == PropertyValue{std::int64_t{2}} &&
        structure.set_field("Ёж", std::int64_t{3}) && structure.field("ёЖ") == PropertyValue{std::int64_t{3}} &&
        structure.set_field("Еж", std::int64_t{4}) && structure.field("еж") == PropertyValue{std::int64_t{4}} &&
        structure.field("ёж") == PropertyValue{std::int64_t{3}}, "structure names must fold Latin and Cyrillic case while preserving Е and Ё");
    require(structure.set_field("_Поле2", true) && structure.set_field("latin", std::int64_t{5}) && structure.size() == 5,
        "case variants must update an existing field rather than append a duplicate");
    for (const auto& name : {std::string{"2Bad"}, std::string{"Bad-Name"}, std::string{}, std::string(1, static_cast<char>(0xff))})
        require(!structure.set_field(name, true) && !structure.field(name) && !structure.erase_field(name) && structure.size() == 5,
            "invalid structure identifiers must fail without changing existing fields");
    require(structure.erase_field("пОлЕ") && !structure.field("ПОЛЕ"), "field removal must use the same case-folding rules");
}

void test_shallow_fixed_structure_and_map() {
    for (const auto kind : {RuntimeCollectionKind::structure, RuntimeCollectionKind::map}) {
        auto source = collection(kind), child = collection(RuntimeCollectionKind::array);
        require(child.append(std::int64_t{1}) && write_slot(source, child), "fixed-copy fixture must retain a child");
        const auto fixed = source.fixed_copy();
        require(fixed && fixed->fixed() && *fixed != source && reference(read_slot(*fixed)) == child,
            "FixedStructure and FixedMap must copy only their outer collection and preserve child identity");
        require(child.set_at(0, std::int64_t{2}) && reference(read_slot(*fixed)).at(0) == PropertyValue{std::int64_t{2}},
            "mutating a shallow child must be visible through its fixed outer collection");
        require(!write_slot(*fixed, true) && !fixed->clear() &&
            (kind == RuntimeCollectionKind::structure ? !fixed->erase_field("Slot") : !fixed->erase_key(std::string{"slot"})),
            "fixed outer collection must reject every applicable mutation");
        require(write_slot(source, true) && reference(read_slot(*fixed)) == child,
            "replacing the mutable source slot must not replace the fixed copy's slot");
    }
}

void test_fixed_array_conversion_and_bounded_failures() {
    auto child = collection(RuntimeCollectionKind::array), grandchild = collection(RuntimeCollectionKind::array);
    auto structure = collection(RuntimeCollectionKind::structure), map = collection(RuntimeCollectionKind::map);
    auto fixed_source = collection(RuntimeCollectionKind::array);
    require(grandchild.append(std::int64_t{3}) && child.append(std::int64_t{1}) && child.append(grandchild) &&
        fixed_source.append(std::int64_t{5}) && structure.set_field("Slot", child) && map.set_key(std::string{"slot"}, child), "mixed fixed-array fixture must initialize");
    const auto already_fixed = fixed_source.fixed_copy();
    require(already_fixed && already_fixed->fixed(), "a simple array must convert to FixedArray");
    auto root = collection(RuntimeCollectionKind::array);
    require(root.append(child) && root.append(child) && root.append(structure) && root.append(map) && root.append(*already_fixed),
        "mutable array must accept repeated and heterogeneous child references");
    const auto fixed = root.fixed_copy();
    require(fixed && fixed->fixed() && *fixed != root && fixed->size() == 5, "FixedArray conversion must create a fixed outer identity");
    auto first = reference(fixed->at(0)), second = reference(fixed->at(1));
    require(first.fixed() && second.fixed() && first != child && second != child && first != second &&
        reference(fixed->at(0)) == first, "each mutable Array occurrence must become an independent stable FixedArray clone");
    auto first_grandchild = reference(first.at(1)), second_grandchild = reference(second.at(1));
    require(first_grandchild.fixed() && second_grandchild.fixed() && first_grandchild != grandchild &&
        second_grandchild != grandchild && first_grandchild != second_grandchild &&
        grandchild.set_at(0, std::int64_t{7}) && first_grandchild.at(0) == PropertyValue{std::int64_t{3}},
        "mutable-array cloning must recurse through each occurrence without memoizing shared descendants");
    require(reference(fixed->at(2)) == structure && reference(fixed->at(3)) == map &&
        reference(fixed->at(4)) == *already_fixed, "other collection kinds and already-fixed arrays must preserve identity");
    require(child.set_at(0, std::int64_t{9}) && first.at(0) == PropertyValue{std::int64_t{1}} &&
        second.at(0) == PropertyValue{std::int64_t{1}} && !first.set_at(0, true) && !fixed->append(true) && !fixed->clear(),
        "converted mutable-array children must be detached from the source and reject mutation");
    require(reference(structure.field("Slot")) == child && reference(map.find_key(std::string{"slot"})) == child,
        "conversion must not recursively rewrite mutable arrays reachable through Structure or Map");
    auto cross_kind_root = collection(RuntimeCollectionKind::array);
    auto cross_kind_child = collection(RuntimeCollectionKind::structure);
    require(cross_kind_root.append(cross_kind_child) && cross_kind_child.set_field("Back", cross_kind_root),
        "cross-kind cycle fixture must initialize");
    const auto cross_kind_fixed = cross_kind_root.fixed_copy();
    require(cross_kind_fixed && reference(cross_kind_fixed->at(0)) == cross_kind_child &&
        reference(cross_kind_child.field("Back")) == cross_kind_root,
        "FixedArray conversion must stop at a Structure child instead of traversing its back edge");
    auto cycle = collection(RuntimeCollectionKind::array);
    require(cycle.append(cycle) && !cycle.fixed_copy() && cycle.size() == 1 && reference(cycle.at(0)) == cycle,
        "a mutable-array cycle must fail conversion atomically");
    auto depth = collection(RuntimeCollectionKind::array);
    require(depth.append(std::int64_t{1}), "depth fixture leaf must initialize");
    const auto leaf = depth.observe_lifetime();
    for (std::size_t index = 0; index < max_typed_value_depth + 2; ++index) {
        auto parent = collection(RuntimeCollectionKind::array);
        require(parent.append(depth), "bounded depth fixture must retain its prior root");
        depth = std::move(parent);
    }
    require(!depth.fixed_copy() && depth.valid() && depth.size() == 1 && !leaf.expired(),
        "conversion over the supported depth must fail without modifying the original graph");
    auto dag = collection(RuntimeCollectionKind::array);
    require(dag.append(std::int64_t{1}), "budget fixture leaf must initialize");
    for (unsigned level = 0; level < 20; ++level) {
        auto parent = collection(RuntimeCollectionKind::array);
        require(parent.append(dag) && parent.append(dag), "compact DAG must retain two occurrences of each child");
        dag = std::move(parent);
    }
    auto before = reference(dag.at(0));
    require(before == reference(dag.at(1)) && !dag.fixed_copy() && dag.size() == 2 &&
        reference(dag.at(0)) == before && reference(dag.at(1)) == before && !dag.fixed(),
        "independent array expansion beyond clone budget must fail atomically on a compact source DAG");
}

void test_existing_value_list_identity() {
    auto list = ValueListRef::create(ValueListValue{});
    auto independent = ValueListRef::create(ValueListValue{});
    require(list && independent && *list != *independent, "independent ValueLists must have different identities");
    auto first = form("ListFirst"), second = form("ListSecond");
    const OrdinaryFormDocument::ObjectKey owner{ObjectCategory::form, ObjectId{1}};
    for (const auto name : {"UniqueKey", "ChoiceInitialValue"}) {
        const auto property = PropertyId::from_name(name);
        require(first.set_prop_val(owner, property, *list) == PropertyMutationStatus::applied_runtime_only &&
            second.set_prop_val(owner, property, *list) == PropertyMutationStatus::applied_runtime_only,
            "runtime variants must reuse existing live ValueList handles");
        const auto stored = first.get_prop_val(owner, property).value;
        require(stored && std::get<ValueListRef>(*stored) == *list,
            "ValueList assignment must retain the original identity");
    }
    auto array = collection(RuntimeCollectionKind::array), map = collection(RuntimeCollectionKind::map);
    require(array.append(*list) && map.set_key(*list, true) && !map.find_key(*independent),
        "live collections must retain ValueList leaves and compare their map keys by identity");
    ValueListValue updated;
    updated.items.push_back(ValueListItem{1, std::int64_t{7}, std::string{"Seven"}, false, {}});
    require(list->replace(updated), "existing ValueList mutation must succeed");
    const auto leaf = array.at(0);
    require(leaf && std::get<ValueListRef>(*leaf).get()->items.size() == 1 &&
        std::get<ValueListRef>(*leaf).get()->items.front().value == PropertyValue{std::int64_t{7}} &&
        map.find_key(*list) == PropertyValue{true},
        "ValueList mutations must stay visible and must not invalidate identity keys");
}


void test_growing_collections_and_index_updates() {
    auto root = collection(RuntimeCollectionKind::array);
    std::vector<std::weak_ptr<const void>> observed;
    for (unsigned i = 0; i < 4096; ++i) {
        auto child = collection(RuntimeCollectionKind::array);
        if (i % 256 == 0) observed.push_back(child.observe_lifetime());
        require(root.append(std::move(child)), "temporary child append must retain each new identity");
    }
    require(root.size() == 4096 && reference(root.at(0)).valid() && reference(root.at(4095)).valid(),
        "growing parent must retain old and new children across region and capacity changes");
    require(root.clear(), "growing parent must clear");
    for (const auto& weak : observed) require(weak.expired(), "cleared temporary children must be reclaimed");
    for (const auto kind : {RuntimeCollectionKind::structure, RuntimeCollectionKind::map}) {
        auto values = collection(kind);
        for (unsigned i = 0; i < 512; ++i) {
            const auto name = "Поле" + std::to_string(i);
            require(kind == RuntimeCollectionKind::structure ? values.set_field(name, std::int64_t{i}) :
                values.set_key(name, std::int64_t{i}), "indexed collection must grow without losing values");
        }
        for (unsigned i = 0; i < 512; i += 2) {
            const auto name = "Поле" + std::to_string(i);
            require(kind == RuntimeCollectionKind::structure ? values.erase_field(name) : values.erase_key(name),
                "erasing a middle entry must update every shifted lookup");
        }
        require(values.size() == 256, "indexed deletion must preserve the remaining size");
        for (unsigned i = 0; i < 512; ++i) {
            const auto name = "Поле" + std::to_string(i);
            const auto found = kind == RuntimeCollectionKind::structure ? values.field(name) : values.find_key(name);
            require(i % 2 == 0 ? !found : found == PropertyValue{std::int64_t{i}},
                "lookups after repeated index shifts must preserve the exact surviving values");
        }
        for (unsigned i = 1; i < 512; i += 2) {
            const auto name = "Поле" + std::to_string(i);
            const auto replacement = -static_cast<std::int64_t>(i);
            require(kind == RuntimeCollectionKind::structure ? values.set_field(name, replacement) : values.set_key(name, replacement),
                "replacement must keep existing indexed identity");
            require((kind == RuntimeCollectionKind::structure ? values.field(name) : values.find_key(name)) == PropertyValue{replacement},
                "lookup after replacement must return the new value");
        }
        require(values.size() == 256, "replacement must not duplicate dictionary entries");
    }
}

}  // namespace

void run_runtime_collection_tests() {
    test_document_matrix();
    test_growing_collections_and_index_updates();
    test_existing_value_list_identity();
    test_invalid_operations_and_handle_lifecycle();
    test_cycles_and_node_collection();
    test_map_identity_keys_and_structure_names();
    test_shallow_fixed_structure_and_map();
    test_fixed_array_conversion_and_bounded_failures();
}
