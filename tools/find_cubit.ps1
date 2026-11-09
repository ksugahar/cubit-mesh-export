<#
.SYNOPSIS
    Find the Coreform Cubit 2026.8 install. Dot-source from build scripts.

.DESCRIPTION
    Centralizes Cubit install discovery so Build.ps1, cubit_plugin
    builds, and CI no longer hardcode a specific version. Replaces
    the prior pattern:
        $env:CUBIT_DIR = 'C:\Program Files\Coreform Cubit 2026.6\cmake'
    with:
        . tools/find_cubit.ps1
        $env:CUBIT_DIR = $CubitCmakeDir

    Only Coreform Cubit 2026.8 is supported. The install is
    $env:CUBIT_INSTALL_DIR when set, else 'C:\Program Files\Coreform Cubit 2026.8'.
    Its BUILD_INFO must record short=2026.8; any other install throws.

    When dot-sourced, sets script-scoped variables:
      $CubitInstallDir   = e.g. 'C:\Program Files\Coreform Cubit 2026.8'
      $CubitBinDir       = "$CubitInstallDir\bin"
      $CubitCmakeDir     = "$CubitInstallDir\cmake"
      $CubitVersion      = e.g. '2026.8'

    When run as a standalone script, prints those values one-per-line
    (KEY=VALUE) so a .bat caller can `FOR /F ... DO` parse them.

.EXAMPLE
    # PowerShell consumer (Build.ps1 style):
    . "$PSScriptRoot\..\tools\find_cubit.ps1"
    if (-not $CubitInstallDir) { Write-Error "Cubit not installed"; exit 1 }
    Write-Host "Using Cubit: $CubitVersion at $CubitBinDir"

.EXAMPLE
    # Batch consumer (build_cubit_only.bat style):
    @echo off
    for /f "tokens=1,2 delims==" %%A in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tools\find_cubit.ps1"') do (
        set "%%A=%%B"
    )
    rem now %CubitInstallDir%, %CubitBinDir%, %CubitCmakeDir%, %CubitVersion% are set

.EXAMPLE
    # Override for an off-default install:
    $env:CUBIT_INSTALL_DIR = 'D:\Tools\Cubit2026.8'
    . tools/find_cubit.ps1
#>

function Find-CubitInstall {
    $root = if ($env:CUBIT_INSTALL_DIR) { $env:CUBIT_INSTALL_DIR } `
            else { Join-Path $env:ProgramFiles 'Coreform Cubit 2026.8' }
    if (-not (Test-Path $root)) { return $null }
    $info = Join-Path $root 'BUILD_INFO'
    $short = if (Test-Path $info) {
        (Get-Content $info | Where-Object { $_ -match '^short=' } | Select-Object -First 1) -replace '^short=', ''
    }
    if ($short -ne '2026.8') {
        throw "Coreform Cubit 2026.8 is required; $root records '$short'."
    }
    return $root
}

# ===== Compute discovery results (always runs, dot-sourced or scripted) =====
$script:CubitInstallDir = Find-CubitInstall
if ($script:CubitInstallDir) {
    $script:CubitBinDir   = Join-Path $script:CubitInstallDir 'bin'
    $script:CubitCmakeDir = Join-Path $script:CubitInstallDir 'cmake'
    $script:CubitVersion  = '2026.8'
} else {
    $script:CubitBinDir   = $null
    $script:CubitCmakeDir = $null
    $script:CubitVersion  = $null
}

# ===== Script mode: emit KEY=VALUE for batch / shell consumers =====
if ($MyInvocation.InvocationName -ne '.') {
    if ($script:CubitInstallDir) {
        "CubitInstallDir=$($script:CubitInstallDir)"
        "CubitBinDir=$($script:CubitBinDir)"
        "CubitCmakeDir=$($script:CubitCmakeDir)"
        "CubitVersion=$($script:CubitVersion)"
    } else {
        Write-Error "Coreform Cubit 2026.8 is not installed. Set CUBIT_INSTALL_DIR to its directory."
        exit 1
    }
}
