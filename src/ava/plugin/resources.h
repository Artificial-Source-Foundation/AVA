#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/plugin/manifest.h"

namespace ava::plugin {

inline constexpr std::size_t kMaxPluginResourceBytes = 64 * 1024;

[[nodiscard]] PluginResourceContribution const* find_plugin_resource(
    std::vector<PluginResourceContribution> const& resources, std::string_view name);
[[nodiscard]] ava::core::Result<std::filesystem::path> resolve_plugin_resource_path(
    PluginManifest const& manifest, PluginResourceContribution const& resource);
[[nodiscard]] ava::core::Result<std::string> read_plugin_resource_text(PluginManifest const& manifest,
                                                                       PluginResourceContribution const& resource);

}  // namespace ava::plugin
