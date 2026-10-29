# TruckersMP 简体中文界面补丁

独立的汉化源码项目，包含 Tab 玩家面板、公交子窗口、货运静态文案和实验性 Settings 汉化。
不依赖 TruckersMP SDK。原生 Settings 模块使用 SCS 的插件入口加载，并对指定版本的 TMP 界面函数安装钩子。

| 部分 | 范围 |
| --- | --- |
| Tab | 玩家面板、列表、右键菜单的中文与紧凑样式 |
| 公交 | 调度窗口中文、标题和适配样式 |
| 货运 | 17 处静态标题、按钮和复选框；保留动态内容与原有动作 |
| Settings（实验性） | 常规、声音、游戏、图形、界面五页；52 个设置标题、27 处说明、9 组静态选项，以及页签与底部按钮 |

仓库只发布翻译数据、差异变换、构建与恢复工具和相关测试。客户端文件、完整资源、字体、生成的 DLL、机器码测试夹具及本机日志均不随源码发布。
原工具箱的车辆标签、遥测、警报、独立面板和其他游戏功能不在此项目中。

## 支持范围与验证边界

仅针对已核查的 Windows x64 / ETS2 / TMP **0.7.9.2** 客户端：

```text
core_ets2mp.dll SHA-256
98033a4939575cd5c961ea052b7f831a7313bd40390f400d222b866bc48890ce
```

同时校验原始 `data/shared/ui.mp`、`data/shared/client.mp` 和相关资源条目。
版本号相同但哈希不同也会拒绝构建；原生模块还检查运行时指令与对象布局。请勿关闭校验或直接将偏移用于新版本。

本次独立提取完成了离线构建和测试。Settings **尚未完成游戏内验收**；离线字符、内存所有权和绑定测试无法证明实际加载、显示、性能或联网兼容性。
本项目非官方项目，未获 TruckersMP 认证，也不承诺不会触发客户端检测或处罚。使用原生钩子模块前请核对官方现行规则。

## 环境准备

- Python 3.11 或更高版本。
- 本机合法安装的匹配版本 TMP / ETS2。
- 自行提供含所需中文字形、允许相应修改与使用的独立 TrueType 字体。
  当前 cmap 工具支持 Unicode 格式 4；不支持 TTC、CFF 或带其他 cmap 格式的字体。
- 构建 Settings 还需要 Visual Studio C++ x64 Build Tools、Windows SDK 和
  [SCS Telemetry SDK 1.16](https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip)。
  `$ScsSdk` 指向包含 `scssdk_telemetry.h` 的目录。SDK 不随仓库提供。

```powershell
git clone https://github.com/sakuradairong/truckersmp-zh-cn.git
cd truckersmp-zh-cn
python -m venv .venv
$Python = '.\.venv\Scripts\python.exe'
& $Python -m pip install -r requirements-build.txt

$TmpRoot = Join-Path $env:APPDATA 'TruckersMP\installation'
$GameRoot = 'X:\SteamLibrary\steamapps\common\Euro Truck Simulator 2'  # 修改为实际位置
$Font = 'X:\Fonts\LicensedChineseFont.ttf'                            # 自行提供
$ScsSdk = 'X:\scs-sdk-1.16\include'                                   # 修改为实际位置
```

## 离线验证与构建

这些命令只读取本机输入并写入仓库的 `build/`，不会启动游戏或连接 TMP 服务器。

```powershell
& $Python -B -m unittest discover -s tests -p 'test_*.py' -v
& $Python -B tools/check_characters.py --font $Font
& $Python -B tools/resources.py build --tmp-root $TmpRoot --font $Font
& $Python -B tools/resources.py verify --tmp-root $TmpRoot

# 可选：构建 Settings，并运行隔离的原生测试
.\build-settings-zh.ps1 -Python $Python -TmpRoot $TmpRoot -ScsSdk $ScsSdk
```

