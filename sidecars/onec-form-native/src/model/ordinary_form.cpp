#include "oof/model/ordinary_form.hpp"

#include <functional>
#include <type_traits>
#include <unordered_map>

#include "oof/model/metamodel.hpp"

namespace oof::model {
namespace {

void add_violation(
    ValidationReport& report,
    InvariantCode code,
    ObjectId source,
    ObjectId target,
    std::string message
) {
    report.violations.push_back({code, source, target, std::move(message)});
}

bool property_value_matches(
    metamodel::ValueCodec expected,
    const PropertyValue& value
) noexcept {
    return std::visit(
        [expected](const auto& typed_value) {
            using Value = std::remove_cvref_t<decltype(typed_value)>;
            if constexpr (std::is_same_v<Value, UndefinedValue>) {
                return expected == metamodel::ValueCodec::date;
            } else if constexpr (std::is_same_v<Value, bool>) {
                return expected == metamodel::ValueCodec::boolean;
            } else if constexpr (std::is_same_v<Value, std::int64_t>) {
                return expected == metamodel::ValueCodec::integer ||
                       expected == metamodel::ValueCodec::decimal;
            } else if constexpr (std::is_same_v<Value, DecimalValue>) {
                return expected == metamodel::ValueCodec::decimal;
            } else if constexpr (std::is_same_v<Value, std::string>) {
                return expected == metamodel::ValueCodec::string;
            } else if constexpr (std::is_same_v<Value, LocalizedStringValue>) {
                return expected == metamodel::ValueCodec::localized_string;
            } else if constexpr (std::is_same_v<Value, FormattedStringValue>) {
                return expected == metamodel::ValueCodec::formatted_string;
            } else if constexpr (std::is_same_v<Value, DateValue>) {
                return expected == metamodel::ValueCodec::date;
            } else if constexpr (std::is_same_v<Value, UuidValue>) {
                return expected == metamodel::ValueCodec::uuid;
            } else if constexpr (std::is_same_v<Value, CompositeIdValue>) {
                return expected == metamodel::ValueCodec::composite_id;
            } else if constexpr (std::is_same_v<Value, TypeDomainPatternValue>) {
                return expected == metamodel::ValueCodec::type_domain;
            } else if constexpr (std::is_same_v<Value, EnumerationValue>) {
                return expected == metamodel::ValueCodec::enumeration;
            } else if constexpr (std::is_same_v<Value, ColorValue>) {
                return expected == metamodel::ValueCodec::color;
            } else if constexpr (std::is_same_v<Value, FontValue>) {
                return expected == metamodel::ValueCodec::font;
            } else if constexpr (std::is_same_v<Value, PictureRef>) {
                return expected == metamodel::ValueCodec::picture;
            } else if constexpr (std::is_same_v<Value, ControlRef>) {
                return expected == metamodel::ValueCodec::control_reference;
            } else if constexpr (std::is_same_v<Value, AttributeRef>) {
                return expected == metamodel::ValueCodec::attribute_reference;
            } else if constexpr (std::is_same_v<Value, CommandRef>) {
                return expected == metamodel::ValueCodec::command_reference;
            }
            return false;
        },
        value);
}

}  // namespace

const PropertyEntry* PropertySet::find(PropertyId id) const noexcept {
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &found->second;
}

PropertyEntry* PropertySet::find(PropertyId id) noexcept {
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &found->second;
}

bool PropertySet::contains(PropertyId id) const noexcept {
    return entries_.contains(id);
}

std::size_t PropertySet::size() const noexcept {
    return entries_.size();
}

bool PropertySet::empty() const noexcept {
    return entries_.empty();
}

void PropertySet::set_explicit(PropertyId id, PropertyValue value) {
    if (!id) {
        throw std::invalid_argument("ordinary-form property ID must be nonzero");
    }
    entries_.insert_or_assign(
        id,
        PropertyEntry{id, PropertyState::explicit_value, std::move(value)});
}

bool PropertySet::unset(PropertyId id) {
    return entries_.erase(id) != 0;
}

void PropertySet::clear() noexcept {
    entries_.clear();
}

ControlKind payload_kind(const ControlPayload& payload) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using Payload = std::remove_cvref_t<decltype(value)>;
            return Payload::kind;
        },
        payload);
}

PropertySet& payload_properties(ControlPayload& payload) noexcept {
    return std::visit([](auto& value) -> PropertySet& { return value.properties; }, payload);
}

