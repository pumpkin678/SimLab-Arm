#include "case.hpp"
#include "benchmark/cases/teaching.hpp"

#include <algorithm>
#include <cmath>

namespace simlab::benchmark::joint_tracking {

namespace {

constexpr double kInitialization = 4;
constexpr double kBandDuration = 4;
constexpr double kSlowPeriod = 4;
constexpr double kFastPeriod = 1;
constexpr float kAmplitude = 10 * kDegreesToRadians;
constexpr double kDuration = kInitialization + 6 * 2 * kBandDuration;

JointCommand jointTracking(double time) {
    auto command = kPoseA;
    if (time < kInitialization) return command;
    const auto band = std::min(static_cast<std::size_t>((time - kInitialization) / kBandDuration), std::size_t{11});
    const double local_time = time - kInitialization - band * kBandDuration;
    const double period = band % 2 == 0 ? kSlowPeriod : kFastPeriod;
    command.position[band / 2] += static_cast<float>(kAmplitude * std::sin(2 * std::numbers::pi * local_time / period));
    return command;
}

} // namespace

CaseSpec definition() {
    CaseSpec spec{"joint_tracking", kDuration,
        {{"initialization_duration", "s", kInitialization}, {"band_duration", "s", kBandDuration},
         {"slow_period", "s", kSlowPeriod}, {"fast_period", "s", kFastPeriod}, {"amplitude", "rad", kAmplitude}}, jointTracking};
    appendPoseParameters(spec.parameters, "pose_a", kPoseA);
    spec.phases.push_back({"initialize", 0, kInitialization, PhaseKind::Initialization});
    for (std::size_t band = 0; band < 12; ++band) {
        std::array<bool, 6> active{};
        active[band / 2] = true;
        const double start = kInitialization + band * kBandDuration;
        spec.phases.push_back({"j" + std::to_string(band / 2 + 1) + (band % 2 == 0 ? "_slow" : "_fast"),
            start, start + kBandDuration, PhaseKind::Tracking, active});
    }
    return spec;
}

} // namespace simlab::benchmark::joint_tracking
