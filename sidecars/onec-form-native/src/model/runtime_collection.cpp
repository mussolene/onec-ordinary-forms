#include "oof/model/ordinary_form.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <type_traits>

#include <unicode/uchar.h>
#include <unicode/stringoptions.h>
#include <unicode/utf8.h>

namespace oof::model {
namespace {

std::atomic<std::uint64_t> next_identity{1};

std::optional<std::uint64_t> allocate_identity() noexcept {
    auto candidate = next_identity.load(std::memory_order_relaxed);
    while (candidate != std::numeric_limits<std::uint64_t>::max()) {
        if (next_identity.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed))
            return candidate;
    }
    return std::nullopt;
}

std::optional<std::string> structure_name(std::string_view name) {
    if (name.empty() || name.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::nullopt;
    std::string folded;
    folded.reserve(name.size());
    std::int32_t position = 0;
    const auto length = static_cast<std::int32_t>(name.size());
    bool first = true;
    while (position < length) {
        UChar32 character;
        U8_NEXT(reinterpret_cast<const std::uint8_t*>(name.data()), position, length, character);
        if (character < 0 || (character != '_' && !(first ? u_isalpha(character) : u_isalnum(character))))
            return std::nullopt;
        first = false;
        const auto lower = u_foldCase(character, U_FOLD_CASE_DEFAULT);
        char buffer[U8_MAX_LENGTH];
        std::int32_t count = 0;
        UBool error = false;
        U8_APPEND(buffer, count, U8_MAX_LENGTH, lower, error);
        if (error) return std::nullopt;
        folded.append(buffer, static_cast<std::size_t>(count));
    }
    return folded;
}

}  // namespace

struct RuntimeCollectionRef::State {
    using NodeId = std::uint64_t;
    using StoredValue = std::variant<PropertyValue, NodeId>;
    using KeyToken = std::variant<std::string, ValueListRef, NodeId>;
    struct KeyHash {
        std::size_t operator()(const KeyToken& key) const noexcept {
            return std::visit([&](const auto& value) noexcept -> std::size_t {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, ValueListRef>)
                    return std::hash<const ValueListValue*>{}(value.get()) ^ key.index();
                else return std::hash<Value>{}(value) ^ key.index();
            }, key);
        }
    };
    using EntryIndex = std::unordered_map<KeyToken, std::size_t, KeyHash>;

    struct Row {
        KeyToken token;
        StoredValue key{PropertyValue{UndefinedValue{}}};
        StoredValue value{PropertyValue{UndefinedValue{}}};
    };
    static_assert(std::is_nothrow_move_constructible_v<Row>);
    static_assert(std::is_nothrow_move_assignable_v<Row>);
    static_assert(std::is_nothrow_move_assignable_v<StoredValue>);

    struct Node {
        RuntimeCollectionKind kind = RuntimeCollectionKind::array;
        bool fixed = false;
        bool marked = false;
        std::size_t external_handle_count = 0;
        // Only edges from nodes with an external handle contribute to this count.
        std::size_t direct_rooted_incoming = 0;
        std::vector<Row> rows;
        EntryIndex index;
        std::size_t index_capacity = 0;

        void reserve_index(std::size_t required) {
            if (required > max_runtime_collection_entries)
                throw std::length_error("Runtime collection entry limit exceeded");
            if (required <= index_capacity) return;
            auto capacity = std::max<std::size_t>(1, index_capacity);
            while (capacity < required)
                capacity += std::min(max_runtime_collection_entries - capacity,
                    std::max<std::size_t>(1, capacity / 2));
            index.reserve(capacity);
            index_capacity = capacity;
        }
    };
    using NodeMap = std::unordered_map<NodeId, std::shared_ptr<Node>>;

    std::shared_ptr<State> parent;
    NodeMap nodes;
    std::vector<NodeId> mark_stack;
    std::size_t entries = 0;
    std::size_t node_capacity = 0;
    static_assert(max_runtime_collection_entries <= std::numeric_limits<std::size_t>::max() / 2);

    void reserve_nodes(std::size_t required) {
        if (required > max_runtime_collection_nodes)
            throw std::length_error("Runtime collection node limit exceeded");
        if (required <= node_capacity) return;
        auto capacity = std::max<std::size_t>(1, node_capacity);
        while (capacity < required)
            capacity += std::min(max_runtime_collection_nodes - capacity,
                std::max<std::size_t>(1, capacity / 2));
        nodes.reserve(capacity);
        mark_stack.reserve(capacity);
        node_capacity = capacity;
    }

