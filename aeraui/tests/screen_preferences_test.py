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
    def test_shared_preferences_save_and_restore_without_early_cache(self):
        source = (ROOT / 'aeraui/platform/aera_backend.cpp').read_text(encoding='utf-8')
        header = '\n'.join(p.read_text(encoding='utf-8') for p in (ROOT / 'aeraui/include/aeraui').glob('*.hpp'))
        save = definition(source, 'bool SaveAeraPreferences(bool require_early) {')
        load = definition(source, 'void LoadAeraPreferencesIfAvailable() {')
        getters = set(re.findall(r'(Recovery\w+)\(\)', save)) - {'RecoveryScreenTimeout'}
        stubs = []
        for getter in sorted(getters):
            declaration = re.search(r'([^\n;{}]+\b' + getter + r'\(\))\s*;', header)
            self.assertIsNotNone(declaration, getter)
            stubs.append(declaration[1].strip() + ' { return {}; }')
        constants = '\n'.join('constexpr const char* ' + name + ' = "' + name + '";'
            for name in sorted(set(re.findall(r'DataManager::Get(?:Int|Str)Value\(([A-Z][A-Z0-9_]+)\)', save))))
        parts = [definition(source, 'struct EarlyUiPreferences') + ';']
        for name in ('bool ParseInteger(', 'bool ValidPlainText(', 'bool ValidLanguage(',
                     'bool ValidAccent(', 'bool SetEarlyUiValue('):
            parts.append(definition(source, name))
        parts.append('\n'.join(re.findall(r'constexpr int kDockLayoutMax[^;]+;', source)))
        # The codec's bound must precede its use.
        parts = [parts[-1]] + parts[:-1]
        program = '''#include <algorithm>
#include <atomic>
#include <thread>
#include <sys/stat.h>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "aeraui/backend.hpp"
namespace aera::shared_storage { void NormalizeAll() {} }
using namespace aeraui;
struct DataManager {
  static inline int timeout = 60;
  static int GetIntValue(const char* key) {
    return std::string(key)=="tw_screen_timeout_secs" ? timeout : 0;
  }
  static std::string GetStrValue(const char*) { return ""; }
  static void SetValue(const char* key, int value) {
    if (std::string(key)=="tw_screen_timeout_secs") timeout=value;
  }
  static void SetValue(const char* key, const std::string& value) { SetValue(key, atoi(value.c_str())); }
};
struct TWFunc { static bool Recursive_Mkdir(const char*, bool) { return true; } };
#define LOGINFO(...) ((void)0)
#define LOGERR(...) ((void)0)
namespace aeraui {
''' + constants + '\n' + '\n'.join(parts) + '\n' + '\n'.join(stubs) + '''
std::string kAeraPreferencesPath;
constexpr const char* kFastbootWifiPreference = "fastboot_wifi";
EarlyUiPreferences gEarlyDefaults{};
bool gPrimaryWifiPreferencesLoaded = false;
EarlyUiPreferences CaptureCurrentEarlyUiPreferences() { return {}; }
bool SaveEarlyWifiPreferences() { return false; }
void CaptureEarlyDefaultsIfNeeded() { gEarlyDefaults.screen_timeout=60; }
void LoadEarlyUiPreferencesIfAvailable() {}
bool SaveEarlyUiPreferences() { return false; } // Device has no early cache.
void ApplyEarlyUiSideEffects() {}
void ApplyEarlyUiPreferences(const EarlyUiPreferences& values) { DataManager::timeout=values.screen_timeout; }
int RecoveryScreenTimeout() { return DataManager::timeout; }
bool WriteFileAtomically(const std::string& path, const std::string& contents) {
  std::ofstream file(path); file << contents; return static_cast<bool>(file);
}
''' + save + '\n' + load + '''
}
int main(int argc, char** argv) {
  assert(argc==3);
  kAeraPreferencesPath=argv[1];
  const int chosen=atoi(argv[2]);
  DataManager::timeout=chosen;
  assert(SaveAeraPreferences(false));
  std::ifstream file(kAeraPreferencesPath);
  const std::string contents((std::istreambuf_iterator<char>(file)), {});
  assert(contents.find("screen_timeout="+std::to_string(chosen)+"\\n")!=std::string::npos);
  DataManager::timeout=60;
  LoadAeraPreferencesIfAvailable();
  assert(DataManager::timeout==chosen);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'shared.cpp').write_text(program, encoding='utf-8')
            result = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'aeraui/include'), str(folder / 'shared.cpp'),
                str(ROOT / 'aeraui/features/developer/kernel_settings.cpp'), '-o', str(folder / 'shared')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for seconds in (0, 15, 30, 60, 600, 3600):
                subprocess.run([str(folder / 'shared'), str(folder / 'preferences.conf'), str(seconds)],
                               check=True, timeout=5)

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
#include <atomic>
#include <thread>
#include <sys/stat.h>
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
                    str(ROOT / 'aeraui/features/developer/kernel_settings.cpp'), '-o', str(path / 'driver')], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(path / 'driver')], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
