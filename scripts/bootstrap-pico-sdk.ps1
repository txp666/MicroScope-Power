param(
    [string]$SdkPath = (Join-Path $PSScriptRoot "..\third_party\pico-sdk")
)

$ErrorActionPreference = "Stop"

$sdkParent = Split-Path -Parent $SdkPath
if (-not (Test-Path $sdkParent)) {
    New-Item -ItemType Directory -Force -Path $sdkParent | Out-Null
}

if (Test-Path $SdkPath) {
    if (-not (Test-Path (Join-Path $SdkPath ".git"))) {
        throw "Path exists but is not a git checkout: $SdkPath"
    }
    Write-Host "Pico SDK already exists: $SdkPath"
} else {
    git clone https://github.com/raspberrypi/pico-sdk.git $SdkPath
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed with exit code $LASTEXITCODE"
    }
}

git -C $SdkPath submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    throw "git submodule update failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path (Join-Path $SdkPath "external\pico_sdk_import.cmake"))) {
    throw "Pico SDK checkout is missing external\pico_sdk_import.cmake"
}

Write-Host "Pico SDK ready: $SdkPath"
Write-Host "For this shell: `$env:PICO_SDK_PATH = '$SdkPath'"
