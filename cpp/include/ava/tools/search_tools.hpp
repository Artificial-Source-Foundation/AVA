#pragma once

#include <filesystem>
#include <string>

#include "ava/tools/tool.hpp"

namespace ava::tools {

class GlobTool final : public Tool {
 public:
  explicit GlobTool(std::filesystem::path workspace_root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] std::string search_hint() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
};

class GrepTool final : public Tool {
 public:
  explicit GrepTool(std::filesystem::path workspace_root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] std::string search_hint() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
};

}  // namespace ava::tools
