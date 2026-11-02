"""Optional real-PE data fixtures. Never loads or executes the client DLL.

Set TMPZH_TEST_CORE to the reviewed baseline and TMPZH_TEST_DLL to our build.
The ordinary unit suite skips these external-fixture tests when not requested.
"""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import settings_compatibility


class ReadOnlyContracts(unittest.TestCase):
    def exporter(self, relocation):
        from prepare_relocation import Exporter
        ex = Exporter.__new__(Exporter)
        ex.manifest = {}
        ex.base = 0x180000000
        ex.relocations = {relocation}
        data = bytearray(64)
        struct.pack_into('<Q', data, relocation, ex.base+0x1234)
        ex.raw = lambda at, size: bytes(data[at:at+size])
        ex.section = lambda at, size=1: 2
        return ex

    def test_trailing_partial_pointer_is_extended_and_target_checked(self):
        node = self.exporter(12).readonly(0)
        self.assertEqual(node['size'], 20)
        self.assertEqual(bytes.fromhex(node['mask']), b'\xff'*12+b'\0'*8)
        self.assertEqual(node['refs'], [dict(off=12, width=8, mode=0, next=0, target=0x1234)])

    def test_complete_pointer_keeps_original_window(self):
        node = self.exporter(8).readonly(0)
        self.assertEqual(node['size'], 16)
        self.assertEqual(node['refs'][0]['target'], 0x1234)

    def test_leading_partial_pointer_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'inside a relocated pointer'):
            self.exporter(0).readonly(4)

    def test_extended_contract_cannot_cross_section_boundary(self):
        ex = self.exporter(12)
        ex.section = lambda at, size=1: 2 if size <= 16 else 3
        with self.assertRaisesRegex(ValueError, 'section boundary'):
            ex.readonly(0)


