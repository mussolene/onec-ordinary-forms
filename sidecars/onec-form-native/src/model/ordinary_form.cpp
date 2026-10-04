#include "oof/model/ordinary_form.hpp"

#include <array>
#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

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
                       (expected == metamodel::ValueCodec::integer32 &&
                        typed_value >= std::numeric_limits<std::int32_t>::min() &&
                        typed_value <= std::numeric_limits<std::int32_t>::max()) ||
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
            } else if constexpr (std::is_same_v<Value, ShortcutValue>) {
                return expected == metamodel::ValueCodec::shortcut &&
                       metamodel::find_shortcut_key(typed_value.key) != nullptr;
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

void OrdinaryFormDocument::set_asset_bytes(ObjectId id, std::vector<std::uint8_t> bytes) {
    const auto found = std::find_if(assets_.begin(), assets_.end(), [id](const PictureAsset& asset) {
        return asset.id == id;
    });
    if (found == assets_.end()) {
        throw std::invalid_argument("picture asset ID does not exist");
    }
    found->bytes = std::move(bytes);
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
    constexpr std::array categories{
        ObjectCategory::form,
        ObjectCategory::control,
        ObjectCategory::page,
        ObjectCategory::attribute,
        ObjectCategory::command,
        ObjectCategory::event,
        ObjectCategory::picture_asset,
    };
    std::optional<ObjectLocation> location;
    for (const ObjectCategory category : categories) {
        const auto candidate = find_location(category, id);
        if (!candidate) {
            continue;
        }
        if (location) {
            return std::nullopt;
        }
        location = candidate;
    }
    if (!location) {
        return std::nullopt;
    }

    switch (location->category) {
        case ObjectCategory::form:
            return ObjectView{std::cref(form_)};
        case ObjectCategory::control:
            return ObjectView{std::cref(collections_.controls.at(location->index))};
        case ObjectCategory::page:
            return ObjectView{std::cref(collections_.pages.at(location->index))};
        case ObjectCategory::attribute:
            return ObjectView{std::cref(collections_.attributes.at(location->index))};
        case ObjectCategory::command:
            return ObjectView{std::cref(collections_.commands.at(location->index))};
        case ObjectCategory::event:
            return ObjectView{std::cref(collections_.events.at(location->index))};
        case ObjectCategory::picture_asset:
            return ObjectView{std::cref(assets_.at(location->index))};
    }
    return std::nullopt;
}

const ControlNode* OrdinaryFormDocument::find_control(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::control, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.controls[found->index];
}

const Page* OrdinaryFormDocument::find_page(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::page, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.pages[found->index];
}

const Attribute* OrdinaryFormDocument::find_attribute(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::attribute, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.attributes[found->index];
}

const Command* OrdinaryFormDocument::find_command(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::command, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.commands[found->index];
}

const Event* OrdinaryFormDocument::find_event(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::event, id);
    if (!found) {
        return nullptr;
    }
    return &collections_.events[found->index];
}

const PictureAsset* OrdinaryFormDocument::find_asset(ObjectId id) const noexcept {
    const auto found = find_location(ObjectCategory::picture_asset, id);
    if (!found) {
        return nullptr;
    }
    return &assets_[found->index];
}

std::optional<OrdinaryFormDocument::ObjectLocation> OrdinaryFormDocument::find_location(
    ObjectCategory category,
    ObjectId id) const noexcept {
    const auto found = index_.find(ObjectKey{category, id});
    return found == index_.end() ? std::nullopt : std::optional<ObjectLocation>{found->second};
}

std::size_t OrdinaryFormDocument::indexed_id_count() const noexcept {
    return index_.size();
}

