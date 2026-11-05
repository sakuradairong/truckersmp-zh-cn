# Truck Companion Overlay 0.2.11

Windows x64 / ETS2 / TruckersMP DX11 中文游戏内面板，作者 [RainySY](https://github.com/sakuradairong)。这是本仓库唯一维护的面板，源码和公共数据模型均在 `overlay/`。

`TruckCompanionOverlay.dll` 由游戏插件系统加载，提供 SCS 遥测及 TMP SDK 入口。附近、本车、货运、资料、设置由本项目自绘；不需要独立 EXE 或注入器。

## 本版变化

- 移除旧资源汉化、Settings hook、MinHook 和独立窗口构建路线；保留首次运行的旧设置读取及历史安装凭据恢复。
- 保留自选做货、普通/WoT 复制及已有结构定位规则。
- 将“已提交”改为“调用已返回，结果待核对”，补齐调用边界与遥测状态记录。
- 主面板隐藏后保留右上角只读货运反馈；15 秒无新任务时提示核对，两分钟无结果保持未知。

原版不弹提示的原因尚未确定，详见[反馈调查](FREIGHT-FEEDBACK.md)。本版没有新增货运 hook、执行入口或重试行为。

## 安装与更新

普通用户解压分享 ZIP，双击 Setup EXE，选择 ETS2 目录，点击“安装 / 升级”。先退出游戏。详见[安装器方案](setup/INSTALL-PLAN.md)和[使用说明](setup/使用说明.txt)。

命令行用户在解压的便携包中运行：

```powershell
.\install.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2'
```

安装到 `bin/win_x64/plugins/TruckCompanionOverlay.dll`，备份本项目旧 `PanelDataBridge.dll`、`TmpSettingsZh.dll`，保留其他插件与配置。TMP 资源目录不由新安装器处理；已用过旧汉化的用户应阅读[迁移说明](MIGRATION.md)。

更新记录为游戏根目录 `companion-overlay-active.json`，备份与逐次恢复记录在 `companion-overlay-backups`。安装脚本只操作文件，不启动游戏或面板。

## 操作

| 操作 | 默认方式 |
| --- | --- |
| 显示/隐藏 | Ctrl+Shift+V |
| 驾驶/操作模式 | 面板显示时右键；备用 Ctrl+Shift+E |
| 返回驾驶 | 右键、ESC 或“返回驾驶” |
| 移动/缩放 | 操作模式拖动标题栏或窗口边缘 |
| 配置 | 设置页修改字号、透明度、快捷键后应用并保存 |

保留已有快捷键。支持可录制单键及组合键；拒绝系统保留组合，不保证能检测其他插件的按键占用。原版已占用输入时不能进入操作模式。只读驾驶模式和货运反馈不接管输入。

附近列表是客户端已加载范围，缺失数据用“—”表示；关注仅保存在本机，同 VTC 使用公开 API 资料。本车信息来自 SCS 遥测。中文字体使用本机系统字体，IME 使用官方字符事件和系统候选框定位。

## 货运

[货运指南](FREIGHT-COPY.md)包含自选路线、货物、挂车、到场接取、普通及 WoT 任务复制。WoT 只复制其他玩家现有在线任务，不任意创建在线合同。

默认 `public-sdk` 构建只显示任务；`local-freight` 构建才包含内部执行。生成货单、接取任务和用户确认分别记录。内部接入尚未取得 TMP 对本项目的明确许可，也没有公开请求回执接口。

## 设置与恢复

设置位于 `%LOCALAPPDATA%\TruckCompanionOverlay/settings.json`。无新设置时只读迁移旧 `TruckCompanionPanel/settings.json` 的账号、关注与偏好，不覆盖旧文件。

停用：退出游戏后运行 `install.ps1 -GameRoot '<游戏目录>' -Disable`，或将 DLL 移出 plugins。不会恢复旧插件。回退：使用 `-Restore '<最新备份目录>/receipt.json'`，按凭据恢复上一次状态；可能包含旧插件，见[迁移说明](MIGRATION.md)。

## 开发

依赖 MSVC x64、外部 SCS SDK、仓库中的 TMP SDK、Dear ImGui 和 PicoJSON。数据模型在 `model.h`，设置在 `settings.*`，不再依赖 `panel/` 的 IPC 或 EXE。

```powershell
.\overlay\build.ps1 -ScsSdk 'X:\scs-sdk-1.16\include'
.\overlay\package.ps1
# 可选本地货运：使用已审查基准生成并留存的描述
.\overlay\build.ps1 -ScsSdk 'X:\scs-sdk-1.16\include' -NativeFreightProfile '<freight_profile.h>'
.\overlay\package.ps1
.\overlay\setup\build-share.ps1
```

也可用 `-NativeFreightCore '<已审查的 0.7.9.4 基准>'` 生成描述。两个参数互斥。版本、地址移动不单独视为失败；指令形状、类型和调用关系必须仍匹配。不能将重定位理解为永久兼容。

构建和打包不执行测试、烟测、SHA 校验、安装或启动游戏。编译不代表货运、输入、全屏、设备重建或安装器已经运行验收。Dear ImGui 本地修改见 `vendor/imgui-1.92.9/COMPANION-NOTES.md`。
