"""Offline graphical-installer backend. No game launch, network requests or SDK module."""
from __future__ import annotations
import json
import os
from pathlib import Path

from common import ROOT, digest, plain, read_json, require
from installer_store import Store, default_state_root
from installer_settings import SettingsStore, bundled as settings_bundle, game_directory
import settings_compatibility
import resources

VERSION = '0.3.0-preview.3'
FONT_FILES = {'regular': 'TMPZHSans-Regular.ttf', 'semibold': 'TMPZHSans-SemiBold.ttf', 'mono': 'TMPZHMono-Regular.ttf',
              'settings_regular': 'TmpSettingsZH.ttf', 'settings_semibold': 'TmpSettingsZH-Semibold.ttf',
              'freight_regular': 'TmpFreightZH.ttf', 'freight_semibold': 'TmpFreightZH-Semibold.ttf'}
LICENSE_FILES = {'NotoSansSC-OFL.txt', 'NotoSansMono-OFL.txt'}


def bundled_fonts(assets=None):
    folder = plain(Path(assets) if assets else ROOT / 'assets')
    manifest = read_json(plain(folder / 'font-bundle.json'))
    require(manifest.get('schema') == 1 and set(manifest.get('files', {})) == set(FONT_FILES.values()) | LICENSE_FILES,
            'Bundled font scope differs')
    for name, expected in manifest['files'].items():
        require(digest(plain(folder / name)) == expected, 'Bundled font or license differs: ' + name)
    return {role: folder / name for role, name in FONT_FILES.items()}


def discover(appdata=None):
    roaming = Path(appdata or os.environ.get('APPDATA', ''))
    if str(roaming).startswith(('\\\\', '//')):
        return [], ['请选择本地磁盘上的 TMP 安装目录。']
    options = roaming / 'TruckersMP/launcher-options.json'
    candidates, notes = [], []
    if options.is_file():
        try:
            configuration = json.loads(options.read_text(encoding='utf-8-sig'))
            configured = configuration.get('installPath')
            if isinstance(configured, str) and configured.strip():
                # Do not resolve relative paths against the launcher's arbitrary current directory.
                candidate = Path(os.path.expandvars(configured))
                if candidate.is_absolute() and not str(candidate).startswith(('\\\\', '//')):
                    candidates.append(candidate)
        except (OSError, ValueError, TypeError):
            notes.append('启动器配置无法读取，可手动选择 TMP 安装目录。')
    candidates.append(roaming / 'TruckersMP/installation')
    seen, existing = set(), []
    for candidate in candidates:
        key = str(candidate.absolute()).casefold()
        if key not in seen and (candidate / 'core_ets2mp.dll').is_file():
            existing.append(candidate.absolute())
            seen.add(key)
    return existing, notes


