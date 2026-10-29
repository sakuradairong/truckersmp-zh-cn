"""Install/restore only the eight declared localization resources, with backups."""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import uuid
from common import active_apps, atomic_json, closed, digest, plain, read_json, require, write_output


def targets(tmp_root, output):
    from resources import OUTPUTS
    tmp_root = plain(tmp_root)
    output = plain(output)
    require(not output.is_relative_to(tmp_root), 'Package cannot be inside the installation')
    result = {}
    for relative in OUTPUTS:
        target = plain(tmp_root / relative, missing=True)
        require(target.is_relative_to(tmp_root), 'Target escapes the TMP directory')
        require(not target.exists() or target.is_file(), 'Target is not a regular file')
        result[relative] = target
    return result


def check_baseline(paths, rows, installed):
    for name, path in paths.items():
        plain(path, missing=True)
        expected = rows[name]['installed_sha256'] if installed else rows[name]['previous_sha256']
        require((path.is_file() and digest(path) == expected) if expected is not None else not path.exists(),
                'Localization target changed; preserving it: ' + str(path))


def stage_copy(source, destination, expected):
    plain(source); plain(destination, missing=True)
    with source.open('rb') as src, destination.open('xb') as dst:
        shutil.copyfileobj(src, dst)
        dst.flush(); os.fsync(dst.fileno())
    require(digest(destination) == expected, 'Staged resource hash differs')


def receipt_for(tmp_root, output, paths):
    receipt = read_json(plain(output / 'installed-resources.json'))
    require(receipt.get('schema') == 1 and receipt.get('package') == 'truckersmp-zh-cn-resources',
            'Unknown resource receipt')
    require(receipt.get('tmp_root') == str(tmp_root) and set(receipt.get('files', {})) == set(paths),
            'Receipt target scope differs')
    transaction = receipt.get('transaction', '')
    require(len(transaction) == 32 and all(char in '0123456789abcdef' for char in transaction),
            'Invalid transaction identifier')
    return receipt


def install(tmp_root, output, processes=active_apps):
    from resources import verify
    require(os.name == 'nt', 'Installer requires Windows rename semantics')
    tmp_root, output = Path(tmp_root).absolute(), Path(output).absolute()
    paths = targets(tmp_root, output)
    manifest = verify(tmp_root, output)
    closed(processes)
    receipt_path = output / 'installed-resources.json'
    if receipt_path.exists():
        previous = receipt_for(tmp_root, output, paths)
        require(previous['state'] in ('installed', 'restored'), 'Previous transaction is incomplete')
        if previous['state'] == 'installed':
            check_baseline(paths, previous['files'], True)
            require(all(previous['files'][name]['installed_sha256'] == manifest['files'][name] for name in paths),
                    'Restore the previous package before installing a different build')
            return dict(state='already_installed', files=len(paths))
    transaction = uuid.uuid4().hex
    backup_root = plain(output / 'backups' / transaction, missing=True)
    backup_root.mkdir(parents=True)
    rows, stages, committed = {}, {}, []
    try:
        for index, (name, target) in enumerate(paths.items()):
            target.parent.mkdir(parents=True, exist_ok=True)
            plain(target, missing=True)
            previous_sha = digest(target) if target.exists() else None
            backup = backup_root / f'{index}.bin'
            if previous_sha is not None:
                stage_copy(target, backup, previous_sha)
            rows[name] = dict(previous_sha256=previous_sha, installed_sha256=manifest['files'][name])
            staged = target.with_name(target.name + '.new-' + transaction)
            stages[name] = staged
            stage_copy(output / 'files' / name, staged, manifest['files'][name])
        closed(processes)
        verify(tmp_root, output)
        check_baseline(paths, rows, False)
        for name, target in paths.items():
            # Check again immediately before each replacement; changed user files are preserved.
            check_baseline({name: target}, rows, False)
            os.replace(stages[name], target)
            committed.append(name)
            require(digest(target) == rows[name]['installed_sha256'], 'Installed resource hash differs')
        check_baseline(paths, rows, True)
        receipt = dict(schema=1, package='truckersmp-zh-cn-resources', state='installed',
                       tmp_root=str(tmp_root), transaction=transaction, files=rows, game_started=False)
        atomic_json(receipt_path, receipt)
    except BaseException:
        for name in reversed(committed):
            target = paths[name]
            if target.is_file() and digest(target) == rows[name]['installed_sha256']:
                previous_sha = rows[name]['previous_sha256']
                if previous_sha is None:
                    target.unlink()
                else:
                    index = list(paths).index(name)
                    rollback = target.with_name(target.name + '.rollback-' + transaction)
                    stage_copy(backup_root / f'{index}.bin', rollback, previous_sha)
                    os.replace(rollback, target)
        raise
    finally:
        for name, staged in stages.items():
            if staged.exists() and digest(staged) == manifest['files'][name]:
                staged.unlink()
    return dict(state='installed', files=len(paths), receipt=str(receipt_path), backups=str(backup_root))


def restore(tmp_root, output, processes=active_apps):
    require(os.name == 'nt', 'Installer requires Windows rename semantics')
    tmp_root, output = Path(tmp_root).absolute(), Path(output).absolute()
    paths = targets(tmp_root, output)
    receipt = receipt_for(tmp_root, output, paths)
    rows = receipt['files']
    closed(processes)
    if receipt['state'] == 'restored':
        check_baseline(paths, rows, False)
        return dict(state='already_restored')
    require(receipt['state'] == 'installed', 'Receipt is not installed')
    check_baseline(paths, rows, True)
    backup_root = plain(output / 'backups' / receipt['transaction'])
    for index, name in enumerate(paths):
        if rows[name]['previous_sha256'] is not None:
            require(digest(plain(backup_root / f'{index}.bin')) == rows[name]['previous_sha256'],
                    'Restore backup changed')
    transaction = uuid.uuid4().hex
    stages, displaced, committed = {}, {}, []
    try:
        for index, (name, target) in enumerate(paths.items()):
            if rows[name]['previous_sha256'] is not None:
                staged = target.with_name(target.name + '.restore-' + transaction)
                stages[name] = staged
                stage_copy(backup_root / f'{index}.bin', staged, rows[name]['previous_sha256'])
        closed(processes)
        check_baseline(paths, rows, True)
        for name, target in paths.items():
            check_baseline({name: target}, rows, True)
            disabled = target.with_name(target.name + '.disabled-' + transaction)
            require(not disabled.exists(), 'Restore staging destination already exists')
            target.rename(disabled)
            displaced[name] = disabled
            committed.append(name)
            if name in stages:
                stages[name].rename(target)
        check_baseline(paths, rows, False)
        atomic_json(output / 'installed-resources.json', dict(receipt, state='restored'))
    except BaseException:
        for name in reversed(committed):
            target, disabled = paths[name], displaced[name]
            previous_sha = rows[name]['previous_sha256']
            safe = not target.exists() or (previous_sha is not None and target.is_file() and digest(target) == previous_sha)
            if safe and disabled.is_file() and digest(disabled) == rows[name]['installed_sha256']:
                os.replace(disabled, target)
        raise
    finally:
        for name, staged in stages.items():
            if staged.exists() and digest(staged) == rows[name]['previous_sha256']:
                staged.unlink()
    for name, disabled in displaced.items():
        if disabled.exists() and digest(disabled) == rows[name]['installed_sha256']:
            disabled.unlink()
    return dict(state='restored', files=len(paths), backups=str(backup_root))
