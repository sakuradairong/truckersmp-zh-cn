"""Fail if Git tracks anything outside the localization-only source allowlist."""
from pathlib import Path
import re
import subprocess
from common import ROOT, require

FILES = {
    '.gitignore', '.gitattributes', 'README.md', 'LICENSE',
    'THIRD_PARTY_NOTICES.md', 'requirements-build.txt', 'build-settings-zh.ps1',
    'profiles/settings-zh-cn.json', 'profiles/resource-edits.json', 'profiles/freight-zh-cn.json',
    'src/settings_zh.cpp', 'src/settings_zh.def', 'tests/native_settings_zh_test.cpp',
    'tests/native_settings_package_test.py', 'tests/test_resources.py', 'tests/test_resource_transactions.py',
    'tools/archive.py', 'tools/common.py', 'tools/font_cmap.py', 'tools/check_characters.py',
    'tools/resources.py', 'tools/resource_transaction.py', 'tools/prepare_native_settings_zh.py',
    'tools/native_settings_package.py', 'tools/check_publish_scope.py',
    'vendor/minhook/LICENSE.txt', 'vendor/minhook/include/MinHook.h',
    'vendor/minhook/src/buffer.c', 'vendor/minhook/src/buffer.h', 'vendor/minhook/src/hook.c',
    'vendor/minhook/src/trampoline.c', 'vendor/minhook/src/trampoline.h',
    'vendor/minhook/src/hde/hde64.c', 'vendor/minhook/src/hde/hde64.h',
    'vendor/minhook/src/hde/pstdint.h', 'vendor/minhook/src/hde/table64.h',
}


def main():
    result = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True, check=True)
    paths = set(result.stdout.decode('utf-8').rstrip('\0').split('\0'))
    require(paths == FILES, 'Tracked file scope differs: ' + repr(sorted(paths ^ FILES)))
    secret = re.compile(r'(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{30,}|sk-[A-Za-z0-9]{24,})')
    private_path = re.compile(r'[A-Za-z]:[\\/]Users[\\/][^\s<>"\']+', re.I)
    forbidden = ('TmpNative' + 'SpeedBridge', 'SpeedLabels' + 'Plugin', 'TmpLocal' + 'Toolbox',
                 'vendor/' + 'tmp-sdk', 'ETS2LA' + 'PluginInput')
    size = 0
    for name in sorted(paths):
        data = subprocess.run(['git', 'show', ':' + name], cwd=ROOT,
                              capture_output=True, check=True).stdout
        size += len(data)
        require(b'\0' not in data, 'Binary content in ' + name)
        text = data.decode('utf-8')
        require(not secret.search(text), 'Possible credential in ' + name)
        require(not private_path.search(text), 'Personal absolute path in ' + name)
        require(not any(word in text for word in forbidden), 'Unrelated feature reference in ' + name)
    print(f'PASS: {len(paths)} localization source files, {size} bytes; no binary assets, private paths or unrelated feature code')


if __name__ == '__main__':
    main()
