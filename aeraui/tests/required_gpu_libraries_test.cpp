// SPDX-License-Identifier: Apache-2.0
#include "../core/required_gpu_libraries.hpp"
#include <cassert>
#include <cstdio>

int main() {
  std::string unavailable;
  char directory[] = "/tmp/aera-gpu-test-XXXXXX";
  assert(mkdtemp(directory));
  const std::string first = std::string(directory) + "/first.so";
  const std::string second = std::string(directory) + "/second.so";
  for (const auto& file : {first, second}) {
    FILE* stream = fopen(file.c_str(), "w");
    assert(stream);
    fclose(stream);
  }
  assert(aeraui::RequiredGpuLibrariesAvailable("", &unavailable));
  assert(aeraui::RequiredGpuLibrariesAvailable(first + ":" + second, &unavailable));
  assert(unlink(second.c_str()) == 0);
  assert(!aeraui::RequiredGpuLibrariesAvailable(first + ":" + second, &unavailable));
  assert(unavailable == second);
  assert(!aeraui::RequiredGpuLibrariesAvailable("relative.so", &unavailable));
  assert(!aeraui::RequiredGpuLibrariesAvailable(first + ":", &unavailable));
  assert(!aeraui::RequiredGpuLibrariesAvailable(":" + first, &unavailable));
  assert(unlink(first.c_str()) == 0);
  assert(rmdir(directory) == 0);
}
