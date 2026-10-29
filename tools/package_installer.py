"""Package only the offline localization installer and licensed fonts. Does not publish."""
from __future__ import annotations
import argparse
import importlib.metadata as metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tkinter
import zipfile

from common import ROOT, atomic_json, digest, plain, require
import installer_backend as backend
import resources


def collect_licenses(folder):
    folder.mkdir(parents=True, exist_ok=True)
    # These are copied from installed build dependencies; they contain no user data.
    for package in ('Pillow', 'PyInstaller'):
        distribution = metadata.distribution(package)
        copied = 0
        for item in distribution.files or []:
            if 'license' not in item.name.lower() and 'copying' not in item.name.lower():
                continue
            source = Path(distribution.locate_file(item))
            if not source.is_file() or source.suffix.lower() in ('.py', '.pyc'):
                continue
            content = source.read_text(encoding='utf-8')
            destination = folder / package / ('-'.join(item.parts[-3:]) + '.txt')
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(content, encoding='utf-8', newline='\n')
            copied += 1
        require(copied > 0, 'Missing runtime license: ' + package)
    python_license = Path(sys.base_prefix) / 'LICENSE.txt'
    if not python_license.is_file():
        python_license = Path(sys.base_prefix) / 'LICENSE'
    require(python_license.is_file(), 'Missing Python license')
    shutil.copyfile(python_license, folder / 'Python.txt')
    interpreter = tkinter.Tcl()
    tcl = Path(interpreter.eval('info library'))
    # Python distributions differ in whether the license is beside the scripts or in license.terms.
    for name, directory in (('Tcl', tcl), ('Tk', tcl.parent / ('tk' + interpreter.eval('info tclversion')))):
        candidates = [directory / value for value in ('license.terms', 'license', 'LICENSE', 'license.txt')]
        source = next((path for path in candidates if path.is_file()), None)
        if source is None and name == 'Tcl' and interpreter.eval('info patchlevel') == '8.6.12':
            source = ROOT / 'licenses/Tcl-8.6.12.txt'
            require(digest(source) == 'c0a69a2bfd757361ec7e6143973b103c90409316b49e9c88db26ad6388e79f16',
                    'Pinned Tcl license differs')
        require(source is not None, 'Missing ' + name + ' runtime license')
        shutil.copyfile(source, folder / (name + '.txt'))


def build(assets):
    require(sys.platform == 'win32' and sys.maxsize > 2**32, 'Build on Windows x64')
    backend.bundled_fonts(assets)
    work = ROOT / 'build/installer-package'
    work.mkdir(parents=True, exist_ok=True)
    licenses = work / 'runtime-licenses'
    collect_licenses(licenses)
    version_file = work / 'version-info.txt'
    version_file.write_text("""VSVersionInfo(ffi=FixedFileInfo(filevers=(0,2,0,2), prodvers=(0,2,0,2),
mask=0x3f, flags=2, OS=0x40004, fileType=0x1, subtype=0x0, date=(0,0)), kids=[
StringFileInfo([StringTable('040904b0', [
StringStruct('FileDescription', 'TruckersMP Simplified Chinese installer'),
StringStruct('FileVersion', '0.2.0-preview.2'),
StringStruct('ProductName', 'TruckersMP ZH-CN'),
StringStruct('ProductVersion', '0.2.0-preview.2'),
StringStruct('LegalCopyright', 'MIT; bundled fonts under SIL OFL 1.1')])]),
VarFileInfo([VarStruct('Translation', [1033,1200])])])""", encoding='utf-8')
    executable_name = 'TruckersMP-ZH-CN-Setup-' + backend.VERSION
    command = [sys.executable, '-B', '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile',
               '--windowed', '--noupx', '--icon', 'NONE', '--name', executable_name,
               '--distpath', str(ROOT / 'dist'), '--workpath', str(work / 'pyinstaller'),
               '--specpath', str(work), '--paths', str(ROOT / 'tools'), '--version-file', str(version_file)]
    data = [(ROOT / name, str(Path(name).parent)) for name in resources.SOURCES]
    data.extend((ROOT / name, '.') for name in ('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md'))
    data.append((ROOT / 'docs/FONTS.md', 'docs'))
    data.extend((assets / name, 'assets') for name in (*backend.FONT_FILES.values(), *backend.LICENSE_FILES,
                'font-bundle.json', 'font-report.json'))
    data.append((licenses, 'runtime-licenses'))
    for source, destination in data:
        require(plain(source).exists(), 'Missing installer data: ' + source.name)
        command.extend(('--add-data', str(source) + ';' + destination))
    command.append(str(ROOT / 'tools/installer_gui.py'))
    environment = os.environ.copy()
    environment['PYINSTALLER_CONFIG_DIR'] = str(ROOT / 'build/pyinstaller-cache')
    subprocess.run(command, cwd=ROOT, env=environment, check=True)
    executable = ROOT / 'dist' / (executable_name + '.exe')
    require(executable.is_file(), 'Executable was not produced')
    return executable


