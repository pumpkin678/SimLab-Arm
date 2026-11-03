#include "simulation.hpp"

#include <array>
#include <stdexcept>

namespace simlab {

void Simulation::ModelDeleter::operator()(mjModel *value) const {
    mj_deleteModel(value);
}

void Simulation::DataDeleter::operator()(mjData *value) const {
    mj_deleteData(value);
}

Simulation::Simulation(const std::string &path) {
    std::array<char, 4096> error{};
    model_.reset(mj_loadXML(path.c_str(), nullptr, error.data(), static_cast<int>(error.size())));
    if (!model_) throw std::runtime_error("Cannot load model '" + path + "': " + error.data());
    bindJoints();
    data_.reset(mj_makeData(model_.get()));
    if (!data_) throw std::runtime_error("Cannot allocate model data for '" + path + "'.");
    reset();
}

void Simulation::bindJoints() {
    if (model_->nq != 6 || model_->nv != 6 || model_->nu != 6) {
        throw std::runtime_error("The arm model requires six joint positions, velocities and actuators.");
    }
    for (std::size_t index = 0; index < actuator_ids_.size(); ++index) {
        const auto number = std::to_string(index + 1);
        const int joint = mj_name2id(model_.get(), mjOBJ_JOINT, ("joint" + number).c_str());
        const int actuator = mj_name2id(model_.get(), mjOBJ_ACTUATOR, ("motor" + number).c_str());
        if (joint < 0 || actuator < 0) throw std::runtime_error("Missing joint" + number + " or motor" + number + '.');
        if (model_->jnt_type[joint] != mjJNT_HINGE ||
            model_->actuator_trntype[actuator] != mjTRN_JOINT ||
            model_->actuator_trnid[2 * actuator] != joint) {
            throw std::runtime_error("Invalid hinge/motor binding for J" + number + '.');
        }
        position_addresses_[index] = model_->jnt_qposadr[joint];
        velocity_addresses_[index] = model_->jnt_dofadr[joint];
        actuator_ids_[index] = actuator;
    }
    zero_key_ = mj_name2id(model_.get(), mjOBJ_KEY, "zero");
    if (zero_key_ < 0) throw std::runtime_error("The arm model requires a zero keyframe.");
}

JointState Simulation::readState() const {
    JointState state;
    state.time = data_->time;
    for (std::size_t index = 0; index < state.position.size(); ++index) {
        state.position[index] = data_->qpos[position_addresses_[index]];
        state.velocity[index] = data_->qvel[velocity_addresses_[index]];
    }
    return state;
}

void Simulation::step(const JointTorques &torques) {
    for (std::size_t index = 0; index < torques.size(); ++index) {
        data_->ctrl[actuator_ids_[index]] = torques[index];
    }
    mj_step(model_.get(), data_.get());
    mj_forward(model_.get(), data_.get());
    for (std::size_t index = 0; index < torques.size(); ++index) {
        last_torques_[index] = data_->actuator_force[actuator_ids_[index]];
    }
}

void Simulation::reset() {
    mj_resetDataKeyframe(model_.get(), data_.get(), zero_key_);
    last_torques_ = {};
    mj_forward(model_.get(), data_.get());
}

} // namespace simlab
