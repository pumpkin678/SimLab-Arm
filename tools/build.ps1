[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDir = Join-Path $repoRoot "build/make/$Configuration"
$outputDir = Join-Path $repoRoot "build/bin/$Configuration"
$make = @(Get-Command make -CommandType Application -ErrorAction Stop)[0].Source

# Load the x64 MSVC compiler, Windows SDK and libraries for command-line builds.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio Installer/vswhere.exe is required to locate MSVC.'
}
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) {
    throw 'Visual Studio with the x64 C++ build tools is required.'
}
Import-Module (Join-Path $vsRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

# Use the CMake tools bundled with the selected Visual Studio installation.
$cmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw 'The Visual Studio CMake tools component is required.'
}

# Keep /showIncludes diagnostics in English for CMake's header dependency parser.
$env:VSLANG = '1033'
$configureArguments = @(
    '-S', $repoRoot
    '-B', $buildDir
    '-G', 'Unix Makefiles'
    "-DCMAKE_MAKE_PROGRAM=$make"
    '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl'
    "-DCMAKE_BUILD_TYPE=$Configuration"
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$outputDir"
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
)
if ($Fresh) { $configureArguments = @('--fresh') + $configureArguments }
& $cmake @configureArguments
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)." }
& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
