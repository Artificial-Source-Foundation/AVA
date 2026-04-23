#include "ava/tools/file_backup.hpp"

#include <chrono>
#include <sstream>
#include <system_error>

namespace ava::tools {

namespace {

[[nodiscard]] std::string now_token() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

[[nodiscard]] std::string sanitize_filename(const std::filesystem::path& path) {
  auto name = path.filename().string();
  for(auto& ch : name) {
    if(ch == '/' || ch == '\\' || ch == ':' || ch == ' ') {
      ch = '_';
    }
  }
  if(name.empty()) {
    name = "unnamed";
  }
  return name;
}

}  // namespace

FileBackupSession::FileBackupSession(std::filesystem::path workspace_root)
    : session_id_(now_token()) {
  backup_root_ = workspace_root / ".ava" / "file-history-m6" / session_id_;
}

void FileBackupSession::backup_file_before_edit(const std::filesystem::path& file_path) const {
  std::error_code ec;
  if(!std::filesystem::exists(file_path, ec) || ec) {
    return;
  }

  std::filesystem::create_directories(backup_root_, ec);
  if(ec) {
    throw std::runtime_error("Failed to create backup directory: " + backup_root_.string());
  }

  const auto backup_name = sanitize_filename(file_path) + "." + now_token() + ".bak";
  const auto destination = backup_root_ / backup_name;
  std::filesystem::copy_file(file_path, destination, std::filesystem::copy_options::overwrite_existing, ec);
  if(ec) {
    throw std::runtime_error("Failed to create file backup for: " + file_path.string());
  }
}

}  // namespace ava::tools
