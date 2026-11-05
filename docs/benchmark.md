# Benchmark 与采集

Benchmark 用代码生成六轴目标位置，通过普通 `Simulation::step(command)` 执行，并按需录制逐周期数据。Test 选项在录制结束后计算指标、教学判定和离线交互报告。启动命令和参数见 [工具说明](../tools/README.md)。

## 工况定义与登记

[CaseSpec](../benchmark/cases/case.hpp) 描述一个工况：

| 字段 | 含义 |
| --- | --- |
| `id` | 唯一工况 ID，用于选择和 CSV 文件名；使用 ASCII 字母、数字、下划线、点或连字符，不能为 . 或 .. |
| `duration` | 执行时长，单位 s，要求为正的有限值 |
| `parameters` | 参数名称、单位和值；名称唯一，值为有限数 |
| `evaluate(double time)` | 以当前步前仿真时间求六轴 JointCommand，时间单位 s，目标单位 rad |
| `phases` | 顺序覆盖工况的阶段描述；无阶段的内部契约测试工况不执行局部评价 |

参考为六轴原始目标位置，J1–J6 对应数组下标 0–5。有限的越界目标原样传给 Arm，非有限目标由 Simulation 拒绝。

每个工况放在 `benchmark/cases/<ID>/` 中。该目录的 `case.hpp` 声明定义入口，`case.cpp` 保存参数、参考函数和返回完整 CaseSpec 的 `definition()`。公共类型放在 `benchmark/cases/case.hpp`，登记与选择放在 [cases.cpp](../benchmark/cases/cases.cpp)。选择 `all` 时按登记顺序执行常规工况，单个 ID 只执行对应项；可选 joint_tracking_stress 必须显式选择。

新增工况时，在独立目录实现定义入口，将其加入登记列表，并在根 `CMakeLists.txt` 的 benchmark 核心源文件列表加入对应 `.cpp`。参考函数依据传入的仿真时间求值，不以墙钟或渲染帧数计时。预先确定切换时刻的分段参考可以直接按时间区间生成。

阶段使用唯一 ID、起止时间、评价类别及六轴主动标记，区间为左闭右开，连续覆盖工况时长。类别为 initialization、response、hold、tracking、recovery。recovery 用于跟随后固定目标的恢复过程，主动标记对应刚结束运动的轴。参考函数与元数据复用工况自身的时间和参数；评价脚本不按 case ID 重新生成参考。GUI 显示最近成功周期所属阶段及进度，显示操作不求值参考。

### 工况示例

[joint_sine](../benchmark/cases/joint_sine/case.cpp) 展示如何通过代码生成关节目标。具体轨迹及参数以工况定义为准，执行时长和已声明的参数写入 `result.json`。教学任务与验收要求见 [教学任务说明](teaching.md)。

## 执行与结束

GUI 和 headless 共用 [Runner](../benchmark/runner.hpp)。每个 case 创建新的 Simulation，从模型零位、零速度和新 Arm 状态开始。Runner 按 `ceil(duration / model_timestep)` 计算周期预算，实际进度读取 `Simulation::stepCount()`。

每个周期在步前以当前 MuJoCo 时间求值一次参考，再调用一次正常物理步进。每步完整推进模型步长，不截短最后一个周期；预计周期数、实际周期数和步长写入结果。

GUI 自动开始，支持 Space 暂停／恢复、Backspace 取消，Esc 或关闭窗口退出。暂停和最小化不推进参考时间；恢复时清除墙钟积压。参考不可编辑，显示的目标是最近成功步进使用的值，显示操作不额外调用参考函数。

一个 case 完成并刷新 CSV 后，Runner 顺序启动下一项。取消或运行失败立即停止后续项，未启动项保留 `not_started`。超速记为考核 FAIL，仿真继续执行当前及后续 case。一次启动对应一次实验，R 不重跑；完成、取消或失败后保留可用场景，关闭窗口后收尾。关闭已结束的窗口不会改变已确定的执行状态。

