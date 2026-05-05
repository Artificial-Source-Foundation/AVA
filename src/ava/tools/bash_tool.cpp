#include "ava/tools/bash_tool.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ava/tools/bash_tool_support.h"
#include "ava/tools/spill_files.h"

namespace ava::tools {

ava::core::Result<BashResult> run_bash(ToolContext const& context, std::string_view command, BashOptions options)
{
  if (command.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command must not be empty");
    return std::unexpected(std::move(error));
  }
  auto const tool_name = context.permission_tool_name.empty() ? std::string("bash") : context.permission_tool_name;
  if (auto permission = ensure_permission(context, ava::permissions::Operation::RunCommand, context.workspace_dir,
                                          command, tool_name, "command requires permission");
      !permission) {
    return std::unexpected(permission.error());
  }
  if (detail::is_bash_canceled(context)) {
    BashResult result;
    result.exit_code = -1;
    result.canceled = true;
    return result;
  }

  auto argv_strings = detail::parse_command_argv(command);
  if (!argv_strings) {
    return std::unexpected(argv_strings.error());
  }
  std::vector<char*> argv;
  argv.reserve(argv_strings->size() + 1);
  for (auto& arg : *argv_strings) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  auto const workspace_text = context.workspace_dir.string();

  auto pipe_result = detail::make_pipe();
  if (!pipe_result) {
    return std::unexpected(pipe_result.error());
  }
  detail::UniqueFd read_fd((*pipe_result)[0]);
  detail::UniqueFd write_fd((*pipe_result)[1]);

  pid_t const pid = fork();
  if (pid < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to fork process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }

  if (pid == 0) {
    setpgid(0, 0);
    close(read_fd.get());
    dup2(write_fd.get(), STDOUT_FILENO);
    dup2(write_fd.get(), STDERR_FILENO);
    close(write_fd.get());
    if (chdir(workspace_text.c_str()) != 0) {
      _exit(127);
    }
    if (setenv("PATH", detail::kTrustedExecPath, 1) != 0) {
      _exit(127);
    }
    detail::close_nonstandard_fds();
    execvp(argv[0], argv.data());
    _exit(127);
  }

  bool const can_signal_group = setpgid(pid, pid) == 0 || errno == EACCES;
  int status = 0;
  write_fd.reset();
  int const flags = fcntl(read_fd.get(), F_GETFL, 0);
  if (flags < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read process pipe flags");
    error.with_context("command", std::string(command));
    error.with_context("cause", std::strerror(errno));
    detail::signal_process(pid, can_signal_group, SIGKILL);
    detail::waitpid_retry(pid, &status, 0);
    return std::unexpected(std::move(error));
  }
  if (fcntl(read_fd.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set process pipe nonblocking");
    error.with_context("command", std::string(command));
    error.with_context("cause", std::strerror(errno));
    detail::signal_process(pid, can_signal_group, SIGKILL);
    detail::waitpid_retry(pid, &status, 0);
    return std::unexpected(std::move(error));
  }

  BashResult result;
  SpillBuffer spill_buffer;
  bool running = true;
  auto const deadline = std::chrono::steady_clock::now() + options.timeout;
  auto last_progress = std::chrono::steady_clock::now();
  std::size_t next_progress_bytes = detail::kBashProgressByteInterval;
  std::array<char, 4096> buffer{};

  auto const maybe_emit_progress = [&]() -> ava::core::VoidResult {
    if (!context.progress_sink) return {};
    auto const now = std::chrono::steady_clock::now();
    if (result.total_bytes < next_progress_bytes && now - last_progress < detail::kBashProgressTimeInterval) return {};
    while (result.total_bytes >= next_progress_bytes) next_progress_bytes += detail::kBashProgressByteInterval;
    last_progress = now;
    return emit_tool_progress(context, "bash output " + std::to_string(result.total_bytes) + " bytes", "running");
  };

  while (running) {
    while (true) {
      auto const bytes = detail::read_retry(read_fd.get(), buffer.data(), buffer.size());
      if (bytes > 0) {
        std::string_view const chunk(buffer.data(), static_cast<std::size_t>(bytes));
        spill_buffer.append(chunk);
        detail::append_tail(result, chunk, options.max_bytes);
        if (auto progress = maybe_emit_progress(); !progress) {
          detail::signal_process(pid, can_signal_group, SIGKILL);
          detail::waitpid_retry(pid, &status, 0);
          return std::unexpected(std::move(progress.error()));
        }
        continue;
      }
      if (bytes == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      auto error = detail::pipe_read_error(command);
      detail::signal_process(pid, can_signal_group, SIGKILL);
      detail::waitpid_retry(pid, &status, 0);
      return std::unexpected(std::move(error));
    }

    if (detail::is_bash_canceled(context)) {
      result.canceled = true;
      if (auto stopped = detail::stop_process(pid, can_signal_group, status, command); !stopped) {
        return std::unexpected(std::move(stopped.error()));
      }
      running = false;
      break;
    }

    pid_t const waited = detail::waitpid_retry(pid, &status, WNOHANG);
    if (waited == pid) {
      running = false;
      break;
    }
    if (waited < 0) {
      return std::unexpected(detail::waitpid_error(command));
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      result.timed_out = true;
      if (auto stopped = detail::stop_process(pid, can_signal_group, status, command); !stopped) {
        return std::unexpected(std::move(stopped.error()));
      }
      running = false;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  while (true) {
    auto const bytes = detail::read_retry(read_fd.get(), buffer.data(), buffer.size());
    if (bytes <= 0) {
      if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        auto error = detail::pipe_read_error(command);
        return std::unexpected(std::move(error));
      }
      break;
    }
    std::string_view const chunk(buffer.data(), static_cast<std::size_t>(bytes));
    spill_buffer.append(chunk);
    detail::append_tail(result, chunk, options.max_bytes);
    if (auto progress = maybe_emit_progress(); !progress) return std::unexpected(std::move(progress.error()));
  }
  read_fd.reset();

  if (result.timed_out || result.canceled) {
    result.exit_code = -1;
  } else if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }
  if (result.truncated && !context.spill_dir.empty()) {
    auto spill = write_spill_file(context, "bash", "txt", spill_buffer);
    if (!spill) return std::unexpected(std::move(spill.error()));
    result.spill_path = spill->path;
    result.spill_truncated = spill->truncated;
    if (auto progress = emit_tool_progress(
            context, "bash output spilled " + std::to_string(spill->bytes_written) + " bytes", "running");
        !progress) {
      return std::unexpected(std::move(progress.error()));
    }
  }
  if (result.total_bytes > 0) {
    if (auto progress = emit_tool_progress(
            context, "bash completed with " + std::to_string(result.total_bytes) + " output bytes", "completed");
        !progress) {
      return std::unexpected(std::move(progress.error()));
    }
  }
  return result;
}

}  // namespace ava::tools
