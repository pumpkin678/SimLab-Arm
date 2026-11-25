# 控制 demo：达到 benchmark PASS 的实现方法

当前实现为“带加减速的内部参考 + 位置/速度反馈 + 条件积分 + 力矩限幅”。Release 完整 all 已得到整体 PASS，四项正式工况全部 PASS；joint_sine 是 OBSERVE 示例。模型、工况、共享接口和评分规则均未修改。

调参经过与原报告曲线见[《机械臂 PID 调参过程复盘》PDF](exports/pid-tuning-review/机械臂PID调参过程复盘.pdf)，也可下载[打印版 HTML](exports/pid-tuning-review/PID调参过程复盘_打印版.html)及同目录 figures/ 后在浏览器中打开。完整原始报告保留在本机 temp/benchmark/pass-pid-a1381029/；该目录包含 CSV、result.json、run.log、source/ 源码快照和 controller-parameters.json，未纳入 Git。

## 输入、输出和调用过程

外部仍然只提供六轴目标位置。参考速度由 Arm 内部根据当前目标生成，不需要用户额外输入，也不读取 case 名称或未来目标。

| 字段 | 含义 | 单位 |
| --- | --- | --- |
| command.position[i] | GUI 或 benchmark 当前给出的原始目标 | rad |
| feedback.position[i] | 物理步开始时的实际位置 | rad |
| feedback.velocity[i] | 物理步开始时的实际角速度 | rad/s |
| context.dt | 当前模型为 0.001 s 的控制周期 | s |
| context.time | 仿真时间；控制器没有按时间切换算法 | s |
| result.torques[i] | 已经业务限幅的请求力矩 | N*m |
| result.torque_saturated[i] | 总请求是否被电机力矩限幅截断 | bool |

数组下标 0-5 对应 J1-J6。Arm 返回力矩，框架写入 data.ctrl，再推进 MuJoCo 一步。框架使用步前反馈记录 trace，随后发布本周期结果。运动后的状态会影响下一周期反馈。

## 为什么上一版不能全部通过

1. 旧 D 项只有 -Kd * 实际速度，运动时需要额外的位置误差来抵消阻尼，造成跟踪滞后。
2. 腕部参考限速只有 0.8 rad/s，J6 从 -120° 到 +120° 仅移动参考就要约 5.24 s，无法满足 2 s 的调节时间要求。
3. J2 大幅反向时重力负载变化，原来位置刚度和阻尼不足，出现超调、末段误差，并扰动 J3。

## 第一步：生成能加速和刹车的内部参考

第一次调用时，参考位置取实际位置，参考速度取零。每个关节根据当前位置到目标的剩余距离计算刹车速度：

```text
remaining = 原始目标 - 内部参考位置
braking_speed = sqrt(2 * acceleration * abs(remaining))
desired_velocity = sign(remaining) * min(最大参考速度, braking_speed)

每周期参考速度变化限制在 ±acceleration * dt
参考位置 += 本周期参考速度 * dt
```

离目标远时可以加速，接近目标时根据停止距离减速。最后不足一个推进步长时，直接对齐目标并将参考速度归零，消除离散推进造成的越界。这是简单的在线参考生成器，不是完整的最优轨迹规划器。

参考参数只约束内部规划，实际速度还受惯性、重力和耦合影响，所以仍以报告的六轴实际速度检查为准。

对应代码：simulation/arm/arm.cpp 中的步骤 2。

## 第二步：让实际速度跟踪参考速度

控制公式变为：

```text
位置误差 = 内部参考位置 - 实际位置
速度误差 = 内部参考速度 - 实际速度

P 项 = Kp * 位置误差
D 项 = Kd * 速度误差
总请求 = P 项 + D 项 + 积分力矩
```

位置项负责纠偏，速度项让关节跟上规划的速度。参考停下时，D 项自然变为 -Kd * 实际速度，继续提供阻尼；参考运动时则不再把所有运动都当成需要抑制的运动。

