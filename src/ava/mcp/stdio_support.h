#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ava/core/result.h"
#include "ava/mcp/config.h"
#include "ava/mcp/stdio_client.h"

namespace ava::mcp::detail {

[[nodiscard]] ava::core::Error with_mcp_server_context(ava::core::Error error, McpServerConfig const& server);
[[nodiscard]] ava::core::Error mcp_error(ava::core::ErrorCategory category, std::string message,
                                         McpServerConfig const& server);
[[nodiscard]] ava::core::Error errno_error(std::string message, McpServerConfig const& server);
[[nodiscard]] ava::core::Error protocol_error(std::string message, McpServerConfig const& server);
[[nodiscard]] ava::core::Error canceled_error(std::string message, McpServerConfig const& server);
[[nodiscard]] bool is_canceled(CancelCallback const& cancel_requested);
[[nodiscard]] std::vector<std::string> mcp_argv(McpServerConfig const& server);
[[nodiscard]] std::filesystem::path child_working_dir(McpStdioClientOptions const& options);
[[nodiscard]] ava::core::VoidResult validate_start_request(McpServerConfig const& server,
                                                           McpStdioClientOptions& options,
                                                           CancelCallback const& cancel_requested);

}  // namespace ava::mcp::detail
