#include "simulation/arm/controller.hpp"

namespace simlab {

JointTorques Controller::update(const JointState &, double) {
    // Implement joint torque control here. State and dt use SI units.
    return {};
}

void Controller::reset() {
    // Reset controller state here.
}

} // namespace simlab
