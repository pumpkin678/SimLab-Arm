# SimLab-Arm

面向机械臂控制学习的六轴仿真环境，基于 C++20 和 MuJoCo 3.9.0。程序提供关节状态反馈和六路力矩输入，控制器由成员实现。

## 启动

支持 Windows x64。先按 [编译与运行说明](tools/README.md) 准备环境，再在仓库根目录执行：

```powershell
git submodule update --init --recursive
./tools/build.ps1
./tools/run.ps1
```

程序默认加载 `assets/description/scene.xml`，启动时暂停。控制器骨架输出零力矩，开始运行后机械臂受重力、接触和关节限位作用。

## 窗口操作

| 操作 | 功能 |
| --- | --- |
| Space / Run、Pause | 运行或暂停 |
| R / Reset | 恢复零位并暂停 |
| F / Reset view | 重置相机 |
| 鼠标左键拖动、右键拖动、滚轮 | 旋转、平移、缩放视角 |
| Coordinate frames | 显示关节坐标系 |
| Esc | 退出 |

右侧面板显示仿真时间，以及 J1–J6 的角度、角速度和实际输出力矩。界面单位为 deg、deg/s、N·m。

## 目录与文档

| 目录 | 内容 |
| --- | --- |
| `app/` | 程序入口、运行调度和 UI |
| `core/` | 窗口、输入事件和渲染 |
| `simulation/` | MuJoCo 封装；`arm/` 存放机械臂类型和控制器 |
| `assets/` | 模型描述、网格和示意图 |
| `tools/` | 构建、运行及 Python 工具 |
| `third_party/` | 第三方依赖配置 |

- [架构说明](docs/architecture.md)：模块职责、数据流和运行时序。
- [资产说明](assets/README.md)：模型坐标、运动范围和力矩参数。
- [编译与运行](tools/README.md)：环境要求和脚本参数。
- [Python 工具](tools/python/README.md)：姿态查看、模型检查、导入和渲染。
