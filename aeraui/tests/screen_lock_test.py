"""Verification-only lock state and real Android metadata, using fake credentials."""
import hashlib
import shutil
import sqlite3
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'), 'Requires Linux C++')
class ScreenLockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.path = Path(cls.temp.name)
        cls.driver = cls.path / 'screen-lock'
        result = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
            '-I', str(ROOT), str(ROOT/'aeraui/tests/screen_lock_driver.cpp'),
            str(ROOT/'aeraui/platform/screen_lock.cpp'),
            str(ROOT/'aeraui/platform/android_screen_metadata.cpp'),
            '-lcrypto', '-lsqlite3', '-o', str(cls.driver)], capture_output=True, text=True)
        if result.returncode: raise RuntimeError(result.stderr)
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def run_driver(self, *args):
        return subprocess.check_output([str(self.driver), *args], text=True, timeout=10)
    def test_actual_backend_only_prepares_after_owner_decryption(self):
        from screen_timer_test import definition
        backend=(ROOT/'aeraui/platform/aera_backend.cpp').read_text()
        pieces=definition(backend,'AndroidScreenLock& ScreenLock()')+'\n'+definition(backend,'void PrepareAndroidScreenLock()')
        program=r'''#include "aeraui/platform/android_screen_credential.hpp"
#include <cassert>
#include <cstdlib>
using namespace aeraui;
int mode=0, reads=0;
constexpr const char* TW_IS_FBE="fbe";
constexpr const char* TW_IS_DECRYPTED="decrypted";
struct DataManager { static int GetIntValue(const char* key) {
  const std::string s=key; return s=="fbe" ? (mode&1) : s=="decrypted" ? (mode&2) : 1;
} };
struct Partitions { bool Is_Mounted_By_Path(const char*) { return mode&8; } } PartitionManager;
namespace android::base { bool GetBoolProperty(const char*,bool) { return mode&4; } }
void LoadAeraPreferencesIfAvailable() {}
namespace aeraui {
bool ReadAndroidScreenCredential(AndroidScreenCredential* m,const std::string&) { ++reads; m->type=3; return mode&16; }
bool AndroidScreenVerifierAvailable(const AndroidScreenCredential&) { return mode&32; }
ScreenVerification VerifyAndroidScreenCredential(const AndroidScreenCredential&,const std::string&) { return {true,0}; }
}
'''+pieces+r'''
int main(int argc,char** argv) {
  assert(argc==2); mode=atoi(argv[1]); PrepareAndroidScreenLock();
  assert(ScreenLock().Available()==(mode==63));
  assert(ScreenLock().Enabled()==(mode==63));
  if ((mode&15)!=15) assert(reads==0);
}
'''
        source=self.path/'capability.cpp'; source.write_text(program)
        driver=self.path/'capability'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I',str(ROOT),str(source),
            str(ROOT/'aeraui/platform/screen_lock.cpp'),'-o',str(driver)],check=True)
        for mode in (0,1,3,7,15,31,47,62,63): subprocess.run([str(driver),str(mode)],check=True)
    def test_crypto_disabled_build_never_offers_android_authentication(self):
        source=self.path/'no-crypto.cpp'
        source.write_text('#include "aeraui/platform/android_screen_credential.hpp"\n#include <cassert>\nint main() { aeraui::AndroidScreenCredential m; assert(!aeraui::ReadAndroidScreenCredential(&m)); assert(!aeraui::AndroidScreenVerifierAvailable(m)); assert(!aeraui::VerifyAndroidScreenCredential(m,"fake").success); }\n')
        driver=self.path/'no-crypto'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT),str(source),
            str(ROOT/'aeraui/platform/android_screen_credential_stub.cpp'),'-o',str(driver)],check=True)
        subprocess.run([str(driver)],check=True)
    def test_actual_gatekeeper_and_weaver_adapter_with_fake_hardware(self):
        headers=self.path/'hal-headers'
        for name in ('android/binder_manager.h','aidl/android/hardware/gatekeeper/IGatekeeper.h',
                     'aidl/android/hardware/weaver/IWeaver.h','android/hardware/gatekeeper/1.0/IGatekeeper.h',
                     'android/hardware/weaver/1.0/IWeaver.h'):
            path=headers/name; path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('#include "screen_hal_mock.hpp"\n')
        driver=self.path/'hal-driver'
        result=subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT),
            '-I',str(ROOT/'aeraui/tests'),'-I',str(headers),
            str(ROOT/'aeraui/tests/screen_hal_driver.cpp'),
            str(ROOT/'aeraui/platform/android_screen_metadata.cpp'),
            str(ROOT/'aeraui/platform/android_screen_verifier.cpp'),
            '-lcrypto','-lsqlite3','-o',str(driver)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        subprocess.run([str(driver)],check=True,timeout=10)
    def test_one_shot_authorization_and_input_validation(self): self.run_driver('state')
    def test_hardware_delay_and_local_throttle(self): self.run_driver('throttle')
    def test_sleep_during_verify_and_concurrent_submit_remain_locked(self): self.run_driver('race')
    def fixture(self, folder, type=3, salt=b'fake-salt', handle=b'opaque-handle', n=11, r=3, p=1):
        (folder/'system').mkdir(parents=True, exist_ok=True)
        blobs=folder/'system_de/0/spblob'; blobs.mkdir(parents=True, exist_ok=True)
        db=sqlite3.connect(folder/'system/locksettings.db')
        db.execute('CREATE TABLE locksettings (name TEXT, user INTEGER, value TEXT)')
        db.execute("INSERT INTO locksettings VALUES ('sp-handle',0,'10')"); db.commit(); db.close()
        pwd=struct.pack('>iBBB', type,n,r,p)+struct.pack('>i',len(salt))+salt+struct.pack('>i',len(handle))+handle
        (blobs/'000000000000000a.pwd').write_bytes(pwd)
        return blobs,pwd
    def test_android_types_and_token_match_android_personalization(self):
        for type,expected in ((1,2),(2,1),(3,3),(4,1)):
            folder=self.path/('type'+str(type)); blobs,_=self.fixture(folder,type)
            before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.rglob('*') if p.is_file()}
            lines=self.run_driver('metadata',str(folder)).splitlines()
            self.assertEqual(lines[0],f'1 {expected} -1')
            token=hashlib.scrypt(b'1234',salt=b'fake-salt',n=2048,r=8,p=2,dklen=32)
            self.assertEqual(lines[1],hashlib.sha512(b'user-gk-authentication'.ljust(128,b'\0')+token).hexdigest())
            after={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.rglob('*') if p.is_file()}
            self.assertEqual(before,after)
    def test_weaver_without_gatekeeper_handle_and_token(self):
        folder=self.path/'weaver'; blobs,_=self.fixture(folder,handle=b'')
        (blobs/'000000000000000a.weaver').write_bytes(struct.pack('<Bi',1,7))
        lines=self.run_driver('metadata',str(folder)).splitlines()
        self.assertEqual(lines[0],'1 3 7')
        token=hashlib.scrypt(b'1234',salt=b'fake-salt',n=2048,r=8,p=2,dklen=32)
        self.assertEqual(lines[1],hashlib.sha512(b'weaver-key'.ljust(128,b'\0')+token).hexdigest())
    def test_missing_corrupt_unsupported_metadata_never_offers_authentication(self):
        folder=self.path/'bad'; blobs,pwd=self.fixture(folder)
        path=blobs/'000000000000000a.pwd'
        for bad in (b'',pwd[:3],pwd[:12],pwd[:-1],pwd+bytes(7),struct.pack('>i',-1)+pwd[4:],pwd[:4]+bytes([255])+pwd[5:]):
            path.write_bytes(bad)
            self.assertEqual(self.run_driver('metadata',str(folder)).strip(),'0 0 -1')
        path.unlink(); path.symlink_to(blobs/'stale.pwd'); (blobs/'stale.pwd').write_bytes(pwd)
        self.assertEqual(self.run_driver('metadata',str(folder)).strip(),'0 0 -1')
        self.assertEqual(self.run_driver('metadata',str(self.path/'missing')).strip(),'0 0 -1')
    def test_wal_snapshot_uses_current_handle_without_mutating_source(self):
        folder=self.path/'wal'; blobs,pwd=self.fixture(folder)
        db=sqlite3.connect(folder/'system/locksettings.db')
        db.execute('PRAGMA journal_mode=WAL')
        db.execute("UPDATE locksettings SET value='11'");db.commit()
        (blobs/'000000000000000b.pwd').write_bytes(struct.pack('>i',1)+pwd[4:])
        before={str(p):p.read_bytes() for p in folder.rglob('*') if p.is_file()}
        self.assertEqual(self.run_driver('metadata',str(folder)).splitlines()[0],'1 2 -1')
        self.assertEqual(before,{str(p):p.read_bytes() for p in folder.rglob('*') if p.is_file()})
        db.close()
    def test_bouncer_does_not_use_decrypt_or_separate_password(self):
        engine=(ROOT/'aeraui/core/engine.cpp').read_text()
        action=engine[engine.index('if (action == Action::kScreenCredentialSubmit)'):engine.index('if (action == Action::kUnlock)')]
        self.assertIn('RecoveryVerifyScreenLock(credential)',action)
        self.assertNotIn('aeraui_decrypt_data',action)
        backend=(ROOT/'aeraui/platform/aera_backend.cpp').read_text()
        self.assertIn('GetBoolProperty("twrp.user.0.decrypt", false)',backend)
        self.assertNotIn('screen-lock.conf',backend)
        self.assertNotIn('RecoveryChangeScreenLockPassword',backend)
        self.assertIn('if (!RecoveryConsumeScreenUnlock()) return;',engine)
        self.assertIn('(!lock_overlay_ || !RecoveryScreenLockEnabled()) &&',engine)
if __name__=='__main__': unittest.main()
