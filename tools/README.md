# 编译与运行

以下命令在仓库根目录执行。

## 编译

需要 Windows x64、Visual Studio（C++ 桌面开发、Windows SDK、CMake 工具及 English 语言包），PATH 中需有 GNU Make。

```powershell
git submodule update --init --recursive
./tools/build.ps1
```

默认编译 Release；使用 `-Configuration Debug` 编译 Debug，`-Fresh` 重新配置构建。

## 运行

```powershell
./tools/run.ps1
```

运行前需完成对应配置的编译。默认使用 Release，加载 `assets/description/scene.xml`。

| 参数 | 用途 |
| --- | --- |
| `-Configuration Debug` | 运行 Debug 版本 |
| `-Model <路径>` | 指定模型文件，相对路径按仓库根目录解析 |

## Python 工具

安装 [uv](https://docs.astral.sh/uv/) 后执行：

```powershell
./tools/python/run.ps1 sync
./tools/python/run.ps1 view
```

其他命令和参数见 [Python 工具说明](python/README.md)。
