#pragma once

#include <filesystem>
#include <string>

namespace ava::tools {

inline constexpr const char* kLegacyProjectAvaDirectoryName = ".ava";
inline constexpr const char* kLegacyProjectFileHistoryDirectoryName = "file-history-m6";

[[nodiscard]] bool is_backup_history_path(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& candidate
);
void reject_backup_history_access(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& candidate,
    const std::string& tool_name
);

class FileBackupSession {
 public:
  explicit FileBackupSession(std::filesystem::path workspace_root);

  [[nodiscard]] const std::filesystem::path& backup_root() const { return backup_root_; }
  void backup_file_before_edit(const std::filesystem::path& file_path) const;

 private:
  std::filesystem::path workspace_root_;
  std::filesystem::path history_root_;
  std::filesystem::path backup_root_;
  std::string session_id_;
};

}  // namespace ava::tools
