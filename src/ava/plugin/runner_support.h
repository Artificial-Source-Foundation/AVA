#pragma once

#include "ava/plugin/runner.h"
#include "ava/core/result.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace ava::plugin {

inline constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

class UniqueFd
{
 public:
  explicit UniqueFd(int fd = -1);
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

class ScopedSignalIgnore
{
 public:
  explicit ScopedSignalIgnore(int signal);
  ScopedSignalIgnore(ScopedSignalIgnore const&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore const&) = delete;
  ScopedSignalIgnore(ScopedSignalIgnore&&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore&&) = delete;
  ~ScopedSignalIgnore();

 private:
  int signal_ = 0;
  struct sigaction previous_{};
  bool installed_ = false;
};

[[nodiscard]] ava::core::Error plugin_error(ava::core::ErrorCategory category, std::string message, PluginManifest const& manifest);
[[nodiscard]] ava::core::Error errno_error(std::string message, PluginManifest const& manifest);
[[nodiscard]] ava::core::Error protocol_error(std::string message, PluginManifest const& manifest);
[[nodiscard]] bool is_canceled(CancelCallback const& cancel_requested);
[[nodiscard]] ava::core::Error canceled_error(std::string message, PluginManifest const& manifest);
[[nodiscard]] ava::core::Result<std::array<int, 2>> make_pipe(PluginManifest const& manifest);
[[nodiscard]] bool set_child_process_group(pid_t pid);
[[nodiscard]] pid_t waitpid_retry(pid_t pid, int* status, int options);
[[nodiscard]] ssize_t read_retry(int fd, char* data, std::size_t size);
[[nodiscard]] ssize_t write_retry(int fd, char const* data, std::size_t size);
[[nodiscard]] std::size_t remaining_ms(std::chrono::steady_clock::time_point deadline);
void close_fd(int& fd) noexcept;
void close_nonstandard_fds();
[[nodiscard]] std::string json_string(std::string_view value);
[[nodiscard]] std::string exit_detail(int status);
[[nodiscard]] std::vector<std::string> plugin_argv(PluginManifest const& manifest);
[[nodiscard]] std::filesystem::path child_working_dir(PluginManifest const& manifest, PluginRunnerOptions const& options);

}  // namespace ava::plugin
