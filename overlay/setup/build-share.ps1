param([string]$Package='')
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(!$Package){$Package=(Get-Content -LiteralPath (Join-Path $project 'build\companion-overlay\package.path') -Raw -Encoding UTF8).Trim()}
$Package=(Resolve-Path -LiteralPath $Package).Path
$info=Get-Content -LiteralPath (Join-Path $Package 'BUILD-INFO.json') -Raw -Encoding UTF8|ConvertFrom-Json
if($info.product -ne 'Truck Companion Overlay' -or !$info.nativeFreight -or $info.flavour -ne 'local-freight'){throw 'Select a compiled local-freight package for the complete experimental installer.'}
$version=[Version]$info.version
$tag=$version.ToString()
$framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$compiler=Join-Path $framework 'csc.exe'
if(!(Test-Path -LiteralPath $compiler -PathType Leaf)){throw '.NET Framework x64 C# compiler not found.'}
$build=Join-Path $project ('build\companion-setup\'+[Guid]::NewGuid().ToString('N'))
$payload=Join-Path $build 'payload'
$share=Join-Path $project ('dist\companion-setup\'+$tag+'-'+[Guid]::NewGuid().ToString('N'))
$release=Join-Path $project 'release\companion-overlay'
foreach($directory in @($build,$payload,$share,$release)){New-Item -ItemType Directory -Path $directory -Force | Out-Null}
Copy-Item -LiteralPath (Join-Path $Package 'TruckCompanionOverlay.dll'),(Join-Path $Package 'BUILD-INFO.json') -Destination $payload
Copy-Item -LiteralPath (Join-Path $Package 'licenses') -Destination $payload -Recurse
$ownLicense=Join-Path $project 'LICENSE'
if(Test-Path -LiteralPath $ownLicense -PathType Leaf){Copy-Item -LiteralPath $ownLicense -Destination (Join-Path $payload 'licenses\project.txt')}
# BOM makes script and guide decoding deterministic for Windows PowerShell 5.1
# and older Notepad. No machine-wide execution-policy setting is changed.
foreach($entry in @(@((Join-Path (Split-Path $PSScriptRoot -Parent) 'install.ps1'),'install.ps1'),@((Join-Path $PSScriptRoot 'worker.ps1'),'worker.ps1'),@((Join-Path $PSScriptRoot '使用说明.txt'),'使用说明.txt'))){
    [IO.File]::WriteAllText((Join-Path $payload $entry[1]),[IO.File]::ReadAllText($entry[0]),[Text.UTF8Encoding]::new($true))
}
$zip=Join-Path $build 'payload.zip'
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $zip
$exeName='TruckCompanion-'+$tag+'-FullExperimental-Setup.exe'
$exe=Join-Path $share $exeName
$arguments=@('/nologo','/target:winexe','/platform:x64','/optimize+','/codepage:65001','/utf8output',('/out:'+$exe),('/win32manifest:'+(Join-Path $PSScriptRoot 'app.manifest')),('/resource:'+$zip+',payload.zip'),'/reference:System.dll','/reference:System.Core.dll','/reference:System.Drawing.dll','/reference:System.Windows.Forms.dll',('/reference:'+(Join-Path $framework 'System.IO.Compression.dll')),('/reference:'+(Join-Path $framework 'System.Web.Extensions.dll')),(Join-Path $PSScriptRoot 'Program.cs'))
& $compiler @arguments
if($LASTEXITCODE -ne 0){throw "Installer compilation failed: $LASTEXITCODE"}
foreach($name in @('使用说明.txt','分享说明.txt')){[IO.File]::WriteAllText((Join-Path $share $name),[IO.File]::ReadAllText((Join-Path $PSScriptRoot $name)),[Text.UTF8Encoding]::new($true))}
Copy-Item -LiteralPath (Join-Path $payload 'licenses') -Destination $share -Recurse
foreach($name in @('MIGRATION.md','FREIGHT-COPY.md','FREIGHT-FEEDBACK.md')){
    Copy-Item -LiteralPath (Join-Path $Package $name) -Destination $share
}
$shareZip=Join-Path $release ('TruckCompanion-'+$tag+'-FullExperimental-Setup.zip')
Compress-Archive -Path (Join-Path $share '*') -DestinationPath $shareZip -Force
Copy-Item -LiteralPath $exe -Destination (Join-Path $release $exeName) -Force
$record=[ordered]@{version=$tag;flavour='full-experimental';installer='Windows Forms / .NET Framework 4.5+';shareZip=$shareZip;installerExe=(Join-Path $release $exeName);portableFolder=$share;nativeFreight=$true;codeSigned=$false;validation='Compilation and static review only. Installer not launched. No tests, smoke tests or SHA verification.'}
$recordPath=Join-Path $project 'build\companion-overlay\share-package.json'
[IO.File]::WriteAllText($recordPath,($record|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
$record|ConvertTo-Json
