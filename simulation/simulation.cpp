#include "simulation.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace simlab {

void Simulation::ModelDeleter::operator()(mjModel *value) const {
    mj_deleteModel(value);
}

void Simulation::DataDeleter::operator()(mjData *value) const {
    mj_deleteData(value);
}

Simulation::Simulation(const std::string &path, const std::optional<TraceOptions> &trace) {
    std::array<char, 4096> error{};
    model_.reset(mj_loadXML(path.c_str(), nullptr, error.data(), static_cast<int>(error.size())));
    if (!model_) throw std::runtime_error("Cannot load model '" + path + "': " + error.data());
    adapter_.emplace(*model_);
    zero_key_ = mj_name2id(model_.get(), mjOBJ_KEY, "zero");
    if (zero_key_ < 0) throw std::runtime_error("The arm model requires a zero keyframe.");
    data_.reset(mj_makeData(model_.get()));
    if (!data_) throw std::runtime_error("Cannot allocate model data for '" + path + "'.");
    reset();
    if (trace) trace_.emplace(*trace);
}

Simulation::~Simulation() {
    try {
        flushTrace();
    } catch (const std::exception &error) {
        std::cerr << "Trace flush failed during cleanup: " << error.what() << '\n';
    }
}

JointFeedback Simulation::readFeedback() const {
    return adapter_->readFeedback(*data_);
}

void Simulation::step(const JointCommand &command) {
    try {
        adapter_->validate(*data_);
        const double time = data_->time;
        constexpr std::array warning_ids{mjWARN_BADQPOS, mjWARN_BADQVEL, mjWARN_BADQACC, mjWARN_BADCTRL};
        std::array<int, warning_ids.size()> warnings{};
        for (std::size_t index = 0; index < warning_ids.size(); ++index) {
            warnings[index] = data_->warning[warning_ids[index]].number;
        }
        const auto feedback = adapter_->readFeedback(*data_);
        const auto context = adapter_->readContext(*data_);
        const auto result = arm_.update(feedback, command, context);
        adapter_->writeTorques(*data_, result.torques);
        mj_step(model_.get(), data_.get());
        mj_forward(model_.get(), data_.get());
        adapter_->validate(*data_);
        const auto applied = adapter_->readTorques(*data_);
        for (std::size_t index = 0; index < warning_ids.size(); ++index) {
            if (data_->warning[warning_ids[index]].number != warnings[index]) {
                throw std::runtime_error("MuJoCo numerical failure during physics step.");
            }
        }
        if (std::abs(data_->time - time - model_->opt.timestep) > 1e-7) {
            throw std::runtime_error("Unexpected simulation time change during physics step.");
        }
        if (trace_) trace_->write(steps_, time, context, command, feedback, result, applied);
        result_ = result;
        last_torques_ = applied;
        ++steps_;
    } catch (...) {
        adapter_->writeTorques(*data_, {});
        throw;
    }
}

void Simulation::reset() {
    try {
        if (trace_) trace_->reset();
    } catch (...) {
        adapter_->writeTorques(*data_, {});
        throw;
    }
    mj_resetDataKeyframe(model_.get(), data_.get(), zero_key_);
    arm_.reset();
    result_ = {};
    last_torques_ = {};
    steps_ = 0;
    adapter_->writeTorques(*data_, {});
    mj_forward(model_.get(), data_.get());
}

void Simulation::flushTrace() {
    try {
        if (trace_) trace_->flush();
    } catch (...) {
        adapter_->writeTorques(*data_, {});
        throw;
    }
}

} // namespace simlab
