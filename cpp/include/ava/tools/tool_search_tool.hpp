#pragma once

#include <string>
#include <vector>

#include "ava/tools/registry.hpp"

namespace ava::tools {

class ToolSearchTool final : public Tool {
 public:
  explicit ToolSearchTool(const ToolRegistry& registry);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] std::string search_hint() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::vector<ToolSearchMatch> search_index_;
};

}  // namespace ava::tools
