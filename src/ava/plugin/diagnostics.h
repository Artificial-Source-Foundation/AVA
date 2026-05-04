#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ava/plugin/discovery.h"

namespace ava::plugin {

struct PluginFailure {
  PluginScope scope = PluginScope::Project;
  std::filesystem::path path;
  std::string message;
  std::string details;
};

struct PluginStatus {
  DiscoveredPlugin plugin;
  bool enabled = false;
};

struct PluginDiagnostics {
  PluginDiscoveryOptions discovery_options;
  std::filesystem::path enablement_file;
  std::vector<PluginStatus> plugins;
  std::vector<PluginFailure> failures;
};

[[nodiscard]] PluginDiagnostics collect_plugin_diagnostics(const PluginDiscoveryOptions& options,
                                                          const std::filesystem::path& enablement_file,
                                                          const std::filesystem::path& workspace_root);

}  // namespace ava::plugin
