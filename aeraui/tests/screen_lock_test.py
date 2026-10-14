"""Exercise the actual verifier, persistence, throttling and UI bypass guards."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'),
                     'Requires Linux, C++ and OpenSSL development files')
class ScreenLockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.temp.name)
        cls.driver = cls.path / 'verifier'
        result = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-pthread', '-I', str(ROOT), str(ROOT / 'aeraui/tests/screen_lock_driver.cpp'),
            str(ROOT / 'aeraui/platform/screen_lock.cpp'), '-lcrypto', '-o', str(cls.driver)],
            capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, name):
        subprocess.run([str(self.driver), name, str(self.path / name / 'screen-lock.conf')],
                       check=True, timeout=10)

    def test_salted_verifier_reload_change_disable_and_single_use_unlock(self):
        self.check('persistence')

    def test_failed_attempts_cannot_disable_change_or_immediately_retry(self):
        self.check('throttle')

    def test_corrupt_verifier_stays_locked(self):
        self.check('corrupt')

    def test_symlink_verifier_is_rejected(self):
        self.check('symlink')

    def test_removed_storage_does_not_unlock_an_enabled_session(self):
        self.check('storage_removed')

    def test_oversized_and_embedded_null_passwords_are_rejected(self):
        self.check('reject_inputs')

    def test_native_engine_does_not_accept_slider_or_pc_dismissal_as_authentication(self):
        engine = (ROOT / 'aeraui/core/engine.cpp').read_text(encoding='utf-8')
        begin = engine.index('if (action == Action::kUnlock)')
        authorization = engine.index('if (!RecoveryConsumeScreenUnlock()) return;', begin)
        dismissal = engine.index('self->lock_overlay_ = nullptr;', begin)
        self.assertLess(authorization, dismissal)
        self.assertIn('(!lock_overlay_ || !RecoveryScreenLockEnabled()) &&', engine)
        backend = (ROOT / 'aeraui/platform/aera_backend.cpp').read_text(encoding='utf-8')
        self.assertIn('ScreenLockCredential credential("/data/system/aera/screen-lock.conf")', backend)
        self.assertIn('PartitionManager.Is_Mounted_By_Path("/data") && ScreenPassword().Change', backend)


if __name__ == '__main__':
    unittest.main()
