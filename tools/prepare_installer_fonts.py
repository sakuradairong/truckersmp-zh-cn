"""Build renamed, static OFL fonts from pinned upstream inputs. Build-time network only."""
from __future__ import annotations
import argparse
import io
import json
from pathlib import Path
import urllib.request
import ttfautohint

import fontTools
from fontTools import subset
from fontTools.merge import Merger
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from PIL import Image, ImageDraw, ImageFont

from common import ROOT, atomic_json, digest, plain, read_json, require, sha
import check_characters


def fetch_inputs(folder, profile, fetch):
    folder = plain(folder, missing=True)
    folder.mkdir(parents=True, exist_ok=True)
    result = {}
    for source in profile['sources']:
        for suffix, remote, expected in (
                ('.ttf', source['path'], source['sha256']),
                ('-OFL.txt', source['license_path'], source['license_sha256'])):
            target = plain(folder / (source['name'] + suffix), missing=True)
            if not target.exists():
                require(fetch, 'Missing pinned input; use --fetch: ' + target.name)
                url = 'https://raw.githubusercontent.com/google/fonts/' + profile['commit'] + '/' + remote
                print('Downloading ' + target.name, flush=True)
                with urllib.request.urlopen(url, timeout=30) as response:
                    content = response.read(32 * 1024 * 1024 + 1)
                require(len(content) <= 32 * 1024 * 1024 and sha(content) == expected,
                        'Upstream file hash differs: ' + target.name)
                with target.open('xb') as stream:
                    stream.write(content)
            require(digest(target) == expected, 'Pinned input differs: ' + target.name)
        result[source['name']] = folder / (source['name'] + '.ttf')
    return result


def name_font(font, family, style, weight):
    replacements = {1: family, 2: style, 3: family + '-' + style + '-1.0',
                    4: family + ' ' + style, 6: family.replace(' ', '') + '-' + style,
                    16: family, 17: style, 18: family + ' ' + style}
    for record in font['name'].names:
        if record.nameID in replacements:
            record.string = replacements[record.nameID].encode(record.getEncoding())
    for name_id, value in replacements.items():
        font['name'].setName(value, name_id, 3, 1, 0x409)
    font['OS/2'].usWeightClass = weight
    font['OS/2'].fsSelection &= ~(1 << 5)
    font['head'].macStyle &= ~1
    for tag in ('STAT', 'DSIG'):
        if tag in font:
            del font[tag]
    font.recalcTimestamp = False
    return font


def instantiate(path, weight, family, style):
    font = TTFont(path, recalcTimestamp=False)
    axes = {axis.axisTag: axis.defaultValue for axis in font['fvar'].axes}
    axes['wght'] = weight
    font = instantiateVariableFont(font, axes, inplace=True, optimize=True)
    require('fvar' not in font and 'gvar' not in font, 'Variable font was not fully instantiated')
    return name_font(font, family, style, weight)


