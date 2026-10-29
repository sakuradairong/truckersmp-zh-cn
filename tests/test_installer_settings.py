"""Settings compatibility, game discovery and recovery using entirely synthetic inputs."""
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from common import ROOT, atomic_json, sha
import installer_backend as backend
from installer_settings import DLL, SettingsStore
import installer_settings
import settings_compatibility as compatibility


def synthetic_pe():
    data = bytearray(0x800)
    data[:2] = b'MZ'; struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 1)
    struct.pack_into('<H', data, 0x94, 0xf0)
    struct.pack_into('<H', data, 0x98, 0x20b)
    struct.pack_into('<Q', data, 0xb0, 0x180000000)
    struct.pack_into('<II', data, 0xd0, 0x3000, 0x200)
    struct.pack_into('<IIII', data, 0x190, 0x600, 0x1000, 0x600, 0x200)
    code = b'synthetic code!!'
    data[0x220:0x220+len(code)] = code
    struct.pack_into('<Q', data, 0x240, 0x180001080)
    literals = []
    for index, text in enumerate(('Cancel', 'Set defaults', 'Apply', 'OK')):
        offset = 0x300 + index * 0x20
        value = text.encode() + b'\0'; data[offset:offset+len(value)] = value
        literals.append(dict(rva=offset+0xe00, text=text))
    profile = dict(schema=1, guards=[dict(name='fixture', rva=0x1020, size=len(code), sha256=sha(code))],
                   pointers=[dict(rva=0x1040, target=0x1080)], literals=literals)
    return bytes(data), profile


class Compatibility(unittest.TestCase):
    def test_unrelated_bytes_and_image_size_are_not_release_pins(self):
        data, profile = synthetic_pe()
        self.assertTrue(compatibility.check_bytes(data, profile)['compatible'])
        changed = bytearray(data); changed[0x2c0] = 42
        struct.pack_into('<I', changed, 0xd0, 0x4000)
        self.assertNotEqual(sha(data), sha(changed))
        self.assertTrue(compatibility.check_bytes(bytes(changed), profile)['compatible'])

    def test_required_code_vtable_and_caption_changes_are_refused(self):
        data, profile = synthetic_pe()
        for offset in (0x220, 0x240, 0x300):
            with self.subTest(offset=offset):
                changed = bytearray(data); changed[offset] ^= 1
                with self.assertRaisesRegex(ValueError, 'code/ABI changed'):
                    compatibility.check_bytes(bytes(changed), profile)

    def test_truncation_and_invalid_address_bounds_are_refused(self):
        data, profile = synthetic_pe()
        for value in (b'bad', data[:0x100], data[:0x600]):
            with self.subTest(length=len(value)), self.assertRaises(ValueError):
                compatibility.check_bytes(value, profile)


