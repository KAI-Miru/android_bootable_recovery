"""Run the actual Qualcomm MAC-preparation method against temporary RAM fixtures."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>
namespace fs = std::filesystem;
static fs::path root;
static std::string serial = "HOST_TEST_SERIAL", scenario;
static fs::path Resolve(const std::string& path) {
  assert(!path.empty() && path.front() == '/');
  return root / path.substr(1);
}
static bool FileExists(const std::string& path) { return fs::is_regular_file(Resolve(path)); }
static bool MkdirRecursive(const std::string& path) {
  std::error_code ec;
  fs::create_directories(Resolve(path), ec);
  return !ec;
}
static bool ReadFile(const std::string& path, std::string& out) {
  std::ifstream file(Resolve(path));
  if (!file) return false;
  out.assign(std::istreambuf_iterator<char>(file), {});
  return true;
}
static bool WriteFile(const std::string& path, const std::string& value) {
  assert(path.find("/persist/") == std::string::npos);
  if (scenario == "mac-write-error" && path.find("wlan_mac.bin") != std::string::npos) return false;
  if (scenario == "config-write-error" && path.find("WCNSS_qcom_cfg.ini") != std::string::npos) return false;
  std::ofstream file(Resolve(path));
  file << value;
  return file.good();
}
static std::string Trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \r\n\t");
  return begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \r\n\t") - begin + 1);
}
static DIR* TestOpendir(const char* path) { return ::opendir(Resolve(path).c_str()); }
static int property_get(const char*, char* out, const char*) {
  strcpy(out, serial.c_str());
  return serial.size();
}
#define PROPERTY_VALUE_MAX 92
#define LOGERR(...) fprintf(stderr, __VA_ARGS__)
#define LOGINFO(...) fprintf(stderr, __VA_ARGS__)
#define opendir TestOpendir
struct Wlan { static bool PrepareStableMacFirmware(); };
'''
MAIN = r'''
int main(int argc, char** argv) {
  assert(argc == 3);
  scenario = argv[1]; root = argv[2];
  MkdirRecursive("/vendor/etc/wifi");
  const std::string config = "/vendor/etc/wifi/WCNSS_qcom_cfg.ini";
  if (scenario != "missing-config") {
    std::ofstream file(Resolve(config));
    file << (scenario == "disabled-config" ? "read_mac_addr_from_mac_file=0\nEND\n"
                                          : "read_mac_addr_from_mac_file=1\nEND\n");
  }
  if (scenario == "empty-serial") serial.clear();
  const bool expected = scenario == "success" || scenario == "disabled-config";
  assert(Wlan::PrepareStableMacFirmware() == expected);
  if (!expected) return 0;
  std::string content, first, repeated, changed;
  assert(ReadFile(config, content));
  assert(content == "read_mac_addr_from_mac_file=1\nEND\n");
  const std::string firmware = "/vendor/firmware/wlan/qca_cld/wlan_mac.bin";
  assert(ReadFile(firmware, first) && first.size() == 33);
  assert(first.rfind("Intf0MacAddress=02", 0) == 0);
  assert(Wlan::PrepareStableMacFirmware());
  assert(ReadFile(firmware, repeated) && repeated == first);
  serial = "OTHER_HOST_TEST_SERIAL";
  assert(Wlan::PrepareStableMacFirmware());
  assert(ReadFile(firmware, changed) && changed != first);
}
'''


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'),
                     'Requires Linux and a native C++ compiler')
class StableMacFirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.temp.name)
        source = (ROOT / 'wlan.cpp').read_text()
        body = source[source.index('bool Wlan::PrepareStableMacFirmware() {'):]
        body = body[:body.index('\nbool Wlan::WaitForAssociation')]
        driver = cls.path / 'driver.cpp'
        driver.write_text(HARNESS + body + MAIN)
        cls.binary = cls.path / 'driver'
        subprocess.run(['g++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        str(driver), '-o', str(cls.binary)], check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, *cases):
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as root:
                subprocess.run([str(self.binary), case, root], check=True,
                               capture_output=True, text=True, timeout=5)

    def test_real_helper_accepts_ramdisk_config_and_is_deterministic(self):
        self.check('success', 'disabled-config')

    def test_absent_config_or_identity_cannot_report_preparation_success(self):
        self.check('missing-config', 'empty-serial')

    def test_failed_config_or_firmware_write_cannot_report_success(self):
        self.check('config-write-error', 'mac-write-error')


if __name__ == '__main__':
    unittest.main()
