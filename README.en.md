# TruckersMP Simplified Chinese UI Patch

[简体中文](README.md) | **English**

An unofficial TruckersMP UI localization project for Windows x64 and Euro Truck Simulator 2, with Simplified Chinese interfaces, an offline graphical installer, and restore tools.

Created by [RainySY](https://github.com/sakuradairong). The installer and TAB profile card show this credit, and the installer links to the GitHub profile.

Current source candidate: **0.3.0-preview.11**. Settings localization, dynamic freight text fixes, TAB player filters, and the personal profile require the experimental native module. In-game rendering, online compatibility, and performance still need validation.

`preview.8` adds startup address resolution using reviewed TMP **0.7.9.3** code and ABI contracts. Pure address movement that satisfies these contracts can use the same DLL. Instruction, layout, call-relation, or dependency mismatches retain the original behaviour for affected features. The installer reports Settings, freight, and TAB/profile availability separately and preserves the original restore baseline.

`preview.9` allows changed localization files to be updated directly. By default, the graphical installer saves copies of changed files before replacing them during installation or recovery. The original restore backups and compatibility checks remain in place.

`preview.11` fixes Settings and freight being disabled after Windows rebases the loaded module: a partial pointer was incorrectly checked as fixed bytes. Rules now cover the complete pointer and validate its target. Native builds test the real client at three simulated load bases without executing client code.

## Features

| Component | Features |
| --- | --- |
| TAB player panel | Chinese player list and context menu, compact layout, and All, Friends, Company, and Staff filters |
| My profile | Automatic local-player nickname, TMP account ID, server ID, and official VTC; VTC lookup requires the public online API |
| Bus dispatcher | Chinese window text, headings, buttons, and layout adjustments |
| Freight | 17 translated static headings, buttons, and checkboxes; font fixes for dynamic city, company, cargo, trailer, and urgency text |
| Settings (experimental) | General, Sound, Gameplay, Graphics, and Interface pages; 52 setting labels, 27 descriptions, nine groups of static options, tabs, and footer buttons |
| Installation and recovery | Offline installation, upgrades, replacement after saving changed files, interrupted-operation recovery, and preservation of the original restore point |

## Installation and updates

Download a published Windows x64 installer EXE from [GitHub Releases](https://github.com/sakuradairong/truckersmp-zh-cn/releases), or extract the ZIP and run its EXE. End users do not need Python, a compiler, an SDK, or separately supplied fonts.

1. Exit ETS2, ATS, and the TruckersMP launcher normally.
2. Run the installer and confirm the TMP installation directory and ETS2 game directory. You can also select them manually.
3. For all features, leave **同时安装原生功能：Settings、货运中文、玩家筛选和个人资料（实验性）** selected: this installs Settings localization, freight text fixes, player filters, and the personal profile. Clearing it installs static localization resources only.
4. Click **安装汉化** (Install localization), or **更新 / 检查汉化** (Update / Check localization) for an existing installation. Restart the game when installation completes.

The native module is installed at `bin/win_x64/plugins/TmpSettingsZh.dll` inside the ETS2 directory and loads after restarting the game. The installer reports resource and native-module status separately and retains specific errors if installation is only partially successful.

Older 0.2.x installers did not include Settings. If Settings remains in English, use a newer installer that includes the native module and confirm the ETS2 directory.

The installer bundles modified Noto fonts permitted for modification and redistribution, together with the full SIL OFL 1.1 license. See the [font documentation](docs/FONTS.md). The EXE is currently unsigned; use the bundled SHA-256 checksums to verify download integrity.

### Restore the previous state

Exit the game and launcher, run the installer again, and click **恢复安装前状态** (Restore the previous state).

- Backups and recovery records are stored in `%LOCALAPPDATA%\TruckersMPZH`. Deleting or downloading the EXE again does not affect recovery. Keep this directory.
- Upgrades preserve the original restore point, including any override resources present before the first installation.
- The graphical installer replaces changed targets by default after saving their current contents. The original restore point remains intact. Use Open backup directory to find copies and original-path records under `replaced-files`.
- Operations stop if original backups are damaged, changed files cannot be saved, or targets change again during backup.
- Run the installer again to recover from an interrupted installation. Existing installation records can still be used to restore files when current client resources are incompatible.

## TAB player filters

| Tab in the Chinese UI | Filter |
| --- | --- |
| 全部 — All | Every player matching the current search |
| 好友 — Friends | The client's native friend marker, `player.friend` |
| 车队 — Company | The client's native same-company marker, `player.company_buddy` |
| 团队成员 — Staff | The TruckersMP staff marker, `player.staff` |

A player can match more than one category. Search and history mode combine with the selected category. The count shows **matching players / search results before category filtering**.

Changing categories returns the list to the top and clears the previous selection. Reopening TAB or clicking the active category preserves the scroll position. The filter bar stays hidden while offline, when the native module is absent or not loaded, or when the required filter code is incompatible.

Filtering applies to the complete UI candidate list after search, followed by native sorting and viewport clipping. The fixed 21dp row height is preserved, and the online and history source lists remain unchanged.

## TAB personal profile

The card reads the local player's nickname, TMP account ID, and current server ID from the live source list. It does not use the selected player, search results, or history, so changing filters cannot replace your identity with another player's. Missing data or an unavailable native module produces a waiting or update message.

The official VTC name and ID come from `https://api.truckersmp.com/v2/player/<TMP-account-ID>`. Player-defined tags are not treated as VTC names. Requests send only the public TMP account ID, without uploading nicknames, Steam IDs, or other game data; personal data is not persisted to disk. The installer itself still works offline.

Requests run on a background worker. Successful results are cached for five minutes while the panel is in use; failures retry after at least one minute and rate limits after at least two minutes, with at least five seconds between requests. Hiding the panel, going offline, or enabling the client's anonymized mode clears the display and stops further requests. Late results from old sessions are discarded. Failed lookups and confirmed absence of VTC membership have distinct messages. Unverified native code or field layouts disable the profile and player filters.

## Compatibility and validation

Resource localization identifies targets by control IDs, original text, and resource structure. It does not depend on fixed character offsets, entry order, or total entry counts. If a required control is missing, duplicated, or unrecognizable, the installer stops before writing and explains the reason.

The native module checks the code, call relationships, and control structures it actually uses. Compatibility checks do not whitelist TMP version numbers, whole-DLL hashes, or exact image sizes. Affected features stop when the required structures change. SHA-256 remains in use for downloads, fonts, backups, and file-change checks during an operation.

The installer and in-game DLL share one read-only native checker. Resolution runs once at startup and keeps addresses in process memory, without per-frame scanning or online offset downloads. Contracts cover instruction patterns, reference relations, vtables, direct helper functions, library entry blocks, and data regions. These bounded ABI checks do not guarantee compatibility with every future version. Changed client resources still need checking and rebuilding.

| Validation | Recorded result |
| --- | --- |
| Python / Lua automated tests | 99 passed, including five real-PE data tests; client code is never executed |
| Isolated native checks | 47,243 passed plus 17 resolver checks, covering ownership, lifecycle, 17 hook trampolines, and nonuniform address movement |
| Isolated installer validation | Existing candidate builds completed real EXE installation, repeat installation, upgrades, Unicode paths, and restore checks without modifying the real game installation |
| In-game acceptance | Outstanding; offline results do not establish actual loading, rendering, online compatibility, or performance |

The dynamic freight fonts cover the 1,765 code points used by the current base game's Simplified Chinese text and have been checked at 11, 14, 18, and 21px. Larger sizes, characters added by mods, and supplementary-plane characters are outside the current validation scope. Static freight captions use a dedicated ASCII-mapped font; dynamic text retains normal Unicode. Untranslated content may still appear in English. See [docs/FONTS.md](docs/FONTS.md) for coverage and atlas limits.

This project is not certified by TruckersMP and does not guarantee that the native module will avoid client detection or penalties. Check the current official rules before use.

## Development

### Prerequisites and setup

- Windows x64 and Python 3.11 or later.
- A legitimate local TMP / ETS2 installation with recognizable client code and resource structures.
- Visual Studio C++ x64 Build Tools, the Windows SDK, and [SCS Telemetry SDK 1.16](https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip) to build the native module.
- `$ScsSdk` must point to the include directory containing `scssdk_telemetry.h`. The SDK is not included in this repository. The project uses the SCS plugin loader entry point and does not depend on the TruckersMP SDK.

Run these commands in PowerShell and replace the example directories with your actual paths:

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

### Build the installer

```powershell
.\build-installer.ps1 -Python $Python -TmpRoot $TmpRoot -ScsSdk $ScsSdk
```

The build downloads open-source fonts from a pinned upstream revision and verifies their SHA-256 hashes. The installer works offline. The EXE, ZIP, and `SHA256SUMS.txt` are written to `dist/`, with the required runtime, fonts, and licenses. Client resources are generated on the user's machine and are not bundled in the installer.

Use `-ReuseFonts` when verified font artifacts are already available. Add `-ReuseSettings` only when the native module has already been built and validated.

### Offline checks

```powershell
& $Python -B -m unittest discover -s tests -p 'test_*.py' -v
& $Python -B tools/check_publish_scope.py --working-tree

# Build the native module and run isolated checks
.\build-settings-zh.ps1 -Python $Python -TmpRoot $TmpRoot -ScsSdk $ScsSdk
```

Builds and isolated checks read local inputs and write to the repository's `build/` directory. They do not start the game or connect to TMP servers. Resource and transaction tests can use synthetic inputs. Native checks require the local client to generate isolated fixtures; they do not load the TMP DLL or access a game process.

### Manual build, installation, and recovery

This workflow is intended for developers. Exit the game and launcher before modifying installation directories.

Supply a standalone TrueType font whose license permits the required modifications and use, with all necessary glyphs, or use fonts generated by the installer build. The cmap tools support Unicode formats 4 and 12 while preserving non-Unicode and variation-selector mappings. TTC, CFF, and uninstantiated variable fonts are unsupported.

```powershell
$Font = 'X:\Fonts\LicensedChineseFont.ttf'
& $Python -B tools/check_characters.py --font $Font
& $Python -B tools/resources.py build --tmp-root $TmpRoot --font $Font
& $Python -B tools/resources.py verify --tmp-root $TmpRoot
& $Python -B tools/resources.py install --tmp-root $TmpRoot

# Optional: install the native module after the resources and fonts above.
# Create the plugins directory first if it does not exist.
& $Python -B tools/native_settings_package.py seal --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tests/native_settings_package_test.py
& $Python -B tools/native_settings_package.py install --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tools/native_settings_package.py verify --tmp-root $TmpRoot --game-root $GameRoot
```

Resource artifacts are written to `build/resources/`, and native-module artifacts to `build/native-settings-zh/`. After changing a font or resource source, use a fresh output directory, such as `--output build/resources-v2`. Use the corresponding output directory for subsequent verification, installation, and recovery. Build tools refuse to overwrite existing artifacts with different content.

Installation tools check running processes, reject links and reparse points, back up existing targets, and verify written hashes. Keep the installation records and backups in `build/` when using this manual workflow.

Manual installation saves a recovery journal before replacing files. If installation is interrupted, run the recovery commands below with the original output directory before installing again. Interrupted recovery can be resumed by repeating the same command. Restoring an older native DLL uses its original installation record and DLL hash; it does not require newly added fonts or resources, or the original `package.json`.

| Installation location | Declared files |
| --- | --- |
| `data/shared_mod/` | `ui.mp`, `client.mp`, `OpenSans.ttf`, `OfflineFreightZH.ttf` |
| `data/shared_mod/fonts/` | `OpenSans.ttf`, `OpenSans-Semibold.ttf`, `RobotoMono.ttf`, `OfflineFreightZH.ttf`, `TmpSettingsZH.ttf`, `TmpSettingsZH-Semibold.ttf`, `TmpFreightZH.ttf`, `TmpFreightZH-Semibold.ttf` |
| ETS2 `bin/win_x64/plugins/` | `TmpSettingsZh.dll` |

Original `data/shared/` files remain unchanged. Native-module installation refuses to overwrite an existing DLL without an installation record.

To restore, exit the game and launcher and reverse the installation order:

```powershell
& $Python -B tools/native_settings_package.py restore --tmp-root $TmpRoot --game-root $GameRoot
& $Python -B tools/resources.py restore --tmp-root $TmpRoot
```

The development commands above still stop recovery when targets or backups change. The graphical installer saves changed targets before replacing them by default. After a TMP update, run the installer again to check and update localization. Native features need adaptation if the relevant client structures change.

## Project layout

| Path | Contents |
| --- | --- |
| `profiles/` | Translation catalogs, edits for eight UI resources, character lists, and compatibility profiles |
| `src/` | Native localization module and player-filter logic |
| `ui/` | Lua UI extension for TAB filters |
| `tools/` | Resource transforms, font processing, builds, installation, and recovery |
| `tests/` | Synthetic resources, installation transactions, Lua checks, and isolated native checks |
| `vendor/minhook/` | The x64 MinHook source subset with its original license |
| [docs/FONTS.md](docs/FONTS.md) | Font sources, licenses, coverage, and atlas validation |

Git contains only the declared localization source files. Full client resources, official binaries, generated DLLs, executable machine-code fixtures, and local logs are excluded.

## License

Original code and translation data are licensed under [MIT](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the rights and licensing boundaries of third-party components, SDKs, client assets, and fonts.

The installer distributes this project's compiled native localization DLL and open-source derived fonts. It does not include full client resources, official DLLs, or executable machine-code fixtures. Derived fonts remain under their respective SIL OFL 1.1 licenses.

### 0.3.0-preview.11: TMP 0.7.9.4

Adds a separately reviewed dependency profile for 0.7.9.4 while retaining the 0.7.9.3 canonical adapter. All 100 consumer instruction patterns are unchanged. Literal contracts exclude adjacent pooled strings. Profiles are validated independently; addresses from different profiles are never combined. Offline PE and ASLR checks do not establish in-game rendering or stability.
