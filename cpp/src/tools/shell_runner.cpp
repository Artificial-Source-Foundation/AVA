#include "shell_runner.hpp"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace ava::tools {
namespace {

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

[[nodiscard]] std::string read_temp_file_or_empty(const std::filesystem::path& path) {
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

[[nodiscard]] std::filesystem::path create_private_temp_file() {
#if defined(_WIN32)
  for(int attempt = 0; attempt < 64; ++attempt) {
    std::random_device random;
    const auto path = std::filesystem::temp_directory_path() /
                      ("ava_tool_output_" + std::to_string(random()) + "_" + std::to_string(attempt) + ".txt");
    const auto fd = _open(path.string().c_str(), _O_CREAT | _O_EXCL | _O_BINARY | _O_RDWR, _S_IREAD | _S_IWRITE);
    if(fd != -1) {
      _close(fd);
      return path;
    }
  }
  throw std::runtime_error("Failed to create private tool output file");
#else
  auto pattern = (std::filesystem::temp_directory_path() / "ava_tool_output_XXXXXX").string();
  const int fd = mkstemp(pattern.data());
  if(fd == -1) {
    throw std::runtime_error("Failed to create private tool output file");
  }
  close(fd);
  return pattern;
#endif
}

}  // namespace

CommandOutcome run_shell_command(
    const std::string& command,
    const std::filesystem::path& cwd,
    std::uint64_t timeout_ms
) {
  const auto temp_file = create_private_temp_file();
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
    content = read_temp_file_or_empty(temp_file);
  }

  int exit_code = 1;
  if(status != -1) {
#if defined(_WIN32)
    exit_code = status;
#else
    if(WIFEXITED(status)) {
      exit_code = WEXITSTATUS(status);
    } else if(WIFSIGNALED(status)) {
      exit_code = 128 + WTERMSIG(status);
    } else {
      exit_code = status;
    }
#endif
  }

  return CommandOutcome{.output = std::move(content), .exit_code = exit_code};
}

std::string render_shell_result(const CommandOutcome& outcome) {
  std::ostringstream oss;
  oss << "stdout:\n" << outcome.output << "\n\nstderr:\n\nexit_code: " << outcome.exit_code;
  return oss.str();
}

}  // namespace ava::tools
