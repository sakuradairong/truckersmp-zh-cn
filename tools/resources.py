"""Build resource localization from compatible local TMP resources and a font."""
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
import resource_text
import native_font_atlas
from common import (ROOT, atomic_json, digest, plain,
                    read_json, require, sha, write_output)

FONT_NAME = 'OfflineFreightZH.ttf'
INPUTS = ('data/shared/ui.mp', 'data/shared/client.mp')
FONT_ALIASES = ('OpenSans.ttf', 'fonts/OpenSans.ttf', 'fonts/OpenSans-Semibold.ttf', 'fonts/RobotoMono.ttf')
LEGACY_OUTPUTS = ('data/shared_mod/ui.mp', 'data/shared_mod/client.mp',
           *(f'data/shared_mod/{name}' for name in (*FONT_ALIASES, FONT_NAME, 'fonts/' + FONT_NAME)))
SETTINGS_FONTS = tuple('data/shared_mod/fonts/' + name for name in native_font_atlas.FONT_NAMES)
PRE_FREIGHT_OUTPUTS = LEGACY_OUTPUTS + SETTINGS_FONTS
FREIGHT_FONTS = tuple('data/shared_mod/fonts/' + name for name in native_font_atlas.FREIGHT_FONT_NAMES)
OUTPUTS = PRE_FREIGHT_OUTPUTS + FREIGHT_FONTS
SOURCES = ('tools/resources.py', 'tools/archive.py', 'tools/font_cmap.py', 'tools/common.py',
           'tools/check_characters.py', 'profiles/resource-edits.json', 'profiles/freight-zh-cn.json',
           'profiles/settings-zh-cn.json', 'tools/resource_text.py', 'tools/native_font_atlas.py',
           'profiles/freight-characters.json', 'ui/player_filters.lua')
UI_NAMES = {'player_panel.rml', 'player_list.rml', 'player_panel.rcss', 'player_context_menu.rml',
            'bus_job_dispatcher.rml', 'bus_job_dispatcher.rcss', 'bus_job_dispatcher.lua', 'player_panel.lua'}
FREIGHT_PATH = 0xc3895edbd485993e
FREIGHT_TARGETS = (
    ('GroupBox', '', 'Source'), ('Button', 'sourceMap', 'Display Map'),
    ('GroupBox', '', 'Destination'), ('Button', 'targetMap', 'Display Map'),
    ('GroupBox', '', 'Cargo'),
    ('CheckBoxWithLabel', 'compatibleCargos', 'List only compatible cargos (between companies)'),
    ('Button', 'randomCargo', 'Pick Random Cargo'), ('GroupBox', '', 'Trailer'),
    ('Button', 'trailerPreview', 'Preview Trailer'),
    ('CheckBoxWithLabel', 'ownedTrailer', 'Use current owned trailer'),
    ('CheckBoxWithLabel', 'compatibleTrailers', 'List only trailers complying with the laws'),
    ('CheckBoxWithLabel', 'remoteTrailer', "Copy player's trailer"),
    ('Button', 'generate', 'Generate Job Offer'),
    ('CheckBoxWithLabel', 'takeJob', 'Take the job immediately'),
    ('CheckBoxWithLabel', 'navigateToCompany', 'Navigate to the company'),
    ('GroupBox', '', 'Job Information'), ('GroupBox', '', 'Urgency'))
FREIGHT_DYNAMIC = {'sourceCities': 'Autocomplete', 'sourceCompanies': 'ComboBox',
    'targetCities': 'Autocomplete', 'targetCompanies': 'ComboBox', 'cargos': 'Autocomplete',
    'trailers': 'ComboBox', 'urgency': 'ComboBox'}


def check_freight_dynamic(source):
    require(isinstance(source, dict) and isinstance(source.get('Controls'), dict),
            'Incompatible freight: invalid dynamic control root')
    found = set()
    def visit(node):
        require(isinstance(node, dict) and isinstance(node.get('Properties', {}), dict) and
                isinstance(node.get('Children', []), list), 'Incompatible freight: invalid dynamic control tree')
        name = node.get('Properties', {}).get('ControlName', '')
        if name in FREIGHT_DYNAMIC:
            require(name not in found and node.get('Type') == FREIGHT_DYNAMIC[name],
                    'Incompatible freight: ambiguous or changed dynamic control ' + name)
            found.add(name)
        for child in node.get('Children', []):
            visit(child)
    visit(source['Controls'])
    require(found == set(FREIGHT_DYNAMIC),
            'Incompatible freight: missing dynamic controls ' + repr(sorted(set(FREIGHT_DYNAMIC) - found)))


