#ifndef SIMLAB_SIMULATION_SIMULATION_HPP
#define SIMLAB_SIMULATION_SIMULATION_HPP

#include "arm/types.hpp"

#include <mujoco/mujoco.h>

#include <array>
#include <memory>
#include <string>

namespace simlab {

class Simulation {
public:
    explicit Simulation(const std::string &path);
    JointState readState() const;
    void step(const JointTorques &torques);
    void reset();
    // Applied motor torques after the model's output limits.
    const JointTorques &lastTorques() const noexcept { return last_torques_; }
    const mjModel &model() const noexcept { return *model_; }
    const mjData &data() const noexcept { return *data_; }
    mjData &data() noexcept { return *data_; }

private:
    void bindJoints();

    struct ModelDeleter {
        void operator()(mjModel *value) const;
    };

    struct DataDeleter {
        void operator()(mjData *value) const;
    };

    std::unique_ptr<mjModel, ModelDeleter> model_;
    std::unique_ptr<mjData, DataDeleter> data_;
    std::array<int, 6> position_addresses_{};
    std::array<int, 6> velocity_addresses_{};
    std::array<int, 6> actuator_ids_{};
    int zero_key_{};
    JointTorques last_torques_{};
};

} // namespace simlab

#endif
