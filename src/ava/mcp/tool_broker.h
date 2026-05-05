#pragma once

#include "ava/agent/tool_registry.h"

#include "ava/tools/file_tools.h"

#include <string>
#include <string_view>

namespace ava::mcp {

[[nodiscard]] std::string mcp_model_tool_name(std::string_view server_id, std::string_view tool_name);
void register_enabled_mcp_tools(ava::agent::ToolRegistry& registry, ava::tools::ToolContext const& context);

}  // namespace ava::mcp
