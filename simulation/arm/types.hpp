#ifndef SIMLAB_SIMULATION_ARM_TYPES_HPP
#define SIMLAB_SIMULATION_ARM_TYPES_HPP

#include <array>

namespace simlab {

using JointTorques = std::array<double, 6>; // N m, J1 through J6.

struct JointState {
    double time{}; // s
    std::array<double, 6> position{}; // rad
    std::array<double, 6> velocity{}; // rad/s
};

} // namespace simlab

#endif
