"""Seal/verify/install/restore only TmpSettingsZh.dll. Installed resources are read-only."""
from __future__ import annotations
import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import uuid

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / 'build/native-settings-zh'
DLL = 'TmpSettingsZh.dll'
NATIVE_SOURCES = ('src/settings_zh.cpp','src/settings_zh.def','profiles/settings-zh-cn.json',
                  'profiles/settings-abi.json','tools/settings_compatibility.py',
                  'tests/native_settings_zh_test.cpp','tools/prepare_native_settings_zh.py','build-settings-zh.ps1',
                  'tools/native_settings_package.py') + tuple(sorted(p.relative_to(ROOT).as_posix()
                  for p in (ROOT/'vendor/minhook').rglob('*') if p.is_file()))
PACKAGE_FILES = (DLL,'native-profile.json','native-checks.json','settings_profile.h')


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def plain(path, *, missing=False):
    """Reject symlinks/junctions before any target mutation, including parents."""
    for item in (path, *path.parents):
        try:
            info = item.lstat()
        except FileNotFoundError:
            require(missing and item == path, 'Missing path: '+str(item))
            continue
        require(not stat.S_ISLNK(info.st_mode) and not getattr(info,'st_file_attributes',0) & 0x400,
                'Reparse/link path refused: '+str(item))
    return path


def atomic_json(path, value):
    plain(path, missing=True)
    tmp = path.with_name(path.name+'.new-'+uuid.uuid4().hex)
    try:
        with tmp.open('x', encoding='utf-8', newline='\n') as stream:
            json.dump(value,stream,ensure_ascii=False,indent=2)
            stream.write('\n');stream.flush();os.fsync(stream.fileno())
        os.replace(tmp,path)
    finally:
        if tmp.exists():
            tmp.unlink()


def active_apps():
    command = "@(Get-Process -Name eurotrucks2,amtrucks,TruckersMP,'TruckersMP-Launcher','TruckersMP Launcher' -ErrorAction SilentlyContinue) | ForEach-Object { $_.ProcessName + ':' + $_.Id }"
    result = subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',command],
                            capture_output=True,text=True,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
    require(result.returncode==0,'Could not check game/TMP processes')
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


@dataclass
class Layout:
    release: Path
    plugins: Path
    pins: dict[Path,str]
    processes: object = active_apps

    @property
    def destination(self): return self.plugins / DLL

    @property
    def receipt(self): return self.release / 'installed.json'

    def closed(self):
        apps = self.processes()
        require(not apps,'Exit the game and TMP launcher first: '+', '.join(apps))

    def inputs(self):
        for path, expected in self.pins.items():
            plain(path)
            require(path.is_file() and digest(path)==expected,'Preserved dependency changed: '+str(path))

    def paths(self):
        plain(self.plugins);plain(self.release)
        plain(self.destination,missing=True)
        require(self.plugins.is_dir() and self.release.is_dir(),'Expected directories are missing')
        require(self.destination.parent.resolve()==self.plugins.resolve(),'Target escapes plugin directory')


def verify(layout):
    layout.paths()
    manifest=json.loads((layout.release/'package.json').read_text(encoding='utf-8'))
    require(manifest['schema']==1 and manifest['package']=='TmpSettingsZh','Unknown package identity')
    require(set(manifest['files'])==set(PACKAGE_FILES),'Unexpected package file scope')
    require(set(manifest['source_files'])==set(NATIVE_SOURCES),'Incomplete native source scope')
    require(manifest['preserved_inputs']=={str(p):h for p,h in layout.pins.items()},'Preservation baseline differs')
    for name, expected in manifest['files'].items():
        path=layout.release/name;plain(path)
        require(digest(path)==expected,'Package file differs: '+name)
    for relative, expected in manifest['source_files'].items():
        require(relative in NATIVE_SOURCES,'Unexpected source path')
        require(digest(ROOT/relative)==expected,'Source changed after validation: '+relative)
    report=json.loads((layout.release/'native-checks.json').read_text(encoding='utf-8'))
    require(report.get('passed') is True and report.get('checks',0)>=700 and
            report.get('mode')=='isolated_native_fixture' and report.get('game_started') is False and
            report.get('tmp_core_loaded') is False and report.get('allocations')==report.get('releases'),
            'Native offline checks are missing or failed')
    return manifest


