"""Exercise install/restore failures using only private synthetic file trees."""
import os
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from common import ROOT, digest, read_json
import resources
import resource_transaction as transaction


@unittest.skipUnless(os.name == 'nt', 'Windows file replacement semantics')
class ResourceTransactions(unittest.TestCase):
    def setUp(self):
        self.base = ROOT / 'build/resource-transaction-tests'
        self.root = self.base / uuid.uuid4().hex
        self.tmp_root, self.output = self.root / 'installation', self.root / 'package'
        self.tmp_root.mkdir(parents=True); self.output.mkdir()
        self.original = {}
        files = {}
        for index, name in enumerate(resources.OUTPUTS):
            target = self.tmp_root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            previous = b'previous-' + str(index).encode() if index % 2 else None
            if previous is not None:
                target.write_bytes(previous)
            self.original[name] = previous
            source = self.output / 'files' / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(b'localized-' + str(index).encode())
            files[name] = digest(source)
        self.manifest = dict(files=files)
        self.verifier = patch.object(resources, 'verify', return_value=self.manifest)
        self.verifier.start()

    def tearDown(self):
        self.verifier.stop()
        self.assertEqual(self.root.resolve().parent, self.base.resolve())
        shutil.rmtree(self.root)

    def assert_original(self):
        for name, expected in self.original.items():
            target = self.tmp_root / name
            self.assertEqual(target.read_bytes() if target.exists() else None, expected)

    def install(self, processes=lambda: []):
        return transaction.install(self.tmp_root, self.output, processes)

    def restore(self, processes=lambda: []):
        return transaction.restore(self.tmp_root, self.output, processes)

    def test_install_idempotence_and_complete_restore(self):
        self.assertEqual(self.install()['state'], 'installed')
        for name, expected in self.manifest['files'].items():
            self.assertEqual(digest(self.tmp_root / name), expected)
        self.assertEqual(self.install()['state'], 'already_installed')
        self.assertEqual(self.restore()['state'], 'restored')
        self.assert_original()
        self.assertEqual(self.restore()['state'], 'already_restored')

    def test_running_game_and_start_during_staging(self):
        with self.assertRaisesRegex(ValueError, 'Exit the game'):
            self.install(lambda: ['eurotrucks2:fixture'])
        self.assert_original()
        calls = iter([[], ['TruckersMP:fixture']])
        with self.assertRaisesRegex(ValueError, 'Exit the game'):
            self.install(lambda: next(calls))
        self.assert_original()

    def test_receipt_failure_rolls_back_install(self):
        with patch.object(transaction, 'atomic_json', side_effect=OSError('receipt failed')):
            with self.assertRaisesRegex(OSError, 'receipt failed'):
                self.install()
        self.assert_original()

    def test_receipt_failure_rolls_back_restore(self):
        self.install()
        with patch.object(transaction, 'atomic_json', side_effect=OSError('receipt failed')):
            with self.assertRaisesRegex(OSError, 'receipt failed'):
                self.restore()
        for name, expected in self.manifest['files'].items():
            self.assertEqual(digest(self.tmp_root / name), expected)

    def test_changed_user_resource_blocks_restore(self):
        self.install()
        target = self.tmp_root / resources.OUTPUTS[0]
        target.write_bytes(b'new user change')
        with self.assertRaisesRegex(ValueError, 'target changed'):
            self.restore()
        self.assertEqual(target.read_bytes(), b'new user change')

    def test_changed_backup_blocks_restore(self):
        result = self.install()
        (Path(result['backups']) / '1.bin').write_bytes(b'corrupt backup')
        with self.assertRaisesRegex(ValueError, 'backup changed'):
            self.restore()
        for name, expected in self.manifest['files'].items():
            self.assertEqual(digest(self.tmp_root / name), expected)

    def test_restore_refuses_changed_receipt_target_scope(self):
        self.install()
        receipt_path = self.output / 'installed-resources.json'
        receipt = json.loads(receipt_path.read_text())
        receipt['files']['../outside'] = receipt['files'].pop(resources.OUTPUTS[0])
        receipt_path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, 'target scope'):
            self.restore()

    def crash(self, action):
        result = subprocess.run([sys.executable, '-B', __file__, '--crash', action,
                                 str(self.tmp_root), str(self.output)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 73, result.stdout + result.stderr)

    def test_hard_exit_install_keeps_original_baseline_and_blocks_retry(self):
        self.crash('install')
        self.assertEqual(read_json(self.output / 'installed-resources.json')['state'], 'applying')
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.install()
        self.restore(); self.assert_original()
        self.assertFalse(list(self.tmp_root.rglob('*.new-*')))
        self.install(); self.restore(); self.assert_original()

    def test_hard_exit_restore_resumes_and_is_idempotent(self):
        self.install(); self.crash('restore')
        self.assertEqual(read_json(self.output / 'installed-resources.json')['state'], 'restoring')
        self.restore(); self.assert_original()
        self.assertEqual(self.restore()['state'], 'already_restored')

    def test_interrupted_install_preserves_user_changes_before_any_restore_write(self):
        self.crash('install')
        target = self.tmp_root / resources.OUTPUTS[-1]
        target.write_bytes(b'user edit after crash')
        snapshot = {name: (self.tmp_root / name).read_bytes() if (self.tmp_root / name).exists() else None
                    for name in resources.OUTPUTS}
        with self.assertRaisesRegex(ValueError, 'target changed'):
            self.restore()
        self.assertEqual(snapshot, {name: (self.tmp_root / name).read_bytes() if (self.tmp_root / name).exists() else None
                                    for name in resources.OUTPUTS})

    def test_final_install_receipt_failure_can_still_restore(self):
        publish = transaction.atomic_json
        def fail_final(path, receipt):
            if receipt['state'] == 'installed':
                raise OSError('final receipt failed')
            publish(path, receipt)
        with patch.object(transaction, 'atomic_json', side_effect=fail_final), self.assertRaises(OSError):
            self.install()
        self.restore(); self.assert_original()

    def test_final_restore_receipt_failure_can_resume(self):
        self.install()
        publish = transaction.atomic_json
        def fail_final(path, receipt):
            if receipt['state'] == 'restored':
                raise OSError('final receipt failed')
            publish(path, receipt)
        with patch.object(transaction, 'atomic_json', side_effect=fail_final), self.assertRaises(OSError):
            self.restore()
        self.restore(); self.assert_original()


def crash_child(action, tmp_root, output):
    # A real process exit skips Python exception handling and finally blocks.
    replace = os.replace
    def abrupt_replace(source, destination):
        replace(source, destination)
        if Path(destination) == tmp_root / resources.OUTPUTS[1]:
            os._exit(73)
    manifest = {'files': {name: digest(output / 'files' / name) for name in resources.OUTPUTS}}
    with patch.object(resources, 'verify', return_value=manifest), patch.object(transaction.os, 'replace', abrupt_replace):
        getattr(transaction, action)(tmp_root, output, lambda: [])
    raise AssertionError('hard exit point was not reached')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--crash':
        crash_child(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]))
    else:
        unittest.main()
