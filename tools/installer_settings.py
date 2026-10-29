"""Persistent, reversible installation of the five-page Settings localization DLL."""
import os
from pathlib import Path
import re
import uuid

from common import ROOT, atomic_json, closed, digest, plain, read_json, require
from installer_store import Store, copy_checked
import settings_compatibility

DLL = 'TmpSettingsZh.dll'
ASSETS = (DLL, 'settings-compatibility.json', 'settings-bundle.json')


def bundled(assets=None):
    folder = plain(Path(assets) if assets else ROOT / 'assets')
    manifest = read_json(plain(folder / 'settings-bundle.json'))
    require(manifest.get('schema') == 1 and set(manifest.get('files', {})) == set(ASSETS[:2]) and
            manifest.get('five_pages') is True, 'Unknown Settings bundle')
    for name, expected in manifest['files'].items():
        require(digest(plain(folder / name)) == expected, 'Bundled Settings file differs: ' + name)
    return folder / DLL, read_json(folder / 'settings-compatibility.json')


def game_directory(path):
    require(path and not str(path).startswith(('\\\\', '//')), 'Choose the ETS2 game directory')
    path = plain(Path(path).absolute())
    require((path / 'bin/win_x64/eurotrucks2.exe').is_file(), 'Choose the ETS2 game directory')
    plain(path / 'bin/win_x64/eurotrucks2.exe')
    return path