Headless 按计算速度执行，结束后直接收尾退出。退出码见 [工具说明](../tools/README.md#benchmark)。

## 录制产物与结果

成功录制在一个新的输出目录中保存以下产物。GUI 预览不创建实验文件；普通手动应用的 Trace 开关只输出指定 CSV。

| 产物 | 内容 |
| --- | --- |
| `<case_id>.csv` | 该 case 使用的 trace v1 数据，每项单独保存 |
| `result.json` | 整体及各 case 的状态、参数、阶段和成功周期数；评价后增加指标 |
| `run.log` | stdout、stderr、进程退出码、错误，以及评价摘要 |
| `report.html` | 仅 Test 或 evaluate 生成；包含判定、检查表和离线交互曲线 |

CSV 在 case 完成、取消或失败时尝试刷新。GUI 保留终态画面期间，已成功刷新的 CSV 可读取；最终 `result.json` 在关闭窗口后写入，脚本随后完成日志收集和结果检查。

执行或收尾出错时，脚本报告错误并保留已有文件。未启动项没有 CSV；加载、写入、刷新失败或强制终止可能留下不完整的产物。

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
| `torque_limits_nm` | 初始化时取得的六轴实际模型电机限幅，每项包含 limited、lower、upper；未取得或历史记录缺失时无可用限幅数据 |
| `speed_limits_radps` | 从模型 `joint_speed_limits_radps` 取得的六轴关节速度上限，按 J1–J6 排列；未取得或模型未声明时为 null |
| `final_velocity_sample` | 初始状态或最后成功步进后的速度样本，包含 double 时间 time_s 和六轴 float 速度 velocity_radps；未创建仿真时为 null |

Case 状态使用 `not_started`、`running`、`completed`、`cancelled`、`failed`。正常收尾文件的整体状态为 `completed`、`cancelled` 或 `failed`。在两项之间取消时，前一项仍为 completed，后一项仍为 not_started，整体为 cancelled。

### 离线评价

评价读取最终结果中的阶段描述、速度上限、最终速度样本和 CSV，校验 trace v1 的完整列序、有限数值、复位段、步号、仿真时间、步长与成功周期数。最终速度样本的时间必须对应最后一行 CSV 加一个模型步长；零成功周期时为时间 0。正常 benchmark 每项只有复位段 0。只有加载前失败或未启动且零样本的项可以没有 CSV。

根 evaluation 保存独立的 format_version、评价状态和统计口径；每个 case 的 evaluation 保存阶段数组和全程 speed 结果，阶段按 ID 对应原始 phases。阶段状态为 completed、partial 或 not_started；初始化阶段的局部性能评价标记 excluded，不输出局部关节指标，但仍纳入全程速度统计。无阶段工况的局部评价标为 not_applicable。

`evaluation.speed` 保存全程速度结果，每轴包含 speed_peak_abs_radps、speed_peak_abs_time_s、first_overspeed_time_s、overspeed_samples 和 overspeed_duration_s。N 行 CSV 加上最终端点，共 N+1 个速度样本。超速时长按 CSV 超速样本对应的物理周期累计。

关节结果包含 joint、active/held 角色以及带单位后缀的指标字段，例如 position_error_rms_rad、velocity_peak_abs_radps、request_torque_peak_abs_nm。response 和 recovery 包含 arrival_time_s、settling_time_s、tail_position_error_mean_rad、tail_position_error_rms_rad 和相应原因，仅 response 包含方向性 overshoot_rad。极值及响应指标还保存定位时间和响应方向。指标定义与解释集中在 [教学任务说明](teaching.md#指标与解释)。

initial_position_error_rad 和 initial_velocity_radps 保存本阶段首样本的状态；保持轴的 held_start_in_band 表示首样本是否位于位置、速度统计误差带内。false 时摘要提示该轴起始未进入误差带。

评价状态描述数据处理进度：完整处理为 completed，取消或失败后处理为 partial，原执行状态保持不变。完整阶段照常处理，部分阶段的完整响应指标留空，无样本阶段的关节数组为空。时间区间按记录时间划分，误差带持续时间按物理周期计算；时间覆盖检查容许 double 累加舍入误差，缺失周期视为记录错误。

evaluation.assessment 保存独立版本、状态、本次选择和使用的限制值；当前规则版本为 2，结果及 trace 格式版本仍为 1。case、阶段和关节同样保存 assessment，全程速度检查保存在 `evaluation.speed.assessment.checks`。checks 包含 metric、value、limit、unit、status、reason、window_s 和可选的 time_s，用于解释结论和定位曲线。速度检查的 metric 为 speed_peak_abs_radps，window_s 覆盖完整记录，phase_id 指向峰值所在阶段以便定位。单位保持 rad、rad/s、s；界面及摘要转换为 deg、deg/s、s。

判定为 PASS、FAIL、INCOMPLETE 或 OBSERVE。正式工况超速时，工况及整体均为 FAIL，优先于 INCOMPLETE；其余检查按 INCOMPLETE、FAIL、PASS 的优先级聚合，无教学检查时为 OBSERVE。通过线及短段规则见 [教学任务说明](teaching.md#教学通过线)。

评价成功后原子替换 result.json 中的评价字段，支持重复评价。CSV 损坏或数值评价失败时保留原始执行结果并报告错误。HTML 独立原子替换；报告生成失败时保留已写出的数值结果及 CSV。自动验收读取 evaluation.assessment.status，退出码见 [工具说明](../tools/README.md#benchmark)。

### 交互报告

打开 report.html 可查看整体判定、各工况结论和逐项检查。点击失败项可选择对应工况、阶段和关节，定位评价区间或极值；图表支持悬停读数、框选缩放、同步时间范围和重置。

四组曲线为原始目标与实际位置、位置误差、实际速度、请求与实际力矩。图中标示到达统计误差带、适用的逐点通过限制、末段 RMS 评价区间及调节时刻。速度图显示双向限速线和最终速度端点；报告提供全程峰值、首次超速时间及累计时长。原始指标、图表说明和运行信息可展开查看。

图表按阶段使用时间桶压缩，保留各通道极值、首尾及判定定位样本；评价使用完整 CSV。未启动和零样本阶段显示记录状态。

电机限幅线使用录制时保存的模型限幅。ArmAdapter 通过既有执行器绑定读取限幅，由 Simulation 提供只读访问，Runner 写入结果；历史记录缺少该字段时省略限幅线。报告为单文件，内联 Plotly.js basic、MIT 许可证、样式和绘图数据。

## 统一 trace v1

手动 GUI、benchmark GUI 和 headless 使用同一个 [TraceWriter](../simulation/trace.hpp)。每个正常完成记录的物理周期对应一行，暂停、最小化及读取 UI 不产生行。

输入、位置与速度反馈直接取本次 `Arm::update()` 使用的局部值，请求力矩取本次返回结果。行时间是步前 MuJoCo 时间；实际力矩取本步 `mj_step()` 和 `mj_forward()` 之后的引擎输出，与 `lastTorques()` 一致。分析同一行时，应把它理解为步前输入／反馈与本次输出的配对。

`input` 保存外部原始目标，用于评价规划与控制的整体表现。内部规划位置与速度不在 trace v1 中。

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
