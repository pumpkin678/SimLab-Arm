#include "adapter.hpp"

#include <stdexcept>
#include <string>

namespace simlab {

ArmAdapter::ArmAdapter(const mjModel &model)
    : timestep_(static_cast<float>(model.opt.timestep)) {
    if (model.nq != 6 || model.nv != 6 || model.nu != 6) {
        throw std::runtime_error("The arm model requires six joint positions, velocities and actuators.");
    }
    for (std::size_t index = 0; index < actuator_ids_.size(); ++index) {
        const auto number = std::to_string(index + 1);
        const int joint = mj_name2id(&model, mjOBJ_JOINT, ("joint" + number).c_str());
        const int actuator = mj_name2id(&model, mjOBJ_ACTUATOR, ("motor" + number).c_str());
        if (joint < 0 || actuator < 0) throw std::runtime_error("Missing joint" + number + " or motor" + number + '.');
        if (model.jnt_type[joint] != mjJNT_HINGE ||
            model.actuator_trntype[actuator] != mjTRN_JOINT ||
            model.actuator_trnid[2 * actuator] != joint) {
            throw std::runtime_error("Invalid hinge/motor binding for J" + number + '.');
        }
        position_addresses_[index] = model.jnt_qposadr[joint];
        velocity_addresses_[index] = model.jnt_dofadr[joint];
        actuator_ids_[index] = actuator;
        position_limits_[index] = {
            model.jnt_limited[joint] != 0,
            static_cast<float>(model.jnt_range[2 * joint]),
            static_cast<float>(model.jnt_range[2 * joint + 1])};
    }
}

JointFeedback ArmAdapter::readFeedback(const mjData &data) const {
    JointFeedback feedback;
    for (std::size_t index = 0; index < feedback.position.size(); ++index) {
        feedback.position[index] = static_cast<float>(data.qpos[position_addresses_[index]]);
        feedback.velocity[index] = static_cast<float>(data.qvel[velocity_addresses_[index]]);
    }
    return feedback;
}

ArmContext ArmAdapter::readContext(const mjData &data) const {
    return {static_cast<float>(data.time), timestep_};
}

void ArmAdapter::writeTorques(mjData &data, const JointTorques &torques) const {
    for (std::size_t index = 0; index < torques.size(); ++index) {
        data.ctrl[actuator_ids_[index]] = static_cast<mjtNum>(torques[index]);
    }
}

JointTorques ArmAdapter::readTorques(const mjData &data) const {
    JointTorques torques;
    for (std::size_t index = 0; index < torques.size(); ++index) {
        torques[index] = static_cast<float>(data.actuator_force[actuator_ids_[index]]);
    }
    return torques;
}

} // namespace simlab
