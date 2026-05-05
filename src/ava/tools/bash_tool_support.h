#pragma once

#include <sys/types.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"

namespace ava::tools::detail {

inline constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
inline constexpr std::size_t kBashProgressByteInterval = 128 * 1024;
inline constexpr auto kBashProgressTimeInterval = std::chrono::seconds(2);

class UniqueFd {
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

[[nodiscard]] bool is_shell_metacharacter(char ch);
[[nodiscard]] ava::core::Result<std::vector<std::string>> parse_command_argv(std::string_view command);
void append_tail(BashResult& result, std::string_view chunk, std::size_t max_bytes);
ssize_t read_retry(int fd, char* data, std::size_t size);
pid_t waitpid_retry(pid_t pid, int* status, int options);
[[nodiscard]] ava::core::Result<std::array<int, 2>> make_pipe();
void close_nonstandard_fds();
[[nodiscard]] ava::core::Error pipe_read_error(std::string_view command);
[[nodiscard]] ava::core::Error waitpid_error(std::string_view command);
void signal_process(pid_t pid, bool can_signal_group, int signal);
[[nodiscard]] ava::core::VoidResult stop_process(pid_t pid, bool can_signal_group, int& status,
                                                 std::string_view command);
[[nodiscard]] bool is_bash_canceled(ToolContext const& context);

}  // namespace ava::tools::detail
