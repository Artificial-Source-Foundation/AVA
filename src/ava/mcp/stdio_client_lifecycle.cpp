#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ava/mcp/process_support.h"
#include "ava/mcp/stdio_client.h"
#include "ava/mcp/stdio_support.h"

namespace ava::mcp {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

using detail::child_working_dir;
using detail::close_fd;
using detail::close_nonstandard_fds;
using detail::errno_error;
using detail::make_mcp_pipe;
using detail::mcp_argv;
using detail::set_child_process_group;
using detail::UniqueFd;
using detail::waitpid_retry;

}  // namespace

ava::core::VoidResult McpStdioClient::launch()
{
  auto stdin_pipe = make_mcp_pipe(server_);
  if (!stdin_pipe) return std::unexpected(std::move(stdin_pipe.error()));
  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);

  auto stdout_pipe = make_mcp_pipe(server_);
  if (!stdout_pipe) return std::unexpected(std::move(stdout_pipe.error()));
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);

  auto stderr_pipe = make_mcp_pipe(server_);
  if (!stderr_pipe) return std::unexpected(std::move(stderr_pipe.error()));
  UniqueFd stderr_read((*stderr_pipe)[0]);
  UniqueFd stderr_write((*stderr_pipe)[1]);

  auto argv_strings = mcp_argv(server_);
  std::vector<char*> argv;
  argv.reserve(argv_strings.size() + 1);
  for (auto& arg : argv_strings) argv.push_back(arg.data());
  argv.push_back(nullptr);

  auto const cwd = child_working_dir(options_).string();
  pid_t const pid = fork();
  if (pid < 0) return std::unexpected(errno_error("failed to fork MCP server process", server_));

  if (pid == 0) {
    setpgid(0, 0);
    stdin_write.reset();
    stdout_read.reset();
    stderr_read.reset();
    if (dup2(stdin_read.get(), STDIN_FILENO) < 0) _exit(127);
    if (dup2(stdout_write.get(), STDOUT_FILENO) < 0) _exit(127);
    if (dup2(stderr_write.get(), STDERR_FILENO) < 0) _exit(127);
    stdin_read.reset();
    stdout_write.reset();
    stderr_write.reset();
    if (chdir(cwd.c_str()) != 0) _exit(127);
    if (setenv("PATH", kTrustedExecPath, 1) != 0) _exit(127);
    close_nonstandard_fds();
    execvp(argv[0], argv.data());
    _exit(127);
  }

  pid_ = static_cast<int>(pid);
  can_signal_group_ = set_child_process_group(pid);
  stdin_read.reset();
  stdout_write.reset();
  stderr_write.reset();
  stdin_fd_ = stdin_write.release();
  stdout_fd_ = stdout_read.release();
  stderr_fd_ = stderr_read.release();

  if (auto nonblocking = set_pipe_nonblocking(stdin_fd_, "stdin"); !nonblocking) {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  if (auto nonblocking = set_pipe_nonblocking(stdout_fd_, "stdout"); !nonblocking) {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  if (auto nonblocking = set_pipe_nonblocking(stderr_fd_, "stderr"); !nonblocking) {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  return {};
}

ava::core::VoidResult McpStdioClient::reap_child()
{
  if (pid_ <= 0 || child_exited_) return {};
  int status = 0;
  auto const waited = waitpid_retry(static_cast<pid_t>(pid_), &status, WNOHANG);
  if (waited < 0) {
    if (errno == ECHILD) {
      pid_ = -1;
      child_exited_ = true;
      return {};
    }
    return std::unexpected(errno_error("failed to reap MCP server process", server_));
  }
  if (waited == 0) return {};
  child_status_ = status;
  child_exited_ = true;
  close_fd(stdin_fd_);
  return {};
}

ava::core::VoidResult McpStdioClient::set_pipe_nonblocking(int fd, std::string_view pipe_name)
{
  int const flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return std::unexpected(errno_error("failed to inspect MCP pipe flags", server_));
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    auto error = errno_error("failed to set MCP pipe nonblocking", server_);
    error.with_context("pipe", std::string(pipe_name));
    return std::unexpected(std::move(error));
  }
  return {};
}

void McpStdioClient::append_stderr(std::string_view chunk)
{
  if (chunk.size() >= options_.max_stderr_bytes) {
    stderr_tail_ = std::string(chunk.substr(chunk.size() - options_.max_stderr_bytes));
    stderr_truncated_ = true;
    return;
  }
  if (stderr_tail_.size() + chunk.size() > options_.max_stderr_bytes) {
    auto const drop = stderr_tail_.size() + chunk.size() - options_.max_stderr_bytes;
    stderr_tail_.erase(0, drop);
    stderr_truncated_ = true;
  }
  stderr_tail_.append(chunk);
}

void McpStdioClient::close_fds() noexcept
{
  close_fd(stdin_fd_);
  close_fd(stdout_fd_);
  close_fd(stderr_fd_);
}

void McpStdioClient::terminate_child() noexcept
{
  if (pid_ <= 0 || child_exited_) return;
  close_fd(stdin_fd_);
  if (can_signal_group_) {
    kill(-static_cast<pid_t>(pid_), SIGTERM);
  } else {
    kill(static_cast<pid_t>(pid_), SIGTERM);
  }
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    auto const waited = waitpid_retry(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (waited == pid_) {
      child_status_ = status;
      child_exited_ = true;
      return;
    }
    if (waited < 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (can_signal_group_) {
    kill(-static_cast<pid_t>(pid_), SIGKILL);
  } else {
    kill(static_cast<pid_t>(pid_), SIGKILL);
  }
  int status = 0;
  if (waitpid_retry(static_cast<pid_t>(pid_), &status, 0) == pid_) {
    child_status_ = status;
    child_exited_ = true;
  }
}

ava::core::VoidResult McpStdioClient::shutdown(std::chrono::milliseconds grace)
{
  close_fd(stdin_fd_);
  auto const deadline = std::chrono::steady_clock::now() + grace;
  while (pid_ > 0 && !child_exited_ && std::chrono::steady_clock::now() < deadline) {
    if (auto drained = drain_stdout(); !drained) return std::unexpected(std::move(drained.error()));
    if (auto drained = drain_stderr(); !drained) return std::unexpected(std::move(drained.error()));
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (!child_exited_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (pid_ > 0 && !child_exited_) terminate_child();
  close_fds();
  return {};
}

}  // namespace ava::mcp
