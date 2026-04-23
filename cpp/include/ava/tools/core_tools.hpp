#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "ava/tools/file_backup.hpp"
#include "ava/tools/registry.hpp"

namespace ava::tools {

class ReadTool final : public Tool {
 public:
  explicit ReadTool(std::filesystem::path workspace_root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
};

class WriteTool final : public Tool {
 public:
  WriteTool(std::filesystem::path workspace_root, std::shared_ptr<FileBackupSession> backup_session);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
  std::shared_ptr<FileBackupSession> backup_session_;
};

class EditTool final : public Tool {
 public:
  EditTool(std::filesystem::path workspace_root, std::shared_ptr<FileBackupSession> backup_session);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
  std::shared_ptr<FileBackupSession> backup_session_;
};

class BashTool final : public Tool {
 public:
  explicit BashTool(std::filesystem::path workspace_root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
};

class GlobTool final : public Tool {
 public:
  explicit GlobTool(std::filesystem::path workspace_root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
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
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
};

class GitReadTool : public Tool {
 public:
  explicit GitReadTool(std::filesystem::path workspace_root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::filesystem::path workspace_root_;
  std::string tool_name_;
};

class GitReadAliasTool final : public GitReadTool {
 public:
  explicit GitReadAliasTool(std::filesystem::path workspace_root);
  [[nodiscard]] std::string name() const override;
};

struct DefaultToolRegistration {
  std::shared_ptr<FileBackupSession> backup_session;
};

[[nodiscard]] DefaultToolRegistration register_default_tools(
    ToolRegistry& registry,
    const std::filesystem::path& workspace_root
);

}  // namespace ava::tools
