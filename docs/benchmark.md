# Benchmark 与采集

Benchmark 用代码生成六轴目标位置，通过普通 `Simulation::step(command)` 执行，并按需录制逐周期数据。Test 选项在录制结束后执行离线评价，输出实测指标；教学通过阈值尚未设置。启动命令和参数见 [工具说明](../tools/README.md)。

## 工况定义与登记

[CaseSpec](../benchmark/cases/case.hpp) 描述一个工况：

| 字段 | 含义 |
| --- | --- |
| `id` | 唯一工况 ID，用于选择和 CSV 文件名；使用 ASCII 字母、数字、下划线、点或连字符，不能为 . 或 .. |
| `duration` | 执行时长，单位 s，要求为正的有限值 |
| `parameters` | 参数名称、单位和值；名称唯一，值为有限数 |
| `evaluate(double time)` | 以当前步前仿真时间求六轴 JointCommand，时间单位 s，目标单位 rad |
| `phases` | 顺序覆盖工况的阶段描述；无阶段的内部契约测试工况不执行局部评价 |

参考只有六轴位置，J1–J6 对应数组下标 0–5。接入层不生成速度、加速度或平滑轨迹；函数输出中的有限越界目标原样传给 Arm，非有限目标由 Simulation 拒绝。

每个工况放在 `benchmark/cases/<ID>/` 中。该目录的 `case.hpp` 声明定义入口，`case.cpp` 保存参数、参考函数和返回完整 CaseSpec 的 `definition()`。公共类型放在 `benchmark/cases/case.hpp`，登记与选择放在 [cases.cpp](../benchmark/cases/cases.cpp)。选择 `all` 时按登记顺序执行，单个 ID 只执行对应项。

新增工况时，在独立目录实现定义入口，将其加入登记列表，并在根 `CMakeLists.txt` 的 benchmark 核心源文件列表加入对应 `.cpp`。参考函数依据传入的仿真时间求值，不以墙钟或渲染帧数计时。预先确定切换时刻的分段参考可以直接按时间区间生成。

阶段使用唯一 ID、起止时间、评价类别及六轴主动标记，区间为左闭右开，连续覆盖工况时长。类别为 initialization、response、hold、tracking。参考函数与元数据复用工况自身的时间和参数；评价脚本不按 case ID 重新生成参考。GUI 显示最近成功周期所属阶段及进度，显示操作不求值参考。

### 工况示例

[joint_sine](../benchmark/cases/joint_sine/case.cpp) 展示如何通过代码生成关节目标。具体轨迹及参数以工况定义为准，执行时长和已声明的参数写入 `result.json`。教学任务与验收要求见 [教学任务说明](teaching.md)。

## 执行与结束

GUI 和 headless 共用 [Runner](../benchmark/runner.hpp)。每个 case 创建新的 Simulation，从模型零位、零速度和新 Arm 状态开始。Runner 按 `ceil(duration / model_timestep)` 计算周期预算，实际进度读取 `Simulation::stepCount()`。

每个周期在步前以当前 MuJoCo 时间求值一次参考，再调用一次正常物理步进。每步完整推进模型步长，不截短最后一个周期；预计周期数、实际周期数和步长写入结果。

GUI 自动开始，支持 Space 暂停／恢复、Backspace 取消，Esc 或关闭窗口退出。暂停和最小化不推进参考时间；恢复时清除墙钟积压。参考不可编辑，显示的目标是最近成功步进使用的值，显示操作不额外调用参考函数。

一个 case 完成并刷新 CSV 后，Runner 顺序启动下一项。取消或失败立即停止后续项，未启动项保留 `not_started`。一次启动对应一次实验，R 不重跑；完成、取消或失败后保留可用场景，关闭窗口后收尾。关闭已结束的窗口不会改变已确定的执行状态。

Headless 不创建窗口，按计算速度执行，结束后直接收尾退出。执行程序 `simlab_benchmark` 的退出码为：

| 退出码 | 含义 |
| --- | --- |
| `0` | 正常完成或正常取消，包括零样本取消 |
| `1` | 执行、采集或结果收尾失败 |
| `2` | 参数错误 |

`completed` 和退出码 0 只表示执行流程完成，不表示跟随性能达标。

## 录制产物与结果

成功录制在一个新的输出目录中保存以下产物。GUI 预览不创建实验文件；普通手动应用的 Trace 开关只输出指定 CSV。

| 产物 | 内容 |
| --- | --- |
| `<case_id>.csv` | 该 case 使用的 trace v1 数据，每项单独保存 |
| `result.json` | 整体及各 case 的状态、参数、阶段和成功周期数；评价后增加指标 |
| `run.log` | stdout、stderr、进程退出码、错误，以及评价摘要 |

CSV 在 case 完成、取消或失败时尝试刷新。GUI 保留终态画面期间，已成功刷新的 CSV 可读取；最终 `result.json` 在关闭窗口后写入，脚本随后完成日志收集和结果检查。

参数错误、加载失败或强制终止可能使产物不齐全，例如只有日志或没有最终结果。脚本将缺少最终结果、非零执行退出码、结果失败或收尾错误报告为错误，保留已有文件。未启动项没有 CSV；记录文件的写入或刷新失败时，已有文件可能不完整。

### result.json

整体信息包含格式版本、trace 版本、工况选择、模型路径、运行模式、状态与结束原因，以及 MuJoCo 版本和编译配置。`cases` 数组按选择顺序保存每项的结果：

