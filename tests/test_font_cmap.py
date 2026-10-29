"""Mixed Unicode/legacy cmap regression checks; no third-party fonts needed."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import font_cmap as cmap


def mixed_cmap():
    bmp = cmap.cmap4_from({65: 3, 0x4E2D: 7}, 0)
    full = cmap.cmap12_from({65: 3, 0x4E2D: 7, 0x1F600: 10}, 0)
    legacy = struct.pack('>5H', 6, 12, 0, 65, 1) + struct.pack('>H', 3)
    variation = struct.pack('>HII', 14, 10, 0)
    parts = [(0, 3, bmp), (3, 10, full), (1, 0, legacy), (0, 5, variation)]
    directory, payload = bytearray(struct.pack('>HH', 0, len(parts))), bytearray()
    for platform, encoding, data in parts:
        directory.extend(struct.pack('>HHI', platform, encoding, 4 + len(parts) * 8 + len(payload)))
        payload.extend(data)
    return bytes(directory + payload)


class UnicodeCmaps(unittest.TestCase):
    def test_remap_preserves_supplementary_legacy_and_variation_tables(self):
        source = mixed_cmap()
        result = cmap.remapped_cmap(source, {65: 7})
        before, after = cmap.cmap_records(source), cmap.cmap_records(result)
        self.assertEqual(before[2:], after[2:])
        maps = cmap.unicode_cmaps(result)
        self.assertEqual(len(maps), 2)
        self.assertTrue(all(row[2][65] == 7 and row[2][0x4E2D] == 7 for row in maps))
        self.assertEqual(maps[1][2][0x1F600], 10)

    def test_format12_round_trip_and_invalid_ranges(self):
        mapping = {65: 3, 66: 4, 0x4E2D: 7, 0x20000: 9}
        self.assertEqual(cmap.read_cmap12(cmap.cmap12_from(mapping, 0)), (mapping, 0))
        for start, end, glyph in ((10, 9, 1), (0x110000, 0x110000, 1), (1, 10, 65535)):
            data = struct.pack('>HHIIIIII', 12, 0, 28, 0, 1, start, end, glyph)
            with self.assertRaises(ValueError):
                cmap.read_cmap12(data)

    def test_truncated_cmap_directory_and_offsets_are_rejected(self):
        for data in (b'', b'\0\0\0\x01', struct.pack('>HHHHI', 0, 1, 0, 3, 9999)):
            with self.assertRaises(ValueError):
                cmap.cmap_records(data)


if __name__ == '__main__':
    unittest.main()
