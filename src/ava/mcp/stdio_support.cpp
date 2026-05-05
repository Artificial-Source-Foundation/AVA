#include "ava/mcp/stdio_support.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <utility>

namespace ava::mcp::detail {

ava::core::Error with_mcp_server_context(ava::core::Error error, McpServerConfig const& server)
{
  error.with_context("mcp_server", server.id);
  if (!server.source_path.empty()) error.with_context("config", server.source_path.string());
  return error;
}

ava::core::Error mcp_error(ava::core::ErrorCategory category, std::string message, McpServerConfig const& server)
{
  return with_mcp_server_context(ava::core::Error(category, std::move(message)), server);
}

ava::core::Error errno_error(std::string message, McpServerConfig const& server)
{
  auto error = mcp_error(ava::core::ErrorCategory::Io, std::move(message), server);
  error.with_context("cause", std::strerror(errno));
  return error;
}

ava::core::Error protocol_error(std::string message, McpServerConfig const& server)
{
  return mcp_error(ava::core::ErrorCategory::Tool, std::move(message), server);
}

ava::core::Error canceled_error(std::string message, McpServerConfig const& server)
{
  auto error = mcp_error(ava::core::ErrorCategory::Unknown, std::move(message), server);
  error.with_context("canceled", "true");
  return error;
}

bool is_canceled(CancelCallback const& cancel_requested)
{
  return cancel_requested && cancel_requested();
}

std::vector<std::string> mcp_argv(McpServerConfig const& server)
{
  std::vector<std::string> argv;
  argv.reserve(server.args.size() + 1);
  argv.push_back(server.command);
  argv.insert(argv.end(), server.args.begin(), server.args.end());
  return argv;
}

std::filesystem::path child_working_dir(McpStdioClientOptions const& options)
{
  if (!options.workspace_dir.empty()) return options.workspace_dir;
  return std::filesystem::current_path();
}

ava::core::VoidResult validate_start_request(McpServerConfig const& server, McpStdioClientOptions& options,
                                             CancelCallback const& cancel_requested)
{
  if (server.command.empty()) {
    return std::unexpected(
        mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP server command must not be empty", server));
  }
  if (options.workspace_dir.empty()) options.workspace_dir = std::filesystem::current_path();
  if (options.startup_timeout < std::chrono::milliseconds(50) || options.startup_timeout > std::chrono::seconds(30)) {
    auto error = mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP startup timeout is out of bounds", server);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.request_timeout < std::chrono::milliseconds(50) || options.request_timeout > std::chrono::seconds(30)) {
    auto error = mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP request timeout is out of bounds", server);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.max_message_bytes == 0 || options.max_stderr_bytes == 0) {
    return std::unexpected(
        mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP client byte limits must be non-zero", server));
  }
  if (is_canceled(cancel_requested)) {
    return std::unexpected(canceled_error("MCP startup canceled", server));
  }
  return {};
}

}  // namespace ava::mcp::detail
