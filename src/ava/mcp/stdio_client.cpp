#include "ava/mcp/stdio_client.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <string_view>
#include <thread>
#include <utility>

#include "ava/core/json.h"
#include "ava/core/version.h"
#include "ava/mcp/process_support.h"
#include "ava/mcp/protocol.h"
#include "ava/mcp/stdio_support.h"
#include "ava/mcp/tool_list_parser.h"

namespace ava::mcp {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

using detail::canceled_error;
using detail::child_working_dir;
using detail::close_fd;
using detail::close_nonstandard_fds;
using detail::errno_error;
using detail::is_canceled;
using detail::make_mcp_pipe;
using detail::mcp_argv;
using detail::mcp_error;
using detail::protocol_error;
using detail::set_child_process_group;
using detail::UniqueFd;
using detail::waitpid_retry;

}  // namespace

McpStdioClient::McpStdioClient(McpServerConfig server, McpStdioClientOptions options)
    : server_(std::move(server)), options_(std::move(options))
{
}

McpStdioClient::~McpStdioClient()
{
  terminate_child();
  close_fds();
}

ava::core::Result<std::unique_ptr<McpStdioClient>> McpStdioClient::start(McpServerConfig server,
                                                                         McpStdioClientOptions options,
                                                                         CancelCallback cancel_requested)
{
  if (auto valid = detail::validate_start_request(server, options, cancel_requested); !valid) {
    return std::unexpected(std::move(valid.error()));
  }

  auto client = std::make_unique<McpStdioClient>(std::move(server), std::move(options));
  if (auto launched = client->launch(); !launched) return std::unexpected(std::move(launched.error()));
  if (auto initialized = client->initialize(cancel_requested); !initialized) {
    return std::unexpected(std::move(initialized.error()));
  }
  return client;
}

McpServerConfig const& McpStdioClient::server() const noexcept
{
  return server_;
}

McpInitialization const& McpStdioClient::initialization() const noexcept
{
  return initialization_;
}

std::string const& McpStdioClient::stderr_tail() const noexcept
{
  return stderr_tail_;
}

bool McpStdioClient::stderr_truncated() const noexcept
{
  return stderr_truncated_;
}

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

ava::core::VoidResult McpStdioClient::initialize(CancelCallback cancel_requested)
{
  std::string const params = mcp_initialize_params_json(ava::core::version::kFullVersion);
  auto response = request("initialize", params, options_.startup_timeout, "timed out waiting for MCP initialization",
                          cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  auto const server_info = ava::core::json::object_field(response->result_json, "serverInfo");
  auto const capabilities = ava::core::json::object_field(response->result_json, "capabilities").value_or("{}");
  if (!server_info || !ava::core::json::is_valid_object(capabilities)) {
    auto error = protocol_error("MCP initialize response is malformed", server_);
    error.with_context("response", response->raw_json.substr(0, 512));
    return std::unexpected(std::move(error));
  }
  initialization_ =
      McpInitialization{.server_name = ava::core::json::string_field(*server_info, "name").value_or(server_.id),
                        .server_version = ava::core::json::string_field(*server_info, "version").value_or(""),
                        .capabilities_json = capabilities,
                        .raw_json = response->raw_json};

  std::string const notification = mcp_initialized_notification_json();
  auto const deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  return write_message(notification, deadline, options_.request_timeout,
                       "timed out writing MCP initialized notification", cancel_requested);
}

ava::core::Result<std::vector<McpToolDescription>> McpStdioClient::list_tools(CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested)) {
    return std::unexpected(canceled_error("MCP tools/list canceled", server_));
  }
  auto response =
      request("tools/list", "{}", options_.request_timeout, "timed out waiting for MCP tools/list", cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return parse_mcp_tool_list_result(response->result_json, response->raw_json, server_);
}

ava::core::Result<McpToolCallResult> McpStdioClient::call_tool(std::string_view tool_name,
                                                               std::string_view arguments_json,
                                                               CancelCallback cancel_requested)
{
  if (!is_valid_mcp_tool_name(tool_name)) {
    return std::unexpected(mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP tool name is invalid", server_));
  }
  if (!ava::core::json::is_valid_object(arguments_json)) {
    auto error =
        mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP tool arguments must be a JSON object", server_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("MCP tools/call canceled", server_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }
  std::string const params = mcp_tool_call_params_json(tool_name, arguments_json);
  auto response =
      request("tools/call", params, options_.request_timeout, "timed out waiting for MCP tools/call", cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return McpToolCallResult{.is_error = mcp_bool_field(response->result_json, "isError").value_or(false),
                           .content = mcp_text_content_from_result(response->result_json),
                           .raw_json = response->raw_json};
}

ava::core::Result<McpStdioClient::JsonRpcResponse> McpStdioClient::request(std::string_view method,
                                                                           std::string_view params_json,
                                                                           std::chrono::milliseconds timeout,
                                                                           std::string_view timeout_message,
                                                                           CancelCallback cancel_requested)
{
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  auto const request_id = "ava_mcp_" + std::to_string(next_request_id_++);
  std::string const request_json = mcp_request_json(request_id, method, params_json);
  if (auto written = write_message(request_json, deadline, timeout, "timed out writing MCP request", cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  while (true) {
    auto message =
        read_message(deadline, timeout, timeout_message, "MCP server closed stdout before response", cancel_requested);
    if (!message) return std::unexpected(std::move(message.error()));
    if (!mcp_json_depth_within_limit(*message, kMcpMaxJsonDepth) || !ava::core::json::is_valid_object(*message)) {
      auto error = protocol_error("MCP response is not a valid JSON object", server_);
      error.with_context("response", message->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    auto const id = mcp_response_id(*message);
    if (!id) continue;
    if (*id != request_id) {
      auto error = protocol_error("MCP response id did not match request", server_);
      error.with_context("expected", request_id);
      error.with_context("actual", *id);
      return std::unexpected(std::move(error));
    }
    if (auto error_json = ava::core::json::object_field(*message, "error")) {
      auto error = protocol_error(mcp_error_message_from_response(*error_json).value_or("MCP request failed"), server_);
      error.with_context("method", std::string(method));
      error.with_context("mcp_error", error_json->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    auto result_json = ava::core::json::object_field(*message, "result");
    if (!result_json) {
      auto error = protocol_error("MCP response is missing result object", server_);
      error.with_context("method", std::string(method));
      error.with_context("response", message->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    return JsonRpcResponse{.result_json = std::move(*result_json), .raw_json = std::move(*message)};
  }
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
