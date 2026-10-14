// SPDX-License-Identifier: Apache-2.0
#include "aeraui/platform/screen_lock.hpp"
#include "aeraui/platform/android_screen_credential.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
using namespace aeraui;
int main(int argc, char** argv) {
  assert(argc >= 2);
  const std::string test = argv[1];
  if (test == "state") {
    AndroidScreenLock lock;
    assert(!lock.Available() && !lock.SetEnabled(true));
    assert(!lock.Prepare(0, {}) && !lock.Prepare(4, {}));
    assert(lock.Prepare(3, [](const std::string& s) { return ScreenVerification{s == "1234", 0}; }));
    assert(lock.CredentialType() == 3 && lock.SetEnabled(true));
    assert(!lock.ConsumeUnlock());
    assert(lock.Verify("1234") && lock.ConsumeUnlock() && !lock.ConsumeUnlock());
    lock.Lock(); assert(!lock.ConsumeUnlock());
    assert(!lock.Verify("bad"));
    assert(lock.Verify("1234")); lock.Lock(); assert(!lock.ConsumeUnlock());
    assert(!lock.Verify(std::string(257, '1')) && !lock.Verify(std::string("1234\0", 5)));
    assert(lock.SetEnabled(false) && lock.ConsumeUnlock());
  } else if (test == "throttle") {
    AndroidScreenLock lock; int calls = 0;
    assert(lock.Prepare(1, [&](const std::string&) { ++calls; return ScreenVerification{false, 17}; }));
    assert(lock.SetEnabled(true) && !lock.Verify("wrong"));
    assert(calls == 1 && lock.RetrySeconds() >= 17 && !lock.Verify("again") && calls == 1);
    assert(!lock.ConsumeUnlock());
  } else if (test == "race") {
    AndroidScreenLock lock; std::mutex mutex; std::condition_variable condition;
    bool entered = false, release = false;
    lock.Prepare(1, [&](const std::string&) {
      std::unique_lock<std::mutex> guard(mutex); entered = true; condition.notify_all();
      condition.wait(guard, [&] { return release; }); return ScreenVerification{true, 0};
    });
    lock.SetEnabled(true);
    bool result = true;
    std::thread check([&] { result = lock.Verify("fake"); });
    { std::unique_lock<std::mutex> guard(mutex); condition.wait(guard, [&] { return entered; }); }
    assert(!lock.Verify("concurrent")); lock.Lock();
    { std::lock_guard<std::mutex> guard(mutex); release = true; } condition.notify_all();
    check.join(); assert(!result && !lock.ConsumeUnlock());
  } else if (test == "metadata") {
    assert(argc == 3);
    AndroidScreenCredential m;
    const bool okay = ReadAndroidScreenCredential(&m, argv[2]);
    std::cout << okay << " " << m.type << " " << m.weaver_slot << "\n";
    if (okay) {
      std::vector<uint8_t> token;
      assert(DeriveAndroidScreenToken(m, "1234", &token));
      for (const auto b : token) printf("%02x", b);
      puts("");
    }
  }
}
