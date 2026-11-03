# 可选 TMP 官方 SDK 采集包

此附加包用于把官方 SDK 提供的附近玩家与当前账号数据送到独立桌面面板。它包含一个 **SCS + TMP SDK** 版本的 `PanelDataBridge.dll`，替换默认纯 SCS 版本。

窗口由独立 EXE 绘制，但采集 DLL 仍在游戏进程内运行。使用官方 SDK 并不等于本项目或旁车速度的外部展示已经得到工单的单独书面批准。此前针对旧原生插件的反馈不能理解成对新项目的许可；本项目未发送相关询问，文件安装情况以各机器的 receipt 为准。

## 使用步骤

先解压默认包，再把附加包中的 `optional-tmp-sdk` 目录放在默认包根目录，与 `collector-scs` 和 `install.ps1` 并列。完全退出游戏、面板和旧工具箱，然后执行：

```powershell
.\install.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2' -WithTmpSdk -DisableLegacy
```

该命令会备份并替换已有采集 DLL。两个版本文件名相同，不能改名同时安装。不要将 `TmpNativeSpeedBridge.dll` 用作本面板的采集器。插件加载位置遵循 TruckersMP 官方 SDK 的 `bin\win_x64\plugins` 安装约定。

## 实现边界

- 通过 `Session::Create` 协商 SDK 版本，检查所需模块可用性；协商失败返回失败，不回退到内部地址扫描。
- 仅使用官方只读 getter 与连接、流入、流出、渲染边界事件。SDK getter 均在 SDK 事件回调所在游戏线程调用；不在网络 `OnUpdate` 回调读取 getter。
- `OnPostRender` 每 100ms 最多采样 32 个玩家，不访问游戏渲染设备或提交绘图。
- 不使用聊天、输入注入、付费功能或需要额外授权的私有 intent。
- 昵称/标签不被当作正式车队。VTC 在面板的 HTTPS 工作线程查询公开 API。
- 数据失效时清空当前显示；本轮没有验证实际客户端的事件时序、卸载、FPS 或独占全屏表现。

## 官方参考

- [SDK 入门和安装](https://docs.truckersmp.com/client-sdk/getting-started)
- [Player / 范围事件](https://docs.truckersmp.com/client-sdk/modules/player)
- [车辆速度](https://docs.truckersmp.com/client-sdk/modules/vehicles-and-trailers)
- [线程约束](https://docs.truckersmp.com/client-sdk/concepts/threading)
- [版本兼容](https://docs.truckersmp.com/client-sdk/concepts/versioning)
- [Intent](https://docs.truckersmp.com/client-sdk/advanced/intents)
- [公开 Web API](https://docs.truckersmp.com/web-api/)

文档核对日期：2026-11-03。SDK 源码与 MIT 声明来自本仓库的 `vendor/tmp-sdk`，随包保留许可证。
