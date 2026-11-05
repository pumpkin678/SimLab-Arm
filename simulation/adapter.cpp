#include "adapter.hpp"

#include <stdexcept>
#include <cmath>
#include <limits>
#include <string>

namespace simlab {

ArmAdapter::ArmAdapter(const mjModel &model)
    : timestep_(static_cast<float>(model.opt.timestep)) {
    if (!std::isfinite(timestep_) || timestep_ <= 0) {
        throw std::runtime_error("The arm model requires a positive finite control timestep.");
    }
    if (model.nq != 6 || model.nv != 6 || model.nu != 6) {
        throw std::runtime_error("The arm model requires six joint positions, velocities and actuators.");
    }
    const int speed_limits = mj_name2id(&model, mjOBJ_NUMERIC, "joint_speed_limits_radps");
    if (speed_limits >= 0) {
        if (model.numeric_size[speed_limits] != 6) {
            throw std::runtime_error("joint_speed_limits_radps requires six values in J1-J6 order.");
        }
        speed_limits_.emplace();
        for (std::size_t index = 0; index < speed_limits_->size(); ++index) {
            const double limit = model.numeric_data[model.numeric_adr[speed_limits] + index];
            if (!std::isfinite(limit) || limit <= 0) {
                throw std::runtime_error("joint_speed_limits_radps requires positive finite values.");
            }
            (*speed_limits_)[index] = limit;
        }
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
        torque_limits_[index] = {
            model.actuator_forcelimited[actuator] != 0,
            static_cast<float>(model.actuator_forcerange[2 * actuator]),
            static_cast<float>(model.actuator_forcerange[2 * actuator + 1])};
        const auto &limit = torque_limits_[index];
        if (limit.limited && (!std::isfinite(limit.lower) || !std::isfinite(limit.upper) || limit.lower > limit.upper)) {
            throw std::runtime_error("Invalid model motor torque range for J" + number);
        }
    }
}

void ArmAdapter::validate(const mjData &data) const {
    constexpr double maximum = std::numeric_limits<float>::max();
    if (!std::isfinite(data.time) || std::abs(data.time) > maximum) {
        throw std::runtime_error("Non-finite or unrepresentable simulation time.");
    }
    for (std::size_t index = 0; index < position_addresses_.size(); ++index) {
        const auto position = data.qpos[position_addresses_[index]];
        const auto velocity = data.qvel[velocity_addresses_[index]];
        if (!std::isfinite(position) || std::abs(position) > maximum ||
            !std::isfinite(velocity) || std::abs(velocity) > maximum ||
            !std::isfinite(data.qacc[velocity_addresses_[index]])) {
            throw std::runtime_error("Invalid physical state for J" + std::to_string(index + 1));
        }
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
        if (!std::isfinite(torques[index])) {
            throw std::runtime_error("Non-finite requested torque for J" + std::to_string(index + 1));
        }
        data.ctrl[actuator_ids_[index]] = static_cast<mjtNum>(torques[index]);
    }
}

JointTorques ArmAdapter::readTorques(const mjData &data) const {
    JointTorques torques;
    for (std::size_t index = 0; index < torques.size(); ++index) {
        torques[index] = static_cast<float>(data.actuator_force[actuator_ids_[index]]);
        if (!std::isfinite(torques[index])) {
            throw std::runtime_error("Non-finite applied torque for J" + std::to_string(index + 1));
        }
    }
    return torques;
}

} // namespace simlab
