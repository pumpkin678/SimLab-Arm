#ifndef SIMLAB_SIMULATION_SIMULATION_HPP
#define SIMLAB_SIMULATION_SIMULATION_HPP

#include "adapter.hpp"
#include "arm/arm.hpp"
#include "trace.hpp"

#include <mujoco/mujoco.h>

#include <memory>
#include <cstdint>
#include <optional>
#include <string>

namespace simlab {

class Simulation {
public:
    explicit Simulation(const std::string &path, const std::optional<TraceOptions> &trace = std::nullopt);
    ~Simulation();
    JointFeedback readFeedback() const;
    void step(const JointCommand &command);
    void reset();
    void flushTrace();
    std::uint64_t stepCount() const noexcept { return steps_; }
    const CycleResult &result() const noexcept { return result_; }
    float time() const noexcept { return adapter_->readContext(*data_).time; }
    const JointPositionLimits &positionLimits() const noexcept { return adapter_->positionLimits(); }
    const JointTorqueLimits &torqueLimits() const noexcept { return adapter_->torqueLimits(); }
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
    std::optional<TraceWriter> trace_;
    Arm arm_;
    int zero_key_{};
    CycleResult result_{};
    JointTorques last_torques_{};
    std::uint64_t steps_{};
};

} // namespace simlab

#endif
