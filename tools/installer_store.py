"""Persistent, journaled resource installation. Never restores over unknown user changes."""
from __future__ import annotations
from contextlib import contextmanager
import os
from pathlib import Path
import re
import shutil
import uuid

from common import active_apps, atomic_json, closed, digest, plain, read_json, require, sha
import resources

HEX64 = re.compile(r'[0-9a-f]{64}\Z')
HEX32 = re.compile(r'[0-9a-f]{32}\Z')


def default_state_root():
    require(os.name == 'nt' and os.environ.get('LOCALAPPDATA'), 'Windows LOCALAPPDATA is required')
    return Path(os.environ['LOCALAPPDATA']) / 'TruckersMPZH'


def copy_checked(source, target, expected):
    plain(source); plain(target, missing=True)
    created = False
    try:
        with Path(source).open('rb') as src, Path(target).open('xb') as dst:
            created = True
            shutil.copyfileobj(src, dst)
            dst.flush(); os.fsync(dst.fileno())
        require(digest(target) == expected, 'Staged resource hash differs')
    except BaseException:
        if created:
            Path(target).unlink(missing_ok=True)
        raise


class Store:
    def __init__(self, tmp_root, state_root=None, processes=active_apps):
        require(not str(tmp_root).startswith(('\\\\', '//')) and
                not str(state_root or '').startswith(('\\\\', '//')), 'Use local disk paths, not network shares')
        self.tmp_root = plain(Path(tmp_root).absolute())
        self.state_root = plain(Path(state_root or default_state_root()).absolute(), missing=True)
        require(not self.state_root.is_relative_to(self.tmp_root) and
                not self.tmp_root.is_relative_to(self.state_root), 'State must be separate from TMP')
        key = sha(str(self.tmp_root).casefold().encode('utf-8'))[:24]
        self.directory = self.state_root / 'installations' / key
        self.pointer = self.directory / 'current.json'
        self.processes = processes

    @contextmanager
    def lock(self):
        require(os.name == 'nt', 'Installer requires Windows')
        import msvcrt
        plain(self.state_root, missing=True).mkdir(parents=True, exist_ok=True)
        path = plain(self.state_root / 'installer.lock', missing=True)
        with path.open('a+b') as stream:
            if stream.tell() == 0:
                stream.write(b'0'); stream.flush()
            stream.seek(0)
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as exc:
                raise ValueError('Another installer is already running') from exc
            try:
                yield
            finally:
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)

    def current(self):
        if not plain(self.pointer, missing=True).exists():
            return None, None
        pointer = read_json(self.pointer)
        transaction = pointer.get('transaction', '')
        require(pointer.get('schema') == 1 and HEX32.fullmatch(transaction), 'Invalid installation pointer')
        package = plain(self.directory / 'packages' / transaction)
        receipt = read_json(plain(package / 'receipt.json'))
        require(receipt.get('schema') == 1 and receipt.get('package') == 'tmpzh-installer' and
                receipt.get('tmp_root') == str(self.tmp_root) and receipt.get('transaction') == transaction and
                receipt.get('state') in ('applying', 'installed', 'restoring', 'restored') and
                set(receipt.get('files', {})) in (set(resources.OUTPUTS), set(resources.PRE_FREIGHT_OUTPUTS),
                                                set(resources.LEGACY_OUTPUTS)),
                'Invalid installation receipt scope')
        for row in receipt['files'].values():
            require(isinstance(row, dict) and set(row) == {'previous_sha256', 'installed_sha256'},
                    'Invalid installation receipt row')
            require(isinstance(row['installed_sha256'], str) and HEX64.fullmatch(row['installed_sha256']) and
                    (row['previous_sha256'] is None or isinstance(row['previous_sha256'], str) and
                     HEX64.fullmatch(row['previous_sha256'])), 'Invalid receipt hash')
        return package, receipt

    def target(self, name):
        require(name in resources.OUTPUTS, 'Unknown installation target')
        target = plain(self.tmp_root / name, missing=True)
        require(not target.exists() or target.is_file(), 'Target is not a regular file')
        return target

    @staticmethod
    def backup_name(name):
        return sha(name.encode('utf-8')) + '.bin'

    def actual(self, name):
        target = self.target(name)
        return digest(target) if target.exists() else None

    def status(self):
        package, receipt = self.current()
        if receipt is None:
            return {'state': 'not_installed', 'can_restore': False}
        state = receipt['state']
        if state in ('installed', 'restored'):
            key = 'installed_sha256' if state == 'installed' else 'previous_sha256'
            if any(self.actual(name) != row[key] for name, row in receipt['files'].items()):
                state = 'changed'
        return dict(state=state, can_restore=receipt['state'] != 'restored', version=receipt.get('version', ''),
                    backup_directory=str(package / 'backups'))

    def install(self, font_regular, font_semibold, font_mono, version, progress=lambda message: None, *, settings_fonts=None, freight_fonts=None):
        with self.lock():
            closed(self.processes)
            package, previous = self.current()
            if previous is not None and previous['state'] != 'restored':
                require(previous['state'] == 'installed', 'An interrupted operation must be restored first')
                require(all(self.actual(name) == row['installed_sha256'] for name, row in previous['files'].items()),
                        'Localization target changed; restore or resolve changes first')
                try:
                    resources.verify(self.tmp_root, package, font_paths=(font_regular, font_semibold, font_mono) +
                                     (tuple(settings_fonts) if settings_fonts else ()) +
                                     (tuple(freight_fonts) if freight_fonts else ()))
                except (ValueError, OSError):
                    pass  # Rebuild changed resources/fonts or an older package schema before touching targets.
                else:
                    return dict(state='already_installed', files=len(resources.OUTPUTS))
            transaction = uuid.uuid4().hex
            package = plain(self.directory / 'packages' / transaction, missing=True)
            package.mkdir(parents=True)
            progress('正在检查中文字体并准备汉化文件…')
            resources.build(self.tmp_root, Path(font_regular), package,
                            semibold=Path(font_semibold), mono=Path(font_mono), settings_fonts=settings_fonts,
                            freight_fonts=freight_fonts)
            manifest = resources.verify(self.tmp_root, package)
            closed(self.processes)
            if previous is not None and previous['state'] == 'installed':
                progress('新版文件已准备好，正在保留原始恢复点并更新汉化…')
                self._restore(progress)
            backups = package / 'backups'
            backups.mkdir()
            rows = {}
            for index, name in enumerate(resources.OUTPUTS):
                target = self.target(name)
                previous_sha = self.actual(name)
                if previous_sha is not None:
                    copy_checked(target, backups / self.backup_name(name), previous_sha)
                rows[name] = dict(previous_sha256=previous_sha, installed_sha256=manifest['files'][name])
            receipt = dict(schema=1, package='tmpzh-installer', transaction=transaction,
                           tmp_root=str(self.tmp_root), version=version, state='applying', files=rows)
            atomic_json(package / 'receipt.json', receipt)
            atomic_json(self.pointer, dict(schema=1, transaction=transaction))
            try:
                progress('已保存恢复备份，正在安装汉化…')
                for name, row in rows.items():
                    target = self.target(name)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    plain(target, missing=True)
                    copy_checked(package / 'files' / name, self.stage(target, transaction), row['installed_sha256'])
                closed(self.processes)
                resources.verify(self.tmp_root, package)
                require(all(self.actual(name) == row['previous_sha256'] for name, row in rows.items()),
                        'Localization target changed while preparing; preserving user changes')
                for name, row in rows.items():
                    require(self.actual(name) == row['previous_sha256'], 'Localization target changed during install')
                    target = self.target(name)
                    os.replace(self.stage(target, transaction), target)
                    require(self.actual(name) == row['installed_sha256'], 'Installed resource hash differs')
                resources.verify(self.tmp_root, package)
                require(all(self.actual(name) == row['installed_sha256'] for name, row in rows.items()),
                        'Localization target changed after installation')
                atomic_json(package / 'receipt.json', dict(receipt, state='installed'))
            except Exception as exc:
                try:
                    self._restore(progress)
                except Exception as recovery_error:
                    raise ValueError('Installation interrupted; backups retained. Restore before playing. ' +
                                     str(exc) + '; recovery: ' + str(recovery_error)) from exc
                raise
            finally:
                self.cleanup_stages(receipt)
            return dict(state='installed', files=len(rows), backup_directory=str(backups))

    @staticmethod
    def stage(target, transaction):
        return target.with_name(target.name + '.tmpzh-' + transaction)

    def cleanup_stages(self, receipt):
        for name, row in receipt['files'].items():
            stage = plain(self.stage(self.target(name), receipt['transaction']), missing=True)
            if stage.is_file() and digest(stage) in (row['installed_sha256'], row['previous_sha256']):
                stage.unlink()

    def restore(self, progress=lambda message: None):
        with self.lock():
            return self._restore(progress)

    def _restore(self, progress):
        closed(self.processes)
        package, receipt = self.current()
        require(receipt is not None, 'No installation receipt was found')
        rows = receipt['files']
        # Validate every target and every backup before changing any file.
        for index, (name, row) in enumerate(rows.items()):
            require(self.actual(name) in (row['previous_sha256'], row['installed_sha256']),
                    'Localization target changed; preserving it: ' + name)
            if row['previous_sha256'] is not None:
                backup = plain(package / 'backups' / self.backup_name(name))
                require(digest(backup) == row['previous_sha256'], 'Restore backup changed; preserving files')
        progress('正在恢复安装前的文件…')
        atomic_json(package / 'receipt.json', dict(receipt, state='restoring'))
        closed(self.processes)
        for name, row in rows.items():
            actual = self.actual(name)
            require(actual in (row['previous_sha256'], row['installed_sha256']), 'Localization target changed during restore')
            if actual == row['previous_sha256']:
                continue
            target = self.target(name)
            if row['previous_sha256'] is None:
                target.unlink()
            else:
                backup = package / 'backups' / self.backup_name(name)
                staged = plain(target.with_name(target.name + '.tmpzh-restore-' + receipt['transaction']), missing=True)
                if staged.exists():
                    require(staged.is_file() and digest(staged) == row['previous_sha256'],
                            'Restore staging file changed; preserving it')
                else:
                    copy_checked(backup, staged, row['previous_sha256'])
                require(self.actual(name) == row['installed_sha256'], 'Localization target changed during restore')
                os.replace(staged, target)
        require(all(self.actual(name) == row['previous_sha256'] for name, row in rows.items()), 'Restore verification failed')
        atomic_json(package / 'receipt.json', dict(receipt, state='restored'))
        self.cleanup_stages(receipt)
        return dict(state='restored', files=len(rows), backup_directory=str(package / 'backups'))
