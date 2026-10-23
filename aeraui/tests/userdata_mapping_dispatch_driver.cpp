// SPDX-License-Identifier: Apache-2.0
// Host integration scaffold: real partition guards/dispatch, fake formatters.
// Only a regular file in a private temporary directory represents a mapping.
#include "../../aera_userdata_mapping.hpp"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <vector>
#include <unistd.h>
using namespace std;
#define PLATFORM_SDK_VERSION 35
#define TW_IS_ENCRYPTED "encrypted"
#define TW_IS_DECRYPTED "decrypted"
#define TW_RM_RF_VAR "rmrf"
#define AERA_DISABLE_FORCED_ENCRYPTION "force"
#define DATA_LOGS_DIR "/data/logs"
template<class... Args> void HostLog(const char*, Args...) {}
#define LOGINFO(...) HostLog(__VA_ARGS__)
#define LOGERR(...) HostLog(__VA_ARGS__)
static string node, mapping_path, expected_filesystem;
static unsigned commands, waits, disappear_after, key_wipes, deleted_hooks;
static int command_result;
static bool command_ran, pre_hook_ok = true, expect_raw_format;
static int Log_Offset;
namespace msg { enum { kError, kWarning }; }
struct Msg {
  Msg(int, const char*) {} explicit Msg(const char*) {}
  Msg operator()(const string&) { return *this; }
};
template<class T> void gui_msg(T) {}
void gui_err(const char*) {}
int tw_get_default_metadata(const char*) { return 0; }
int tw_set_default_metadata(const char*) { return 0; }
namespace android { namespace base {
bool GetBoolProperty(const char*, bool value) { return value; }
}}
struct BasePartition {
  bool PreWipeEncryption() { return pre_hook_ok; }
  bool PostWipeEncryption() { return true; }
  ~BasePartition() { ++deleted_hooks; }
};
static int HostSleep(unsigned usec) {
  assert(usec == 50000 && ++waits <= 40);
  if (command_ran && waits >= disappear_after) assert(unlink(node.c_str()) == 0);
  return 0;
}
struct TWFunc {
  static bool Block_Operations_Until_Reboot() { return false; }
  static void PostWipeEncryption() {}
  static string get_log_dir() { return "/cache"; }
  static bool Path_Exists(const string& path) {
    return path == "/system/bin/make_f2fs" || path == "/system/bin/mke2fs" ||
        path == "/system/bin/e2fsdroid" || access(path.c_str(), F_OK) == 0;
  }
  static int Exec_Cmd(const string& command, bool) {
    const char* expected = aera::userdata_mapping::kCommand ? aera::userdata_mapping::kCommand : "disabled";
    assert(aera::userdata_mapping::kCommand && command == expected);
    ++commands; command_ran = true;
    if (command_result == 0 && disappear_after == 0) assert(unlink(node.c_str()) == 0);
    return command_result;
  }
};
struct DataManager {
  static void SetValue(const char*, int) {}
  static int GetIntValue(const char*) { return 0; }
  static void GetValue(const char*, int& value) { value = 0; }
  static void Output_Version() {}
};
struct Manager {
  bool Wipe_By_Path(const string&) { ++key_wipes; return true; }
  void Add_MTP_Storage(int) {}
  string Get_Android_Root_Path() { return "/system_root"; }
  bool Recreate_Logs_Dir() { return true; }
} PartitionManager;
struct TWPartition {
  string Mount_Point = "/data", Display_Name = "Data", Fstab_File_System = "f2fs";
  string Current_File_System = "f2fs", Decrypted_Block_Device, Actual_Block_Device;
  string Key_Directory = "/metadata/key", Symlink_Mount_Point = "/sdcard";
  string Crypto_Key_Location = "footer";
  bool Has_Data_Media = true, Is_Present = true, Is_Decrypted = true, Is_Encrypted = true;
  bool Is_FBE = true, unmount_ok = true, Can_Be_Wiped = true, Use_Rm_Rf = false;
  bool Is_Storage = false, Needs_Fs_Compress = false;
  int MTP_Storage_ID = 0, formats = 0, media_wipes = 0;
  BasePartition* make_partition() { return new BasePartition; }
  void Find_Actual_Block_Device() {
    Actual_Block_Device = Is_Decrypted && !Decrypted_Block_Device.empty() ? Decrypted_Block_Device : "/dev/fake-raw";
  }
  bool UnMount(bool) { return unmount_ok; }
  bool Mount(bool) { return true; }
  void Setup_File_System(bool) {}
  bool Wipe_Data_Without_Wiping_Media() { ++media_wipes; return true; }
  bool RecordFormat(const string& filesystem) {
    ++formats;
    if (!expected_filesystem.empty()) assert(filesystem == expected_filesystem);
    if (expect_raw_format)
      assert(!Has_Data_Media && !Is_Encrypted && !Is_Decrypted && Decrypted_Block_Device.empty());
    const bool mapped = Is_Decrypted && !Decrypted_Block_Device.empty();
    assert(Actual_Block_Device == (mapped ? Decrypted_Block_Device : "/dev/fake-raw"));
    if (aera::userdata_mapping::kCommand && Mount_Point == "/data" && !mapped)
      assert(aera::userdata_mapping::Probe(mapping_path.c_str()) == aera::userdata_mapping::State::kAbsent);
    return true;
  }
  bool Wipe_RMRF() { return false; }
  bool Wipe_EXFAT() { return false; }
  bool Wipe_NTFS() { return false; }
  bool Wipe_FAT() { return false; }
  bool Wipe_MTD() { return false; }
  bool BlkDiscard() { return false; }
  bool Wipe(string);
  vector<string> Format_File_Systems() { return {"ext2", "ext3", "ext4", "f2fs"}; }
  bool Format_File_System(const string&);
  bool Wipe_Encryption(const string& filesystem = "");
  bool Wipe_EXTFS(string);
  bool Wipe_EXT4();
  bool Wipe_F2FS();
};

