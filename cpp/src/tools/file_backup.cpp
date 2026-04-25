#include "ava/tools/file_backup.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <system_error>

#include "ava/tools/path_guard.hpp"

namespace ava::tools {

namespace {

constexpr std::uintmax_t kBackupMaxFileBytes = 8 * 1024 * 1024;

std::atomic<std::uint64_t> g_backup_counter{0};

[[nodiscard]] std::string now_token() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

[[nodiscard]] std::string unique_token() {
  return now_token() + "-" + std::to_string(g_backup_counter.fetch_add(1, std::memory_order_relaxed));
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

[[nodiscard]] std::string path_hash_token(const std::filesystem::path& path) {
  std::uint64_t hash = 1469598103934665603ULL;
  for(const auto ch : path.lexically_normal().generic_string()) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= 1099511628211ULL;
  }
  return std::to_string(hash);
}

void reject_symlinked_backup_component(const std::filesystem::path& path) {
  std::error_code ec;
  if(std::filesystem::exists(path, ec) && !ec && std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec))) {
    throw std::runtime_error("Backup path component must not be a symlink: " + path.string());
  }
}

}  // namespace

FileBackupSession::FileBackupSession(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(std::move(workspace_root))),
      session_id_(unique_token()) {
  backup_root_ = workspace_root_ / ".ava" / "file-history-m6" / session_id_;
}

void FileBackupSession::backup_file_before_edit(const std::filesystem::path& file_path) const {
  std::error_code ec;
  if(!std::filesystem::exists(file_path, ec) || ec) {
    return;
  }
  const auto source_size = std::filesystem::file_size(file_path, ec);
  if(ec || source_size > kBackupMaxFileBytes) {
    throw std::runtime_error("Backup source is too large: " + file_path.string());
  }

  const auto ava_dir = workspace_root_ / ".ava";
  const auto history_dir = ava_dir / "file-history-m6";
  reject_symlinked_backup_component(ava_dir);
  reject_symlinked_backup_component(history_dir);
  reject_symlinked_backup_component(backup_root_);

  std::filesystem::create_directories(backup_root_, ec);
  if(ec) {
    throw std::runtime_error("Failed to create backup directory: " + backup_root_.string());
  }
  reject_symlinked_backup_component(backup_root_);

  const auto canonical_backup_root = std::filesystem::weakly_canonical(backup_root_, ec);
  if(ec || !is_path_within_or_equal(workspace_root_, canonical_backup_root)) {
    throw std::runtime_error("Backup directory escapes workspace: " + backup_root_.string());
  }

  const auto backup_name = sanitize_filename(file_path) + "." + path_hash_token(file_path) + "." + unique_token() + ".bak";
  const auto destination = backup_root_ / backup_name;
  std::filesystem::copy_file(file_path, destination, std::filesystem::copy_options::overwrite_existing, ec);
  if(ec) {
    throw std::runtime_error("Failed to create file backup for: " + file_path.string());
  }
  std::filesystem::permissions(
      destination,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace,
      ec
  );
}

}  // namespace ava::tools
