#include "arm.hpp"

namespace simlab {

CycleResult Arm::update(const JointFeedback &, const JointCommand &, const ArmContext &) {
    // Implement arm behavior here. All inputs and outputs use SI units.
    return {};
}

void Arm::reset() {
    // Reset arm state here.
}

} // namespace simlab
