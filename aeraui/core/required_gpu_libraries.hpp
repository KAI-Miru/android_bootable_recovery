// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unistd.h>

namespace aeraui {

// Device overlays can name driver inputs loaded outside DT_NEEDED. An empty
// list preserves the normal renderer path on devices without extra inputs.
inline bool RequiredGpuLibrariesAvailable(const std::string& libraries,
                                         std::string* unavailable) {
  size_t begin = 0;
  while (begin < libraries.size()) {
    const size_t end = libraries.find(':', begin);
    const std::string path = libraries.substr(begin, end - begin);
    if (path.empty() || path.front() != '/' || access(path.c_str(), R_OK) != 0) {
      *unavailable = path;
      return false;
    }
    if (end == std::string::npos) break;
    begin = end + 1;
    if (begin == libraries.size()) {
      *unavailable = "";
      return false;
    }
  }
  return true;
}

}  // namespace aeraui
