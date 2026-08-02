#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/plugin/discovery.h"
#include "ava/mcp/config.h"
#include "ava/config/xdg_paths.h"

#include <filesystem>
#include <vector>

namespace ava::app::runtime {

class Session;

struct ExtensionResourcePolicy
{
  bool const include_project_resources;
  ava::plugin::PluginDiscoveryOptions const plugin_discovery;
  std::filesystem::path const plugin_enablement_file;
  ava::mcp::McpConfigLoadOptions const mcp_config;
  std::filesystem::path const global_lsp_config_file;
  std::filesystem::path const project_lsp_config_file;
  std::vector<std::filesystem::path> const global_skill_dirs;
  std::vector<std::filesystem::path> const project_skill_dirs;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ExtensionResourcePolicy make_extension_resource_policy(ava::config::XdgPaths const& paths, std::filesystem::path const& workspace_dir,
                                                                     bool include_project_resources);
[[nodiscard]] ExtensionResourcePolicy make_extension_resource_policy_1(Session const& session);

}  // namespace ava::app::runtime
