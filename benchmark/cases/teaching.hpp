#ifndef SIMLAB_BENCHMARK_CASES_TEACHING_HPP
#define SIMLAB_BENCHMARK_CASES_TEACHING_HPP

#include "case.hpp"

#include <numbers>

namespace simlab::benchmark {

inline constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
inline constexpr JointCommand kPoseA{{-15 * kDegreesToRadians, 45 * kDegreesToRadians,
    -60 * kDegreesToRadians, 0, 15 * kDegreesToRadians, 0}};
inline constexpr JointCommand kPoseB{{-30 * kDegreesToRadians, 60 * kDegreesToRadians,
    -90 * kDegreesToRadians, 20 * kDegreesToRadians, 20 * kDegreesToRadians, -20 * kDegreesToRadians}};

inline void appendPoseParameters(std::vector<Parameter> &parameters, const std::string &name, const JointCommand &pose) {
    for (std::size_t joint = 0; joint < pose.position.size(); ++joint) {
        parameters.push_back({name + "_j" + std::to_string(joint + 1), "rad", pose.position[joint]});
    }
}

inline std::array<bool, 6> movingJoints(const JointCommand &before, const JointCommand &after) {
    std::array<bool, 6> active{};
    for (std::size_t joint = 0; joint < active.size(); ++joint) active[joint] = before.position[joint] != after.position[joint];
    return active;
}

} // namespace simlab::benchmark

#endif
