# TruckersMP 简体中文界面补丁

**简体中文** | [English](README.en.md)

面向 Windows x64 / Euro Truck Simulator 2 的非官方 TruckersMP 界面汉化项目，提供中文界面、离线图形安装器和恢复工具。

制作：[RainySY](https://github.com/sakuradairong)。安装器和 TAB 个人资料卡显示作者署名，安装器提供 GitHub 主页入口。

当前源码候选版本：**0.3.0-preview.11**。Settings 汉化、货运动态中文修复、TAB 玩家筛选和个人资料依赖实验性原生模块；游戏内显示、联网兼容性和性能仍待验收。

`preview.8` 增加启动时自动地址定位，基于已审核的 TMP **0.7.9.3** 代码与 ABI 契约。满足契约的纯地址移动可使用同一 DLL 继续工作；指令、字段、调用关系或依赖检查不通过时，相应功能保留原版行为。安装器分别显示 Settings、货运和 TAB / 个人资料的可用状态，更新保留最初的恢复备份。

`preview.9` 允许直接更新发生变化的汉化文件。图形安装器在安装和恢复时默认先保存变更副本，再替换目标文件；首次恢复备份继续保留，兼容检查不变。

`preview.11` 修复 Windows 加载地址重定位时，部分指针被误当成固定字节校验，导致 Settings 和货运模块在游戏内被停用的问题。校验现在覆盖完整指针及其目标；原生构建增加真实客户端的三个加载基址回归检查，不执行客户端代码。

## 功能

| 模块 | 功能 |
| --- | --- |
| TAB 玩家面板 | 玩家列表、右键菜单中文与紧凑布局；全部、好友、车队、团队成员四类筛选 |
| 我的资料 | 自动显示当前玩家昵称、TMP 账号 ID、服务器 ID 和正式 VTC 车队；VTC 需联网查询公开 API |
| 公交调度 | 调度窗口、标题和按钮汉化及布局适配 |
| 货运 | 17 处静态标题、按钮和复选框汉化；城市、公司、货物、挂车和紧急程度的动态中文字体修复 |
| Settings（实验性） | 常规、声音、游戏、图形、界面五页；52 个设置标题、27 处说明、9 组静态选项，以及页签和底部按钮 |
| 安装与恢复 | 离线安装、旧版升级、变更备份后直接替换、中断恢复，并保留首次安装前的恢复点 |

## 安装与更新

从 [GitHub Releases](https://github.com/sakuradairong/truckersmp-zh-cn/releases) 下载已发布的 Windows x64 安装器 EXE，或解压 ZIP 后运行其中的 EXE。普通用户无需安装 Python、编译器、SDK 或另行准备字体。

1. 正常退出 ETS2、ATS 和 TruckersMP 启动器。
2. 运行安装器，确认 TMP 安装目录和 ETS2 游戏目录；也可手动选择。
3. 如需完整功能，保留勾选 **同时安装原生功能：Settings、货运中文、玩家筛选和个人资料（实验性）**。取消勾选时只安装静态资源汉化。
4. 点击 **安装汉化**；已有安装时点击 **更新 / 检查汉化**。完成后重新启动游戏。

原生模块安装到 ETS2 的 `bin/win_x64/plugins/TmpSettingsZh.dll`，重启游戏后加载。安装器分别显示资源和原生模块的安装状态，部分安装失败时会保留具体错误。

旧版 0.2.x 安装器不包含 Settings。设置页仍为英文时，使用包含原生模块的新版安装器，并确认 ETS2 游戏目录。

安装器包含允许修改和再分发的 Noto 派生字体及完整 SIL OFL 1.1 许可，详见 [字体说明](docs/FONTS.md)。EXE 尚未进行代码签名；可使用随包提供的 SHA-256 校验下载完整性。

### 恢复安装前状态

退出游戏和启动器，重新运行安装器，点击 **恢复安装前状态**。

- 备份与恢复记录保存在 `%LOCALAPPDATA%\TruckersMPZH`。删除或重新下载 EXE 不影响恢复，请保留该目录。
- 升级保留首次安装前的恢复点，包括当时已有的覆盖资源。
- 图形安装器默认直接替换发生变化的目标文件；替换前保存当前内容，保留首次安装前的恢复点。“打开备份目录”中的 `replaced-files` 保存变更副本及原路径记录。
- 原始备份损坏、变更副本无法保存或备份期间文件再次变化时，操作会停止，避免丢失恢复依据。
- 安装中断后可重新运行安装器恢复；客户端资源已不兼容时，已有安装记录仍可用于恢复。

## TAB 玩家筛选

| 页签 | 筛选依据 |
| --- | --- |
| 全部 | 所有符合当前搜索条件的玩家 |
| 好友 | 客户端原生好友标记 `player.friend` |
| 车队 | 客户端原生同车队标记 `player.company_buddy` |
| 团队成员 | TruckersMP 工作人员标记 `player.staff` |

同一玩家可以同时匹配多个分类。搜索和历史模式与当前分类叠加，人数显示为 **匹配数 / 筛选前搜索结果数**。

切换分类会回到列表顶部并清除旧选择；重新打开 TAB 或点击当前分类会保留滚动位置。未安装或未加载原生模块、筛选代码不兼容或处于离线状态时，筛选条保持隐藏。

筛选作用于搜索后的完整界面候选列表，随后沿用原生排序与滚动裁剪，保留固定 21dp 行高；在线和历史源列表不变。

## TAB 我的资料

资料卡显示当前玩家的昵称、TMP 账号 ID 和本次服务器分配的 ID。它只读在线源列表中的本车记录，不使用已选玩家、搜索结果或历史记录，因此不会随列表筛选切换成其他玩家。缺少本车数据或原生模块时显示等待或更新提示。

正式 VTC 车队通过 `https://api.truckersmp.com/v2/player/<TMP账号ID>` 查询，展示车队名和 VTC ID；玩家自定义标签不作为车队名称。查询只发送公开 TMP 账号 ID，不上传昵称、Steam ID 或其他游戏数据，不保存个人资料到磁盘。安装器仍可离线运行。

请求在后台工作线程执行，成功结果在面板使用期间缓存 5 分钟；失败至少等 1 分钟重试，限流至少等 2 分钟，任意两次请求间隔至少 5 秒。关闭面板、离线或启用客户端匿名模式会清空展示并停止后续查询；未完成请求的旧结果不会用于新会话。请求失败和“未加入 VTC”分别显示。相关代码或字段布局无法验证时，个人资料和玩家筛选会停用。

## 兼容性与验证状态

资源汉化按控件标识、原文和资源结构识别目标，不依赖固定字符偏移、条目顺序或条目总数。必要控件缺失、重复或格式无法识别时，安装器会停止写入并说明原因。

原生模块检查实际使用的代码、调用关系和控件结构。兼容性判断不使用 TMP 版本号、整个 DLL 哈希或精确映像大小作为白名单；相关结构变化时，对应功能会停用。SHA-256 仍用于下载、字体、备份完整性和操作期间的文件变化检查。

安装器与游戏内 DLL 共用同一个只读原生检查器。启动时解析一次，地址保存在进程内存中，不逐帧扫描，不联网下载偏移表。校验包含代码特征、引用关系、虚表、直接辅助函数、库函数入口和数据区域；这仍是有边界的 ABI 检查，不能保证任意未来版本兼容。客户端资源变化仍需重新检查和生成覆盖包。

| 验证项目 | 当前记录 |
| --- | --- |
| Python / Lua 自动测试 | 99 项通过，含 5 项真实 PE 数据测试；不执行客户端代码 |
| 原生隔离检查 | 47,243 项通过，另有 17 项定位检查；覆盖字符串所有权、生命周期、17 个钩子跳板和非均匀地址迁移 |
| 安装器隔离验证 | 已有候选构建完成实际 EXE 安装、重复安装、升级、中文路径和恢复检查；未修改真实游戏安装 |
| 游戏内验收 | 尚未完成；离线结果不能证明实际加载、显示、联网兼容性或性能 |

货运动态字库覆盖当前基础游戏简体中文使用的 1,765 个码位，检查字号为 11、14、18、21px。更大字号、模组新增字符和补充平面字符不在当前验证范围。静态货运文案使用专用 ASCII 映射字体，动态文字保留正常 Unicode；未翻译的内容仍可能显示英文。字体覆盖与图集容量说明见 [docs/FONTS.md](docs/FONTS.md)。

本项目未经 TruckersMP 官方认证，不保证原生模块不会触发客户端检测或处罚。使用前请核对官方现行规则。

## 开发

### 环境准备

- Windows x64、Python 3.11 或更高版本。
- 本机合法安装的 TMP / ETS2，且所需客户端代码和资源结构可识别。
- 构建原生模块需要 Visual Studio C++ x64 Build Tools、Windows SDK，以及 [SCS Telemetry SDK 1.16](https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip)。
- `$ScsSdk` 必须指向包含 `scssdk_telemetry.h` 的 include 目录。SDK 不随仓库提供；本项目只使用 SCS 插件加载入口，不依赖 TruckersMP SDK。

在 PowerShell 中运行，并将示例目录替换为实际位置：

```powershell
git clone https://github.com/sakuradairong/truckersmp-zh-cn.git
cd truckersmp-zh-cn
python -m venv .venv
$Python = '.\.venv\Scripts\python.exe'
& $Python -m pip install -r requirements-installer.txt

$TmpRoot = Join-Path $env:APPDATA 'TruckersMP\installation'
$GameRoot = 'X:\SteamLibrary\steamapps\common\Euro Truck Simulator 2'
$ScsSdk = 'X:\scs-sdk-1.16\include'
```

### 构建安装器

```powershell
.\build-installer.ps1 -Python $Python -TmpRoot $TmpRoot -ScsSdk $ScsSdk
```

构建阶段从固定上游版本获取开源字体并校验 SHA-256，安装器运行时不联网。EXE、ZIP 和 `SHA256SUMS.txt` 输出到 `dist/`，包含所需运行库、字体和许可证。客户端资源在用户本机生成，不随安装器分发。

已有经过验证的字体产物时可使用 `-ReuseFonts`；已有编译并验证的原生模块时可再使用 `-ReuseSettings`。

### 离线检查

```powershell
& $Python -B -m unittest discover -s tests -p 'test_*.py' -v
& $Python -B tools/check_publish_scope.py --working-tree

# 构建原生模块并运行隔离检查
.\build-settings-zh.ps1 -Python $Python -TmpRoot $TmpRoot -ScsSdk $ScsSdk
```

构建和隔离检查只读取本机输入并写入仓库的 `build/`，不会启动游戏或连接 TMP 服务器。资源与事务测试可使用合成输入运行；原生检查需要本机客户端生成隔离夹具，不加载 TMP DLL，也不访问游戏进程。

### 手动构建、安装与恢复

以下流程适用于开发者。操作安装目录前，先正常退出游戏和启动器。

自行提供允许相应修改和使用、包含所需字形的独立 TrueType 字体，或使用安装器构建流程生成的字体。cmap 工具支持 Unicode 格式 4 / 12，并保留非 Unicode 与变体选择映射；不支持 TTC、CFF 或未实例化的可变字体。

```powershell
$Font = 'X:\Fonts\LicensedChineseFont.ttf'
& $Python -B tools/check_characters.py --font $Font
& $Python -B tools/resources.py build --tmp-root $TmpRoot --font $Font
& $Python -B tools/resources.py verify --tmp-root $TmpRoot
& $Python -B tools/resources.py install --tmp-root $TmpRoot

# 可选：安装原生模块。先安装上述资源与字体；如有需要，先创建 plugins 目录。
& $Python -B tools/native_settings_package.py seal --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tests/native_settings_package_test.py
& $Python -B tools/native_settings_package.py install --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tools/native_settings_package.py verify --tmp-root $TmpRoot --game-root $GameRoot
```

资源产物位于 `build/resources/`，原生模块产物位于 `build/native-settings-zh/`。更换字体或资源源码后，请使用新的输出目录，例如 `--output build/resources-v2`；后续验证、安装和恢复必须使用对应目录。构建工具拒绝覆盖内容不同的既有产物。

安装工具检查进程、拒绝链接与重解析点、备份现有目标并核对写入哈希。手动流程的恢复记录和备份位于 `build/`，请保留。

手动安装会在替换文件前保存恢复日志。若安装被强制中断，请使用原输出目录先执行下方恢复命令，再重新安装；恢复过程中断后可以重复执行同一命令。恢复旧版原生 DLL 依据原安装记录和 DLL 哈希，不要求当前版本新增的字体或资源，也不要求保留原 `package.json`。

| 安装位置 | 声明的文件 |
| --- | --- |
| `data/shared_mod/` | `ui.mp`、`client.mp`、`OpenSans.ttf`、`OfflineFreightZH.ttf` |
| `data/shared_mod/fonts/` | `OpenSans.ttf`、`OpenSans-Semibold.ttf`、`RobotoMono.ttf`、`OfflineFreightZH.ttf`、`TmpSettingsZH.ttf`、`TmpSettingsZH-Semibold.ttf`、`TmpFreightZH.ttf`、`TmpFreightZH-Semibold.ttf` |
| ETS2 `bin/win_x64/plugins/` | `TmpSettingsZh.dll` |

原始 `data/shared/` 文件保持不变。原生模块安装拒绝覆盖没有安装记录的既有 DLL。

恢复时退出游戏和启动器，按安装顺序反向执行：

```powershell
& $Python -B tools/native_settings_package.py restore --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tools/resources.py restore --tmp-root $TmpRoot
```

以上开发命令在目标或备份变化时仍停止恢复。图形安装器默认备份变更后直接替换目标。TMP 更新后可重新运行安装器检查并更新汉化；相关客户端结构变化时，原生功能需重新适配。

## 项目结构

| 路径 | 内容 |
| --- | --- |
| `profiles/` | 翻译词条、八个 UI 资源的差异、字库清单与兼容检查配置 |
| `src/` | 原生汉化模块和玩家筛选逻辑 |
| `ui/` | TAB 筛选的 Lua 界面扩展 |
| `tools/` | 资源变换、字体处理、构建、安装与恢复工具 |
| `tests/` | 合成资源、安装事务、Lua 与隔离原生检查 |
| `vendor/minhook/` | 保留原许可证的 x64 MinHook 源码子集 |
| [docs/FONTS.md](docs/FONTS.md) | 字体来源、许可、覆盖范围和图集验证 |

Git 仅包含声明的汉化源码，不包含完整客户端资源、官方二进制、生成的 DLL、机器码测试夹具或本机日志。

## 许可证

原创代码与翻译数据采用 [MIT](LICENSE)。第三方组件、SDK、客户端资源和字体的权利边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

安装器分发本项目编译的原生汉化 DLL 和开源派生字体，不分发完整客户端资源、官方 DLL 或机器码测试夹具。派生字体适用各自的 SIL OFL 1.1 许可。

### 0.3.0-preview.11：TMP 0.7.9.4

修复 0.7.9.4 更新后 `unmatched window` 导致 Settings/TAB 原生模块停用。100 个关键函数的完整指令模式保持一致，新版依赖规则单独审查并打包，保留 0.7.9.3 支持。字符串按自身终止符核对，避免相邻字符串调整造成误判。多份规则各自独立验证，不拼接不同版本的地址；对象字段、错误指针、重复入口和越界仍会停用相关功能。兼容检测仅读取客户端，不执行它。真实游戏画面验收与离线检查分开记录。
