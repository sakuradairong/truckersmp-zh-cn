# Truck Companion Panel 0.1.0

独立 Windows x64 悬浮面板，作者署名 RainySY（[sakuradairong](https://github.com/sakuradairong)）。面板自己绘制窗口，适用于 ETS2 / ATS 的窗口化或无边框模式。

此目录保留独立窗口的源码和编译入口，作为可回退实现；**未执行测试、烟测、SHA 校验及完整游戏内验收**。各机器的实际安装情况以 receipt 为准，编译成功或文件部署不代表游戏显示、FPS 或 TruckersMP 的使用许可已验证。

## 两种数据采集方式

| 内容 | 默认 SCS 包 | 可选 TMP SDK 附加包 |
| --- | --- | --- |
| 本车速度、油量、档位、导航距离、道路限速、转速 | 有 | 有 |
| 当前货物、起点、终点 | 有，取决于游戏当前任务 | 有，取决于游戏当前任务 |
| 附近已加载玩家的昵称、ID、速度、距离、延迟、标签 | 无 | 官方 SDK 提供时可用 |
| 自动识别当前 TMP 账号和服务器 | 无 | 官方 SDK 提供时可用 |
| 个人公开资料、正式 VTC | 在设置中填写自己的 TMP ID | 自动识别，也可手动指定 |
| 本机关注、搜索、排序、同 VTC / 工作人员筛选 | 没有附近数据时为空 | 有 |

这里的“关注”保存在本机，不能识别 Steam 好友。附近列表代表本机流入范围，**不是全服玩家列表**。VTC 来自 TruckersMP 公开 API，不是昵称或标签推断。

默认 ZIP 不含可选 SDK DLL。可选包的 DLL 与默认 DLL 同名，安装时二选一；不能改名后把两个版本同时放进插件目录。

## 安装

1. 解压默认包到普通本地目录。保持 EXE、`install.ps1` 和 `collector-scs` 的相对位置。
2. 完全退出 ETS2 / ATS、面板和旧工具箱。
3. 在解压目录打开 PowerShell，运行下列命令。`GameRoot` 必须是含有 `bin\win_x64` 的游戏根目录，可替换为自己的路径。

```powershell
.\install.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2' -DisableLegacy
```

此命令默认安装 **SCS 版** `PanelDataBridge.dll`。`-DisableLegacy` 将已存在的 `TmpSettingsZh.dll`、`TmpNativeSpeedBridge.dll` 移到游戏根目录下的备份目录，避免继续加载旧原生插件。没有旧插件时，该参数无额外操作。省略该参数且发现旧插件时，脚本会停止并说明原因。

每次更新会自动备份已有 `PanelDataBridge.dll`，写入安装记录，再原子替换目标 DLL。失败时尝试恢复旧文件并保留备份。脚本不修改游戏配置、存档、资源、公开 Release 或注册表，不启动任何程序。

4. 按通常方式手动启动游戏。如游戏显示 SCS SDK 插件提示，按游戏自身流程处理。
5. 双击 `TruckCompanionPanel.exe`。首次进入编辑模式，未连接游戏时显示等待状态。

可选 SDK 包使用说明见 [TMP-SDK-OPTIONAL.md](TMP-SDK-OPTIONAL.md)。构建和打包不会自动安装到游戏目录。

## 操作

| 操作 | 默认方式 |
| --- | --- |
| 显示 / 隐藏 | `Ctrl+Shift+V` |
| 驾驶 / 编辑模式 | `Ctrl+Shift+E` |
| 移动窗口 | 编辑模式拖动标题左侧 |
| 查看玩家 VTC / 关注 | 编辑模式点击对应玩家，再点关注按钮 |
| 搜索昵称、ID、标签 | 编辑模式点“搜索玩家”，支持中文输入法 |
| 切换筛选 / 排序 | 点击相应按钮循环切换 |
| 列表翻页 | 编辑模式使用鼠标滚轮 |
| 改透明度、尺寸、字号、热键、账号 | “设置” |
| 找回穿透或隐藏的窗口 | 双击系统托盘图标，或右键托盘菜单 |
| 退出 | 面板右上角 ×，或托盘菜单“退出” |

驾驶模式置顶、鼠标穿透且不主动获得焦点；默认只在已发现的游戏处于前台时显示。没有游戏进程时仍可显示等待状态。编辑模式保持可操作。独占全屏可能覆盖桌面窗口，请使用窗口化或无边框。

存在多个 ETS2 / ATS 进程时优先前台游戏，再选择可连接采集模块的进程。最多尝试四个游戏进程。窗口每 200ms 更新一次，只有当前页面需要的资料才进入账号查询队列。

## 状态、缓存与容量

- 缺失值显示 `—`。游戏暂停时本车大号速度隐藏；其他本车指标仍是最近遥测。
- 管道断开、TMP 断线或玩家数据超过三秒未更新时，不把旧速度作为当前结果展示。
- 采集器最多保留 512 个活动玩家进行分批采样，每 100ms 读取最多 32 人；向面板发送其中按已知距离排序的最多 256 人。超过容量或尚未采样的人数在页面标注。在极端拥堵区不保证是全体附近玩家中最近的 256 人。
- 每个联机会话首次成功调用 `GetAllPlayers()` 后，用流入/流出事件维护成员；初次读取失败最多每五秒重试。离开采集范围不代表离开服务器。
- 个人资料/VTC 成功缓存 15 分钟；请求间隔至少三秒；失败通常等待一分钟；HTTP 429 遵循 Retry-After。服务器信息约一分钟更新一次。
- “同 VTC”只显示资料仍有效且已确认同队的玩家；首次查询大量玩家可能需要数分钟，未确认账号会标注为待查。没有 VTC 的账号不会匹配所有无车队玩家。
- 公开 API 失败或缓存过期会保留状态提示；不以失败响应推断玩家车队。手动填写的账号会标注“手动指定账号”，不会误作 SDK 自动识别。

## 配置与网络

配置路径：`%LOCALAPPDATA%\TruckCompanionPanel\settings.json`。仅保存窗口位置、尺寸、透明度、字号、热键、手动账号 ID 和关注 ID；不保存附近玩家历史或聊天。

面板通过仅本用户和 SYSTEM 可访问的本地命名管道读取快照，核对服务器进程 PID。游戏内采集代码不发 HTTP 请求。面板访问固定 HTTPS 主机 `api.truckersmp.com` 查询公开资料和服务器，不需要密钥或登录。点击公开主页/作者链接才打开浏览器。

热键注册失败时保持编辑模式，在设置中更换快捷键。配置损坏时使用可读取的默认设置并显示提示；关闭程序后可手动备份并移走配置文件以重置。

## 卸载和回退

**仅停用新模块：**先退出游戏与面板，将 `bin\win_x64\plugins\PanelDataBridge.dll` 移到该目录之外即可。面板本身是便携 EXE，不注册启动项。保留 `companion-panel-backups` 便于后续恢复。

**回退上一次安装：**使用安装时输出的最新记录路径，仍需先退出游戏与面板。

```powershell
.\install.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2' `
  -Restore 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2\companion-panel-backups\<本次目录>\receipt.json'
```

回退保留当前 DLL 的归档，恢复之前的 DLL 和本次停用的旧插件。若只想停用新方案，不需要恢复旧原生插件。恢复旧文件不表示它们符合当前 TMP 要求；请结合已收到的开发者反馈决定是否使用。多次更新需从最新记录依次回退。不要删除备份或手工改安装记录。

## 开发与更新兼容

完整方案：[DEVELOPMENT.md](DEVELOPMENT.md)。从仓库根目录运行：

```powershell
.\panel\build.ps1 -ScsSdk 'X:\scs-sdk-1.16\include'
.\panel\package.ps1
```

需要 Windows x64、VS 2022 C++ Build Tools、Windows SDK。新脚本只编译/链接、整理文件与压缩，不调用旧构建链，不执行测试或校验。`-CollectorOnly` 可只编译两种采集 DLL。

SCS SDK 是外部依赖。`-ScsSdk` 指定含 `scssdk_telemetry.h` 的 include 目录；默认使用本地忽略目录 `vendor/scs`。打包从构建记录找到所用 SDK 并保留声明，不需要访问原速度工具箱仓库。

新目标只使用 `panel/`、SCS SDK、PicoJSON 和可选官方 TMP SDK。没有 MinHook、客户端 RVA、机器码特征、TMP 资源解析、原生控件复用或游戏内绘图。TMP 的 `OnPostRender` 回调仅提供官方采样时机，不取游戏设备句柄。

TMP 内部地址变化不再需要修改本面板。后续官方 SDK 协议/模块、SCS 通道或公开 API 结构的变化仍可能需要适配；不能保证永久兼容。SDK 不支持时停止对应数据源，并保留独立窗口和可用的 SCS 功能。
