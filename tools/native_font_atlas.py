"""Conservative offline capacity check for the native 1024px BMP font atlas."""
import io
from PIL import ImageFont
import font_cmap
from common import read_json, ROOT, require

FONT_NAMES = ('TmpSettingsZH.ttf', 'TmpSettingsZH-Semibold.ttf')
SIZES = (11, 14, 18, 21, 28, 32, 42, 48)
FREIGHT_FONT_NAMES = ('TmpFreightZH.ttf', 'TmpFreightZH-Semibold.ttf')
FREIGHT_SIZES = (11, 14, 18, 21)


def freight_characters():
    profile = read_json(ROOT / 'profiles/freight-characters.json')
    points = profile.get('codepoints', [])
    require(profile.get('schema') == 1 and points == sorted(set(points)) and
            set(range(32, 127)) <= set(points) and
            all(isinstance(cp, int) and 32 <= cp < 0xffff for cp in points),
            'Invalid native freight character repertoire')
    return set(points)


def settings_characters():
    catalog = read_json(ROOT / 'profiles/settings-zh-cn.json')
    texts = [catalog['window'][1], *(p[1] for p in catalog['buttons'])]
    for page in catalog['pages']:
        texts.append(page['title'][1])
        for row in page['rows']:
            texts.append(row['label'][1])
            if 'tooltip' in row:
                texts.append(row['tooltip'][1])
            texts.extend(p[1] for p in row.get('options', []))
    return set(range(32, 127)) | {ord(c) for text in texts for c in text}


def compact(data, semibold=False, *, characters=None, family='TMPZH Settings', postscript='TMPZHSettings'):
    from fontTools import subset
    from fontTools.ttLib import TTFont
    font = TTFont(io.BytesIO(data), recalcTimestamp=False)
    needed = settings_characters() if characters is None else set(characters)
    require(needed <= set(font.getBestCmap()), 'Native font source is missing required characters')
    options = subset.Options()
    options.recalc_timestamp = False
    options.name_IDs = ['*']; options.name_legacy = True; options.name_languages = ['*']
    job = subset.Subsetter(options=options); job.populate(unicodes=needed); job.subset(font)
    style = 'SemiBold' if semibold else 'Regular'
    names = {1: family, 2: style, 3: family + '-' + style + '-1.0',
             4: family + ' ' + style, 6: postscript + '-' + style, 16: family, 17: style, 18: family + ' ' + style}
    for record in font['name'].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    for key, value in names.items():
        font['name'].setName(value, key, 3, 1, 0x409)
    result = io.BytesIO(); font.save(result, reorderTables=True)
    return result.getvalue()


def packing(glyphs, size, limit=1024, *, conservative=True):
    """Report a safety rejection separately from the native post-insertion stop.

    Even native shelf order still uses Pillow bounds here, not TMP's rasterizer.
    A stop codepoint is model evidence, not an observed in-game missing glyph.
    """
    x = y = 5; row_height = 0; loaded = set(); high_water = 5
    for cp, width, height in glyphs:
        require(width >= 0 and height >= 0, 'Invalid glyph bounds')
        reason = ('horizontal_bounds' if x + width > limit else
                  'vertical_bounds' if y + height > limit else None)
        if conservative and reason:
            return dict(loaded=loaded, height=max(high_water, y + height), stop=cp, reason=reason)
        loaded.add(cp); row_height = max(row_height, height); x += width
        high_water = max(high_water, y + row_height)
        if x + size >= limit:
            x = 5; y += row_height; row_height = 0
        if y + row_height > limit:
            return dict(loaded=loaded, height=y + row_height, stop=cp, reason='shelf_height')
    return dict(loaded=loaded, height=high_water, stop=None, reason=None)


def pack(glyphs, size, limit=1024):
    result = packing(glyphs, size, limit)
    return result['loaded'], result['height'], result['stop']


def validate(data, sizes=SIZES, *, needed=None):
    maps = font_cmap.unicode_cmaps(font_cmap.tables_from(data)[b'cmap'])
    characters = set().union(*(set(m) for _, _, m, _ in maps))
    needed = settings_characters() if needed is None else set(needed)
    require(needed <= characters, 'Native atlas font is missing required characters')
    require(all(cp < 0xffff for cp in needed), 'Native font renderer requires BMP characters')
    rows = []
    for size in sizes:
        font = ImageFont.truetype(io.BytesIO(data), size, layout_engine=ImageFont.Layout.BASIC)
        glyphs = ((cp, *font.getmask(chr(cp), mode='L').size)
                  for cp in sorted(characters) if 32 <= cp < 0xffff)
        result = packing(glyphs, size)
        loaded, height, overflow = result['loaded'], result['height'], result['stop']
        missing = needed - loaded
        require(overflow is None and not missing,
                f'Native font atlas overflow/rejection at {size}px; reason={result["reason"]}, '
                f'height={height}, model stop={hex(overflow) if overflow is not None else None}, '
                f'missing required characters={len(missing)}')
        rows.append(dict(px=size, loaded=len(loaded), height=height, missing=0))
    return dict(passed=True, atlas=[1024, 1024], characters=len(characters),
                required_characters=len(needed), sizes=rows, game_started=False,
                model='Conservative Pillow mask bounds with reviewed native BMP shelf order',
                limits='Checks conservative capacity at ' + ','.join(map(str, sizes)) +
                       'px; does not prove actual in-game font loading or drawing.')
