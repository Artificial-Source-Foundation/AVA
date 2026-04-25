#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "ava/mcp/manager.hpp"
#include "ava/tools/tool.hpp"

namespace ava::tools {

class ToolRegistry;

[[nodiscard]] std::string namespaced_mcp_tool_name(const std::string& server_name, const std::string& tool_name);

class McpBridgeTool final : public Tool {
 public:
  McpBridgeTool(
      std::shared_ptr<ava::mcp::McpManager> manager,
      std::string server_name,
      ava::mcp::McpTool tool
  );

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

  [[nodiscard]] const std::string& original_name() const;
  [[nodiscard]] const std::string& server_name() const;

 private:
  std::shared_ptr<ava::mcp::McpManager> manager_;
  std::string namespaced_name_;
  std::string server_name_;
  std::string original_name_;
  std::string description_;
  nlohmann::json input_schema_;
};

std::size_t register_mcp_tools(
    ToolRegistry& registry,
    const std::shared_ptr<ava::mcp::McpManager>& manager
);

}  // namespace ava::tools
