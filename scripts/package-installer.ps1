$ErrorActionPreference = 'Stop'

$workspace = Split-Path -Parent $PSScriptRoot
$publishDir = Join-Path $workspace 'artifacts\publish\win-x64'
$installerOutDir = Join-Path $workspace 'artifacts\installer'
$issFile = Join-Path $workspace 'installer\ManualDoubleSidedPrinter.iss'

Write-Host 'Publishing win-x64 release payload...'
dotnet publish "$workspace\ManualDoubleSidedPrinter.csproj" -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o "$publishDir"

if (-not (Test-Path $publishDir)) {
    throw "Publish output directory missing: $publishDir"
}

$possibleIscc = @(
    "$Env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "$Env:ProgramFiles(x86)\Inno Setup 6\ISCC.exe",
    "$Env:ProgramFiles\Inno Setup 6\ISCC.exe"
)

$iscc = $possibleIscc | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    throw 'Inno Setup 6 is not installed. Please install Inno Setup 6 and retry.'
}

if (-not (Test-Path $issFile)) {
    throw "Installer script missing: $issFile"
}

New-Item -ItemType Directory -Path $installerOutDir -Force | Out-Null

Write-Host 'Building installer...'
& $iscc "/DMyAppPublishDir=$publishDir" "/DMyAppOutputDir=$installerOutDir" "$issFile"

Write-Host "Installer created under: $installerOutDir"
