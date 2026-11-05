# Third-party notices

The MIT license covers this project's original code. It grants no rights to TruckersMP, ETS2, their client assets, trademarks, or user-supplied SDKs. This is an unofficial project.

- **PicoJSON v1.3.0**, commit `25fc213cca61ea22b3c2e4db8def9927562ba5f7`, parses settings and public API data. Source: <https://github.com/kazuho/picojson>. BSD 2-Clause license: [vendor/picojson/LICENSE.txt](vendor/picojson/LICENSE.txt).
- **TruckersMP GameClientSDK** is included under MIT in `vendor/tmp-sdk/`. Source: <https://github.com/TruckersMP/GameClientSDK>. Revision: [vendor/tmp-sdk/COMMIT](vendor/tmp-sdk/COMMIT); [license](vendor/tmp-sdk/LICENSE). Its public Render/Input/Player APIs are used by the panel; optional freight execution is separate internal native integration.
- **Dear ImGui 1.92.9**, MIT. Source: <https://github.com/ocornut/imgui>. [License](vendor/imgui-1.92.9/LICENSE.txt); [local DX11 backend changes](vendor/imgui-1.92.9/COMPANION-NOTES.md). The overlay does not install a WndProc or MinHook detour.
- **SCS Telemetry SDK 1.16** is an external prerequisite, obtained from <https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip>. Follow SCS's terms. Packages retain the selected SDK's root headers for their notices; the SDK source directory is excluded from Git.

Packages include these notices and the project license. Chinese system fonts are loaded locally, not redistributed. The optional profile generator uses Python's standard library. The Setup executable uses Windows/.NET Framework; it does not bundle Python or the retired localization font/runtime dependencies.

Full TMP binaries, resources, third-party panels and authorization caches are not distributed. Retired release packages retain their own historical license notices; removing obsolete source dependencies does not change those packages.
