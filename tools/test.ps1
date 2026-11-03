[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDir = Join-Path $repoRoot "build/make/$Configuration"
$executable = Join-Path $repoRoot "build/bin/$Configuration/simlab_trace_test.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "simlab_trace_test is not built. Run tools/build.ps1 -Configuration $Configuration first."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio Installer/vswhere.exe is required to locate CTest.'
}
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Visual Studio with the x64 C++ build tools is required.' }
$ctest = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw 'The Visual Studio CMake tools component is required.' }
& $ctest --test-dir $buildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Trace tests failed ($LASTEXITCODE)." }
