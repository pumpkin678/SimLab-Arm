#ifndef SIMLAB_BENCHMARK_CASES_JOINT_TRACKING_SEQUENCE_HPP
#define SIMLAB_BENCHMARK_CASES_JOINT_TRACKING_SEQUENCE_HPP

#include "benchmark/cases/teaching.hpp"

#include <algorithm>
#include <cmath>

namespace simlab::benchmark::joint_tracking::sequence {

inline constexpr double kInitialization = 4;
inline constexpr double kTrackingDuration = 4;
inline constexpr double kRecoveryDuration = 2;
inline constexpr double kAxisDuration = kTrackingDuration + kRecoveryDuration;
inline constexpr double kDuration = kInitialization + 6 * kAxisDuration;
inline constexpr float kAmplitude = 10 * kDegreesToRadians;

inline JointCommand evaluate(double time, double period) {
    auto command = kPoseA;
    if (time < kInitialization) return command;
    const auto joint = std::min(static_cast<std::size_t>((time - kInitialization) / kAxisDuration), std::size_t{5});
    const double local_time = time - kInitialization - joint * kAxisDuration;
    if (local_time < kTrackingDuration) {
        command.position[joint] += static_cast<float>(kAmplitude * std::sin(2 * std::numbers::pi * local_time / period));
    }
    return command;
}

inline CaseSpec definition(const std::string &id, double period, JointCommand (*evaluate)(double)) {
    CaseSpec spec{id, kDuration,
        {{"initialization_duration", "s", kInitialization}, {"tracking_duration", "s", kTrackingDuration},
         {"recovery_duration", "s", kRecoveryDuration}, {"period", "s", period}, {"amplitude", "rad", kAmplitude}}, evaluate};
    appendPoseParameters(spec.parameters, "pose_a", kPoseA);
    spec.phases.push_back({"initialize", 0, kInitialization, PhaseKind::Initialization});
    for (std::size_t joint = 0; joint < 6; ++joint) {
        const double start = kInitialization + joint * kAxisDuration;
        const auto name = "j" + std::to_string(joint + 1);
        std::array<bool, 6> active{};
        active[joint] = true;
        spec.phases.push_back({name + "_tracking", start, start + kTrackingDuration, PhaseKind::Tracking, active});
        spec.phases.push_back({name + "_recovery", start + kTrackingDuration, start + kAxisDuration, PhaseKind::Recovery, active});
    }
    return spec;
}

} // namespace simlab::benchmark::joint_tracking::sequence

#endif
