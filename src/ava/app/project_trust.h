#pragma once

#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

enum class ProjectTrustDecision
{
  Unknown,
  Trusted,
  Denied,
};

struct ProjectTrustResource
{
  std::string kind;
  std::filesystem::path path;
};

struct ProjectTrustState
{
  std::filesystem::path workspace_dir;
  std::filesystem::path trust_file;
  ProjectTrustDecision decision = ProjectTrustDecision::Unknown;
  std::filesystem::path matched_path;
  std::vector<ProjectTrustResource> protected_resources;
  std::string diagnostic;
};

[[nodiscard]] std::string_view to_string(ProjectTrustDecision decision);
[[nodiscard]] bool project_resources_trusted(ProjectTrustState const& state);
[[nodiscard]] std::filesystem::path project_trust_file(ava::config::XdgPaths const& paths);
[[nodiscard]] ProjectTrustState load_project_trust_state(ava::config::XdgPaths const& paths,
                                                         std::filesystem::path const& workspace_dir);
[[nodiscard]] ava::core::VoidResult set_project_trust_decision(ava::config::XdgPaths const& paths,
                                                               std::filesystem::path const& workspace_dir,
                                                               bool trusted);
[[nodiscard]] ava::core::VoidResult clear_project_trust_decision(ava::config::XdgPaths const& paths,
                                                                 std::filesystem::path const& workspace_dir);

}  // namespace ava::app