def inputs(tmp_root):
    result = {}
    for relative in INPUTS:
        path = plain(tmp_root / relative)
        data = path.read_bytes()
        result[relative] = data
    return result


def apply_edits(raw, entry):
    if entry['name'] == 'player_panel.lua':
        text = raw.decode('utf-8')
        require(text.count('function OnLoad(document)') == 1 and
                all('PlayerPanel.' + name in text for name in ('SearchPlayers', 'SortPlayers', 'ToggleHistory')) and
                'zh_player_filters' not in text, 'Incompatible resource player_panel.lua: native callbacks changed')
        newline = b'\r\n' if b'\r\n' in raw else b'\n'
        extension = (ROOT / 'ui/player_filters.lua').read_bytes().replace(b'\r\n', b'\n')
        return raw + newline + extension.replace(b'\n', newline)
    return resource_text.apply(raw, entry)


def localized_ui(source):
    profile = read_json(ROOT / 'profiles/resource-edits.json')
    require(profile['schema'] == 2, 'Unknown resource profile schema')
    entries = profile['ui_entries']
    require(len(entries) == 8 and {entry['name'] for entry in entries} == UI_NAMES and
            len({entry['path_hash'] for entry in entries}) == len(entries),
            'Unexpected UI translation scope')
    rows = archive.parse(source)
    by_path = {row['path_hash']: row for row in rows}
    replacements = {}
    for entry in entries:
        row = by_path.get(int(entry['path_hash'], 16))
        require(row is not None, 'Incompatible resource: missing ' + entry['name'])
        replacements[row['index']] = apply_edits(row['raw'], entry)
    return archive.repack(source, len(rows), replacements)


def freight_row(source):
    rows = archive.parse(source)
    row = next((row for row in rows if row['path_hash'] == FREIGHT_PATH), None)
    require(row is not None, 'Incompatible resource: missing freight window')
    return rows, row


def check_compatibility(tmp_root):
    originals = inputs(tmp_root)
    localized_ui(originals['data/shared/ui.mp'])
    _, row = freight_row(originals['data/shared/client.mp'])
    translations = read_json(ROOT / 'profiles/freight-zh-cn.json')
    freight_candidate(json.loads(row['raw']), translations, make_codebook(translations))
    check_freight_dynamic(json.loads(row['raw']))
    return originals


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
    # This face is used only by encoded static captions. Retaining the original
    # CJK cmap makes the native renderer preload thousands of unused glyphs.
    printable = set(range(32, 127))
    updated[b'cmap'] = fonts.remapped_cmap(tables[b'cmap'], replacements, codepoints=printable)
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
        expected = {cp: glyph for cp, glyph in expected.items() if cp in printable}
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
    require(isinstance(source, dict) and isinstance(source.get('Controls'), dict),
            'Incompatible freight: missing control tree')
    result = copy.deepcopy(source)
    allowed, captions, found = set(), [], set()
    named = {name: (kind, name, text) for kind, name, text in FREIGHT_TARGETS if name}
    groups = {text: (kind, name, text) for kind, name, text in FREIGHT_TARGETS if not name}

    def visit(node, path):
        require(isinstance(node, dict) and isinstance(node.get('Type'), str) and
                isinstance(node.get('Properties', {}), dict) and isinstance(node.get('Children', []), list),
                'Incompatible freight: invalid control structure')
        properties = node.get('Properties', {})
        kind = node['Type']
        name = properties.get('ControlName', '')
        require(isinstance(name, str), 'Incompatible freight: invalid control name')
        title = properties.get('Text')
        target = named.get(name) if name else groups.get(title) if kind == 'GroupBox' and isinstance(title, str) else None
        if target is not None:
            require(target not in found and kind == target[0], 'Incompatible freight: ambiguous control ' + str(target))
            found.add(target)
            key = 'CheckboxText' if kind == 'CheckBoxWithLabel' else 'Text'
            english = properties.get(key)
            require(english == target[2], 'Incompatible freight caption: ' + str(english))
            chinese = translations[target[2]]
            properties[key] = ''.join(codebook.get(char, char) for char in chinese)
            properties['Font'] = FONT_NAME
            allowed.update((path + ('Properties', key), path + ('Properties', 'Font')))
            captions.append({'english': english, 'chinese': chinese, 'control': properties.get('ControlName', '')})
        for index, child in enumerate(node.get('Children', [])):
            visit(child, path + ('Children', index))

    visit(result['Controls'], ('Controls',))
    require(found == set(FREIGHT_TARGETS),
            'Incompatible freight: missing controls ' + repr(sorted(set(FREIGHT_TARGETS) - found)))
    require(len(captions) == 17 and len(allowed) == 34, 'Unexpected freight caption count')
    require(set(fonts.changes(source, result)) == allowed, 'Freight changes exceed static text/font fields')
    return result, captions


