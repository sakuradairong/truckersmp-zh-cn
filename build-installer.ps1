param(
    [string]$Python = '.\.venv\Scripts\python.exe',
    [switch]$ReuseFonts
)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (-not $ReuseFonts) {
        & $Python -B tools\prepare_installer_fonts.py --fetch
        if ($LASTEXITCODE -ne 0) { throw 'Font preparation failed' }
    }
    & $Python -B tools\package_installer.py
    if ($LASTEXITCODE -ne 0) { throw 'Installer packaging failed' }
} finally {
    Pop-Location
}
