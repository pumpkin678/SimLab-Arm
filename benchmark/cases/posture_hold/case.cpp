#include "case.hpp"
#include "benchmark/cases/teaching.hpp"

#include <algorithm>

namespace simlab::benchmark::posture_hold {

namespace {

constexpr double kStageDuration = 6;
constexpr double kApproachDuration = 4;
constexpr std::array kTargets{kPoseA, kPoseB, kPoseA};
constexpr double kDuration = kStageDuration * kTargets.size();

JointCommand postureHold(double time) {
    const auto index = std::min(static_cast<std::size_t>(time / kStageDuration), kTargets.size() - 1);
    return kTargets[index];
}

} // namespace

CaseSpec definition() {
    CaseSpec spec{"posture_hold", kDuration,
        {{"stage_duration", "s", kStageDuration}, {"approach_duration", "s", kApproachDuration}}, postureHold};
    appendPoseParameters(spec.parameters, "pose_a", kPoseA);
    appendPoseParameters(spec.parameters, "pose_b", kPoseB);
    JointCommand previous{};
    for (std::size_t index = 0; index < kTargets.size(); ++index) {
        const auto name = "pose_" + std::to_string(index + 1);
        const double start = index * kStageDuration;
        spec.phases.push_back({name + "_approach", start, start + kApproachDuration,
            PhaseKind::Response, movingJoints(previous, kTargets[index])});
        spec.phases.push_back({name + "_hold", start + kApproachDuration, start + kStageDuration, PhaseKind::Hold});
        previous = kTargets[index];
    }
    return spec;
}

} // namespace simlab::benchmark::posture_hold