class SettingsStore:
    def __init__(self, tmp_root, state_root=None, processes=None):
        kwargs = {} if processes is None else {'processes': processes}
        self.anchor = Store(tmp_root, state_root, **kwargs)
        self.directory = self.anchor.directory / 'settings'
        self.pointer = self.directory / 'current.json'

    def current(self):
        if not plain(self.pointer, missing=True).exists():
            return None, None
        pointer = read_json(self.pointer)
        transaction = pointer.get('transaction', '')
        require(pointer.get('schema') == 1 and re.fullmatch('[0-9a-f]{32}', transaction),
                'Invalid Settings installation pointer')
        package = plain(self.directory / 'packages' / transaction)
        receipt = read_json(plain(package / 'receipt.json'))
        require(receipt.get('schema') == 1 and receipt.get('package') == 'tmpzh-settings' and
                receipt.get('transaction') == transaction and receipt.get('tmp_root') == str(self.anchor.tmp_root) and
                receipt.get('state') in ('applying', 'installed', 'restoring', 'restored'),
                'Invalid Settings installation receipt')
        for key in ('installed_sha256', 'previous_sha256'):
            value = receipt.get(key)
            require((key == 'previous_sha256' and value is None) or isinstance(value, str) and
                    re.fullmatch('[0-9a-f]{64}', value), 'Invalid Settings receipt hash')
        require(isinstance(receipt.get('game_root'), str) and Path(receipt['game_root']).is_absolute(),
                'Invalid Settings game directory')
        self.target(receipt['game_root'])
        return package, receipt

    def target(self, game_root):
        require(not str(game_root).startswith(('\\\\', '//')), 'Choose the ETS2 game directory')
        root = plain(Path(game_root).absolute())
        require(not root.is_relative_to(self.anchor.state_root) and
                not self.anchor.state_root.is_relative_to(root), 'State must be separate from ETS2')
        target = plain(root / 'bin/win_x64/plugins' / DLL, missing=True)
        require(not target.exists() or target.is_file(), 'Settings target is not a regular file')
        return target

    def actual(self, game_root):
        target = self.target(game_root)
        return digest(target) if target.exists() else None

    def status(self):
        package, receipt = self.current()
        if receipt is None:
            return dict(state='not_installed', can_restore=False)
        state = receipt['state']
        if state in ('installed', 'restored'):
            key = 'installed_sha256' if state == 'installed' else 'previous_sha256'
            if self.actual(receipt['game_root']) != receipt[key]:
                state = 'changed'
        return dict(state=state, can_restore=receipt['state'] != 'restored', game_root=receipt['game_root'],
                    backup_directory=str(package))

    def install(self, game_root, assets=None, version='', progress=lambda message: None):
        with self.anchor.lock():
            closed(self.anchor.processes)
            game_root = game_directory(game_root)
            payload, compatibility = bundled(assets)
            core = plain(self.anchor.tmp_root / 'core_ets2mp.dll')
            core_before = digest(core)
            settings_compatibility.check_core(core, compatibility)
            expected = digest(payload)
            _, previous = self.current()
            if previous and previous['state'] != 'restored':
                require(previous['state'] == 'installed', 'An interrupted Settings operation must be restored first')
                require(str(game_root) == previous['game_root'], 'Restore Settings before changing the ETS2 directory')
                require(self.actual(game_root) == previous['installed_sha256'], 'Localization target changed: Settings')
                if previous['installed_sha256'] == expected:
                    return dict(state='already_installed', path=str(self.target(game_root)))
                self._restore(progress)
            transaction = uuid.uuid4().hex
            package = plain(self.directory / 'packages' / transaction, missing=True)
            package.mkdir(parents=True)
            target = self.target(game_root)
            previous_sha = self.actual(game_root)
            if previous_sha is not None:
                copy_checked(target, package / 'previous.dll', previous_sha)
            target.parent.mkdir(parents=True, exist_ok=True)
            stage = plain(target.with_name(target.name + '.tmpzh-' + transaction), missing=True)
            copy_checked(payload, stage, expected)
            receipt = dict(schema=1, package='tmpzh-settings', transaction=transaction, state='applying',
                           tmp_root=str(self.anchor.tmp_root), game_root=str(game_root), version=version,
                           installed_sha256=expected, previous_sha256=previous_sha)
            closed(self.anchor.processes)
            require(self.actual(game_root) == previous_sha and digest(core) == core_before,
                    'Localization target changed while preparing Settings')
            atomic_json(package / 'receipt.json', receipt)
            atomic_json(self.pointer, dict(schema=1, transaction=transaction))
            try:
                progress('正在安装 Settings 五页汉化…')
                require(self.actual(game_root) == previous_sha, 'Localization target changed: Settings')
                os.replace(stage, target)
                require(self.actual(game_root) == expected and digest(core) == core_before,
                        'Settings inputs changed during installation')
                atomic_json(package / 'receipt.json', dict(receipt, state='installed'))
            except Exception as exc:
                try:
                    self._restore(progress)
                except Exception as recovery:
                    raise ValueError('Installation interrupted; restore Settings before playing: ' + str(recovery)) from exc
                raise
            finally:
                if stage.is_file() and digest(stage) == expected:
                    stage.unlink()
            return dict(state='installed', path=str(target), backup_directory=str(package))

    def restore(self, progress=lambda message: None):
        with self.anchor.lock():
            return self._restore(progress)

    def _restore(self, progress):
        closed(self.anchor.processes)
        package, receipt = self.current()
        require(receipt is not None, 'No Settings installation receipt')
        target = self.target(receipt['game_root'])
        actual = self.actual(receipt['game_root'])
        previous, installed = receipt['previous_sha256'], receipt['installed_sha256']
        require(actual in (previous, installed), 'Localization target changed: Settings')
        if previous is not None:
            require(digest(plain(package / 'previous.dll')) == previous, 'Restore backup changed: Settings')
        atomic_json(package / 'receipt.json', dict(receipt, state='restoring'))
        progress('正在恢复 Settings 安装前状态…')
        closed(self.anchor.processes)
        if actual != previous:
            require(self.actual(receipt['game_root']) == installed, 'Localization target changed: Settings')
            if previous is None:
                target.unlink()
            else:
                stage = plain(target.with_name(target.name + '.tmpzh-restore-' + receipt['transaction']), missing=True)
                if stage.exists():
                    require(digest(stage) == previous, 'Restore staging file changed: Settings')
                else:
                    copy_checked(package / 'previous.dll', stage, previous)
                require(self.actual(receipt['game_root']) == installed, 'Localization target changed: Settings')
                os.replace(stage, target)
        require(self.actual(receipt['game_root']) == previous, 'Restore verification failed: Settings')
        atomic_json(package / 'receipt.json', dict(receipt, state='restored'))
        stage = plain(target.with_name(target.name + '.tmpzh-' + receipt['transaction']), missing=True)
        if stage.is_file() and digest(stage) == installed:
            stage.unlink()
        return dict(state='restored', path=str(target))
