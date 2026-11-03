param()
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$output=Join-Path $project 'release\companion-overlay'
$dll=Join-Path $output 'TruckCompanionOverlay.dll'
if(!(Test-Path -LiteralPath $dll -PathType Leaf)){throw 'Compile overlay/build.ps1 first'}
$scsSdk=(Get-Content -LiteralPath (Join-Path $project 'build\companion-overlay\scs-sdk.path') -Raw -Encoding UTF8).Trim()
if(!(Test-Path -LiteralPath (Join-Path $scsSdk 'scssdk_telemetry.h') -PathType Leaf)){throw 'The SCS SDK used for compilation is no longer available.'}
$stage=Join-Path $project ('dist\companion-overlay\0.2.5-'+[Guid]::NewGuid().ToString('N'))
foreach($dir in @($stage,(Join-Path $stage 'licenses'),(Join-Path $stage 'licenses\scs'))){New-Item -ItemType Directory -Path $dir -Force | Out-Null}
Copy-Item -LiteralPath $dll -Destination $stage
foreach($name in @('README.md','FREIGHT-RESEARCH.md','FREIGHT-DEEP-DIVE.md','FREIGHT-INTEGRATION-OPTIONS.md','install.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $stage}
Copy-Item -LiteralPath (Join-Path $project 'vendor\imgui-1.92.9\LICENSE.txt') -Destination (Join-Path $stage 'licenses\imgui.txt')
Copy-Item -LiteralPath (Join-Path $project 'vendor\imgui-1.92.9\COMPANION-NOTES.md') -Destination (Join-Path $stage 'licenses\imgui-local-changes.md')
Copy-Item -LiteralPath (Join-Path $project 'vendor\tmp-sdk\LICENSE') -Destination (Join-Path $stage 'licenses\truckersmp-sdk.txt')
Copy-Item -LiteralPath (Join-Path $project 'vendor\picojson\LICENSE.txt') -Destination (Join-Path $stage 'licenses\picojson.txt')
Get-ChildItem -LiteralPath $scsSdk -Filter '*.h' -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $stage 'licenses\scs')}
$record=[ordered]@{product='Truck Companion Overlay';version='0.2.5';packagedUtc=[DateTime]::UtcNow.ToString('o');scope='TMP public SDK + SCS, in-process DX11 UI';validation='Compilation and static review only. No runtime acceptance, tests, smoke tests or checksum verification.'}
[IO.File]::WriteAllText((Join-Path $stage 'BUILD-INFO.json'),($record|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
$zip=Join-Path $output 'TruckCompanionOverlay-0.2.5-windows-x64.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
[IO.File]::WriteAllText((Join-Path $project 'build\companion-overlay\package.path'),$stage,[Text.UTF8Encoding]::new($false))
Write-Host "Package: $zip"
Write-Host "Portable folder: $stage"
