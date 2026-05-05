#include "ava/session/session_store_support.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "ava/session/session_entry_codec.h"

namespace ava::session::detail {

std::string project_key(std::filesystem::path const& workspace_dir)
{
  auto const normalized = std::filesystem::absolute(workspace_dir).lexically_normal().string();
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char const ch : normalized) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}

std::filesystem::path session_project_directory(std::filesystem::path const& root_dir,
                                                std::filesystem::path const& workspace_dir)
{
  return root_dir / project_key(workspace_dir);
}

std::filesystem::path session_file_path(std::filesystem::path const& root_dir,
                                        std::filesystem::path const& workspace_dir, std::string_view session_id)
{
  return session_project_directory(root_dir, workspace_dir) / (std::string(session_id) + ".jsonl");
}

ava::core::VoidResult create_private_session_directories(std::filesystem::path const& root_dir,
                                                         std::filesystem::path const& session_dir)
{
  std::error_code mkdir_error;
  std::filesystem::create_directories(session_dir, mkdir_error);
  if (mkdir_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create session directory");
    error.with_context("path", session_dir.string());
    error.with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }

  for (auto const& directory : {root_dir, session_dir}) {
    std::error_code permissions_error;
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                                 permissions_error);
    if (permissions_error) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set session directory permissions");
      error.with_context("path", directory.string());
      error.with_context("cause", permissions_error.message());
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

ava::core::Result<bool> inspect_session_file(std::filesystem::path const& path, std::string_view session_id,
                                             MissingSessionFile missing)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || !std::filesystem::exists(status)) {
    if (!status_error || status_error == std::errc::no_such_file_or_directory) {
      if (missing == MissingSessionFile::Allow) return false;
      auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session not found");
      error.with_context("session_id", std::string(session_id));
      error.with_context("path", path.string());
      return std::unexpected(std::move(error));
    }
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect session file");
    error.with_context("path", path.string());
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (std::filesystem::is_symlink(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session path must not be a symlink");
    error.with_context("session_id", std::string(session_id));
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::is_regular_file(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session path is not a regular file");
    error.with_context("session_id", std::string(session_id));
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return true;
}

ava::core::VoidResult set_private_session_file_permissions(std::filesystem::path const& path)
{
  std::error_code file_permissions_error;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, file_permissions_error);
  if (file_permissions_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set session file permissions");
    error.with_context("path", path.string());
    error.with_context("cause", file_permissions_error.message());
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<std::vector<SessionEntry>> read_session_entries(std::filesystem::path const& path,
                                                                  std::string_view session_id)
{
  std::ifstream file(path);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open session file");
    error.with_context("session_id", std::string(session_id));
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::vector<SessionEntry> entries;
  std::string line;
  while (true) {
    auto line_read = read_limited_session_line(file, line);
    if (!line_read) {
      return std::unexpected(line_read.error());
    }
    if (!*line_read) {
      break;
    }
    auto entry = decode_session_entry_line(line, path);
    if (!entry) return std::unexpected(std::move(entry.error()));
    entries.push_back(std::move(*entry));
  }
  return entries;
}

bool is_listable_session_file(std::filesystem::directory_entry const& entry)
{
  std::error_code entry_error;
  auto const entry_status = entry.symlink_status(entry_error);
  return !entry_error && !std::filesystem::is_symlink(entry_status) && std::filesystem::is_regular_file(entry_status) &&
         entry.path().extension() == ".jsonl";
}

void sort_session_summaries(std::vector<SessionSummary>& summaries)
{
  std::ranges::sort(summaries, [](SessionSummary const& left, SessionSummary const& right) {
    std::error_code left_error;
    std::error_code right_error;
    auto const left_time = std::filesystem::last_write_time(left.path, left_error);
    auto const right_time = std::filesystem::last_write_time(right.path, right_error);
    if (!left_error && !right_error && left_time != right_time) {
      return left_time > right_time;
    }
    return left.session_id > right.session_id;
  });
}

}  // namespace ava::session::detail
