#pragma once

#include <signal.h>
#include <sys/types.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>

#include "ava/core/result.h"
#include "ava/mcp/config.h"

namespace ava::mcp::detail {

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
  struct sigaction previous_ {};
  bool installed_ = false;
};

[[nodiscard]] ava::core::Result<std::array<int, 2>> make_mcp_pipe(McpServerConfig const& server);
[[nodiscard]] bool set_child_process_group(pid_t pid);
[[nodiscard]] pid_t waitpid_retry(pid_t pid, int* status, int options);
[[nodiscard]] ssize_t read_retry(int fd, char* data, std::size_t size);
[[nodiscard]] ssize_t write_retry(int fd, char const* data, std::size_t size);
[[nodiscard]] std::size_t remaining_ms(std::chrono::steady_clock::time_point deadline);
void close_fd(int& fd) noexcept;
void close_nonstandard_fds();
[[nodiscard]] std::string exit_detail(int status);

}  // namespace ava::mcp::detail
