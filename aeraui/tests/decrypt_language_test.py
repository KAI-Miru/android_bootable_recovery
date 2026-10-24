"""Run the production language setter and preference-restore code without Android."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from screen_timer_test import definition

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'), 'Requires Linux C++')
class DecryptLanguageTests(unittest.TestCase):
    def test_explicit_language_survives_early_and_post_decryption_preferences(self):
        source = (ROOT / 'aeraui/platform/aera_backend.cpp').read_text()
        pieces = [definition(source, 'struct EarlyUiPreferences') + ';',
                  definition(source, 'std::string SessionLanguageOr('),
                  definition(source, 'void ApplyEarlyUiPreferences(')]
        self.assertIn('SessionLanguageOr(preferences.language)', pieces[-1])
        program = r'''
#include <cassert>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include "aeraui/backend.hpp"
#define AERA_DEFAULT_LANGUAGE "en"
#define TW_TIME_ZONE_VAR "timezone"
#define AERA_PRESERVE_RECOVERY_VAR "preserve_recovery"
#define AERA_PRESERVE_ABL_VAR "preserve_abl"
#define TW_SKIP_DIGEST_GENERATE_VAR "skip_digest_generate"
#define TW_SKIP_DIGEST_CHECK_VAR "skip_digest_check"
struct DataManager {
  static inline std::map<std::string, std::string> values;
  static inline bool fail = false;
  static std::string GetStrValue(const char *key) { return values[key]; }
  static int SetValue(const char *key, const std::string &value, int = 0) {
    if (fail) return -1;
    values[key] = value; return 0;
  }
  static int SetValue(const char *key, int value, int persist = 0) {
    return SetValue(key, std::to_string(value), persist);
  }
};
namespace aeraui {
std::string gSessionLanguage;
std::mutex gSessionLanguageMutex;
bool RecoverySetScreenTimeout(int) { return true; }
''' + '\n'.join(pieces) + r'''
EarlyUiPreferences disk;
void LoadAeraPreferencesIfAvailable() { ApplyEarlyUiPreferences(disk); }
''' + definition(source, 'std::string RecoveryLanguage(') + '\n' + definition(source, 'bool RecoverySetLanguage(') + r'''
}
using namespace aeraui;
int main() {
  disk.language = "fr_FR";
  assert(RecoveryLanguage() == "fr_FR"); // No explicit choice: restore as before.
  for (const auto &choice : {"de_DE", "zh_CN", "zh_TW", "ar_SA", "en"}) {
    assert(RecoverySetLanguage(choice));
    assert(RecoveryLanguage() == choice); // This call performs a disk reload.
    assert(DataManager::GetStrValue("tw_language") == choice);
    disk.language = "fr_FR";
    ApplyEarlyUiPreferences(disk); // The same path serves early and full settings.
    assert(DataManager::GetStrValue("tw_language") == choice);
    assert(RecoveryLanguage() == choice);
  }
  for (const auto &invalid : {"", "../../language", "en\n", "abcdefghijklmnopq"})
    assert(!RecoverySetLanguage(invalid));
  assert(RecoveryLanguage() == "en");
  DataManager::fail = true;
  assert(!RecoverySetLanguage("de_DE"));
  assert(gSessionLanguage == "en");
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'language.cpp').write_text(program)
            result = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'aeraui/include'), str(folder / 'language.cpp'),
                '-o', str(folder / 'language')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(folder / 'language')], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