资源产物位于 `build/resources/`；Settings 产物位于 `build/native-settings-zh/`。
若更换字体或修改资源源码，使用新的资源输出目录，例如 `--output build/resources-v2`。
构建工具不会覆盖内容不同的现有产物。

货运窗口的部分原生控件不能正确将 UTF-8 转成宽字符，因此仅对这 17 处静态文案使用 ASCII 码位与本地字体 cmap 映射。
专用字体只用于这些控件，保留原始动态文本、控件标识、选项和动作；未覆盖的动态条目与提示仍可能显示英文。
Tab / 公交使用原有 RML 界面；Settings 使用 TMP 自身的宽字符串赋值函数，并保留原始窄字符串标识与设置值。

## 安装与恢复

先正常退出 ETS2 和 TMP 启动器。工具会检查进程、拒绝链接/重解析点、备份现有目标并核对写入哈希。
安装只操作下列声明的文件；保留 `build/` 中的安装记录与备份才能恢复。

```powershell
& $Python -B tools/resources.py install --tmp-root $TmpRoot

# 可选：安装原生 Settings。需要先有上述资源/中文字体。
# 如 plugins 目录不存在，先自行创建 $GameRoot\bin\win_x64\plugins。
& $Python -B tools/native_settings_package.py seal --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tests/native_settings_package_test.py
& $Python -B tools/native_settings_package.py install --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tools/native_settings_package.py verify --tmp-root $TmpRoot --game-root $GameRoot
```

资源包只写 `data/shared_mod/` 下的 `ui.mp`、`client.mp`、根目录与 `fonts/` 中的 `OfflineFreightZH.ttf`，
以及 `OpenSans.ttf`、`fonts/OpenSans.ttf`、`fonts/OpenSans-Semibold.ttf`、`fonts/RobotoMono.ttf` 四个字体别名。
原始 `data/shared/` 文件不变。Settings 仅安装 `bin/win_x64/plugins/TmpSettingsZh.dll`，拒绝覆盖无安装记录的既有 DLL。

恢复时仍需退出游戏与启动器，按安装顺序反向执行：

```powershell
& $Python -B tools/native_settings_package.py restore --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tools/resources.py restore --tmp-root $TmpRoot
```

若使用了自定义 `--output`，后续验证、安装与恢复须传入同一个目录。
目标文件或备份被其他程序修改时，恢复会停止并保留文件，不会强行覆盖。
TMP 更新可能重置覆盖资源或使模块停用；重新核查版本后再构建，不要绕过校验。

## 目录与测试

- `profiles/`：五页 Settings 词条、货运翻译、七个 UI 资源的差异编辑；不包含完整客户端资源。
- `src/settings_zh.cpp`：限制作用范围、版本与 ABI 校验的原生汉化模块。
- `tools/`：本地资源变换、字符检查、构建配置生成、安装与恢复。
- `tests/`：合成资源/事务测试，以及需要本机客户端输入生成夹具的原生测试。
- `vendor/minhook/`：保留原许可证的 x64 MinHook 源码子集。

独立提取验证：14 项资源/事务测试、9 项 Settings 安装恢复测试、1,069 项隔离原生检查均通过。
使用本机测试字体完成 369 个字符在四个字号下的 1,476 次栅格检查，以及货运映射的 348 次字形对照。
字体检查结果取决于实际提供的字体。原生测试在独立进程内重定向客户端片段的外部调用，未加载 TMP DLL 或访问游戏进程。
资源与事务测试可在不提供客户端文件的环境中运行；原生检查需本机提供指定客户端来生成隔离夹具。
发布内容可用 `python -B tools/check_publish_scope.py` 核查，仅允许声明的汉化源码进入 Git。

## 许可证

原创代码与翻译数据采用 [MIT](LICENSE)。第三方组件及外部资源的权利边界见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
`build/` 内生成的客户端派生资源、字体、机器码夹具与原生 DLL 不属于本次公开发布内容。
