#ifndef SIMLAB_SIMULATION_SIMULATION_HPP
#define SIMLAB_SIMULATION_SIMULATION_HPP

#include "adapter.hpp"
#include "arm/arm.hpp"

#include <mujoco/mujoco.h>

#include <memory>
#include <optional>
#include <string>

namespace simlab {

class Simulation {
public:
    explicit Simulation(const std::string &path);
    JointFeedback readFeedback() const;
    void step(const JointCommand &command);
    void reset();
    const CycleResult &result() const noexcept { return result_; }
    float time() const noexcept { return adapter_->readContext(*data_).time; }
    const JointPositionLimits &positionLimits() const noexcept { return adapter_->positionLimits(); }
    // Applied motor torques after the model's output limits.
    const JointTorques &lastTorques() const noexcept { return last_torques_; }
    const mjModel &model() const noexcept { return *model_; }
    const mjData &data() const noexcept { return *data_; }
    mjData &data() noexcept { return *data_; }

private:
    struct ModelDeleter {
        void operator()(mjModel *value) const;
    };

    struct DataDeleter {
        void operator()(mjData *value) const;
    };

    std::unique_ptr<mjModel, ModelDeleter> model_;
    std::unique_ptr<mjData, DataDeleter> data_;
    std::optional<ArmAdapter> adapter_;
    Arm arm_;
    int zero_key_{};
    CycleResult result_{};
    JointTorques last_torques_{};
};

} // namespace simlab

#endif
