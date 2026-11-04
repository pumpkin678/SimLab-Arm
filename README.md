# SimLab-Arm

面向机械臂规划与控制学习的六轴仿真环境，基于 C++20 和 MuJoCo 3.9.0。框架提供六轴目标位置、关节位置与速度反馈，以及六路力矩输出接口；成员在 `simulation/arm/` 中实现机械臂业务。

## 启动

支持 Windows x64，使用 MSVC 或 MinGW GCC 工具链。先按 [编译与运行说明](tools/README.md) 准备工具并填写本机配置，再在仓库根目录执行：

```powershell
git submodule update --init --recursive
Copy-Item tools/config.example.json tools/config.local.json
# 编辑 config.local.json，填写本机工具路径
./tools/build.ps1
./tools/run.ps1
```

普通应用默认加载 `assets/description/scene.xml`，以零目标、暂停状态启动。`Arm` 骨架输出零力矩，开始运行后机械臂受重力、接触和关节限位作用。

程序参考通过独立 benchmark 入口运行：

```powershell
./tools/benchmark.ps1
```

benchmark 默认打开 GUI 并自动执行已登记工况，预览时不保存文件。录制、headless 和测试命令见 [编译与运行说明](tools/README.md)。

## 窗口操作

| 操作 | 普通应用 | Benchmark GUI |
| --- | --- | --- |
| Space / 运行控制按钮 | 运行或暂停 | 暂停或恢复 |
| R / Reset | 恢复零位、清零目标并暂停 | 不重跑；重新启动程序执行新一轮 |
| Backspace / Cancel | 无操作 | 取消本次执行，停止后续 case |
| F / Reset view | 重置相机 | 重置相机 |
| 鼠标左键拖动、右键拖动、滚轮 | 旋转、平移、缩放视角 | 旋转、平移、缩放视角 |
| Coordinate frames | 显示关节坐标系 | 显示关节坐标系 |
| Esc / 关闭窗口 | 退出 | 退出并完成结果收尾 |

普通应用的 Reference 面板可编辑 J1–J6 目标角度，支持滑条和 Ctrl+单击数值输入；暂停时也可编辑。有位置限位的关节按模型范围限制编辑，无位置限位的关节使用数值输入。右侧读数显示仿真时间、关节角度、角速度和实际输出力矩，界面单位为 deg、deg/s、N·m。

普通应用和 benchmark 中，饱和关节的实际力矩读数显示为橙色，悬停提示 `Torque limited`。提示显示最近成功周期的状态；暂停时保留，解除饱和后恢复正常颜色，普通应用 Reset 时清除。

Benchmark GUI 显示工况参数、执行进度和最近成功步进使用的参考目标，目标不可编辑。执行结束或取消后保留可用场景供查看，关闭窗口完成进程收尾。

## 目录与文档

| 目录 | 内容 |
| --- | --- |
| `app/` | 手动应用入口、调度及共用 UI |
| `benchmark/` | 工况定义与登记、Runner、独立入口和 GUI |
| `core/` | 窗口、输入事件和渲染 |
| `simulation/` | MuJoCo 封装；`arm/` 存放机械臂类型和控制器 |
| `assets/` | 模型描述、网格和示意图 |
| `tools/` | 构建、运行及 Python 工具 |
| `tests/` | trace 和 benchmark 契约检查 |
| `third_party/` | 第三方依赖配置 |

- [架构说明](docs/architecture.md)：模块职责、数据流和运行时序。
- [Benchmark 与采集](docs/benchmark.md)：工况定义、执行行为和记录格式。
- [教学任务说明](docs/teaching.md)：教学目标、成员职责和待确定的验收内容。
- [资产说明](assets/README.md)：模型坐标、运动范围和力矩参数。
- [编译与运行](tools/README.md)：环境要求和脚本参数。
- [Python 工具](tools/python/README.md)：姿态查看、模型检查、导入和渲染。
