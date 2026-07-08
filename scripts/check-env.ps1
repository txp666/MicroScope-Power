$ErrorActionPreference = "Continue"

function Show-CommandStatus {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Command
    )

    $found = Get-Command $Command -ErrorAction SilentlyContinue
    if ($found) {
        Write-Host "[OK]   ${Name}: $($found.Source)"
    } else {
        Write-Host "[MISS] ${Name}: $Command not found in PATH"
    }
}

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

function Show-ToolWithFallback {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string[]]$SearchRoots,
        [Parameter(Mandatory = $true)][string]$Filter
    )

    $found = Get-Command $Command -ErrorAction SilentlyContinue
    if ($found) {
        Write-Host "[OK]   ${Name}: $($found.Source)"
        return
    }

    $fallback = Find-FirstFile -Roots $SearchRoots -Filter $Filter
    if ($fallback) {
        Write-Host "[OK]   ${Name}: $fallback (not in current PATH)"
    } else {
        Write-Host "[MISS] ${Name}: $Command not found"
    }
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$localSdk = Join-Path $root "third_party\pico-sdk"
$envTable = [Environment]::GetEnvironmentVariables()
$picoSdkPath = $envTable["PICO_SDK_PATH"]
$wingetPackages = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"

Write-Host "Project root: $root"
Show-CommandStatus -Name "CMake" -Command "cmake"
Show-CommandStatus -Name "Git" -Command "git"
Show-ToolWithFallback -Name "Ninja" -Command "ninja" -SearchRoots @($wingetPackages) -Filter "ninja.exe"
Show-ToolWithFallback -Name "ARM GCC" -Command "arm-none-eabi-gcc" -SearchRoots @("C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi", "C:\Program Files\Arm GNU Toolchain arm-none-eabi") -Filter "arm-none-eabi-gcc.exe"

if ($picoSdkPath -and (Test-Path (Join-Path $picoSdkPath "external\pico_sdk_import.cmake"))) {
    Write-Host "[OK]   PICO_SDK_PATH: $picoSdkPath"
} elseif (Test-Path (Join-Path $localSdk "external\pico_sdk_import.cmake")) {
    Write-Host "[OK]   Local Pico SDK: $localSdk"
} else {
    Write-Host "[MISS] Pico SDK: set PICO_SDK_PATH or run .\scripts\bootstrap-pico-sdk.ps1"
}