const PropertySet& payload_properties(const ControlPayload& payload) noexcept {
    return std::visit(
        [](const auto& value) -> const PropertySet& { return value.properties; },
        payload);
}

ControlNode::ControlNode(
    ObjectId object_id,
    std::string object_name,
    ControlPayload control_payload
)
    : id(object_id), name(std::move(object_name)), payload(std::move(control_payload)) {}

ControlKind ControlNode::kind() const noexcept {
    return payload_kind(payload);
}

PropertySet& ControlNode::properties() noexcept {
    return payload_properties(payload);
}

const PropertySet& ControlNode::properties() const noexcept {
    return payload_properties(payload);
}

bool ValidationReport::has(InvariantCode code) const noexcept {
    for (const auto& violation : violations) {
        if (violation.code == code) {
            return true;
        }
    }
    return false;
}

InvariantError::InvariantError(ValidationReport report)
    : std::logic_error(
          "ordinary-form document violates " +
          std::to_string(report.violations.size()) + " invariant(s)"),
      report_(std::move(report)) {}

OrdinaryFormDocument::OrdinaryFormDocument() {
    rebuild_index();
}

OrdinaryFormDocument::OrdinaryFormDocument(Form form) : form_(std::move(form)) {
    rebuild_index();
}

void OrdinaryFormDocument::set_form(Form form) {
    form_ = std::move(form);
    rebuild_index();
}

void OrdinaryFormDocument::set_module(FormModule module) {
    module_ = std::move(module);
}

void OrdinaryFormDocument::add_asset(PictureAsset asset) {
    const std::size_t index = assets_.size();
    assets_.push_back(std::move(asset));
    index_first(assets_.back().id, ObjectCategory::picture_asset, index);
}

void OrdinaryFormDocument::add_control(ControlNode control) {
    const std::size_t index = collections_.controls.size();
    collections_.controls.push_back(std::move(control));
    index_first(collections_.controls.back().id, ObjectCategory::control, index);
}

void OrdinaryFormDocument::add_page(Page page) {
    const std::size_t index = collections_.pages.size();
    collections_.pages.push_back(std::move(page));
    index_first(collections_.pages.back().id, ObjectCategory::page, index);
}

void OrdinaryFormDocument::add_attribute(Attribute attribute) {
    const std::size_t index = collections_.attributes.size();
    collections_.attributes.push_back(std::move(attribute));
    index_first(collections_.attributes.back().id, ObjectCategory::attribute, index);
}

void OrdinaryFormDocument::add_command(Command command) {
    const std::size_t index = collections_.commands.size();
    collections_.commands.push_back(std::move(command));
    index_first(collections_.commands.back().id, ObjectCategory::command, index);
}

void OrdinaryFormDocument::add_event(Event event) {
    const std::size_t index = collections_.events.size();
    collections_.events.push_back(std::move(event));
    index_first(collections_.events.back().id, ObjectCategory::event, index);
}

std::optional<OrdinaryFormDocument::ObjectView> OrdinaryFormDocument::find(ObjectId id) const {
    const auto found = index_.find(id);
    if (found == index_.end()) {
        return std::nullopt;
    }

    const ObjectLocation location = found->second;
    switch (location.category) {
        case ObjectCategory::form:
            return ObjectView{std::cref(form_)};
        case ObjectCategory::control:
            return ObjectView{std::cref(collections_.controls.at(location.index))};
        case ObjectCategory::page:
            return ObjectView{std::cref(collections_.pages.at(location.index))};
        case ObjectCategory::attribute:
            return ObjectView{std::cref(collections_.attributes.at(location.index))};
        case ObjectCategory::command:
            return ObjectView{std::cref(collections_.commands.at(location.index))};
        case ObjectCategory::event:
            return ObjectView{std::cref(collections_.events.at(location.index))};
        case ObjectCategory::picture_asset:
            return ObjectView{std::cref(assets_.at(location.index))};
    }
    return std::nullopt;
}

const ControlNode* OrdinaryFormDocument::find_control(ObjectId id) const noexcept {
    const auto found = index_.find(id);
    if (found == index_.end() || found->second.category != ObjectCategory::control) {
        return nullptr;
    }
    return &collections_.controls[found->second.index];
}

const Page* OrdinaryFormDocument::find_page(ObjectId id) const noexcept {
    const auto found = index_.find(id);
    if (found == index_.end() || found->second.category != ObjectCategory::page) {
        return nullptr;
    }
    return &collections_.pages[found->second.index];
}

