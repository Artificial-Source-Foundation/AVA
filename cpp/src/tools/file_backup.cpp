#include "ava/tools/file_backup.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <system_error>

#include "ava/config/paths.hpp"
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
  if(name.empty() || name == "." || name == "..") {
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
  std::error_code exists_ec;
  if(!std::filesystem::exists(path, exists_ec) || exists_ec) {
    return;
  }
  std::error_code status_ec;
  if(std::filesystem::is_symlink(std::filesystem::symlink_status(path, status_ec))) {
    throw std::runtime_error("Backup path component must not be a symlink: " + path.string());
  }
}

void restrict_backup_path_permissions(const std::filesystem::path& path, std::filesystem::perms permissions) {
  std::error_code ec;
  std::filesystem::permissions(path, permissions, std::filesystem::perm_options::replace, ec);
  if(ec) {
    throw std::runtime_error("Failed to restrict backup path permissions: " + path.string());
  }
}

}  // namespace

bool is_backup_history_path(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& candidate
) {
  std::error_code ec;
  const auto normalized = std::filesystem::weakly_canonical(candidate, ec);
  if(ec) {
    return false;
  }
  const auto legacy_history_root = std::filesystem::weakly_canonical(
      workspace_root / kLegacyProjectAvaDirectoryName / kLegacyProjectFileHistoryDirectoryName,
      ec
  );
  if(!ec && is_path_within_or_equal(legacy_history_root, normalized)) {
    return true;
  }

  ec.clear();
  const auto xdg_history_root = std::filesystem::weakly_canonical(ava::config::file_history_dir(), ec);
  return !ec && is_path_within_or_equal(xdg_history_root, normalized);
}

void reject_backup_history_access(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& candidate,
    const std::string& tool_name
) {
  if(is_backup_history_path(workspace_root, candidate)) {
    throw std::runtime_error(tool_name + " cannot access AVA file-history backups");
  }
}

FileBackupSession::FileBackupSession(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(std::move(workspace_root))),
      history_root_(ava::config::file_history_dir()),
      session_id_(unique_token()) {
  backup_root_ = history_root_ / path_hash_token(workspace_root_) / session_id_;
}

void FileBackupSession::backup_file_before_edit(const std::filesystem::path& file_path) const {
  std::error_code ec;
  if(!std::filesystem::exists(file_path, ec) || ec) {
    return;
  }
  if(!std::filesystem::is_regular_file(file_path, ec) || ec) {
    throw std::runtime_error("Backup source is not a regular file: " + file_path.string());
  }
  const auto source_size = std::filesystem::file_size(file_path, ec);
  if(ec) {
    throw std::runtime_error("Failed to inspect backup source: " + file_path.string());
  }
  if(source_size > kBackupMaxFileBytes) {
    throw std::runtime_error("Backup source is too large: " + file_path.string());
  }

  const auto history_dir = backup_root_.parent_path();
  reject_symlinked_backup_component(history_root_);
  reject_symlinked_backup_component(history_dir);
  reject_symlinked_backup_component(backup_root_);

  std::filesystem::create_directories(backup_root_, ec);
  if(ec) {
    throw std::runtime_error("Failed to create backup directory: " + backup_root_.string());
  }
  restrict_backup_path_permissions(history_root_, std::filesystem::perms::owner_all);
  restrict_backup_path_permissions(history_dir, std::filesystem::perms::owner_all);
  restrict_backup_path_permissions(backup_root_, std::filesystem::perms::owner_all);
  reject_symlinked_backup_component(backup_root_);

  const auto canonical_history_root = std::filesystem::weakly_canonical(history_root_, ec);
  if(ec) {
    throw std::runtime_error("Failed to resolve backup history root: " + history_root_.string());
  }

  ec.clear();
  const auto canonical_backup_root = std::filesystem::weakly_canonical(backup_root_, ec);
  if(ec || !is_path_within_or_equal(canonical_history_root, canonical_backup_root)) {
    throw std::runtime_error("Backup directory escapes file-history root: " + backup_root_.string());
  }

  const auto backup_name = sanitize_filename(file_path) + "." + path_hash_token(file_path) + "." + unique_token() + ".bak";
  const auto destination = canonical_backup_root / backup_name;
  std::filesystem::copy_file(file_path, destination, std::filesystem::copy_options::none, ec);
  if(ec) {
    throw std::runtime_error("Failed to create file backup for: " + file_path.string());
  }
  restrict_backup_path_permissions(
      destination,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
  );
}

}  // namespace ava::tools
