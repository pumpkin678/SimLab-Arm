function Get-ConfiguredToolPath {
    param($Config, [string]$Name, [string]$RepositoryRoot, [switch]$Directory)

    if ($null -eq $Config.PSObject.Properties['tools'] -or $null -eq $Config.tools -or
        $null -eq $Config.tools.PSObject.Properties[$Name] -or
        [string]::IsNullOrWhiteSpace([string]$Config.tools.$Name)) {
        throw "Missing tools.$Name in tools/config.local.json."
    }
    $configured = [string]$Config.tools.$Name
    $path = if ([IO.Path]::IsPathRooted($configured)) {
        [IO.Path]::GetFullPath($configured)
    } else {
        [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $configured))
    }
    $pathType = if ($Directory) { 'Container' } else { 'Leaf' }
    if (-not (Test-Path -LiteralPath $path -PathType $pathType)) { throw "Configured tools.$Name not found: $path" }
    return $path
}

function Get-ToolContext {
    param(
        [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
        [ValidateSet('Build', 'Test', 'Run')][string]$Operation = 'Build'
    )

    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    $configPath = Join-Path $repositoryRoot 'tools/config.local.json'
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        throw 'Missing tools/config.local.json. Copy tools/config.example.json and configure the local tool paths.'
    }
    try {
        $config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
    } catch {
        throw "Invalid JSON in tools/config.local.json: $($_.Exception.Message)"
    }
    if ($null -eq $config -or $null -eq $config.PSObject.Properties['toolchain'] -or
        [string]$config.toolchain -notin @('msvc', 'mingw')) {
        throw 'toolchain in tools/config.local.json must be msvc or mingw.'
    }
    $toolchain = ([string]$config.toolchain).ToLowerInvariant()
    $cmake = $null
    $ctest = $null
    $buildTool = $null
    $runtimeDirectory = $null
    $cCompiler = $null
    $cxxCompiler = $null
    if ($toolchain -eq 'mingw') {
        $mingwRoot = Get-ConfiguredToolPath $config 'mingwRoot' $repositoryRoot -Directory
        $runtimeDirectory = Join-Path $mingwRoot 'bin'
        if (-not (Test-Path -LiteralPath $runtimeDirectory -PathType Container)) { throw "Missing MinGW bin directory: $runtimeDirectory" }
        if ($Operation -eq 'Build') {
            $cCompiler = Join-Path $runtimeDirectory 'gcc.exe'
            $cxxCompiler = Join-Path $runtimeDirectory 'g++.exe'
            foreach ($compiler in @($cCompiler, $cxxCompiler)) {
                if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw "Missing MinGW compiler: $compiler" }
            }
        }
    }
    if ($Operation -in @('Build', 'Test')) {
        $cmake = Get-ConfiguredToolPath $config 'cmake' $repositoryRoot
        if ($Operation -eq 'Test') {
            $ctest = Join-Path ([IO.Path]::GetDirectoryName($cmake)) 'ctest.exe'
            if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw "CTest not found beside configured CMake: $ctest" }
        } else {
            $buildTool = Get-ConfiguredToolPath $config 'buildTool' $repositoryRoot
        }
    }
    return [pscustomobject]@{
        Toolchain = $toolchain
        Configuration = $Configuration
        Operation = $Operation
        RepositoryRoot = $repositoryRoot
        BuildDirectory = Join-Path $repositoryRoot "build/ninja/$toolchain/$Configuration"
        BinaryDirectory = Join-Path $repositoryRoot "build/bin/$toolchain/$Configuration"
        CMake = $cmake
        CTest = $ctest
        BuildTool = $buildTool
        RuntimeDirectory = $runtimeDirectory
        CCompiler = $cCompiler
        CxxCompiler = $cxxCompiler
    }
}

function Initialize-ToolEnvironment {
    param([Parameter(Mandatory = $true)]$Context)

    if ($Context.Operation -eq 'Build' -and $Context.Toolchain -eq 'msvc') {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Visual Studio Installer/vswhere.exe is required to locate MSVC.' }
        $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Visual Studio with the x64 C++ build tools is required.' }
        Import-Module (Join-Path $vsRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
        Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
        $Context.CCompiler = @(Get-Command cl -CommandType Application -ErrorAction Stop)[0].Source
        $Context.CxxCompiler = $Context.CCompiler
        $env:VSLANG = '1033'
    }
    $directories = @($Context.RuntimeDirectory)
    foreach ($tool in @($Context.CMake, $Context.BuildTool)) {
        if ($tool) { $directories += [IO.Path]::GetDirectoryName($tool) }
    }
    $env:PATH = (@($directories | Where-Object { $_ } | Select-Object -Unique) + @($env:PATH)) -join ';'
}
