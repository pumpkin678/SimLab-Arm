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
    adapter_.emplace(*model_);
    zero_key_ = mj_name2id(model_.get(), mjOBJ_KEY, "zero");
    if (zero_key_ < 0) throw std::runtime_error("The arm model requires a zero keyframe.");
    data_.reset(mj_makeData(model_.get()));
    if (!data_) throw std::runtime_error("Cannot allocate model data for '" + path + "'.");
    reset();
}

JointFeedback Simulation::readFeedback() const {
    return adapter_->readFeedback(*data_);
}

void Simulation::step(const JointCommand &command) {
    const auto feedback = adapter_->readFeedback(*data_);
    const auto context = adapter_->readContext(*data_);
    result_ = arm_.update(feedback, command, context);
    adapter_->writeTorques(*data_, result_.torques);
    mj_step(model_.get(), data_.get());
    mj_forward(model_.get(), data_.get());
    last_torques_ = adapter_->readTorques(*data_);
}

void Simulation::reset() {
    mj_resetDataKeyframe(model_.get(), data_.get(), zero_key_);
    arm_.reset();
    result_ = {};
    last_torques_ = {};
    adapter_->writeTorques(*data_, {});
    mj_forward(model_.get(), data_.get());
}

} // namespace simlab
