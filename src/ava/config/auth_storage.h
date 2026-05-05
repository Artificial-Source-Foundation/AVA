#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

namespace ava::config {

struct AuthFileRead {
  std::optional<std::string> content;
};

class AuthFileLock {
 public:
  explicit AuthFileLock(int fd) noexcept;
  AuthFileLock(AuthFileLock const&) = delete;
  AuthFileLock& operator=(AuthFileLock const&) = delete;
  AuthFileLock(AuthFileLock&& other) noexcept;
  AuthFileLock& operator=(AuthFileLock&& other) noexcept;
  ~AuthFileLock();

  [[nodiscard]] int get() const noexcept;

 private:
  void close_if_open() noexcept;

  int fd_ = -1;
};

[[nodiscard]] bool auth_error_has_context(ava::core::Error const& error, std::string_view key, std::string_view value);
[[nodiscard]] ava::core::VoidResult ensure_auth_directory(XdgPaths const& paths);
[[nodiscard]] ava::core::Result<AuthFileLock> acquire_auth_file_lock(XdgPaths const& paths);
[[nodiscard]] ava::core::Result<AuthFileRead> read_auth_text_if_exists(std::filesystem::path const& path,
                                                                       bool explicit_ava_auth_file,
                                                                       bool allow_broad_permissions = false);
[[nodiscard]] ava::core::VoidResult write_auth_file_atomic(std::filesystem::path const& path, std::string_view body);

}  // namespace ava::config
