[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectDefinition = Get-Content -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match($projectDefinition, 'project\(Ausyn VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) { throw 'Could not read the release version from CMakeLists.txt.' }
$releaseVersion = $versionMatch.Groups[1].Value
$executable = Join-Path $repositoryRoot 'build-nmake\ausyn.exe'

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
if (-not (Test-Path -LiteralPath $executable)) {
    throw "The Ausyn executable was not produced at $executable."
}

$qtRoot = $env:QTDIR
if ([string]::IsNullOrWhiteSpace($qtRoot)) {
    $qtRoot = Get-ChildItem -LiteralPath 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } |
        Where-Object { Test-Path (Join-Path $_ 'bin\windeployqt.exe') } |
        Sort-Object -Descending |
        Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($qtRoot)) {
    throw 'Could not find windeployqt. Set QTDIR to the Qt MSVC kit folder.'
}

$deployTool = Join-Path $qtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployTool)) {
    throw "Qt deployment tool was not found at $deployTool."
}
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer locator (vswhere.exe) was not found.'
}
$visualStudioPath = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($visualStudioPath)) {
    throw 'Visual Studio C++ Build Tools are required to deploy the compiler runtime.'
}
$developerCommand = Join-Path $visualStudioPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $developerCommand)) {
    throw "Visual Studio developer environment was not found at $developerCommand."
}

$buildRoot = (Resolve-Path (Join-Path $repositoryRoot 'build-nmake')).Path
$stageDirectory = Join-Path $buildRoot 'package-stage'
$normalizedStage = [System.IO.Path]::GetFullPath($stageDirectory)
if (-not $normalizedStage.StartsWith($buildRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to stage release files outside build-nmake.'
}
if (Test-Path -LiteralPath $normalizedStage) {
    Remove-Item -LiteralPath $normalizedStage -Recurse -Force
}
New-Item -ItemType Directory -Path $normalizedStage | Out-Null

Write-Host "Deploying Qt and compiler runtime dependencies from $qtRoot"
 $deploymentCommand = 'call "{0}" -arch=x64 -host_arch=x64 && "{1}" --release --compiler-runtime --no-translations --dir "{2}" "{3}"' -f `
    $developerCommand, $deployTool, $normalizedStage, $executable
& cmd.exe /d /s /c $deploymentCommand
if ($LASTEXITCODE -ne 0) {
    throw "Qt runtime deployment failed with exit code $LASTEXITCODE."
}
$qtConcurrentRuntime = Join-Path $qtRoot 'bin\Qt6Concurrent.dll'
if (-not (Test-Path -LiteralPath $qtConcurrentRuntime)) {
    throw "Qt Concurrent runtime was not found at $qtConcurrentRuntime."
}
Copy-Item -LiteralPath $qtConcurrentRuntime -Destination $normalizedStage -Force
$redistributableRoot = Join-Path $visualStudioPath 'VC\Redist\MSVC'
$crtDirectory = Get-ChildItem -LiteralPath $redistributableRoot -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object {
        $candidate = Join-Path $_.FullName 'x64\Microsoft.VC143.CRT'
        if (Test-Path -LiteralPath $candidate) { $candidate }
    } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($crtDirectory)) {
    throw 'The x64 Visual C++ runtime DLLs were not found in the Visual Studio installation.'
}
Copy-Item -Path (Join-Path $crtDirectory '*.dll') -Destination $normalizedStage -Force
$vcInstaller = Join-Path $normalizedStage 'vc_redist.x64.exe'
if (Test-Path -LiteralPath $vcInstaller) {
    Remove-Item -LiteralPath $vcInstaller -Force
}
Copy-Item -LiteralPath $executable -Destination (Join-Path $normalizedStage 'ausyn.exe') -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'docs') -Destination $normalizedStage -Recurse -Force

$packageReadme = @'
# Ausyn for Windows

Ausyn monitors Windows-reported system health and provides written, evidence-based explanations and user-led recommendations.

## Start

- Run `ausyn.exe` from this folder.
- Ausyn stores settings and history under your Windows account's local application-data folder, not beside this executable.
- Open the `docs` folder for installation, privacy, security, and troubleshooting information.

Live monitoring and local assistant replies work offline. Cloud AI is optional, off by default, and requires request review and approval. Ausyn does not run a custom antivirus scan, close apps, remove files, or install updates. On Battery & power, you can check Windows sleep blockers and optionally switch among Windows-available power plans with confirmation, verification, and session-only undo. History & reports can create and restore verified local database backups. The Gaming readiness page offers built-in profiles and a pre-launch check for profiled games.
'@
Set-Content -LiteralPath (Join-Path $normalizedStage 'README.md') -Value $packageReadme -Encoding utf8

$quickStart = @'
AUSYN FOR WINDOWS

1. Extract this folder or ZIP to a location you can write to.
2. Double-click ausyn.exe.

Ausyn stores its local settings and telemetry under your Windows user profile.
The source project and development tools are not required to run this package.
See README.md and the docs folder for installation, privacy, and troubleshooting details.
'@
Set-Content -LiteralPath (Join-Path $normalizedStage 'QUICK_START.txt') -Value $quickStart -Encoding utf8

$requiredFiles = @(
    'ausyn.exe',
    'Qt6Core.dll',
    'Qt6Concurrent.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'Qt6Sql.dll',
    'Qt6Network.dll',
    'platforms\qwindows.dll',
    'sqldrivers\qsqlite.dll',
    'docs\installation.md',
    'docs\security.md',
    'vcruntime140.dll',
    'msvcp140.dll'
)
foreach ($relativePath in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $normalizedStage $relativePath))) {
        throw "The release package is missing required runtime file: $relativePath"
    }
}

$distributionDirectory = Join-Path $repositoryRoot 'dist'
New-Item -ItemType Directory -Path $distributionDirectory -Force | Out-Null
$packagePath = Join-Path $distributionDirectory ('Ausyn-{0}-Windows-x64.zip' -f $releaseVersion)
Compress-Archive -Path (Join-Path $normalizedStage '*') -DestinationPath $packagePath -Force
Write-Host "Portable release package created: $packagePath"

$innoCompiler = $env:INNO_SETUP_COMPILER
if ([string]::IsNullOrWhiteSpace($innoCompiler)) {
    $innoCompiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
}
if ([string]::IsNullOrWhiteSpace($innoCompiler)) {
    $knownCompiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
    if (Test-Path -LiteralPath $knownCompiler) { $innoCompiler = $knownCompiler }
}
if ([string]::IsNullOrWhiteSpace($innoCompiler) -or -not (Test-Path -LiteralPath $innoCompiler)) {
    Write-Host 'Inno Setup was not found; installer source is ready, portable ZIP is available.'
} else {
    $installerDefinition = Join-Path $PSScriptRoot 'installer.iss'
    & $innoCompiler (('/DAppVersion={0}' -f $releaseVersion)) $installerDefinition
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup failed with exit code $LASTEXITCODE. The portable ZIP is still available."
    }
    Write-Host "Windows installer created in $distributionDirectory"
}