def seal(layout):
    layout.paths();layout.inputs()
    import pefile
    p=pefile.PE(str(layout.release/DLL))
    require(p.FILE_HEADER.Machine==0x8664,'Expected x64 DLL')
    exports={s.name.decode() for s in p.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
    require(exports=={'scs_telemetry_init','scs_telemetry_shutdown'},'Unexpected exports or TMP SDK linkage')
    imports=[d.dll.decode() for d in p.DIRECTORY_ENTRY_IMPORT]
    require(not any('truckersmp' in s.lower() for s in imports),'TMP SDK import refused')
    report=json.loads((layout.release/'native-checks.json').read_text(encoding='utf-8'))
    require((layout.release/'native-checks.json').stat().st_mtime_ns >= (layout.release/DLL).stat().st_mtime_ns,'Stale native checks')
    for relative in NATIVE_SOURCES:
        require((ROOT/relative).stat().st_mtime_ns <= (layout.release/DLL).stat().st_mtime_ns,'Rebuild after source changes: '+relative)
    reference=json.loads((layout.release/'native-profile.json').read_text(encoding='utf-8'))
    manifest=dict(schema=1,package='TmpSettingsZh',version='0.2.0-experimental.1',core_sha256=reference['core_sha256'],
                  created_utc=datetime.now(timezone.utc).isoformat(),
                  files={n:digest(layout.release/n) for n in PACKAGE_FILES},
                  source_files={n:digest(ROOT/n) for n in NATIVE_SOURCES},
                  exports=sorted(exports),imports=imports,native_checks=report['checks'],
                  preserved_inputs={str(p):h for p,h in layout.pins.items()},
                  install_files=[DLL],live_validation='not_performed')
    atomic_json(layout.release/'package.json',manifest)
    verify(layout)
    return dict(state='sealed',native_checks=report['checks'],dll_sha256=manifest['files'][DLL],exports=sorted(exports))


def install(layout):
    require(os.name=='nt','Installer uses Windows no-replace rename semantics')
    manifest=verify(layout);layout.closed();layout.inputs()
    expected=manifest['files'][DLL]
    target=layout.destination
    if target.exists():
        require(layout.receipt.exists(),'Existing DLL has no owned receipt; refusing replacement')
        previous=json.loads(layout.receipt.read_text(encoding='utf-8'))
        require(previous.get('state')=='installed' and previous.get('dll_sha256')==expected and digest(target)==expected,
                'Existing DLL differs; refusing replacement')
        return dict(state='already_installed',path=str(target),dll_sha256=expected)
    transaction=uuid.uuid4().hex
    receipt=dict(schema=1,package='TmpSettingsZh',state='installed',transaction=transaction,
                 version=manifest['version'],path=str(target),dll_sha256=expected,previous_dll=None,
                 installed_utc=datetime.now(timezone.utc).isoformat(),
                 preserved_inputs={str(p):h for p,h in layout.pins.items()},
                 native_checks=manifest['native_checks'],live_validation='not_performed')
    stage=target.with_name(target.name+'.new-'+transaction)
    published=False
    try:
        with stage.open('xb') as stream, (layout.release/DLL).open('rb') as source:
            shutil.copyfileobj(source,stream);stream.flush();os.fsync(stream.fileno())
        require(digest(stage)==expected,'Staged DLL hash mismatch')
        layout.closed();layout.inputs();layout.paths()
        require(not target.exists(),'Destination appeared during staging')
        # Windows rename fails if the target exists; it cannot overwrite a foreign DLL.
        stage.rename(target);published=True
        require(digest(target)==expected,'Installed DLL hash mismatch')
        layout.inputs()
        atomic_json(layout.receipt,receipt)
    except BaseException:
        if published and target.exists() and digest(target)==expected:
            target.rename(stage)
        raise
    finally:
        if stage.exists():
            require(digest(stage)==expected,'Foreign staging content preserved')
            stage.unlink()
    return dict(state='installed',path=str(target),dll_sha256=expected,receipt=str(layout.receipt),preserved_files=len(layout.pins))


def restore(layout):
    layout.paths();layout.closed()
    receipt=json.loads(layout.receipt.read_text(encoding='utf-8'))
    require(receipt.get('schema')==1 and receipt.get('package')=='TmpSettingsZh','Unknown installation receipt')
    require(receipt.get('path')==str(layout.destination) and receipt.get('previous_dll') is None,'Unexpected restore target')
    if receipt.get('state')=='restored':
        require(not layout.destination.exists(),'DLL reappeared after restoration')
        return dict(state='already_restored')
    require(receipt.get('state')=='installed','Receipt is not installed')
    expected=receipt['dll_sha256']
    require(digest(layout.destination)==expected,'Installed DLL changed; refusing to move it')
    archive=layout.destination.with_name(DLL+'.disabled-'+uuid.uuid4().hex)
    layout.closed();layout.paths()
    require(digest(layout.destination)==expected,'Installed DLL changed during restore')
    layout.destination.rename(archive)
    try:
        require(digest(archive)==expected,'Restore archive hash mismatch')
        updated=dict(receipt,state='restored',disabled_copy=str(archive),restored_utc=datetime.now(timezone.utc).isoformat())
        atomic_json(layout.receipt,updated)
    except BaseException:
        if not layout.destination.exists() and digest(archive)==expected:
            archive.rename(layout.destination)
        raise
    return dict(state='restored',disabled_copy=str(archive),path=str(layout.destination))


def status(layout):
    manifest=verify(layout);layout.inputs()
    result=dict(state='verified_package',dll_sha256=manifest['files'][DLL],native_checks=manifest['native_checks'],preserved_files=len(layout.pins))
    if layout.receipt.exists():
        receipt=json.loads(layout.receipt.read_text(encoding='utf-8'))
        if receipt.get('state')=='installed':
            require(receipt.get('path')==str(layout.destination) and digest(layout.destination)==receipt.get('dll_sha256')==manifest['files'][DLL], 'Installation verification failed')
            result.update(state='verified_installation',path=str(layout.destination),live_validation='not_performed')
    return result


def configured_layout(tmp_root, game_root, release, action):
    from common import read_json
    from settings_compatibility import check_core
    from resources import OUTPUTS
    import check_characters
    tmp_root = tmp_root.absolute(); game_root = game_root.absolute(); release = release.absolute()
    plain(tmp_root); plain(game_root); plain(release)
    plugins = game_root / 'bin/win_x64/plugins'
    require(not release.is_relative_to(tmp_root) and not release.is_relative_to(game_root),
            'Package cannot be inside the TMP/game installation')
    source_paths = {tmp_root / 'core_ets2mp.dll', tmp_root / 'data/shared/ui.mp', tmp_root / 'data/shared/client.mp'}
    paths = source_paths | {tmp_root / name for name in OUTPUTS}
    if action == 'seal':
        pins = {path: digest(plain(path)) for path in paths}
        check_core(tmp_root / 'core_ets2mp.dll', read_json(ROOT / 'profiles/settings-abi.json'))
        check_characters.validate((tmp_root/'data/shared_mod/fonts/OpenSans.ttf').read_bytes())
    else:
        manifest = json.loads((release/'package.json').read_text(encoding='utf-8'))
        recorded = manifest['preserved_inputs']
        require(set(recorded) == {str(path) for path in paths}, 'Preservation target scope differs')
        pins = {path: recorded[str(path)] for path in paths}
    return Layout(release, plugins, pins)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('seal','verify','install','restore'))
    parser.add_argument('--tmp-root',type=Path,required=True)
    parser.add_argument('--game-root',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=RELEASE)
    ns=parser.parse_args()
    layout=configured_layout(ns.tmp_root,ns.game_root,ns.output,ns.action)
    functions=dict(seal=seal,verify=status,install=install,restore=restore)
    print(json.dumps(functions[ns.action](layout),ensure_ascii=True,indent=2))


if __name__=='__main__':
    main()
