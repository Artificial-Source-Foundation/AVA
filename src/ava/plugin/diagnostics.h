#pragma once

#include "ava/plugin/discovery.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ava::plugin {

struct PluginFailure
{
  PluginScope scope = PluginScope::Project;
  std::filesystem::path path;
  std::string message;
  std::string details;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginStatus
{
  DiscoveredPlugin plugin;
  bool enabled = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginDiagnostics
{
  PluginDiscoveryOptions discovery_options;
  std::filesystem::path enablement_file;
  std::vector<PluginStatus> plugins;
  std::vector<PluginFailure> failures;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] PluginDiagnostics collect_plugin_diagnostics(PluginDiscoveryOptions const& options, std::filesystem::path const& enablement_file,
                                                           std::filesystem::path const& workspace_root);

}  // namespace ava::plugin
