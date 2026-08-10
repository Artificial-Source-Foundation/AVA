#include "sys.h"
#include "ExtensionResourcePolicy.h"
#include "Session.h"
#include "ava/app/project_trust.h"
#include "ava/context/skill_loader.h"

namespace ava::app::runtime {

ExtensionResourcePolicy make_extension_resource_policy(ava::config::XdgPaths const& paths, std::filesystem::path const& workspace_dir,
                                                       bool include_project_resources)
{
  return ExtensionResourcePolicy{
      .include_project_resources = include_project_resources,
      .plugin_discovery =
          ava::plugin::PluginDiscoveryOptions{
              .global_plugins_dir = paths.ava_config_dir / "plugins",
              .project_plugins_dir = include_project_resources ? workspace_dir / ".ava" / "plugins" : std::filesystem::path{},
          },
      .plugin_enablement_file = paths.ava_state_dir / "plugin-enablement.json",
      .mcp_config =
          ava::mcp::McpConfigLoadOptions{
              .workspace_dir = workspace_dir,
              .global_config_file = paths.ava_config_dir / "mcp.json",
              .project_config_file = include_project_resources ? workspace_dir / ".ava" / "mcp.json" : std::filesystem::path{},
          },
      .global_lsp_config_file = paths.ava_config_dir / "lsp.json",
      .project_lsp_config_file = include_project_resources ? workspace_dir / ".ava" / "lsp.json" : std::filesystem::path{},
      .global_skill_dirs = ava::context::default_global_skill_dirs(paths),
      .project_skill_dirs = include_project_resources ? ava::context::default_project_skill_dirs(workspace_dir) : std::vector<std::filesystem::path>{},
  };
}

ExtensionResourcePolicy make_extension_resource_policy_1(session_ts const& unlocked_session)
{
  SCOPED_CRITICAL_AREA_CR(session_r, unlocked_session);
  return make_extension_resource_policy(session_r->paths(), session_r->workspace_dir(), project_resources_trusted(session_r->project_trust()));
}

}  // namespace ava::app::runtime
