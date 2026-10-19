"""Run the native timer and runner input path with a deterministic clock."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def definition(source, name):
    begin = source.index(name)
    opening = source.index('{', begin)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'),
                     'Requires Linux and a C++ compiler')
class ScreenTimerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.temp.name)
        (cls.path / 'minuitwrp').mkdir()
        (cls.path / 'data.hpp').write_text('''#pragma once
#include <string>
struct DataManager {
  static int GetIntValue(const char* key) { return std::string(key)=="tw_brightness_max" ? 100 : 1; }
  static void GetValue(const char*, std::string& value) { value = "1234"; }
};
''', encoding='utf-8')
        (cls.path / 'twrp-functions.hpp').write_text('''#pragma once
#include <string>
#include <time.h>
struct TWFunc {
  static timespec timespec_diff(timespec before, timespec after) {
    return {after.tv_sec - before.tv_sec, after.tv_nsec - before.tv_nsec};
  }
  static void Set_Brightness(const std::string& value);
  static void check_and_run_script(const char* path, const char* argument);
};
''', encoding='utf-8')
        (cls.path / 'minuitwrp/minui.h').write_text(
            '#pragma once\nvoid gr_fb_blank(bool blank);\n', encoding='utf-8')
        runner = (ROOT / 'aeraui/core/runner.cpp').read_text(encoding='utf-8')
        cls.assertions = runner
        sections = [definition(runner, 'struct HardwareState') + ';',
                    definition(runner, 'void AdjustVolume('),
                    definition(runner, 'bool SynchronizeScreenState('),
                    definition(runner, 'bool HandleEvent('),
                    definition(runner, 'void CheckScreenTimeout(')]
        source = (ROOT / 'aeraui/tests/screen_timer_driver.cpp').read_text(encoding='utf-8')
        (cls.path / 'driver.cpp').write_text(source.replace('// RUNNER_FUNCTIONS', '\n'.join(sections)),
                                             encoding='utf-8')
        for mode, flags in (('normal', []), ('no_blank', ['-DTW_NO_SCREEN_BLANK']),
                            ('disabled', ['-DTW_NO_SCREEN_TIMEOUT'])):
            result = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter', '-pthread', '-I', str(cls.path),
                            '-I', str(ROOT), *flags, str(cls.path / 'driver.cpp'),
                            str(ROOT / 'aeraui/platform/aera_screen_timer.cpp'),
                            '-Wl,--wrap=clock_gettime', '-o', str(cls.path / mode)],
                           capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_mode(self, mode):
        subprocess.run([str(self.path / mode)], check=True, timeout=5)

    def test_inactivity_touch_keyboard_and_power_wake(self):
        self.run_mode('normal')

    def test_brightness_only_devices_run_both_panel_hooks(self):
        self.run_mode('no_blank')

    def test_device_timeout_opt_out_is_preserved(self):
        self.run_mode('disabled')

    def test_native_loop_reconciles_external_timer_changes_without_input(self):
        start = self.assertions.index('    for (;;) {\n        SynchronizeScreenState(engine, hardware);')
        self.assertLess(start, self.assertions.index('const int wait_ms = static_cast<int>(engine.RunFrame());', start))

    def test_native_loop_checks_inactivity_once_after_input(self):
        call = 'CheckScreenTimeout(engine, hardware);'
        self.assertEqual(self.assertions.count(call), 1)
        self.assertGreater(self.assertions.index(call),
                           self.assertions.index('HandleEvent(engine, performance, hardware, event)'))


if __name__ == '__main__':
    unittest.main()
