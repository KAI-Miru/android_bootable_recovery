"""Exercise the real operation-wake method without a display or credentials."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from screen_timer_test import definition

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'),
                     'Requires Linux C++')
class ScreenWakeTests(unittest.TestCase):
    def test_operation_wake_preserves_the_owner_credential_gate(self):
        source = (ROOT / 'aeraui/core/engine.cpp').read_text()
        method = definition(source, '  bool WakeForOperation()')
        program = r'''
#include <cassert>
struct Object {};
static bool protected_lock;
static int deletes;
bool RecoveryScreenLockEnabled() { return protected_lock; }
void lv_obj_delete(Object*) { ++deletes; }
struct Engine {
  bool backend_ready_ = true, interactive_ready_ = true;
  bool decryption_active_ = false, decrypt_running_ = false;
  bool lock_ambient_ = false, suspended = true, needs_wake = true;
  bool credential_required = false;
  int bouncers = 0;
  Object object;
  Object* lock_overlay_ = nullptr;
  bool NeedsWake() { return needs_wake; }
  void SetSuspended(bool value) { suspended = value; }
  void ShowLockScreen(bool ambient) {
    assert(!ambient);
    ++bouncers;
    lock_overlay_ = &object;
    lock_ambient_ = false;
    credential_required = true;
  }
''' + method + r'''
};
int main() {
  for (bool enabled : {false, true})
    for (bool overlay : {false, true})
      for (bool ambient : {false, true}) {
        protected_lock = enabled; deletes = 0;
        Engine engine;
        engine.lock_ambient_ = ambient;
        if (overlay) engine.lock_overlay_ = &engine.object;
        assert(engine.WakeForOperation());
        assert(!engine.suspended);
        if (enabled) {
          assert(engine.bouncers == 1 && engine.credential_required);
          assert(engine.lock_overlay_ && !engine.lock_ambient_ && deletes == 0);
        } else {
          assert(engine.bouncers == 0 && !engine.lock_overlay_);
          assert(deletes == static_cast<int>(overlay));
        }
      }
  for (int gate = 0; gate < 5; ++gate) {
    protected_lock = true; deletes = 0;
    Engine engine; engine.lock_overlay_ = &engine.object;
    if (gate == 0) engine.backend_ready_ = false;
    if (gate == 1) engine.interactive_ready_ = false;
    if (gate == 2) engine.decryption_active_ = true;
    if (gate == 3) engine.decrypt_running_ = true;
    if (gate == 4) engine.needs_wake = false;
    assert(!engine.WakeForOperation());
    assert(engine.suspended && engine.lock_overlay_ && engine.bouncers == 0);
    assert(deletes == 0);
  }
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / 'wake.cpp', Path(folder) / 'wake'
            cpp.write_text('#include <initializer_list>\n' + program)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
