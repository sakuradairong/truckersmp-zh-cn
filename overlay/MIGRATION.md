# 迁移到唯一游戏内面板

当前只维护 `TruckCompanionOverlay.dll`。旧资源安装器、Settings 汉化 hook、独立 EXE 和其构建代码已退出源码树。货运内部接入保留。

## 已安装旧版的用户

1. 保存进度并完全退出 ETS2/ATS、TMP 启动器和旧面板。
2. 新面板安装器会备份停用本项目的 `PanelDataBridge.dll`、`TmpSettingsZh.dll`；配置和备份保留。它不会停用 `TmpNativeSpeedBridge.dll`、`SpeedLabelsPlugin.dll` 或其他作者插件。
3. 旧资源汉化需使用原安装器及 `%LOCALAPPDATA%\TruckersMPZH` 中的原始记录恢复。新安装器不会修改 TMP 的 `data/shared_mod` 或 `data/shared`。
4. 若原安装器不能恢复且只需要暂时停用覆盖资源，可将 TMP 安装目录中的 `data/shared_mod` 重命名到目录外保存。此操作也会停用其中其他覆盖内容；不要删除 `data/shared` 或原备份。精确恢复已有覆盖内容仍需原安装记录。
5. 无法使用新安装器时，可将 `bin/win_x64/plugins/TmpSettingsZh.dll`、`PanelDataBridge.dll` 移到插件目录外保存，再安装 overlay。不要同时加载旧数据桥和新面板。

这些是迁移步骤，不表示源码清理已经改变你电脑中的游戏文件。本轮没有执行实际游戏目录的安装、卸载或资源恢复。

## 设置与回退

新面板继续使用 `%LOCALAPPDATA%\TruckCompanionOverlay/settings.json`。首次运行时可只读迁移旧 `TruckCompanionPanel/settings.json` 的偏好；保留该路径兼容不代表继续维护独立 EXE。

退出游戏后使用 `install.ps1 -Disable` 只停用新面板，不恢复旧插件。使用 `-Restore <最新 receipt.json>` 则按原凭据恢复之前的文件，可能包含旧插件。保留 `companion-overlay-backups`、`companion-overlay-active.json` 和原资源安装记录。

历史安装凭据仍接受旧文件名，以保持回退能力；新的安装只迁移本项目两个已退役模块。旧发布包、本机调查材料和备份保留在原处，不属于当前构建输入。
