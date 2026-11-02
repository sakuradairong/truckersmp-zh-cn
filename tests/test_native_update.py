"""Opt-in 0.7.9.4 production-DLL checks; client bytes are never executed."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import settings_compatibility


@unittest.skipUnless(os.environ.get('TMPZH_UPDATE_CORE'), 'requires explicit 0.7.9.4 PE fixture')
class NativeUpdate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import pefile
        cls.path = Path(os.environ['TMPZH_UPDATE_CORE'])
        cls.source = cls.path.read_bytes()
        cls.digest = hashlib.sha256(cls.source).hexdigest()
        assert cls.digest == '4916d448b9ea3f8cdec0a82ae092e9b58c7e46038a361b0acaa90fd1f4510cd1'
        cls.pe = pefile.PE(data=cls.source)
        cls.native = ROOT/'build/native-settings-zh'
        report = json.loads((cls.native/'native-profile.json').read_text(encoding='utf-8'))
        cls.profile = dict(schema=2, abi=1, profile_sha256=report['relocation']['profile_sha256'])
        cls.mapping = json.loads((ROOT/'profiles/native-0794-review.json').read_text(encoding='utf-8'))['canonical_mapping']

    def check(self, mutation=None):
        folder = ROOT/'build/native-update-fixtures'/uuid.uuid4().hex
        folder.mkdir(parents=True)
        try:
            data = bytearray(self.source)
            if mutation:
                pos = self.pe.get_offset_from_rva(self.mapping[str(mutation)])
                data[pos] ^= 1
            fixture = folder/'client-data.dll'
            fixture.write_bytes(data)
            return settings_compatibility.check_core(fixture, self.profile, verifier=self.native/'TmpSettingsZh.dll')
        finally:
            assert folder.parent == ROOT/'build/native-update-fixtures'
            shutil.rmtree(folder)
            self.assertEqual(hashlib.sha256(self.path.read_bytes()).hexdigest(), self.digest)

    def test_current_client(self):
        result = self.check()
        self.assertTrue(all(result[k] for k in ('settings', 'freight', 'players', 'relocated')))

    def test_bad_settings_cannot_be_rescued_by_other_profile(self):
        result = self.check(0x13d1d0)
        self.assertFalse(result['settings'])
        self.assertTrue(result['players'])

    def test_bad_players_preserve_settings(self):
        result = self.check(0x1cdd90)
        self.assertFalse(result['players'])
        self.assertTrue(result['settings'])

    def test_lua_registration_literal_identity(self):
        result = self.check(0xdde920)
        self.assertFalse(result['players'])
        self.assertTrue(result['settings'])


if __name__ == '__main__':
    unittest.main()
