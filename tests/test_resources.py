"""Portable tests with original synthetic data; no TMP assets are required."""
import copy
import json
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import archive
from common import ROOT, read_json, sha
import resources


def synthetic_archive():
    payloads = [b'untouched synthetic payload', b'English synthetic text']
    data = bytearray(b'TMP#\x01\x00\x00\x00CITY' + struct.pack('<IQ', 2, 32) + b'\0' * 8)
    data.extend(b'\0' * (2 * archive.ENTRY.size))
    for index, raw in enumerate(payloads):
        packed = zlib.compress(raw)
        offset = len(data); data.extend(packed)
        archive.ENTRY.pack_into(data, 32 + index * archive.ENTRY.size, 100 + index,
                                offset, 14, 0x12345678, len(raw), len(packed))
    return bytes(data)


class ArchiveChecks(unittest.TestCase):
    def test_repack_preserves_metadata_and_unrelated_compressed_data(self):
        source = synthetic_archive()
        result = archive.repack(source, 2, {1: '中文'.encode()})
        old, new = archive.verify(source, result, 2, {1})
        self.assertEqual(old[0]['packed'], new[0]['packed'])
        self.assertEqual(new[1]['raw'].decode(), '中文')
        self.assertEqual(old[1]['opaque'], new[1]['opaque'])

    def test_malformed_containers_are_rejected(self):
        data = synthetic_archive()
        bad_magic = b'NOPE' + data[4:]
        bad_offset = bytearray(data); struct.pack_into('<Q', bad_offset, 40, 0)
        corrupt_zlib = bytearray(data); corrupt_zlib[-1] ^= 0xff
        for candidate in (bad_magic, bytes(bad_offset), bytes(corrupt_zlib), data[:-1], data + b'x'):
            with self.subTest(length=len(candidate)):
                with self.assertRaises((ValueError, zlib.error)):
                    archive.parse(candidate, 2)

    def test_unexpected_replacement_scope_is_rejected(self):
        with self.assertRaises(ValueError):
            archive.repack(synthetic_archive(), 2, {2: b'outside'})


class TranslationChecks(unittest.TestCase):
    def test_offset_edits_are_bound_to_source_and_result(self):
        raw = b'hello\r\nworld'
        entry = dict(name='synthetic', source_sha256=sha(raw), result_sha256=sha('你好\nworld'.encode()),
                     edits=[dict(start=0, stop=5, text='你好')])
        self.assertEqual(resources.apply_edits(raw, entry), '你好\nworld'.encode())
        crlf = dict(entry, newline='crlf', result_sha256=sha('你好\r\nworld'.encode()))
        self.assertEqual(resources.apply_edits(raw, crlf), '你好\r\nworld'.encode())
        with self.assertRaisesRegex(ValueError, 'source differs'):
            resources.apply_edits(raw + b'x', entry)
        bad = copy.deepcopy(entry); bad['edits'][0]['start'] = 100
        with self.assertRaisesRegex(ValueError, 'invalid'):
            resources.apply_edits(raw, bad)
        bad = copy.deepcopy(entry); bad['edits'][0]['text'] = 'wrong'
        with self.assertRaisesRegex(ValueError, 'result differs'):
            resources.apply_edits(raw, bad)

    def test_profiles_have_only_reviewed_localization_scope(self):
        profile = read_json(ROOT / 'profiles/resource-edits.json')
        self.assertEqual({entry['index'] for entry in profile['ui_entries']}, {3, 4, 8, 10, 12, 13, 17})
        settings = read_json(ROOT / 'profiles/settings-zh-cn.json')
        self.assertEqual([page['id'] for page in settings['pages']], ['general', 'sound', 'game', 'graphics', 'interface'])
        self.assertEqual(sum(len(page['rows']) for page in settings['pages']), 52)
        for name in ('settings-zh-cn.json', 'resource-edits.json', 'freight-zh-cn.json'):
            text = (ROOT / 'profiles' / name).read_text(encoding='utf-8')
            self.assertNotIn('\ufffd', text)
            self.assertFalse(text.startswith('\ufeff'))

    def test_freight_keeps_dynamic_text_geometry_and_actions(self):
        translations = read_json(ROOT / 'profiles/freight-zh-cn.json')
        codebook = resources.make_codebook(translations)
        controls = []
        for index, english in enumerate(list(translations)[:17]):
            kind = ('GroupBox', 'Button', 'CheckBoxWithLabel')[index % 3]
            key = 'CheckboxText' if kind == 'CheckBoxWithLabel' else 'Text'
            controls.append(dict(Type=kind, Properties={key: english, 'Font': 'Original.ttf',
                                 'Size': [200, 24], 'FontSize': 14, 'ControlName': str(index)}, Action='original'))
        controls.append(dict(Type='Label', Properties={'Text': '{player.name}', 'Font': 'Original.ttf'}))
        source = dict(Controls=dict(Type='Window', Properties={'Size': [800, 600]}, Children=controls))
        snapshot = copy.deepcopy(source)
        result, captions = resources.freight_candidate(source, translations, codebook)
        self.assertEqual(source, snapshot)
        self.assertEqual(len(captions), 17)
        self.assertEqual(result['Controls']['Children'][-1], source['Controls']['Children'][-1])
        self.assertEqual(result['Controls']['Properties'], source['Controls']['Properties'])
        for old, new in zip(source['Controls']['Children'][:17], result['Controls']['Children'][:17]):
            self.assertEqual(old['Action'], new['Action'])
            self.assertEqual(old['Properties']['Size'], new['Properties']['Size'])
        bad = copy.deepcopy(source)
        bad['Controls']['Children'][0]['Properties']['Text'] = 'unreviewed'
        with self.assertRaisesRegex(ValueError, 'Unexpected freight'):
            resources.freight_candidate(bad, translations, codebook)

    def test_ascii_codebook_is_unique_and_byte_widening_safe(self):
        import font_cmap
        codebook = resources.make_codebook(read_json(ROOT / 'profiles/freight-zh-cn.json'))
        self.assertEqual(len(codebook), len(set(codebook.values())))
        for token in codebook.values():
            self.assertEqual(font_cmap.widen(token), token)
            self.assertEqual(font_cmap.widen(token, True), token)
        bad = dict(codebook); keys = list(bad); bad[keys[1]] = bad[keys[0]]
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            font_cmap.validate_codebook(bad, keys)


if __name__ == '__main__':
    unittest.main()
