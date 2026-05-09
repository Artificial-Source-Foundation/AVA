#include "ava/mcp/protocol.h"
#include "ava/mcp/stdio_client.h"
#include "ava/mcp/stdio_client_support.h"
#include "ava/core/json.h"
#include "ava/core/version.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

namespace ava::mcp {
namespace {

constexpr int kMaxDrainReadsPerPoll = 16;

}  // namespace

McpStdioClient::McpStdioClient(McpServerConfig server, McpStdioClientOptions options) : server_(std::move(server)), options_(std::move(options))
{
}

McpStdioClient::~McpStdioClient()
{
  terminate_child();
  close_fds();
}

ava::core::Result<std::unique_ptr<McpStdioClient>> McpStdioClient::start(McpServerConfig server, McpStdioClientOptions options, CancelCallback cancel_requested)
{
  if (server.command.empty())
  {
    return std::unexpected(mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP server command must not be empty", server));
  }
  if (options.workspace_dir.empty())
    options.workspace_dir = std::filesystem::current_path();
  if (options.startup_timeout < std::chrono::milliseconds(50) || options.startup_timeout > std::chrono::seconds(30))
  {
    auto error = mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP startup timeout is out of bounds", server);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.request_timeout < std::chrono::milliseconds(50) || options.request_timeout > std::chrono::seconds(30))
  {
    auto error = mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP request timeout is out of bounds", server);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.max_message_bytes == 0 || options.max_stderr_bytes == 0)
  {
    return std::unexpected(mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP client byte limits must be non-zero", server));
  }
  if (is_canceled(cancel_requested))
  {
    return std::unexpected(canceled_error("MCP startup canceled", server));
  }

  auto client = std::make_unique<McpStdioClient>(std::move(server), std::move(options));
  if (auto launched = client->launch(); !launched)
    return std::unexpected(std::move(launched.error()));
  if (auto initialized = client->initialize(cancel_requested); !initialized)
  {
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
  auto stdin_pipe = make_pipe(server_);
  if (!stdin_pipe)
    return std::unexpected(std::move(stdin_pipe.error()));
  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);

  auto stdout_pipe = make_pipe(server_);
  if (!stdout_pipe)
    return std::unexpected(std::move(stdout_pipe.error()));
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);

  auto stderr_pipe = make_pipe(server_);
  if (!stderr_pipe)
    return std::unexpected(std::move(stderr_pipe.error()));
  UniqueFd stderr_read((*stderr_pipe)[0]);
  UniqueFd stderr_write((*stderr_pipe)[1]);

  auto argv_strings = mcp_argv(server_);
  std::vector<char*> argv;
  argv.reserve(argv_strings.size() + 1);
  for (auto& arg : argv_strings) argv.push_back(arg.data());
  argv.push_back(nullptr);

  auto const cwd = child_working_dir(options_).string();
  pid_t const pid = fork();
  if (pid < 0)
    return std::unexpected(errno_error("failed to fork MCP server process", server_));

  if (pid == 0)
  {
    setpgid(0, 0);
    stdin_write.reset();
    stdout_read.reset();
    stderr_read.reset();
    if (dup2(stdin_read.get(), STDIN_FILENO) < 0)
      _exit(127);
    if (dup2(stdout_write.get(), STDOUT_FILENO) < 0)
      _exit(127);
    if (dup2(stderr_write.get(), STDERR_FILENO) < 0)
      _exit(127);
    stdin_read.reset();
    stdout_write.reset();
    stderr_write.reset();
    if (chdir(cwd.c_str()) != 0)
      _exit(127);
    if (setenv("PATH", kTrustedExecPath, 1) != 0)
      _exit(127);
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

  if (auto nonblocking = set_pipe_nonblocking(stdin_fd_, "stdin"); !nonblocking)
  {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  if (auto nonblocking = set_pipe_nonblocking(stdout_fd_, "stdout"); !nonblocking)
  {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  if (auto nonblocking = set_pipe_nonblocking(stderr_fd_, "stderr"); !nonblocking)
  {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  return {};
}

ava::core::VoidResult McpStdioClient::initialize(CancelCallback cancel_requested)
{
  std::string const params =
      "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"ava\","
      "\"version\":\"" +
      std::string(ava::core::version::kFullVersion) + "\"}}";
  auto response = request("initialize", params, options_.startup_timeout, "timed out waiting for MCP initialization", cancel_requested);
  if (!response)
    return std::unexpected(std::move(response.error()));
  auto const server_info = ava::core::json::object_field(response->result_json, "serverInfo");
  auto const capabilities = ava::core::json::object_field(response->result_json, "capabilities").value_or("{}");
  if (!server_info || !ava::core::json::is_valid_object(capabilities))
  {
    auto error = protocol_error("MCP initialize response is malformed", server_);
    error.with_context("response", response->raw_json.substr(0, 512));
    return std::unexpected(std::move(error));
  }
  initialization_ = McpInitialization{.server_name = ava::core::json::string_field(*server_info, "name").value_or(server_.id),
                                      .server_version = ava::core::json::string_field(*server_info, "version").value_or(""),
                                      .capabilities_json = capabilities,
                                      .raw_json = response->raw_json};

  std::string const notification = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
  auto const deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  return write_message(notification, deadline, options_.request_timeout, "timed out writing MCP initialized notification", cancel_requested);
}

ava::core::Result<std::vector<McpToolDescription>> McpStdioClient::list_tools(CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested))
  {
    return std::unexpected(canceled_error("MCP tools/list canceled", server_));
  }
  auto response = request("tools/list", "{}", options_.request_timeout, "timed out waiting for MCP tools/list", cancel_requested);
  if (!response)
    return std::unexpected(std::move(response.error()));
  auto const tools_start = ava::core::json::field_value_start(response->result_json, "tools");
  if (tools_start && (*tools_start >= response->result_json.size() || response->result_json[*tools_start] != '['))
  {
    auto error = protocol_error("MCP tools/list result has invalid tools field", server_);
    error.with_context("response", response->raw_json.substr(0, 512));
    return std::unexpected(std::move(error));
  }

  std::vector<McpToolDescription> tools;
  for (auto const& tool_json : ava::core::json::objects_in_array_field(response->result_json, "tools"))
  {
    auto name = ava::core::json::string_field(tool_json, "name");
    if (!name || !ava::mcp::is_valid_mcp_tool_name(*name))
    {
      auto error = protocol_error("MCP tool has invalid name", server_);
      error.with_context("response", tool_json.substr(0, 512));
      return std::unexpected(std::move(error));
    }
    auto input_schema = ava::core::json::object_field(tool_json, "inputSchema").value_or("{\"type\":\"object\"}");
    if (!ava::core::json::is_valid_object(input_schema))
    {
      auto error = protocol_error("MCP tool inputSchema is invalid", server_);
      error.with_context("tool", *name);
      return std::unexpected(std::move(error));
    }
    tools.push_back(McpToolDescription{.name = std::move(*name),
                                       .description = ava::core::json::string_field(tool_json, "description").value_or(""),
                                       .input_schema_json = std::move(input_schema)});
  }
  return tools;
}

ava::core::Result<McpToolCallResult> McpStdioClient::call_tool(std::string_view tool_name, std::string_view arguments_json, CancelCallback cancel_requested)
{
  if (!ava::mcp::is_valid_mcp_tool_name(tool_name))
  {
    return std::unexpected(mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP tool name is invalid", server_));
  }
  if (!ava::core::json::is_valid_object(arguments_json))
  {
    auto error = mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP tool arguments must be a JSON object", server_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested))
  {
    auto error = canceled_error("MCP tools/call canceled", server_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }
  std::string const params = "{\"name\":" + json_string(tool_name) + ",\"arguments\":" + std::string(arguments_json) + "}";
  auto response = request("tools/call", params, options_.request_timeout, "timed out waiting for MCP tools/call", cancel_requested);
  if (!response)
    return std::unexpected(std::move(response.error()));
  return McpToolCallResult{.is_error = mcp_bool_field(response->result_json, "isError").value_or(false),
                           .content = mcp_text_content_from_result(response->result_json),
                           .raw_json = response->raw_json};
}

ava::core::Result<std::vector<McpPromptDescription>> McpStdioClient::list_prompts(CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested))
  {
    return std::unexpected(canceled_error("MCP prompts/list canceled", server_));
  }
  auto response = request("prompts/list", "{}", options_.request_timeout, "timed out waiting for MCP prompts/list", cancel_requested);
  if (!response)
    return std::unexpected(std::move(response.error()));
  auto const prompts_start = ava::core::json::field_value_start(response->result_json, "prompts");
  if (prompts_start && (*prompts_start >= response->result_json.size() || response->result_json[*prompts_start] != '['))
  {
    auto error = protocol_error("MCP prompts/list result has invalid prompts field", server_);
    error.with_context("response", response->raw_json.substr(0, 512));
    return std::unexpected(std::move(error));
  }

  std::vector<McpPromptDescription> prompts;
  for (auto const& prompt_json : ava::core::json::objects_in_array_field(response->result_json, "prompts"))
  {
    auto name = ava::core::json::string_field(prompt_json, "name");
    if (!name || !ava::mcp::is_valid_mcp_tool_name(*name))
    {
      auto error = protocol_error("MCP prompt has invalid name", server_);
      error.with_context("response", prompt_json.substr(0, 512));
      return std::unexpected(std::move(error));
    }
    McpPromptDescription prompt{
        .name = std::move(*name), .description = ava::core::json::string_field(prompt_json, "description").value_or(""), .arguments = {}};
    for (auto const& argument_json : ava::core::json::objects_in_array_field(prompt_json, "arguments"))
    {
      auto argument_name = ava::core::json::string_field(argument_json, "name");
      if (!argument_name || !ava::mcp::is_valid_mcp_tool_name(*argument_name))
      {
        auto error = protocol_error("MCP prompt argument has invalid name", server_);
        error.with_context("prompt", prompt.name);
        error.with_context("response", argument_json.substr(0, 512));
        return std::unexpected(std::move(error));
      }
      prompt.arguments.push_back(McpPromptArgumentDescription{.name = std::move(*argument_name),
                                                              .description = ava::core::json::string_field(argument_json, "description").value_or(""),
                                                              .required = mcp_bool_field(argument_json, "required").value_or(false)});
    }
    prompts.push_back(std::move(prompt));
  }
  return prompts;
}

ava::core::Result<McpPromptGetResult> McpStdioClient::get_prompt(std::string_view prompt_name, std::string_view arguments_json, CancelCallback cancel_requested)
{
  if (!ava::mcp::is_valid_mcp_tool_name(prompt_name))
  {
    return std::unexpected(mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP prompt name is invalid", server_));
  }
  if (!ava::core::json::is_valid_object(arguments_json))
  {
    auto error = mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP prompt arguments must be a JSON object", server_);
    error.with_context("prompt", std::string(prompt_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested))
  {
    auto error = canceled_error("MCP prompts/get canceled", server_);
    error.with_context("prompt", std::string(prompt_name));
    return std::unexpected(std::move(error));
  }
  std::string const params = "{\"name\":" + json_string(prompt_name) + ",\"arguments\":" + std::string(arguments_json) + "}";
  auto response = request("prompts/get", params, options_.request_timeout, "timed out waiting for MCP prompts/get", cancel_requested);
  if (!response)
    return std::unexpected(std::move(response.error()));
  return McpPromptGetResult{.content = mcp_prompt_text_from_result(response->result_json), .raw_json = response->raw_json};
}

ava::core::Result<McpStdioClient::JsonRpcResponse> McpStdioClient::request(std::string_view method, std::string_view params_json,
                                                                           std::chrono::milliseconds timeout, std::string_view timeout_message,
                                                                           CancelCallback cancel_requested)
{
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  auto const request_id = "ava_mcp_" + std::to_string(next_request_id_++);
  std::string const request_json =
      "{\"jsonrpc\":\"2.0\",\"id\":" + json_string(request_id) + ",\"method\":" + json_string(method) + ",\"params\":" + std::string(params_json) + "}";
  if (auto written = write_message(request_json, deadline, timeout, "timed out writing MCP request", cancel_requested); !written)
  {
    return std::unexpected(std::move(written.error()));
  }

  while (true)
  {
    auto message = read_message(deadline, timeout, timeout_message, "MCP server closed stdout before response", cancel_requested);
    if (!message)
      return std::unexpected(std::move(message.error()));
    if (!mcp_json_depth_within_limit(*message, kMaxMcpJsonDepth) || !ava::core::json::is_valid_object(*message))
    {
      auto error = protocol_error("MCP response is not a valid JSON object", server_);
      error.with_context("response", message->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    auto const id = mcp_response_id(*message);
    if (!id)
      continue;
    if (*id != request_id)
    {
      auto error = protocol_error("MCP response id did not match request", server_);
      error.with_context("expected", request_id);
      error.with_context("actual", *id);
      return std::unexpected(std::move(error));
    }
    if (auto error_json = ava::core::json::object_field(*message, "error"))
    {
      auto error = protocol_error(mcp_error_message_from_response(*error_json).value_or("MCP request failed"), server_);
      error.with_context("method", std::string(method));
      error.with_context("mcp_error", error_json->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    auto result_json = ava::core::json::object_field(*message, "result");
    if (!result_json)
    {
      auto error = protocol_error("MCP response is missing result object", server_);
      error.with_context("method", std::string(method));
      error.with_context("response", message->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    return JsonRpcResponse{.result_json = std::move(*result_json), .raw_json = std::move(*message)};
  }
}

ava::core::VoidResult McpStdioClient::write_message(std::string_view message, std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                    std::string_view timeout_message, CancelCallback cancel_requested)
{
  if (stdin_fd_ < 0)
    return std::unexpected(protocol_error("MCP stdin is closed", server_));
  std::string const frame = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + std::string(message);
  std::size_t offset = 0;
  ScopedSignalIgnore const ignore_sigpipe(SIGPIPE);
  while (offset < frame.size())
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("MCP request canceled", server_));
    }
    auto const bytes = write_retry(stdin_fd_, frame.data() + offset, frame.size() - offset);
    if (bytes > 0)
    {
      offset += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes == 0)
      return std::unexpected(errno_error("failed to write MCP request", server_));
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      if (auto writable = wait_for_writable(deadline, timeout, timeout_message, cancel_requested); !writable)
      {
        return std::unexpected(std::move(writable.error()));
      }
      continue;
    }
    return std::unexpected(errno_error("failed to write MCP request", server_));
  }
  return {};
}

ava::core::Result<std::optional<std::string>> McpStdioClient::try_extract_message()
{
  auto const header_end = mcp_header_end_offset(stdout_buffer_);
  if (!header_end)
  {
    if (stdout_buffer_.size() > kMaxMcpHeaderBytes)
    {
      auto error = protocol_error("MCP message header exceeds size cap", server_);
      error.with_context("max_bytes", std::to_string(kMaxMcpHeaderBytes));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    return std::optional<std::string>{};
  }
  auto const header_size = *header_end;
  if (header_size > kMaxMcpHeaderBytes)
  {
    auto error = protocol_error("MCP message header exceeds size cap", server_);
    error.with_context("max_bytes", std::to_string(kMaxMcpHeaderBytes));
    terminate_child();
    return std::unexpected(std::move(error));
  }
  auto content_length = parse_mcp_content_length(std::string_view(stdout_buffer_).substr(0, header_size), server_, options_.max_message_bytes);
  if (!content_length)
  {
    terminate_child();
    return std::unexpected(std::move(content_length.error()));
  }
  if (stdout_buffer_.size() < header_size + *content_length)
    return std::optional<std::string>{};
  auto message = stdout_buffer_.substr(header_size, *content_length);
  stdout_buffer_.erase(0, header_size + *content_length);
  return std::optional<std::string>{std::move(message)};
}

ava::core::Result<std::string> McpStdioClient::read_message(std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                            std::string_view timeout_message, std::string_view closed_message, CancelCallback cancel_requested)
{
  while (true)
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("MCP request canceled", server_));
    }
    auto extracted = try_extract_message();
    if (!extracted)
      return std::unexpected(std::move(extracted.error()));
    if (*extracted)
      return std::move(**extracted);

    if (stdout_fd_ < 0)
    {
      auto const reap_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
      do
      {
        if (auto reaped = reap_child(); !reaped)
          return std::unexpected(std::move(reaped.error()));
        if (child_exited_)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (std::chrono::steady_clock::now() < reap_deadline);
      auto error = protocol_error(stdout_buffer_.empty() ? std::string(closed_message) : "MCP protocol message ended before full frame", server_);
      if (child_exited_)
        error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.empty())
        error.with_context("stderr_tail", stderr_tail_);
      return std::unexpected(std::move(error));
    }
    if (auto reaped = reap_child(); !reaped)
      return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline)
    {
      auto error = protocol_error(std::string(timeout_message), server_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      if (!stderr_tail_.empty())
        error.with_context("stderr_tail", stderr_tail_);
      terminate_child();
      return std::unexpected(std::move(error));
    }

    std::array<pollfd, 2> fds{pollfd{.fd = stdout_fd_, .events = POLLIN, .revents = 0}, pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    int const poll_timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    int const polled = poll(fds.data(), fds.size(), poll_timeout);
    if (polled < 0)
    {
      if (errno == EINTR)
        continue;
      return std::unexpected(errno_error("failed to poll MCP process pipes", server_));
    }
    if (polled == 0)
      continue;
    if (fds[1].revents != 0)
    {
      if (auto drained = drain_stderr(); !drained)
        return std::unexpected(std::move(drained.error()));
    }
    if (fds[0].revents != 0)
    {
      if (auto drained = drain_stdout(); !drained)
        return std::unexpected(std::move(drained.error()));
    }
  }
}

ava::core::VoidResult McpStdioClient::wait_for_writable(std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                        std::string_view timeout_message, CancelCallback cancel_requested)
{
  while (true)
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("MCP request canceled", server_));
    }
    if (auto reaped = reap_child(); !reaped)
      return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline)
    {
      auto error = protocol_error(std::string(timeout_message), server_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    std::array<pollfd, 2> fds{pollfd{.fd = stdin_fd_, .events = POLLOUT, .revents = 0}, pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    int const poll_timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    int const polled = poll(fds.data(), fds.size(), poll_timeout);
    if (polled < 0)
    {
      if (errno == EINTR)
        continue;
      return std::unexpected(errno_error("failed to poll MCP request pipe", server_));
    }
    if (fds[1].revents != 0)
    {
      if (auto drained = drain_stderr(); !drained)
        return std::unexpected(std::move(drained.error()));
    }
    if ((fds[0].revents & POLLOUT) != 0)
      return {};
    if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
      auto error = protocol_error("MCP request pipe closed", server_);
      if (child_exited_)
        error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.empty())
        error.with_context("stderr_tail", stderr_tail_);
      return std::unexpected(std::move(error));
    }
  }
}

ava::core::VoidResult McpStdioClient::drain_stdout()
{
  if (stdout_fd_ < 0)
    return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true)
  {
    auto const bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
    if (bytes > 0)
    {
      stdout_buffer_.append(buffer.data(), static_cast<std::size_t>(bytes));
      if (++reads >= kMaxDrainReadsPerPoll)
        return {};
      continue;
    }
    if (bytes == 0)
    {
      close_fd(stdout_fd_);
      return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return {};
    return std::unexpected(errno_error("failed to read MCP stdout", server_));
  }
}

ava::core::VoidResult McpStdioClient::drain_stderr()
{
  if (stderr_fd_ < 0)
    return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true)
  {
    auto const bytes = read_retry(stderr_fd_, buffer.data(), buffer.size());
    if (bytes > 0)
    {
      append_stderr(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
      if (++reads >= kMaxDrainReadsPerPoll)
        return {};
      continue;
    }
    if (bytes == 0)
    {
      close_fd(stderr_fd_);
      return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return {};
    return std::unexpected(errno_error("failed to read MCP stderr", server_));
  }
}

ava::core::VoidResult McpStdioClient::reap_child()
{
  if (pid_ <= 0 || child_exited_)
    return {};
  int status = 0;
  auto const waited = waitpid_retry(static_cast<pid_t>(pid_), &status, WNOHANG);
  if (waited < 0)
  {
    if (errno == ECHILD)
    {
      pid_ = -1;
      child_exited_ = true;
      return {};
    }
    return std::unexpected(errno_error("failed to reap MCP server process", server_));
  }
  if (waited == 0)
    return {};
  child_status_ = status;
  child_exited_ = true;
  close_fd(stdin_fd_);
  return {};
}

ava::core::VoidResult McpStdioClient::set_pipe_nonblocking(int fd, std::string_view pipe_name)
{
  int const flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return std::unexpected(errno_error("failed to inspect MCP pipe flags", server_));
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
  {
    auto error = errno_error("failed to set MCP pipe nonblocking", server_);
    error.with_context("pipe", std::string(pipe_name));
    return std::unexpected(std::move(error));
  }
  return {};
}

void McpStdioClient::append_stderr(std::string_view chunk)
{
  if (chunk.size() >= options_.max_stderr_bytes)
  {
    stderr_tail_ = std::string(chunk.substr(chunk.size() - options_.max_stderr_bytes));
    stderr_truncated_ = true;
    return;
  }
  if (stderr_tail_.size() + chunk.size() > options_.max_stderr_bytes)
  {
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
  if (pid_ <= 0 || child_exited_)
    return;
  close_fd(stdin_fd_);
  if (can_signal_group_)
  {
    kill(-static_cast<pid_t>(pid_), SIGTERM);
  }
  else
  {
    kill(static_cast<pid_t>(pid_), SIGTERM);
  }
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while (std::chrono::steady_clock::now() < deadline)
  {
    int status = 0;
    auto const waited = waitpid_retry(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (waited == pid_)
    {
      child_status_ = status;
      child_exited_ = true;
      return;
    }
    if (waited < 0)
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (can_signal_group_)
  {
    kill(-static_cast<pid_t>(pid_), SIGKILL);
  }
  else
  {
    kill(static_cast<pid_t>(pid_), SIGKILL);
  }
  int status = 0;
  if (waitpid_retry(static_cast<pid_t>(pid_), &status, 0) == pid_)
  {
    child_status_ = status;
    child_exited_ = true;
  }
}

ava::core::VoidResult McpStdioClient::shutdown(std::chrono::milliseconds grace)
{
  close_fd(stdin_fd_);
  auto const deadline = std::chrono::steady_clock::now() + grace;
  while (pid_ > 0 && !child_exited_ && std::chrono::steady_clock::now() < deadline)
  {
    if (auto drained = drain_stdout(); !drained)
      return std::unexpected(std::move(drained.error()));
    if (auto drained = drain_stderr(); !drained)
      return std::unexpected(std::move(drained.error()));
    if (auto reaped = reap_child(); !reaped)
      return std::unexpected(std::move(reaped.error()));
    if (!child_exited_)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (pid_ > 0 && !child_exited_)
    terminate_child();
  close_fds();
  return {};
}

}  // namespace ava::mcp
