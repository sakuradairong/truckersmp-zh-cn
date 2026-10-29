param(
    [string]$Python = '.\.venv\Scripts\python.exe',
    [switch]$ReuseFonts,
    [switch]$ReuseSettings,
    [string]$TmpRoot,
    [string]$ScsSdk
)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (-not $ReuseFonts) {
        & $Python -B tools\prepare_installer_fonts.py --fetch
        if ($LASTEXITCODE -ne 0) { throw 'Font preparation failed' }
    }
    if (-not $ReuseSettings) {
        if (-not $TmpRoot -or -not $ScsSdk) { throw 'TmpRoot and ScsSdk are required to build Settings; use ReuseSettings only after a validated build' }
        & (Join-Path $PSScriptRoot 'build-settings-zh.ps1') -Python $Python -TmpRoot $TmpRoot -ScsSdk $ScsSdk
        if ($LASTEXITCODE -ne 0) { throw 'Settings build failed' }
    }
    & $Python -B tools\prepare_installer_settings.py
    if ($LASTEXITCODE -ne 0) { throw 'Settings packaging failed' }
    & $Python -B tools\package_installer.py
    if ($LASTEXITCODE -ne 0) { throw 'Installer packaging failed' }
} finally {
    Pop-Location
}
