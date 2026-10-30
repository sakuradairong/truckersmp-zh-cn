"""Fail if Git tracks anything outside the localization-only source allowlist."""
from pathlib import Path
import argparse
import re
import subprocess
from common import ROOT, require

FILES = {
    '.gitignore', '.gitattributes', 'README.md', 'LICENSE',
    'THIRD_PARTY_NOTICES.md', 'requirements-build.txt', 'build-settings-zh.ps1',
    'profiles/settings-zh-cn.json', 'profiles/resource-edits.json', 'profiles/freight-zh-cn.json',
    'profiles/settings-abi.json', 'tools/settings_compatibility.py', 'tools/installer_settings.py',
    'tools/prepare_installer_settings.py', 'tests/test_installer_settings.py',
    'src/settings_zh.cpp', 'src/settings_zh.def', 'tests/native_settings_zh_test.cpp',
    'tests/native_settings_package_test.py', 'tests/test_resources.py', 'tests/test_resource_transactions.py',
    'tools/archive.py', 'tools/common.py', 'tools/font_cmap.py', 'tools/check_characters.py',
    'tools/resources.py', 'tools/resource_text.py', 'tools/resource_transaction.py', 'tools/prepare_native_settings_zh.py',
    'tools/native_settings_package.py', 'tools/check_publish_scope.py',
    'vendor/minhook/LICENSE.txt', 'vendor/minhook/include/MinHook.h',
    'vendor/minhook/src/buffer.c', 'vendor/minhook/src/buffer.h', 'vendor/minhook/src/hook.c',
    'vendor/minhook/src/trampoline.c', 'vendor/minhook/src/trampoline.h',
    'vendor/minhook/src/hde/hde64.c', 'vendor/minhook/src/hde/hde64.h',
    'vendor/minhook/src/hde/pstdint.h', 'vendor/minhook/src/hde/table64.h',
    'build-installer.ps1', 'requirements-installer.txt', 'docs/FONTS.md',
    'profiles/font-sources.json', 'tools/prepare_installer_fonts.py',
    'tools/native_font_atlas.py', 'tests/test_native_font_atlas.py',
    'profiles/freight-characters.json',
    'profiles/player-filters-abi.json', 'ui/player_filters.lua',
    'src/player_filter_logic.h', 'src/player_filters.h', 'tools/prepare_player_filters.py',
    'tests/native_player_filters_test.h', 'tests/test_player_filters.py',
    'tools/installer_store.py', 'tools/installer_backend.py', 'tools/installer_gui.py',
    'tools/installer_selftest.py', 'tools/package_installer.py',
    'tests/test_font_cmap.py', 'tests/test_installer.py',
    'licenses/NotoSansSC-OFL.txt', 'licenses/NotoSansMono-OFL.txt', 'licenses/Tcl-8.6.12.txt',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--working-tree', action='store_true')
    args = parser.parse_args()
    command = ['git', 'ls-files', '-z']
    if args.working_tree:
        command.extend(('--cached', '--others', '--exclude-standard'))
    result = subprocess.run(command, cwd=ROOT, capture_output=True, check=True)
    paths = set(result.stdout.decode('utf-8').rstrip('\0').split('\0'))
    require(paths == FILES, 'Tracked file scope differs: ' + repr(sorted(paths ^ FILES)))
    secret = re.compile(r'(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{30,}|sk-[A-Za-z0-9]{24,})')
    private_path = re.compile(r'[A-Za-z]:[\\/]Users[\\/][^\s<>"\']+', re.I)
    forbidden = ('TmpNative' + 'SpeedBridge', 'SpeedLabels' + 'Plugin', 'TmpLocal' + 'Toolbox',
                 'vendor/' + 'tmp-sdk', 'ETS2LA' + 'PluginInput')
    size = 0
    for name in sorted(paths):
        data = ((ROOT / name).read_bytes() if args.working_tree else
                subprocess.run(['git', 'show', ':' + name], cwd=ROOT, capture_output=True, check=True).stdout)
        size += len(data)
        require(b'\0' not in data, 'Binary content in ' + name)
        text = data.decode('utf-8')
        require(not secret.search(text), 'Possible credential in ' + name)
        require(not private_path.search(text), 'Personal absolute path in ' + name)
        require(not any(word in text for word in forbidden), 'Unrelated feature reference in ' + name)
    print(f'PASS: {len(paths)} localization source files, {size} bytes; no binary assets, private paths or unrelated feature code')


if __name__ == '__main__':
    main()