#define usleep HostSleep
// PARTITION_FUNCTIONS
#undef usleep

static void Reset(bool present = true) {
  unlink(node.c_str()); mapping_path = node;
  if (present) {
    int fd = open(node.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && close(fd) == 0);
  }
  commands = waits = disappear_after = key_wipes = deleted_hooks = 0;
  command_result = 0; command_ran = false; pre_hook_ok = true; expect_raw_format = false;
  expected_filesystem.clear();
}

int main(int argc, char** argv) {
  assert(argc == 2);
  char directory[] = "/tmp/aera-mapping-dispatch-XXXXXX";
  assert(mkdtemp(directory)); node = string(directory) + "/userdata";
  const string scenario = argv[1];
  const bool enabled = aera::userdata_mapping::kCommand != nullptr;
  if (scenario == "encryption") {
    for (bool explicit_choice : {false, true})
    for (const char* filesystem : {"ext2", "ext3", "ext4", "f2fs"}) for (int failure = 0; failure < 8; ++failure) {
      Reset(failure != 6);
      expect_raw_format = true;
      expected_filesystem = filesystem;
      TWPartition part; part.Decrypted_Block_Device = node;
      part.Fstab_File_System = explicit_choice ? "f2fs" : filesystem;
      if (failure == 1) disappear_after = 3;
      if (failure == 2) command_result = -1;
      if (failure == 3) disappear_after = 41;
      if (failure == 4) mapping_path = node + "/invalid-child";
      if (failure == 5) part.unmount_ok = false;
      if (failure == 7) pre_hook_ok = false;
      const bool expected = failure != 5 && failure != 7 && (!enabled || failure < 2 || failure == 6);
      assert(part.Wipe_Encryption(explicit_choice ? filesystem : "") == expected);
      assert(part.formats == int(expected) && key_wipes == unsigned(expected));
      assert(deleted_hooks == 1);
      if (!expected) {
        assert(part.Is_Encrypted && part.Is_Decrypted && part.Has_Data_Media);
        assert(part.Decrypted_Block_Device == node);
      } else assert(!part.Is_Encrypted && !part.Is_Decrypted && part.Decrypted_Block_Device.empty());
    }
  } else if (scenario == "direct") {
    for (const char* filesystem : {"ext2", "ext3", "ext4", "f2fs"}) for (int failure = 0; failure < 3; ++failure) {
      Reset(); TWPartition part; part.Has_Data_Media = false; part.Is_Decrypted = part.Is_Encrypted = false;
      if (failure == 1) command_result = -1;
      if (failure == 2) disappear_after = 41;
      const bool expected = !enabled || failure == 0;
      assert(part.Wipe(filesystem) == expected && part.formats == int(expected));
    }
  } else if (scenario == "preserve") {
    for (const char* filesystem : {"ext2", "ext3", "ext4", "f2fs"}) {
      Reset(); TWPartition part; part.Has_Data_Media = false; part.Decrypted_Block_Device = node;
      assert(part.Wipe(filesystem) && part.formats == 1 && commands == 0);
      assert(part.Is_Decrypted && part.Is_Encrypted && part.Decrypted_Block_Device == node);
      assert(access(node.c_str(), F_OK) == 0);
    }
  } else if (scenario == "media") {
    Reset(); TWPartition part; part.Decrypted_Block_Device = node;
    assert(part.Wipe("f2fs") && part.media_wipes == 1 && part.formats == 0 && commands == 0);
  } else if (scenario == "other") {
    Reset(); TWPartition part; part.Mount_Point = "/sd-ext"; part.Has_Data_Media = false;
    part.Is_Decrypted = part.Is_Encrypted = false;
    assert(part.Wipe("ext4") && part.formats == 1 && commands == 0);
    assert(access(node.c_str(), F_OK) == 0);
  } else if (scenario == "unmount") {
    for (const char* filesystem : {"ext2", "ext3", "ext4", "f2fs"}) {
      Reset(); TWPartition part; part.Has_Data_Media = false; part.unmount_ok = false;
      assert(!part.Wipe(filesystem) && part.formats == 0 && commands == 0);
    }
  } else if (scenario == "unknown") {
    Reset(); TWPartition part; part.Has_Data_Media = false;
    assert(!part.Wipe("unsupported-filesystem") && part.formats == 0 && commands == 0);
    part.Has_Data_Media = true;
    part.Decrypted_Block_Device = node;
    assert(!part.Wipe_Encryption("unsupported-filesystem") && part.formats == 0 && commands == 0);
    assert(part.Is_Decrypted && part.Is_Encrypted && part.Has_Data_Media);
    assert(part.Decrypted_Block_Device == node && key_wipes == 0 && deleted_hooks == 0);
  } else assert(false);
  unlink(node.c_str()); assert(rmdir(directory) == 0);
}
