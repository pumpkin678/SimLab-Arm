[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('sync', 'import', 'check', 'render', 'view', 'evaluate')]
    [string]$Command,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ForwardedArguments
)

$ErrorActionPreference = 'Stop'
if (-not (Get-Command uv -CommandType Application -ErrorAction SilentlyContinue)) {
    [Console]::Error.WriteLine('uv was not found in PATH. Install uv from https://docs.astral.sh/uv/.')
    exit 2
}

$pythonRoot = $PSScriptRoot
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $pythonRoot '../..'))
Push-Location -LiteralPath $repositoryRoot
try {
    switch ($Command) {
        'sync' {
            & uv sync --locked --project $pythonRoot @ForwardedArguments
        }
        'import' {
            & uv run --locked --project $pythonRoot python -B -m tools.python.import_cad @ForwardedArguments
        }
        'check' {
            & uv run --locked --project $pythonRoot python -B -m tools.python.check_model @ForwardedArguments
        }
        'render' {
            & uv run --locked --project $pythonRoot python -B -m tools.python.render_review @ForwardedArguments
        }
        'view' {
            & uv run --locked --project $pythonRoot python -B -m tools.python.view_model @ForwardedArguments
        }
        'evaluate' {
            & uv run --locked --project $pythonRoot python -B -m tools.python.benchmark.evaluate @ForwardedArguments
        }
    }
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
