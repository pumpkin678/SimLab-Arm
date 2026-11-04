param(
    [Parameter(Mandatory = $true)][string]$NativeTest,
    [Parameter(Mandatory = $true)][string]$NormalApp,
    [Parameter(Mandatory = $true)][ValidateSet('Release', 'Debug')][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$Model,
    [Parameter(Mandatory = $true)][string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
& $NativeTest $Model $OutputRoot
if ($LASTEXITCODE -ne 0) { throw 'Native benchmark contract failed.' }

. (Join-Path $RepositoryRoot 'tools/benchmark/process.ps1')
$directory = Join-Path $OutputRoot ([Guid]::NewGuid().ToString('N'))
$existingOutput = Join-Path $directory 'existing output'
[IO.Directory]::CreateDirectory($existingOutput) | Out-Null
$script = Join-Path $RepositoryRoot 'tools/benchmark.ps1'
$invalidOutput = Join-Path $directory 'must not be created'
foreach ($arguments in @(
    @('-OutputDirectory', $invalidOutput),
    @('-Headless', '-OutputDirectory', $existingOutput)
)) {
    $process = Invoke-BenchmarkProcess -FilePath "$PSHOME/powershell.exe" -WorkingDirectory $RepositoryRoot `
        -Arguments (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script, '-Configuration', $configuration) + $arguments)
    if ($process.ExitCode -ne 2) { throw "Script parameter error must return 2: $($process.StandardError)" }
}
if (Test-Path -LiteralPath $invalidOutput) { throw 'An invalid invocation created an experiment directory.' }
if (@(Get-ChildItem -LiteralPath $existingOutput -Force).Count) { throw 'An existing directory was modified.' }

$unknownOutput = Join-Path $directory 'unknown case'
$process = Invoke-BenchmarkProcess -FilePath "$PSHOME/powershell.exe" -WorkingDirectory $RepositoryRoot `
    -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script, '-Configuration', $configuration,
        '-Headless', '-Case', 'missing', '-OutputDirectory', $unknownOutput)
if ($process.ExitCode -ne 2 -or $process.StandardError -notmatch 'Unknown benchmark case') { throw 'Missing unknown-case error.' }
$files = @(Get-ChildItem -LiteralPath $unknownOutput -Force)
if ($files.Count -ne 1 -or $files[0].Name -ne 'run.log') { throw 'A parameter failure should retain only its log.' }

$recordedOutput = Join-Path $directory 'recorded output'
$process = Invoke-BenchmarkProcess -FilePath "$PSHOME/powershell.exe" -WorkingDirectory $RepositoryRoot `
    -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script, '-Configuration', $configuration,
        '-Model', $Model, '-Headless', '-Case', 'joint_sine', '-OutputDirectory', $recordedOutput)
if ($process.ExitCode -ne 0) { throw "Recording failed: $($process.StandardError)" }
$names = @(Get-ChildItem -LiteralPath $recordedOutput -Force | Sort-Object Name | ForEach-Object Name)
if (($names -join ',') -ne 'joint_sine.csv,result.json,run.log') { throw 'Recording must produce only CSV, result and log.' }

$process = Invoke-BenchmarkProcess -FilePath $NormalApp -Arguments @('--case', 'joint_sine') -WorkingDirectory $RepositoryRoot
if ($process.ExitCode -eq 0 -or $process.StandardError -notmatch 'tools/benchmark.ps1') { throw 'Missing old-entry migration error.' }
Write-Host 'PASS script contract: parameter exit codes, output protection, three recording artifacts and old-entry migration.'
