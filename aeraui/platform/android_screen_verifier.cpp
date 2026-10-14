// SPDX-License-Identifier: Apache-2.0
#include "android_screen_credential.hpp"
#include <algorithm>
#include <climits>
#include <memory>
#include <hardware/hw_auth_token.h>
#include <openssl/crypto.h>
#include <android/binder_manager.h>
#include <aidl/android/hardware/gatekeeper/IGatekeeper.h>
#include <aidl/android/hardware/weaver/IWeaver.h>
#include <android/hardware/gatekeeper/1.0/IGatekeeper.h>
#include <android/hardware/weaver/1.0/IWeaver.h>

namespace aeraui {
namespace {
namespace ag = aidl::android::hardware::gatekeeper;
namespace aw = aidl::android::hardware::weaver;
namespace hg = android::hardware::gatekeeper::V1_0;
namespace hw = android::hardware::weaver::V1_0;
template<class T> std::shared_ptr<T> Service() {
  const auto instance = std::string(T::descriptor) + "/default";
  return T::fromBinder(ndk::SpAIBinder(AServiceManager_checkService(instance.c_str())));
}
int Delay(int64_t milliseconds) {
  return static_cast<int>(std::clamp<int64_t>(milliseconds / 1000 + (milliseconds % 1000 > 0), 0, INT_MAX));
}
ScreenVerification Gatekeeper(const AndroidScreenCredential& m, const std::vector<uint8_t>& token) {
  if (m.handle.empty()) return {};
  const int user = 100000 + m.user_id; // SyntheticPasswordManager.fakeUid.
  if (auto service = Service<ag::IGatekeeper>()) {
    ag::GatekeeperVerifyResponse response{};
    const auto result = service->verify(user, 0, m.handle, token, &response);
    const bool okay = result.isOk() && response.hardwareAuthToken.mac.size() == 32 &&
        (response.statusCode == ag::IGatekeeper::STATUS_OK ||
         response.statusCode == ag::IGatekeeper::STATUS_REENROLL);
    const int delay = result.isOk() && response.statusCode == ag::IGatekeeper::ERROR_RETRY_TIMEOUT
        ? Delay(response.timeoutMs) : 0;
    // Discard the returned token; screen unlock does not authorize keystore or
    // reenroll, unwrap synthetic passwords, install CE keys, or mount data.
    OPENSSL_cleanse(response.hardwareAuthToken.mac.data(), response.hardwareAuthToken.mac.size());
    return {okay, delay};
  }
  auto service = hg::IGatekeeper::tryGetService();
  if (!service) return {};
  android::hardware::hidl_vec<uint8_t> handle, secret;
  handle.setToExternal(const_cast<uint8_t*>(m.handle.data()), m.handle.size());
  secret.setToExternal(const_cast<uint8_t*>(token.data()), token.size());
  ScreenVerification verification;
  bool called = false;
  const auto result = service->verify(user, 0, handle, secret, [&](const hg::GatekeeperResponse& r) {
    called = true;
    verification.success = r.data.size() == sizeof(hw_auth_token_t) &&
        (r.code == hg::GatekeeperStatusCode::STATUS_OK ||
         r.code == hg::GatekeeperStatusCode::STATUS_REENROLL);
    if (r.code == hg::GatekeeperStatusCode::ERROR_RETRY_TIMEOUT)
      verification.retry_seconds = Delay(r.timeout);
  });
  return result.isOk() && called ? verification : ScreenVerification{};
}
ScreenVerification Weaver(const AndroidScreenCredential& m, std::vector<uint8_t> token) {
  ScreenVerification verification;
  if (auto service = Service<aw::IWeaver>()) {
    aw::WeaverConfig config{};
    aw::WeaverReadResponse response{};
    if (service->getConfig(&config).isOk() && config.keySize > 0 &&
        config.keySize <= static_cast<int>(token.size()) && m.weaver_slot < config.slots) {
      OPENSSL_cleanse(token.data() + config.keySize, token.size() - config.keySize);
      token.resize(config.keySize);
      if (service->read(m.weaver_slot, token, &response).isOk()) {
        verification = {response.status == aw::WeaverReadStatus::OK && response.timeout == 0,
                        Delay(response.timeout)};
        OPENSSL_cleanse(response.value.data(), response.value.size());
      }
    }
  } else if (auto hidl = hw::IWeaver::tryGetService()) {
    hw::WeaverConfig config{};
    bool ready = false, called = false;
    const auto config_result = hidl->getConfig([&](hw::WeaverStatus s, hw::WeaverConfig c) {
      config = c;
      ready = s == hw::WeaverStatus::OK && c.keySize > 0 && c.keySize <= token.size() &&
          static_cast<uint32_t>(m.weaver_slot) < c.slots;
    });
    if (config_result.isOk() && ready) {
      android::hardware::hidl_vec<uint8_t> key;
      key.setToExternal(token.data(), config.keySize);
      const auto result = hidl->read(m.weaver_slot, key,
          [&](hw::WeaverReadStatus s, const hw::WeaverReadResponse& r) {
        called = true;
        verification = {s == hw::WeaverReadStatus::OK && r.timeout == 0, Delay(r.timeout)};
      });
      if (!result.isOk() || !called) verification = {};
    }
  }
  OPENSSL_cleanse(token.data(), token.size());
  return verification;
}
}  // namespace
bool AndroidScreenVerifierAvailable(const AndroidScreenCredential& m) {
  return m.weaver_slot >= 0
      ? bool(Service<aw::IWeaver>()) || bool(hw::IWeaver::tryGetService())
      : bool(Service<ag::IGatekeeper>()) || bool(hg::IGatekeeper::tryGetService());
}
ScreenVerification VerifyAndroidScreenCredential(const AndroidScreenCredential& m,
                                                 const std::string& credential) {
  std::vector<uint8_t> token;
  if (!DeriveAndroidScreenToken(m, credential, &token)) return {};
  const auto result = m.weaver_slot >= 0 ? Weaver(m, token) : Gatekeeper(m, token);
  OPENSSL_cleanse(token.data(), token.size());
  return result;
}
}  // namespace aeraui