| 字段 | 内容 |
| --- | --- |
| `id`、`duration_s`、`parameters` | 实际 CaseSpec 定义；每个参数包含 value 和 unit |
| `trace` | 对应的 CSV 文件名，未启动或加载失败时文件可能不存在 |
| `status`、`reason` | 执行状态及结束原因 |
| `timestep_s`、`expected_steps` | 实际模型步长和周期预算；尚未取得时为 null |
| `samples` | Simulation 维护的成功周期数，零样本为 0 |
| `last_step` | 最后一个成功步号；零样本为 null |
| `phases` | 阶段的 id、start_s、end_s、kind 和 active_joints；主动关节编号为 1–6 |
| `evaluation` | 评价入口成功后写入的逐阶段、逐轴结果；普通录制不生成此字段 |

Case 状态使用 `not_started`、`running`、`completed`、`cancelled`、`failed`。正常收尾文件的整体状态为 `completed`、`cancelled` 或 `failed`。在两项之间取消时，前一项仍为 completed，后一项仍为 not_started，整体为 cancelled。

### 离线评价

评价只读取最终结果中的阶段描述和 CSV，校验 trace v1 的完整列序、有限数值、复位段、步号、仿真时间、步长与成功周期数。正常 benchmark 每项只有复位段 0。只有加载前失败或未启动且零样本的项可以没有 CSV。

根 evaluation 保存独立的 format_version、评价状态和统计口径；每个 case 的 evaluation 保存阶段数组，按阶段 ID 对应原始 phases。阶段状态为 completed、partial 或 not_started；初始化阶段标记 excluded，不输出关节指标。无阶段工况标为 not_applicable。

关节结果包含 joint、active/held 角色以及带单位后缀的指标字段，例如 position_error_rms_rad、velocity_peak_abs_radps、request_torque_peak_abs_nm。响应阶段还包含 arrival_time_s、settling_time_s、overshoot_rad、tail_position_error_mean_rad 和相应原因。指标定义与解释集中在 [教学任务说明](teaching.md#指标与解释)。

评价 completed 仅表示数据处理完成。取消或失败时评价状态为 partial，原执行状态不变；完整阶段照常处理，部分阶段不输出完整响应结论。无样本阶段的关节数组为空。时间区间按记录时间划分，误差带持续时间按物理周期计算；时间覆盖检查容许 double 累加舍入误差，不容许缺失周期。

评价成功后原子替换 result.json，仅增加或替换评价字段，允许重新评价。CSV 损坏或评价失败时保留原始执行结果并报告错误；终端和日志摘要使用 deg、deg/s、s、N·m。Test 的执行或评价错误返回 1，参数错误返回 2，正常取消与较差的性能指标不产生非零退出码。

## 统一 trace v1

手动 GUI、benchmark GUI 和 headless 使用同一个 [TraceWriter](../simulation/trace.hpp)。每个正常完成记录的物理周期对应一行，暂停、最小化及读取 UI 不产生行。

输入、位置与速度反馈直接取本次 `Arm::update()` 使用的局部值，请求力矩取本次返回结果。行时间是步前 MuJoCo 时间；实际力矩取本步 `mj_step()` 和 `mj_forward()` 之后的引擎输出，与 `lastTorques()` 一致。分析同一行时，应把它理解为步前输入／反馈与本次输出的配对。

`input` 保存外部原始目标，内部规划位置与速度不在 trace v1 中。它可用于评价从原始输入到实际运动的整体表现，但不能单独分离规划误差与控制器跟随误差。

CSV 固定为 35 列，以下序号从 1 开始，各组六轴字段均按 J1–J6 排列，N 表示 1–6：

| 列序号 | 字段 | 含义与单位 |
| --- | --- | --- |
| 1 | `trace_version` | 固定为 1 |
| 2 | `reset_index` | 复位段号，从 0 开始 |
| 3 | `step_index` | 段内成功步号，从 0 开始 |
| 4 | `simulation_time_s` | 步前仿真时间，s |
| 5 | `cycle_timestep_s` | 实际传给 Arm 的 dt，s |
| 6–11 | `input_jN_position_rad` | 输入目标位置，rad |
| 12–17 | `feedback_jN_position_rad` | 步前反馈位置，rad |
| 18–23 | `feedback_jN_velocity_radps` | 步前反馈速度，rad/s |
| 24–29 | `request_jN_torque_nm` | Arm 请求力矩，N·m |
| 30–35 | `applied_jN_torque_nm` | 引擎实际电机力矩，N·m |

仿真时间以 double 往返精度写入，dt 和六轴业务数值按原 float 往返精度写入，使用标准小数点。复位段号和步号使用 uint64。请求力矩与实际力矩分开保存，模型的力矩限幅可能使两者不同。

`request` 是 Arm 返回的请求，可能已经经过业务限幅，因此与 `applied` 相同并不表示没有饱和。`CycleResult::torque_saturated` 不写入 trace v1；该标记的发布契约见 [架构说明](architecture.md#控制周期与接口)。

### 复位与刷新

文件打开后写表头，第一次实际步进才写数据；没有步进的文件只有表头。每 100 行、Reset 前、case 结束以及正常退出时显式刷新，异常清理尽力刷新已有记录。

普通应用 Reset 先刷新文件，再使 `reset_index` 加一，`step_index`、仿真时间和手动目标归零。连续 Reset 可以产生没有样本的空段，不写占位行。Benchmark 每项使用新文件和新 Simulation，段号从 0 开始。

目标、反馈、成员输出及引擎数值的有效性检查由仿真接入边界负责。失败周期不增加成功步数，物理状态不回滚；核对部分结果时以 `result.json` 中的 samples 与实际 CSV 共同判断。
