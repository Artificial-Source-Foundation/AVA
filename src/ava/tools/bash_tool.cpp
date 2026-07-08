#include "sys.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/spill_files.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ava::tools {

namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
constexpr std::size_t kBashProgressByteInterval = 128 * 1024;
constexpr auto kBashProgressTimeInterval = std::chrono::seconds(2);

class UniqueFd
{
 public:
  explicit UniqueFd(int fd = -1) : fd_(fd) { }
  UniqueFd(UniqueFd const&) = delete;
  UniqueFd& operator=(UniqueFd const&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) { }
  UniqueFd& operator=(UniqueFd&& other) noexcept
  {
    if (this != &other)
    {
      reset(other.release());
    }
    return *this;
  }
  ~UniqueFd() { reset(); }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] int release() noexcept
  {
    int const fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset(int fd = -1) noexcept
  {
    if (fd_ >= 0)
    {
      close(fd_);
    }
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

bool is_shell_metacharacter(char ch)
{
  switch (ch)
  {
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

  for (char const ch : command)
  {
    if (escaping)
    {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\')
    {
      escaping = true;
      continue;
    }
    if (quote != '\0')
    {
      if (ch == quote)
      {
        quote = '\0';
      }
      else
      {
        current.push_back(ch);
      }
      continue;
    }
    if (ch == '\'' || ch == '"')
    {
      quote = ch;
      continue;
    }
    if (is_shell_metacharacter(ch))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "shell metacharacters are not supported by the command tool");
      error.with_context("command", std::string(command));
      return std::unexpected(std::move(error));
    }
    if (std::isspace(static_cast<unsigned char>(ch)))
    {
      if (!current.empty())
      {
        argv.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }

  if (escaping || quote != '\0')
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unterminated command escape or quote");
    error.with_context("command", std::string(command));
    return std::unexpected(std::move(error));
  }
  if (!current.empty())
  {
    argv.push_back(current);
  }
  if (argv.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command must not be empty");
    return std::unexpected(std::move(error));
  }
  return argv;
}

std::size_t logical_line_count(std::string_view text)
{
  if (text.empty())
    return 0;
  auto const newline_count = static_cast<std::size_t>(std::ranges::count(text, '\n'));
  return text.back() == '\n' ? newline_count : newline_count + 1;
}

std::size_t byte_offset_for_line(std::string_view text, std::size_t line)
{
  if (line <= 1)
    return 0;
  std::size_t current_line = 1;
  for (std::size_t index = 0; index < text.size(); ++index)
  {
    if (text[index] != '\n')
      continue;
    ++current_line;
    if (current_line == line)
      return index + 1;
  }
  return text.size();
}

void trim_to_last_lines(BashResult& result, std::size_t max_lines)
{
  auto const output_lines = logical_line_count(result.output);
  if (output_lines == 0)
    return;
  if (max_lines == 0)
  {
    result.output.clear();
    result.line_limited = true;
    return;
  }
  if (output_lines <= max_lines)
    return;
  auto const start_line = output_lines - max_lines + 1;
  auto const offset = byte_offset_for_line(result.output, start_line);
  if (offset == 0)
    return;
  result.output.erase(0, offset);
  result.line_limited = true;
}

void trim_to_max_bytes(BashResult& result, std::size_t max_bytes)
{
  if (result.output.empty())
    return;
  if (max_bytes == 0)
  {
    result.output.clear();
    result.byte_limited = true;
    return;
  }
  if (result.output.size() <= max_bytes)
    return;

  auto offset = result.output.size() - max_bytes;
  auto const newline = result.output.find('\n', offset);
  if (newline != std::string::npos && newline + 1 < result.output.size())
  {
    offset = newline + 1;
  }
  result.output.erase(0, offset);
  result.byte_limited = true;
}

void append_output(BashResult& result, std::string_view chunk, BashOptions const& options, bool& saw_output, bool& previous_was_newline,
                   std::size_t& newline_count)
{
  result.total_bytes += chunk.size();
  for (char const ch : chunk)
  {
    saw_output = true;
    if (ch == '\n')
    {
      ++newline_count;
      previous_was_newline = true;
    }
    else
    {
      previous_was_newline = false;
    }
  }
  result.output.append(chunk);
  trim_to_last_lines(result, options.max_lines);
  trim_to_max_bytes(result, options.max_bytes);
  result.output_bytes = result.output.size();
}

void finalize_output(BashResult& result, BashOptions const& options, bool saw_output, bool previous_was_newline, std::size_t newline_count)
{
  result.total_lines = saw_output ? newline_count + (previous_was_newline ? 0 : 1) : 0;
  if (options.max_lines > 0 && result.total_lines > options.max_lines)
    result.line_limited = true;
  result.output_bytes = result.output.size();
  result.output_lines = logical_line_count(result.output);
  result.omitted_lines = result.total_lines > result.output_lines ? result.total_lines - result.output_lines : 0;
  result.truncated = result.byte_limited || result.line_limited || result.output_bytes < result.total_bytes;
}

ssize_t read_retry(int fd, char* data, std::size_t size)
{
  while (true)
  {
    auto const bytes = read(fd, data, size);
    if (bytes < 0 && errno == EINTR)
    {
      continue;
    }
    return bytes;
  }
}

pid_t waitpid_retry(pid_t pid, int* status, int options)
{
  while (true)
  {
    auto const waited = waitpid(pid, status, options);
    if (waited < 0 && errno == EINTR)
    {
      continue;
    }
    return waited;
  }
}

ava::core::Result<std::array<int, 2>> make_pipe()
{
  std::array<int, 2> pipe_fds{-1, -1};
  if (pipe(pipe_fds.data()) != 0)
  {
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
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd)
  {
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
  if (terminated < 0)
  {
    return std::unexpected(waitpid_error(command));
  }
  if (terminated == 0)
  {
    signal_process(pid, can_signal_group, SIGKILL);
    if (waitpid_retry(pid, &status, 0) < 0)
    {
      return std::unexpected(waitpid_error(command));
    }
  }
  return {};
}

bool is_canceled(ToolContext const& context)
{
  return context.cancel_requested && context.cancel_requested();
}

}  // namespace

ava::core::Result<BashResult> run_bash(ToolContext const& context, std::string_view command, BashOptions options)
{
  if (command.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command must not be empty");
    return std::unexpected(std::move(error));
  }
  auto const tool_name = context.permission_tool_name.empty() ? std::string("bash") : context.permission_tool_name;
  if (auto permission =
          ensure_permission(context, ava::permissions::Operation::RunCommand, context.workspace_dir, command, tool_name, "command requires permission");
      !permission)
  {
    return std::unexpected(permission.error());
  }
  if (is_canceled(context))
  {
    BashResult result;
    result.exit_code = -1;
    result.canceled = true;
    return result;
  }

  auto argv_strings = parse_command_argv(command);
  if (!argv_strings)
  {
    return std::unexpected(argv_strings.error());
  }
  std::vector<char*> argv;
  argv.reserve(argv_strings->size() + 1);
  for (auto& arg : *argv_strings)
  {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  auto const workspace_text = context.workspace_dir.string();

  auto pipe_result = make_pipe();
  if (!pipe_result)
  {
    return std::unexpected(pipe_result.error());
  }
  UniqueFd read_fd((*pipe_result)[0]);
  UniqueFd write_fd((*pipe_result)[1]);

  pid_t const pid = fork();
  if (pid < 0)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to fork process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }

  if (pid == 0)
  {
    setpgid(0, 0);
    close(read_fd.get());
    dup2(write_fd.get(), STDOUT_FILENO);
    dup2(write_fd.get(), STDERR_FILENO);
    close(write_fd.get());
    if (chdir(workspace_text.c_str()) != 0)
    {
      _exit(127);
    }
    if (setenv("PATH", kTrustedExecPath, 1) != 0)
    {
      _exit(127);
    }
    close_nonstandard_fds();
    execvp(argv[0], argv.data());
    _exit(127);
  }

  bool const can_signal_group = setpgid(pid, pid) == 0 || errno == EACCES;
  int status = 0;
  write_fd.reset();
  int const flags = fcntl(read_fd.get(), F_GETFL, 0);
  if (flags < 0)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read process pipe flags");
    error.with_context("command", std::string(command));
    error.with_context("cause", std::strerror(errno));
    signal_process(pid, can_signal_group, SIGKILL);
    waitpid_retry(pid, &status, 0);
    return std::unexpected(std::move(error));
  }
  if (fcntl(read_fd.get(), F_SETFL, flags | O_NONBLOCK) < 0)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set process pipe nonblocking");
    error.with_context("command", std::string(command));
    error.with_context("cause", std::strerror(errno));
    signal_process(pid, can_signal_group, SIGKILL);
    waitpid_retry(pid, &status, 0);
    return std::unexpected(std::move(error));
  }

  BashResult result;
  SpillBuffer spill_buffer;
  bool running = true;
  bool saw_output = false;
  bool previous_was_newline = false;
  std::size_t newline_count = 0;
  auto const deadline = std::chrono::steady_clock::now() + options.timeout;
  auto last_progress = std::chrono::steady_clock::now();
  std::size_t next_progress_bytes = kBashProgressByteInterval;
  std::array<char, 4096> buffer{};

  auto const maybe_emit_progress = [&]() -> ava::core::VoidResult {
    if (!context.progress_sink)
      return {};
    auto const now = std::chrono::steady_clock::now();
    if (result.total_bytes < next_progress_bytes && now - last_progress < kBashProgressTimeInterval)
      return {};
    while (result.total_bytes >= next_progress_bytes) next_progress_bytes += kBashProgressByteInterval;
    last_progress = now;
    return emit_tool_progress(context, "bash output " + std::to_string(result.total_bytes) + " bytes", "running");
  };

  while (running)
  {
    while (true)
    {
      auto const bytes = read_retry(read_fd.get(), buffer.data(), buffer.size());
      if (bytes > 0)
      {
        std::string_view const chunk(buffer.data(), static_cast<std::size_t>(bytes));
        spill_buffer.append(chunk);
        append_output(result, chunk, options, saw_output, previous_was_newline, newline_count);
        if (auto progress = maybe_emit_progress(); !progress)
        {
          signal_process(pid, can_signal_group, SIGKILL);
          waitpid_retry(pid, &status, 0);
          return std::unexpected(std::move(progress.error()));
        }
        continue;
      }
      if (bytes == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
      {
        break;
      }
      auto error = pipe_read_error(command);
      signal_process(pid, can_signal_group, SIGKILL);
      waitpid_retry(pid, &status, 0);
      return std::unexpected(std::move(error));
    }

    if (is_canceled(context))
    {
      result.canceled = true;
      if (auto stopped = stop_process(pid, can_signal_group, status, command); !stopped)
      {
        return std::unexpected(std::move(stopped.error()));
      }
      running = false;
      break;
    }

    pid_t const waited = waitpid_retry(pid, &status, WNOHANG);
    if (waited == pid)
    {
      running = false;
      break;
    }
    if (waited < 0)
    {
      return std::unexpected(waitpid_error(command));
    }

    if (std::chrono::steady_clock::now() >= deadline)
    {
      result.timed_out = true;
      if (auto stopped = stop_process(pid, can_signal_group, status, command); !stopped)
      {
        return std::unexpected(std::move(stopped.error()));
      }
      running = false;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  while (true)
  {
    auto const bytes = read_retry(read_fd.get(), buffer.data(), buffer.size());
    if (bytes <= 0)
    {
      if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
      {
        auto error = pipe_read_error(command);
        return std::unexpected(std::move(error));
      }
      break;
    }
    std::string_view const chunk(buffer.data(), static_cast<std::size_t>(bytes));
    spill_buffer.append(chunk);
    append_output(result, chunk, options, saw_output, previous_was_newline, newline_count);
    if (auto progress = maybe_emit_progress(); !progress)
      return std::unexpected(std::move(progress.error()));
  }
  read_fd.reset();

  if (result.timed_out || result.canceled)
  {
    result.exit_code = -1;
  }
  else if (WIFEXITED(status))
  {
    result.exit_code = WEXITSTATUS(status);
  }
  else if (WIFSIGNALED(status))
  {
    result.exit_code = 128 + WTERMSIG(status);
  }
  finalize_output(result, options, saw_output, previous_was_newline, newline_count);
  if (result.truncated && !context.spill_dir.empty())
  {
    auto spill = write_spill_file(context, "bash", "txt", spill_buffer);
    if (!spill)
      return std::unexpected(std::move(spill.error()));
    result.spill_path = spill->path;
    result.spill_truncated = spill->truncated;
    if (auto progress = emit_tool_progress(context, "bash output spilled " + std::to_string(spill->bytes_written) + " bytes", "running"); !progress)
    {
      return std::unexpected(std::move(progress.error()));
    }
  }
  if (result.total_bytes > 0)
  {
    if (auto progress = emit_tool_progress(context, "bash completed with " + std::to_string(result.total_bytes) + " output bytes", "completed"); !progress)
    {
      return std::unexpected(std::move(progress.error()));
    }
  }
  return result;
}

}  // namespace ava::tools
