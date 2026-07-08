$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$firmware = Join-Path $root "firmware"
$localSdk = Join-Path $root "third_party\pico-sdk"

function Find-FirstFile {
    param(
        [Parameter(Mandatory = $true)][string[]]$Roots,
        [Parameter(Mandatory = $true)][string]$Filter
    )

    foreach ($rootPath in $Roots) {
        if (Test-Path $rootPath) {
            $match = Get-ChildItem -LiteralPath $rootPath -Recurse -Filter $Filter -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($match) {
                return $match.FullName
            }
        }
    }

    return $null
}

function Add-ToolDirToPath {
    param(
        [Parameter(Mandatory = $true)][string]$ToolPath
    )

    $toolDir = Split-Path -Parent $ToolPath
    if ($env:PATH -notlike "*$toolDir*") {
        $env:PATH = "$toolDir;$env:PATH"
    }
}

if (-not $env:PICO_SDK_PATH -and (Test-Path (Join-Path $localSdk "external\pico_sdk_import.cmake"))) {
    $env:PICO_SDK_PATH = $localSdk
}

$wingetPackages = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"

if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $ninjaPath = Find-FirstFile -Roots @($wingetPackages) -Filter "ninja.exe"
    if ($ninjaPath) {
        Add-ToolDirToPath -ToolPath $ninjaPath
    }
}

if (-not (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) {
    $gccPath = Find-FirstFile -Roots @("C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi", "C:\Program Files\Arm GNU Toolchain arm-none-eabi") -Filter "arm-none-eabi-gcc.exe"
    if ($gccPath) {
        Add-ToolDirToPath -ToolPath $gccPath
    }
}

$generatorArgs = @()
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    $generatorArgs = @("-G", "Ninja")
    $build = Join-Path $root "build\firmware-ninja"
} else {
    $build = Join-Path $root "build\firmware"
}

cmake -S $firmware -B $build @generatorArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