def build(tmp_root, font_path, output, *, semibold=None, mono=None, settings_fonts=None, freight_fonts=None):
    plain(tmp_root); plain(font_path)
    output = plain(output, missing=True)
    require(not output.is_relative_to(tmp_root.absolute()), 'Build output cannot be inside the TMP installation')
    originals = inputs(tmp_root)
    font_data = font_path.read_bytes()
    character_checks = check_characters.validate(font_data)
    font_inputs = {font_path: font_data}
    alias_fonts = {name: font_data for name in FONT_ALIASES}
    for alias, path in (('fonts/OpenSans-Semibold.ttf', semibold), ('fonts/RobotoMono.ttf', mono)):
        if path is not None:
            plain(path)
            content = Path(path).read_bytes()
            check_characters.validate(content)
            font_inputs[Path(path)] = content
            alias_fonts[alias] = content
    translations = read_json(ROOT / 'profiles/freight-zh-cn.json')
    codebook = make_codebook(translations)
    remapped_font, raster_checks = remap_font(font_data, codebook)
    ui = localized_ui(originals['data/shared/ui.mp'])
    client_source = originals['data/shared/client.mp']
    client_rows, row = freight_row(client_source)
    source_tree = json.loads(row['raw'])
    check_freight_dynamic(source_tree)
    candidate, captions = freight_candidate(source_tree, translations, codebook)
    payload = (json.dumps(candidate, ensure_ascii=True, indent=2) + '\n').encode('ascii')
    client = archive.repack(client_source, len(client_rows), {row['index']: payload})
    data = {'data/shared_mod/ui.mp': ui, 'data/shared_mod/client.mp': client}
    data.update({f'data/shared_mod/{name}': content for name, content in alias_fonts.items()})
    data.update({f'data/shared_mod/{name}': remapped_font for name in (FONT_NAME, 'fonts/' + FONT_NAME)})
    atlas_checks = {'data/shared_mod/fonts/' + FONT_NAME:
        native_font_atlas.validate(remapped_font, needed=set(range(32, 127)))}
    require(settings_fonts is None or len(settings_fonts) == 2, 'Expected two Settings font faces')
    for index, relative in enumerate(SETTINGS_FONTS):
        if settings_fonts is None:
            source = font_data if index == 0 else alias_fonts['fonts/OpenSans-Semibold.ttf']
            content = native_font_atlas.compact(source, bool(index))
        else:
            path = plain(Path(settings_fonts[index]))
            content = path.read_bytes(); font_inputs[path] = content
        atlas_checks[relative] = native_font_atlas.validate(content)
        data[relative] = content
    require(freight_fonts is None or len(freight_fonts) == 2, 'Expected two freight font faces')
    for index, relative in enumerate(FREIGHT_FONTS):
        if freight_fonts is None:
            source = font_data if index == 0 else alias_fonts['fonts/OpenSans-Semibold.ttf']
            content = native_font_atlas.compact(source, bool(index),
                characters=native_font_atlas.freight_characters(), family='TMPZH Freight', postscript='TMPZHFreight')
        else:
            path = plain(Path(freight_fonts[index]))
            content = path.read_bytes(); font_inputs[path] = content
        atlas_checks[relative] = native_font_atlas.validate(content, native_font_atlas.FREIGHT_SIZES,
            needed=native_font_atlas.freight_characters())
        data[relative] = content
    require(set(data) == set(OUTPUTS), 'Unexpected output file scope')
    # Re-read all installed inputs before writing only local build outputs.
    require(inputs(tmp_root) == originals, 'Resource input changed while building; retry with the client closed')
    require(all(path.read_bytes() == content for path, content in font_inputs.items()), 'Font changed while building')
    for relative, content in data.items():
        write_output(output / 'files' / relative, content)
    manifest = dict(schema=3, package='truckersmp-zh-cn-resources',
                    inputs={name: sha(content) for name, content in originals.items()},
                    files={name: sha(content) for name, content in data.items()},
                    source_files={name: digest(ROOT / name) for name in SOURCES},
                    character_checks=character_checks, freight_glyph_rasters=raster_checks,
                    native_font_atlas=atlas_checks,
                    freight_static_captions=captions, game_started=False,
                    live_validation='Public extraction verified offline; native Settings not verified in-game.')
    atomic_json(output / 'package.json', manifest)
    verify(tmp_root, output)
    return dict(state='built', files=len(data), ui_entries=sorted(UI_NAMES), freight_captions=len(captions),
                freight_glyph_rasters=raster_checks, character_checks=character_checks)


