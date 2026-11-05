param(
    [Parameter(Mandatory=$true)][ValidateSet('Install','Restore','Disable')][string]$Action,
    [Parameter(Mandatory=$true)][string]$GameRoot
)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
function Assert-Plain([string]$Path){
    $cursor=[IO.Path]::GetFullPath($Path)
    while($cursor){
        if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw '安装路径包含链接，请选择游戏的实际目录。'}
        $parent=[IO.Path]::GetDirectoryName($cursor)
        if(!$parent -or $parent -eq $cursor){break}
        $cursor=$parent
    }
}
function Read-Record([string]$Path){
    Assert-Plain $Path
    if(!(Test-Path -LiteralPath $Path -PathType Leaf) -or (Get-Item -LiteralPath $Path).Length -gt 32768){throw '安装记录缺失或过大，请保留备份并联系发布者。'}
    return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8|ConvertFrom-Json)
}
$result=[ordered]@{success=$false;action=$Action;message='';gameRoot=$GameRoot;receipt='';details=''}
$resultPath=Join-Path $PSScriptRoot 'operation.json'
try {
    $root=[IO.Path]::GetFullPath($GameRoot).TrimEnd('\')
    Assert-Plain $root
    if(!(Test-Path -LiteralPath (Join-Path $root 'bin\win_x64\eurotrucks2.exe') -PathType Leaf)){throw '请选择包含 bin\win_x64\eurotrucks2.exe 的 ETS2 游戏目录。'}
    $running=@(Get-Process -Name eurotrucks2,amtrucks,TruckCompanionPanel,TmpLocalToolboxNext,TmpLocalToolbox -ErrorAction SilentlyContinue)
    if($running.Count){throw ('请先保存进度并完全退出游戏及相关工具：'+(($running.ProcessName|Sort-Object -Unique)-join '、'))}
    $engine=Join-Path $PSScriptRoot 'install.ps1'
    $activePath=Join-Path $root 'companion-overlay-active.json'
    if($Action -eq 'Install'){
        $info=Read-Record (Join-Path $PSScriptRoot 'BUILD-INFO.json')
        if($info.product -ne 'Truck Companion Overlay' -or $info.flavour -ne 'local-freight' -or !$info.nativeFreight){throw '安装包描述不匹配，请重新获取完整实验版。'}
        & $engine -GameRoot $root -RejectDowngrade
        $active=Read-Record $activePath
        $receipt=Read-Record $active.receipt
        if($receipt.status -ne 'installed' -or !(Test-Path -LiteralPath (Join-Path $root 'bin\win_x64\plugins\TruckCompanionOverlay.dll') -PathType Leaf)){throw '安装记录未完成，请保留安装日志和备份。'}
        $result.receipt=$active.receipt
        $result.message='安装完成。请自行从 TruckersMP 启动 ETS2（DX11）。首次使用按 Ctrl+Shift+V 显示面板，右键进入操作模式。升级沿用原有快捷键。'
    }elseif($Action -eq 'Restore'){
        if(!(Test-Path -LiteralPath $activePath -PathType Leaf)){throw '没有找到本面板的安装记录，不能自动回退。'}
        $active=Read-Record $activePath
        & $engine -GameRoot $root -Restore ([string]$active.receipt)
        $result.receipt=$active.receipt
        $result.message='已恢复到本次安装之前。首次安装的回退会移除本面板；升级的回退会恢复之前版本。配置和备份保留。'
    }else{
        & $engine -GameRoot $root -Disable
        if(Test-Path -LiteralPath (Join-Path $root 'bin\win_x64\plugins\TruckCompanionOverlay.dll')){throw '面板文件仍在插件目录，请保留日志并联系发布者。'}
        $result.message='面板已停用，DLL 已移到游戏目录内的 companion-overlay-disabled 备份目录。下次启动不再加载本面板；个人配置和其他插件保留。'
    }
    $result.success=$true
}catch{
    $message=$_.Exception.Message
    if($message.StartsWith('A newer version is already installed.')){$message='已经安装了更新的版本。请获取新版安装器；需要回退时使用“回退本次安装”。'}
    elseif($message.StartsWith('Another panel installation or restore is in progress.')){$message='另一个安装或回退操作正在进行，请等待完成后再试。'}
    elseif($message.StartsWith('Exit the games and companion/toolbox applications first:')){$message='游戏或相关工具重新启动了，请先保存并完全退出，再进行安装。'}
    elseif($message.StartsWith('Linked paths are not supported:')){$message='安装或备份位置包含链接，请选择实际游戏目录并保留现有备份。'}
    $result.message='操作未完成：'+$message
    $result.details=$_.ToString()
}
[IO.File]::WriteAllText($resultPath,($result|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
if(!$result.success){exit 1}
