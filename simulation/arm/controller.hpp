#ifndef SIMLAB_SIMULATION_ARM_CONTROLLER_HPP
#define SIMLAB_SIMULATION_ARM_CONTROLLER_HPP

#include "types.hpp"

namespace simlab {

class Controller {
public:
    JointTorques update(const JointState &state, double dt);
    void reset();
};

} // namespace simlab

#endif