def verify(tmp_root, output, *, font_paths=None):
    plain(output)
    manifest = read_json(output / 'package.json')
    require(manifest['schema'] == 3 and manifest['package'] == 'truckersmp-zh-cn-resources' and
            set(manifest['inputs']) == set(INPUTS), 'Unknown resource package')
    require(set(manifest['files']) == set(OUTPUTS) and set(manifest['source_files']) == set(SOURCES),
            'Unexpected package scope')
    for relative, expected in manifest['files'].items():
        require(digest(plain(output / 'files' / relative)) == expected, 'Package file differs: ' + relative)
    if font_paths is not None:
        require(len(font_paths) in (3, 5, 7), 'Unexpected font input scope')
        for alias, path in zip(('OpenSans.ttf', 'fonts/OpenSans-Semibold.ttf', 'fonts/RobotoMono.ttf',
                                *('fonts/' + n for n in native_font_atlas.FONT_NAMES),
                                *('fonts/' + n for n in native_font_atlas.FREIGHT_FONT_NAMES)), font_paths):
            require(digest(plain(Path(path))) == manifest['files']['data/shared_mod/' + alias],
                    'Font input changed; rebuild the localization')
    require(manifest.get('native_font_atlas') == {
        'data/shared_mod/fonts/' + FONT_NAME: native_font_atlas.validate(
            (output / 'files/data/shared_mod/fonts' / FONT_NAME).read_bytes(), needed=set(range(32, 127))),
        **{name: native_font_atlas.validate((output / 'files' / name).read_bytes()) for name in SETTINGS_FONTS},
        **{name: native_font_atlas.validate((output / 'files' / name).read_bytes(), native_font_atlas.FREIGHT_SIZES,
             needed=native_font_atlas.freight_characters()) for name in FREIGHT_FONTS}},
        'Native Settings atlas checks differ')
    for relative, expected in manifest['source_files'].items():
        require(digest(ROOT / relative) == expected, 'Source changed; rebuild: ' + relative)
    originals = inputs(tmp_root)
    require(manifest['inputs'] == {name: sha(content) for name, content in originals.items()},
            'Resource input changed; rebuild the localization for the current files')
    require((output / 'files/data/shared_mod/ui.mp').read_bytes() == localized_ui(originals['data/shared/ui.mp']),
            'UI output does not match the translation profile')
    client_source = originals['data/shared/client.mp']
    rows, row = freight_row(client_source)
    index = row['index']
    before, after = archive.verify(client_source,
                                   (output / 'files/data/shared_mod/client.mp').read_bytes(), len(rows), {index})
    translations = read_json(ROOT / 'profiles/freight-zh-cn.json')
    expected, captions = freight_candidate(json.loads(before[index]['raw']), translations, make_codebook(translations))
    require(json.loads(after[index]['raw']) == expected and manifest['freight_static_captions'] == captions,
            'Freight output differs beyond the intended text/font fields')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'verify', 'install', 'restore'))
    parser.add_argument('--tmp-root', type=Path, required=True, help='TruckersMP installation directory')
    parser.add_argument('--font', type=Path, help='User-supplied TrueType font; required for build')
    parser.add_argument('--semibold', type=Path, help='Optional separate semibold TrueType font')
    parser.add_argument('--mono', type=Path, help='Optional Chinese font with monospaced Latin glyphs')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/resources')
    args = parser.parse_args()
    args.tmp_root = args.tmp_root.absolute(); args.output = args.output.absolute()
    if args.action == 'build':
        require(args.font is not None, '--font is required for build')
        result = build(args.tmp_root, args.font, args.output, semibold=args.semibold, mono=args.mono)
    elif args.action == 'verify':
        result = dict(state='verified', files=len(verify(args.tmp_root, args.output)['files']))
    else:
        from resource_transaction import install, restore
        result = (install if args.action == 'install' else restore)(args.tmp_root, args.output)
    print(json.dumps(result, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
