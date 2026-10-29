"""Build resource localization from a user's own pinned TMP files and font."""
from __future__ import annotations
import argparse
import copy
import io
import json
from pathlib import Path
import struct
from PIL import ImageFont
import archive
import check_characters
import font_cmap as fonts
from common import (ROOT, CORE_SHA, UI_SHA, CLIENT_SHA, atomic_json, digest, plain,
                    read_json, require, sha, write_output)

FONT_NAME = 'OfflineFreightZH.ttf'
INPUTS = {'core_ets2mp.dll': CORE_SHA, 'data/shared/ui.mp': UI_SHA, 'data/shared/client.mp': CLIENT_SHA}
FONT_ALIASES = ('OpenSans.ttf', 'fonts/OpenSans.ttf', 'fonts/OpenSans-Semibold.ttf', 'fonts/RobotoMono.ttf')
OUTPUTS = ('data/shared_mod/ui.mp', 'data/shared_mod/client.mp',
           *(f'data/shared_mod/{name}' for name in (*FONT_ALIASES, FONT_NAME, 'fonts/' + FONT_NAME)))
SOURCES = ('tools/resources.py', 'tools/archive.py', 'tools/font_cmap.py', 'tools/common.py',
           'tools/check_characters.py', 'profiles/resource-edits.json', 'profiles/freight-zh-cn.json',
           'profiles/settings-zh-cn.json')
UI_ENTRIES = {3, 4, 8, 10, 12, 13, 17}


def inputs(tmp_root):
    result = {}
    for relative, expected in INPUTS.items():
        path = plain(tmp_root / relative)
        data = path.read_bytes()
        require(sha(data) == expected, 'Unsupported TMP input: ' + relative)
        result[relative] = data
    return result


def apply_edits(raw, entry):
    require(sha(raw) == entry['source_sha256'], 'Resource source differs: ' + entry['name'])
    text = raw.decode('utf-8').replace('\r\n', '\n')
    cursor, parts = 0, []
    for edit in entry['edits']:
        start, stop = edit['start'], edit['stop']
        require(isinstance(start, int) and isinstance(stop, int) and cursor <= start <= stop <= len(text),
                'Unordered or invalid translation edit')
        parts.extend((text[cursor:start], edit['text']))
        cursor = stop
    parts.append(text[cursor:])
    newline = entry.get('newline', 'lf')
    require(newline in ('lf', 'crlf'), 'Unsupported resource line ending')
    result_text = ''.join(parts)
    if newline == 'crlf':
        result_text = result_text.replace('\n', '\r\n')
    result = result_text.encode('utf-8')
    require(sha(result) == entry['result_sha256'], 'Translation result differs: ' + entry['name'])
    return result


def localized_ui(source):
    profile = read_json(ROOT / 'profiles/resource-edits.json')
    require(profile['schema'] == 1 and profile['core_sha256'] == CORE_SHA and
            profile['ui_sha256'] == UI_SHA and profile['client_sha256'] == CLIENT_SHA,
            'Resource profile identity differs')
    entries = profile['ui_entries']
    require(len(entries) == 7 and {entry['index'] for entry in entries} == UI_ENTRIES,
            'Unexpected UI translation scope')
    rows = archive.parse(source, 21)
    replacements = {}
    for entry in entries:
        row = rows[entry['index']]
        require(hex(row['path_hash']) == entry['path_hash'], 'Resource path hash differs')
        replacements[entry['index']] = apply_edits(row['raw'], entry)
    return archive.repack(source, 21, replacements)


def make_codebook(translations):
    characters = sorted({char for text in translations.values() for char in text if ord(char) > 127})
    literal_ascii = {char for text in translations.values() for char in text if ord(char) < 128}
    slots = [chr(cp) for cp in range(33, 127) if chr(cp) not in literal_ascii | set('\\&')]
    require(len(characters) <= len(slots), 'Not enough safe ASCII slots')
    result = dict(zip(characters, slots))
    fonts.validate_codebook(result, characters)
    return result