    static std::shared_ptr<State> root(std::shared_ptr<State> state) noexcept {
        if (!state) return {};
        auto master = state;
        while (master->parent) master = master->parent;
        while (state->parent && state->parent != master) {
            auto next = std::move(state->parent);
            state->parent = master;
            state = std::move(next);
        }
        return master;
    }

    std::shared_ptr<Node> node(NodeId id) const noexcept {
        const auto found = nodes.find(id);
        return found == nodes.end() ? std::shared_ptr<Node>{} : found->second;
    }

    void adjust_rooted_edge(const StoredValue& value, bool added) noexcept {
        const auto* edge = std::get_if<NodeId>(&value);
        if (!edge) return;
        const auto target = nodes.find(*edge);
        assert(target != nodes.end());
        if (target == nodes.end()) return;
        if (added) {
            assert(target->second->direct_rooted_incoming < 2 * max_runtime_collection_entries);
            ++target->second->direct_rooted_incoming;
        } else {
            assert(target->second->direct_rooted_incoming != 0);
            --target->second->direct_rooted_incoming;
        }
    }

    void adjust_rooted_row(const Row& row, bool added) noexcept {
        adjust_rooted_edge(row.key, added);
        adjust_rooted_edge(row.value, added);
    }

    bool may_be_unreachable(const StoredValue& value) const noexcept {
        const auto* edge = std::get_if<NodeId>(&value);
        if (!edge) return false;
        const auto target = nodes.find(*edge);
        return target != nodes.end() && target->second->external_handle_count == 0 &&
            target->second->direct_rooted_incoming == 0;
    }

    void collect() noexcept {
        mark_stack.clear();
        for (const auto& [id, value] : nodes) value->marked = false;
        for (const auto& [id, value] : nodes) {
            if (value->external_handle_count != 0) {
                value->marked = true;
                mark_stack.push_back(id);
            }
        }
        const auto trace = [&](const StoredValue& value) noexcept {
            const auto* edge = std::get_if<NodeId>(&value);
            if (!edge) return;
            const auto found = nodes.find(*edge);
            if (found != nodes.end() && !found->second->marked) {
                found->second->marked = true;
                mark_stack.push_back(*edge);
            }
        };
        while (!mark_stack.empty()) {
            const auto id = mark_stack.back();
            mark_stack.pop_back();
            const auto found = nodes.find(id);
            if (found == nodes.end()) continue;
            for (const auto& row : found->second->rows) {
                trace(row.key);
                trace(row.value);
            }
        }
        for (auto iterator = nodes.begin(); iterator != nodes.end();) {
            if (!iterator->second->marked) {
                assert(iterator->second->external_handle_count == 0 && iterator->second->direct_rooted_incoming == 0);
                entries -= iterator->second->rows.size();
                iterator = nodes.erase(iterator);
            } else ++iterator;
        }
    }

    static std::optional<StoredValue> store(const PropertyValue& value) {
        if (validate_runtime_value(value) != RuntimeValueValidation::valid) return std::nullopt;
        if (const auto* reference = std::get_if<RuntimeCollectionRef>(&value))
            return StoredValue{std::in_place_type<NodeId>, reference->id_};
        return StoredValue{std::in_place_type<PropertyValue>, value};
    }

    static std::optional<KeyToken> key_token(const PropertyValue& value) {
        if (validate_runtime_value(value) != RuntimeValueValidation::valid) return std::nullopt;
        if (const auto* reference = std::get_if<RuntimeCollectionRef>(&value))
            return KeyToken{std::in_place_type<NodeId>, reference->id_};
        if (const auto* list = std::get_if<ValueListRef>(&value))
            return KeyToken{std::in_place_type<ValueListRef>, *list};
        if (const auto* enumeration = std::get_if<EnumerationValue>(&value)) {
            std::string token = "E" + std::to_string(enumeration->type_name.size()) + ":" + enumeration->type_name +
                std::to_string(enumeration->member.size()) + ":" + enumeration->member;
            return KeyToken{std::in_place_type<std::string>, std::move(token)};
        }
        auto token = typed_map_key(value);
        if (!token) return std::nullopt;
        return KeyToken{std::in_place_type<std::string>, std::move(*token)};
    }

