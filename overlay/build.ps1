param([string]$ScsSdk='')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
if(!$ScsSdk){$ScsSdk=Join-Path $project 'vendor\scs'}
$ScsSdk=(Resolve-Path -LiteralPath $ScsSdk).Path
if(!(Test-Path -LiteralPath (Join-Path $ScsSdk 'scssdk_telemetry.h') -PathType Leaf)){throw 'ScsSdk must be the external SCS SDK include directory.'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio C++ x64 Build Tools not found'}
$build=Join-Path $project 'build\companion-overlay'
$output=Join-Path $project 'release\companion-overlay'
New-Item -ItemType Directory -Path $build,$output -Force | Out-Null
$batch=@'
@echo off
call "__VS__\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "__BUILD__"
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /LD /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DIMGUI_USE_WCHAR32 /I"__SCS__" /I"__ROOT__\vendor\tmp-sdk\include" /I"__ROOT__\vendor\imgui-1.92.9" /I"__ROOT__\vendor\imgui-1.92.9\backends" "__ROOT__\overlay\entry.cpp" "__ROOT__\overlay\runtime.cpp" "__ROOT__\overlay\telemetry.cpp" "__ROOT__\overlay\renderer.cpp" "__ROOT__\overlay\views.cpp" "__ROOT__\overlay\service.cpp" "__ROOT__\panel\settings.cpp" "__ROOT__\vendor\imgui-1.92.9\imgui.cpp" "__ROOT__\vendor\imgui-1.92.9\imgui_draw.cpp" "__ROOT__\vendor\imgui-1.92.9\imgui_tables.cpp" "__ROOT__\vendor\imgui-1.92.9\imgui_widgets.cpp" "__ROOT__\vendor\imgui-1.92.9\backends\imgui_impl_dx11.cpp" /link user32.lib gdi32.lib shell32.lib ole32.lib winhttp.lib imm32.lib d3d11.lib d3dcompiler.lib dxgi.lib /DEF:"__ROOT__\overlay\overlay.def" /OUT:"__OUTPUT__\TruckCompanionOverlay.dll" /DYNAMICBASE /NXCOMPAT
exit /b %errorlevel%
'@
$batch=$batch.Replace('__VS__',$vs).Replace('__BUILD__',$build).Replace('__ROOT__',$project).Replace('__OUTPUT__',$output).Replace('__SCS__',$ScsSdk)
$batch=$batch -replace "`r?`n","`r`n"
$script=Join-Path $build 'compile.cmd'
[IO.File]::WriteAllText($script,$batch,[Text.Encoding]::Default)
& $env:ComSpec /d /c "`"$script`""
if($LASTEXITCODE -ne 0){throw "Compilation failed: $LASTEXITCODE"}
[IO.File]::WriteAllText((Join-Path $build 'scs-sdk.path'),$ScsSdk,[Text.UTF8Encoding]::new($false))
Write-Host "Compiled only: $output\TruckCompanionOverlay.dll"
