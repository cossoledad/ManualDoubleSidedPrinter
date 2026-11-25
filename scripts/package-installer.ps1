$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

$workspace = Split-Path -Parent $PSScriptRoot
$conanOutput = Join-Path $workspace 'build/conan'
$buildDir = Join-Path $conanOutput 'build'
$publishDir = Join-Path $workspace 'artifacts/publish/win-x64'
$installerDir = Join-Path $workspace 'artifacts/installer'
$installerScript = Join-Path $workspace 'installer/ManualDoubleSidedPrinter.iss'

Push-Location $workspace
try {
    conan build . -of $conanOutput -s:h build_type=Release -s:h arch=x86_64 `
        -s:h compiler.cppstd=20 -s:h compiler.runtime=static
    ctest --test-dir $buildDir -C Release --output-on-failure
    cmake --install $buildDir --config Release --prefix $publishDir
} finally {
    Pop-Location
}

$exe = Get-Item (Join-Path $publishDir 'ManualDoubleSidedPrinter.exe')
$version = $exe.VersionInfo.ProductVersion
$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $iscc = @(
        "$env:LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe",
        "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe",
        "$env:ProgramFiles/Inno Setup 6/ISCC.exe"
    ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $iscc) {
    throw 'ISCC.exe was not found. Install Inno Setup 6 to build the installer.'
}

New-Item -ItemType Directory -Path $installerDir -Force | Out-Null
& $iscc "/DMyAppVersion=$version" "/DMyAppPublishDir=$publishDir" "/DMyAppOutputDir=$installerDir" $installerScript
Compress-Archive -LiteralPath $exe.FullName `
    -DestinationPath (Join-Path $installerDir "ManualDoubleSidedPrinter-win-x64-v$version.zip") -Force
