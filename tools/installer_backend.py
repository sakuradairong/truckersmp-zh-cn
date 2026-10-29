"""Offline graphical-installer backend. No game launch, network requests or SDK module."""
from __future__ import annotations
import json
import os
from pathlib import Path

from common import ROOT, digest, plain, read_json, require
from installer_store import Store, default_state_root
import resources

VERSION = '0.2.0-preview.1'
FONT_FILES = {'regular': 'TMPZHSans-Regular.ttf', 'semibold': 'TMPZHSans-SemiBold.ttf', 'mono': 'TMPZHMono-Regular.ttf'}
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


def inspect(tmp_root, state_root=None):
    require(not str(tmp_root).startswith(('\\\\', '//')), 'Use local disk paths, not network shares')
    tmp_root = plain(Path(tmp_root).absolute())
    compatibility, detail = True, ''
    try:
        resources.inputs(tmp_root)
    except (ValueError, OSError) as exc:
        compatibility, detail = False, str(exc)
    store = Store(tmp_root, state_root)
    status = store.status()
    return dict(compatible=compatibility, detail=detail, **status)


def install(tmp_root, assets=None, state_root=None, progress=lambda message: None):
    fonts = bundled_fonts(assets)
    return Store(tmp_root, state_root).install(fonts['regular'], fonts['semibold'], fonts['mono'], VERSION, progress)


def restore(tmp_root, state_root=None, progress=lambda message: None):
    return Store(tmp_root, state_root).restore(progress)


def friendly_error(exc):
    text = str(exc)
    translations = (
        ('Exit the game and TMP launcher first', '请先正常退出 ETS2、ATS 和 TMP 启动器，再重试。'),
        ('Unsupported TMP input', '此 TMP 客户端文件与当前支持版本不匹配，已停止安装。请等待适配更新。'),
        ('Another installer is already running', '另一个安装器正在操作，请等待它完成。'),
        ('Restore the previous version before upgrading', '请先恢复安装前状态，再安装新版汉化。'),
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