ValidationReport OrdinaryFormDocument::validate() const {
    ValidationReport report;
    std::unordered_set<ObjectKey, ObjectKeyHash> seen;

    const auto record_id = [&](ObjectId id, ObjectCategory category) {
        if (!id) {
            add_violation(
                report,
                InvariantCode::invalid_id,
                id,
                id,
                "ordinary-form object IDs must be nonzero");
        }
        if (!seen.insert(ObjectKey{category, id}).second) {
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

    std::unordered_map<ObjectId, std::size_t, ObjectIdHash> event_reference_counts;
    const auto require_owned_event = [&]<typename OwnerRef>(
                                         ObjectId source,
                                         EventRef reference,
                                         OwnerRef expected_owner,
                                         auto&& descriptor_lookup) {
        require_event(source, reference);
        const Event* event = find_event(reference.id());
        if (event == nullptr) {
            return;
        }
        ++event_reference_counts[event->id];
        const OwnerRef* actual_owner = std::get_if<OwnerRef>(&event->owner);
        if (actual_owner == nullptr || actual_owner->id() != expected_owner.id()) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                source,
                event->id,
                "event is listed by an owner different from Event.owner");
        }
        if (descriptor_lookup(event->name) == nullptr) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                source,
                event->id,
                "event name is not declared for its ordinary-form owner");
        }
    };
    const auto require_picture = [&](ObjectId source, const PictureRef& reference) {
        if (reference.standard_name) {
            if (reference.asset.id() ||
                metamodel::find_standard_picture(reference.standard_name->value) == nullptr) {
                add_violation(report, InvariantCode::invalid_property, source, reference.asset.id(),
                    "standard picture reference must name one known descriptor and no file asset");
            }
            return;
        }
        if (!reference.asset.id()) {
            add_violation(report, InvariantCode::dangling_reference, source, reference.asset.id(),
                "picture reference has neither a file asset nor a standard picture name");
            return;
        }
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

    const auto validate_position = [&](ObjectId source, const Position& position) {
        if (position.width.value() < 0 || position.height.value() < 0) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                source,
                {},
                "Position dimensions must be non-negative");
        }
        std::array<bool, 6> anchor_coordinates{};
        for (const auto& binding : position.bindings.anchors) {
            const auto coordinate = static_cast<std::size_t>(binding.coordinate);
            if (coordinate >= anchor_coordinates.size() || anchor_coordinates[coordinate]) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "Position contains a duplicate binding coordinate");
            } else {
                anchor_coordinates[coordinate] = true;
            }
            if (binding.target.has_value()) {
                require_control(source, *binding.target);
            }
            const auto target_coordinate = static_cast<std::size_t>(binding.target_coordinate);
            if (target_coordinate >= anchor_coordinates.size()) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "Position contains an invalid target binding coordinate");
            }
            if (binding.proportional.has_value()) {
                const auto& target = *binding.proportional;
                const auto proportional_coordinate = static_cast<std::size_t>(target.coordinate);
                if (proportional_coordinate >= anchor_coordinates.size()) {
                    add_violation(
                        report,
                        InvariantCode::invalid_property,
                        source,
                        {},
                        "Position contains an invalid proportional target coordinate");
                }
                if (target.target.has_value()) {
                    require_control(source, *target.target);
                }
            }
        }
        std::array<bool, 5> binding_dimensions{};
        for (const auto& binding : position.bindings.dimensions) {
            const auto dimension = static_cast<std::size_t>(binding.dimension);
            if (dimension >= binding_dimensions.size() || binding_dimensions[dimension]) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "Position contains a duplicate dimension binding");
            } else {
                binding_dimensions[dimension] = true;
            }
        }
    };

    const auto child_id = [](const ChildItemRef& child) {
        return std::visit([](const auto& reference) { return reference.id(); }, child);
    };
    const auto child_key = [](const ChildItemRef& child) {
        return std::visit(
            [](const auto& reference) {
                using ReferenceType = std::remove_cvref_t<decltype(reference)>;
                if constexpr (std::is_same_v<ReferenceType, ControlRef>) {
                    return ObjectKey{ObjectCategory::control, reference.id()};
                } else {
                    return ObjectKey{ObjectCategory::page, reference.id()};
                }
            },
            child);
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
    }
    std::set<std::string_view> form_event_names;
    for (const EventRef event : form_.events) {
        require_owned_event(
            form_.id,
            event,
            FormRef{form_.id},
            [](std::string_view name) { return metamodel::find_form_event(name); });
        if (const Event* resolved = find_event(event.id());
            resolved != nullptr && !form_event_names.insert(resolved->name).second) {
            add_violation(
                report,
                InvariantCode::invalid_property,
                form_.id,
                resolved->id,
                "event name occurs more than once for the same owner");
        }
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

    std::unordered_map<ObjectKey, std::size_t, ObjectKeyHash> parent_counts;
    const auto register_parent = [&](ObjectId source, const ChildItemRef& child) {
        const ObjectId id = child_id(child);
        const ObjectKey key = child_key(child);
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
        const std::size_t count = ++parent_counts[key];
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
        const std::vector<CommandBarButton>* owned_buttons = nullptr;
        if (const auto* button = std::get_if<ButtonPayload>(&control.payload)) owned_buttons = &button->buttons;
        if (const auto* command_bar = std::get_if<CommandBarPayload>(&control.payload)) owned_buttons = &command_bar->buttons;
        if (owned_buttons != nullptr) {
            const auto validate_buttons = [&](const auto& self, const std::vector<CommandBarButton>& items, std::size_t depth) -> void {
                if (depth > 256) {
                    add_violation(report, InvariantCode::invalid_property, control.id, {}, "button menu nesting is too deep");
                    return;
                }
                std::set<std::string> names;
                for (const auto& item : items) {
                    const auto invalid = [&](std::string reason) {
                        add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(reason));
                    };
                    if (item.type != CommandBarButtonKind::action && item.type != CommandBarButtonKind::submenu && item.type != CommandBarButtonKind::separator) invalid("unknown menu item type");
                    if (item.representation != ButtonRepresentation::automatic && item.representation != ButtonRepresentation::picture && item.representation != ButtonRepresentation::text && item.representation != ButtonRepresentation::picture_text) invalid("unknown menu representation");
                    if (item.name.empty() || !names.insert(item.name).second) invalid("button menu item names must be non-empty and unique within each collection");
                    if (item.type == CommandBarButtonKind::action && (!item.action || item.action->empty())) invalid("Action menu item requires a handler");
                    if (item.type != CommandBarButtonKind::action && item.action) invalid("only Action menu items may have a handler");
                    if (item.order != CommandBarButtonOrder::none && item.order != CommandBarButtonOrder::ascending && item.order != CommandBarButtonOrder::descending) invalid("unknown menu order");
                    if (item.type != CommandBarButtonKind::submenu && item.order != CommandBarButtonOrder::none) invalid("only Submenu menu items may have an order");
                    if (item.type != CommandBarButtonKind::submenu && !item.buttons.empty()) invalid("only Submenu items may contain buttons");
                    if (item.type == CommandBarButtonKind::separator &&
                        (!item.text.empty() || !item.explanation.empty() || !item.tooltip.empty() || !item.enabled || item.checked || item.changes_data || item.representation != ButtonRepresentation::automatic || item.shortcut != ShortcutValue{} || item.picture || item.action)) invalid("Separator cannot have properties");
                    if (metamodel::find_shortcut_key(item.shortcut.key) == nullptr) invalid("button menu Shortcut has unsupported key");
                    if (item.picture) require_picture(control.id, *item.picture);
                    self(self, item.buttons, depth + 1);
                }
            };
            validate_buttons(validate_buttons, *owned_buttons, 0);
        }
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
        std::set<std::string_view> control_event_names;
        for (const EventRef event : control.events) {
            require_owned_event(
                control.id,
                event,
                ControlRef{control.id},
                [&](std::string_view name) {
                    return metamodel::find_event(control.kind(), name);
                });
            if (const Event* resolved = find_event(event.id());
                resolved != nullptr && !control_event_names.insert(resolved->name).second) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    control.id,
                    resolved->id,
                    "event name occurs more than once for the same owner");
            }
        }
        validate_position(control.id, control.position);
        if (control.data_path.has_value()) {
            require_attribute(control.id, control.data_path->attribute);
        }
        if (const auto* table = std::get_if<TablePayload>(&control.payload)) {
            const auto invalid_table = [&](std::string reason) {
                add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(reason));
            };
            if (!control.data_path || !control.data_path->members.empty()) {
                invalid_table("Table requires a direct DataPath to a ValueTable Attribute");
            } else if (const Attribute* attribute = find_attribute(control.data_path->attribute.id());
                       attribute == nullptr || attribute->type.entries.size() != 1 ||
                       [&] {
                           TypeDomainEntry expected;
                           expected.term = TypeDomainTerm::value_table;
                           return attribute->type.entries.front() != expected;
                       }()) {
                invalid_table("Table DataPath must reference the named ValueTable type");
            }
            std::set<std::string> column_names;
            for (const auto& column : table->columns) {
                if (column.name.empty() || !column_names.insert(column.name).second) {
                    invalid_table("Table Column names must be non-empty and unique");
                }
                if (column.data_path.empty()) {
                    invalid_table("Table Column DataPath must be non-empty");
                }
                if (column.header.items.empty()) {
                    invalid_table("Table Column Header must contain localized text");
                }
                std::set<std::string> languages;
                for (const auto& item : column.header.items) {
                    if (item.language.empty() || !languages.insert(item.language).second) {
                        invalid_table("Table Column Header languages must be non-empty and unique");
                    }
                }
                if (column.control.kind != ControlKind::input_field) {
                    invalid_table("Table Column Control currently supports only InputField");
                }
                column.control.properties.for_each_explicit([&](const PropertyEntry& entry) {
                    const auto* property = metamodel::find_property(ControlKind::input_field, entry.id);
                    if (property == nullptr ||
                        (property->api_name != "Enabled" && property->api_name != "ReadOnly") ||
                        !std::holds_alternative<bool>(entry.value)) {
                        invalid_table("Table Column InputField supports only Boolean Enabled and ReadOnly");
                        return;
                    }
                    const bool value = std::get<bool>(entry.value);
                    if ((property->api_name == "Enabled" && !value) ||
                        (property->api_name == "ReadOnly" && value)) {
                        invalid_table("Table Column InputField supports only Enabled=true and ReadOnly=false");
                    }
                });
            }
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
        validate_position(page.id, page.position.value());
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

    for (const auto& event : collections_.events) {
        const std::size_t count = event_reference_counts[event.id];
        if (count == 0) {
            add_violation(
                report,
                InvariantCode::orphan,
                form_.id,
                event.id,
                "event is not present in its owner's authoritative event sequence");
        } else if (count > 1) {
            add_violation(
                report,
                InvariantCode::multiple_parents,
                form_.id,
                event.id,
                "event appears more than once in authoritative event sequences");
        }
    }

    for (const auto& control : collections_.controls) {
        if (!parent_counts.contains(ObjectKey{ObjectCategory::control, control.id})) {
            add_violation(
                report,
                InvariantCode::orphan,
                form_.id,
                control.id,
                "control is not present in the authoritative child tree");
        }
    }
    for (const auto& page : collections_.pages) {
        if (!parent_counts.contains(ObjectKey{ObjectCategory::page, page.id})) {
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
    std::unordered_map<ObjectKey, VisitState, ObjectKeyHash> visit_states;
    std::function<void(ObjectKey, const std::vector<ChildItemRef>&)> visit =
        [&](ObjectKey source, const std::vector<ChildItemRef>& children) {
        visit_states[source] = VisitState::visiting;
        for (const ChildItemRef& child_ref : children) {
            const ObjectId id = child_id(child_ref);
            const ObjectKey key = child_key(child_ref);
            const auto state = visit_states.find(key);
            if (state != visit_states.end() && state->second == VisitState::visiting) {
                add_violation(
                    report,
                    InvariantCode::cycle,
                    source.id,
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
                                visit(key, child->children);
                            }
                        } else if (const Page* child = find_page(reference.id())) {
                            visit(key, child->children);
                        }
                    },
                    child_ref);
            }
        }
        visit_states[source] = VisitState::complete;
    };

    for (const auto& control : collections_.controls) {
        if (!visit_states.contains(ObjectKey{ObjectCategory::control, control.id})) {
            visit(ObjectKey{ObjectCategory::control, control.id}, control.children);
        }
    }
    for (const auto& page : collections_.pages) {
        if (!visit_states.contains(ObjectKey{ObjectCategory::page, page.id})) {
            visit(ObjectKey{ObjectCategory::page, page.id}, page.children);
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
    index_.try_emplace(ObjectKey{category, id}, ObjectLocation{category, index});
}

}  // namespace oof::model
