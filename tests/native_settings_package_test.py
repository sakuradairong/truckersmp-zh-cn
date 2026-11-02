"""Transaction tests use private workspace fixture directories, never game paths."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
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

    def crash(self, action):
        result=subprocess.run([sys.executable,'-B',__file__,'--crash',action,str(self.root)],
                              capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,74,result.stdout+result.stderr)

    def test_hard_exit_install_is_owned_and_recoverable(self):
        self.crash('install')
        self.assertTrue(self.layout.destination.is_file())
        self.assertEqual(pkg.owned_receipt(self.layout)['state'],'applying')
        with self.assertRaisesRegex(ValueError,'incomplete'):
            pkg.install(self.layout)
        pkg.restore(self.layout)
        self.assertFalse(self.layout.destination.exists())
        self.assertEqual(pkg.restore(self.layout)['state'],'already_restored')
        pkg.install(self.layout);pkg.restore(self.layout)

    def test_hard_exit_restore_can_resume(self):
        pkg.install(self.layout);self.crash('restore')
        self.assertFalse(self.layout.destination.exists())
        self.assertEqual(pkg.owned_receipt(self.layout)['state'],'restoring')
        result=pkg.restore(self.layout)
        self.assertEqual(pkg.digest(Path(result['disabled_copy'])),pkg.digest(self.layout.release/pkg.DLL))
        self.assertEqual(pkg.restore(self.layout)['state'],'already_restored')

    def test_interrupted_install_preserves_changed_dll(self):
        self.crash('install')
        self.layout.destination.write_bytes(b'user changed after crash')
        with self.assertRaisesRegex(ValueError,'DLL changed'):
            pkg.restore(self.layout)
        self.assertEqual(self.layout.destination.read_bytes(),b'user changed after crash')

    def test_applying_receipt_without_published_dll_can_restore(self):
        rename=Path.rename
        def fail_publish(path,target):
            if Path(target)==self.layout.destination:
                raise OSError('failed before publish')
            return rename(path,target)
        with patch.object(Path,'rename',fail_publish),self.assertRaises(OSError):
            pkg.install(self.layout)
        self.assertEqual(pkg.owned_receipt(self.layout)['state'],'applying')
        self.assertEqual(pkg.restore(self.layout)['state'],'restored')
        self.assertFalse(self.layout.destination.exists())

    def test_final_receipt_failures_remain_recoverable(self):
        publish=pkg.atomic_json
        def fail_installed(path,receipt):
            if receipt.get('state')=='installed':raise OSError('final install receipt failed')
            publish(path,receipt)
        with patch.object(pkg,'atomic_json',side_effect=fail_installed),self.assertRaises(OSError):
            pkg.install(self.layout)
        pkg.restore(self.layout);pkg.install(self.layout)
        def fail_restored(path,receipt):
            if receipt.get('state')=='restored':raise OSError('final restore receipt failed')
            publish(path,receipt)
        with patch.object(pkg,'atomic_json',side_effect=fail_restored),self.assertRaises(OSError):
            pkg.restore(self.layout)
        self.assertEqual(pkg.restore(self.layout)['state'],'restored')

    def test_legacy_restore_does_not_require_current_dependencies_or_package(self):
        import resources
        tmp=self.root/'TMP';tmp.mkdir()
        game=self.root/'ETS2';game.mkdir()
        plugins=game/'bin/win_x64/plugins';plugins.mkdir(parents=True)
        layout=pkg.Layout(self.layout.release,plugins,{},lambda:[])
        layout.destination.write_bytes(b'owned legacy DLL')
        receipt=dict(schema=1,package='TmpSettingsZh',state='installed',path=str(layout.destination),
                     previous_dll=None,dll_sha256=pkg.digest(layout.destination))
        # This historical receipt deliberately has no transaction field.
        for names in (resources.LEGACY_OUTPUTS,resources.PRE_FREIGHT_OUTPUTS,None):
            with self.subTest(scope=names):
                layout.destination.write_bytes(b'owned legacy DLL')
                pkg.atomic_json(layout.receipt,receipt)
                manifest=layout.release/'package.json'
                if names is None:
                    manifest.unlink()
                else:
                    pkg.atomic_json(manifest,dict(preserved_inputs={str(tmp/n):'old hash'
                        for n in ('core_ets2mp.dll',*resources.INPUTS,*names)}))
                recovered=pkg.configured_layout(tmp,game,layout.release,'restore')
                recovered.processes=lambda:[]
                self.assertEqual(pkg.restore(recovered)['state'],'restored')
                self.assertFalse(layout.destination.exists())

    def test_legacy_restore_still_rejects_wrong_target_and_hash(self):
        pkg.install(self.layout)
        receipt=pkg.owned_receipt(self.layout)
        pkg.atomic_json(self.layout.receipt,dict(receipt,path=str(self.root/'foreign.dll')))
        with self.assertRaisesRegex(ValueError,'restore target'):pkg.restore(self.layout)
        pkg.atomic_json(self.layout.receipt,dict(receipt,dll_sha256='0'*64))
        with self.assertRaisesRegex(ValueError,'DLL changed'):pkg.restore(self.layout)
        self.assertTrue(self.layout.destination.is_file())


def crash_child(action, root):
    release,plugins=root/'release',root/'plugins'
    manifest=json.loads((release/'package.json').read_text(encoding='utf-8'))
    layout=pkg.Layout(release,plugins,{Path(p):h for p,h in manifest['preserved_inputs'].items()},lambda:[])
    rename=Path.rename
    def abrupt_rename(source,destination):
        result=rename(source,destination)
        if Path(destination)==layout.destination or source==layout.destination:
            os._exit(74)
        return result
    with patch.object(Path,'rename',abrupt_rename):
        getattr(pkg,action)(layout)
    raise AssertionError('hard exit point was not reached')


if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='--crash':
        crash_child(sys.argv[2],Path(sys.argv[3]))
        sys.exit(1)
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Transactions))
    report=dict(passed=result.wasSuccessful(),tests=result.testsRun,failures=len(result.failures),errors=len(result.errors),
                scope='private workspace fixture directories only')
    pkg.atomic_json(pkg.RELEASE/'transaction-checks.json',report)
    sys.exit(0 if result.wasSuccessful() else 1)