const Attribute* OrdinaryFormDocument::find_attribute(ObjectId id) const noexcept {
    const auto found = index_.find(id);
    if (found == index_.end() || found->second.category != ObjectCategory::attribute) {
        return nullptr;
    }
    return &collections_.attributes[found->second.index];
}

const Command* OrdinaryFormDocument::find_command(ObjectId id) const noexcept {
    const auto found = index_.find(id);
    if (found == index_.end() || found->second.category != ObjectCategory::command) {
        return nullptr;
    }
    return &collections_.commands[found->second.index];
}

const Event* OrdinaryFormDocument::find_event(ObjectId id) const noexcept {
    const auto found = index_.find(id);
    if (found == index_.end() || found->second.category != ObjectCategory::event) {
        return nullptr;
    }
    return &collections_.events[found->second.index];
}

const PictureAsset* OrdinaryFormDocument::find_asset(ObjectId id) const noexcept {
    const auto found = index_.find(id);
    if (found == index_.end() || found->second.category != ObjectCategory::picture_asset) {
        return nullptr;
    }
    return &assets_[found->second.index];
}

std::size_t OrdinaryFormDocument::indexed_id_count() const noexcept {
    return index_.size();
}

ValidationReport OrdinaryFormDocument::validate() const {
    ValidationReport report;
    std::unordered_map<ObjectId, ObjectCategory, ObjectIdHash> seen;

    const auto record_id = [&](ObjectId id, ObjectCategory category) {
        if (!id) {
            add_violation(
                report,
                InvariantCode::invalid_id,
                id,
                id,
                "ordinary-form object IDs must be nonzero");
        }
        const auto [position, inserted] = seen.try_emplace(id, category);
        if (!inserted) {
            add_violation(
                report,
                InvariantCode::duplicate_id,
                id,
                id,
                "ordinary-form object ID is duplicated");
        }
    };

    record_id(form_.id, ObjectCategory::form);
    for (const auto& control : collections_.controls) {
        record_id(control.id, ObjectCategory::control);
    }
    for (const auto& page : collections_.pages) {
        record_id(page.id, ObjectCategory::page);
    }
    for (const auto& attribute : collections_.attributes) {
        record_id(attribute.id, ObjectCategory::attribute);
    }
    for (const auto& command : collections_.commands) {
        record_id(command.id, ObjectCategory::command);
    }
    for (const auto& event : collections_.events) {
        record_id(event.id, ObjectCategory::event);
    }
    for (const auto& asset : assets_) {
        record_id(asset.id, ObjectCategory::picture_asset);
    }

    const auto require_control = [&](ObjectId source, ControlRef reference) {
        if (find_control(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "control reference does not resolve to a control");
        }
    };
    const auto require_page = [&](ObjectId source, PageRef reference) {
        if (find_page(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "page reference does not resolve to a page");
        }
    };
    const auto require_attribute = [&](ObjectId source, AttributeRef reference) {
        if (find_attribute(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "attribute reference does not resolve to an attribute");
        }
    };
    const auto require_command = [&](ObjectId source, CommandRef reference) {
        if (find_command(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "command reference does not resolve to a command");
        }
    };
    const auto require_event = [&](ObjectId source, EventRef reference) {
        if (find_event(reference.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.id(),
                "event reference does not resolve to an event");
        }
    };
    const auto require_picture = [&](ObjectId source, const PictureRef& reference) {
        if (find_asset(reference.asset.id()) == nullptr) {
            add_violation(
                report,
                InvariantCode::dangling_reference,
                source,
                reference.asset.id(),
                "picture reference does not resolve to an asset");
        }
    };

    const auto validate_property_value = [&](ObjectId source, const PropertyValue& value) {
        std::visit(
            [&](const auto& typed_value) {
                using Value = std::remove_cvref_t<decltype(typed_value)>;
                if constexpr (std::is_same_v<Value, ControlRef>) {
                    require_control(source, typed_value);
                } else if constexpr (std::is_same_v<Value, AttributeRef>) {
                    require_attribute(source, typed_value);
                } else if constexpr (std::is_same_v<Value, CommandRef>) {
                    require_command(source, typed_value);
                } else if constexpr (std::is_same_v<Value, PictureRef>) {
                    require_picture(source, typed_value);
                }
            },
            value);
    };

    const auto validate_property_set = [&]<typename Resolve>(
                                           ObjectId source,
                                           const PropertySet& properties,
                                           Resolve&& resolve,
                                           auto surface_is_allowed) {
        properties.for_each_explicit([&](const PropertyEntry& property) {
            if (!property.id || property.state != PropertyState::explicit_value) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "stored property entries must have an ID and explicit state");
            }
            const metamodel::PropertyDescriptor* descriptor = resolve(property.id);
            if (descriptor == nullptr || !surface_is_allowed(*descriptor)) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "property is not declared for this ordinary-form object surface");
            } else if (!property_value_matches(descriptor->value_codec, property.value)) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "property value does not match its metamodel value kind");
            }
            validate_property_value(source, property.value);
        });
    };

    const auto child_id = [](const ChildItemRef& child) {
        return std::visit([](const auto& reference) { return reference.id(); }, child);
    };
    const auto require_child = [&](ObjectId source, const ChildItemRef& child) {
        std::visit(
            [&](const auto& reference) {
                using ReferenceType = std::remove_cvref_t<decltype(reference)>;
                if constexpr (std::is_same_v<ReferenceType, ControlRef>) {
                    require_control(source, reference);
                } else {
                    require_page(source, reference);
                }
            },
            child);
    };

    for (const ChildItemRef& child : form_.children) {
        require_child(form_.id, child);
        if (std::holds_alternative<PageRef>(child)) {
            add_violation(
                report,
                InvariantCode::illegal_children,
                form_.id,
                child_id(child),
                "form root accepts controls, not panel pages");
        }
    }
    for (const EventRef event : form_.events) {
        require_event(form_.id, event);
    }
    validate_property_set(
        form_.id,
        form_.properties,
        [](PropertyId id) { return metamodel::find_form_property(id); },
        [](const metamodel::PropertyDescriptor& descriptor) {
            return descriptor.surface == metamodel::PropertySurface::form;
        });

    for (const auto& command : collections_.commands) {
        if (const auto& picture = command.picture.value(); picture.has_value()) {
            require_picture(command.id, *picture);
        }
    }

    for (const auto& event : collections_.events) {
        std::visit(
            [&](const auto& owner) {
                using Owner = std::remove_cvref_t<decltype(owner)>;
                if constexpr (std::is_same_v<Owner, FormRef>) {
                    if (owner.id() != form_.id) {
                        add_violation(
                            report,
                            InvariantCode::dangling_reference,
                            event.id,
                            owner.id(),
                            "event owner does not resolve to the form");
                    }
                } else {
                    require_control(event.id, owner);
                }
            },
            event.owner);
    }

    std::unordered_map<ObjectId, std::size_t, ObjectIdHash> parent_counts;
    const auto register_parent = [&](ObjectId source, const ChildItemRef& child) {
        const ObjectId id = child_id(child);
        const bool resolves = std::visit(
            [&](const auto& reference) {
                using ReferenceType = std::remove_cvref_t<decltype(reference)>;
                if constexpr (std::is_same_v<ReferenceType, ControlRef>) {
                    return find_control(reference.id()) != nullptr;
                } else {
                    return find_page(reference.id()) != nullptr;
                }
            },
            child);
        if (!resolves) {
            return;
        }
        const std::size_t count = ++parent_counts[id];
        if (count > 1) {
            add_violation(
                report,
                InvariantCode::multiple_parents,
                source,
                id,
                "child item appears in more than one authoritative child sequence");
        }
    };
    for (const ChildItemRef& child : form_.children) {
        register_parent(form_.id, child);
    }

    for (const auto& control : collections_.controls) {
        const auto& descriptor = metamodel::descriptor_for(control.kind());
        if (!control.children.empty() && descriptor.child_policy == metamodel::ChildPolicy::forbidden) {
            add_violation(
                report,
                InvariantCode::illegal_children,
                control.id,
                child_id(control.children.front()),
                "control kind does not permit child controls");
        }

        for (const ChildItemRef& child : control.children) {
            require_child(control.id, child);
            if (descriptor.child_policy == metamodel::ChildPolicy::ordered_controls &&
                std::holds_alternative<PageRef>(child)) {
                add_violation(
                    report,
                    InvariantCode::illegal_children,
                    control.id,
                    child_id(child),
                    "control kind accepts controls but not panel pages");
            }
            register_parent(control.id, child);
        }
        for (const EventRef event : control.events) {
            require_event(control.id, event);
        }
        for (const auto& binding : control.position.bindings.anchors) {
            if (binding.target.has_value()) {
                require_control(control.id, *binding.target);
            }
        }
        if (control.data_path.has_value()) {
            require_attribute(control.id, control.data_path->attribute);
        }
        validate_property_set(
            control.id,
            control.extension_properties,
            [&](PropertyId id) { return metamodel::find_property(control.kind(), id); },
            [](const metamodel::PropertyDescriptor& descriptor) {
                return descriptor.surface == metamodel::PropertySurface::control_extension &&
                       descriptor.api_name != "Name" && descriptor.api_name != "Data";
            });
        validate_property_set(
            control.id,
            control.properties(),
            [&](PropertyId id) { return metamodel::find_property(control.kind(), id); },
            [](const metamodel::PropertyDescriptor& descriptor) {
                return descriptor.surface == metamodel::PropertySurface::control_payload;
            });
    }

    for (const auto& page : collections_.pages) {
        for (const ChildItemRef& child : page.children) {
            require_child(page.id, child);
            if (std::holds_alternative<PageRef>(child)) {
                add_violation(
                    report,
                    InvariantCode::illegal_children,
                    page.id,
                    child_id(child),
                    "panel page accepts controls but not nested pages");
            }
            register_parent(page.id, child);
        }
    }

    for (const auto& control : collections_.controls) {
        if (!parent_counts.contains(control.id)) {
            add_violation(
                report,
                InvariantCode::orphan,
                form_.id,
                control.id,
                "control is not present in the authoritative child tree");
        }
    }
    for (const auto& page : collections_.pages) {
        if (!parent_counts.contains(page.id)) {
            add_violation(
                report,
                InvariantCode::orphan,
                form_.id,
                page.id,
                "panel page is not present in the authoritative child tree");
        }
    }

    enum class VisitState : std::uint8_t {
        visiting,
        complete,
    };
    std::unordered_map<ObjectId, VisitState, ObjectIdHash> visit_states;
    std::function<void(ObjectId, const std::vector<ChildItemRef>&)> visit =
        [&](ObjectId source, const std::vector<ChildItemRef>& children) {
        visit_states[source] = VisitState::visiting;
        for (const ChildItemRef& child_ref : children) {
            const ObjectId id = child_id(child_ref);
            const auto state = visit_states.find(id);
            if (state != visit_states.end() && state->second == VisitState::visiting) {
                add_violation(
                    report,
                    InvariantCode::cycle,
                    source,
                    id,
                    "form child graph contains a cycle");
                continue;
            }
            if (state == visit_states.end()) {
                std::visit(
                    [&](const auto& reference) {
                        using ReferenceType = std::remove_cvref_t<decltype(reference)>;
                        if constexpr (std::is_same_v<ReferenceType, ControlRef>) {
                            if (const ControlNode* child = find_control(reference.id())) {
                                visit(child->id, child->children);
                            }
                        } else if (const Page* child = find_page(reference.id())) {
                            visit(child->id, child->children);
                        }
                    },
                    child_ref);
            }
        }
        visit_states[source] = VisitState::complete;
    };

    for (const auto& control : collections_.controls) {
        if (!visit_states.contains(control.id)) {
            visit(control.id, control.children);
        }
    }
    for (const auto& page : collections_.pages) {
        if (!visit_states.contains(page.id)) {
            visit(page.id, page.children);
        }
    }

    return report;
}

