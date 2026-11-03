param([switch]$CollectorOnly,[string]$ScsSdk='')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if(!$ScsSdk){$ScsSdk=Join-Path $projectRoot 'vendor\scs'}
$ScsSdk=(Resolve-Path -LiteralPath $ScsSdk).Path
if(!(Test-Path -LiteralPath (Join-Path $ScsSdk 'scssdk_telemetry.h') -PathType Leaf)){throw 'ScsSdk must be the external SCS SDK include directory.'}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Visual Studio C++ x64 Build Tools not found.' }
$devcmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$buildRoot = Join-Path $projectRoot 'build\companion-panel'
$outputRoot = Join-Path $projectRoot 'release\companion-panel'
foreach ($dir in @($buildRoot, "$buildRoot\app", "$buildRoot\scs", "$buildRoot\tmp", $outputRoot, "$outputRoot\collector-scs", "$outputRoot\optional-tmp-sdk")) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
# This entry point only compiles/links. It never runs any produced binary.
$app = @'
cd /d "__BUILD__\app"
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 "__ROOT__\panel\main.cpp" "__ROOT__\panel\settings.cpp" "__ROOT__\panel\public_api.cpp" "__ROOT__\panel\transport.cpp" /link d2d1.lib dwrite.lib user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib winhttp.lib /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:"__ROOT__\panel\app.manifest" /OUT:"__OUTPUT__\TruckCompanionPanel.exe" /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
'@
$batch = @'
@echo off
call "__DEVCMD__" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
__APP__
cd /d "__BUILD__\scs"
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /LD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I"__SCS__" "__ROOT__\panel\collector.cpp" "__ROOT__\panel\transport.cpp" /link user32.lib advapi32.lib /DEF:"__ROOT__\panel\collector.def" /OUT:"__OUTPUT__\collector-scs\PanelDataBridge.dll" /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cd /d "__BUILD__\tmp"
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /LD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DPANEL_WITH_TMP_SDK /I"__SCS__" /I"__ROOT__\vendor\tmp-sdk\include" "__ROOT__\panel\collector.cpp" "__ROOT__\panel\transport.cpp" /link user32.lib advapi32.lib /DEF:"__ROOT__\panel\collector.def" /OUT:"__OUTPUT__\optional-tmp-sdk\PanelDataBridge.dll" /DYNAMICBASE /NXCOMPAT
exit /b %errorlevel%
'@
if ($CollectorOnly) { $app = '' }
$batch = $batch.Replace('__APP__', $app).Replace('__DEVCMD__', $devcmd).Replace('__BUILD__', $buildRoot).Replace('__ROOT__', $projectRoot).Replace('__OUTPUT__', $outputRoot).Replace('__SCS__',$ScsSdk)
$batchPath = Join-Path $buildRoot 'compile.cmd'
# cmd.exe expects CRLF. Do not use the legacy build scripts here.
$batch = $batch -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText($batchPath, $batch, [Text.Encoding]::Default)
& $env:ComSpec /d /c "`"$batchPath`""
if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $LASTEXITCODE" }
[IO.File]::WriteAllText((Join-Path $buildRoot 'scs-sdk.path'),$ScsSdk,[Text.UTF8Encoding]::new($false))
Write-Host "Compilation finished. Outputs: $outputRoot"
