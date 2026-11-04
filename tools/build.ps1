[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scripts/common.ps1')
$context = Get-ToolContext -Configuration $Configuration -Operation Build
Initialize-ToolEnvironment $context
Write-Host "Toolchain: $($context.Toolchain); build directory: $($context.BuildDirectory)"
Write-Host "C++ compiler: $($context.CxxCompiler)"
Write-Host "CMake: $($context.CMake); Ninja: $($context.BuildTool)"
$configureArguments = @(
    '-S', $context.RepositoryRoot
    '-B', $context.BuildDirectory
    '-G', 'Ninja'
    "-DCMAKE_MAKE_PROGRAM=$($context.BuildTool.Replace('\', '/'))"
    "-DCMAKE_C_COMPILER=$($context.CCompiler.Replace('\', '/'))", "-DCMAKE_CXX_COMPILER=$($context.CxxCompiler.Replace('\', '/'))"
    "-DCMAKE_BUILD_TYPE=$Configuration"
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$($context.BinaryDirectory.Replace('\', '/'))"
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
)
if ($Fresh) { $configureArguments = @('--fresh') + $configureArguments }
& $context.CMake @configureArguments
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)." }
& $context.CMake --build $context.BuildDirectory --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
