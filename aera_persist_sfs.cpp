// SPDX-License-Identifier: GPL-3.0-or-later
#include "aera_persist_sfs.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mntent.h>
#include <string>
#include <sys/mount.h>
#include <sys/statvfs.h>

namespace aera {

void PreparePersistForSfs() {
  constexpr const char* path = "/mnt/vendor/persist";
  FILE* mounts = setmntent("/proc/self/mounts", "r");
  if (!mounts) return;
  std::string device;
  unsigned long flags = MS_REMOUNT;
  struct mntent entry;
  char buffer[4096];
  while (getmntent_r(mounts, &entry, buffer, sizeof(buffer))) {
    if (strcmp(entry.mnt_dir, path) || strcmp(entry.mnt_type, "ext4")) continue;
    if (!hasmntopt(&entry, "ro")) break;
    device = entry.mnt_fsname;
    // Remount must retain the existing per-mount security and atime flags.
    const struct { const char* option; unsigned long flag; } options[] = {
        {"nosuid", MS_NOSUID}, {"nodev", MS_NODEV}, {"noexec", MS_NOEXEC},
        {"sync", MS_SYNCHRONOUS}, {"dirsync", MS_DIRSYNC},
        {"mand", MS_MANDLOCK}, {"noatime", MS_NOATIME},
        {"nodiratime", MS_NODIRATIME}, {"relatime", MS_RELATIME},
        {"strictatime", MS_STRICTATIME},
    };
    for (const auto& option : options) {
      if (hasmntopt(&entry, option.option)) flags |= option.flag;
    }
    break;
  }
  endmntent(mounts);
  // Only remount an existing ext4 persist mount; never mount a guessed device.
  if (device.empty()) return;

  void* library = dlopen("libQSEEComAPI.so", RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    fprintf(stderr, "AERA: RPMB status unavailable; persist unchanged.\n");
    return;
  }
  using SendServiceCommand = int (*)(void*, uint32_t, void*, uint32_t, uint32_t);
  auto command = reinterpret_cast<SendServiceCommand>(
      dlsym(library, "QSEECom_send_service_cmd"));
  // QSEOS_RPMB_CHECK_PROV_STATUS_COMMAND is status-only, never key provisioning.
  constexpr uint32_t kCheckProvisionStatus = 0x1b;
  constexpr int32_t kKeyNotProvisioned = -249;
  int32_t status = INT32_MIN;
  const int result = command
      ? command(nullptr, 0, &status, sizeof(status), kCheckProvisionStatus) : -1;
  dlclose(library);
  if (result != 0 || status != kKeyNotProvisioned) {
    fprintf(stderr, "AERA: RPMB query result=%d status=%d; persist unchanged.\n",
            result, status);
    return;
  }

  // Keep filesystem-specific options unchanged. Do not unmount a filesystem
  // already used by QSEE listeners, calibration consumers or RFS RAM binds.
  if (mount(device.c_str(), path, "ext4", flags, nullptr) != 0) {
    fprintf(stderr, "AERA: persist SFS remount failed: %s\n", strerror(errno));
    return;
  }
  struct statvfs fs;
  if (statvfs(path, &fs) != 0 || (fs.f_flag & ST_RDONLY)) {
    fprintf(stderr, "AERA: persist SFS remount is not confirmed writable.\n");
    return;
  }
  fprintf(stderr, "AERA: RPMB_KEY_NOT_PROVISIONED; persist mounted rw for SFS.\n");
}

}  // namespace aera
