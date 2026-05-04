#pragma once

#include <filesystem>
#include <vector>

#include "ava/core/result.h"
#include "ava/plugin/manifest.h"

namespace ava::plugin {

struct PluginDiscoveryOptions {
  std::filesystem::path global_plugins_dir;
  std::filesystem::path project_plugins_dir;
};

[[nodiscard]] PluginDiscoveryOptions default_plugin_discovery_options(std::filesystem::path const& workspace_root);
[[nodiscard]] ava::core::Result<std::vector<DiscoveredPlugin>> discover_plugins(PluginDiscoveryOptions const& options);

}  // namespace ava::plugin
