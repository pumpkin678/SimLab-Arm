[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$Model = 'assets/description/scene.xml',
    [ValidateNotNullOrEmpty()]
    [string]$Case,
    [switch]$Headless,
    [ValidateNotNullOrEmpty()]
    [string]$Trace,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
if ($PSBoundParameters.ContainsKey('Case') -or $PSBoundParameters.ContainsKey('Headless')) {
    throw 'Case execution moved to tools/benchmark.ps1. Use -Case and -Headless there.'
}
. (Join-Path $PSScriptRoot 'scripts/common.ps1')
$context = Get-ToolContext -Configuration $Configuration -Operation Run
Initialize-ToolEnvironment $context
$repoRoot = $context.RepositoryRoot
$executable = Join-Path $context.BinaryDirectory 'simlab_arm.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "simlab_arm is not built. Run tools/build.ps1 -Configuration $Configuration first."
}
$modelPath = if ([IO.Path]::IsPathRooted($Model)) {
    [IO.Path]::GetFullPath($Model)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $Model))
}
Push-Location -LiteralPath $repoRoot
try {
    $runArguments = @($modelPath)
    if ($PSBoundParameters.ContainsKey('Trace')) {
        $tracePath = if ([IO.Path]::IsPathRooted($Trace)) {
            [IO.Path]::GetFullPath($Trace)
        } else {
            [IO.Path]::GetFullPath((Join-Path $repoRoot $Trace))
        }
        $runArguments += @('--trace', $tracePath)
    }
    if ($Force) { $runArguments += '--force' }
    & $executable @runArguments
    if ($LASTEXITCODE -ne 0) { throw "simlab_arm failed ($LASTEXITCODE)." }
} finally {
    Pop-Location
}