def move_assign(data, profile):
    """Relocate one real function and checked references to a new code section."""
    import pefile
    pe = pefile.PE(data=data)
    node = next(n for n in profile['nodes'] if n['old'] == 0x82020)
    source, size = node['old'], node['size']
    align = lambda n, a: (n+a-1)//a*a
    new_rva = align(pe.OPTIONAL_HEADER.SizeOfImage+0x10000, 0x1000)
    raw_at = align(len(data), pe.OPTIONAL_HEADER.FileAlignment)
    raw_size = align(size, pe.OPTIONAL_HEADER.FileAlignment)
    out = bytearray(data)+bytearray(raw_at+raw_size-len(data))
    out[raw_at:raw_at+size] = pe.get_data(source, size)
    source_at = pe.get_offset_from_rva(source)
    out[source_at:source_at+size] = b'\xcc'*size
    def moved(at):
        return new_rva+at-source if source <= at < source+size else at
    def offset(at):
        return raw_at+at-new_rva if new_rva <= at < new_rva+size else pe.get_offset_from_rva(at)
    patches = {}
    def patch(at, encoded):
        pos = offset(at)
        if pos in patches:
            assert patches[pos] == encoded, 'Conflicting relocation evidence'
        patches[pos] = encoded
    specs = [(n['old'], n) for n in profile['nodes']]
    specs += [(int(at), n) for at, n in profile['contracts'].items()]
    for at, spec in specs:
        for ref in spec.get('refs', []):
            target = moved(ref['target'])
            field = moved(at)+ref['off']
            mode = ref['mode']
            value = (target-moved(at)-ref['next']) if mode == 1 else (
                pe.OPTIONAL_HEADER.ImageBase+target if mode == 0 else target)
            patch(field, value.to_bytes(ref['width'], 'little', signed=mode == 1))
    for block in pe.DIRECTORY_ENTRY_BASERELOC:
        for entry in block.entries:
            if entry.type != 10:
                continue
            old = struct.unpack('<Q', pe.get_data(entry.rva, 8))[0]-pe.OPTIONAL_HEADER.ImageBase
            if moved(old) != old:
                patch(moved(entry.rva), struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase+moved(old)))
    for pos, value in patches.items():
        out[pos:pos+len(value)] = value
    entries = []
    for e in pe.DIRECTORY_ENTRY_EXCEPTION:
        begin, end, unwind = e.struct.BeginAddress, e.struct.EndAddress, e.struct.UnwindData
        if source <= begin < source+size:
            begin, end = moved(begin), new_rva+end-source
        entries.append((begin, end, unwind))
    table = pe.get_offset_from_rva(pe.OPTIONAL_HEADER.DATA_DIRECTORY[3].VirtualAddress)
    for i, entry in enumerate(sorted(entries)):
        struct.pack_into('<III', out, table+i*12, *entry)
    header = pe.sections[-1].get_file_offset()+40
    assert header+40 <= pe.OPTIONAL_HEADER.SizeOfHeaders
    struct.pack_into('<8sIIIIIIHHI', out, header, b'.zhproof', size, new_rva, raw_size, raw_at, 0, 0, 0, 0, 0x60000020)
    struct.pack_into('<H', out, pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'), len(pe.sections)+1)
    struct.pack_into('<I', out, pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'), new_rva+align(size, 0x1000))
    struct.pack_into('<I', out, pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfCode'), pe.OPTIONAL_HEADER.SizeOfCode+raw_size)
    struct.pack_into('<I', out, pe.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum'), 0)
    return bytes(out), dict(old=hex(source), new=hex(new_rva), patched_operands=len(patches))


@unittest.skipUnless(os.name == 'nt' and os.environ.get('TMPZH_TEST_CORE'), 'requires explicit native PE fixture')
class NativeRelocation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.native = ROOT/'build/native-settings-zh'
        cls.dll = Path(os.environ.get('TMPZH_TEST_DLL', str(cls.native/'TmpSettingsZh.dll')))
        cls.core = Path(os.environ['TMPZH_TEST_CORE']).read_bytes()
        cls.description = json.loads((cls.native/'relocation-profile.json').read_text())
        cls.build = json.loads((cls.native/'native-profile.json').read_text())
        cls.profile = dict(schema=2, abi=1, profile_sha256=cls.build['relocation']['profile_sha256'])

    def setUp(self):
        self.base = ROOT/'build/native-relocation-fixtures'
        self.directory = self.base/uuid.uuid4().hex
        self.directory.mkdir(parents=True)

    def tearDown(self):
        self.assertEqual(self.directory.resolve().parent, self.base.resolve())
        shutil.rmtree(self.directory)

    def check(self, data):
        path = self.directory/'core-data-only.dll'
        path.write_bytes(data)
        return settings_compatibility.check_core(path, self.profile, verifier=self.dll)

    def change(self, at):
        import pefile
        pe = pefile.PE(data=self.core)
        data = bytearray(self.core)
        data[pe.get_offset_from_rva(at)] ^= 1
        return bytes(data)

    def test_baseline_uses_native_checker(self):
        result = self.check(self.core)
        self.assertTrue(all(result[k] for k in ('settings', 'freight', 'players')))
        self.assertFalse(result['relocated'])

    def test_windows_loader_rebase_preserves_all_features(self):
        result = subprocess.run([str(self.native/'native-relocation-test.exe'), '--rebase-fixture',
                                 os.environ['TMPZH_TEST_CORE']], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertIn('ASLR rebasing', result.stdout)
        self.assertIn('wrong pointer rejected', result.stdout)

    def test_real_function_moves_without_new_profile(self):
        data, evidence = move_assign(self.core, self.description)
        result = self.check(data)
        self.assertTrue(result['relocated'])
        self.assertTrue(all(result[k] for k in ('settings', 'freight', 'players')))
        report = dict(passed=True, data_only=True, client_executed=False, relocation=evidence, result=result)
        (self.base/'movement-proof.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')

    def test_settings_failure_preserves_players(self):
        result = self.check(self.change(0x13d1d0))
        self.assertFalse(result['settings'])
        self.assertTrue(result['players'])

    def test_player_failure_preserves_settings(self):
        result = self.check(self.change(0x1cdd90))
        self.assertFalse(result['players'])
        self.assertTrue(result['settings'])

    def test_malformed_image_is_refused(self):
        with self.assertRaises(ValueError):
            self.check(b'MZ'+bytes(4094))


if __name__ == '__main__':
    unittest.main()
