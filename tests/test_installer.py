"""Installer discovery and journal recovery with synthetic private resources."""
import copy
import json
import os
from pathlib import Path
import shutil
import sys
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from common import ROOT, atomic_json, digest, sha
import installer_backend as backend
from installer_store import Store
import installer_store
import resources


class PrivateTree(unittest.TestCase):
    def setUp(self):
        self.base = ROOT / 'build/installer-tests'
        self.directory = self.base / uuid.uuid4().hex
        self.directory.mkdir(parents=True)

    def tearDown(self):
        self.assertEqual(self.directory.resolve().parent, self.base.resolve())
        shutil.rmtree(self.directory)


class Discovery(PrivateTree):
    def test_launcher_config_bom_and_custom_install_path(self):
        configured = self.directory / '中文 TMP'
        configured.mkdir(); (configured / 'core_ets2mp.dll').write_bytes(b'synthetic')
        configuration = self.directory / 'TruckersMP/launcher-options.json'
        configuration.parent.mkdir()
        configuration.write_text(json.dumps({'installPath': str(configured)}), encoding='utf-8-sig')
        candidates, notes = backend.discover(self.directory)
        self.assertEqual(candidates, [configured]); self.assertEqual(notes, [])

    def test_invalid_config_falls_back_without_writing_configuration(self):
        configuration = self.directory / 'TruckersMP/launcher-options.json'
        configuration.parent.mkdir(); configuration.write_text('{bad json', encoding='utf-8')
        fallback = configuration.parent / 'installation'
        fallback.mkdir(); (fallback / 'core_ets2mp.dll').write_bytes(b'synthetic')
        candidates, notes = backend.discover(self.directory)
        self.assertEqual(candidates, [fallback]); self.assertTrue(notes)
        self.assertEqual(configuration.read_text(), '{bad json')

    def test_relative_config_is_not_resolved_against_current_directory(self):
        configuration = self.directory / 'TruckersMP/launcher-options.json'
        configuration.parent.mkdir()
        configuration.write_text('{"installPath":"relative/untrusted"}', encoding='utf-8')
        self.assertEqual(backend.discover(self.directory)[0], [])

    def test_unc_discovery_is_refused_without_network_file_queries(self):
        with patch.object(Path, 'is_file', side_effect=AssertionError('Must not query network files')):
            self.assertEqual(backend.discover(r'\\untrusted\share')[0], [])
        with self.assertRaisesRegex(ValueError, 'local disk'):
            backend.inspect(r'\\untrusted\share')


