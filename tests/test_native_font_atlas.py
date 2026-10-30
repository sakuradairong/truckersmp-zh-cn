"""Atlas overflow regression; the full resource font must remain separate."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_font_atlas as atlas
from common import ROOT


class Packing(unittest.TestCase):
    def test_horizontal_safety_rejection_is_not_native_height_stop(self):
        glyphs = [(32, 1004, 8), (33, 17, 8), (34, 8, 8)]
        safety = atlas.packing(glyphs, 14)
        native = atlas.packing(glyphs, 14, conservative=False)
        self.assertEqual(safety['reason'], 'horizontal_bounds')
        self.assertEqual(safety['stop'], 33)
        self.assertLess(safety['height'], 1024)
        self.assertIsNone(native['stop'])
        self.assertEqual(native['loaded'], {32, 33, 34})

    def test_wrapping_keeps_previous_row_maximum(self):
        loaded, height, overflow = atlas.pack([(32, 490, 20), (33, 490, 11), (34, 60, 12)], 40)
        self.assertEqual(loaded, {32, 33, 34})
        self.assertEqual(height, 37)
        self.assertIsNone(overflow)

    def test_height_overflow_stops_later_unicode(self):
        loaded, _, overflow = atlas.pack([(cp, 1000, 100) for cp in range(32, 50)], 40)
        self.assertEqual(overflow, 42)
        self.assertNotIn(43, loaded)

    def test_required_set_includes_all_settings_and_printable_ascii(self):
        needed = atlas.settings_characters()
        self.assertTrue(set(range(32, 127)) <= needed)
        self.assertTrue(set(map(ord, '设置常规恢复默认')) <= needed)
        self.assertEqual(len(needed), 384)

    def test_real_compact_faces_fit_all_supported_sizes(self):
        for name in atlas.FONT_NAMES:
            path = ROOT / 'build/installer-assets' / name
            if not path.is_file():
                self.skipTest('Prepare installer font assets for real-font regression')
            result = atlas.validate(path.read_bytes())
            self.assertTrue(result['passed'])
            self.assertEqual(len(result['sizes']), len(atlas.SIZES))

    def test_previous_full_font_overflows_native_atlas(self):
        path = ROOT / 'build/installer-assets/TMPZHSans-Regular.ttf'
        if not path.is_file():
            self.skipTest('Prepare full installer font assets for overflow regression')
        with self.assertRaisesRegex(ValueError, 'atlas overflow'):
            atlas.validate(path.read_bytes(), sizes=(21,))

    def test_freight_faces_cover_base_game_including_latin_fallback(self):
        needed = atlas.freight_characters()
        self.assertTrue(set(map(ord, '型内燃机车ı')) <= needed)
        for name in atlas.FREIGHT_FONT_NAMES:
            path = ROOT / 'build/installer-assets' / name
            if not path.is_file():
                self.skipTest('Prepare freight font assets')
            result = atlas.validate(path.read_bytes(), atlas.FREIGHT_SIZES, needed=needed)
            self.assertTrue(all(row['missing'] == 0 and row['height'] <= 1024 for row in result['sizes']))


if __name__ == '__main__':
    unittest.main()
