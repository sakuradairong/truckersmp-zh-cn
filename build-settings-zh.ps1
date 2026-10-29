param(
    [string]$Python = 'python',
    [Parameter(Mandatory = $true)][string]$TmpRoot,
    [Parameter(Mandatory = $true)][string]$ScsSdk
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$TmpRoot = (Resolve-Path -LiteralPath $TmpRoot).Path
$ScsSdk = (Resolve-Path -LiteralPath $ScsSdk).Path
if (!(Test-Path -LiteralPath (Join-Path $ScsSdk 'scssdk_telemetry.h'))) {
    throw 'ScsSdk must be the SDK include directory containing scssdk_telemetry.h'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'MSVC x64 tools not found; install Visual Studio C++ Build Tools and Windows SDK' }
$devcmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
foreach ($buildPath in @($projectRoot, $TmpRoot, $ScsSdk, $devcmd)) {
    if ($buildPath -match '[&|<>^%!"\r\n]') { throw 'Build path contains a cmd.exe metacharacter' }
}
& $Python -B (Join-Path $projectRoot 'tools\prepare_native_settings_zh.py') --core (Join-Path $TmpRoot 'core_ets2mp.dll')
if ($LASTEXITCODE -ne 0) { throw 'Native Settings profile preparation failed' }
$batch = @"
@echo off
chcp 65001 >nul
call "__DEVCMD__" -arch=x64 -host_arch=x64 >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%
cd /d "__ROOT__\build\native-settings-zh"
cl /nologo /O2 /MT /W3 /c /DWIN32 /D_WIN64 /I"..\..\vendor\minhook\include" /I"..\..\vendor\minhook\src" "..\..\vendor\minhook\src\buffer.c" "..\..\vendor\minhook\src\hook.c" "..\..\vendor\minhook\src\trampoline.c" "..\..\vendor\minhook\src\hde\hde64.c"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /LD /I"." /I"..\..\vendor\minhook\include" /I"__SCS__" "..\..\src\settings_zh.cpp" buffer.obj hook.obj trampoline.obj hde64.obj /link bcrypt.lib /DEF:"..\..\src\settings_zh.def" /OUT:TmpSettingsZh.dll /PDB:TmpSettingsZh.pdb /DYNAMICBASE /NXCOMPAT
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /I"." /I"..\..\vendor\minhook\include" /I"__SCS__" "..\..\tests\native_settings_zh_test.cpp" buffer.obj hook.obj trampoline.obj hde64.obj /link bcrypt.lib /OUT:native-settings-zh-test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
native-settings-zh-test.exe TmpSettingsZh.dll native-checks.json
exit /b %errorlevel%
"@
$batch = $batch.Replace('__DEVCMD__', $devcmd).Replace('__ROOT__', $projectRoot).Replace('__SCS__', $ScsSdk)
$batchPath = Join-Path $projectRoot 'build\native-settings-zh\build.cmd'
[IO.File]::WriteAllText($batchPath, $batch, [Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $batchPath | Tee-Object -FilePath (Join-Path $projectRoot 'build\native-settings-zh\build.log')
if ($LASTEXITCODE -ne 0) { throw "Native Settings build/tests failed: $LASTEXITCODE" }
Get-FileHash -LiteralPath (Join-Path $projectRoot 'build\native-settings-zh\TmpSettingsZh.dll') -Algorithm SHA256