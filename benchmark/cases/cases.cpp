#include "case.hpp"
#include "joint_sine/case.hpp"
#include "posture_hold/case.hpp"
#include "joint_step/case.hpp"
#include "joint_tracking/case.hpp"
#include "target_switch/case.hpp"

#include <stdexcept>

namespace simlab::benchmark {

std::string_view phaseKindName(PhaseKind kind) {
    switch (kind) {
    case PhaseKind::Initialization: return "initialization";
    case PhaseKind::Response: return "response";
    case PhaseKind::Hold: return "hold";
    case PhaseKind::Tracking: return "tracking";
    }
    throw std::logic_error("Unknown benchmark phase kind.");
}

const PhaseSpec *phaseAt(const CaseSpec &spec, double time) {
    for (const auto &phase : spec.phases) {
        if (time >= phase.start && time < phase.end) return &phase;
    }
    return nullptr;
}

std::vector<CaseSpec> selectCases(std::string_view selection) {
    const std::vector<CaseSpec> definitions{joint_sine::definition(), posture_hold::definition(),
        joint_step::definition(), joint_tracking::definition(), target_switch::definition()};
    std::vector<CaseSpec> selected;
    for (const auto &definition : definitions) {
        if (selection == "all" || selection == definition.id) selected.push_back(definition);
    }
    if (selected.empty()) throw std::invalid_argument("Unknown benchmark case: " + std::string(selection));
    return selected;
}

} // namespace simlab::benchmark
