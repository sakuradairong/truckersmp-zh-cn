[CmdletBinding(DefaultParameterSetName='Install')]
param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [Parameter(Mandatory=$true,ParameterSetName='Restore')][string]$Restore
)
$ErrorActionPreference = 'Stop'
$gamePath = [IO.Path]::GetFullPath($GameRoot).TrimEnd('\')
function Assert-PlainPath([string]$Path) {
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked paths are not supported: $cursor" }
        }
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if (!$parent -or $parent -eq $cursor) { break }
        $cursor = $parent
    }
}
function Assert-GamePath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (!$absolute.StartsWith($gamePath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes the selected game: $absolute" }
    Assert-PlainPath $absolute
}
function Assert-File([string]$Path) {
    Assert-PlainPath $Path
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "File missing: $Path" }
}
function Assert-Stopped {
    $running = @(Get-Process -Name eurotrucks2,amtrucks,TruckCompanionPanel,TmpLocalToolboxNext,TmpLocalToolbox -ErrorAction SilentlyContinue)
    if ($running.Count) { throw ('Exit the games and companion/toolbox applications first: ' + (($running.ProcessName | Sort-Object -Unique) -join ', ')) }
}
function Read-Json([string]$Path) {
    Assert-File $Path
    if ((Get-Item -LiteralPath $Path).Length -gt 32768) { throw "Record too large: $Path" }
    return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json)
}
function Publish-File([string]$Source,[string]$Target) {
    Assert-File $Source; Assert-GamePath $Target
    $stage = $Target + '.stage-' + [Guid]::NewGuid().ToString('N')
    Assert-GamePath $stage
    try {
        Copy-Item -LiteralPath $Source -Destination $stage
        # PowerShell can coerce $null to an empty string for a .NET string argument.
        if (Test-Path -LiteralPath $Target) { [IO.File]::Replace($stage,$Target,[NullString]::Value) }
        else { [IO.File]::Move($stage,$Target) }
    } finally { if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage } }
}
function Write-Json([string]$Path,$Value) {
    Assert-GamePath $Path
    $stage = $Path + '.stage-' + [Guid]::NewGuid().ToString('N')
    Assert-GamePath $stage
    try {
        [IO.File]::WriteAllText($stage,($Value | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($stage,$Path,[NullString]::Value) }
        else { [IO.File]::Move($stage,$Path) }
    } finally { if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage } }
}
$installMutex = [Threading.Mutex]::new($false, 'Local\TruckCompanionPanel.Install')
$lockOwned = $false
try { $lockOwned = $installMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $lockOwned = $true }
if (!$lockOwned) { $installMutex.Dispose(); throw 'Another panel installation or restore is in progress.' }
try {
Assert-PlainPath $gamePath
$bin = Join-Path $gamePath 'bin\win_x64'
if (!(Test-Path -LiteralPath (Join-Path $bin 'eurotrucks2.exe') -PathType Leaf) -and !(Test-Path -LiteralPath (Join-Path $bin 'amtrucks.exe') -PathType Leaf)) {
    throw 'GameRoot must be an ETS2 or ATS installation containing bin\win_x64 and its game executable.'
}
Assert-Stopped
$plugins = Join-Path $bin 'plugins'
$target = Join-Path $plugins 'TruckCompanionOverlay.dll'
$backupRoot = Join-Path $gamePath 'companion-overlay-backups'
$activePath = Join-Path $gamePath 'companion-overlay-active.json'
foreach ($path in @($plugins,$target,$backupRoot,$activePath)) { Assert-GamePath $path }
$legacyNames = @('PanelDataBridge.dll','TmpNativeSpeedBridge.dll','TmpSettingsZh.dll','SpeedLabelsPlugin.dll')

if ($PSCmdlet.ParameterSetName -eq 'Restore') {
    $receiptPath = [IO.Path]::GetFullPath($Restore)
    Assert-GamePath $receiptPath
    $backup = [IO.Path]::GetDirectoryName($receiptPath)
    if ([IO.Path]::GetDirectoryName($backup) -ne $backupRoot -or [IO.Path]::GetFileName($receiptPath) -ne 'receipt.json') { throw 'Use the receipt.json from this game installation backup directory.' }
    $receipt = Read-Json $receiptPath
    $active = Read-Json $activePath
    if ($receipt.schema -ne 1 -or $receipt.gameRoot -ne $gamePath -or $receipt.status -ne 'installed' -or $active.receipt -ne $receiptPath) { throw 'This is not the active installation receipt. Restore the latest installation first.' }
    $disabled = @($receipt.disabledLegacy)
    foreach ($name in $disabled) {
        if ($legacyNames -notcontains $name) { throw 'Unknown filename in receipt.' }
        $dest = Join-Path $plugins $name; Assert-GamePath $dest
        Assert-File (Join-Path $backup $name)
        if (Test-Path -LiteralPath $dest) { throw "Restore would overwrite a currently installed legacy plugin: $dest. Move it aside manually first." }
    }
    $previous = Join-Path $backup 'TruckCompanionOverlay.dll'
    if ($receipt.hadBridge) { Assert-File $previous }
    if ($receipt.hadActive) { Assert-File (Join-Path $backup 'previous-active.json') }
    $archive = Join-Path $backup ('removed-' + [Guid]::NewGuid().ToString('N'))
    Assert-GamePath $archive; New-Item -ItemType Directory -Path $archive | Out-Null
    $current = Join-Path $archive 'TruckCompanionOverlay.dll'
    $hadCurrent = Test-Path -LiteralPath $target -PathType Leaf
    if ($hadCurrent) { Copy-Item -LiteralPath $target -Destination $current }
    Copy-Item -LiteralPath $activePath -Destination (Join-Path $archive 'active.json')
    $restored = @()
    try {
        Assert-Stopped
        if ($receipt.hadBridge) { Publish-File $previous $target }
        elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
        foreach ($name in $disabled) { Publish-File (Join-Path $backup $name) (Join-Path $plugins $name); $restored += $name }
        if ($receipt.hadActive) { Publish-File (Join-Path $backup 'previous-active.json') $activePath }
        else { Remove-Item -LiteralPath $activePath }
        $receipt.status = 'restored'; Write-Json $receiptPath $receipt
    } catch {
        $reason = $_
        foreach ($name in $restored) { $path = Join-Path $plugins $name; Assert-GamePath $path; Remove-Item -LiteralPath $path }
        if ($hadCurrent) { Publish-File $current $target }
        elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
        Publish-File (Join-Path $archive 'active.json') $activePath
        throw "Restore did not finish; the previous active state was reapplied where possible. Keep backup $backup. $reason"
    }
    Write-Host "Restored the previous installation. Current bridge archived at: $archive"
    Write-Host 'Restoring a legacy DLL only restores files; it does not establish current TruckersMP permission or compatibility.'
    return
}

$package = $PSScriptRoot
if (!(Test-Path -LiteralPath (Join-Path $package 'TruckCompanionOverlay.dll') -PathType Leaf)) {
    $package = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\release\companion-overlay'))
}
$variant = 'inprocess-dx11'
$source = Join-Path $package 'TruckCompanionOverlay.dll'
Assert-File $source
$legacy = @($legacyNames | Where-Object { Test-Path -LiteralPath (Join-Path $plugins $_) -PathType Leaf })
foreach ($name in $legacy) { Assert-GamePath (Join-Path $plugins $name) }
$hadBridge = Test-Path -LiteralPath $target -PathType Leaf
$hadActive = Test-Path -LiteralPath $activePath -PathType Leaf
if ($hadActive) {
    $active = Read-Json $activePath
    $priorPath = [IO.Path]::GetFullPath([string]$active.receipt); Assert-GamePath $priorPath
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($priorPath)) -ne $backupRoot -or [IO.Path]::GetFileName($priorPath) -ne 'receipt.json') { throw 'Invalid active receipt location.' }
    $prior = Read-Json $priorPath
    if ($active.schema -ne 1 -or $prior.status -ne 'installed' -or $prior.gameRoot -ne $gamePath) { throw 'Existing active installation record is inconsistent. Preserve it and resolve manually.' }
}
$backup = Join-Path $backupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
Assert-GamePath $backup
New-Item -ItemType Directory -Path $backup -Force | Out-Null
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
$receiptPath = Join-Path $backup 'receipt.json'
$receipt = [ordered]@{ schema=1; gameRoot=$gamePath; createdUtc=[DateTime]::UtcNow.ToString('o'); variant=$variant; hadBridge=[bool]$hadBridge; hadActive=[bool]$hadActive; disabledLegacy=$legacy; status='preparing' }
Write-Json $receiptPath $receipt
if ($hadBridge) { Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'TruckCompanionOverlay.dll') }
if ($hadActive) { Copy-Item -LiteralPath $activePath -Destination (Join-Path $backup 'previous-active.json') }
$moved = @(); $published = $false; $activeChanged = $false
try {
    Assert-Stopped
    foreach ($name in $legacy) {
        $from = Join-Path $plugins $name; $to = Join-Path $backup $name
        Assert-GamePath $from; Assert-GamePath $to
        Move-Item -LiteralPath $from -Destination $to; $moved += $name
    }
    Publish-File $source $target; $published = $true
    Write-Json $activePath ([ordered]@{ schema=1; receipt=$receiptPath }); $activeChanged = $true
    $receipt.status = 'installed'; Write-Json $receiptPath $receipt
} catch {
    $reason = $_
    if ($published) {
        if ($hadBridge) { Publish-File (Join-Path $backup 'TruckCompanionOverlay.dll') $target }
        elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    }
    foreach ($name in $moved) { Publish-File (Join-Path $backup $name) (Join-Path $plugins $name) }
    if ($activeChanged) {
        if ($hadActive) { Publish-File (Join-Path $backup 'previous-active.json') $activePath }
        elseif (Test-Path -LiteralPath $activePath) { Remove-Item -LiteralPath $activePath }
    }
    $receipt.status = 'failed'; Write-Json $receiptPath $receipt
    throw "Installation did not finish; previous files were reapplied where possible. Keep backup $backup. $reason"
}
Write-Host "Installed $variant : $target"
Write-Host "Receipt / rollback source: $receiptPath"
Write-Host 'Next: launch the game through TruckersMP in DX11 mode; the DLL draws its panel after SDK initialization. No executable has been started by this script.'
} finally { $installMutex.ReleaseMutex(); $installMutex.Dispose() }
