#include "ava/mcp/tool_list_parser.h"

#include <string>
#include <utility>
#include <vector>

#include "ava/core/json.h"
#include "ava/mcp/protocol.h"
#include "ava/mcp/stdio_support.h"

namespace ava::mcp {

ava::core::Result<std::vector<McpToolDescription>> parse_mcp_tool_list_result(std::string_view result_json,
                                                                              std::string_view raw_response_context,
                                                                              McpServerConfig const& server)
{
  auto const tools_start = ava::core::json::field_value_start(result_json, "tools");
  if (tools_start && (*tools_start >= result_json.size() || result_json[*tools_start] != '[')) {
    auto error = detail::protocol_error("MCP tools/list result has invalid tools field", server);
    error.with_context("response", std::string(raw_response_context.substr(0, 512)));
    return std::unexpected(std::move(error));
  }

  std::vector<McpToolDescription> tools;
  for (auto const& tool_json : ava::core::json::objects_in_array_field(result_json, "tools")) {
    auto name = ava::core::json::string_field(tool_json, "name");
    if (!name || !is_valid_mcp_tool_name(*name)) {
      auto error = detail::protocol_error("MCP tool has invalid name", server);
      error.with_context("response", std::string(tool_json.substr(0, 512)));
      return std::unexpected(std::move(error));
    }
    std::string input_schema = "{\"type\":\"object\"}";
    auto const input_schema_start = ava::core::json::field_value_start(tool_json, "inputSchema");
    if (input_schema_start) {
      if (*input_schema_start >= tool_json.size() || tool_json[*input_schema_start] != '{') {
        auto error = detail::protocol_error("MCP tool inputSchema is invalid", server);
        error.with_context("tool", *name);
        return std::unexpected(std::move(error));
      }
      input_schema = ava::core::json::object_field(tool_json, "inputSchema").value_or("");
    }
    if (!ava::core::json::is_valid_object(input_schema)) {
      auto error = detail::protocol_error("MCP tool inputSchema is invalid", server);
      error.with_context("tool", *name);
      return std::unexpected(std::move(error));
    }
    tools.push_back(
        McpToolDescription{.name = std::move(*name),
                           .description = ava::core::json::string_field(tool_json, "description").value_or(""),
                           .input_schema_json = std::move(input_schema)});
  }
  return tools;
}

}  // namespace ava::mcp
