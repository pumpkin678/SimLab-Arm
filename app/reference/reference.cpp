#include "app/reference/reference.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace simlab {

namespace {

JointCommand jointSine(double time) {
    JointCommand command;
    command.position[0] = static_cast<float>(
        -std::numbers::pi / 12.0 * (1.0 - std::cos(2.0 * std::numbers::pi * time / 4.0)));
    return command;
}

constexpr ReferenceCase kJointSine{"joint_sine", 8.0, jointSine};

} // namespace

const ReferenceCase &findReferenceCase(std::string_view name) {
    if (name == kJointSine.name) return kJointSine;
    throw std::runtime_error("Unknown reference case: " + std::string(name));
}

ReferenceRun::ReferenceRun(Simulation &simulation, const ReferenceCase *reference_case)
    : simulation_(simulation),
      reference_case_(reference_case) {
    if (!reference_case_) return;
    const double timestep = simulation_.model().opt.timestep;
    if (!reference_case_->evaluate || !std::isfinite(reference_case_->duration) ||
        reference_case_->duration <= 0 || !std::isfinite(timestep) || timestep <= 0) {
        throw std::runtime_error("Reference case requires a function, positive finite duration and timestep.");
    }
    const double steps = std::ceil(reference_case_->duration / timestep);
    if (!std::isfinite(steps) || steps >= static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("Reference case requires too many physics steps.");
    }
    total_steps_ = static_cast<std::size_t>(steps);
}

JointCommand ReferenceRun::command() const {
    const auto command = reference_case_ ? reference_case_->evaluate(simulation_.data().time) : manual_target_;
    for (std::size_t index = 0; index < command.position.size(); ++index) {
        if (!std::isfinite(command.position[index])) {
            throw std::runtime_error("Non-finite reference position for J" + std::to_string(index + 1));
        }
    }
    return command;
}

bool ReferenceRun::step() {
    if (completed()) return false;
    simulation_.step(command());
    ++steps_;
    return true;
}

void ReferenceRun::reset() {
    simulation_.reset();
    manual_target_ = {};
    steps_ = 0;
}

} // namespace simlab
