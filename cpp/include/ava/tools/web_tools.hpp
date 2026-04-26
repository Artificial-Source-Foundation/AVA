#pragma once

#include <filesystem>
#include <string>

#include "ava/tools/tool.hpp"

namespace ava::tools {

class WebFetchTool final : public Tool {
 public:
  explicit WebFetchTool(std::filesystem::path workspace_root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] std::string search_hint() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
};

class WebSearchTool final : public Tool {
 public:
  explicit WebSearchTool(std::filesystem::path workspace_root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] std::string search_hint() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
};

}  // namespace ava::tools