@unittest.skipUnless(os.name == 'nt', 'Windows installer semantics')
class Recovery(PrivateTree):
    def setUp(self):
        super().setUp()
        self.tmp = self.directory / 'TMP 安装目录'
        self.tmp.mkdir()
        self.state = self.directory / '固定备份目录'
        self.before = {}
        self.files = {name: sha(('localized-' + name).encode()) for name in resources.OUTPUTS}
        for index, name in enumerate(resources.OUTPUTS):
            target = self.tmp / name
            target.parent.mkdir(parents=True, exist_ok=True)
            data = ('previous-' + str(index)).encode() if index % 2 else None
            if data:
                target.write_bytes(data)
            self.before[name] = sha(data) if data else None
        self.patches = [patch.object(resources, 'build', side_effect=self.build),
                        patch.object(resources, 'verify', return_value={'files': self.files}),
                        patch.object(resources, 'inputs', return_value={})]
        for item in self.patches:
            item.start()
        self.store = Store(self.tmp, self.state, processes=lambda: [])

    def tearDown(self):
        for item in reversed(self.patches):
            item.stop()
        super().tearDown()

    def build(self, tmp, font, output, **kwargs):
        for name in resources.OUTPUTS:
            target = output / 'files' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(('localized-' + name).encode())

    def install(self, version='fixture-1'):
        return self.store.install('regular.ttf', 'semibold.ttf', 'mono.ttf', version)

    def assert_before(self):
        self.assertEqual({name: self.store.actual(name) for name in resources.OUTPUTS}, self.before)

    def test_install_restore_idempotence_and_persistent_reopen(self):
        self.assertEqual(self.install()['state'], 'installed')
        self.assertEqual(self.install()['state'], 'already_installed')
        reopened = Store(self.tmp, self.state, processes=lambda: [])
        self.assertEqual(reopened.status()['state'], 'installed')
        reopened.restore(); self.assert_before()
        reopened.restore(); self.assert_before()
        self.assertEqual(reopened.status()['state'], 'restored')

    def test_unknown_client_can_still_restore(self):
        self.install()
        with patch.object(resources, 'check_compatibility', side_effect=ValueError('Incompatible resource')):
            self.assertFalse(backend.inspect(self.tmp, self.state)['compatible'])
            self.assertEqual(self.store.restore()['state'], 'restored')
        self.assert_before()

    def test_running_game_blocks_all_target_writes(self):
        self.store.processes = lambda: ['eurotrucks2:fixture']
        with self.assertRaisesRegex(ValueError, 'Exit the game'):
            self.install()
        self.assert_before()

    def test_install_power_loss_can_be_restored(self):
        replace = os.replace
        count = 0
        def power_loss(source, destination):
            nonlocal count
            replace(source, destination)
            if Path(destination).is_relative_to(self.tmp):
                count += 1
                if count == 2:
                    raise SystemExit('synthetic power loss')
        with patch.object(installer_store.os, 'replace', side_effect=power_loss):
            with self.assertRaises(SystemExit):
                self.install()
        self.assertEqual(self.store.status()['state'], 'applying')
        Store(self.tmp, self.state, processes=lambda: []).restore()
        self.assert_before()

    def test_restore_power_loss_can_resume(self):
        self.install()
        replace = os.replace
        def power_loss(source, destination):
            replace(source, destination)
            if Path(destination).is_relative_to(self.tmp):
                raise SystemExit('synthetic power loss')
        with patch.object(installer_store.os, 'replace', side_effect=power_loss):
            with self.assertRaises(SystemExit):
                self.store.restore()
        self.assertEqual(self.store.status()['state'], 'restoring')
        self.store.restore(); self.assert_before()

    def test_io_failure_rolls_back_with_receipt_retained(self):
        replace = os.replace
        failed = False
        def fail_once(source, destination):
            nonlocal failed
            if Path(destination).is_relative_to(self.tmp) and not failed:
                failed = True
                raise OSError('synthetic write failure')
            return replace(source, destination)
        with patch.object(installer_store.os, 'replace', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'synthetic write'):
                self.install()
        self.assert_before()
        self.assertEqual(self.store.status()['state'], 'restored')

    def test_unknown_user_edit_is_preserved_before_any_restore_write(self):
        self.install()
        target = self.tmp / resources.OUTPUTS[-1]
        target.write_bytes(b'user-owned edit')
        before = {name: self.store.actual(name) for name in resources.OUTPUTS}
        with self.assertRaisesRegex(ValueError, 'target changed'):
            self.store.restore()
        self.assertEqual({name: self.store.actual(name) for name in resources.OUTPUTS}, before)
        self.assertEqual(target.read_bytes(), b'user-owned edit')

    def test_corrupt_backup_blocks_every_restore_write(self):
        self.install()
        package, receipt = self.store.current()
        (package / 'backups' / Store.backup_name(resources.OUTPUTS[1])).write_bytes(b'corrupt')
        before = {name: self.store.actual(name) for name in resources.OUTPUTS}
        with self.assertRaisesRegex(ValueError, 'backup changed'):
            self.store.restore()
        self.assertEqual({name: self.store.actual(name) for name in resources.OUTPUTS}, before)

    def test_pointer_and_receipt_cannot_expand_write_scope(self):
        self.install()
        pointer = json.loads(self.store.pointer.read_text())
        atomic_json(self.store.pointer, {'schema': 1, 'transaction': '../outside'})
        with self.assertRaisesRegex(ValueError, 'pointer'):
            self.store.restore()
        atomic_json(self.store.pointer, pointer)
        package, receipt = self.store.current()
        bad = copy.deepcopy(receipt)
        bad['files']['../outside'] = bad['files'].pop(resources.OUTPUTS[0])
        atomic_json(package / 'receipt.json', bad)
        with self.assertRaisesRegex(ValueError, 'scope'):
            self.store.restore()

    def test_installer_version_alone_does_not_require_restore(self):
        self.install()
        older, _ = self.store.current()
        self.assertEqual(self.install('fixture-2')['state'], 'already_installed')
        self.assertEqual(self.store.current()[0], older)
        self.store.restore(); self.assert_before()

    def old_package_requires_rebuild(self, older):
        def verify(tmp, output, **kwargs):
            if output == older and 'font_paths' in kwargs:
                raise ValueError('Unknown resource package')
            return {'files': self.files}
        return patch.object(resources, 'verify', side_effect=verify)

    def test_upgrade_rebuilds_and_retains_the_first_restore_point(self):
        self.install()
        older, _ = self.store.current()
        with self.old_package_requires_rebuild(older):
            self.assertEqual(self.install('fixture-2')['state'], 'installed')
        newer, receipt = self.store.current()
        self.assertNotEqual(older, newer)
        self.assertEqual(receipt['version'], 'fixture-2')
        self.assertEqual({name: row['previous_sha256'] for name, row in receipt['files'].items()}, self.before)
        self.assertTrue((older / 'backups' / Store.backup_name(resources.OUTPUTS[1])).is_file())
        self.store.restore(); self.assert_before()

    def test_incompatible_upgrade_keeps_the_existing_installation(self):
        self.install()
        older, _ = self.store.current()
        snapshot = {name: self.store.actual(name) for name in resources.OUTPUTS}
        with self.old_package_requires_rebuild(older), patch.object(resources, 'build', side_effect=ValueError('Incompatible resource')):
            with self.assertRaisesRegex(ValueError, 'Incompatible resource'):
                self.install('fixture-2')
        self.assertEqual(self.store.current()[0], older)
        self.assertEqual(snapshot, {name: self.store.actual(name) for name in resources.OUTPUTS})

    def test_interrupted_upgrade_restores_the_first_install_baseline(self):
        self.install()
        older, _ = self.store.current()
        replace = os.replace
        def power_loss(source, destination):
            replace(source, destination)
            if Path(destination).is_relative_to(self.tmp) and self.store.current()[1]['state'] == 'applying':
                raise SystemExit('synthetic upgrade power loss')
        with self.old_package_requires_rebuild(older), patch.object(installer_store.os, 'replace', side_effect=power_loss):
            with self.assertRaises(SystemExit):
                self.install('fixture-2')
        self.assertEqual(self.store.status()['state'], 'applying')
        self.store.restore(); self.assert_before()

    def test_concurrent_installer_is_refused(self):
        with self.store.lock():
            other = Store(self.tmp, self.state, processes=lambda: [])
            with self.assertRaisesRegex(ValueError, 'already running'):
                with other.lock():
                    self.fail('Overlapping transaction acquired a lock')


if __name__ == '__main__':
    unittest.main()
