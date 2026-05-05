#include "ava/plugin/runner.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ava/core/json.h"
#include "ava/plugin/process_support.h"
#include "ava/plugin/protocol.h"

namespace ava::plugin {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
constexpr int kMaxDrainReadsPerPoll = 16;

using detail::close_fd;
using detail::close_nonstandard_fds;
using detail::exit_detail;
using detail::make_plugin_pipe;
using detail::read_retry;
using detail::remaining_ms;
using detail::ScopedSignalIgnore;
using detail::set_child_process_group;
using detail::UniqueFd;
using detail::waitpid_retry;
using detail::write_retry;

ava::core::Error plugin_error(ava::core::ErrorCategory category, std::string message, PluginManifest const& manifest)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("plugin", manifest.id);
  if (!manifest.path.empty()) error.with_context("manifest", manifest.path.string());
  return error;
}

ava::core::Error errno_error(std::string message, PluginManifest const& manifest)
{
  auto error = plugin_error(ava::core::ErrorCategory::Io, std::move(message), manifest);
  error.with_context("cause", std::strerror(errno));
  return error;
}

ava::core::Error protocol_error(std::string message, PluginManifest const& manifest)
{
  return plugin_error(ava::core::ErrorCategory::Tool, std::move(message), manifest);
}

bool is_canceled(CancelCallback const& cancel_requested)
{
  return cancel_requested && cancel_requested();
}

ava::core::Error canceled_error(std::string message, PluginManifest const& manifest)
{
  auto error = plugin_error(ava::core::ErrorCategory::Unknown, std::move(message), manifest);
  error.with_context("canceled", "true");
  return error;
}

std::vector<std::string> plugin_argv(PluginManifest const& manifest)
{
  std::vector<std::string> argv;
  argv.reserve(manifest.entrypoint.args.size() + 1);
  argv.push_back(manifest.entrypoint.command);
  argv.insert(argv.end(), manifest.entrypoint.args.begin(), manifest.entrypoint.args.end());
  return argv;
}

std::filesystem::path child_working_dir(PluginManifest const& manifest, PluginRunnerOptions const& options)
{
  if (!manifest.directory.empty()) return manifest.directory;
  if (!options.workspace_dir.empty()) return options.workspace_dir;
  return std::filesystem::current_path();
}

}  // namespace

PluginProcess::PluginProcess(PluginManifest manifest, PluginRunnerOptions options)
    : manifest_(std::move(manifest)),
      options_(std::move(options)),
      stdout_buffer_(options_.max_record_bytes),
      stderr_tail_(options_.max_stderr_bytes)
{
}

PluginProcess::~PluginProcess()
{
  terminate_child();
  close_fds();
}

ava::core::Result<std::unique_ptr<PluginProcess>> PluginProcess::start(PluginManifest manifest,
                                                                       PluginRunnerOptions options,
                                                                       CancelCallback cancel_requested)
{
  if (manifest.entrypoint.command.empty()) {
    return std::unexpected(plugin_error(ava::core::ErrorCategory::InvalidArgument,
                                        "plugin entrypoint command must not be empty", manifest));
  }
  if (options.workspace_dir.empty()) options.workspace_dir = std::filesystem::current_path();
  if (options.startup_timeout < std::chrono::milliseconds(50) || options.startup_timeout > std::chrono::seconds(30)) {
    auto error =
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin startup timeout is out of bounds", manifest);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.request_timeout < std::chrono::milliseconds(50) || options.request_timeout > std::chrono::seconds(30)) {
    auto error =
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin request timeout is out of bounds", manifest);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.max_record_bytes == 0 || options.max_stderr_bytes == 0) {
    return std::unexpected(plugin_error(ava::core::ErrorCategory::InvalidArgument,
                                        "plugin runner byte limits must be non-zero", manifest));
  }
  if (is_canceled(cancel_requested)) {
    return std::unexpected(canceled_error("plugin startup canceled", manifest));
  }

  auto process = std::unique_ptr<PluginProcess>(new PluginProcess(std::move(manifest), std::move(options)));
  if (auto launched = process->launch(); !launched) return std::unexpected(std::move(launched.error()));
  if (auto initialized = process->initialize(cancel_requested); !initialized) {
    return std::unexpected(std::move(initialized.error()));
  }
  return process;
}

PluginManifest const& PluginProcess::manifest() const noexcept
{
  return manifest_;
}

PluginInitialization const& PluginProcess::initialization() const noexcept
{
  return initialization_;
}

std::string const& PluginProcess::stderr_tail() const noexcept
{
  return stderr_tail_.text();
}

bool PluginProcess::stderr_truncated() const noexcept
{
  return stderr_tail_.truncated();
}

