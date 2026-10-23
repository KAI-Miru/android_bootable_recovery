// SPDX-License-Identifier: GPL-3.0-or-later
// Filesystem boundaries for the real wrapper; no real metadata or mounts.
#include "aera_checkpoint/checkpoint.hpp"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mntent.h>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

extern bool MetadataMountNeedsCheckpoint();
extern bool upstream_checkpoint, upstream_state;
extern int upstream_calls;

namespace {
std::string scenario;
int opens = 0, reads = 0, closes = 0;
constexpr int kCheckpointFd = 4242;

std::string Record() {
  if (scenario == "pending") return "2";
  if (scenario == "pending-slot") return "-1 _a";
  if (scenario == "malformed") return "garbage";
  if (scenario == "empty") return "";
  if (scenario == "whitespace") return "0\n";
  return "0";
}
}

extern "C" FILE* __wrap_setmntent(const char* path, const char* mode) {
  assert(!strcmp(path, "/proc/self/mounts") && !strcmp(mode, "r"));
  if (scenario == "mount-table-error") { errno = EACCES; return nullptr; }
  FILE* table = tmpfile();
  assert(table);
  const char* entry = "/dev/block/dm-0 /metadata ext4 rw 0 0\n";
  if (scenario == "metadata-f2fs") entry = "/dev/block/dm-0 /metadata f2fs rw 0 0\n";
  if (scenario == "metadata-parent") entry = "/dev/block/dm-0 / ext4 rw 0 0\n";
  if (scenario == "metadata-child") entry = "/dev/block/dm-0 /metadata/other ext4 rw 0 0\n";
  if (scenario == "metadata-tmpfs") entry = "tmpfs /metadata tmpfs rw 0 0\n";
  fputs(entry, table);
  rewind(table);
  return table;
}

extern "C" int __wrap_open(const char* path, int flags, ...) {
  assert(!strcmp(path, "/metadata/vold/checkpoint"));
  assert((flags & O_ACCMODE) == O_RDONLY);
  assert(!(flags & (O_CREAT | O_TRUNC | O_APPEND)));
  assert((flags & (O_CLOEXEC | O_NOFOLLOW)) == (O_CLOEXEC | O_NOFOLLOW));
  ++opens;
  if (scenario == "absent") { errno = ENOENT; return -1; }
  if (scenario == "unreadable") { errno = EACCES; return -1; }
  if (scenario == "symlink") { errno = ELOOP; return -1; }
  if (scenario == "open-error") { errno = EIO; return -1; }
  return kCheckpointFd;
}

extern "C" int __wrap_fstat(int fd, struct stat* info) {
  assert(fd == kCheckpointFd);
  if (scenario == "stat-error") { errno = EIO; return -1; }
  *info = {};
  info->st_mode = scenario == "directory" ? S_IFDIR | 0700 : S_IFREG | 0600;
  info->st_size = static_cast<off_t>(Record().size());
  return 0;
}

extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t size) {
  assert(fd == kCheckpointFd && size == 2);
  ++reads;
  if (scenario == "read-error") { errno = EIO; return -1; }
  if (scenario == "interrupted" && reads == 1) { errno = EINTR; return -1; }
  if (scenario == "short-read") return 0;
  const std::string record = scenario == "changed-record" ? "02" : Record();
  assert(record.size() <= size);
  memcpy(buffer, record.data(), record.size());
  return static_cast<ssize_t>(record.size());
}

extern "C" int __wrap_close(int fd) {
  assert(fd == kCheckpointFd);
  ++closes;
  if (scenario == "close-error") { errno = EIO; return -1; }
  return 0;
}

int main(int argc, char** argv) {
  assert(argc == 4);
  scenario = argv[1];
  upstream_checkpoint = !strcmp(argv[2], "1");
  if (scenario == "other-thread") {
    const aera::MetadataCheckpointScope scope;
    std::thread worker([] {
      assert(MetadataMountNeedsCheckpoint() == upstream_checkpoint);
    });
    worker.join();
    assert(upstream_calls == 1 && opens == 0);
    return 0;
  }
  const bool scoped = strcmp(argv[3], "0") != 0;
  bool result;
  if (scoped) {
    const aera::MetadataCheckpointScope outer;
    {
      const aera::MetadataCheckpointScope inner;
    }
    result = MetadataMountNeedsCheckpoint();
  } else {
    result = MetadataMountNeedsCheckpoint();
  }
  const bool inactive = scoped && (scenario == "absent" || scenario == "committed" ||
                        scenario == "metadata-f2fs" || scenario == "interrupted");
  assert(result == (!inactive && upstream_checkpoint));
  assert(upstream_calls == (inactive ? 0 : 1));
  assert(upstream_state == (!inactive && upstream_checkpoint));
  assert(closes == (opens && scenario != "absent" && scenario != "unreadable" &&
                    scenario != "symlink" && scenario != "open-error" ? 1 : 0));
  if (scoped && scenario == "interrupted") assert(reads == 2);
  const int old_calls = upstream_calls, old_opens = opens;
  // Leaving the nested scope must restore the unmodified crypto query.
  assert(MetadataMountNeedsCheckpoint() == upstream_checkpoint);
  assert(upstream_calls == old_calls + 1 && opens == old_opens);
  printf("result=%d upstream=%d state=%d opens=%d reads=%d closes=%d\n",
         result, upstream_calls, upstream_state, opens, reads, closes);
}
