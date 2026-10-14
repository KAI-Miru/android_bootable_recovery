// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
namespace aeraui {
struct ScreenVerification { bool success = false; int retry_seconds = 0; };
// Session UI authorization only. Android owns credential verification.
// No password, independent verifier or disk key is stored here.
class AndroidScreenLock {
 public:
  using Verifier = std::function<ScreenVerification(const std::string&)>;
  bool Prepare(int credential_type, Verifier verifier);
  bool Available();
  int CredentialType();
  bool SetEnabled(bool enabled);
  bool Enabled();
  bool Verify(const std::string& credential);
  void Lock();
  bool ConsumeUnlock();
  int RetrySeconds();
 private:
  std::mutex mutex_;
  Verifier verifier_;
  int type_ = 0;
  bool enabled_ = false, authorized_ = false, verifying_ = false;
  unsigned generation_ = 0, failures_ = 0;
  std::chrono::steady_clock::time_point retry_after_{};
};
}  // namespace aeraui