    static std::optional<PropertyValue> read(const std::shared_ptr<State>& state, const StoredValue& value) {
        if (const auto* edge = std::get_if<NodeId>(&value)) {
            RuntimeCollectionRef reference(state, *edge);
            if (!reference.valid()) return std::nullopt;
            return PropertyValue{std::move(reference)};
        }
        return std::get<PropertyValue>(value);
    }

    struct Union {
        std::array<std::shared_ptr<State>, 3> roots;
        std::size_t count = 0;
        std::shared_ptr<State> master;
        NodeMap prepared;
        std::array<NodeMap, 3> retired_nodes;
        std::array<std::vector<NodeId>, 3> retired_stacks;
        std::size_t entries = 0;

        void commit() noexcept {
            if (count > 1) {
                // Reserved buckets and preallocated node handles make publication allocation-free.
                while (!prepared.empty()) master->nodes.insert(prepared.extract(prepared.begin()));
                for (std::size_t index = 0; index < count; ++index) {
                    if (roots[index] == master) continue;
                    roots[index]->nodes.swap(retired_nodes[index]);
                    roots[index]->mark_stack.swap(retired_stacks[index]);
                    roots[index]->entries = 0;
                    roots[index]->node_capacity = 0;
                    roots[index]->parent = master;
                }
            }
            master->entries = entries;
        }
    };

    static std::optional<Union> prepare_union(const std::shared_ptr<State>& target,
        const PropertyValue* key, const PropertyValue* value, std::size_t removed, std::size_t added) {
        Union result;
        result.roots[result.count++] = target;
        for (const auto* candidate : {key, value}) {
            const auto* reference = candidate ? std::get_if<RuntimeCollectionRef>(candidate) : nullptr;
            if (!reference) continue;
            auto region = root(reference->state_);
            if (!region || !region->node(reference->id_)) return std::nullopt;
            bool duplicate = false;
            for (std::size_t index = 0; index < result.count; ++index)
                duplicate = duplicate || result.roots[index] == region;
            if (!duplicate) result.roots[result.count++] = std::move(region);
        }
        std::size_t node_count = 0;
        for (std::size_t index = 0; index < result.count; ++index) {
            const auto& region = result.roots[index];
            if (region->nodes.size() > max_runtime_collection_nodes - node_count ||
                region->entries > max_runtime_collection_entries - result.entries)
                return std::nullopt;
            node_count += region->nodes.size();
            result.entries += region->entries;
        }
        if (removed > result.entries || added > max_runtime_collection_entries - (result.entries - removed)) return std::nullopt;
        result.entries = result.entries - removed + added;
        result.master = target;
        if (result.count > 1) {
            for (std::size_t index = 1; index < result.count; ++index)
                if (result.roots[index]->nodes.size() > result.master->nodes.size()) result.master = result.roots[index];
            result.prepared.reserve(node_count - result.master->nodes.size());
            for (std::size_t index = 0; index < result.count; ++index) {
                if (result.roots[index] == result.master) continue;
                for (const auto& [id, member] : result.roots[index]->nodes)
                    if (result.master->nodes.contains(id) || !result.prepared.emplace(id, member).second)
                        return std::nullopt;
            }
            result.master->reserve_nodes(node_count);
        }
        return result;
    }

    enum class Action { append, set_at, set_field, set_key, erase_at, erase_field, erase_key, clear };
    struct Request {
        Action action;
        std::size_t index = 0;
        std::string_view name;
        const PropertyValue* key = nullptr;
        const PropertyValue* value = nullptr;
    };

