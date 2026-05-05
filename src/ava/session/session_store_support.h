#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/session/session_store.h"

namespace ava::session::detail {

enum class MissingSessionFile {
  Allow,
  NotFoundError,
};

[[nodiscard]] std::string project_key(std::filesystem::path const& workspace_dir);
[[nodiscard]] std::filesystem::path session_project_directory(std::filesystem::path const& root_dir,
                                                              std::filesystem::path const& workspace_dir);
[[nodiscard]] std::filesystem::path session_file_path(std::filesystem::path const& root_dir,
                                                      std::filesystem::path const& workspace_dir,
                                                      std::string_view session_id);
[[nodiscard]] ava::core::VoidResult create_private_session_directories(std::filesystem::path const& root_dir,
                                                                       std::filesystem::path const& session_dir);
[[nodiscard]] ava::core::Result<bool> inspect_session_file(std::filesystem::path const& path,
                                                           std::string_view session_id, MissingSessionFile missing);
[[nodiscard]] ava::core::VoidResult set_private_session_file_permissions(std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<std::vector<SessionEntry>> read_session_entries(std::filesystem::path const& path,
                                                                                std::string_view session_id);
[[nodiscard]] bool is_listable_session_file(std::filesystem::directory_entry const& entry);
void sort_session_summaries(std::vector<SessionSummary>& summaries);

}  // namespace ava::session::detail
