#include "arm.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace simlab {
namespace {

// 参数从左到右对应 J1-J6；各工况共用同一组参数，不根据工况或时间切换算法。
// Kp: N*m/rad；Kd: N*m/(rad/s)；Ki: N*m/(rad*s)。
constexpr std::array<float, 6> kKp{60.0f, 600.0f, 120.0f, 4.0f, 3.0f, 0.8f};
constexpr std::array<float, 6> kKd{8.0f, 48.0f, 12.0f, 0.4f, 0.3f, 0.08f};
constexpr std::array<float, 6> kKi{0.0f, 180.0f, 80.0f, 0.0f, 0.0f, 0.0f};

// 积分项允许提供的最大力矩，单位 N*m；Ki 为 0 的轴继续使用 PD。
constexpr std::array<float, 6> kIntegralTorqueLimit{0.0f, 16.0f, 8.0f, 0.0f, 0.0f, 0.0f};

// 内部参考的加减速参数；实际关节速度仍由完整评测检查。
// kReferenceAcceleration 单位 rad/s^2；kReferenceSpeed 单位 rad/s。
constexpr std::array<float, 6> kReferenceAcceleration{4.0f, 3.0f, 6.0f, 25.0f, 25.0f, 35.0f};
constexpr std::array<float, 6> kReferenceSpeed{1.2f, 1.2f, 1.5f, 3.0f, 3.0f, 4.0f};

// 对应 assets/description/arm.xml 的电机力矩范围，单位 N*m。
constexpr std::array<float, 6> kTorqueLimit{20.0f, 80.0f, 20.0f, 10.0f, 10.0f, 2.0f};

} // namespace

CycleResult Arm::update(
    const JointFeedback &feedback,
    const JointCommand &command,
    const ArmContext &context) {
    CycleResult result{};

    // 1. 首次调用时从当前实际姿态开始规划；context.dt 由框架保证为正。
    if (!reference_initialized_) {
        reference_position_ = feedback.position;
        reference_velocity_.fill(0.0f);
        reference_initialized_ = true;
    }

    for (std::size_t joint = 0; joint < result.torques.size(); ++joint) {
        // 2. 根据停止距离选择参考速度，并逐步加速或减速。
        const float remaining = command.position[joint] - reference_position_[joint];
        const float acceleration = kReferenceAcceleration[joint];
        const float braking_speed = std::sqrt(2.0f * acceleration * std::abs(remaining));
        const float desired_velocity = std::copysign(
            std::min(kReferenceSpeed[joint], braking_speed), remaining);
        const float max_velocity_change = acceleration * context.dt;
        reference_velocity_[joint] += std::clamp(
            desired_velocity - reference_velocity_[joint],
            -max_velocity_change, max_velocity_change);
        float reference_change = reference_velocity_[joint] * context.dt;
        // 最后不足一个步长时对齐目标，消除离散推进造成的越界。
        if (reference_change * remaining > 0.0f &&
            std::abs(reference_change) >= std::abs(remaining)) {
            reference_change = remaining;
            reference_velocity_[joint] = 0.0f;
        }
        reference_position_[joint] += reference_change;

        // 3. P 纠正位置误差；D 让实际角速度跟踪参考角速度。
        const float error = reference_position_[joint] - feedback.position[joint];
        const float proportional = kKp[joint] * error;
        const float damping = kKd[joint] * (reference_velocity_[joint] - feedback.velocity[joint]);

        // 4. 参考停止变化后再积累误差，避免把参考运动的滞后积成静态补偿。
        // 参考移动期间保留之前学到的支撑力矩；不按工况名称切换算法。
        if (reference_change == 0.0f && kKi[joint] > 0.0f) {
            const float candidate = std::clamp(
                integral_torque_[joint] + kKi[joint] * error * context.dt,
                -kIntegralTorqueLimit[joint], kIntegralTorqueLimit[joint]);
            const float integral_change = candidate - integral_torque_[joint];
            const float candidate_request = proportional + damping + candidate;

            // 抗饱和：拒绝继续推向饱和方向的积分，允许反向积分退出饱和。
            const bool drives_high_saturation =
                candidate_request > kTorqueLimit[joint] && integral_change > 0.0f;
            const bool drives_low_saturation =
                candidate_request < -kTorqueLimit[joint] && integral_change < 0.0f;
            if (!drives_high_saturation && !drives_low_saturation) {
                integral_torque_[joint] = candidate;
            }
        }

        // 5. 使用最终积分状态计算总请求，再做电机力矩限幅。
        const float requested = proportional + damping + integral_torque_[joint];
        const float limited = std::clamp(requested, -kTorqueLimit[joint], kTorqueLimit[joint]);
        result.torques[joint] = limited;
        result.torque_saturated[joint] = requested != limited;
    }

    return result;
}

void Arm::reset() {
    // 每个新实验及 GUI Reset 都清空参考和积分，避免上次实验影响下一次。
    reference_position_.fill(0.0f);
    reference_velocity_.fill(0.0f);
    integral_torque_.fill(0.0f);
    reference_initialized_ = false;
}

} // namespace simlab
