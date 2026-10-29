"""Shared local-file checks. Nothing in this module contacts a service."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import uuid

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1]))
CORE_SHA = '98033a4939575cd5c961ea052b7f831a7313bd40390f400d222b866bc48890ce'
UI_SHA = '455d1ec17ff6c6b04e2235b2aa048bafa73521471ba975729edd89136ef76551'
CLIENT_SHA = '7418e39627ace5b7d593a98586ea6397157ad6c408e9039bbbc8fba0fc10b029'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def plain(path, missing=False):
    require('..' not in Path(path).parts, 'Parent path segments refused')
    path = Path(path).absolute()
    for item in (path, *path.parents):
        try:
            info = item.lstat()
        except FileNotFoundError:
            require(missing, 'Missing path: ' + str(item))
            continue
        require(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400,
                'Reparse/link path refused: ' + str(item))
    return path


def atomic_json(path, value):
    path = plain(path, missing=True)
    temporary = path.with_name(path.name + '.new-' + uuid.uuid4().hex)
    try:
        with temporary.open('x', encoding='utf-8', newline='\n') as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def write_output(path, data):
    path = plain(path, missing=True)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        require(path.read_bytes() == data, 'Output differs; use a new output directory: ' + str(path))
        return
    with path.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def active_apps():
    require(os.name == 'nt', 'Installation is supported on Windows only')
    command = "@(Get-Process -Name eurotrucks2,amtrucks,TruckersMP,'TruckersMP-Launcher','TruckersMP Launcher' -ErrorAction SilentlyContinue) | ForEach-Object { $_.ProcessName + ':' + $_.Id }"
    powershell = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    result = subprocess.run([str(powershell), '-NoProfile', '-NonInteractive', '-Command', command],
                            capture_output=True, text=True, timeout=20,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    require(result.returncode == 0, 'Could not check game/TMP processes')
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def closed(processes=active_apps):
    apps = processes()
    require(not apps, 'Exit the game and TMP launcher first: ' + ', '.join(apps))


def read_json(path):
    def unique(items):
        result = {}
        for key, value in items:
            require(key not in result, 'Duplicate JSON property: ' + key)
            result[key] = value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique)
