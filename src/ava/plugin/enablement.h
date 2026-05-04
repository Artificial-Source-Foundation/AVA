#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ava/core/result.h"
#include "ava/plugin/manifest.h"

namespace ava::plugin {

struct PluginEnablementRecord {
  std::filesystem::path workspace;
  std::string plugin_id;
  PluginScope scope = PluginScope::Project;
  bool enabled = false;
};

[[nodiscard]] std::filesystem::path default_plugin_enablement_file();
[[nodiscard]] std::filesystem::path canonical_workspace_key(const std::filesystem::path& workspace_root);
[[nodiscard]] ava::core::Result<std::vector<PluginEnablementRecord>> load_plugin_enablement(
    const std::filesystem::path& state_file);
[[nodiscard]] ava::core::Result<bool> plugin_enabled(const std::filesystem::path& state_file,
                                                     const std::filesystem::path& workspace_root,
                                                     std::string_view plugin_id,
                                                     PluginScope scope = PluginScope::Project);
[[nodiscard]] ava::core::VoidResult set_plugin_enabled(const std::filesystem::path& state_file,
                                                       const std::filesystem::path& workspace_root,
                                                       std::string_view plugin_id, bool enabled,
                                                       PluginScope scope = PluginScope::Project);

}  // namespace ava::plugin