    static bool change(const RuntimeCollectionRef& owner, const Request& request) {
        try {
            auto target = root(owner.state_);
            auto member = target ? target->node(owner.id_) : nullptr;
            if (!member || member->fixed) return false;
            assert(member->external_handle_count != 0);
            auto index = request.index;
            bool insertion = request.action == Action::append;
            const bool assignment = request.action == Action::append || request.action == Action::set_at ||
                request.action == Action::set_field || request.action == Action::set_key;
            const bool erase = request.action == Action::erase_at || request.action == Action::erase_field ||
                request.action == Action::erase_key;
            Row incoming;
            if (request.action == Action::append || request.action == Action::set_at || request.action == Action::erase_at) {
                if (member->kind != RuntimeCollectionKind::array ||
                    (!insertion && index >= member->rows.size())) return false;
            } else if (request.action == Action::set_field || request.action == Action::erase_field) {
                if (member->kind != RuntimeCollectionKind::structure) return false;
                auto folded = structure_name(request.name);
                if (!folded) return false;
                KeyToken token{std::in_place_type<std::string>, std::move(*folded)};
                const auto found = member->index.find(token);
                insertion = found == member->index.end();
                index = insertion ? member->rows.size() : found->second;
                if (erase && insertion) return false;
                if (insertion) incoming.token = std::move(token);
            } else if (request.action == Action::set_key || request.action == Action::erase_key) {
                if (member->kind != RuntimeCollectionKind::map || !request.key) return false;
                auto token = key_token(*request.key);
                if (!token) return false;
                const auto found = member->index.find(*token);
                insertion = found == member->index.end();
                index = insertion ? member->rows.size() : found->second;
                if (erase && insertion) return false;
                if (insertion) {
                    auto stored = store(*request.key);
                    if (!stored) return false;
                    incoming.key = std::move(*stored);
                    incoming.token = std::move(*token);
                }
            } else if (request.action != Action::clear) return false;

            if (assignment) {
                if (!request.value) return false;
                auto stored = store(*request.value);
                if (!stored) return false;
                incoming.value = std::move(*stored);
            }
            const auto removed = request.action == Action::clear ? member->rows.size() : erase ? 1U : 0U;
            const auto added = assignment && insertion ? 1U : 0U;
            auto regions = prepare_union(target,
                assignment && insertion && request.action == Action::set_key ? request.key : nullptr,
                assignment ? request.value : nullptr, removed, added);
            if (!regions) return false;
            EntryIndex prepared_index;
            EntryIndex::node_type prepared_index_node;
            if (added != 0 && member->kind != RuntimeCollectionKind::array) {
                prepared_index.emplace(incoming.token, member->rows.size());
                prepared_index_node = prepared_index.extract(prepared_index.begin());
                member->reserve_index(member->rows.size() + 1);
            }
            if (added != 0 && member->rows.size() == member->rows.capacity()) {
                const auto capacity = std::min(max_runtime_collection_entries, member->rows.capacity());
                const auto growth = capacity + std::min(max_runtime_collection_entries - capacity,
                    std::max<std::size_t>(1, capacity / 2));
                member->rows.reserve(std::min(max_runtime_collection_entries, std::max(member->rows.size() + 1, growth)));
            }
            regions->commit();
            auto& master = *regions->master;
            bool maybe_detached = false;
            if (request.action == Action::clear) {
                for (const auto& row : member->rows) master.adjust_rooted_row(row, false);
                maybe_detached = std::ranges::any_of(member->rows, [&](const Row& row) {
                    return master.may_be_unreachable(row.key) || master.may_be_unreachable(row.value);
                });
                member->rows.clear();
                member->index.clear();
            } else if (erase) {
                const auto& row = member->rows[index];
                master.adjust_rooted_row(row, false);
                maybe_detached = master.may_be_unreachable(row.key) || master.may_be_unreachable(row.value);
                if (member->kind != RuntimeCollectionKind::array) member->index.erase(row.token);
                member->rows.erase(member->rows.begin() + static_cast<std::ptrdiff_t>(index));
                if (member->kind != RuntimeCollectionKind::array)
                    for (auto shifted = index; shifted < member->rows.size(); ++shifted) {
                        const auto entry = member->index.find(member->rows[shifted].token);
                        assert(entry != member->index.end());
                        entry->second = shifted;
                    }
            } else if (insertion) {
                member->rows.push_back(std::move(incoming));
                if (member->kind != RuntimeCollectionKind::array) {
                    const auto inserted = member->index.insert(std::move(prepared_index_node));
                    assert(inserted.inserted);
                    static_cast<void>(inserted);
                }
                master.adjust_rooted_row(member->rows.back(), true);
            } else {
                master.adjust_rooted_edge(member->rows[index].value, false);
                master.adjust_rooted_edge(incoming.value, true);
                maybe_detached = master.may_be_unreachable(member->rows[index].value);
                member->rows[index].value = std::move(incoming.value);
            }
            if (maybe_detached) master.collect();
            return true;
        } catch (...) { return false; }
    }
};

RuntimeCollectionRef::RuntimeCollectionRef(std::shared_ptr<State> state, std::uint64_t id) {
    auto master = State::root(std::move(state));
    auto member = master ? master->node(id) : nullptr;
    if (!member) return;
    if (member->external_handle_count == std::numeric_limits<std::size_t>::max())
        throw std::length_error("Runtime collection handle limit exceeded");
    if (member->external_handle_count++ == 0)
        for (const auto& row : member->rows) master->adjust_rooted_row(row, true);
    state_ = std::move(master);
    id_ = id;
}

