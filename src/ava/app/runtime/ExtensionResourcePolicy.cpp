#include "sys.h"
#include "ExtensionResourcePolicy.h"
#include "Session.h"
#include "ava/app/project_trust.h"

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
  };
}

ExtensionResourcePolicy make_extension_resource_policy(Session const& session)
{
  return make_extension_resource_policy(session.paths(), session.workspace_dir(), project_resources_trusted(session.project_trust()));
}

}  // namespace ava::app::runtime
