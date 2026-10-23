"""Test the real persist helper without QSEE, phone access or host mounts."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'),
                     'Requires Linux and a native C++ compiler')
class PersistSfsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / 'persist-sfs'
        boundaries = ('setmntent', 'dlopen', 'dlsym', 'dlclose', 'mount', 'statvfs')
        subprocess.run([
            'g++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
            str(ROOT / 'aera_persist_sfs.cpp'),
            str(Path(__file__).with_name('persist_sfs_driver.cpp')),
            *('-Wl,--wrap=' + name for name in boundaries), '-ldl',
            '-o', str(cls.binary)], check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_cases(self, *cases):
        results = {}
        for case in cases:
            with self.subTest(case=case):
                results[case] = subprocess.run(
                    [str(self.binary), case], capture_output=True, text=True,
                    timeout=5, check=True).stderr
        return results

    def test_only_explicit_unprovisioned_status_remounts(self):
        results = self.check_cases('not-provisioned', 'provisioned', 'mac-mismatch',
                                   'unknown-status', 'untouched-response', 'transport-error')
        self.assertIn('persist mounted rw for SFS', results['not-provisioned'])
        for case in ('provisioned', 'mac-mismatch', 'unknown-status',
                     'untouched-response', 'transport-error'):
            self.assertIn('persist unchanged', results[case])

    def test_missing_mount_or_qsee_never_changes_a_mount(self):
        self.check_cases('absent-mount', 'parent-only', 'wrong-filesystem',
                         'already-writable', 'mount-table-error',
                         'missing-library', 'missing-symbol')

    def test_remount_or_verification_failure_is_not_reported_as_success(self):
        for result in self.check_cases('remount-error', 'verification-error',
                                       'still-read-only').values():
            self.assertNotIn('persist mounted rw for SFS', result)

    def test_called_before_first_fbe_setup_and_linked_into_recovery(self):
        source = (ROOT / 'partitionmanager.cpp').read_text()
        start = source.index('void TWPartitionManager::Setup_Fstab_Partitions(')
        body = source[start:source.index('void TWPartitionManager::Decrypt_Data()', start)]
        self.assertLess(body.index('aera::PreparePersistForSfs();'),
                        body.index('(*iter)->Partition_Post_Processing('))
        self.assertIn('aera_persist_sfs.cpp', (ROOT / 'Android.mk').read_text())


if __name__ == '__main__':
    unittest.main()
