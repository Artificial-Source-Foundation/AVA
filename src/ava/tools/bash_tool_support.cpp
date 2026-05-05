#include "ava/tools/bash_tool_support.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <thread>
#include <utility>

namespace ava::tools::detail {

UniqueFd::UniqueFd(int fd) : fd_(fd)
{
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(other.release())
{
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
{
  if (this != &other) {
    reset(other.release());
  }
  return *this;
}

UniqueFd::~UniqueFd()
{
  reset();
}

int UniqueFd::get() const noexcept
{
  return fd_;
}

int UniqueFd::release() noexcept
{
  int const fd = fd_;
  fd_ = -1;
  return fd;
}

void UniqueFd::reset(int fd) noexcept
{
  if (fd_ >= 0) {
    close(fd_);
  }
  fd_ = fd;
}

bool is_shell_metacharacter(char ch)
{
  switch (ch) {
    case ';':
    case '&':
    case '|':
    case '<':
    case '>':
    case '`':
    case '$':
    case '(':
    case ')':
      return true;
    default:
      return false;
  }
}

ava::core::Result<std::vector<std::string>> parse_command_argv(std::string_view command)
{
  std::vector<std::string> argv;
  std::string current;
  char quote = '\0';
  bool escaping = false;

  for (char const ch : command) {
    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (quote != '\0') {
      if (ch == quote) {
        quote = '\0';
      } else {
        current.push_back(ch);
      }
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
      continue;
    }
    if (is_shell_metacharacter(ch)) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                    "shell metacharacters are not supported by the command tool");
      error.with_context("command", std::string(command));
      return std::unexpected(std::move(error));
    }
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!current.empty()) {
        argv.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }

  if (escaping || quote != '\0') {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unterminated command escape or quote");
    error.with_context("command", std::string(command));
    return std::unexpected(std::move(error));
  }
  if (!current.empty()) {
    argv.push_back(current);
  }
  if (argv.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command must not be empty");
    return std::unexpected(std::move(error));
  }
  return argv;
}

void append_tail(BashResult& result, std::string_view chunk, std::size_t max_bytes)
{
  result.total_bytes += chunk.size();
  if (max_bytes == 0) {
    result.truncated = result.total_bytes > 0;
    return;
  }

  if (chunk.size() >= max_bytes) {
    result.output.assign(chunk.substr(chunk.size() - max_bytes));
    result.truncated = true;
    return;
  }

  auto const next_size = result.output.size() + chunk.size();
  if (next_size > max_bytes) {
    result.output.erase(0, next_size - max_bytes);
    result.truncated = true;
  }
  result.output.append(chunk);
}

ssize_t read_retry(int fd, char* data, std::size_t size)
{
  while (true) {
    auto const bytes = read(fd, data, size);
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    return bytes;
  }
}

pid_t waitpid_retry(pid_t pid, int* status, int options)
{
  while (true) {
    auto const waited = waitpid(pid, status, options);
    if (waited < 0 && errno == EINTR) {
      continue;
    }
    return waited;
  }
}

ava::core::Result<std::array<int, 2>> make_pipe()
{
  std::array<int, 2> pipe_fds{-1, -1};
  if (pipe(pipe_fds.data()) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create process pipe");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  return pipe_fds;
}

void close_nonstandard_fds()
{
  long const open_max = sysconf(_SC_OPEN_MAX);
  int const max_fd = open_max > 0 ? static_cast<int>(open_max) : 1024;
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
    close(fd);
  }
}

ava::core::Error pipe_read_error(std::string_view command)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read process output");
  error.with_context("command", std::string(command));
  error.with_context("cause", std::strerror(errno));
  return error;
}

ava::core::Error waitpid_error(std::string_view command)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to wait for process");
  error.with_context("command", std::string(command));
  error.with_context("cause", std::strerror(errno));
  return error;
}

void signal_process(pid_t pid, bool can_signal_group, int signal)
{
  kill(can_signal_group ? -pid : pid, signal);
}

ava::core::VoidResult stop_process(pid_t pid, bool can_signal_group, int& status, std::string_view command)
{
  signal_process(pid, can_signal_group, SIGTERM);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  pid_t const terminated = waitpid_retry(pid, &status, WNOHANG);
  if (terminated < 0) {
    return std::unexpected(waitpid_error(command));
  }
  if (terminated == 0) {
    signal_process(pid, can_signal_group, SIGKILL);
    if (waitpid_retry(pid, &status, 0) < 0) {
      return std::unexpected(waitpid_error(command));
    }
  }
  return {};
}

bool is_bash_canceled(ToolContext const& context)
{
  return context.cancel_requested && context.cancel_requested();
}

}  // namespace ava::tools::detail
