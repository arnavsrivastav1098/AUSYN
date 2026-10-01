[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$qtRoot = $env:QTDIR
if ([string]::IsNullOrWhiteSpace($qtRoot)) {
    $qtRoot = Get-ChildItem -LiteralPath 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } |
        Where-Object { Test-Path (Join-Path $_ 'lib\cmake\Qt6\Qt6Config.cmake') } |
        Sort-Object -Descending |
        Select-Object -First 1
}

if ([string]::IsNullOrWhiteSpace($qtRoot) -or
    -not (Test-Path (Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw 'Could not find the MSVC Qt 6 kit. Set QTDIR to the kit folder, such as C:\Qt\6.11.2\msvc2022_64.'
}

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer locator (vswhere.exe) was not found.'
}

$visualStudioPath = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($visualStudioPath)) {
    throw 'The Visual Studio C++ x64 toolchain was not found. Install the Desktop development with C++ workload.'
}

$developerCommand = Join-Path $visualStudioPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $developerCommand)) {
    throw "Visual Studio developer environment was not found at $developerCommand."
}

$compilerRoot = Join-Path $visualStudioPath 'VC\Tools\MSVC'
$makeProgram = Get-ChildItem -LiteralPath $compilerRoot -Directory |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\nmake.exe' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($makeProgram)) {
    throw 'NMake was not found in the installed Visual Studio C++ toolchain.'
}

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { 'C:\Program Files\CMake\bin\cmake.exe' }
if (-not (Test-Path -LiteralPath $cmakePath)) {
    throw 'CMake was not found. Install CMake 3.24 or newer.'
}

$buildDirectory = Join-Path $repositoryRoot 'build-nmake'
$configure = '"{0}" -S "{1}" -B "{2}" -G "NMake Makefiles" -DCMAKE_MAKE_PROGRAM="{3}" -DCMAKE_PREFIX_PATH="{4}" -DCMAKE_BUILD_TYPE={5}' -f `
    $cmakePath, $repositoryRoot, $buildDirectory, $makeProgram, $qtRoot, $Configuration
$build = '"{0}" --build "{1}" --config {2}' -f $cmakePath, $buildDirectory, $Configuration
$command = 'call "{0}" -arch=x64 -host_arch=x64 && {1} && {2}' -f `
    $developerCommand, $configure, $build

Write-Host "Configuring Ausyn with Qt at $qtRoot"
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Ausyn $Configuration build failed with exit code $LASTEXITCODE."
}

Write-Host "Build complete: $(Join-Path $buildDirectory 'ausyn.exe')"
