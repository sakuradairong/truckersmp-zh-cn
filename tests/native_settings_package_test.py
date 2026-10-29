"""Transaction tests use private workspace fixture directories, never game paths."""
import copy
import json
from pathlib import Path
import shutil
import sys
import uuid
import unittest
from unittest.mock import patch

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_settings_package as pkg


class Transactions(unittest.TestCase):
    def setUp(self):
        base=pkg.ROOT/'build/native-settings-package-tests'
        base.mkdir(exist_ok=True)
        self.root=base/uuid.uuid4().hex
        self.root.mkdir()
        root=self.root
        pkg.require(root.absolute().parent==base.absolute(),'Fixture directory escapes the named test workspace')
        release=root/'release';release.mkdir()
        plugins=root/'plugins';plugins.mkdir()
        baseline=root/'preserved-resource';baseline.write_bytes(b'unchanged existing resource')
        for file in (*pkg.PACKAGE_FILES,'package.json'):
            shutil.copyfile(pkg.RELEASE/file,release/file)
        self.layout=pkg.Layout(release,plugins,{baseline:pkg.digest(baseline)},lambda:[])
        manifest=json.loads((release/'package.json').read_text(encoding='utf-8'))
        manifest['preserved_inputs']={str(baseline):pkg.digest(baseline)}
        pkg.atomic_json(release/'package.json',manifest)

    def tearDown(self):
        root=self.root
        pkg.require(root.resolve().parent==(pkg.ROOT/'build/native-settings-package-tests').resolve(),
                    'Refusing recursive fixture cleanup outside the named test workspace')
        shutil.rmtree(root)

    def test_install_verify_idempotence_restore(self):
        result=pkg.install(self.layout)
        self.assertEqual(result['state'],'installed')
        self.assertEqual(pkg.status(self.layout)['state'],'verified_installation')
        self.assertEqual(pkg.install(self.layout)['state'],'already_installed')
        result=pkg.restore(self.layout)
        self.assertFalse(self.layout.destination.exists())
        self.assertEqual(pkg.digest(Path(result['disabled_copy'])),pkg.digest(self.layout.release/pkg.DLL))
        self.assertEqual(pkg.restore(self.layout)['state'],'already_restored')
        self.layout.inputs()

    def test_running_game_blocks_install_and_restore(self):
        self.layout.processes=lambda:['eurotrucks2:fixture']
        with self.assertRaisesRegex(ValueError,'Exit the game'):pkg.install(self.layout)
        self.assertFalse(self.layout.destination.exists())
        self.layout.processes=lambda:[];pkg.install(self.layout)
        self.layout.processes=lambda:['TruckersMP-Launcher:fixture']
        with self.assertRaisesRegex(ValueError,'Exit the game'):pkg.restore(self.layout)
        self.assertTrue(self.layout.destination.exists())

    def test_game_starts_during_staging(self):
        calls=iter([[],['eurotrucks2:fixture']]);self.layout.processes=lambda:next(calls)
        with self.assertRaisesRegex(ValueError,'Exit the game'):pkg.install(self.layout)
        self.assertFalse(self.layout.destination.exists())
        self.assertFalse(self.layout.receipt.exists())
        self.assertEqual(list(self.layout.plugins.iterdir()),[])

    def test_foreign_existing_dll_is_preserved(self):
        self.layout.destination.write_bytes(b'foreign existing DLL')
        with self.assertRaisesRegex(ValueError,'no owned receipt'):pkg.install(self.layout)
        self.assertEqual(self.layout.destination.read_bytes(),b'foreign existing DLL')

    def test_changed_dependency_blocks_install(self):
        path=next(iter(self.layout.pins));path.write_bytes(b'user update')
        with self.assertRaisesRegex(ValueError,'dependency changed'):pkg.install(self.layout)
        self.assertEqual(path.read_bytes(),b'user update')
        self.assertFalse(self.layout.destination.exists())

    def test_package_tampering_blocks_install(self):
        (self.layout.release/pkg.DLL).write_bytes(b'changed DLL')
        with self.assertRaisesRegex(ValueError,'Package file differs'):pkg.install(self.layout)
        self.assertFalse(self.layout.destination.exists())

    def test_failed_receipt_publication_rolls_back_install(self):
        with patch.object(pkg,'atomic_json',side_effect=OSError('simulated receipt failure')):
            with self.assertRaisesRegex(OSError,'simulated'):pkg.install(self.layout)
        self.assertFalse(self.layout.destination.exists())
        self.assertFalse(self.layout.receipt.exists())
        self.assertEqual(list(self.layout.plugins.iterdir()),[])

    def test_failed_restore_receipt_keeps_installed_dll(self):
        pkg.install(self.layout);expected=pkg.digest(self.layout.destination)
        with patch.object(pkg,'atomic_json',side_effect=OSError('simulated receipt failure')):
            with self.assertRaisesRegex(OSError,'simulated'):pkg.restore(self.layout)
        self.assertEqual(pkg.digest(self.layout.destination),expected)
        self.assertEqual(pkg.status(self.layout)['state'],'verified_installation')
        self.assertEqual(list(self.layout.plugins.iterdir()),[self.layout.destination])

    def test_modified_installed_dll_is_preserved(self):
        pkg.install(self.layout);self.layout.destination.write_bytes(b'user update')
        with self.assertRaisesRegex(ValueError,'DLL changed'):pkg.restore(self.layout)
        self.assertEqual(self.layout.destination.read_bytes(),b'user update')


if __name__=='__main__':
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Transactions))
    report=dict(passed=result.wasSuccessful(),tests=result.testsRun,failures=len(result.failures),errors=len(result.errors),
                scope='private workspace fixture directories only')
    pkg.atomic_json(pkg.RELEASE/'transaction-checks.json',report)
    sys.exit(0 if result.wasSuccessful() else 1)