@unittest.skipUnless(os.name == 'nt', 'Windows installer semantics')
class Installation(unittest.TestCase):
    def setUp(self):
        self.base = ROOT / 'build/settings-installer-tests'
        self.directory = self.base / uuid.uuid4().hex
        self.tmp, self.game, self.assets = [self.directory / name for name in ('TMP', 'ETS2', 'assets')]
        self.state = self.directory / '恢复备份'
        self.tmp.mkdir(parents=True); self.assets.mkdir()
        self.core, self.profile = synthetic_pe()
        (self.tmp / 'core_ets2mp.dll').write_bytes(self.core)
        executable = self.game / 'bin/win_x64/eurotrucks2.exe'
        executable.parent.mkdir(parents=True); executable.write_bytes(b'fixture: never executed')
        self.target = executable.parent / 'plugins' / DLL
        self.target.parent.mkdir(); self.target.write_bytes(b'original settings DLL')
        self.bundle(b'localized settings DLL')
        self.store = SettingsStore(self.tmp, self.state, processes=lambda: [])

    def tearDown(self):
        self.assertEqual(self.directory.resolve().parent, self.base.resolve())
        shutil.rmtree(self.directory)

    def bundle(self, data):
        (self.assets / DLL).write_bytes(data)
        atomic_json(self.assets / 'settings-compatibility.json', self.profile)
        atomic_json(self.assets / 'settings-bundle.json', dict(schema=1, five_pages=True,
            files={DLL: sha(data), 'settings-compatibility.json': sha((self.assets / 'settings-compatibility.json').read_bytes())}))

    def install(self, version='first'):
        return self.store.install(self.game, self.assets, version)

    def test_install_idempotence_and_reopen_restore_previous_dll(self):
        self.assertEqual(self.install()['state'], 'installed')
        self.assertEqual(self.target.read_bytes(), b'localized settings DLL')
        self.assertEqual(self.install('unrelated installer version')['state'], 'already_installed')
        reopened = SettingsStore(self.tmp, self.state, processes=lambda: [])
        reopened.restore()
        self.assertEqual(self.target.read_bytes(), b'original settings DLL')
        self.assertEqual((self.tmp / 'core_ets2mp.dll').read_bytes(), self.core)

    def test_client_outer_hash_change_does_not_prevent_install_or_restore(self):
        changed = bytearray(self.core); changed[0x2c0] ^= 1
        (self.tmp / 'core_ets2mp.dll').write_bytes(changed)
        self.install()
        (self.tmp / 'core_ets2mp.dll').write_bytes(b'client updated after installation')
        self.store.restore()
        self.assertEqual(self.target.read_bytes(), b'original settings DLL')

    def test_update_keeps_first_backup_without_binding_installer_version(self):
        self.install(); old, _ = self.store.current()
        self.bundle(b'new settings DLL')
        self.install('next'); self.assertEqual(self.target.read_bytes(), b'new settings DLL')
        self.store.restore()
        self.assertEqual(self.target.read_bytes(), b'original settings DLL')
        self.assertTrue((old / 'previous.dll').is_file())

    def test_user_changes_and_corrupt_backups_are_preserved(self):
        self.install(); package, _ = self.store.current()
        self.target.write_bytes(b'user changed')
        with self.assertRaisesRegex(ValueError, 'target changed'):
            self.store.restore()
        self.assertEqual(self.target.read_bytes(), b'user changed')
        self.target.write_bytes(b'localized settings DLL')
        (package / 'previous.dll').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'backup changed'):
            self.store.restore()
        self.assertEqual(self.target.read_bytes(), b'localized settings DLL')

    def test_power_loss_after_replacement_can_be_restored(self):
        replace = os.replace
        def power_loss(source, destination):
            replace(source, destination)
            if Path(destination) == self.target:
                raise SystemExit('synthetic power loss')
        with patch.object(installer_settings.os, 'replace', side_effect=power_loss), self.assertRaises(SystemExit):
            self.install()
        self.assertEqual(self.store.status()['state'], 'applying')
        self.store.restore()
        self.assertEqual(self.target.read_bytes(), b'original settings DLL')

    def test_game_running_and_bad_bundle_stop_before_target_mutation(self):
        self.store.anchor.processes = lambda: ['eurotrucks2:fixture']
        with self.assertRaisesRegex(ValueError, 'Exit the game'):
            self.install()
        self.store.anchor.processes = lambda: []
        (self.assets / DLL).write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'Bundled Settings'):
            self.install()
        self.assertEqual(self.target.read_bytes(), b'original settings DLL')

    def test_launcher_game_discovery_uses_actual_game_root(self):
        options = self.directory / 'TruckersMP/launcher-options.json'
        options.parent.mkdir()
        options.write_text(json.dumps({'games': {'ets2': {'path': str(self.game / 'bin/win_x64/eurotrucks2.exe')}}}), encoding='utf-8-sig')
        self.assertEqual(backend.discover_game(self.directory)[0], [self.game])

    def test_backend_preflights_settings_before_installing_any_resources(self):
        changed = bytearray(self.core); changed[0x220] ^= 1
        (self.tmp / 'core_ets2mp.dll').write_bytes(changed)
        with patch.object(backend.resources, 'check_compatibility', return_value={}), patch.object(
                backend, 'bundled_fonts', side_effect=AssertionError('Must not install resources')):
            with self.assertRaisesRegex(ValueError, 'code/ABI changed'):
                backend.install(self.tmp, self.assets, self.state, game_root=self.game, include_settings=True)
        self.assertEqual(self.target.read_bytes(), b'original settings DLL')

    def test_backend_installs_both_components_and_restores_settings_before_fonts(self):
        with patch.object(backend, 'Store') as resource_type, patch.object(backend.resources, 'check_compatibility', return_value={}), patch.object(
                backend, 'bundled_fonts', return_value={key: self.assets / DLL for key in ('regular', 'semibold', 'mono')}), patch.object(
                backend, 'SettingsStore', return_value=self.store):
            store = resource_type.return_value
            store.status.return_value = dict(state='not_installed', can_restore=False)
            store.install.return_value = dict(state='installed')
            result = backend.install(self.tmp, self.assets, self.state, game_root=self.game, include_settings=True)
            self.assertEqual(result['settings']['state'], 'installed')
            self.assertEqual(self.target.read_bytes(), b'localized settings DLL')
            store.status.return_value = dict(state='installed', can_restore=True)
            def restore_fonts(progress):
                self.assertEqual(self.target.read_bytes(), b'original settings DLL')
                return dict(state='restored')
            store.restore.side_effect = restore_fonts
            backend.restore(self.tmp, self.state)
            store.install.assert_called_once()
            store.restore.assert_called_once()

    def test_backend_reports_partial_install_instead_of_full_success(self):
        with patch.object(backend, 'Store') as resource_type, patch.object(backend.resources, 'check_compatibility', return_value={}), patch.object(
                backend, 'bundled_fonts', return_value={key: self.assets / DLL for key in ('regular', 'semibold', 'mono')}), patch.object(
                SettingsStore, 'install', side_effect=ValueError('synthetic Settings write failure')):
            resource_type.return_value.status.return_value = dict(state='not_installed', can_restore=False)
            resource_type.return_value.install.return_value = dict(state='installed')
            with self.assertRaisesRegex(ValueError, 'Resource localization installed; Settings incomplete') as error:
                backend.install(self.tmp, self.assets, self.state, game_root=self.game, include_settings=True)
            self.assertIn('资源汉化已安装，但 Settings 安装未完成', backend.friendly_error(error.exception))


if __name__ == '__main__':
    unittest.main()
