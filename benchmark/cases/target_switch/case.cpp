#include "case.hpp"
#include "benchmark/cases/teaching.hpp"

namespace simlab::benchmark::target_switch {

namespace {

constexpr JointCommand kPoseC{{kPoseA.position[0], kPoseA.position[1], kPoseA.position[2],
    60 * kDegreesToRadians, kPoseA.position[4], 120 * kDegreesToRadians}};
constexpr JointCommand kPoseD{{kPoseA.position[0], kPoseA.position[1], kPoseA.position[2],
    -60 * kDegreesToRadians, kPoseA.position[4], -120 * kDegreesToRadians}};

struct Stage {
    const char *id;
    double start;
    JointCommand target;
};

constexpr std::array kStages{
    Stage{"initialize", 0, kPoseA}, Stage{"multi_axis", 4, kPoseB},
    Stage{"return_to_a", 10, kPoseA},
    Stage{"brief_target", 14, kPoseC}, Stage{"withdraw", 14.2, kPoseA},
    Stage{"resume_motion", 20, kPoseC}, Stage{"reverse", 20.6, kPoseD},
    Stage{"coupled_forward", 26, kPoseC}, Stage{"coupled_reverse", 32, kPoseD},
    Stage{"recover", 38, kPoseA}};
constexpr double kDuration = 44;

JointCommand targetSwitch(double time) {
    for (std::size_t index = kStages.size(); index-- > 0;) {
        if (time >= kStages[index].start) return kStages[index].target;
    }
    return kStages.front().target;
}

} // namespace

CaseSpec definition() {
    CaseSpec spec{"target_switch", kDuration, {}, targetSwitch};
    appendPoseParameters(spec.parameters, "pose_a", kPoseA);
    appendPoseParameters(spec.parameters, "pose_b", kPoseB);
    appendPoseParameters(spec.parameters, "pose_c", kPoseC);
    appendPoseParameters(spec.parameters, "pose_d", kPoseD);
    for (std::size_t index = 0; index < kStages.size(); ++index) {
        const auto &stage = kStages[index];
        const double end = index + 1 < kStages.size() ? kStages[index + 1].start : kDuration;
        spec.phases.push_back({stage.id, stage.start, end,
            index == 0 ? PhaseKind::Initialization : PhaseKind::Response,
            index == 0 ? std::array<bool, 6>{} : movingJoints(kStages[index - 1].target, stage.target)});
    }
    return spec;
}

} // namespace simlab::benchmark::target_switch