void OrdinaryFormDocument::validate_or_throw() const {
    ValidationReport report = validate();
    if (!report.ok()) {
        throw InvariantError(std::move(report));
    }
}

void OrdinaryFormDocument::rebuild_index() {
    index_.clear();
    index_first(form_.id, ObjectCategory::form, 0);
    for (std::size_t index = 0; index < collections_.controls.size(); ++index) {
        index_first(collections_.controls[index].id, ObjectCategory::control, index);
    }
    for (std::size_t index = 0; index < collections_.pages.size(); ++index) {
        index_first(collections_.pages[index].id, ObjectCategory::page, index);
    }
    for (std::size_t index = 0; index < collections_.attributes.size(); ++index) {
        index_first(collections_.attributes[index].id, ObjectCategory::attribute, index);
    }
    for (std::size_t index = 0; index < collections_.commands.size(); ++index) {
        index_first(collections_.commands[index].id, ObjectCategory::command, index);
    }
    for (std::size_t index = 0; index < collections_.events.size(); ++index) {
        index_first(collections_.events[index].id, ObjectCategory::event, index);
    }
    for (std::size_t index = 0; index < assets_.size(); ++index) {
        index_first(assets_[index].id, ObjectCategory::picture_asset, index);
    }
}

void OrdinaryFormDocument::index_first(
    ObjectId id,
    ObjectCategory category,
    std::size_t index
) {
    index_.try_emplace(id, ObjectLocation{category, index});
}

}  // namespace oof::model
