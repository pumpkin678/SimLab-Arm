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
$env:SIMLAB_BENCHMARK_TEST_OUTPUT = $directory
Push-Location -LiteralPath $RepositoryRoot
try {
    & uv run --locked --project (Join-Path $RepositoryRoot 'tools/python') python -B -m unittest discover `
        -s tests -p benchmark_evaluation_test.py
    if ($LASTEXITCODE -ne 0) { throw 'Offline benchmark evaluation contract failed.' }
} finally {
    Pop-Location
    Remove-Item Env:SIMLAB_BENCHMARK_TEST_OUTPUT
}
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
        '-Model', $Model, '-Headless', '-Test', '-Case', 'joint_sine', '-OutputDirectory', $recordedOutput)
if ($process.ExitCode -ne 0) { throw "Recording failed: $($process.StandardError)" }
$names = @(Get-ChildItem -LiteralPath $recordedOutput -Force | Sort-Object Name | ForEach-Object Name)
if (($names -join ',') -ne 'joint_sine.csv,result.json,run.log') { throw 'Recording must produce only CSV, result and log.' }
$result = Get-Content -Raw -LiteralPath (Join-Path $recordedOutput 'result.json') | ConvertFrom-Json
if ($result.evaluation.format_version -ne 1 -or $result.evaluation.status -ne 'completed' -or
    $result.cases[0].evaluation.phases[0].samples -ne 8000) { throw 'Missing automatic evaluation.' }
if ((Get-Content -Raw -LiteralPath (Join-Path $recordedOutput 'run.log')) -notmatch 'measured values only') {
    throw 'Missing evaluation summary in run.log.'
}

$failedOutput = Join-Path $directory 'model load failure'
$process = Invoke-BenchmarkProcess -FilePath "$PSHOME/powershell.exe" -WorkingDirectory $RepositoryRoot `
    -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script, '-Configuration', $configuration,
        '-Model', ($Model + '.missing'), '-Headless', '-Test', '-Case', 'joint_sine', '-OutputDirectory', $failedOutput)
$result = Get-Content -Raw -LiteralPath (Join-Path $failedOutput 'result.json') | ConvertFrom-Json
if ($process.ExitCode -ne 1 -or $result.status -ne 'failed' -or $result.evaluation.status -ne 'partial') {
    throw 'Evaluation changed or hid execution failure.'
}

$oldPath = $env:PATH
try {
    $uvDirectory = [IO.Path]::GetDirectoryName((Get-Command uv -CommandType Application).Source)
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_.TrimEnd('\', '/') -ne $uvDirectory.TrimEnd('\', '/') }) -join ';'
    $process = Invoke-BenchmarkProcess -FilePath "$PSHOME/powershell.exe" -WorkingDirectory $RepositoryRoot `
        -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script, '-Configuration', $configuration,
            '-Case', 'missing')
    if ($process.ExitCode -ne 2 -or $process.StandardError -notmatch 'Unknown benchmark case') {
        throw 'Ordinary preview must not require Python.'
    }
    $noPythonOutput = Join-Path $directory 'missing Python must not start'
    $process = Invoke-BenchmarkProcess -FilePath "$PSHOME/powershell.exe" -WorkingDirectory $RepositoryRoot `
        -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script, '-Configuration', $configuration,
            '-Test', '-OutputDirectory', $noPythonOutput)
    if ($process.ExitCode -ne 1 -or $process.StandardError -notmatch 'requires uv' -or (Test-Path $noPythonOutput)) {
        throw 'Test must check Python before recording or opening a GUI.'
    }
} finally {
    $env:PATH = $oldPath
}

$process = Invoke-BenchmarkProcess -FilePath $NormalApp -Arguments @('--case', 'joint_sine') -WorkingDirectory $RepositoryRoot
if ($process.ExitCode -eq 0 -or $process.StandardError -notmatch 'tools/benchmark.ps1') { throw 'Missing old-entry migration error.' }
Write-Host 'PASS script contract: parameters, output protection, three artifacts, automatic evaluation, Python preflight and failure preservation.'
