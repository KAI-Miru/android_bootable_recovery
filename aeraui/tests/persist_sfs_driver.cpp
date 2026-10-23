// SPDX-License-Identifier: GPL-3.0-or-later
// Link the production helper; intercept only its OS/vendor boundaries.
#include "../../aera_persist_sfs.hpp"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mntent.h>
#include <string>
#include <sys/mount.h>
#include <sys/statvfs.h>

static std::string scenario;
static std::string table;
static int queries, remounts, closes;

static int Status(void* request, uint32_t request_size, void* response,
                  uint32_t response_size, uint32_t command) {
  ++queries;
  assert(!request && request_size == 0);
  assert(response && response_size == sizeof(int32_t));
  assert(command == 0x1b);  // No provisioning command can pass this boundary.
  if (scenario != "untouched-response") {
    *static_cast<int32_t*>(response) = scenario == "provisioned" ? 0
        : scenario == "mac-mismatch" ? -244
        : scenario == "unknown-status" ? -21 : -249;
  }
  return scenario == "transport-error" ? -1 : 0;
}

extern "C" FILE* __wrap_setmntent(const char* path, const char* mode) {
  assert(strcmp(path, "/proc/self/mounts") == 0 && strcmp(mode, "r") == 0);
  if (scenario == "mount-table-error") return nullptr;
  return fmemopen(table.data(), table.size(), "r");
}

extern "C" void* __wrap_dlopen(const char* name, int) {
  assert(strcmp(name, "libQSEEComAPI.so") == 0);
  return scenario == "missing-library" ? nullptr : reinterpret_cast<void*>(1);
}

extern "C" void* __wrap_dlsym(void* library, const char* name) {
  assert(library == reinterpret_cast<void*>(1));
  assert(strcmp(name, "QSEECom_send_service_cmd") == 0);
  return scenario == "missing-symbol" ? nullptr : reinterpret_cast<void*>(&Status);
}

extern "C" int __wrap_dlclose(void* library) {
  assert(library == reinterpret_cast<void*>(1));
  ++closes;
  return 0;
}

extern "C" int __wrap_mount(const char* device, const char* target,
                             const char* filesystem, unsigned long flags,
                             const void* options) {
  ++remounts;
  assert(queries == 1);
  assert(strcmp(device, "/dev/block/persist") == 0);
  assert(strcmp(target, "/mnt/vendor/persist") == 0);
  assert(strcmp(filesystem, "ext4") == 0 && !options);
  assert(flags == (MS_REMOUNT | MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_NOATIME));
  if (scenario == "remount-error") { errno = EROFS; return -1; }
  return 0;
}

extern "C" int __wrap_statvfs(const char* path, struct statvfs* fs) {
  assert(strcmp(path, "/mnt/vendor/persist") == 0);
  assert(remounts == 1);
  if (scenario == "verification-error") { errno = EACCES; return -1; }
  memset(fs, 0, sizeof(*fs));
  fs->f_flag = scenario == "still-read-only" ? ST_RDONLY : 0;
  return 0;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  scenario = argv[1];
  table = "/dev/block/persist /mnt/vendor/persist ext4 ro,nosuid,nodev,noexec,noatime,seclabel 0 0\n";
  if (scenario == "absent-mount") table = "";
  if (scenario == "parent-only") table = "tmpfs /mnt/vendor tmpfs ro 0 0\n";
  if (scenario == "wrong-filesystem") table = "tmpfs /mnt/vendor/persist tmpfs ro 0 0\n";
  if (scenario == "already-writable") table = "/dev/block/persist /mnt/vendor/persist ext4 rw 0 0\n";
  aera::PreparePersistForSfs();
  const bool no_mount = scenario == "absent-mount" || scenario == "parent-only" ||
      scenario == "wrong-filesystem" || scenario == "already-writable" ||
      scenario == "mount-table-error";
  const bool no_query = no_mount || scenario == "missing-library" || scenario == "missing-symbol";
  const bool should_remount = scenario == "not-provisioned" || scenario == "remount-error" ||
      scenario == "verification-error" || scenario == "still-read-only";
  assert(queries == (no_query ? 0 : 1));
  assert(remounts == (should_remount ? 1 : 0));
  assert(closes == (no_mount || scenario == "missing-library" ? 0 : 1));
}