def discover_game(appdata=None):
    roaming = Path(appdata or os.environ.get('APPDATA', ''))
    if str(roaming).startswith(('\\\\', '//')):
        return [], []
    candidates, notes = [], []
    options = roaming / 'TruckersMP/launcher-options.json'
    if options.is_file():
        try:
            plain(options)
            value = json.loads(options.read_text(encoding='utf-8-sig')).get('games', {}).get('ets2', {}).get('path', '')
            if isinstance(value, str) and value and Path(value).is_absolute():
                path = Path(value)
                candidates.append(path.parents[2] if path.name.casefold() == 'eurotrucks2.exe' else path)
        except (ValueError, OSError, IndexError, AttributeError):
            notes.append('无法读取 ETS2 路径，请手动选择游戏目录。')
    if os.name == 'nt' and appdata is None:
        import winreg
        for hive, key, name in (
            (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 227300', 'InstallLocation'),
            (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 227300', 'InstallLocation')):
            try:
                with winreg.OpenKey(hive, key) as handle:
                    candidates.append(Path(winreg.QueryValueEx(handle, name)[0]))
            except OSError:
                pass
    found = []
    for candidate in candidates:
        try:
            valid = game_directory(candidate)
            if valid not in found:
                found.append(valid)
        except (ValueError, OSError):
            continue
    return found, notes


def inspect(tmp_root, state_root=None, *, game_root=None, include_settings=False, assets=None):
    require(not str(tmp_root).startswith(('\\\\', '//')), 'Use local disk paths, not network shares')
    tmp_root = plain(Path(tmp_root).absolute())
    compatibility, detail = True, ''
    try:
        resources.check_compatibility(tmp_root)
    except (ValueError, OSError) as exc:
        compatibility, detail = False, str(exc)
    store = Store(tmp_root, state_root)
    status = store.status()
    settings = SettingsStore(tmp_root, state_root).status()
    if include_settings:
        try:
            game = game_directory(game_root or settings.get('game_root'))
            _, profile = settings_bundle(assets)
            settings_compatibility.check_core(tmp_root / 'core_ets2mp.dll', profile)
            require(not settings['can_restore'] or settings.get('game_root') == str(game),
                    'Restore Settings before changing the ETS2 directory')
        except (ValueError, OSError) as exc:
            compatibility, detail = False, str(exc)
    return dict(compatible=compatibility, detail=detail, settings=settings,
                **dict(status, resource_can_restore=status['can_restore'],
                       can_restore=status['can_restore'] or settings['can_restore']))


def install(tmp_root, assets=None, state_root=None, progress=lambda message: None, *,
            game_root=None, include_settings=False):
    if include_settings:
        status = inspect(tmp_root, state_root, game_root=game_root, include_settings=True, assets=assets)
        require(status['compatible'], status['detail'])
        require(status['settings']['state'] in ('not_installed', 'restored', 'installed'),
                'An interrupted Settings operation must be restored first')
        game_root = game_root or status['settings'].get('game_root')
    fonts = bundled_fonts(assets)
    result = Store(tmp_root, state_root).install(fonts['regular'], fonts['semibold'], fonts['mono'], VERSION, progress,
                                               settings_fonts=(fonts['settings_regular'], fonts['settings_semibold']),
                                               freight_fonts=(fonts['freight_regular'], fonts['freight_semibold']))
    if include_settings:
        try:
            result['settings'] = SettingsStore(tmp_root, state_root).install(game_root, assets, VERSION, progress)
        except (ValueError, OSError) as exc:
            raise ValueError('Resource localization installed; Settings incomplete: ' + str(exc)) from exc
    return result


def restore(tmp_root, state_root=None, progress=lambda message: None):
    settings = SettingsStore(tmp_root, state_root)
    native = settings.restore(progress) if settings.status()['can_restore'] else None
    store = Store(tmp_root, state_root)
    result = store.restore(progress) if store.status()['can_restore'] else dict(state='restored')
    if native:
        result['settings'] = native
    return result


def friendly_error(exc):
    text = str(exc)
    if text.startswith('Resource localization installed; Settings incomplete: '):
        return '资源汉化已安装，但 Settings 安装未完成：' + friendly_error(ValueError(text.split(': ', 1)[1]))
    translations = (
        ('Choose the ETS2 game directory', '请先选择 ETS2 游戏目录（包含 bin\\win_x64\\eurotrucks2.exe）。'),
        ('Settings code/ABI changed', 'Settings 所需界面代码或控件结构已变化，需要重新适配：' + text.split(':')[-1]),
        ('Restore Settings before changing', '请先恢复已有 Settings 安装，再更换 ETS2 游戏目录。'),
        ('Bundled Settings', '安装器内的 Settings 模块校验失败，请重新下载完整安装器。'),
        ('Exit the game and TMP launcher first', '请先正常退出 ETS2、ATS 和 TMP 启动器，再重试。'),
        ('Incompatible resource', '所需界面资源或控件无法识别，已停止安装。详情：' + text[:250]),
        ('Incompatible freight', '所需货运控件无法识别，已停止安装。详情：' + text[:250]),
        ('Resource input changed', '准备期间原始界面资源发生了变化，请关闭 TMP 启动器后重新安装。'),
        ('Unsupported resource format', '界面资源格式无法识别，请确认所选目录是完整的 TMP 安装目录。'),
        ('Unexpected resource table', '界面资源目录结构无法识别，请确认 TMP 客户端文件完整。'),
        ('Another installer is already running', '另一个安装器正在操作，请等待它完成。'),
        ('interrupted operation', '上次安装未完成。请先点击“恢复安装前状态”。'),
        ('Installation interrupted', '安装中断，恢复备份已保留。请先恢复安装前状态，再启动游戏。'),
        ('Localization target changed', '汉化文件已被其他程序修改，为保护这些文件，操作已停止。'),
        ('Restore backup changed', '恢复备份损坏或被修改，已停止恢复并保留当前文件。'),
        ('Reparse/link', '所选路径包含链接或重解析点，请选择实际安装目录。'),
        ('Bundled font', '安装器内的字体文件校验失败，请重新下载完整安装器。'),
        ('No installation receipt', '此目录没有本安装器的恢复记录。'),
        ('Use local disk paths', '请选择本地磁盘上的实际目录，不支持网络共享路径。'),
        ('Missing path', '没有找到所需文件，请重新选择 TMP 安装目录。'),
    )
    if isinstance(exc, PermissionError):
        return '没有写入权限，或文件正被占用。请先关闭游戏和 TMP 启动器，并确认目录可写。'
    for needle, chinese in translations:
        if needle in text:
            return chinese
    return '操作未完成，已保留恢复记录。详细信息：' + text[:400]
