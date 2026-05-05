#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/mcp/config.h"

namespace ava::mcp {

struct McpToolDescription {
  std::string name;
  std::string description;
  std::string input_schema_json;
};

[[nodiscard]] ava::core::Result<std::vector<McpToolDescription>> parse_mcp_tool_list_result(
    std::string_view result_json, std::string_view raw_response_context, McpServerConfig const& server);

}  // namespace ava::mcp
