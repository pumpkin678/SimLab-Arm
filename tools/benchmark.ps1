[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [ValidateNotNullOrEmpty()]
    [string]$Model = 'assets/description/scene.xml',
    [ValidateNotNullOrEmpty()]
    [string]$Case = 'all',
    [ValidateNotNullOrEmpty()]
    [string]$OutputDirectory,
    [switch]$Record,
    [switch]$Headless
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'benchmark/process.ps1')

function Invoke-BenchmarkRun {
    $repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $executable = Join-Path $repoRoot "build/bin/$Configuration/simlab_benchmark.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "simlab_benchmark is not built. Run tools/build.ps1 -Configuration $Configuration first."
    }
    $modelPath = if ([IO.Path]::IsPathRooted($Model)) {
        [IO.Path]::GetFullPath($Model)
    } else {
        [IO.Path]::GetFullPath((Join-Path $repoRoot $Model))
    }
    $recording = $Record -or $Headless
    if ($OutputDirectory -and -not $recording) { throw [ArgumentException]::new('-OutputDirectory requires -Record or -Headless.') }
    $arguments = @($modelPath, '--case', $Case)
    if (-not $recording) {
        Write-Host 'Benchmark preview: no experiment files will be saved.'
        $preview = Invoke-BenchmarkProcess -FilePath $executable -Arguments $arguments -WorkingDirectory $repoRoot
        if ($preview.StandardOutput) { Write-Host $preview.StandardOutput -NoNewline }
        if ($preview.StandardError) { Write-Host $preview.StandardError -NoNewline }
        if ($preview.ExitCode -eq 2) { throw [ArgumentException]::new($preview.StandardError.Trim()) }
        if ($preview.ExitCode -ne 0) { throw "Benchmark preview failed (exit $($preview.ExitCode))." }
        return
    }
    if (-not $OutputDirectory) {
        $runName = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
        $OutputDirectory = Join-Path $repoRoot "temp/benchmark/$runName"
    } elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory = Join-Path $repoRoot $OutputDirectory
    }
    $output = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $output) { throw [ArgumentException]::new("Refusing to reuse existing experiment directory: $output") }
    $arguments += @('--output', $output)
    if ($Headless) { $arguments += '--headless' }
    [IO.Directory]::CreateDirectory($output) | Out-Null
    $encoding = New-Object System.Text.UTF8Encoding($false)
    $log = Join-Path $output 'run.log'
    try {
        [IO.File]::WriteAllText($log, "Benchmark output: $output" + [Environment]::NewLine, $encoding)
        Write-Host "Benchmark output: $output"
        $process = Invoke-BenchmarkProcess -FilePath $executable -Arguments $arguments -WorkingDirectory $repoRoot -LogPath $log
        [IO.File]::AppendAllText($log, "Process exit code: $($process.ExitCode)" + [Environment]::NewLine, $encoding)
        if ($process.StandardOutput) { Write-Host $process.StandardOutput -NoNewline }
        if ($process.StandardError) { Write-Host $process.StandardError -NoNewline }
        if ($process.ExitCode -eq 2) { throw [ArgumentException]::new($process.StandardError.Trim()) }
        $resultPath = Join-Path $output 'result.json'
        if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) { throw "Benchmark did not finalize result.json (exit $($process.ExitCode))." }
        $result = Get-Content -Raw -Encoding UTF8 -LiteralPath $resultPath | ConvertFrom-Json
        if ($result.runtime.configuration -ne $Configuration) { throw 'Executable configuration does not match the selected build.' }
        if ($result.status -notin @('completed', 'cancelled', 'failed')) { throw 'Benchmark returned a nonterminal execution status.' }
        if ($process.ExitCode -ne 0 -or $result.status -eq 'failed') { throw "Benchmark failed; preserve this run and inspect $log." }
    } catch {
        try { [IO.File]::AppendAllText($log, $_.Exception.Message + [Environment]::NewLine, $encoding) } catch { }
        throw
    }
    Write-Host "Benchmark $($result.status). Results: $output"
}

try {
    Invoke-BenchmarkRun
    exit 0
} catch [ArgumentException] {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 2
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
