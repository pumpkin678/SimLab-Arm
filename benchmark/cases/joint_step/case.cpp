#include "case.hpp"
#include "benchmark/cases/teaching.hpp"

#include <algorithm>

namespace simlab::benchmark::joint_step {

namespace {

constexpr double kInitialization = 4;
constexpr double kStepDuration = 2;
constexpr std::array<float, 5> kOffsets{5 * kDegreesToRadians, -5 * kDegreesToRadians,
    20 * kDegreesToRadians, -20 * kDegreesToRadians, 0};
constexpr double kDuration = kInitialization + 6 * kOffsets.size() * kStepDuration;

JointCommand jointStep(double time) {
    auto command = kPoseA;
    if (time < kInitialization) return command;
    const auto stage = std::min(static_cast<std::size_t>((time - kInitialization) / kStepDuration), 6 * kOffsets.size() - 1);
    command.position[stage / kOffsets.size()] += kOffsets[stage % kOffsets.size()];
    return command;
}

} // namespace

CaseSpec definition() {
    CaseSpec spec{"joint_step", kDuration,
        {{"initialization_duration", "s", kInitialization}, {"step_duration", "s", kStepDuration}}, jointStep};
    appendPoseParameters(spec.parameters, "pose_a", kPoseA);
    for (std::size_t offset = 0; offset < kOffsets.size(); ++offset) {
        spec.parameters.push_back({"offset_" + std::to_string(offset + 1), "rad", kOffsets[offset]});
    }
    spec.phases.push_back({"initialize", 0, kInitialization, PhaseKind::Initialization});
    for (std::size_t joint = 0; joint < 6; ++joint) {
        for (std::size_t offset = 0; offset < kOffsets.size(); ++offset) {
            const double start = kInitialization + (joint * kOffsets.size() + offset) * kStepDuration;
            std::array<bool, 6> active{};
            active[joint] = true;
            spec.phases.push_back({"j" + std::to_string(joint + 1) + "_step_" + std::to_string(offset + 1),
                start, start + kStepDuration, PhaseKind::Response, active});
        }
    }
    return spec;
}

} // namespace simlab::benchmark::joint_step
