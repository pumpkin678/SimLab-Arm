#ifndef SIMLAB_SIMULATION_ARM_TYPES_HPP
#define SIMLAB_SIMULATION_ARM_TYPES_HPP

#include <array>

namespace simlab {

using JointTorques = std::array<float, 6>; // N m, J1 through J6.

struct JointFeedback {
    std::array<float, 6> position{}; // rad, J1 through J6.
    std::array<float, 6> velocity{}; // rad/s, J1 through J6.
};

struct JointCommand {
    std::array<float, 6> position{}; // rad, J1 through J6.
};

struct ArmContext {
    float time{}; // s
    float dt{}; // s
};

struct CycleResult {
    JointTorques torques{};
    std::array<bool, 6> torque_saturated{}; // Business or model torque limit reached.
};

} // namespace simlab

#endif
