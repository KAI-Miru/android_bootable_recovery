"""Run the real encryption-wipe dispatch with a fake mapper and formatter."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from screen_timer_test import definition

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'), 'Requires Linux C++')
class UserdataMappingTests(unittest.TestCase):
    def test_mapping_cleanup_precedes_format_and_preserves_state_on_failure(self):
        source = (ROOT / 'partition.cpp').read_text()
        program = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <string>
#include <vector>
using namespace std;
#define TW_INCLUDE_CRYPTO
#define TW_IS_ENCRYPTED "encrypted"
#define AERA_DISABLE_FORCED_ENCRYPTION "force"
#define LOGINFO(...) ((void)0)
#define LOGERR(...) ((void)0)
namespace msg { enum { kError, kWarning }; }
struct Msg { Msg(int, const char*) {} Msg operator()(const string&) { return *this; } };
template<class T> void gui_msg(T) {}
void gui_err(const char*) {}
namespace android { namespace dm {
struct DeviceMapper {
  struct DmBlockDevice { string value; const string& name() const { return value; } };
  vector<DmBlockDevice> devices{{"userdata"}, {"system_a"}};
  int queries=0, deletes=0, fail_query=0;
  bool busy=false, survives=false;
  static DeviceMapper& Instance() { static DeviceMapper dm; return dm; }
  bool GetAvailableDevices(vector<DmBlockDevice>* out) {
    if (++queries == fail_query) return false;
    *out=devices; return true;
  }
  bool DeleteDevice(const string& name, chrono::milliseconds timeout) {
    assert(name=="userdata" && timeout==chrono::seconds(2)); ++deletes;
    if (busy) return false;
    if (!survives) devices.erase(devices.begin());
    return true;
  }
};
}}
struct BasePartition {
  bool PreWipeEncryption() { return true; }
  bool PostWipeEncryption() { return true; }
};
struct TWFunc {
  static bool Block_Operations_Until_Reboot() { return false; }
  static void PostWipeEncryption() {}
};
struct DataManager {
  static void SetValue(const char*, int) {}
  static int GetIntValue(const char*) { return 0; }
};
struct Manager {
  int key_wipes=0;
  bool Wipe_By_Path(const string&) { ++key_wipes; return true; }
  void Add_MTP_Storage(int) {}
} PartitionManager;
struct TWPartition {
  string Mount_Point="/data", Display_Name="Data", Fstab_File_System="f2fs";
  string Decrypted_Block_Device="/dev/block/dm-0", Key_Directory="/metadata/key";
  string Symlink_Mount_Point="/sdcard";
  bool Has_Data_Media=true, Is_Present=true, Is_Decrypted=true, Is_Encrypted=true;
  bool Is_FBE=true, unmount_ok=true;
  int MTP_Storage_ID=0, formats=0;
  vector<string> Format_File_Systems() { return {"ext4","f2fs"}; }
  BasePartition* make_partition() { return new BasePartition; }
  void Find_Actual_Block_Device() {}
  bool UnMount(bool) { return unmount_ok; }
  bool Mount(bool) { return true; }
  bool Wipe(const string&) {
    ++formats;
    assert(!Is_Encrypted && !Is_Decrypted && Decrypted_Block_Device.empty());
    if (Mount_Point=="/data")
      for (const auto& d: android::dm::DeviceMapper::Instance().devices)
        assert(d.name()!="userdata");
    return true;
  }
  bool Format_File_System(const string& fs) { return Wipe(fs); }
  bool Wipe_Encryption(const string& filesystem);
};
''' + definition(source, 'static bool Release_Userdata_Mapping(') + '\n' + \
            definition(source, 'bool TWPartition::Wipe_Encryption(') + r'''
int main() {
  auto& dm=android::dm::DeviceMapper::Instance();
  for (const string fs: {"", "ext4", "f2fs"}) {
    for (int failure=0; failure<5; ++failure) {
      dm=android::dm::DeviceMapper(); PartitionManager.key_wipes=0;
      TWPartition part;
      if (failure==1) dm.busy=true;
      if (failure==2) dm.fail_query=1;
      if (failure==3) dm.survives=true;
      if (failure==4) dm.fail_query=2;
      assert(part.Wipe_Encryption(fs)==(failure==0));
      assert(part.formats==(failure==0));
      assert(PartitionManager.key_wipes==(failure==0));
      if (failure) {
        assert(part.Is_Encrypted && part.Is_Decrypted && part.Has_Data_Media);
        assert(part.Decrypted_Block_Device=="/dev/block/dm-0");
      }
      assert(dm.devices.back().name()=="system_a");
    }
    dm=android::dm::DeviceMapper(); dm.devices.erase(dm.devices.begin());
    TWPartition no_mapping;
    assert(no_mapping.Wipe_Encryption(fs) && dm.deletes==0);
  }
  dm=android::dm::DeviceMapper();
  TWPartition mounted; mounted.unmount_ok=false;
  assert(!mounted.Wipe_Encryption("f2fs") && dm.queries==0 && mounted.formats==0);
  TWPartition other; other.Mount_Point="/sd-ext";
  assert(other.Wipe_Encryption("ext4") && dm.deletes==0);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
            cpp.write_text(program)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
