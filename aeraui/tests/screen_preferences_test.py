"""Check timeout API bounds and the actual early-preference codec."""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from screen_timer_test import definition

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'), 'Requires Linux C++')
class ScreenPreferencesTests(unittest.TestCase):
    def test_timeout_roundtrip_legacy_defaults_and_device_opt_out(self):
        source = (ROOT / 'aeraui/platform/aera_backend.cpp').read_text(encoding='utf-8')
        pieces = [definition(source, 'struct EarlyUiPreferences') + ';']
        for name in ('bool ParseInteger(', 'bool ValidPlainText(', 'bool ValidLanguage(',
                     'bool ValidAccent(', 'bool SetEarlyUiValue(', 'std::string SerializeEarlyUiPreferences(',
                     'bool RecoveryScreenTimeoutAvailable(', 'int RecoveryScreenTimeout(',
                     'bool RecoverySetScreenTimeout('):
            pieces.append(definition(source, name))
        constants = '\n'.join(re.findall(r'constexpr int kDockLayoutMax[^;]+;', source))
        program = '''#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include "aeraui/backend.hpp"
using namespace aeraui;
struct DataManager {
  static inline int timeout = 60;
  static int GetIntValue(const char*) { return timeout; }
  static int SetValue(const char*, int value) { timeout = value; return 0; }
};
''' + 'namespace aeraui {\n' + constants + '\n' + '\n'.join(pieces) + '''
}
int main() {
  EarlyUiPreferences values{}; values.screen_timeout = 60;
  assert(SetEarlyUiValue(&values, "brightness", "50"));
  assert(values.screen_timeout == 60); // Older files retain the default.
  for (int seconds : {0, 15, 60, 600, 3600}) {
    assert(SetEarlyUiValue(&values, "screen_timeout", std::to_string(seconds)));
    assert(values.screen_timeout == seconds);
    assert(SerializeEarlyUiPreferences(values).find("screen_timeout=" + std::to_string(seconds) + "\\n") != std::string::npos);
  }
  for (const char* invalid : {"-1", "3601", "60 seconds", "", "99999999999999999999"}) {
    assert(!SetEarlyUiValue(&values, "screen_timeout", invalid));
    assert(values.screen_timeout == 3600);
  }
#ifdef TW_NO_SCREEN_TIMEOUT
  assert(!RecoveryScreenTimeoutAvailable() && RecoveryScreenTimeout() == 0);
  assert(!RecoverySetScreenTimeout(60));
#else
  assert(RecoveryScreenTimeoutAvailable());
  assert(RecoverySetScreenTimeout(0) && RecoveryScreenTimeout() == 0);
  assert(RecoverySetScreenTimeout(60) && RecoveryScreenTimeout() == 60);
  assert(!RecoverySetScreenTimeout(-1) && !RecoverySetScreenTimeout(3601));
  assert(RecoveryScreenTimeout() == 60);
#endif
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / 'driver.cpp').write_text(program, encoding='utf-8')
            for flags in ([], ['-DTW_NO_SCREEN_TIMEOUT']):
                result = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'aeraui/include'), *flags, str(path / 'driver.cpp'),
                    '-o', str(path / 'driver')], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(path / 'driver')], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
