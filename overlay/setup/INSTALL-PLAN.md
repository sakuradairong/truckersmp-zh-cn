# 普通玩家分享与安装方案

交付选择：完整实验版，包含自选货运和普通/WoT 任务复制。只生成本地文件，不自动发布、上传或向他人发送。

## 玩家拿到什么

- 一个分享 ZIP：内含单文件中文 Setup EXE、使用说明、转发文案和许可证。
- EXE 内嵌本面板 DLL、文件安装脚本及所需文档，离线完成安装，无需 Python 或开发工具。
- 安装器使用 Windows 10/11 自带的 .NET Framework 和 Windows PowerShell。Windows 会申请管理员权限以写入游戏目录。
- 安装器和说明均标注完整实验版、非官方状态、内部货运接入未获明确许可，以及原版可能不弹接取提示的问题。

## 操作流程

1. 玩家保存并退出游戏。
2. 解压 ZIP，双击 Setup EXE。
3. 从识别到的 Steam 游戏目录中选择 ETS2；多目录不擅自选第一个，未识别时可浏览选择。
4. 点击“安装 / 升级”，自动备份旧 DLL 并替换。配置与第三方插件保留。
5. 玩家自行从 TMP 以 DX11 启动 ETS2。新安装默认 Ctrl+Shift+V 显示、右键操作；升级沿用旧快捷键。

安装器不启动游戏、不强杀进程、不加载目标 DLL、不写 TMP 核心或资源、不修改全局执行策略。

## 三个动作的区别

| 按钮 | 效果 | 保留内容 |
| --- | --- | --- |
| 安装 / 升级 | 备份替换 TruckCompanionOverlay.dll；本项目旧 PanelDataBridge.dll、TmpSettingsZh.dll 如存在则备份停用 | 配置、其他作者插件、原私人速度工具箱、历次备份 |
| 回退本次安装 | 根据最新安装凭据恢复到那次安装之前；首次安装回退为不安装 | 配置、备份；历史凭据涉及的旧插件按原记录恢复 |
| 停用面板 | 将当前 DLL 移出 plugins 并归档，不恢复任何旧插件 | 配置、其他插件、安装凭据和所有备份 |

停用与回退不同，不将“卸载”实现成误恢复多年前的插件。多人安装器并行由同一命名互斥量保护；进程检查在文件操作前再次执行。自动版本降级保护在安装锁内执行，旧记录缺少版本时只能保留备份，不能猜测版本。

## 可恢复性与范围

安装继续复用现有 install.ps1 的逐次回退凭据和原子文件替换；新增停用动作会先记录，再归档本 DLL。文件操作失败时尽力恢复原文件，并保留备份和错误记录。路径中的重解析点会被拒绝，不跟随链接写入其他位置。

GUI 与命令行共用迁移范围：只备份停用本项目旧 PanelDataBridge.dll、TmpSettingsZh.dll。TmpNativeSpeedBridge.dll、SpeedLabelsPlugin.dll 和其他作者插件保留。旧凭据仍可按原记录回退。GUI 启用 RejectDowngrade。TMP shared_mod 资源的恢复需依据旧安装备份手动完成，安装器不覆盖资源目录。

安装日志留在每次打开安装器时新建的临时目录；界面提供打开按钮。用户分享日志前可自行查看路径信息。不会上传日志。

## 分享包构建

前提：已有编译、打包好的完整 local-freight 包。不会重新提取客户端文件或捆绑第三方面板。

```powershell
.\overlay\setup\build-share.ps1
# 或明确选择本次编译包：
.\overlay\setup\build-share.ps1 -Package '<本地完整实验版包目录>'
```

构建从所选包复制 DLL、BUILD-INFO.json 和 licenses，再加入本仓库安装脚本与普通玩家说明。文件清单不包含 settings.json、Documents 部署目录、研究资料、缓存、安装凭据、游戏文件或私人速度工具箱。

输出：release/companion-overlay 下的 FullExperimental-Setup.exe 与同名 ZIP；build/companion-overlay/share-package.json 记录交付路径。

本轮只进行源码检查、编译和打包，不启动安装器，不执行安装/卸载测试、烟测或 SHA 校验。尚未完成普通用户安装环境与游戏内验收；不声称签名发行、官方授权或全部功能验证通过。
