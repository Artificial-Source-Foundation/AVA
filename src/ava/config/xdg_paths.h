#pragma once

#include <filesystem>

namespace ava::config {

struct XdgPaths {
  std::filesystem::path config_home;
  std::filesystem::path state_home;
  std::filesystem::path data_home;
  std::filesystem::path ava_config_dir;
  std::filesystem::path ava_state_dir;
  std::filesystem::path auth_file;
  std::filesystem::path compaction_file;
  std::filesystem::path global_agents_file;
  std::filesystem::path models_file;
  std::filesystem::path prompts_dir;
  std::filesystem::path sessions_dir;
};

[[nodiscard]] XdgPaths xdg_paths();
[[nodiscard]] std::filesystem::path legacy_ava_credentials_path();
[[nodiscard]] std::filesystem::path opencode_auth_path();

}  // namespace ava::config
