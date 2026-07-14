#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/plugin/diagnostics.h"
#include "ava/core/result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ava::plugin {

struct PluginStaticSkillFile
{
  std::string scope;
  std::string plugin_id;
  std::string name;
  std::string description;
  std::filesystem::path path;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] std::filesystem::path plugin_static_resource_display_path(PluginManifest const& manifest, PluginResourceContribution const& resource);
[[nodiscard]] ava::core::Result<std::filesystem::path> plugin_static_resource_path(PluginManifest const& manifest, PluginResourceContribution const& resource);
[[nodiscard]] std::vector<PluginStaticSkillFile> enabled_plugin_static_skill_files(PluginDiagnostics const& diagnostics);

}  // namespace ava::plugin