RuntimeCollectionRef::RuntimeCollectionRef(const RuntimeCollectionRef& other)
    : RuntimeCollectionRef(other.state_, other.id_) {}

RuntimeCollectionRef::RuntimeCollectionRef(RuntimeCollectionRef&& other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

RuntimeCollectionRef& RuntimeCollectionRef::operator=(const RuntimeCollectionRef& other) {
    if (this == &other) return *this;
    RuntimeCollectionRef replacement(other);
    state_.swap(replacement.state_);
    std::swap(id_, replacement.id_);
    return *this;
}

RuntimeCollectionRef& RuntimeCollectionRef::operator=(RuntimeCollectionRef&& other) noexcept {
    if (this == &other) return *this;
    release();
    state_ = std::move(other.state_);
    id_ = std::exchange(other.id_, 0);
    return *this;
}

RuntimeCollectionRef::~RuntimeCollectionRef() { release(); }

void RuntimeCollectionRef::release() noexcept {
    auto master = State::root(state_);
    auto member = master ? master->node(id_) : nullptr;
    if (member && member->external_handle_count != 0 && --member->external_handle_count == 0) {
        // Remove this root's contribution, including self edges, before testing another rooted predecessor.
        for (const auto& row : member->rows) master->adjust_rooted_row(row, false);
        if (member->direct_rooted_incoming == 0) master->collect();
    }
    id_ = 0;
    state_.reset();
}

std::optional<RuntimeCollectionRef> RuntimeCollectionRef::create(RuntimeCollectionKind kind) {
    if (kind != RuntimeCollectionKind::array && kind != RuntimeCollectionKind::structure && kind != RuntimeCollectionKind::map)
        return std::nullopt;
    try {
        auto state = std::make_shared<State>();
        auto member = std::make_shared<State::Node>();
        member->kind = kind;
        state->reserve_nodes(1);
        const auto id = allocate_identity();
        if (!id) return std::nullopt;
        state->nodes.emplace(*id, std::move(member));
        return RuntimeCollectionRef(std::move(state), *id);
    } catch (...) { return std::nullopt; }
}

bool RuntimeCollectionRef::valid() const noexcept {
    const auto master = State::root(state_);
    return master && master->nodes.contains(id_);
}

std::optional<RuntimeCollectionKind> RuntimeCollectionRef::kind() const noexcept {
    const auto master = State::root(state_);
    const auto member = master ? master->node(id_) : nullptr;
    return member ? std::optional{member->kind} : std::nullopt;
}

bool RuntimeCollectionRef::fixed() const noexcept {
    const auto master = State::root(state_);
    const auto member = master ? master->node(id_) : nullptr;
    return member && member->fixed;
}

std::optional<std::size_t> RuntimeCollectionRef::size() const noexcept {
    const auto master = State::root(state_);
    const auto member = master ? master->node(id_) : nullptr;
    return member ? std::optional{member->rows.size()} : std::nullopt;
}

std::optional<PropertyValue> RuntimeCollectionRef::at(std::size_t index) const {
    try {
        const auto master = State::root(state_);
        const auto member = master ? master->node(id_) : nullptr;
        if (!member || member->kind != RuntimeCollectionKind::array || index >= member->rows.size()) return std::nullopt;
        return State::read(master, member->rows[index].value);
    } catch (...) { return std::nullopt; }
}

std::optional<PropertyValue> RuntimeCollectionRef::field(std::string_view name) const {
    try {
        const auto master = State::root(state_);
        const auto member = master ? master->node(id_) : nullptr;
        if (!member || member->kind != RuntimeCollectionKind::structure) return std::nullopt;
        const auto folded = structure_name(name);
        if (!folded) return std::nullopt;
        const auto found = member->index.find(State::KeyToken{std::in_place_type<std::string>, *folded});
        return found == member->index.end() ? std::nullopt : State::read(master, member->rows[found->second].value);
    } catch (...) { return std::nullopt; }
}

std::optional<PropertyValue> RuntimeCollectionRef::find_key(const PropertyValue& key) const {
    try {
        const auto master = State::root(state_);
        const auto member = master ? master->node(id_) : nullptr;
        if (!member || member->kind != RuntimeCollectionKind::map) return std::nullopt;
        const auto token = State::key_token(key);
        if (!token) return std::nullopt;
        const auto found = member->index.find(*token);
        return found == member->index.end() ? std::nullopt : State::read(master, member->rows[found->second].value);
    } catch (...) { return std::nullopt; }
}

bool RuntimeCollectionRef::append(PropertyValue value) const {
    return State::change(*this, {State::Action::append, 0, {}, nullptr, &value});
}

bool RuntimeCollectionRef::set_at(std::size_t index, PropertyValue value) const {
    return State::change(*this, {State::Action::set_at, index, {}, nullptr, &value});
}

bool RuntimeCollectionRef::set_field(std::string name, PropertyValue value) const {
    return State::change(*this, {State::Action::set_field, 0, name, nullptr, &value});
}

bool RuntimeCollectionRef::set_key(PropertyValue key, PropertyValue value) const {
    return State::change(*this, {State::Action::set_key, 0, {}, &key, &value});
}

bool RuntimeCollectionRef::erase_at(std::size_t index) const {
    return State::change(*this, {State::Action::erase_at, index, {}, nullptr, nullptr});
}

bool RuntimeCollectionRef::erase_field(std::string_view name) const {
    return State::change(*this, {State::Action::erase_field, 0, name});
}

bool RuntimeCollectionRef::erase_key(const PropertyValue& key) const {
    return State::change(*this, {State::Action::erase_key, 0, {}, &key});
}

bool RuntimeCollectionRef::clear() const {
    return State::change(*this, {State::Action::clear, 0, {}, nullptr, nullptr});
}

std::optional<RuntimeCollectionRef> RuntimeCollectionRef::fixed_copy() const {
    try {
        auto master = State::root(state_);
        const auto member = master ? master->node(id_) : nullptr;
        if (!member) return std::nullopt;
        State::NodeMap prepared;
        std::vector<State::NodeId> path;
        path.reserve(max_typed_value_depth);
        std::size_t staged_nodes = 0;
        std::size_t staged_entries = 0;
        const auto clone = [&](auto&& self, State::NodeId source_id) -> std::optional<State::NodeId> {
            const auto source = master->node(source_id);
            if (!source || path.size() >= max_typed_value_depth ||
                std::ranges::find(path, source_id) != path.end() ||
                staged_nodes >= max_runtime_collection_nodes - master->nodes.size() ||
                source->rows.size() > max_runtime_collection_entries - master->entries - staged_entries)
                return std::nullopt;
            ++staged_nodes;
            staged_entries += source->rows.size();
            const auto copy_id = allocate_identity();
            if (!copy_id) return std::nullopt;
            auto copy = std::make_shared<State::Node>();
            copy->kind = source->kind;
            copy->fixed = true;
            if (source->kind != RuntimeCollectionKind::array) {
                copy->rows = source->rows;
                copy->index = source->index;
                copy->index_capacity = copy->index.size();
            }
            else {
                path.push_back(source_id);
                copy->rows.reserve(source->rows.size());
                for (const auto& source_row : source->rows) {
                    auto row = source_row;
                    if (const auto* edge = std::get_if<State::NodeId>(&row.value)) {
                        const auto child = master->node(*edge);
                        if (!child) { path.pop_back(); return std::nullopt; }
                        if (child->kind == RuntimeCollectionKind::array && !child->fixed) {
                            const auto child_copy = self(self, *edge);
                            if (!child_copy) { path.pop_back(); return std::nullopt; }
                            row.value = *child_copy;
                        }
                    }
                    copy->rows.push_back(std::move(row));
                }
                path.pop_back();
            }
            prepared.emplace(*copy_id, std::move(copy));
            return copy_id;
        };
        const auto copy_id = clone(clone, id_);
        if (!copy_id) return std::nullopt;
        master->reserve_nodes(master->nodes.size() + staged_nodes);
        // All graph nodes, row allocations, hash nodes and buckets exist before publication.
        while (!prepared.empty()) master->nodes.insert(prepared.extract(prepared.begin()));
        master->entries += staged_entries;
        return RuntimeCollectionRef(std::move(master), *copy_id);
    } catch (...) { return std::nullopt; }
}

std::weak_ptr<const void> RuntimeCollectionRef::observe_lifetime() const noexcept {
    const auto master = State::root(state_);
    return master ? std::weak_ptr<const void>{master->node(id_)} : std::weak_ptr<const void>{};
}

bool operator==(const RuntimeCollectionRef& left, const RuntimeCollectionRef& right) noexcept {
    return left.id_ == right.id_;
}

}  // namespace oof::model
