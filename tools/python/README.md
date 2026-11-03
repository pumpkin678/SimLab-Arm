# Python 工具

使用 [uv](https://docs.astral.sh/uv/) 管理 Python 3.11、MuJoCo 3.9.0、NumPy 和 Pillow。依赖配置为 `pyproject.toml`、`uv.lock`，本地环境位于本目录的 `.venv/`。

以下命令在仓库根目录执行，相对路径以仓库根目录为准。运行时自动按锁文件同步环境。

| 命令 | 用途 |
| --- | --- |
| `sync` | 创建或同步 Python 环境 |
| `view` | 查看机械臂关节姿态 |
| `check` | 检查 CAD 迁移、惯量、运动学、力矩映射和接触策略 |
| `render` | 生成零位与关节正方向示意图 |
| `import` | 从 CAD 资产生成 MJCF 候选目录 |

```powershell
./tools/python/run.ps1 sync
./tools/python/run.ps1 view --q-deg 0 0 0 0 0 0
./tools/python/run.ps1 check
./tools/python/run.ps1 render
```

`view` 默认加载 `assets/description/scene.xml`，通过 `qpos` 调整姿态，不推进动力学。`--q-deg` 指定六轴初始角度（度）；按 Space 暂停原生界面后，可在右侧 Joint 面板调整角度（弧度）。R 归零，关闭窗口退出。

`check` 默认输出 `temp/model-check/checks.json`；`render` 默认输出 `assets/images/zero_pose.png`。两者均支持 `--output` 指定输出文件。

`import --output` 指定一个新的候选目录；目录中已有 `description/arm.xml` 时拒绝覆盖。示例：

```powershell
./tools/python/run.ps1 import --output temp/cad-candidate
./tools/python/run.ps1 check --scene temp/cad-candidate/description/scene.xml --output temp/cad-candidate/checks.json
```

各命令参数见 `./tools/python/run.ps1 <命令> --help`。坐标、惯量和碰撞约定见 [资产说明](../../assets/README.md)；C++ 入口见 [工具说明](../README.md)。
