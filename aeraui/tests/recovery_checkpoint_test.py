"""Exercise the real recovery checkpoint query and static-library link wrapping."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SYMBOL = '_ZN7android4vold18cp_needsCheckpointEv'


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++') and shutil.which('ar'),
                     'Requires Linux C++ and ar')
class RecoveryCheckpointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        # Separate archive members mirror MetadataCrypt.cpp calling a function
        # defined in Checkpoint.cpp. Use the real C++ symbol, not a C-only mock.
        (folder / 'checkpoint.cpp').write_text('''
bool upstream_checkpoint = false, upstream_state = false;
int upstream_calls = 0;
namespace android::vold {
bool cp_needsCheckpoint() {
  ++upstream_calls; upstream_state = upstream_checkpoint;
  return upstream_checkpoint;
}
}
''')
        (folder / 'metadata.cpp').write_text('''
namespace android::vold { bool cp_needsCheckpoint(); }
bool MetadataMountNeedsCheckpoint() { return android::vold::cp_needsCheckpoint(); }
''')
        cls.binary = folder / 'checkpoint-test'
        cls.binaries = []
        for compiler in ('g++', 'clang++'):
            if not shutil.which(compiler):
                continue
            prefix = folder / compiler
            objects = []
            for name in ('checkpoint', 'metadata'):
                obj = str(prefix) + '-' + name + '.o'
                subprocess.run([compiler, '-std=c++17', '-O2', '-c',
                                str(folder / (name + '.cpp')), '-o', obj], check=True,
                               capture_output=True, text=True)
                objects.append(obj)
            archive = str(prefix) + '-vold.a'
            subprocess.run(['ar', 'rcs', archive, *objects], check=True)
            binary = str(prefix) + '-test'
            subprocess.run([compiler, '-std=c++17', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-I', str(ROOT),
                            str(ROOT / 'aera_checkpoint/checkpoint.cpp'),
                            str(Path(__file__).with_name('recovery_checkpoint_driver.cpp')), archive,
                            '-Wl,--wrap=' + SYMBOL,
                            *('-Wl,--wrap=' + name for name in
                              ('setmntent', 'open', 'fstat', 'read', 'close')),
                            '-o', binary], check=True, capture_output=True, text=True)
            cls.binaries.append(binary)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def cases(self, *names, scoped=True):
        for binary in self.binaries:
            for name in names:
                for upstream in ('0', '1'):
                    with self.subTest(compiler=Path(binary).name, case=name, upstream=upstream):
                        subprocess.run([binary, name, upstream, '1' if scoped else '0'], check=True,
                                       capture_output=True, text=True, timeout=5)

    def test_unmarked_slot_without_transaction_does_not_enable_rollback(self):
        self.cases('absent', 'committed', 'metadata-f2fs', 'interrupted')

    def test_active_and_malformed_transactions_keep_original_vold_decision(self):
        self.cases('pending', 'pending-slot', 'malformed', 'empty', 'whitespace')

    def test_missing_or_unverifiable_metadata_never_bypasses_vold(self):
        self.cases('mount-table-error', 'metadata-parent', 'metadata-child', 'metadata-tmpfs')

    def test_file_errors_and_races_keep_original_vold_decision(self):
        self.cases('unreadable', 'symlink', 'open-error', 'stat-error', 'directory',
                   'read-error', 'short-read', 'changed-record', 'close-error')

    def test_queries_outside_metadata_mount_leave_crypto_behavior_unchanged(self):
        self.cases('absent', 'committed', 'pending', 'unreadable', scoped=False)

    def test_metadata_scope_does_not_change_other_threads(self):
        self.cases('other-thread')

    def test_crypto_only_build_uses_same_archive_symbol_and_no_platform_patch(self):
        make = (ROOT / 'Android.mk').read_text()
        start = make.index('ifeq ($(TW_INCLUDE_CRYPTO), true)')
        block = make[start:make.index('ifeq ($(TARGET_HW_DISK_ENCRYPTION)', start)]
        self.assertIn('LOCAL_SRC_FILES += aera_checkpoint/checkpoint.cpp', block)
        self.assertIn('LOCAL_LDFLAGS += -Wl,--wrap=' + SYMBOL, block)
        partition = (ROOT / 'partitionmanager.cpp').read_text()
        start = partition.index('const bool metadata_ready = [&] {')
        mount = partition[start:partition.index('if (metadata_ready)', start)]
        self.assertLess(mount.index('aera::MetadataCheckpointScope checkpoint_scope'),
                        mount.index('android::vold::fscrypt_mount_metadata_encrypted'))
        self.assertIn('}();', mount)
        source = (ROOT / 'aera_checkpoint/checkpoint.cpp').read_text()
        for forbidden in ('mount(', 'unlink(', 'O_WRONLY', 'O_RDWR', 'O_CREAT,',
                          'cp_commitChanges', 'MarkBootSuccessful', 'sys.boot_completed'):
            self.assertNotIn(forbidden, source)


if __name__ == '__main__':
    unittest.main()
