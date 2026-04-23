#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/tools/tool.hpp"

namespace ava::tools {

struct ToolDefinitionWithSource {
  ava::types::Tool definition;
  ToolSource source;
  ToolTier tier{ToolTier::Default};
};

class ToolRegistry {
 public:
  ToolRegistry() = default;

  void register_tool(std::unique_ptr<Tool> tool);
  void register_tool_with_tier(std::unique_ptr<Tool> tool, ToolTier tier);
  void register_tool_with_source(std::unique_ptr<Tool> tool, ToolSource source);
  void unregister_tool(const std::string& name);

  void add_middleware(std::shared_ptr<Middleware> middleware);

  [[nodiscard]] ava::types::ToolResult execute(ava::types::ToolCall tool_call) const;

  [[nodiscard]] std::vector<ava::types::Tool> list_tools() const;
  [[nodiscard]] std::vector<ava::types::Tool> list_tools_for_tiers(
      const std::vector<ToolTier>& tiers
  ) const;
  [[nodiscard]] std::vector<ToolDefinitionWithSource> list_tools_with_source() const;

  [[nodiscard]] std::size_t tool_count() const;
  [[nodiscard]] bool has_tool(const std::string& name) const;
  [[nodiscard]] std::vector<std::string> tool_names() const;
  [[nodiscard]] std::optional<ToolSource> tool_source(const std::string& name) const;
  [[nodiscard]] std::optional<nlohmann::json> tool_parameters(const std::string& name) const;

 private:
  [[nodiscard]] const Tool& find_tool_or_throw(const std::string& name) const;

  std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
  std::unordered_map<std::string, ToolSource> sources_;
  std::unordered_map<std::string, ToolTier> tiers_;
  std::vector<std::shared_ptr<Middleware>> middleware_;
};

}  // namespace ava::tools
