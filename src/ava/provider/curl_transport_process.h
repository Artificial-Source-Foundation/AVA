#pragma once

#include <signal.h>
#include <sys/types.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "ava/core/result.h"

namespace ava::provider::detail {

inline constexpr char kTrustedCurlExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
inline constexpr std::size_t kMaxCurlResponseBytes = 8 * 1024 * 1024;
inline constexpr std::size_t kMaxCurlStderrBytes = 64 * 1024;

class UniqueFd {
 public:
  explicit UniqueFd(int fd = -1) noexcept;
  UniqueFd(UniqueFd const&) = delete;
  UniqueFd& operator=(UniqueFd const&) = delete;
  UniqueFd(UniqueFd&& other) noexcept;
  UniqueFd& operator=(UniqueFd&& other) noexcept;
  ~UniqueFd();

  [[nodiscard]] int get() const noexcept;
  [[nodiscard]] int release() noexcept;
  void reset(int fd = -1) noexcept;

 private:
  int fd_ = -1;
};

class TempBodyFile {
 public:
  TempBodyFile() = default;
  TempBodyFile(TempBodyFile const&) = delete;
  TempBodyFile& operator=(TempBodyFile const&) = delete;
  TempBodyFile(TempBodyFile&& other) noexcept;
  TempBodyFile& operator=(TempBodyFile&& other) noexcept;
  ~TempBodyFile();

  [[nodiscard]] std::string const& path() const noexcept;

  [[nodiscard]] static ava::core::Result<TempBodyFile> create(std::string_view body);

 private:
  void cleanup() noexcept;

  std::string path_;
};

class ScopedSignalIgnore {
 public:
  explicit ScopedSignalIgnore(int signal) noexcept;
  ScopedSignalIgnore(ScopedSignalIgnore const&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore const&) = delete;
  ScopedSignalIgnore(ScopedSignalIgnore&&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore&&) = delete;
  ~ScopedSignalIgnore();

 private:
  int signal_ = 0;
  bool active_ = false;
  struct sigaction previous_ {};
};

[[nodiscard]] ava::core::Result<std::array<int, 2>> make_curl_pipe();
void close_nonstandard_fds();
[[nodiscard]] ssize_t read_retry(int fd, char* data, std::size_t size);
pid_t waitpid_retry(pid_t pid, int* status, int options);
void kill_and_wait(pid_t pid);
[[nodiscard]] ava::core::VoidResult write_curl_config(int fd, pid_t pid, std::string_view config);
void append_bounded(std::string& value, char const* data, std::size_t size, std::size_t limit);

}  // namespace ava::provider::detail
