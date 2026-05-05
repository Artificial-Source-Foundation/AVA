#include "ava/mcp/stdio_client.h"

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/core/json.h"
#include "ava/core/version.h"
#include "ava/mcp/protocol.h"
#include "ava/mcp/stdio_support.h"
#include "ava/mcp/tool_list_parser.h"

namespace ava::mcp {
namespace {

using detail::canceled_error;
using detail::is_canceled;
using detail::mcp_error;
using detail::protocol_error;

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

}  // namespace ava::mcp
