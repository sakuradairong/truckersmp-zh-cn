# Truck Companion Overlay 0.2.4

游戏进程内自绘面板，作者 RainySY（[sakuradairong](https://github.com/sakuradairong)）。适用于 Windows x64、TruckersMP 的 DirectX 11 渲染模式。

`TruckCompanionOverlay.dll` 同时提供 SCS 遥测和 TMP 官方 SDK 入口，由游戏插件系统加载。游戏完成自身的 SDK 提示确认、初始化插件并提供有效绘图帧后，面板默认显示为只读驾驶模式；不需要打开面板 EXE，也不需要另一个注入器或后台监控程序。

## 0.2.4 操作与快捷键

面板显示且游戏位于前台时，右键切换操作/驾驶；ESC 或底部“返回驾驶”也可退出操作。切换所用的右键按下与松开都通过公开 SDK 标记为已处理。面板隐藏时不接管右键；游戏/TMP 的输入锁、现有鼠标光标或更早处理的鼠标事件表明其他界面正在使用输入时，不进入操作模式。未修改 TMP 内部代码，也没有增加输入挂钩。

设置页改为点击快捷键按钮后直接按键录制，ESC 取消录制。支持单个字母、数字、F1–F24、Tab、Insert 等导航键，或与 Ctrl/Shift/Alt 组合；保留原有显示键及操作备用键。录制时暂停旧快捷键，完成后等按键松开，避免触发原绑定或将按键输入到文本框。输入文字时暂停快捷键，右键和 ESC 仍可返回驾驶。两个操作不能使用相同绑定，不接受 Windows 键、ESC 及已列出的系统组合（Alt+F4、Alt+Tab、Alt+Space、Ctrl+Alt+Delete）。

录制只修改设置草稿，点击“应用并保存”后生效；保存的单键绑定可在下次启动恢复。公开 SDK 没有提供全局按键占用查询或逐个键盘事件的阻止标志，因此这里不宣称能阻止其他插件响应同一按键，需避开游戏和其他面板已有绑定。独立 EXE 继续要求带修饰键的组合，防止注册全局单字母快捷键。

0.2.4 包含以下 ID 冲突和上下文清理修复。本次仅做静态检查、编译和打包，不运行测试、烟测、SHA 校验或游戏内验收。

## 0.2.3 修复

修复“附近”页的 `2 visible items with conflicting ID` 提示：上方“关注”筛选与下方玩家“关注”按钮曾处于同一 ImGui ID 作用域，显示文字相同导致控件冲突。筛选组、玩家操作和底部导航现使用独立作用域，各内容页也分别隔离。玩家操作使用完整 TMP 账号区分目标，“关注/取消关注”共用稳定的隐藏操作标识，避免文字变化影响控件身份。

保留 ImGui 冲突诊断和断言；界面文字、布局和本机关注数据不变。0.2.3 包含以下 0.2.2 后端清理修复。本次交付仅做源码检查和编译打包，没有执行测试、烟测、SHA 校验或游戏内复现；安装与实机交互验收需另行进行。

## 0.2.2 修复

修复用户截图中的 `imgui.cpp:4530` / `Forgot to shutdown Platform backend?` 断言。旧版在创建上下文时登记自定义平台后端，在 Reset 时仅关闭 DX11 后端，遗漏平台数据清理。新版在销毁上下文前停止 IME 编辑、恢复候选框位置，清理平台回调、窗口句柄及后端数据；正常卸载、设备重建和初始化失败都使用该清理路径。

同时明确切换到本面板上下文执行初始化，并用作用域恢复此前上下文（包括空上下文和初始化异常路径），避免更改其他上下文。保留 ImGui 断言和原有后端检查。

截图足以确认销毁流程的遗漏，不能单凭它判断当时由退出游戏还是设备重建触发。0.2.2 已完成源码检查与编译，没有执行测试、烟测、SHA 校验或游戏内复现；各机器的实际安装版本以安装记录为准。

## 已实现内容

- 参考 TMP TAB 的深灰半透明背景、红色选中项、紧凑列表和底部导航；图形由本项目自绘。
- 附近、本车、货运、资料、设置五个入口。附近列表分为 ID、玩家、车速、延迟、距离五列。
- 附近玩家昵称、游戏内 ID、TMP ID、速度、距离、延迟、标签、工作人员标记。
- 搜索、距离/速度/延迟排序、本机关注、同 VTC 和工作人员筛选。
- 本车速度、档位、油量、导航距离、限速、转速，以及当前货物、起点和终点。
- 自动读取当前 TMP 账号、公开 API 个人资料、正式 VTC、当前服务器资料。
- 右键操作/驾驶、可录制的显示/隐藏及操作备用键，背景透明度、字号、位置和尺寸保存。
- 官方 SDK 字符输入、中文系统字体、系统 IME 候选框位置适配。

缺失数据显示“—”，暂停时速度显示“—”。附近是客户端已加载范围，不代表全服玩家。关注仅保存在本机，不代表 Steam 好友；同车队按正式 VTC 筛选。

货运页展示当前货物、起终点城市及导航距离。**本版本没有创建或接取任务的功能。** “选城市/公司/货物，并到货场直接接取”的接口调查和后续设计见 [货运研究](FREIGHT-RESEARCH.md)。

## 安装和替换

关闭 ETS2/ATS、独立面板 EXE 与旧工具箱，将 ZIP 解压后，在目录中运行：

```powershell
.\install.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2'
```

脚本默认备份并替换。会将已有 `PanelDataBridge.dll`、`TmpNativeSpeedBridge.dll`、`TmpSettingsZh.dll` 或 `SpeedLabelsPlugin.dll` 移到备份目录，再安装新 DLL，避免本项目的两个模式同时加载。其他插件保留。

新 DLL 路径为 `bin\win_x64\plugins\TruckCompanionOverlay.dll`。更新时会备份该文件；安装记录位于游戏根目录 `companion-overlay-active.json`，备份和逐次回退记录位于 `companion-overlay-backups`。此前独立面板的安装记录和 EXE 可以作为历史回退材料保留，以新 overlay 记录确认当前模式。

安装脚本只处理文件，不启动游戏、面板或任何检查程序。

## 操作

| 操作 | 默认方式 |
| --- | --- |
| 显示 / 隐藏面板 | Ctrl+Shift+V |
| 驾驶 / 操作模式 | 面板显示时右键；备用键 Ctrl+Shift+E |
| 返回驾驶 | 右键、ESC 或“返回驾驶” |
| 切换页面、搜索、筛选、关注 | 先右键进入操作，再点击面板 |
| 移动、缩放 | 操作模式拖动标题栏或窗口边缘 |
| 透明度、字号、热键、账号 | 右键后点底部“设置” |
| 修改快捷键 | 点击对应快捷键按钮 → 按下新按键 → 应用并保存 |
| 取消快捷键录制 | ESC 或“取消录制”，保留原绑定 |
| 紧凑尺寸 | 设置 → 紧凑布局 620 × 480 → 应用并保存 |

如果游戏或 TMP 的其他界面已经占用输入，面板会拒绝进入操作并提示先关闭该界面。驾驶模式不请求鼠标、不锁定驾驶输入。使用的都是官方 Input 事件与锁接口，不替换游戏窗口过程。

字符输入使用 UTF-32 转接及 IME 合成/候选框定位，离开编辑时尝试恢复原先的 IME 位置。中文输入法的合成行为、候选框和各种屏幕模式仍未做实机验收。

## 配置和数据

配置文件为 `%LOCALAPPDATA%\TruckCompanionOverlay\settings.json`。第一次运行且没有该文件时，会读取旧独立面板的账号、关注和偏好；窗口位置按游戏视口重新初始化，不覆盖旧配置。

0.2.1 保留已有尺寸、透明度、字号和快捷键；没有任何旧配置时默认使用 620×480。DLL 面板高度范围为 360–1200（独立 EXE 仍为 600–1200）；尺寸会按游戏视口缩放。大字号或窄窗口的玩家表格可横向滚动，底部导航固定在内容区域外。

位置使用视口相对坐标，配置保存由工作线程执行。字号/尺寸等更改按“应用并保存”提交；关注和拖动位置会自动排队保存。关闭或热重载前，可在设置页查看是否已保存。极短时间内连续重载可能放弃旧实例尚未提交的设置，旧实例不能覆盖新实例的配置。

附近数据每 100ms 最多采样 32 人，活动采样池上限 512，输出最多 256 人；每次采样通过公开 `GetPlayerByID()` 获取当次句柄，不保存 SDK 实体对象供下次调用。初始成员快照及由事件触发的成员核对最多每五秒一次。失效/过期条目不作为当前速度展示，超出容量明确计数。

资料请求仅访问固定 HTTPS 主机 `api.truckersmp.com`，至少间隔三秒；个人资料/VTC 成功缓存 15 分钟，服务器约一分钟更新。失败和 429 限频显示状态；同 VTC 筛选会等待已确认的公开资料，不能从昵称/标签猜车队。

网络采用工作线程内的同步 WinHTTP、每阶段超时和协作式停止。SDK 关闭回调只发停止信号，不等待 HTTP；线程持有本 DLL 引用，等底层调用返回后自行清理。这里不承诺停止信号能立即打断正在执行的 Windows 网络调用。

## 兼容与限制

- 目前仅针对 **TMP + DX11**；OpenGL 暂停绘图并写入 TMP 日志。普通单机 ETS2 仅有 SCS 遥测，没有本方案所需的 TMP Render/Input 回调。
- 官方 Render 回调提供设备和帧边界，界面由 Dear ImGui 自绘，不使用 TMP 原生控件或资源覆盖，不扫描客户端地址，不挂钩 Present/SwapChain，不包含 MinHook。
- 后端保存恢复修改过的图形状态，补齐 HS/DS/CS shader 和 class instance，并释放 GS class instance 引用。当前 render target 仅在单次回调中借用，不跨帧持有 backbuffer。
- SDK 调用仅发生在 SDK 回调线程；工作线程处理字体、网络和配置，不调用 SDK。面板隐藏时停止 UI 绘图。
- 进程内代码不再具备外部 EXE 的故障隔离。官方 SDK 版本、设备/输入接口或渲染模式变化仍可能需要适配，不能保证永久兼容。
- SDK 文档支持此扩展机制，不等于本项目已经得到工单的单独书面批准。没有发送工单或发布 GitHub Release。

## 停用与回退

仅停用：退出游戏后，把 `plugins\TruckCompanionOverlay.dll` 移到 plugins 目录外即可。

回到本次安装前：退出游戏和相关工具，使用安装时输出的最新 receipt：

```powershell
.\install.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2' `
  -Restore 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2\companion-overlay-backups\<本次目录>\receipt.json'
```

回退归档当前 DLL，并恢复之前版本及本次迁出的旧模块；多次更新需按最新到最旧顺序回退。若恢复的是原生汉化/旧挂钩插件，文件恢复本身不代表当前兼容或许可。不要同时加载新 DLL 与旧 `PanelDataBridge.dll`。

## 开发与交付状态

源码在 `overlay/`；配置工具来自 `panel/settings.*`，数据模型来自 `panel/protocol.h`，依赖源码在 `vendor/`。`panel/` 的独立窗口版本保留作回退。Dear ImGui 1.92.9 的本地后端补丁说明位于 `vendor/imgui-1.92.9/COMPANION-NOTES.md`。

```powershell
.\overlay\build.ps1 -ScsSdk 'X:\scs-sdk-1.16\include'
.\overlay\package.ps1
```

构建脚本仅编译/链接 DLL，打包脚本仅整理文件和 ZIP。Windows x64 / MSVC 编译已完成。遵守用户要求，没有执行测试、烟测、SHA 校验，也没有启动游戏或加载 DLL 验收。实际显示、输入、中文 IME、设备重建、全屏、插件重载和帧时间仍是未执行项目。

SCS SDK 保持为外部依赖，不提交到仓库；`-ScsSdk` 指定包含 `scssdk_telemetry.h` 的 include 目录，省略时使用本地忽略目录 `vendor/scs`。构建记录所用 SDK 路径，打包按该记录保留声明。TMP SDK、ImGui 和 PicoJSON 源码及许可证随仓库维护。

参考：[官方 Render](https://docs.truckersmp.com/client-sdk/modules/render)、[Input](https://docs.truckersmp.com/client-sdk/modules/input)、[版本兼容](https://docs.truckersmp.com/client-sdk/concepts/versioning)、[前期完整探索](../panel/INPROCESS-OVERLAY-RESEARCH.md)。
