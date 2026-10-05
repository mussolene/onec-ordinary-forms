#include "oof/model/ordinary_form.hpp"

#include <array>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "oof/model/metamodel.hpp"
#include "oof/storage/value_codec.hpp"

namespace oof::model {

bool valid_border_value(const BorderValue& value) noexcept {
    if (static_cast<unsigned>(value.border_type) > static_cast<unsigned>(ControlBorderType::rounded) ||
        value.width > 5) return false;
    switch (value.kind) {
        case BorderKind::absolute:
            return std::holds_alternative<std::monostate>(value.style) &&
                (value.border_type != ControlBorderType::without_border || value.width <= 1) &&
                (value.border_type != ControlBorderType::rounded || value.width == 1);
        case BorderKind::style_reference:
            if (value.border_type != ControlBorderType::without_border || value.width != 0) return false;
            if (const auto* name = std::get_if<QualifiedName>(&value.style)) return !name->value.empty();
            if (const auto* composite = std::get_if<CompositeIdValue>(&value.style)) {
                const auto& uuid = composite->uuid.canonical;
                if (composite->is_null || uuid.size() != 36 ||
                    (composite->object_id == 0 && uuid == "00000000-0000-0000-0000-000000000000")) return false;
                for (std::size_t index = 0; index < uuid.size(); ++index) {
                    if (index == 8 || index == 13 || index == 18 || index == 23) {
                        if (uuid[index] != '-') return false;
                    } else if (!((uuid[index] >= '0' && uuid[index] <= '9') ||
                                 (uuid[index] >= 'a' && uuid[index] <= 'f') ||
                                 (uuid[index] >= 'A' && uuid[index] <= 'F'))) return false;
                }
                return true;
            }
            return false;
    }
    return false;
}

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
    const metamodel::PropertyDescriptor& descriptor,
    const PropertyValue& value
) noexcept {
    return std::visit(
        [&descriptor](const auto& typed_value) {
            const auto expected = descriptor.value_codec;
            using Value = std::remove_cvref_t<decltype(typed_value)>;
            if constexpr (std::is_same_v<Value, UndefinedValue>) {
                return expected == metamodel::ValueCodec::date ||
                       expected == metamodel::ValueCodec::action_source_reference;
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
                if (expected != metamodel::ValueCodec::enumeration) return false;
                if (descriptor.control_kind == ControlKind::command_bar &&
                    descriptor.api_name == "ButtonsAlignment") {
                    return typed_value.type_name == "CommandBarButtonAlignment" &&
                        (typed_value.member == "Left" || typed_value.member == "Center" ||
                         typed_value.member == "Right");
                }
                if (descriptor.control_kind == ControlKind::command_bar &&
                    descriptor.api_name == "Orientation") {
                    return typed_value.type_name == "Orientation" &&
                        (typed_value.member == "Auto" || typed_value.member == "Horizontal" ||
                         typed_value.member == "Vertical");
                }
                return true;
            } else if constexpr (std::is_same_v<Value, BorderValue>) {
                return expected == metamodel::ValueCodec::border && valid_border_value(typed_value);
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
                return expected == metamodel::ValueCodec::control_reference ||
                       expected == metamodel::ValueCodec::action_source_reference;
            } else if constexpr (std::is_same_v<Value, FormRef>) {
                return expected == metamodel::ValueCodec::action_source_reference;
            } else if constexpr (std::is_same_v<Value, AttributeRef>) {
                return expected == metamodel::ValueCodec::attribute_reference;
            } else if constexpr (std::is_same_v<Value, CommandRef>) {
                return expected == metamodel::ValueCodec::command_reference;
            }
            return false;
        },
        value);
}

bool table_column_editor_property_allowed(ControlKind kind, std::string_view name) {
    switch (kind) {
        case ControlKind::input_field:
            return name == "Enabled" || name == "ReadOnly";
        case ControlKind::choice_field:
            return name == "Enabled" || name == "ToolTip";
        case ControlKind::check_box:
            return name == "Enabled" || name == "Caption" || name == "ToolTip" || name == "Font";
        default:
            return false;
    }
}

