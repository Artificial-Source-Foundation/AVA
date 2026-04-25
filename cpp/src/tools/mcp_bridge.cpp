#include "ava/tools/mcp_bridge.hpp"

#include <cctype>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "ava/tools/registry.hpp"

namespace ava::tools {
namespace {

[[nodiscard]] std::string sanitize_tool_segment(std::string value) {
  for(char& ch : value) {
    const auto uchar = static_cast<unsigned char>(ch);
    if(std::isalnum(uchar) != 0 || ch == '_' || ch == '-') {
      continue;
    }
    ch = '_';
  }
  if(value.empty()) {
    return "unnamed";
  }
  return value;
}

[[nodiscard]] std::string extract_tool_content(const nlohmann::json& result) {
  if(result.contains("content")) {
    const auto& content = result.at("content");
    if(content.is_string()) {
      return content.get<std::string>();
    }

    if(content.is_array()) {
      std::string joined;
      std::size_t index = 0;
      for(const auto& block : content) {
        if(index > 0) {
          joined += "\n";
        }
        if(block.is_object() && block.value("type", std::string{}) == "text" && block.contains("text") &&
           block.at("text").is_string()) {
          joined += block.at("text").get<std::string>();
        } else {
          joined += block.dump();
        }
        ++index;
      }
      return joined;
    }
  }

  return result.dump();
}

}  // namespace

std::string namespaced_mcp_tool_name(const std::string& server_name, const std::string& tool_name) {
  return "mcp_" + sanitize_tool_segment(server_name) + "_" + sanitize_tool_segment(tool_name);
}

McpBridgeTool::McpBridgeTool(
    std::shared_ptr<ava::mcp::McpManager> manager,
    std::string server_name,
    ava::mcp::McpTool tool
)
    : manager_(std::move(manager)),
      namespaced_name_(namespaced_mcp_tool_name(server_name, tool.name)),
      server_name_(std::move(server_name)),
      original_name_(std::move(tool.name)),
      description_(std::move(tool.description)),
      input_schema_(std::move(tool.input_schema)) {
  if(!manager_) {
    throw std::runtime_error("MCP bridge tool requires a manager");
  }
  if(server_name_.empty()) {
    throw std::runtime_error("MCP bridge tool requires a server name");
  }
  if(original_name_.empty()) {
    throw std::runtime_error("MCP bridge tool requires a non-empty tool name");
  }
}

std::string McpBridgeTool::name() const {
  return namespaced_name_;
}

std::string McpBridgeTool::description() const {
  return description_;
}

nlohmann::json McpBridgeTool::parameters() const {
  return input_schema_;
}

ava::types::ToolResult McpBridgeTool::execute(const nlohmann::json& args) const {
  try {
    const auto result = manager_->call_tool(server_name_, original_name_, args);
    return ava::types::ToolResult{
        .call_id = "",
        .content = extract_tool_content(result),
        .is_error = result.value("isError", false),
    };
  } catch(const std::exception& e) {
    return ava::types::ToolResult{
        .call_id = "",
        .content = std::string("MCP bridge call failed: ") + e.what(),
        .is_error = true,
    };
  } catch(...) {
    return ava::types::ToolResult{
        .call_id = "",
        .content = "MCP bridge call failed: non-standard exception",
        .is_error = true,
    };
  }
}

const std::string& McpBridgeTool::original_name() const {
  return original_name_;
}

const std::string& McpBridgeTool::server_name() const {
  return server_name_;
}

std::size_t register_mcp_tools(
    ToolRegistry& registry,
    const std::shared_ptr<ava::mcp::McpManager>& manager
) {
  if(!manager) {
    return 0;
  }

  const auto before = registry.tool_count();
  std::unordered_set<std::string> seen_namespaces;
  for(const auto& entry : manager->list_tools()) {
    const auto namespaced_name = namespaced_mcp_tool_name(entry.server_name, entry.tool.name);
    if(seen_namespaces.contains(namespaced_name)) {
      continue;
    }
    seen_namespaces.insert(namespaced_name);

    registry.register_tool_with_source(
        std::make_unique<McpBridgeTool>(manager, entry.server_name, entry.tool),
        ToolSource::mcp(entry.server_name)
    );
  }
  return registry.tool_count() - before;
}

}  // namespace ava::tools
