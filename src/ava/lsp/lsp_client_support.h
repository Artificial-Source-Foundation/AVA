#pragma once

#include <signal.h>
#include <sys/types.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/lsp/lsp_client.h"

namespace ava::lsp::detail {

inline constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
inline constexpr std::size_t kMaxLspHeaderBytes = 64 * 1024;
inline constexpr std::size_t kMaxLspMessageBytes = 4 * 1024 * 1024;

class ScopedSignalIgnore {
 public:
  explicit ScopedSignalIgnore(int signal_number) noexcept;
  ScopedSignalIgnore(ScopedSignalIgnore const&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore const&) = delete;
  ScopedSignalIgnore(ScopedSignalIgnore&&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore&&) = delete;
  ~ScopedSignalIgnore();

 private:
  int signal_number_ = 0;
  bool active_ = false;
  struct sigaction previous_ {};
};

[[nodiscard]] ava::core::Error lsp_error(ava::core::ErrorCategory category, std::string message,
                                         ServerConfig const& config);
[[nodiscard]] ava::core::Error errno_error(std::string message, ServerConfig const& config);
[[nodiscard]] bool is_canceled(CancelCallback const& cancel_requested);
[[nodiscard]] ava::core::Error canceled_error(std::string message, ServerConfig const& config);
pid_t waitpid_retry(pid_t pid, int* status, int options);
[[nodiscard]] ssize_t read_retry(int fd, char* data, std::size_t size);
[[nodiscard]] ssize_t write_retry(int fd, char const* data, std::size_t size);
void close_fd(int& fd) noexcept;
[[nodiscard]] ava::core::Result<std::array<int, 2>> make_pipe(ServerConfig const& config);
void close_nonstandard_fds();
[[nodiscard]] std::string file_uri(std::filesystem::path const& path);
[[nodiscard]] std::string json_string(std::string_view value);
[[nodiscard]] std::size_t remaining_ms(std::chrono::steady_clock::time_point deadline);
[[nodiscard]] ava::core::Result<std::size_t> parse_content_length(std::string_view header, ServerConfig const& config);
[[nodiscard]] std::string exit_detail(int status);
[[nodiscard]] ava::core::Result<std::vector<Diagnostic>> parse_diagnostics_response(std::string_view response,
                                                                                    ServerConfig const& config,
                                                                                    std::filesystem::path const& path);

}  // namespace ava::lsp::detail
