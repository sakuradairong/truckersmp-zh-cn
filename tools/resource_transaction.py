"""Install/restore only the declared localization resources, with backups."""
from __future__ import annotations
import os
from pathlib import Path
import re
import shutil
import uuid
from common import active_apps, atomic_json, closed, digest, plain, read_json, require


def targets(tmp_root, output, names=None):
    from resources import OUTPUTS, LEGACY_OUTPUTS, PRE_FREIGHT_OUTPUTS
    tmp_root = plain(tmp_root)
    output = plain(output)
    require(not output.is_relative_to(tmp_root), 'Package cannot be inside the installation')
    result = {}
    require(names is None or set(names) in (set(OUTPUTS), set(PRE_FREIGHT_OUTPUTS), set(LEGACY_OUTPUTS)),
            'Unknown resource target scope')
    for relative in (OUTPUTS if names is None else (n for n in OUTPUTS if n in names)):
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
    require(receipt.get('state') in ('applying', 'installed', 'restoring', 'restored'),
            'Invalid resource receipt state')
    for row in receipt['files'].values():
        require(isinstance(row, dict) and set(row) == {'previous_sha256', 'installed_sha256'},
                'Invalid resource receipt row')
        for key, value in row.items():
            require((key == 'previous_sha256' and value is None) or
                    isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value), 'Invalid receipt hash')
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
        # Publish the recovery mapping before the first target mutation, including on first install.
        receipt = dict(schema=1, package='truckersmp-zh-cn-resources', state='applying',
                       tmp_root=str(tmp_root), transaction=transaction, files=rows, game_started=False)
        atomic_json(receipt_path, receipt)
        for name, target in paths.items():
            # Check again immediately before each replacement; changed user files are preserved.
            check_baseline({name: target}, rows, False)
            os.replace(stages[name], target)
            committed.append(name)
            require(digest(target) == rows[name]['installed_sha256'], 'Installed resource hash differs')
        check_baseline(paths, rows, True)
        atomic_json(receipt_path, dict(receipt, state='installed'))
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
    prior = read_json(plain(output / 'installed-resources.json'))
    paths = targets(tmp_root, output, prior.get('files', {}))
    receipt = receipt_for(tmp_root, output, paths)
    rows = receipt['files']
    closed(processes)
    if receipt['state'] == 'restored':
        check_baseline(paths, rows, False)
        return dict(state='already_restored')
    backup_root = plain(output / 'backups' / receipt['transaction'])
    def actual(name):
        target = plain(paths[name], missing=True)
        require(not target.exists() or target.is_file(), 'Target is not a regular file')
        return digest(target) if target.exists() else None

    # Applying/restoring journals may contain any mix of original and installed files.
    # Check every target and backup before making any recovery write.
    for index, name in enumerate(paths):
        row = rows[name]
        allowed = (row['installed_sha256'],) if receipt['state'] == 'installed' else (
            row['previous_sha256'], row['installed_sha256'])
        require(actual(name) in allowed, 'Localization target changed; preserving it: ' + str(paths[name]))
        if rows[name]['previous_sha256'] is not None:
            require(digest(plain(backup_root / f'{index}.bin')) == rows[name]['previous_sha256'],
                    'Restore backup changed')
    atomic_json(output / 'installed-resources.json', dict(receipt, state='restoring'))
    closed(processes)
    for index, (name, target) in enumerate(paths.items()):
        row = rows[name]
        current = actual(name)
        require(current in (row['previous_sha256'], row['installed_sha256']),
                'Localization target changed during restore: ' + str(target))
        if current == row['previous_sha256']:
            continue
        if row['previous_sha256'] is None:
            target.unlink()
        else:
            staged = plain(target.with_name(target.name + '.restore-' + receipt['transaction']), missing=True)
            if staged.exists():
                require(staged.is_file() and digest(staged) == row['previous_sha256'],
                        'Restore staging file changed; preserving it')
            else:
                stage_copy(backup_root / f'{index}.bin', staged, row['previous_sha256'])
            check_baseline({name: target}, rows, True)
            os.replace(staged, target)
    check_baseline(paths, rows, False)
    atomic_json(output / 'installed-resources.json', dict(receipt, state='restored'))
    for name, target in paths.items():
        staged = plain(target.with_name(target.name + '.new-' + receipt['transaction']), missing=True)
        if staged.is_file() and digest(staged) == rows[name]['installed_sha256']:
            staged.unlink()
    return dict(state='restored', files=len(paths), backups=str(backup_root))
