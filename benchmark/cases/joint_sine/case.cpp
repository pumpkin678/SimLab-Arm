#include "case.hpp"

#include <cmath>
#include <numbers>

namespace simlab::benchmark::joint_sine {

namespace {

constexpr double kDuration = 8.0;
constexpr double kPeriod = 4.0;
constexpr double kCoefficient = -std::numbers::pi / 12.0;

JointCommand jointSine(double time) {
    JointCommand command;
    command.position[0] = static_cast<float>(
        kCoefficient * (1.0 - std::cos(2.0 * std::numbers::pi * time / kPeriod)));
    return command;
}

} // namespace

CaseSpec definition() {
    return {"joint_sine", kDuration,
        {{"joint", "index", 1}, {"duration", "s", kDuration},
         {"period", "s", kPeriod}, {"coefficient", "rad", kCoefficient}}, jointSine,
        {{"example_tracking", 0, kDuration, PhaseKind::Tracking, {true, false, false, false, false, false}}}};
}

} // namespace simlab::benchmark::joint_sine
