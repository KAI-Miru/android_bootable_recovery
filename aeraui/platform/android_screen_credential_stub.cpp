// SPDX-License-Identifier: Apache-2.0
#include "android_screen_credential.hpp"
#ifndef TW_INCLUDE_CRYPTO
namespace aeraui {
bool ReadAndroidScreenCredential(AndroidScreenCredential*, const std::string&) { return false; }
bool AndroidScreenVerifierAvailable(const AndroidScreenCredential&) { return false; }
ScreenVerification VerifyAndroidScreenCredential(const AndroidScreenCredential&, const std::string&) { return {}; }
}  // namespace aeraui
#endif
