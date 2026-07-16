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

}  // namespace

ControlKind payload_kind(const ControlPayload& payload) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using Payload = std::remove_cvref_t<decltype(value)>;
            return Payload::kind;
        },
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

    for (const ControlRef child : form_.children) {
        require_control(form_.id, child);
    }
    for (const EventRef event : form_.events) {
        require_event(form_.id, event);
    }

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
    const auto register_parent = [&](ObjectId source, ControlRef child) {
        if (find_control(child.id()) == nullptr) {
            return;
        }
        const std::size_t count = ++parent_counts[child.id()];
        if (count > 1) {
            add_violation(
                report,
                InvariantCode::multiple_parents,
                source,
                child.id(),
                "control appears in more than one authoritative child sequence");
        }
    };
    for (const ControlRef child : form_.children) {
        register_parent(form_.id, child);
    }

    for (const auto& control : collections_.controls) {
        const auto& descriptor = metamodel::descriptor_for(control.kind());
        if (!control.children.empty() &&
            descriptor.child_policy == metamodel::ChildPolicy::forbidden) {
            add_violation(
                report,
                InvariantCode::illegal_children,
                control.id,
                control.children.front().id(),
                "control kind does not permit child controls");
        }

        for (const ControlRef child : control.children) {
            require_control(control.id, child);
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

        std::visit(
            [&](const auto& payload) {
                if constexpr (requires { payload.data_attribute; }) {
                    if (const auto& attribute = payload.data_attribute.value();
                        attribute.has_value()) {
                        require_attribute(control.id, *attribute);
                    }
                }
                if constexpr (requires { payload.command; }) {
                    if (const auto& command = payload.command.value(); command.has_value()) {
                        require_command(control.id, *command);
                    }
                }
                if constexpr (requires { payload.picture; }) {
                    if (const auto& picture = payload.picture.value(); picture.has_value()) {
                        require_picture(control.id, *picture);
                    }
                }
            },
            control.payload);
    }

    enum class VisitState : std::uint8_t {
        visiting,
        complete,
    };
    std::unordered_map<ObjectId, VisitState, ObjectIdHash> visit_states;
    std::function<void(const ControlNode&)> visit = [&](const ControlNode& control) {
        visit_states[control.id] = VisitState::visiting;
        for (const ControlRef child_ref : control.children) {
            const ControlNode* child = find_control(child_ref.id());
            if (child == nullptr) {
                continue;
            }
            const auto state = visit_states.find(child->id);
            if (state != visit_states.end() && state->second == VisitState::visiting) {
                add_violation(
                    report,
                    InvariantCode::cycle,
                    control.id,
                    child->id,
                    "control child graph contains a cycle");
                continue;
            }
            if (state == visit_states.end()) {
                visit(*child);
            }
        }
        visit_states[control.id] = VisitState::complete;
    };

    for (const auto& control : collections_.controls) {
        if (!visit_states.contains(control.id)) {
            visit(control);
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