def remap_font(font_data, codebook):
    tables = fonts.tables_from(font_data)
    mappings = fonts.unicode_cmaps(tables[b'cmap'])
    baseline = mappings[0][2]
    glyph_count = struct.unpack_from('>H', tables[b'maxp'], 4)[0]
    require(all(0 < baseline.get(ord(char), 0) < glyph_count for char in codebook), 'Missing freight glyph')
    require(all(all(mapping.get(ord(char)) == baseline[ord(char)] for char in codebook)
                for _, _, mapping, _ in mappings), 'Font Unicode maps disagree')
    replacements = {ord(token): baseline[ord(char)] for char, token in codebook.items()}
    updated = dict(tables)
    updated[b'cmap'] = fonts.remapped_cmap(tables[b'cmap'], replacements)
    updated[b'name'] = fonts.renamed_names(tables[b'name'])
    data = fonts.repack(updated)
    saved = fonts.tables_from(data)
    require(fonts.checksum(data) == fonts.MAGIC_SUM, 'Remapped font checksum failed')
    for tag, original in tables.items():
        if tag not in (b'head', b'cmap', b'name', b'DSIG'):
            require(saved[tag] == original, 'Unrelated font table changed')
    old_head, new_head = bytearray(tables[b'head']), bytearray(saved[b'head'])
    old_head[8:12] = new_head[8:12] = b'\0' * 4
    require(old_head == new_head, 'Unexpected font head change')
    for old, new in zip(mappings, fonts.unicode_cmaps(saved[b'cmap'])):
        expected = dict(old[2]); expected.update(replacements)
        require(new[2] == expected, 'Unexpected cmap change')
    checks = 0
    for size in (11, 14, 18, 21):
        original = ImageFont.truetype(io.BytesIO(font_data), size, layout_engine=ImageFont.Layout.BASIC)
        remapped = ImageFont.truetype(io.BytesIO(data), size, layout_engine=ImageFont.Layout.BASIC)
        for char, token in codebook.items():
            require(fonts.raster(original, char) == fonts.raster(remapped, token), 'Freight glyph raster differs')
            require(fonts.widen(token) == token and fonts.widen(token, True) == token, 'ASCII widening failed')
            checks += 1
    return data, checks


def freight_candidate(source, translations, codebook):
    result = copy.deepcopy(source)
    allowed, captions = set(), []

    def visit(node, path):
        properties = node.get('Properties', {})
        kind = node['Type']
        if kind in ('GroupBox', 'Button', 'CheckBoxWithLabel'):
            key = 'CheckboxText' if kind == 'CheckBoxWithLabel' else 'Text'
            english = properties[key]
            require(english in translations, 'Unexpected freight caption: ' + english)
            chinese = translations[english]
            properties[key] = ''.join(codebook.get(char, char) for char in chinese)
            properties['Font'] = FONT_NAME
            allowed.update((path + ('Properties', key), path + ('Properties', 'Font')))
            captions.append({'english': english, 'chinese': chinese, 'control': properties.get('ControlName', '')})
        for index, child in enumerate(node.get('Children', [])):
            visit(child, path + ('Children', index))

    visit(result['Controls'], ('Controls',))
    require(len(captions) == 17 and len(allowed) == 34, 'Unexpected freight caption count')
    require(set(fonts.changes(source, result)) == allowed, 'Freight changes exceed static text/font fields')
    return result, captions


