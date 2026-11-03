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
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$executable = Join-Path $repoRoot "build/bin/$Configuration/simlab_arm.exe"
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
    if ($PSBoundParameters.ContainsKey('Case')) { $runArguments += @('--case', $Case) }
    if ($Headless) { $runArguments += '--headless' }
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
