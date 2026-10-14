// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <linux/input.h>
#include <string>
#include <vector>
#include "aeraui/platform/aera_screen_timer.hpp"
#include "twrp-functions.hpp"

static time_t now_seconds = 0;
static std::string brightness = "1234";
static int blank_hooks = 0, wake_hooks = 0, panel_blank = 0, panel_wake = 0;
extern "C" int __wrap_clock_gettime(clockid_t, timespec* value) {
  *value = {now_seconds, 0}; return 0;
}
void TWFunc::Set_Brightness(const std::string& value) { brightness = value; }
void TWFunc::check_and_run_script(const char*, const char* argument) {
  if (std::string(argument) == "blank") ++blank_hooks; else ++wake_hooks;
}
void gr_fb_blank(bool blank) { blank ? ++panel_blank : ++panel_wake; }

constexpr int ANDROID_LOG_DEBUG = 0, ANDROID_LOG_INFO = 1;
constexpr char kLogTag[] = "timer test";
static int __android_log_print(int, const char*, const char*, ...) { return 0; }
static uint64_t MonotonicMilliseconds() { return now_seconds * 1000ULL; }
static void WriteVolume(int) {}
struct PointerEvent { int x, y, slot; bool pressed; };
struct InteractionBoost { void Boost() {} };
struct Engine {
  int pointers = 0, locks = 0;
  bool suspended = false;
  void MouseMove(int, int) {}
  void MouseWheel(int) {}
  void MouseButton(bool) {}
  void SetPointer(PointerEvent) { ++pointers; }
  bool KeyboardKey(int key, int) { return key == KEY_A; }
  void ShowLockScreen() { ++locks; }
  void SetSuspended(bool value) { suspended = value; }
  void RunFrame() {}
  void ShowVolume(int) {}
  void NavigateBack() {}
  void NavigateHome() {}
};

// RUNNER_FUNCTIONS

int main() {
  aeraScreenTimer.SetTimeout(60);
  Engine engine;
  HardwareState hardware;
  [[maybe_unused]] InteractionBoost performance;
  now_seconds = 58;
  CheckScreenTimeout(engine, hardware);
#ifdef TW_NO_SCREEN_TIMEOUT
  assert(brightness == "1234");
  now_seconds = 10000;
  CheckScreenTimeout(engine, hardware);
  assert(!hardware.screen_off && !engine.suspended && !blank_hooks);
#else
  assert(brightness == "5" && !hardware.screen_off);
  // Actual translated touch input resets the timer and restores dimmed light.
  input_event touch{}; touch.type = EV_ABS; touch.code = 1;
  HandleEvent(engine, performance, hardware, touch);
  assert(brightness == "1234" && engine.pointers == 1);
  now_seconds = 117;
  CheckScreenTimeout(engine, hardware);
  assert(!hardware.screen_off);
  input_event key{}; key.type = EV_KEY; key.code = KEY_A; key.value = 1;
  HandleEvent(engine, performance, hardware, key);
  now_seconds = 176;
  CheckScreenTimeout(engine, hardware);
  assert(!hardware.screen_off);
  now_seconds = 177;
  CheckScreenTimeout(engine, hardware);
  assert(hardware.screen_off && engine.suspended && brightness == "0");
  assert(blank_hooks == 1);
  HandleEvent(engine, performance, hardware, touch);
  assert(engine.pointers == 1 && hardware.screen_off);
  input_event power{}; power.type = EV_KEY; power.code = KEY_POWER; power.value = 1;
  HandleEvent(engine, performance, hardware, power);
  assert(!hardware.screen_off && !engine.suspended && engine.locks == 1);
  assert(brightness == "1234" && wake_hooks == 1);
  power.value = 0;
  HandleEvent(engine, performance, hardware, power);
  assert(!hardware.screen_off);  // Waking press must not immediately blank again.
#ifdef TW_NO_SCREEN_BLANK
  assert(panel_blank == 0 && panel_wake == 0);
#else
  assert(panel_blank == 1 && panel_wake == 1);
#endif
  // Never restores a dimmed screen and still permits manual blank/wake.
  now_seconds += 58;
  CheckScreenTimeout(engine, hardware);
  assert(brightness == "5");
  aeraScreenTimer.SetTimeout(0);
  assert(brightness == "1234");
  now_seconds += 10000;
  CheckScreenTimeout(engine, hardware);
  assert(!hardware.screen_off);
  aeraScreenTimer.Toggle();
  assert(aeraScreenTimer.IsScreenOff() && brightness == "0");
  aeraScreenTimer.Toggle();
  assert(!aeraScreenTimer.IsScreenOff() && brightness == "1234");
  aeraScreenTimer.SetTimeout(-1);
  now_seconds += 10000;
  assert(!aeraScreenTimer.IsScreenOff());
#endif
}
