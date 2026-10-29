"""Exercise the real installer exclusively in a NEW private test directory."""
from pathlib import Path

from common import atomic_json, digest, plain, require
import installer_backend as backend
from installer_store import Store
import resources
from installer_settings import SettingsStore, DLL


def run(directory, source=None, assets=None):
    if source is None:
        candidates, _ = backend.discover()
        source = next((path for path in candidates if backend.inspect(path)['compatible']), None)
    require(source is not None, 'No compatible read-only TMP source was found')
    source = plain(Path(source).absolute())
    directory = plain(Path(directory).absolute(), missing=True)
    require(not directory.exists(), 'Self-test requires a NEW directory')
    require(not directory.is_relative_to(source) and not source.is_relative_to(directory), 'Self-test must be isolated')
    originals = resources.inputs(source)
    live_before = {name: digest(plain(source / name)) if (source / name).exists() else None
                   for name in {*resources.INPUTS, *resources.OUTPUTS, 'core_ets2mp.dll'}}
    fonts = backend.bundled_fonts(assets)
    directory.mkdir(parents=True)
    clone = directory / 'TMP 安装测试'
    for name, data in originals.items():
        target = clone / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (clone / 'core_ets2mp.dll').write_bytes((source / 'core_ets2mp.dll').read_bytes())
    game = directory / 'ETS2 游戏测试'
    executable = game / 'bin/win_x64/eurotrucks2.exe'
    executable.parent.mkdir(parents=True)
    executable.write_bytes(b'isolated fixture: never executed')
    baseline = {}
    for index, name in enumerate(resources.OUTPUTS):
        target = clone / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if index % 2:
            target.write_bytes(('private previous overlay ' + str(index)).encode())
        baseline[name] = digest(target) if target.exists() else None
    state = directory / '持久恢复记录'
    store = Store(clone, state, processes=lambda: [])
    result = store.install(fonts['regular'], fonts['semibold'], fonts['mono'], backend.VERSION)
    require(result['state'] == 'installed' and store.status()['state'] == 'installed', 'Installation self-test failed')
    native = SettingsStore(clone, state, processes=lambda: [])
    native_result = native.install(game, assets, backend.VERSION)
    require(native_result['state'] == 'installed' and (executable.parent / 'plugins' / DLL).is_file(),
            'Settings DLL was not installed at the SCS plugin loading path')
    require(backend.inspect(clone, state, game_root=game, include_settings=True, assets=assets)['compatible'],
            'Combined installer compatibility self-test failed')
    package, receipt = store.current()
    require(all(store.actual(name) == row['installed_sha256'] for name, row in receipt['files'].items()),
            'Installed self-test bytes differ')
    again = store.install(fonts['regular'], fonts['semibold'], fonts['mono'], backend.VERSION)
    require(again['state'] == 'already_installed', 'Idempotence self-test failed')
    require(native.install(game, assets, backend.VERSION)['state'] == 'already_installed',
            'Settings idempotence self-test failed')
    native.restore()
    require(not (executable.parent / 'plugins' / DLL).exists(), 'Settings restore self-test failed')
    restored = store.restore()
    require(restored['state'] == 'restored' and all(store.actual(name) == value for name, value in baseline.items()),
            'Restore self-test failed')
    # A new Store instance models closing and re-downloading the portable executable.
    require(Store(clone, state, processes=lambda: []).status()['state'] == 'restored', 'Persistent receipt self-test failed')
    require(all((digest(source / name) if (source / name).exists() else None) == value
                for name, value in live_before.items()), 'Read-only source changed during self-test')
    report = dict(passed=True, version=backend.VERSION, files=len(resources.OUTPUTS), install=True,
                  idempotence=True, restore=True, persistent_receipts=True, live_source_unchanged=True,
                  unicode_paths=True, game_started=False)
    report.update(settings_installed=True, settings_restore=True, settings_idempotence=True,
                  settings_path='bin/win_x64/plugins/' + DLL)
    atomic_json(directory / 'self-test.json', report)
    return report
