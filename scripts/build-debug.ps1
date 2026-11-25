$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio Build Tools with Desktop development with C++.'
}

$visualStudio = & $vswhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    throw 'No Visual Studio installation with the MSVC x64 toolset was found.'
}

$developerShell = Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1'
& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'The Visual Studio developer environment did not provide cl.exe.'
}

if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) {
    $bundledNinja = Join-Path $visualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
    $env:Path = "$bundledNinja;$env:Path"
}
if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) {
    throw 'Ninja was not found. Install C++ CMake tools for Windows in Visual Studio, or put Ninja on PATH.'
}

Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    cmake --preset windows-debug
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    cmake --build --preset windows-debug
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
