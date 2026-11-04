#include "case.hpp"
#include "benchmark/cases/joint_tracking/sequence.hpp"

namespace simlab::benchmark::joint_tracking_stress {

namespace {

constexpr double kPeriod = 1;

JointCommand jointTrackingStress(double time) {
    return joint_tracking::sequence::evaluate(time, kPeriod);
}

} // namespace

CaseSpec definition() {
    return joint_tracking::sequence::definition("joint_tracking_stress", kPeriod, jointTrackingStress);
}

} // namespace simlab::benchmark::joint_tracking_stress
