"""Portable tests with original synthetic data; no TMP assets are required."""
import copy
import json
from pathlib import Path
import struct
import sys
import shutil
import unittest
from unittest.mock import patch
import uuid
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import archive
from common import ROOT, read_json, sha
import resources


def synthetic_archive(payloads=None):
    payloads = payloads or [(100, b'untouched synthetic payload'), (101, b'English synthetic text')]
    data = bytearray(b'TMP#\x01\x00\x00\x00CITY' + struct.pack('<IQ', len(payloads), 32) + b'\0' * 8)
    data.extend(b'\0' * (len(payloads) * archive.ENTRY.size))
    for index, (path_hash, raw) in enumerate(sorted(payloads)):
        packed = zlib.compress(raw)
        offset = len(data); data.extend(packed)
        archive.ENTRY.pack_into(data, 32 + index * archive.ENTRY.size, path_hash,
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
    def test_text_targets_survive_newlines_attributes_and_unrelated_additions(self):
        entry = dict(name='synthetic.rml', edits=[dict(selector={'attrs': {'id': 'caption'}},
                     source='Hello world', text='你好')])
        for newline in ('\n', '\r\n'):
            source = '<!-- new -->\n<div id="caption" data-if="online">Hello\n world</div><p>{{dynamic}}</p>'
            raw = source.replace('\n', newline).encode()
            expected = '<!-- new -->\n<div id="caption" data-if="online">你好</div><p>{{dynamic}}</p>'
            self.assertEqual(resources.apply_edits(raw, entry), expected.replace('\n', newline).encode())

    def test_missing_or_ambiguous_controls_fail_without_falling_back_to_offsets(self):
        entry = dict(name='synthetic.rml', edits=[dict(selector={'attrs': {'id': 'caption'}},
                     source='Hello', text='你好')])
        for raw in (b'<div>Unrelated</div>', b'<div id="caption">Hello</div>' * 2):
            with self.assertRaisesRegex(ValueError, 'Incompatible resource synthetic.rml: Missing or ambiguous'):
                resources.apply_edits(raw, entry)

    def test_attributes_and_bindings_preserve_identifiers_and_actions(self):
        raw = b'<input data-value="query" placeholder="Search players..." /><button id="history">{{old ? \'Close\' : \'Open\'}}</button>'
        entry = dict(name='synthetic.rml', edits=[
            dict(mode='attribute', selector={'tag': 'input', 'attrs': {'data-value': 'query'}},
                 attribute='placeholder', source='Search players...', text='搜索玩家'),
            dict(mode='literal', selector={'attrs': {'id': 'history'}}, source="'Close'", text="'关闭'")])
        result = resources.apply_edits(raw, entry).decode()
        self.assertIn('data-value="query"', result)
        self.assertIn("{{old ? '关闭' : 'Open'}}", result)
        self.assertIn('placeholder="搜索玩家"', result)

    def test_style_overrides_keep_upstream_declarations_and_other_rules(self):
        raw = b'/* new */ #caption { color: red; new-property: upstream; }\n#other { width: 99dp; }'
        entry = dict(name='synthetic.rcss', edits=[], styles=[dict(selector='#caption', properties={'color': 'blue'})])
        result = resources.apply_edits(raw, entry)
        self.assertTrue(result.startswith(raw))
        self.assertIn(b'color: blue;', result)
        with self.assertRaisesRegex(ValueError, 'Missing style selector'):
            resources.apply_edits(b'#unrelated { color: red; }', entry)

    def test_archive_entries_are_found_by_identity_after_insertions(self):
        profile = read_json(ROOT / 'profiles/resource-edits.json')
        payloads = [(int(entry['path_hash'], 16), entry['name'].encode()) for entry in profile['ui_entries']]
        payloads.extend([(1, b'new untouched entry'), (2, b'another unrelated entry')])
        source = synthetic_archive(payloads)
        with patch.object(resources, 'apply_edits', side_effect=lambda raw, entry: raw + b' translated'):
            result = resources.localized_ui(source)
        before, after = archive.parse(source), archive.parse(result)
        self.assertEqual(len(before), 9)
        self.assertEqual(before[0]['packed'], after[0]['packed'])
        self.assertEqual(before[1]['packed'], after[1]['packed'])
        for old, new in zip(before[2:], after[2:]):
            self.assertEqual(old['path_hash'], new['path_hash'])
            self.assertEqual(new['raw'], old['raw'] + b' translated')

    def test_inputs_do_not_gate_on_a_core_version_or_content_hash(self):
        root = ROOT / 'build' / ('input-check-' + uuid.uuid4().hex)
        root.mkdir(parents=True)
        try:
            for name in resources.INPUTS:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b'new source content')
            (root / 'core_ets2mp.dll').write_bytes(b'unknown future client')
            self.assertEqual(set(resources.inputs(root)), set(resources.INPUTS))
            (root / 'core_ets2mp.dll').write_bytes(b'another client version')
            self.assertEqual(set(resources.inputs(root).values()), {b'new source content'})
        finally:
            self.assertEqual(root.resolve().parent, (ROOT / 'build').resolve())
            shutil.rmtree(root)

    def test_profiles_have_only_reviewed_localization_scope(self):
        profile = read_json(ROOT / 'profiles/resource-edits.json')
        self.assertEqual(profile['schema'], 2)
        self.assertEqual({entry['name'] for entry in profile['ui_entries']}, resources.UI_NAMES)
        self.assertFalse(any('sha256' in key for key in profile))
        for entry in profile['ui_entries']:
            self.assertNotIn('index', entry)
            self.assertNotIn('source_sha256', entry)
            self.assertFalse(any('start' in edit or 'stop' in edit for edit in entry['edits']))
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
        for index, (kind, name, english) in enumerate(resources.FREIGHT_TARGETS):
            key = 'CheckboxText' if kind == 'CheckBoxWithLabel' else 'Text'
            controls.append(dict(Type=kind, Properties={key: english, 'Font': 'Original.ttf',
                                 'Size': [200, 24], 'FontSize': 14, 'ControlName': name}, Action='original'))
        controls.append(dict(Type='Label', Properties={'Text': '{player.name}', 'Font': 'Original.ttf'}))
        controls.append(dict(Type='Button', Properties={'Text': 'New upstream action', 'ControlName': 'newAction'}))
        source = dict(Controls=dict(Type='Window', Properties={'Size': [800, 600]}, Children=controls))
        snapshot = copy.deepcopy(source)
        result, captions = resources.freight_candidate(source, translations, codebook)
        self.assertEqual(source, snapshot)
        self.assertEqual(len(captions), 17)
        self.assertEqual(result['Controls']['Children'][-1], source['Controls']['Children'][-1])
        self.assertEqual(result['Controls']['Children'][-2], source['Controls']['Children'][-2])
        self.assertEqual(result['Controls']['Properties'], source['Controls']['Properties'])
        for old, new in zip(source['Controls']['Children'][:17], result['Controls']['Children'][:17]):
            self.assertEqual(old['Action'], new['Action'])
            self.assertEqual(old['Properties']['Size'], new['Properties']['Size'])
        bad = copy.deepcopy(source)
        bad['Controls']['Children'][0]['Properties']['Text'] = 'unreviewed'
        with self.assertRaisesRegex(ValueError, 'Incompatible freight'):
            resources.freight_candidate(bad, translations, codebook)
        bad = copy.deepcopy(source)
        bad['Controls']['Children'].append(copy.deepcopy(controls[1]))
        with self.assertRaisesRegex(ValueError, 'ambiguous control'):
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
