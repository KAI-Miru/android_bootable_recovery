// SPDX-License-Identifier: GPL-3.0-or-later
#include "checkpoint.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mntent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

thread_local bool metadata_mount = false;

bool MetadataMounted() {
  FILE* mounts = setmntent("/proc/self/mounts", "r");
  if (!mounts) return false;
  bool found = false;
  struct mntent entry;
  char buffer[4096];
  while (getmntent_r(mounts, &entry, buffer, sizeof(buffer))) {
    if (!strcmp(entry.mnt_dir, "/metadata") &&
        (!strcmp(entry.mnt_type, "ext4") || !strcmp(entry.mnt_type, "f2fs"))) {
      found = true;
      break;
    }
  }
  endmntent(mounts);
  return found;
}

bool NoPendingCheckpoint() {
  // An absent file is meaningful only on the real mounted metadata partition.
  if (!MetadataMounted()) return false;
  const int fd = open("/metadata/vold/checkpoint", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return errno == ENOENT;
  struct stat info;
  bool inactive = false;
  if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == 1) {
    char value[2];
    ssize_t count;
    do {
      count = read(fd, value, sizeof(value));
    } while (count < 0 && errno == EINTR);
    // Match Vold's exact committed sentinel; whitespace and malformed records
    // must retain its original checkpoint decision rather than imply a commit.
    inactive = count == 1 && value[0] == '0';
  }
  if (close(fd) != 0) return false;
  return inactive;
}

}  // namespace

namespace aera {
MetadataCheckpointScope::MetadataCheckpointScope() : previous_(metadata_mount) {
  metadata_mount = true;
}
MetadataCheckpointScope::~MetadataCheckpointScope() {
  metadata_mount = previous_;
}
}  // namespace aera

// libvold is linked statically into recovery. Wrap only its external checkpoint
// query; do not modify Android's Vold, fstab, slot flags or transaction records.
extern "C" bool __real__ZN7android4vold18cp_needsCheckpointEv();

extern "C" bool __wrap__ZN7android4vold18cp_needsCheckpointEv() {
  if (!metadata_mount || !NoPendingCheckpoint())
    return __real__ZN7android4vold18cp_needsCheckpointEv();
  // A recovery boot must not start an Android OTA transaction just because the
  // current slot is unmarked. There is no framework boot to commit it later.
  fprintf(stderr, "AERA: no pending Android checkpoint; keeping recovery writes durable.\n");
  return false;
}