def build(tmp_root, font_path, output):
    plain(tmp_root); plain(font_path)
    output = plain(output, missing=True)
    require(not output.is_relative_to(tmp_root.absolute()), 'Build output cannot be inside the TMP installation')
    originals = inputs(tmp_root)
    font_data = font_path.read_bytes()
    character_checks = check_characters.validate(font_data)
    translations = read_json(ROOT / 'profiles/freight-zh-cn.json')
    codebook = make_codebook(translations)
    remapped_font, raster_checks = remap_font(font_data, codebook)
    ui = localized_ui(originals['data/shared/ui.mp'])
    client_source = originals['data/shared/client.mp']
    source_tree = json.loads(archive.parse(client_source, 11)[9]['raw'])
    candidate, captions = freight_candidate(source_tree, translations, codebook)
    payload = (json.dumps(candidate, ensure_ascii=True, indent=2) + '\n').encode('ascii')
    client = archive.repack(client_source, 11, {9: payload})
    data = {'data/shared_mod/ui.mp': ui, 'data/shared_mod/client.mp': client}
    data.update({f'data/shared_mod/{name}': font_data for name in FONT_ALIASES})
    data.update({f'data/shared_mod/{name}': remapped_font for name in (FONT_NAME, 'fonts/' + FONT_NAME)})
    require(set(data) == set(OUTPUTS), 'Unexpected output file scope')
    # Re-read all installed inputs before writing only local build outputs.
    inputs(tmp_root)
    require(font_path.read_bytes() == font_data, 'Font changed while building')
    for relative, content in data.items():
        write_output(output / 'files' / relative, content)
    manifest = dict(schema=1, package='truckersmp-zh-cn-resources', core_sha256=CORE_SHA,
                    inputs=INPUTS, files={name: sha(content) for name, content in data.items()},
                    source_files={name: digest(ROOT / name) for name in SOURCES},
                    character_checks=character_checks, freight_glyph_rasters=raster_checks,
                    freight_static_captions=captions, game_started=False,
                    live_validation='Public extraction verified offline; native Settings not verified in-game.')
    atomic_json(output / 'package.json', manifest)
    verify(tmp_root, output)
    return dict(state='built', files=len(data), ui_entries=sorted(UI_ENTRIES), freight_captions=len(captions),
                freight_glyph_rasters=raster_checks, character_checks=character_checks)


def verify(tmp_root, output):
    plain(output)
    manifest = read_json(output / 'package.json')
    require(manifest['schema'] == 1 and manifest['package'] == 'truckersmp-zh-cn-resources' and
            manifest['core_sha256'] == CORE_SHA and manifest['inputs'] == INPUTS, 'Unknown resource package')
    require(set(manifest['files']) == set(OUTPUTS) and set(manifest['source_files']) == set(SOURCES),
            'Unexpected package scope')
    for relative, expected in manifest['files'].items():
        require(digest(plain(output / 'files' / relative)) == expected, 'Package file differs: ' + relative)
    for relative, expected in manifest['source_files'].items():
        require(digest(ROOT / relative) == expected, 'Source changed; rebuild: ' + relative)
    originals = inputs(tmp_root)
    require((output / 'files/data/shared_mod/ui.mp').read_bytes() == localized_ui(originals['data/shared/ui.mp']),
            'UI output does not match the translation profile')
    before, after = archive.verify(originals['data/shared/client.mp'],
                                   (output / 'files/data/shared_mod/client.mp').read_bytes(), 11, {9})
    translations = read_json(ROOT / 'profiles/freight-zh-cn.json')
    expected, captions = freight_candidate(json.loads(before[9]['raw']), translations, make_codebook(translations))
    require(json.loads(after[9]['raw']) == expected and manifest['freight_static_captions'] == captions,
            'Freight output differs beyond the intended text/font fields')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'verify', 'install', 'restore'))
    parser.add_argument('--tmp-root', type=Path, required=True, help='TruckersMP installation directory')
    parser.add_argument('--font', type=Path, help='User-supplied TrueType font; required for build')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/resources')
    args = parser.parse_args()
    args.tmp_root = args.tmp_root.absolute(); args.output = args.output.absolute()
    if args.action == 'build':
        require(args.font is not None, '--font is required for build')
        result = build(args.tmp_root, args.font, args.output)
    elif args.action == 'verify':
        result = dict(state='verified', files=len(verify(args.tmp_root, args.output)['files']))
    else:
        from resource_transaction import install, restore
        result = (install if args.action == 'install' else restore)(args.tmp_root, args.output)
    print(json.dumps(result, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
