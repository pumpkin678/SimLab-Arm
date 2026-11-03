#ifndef SIMLAB_APP_REFERENCE_REFERENCE_HPP
#define SIMLAB_APP_REFERENCE_REFERENCE_HPP

#include "simulation/simulation.hpp"

#include <cstddef>
#include <string_view>

namespace simlab {

struct ReferenceCase {
    std::string_view name;
    double duration;
    JointCommand (*evaluate)(double time);
};

const ReferenceCase &findReferenceCase(std::string_view name);

class ReferenceRun {
public:
    explicit ReferenceRun(Simulation &simulation, const ReferenceCase *reference_case = nullptr);
    JointCommand command() const;
    bool step();
    void reset();
    JointCommand &manualTarget() noexcept { return manual_target_; }
    const ReferenceCase *caseDefinition() const noexcept { return reference_case_; }
    bool completed() const noexcept { return reference_case_ && stepCount() >= total_steps_; }
    std::uint64_t stepCount() const noexcept { return simulation_.stepCount(); }
    std::size_t totalSteps() const noexcept { return total_steps_; }

private:
    Simulation &simulation_;
    const ReferenceCase *reference_case_{};
    JointCommand manual_target_{};
    std::size_t total_steps_{};
};

} // namespace simlab

#endif
