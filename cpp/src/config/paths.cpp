#include "ava/config/paths.hpp"

#include <cstdlib>
#include <stdexcept>

namespace ava::config {
namespace {

constexpr const char* kAppDirName = "ava";

[[nodiscard]] std::filesystem::path env_path(const char* name) {
  if(const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
    return std::filesystem::path(value);
  }
  return {};
}

[[nodiscard]] std::filesystem::path home_dir() {
  if(const auto home = env_path("HOME"); !home.empty()) {
    return home;
  }
  throw std::runtime_error("Could not resolve HOME for XDG path resolution");
}

[[nodiscard]] std::filesystem::path prefer_existing(
    const std::filesystem::path& preferred,
    const std::filesystem::path& legacy_suffix
) {
  if(std::filesystem::exists(preferred)) {
    return preferred;
  }

  const auto legacy = home_dir() / ".ava" / legacy_suffix;
  if(std::filesystem::exists(legacy)) {
    return legacy;
  }
  return preferred;
}

}  // namespace

AppPaths resolve_app_paths() {
  const auto home = home_dir();
  const auto config_root = env_path("XDG_CONFIG_HOME").empty() ? (home / ".config") : env_path("XDG_CONFIG_HOME");
  const auto data_root = env_path("XDG_DATA_HOME").empty() ? (home / ".local" / "share") : env_path("XDG_DATA_HOME");
  const auto state_root = env_path("XDG_STATE_HOME").empty() ? (home / ".local" / "state") : env_path("XDG_STATE_HOME");
  const auto cache_root = env_path("XDG_CACHE_HOME").empty() ? (home / ".cache") : env_path("XDG_CACHE_HOME");

  return AppPaths{
      .config_dir = config_root / kAppDirName,
      .data_dir = data_root / kAppDirName,
      .state_dir = state_root / kAppDirName,
      .cache_dir = cache_root / kAppDirName,
  };
}

std::filesystem::path config_file_path() {
  const auto paths = resolve_app_paths();
  return prefer_existing(paths.config_dir / "config.yaml", "config.yaml");
}

std::filesystem::path credentials_path() {
  const auto paths = resolve_app_paths();
  return prefer_existing(paths.data_dir / "credentials.json", "credentials.json");
}

std::filesystem::path trusted_projects_path() {
  const auto paths = resolve_app_paths();
  return prefer_existing(paths.config_dir / "trusted_projects.json", "trusted_projects.json");
}

std::filesystem::path app_db_path() {
  const auto paths = resolve_app_paths();
  return prefer_existing(paths.data_dir / "data.db", "data.db");
}

std::filesystem::path mcp_config_path() {
  const auto paths = resolve_app_paths();
  return prefer_existing(paths.config_dir / "mcp.json", "mcp.json");
}

std::filesystem::path custom_tools_dir() {
  const auto paths = resolve_app_paths();
  return prefer_existing(paths.config_dir / "tools", "tools");
}

std::filesystem::path project_mcp_config_path(const std::filesystem::path& workspace_root) {
  return workspace_root / ".ava" / "mcp.json";
}

std::filesystem::path project_custom_tools_dir(const std::filesystem::path& workspace_root) {
  return workspace_root / ".ava" / "tools";
}

}  // namespace ava::config
