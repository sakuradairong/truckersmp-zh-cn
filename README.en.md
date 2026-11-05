# Truck Companion Overlay

[简体中文](README.md) | **English**

An unofficial Chinese in-game panel for Windows x64, ETS2 and TruckersMP DX11, by [RainySY](https://github.com/sakuradairong).

Current source version: **0.2.11**. `overlay/` is the only maintained application. It includes nearby players, truck telemetry, public profile/VTC information, settings and optional custom freight generation and normal/WoT job copying.

The resource replacement installer, Settings localization hooks, MinHook and external EXE implementation have been removed. Rendering and input use the public SDK. Optional freight execution still uses internal native calls; retiring localization hooks does not turn freight into a public SDK integration.

See the [panel guide](overlay/README.md), [migration and recovery guide](overlay/MIGRATION.md), [freight guide](overlay/FREIGHT-COPY.md), [submission investigation](overlay/FREIGHT-FEEDBACK.md) and [installer guide](overlay/setup/INSTALL-PLAN.md).

Version 0.2.11 distinguishes a returned native call, an observed job and user confirmation. A read-only notice remains after the main panel hides. Historical logs do not establish why the original prompt was absent, or whether the server received that request. A timeout is not a rejection or cancellation.

## Build

Install MSVC x64 and obtain the include directory of [SCS Telemetry SDK 1.16](https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip). The default build excludes native freight execution:

```powershell
.\overlay\build.ps1 -ScsSdk 'X:\scs-sdk-1.16\include'
.\overlay\package.ps1
```

To retain existing freight integration, reuse the locally generated header from the previously reviewed reference:

```powershell
.\overlay\build.ps1 -ScsSdk 'X:\scs-sdk-1.16\include' -NativeFreightProfile '<local freight_profile.h>'
.\overlay\package.ps1
.\overlay\setup\build-share.ps1
```

Alternatively, `-NativeFreightCore '<reviewed 0.7.9.4 reference file>'` generates the header using Python's standard library. Do not generate new contracts from an unknown client. SDKs, client files, profiles, local settings and build products remain excluded from Git.

Build and packaging scripts compile and assemble files only. They do not run tests, smoke tests or SHA verification, install plugins, launch games or publish releases. Compilation does not establish runtime acceptance.

Project license: MIT. See [third-party notices](THIRD_PARTY_NOTICES.md).
