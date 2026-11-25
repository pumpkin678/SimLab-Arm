#ifndef SIMLAB_SIMULATION_ARM_ARM_HPP
#define SIMLAB_SIMULATION_ARM_ARM_HPP

#include "types.hpp"

namespace simlab {

class Arm {
public:
    CycleResult update(
        const JointFeedback &feedback,
        const JointCommand &command,
        const ArmContext &context);
    void reset();

private:
    // 内部参考从实际位置开始，再逐步靠近外部目标，单位 rad。
    std::array<float, 6> reference_position_{};
    // 内部参考角速度，单位 rad/s。
    std::array<float, 6> reference_velocity_{};
    // 积分项直接保存为力矩，单位 N*m。
    std::array<float, 6> integral_torque_{};
    bool reference_initialized_{};
};

} // namespace simlab

#endif
