#ifndef SIMLAB_SIMULATION_ADAPTER_HPP
#define SIMLAB_SIMULATION_ADAPTER_HPP

#include "arm/types.hpp"

#include <mujoco/mujoco.h>

#include <array>

namespace simlab {

struct JointPositionLimit {
    bool limited{};
    float lower{}; // rad
    float upper{}; // rad
};

using JointPositionLimits = std::array<JointPositionLimit, 6>;

struct JointTorqueLimit {
    bool limited{};
    float lower{}; // N m
    float upper{}; // N m
};

using JointTorqueLimits = std::array<JointTorqueLimit, 6>;

class ArmAdapter {
public:
    explicit ArmAdapter(const mjModel &model);
    void validate(const mjData &data) const;
    JointFeedback readFeedback(const mjData &data) const;
    ArmContext readContext(const mjData &data) const;
    void writeTorques(mjData &data, const JointTorques &torques) const;
    JointTorques readTorques(const mjData &data) const;
    const JointPositionLimits &positionLimits() const noexcept { return position_limits_; }
    const JointTorqueLimits &torqueLimits() const noexcept { return torque_limits_; }

private:
    std::array<int, 6> position_addresses_{};
    std::array<int, 6> velocity_addresses_{};
    std::array<int, 6> actuator_ids_{};
    JointPositionLimits position_limits_{};
    JointTorqueLimits torque_limits_{};
    float timestep_{};
};

} // namespace simlab

#endif
