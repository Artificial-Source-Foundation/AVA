#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "ava/tools/file_backup.hpp"
#include "ava/tools/tool.hpp"

namespace ava::tools {

class WriteTool final : public Tool {
 public:
  WriteTool(std::filesystem::path workspace_root, std::shared_ptr<FileBackupSession> backup_session);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] std::string search_hint() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
  std::shared_ptr<FileBackupSession> backup_session_;
};

}  // namespace ava::tools
