// SPDX-License-Identifier: Apache-2.0
#include "screen_hal_mock.hpp"
#include "aeraui/platform/android_screen_credential.hpp"
#include <cassert>
using namespace aeraui;
using namespace screen_hal_mock;
int main() {
  AndroidScreenCredential metadata;
  metadata.type = 3; metadata.synthetic = true; metadata.salt = {1,2,3,4};
  metadata.handle = {5,6,7,8}; metadata.scrypt_n = 11; metadata.scrypt_r = 3; metadata.scrypt_p = 1;
  assert(!AndroidScreenVerifierAvailable(metadata));
  assert(!VerifyAndroidScreenCredential(metadata, "1234").success);
  for (const bool aidl : {false, true}) {
    hidl_gatekeeper = !aidl; aidl_gatekeeper = aidl;
    assert(AndroidScreenVerifierAvailable(metadata));
    for (const int code : {0, 1, -1, -2, 2}) {
      status = code; timeout = code == -2 ? 5010 : 0;
      const auto result = VerifyAndroidScreenCredential(metadata, "1234");
      assert(result.success == (code == 0 || code == 1));
      assert(result.retry_seconds == (code == -2 ? 6 : 0));
      assert(uid == 100000 && challenge == 0);
    }
    status = 0; transport = false;
    assert(!VerifyAndroidScreenCredential(metadata, "1234").success); transport = true;
  }
  aidl_gatekeeper = hidl_gatekeeper = false; metadata.weaver_slot = 7;
  for (const bool aidl : {false, true}) {
    hidl_weaver = !aidl; aidl_weaver = aidl;
    assert(AndroidScreenVerifierAvailable(metadata));
    for (const int code : {0, 1, 2, 3}) {
      status = code; timeout = code == 0 ? 0 : 10010;
      const auto result = VerifyAndroidScreenCredential(metadata, "1234");
      assert(result.success == (code == 0));
      assert(result.retry_seconds == (code == 0 ? 0 : 11));
      assert(uid == 7 && challenge == 16);
    }
    status = 0; timeout = 0;
    for (const int invalid_size : {0, 65}) {
      key_size = invalid_size; const int before = calls;
      assert(!VerifyAndroidScreenCredential(metadata, "1234").success && calls == before);
    }
    key_size = 16; metadata.weaver_slot = 8;
    assert(!VerifyAndroidScreenCredential(metadata, "1234").success); metadata.weaver_slot = 7;
    transport = false; assert(!VerifyAndroidScreenCredential(metadata, "1234").success); transport = true;
  }
}
