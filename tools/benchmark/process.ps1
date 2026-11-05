function Invoke-BenchmarkProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [string]$LogPath
    )

    # Windows argv quoting also preserves embedded quotes and trailing backslashes.
    $quoted = foreach ($argument in $Arguments) {
        $escaped = [regex]::Replace($argument, '(\\*)"', '$1$1\"')
        $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
        '"' + $escaped + '"'
    }
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $FilePath
    $start.Arguments = $quoted -join ' '
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $start.StandardErrorEncoding = New-Object System.Text.UTF8Encoding($false)
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Failed to start $FilePath" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $result = [pscustomobject]@{
            ExitCode = $process.ExitCode
            StandardOutput = $stdout.GetAwaiter().GetResult()
            StandardError = $stderr.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
    if ($LogPath) {
        $encoding = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::AppendAllText($LogPath, $result.StandardOutput + $result.StandardError, $encoding)
    }
    return $result
}

function Write-BenchmarkOutput {
    param([string]$Text)
    if (-not $Text) { return }
    if ([Console]::IsOutputRedirected -or $env:NO_COLOR) {
        Write-Host $Text -NoNewline
        return
    }
    $colors = @{ PASS = 'Green'; FAIL = 'Red'; INCOMPLETE = 'Yellow'; OBSERVE = 'Gray' }
    foreach ($line in [regex]::Split($Text, '(?<=\n)')) {
        if ($line -match '^\[(PASS|FAIL|INCOMPLETE|OBSERVE)\]') {
            Write-Host $line -NoNewline -ForegroundColor $colors[$Matches[1]]
        } else { Write-Host $line -NoNewline }
    }
}
