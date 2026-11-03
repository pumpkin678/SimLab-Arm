#ifndef SIMLAB_SIMULATION_ADAPTER_HPP
#define SIMLAB_SIMULATION_ADAPTER_HPP

#include "arm/types.hpp"

#include <mujoco/mujoco.h>

#include <array>

namespace simlab {

class ArmAdapter {
public:
    explicit ArmAdapter(const mjModel &model);
    JointFeedback readFeedback(const mjData &data) const;
    ArmContext readContext(const mjData &data) const;
    void writeTorques(mjData &data, const JointTorques &torques) const;
    JointTorques readTorques(const mjData &data) const;

private:
    std::array<int, 6> position_addresses_{};
    std::array<int, 6> velocity_addresses_{};
    std::array<int, 6> actuator_ids_{};
    float timestep_{};
};

} // namespace simlab

#endif
