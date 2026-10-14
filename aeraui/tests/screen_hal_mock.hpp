// SPDX-License-Identifier: Apache-2.0
// Only included through temporary host-test include paths, never Android.mk.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace screen_hal_mock {
inline bool aidl_gatekeeper = false, hidl_gatekeeper = false, aidl_weaver = false, hidl_weaver = false;
inline bool transport = true;
inline int status = 0, timeout = 0, calls = 0, uid = -1, challenge = -1;
inline int slots = 8, key_size = 16;
}
inline void* AServiceManager_checkService(const char* name) {
  const bool available = std::string(name).find("gatekeeper") != std::string::npos
      ? screen_hal_mock::aidl_gatekeeper : screen_hal_mock::aidl_weaver;
  return available ? reinterpret_cast<void*>(1) : nullptr;
}
namespace ndk { struct SpAIBinder { void* value; explicit SpAIBinder(void* v) : value(v) {} }; }
struct ScreenMockResult { bool isOk() const { return screen_hal_mock::transport; } };
namespace android::hardware {
template<class T> struct hidl_vec : std::vector<T> {
  void setToExternal(T* p, size_t n) { this->assign(p, p+n); }
};
}
namespace aidl::android::hardware::gatekeeper {
struct GatekeeperVerifyResponse {
  int statusCode = 0, timeoutMs = 0;
  struct { std::vector<uint8_t> mac; } hardwareAuthToken;
};
struct IGatekeeper {
  static constexpr const char* descriptor = "android.hardware.gatekeeper.IGatekeeper";
  static constexpr int STATUS_OK = 0, STATUS_REENROLL = 1, ERROR_RETRY_TIMEOUT = -2;
  static std::shared_ptr<IGatekeeper> fromBinder(ndk::SpAIBinder b) {
    return b.value ? std::make_shared<IGatekeeper>() : nullptr;
  }
  ScreenMockResult verify(int user, int c, const std::vector<uint8_t>&, const std::vector<uint8_t>&,
                          GatekeeperVerifyResponse* response) {
    ++screen_hal_mock::calls; screen_hal_mock::uid = user; screen_hal_mock::challenge = c;
    response->statusCode = screen_hal_mock::status; response->timeoutMs = screen_hal_mock::timeout;
    response->hardwareAuthToken.mac.assign(32, 1);
    return {};
  }
};
}
namespace android::hardware::gatekeeper::V1_0 {
enum class GatekeeperStatusCode { STATUS_OK = 0, STATUS_REENROLL = 1, ERROR_RETRY_TIMEOUT = -2 };
struct GatekeeperResponse { GatekeeperStatusCode code; uint32_t timeout; hidl_vec<uint8_t> data; };
struct IGatekeeper {
  static std::shared_ptr<IGatekeeper> tryGetService() {
    return screen_hal_mock::hidl_gatekeeper ? std::make_shared<IGatekeeper>() : nullptr;
  }
  template<class F> ScreenMockResult verify(int user, int c, const hidl_vec<uint8_t>&,
                                          const hidl_vec<uint8_t>&, F callback) {
    ++screen_hal_mock::calls; screen_hal_mock::uid = user; screen_hal_mock::challenge = c;
    GatekeeperResponse response{static_cast<GatekeeperStatusCode>(screen_hal_mock::status),
                                static_cast<uint32_t>(screen_hal_mock::timeout), {}};
    response.data.resize(69);
    callback(response); return {};
  }
};
}
namespace aidl::android::hardware::weaver {
enum class WeaverReadStatus { OK = 0, FAILED = 1, INCORRECT_KEY = 2, THROTTLE = 3 };
struct WeaverConfig { int slots = 0, keySize = 0; };
struct WeaverReadResponse { WeaverReadStatus status; int64_t timeout = 0; std::vector<uint8_t> value; };
struct IWeaver {
  static constexpr const char* descriptor = "android.hardware.weaver.IWeaver";
  static std::shared_ptr<IWeaver> fromBinder(ndk::SpAIBinder b) {
    return b.value ? std::make_shared<IWeaver>() : nullptr;
  }
  ScreenMockResult getConfig(WeaverConfig* config) {
    config->slots = screen_hal_mock::slots; config->keySize = screen_hal_mock::key_size; return {};
  }
  ScreenMockResult read(int slot, const std::vector<uint8_t>& key, WeaverReadResponse* response) {
    ++screen_hal_mock::calls;
    screen_hal_mock::uid = slot; screen_hal_mock::challenge = key.size();
    response->status = static_cast<WeaverReadStatus>(screen_hal_mock::status);
    response->timeout = screen_hal_mock::timeout; response->value.resize(16); return {};
  }
};
}
namespace android::hardware::weaver::V1_0 {
enum class WeaverStatus { OK = 0, FAILED = 1 };
enum class WeaverReadStatus { OK = 0, FAILED = 1, INCORRECT_KEY = 2, THROTTLE = 3 };
struct WeaverConfig { uint32_t slots = 0, keySize = 0; };
struct WeaverReadResponse { uint32_t timeout = 0; hidl_vec<uint8_t> value; };
struct IWeaver {
  static std::shared_ptr<IWeaver> tryGetService() {
    return screen_hal_mock::hidl_weaver ? std::make_shared<IWeaver>() : nullptr;
  }
  template<class F> ScreenMockResult getConfig(F callback) {
    callback(WeaverStatus::OK, WeaverConfig{static_cast<uint32_t>(screen_hal_mock::slots),
                                          static_cast<uint32_t>(screen_hal_mock::key_size)});
    return {};
  }
  template<class F> ScreenMockResult read(int slot, const hidl_vec<uint8_t>& key, F callback) {
    ++screen_hal_mock::calls; screen_hal_mock::uid = slot; screen_hal_mock::challenge = key.size();
    WeaverReadResponse response{static_cast<uint32_t>(screen_hal_mock::timeout), {}};
    response.value.resize(16);
    callback(static_cast<WeaverReadStatus>(screen_hal_mock::status), response); return {};
  }
};
}
