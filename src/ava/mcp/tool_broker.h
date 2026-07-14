#pragma once

#include "ava/agent/tool_registry.h"
#include "ava/tools/file_tools.h"
#include "ava/mcp/config.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace ava::mcp {

// Canonical permission identity for an immutable session MCP child. The result
// is intentionally not truncated: identities above the persistent-rule command
// bound cannot be authorized and therefore fail closed.
[[nodiscard]] std::string session_mcp_launch_identity(McpServerConfig const& server, std::filesystem::path const& canonical_child_cwd);
[[nodiscard]] std::string mcp_model_tool_name(std::string_view server_id, std::string_view tool_name);
[[nodiscard]] std::string mcp_model_resource_tool_name(std::string_view server_id, std::string_view resource_uri);
[[nodiscard]] ava::core::VoidResult register_enabled_mcp_tools(ava::agent::ToolRegistry& registry, ava::tools::ToolContext const& context);

}  // namespace ava::mcp
