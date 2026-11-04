# 编译与运行

以下命令在仓库根目录执行，相对路径以仓库根目录为准，命令行中带空格的路径需加引号。

## 编译

需要 Windows x64、PowerShell、Git、CMake 3.24 或更新版本，以及 Ninja。C++ 工具链可选择：

- MSVC：安装 Visual Studio 或 Build Tools 中的 x64 C++ 工具链、Windows SDK 和 English 语言包。
- MinGW：使用完整的 MinGW-w64 x64 安装目录，包含 GCC、G++ 和配套头文件、库。

MuJoCo SDK 由 CMake 下载和配置。运行程序需要 Microsoft Visual C++ x64 运行库，使用 MinGW 时也需安装。

首次使用时创建本机配置：

```powershell
Copy-Item tools/config.example.json tools/config.local.json
```

编辑 `tools/config.local.json`，填写本机工具路径：

| 配置 | 用途 |
| --- | --- |
| `toolchain` | 选择 msvc 或 mingw，默认 msvc |
| `tools.cmake` | CMake 可执行文件；测试使用其同目录下的 ctest.exe |
| `tools.buildTool` | Ninja 可执行文件 |
| `tools.mingwRoot` | MinGW 完整安装根目录，仅选择 mingw 时使用 |

工具路径可填绝对路径或相对仓库根目录的路径。CMake 和 Ninja 可使用独立安装、Visual Studio 或 MinGW 工具包中的版本。

配置完成后编译：

```powershell
git submodule update --init --recursive
./tools/build.ps1
```

默认编译 Release；使用 `-Configuration Debug` 编译 Debug。修改工具安装路径后，使用 `./tools/build.ps1 -Fresh` 重新配置。

可执行文件位于 `build/bin/<toolchain>/<Configuration>/`，构建目录位于 `build/ninja/<toolchain>/<Configuration>/`。切换工具链时修改配置中的 `toolchain`，再编译对应版本。运行和测试使用当前配置的产物，不自动编译。

## 运行

```powershell
./tools/run.ps1
# 录制
./tools/run.ps1 -Trace temp/trace/manual.csv
# 覆盖已有 CSV
./tools/run.ps1 -Trace temp/trace/manual.csv -Force
```

运行前需完成对应配置的编译。默认使用 Release，加载 `assets/description/scene.xml`。

| 参数 | 用途 |
| --- | --- |
| `-Configuration Debug` | 运行 Debug 版本 |
| `-Model <路径>` | 指定模型文件，相对路径按仓库根目录解析 |
| `-Trace <CSV路径>` | 开启逐物理周期采集，自动创建缺失父目录，默认拒绝覆盖已有文件 |
| `-Force` | 与 Trace 一起使用，仅覆盖指定 CSV |

普通应用以零目标、暂停状态启动，在 Reference 面板编辑六轴目标。

## Benchmark

默认打开 GUI 预览并自动执行工况，使用 Release 和 `assets/description/scene.xml`。

```powershell
# GUI 预览，自动执行已登记工况
./tools/benchmark.ps1
# GUI 录制单个工况
./tools/benchmark.ps1 -Case joint_sine -Record
# 无窗口录制，指定新的输出目录
./tools/benchmark.ps1 -Case joint_sine -Headless -OutputDirectory "temp/benchmark/session 01"
```

| 参数 | 用途 |
| --- | --- |
| `-Configuration Debug` | 使用 Debug 版本，默认 Release |
| `-Model <路径>` | 指定模型，相对路径按仓库根目录解析 |
| `-Case <ID或all>` | 选择单个已登记工况，默认 all；当前只登记 joint_sine |
| `-Record` | GUI 模式下开启录制 |
| `-Headless` | 不创建窗口，自动开启录制，执行结束后退出 |
| `-OutputDirectory <路径>` | 指定新的录制目录，必须与 Record 或 Headless 一起使用 |

预览不保存文件。录制默认在 `temp/benchmark/` 下创建唯一目录，已有输出目录拒绝复用。

录制保存每个 case 的 CSV、一份 `result.json` 和一份 `run.log`；GUI 录制在关闭窗口后完成结果收尾。执行与数据含义见 [Benchmark 与采集](../docs/benchmark.md)。

## 自动测试

```powershell
./tools/scripts/test.ps1
./tools/scripts/test.ps1 -Configuration Debug
```

运行已编译版本的 `trace` 和 `benchmark_contract` 两项检查。测试产物位于 `build/ninja/<toolchain>/<Configuration>/test-output/`。

## Python 工具

安装 [uv](https://docs.astral.sh/uv/) 后执行：

```powershell
./tools/python/run.ps1 sync
./tools/python/run.ps1 view
```

其他命令和参数见 [Python 工具说明](python/README.md)。