def prepare(output, inputs, fetch=False):
    profile = read_json(ROOT / 'profiles/font-sources.json')
    require(fontTools.__version__ == profile['fonttools'], 'Use the pinned FontTools version')
    require(ttfautohint.__version__ == profile['ttfautohint_py'], 'Use the pinned ttfautohint-py version')
    sources = fetch_inputs(inputs, profile, fetch)
    output = plain(output, missing=True)
    output.mkdir(parents=True, exist_ok=True)
    scratch = output / 'font-work'
    scratch.mkdir(exist_ok=True)
    fonts = {}
    for weight, style in ((400, 'Regular'), (600, 'SemiBold')):
        print('Instantiating Chinese ' + style, flush=True)
        font = instantiate(sources['NotoSansSC'], weight, 'TMPZH Sans', style)
        path = output / ('TMPZHSans-' + style + '.ttf')
        font.save(path, reorderTables=True)
        fonts[style.lower()] = path

    print('Building monospaced Latin with Chinese fallback', flush=True)
    latin = instantiate(sources['NotoSansMono'], 400, 'TMPZH Mono', 'Regular')
    chinese = TTFont(fonts['regular'], recalcTimestamp=False)
    require(latin['head'].unitsPerEm == chinese['head'].unitsPerEm, 'Font units differ')
    # Disjoint cmaps retain Noto Sans Mono for Latin/Greek/Cyrillic and SC for other scripts.
    latin_chars = set(latin.getBestCmap())
    options = subset.Options()
    options.recalc_timestamp = False
    options.name_IDs = ['*']
    options.name_legacy = True
    options.name_languages = ['*']
    job = subset.Subsetter(options=options)
    job.populate(unicodes=set(chinese.getBestCmap()) - latin_chars)
    job.subset(chinese)
    # This alias is used horizontally. The Latin font has no vertical metrics,
    # and the merger cannot combine vhea/vmtx with a missing peer table.
    for tag in ('vhea', 'vmtx', 'BASE'):
        if tag in chinese:
            del chinese[tag]
    latin_path, chinese_path = scratch / 'latin.ttf', scratch / 'chinese.ttf'
    latin.save(latin_path); chinese.save(chinese_path)
    merged = Merger().merge([str(latin_path), str(chinese_path)])
    merged = name_font(merged, 'TMPZH Mono', 'Regular', 400)
    # The upstream variable font leaves this flag unset. Without the static flag,
    # FreeType's small-size hinting changes advances despite constant ASCII hmtx.
    merged['post'].isFixedPitch = 1
    copyrights = '\n'.join(dict.fromkeys(f['name'].getDebugName(0) or '' for f in (latin, chinese)))
    merged['name'].setName(copyrights, 0, 3, 1, 0x409)
    merged['head'].created = latin['head'].created
    merged['head'].modified = latin['head'].modified
    mono = output / 'TMPZHMono-Regular.ttf'
    merged.save(mono, reorderTables=True)
    fonts['mono'] = mono

    # Bake hinting before ASCII remapping: otherwise FreeType reclassifies the
    # aliased Chinese glyphs as Latin and changes their small-size rasterization.
    for role, path in fonts.items():
        print('Baking stable TrueType hinting: ' + role, flush=True)
        hinted = ttfautohint.ttfautohint(in_buffer=path.read_bytes(), no_info=True,
                                       windows_compatibility=True, epoch=1704067200)
        path.write_bytes(hinted)

    reports, bundle_files = {}, {}
    for role, path in fonts.items():
        data = path.read_bytes()
        report = check_characters.validate(data)
        font = TTFont(io.BytesIO(data), recalcTimestamp=False)
        cmap = font.getBestCmap()
        ascii_widths = {font['hmtx'][cmap[cp]][0] for cp in range(32, 127)}
        report.update(name=font['name'].getDebugName(4), weight=font['OS/2'].usWeightClass,
                      codepoints=len(cmap), bmp_codepoints=sum(cp < 65536 for cp in cmap),
                      supplementary_codepoints=sum(cp >= 65536 for cp in cmap),
                      ascii_advance_units=sorted(ascii_widths), units_per_em=font['head'].unitsPerEm,
                      variable='fvar' in font, bytes=len(data), sha256=sha(data))
        if role == 'mono':
            require(len(ascii_widths) == 1, 'ASCII glyphs are not monospaced')
            require(len({ImageFont.truetype(str(path), 14).getlength(chr(cp)) for cp in range(32, 127)}) == 1,
                    'Rendered ASCII widths are not monospaced')
        reports[role] = report
        bundle_files[path.name] = report['sha256']
    for source in profile['sources']:
        name = source['name'] + '-OFL.txt'
        (output / name).write_bytes((inputs / name).read_bytes())
        bundle_files[name] = source['license_sha256']
    hint_version = ttfautohint.run(['--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    report = dict(schema=1, source_commit=profile['commit'], fonttools=fontTools.__version__,
                  ttfautohint_py=ttfautohint.__version__, hinting_tool=hint_version,
                  fonts=reports, game_started=False,
                  limits='Offline glyph checks do not prove the game renderer or final in-game layout.')
    atomic_json(output / 'font-report.json', report)
    atomic_json(output / 'font-bundle.json', dict(schema=1, files=bundle_files))
    preview(fonts, output / 'font-preview.png')
    return report


def preview(fonts, target):
    image = Image.new('RGB', (1120, 600), '#17212b')
    draw = ImageDraw.Draw(image)
    regular, bold, mono = [ImageFont.truetype(str(fonts[key]), 22) for key in ('regular', 'semibold', 'mono')]
    draw.text((28, 20), 'TruckersMP 简体中文 · 字体离线样张', font=bold, fill='#eaf2f7')
    draw.text((28, 70), 'TMPZH Sans Regular 400', font=regular, fill='#7dd9ce')
    draw.text((28, 108), '玩家面板  公交调度  货运任务  历史玩家  复制当前任务', font=regular, fill='#edf2f7')
    draw.text((28, 156), 'TMPZH Sans SemiBold 600', font=bold, fill='#7dd9ce')
    draw.text((28, 194), '玩家面板  公交调度  货运任务  历史玩家  复制当前任务', font=bold, fill='#edf2f7')
    draw.text((28, 242), 'TMPZH Mono Regular 400 · ASCII 等宽 / 中文回退', font=regular, fill='#7dd9ce')
    for index, text in enumerate(('ID  Ping   Distance   Player', '01  036ms  00125m     张三 / [VTC] Alice',
                                  '79  138ms  00551m     李四 / [TMP] Mikhail')):
        draw.text((28, 280 + index * 34), text, font=mono, fill='#edf2f7')
    text = '查看地图  特殊运输  双挂车  外部货运市场  自动调整车内音量'
    y = 406
    for size in (11, 14, 18, 21):
        font = ImageFont.truetype(str(fonts['regular']), size)
        draw.text((28, y), str(size) + 'px  ' + text, font=font, fill='#edf2f7')
        y += 36
    image.save(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, default=ROOT / 'inputs/open-fonts')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/installer-assets')
    parser.add_argument('--fetch', action='store_true')
    args = parser.parse_args()
    report = prepare(args.output.absolute(), args.inputs.absolute(), args.fetch)
    print(json.dumps({key: {k: value[k] for k in ('weight', 'codepoints', 'bytes', 'variable')}
                      for key, value in report['fonts'].items()}, indent=2))


if __name__ == '__main__':
    main()
