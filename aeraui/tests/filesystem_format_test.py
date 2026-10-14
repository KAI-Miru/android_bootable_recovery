"""Exercise native format dispatch with fake partitions; never format a device."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from screen_timer_test import definition

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'), 'Requires Linux C++')
class FilesystemFormatTests(unittest.TestCase):
    def test_capabilities_and_native_dispatch(self):
        partition = (ROOT / 'partition.cpp').read_text()
        manager = (ROOT / 'partitionmanager.cpp').read_text()
        program = '''
#include <cassert>
#include <algorithm>
#include <string>
#include <vector>
#include "aera_filesystem_format.hpp"
#include "aeraui/backend.hpp"
using std::string;
#define TW_RM_RF_VAR "rm_rf"
struct DataManager {
  static inline int rm = 1;
  static int GetIntValue(const char*) { return rm; }
  static void SetValue(const char*, int value) { rm = value; }
};
struct TWFunc {
  static inline bool blocked = false;
  static bool Block_Operations_Until_Reboot() { return blocked; }
};
struct TWPartition {
  string Mount_Point = "/data", Current_File_System = "ext4";
  bool Has_Data_Media = true, success = true;
  int calls = 0;
  std::vector<string> choices{"ext4", "f2fs"};
  std::vector<string> Format_File_Systems() { return choices; }
  bool Wipe(const string& fs) {
    ++calls; assert(!Has_Data_Media && DataManager::rm == 0);
    assert(Current_File_System == fs); return success;
  }
  bool Format_File_System(const string& fs);
};
struct TWPartitionManager {
  TWPartition part;
  int data_calls = 0;
  string data_fs;
  TWPartition* Find_Partition_By_Path(const string&) { return &part; }
  int Format_Data(const string& fs) { ++data_calls; data_fs = fs; return true; }
  int Format_By_Path(const string&, const string&);
};
''' + definition(partition, 'bool TWPartition::Format_File_System(') + '\n' + \
            definition(manager, 'int TWPartitionManager::Format_By_Path(') + '''
int main() {
  aera_format::Capabilities c;
  c.path="/data"; c.present=c.writable=true;
  c.kernel={"ext4","f2fs","vfat"}; c.formatters=c.kernel;
  assert((aera_format::Filesystems(c)==std::vector<string>{"ext4","f2fs"}));
  c.formatters={"ext4"}; assert(aera_format::Filesystems(c).size()==1);
  c.kernel={"f2fs"}; assert(aera_format::Filesystems(c).empty());
  c.formatters={"f2fs"}; assert(aera_format::Filesystems(c)==std::vector<string>{"f2fs"});
  c.forced_data="ext4"; assert(aera_format::Filesystems(c).empty()); c.forced_data="";
  for (bool* flag : {&c.logical, &c.rm_rf_only, &c.subpartition}) {
    *flag=true; assert(aera_format::Filesystems(c).empty()); *flag=false;
  }
  for (const char* path : {"/metadata","/persist","/vendor","/boot","/data/media","/data;rm"}) {
    c.path=path; assert(aera_format::Filesystems(c).empty());
  }
  c.path="/usb_otg"; c.removable=c.storage=true;
  assert(aera_format::Filesystems(c)==std::vector<string>{"f2fs"});
  c.present=false; assert(aera_format::Filesystems(c).empty());
  TWPartitionManager manager;
  assert(!manager.Format_By_Path("/data/media","f2fs"));
  assert(!manager.Format_By_Path("/data","ext4;bad"));
  assert(manager.data_calls==0 && manager.part.calls==0);
  TWFunc::blocked=true; assert(!manager.Format_By_Path("/data","f2fs")); TWFunc::blocked=false;
  assert(manager.Format_By_Path("/data","f2fs"));
  assert(manager.data_calls==1 && manager.data_fs=="f2fs" && manager.part.calls==0);
  manager.part.Mount_Point="/cache";
  assert(manager.Format_By_Path("/cache","ext4"));
  assert(manager.part.calls==1 && manager.part.Has_Data_Media && DataManager::rm==1);
  manager.part.success=false;
  assert(!manager.Format_By_Path("/cache","f2fs"));
  assert(manager.part.Current_File_System=="ext4" && manager.part.Has_Data_Media && DataManager::rm==1);
  aeraui::JobRequest request; request.job=aeraui::Job::kChangeFilesystem;
  request.path="/data"; request.filesystem="f2fs";
  for (const char* text : {"", "YES", "yesplease", "yes "}) {
    request.confirmation=text; assert(!aeraui::FilesystemFormatAuthorized(request));
  }
  request.confirmation="yes"; assert(aeraui::FilesystemFormatAuthorized(request));
  request.partitions={"/data"}; assert(!aeraui::FilesystemFormatAuthorized(request));
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'test.cpp'
            source.write_text(program)
            binary = Path(folder) / 'test'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT), '-I' + str(ROOT / 'aeraui/include'),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_data_keeps_native_encryption_cleanup(self):
        manager = (ROOT / 'partitionmanager.cpp').read_text()
        partition = (ROOT / 'partition.cpp').read_text()
        formatting = definition(manager, 'int TWPartitionManager::Format_Data(')
        self.assertLess(formatting.index('Format_File_Systems()'), formatting.index('metadata->UnMount'))
        self.assertIn('dat->Wipe_Encryption(filesystem)', formatting)
        encryption = definition(partition, 'bool TWPartition::Wipe_Encryption(')
        self.assertIn('Format_File_System(the_wipe_fs)', encryption)
        self.assertIn('PartitionManager.Wipe_By_Path(Key_Directory)', encryption)
        self.assertIn('base_partition->PostWipeEncryption()', encryption)
        backend = (ROOT / 'aeraui/platform/aera_backend.cpp').read_text()
        self.assertIn('PartitionManager.Format_By_Path(request.path, request.filesystem)', backend)


if __name__ == '__main__':
    unittest.main()
