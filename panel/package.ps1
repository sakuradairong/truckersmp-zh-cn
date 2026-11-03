param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$output = Join-Path $projectRoot 'release\companion-panel'
$scsSdk=(Get-Content -LiteralPath (Join-Path $projectRoot 'build\companion-panel\scs-sdk.path') -Raw -Encoding UTF8).Trim()
if(!(Test-Path -LiteralPath (Join-Path $scsSdk 'scssdk_telemetry.h') -PathType Leaf)){throw 'The SCS SDK used for compilation is no longer available.'}
foreach ($file in @('TruckCompanionPanel.exe','collector-scs\PanelDataBridge.dll','optional-tmp-sdk\PanelDataBridge.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $output $file) -PathType Leaf)) { throw "Compile first: missing $file" }
}
$stage = Join-Path $projectRoot ('dist\companion-panel\0.1.0-' + [Guid]::NewGuid().ToString('N'))
$base = Join-Path $stage 'default'
$addon = Join-Path $stage 'addon'
foreach ($dir in @($base,"$base\collector-scs","$base\licenses\scs","$addon\optional-tmp-sdk","$addon\licenses")) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}
Copy-Item -LiteralPath (Join-Path $output 'TruckCompanionPanel.exe') -Destination $base
Copy-Item -LiteralPath (Join-Path $output 'collector-scs\PanelDataBridge.dll') -Destination "$base\collector-scs"
Copy-Item -LiteralPath (Join-Path $output 'optional-tmp-sdk\PanelDataBridge.dll') -Destination "$addon\optional-tmp-sdk"
foreach ($name in @('README.md','DEVELOPMENT.md','TMP-SDK-OPTIONAL.md','install.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $base
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'TMP-SDK-OPTIONAL.md') -Destination $addon
Copy-Item -LiteralPath (Join-Path $projectRoot 'vendor\picojson\LICENSE.txt') -Destination "$base\licenses\picojson.txt"
Copy-Item -LiteralPath (Join-Path $projectRoot 'vendor\tmp-sdk\LICENSE') -Destination "$addon\licenses\truckersmp-sdk.txt"
Get-ChildItem -LiteralPath $scsSdk -Filter '*.h' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination "$base\licenses\scs" }
$record = [ordered]@{ product='Truck Companion Panel'; version='0.1.0'; packagedUtc=[DateTime]::UtcNow.ToString('o'); validation='Compilation and static source review only. No runtime acceptance, tests, smoke tests or checksum verification performed.' }
$json = $record | ConvertTo-Json
[IO.File]::WriteAllText((Join-Path $base 'BUILD-INFO.json'),$json,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $addon 'BUILD-INFO.json'),$json,[Text.UTF8Encoding]::new($false))
$defaultZip = Join-Path $output 'TruckCompanionPanel-0.1.0-windows-x64.zip'
$addonZip = Join-Path $output 'TruckCompanionPanel-0.1.0-tmp-sdk-addon.zip'
Compress-Archive -Path "$base\*" -DestinationPath $defaultZip -Force
Compress-Archive -Path "$addon\*" -DestinationPath $addonZip -Force
Write-Host "Default package: $defaultZip"
Write-Host "Optional SDK addon: $addonZip"
Write-Host "Uncompressed portable package: $base"