该形式可参考 [MIT 机械臂控制课程](https://manipulation.mit.edu/force.html)。本实现没有使用模型重力前馈，静态负载主要由积分逐步补偿。

对应代码：simulation/arm/arm.cpp 中的步骤 3。

## 第三步：保留积分和饱和保护

J2、J3 启用积分，直接以 N*m 保存状态：

```text
候选积分力矩 = clamp(原积分力矩 + Ki * 位置误差 * dt, ±积分额度)
```

- 参考停止变化后才更新积分，运动时保留之前学到的支撑力矩。
- 积分有单独的幅值限制。
- 总请求已超出电机范围，且积分增量继续推向饱和方向时，拒绝本次积分；允许反向积分帮助退出饱和。
- 每个新实验及 GUI Reset 都清空参考位置、参考速度和积分。

对应代码：arm.cpp 的步骤 4、5 以及 reset()；状态保存在 arm.hpp。

## 第四步：按失败项调整各轴参数

本轮先用规划和速度反馈解决跟踪及腕部到位问题，再针对剩余的 J2/J3 阶跃失败增加位置刚度，并配套增加阻尼。没有修改评分阈值，也没有针对不同 case 切换参数。

以下数组均从左到右对应 J1-J6：

| 参数 | J1 | J2 | J3 | J4 | J5 | J6 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Kp，N*m/rad | 60 | 600 | 120 | 4 | 3 | 0.8 |
| Kd，N*m/(rad/s) | 8 | 48 | 12 | 0.4 | 0.3 | 0.08 |
| Ki，N*m/(rad*s) | 0 | 180 | 80 | 0 | 0 | 0 |
| 积分额度，N*m | 0 | 16 | 8 | 0 | 0 | 0 |
| 最大参考速度，rad/s | 1.2 | 1.2 | 1.5 | 3 | 3 | 4 |
| 参考加速度参数，rad/s² | 4 | 3 | 6 | 25 | 25 | 35 |
| 电机力矩上限，N*m | 20 | 80 | 20 | 10 | 10 | 2 |

参数位于 arm.cpp 文件开头。Kp/Kd/Ki 是控制增益，不能把这些数值当成角度。

## 实际结果与修改前对比

最终固定同一组参数完整运行 all，共 174,000 个周期，全部执行完成；CSV 数值均有限，请求力矩均在模型范围内。完整评价包括最后的速度端点，整体和四项正式工况均 PASS。框架契约检查 2/2 通过。

| 指标 | 修改前的 PID | 当前版本 | 要求 |
| --- | ---: | ---: | ---: |
| joint_tracking / J1 跟踪误差 RMS | 1.392° | 约 0.334° | ≤1° |
| target_switch / coupled_reverse / J6 调节时间 | 5.455 s | 1.153 s | ≤2 s |
| joint_step / j2_step_4 / J2 末 0.5 s 误差 RMS | 1.930° | 约 0.312° | ≤0.5° |

上述修改前数据对应本机 temp/benchmark/pid-all-ca37b09f/report.html；最初的纯 PD 报告位于 temp/benchmark/control-demo-9bb524da/report.html。原始报告均保留在本机，关键对比曲线已收录在上方 PDF 中。

当前报告的工况状态：

| 工况 | 判定 |
| --- | --- |
| posture_hold | PASS |
| joint_step | PASS |
| joint_tracking | PASS |
| target_switch | PASS |
| joint_sine | OBSERVE，示例项 |

## 用第一周期理解实际计算

posture_hold 从零位、零速度开始，J1 原始目标为 -15°：

```text
目标角度              -0.261799... rad
实际位置和速度         0
参考加速度参数          4 rad/s²
控制周期               0.001 s
本周期参考速度          -0.004 rad/s
本周期参考位置          -0.000004 rad
P 项                   60 * (-0.000004) = -0.00024 N*m
D 项                   8 * (-0.004 - 0) = -0.032 N*m
I 项                   0
返回力矩               -0.03224 N*m
```

CSV 首行记录约 -0.0322400033 N*m，与计算一致。下一周期会读取新的实际状态再计算。

## 如何复现

在仓库根目录执行。PATH 调整只影响当前终端，用于绕开本机重复 uv 的问题。

```powershell
$env:Path = ($env:Path -split ';' | Where-Object { $_ -notlike '*\WinGet\Packages\astral-sh.uv_*' }) -join ';'
./tools/build.ps1 -Configuration Release
./tools/benchmark.ps1 -Configuration Release -Case all -Headless -Test
```

每次默认创建新目录，打开终端打印的新目录中的 report.html。修改任何参数后都应重新编译，再用同一组参数完整评测。

也可运行 ./tools/run.ps1，在 GUI 中设定目标并按 Space 运行。CSV 的 input 列对应原始目标，feedback 对应步前实际状态，request 对应 Arm 限幅后的输出，applied 对应实际电机力矩；内部参考和 P/D/I 分项可在代码中下断点观察。
