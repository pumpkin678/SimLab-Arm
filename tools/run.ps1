[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$Model = 'assets/description/scene.xml'
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
    & $executable $modelPath
    if ($LASTEXITCODE -ne 0) { throw "simlab_arm failed ($LASTEXITCODE)." }
} finally {
    Pop-Location
}
