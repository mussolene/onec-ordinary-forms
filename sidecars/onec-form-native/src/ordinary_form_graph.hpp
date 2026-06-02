#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ordinary_controls.hpp"
#include "platform_value.hpp"

namespace oof::platform::ordinary {

struct PlatformControlIdentity {
    std::uint32_t object_id = 0;
    std::string name;
};

struct PlatformControlPresentation {
    value::FormattedString title;
};

struct PlatformControlObject {
    PlatformControlIdentity identity;
    PlatformControlPresentation presentation;
    ControlPayloadRecord payload;
    ControlPositionRecord position;
    ControlInfoRecord info;
};

class OrdinaryFormGraph {
public:
    void add_control(PlatformControlObject control) {
        if (control.identity.name.empty()) {
            throw std::runtime_error("ordinary control identity name is required");
        }
        controls_.push_back(std::move(control));
    }

    const std::vector<PlatformControlObject>& controls() const {
        return controls_;
    }

    OrdinaryTransferSet transfer_set() const {
        OrdinaryTransferSet set;
        set.positions.reserve(controls_.size());
        set.infos.reserve(controls_.size());
        for (const auto& control : controls_) {
            const auto payload = control.payload.serialize();
            set.controls.bytes.insert(set.controls.bytes.end(), payload.begin(), payload.end());
            set.positions.push_back(control.position);
            set.infos.push_back(control.info);
        }
        return set;
    }

    std::vector<std::uint8_t> serialize_transfer_records() const {
        return transfer_set().serialize_records();
    }

private:
    std::vector<PlatformControlObject> controls_;
};

inline OrdinaryFormGraph make_single_control_graph(std::uint32_t object_id, std::string name, std::string title) {
    value::LocalWString localized_title;
    localized_title.add_item("ru", std::move(title));

    PlatformControlObject control;
    control.identity.object_id = object_id;
    control.identity.name = std::move(name);
    control.presentation.title = value::FormattedString(std::move(localized_title), false);
    control.payload.words[0] = cf_form_controls8;
    control.payload.words[1] = object_id;
    control.position.words[0] = cf_form_controls_position8;
    control.position.words[1] = object_id;
    control.info.words[0] = cf_form_controls_info8;
    control.info.words[1] = object_id;

    OrdinaryFormGraph graph;
    graph.add_control(std::move(control));
    return graph;
}

}  // namespace oof::platform::ordinary