bool table_column_editor_property_default(std::string_view name, const PropertyValue& value) {
    if (name == "Enabled") return std::holds_alternative<bool>(value) && std::get<bool>(value);
    if (name == "ReadOnly") return std::holds_alternative<bool>(value) && !std::get<bool>(value);
    if (name == "Caption" || name == "ToolTip") {
        return std::holds_alternative<std::string>(value) && std::get<std::string>(value).empty();
    }
    if (name == "Font") {
        return std::holds_alternative<FontValue>(value) && std::get<FontValue>(value) == FontValue{};
    }
    return false;
}

bool canonical_chart_decimal(std::string_view value) {
    if (value.empty()) return false;
    if (value.front() == '-') value.remove_prefix(1);
    if (value.empty()) return false;
    const auto point = value.find('.');
    if (point != std::string_view::npos && value.find('.', point + 1) != std::string_view::npos) return false;
    const auto integer = point == std::string_view::npos ? value : value.substr(0, point);
    const auto fraction = point == std::string_view::npos ? std::string_view{} : value.substr(point + 1);
    const auto digits = [](std::string_view part) {
        return std::ranges::all_of(part, [](unsigned char c) { return c >= '0' && c <= '9'; });
    };
    if ((!integer.empty() && !digits(integer)) || (!fraction.empty() && !digits(fraction)) ||
        (integer.empty() && fraction.empty())) return false;
    if (integer.size() > 1 && integer.front() == '0') return false;
    if (!fraction.empty() && fraction.back() == '0') return false;
    if (fraction.empty() && point != std::string_view::npos) return false;
    return !(value == "-0");
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
    if (form_.main_attribute.id() && find_attribute(form_.main_attribute.id()) == nullptr) {
        add_violation(report, InvariantCode::dangling_reference, form_.id, form_.main_attribute.id(),
            "main form attribute must reference an existing Attribute");
    }
    if (form_.extension && *form_.extension != metamodel::data_processor_form_extension.kind) {
        add_violation(report, InvariantCode::invalid_property, form_.id, form_.id,
            "form extension is not declared by the executable metamodel");
    }
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
                } else if constexpr (std::is_same_v<Value, FormRef>) {
                    if (typed_value.id() != form_.id) {
                        add_violation(report, InvariantCode::dangling_reference, source,
                            typed_value.id(), "form reference does not resolve to this form");
                    }
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
            } else if (!property_value_matches(*descriptor, property.value)) {
                add_violation(
                    report,
                    InvariantCode::invalid_property,
                    source,
                    {},
                    "property value does not match its metamodel value kind");
            }
            if (descriptor != nullptr && descriptor->api_name == "ActionSource" &&
                descriptor->control_kind == ControlKind::command_bar) {
                if (const auto* target = std::get_if<ControlRef>(&property.value)) {
                    const ControlNode* source_control = find_control(target->id());
                    if (source_control != nullptr &&
                        source_control->kind() != ControlKind::table &&
                        source_control->kind() != ControlKind::html_document_field) {
                        add_violation(report, InvariantCode::invalid_property, source,
                            target->id(), "CommandBar ActionSource control must be Table or HTMLDocumentField");
                    }
                }
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

    validate_property_set(form_.id, form_.panel.properties,
        [](PropertyId id) { return metamodel::find_property(ControlKind::panel, id); },
        [](const metamodel::PropertyDescriptor& descriptor) { return descriptor.surface == metamodel::PropertySurface::control_payload &&
            descriptor.persistence == metamodel::PersistenceClass::persisted_editable; });

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

    std::size_t default_button_count = 0;
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
                    if (item.default_button) {
                        const auto* secondary = control.properties().find(PropertyId::from_name("Secondary"));
                        if (control.kind() != ControlKind::command_bar || secondary == nullptr ||
                            !std::holds_alternative<bool>(secondary->value) || std::get<bool>(secondary->value) ||
                            item.type != CommandBarButtonKind::action || depth != 0)
                            invalid("DefaultButton requires a top-level Action on a primary CommandBar (Secondary=false)");
                        if (++default_button_count > 1) invalid("the form may have only one DefaultButton");
                    }
                    if (item.type != CommandBarButtonKind::action && item.type != CommandBarButtonKind::submenu && item.type != CommandBarButtonKind::separator) invalid("unknown menu item type");
                    if (item.representation != ButtonRepresentation::automatic && item.representation != ButtonRepresentation::picture && item.representation != ButtonRepresentation::text && item.representation != ButtonRepresentation::picture_text) invalid("unknown menu representation");
                    if (item.name.empty() || !names.insert(item.name).second) invalid("button menu item names must be non-empty and unique within each collection");
                    if (item.type == CommandBarButtonKind::action && (!item.action || item.action->handler.empty())) invalid("Action menu item requires a handler");
                    if (item.type != CommandBarButtonKind::action && item.action) invalid("only Action menu items may have an Action value");
                    if (item.order != CommandBarButtonOrder::none && item.order != CommandBarButtonOrder::ascending && item.order != CommandBarButtonOrder::descending) invalid("unknown menu order");
                    if (item.type != CommandBarButtonKind::submenu && item.order != CommandBarButtonOrder::none) invalid("only Submenu menu items may have an order");
                    if (item.type != CommandBarButtonKind::submenu && !item.buttons.empty()) invalid("only Submenu items may contain buttons");
                    if (item.type == CommandBarButtonKind::separator &&
                        (item.text || item.explanation || item.tooltip || !item.enabled || item.checked || item.changes_data || item.representation != ButtonRepresentation::automatic || item.shortcut != ShortcutValue{} || item.picture || item.action)) invalid("Separator cannot have properties");
                    if (metamodel::find_shortcut_key(item.shortcut.key) == nullptr) invalid("button menu Shortcut has unsupported key");
                    if (item.picture) require_picture(control.id, *item.picture);
                    self(self, item.buttons, depth + 1);
                }
            };
            validate_buttons(validate_buttons, *owned_buttons, 0);
        }
        const auto& descriptor = metamodel::descriptor_for(control.kind());
        if (const auto* spreadsheet = std::get_if<SpreadsheetDocumentFieldPayload>(&control.payload)) {
            std::set<std::pair<std::uint32_t, std::uint32_t>> coordinates;
            std::optional<std::pair<std::uint32_t, std::uint32_t>> previous;
            for (const auto& cell : spreadsheet->cells) {
                const auto coordinate = std::pair{cell.row, cell.column};
                if (cell.row == 0 || cell.column == 0 || !coordinates.emplace(coordinate).second ||
                    (previous && coordinate <= *previous)) {
                    add_violation(
                        report,
                        InvariantCode::invalid_property,
                        control.id,
                        control.id,
                        "Spreadsheet Document cells require unique positive uint32 row and column coordinates in row-major order");
                    break;
                }
                if (cell.typed_value.has_value()) {
                    const auto& typed = *cell.typed_value;
                    bool supported = false;
                    if (typed.type.entries.size() == 1) {
                        const auto& entry = typed.type.entries.front();
                        if (entry.term == TypeDomainTerm::string) {
                            const bool unrelated_qualifiers_default = !entry.type_uuid.has_value() &&
                                entry.numeric == NumericQualifiers{} && entry.binary == LengthQualifiers{} &&
                                entry.date == DateQualifiers{};
                            supported = unrelated_qualifiers_default &&
                                std::holds_alternative<std::string>(typed.value);
                        } else if (entry.term == TypeDomainTerm::numeric) {
                            const bool unrelated_qualifiers_default = !entry.type_uuid.has_value() &&
                                entry.string == LengthQualifiers{} && entry.binary == LengthQualifiers{} &&
                                entry.date == DateQualifiers{};
                            supported = unrelated_qualifiers_default &&
                                (entry.numeric.length == 0 || entry.numeric.precision <= entry.numeric.length) &&
                                std::holds_alternative<DecimalValue>(typed.value);
                        } else if (entry.term == TypeDomainTerm::boolean) {
                            supported = entry == TypeDomainEntry{.term = TypeDomainTerm::boolean} &&
                                std::holds_alternative<bool>(typed.value);
                        } else if (entry.term == TypeDomainTerm::date) {
                            supported = !entry.type_uuid.has_value() && entry.numeric == NumericQualifiers{} &&
                                entry.string == LengthQualifiers{} && entry.binary == LengthQualifiers{} &&
                                entry.date == DateQualifiers{true, true} &&
                                std::holds_alternative<DateValue>(typed.value);
                        }
                    }
                    if (!cell.text.empty() || !supported) {
                        add_violation(
                            report,
                            InvariantCode::invalid_property,
                            control.id,
                            control.id,
                            "Spreadsheet typed cells require an empty Text and a supported ValueType/Value pair");
                        break;
                    }
                }
                if (cell.control.has_value()) {
                    bool supported = cell.typed_value.has_value() &&
                        cell.control->kind == ControlKind::input_field;
                    cell.control->properties.for_each_explicit([&](const PropertyEntry& entry) {
                        if (entry.id != PropertyId::from_name("ReadOnly") ||
                            !std::holds_alternative<bool>(entry.value)) supported = false;
                    });
                    if (!supported) {
                        add_violation(report, InvariantCode::invalid_property, control.id, control.id,
                            "Spreadsheet Cell.Control requires a typed cell, InputField, and Boolean ReadOnly only");
                        break;
                    }
                }
                previous = coordinate;
            }
        }
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
        if (const auto* gantt = std::get_if<GanttChartPayload>(&control.payload)) {
            const auto invalid_gantt = [&](std::string message) {
                add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(message));
            };
            std::unordered_set<std::uint64_t> series_ids;
            std::unordered_set<std::uint64_t> point_ids;
            for (const auto& item : gantt->series) {
                if (!item.id || !series_ids.insert(item.id.value()).second) {
                    invalid_gantt("Gantt Series IDs must be non-empty and unique in their domain");
                }
            }
            for (const auto& item : gantt->points) {
                if (!item.id || !point_ids.insert(item.id.value()).second) {
                    invalid_gantt("Gantt Point IDs must be non-empty and unique in their domain");
                }
            }
            const auto platform_date_key = [](const DateValue& date) {
                const std::string encoded = storage::value_codec::date_to_platform(date.canonical);
                std::uint64_t key = 0;
                const auto parsed = std::from_chars(encoded.data(), encoded.data() + encoded.size(), key, 10);
                if (encoded.empty() || parsed.ec != std::errc{} || parsed.ptr != encoded.data() + encoded.size())
                    throw std::invalid_argument("invalid encoded platform date");
                return key;
            };
            for (const auto& interval : gantt->intervals) {
                if (!series_ids.contains(interval.series_ref.value()) || !point_ids.contains(interval.point_ref.value())) {
                    invalid_gantt("Gantt Interval references must resolve to a Series and Point in the same control");
                }
                try {
                    if (platform_date_key(interval.start_date) > platform_date_key(interval.end_date)) {
                        invalid_gantt("Gantt Interval start date must not follow its end date");
                    }
                } catch (const std::exception&) {
                    invalid_gantt("Gantt Interval dates must be valid local date-times");
                }
            }
            bool auto_full_interval = true;
            const auto* auto_entry = control.properties().find(PropertyId::from_name("AutoFullInterval"));
            if (auto_entry != nullptr) {
                if (const auto* value = std::get_if<bool>(&auto_entry->value)) auto_full_interval = *value;
            }
            const auto date_property = [&](std::string_view name) -> const DateValue* {
                const auto* entry = control.properties().find(PropertyId::from_name(name));
                return entry == nullptr ? nullptr : std::get_if<DateValue>(&entry->value);
            };
            const DateValue* begin = date_property("FullIntervalBegin");
            const DateValue* end = date_property("FullIntervalEnd");
            if (!auto_full_interval && (begin == nullptr || end == nullptr)) {
                invalid_gantt("Manual Gantt full interval requires both FullIntervalBegin and FullIntervalEnd");
            }
            std::optional<std::uint64_t> begin_key;
            std::optional<std::uint64_t> end_key;
            try {
                if (begin != nullptr) begin_key = platform_date_key(*begin);
                if (end != nullptr) end_key = platform_date_key(*end);
            } catch (const std::exception&) {
                invalid_gantt("Gantt full interval properties must be valid local date-times");
            }
            if (begin_key.has_value() && end_key.has_value() && *begin_key >= *end_key) {
                invalid_gantt("Gantt FullIntervalBegin must precede FullIntervalEnd");
            }
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
                column.control.properties.for_each_explicit([&](const PropertyEntry& entry) {
                    const auto* property = metamodel::find_property(column.control.kind, entry.id);
                    if (!table_column_editor_property_allowed(column.control.kind,
                            property == nullptr ? std::string_view{} : property->api_name) ||
                        !property_value_matches(*property, entry.value) ||
                        !table_column_editor_property_default(property->api_name, entry.value)) {
                        invalid_table("Table Column editor has an incompatible, unsupported, or nondefault property");
                        return;
                    }
                });
                if (column.control.kind != ControlKind::input_field &&
                    column.control.kind != ControlKind::choice_field &&
                    column.control.kind != ControlKind::check_box) {
                    invalid_table("Table Column Control kind is unsupported");
                }
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
        if (const auto* chart = std::get_if<ChartPayload>(&control.payload)) {
            const auto invalid_chart = [&](std::string reason) {
                add_violation(report, InvariantCode::invalid_property, control.id, {}, std::move(reason));
            };
            if (control.kind() != ControlKind::chart) {
                invalid_chart("Chart payload is attached to a non-Chart control");
            }
            if (!chart->points.empty() && chart->series.size() >
                std::numeric_limits<std::size_t>::max() / chart->points.size()) {
                invalid_chart("Chart dense Values dimensions overflow");
            } else if (chart->values.size() != chart->series.size() * chart->points.size()) {
                invalid_chart("Chart Values must be a dense Series by Point matrix");
            }
            std::unordered_set<ObjectId, ObjectIdHash> series_ids;
            for (const auto& series : chart->series) {
                if (!series.id || series.id.value() == 1 || !series_ids.insert(series.id).second) {
                    invalid_chart("Chart Series IDs must be unique, positive, and distinct from reserved summary ID 1");
                }
                const bool absolute_color = series.color.kind == ColorKind::absolute && series.color.alpha == 255 &&
                    std::holds_alternative<std::monostate>(series.color.style);
                if (!absolute_color) {
                    invalid_chart("Chart Series Color must be an absolute opaque RGB value");
                }
                if (series.marker.type_name != "ChartMarkerType" ||
                    (series.marker.member != "Auto" && series.marker.member != "Alternation" &&
                     series.marker.member != "Rect" && series.marker.member != "Circle" &&
                     series.marker.member != "None" && series.marker.member != "Rhomb")) {
                    invalid_chart("Chart Series Marker must name a supported ТипМаркераДиаграммы member");
                }
            }
            std::unordered_set<ObjectId, ObjectIdHash> point_ids;
            for (const auto& point : chart->points) {
                if (!point.id || !point_ids.insert(point.id).second) {
                    invalid_chart("Chart Point IDs must be positive and unique");
                }
                const bool absolute_color = point.color.kind == ColorKind::absolute && point.color.alpha == 255 &&
                    std::holds_alternative<std::monostate>(point.color.style);
                if (!absolute_color) {
                    invalid_chart("Chart Point Color must be an absolute opaque RGB value");
                }
            }
            std::set<std::pair<ObjectId, ObjectId>> value_pairs;
            for (const auto& value : chart->values) {
                if (!series_ids.contains(value.series_ref) || !point_ids.contains(value.point_ref)) {
                    invalid_chart("Chart Value references must resolve to a Series and a Point");
                }
                if (!value_pairs.emplace(value.series_ref, value.point_ref).second) {
                    invalid_chart("Chart Value pairs must be unique");
                }
                if (const auto* number = std::get_if<DecimalValue>(&value.value);
                    number != nullptr && !canonical_chart_decimal(number->canonical)) {
                    invalid_chart("Chart Value Number must contain a decimal value");
                }
            }
        }
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