ava::core::VoidResult PluginProcess::launch()
{
  auto stdin_pipe = make_plugin_pipe(manifest_);
  if (!stdin_pipe) return std::unexpected(std::move(stdin_pipe.error()));
  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);

  auto stdout_pipe = make_plugin_pipe(manifest_);
  if (!stdout_pipe) return std::unexpected(std::move(stdout_pipe.error()));
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);

  auto stderr_pipe = make_plugin_pipe(manifest_);
  if (!stderr_pipe) return std::unexpected(std::move(stderr_pipe.error()));
  UniqueFd stderr_read((*stderr_pipe)[0]);
  UniqueFd stderr_write((*stderr_pipe)[1]);

  auto argv_strings = plugin_argv(manifest_);
  std::vector<char*> argv;
  argv.reserve(argv_strings.size() + 1);
  for (auto& arg : argv_strings) argv.push_back(arg.data());
  argv.push_back(nullptr);

  auto const cwd = child_working_dir(manifest_, options_).string();
  pid_t const pid = fork();
  if (pid < 0) return std::unexpected(errno_error("failed to fork plugin process", manifest_));

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

ava::core::VoidResult PluginProcess::initialize(CancelCallback cancel_requested)
{
  auto const deadline = std::chrono::steady_clock::now() + options_.startup_timeout;
  std::string const request =
      plugin_initialize_request_json(kPluginApiVersion, manifest_.id, options_.workspace_dir.string());
  if (auto written = write_record(request, deadline, options_.startup_timeout,
                                  "timed out writing plugin initialization", cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.startup_timeout, "timed out waiting for plugin initialization",
                            "plugin process closed stdout before initialization", cancel_requested);
  if (!record) return std::unexpected(std::move(record.error()));
  auto initialized = parse_plugin_initialized_response(*record);
  if (!initialized) {
    auto error = protocol_error("plugin initialize response is malformed or unsupported", manifest_);
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  initialization_ = std::move(*initialized);
  return {};
}

ava::core::Result<PluginToolCallResult> PluginProcess::call_tool(std::string_view tool_name,
                                                                 std::string_view arguments_json,
                                                                 std::string_view call_id,
                                                                 CancelCallback cancel_requested)
{
  if (tool_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin tool name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(arguments_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin tool arguments must be a JSON object",
                              manifest_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("plugin tool call canceled", manifest_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }

  auto const deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  std::string const request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_tool_" + std::string(call_id);
  std::string request =
      plugin_tool_call_request_json(request_id, tool_name, arguments_json, call_id, options_.workspace_dir.string());
  if (auto written = write_record(request, deadline, options_.request_timeout, "timed out writing plugin tool request",
                                  cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin tool result",
                            "plugin process closed stdout before plugin tool result", cancel_requested);
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_plugin_tool_result_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin tool result is malformed", manifest_);
    error.with_context("tool", std::string(tool_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

ava::core::Result<PluginCommandCallResult> PluginProcess::call_command(std::string_view command_name,
                                                                       std::string_view arguments_json,
                                                                       std::string_view call_id,
                                                                       CancelCallback cancel_requested)
{
  if (command_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin command name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(arguments_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument,
                              "plugin command arguments must be a JSON object", manifest_);
    error.with_context("command", std::string(command_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("plugin command call canceled", manifest_);
    error.with_context("command", std::string(command_name));
    return std::unexpected(std::move(error));
  }

  auto const deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  std::string const request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_command_" + std::string(call_id);
  std::string request = plugin_command_call_request_json(request_id, command_name, arguments_json, call_id,
                                                         options_.workspace_dir.string());
  if (auto written = write_record(request, deadline, options_.request_timeout,
                                  "timed out writing plugin command request", cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin command result",
                            "plugin process closed stdout before plugin command result", cancel_requested);
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_plugin_command_result_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin command result is malformed", manifest_);
    error.with_context("command", std::string(command_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

ava::core::Result<PluginEventObserveResult> PluginProcess::observe_event(std::string_view event_name,
                                                                         std::string_view payload_json,
                                                                         std::string_view call_id,
                                                                         CancelCallback cancel_requested)
{
  if (event_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin event name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(payload_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin event payload must be a JSON object",
                              manifest_);
    error.with_context("event", std::string(event_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("plugin event observation canceled", manifest_);
    error.with_context("event", std::string(event_name));
    return std::unexpected(std::move(error));
  }

  auto const deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  std::string const request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_event_" + std::string(call_id);
  std::string request =
      plugin_event_observe_request_json(request_id, event_name, payload_json, call_id, options_.workspace_dir.string());
  if (auto written = write_record(request, deadline, options_.request_timeout, "timed out writing plugin event request",
                                  cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin event response",
                            "plugin process closed stdout before plugin event response", cancel_requested);
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_plugin_event_observed_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin event response is malformed", manifest_);
    error.with_context("event", std::string(event_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

ava::core::VoidResult PluginProcess::write_record(std::string_view record,
                                                  std::chrono::steady_clock::time_point deadline,
                                                  std::chrono::milliseconds timeout, std::string_view timeout_message,
                                                  CancelCallback cancel_requested)
{
  if (stdin_fd_ < 0) return std::unexpected(protocol_error("plugin stdin is closed", manifest_));
  std::string const frame = std::string(record) + '\n';
  std::size_t offset = 0;
  ScopedSignalIgnore const ignore_sigpipe(SIGPIPE);
  while (offset < frame.size()) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("plugin request canceled", manifest_));
    }
    auto const bytes = write_retry(stdin_fd_, frame.data() + offset, frame.size() - offset);
    if (bytes > 0) {
      offset += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes == 0) return std::unexpected(errno_error("failed to write plugin request", manifest_));
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (auto writable = wait_for_writable(deadline, timeout, timeout_message, cancel_requested); !writable) {
        return std::unexpected(std::move(writable.error()));
      }
      continue;
    }
    return std::unexpected(errno_error("failed to write plugin request", manifest_));
  }
  return {};
}

ava::core::Result<std::string> PluginProcess::read_record(std::chrono::steady_clock::time_point deadline,
                                                          std::chrono::milliseconds timeout,
                                                          std::string_view timeout_message,
                                                          std::string_view closed_message,
                                                          CancelCallback cancel_requested)
{
  while (true) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("plugin request canceled", manifest_));
    }
    if (auto record = stdout_buffer_.take_record()) {
      if (record->size() > options_.max_record_bytes) {
        auto error = protocol_error("plugin protocol record exceeds size cap", manifest_);
        error.with_context("max_bytes", std::to_string(options_.max_record_bytes));
        terminate_child();
        return std::unexpected(std::move(error));
      }
      return *record;
    }
    if (stdout_buffer_.exceeds_limit()) {
      auto error = protocol_error("plugin protocol record exceeds size cap", manifest_);
      error.with_context("max_bytes", std::to_string(options_.max_record_bytes));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    if (stdout_fd_ < 0) {
      auto const reap_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
      do {
        if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
        if (child_exited_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (std::chrono::steady_clock::now() < reap_deadline);
      auto error = protocol_error(
          stdout_buffer_.empty() ? std::string(closed_message) : "plugin protocol record ended without newline",
          manifest_);
      if (child_exited_) error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.text().empty()) error.with_context("stderr_tail", stderr_tail_.text());
      return std::unexpected(std::move(error));
    }
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline) {
      auto error = protocol_error(std::string(timeout_message), manifest_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      if (!stderr_tail_.text().empty()) error.with_context("stderr_tail", stderr_tail_.text());
      terminate_child();
      return std::unexpected(std::move(error));
    }

    std::array<pollfd, 2> fds{pollfd{.fd = stdout_fd_, .events = POLLIN, .revents = 0},
                              pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    int const timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    int const polled = poll(fds.data(), fds.size(), timeout);
    if (polled < 0) {
      if (errno == EINTR) continue;
      return std::unexpected(errno_error("failed to poll plugin process pipes", manifest_));
    }
    if (polled == 0) continue;
    if (fds[1].revents != 0) {
      if (auto drained = drain_stderr(); !drained) return std::unexpected(std::move(drained.error()));
    }
    if (fds[0].revents != 0) {
      if (auto drained = drain_stdout(); !drained) return std::unexpected(std::move(drained.error()));
    }
  }
}

ava::core::VoidResult PluginProcess::wait_for_writable(std::chrono::steady_clock::time_point deadline,
                                                       std::chrono::milliseconds timeout,
                                                       std::string_view timeout_message,
                                                       CancelCallback cancel_requested)
{
  while (true) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("plugin request canceled", manifest_));
    }
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline) {
      auto error = protocol_error(std::string(timeout_message), manifest_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    std::array<pollfd, 2> fds{pollfd{.fd = stdin_fd_, .events = POLLOUT, .revents = 0},
                              pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    int const timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    int const polled = poll(fds.data(), fds.size(), timeout);
    if (polled < 0) {
      if (errno == EINTR) continue;
      return std::unexpected(errno_error("failed to poll plugin request pipe", manifest_));
    }
    if (fds[1].revents != 0) {
      if (auto drained = drain_stderr(); !drained) return std::unexpected(std::move(drained.error()));
    }
    if ((fds[0].revents & POLLOUT) != 0) return {};
    if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      auto error = protocol_error("plugin request pipe closed", manifest_);
      if (child_exited_) error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.text().empty()) error.with_context("stderr_tail", stderr_tail_.text());
      return std::unexpected(std::move(error));
    }
  }
}

ava::core::VoidResult PluginProcess::drain_stdout()
{
  if (stdout_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    auto const bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      stdout_buffer_.append(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
      if (stdout_buffer_.exceeds_limit()) {
        auto error = protocol_error("plugin protocol record exceeds size cap", manifest_);
        error.with_context("max_bytes", std::to_string(options_.max_record_bytes));
        terminate_child();
        return std::unexpected(std::move(error));
      }
      ++reads;
      if (reads >= kMaxDrainReadsPerPoll) return {};
      continue;
    }
    if (bytes == 0) {
      close_fd(stdout_fd_);
      return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return {};
    return std::unexpected(errno_error("failed to read plugin stdout", manifest_));
  }
}

ava::core::VoidResult PluginProcess::drain_stderr()
{
  if (stderr_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    auto const bytes = read_retry(stderr_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      stderr_tail_.append(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
      ++reads;
      if (reads >= kMaxDrainReadsPerPoll) return {};
      continue;
    }
    if (bytes == 0) {
      close_fd(stderr_fd_);
      return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return {};
    return std::unexpected(errno_error("failed to read plugin stderr", manifest_));
  }
}

ava::core::VoidResult PluginProcess::reap_child()
{
  if (pid_ < 0) return {};
  int status = 0;
  pid_t const waited = waitpid_retry(pid_, &status, WNOHANG);
  if (waited == 0) return {};
  if (waited == pid_) {
    child_status_ = status;
    child_exited_ = true;
    pid_ = -1;
    return {};
  }
  if (errno == ECHILD) {
    pid_ = -1;
    return {};
  }
  return std::unexpected(errno_error("failed to wait for plugin process", manifest_));
}

ava::core::VoidResult PluginProcess::set_pipe_nonblocking(int fd, std::string_view pipe_name)
{
  int const flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    auto error = errno_error("failed to configure plugin process pipe", manifest_);
    error.with_context("pipe", std::string(pipe_name));
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult PluginProcess::shutdown(std::chrono::milliseconds grace)
{
  close_fd(stdin_fd_);
  auto const deadline = std::chrono::steady_clock::now() + grace;
  while (pid_ >= 0 && std::chrono::steady_clock::now() < deadline) {
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (pid_ < 0) break;
    drain_available_noexcept();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (pid_ >= 0) terminate_child();
  drain_available_noexcept();
  close_fds();
  return {};
}

void PluginProcess::close_fds() noexcept
{
  close_fd(stdin_fd_);
  close_fd(stdout_fd_);
  close_fd(stderr_fd_);
}

void PluginProcess::terminate_child() noexcept
{
  if (pid_ < 0) return;
  pid_t const pid = static_cast<pid_t>(pid_);
  pid_t const target = can_signal_group_ ? -pid : pid;
  kill(target, SIGTERM);
  for (int attempt = 0; attempt < 5; ++attempt) {
    int status = 0;
    pid_t const waited = waitpid_retry(pid, &status, WNOHANG);
    if (waited == pid || waited < 0) {
      if (waited == pid) {
        child_status_ = status;
        child_exited_ = true;
      }
      pid_ = -1;
      return;
    }
    drain_available_noexcept();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  kill(target, SIGKILL);
  for (int attempt = 0; attempt < 25; ++attempt) {
    int status = 0;
    pid_t const waited = waitpid_retry(pid, &status, WNOHANG);
    if (waited == pid || waited < 0) {
      if (waited == pid) {
        child_status_ = status;
        child_exited_ = true;
      }
      pid_ = -1;
      return;
    }
    drain_available_noexcept();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  pid_ = -1;
}

void PluginProcess::drain_available_noexcept() noexcept
{
  try {
    if (stdout_fd_ >= 0) {
      std::array<char, 4096> buffer{};
      for (int reads = 0; reads < kMaxDrainReadsPerPoll; ++reads) {
        auto const bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
        if (bytes > 0) {
          stdout_buffer_.append(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
          stdout_buffer_.trim_front_to_limit();
          continue;
        }
        if (bytes == 0) close_fd(stdout_fd_);
        break;
      }
    }
    if (stderr_fd_ >= 0) {
      std::array<char, 4096> buffer{};
      for (int reads = 0; reads < kMaxDrainReadsPerPoll; ++reads) {
        auto const bytes = read_retry(stderr_fd_, buffer.data(), buffer.size());
        if (bytes > 0) {
          stderr_tail_.append(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
          continue;
        }
        if (bytes == 0) close_fd(stderr_fd_);
        break;
      }
    }
  } catch (...) {
  }
}

}  // namespace ava::plugin
