"""Verify the recovery entrypoint keeps the pinned crypto startup unchanged."""
import hashlib
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class KeystoreStartupTests(unittest.TestCase):
    def test_only_android_boot_watcher_differs_from_pinned_entrypoint(self):
        source = (ROOT / 'aera_keystore/main.rs').read_bytes().replace(b'\r\n', b'\n')
        metadata = json.loads((ROOT / 'aera_keystore/source.json').read_text())
        recovery = (b'    // Recovery never completes an Android framework boot. Keep the library\n'
                    b'    // boot-completed/GC gate false; all crypto and Binder startup below is\n'
                    b'    // unchanged from the pinned platform entrypoint.\n')
        self.assertEqual(source.count(recovery), 1)
        restored = source.replace(recovery,
            b'    std::thread::spawn(keystore2::globals::await_boot_completed);\n')
        self.assertEqual(hashlib.sha256(restored).hexdigest(), metadata['upstream_sha256'])
        self.assertNotIn(b'BOOT_COMPLETED.store', source)
        self.assertNotIn(b'sys.boot_completed', source)

    def test_crypto_entropy_negotiation_and_all_binder_services_are_retained(self):
        source = (ROOT / 'aera_keystore/main.rs').read_text()
        for marker in ('ENFORCEMENTS.install_confirmation_token_receiver',
                       'entropy::register_feeder()',
                       'shared_secret_negotiation::perform_shared_secret_negotiation()',
                       'Maintenance::check_send_module_info()',
                       'KeystoreService::new_native_binder',
                       'AuthorizationManager::new_native_binder',
                       'LegacyKeystore::new_native_binder',
                       'binder::ProcessState::join_thread_pool()'):
            self.assertIn(marker, source)

    def test_same_platform_libraries_and_separate_recovery_executable(self):
        source = (ROOT / 'aera_keystore/Android.bp').read_text()
        libraries = re.search(r'rustlibs:\s*\[(.*?)\]', source, re.S).group(1)
        self.assertEqual(set(re.findall(r'"([^"]+)"', libraries)),
                         {'libandroid_logger', 'libbinder_rs', 'liblog_rust',
                          'libkeystore2', 'liblegacykeystore-rust', 'librusqlite'})
        self.assertIn('name: "aera_keystore2"', source)
        self.assertIn('prefer_rlib: true', source)
        self.assertIn('"keystore_cli_v2"', source)

    def test_init_and_packaging_select_recovery_entrypoint_without_boot_spoof(self):
        init = (ROOT / 'etc/init/keystore2.rc').read_text()
        self.assertIn('service keystore2 /system/bin/aera_keystore2 /tmp/misc/keystore', init)
        self.assertNotIn('setprop sys.boot_completed', init)
        self.assertIn('aera_keystore2 \\', (ROOT / 'Android.mk').read_text())
        self.assertIn('RECOVERY_BINARY_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/aera_keystore2',
                      (ROOT / 'Android.mk').read_text())


if __name__ == '__main__':
    unittest.main()
