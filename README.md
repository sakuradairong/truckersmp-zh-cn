# Truck Companion Overlay

**简体中文** | [English](README.en.md)

面向 Windows x64 / ETS2 / TruckersMP DX11 的非官方中文游戏内面板。作者：[RainySY](https://github.com/sakuradairong)。

当前源码版本 **0.2.11**。本仓库只维护 `overlay/`：附近玩家、本车遥测、个人资料/VTC、设置，以及可选的自选做货和普通/WoT 任务复制。

旧 TMP 资源替换、Settings 汉化 hook、MinHook 和独立 EXE 路线已移除。新版绘图和输入使用官方 SDK；现有货运执行仍为可选内部调用。删除旧路线不改变这一技术边界。

## 使用与迁移

- [面板安装、操作及构建](overlay/README.md)
- [旧路线迁移与恢复](overlay/MIGRATION.md)
- [自选做货与任务复制](overlay/FREIGHT-COPY.md)
- [提交与结果反馈调查](overlay/FREIGHT-FEEDBACK.md)
- [中文安装器](overlay/setup/INSTALL-PLAN.md)

0.2.11 区分调用返回、观察到任务和用户确认；主面板隐藏后保留只读反馈。无提示问题的具体运行时原因尚未确定。现有历史日志无法证明服务端收到请求，不把两分钟超时当成拒绝或取消。

## 构建

准备 MSVC x64 和 [SCS Telemetry SDK 1.16](https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip) 的 include 目录。默认构建不含内部货运执行：

```powershell
.\overlay\build.ps1 -ScsSdk 'X:\scs-sdk-1.16\include'
.\overlay\package.ps1
```

保留现有货运接入时，使用之前从已审查基准生成并保留的本地描述：

```powershell
.\overlay\build.ps1 -ScsSdk 'X:\scs-sdk-1.16\include' -NativeFreightProfile '<本地 freight_profile.h 路径>'
.\overlay\package.ps1
.\overlay\setup\build-share.ps1
```

也支持 `-NativeFreightCore '<已审查的 0.7.9.4 基准文件>'` 生成描述，需要 Python 标准库。不要从未知客户端重新学习规则。描述、SDK、客户端文件、构建输出和本机配置不提交到源码。

构建与打包入口只编译、整理文件，不执行测试、烟测或 SHA 校验，不自动安装、启动游戏或发布。编译不代表货运、输入及安装器已完成运行验收。

本项目 MIT 许可；依赖声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
