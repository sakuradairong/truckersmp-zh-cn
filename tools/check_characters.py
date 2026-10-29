"""Check supplied fonts and translation characters without starting the game."""
from __future__ import annotations
import argparse
import io
import json
from pathlib import Path
import struct
from PIL import ImageFont
import font_cmap as fonts
from common import ROOT, atomic_json, digest, read_json, require


def phrases():
    catalog = read_json(ROOT / 'profiles/settings-zh-cn.json')
    result = [catalog['window'][1], *(pair[1] for pair in catalog['buttons'])]
    for page in catalog['pages']:
        result.append(page['title'][1])
        for row in page['rows']:
            result.append(row['label'][1])
            if 'tooltip' in row:
                result.append(row['tooltip'][1])
            result.extend(pair[1] for pair in row.get('options', []))
    result.extend(read_json(ROOT / 'profiles/freight-zh-cn.json').values())
    result.extend(edit['text'] for entry in read_json(ROOT / 'profiles/resource-edits.json')['ui_entries']
                  for edit in entry['edits'])
    return result


def validate(font_data):
    texts = phrases()
    characters = sorted({char for text in texts for char in text if ord(char) > 127})
    tables = fonts.tables_from(font_data)
    require(b'fvar' not in tables and b'glyf' in tables and b'loca' in tables,
            'Use a standalone static TrueType font with glyf outlines')
    maps = fonts.unicode_cmaps(tables[b'cmap'])
    glyph_count = struct.unpack_from('>H', tables[b'maxp'], 4)[0]
    require(all(0 < mapping.get(ord(char), 0) < glyph_count for _, _, mapping, _ in maps for char in characters),
            'Supplied font is missing required Chinese characters')
    for text in texts:
        require(text.encode('utf-16-le').decode('utf-16-le') == text, 'UTF-16 round trip failed')
    checks = 0
    for size in (11, 14, 18, 21):
        font = ImageFont.truetype(io.BytesIO(font_data), size, layout_engine=ImageFont.Layout.BASIC)
        for char in characters:
            require(any(bytes(font.getmask(char, mode='L'))), 'Empty glyph for ' + repr(char))
            checks += 1
    # Settings captions have a reviewed 295px column at 14px. Tooltips wrap independently.
    font = ImageFont.truetype(io.BytesIO(font_data), 14, layout_engine=ImageFont.Layout.BASIC)
    catalog = read_json(ROOT / 'profiles/settings-zh-cn.json')
    widths = [(row['id'], font.getlength(row['label'][1])) for page in catalog['pages'] for row in page['rows']]
    overflow = [name for name, width in widths if width > catalog['layout']['label_width']]
    require(not overflow, 'Settings label overflow: ' + ', '.join(overflow))
    return dict(passed=True, phrases=len(texts), unique_characters=len(characters),
                glyph_rasters=checks, sizes=[11, 14, 18, 21], utf16_roundtrips=True,
                settings_label_overflows=overflow, settings_label_max_width=max(width for _, width in widths),
                settings_label_available_width=catalog['layout']['label_width'], game_started=False,
                limits='Offline font metrics do not prove live game rendering or layout.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, required=True, help='User-supplied standalone TrueType font')
    parser.add_argument('--report', type=Path, default=ROOT / 'build/character-checks.json')
    args = parser.parse_args()
    report = validate(args.font.read_bytes())
    report['font_sha256'] = digest(args.font)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    atomic_json(args.report, report)
    print(json.dumps(report, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
