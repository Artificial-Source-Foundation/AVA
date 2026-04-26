#pragma once

#include <filesystem>
#include <string>

namespace ava::tools {

inline constexpr const char* kAvaDirectoryName = ".ava";
// Existing backup stores use this directory name; keep it stable for persisted workspaces.
inline constexpr const char* kFileHistoryDirectoryName = "file-history-m6";

class FileBackupSession {
 public:
  explicit FileBackupSession(std::filesystem::path workspace_root);

  [[nodiscard]] const std::filesystem::path& backup_root() const { return backup_root_; }
  void backup_file_before_edit(const std::filesystem::path& file_path) const;

 private:
  std::filesystem::path workspace_root_;
  std::filesystem::path backup_root_;
  std::string session_id_;
};

}  // namespace ava::tools
