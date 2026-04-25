#pragma once

#include <filesystem>

namespace ava::config {

struct AppPaths {
  std::filesystem::path config_dir;
  std::filesystem::path data_dir;
  std::filesystem::path state_dir;
  std::filesystem::path cache_dir;
};

[[nodiscard]] AppPaths resolve_app_paths();

[[nodiscard]] std::filesystem::path config_file_path();
[[nodiscard]] std::filesystem::path credentials_path();
[[nodiscard]] std::filesystem::path trusted_projects_path();
[[nodiscard]] std::filesystem::path app_db_path();
[[nodiscard]] std::filesystem::path mcp_config_path();
[[nodiscard]] std::filesystem::path custom_tools_dir();
[[nodiscard]] std::filesystem::path project_mcp_config_path(const std::filesystem::path& workspace_root);
[[nodiscard]] std::filesystem::path project_custom_tools_dir(const std::filesystem::path& workspace_root);

}  // namespace ava::config
