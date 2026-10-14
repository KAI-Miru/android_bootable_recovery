// SPDX-License-Identifier: Apache-2.0
#include "aeraui/platform/screen_lock.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

int main(int argc, char** argv) {
  assert(argc == 3);
  const std::string scenario = argv[1], path = argv[2];
  const std::string password = "host-test-password", changed = "changed-host-password";
  aeraui::ScreenLockCredential lock(path);
  assert(!lock.Enabled() && lock.ConsumeUnlock());
  assert(!lock.Change("", "short"));
  assert(lock.Change("", password));
  assert(lock.Enabled() && !lock.ConsumeUnlock());
  struct stat st{}; assert(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
  std::ifstream input(path); std::string record((std::istreambuf_iterator<char>(input)), {});
  assert(record.find(password) == std::string::npos);
  assert(!std::filesystem::exists(path + ".new"));
  if (scenario == "persistence") {
    aeraui::ScreenLockCredential reload(path);
    assert(reload.Enabled() && !reload.ConsumeUnlock());
    assert(reload.Verify(password) && reload.ConsumeUnlock() && !reload.ConsumeUnlock());
    assert(reload.Verify(password)); reload.Lock(); assert(!reload.ConsumeUnlock());
    assert(reload.Change(password, changed));
    std::ifstream after(path); std::string updated((std::istreambuf_iterator<char>(after)), {});
    assert(record != updated && updated.find(changed) == std::string::npos);
    assert(reload.Verify(changed));
    assert(reload.Disable(changed));
    assert(!reload.Enabled() && !std::filesystem::exists(path));
  } else if (scenario == "throttle") {
    assert(!lock.Verify("wrong") && lock.RetrySeconds() > 0);
    assert(!lock.Verify(password));
    assert(!lock.Change("wrong", changed) && !lock.Disable("wrong"));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(lock.Verify(password));
    assert(lock.RetrySeconds() == 0 && lock.ConsumeUnlock() && !lock.ConsumeUnlock());
  } else if (scenario == "corrupt") {
    std::ofstream(path) << "malformed credential\n";
    aeraui::ScreenLockCredential reload(path);
    assert(reload.Enabled() && !reload.ConsumeUnlock());
    assert(!reload.Verify(password) && !reload.Change("", changed) && !reload.Disable(password));
    // A transient unreadable/invalid record must be retried when storage
    // becomes available, while never treating the failure as an unlocked UI.
    std::ofstream(path) << record;
    assert(reload.Verify(password));
  } else if (scenario == "symlink") {
    const std::string other = path + ".other";
    std::filesystem::rename(path, other);
    assert(symlink(other.c_str(), path.c_str()) == 0);
    aeraui::ScreenLockCredential reload(path);
    assert(reload.Enabled() && !reload.Verify(password) && !reload.ConsumeUnlock());
  } else if (scenario == "storage_removed") {
    assert(unlink(path.c_str()) == 0);
    assert(lock.Enabled() && !lock.ConsumeUnlock());
    assert(lock.Verify(password) && lock.ConsumeUnlock());
    assert(lock.Disable(password));  // Authenticated removal remains possible.
  } else if (scenario == "reject_inputs") {
    assert(!lock.Change(password, std::string(257, 'x')));
    assert(!lock.Change(password, std::string("12345678\0tail", 13)));
    assert(lock.Verify(password));
  } else {
    assert(false);
  }
}
