[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$context = Get-ToolContext -Configuration $Configuration -Operation Test
Initialize-ToolEnvironment $context
$executable = Join-Path $context.BinaryDirectory 'simlab_trace_test.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "simlab_trace_test is not built. Run tools/build.ps1 -Configuration $Configuration first."
}

& $context.CTest --test-dir $context.BuildDirectory --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Simulation tests failed ($LASTEXITCODE)." }
