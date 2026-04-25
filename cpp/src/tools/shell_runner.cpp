#include "shell_runner.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

#include <sys/wait.h>

namespace ava::tools {
namespace {

std::atomic<std::uint64_t> g_temp_file_counter{0};

[[nodiscard]] std::string shell_single_quote(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('\'');
  for(const auto ch : value) {
    if(ch == '\'') {
      escaped += "'\\''";
    } else {
      escaped.push_back(ch);
    }
  }
  escaped.push_back('\'');
  return escaped;
}

[[nodiscard]] std::string read_file_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if(!file) {
    return {};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

class TempFileGuard {
 public:
  explicit TempFileGuard(std::filesystem::path path) : path_(std::move(path)) {}

  ~TempFileGuard() {
    if(path_.empty()) {
      return;
    }

    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

CommandOutcome run_shell_command(
    const std::string& command,
    const std::filesystem::path& cwd,
    std::uint64_t timeout_ms
) {
  const auto temp_file = std::filesystem::temp_directory_path() /
                         ("ava_tool_output_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                          "_" + std::to_string(g_temp_file_counter.fetch_add(1, std::memory_order_relaxed)) + ".txt");
  TempFileGuard temp_file_guard(temp_file);

  const auto cd_clause = "cd " + shell_single_quote(cwd.string()) + " && ";
  const auto timeout_secs = timeout_ms >= std::numeric_limits<std::uint64_t>::max() - 999
                                ? std::numeric_limits<std::uint64_t>::max() / 1000
                                : std::max<std::uint64_t>(1, (timeout_ms + 999) / 1000);
  const auto output_redirect = " >" + shell_single_quote(temp_file.string()) + " 2>&1";
  const auto tool_command = "sh -lc " + shell_single_quote(command);
  const auto timeout_warning = shell_single_quote("warning: 'timeout' not found; command ran without timeout enforcement");
  const auto wrapped = cd_clause + "if command -v timeout >/dev/null 2>&1; then timeout --signal=TERM --kill-after=1s " +
                        std::to_string(timeout_secs) + "s " + tool_command + output_redirect + "; else printf '%s\n' " +
                        timeout_warning + " >" + shell_single_quote(temp_file.string()) + "; " + tool_command +
                        " >>" + shell_single_quote(temp_file.string()) + " 2>&1; fi";

  const auto full = "sh -lc " + shell_single_quote(wrapped);
  const int status = std::system(full.c_str());

  std::error_code ec;
  std::string content;
  if(std::filesystem::exists(temp_file, ec) && !ec) {
    content = read_file_text(temp_file);
  }

  int exit_code = 1;
  if(status != -1) {
    if(WIFEXITED(status)) {
      exit_code = WEXITSTATUS(status);
    } else if(WIFSIGNALED(status)) {
      exit_code = 128 + WTERMSIG(status);
    } else {
      exit_code = status;
    }
  }

  return CommandOutcome{.output = std::move(content), .exit_code = exit_code};
}

std::string render_shell_result(const CommandOutcome& outcome) {
  std::ostringstream oss;
  oss << "stdout:\n" << outcome.output << "\n\nstderr:\n\nexit_code: " << outcome.exit_code;
  return oss.str();
}

}  // namespace ava::tools