def package(executable, assets):
    # No client .mp/.dll, upstream font inputs, backup, SDK or native Settings module is packaged.
    folder = executable.parent
    instructions = folder / '安装说明.txt'
    instructions.write_text(
        'TruckersMP 简体中文安装器 ' + backend.VERSION + '\n\n'
        '1. 正常退出 ETS2、ATS 和 TMP 启动器。\n'
        '2. 双击同目录的安装器 EXE，确认识别到的 TMP 安装目录。\n'
        '3. 点击“安装汉化”，完成后正常从 TMP 启动器进入游戏。\n'
        '4. 如需撤销，重新运行安装器，点击“恢复安装前状态”。\n\n'
        'Windows x64 / ETS2；按界面资源和控件识别兼容性，不限定 TMP 版本或原始文件哈希。\n'
        '已安装旧版汉化时，可直接点击“更新 / 检查汉化”，保留首次安装前的恢复点。\n'
        '包括 Tab、公交和货运静态文案；不安装实验性 Settings 模块。\n'
        '备份保存在 %LOCALAPPDATA%\\TruckersMPZH，删除下载的 EXE 不会删除备份。\n'
        '无需 Python、编译器、SDK、联网下载或用户自备字体。\n'
        '预览版：新字体通过离线验证，游戏内显示仍待验收。\n'
        '本项目非官方项目，未获 TruckersMP 认证，不承诺联网使用或处罚风险为零。\n\n'
        '源码：https://github.com/sakuradairong/truckersmp-zh-cn\n'
        '字体为 Noto 派生作品，适用 SIL OFL 1.1。版权声明和完整许可在附带文本及安装器“字体与许可”中。\n',
        encoding='utf-8', newline='\n')
    archive = folder / ('TruckersMP-ZH-CN-' + backend.VERSION + '-windows-x64.zip')
    entries = [(executable, executable.name), (instructions, instructions.name), (ROOT / 'LICENSE', 'LICENSE'),
               (ROOT / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md'), (ROOT / 'docs/FONTS.md', 'FONTS.md')]
    entries.extend((assets / name, 'licenses/' + name) for name in sorted(backend.LICENSE_FILES))
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for source, name in entries:
            output.write(source, name)
    with zipfile.ZipFile(archive) as output:
        require(set(output.namelist()) == {name for _, name in entries}, 'Release ZIP scope differs')
    sums = folder / 'SHA256SUMS.txt'
    sums.write_text(''.join(digest(path) + '  ' + path.name + '\n' for path in (executable, archive)), encoding='ascii')
    report = dict(version=backend.VERSION, files={path.name: dict(bytes=path.stat().st_size, sha256=digest(path))
                                                 for path in (executable, archive)},
                  zip_entries=[name for _, name in entries], code_signed=False, published=False,
                  fonts_bundled=True, client_assets_bundled=False, native_settings_bundled=False)
    atomic_json(folder / 'installer-package.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'build/installer-assets')
    parser.add_argument('--package-existing', type=Path)
    args = parser.parse_args()
    executable = args.package_existing or build(args.assets)
    print(json.dumps(package(executable, args.assets), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
