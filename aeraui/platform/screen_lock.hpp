// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <utility>

namespace aeraui {

// UI access control only. This does not encrypt storage or restrict root ADB.
class ScreenLockCredential {
 public:
  explicit ScreenLockCredential(std::string path) : path_(std::move(path)) {}
  bool Enabled();
  bool Change(const std::string& current, const std::string& replacement);
  bool Disable(const std::string& current);
  bool Verify(const std::string& password);
  void Lock();
  bool ConsumeUnlock();
  int RetrySeconds();

 private:
  bool Load();
  bool VerifyLocked(const std::string& password);
  bool Write(const std::array<unsigned char, 16>& salt,
             const std::array<unsigned char, 32>& digest);
  std::mutex mutex_;
  std::string path_;
  bool enabled_ = false, valid_ = false, authorized_ = false;
  std::array<unsigned char, 16> salt_{};
  std::array<unsigned char, 32> digest_{};
  unsigned failures_ = 0;
  std::chrono::steady_clock::time_point retry_after_{};
};

}  // namespace aeraui
