"""Seal the compiled localization DLL and its ABI profile for the offline installer."""
import argparse
from pathlib import Path
import shutil

from common import ROOT, atomic_json, digest, read_json, require, sha
from native_settings_package import NATIVE_SOURCES


def prepare(native, assets):
    profile = read_json(native / 'native-profile.json')
    checks = read_json(native / 'native-checks.json')
    require(checks.get('passed') and checks.get('checks', 0) >= 1069 and
            checks.get('mode') == 'isolated_native_fixture', 'Native Settings checks have not passed')
    require(profile.get('player_filters', {}).get('hooks') == 5,
            'Player-filter native build evidence missing')
    built = (native / 'TmpSettingsZh.dll').stat().st_mtime_ns
    require((native / 'native-checks.json').stat().st_mtime_ns >= built and
            all((ROOT / name).stat().st_mtime_ns <= built for name in NATIVE_SOURCES),
            'Rebuild Settings after native source changes')
    evidence = profile['source_evidence']
    footer = read_json(ROOT / 'profiles/settings-zh-cn.json')['buttons']
    literals = []
    for english, _ in footer:
        found = [row for row in evidence if row['english'] == english]
        require(len(found) == 1, 'Ambiguous Settings footer source')
        literals.append(dict(rva=found[0]['source_rva'], text=english))
    compatibility = dict(schema=1,
        guards=[dict(name=row['name'], rva=row['rva'], size=len(bytes.fromhex(row['bytes'])),
                     sha256=sha(bytes.fromhex(row['bytes']))) for row in profile['runtime_guards']],
        pointers=[dict(rva=0xdd8330 + 0x10, target=0x13e860), dict(rva=0xdf2450 + 0x558, target=0x336bc0)],
        literals=literals)
    assets.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(native / 'TmpSettingsZh.dll', assets / 'TmpSettingsZh.dll')
    atomic_json(assets / 'settings-compatibility.json', compatibility)
    atomic_json(assets / 'settings-bundle.json', dict(schema=1,
        files={name: digest(assets / name) for name in ('TmpSettingsZh.dll', 'settings-compatibility.json')},
        native_checks=checks['checks'], five_pages=True, player_filters=True, live_validation='not_performed'))
    return dict(prepared=True, native_checks=checks['checks'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, default=ROOT / 'build/native-settings-zh')
    parser.add_argument('--assets', type=Path, default=ROOT / 'build/installer-assets')
    args = parser.parse_args()
    print(prepare(args.native, args.assets))
