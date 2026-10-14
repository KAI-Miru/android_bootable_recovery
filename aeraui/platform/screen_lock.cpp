// SPDX-License-Identifier: Apache-2.0
#include "screen_lock.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace aeraui {
namespace {
constexpr int kIterations = 600000;

std::string Hex(const unsigned char* value, size_t length) {
  constexpr char digits[] = "0123456789abcdef";
  std::string text;
  for (size_t i = 0; i < length; ++i) {
    text += digits[value[i] >> 4]; text += digits[value[i] & 15];
  }
  return text;
}

template<size_t N>
bool Decode(const std::string& text, std::array<unsigned char, N>& output) {
  if (text.size() != N * 2) return false;
  const auto digit = [](char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
  };
  for (size_t i = 0; i < N; ++i) {
    const int high = digit(text[i * 2]), low = digit(text[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[i] = static_cast<unsigned char>(high * 16 + low);
  }
  return true;
}

bool Derive(const std::string& password, const std::array<unsigned char, 16>& salt,
            std::array<unsigned char, 32>& digest) {
  return password.size() <= 256 && password.find('\0') == std::string::npos &&
      PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                        salt.data(), salt.size(), kIterations, EVP_sha256(),
                        digest.size(), digest.data()) == 1;
}
}  // namespace

bool ScreenLockCredential::Load() {
  // Keep an enabled verifier in memory if /data goes away. Missing storage or
  // a removed/corrupt file must not turn off a lock already enabled this boot.
  if (enabled_ && valid_) return true;
  const int fd = open(path_.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    if (errno != ENOENT) enabled_ = true;
    return enabled_;
  }
  enabled_ = true;
  struct stat st{};
  char buffer[192]{};
  const bool safe = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
      st.st_uid == getuid() && !(st.st_mode & 0077) && st.st_size > 0 &&
      st.st_size < static_cast<off_t>(sizeof(buffer));
  const ssize_t bytes = safe ? read(fd, buffer, sizeof(buffer) - 1) : -1;
  close(fd);
  if (!safe || bytes != st.st_size) return true;
  std::istringstream input(std::string(buffer, static_cast<size_t>(bytes)));
  std::string schema, iterations, salt, digest, extra;
  valid_ = std::getline(input, schema) && schema == "AERA-SCREEN-LOCK-1" &&
      std::getline(input, iterations) && iterations == std::to_string(kIterations) &&
      std::getline(input, salt) && Decode(salt, salt_) &&
      std::getline(input, digest) && Decode(digest, digest_) && !std::getline(input, extra);
  return true;
}

bool ScreenLockCredential::Enabled() {
  std::lock_guard<std::mutex> guard(mutex_);
  return Load();
}

bool ScreenLockCredential::VerifyLocked(const std::string& password) {
  const auto now = std::chrono::steady_clock::now();
  if (now < retry_after_ || !Load() || !valid_) return false;
  std::array<unsigned char, 32> candidate{};
  const bool match = Derive(password, salt_, candidate) &&
      CRYPTO_memcmp(candidate.data(), digest_.data(), digest_.size()) == 0;
  OPENSSL_cleanse(candidate.data(), candidate.size());
  if (match) {
    failures_ = 0;
    retry_after_ = {};
  } else {
    const unsigned delay = 1u << failures_;
    failures_ = std::min(failures_ + 1, 5u);
    retry_after_ = std::chrono::steady_clock::now() + std::chrono::seconds(delay);
  }
  return match;
}

bool ScreenLockCredential::Verify(const std::string& password) {
  std::lock_guard<std::mutex> guard(mutex_);
  authorized_ = false;
  authorized_ = VerifyLocked(password);
  return authorized_;
}

bool ScreenLockCredential::Write(const std::array<unsigned char, 16>& salt,
                                 const std::array<unsigned char, 32>& digest) {
  const auto slash = path_.find_last_of('/');
  if (slash == std::string::npos) return false;
  const std::string directory = path_.substr(0, slash);
  if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) return false;
  const int parent = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (parent < 0) return false;
  struct stat st{};
  if (fstat(parent, &st) != 0 || st.st_uid != getuid() || (st.st_mode & 0077)) {
    close(parent); return false;
  }
  const std::string name = path_.substr(slash + 1), temporary = name + ".new";
  unlinkat(parent, temporary.c_str(), 0);
  const int fd = openat(parent, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL |
                        O_NOFOLLOW | O_CLOEXEC, 0600);
  const std::string record = "AERA-SCREEN-LOCK-1\n" + std::to_string(kIterations) +
      "\n" + Hex(salt.data(), salt.size()) + "\n" + Hex(digest.data(), digest.size()) + "\n";
  bool okay = fd >= 0;
  size_t offset = 0;
  while (okay && offset < record.size()) {
    const ssize_t amount = write(fd, record.data() + offset, record.size() - offset);
    if (amount < 0 && errno == EINTR) continue;
    if (amount <= 0) okay = false; else offset += static_cast<size_t>(amount);
  }
  if (okay) okay = fsync(fd) == 0;
  if (fd >= 0 && close(fd) != 0) okay = false;
  if (okay) okay = renameat(parent, temporary.c_str(), parent, name.c_str()) == 0;
  if (okay) fsync(parent);
  if (!okay) unlinkat(parent, temporary.c_str(), 0);
  close(parent);
  return okay;
}

bool ScreenLockCredential::Change(const std::string& current, const std::string& replacement) {
  std::lock_guard<std::mutex> guard(mutex_);
  const auto characters = std::count_if(replacement.begin(), replacement.end(),
      [](unsigned char c) { return (c & 0xc0) != 0x80; });
  if (characters < 8 || replacement.size() > 256 ||
      (Load() && !VerifyLocked(current))) return false;
  std::array<unsigned char, 16> salt{};
  std::array<unsigned char, 32> digest{};
  const bool okay = RAND_bytes(salt.data(), salt.size()) == 1 &&
      Derive(replacement, salt, digest) && Write(salt, digest);
  if (okay) { salt_ = salt; digest_ = digest; }
  OPENSSL_cleanse(digest.data(), digest.size());
  if (!okay) return false;
  enabled_ = valid_ = true;
  authorized_ = false;
  return true;
}

bool ScreenLockCredential::Disable(const std::string& current) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!VerifyLocked(current) || (unlink(path_.c_str()) != 0 && errno != ENOENT)) return false;
  enabled_ = valid_ = authorized_ = false;
  OPENSSL_cleanse(digest_.data(), digest_.size());
  return true;
}

void ScreenLockCredential::Lock() {
  std::lock_guard<std::mutex> guard(mutex_);
  authorized_ = false;
}

bool ScreenLockCredential::ConsumeUnlock() {
  std::lock_guard<std::mutex> guard(mutex_);
  const bool allowed = !Load() || authorized_;
  authorized_ = false;
  return allowed;
}

int ScreenLockCredential::RetrySeconds() {
  std::lock_guard<std::mutex> guard(mutex_);
  const auto remaining = retry_after_ - std::chrono::steady_clock::now();
  return remaining > decltype(remaining)::zero()
      ? static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(remaining).count()) + 1 : 0;
}
}  // namespace aeraui
