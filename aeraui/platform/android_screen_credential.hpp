// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "screen_lock.hpp"
#include <cstdint>
#include <vector>
namespace aeraui {
struct AndroidScreenCredential {
  int type = 0, user_id = 0;
  bool synthetic = false;
  int weaver_slot = -1;
  unsigned scrypt_n = 0, scrypt_r = 0, scrypt_p = 0;
  std::vector<uint8_t> salt, handle;
};
// Active Android owner credential metadata, captured after FBE unlock.
// Opaque handles only; no plaintext credential or disk key is retained.
bool ReadAndroidScreenCredential(AndroidScreenCredential* metadata,
                                 const std::string& data_root = "/data");
bool ParseAndroidPasswordData(const std::vector<uint8_t>& bytes,
                              AndroidScreenCredential* metadata);
bool DeriveAndroidScreenToken(const AndroidScreenCredential& metadata,
                              const std::string& credential, std::vector<uint8_t>* token);
bool AndroidScreenVerifierAvailable(const AndroidScreenCredential& metadata);
ScreenVerification VerifyAndroidScreenCredential(const AndroidScreenCredential& metadata,
                                                 const std::string& credential);
}  // namespace aeraui
