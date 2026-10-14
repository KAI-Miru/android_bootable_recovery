// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace aera_format {
struct Capabilities {
  bool present = false, writable = false, logical = false, removable = false;
  bool rm_rf_only = false, subpartition = false, storage = false;
  std::string path, forced_data;
  std::vector<std::string> kernel, formatters;
};
inline std::vector<std::string> Filesystems(const Capabilities& c) {
  if (!c.present || !c.writable || c.logical || c.rm_rf_only || c.subpartition ||
      !(c.path == "/data" || c.path == "/cache" || c.path == "/sd-ext" ||
        (c.removable && c.storage)) || c.path == "/metadata" || c.path == "/persist") return {};
  std::vector<std::string> result;
  for (const char* fs : {"ext4", "f2fs", "ext3", "ext2", "vfat", "exfat", "ntfs"}) {
    if (c.path == "/data" && std::string(fs) != "ext4" && std::string(fs) != "f2fs") continue;
    if (c.path == "/data" && !c.forced_data.empty() && c.forced_data != fs) continue;
    if (std::find(c.kernel.begin(), c.kernel.end(), fs) != c.kernel.end() &&
        std::find(c.formatters.begin(), c.formatters.end(), fs) != c.formatters.end())
      result.emplace_back(fs);
  }
  return result;
}
}  // namespace aera_format
