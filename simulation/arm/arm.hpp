#ifndef SIMLAB_SIMULATION_ARM_ARM_HPP
#define SIMLAB_SIMULATION_ARM_ARM_HPP

#include "types.hpp"

namespace simlab {

class Arm {
public:
    CycleResult update(
        const JointFeedback &feedback,
        const JointCommand &command,
        const ArmContext &context);
    void reset();
};

} // namespace simlab

#endif
