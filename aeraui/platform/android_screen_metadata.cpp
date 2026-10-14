// SPDX-License-Identifier: Apache-2.0
#include "android_screen_credential.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

namespace aeraui {
namespace {
bool Read(const std::string& path, std::vector<uint8_t>* bytes, size_t limit) {
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  struct stat st{};
  bool okay = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
      static_cast<uint64_t>(st.st_size) <= limit;
  if (okay) {
    bytes->resize(st.st_size);
    size_t offset = 0;
    while (offset < bytes->size()) {
      const auto count = read(fd, bytes->data() + offset, bytes->size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) { okay = false; break; }
      offset += count;
    }
    struct stat after{};
    okay = okay && fstat(fd, &after) == 0 && after.st_size == st.st_size &&
        after.st_mtim.tv_sec == st.st_mtim.tv_sec && after.st_mtim.tv_nsec == st.st_mtim.tv_nsec;
  }
  close(fd);
  return okay;
}
uint32_t BigEndian(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
bool ParametersSafe(const AndroidScreenCredential& m) {
  if (m.scrypt_n > 16 || m.scrypt_r > 4 || m.scrypt_p > 4 || m.scrypt_n == 0)
    return false;
  return (uint64_t{128} << (m.scrypt_n + m.scrypt_r)) <= 64 * 1024 * 1024 &&
      (uint64_t{1} << (m.scrypt_n + m.scrypt_r + m.scrypt_p)) <= (1U << 22);
}
// SQLite may create shared-memory sidecars even for a read-only WAL query.
// Copy the database and WAL to recovery's RAM /tmp first. No Android database
// or sidecar is opened for writing; the copies are removed immediately.
bool OwnerHandle(const std::string& root, std::string* handle) {
  std::vector<uint8_t> db_bytes, wal_bytes;
  const auto source = root + "/system/locksettings.db";
  if (!Read(source, &db_bytes, 16 * 1024 * 1024) || db_bytes.empty()) return false;
  struct stat wal_stat{};
  const bool has_wal = lstat((source + "-wal").c_str(), &wal_stat) == 0;
  if (has_wal && !Read(source + "-wal", &wal_bytes, 16 * 1024 * 1024)) return false;
  char directory[] = "/tmp/aera-lock-metadata-XXXXXX";
  if (!mkdtemp(directory)) return false;
  const auto copy = std::string(directory) + "/locksettings.db";
  const auto write_copy = [](const std::string& path, const std::vector<uint8_t>& bytes) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    size_t offset = 0;
    while (offset < bytes.size()) {
      const auto n = write(fd, bytes.data() + offset, bytes.size() - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      offset += n;
    }
    close(fd);
    return offset == bytes.size();
  };
  bool okay = write_copy(copy, db_bytes) &&
      (!has_wal || write_copy(copy + "-wal", wal_bytes));
  sqlite3* db = nullptr;
  sqlite3_stmt* query = nullptr;
  if (okay) okay = sqlite3_open_v2(copy.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK;
  if (okay) okay = sqlite3_prepare_v2(db,
      "SELECT value FROM locksettings WHERE name='sp-handle' AND user=0", -1, &query, nullptr) == SQLITE_OK;
  if (okay) {
    okay = sqlite3_step(query) == SQLITE_ROW;
    if (okay) {
      const uint64_t id = static_cast<uint64_t>(sqlite3_column_int64(query, 0));
      char hex[17]{};
      snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(id));
      *handle = hex;
      okay = id != 0 && sqlite3_step(query) == SQLITE_DONE;
    }
  }
  if (query) sqlite3_finalize(query);
  if (db) sqlite3_close(db);
  for (const char* suffix : {"", "-wal", "-shm", "-journal"}) unlink((copy + suffix).c_str());
  rmdir(directory);
  return okay;
}
}  // namespace

bool ParseAndroidPasswordData(const std::vector<uint8_t>& bytes, AndroidScreenCredential* out) {
  if (!out || bytes.size() < 15) return false;
  AndroidScreenCredential m;
  const auto type = BigEndian(bytes.data());
  m.type = type == 1 ? 2 : type == 3 ? 3 : (type == 2 || type == 4) ? 1 : 0;
  if (!m.type) return false;
  m.synthetic = true;
  m.scrypt_n = bytes[4]; m.scrypt_r = bytes[5]; m.scrypt_p = bytes[6];
  if (!ParametersSafe(m)) return false;
  const auto salt_size = BigEndian(bytes.data() + 7);
  if (!salt_size || salt_size > 64 || salt_size > bytes.size() - 15) return false;
  m.salt.assign(bytes.begin() + 11, bytes.begin() + 11 + salt_size);
  const size_t offset = 11 + salt_size;
  const auto handle_size = BigEndian(bytes.data() + offset);
  if (handle_size > 4096 || handle_size > bytes.size() - offset - 4) return false;
  m.handle.assign(bytes.begin() + offset + 4, bytes.begin() + offset + 4 + handle_size);
  // Android 14+ appends PIN-length metadata. It is not credential material.
  const size_t tail = bytes.size() - offset - 4 - handle_size;
  if (tail != 0 && tail != 4) return false;
  *out = std::move(m);
  return true;
}

bool ReadAndroidScreenCredential(AndroidScreenCredential* out, const std::string& root) {
  if (!out) return false;
  std::string handle;
  if (!OwnerHandle(root, &handle)) return false;
  const auto base = root + "/system_de/0/spblob/";
  // Native vold accepts unpadded handles as well as Android's padded form.
  const auto nonzero = handle.find_first_not_of('0');
  const auto short_handle = handle.substr(nonzero);
  std::vector<std::string> names{handle, short_handle, "0" + short_handle, "00" + short_handle};
  for (const auto& name : names) {
    std::vector<uint8_t> bytes;
    if (!Read(base + name + ".pwd", &bytes, 8192)) continue;
    AndroidScreenCredential m;
    if (!ParseAndroidPasswordData(bytes, &m)) return false;
    struct stat st{};
    if (lstat((base + name + ".weaver").c_str(), &st) == 0) {
      if (!Read(base + name + ".weaver", &bytes, 5) || bytes.size() != 5 || bytes[0] != 1)
        return false;
      const uint32_t slot = uint32_t(bytes[1]) | uint32_t(bytes[2]) << 8 |
          uint32_t(bytes[3]) << 16 | uint32_t(bytes[4]) << 24;
      if (slot > INT32_MAX) return false;
      m.weaver_slot = slot;
    } else if (errno != ENOENT || m.handle.empty()) return false;
    *out = std::move(m);
    return true;
  }
  return false;
}

bool DeriveAndroidScreenToken(const AndroidScreenCredential& m, const std::string& credential,
                              std::vector<uint8_t>* token) {
  if (!token || !m.synthetic || m.user_id != 0 || m.type < 1 || m.type > 3 || !ParametersSafe(m) || m.salt.empty() || m.salt.size() > 64 ||
      credential.empty() || credential.size() > 256 || credential.find('\0') != std::string::npos)
    return false;
  std::vector<uint8_t> stretched(32);
  if (EVP_PBE_scrypt(credential.data(), credential.size(), m.salt.data(), m.salt.size(),
      uint64_t{1} << m.scrypt_n, uint64_t{1} << m.scrypt_r, uint64_t{1} << m.scrypt_p,
      64 * 1024 * 1024, stretched.data(), stretched.size()) != 1) return false;
  // Same personalization as Android SyntheticPasswordManager and native vold.
  const char* label = m.weaver_slot >= 0 ? "weaver-key" : "user-gk-authentication";
  std::vector<uint8_t> personalized(128 + stretched.size(), 0);
  std::copy(label, label + strlen(label), personalized.begin());
  std::copy(stretched.begin(), stretched.end(), personalized.begin() + 128);
  token->resize(SHA512_DIGEST_LENGTH);
  SHA512(personalized.data(), personalized.size(), token->data());
  OPENSSL_cleanse(stretched.data(), stretched.size());
  OPENSSL_cleanse(personalized.data(), personalized.size());
  return true;
}
}  // namespace aeraui
