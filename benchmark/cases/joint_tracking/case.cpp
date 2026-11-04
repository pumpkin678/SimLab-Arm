#include "case.hpp"
#include "sequence.hpp"

namespace simlab::benchmark::joint_tracking {

namespace {

constexpr double kPeriod = 4;

JointCommand jointTracking(double time) {
    return sequence::evaluate(time, kPeriod);
}

} // namespace

CaseSpec definition() {
    return sequence::definition("joint_tracking", kPeriod, jointTracking);
}

} // namespace simlab::benchmark::joint_tracking
