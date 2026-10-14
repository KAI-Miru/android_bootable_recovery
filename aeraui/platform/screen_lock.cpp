// SPDX-License-Identifier: Apache-2.0
#include "screen_lock.hpp"
#include <algorithm>
namespace aeraui {
bool AndroidScreenLock::Prepare(int type, Verifier verifier) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (verifier_) return type_ == type;
  if (type < 1 || type > 3 || !verifier) return false;
  type_ = type;
  verifier_ = std::move(verifier);
  return true;
}
bool AndroidScreenLock::Available() {
  std::lock_guard<std::mutex> guard(mutex_);
  return static_cast<bool>(verifier_);
}
int AndroidScreenLock::CredentialType() {
  std::lock_guard<std::mutex> guard(mutex_);
  return type_;
}
bool AndroidScreenLock::SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (enabled && !verifier_) return false;
  enabled_ = enabled; authorized_ = false; ++generation_;
  return true;
}
bool AndroidScreenLock::Enabled() {
  std::lock_guard<std::mutex> guard(mutex_);
  return enabled_;
}
bool AndroidScreenLock::Verify(const std::string& credential) {
  Verifier verifier;
  unsigned generation;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    authorized_ = false;
    const size_t minimum = type_ == 2 ? 4 : 1;
    if (!enabled_ || !verifier_ || verifying_ || credential.size() < minimum ||
        credential.size() > 256 || credential.find('\0') != std::string::npos ||
        std::chrono::steady_clock::now() < retry_after_) return false;
    if (type_ == 3 && credential.find_first_not_of("0123456789") != std::string::npos)
      return false;
    verifier = verifier_; generation = generation_; verifying_ = true;
  }
  const auto result = verifier(credential);
  std::lock_guard<std::mutex> guard(mutex_);
  verifying_ = false;
  if (!result.success) {
    const int local_delay = 1 << std::min(failures_++, 5U);
    retry_after_ = std::chrono::steady_clock::now() +
        std::chrono::seconds(std::max(local_delay, result.retry_seconds));
    return false;
  }
  failures_ = 0; retry_after_ = {};
  authorized_ = enabled_ && generation == generation_;
  return authorized_;
}
void AndroidScreenLock::Lock() {
  std::lock_guard<std::mutex> guard(mutex_);
  authorized_ = false; ++generation_;
}
bool AndroidScreenLock::ConsumeUnlock() {
  std::lock_guard<std::mutex> guard(mutex_);
  const bool allowed = !enabled_ || authorized_;
  authorized_ = false; return allowed;
}
int AndroidScreenLock::RetrySeconds() {
  std::lock_guard<std::mutex> guard(mutex_);
  const auto remaining = retry_after_ - std::chrono::steady_clock::now();
  return remaining > decltype(remaining)::zero()
      ? static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(remaining).count()) + 1 : 0;
}
}  // namespace aeraui
