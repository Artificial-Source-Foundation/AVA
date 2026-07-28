#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/manifest.h"
#include "ava/core/result.h"

#include <filesystem>

namespace ava::plugin {

// Opaque, move-only token for a validated local plugin install source.
// Only inspect_plugin_install_source may construct it.
class PluginInstallSource final
{
 public:
  PluginInstallSource(PluginInstallSource const&) = delete;
  PluginInstallSource& operator=(PluginInstallSource const&) = delete;
  PluginInstallSource(PluginInstallSource&&) noexcept = default;
  PluginInstallSource& operator=(PluginInstallSource&&) noexcept = default;
  ~PluginInstallSource() = default;

  [[nodiscard]] std::filesystem::path const& directory() const noexcept { return directory_; }
  [[nodiscard]] PluginManifest const& manifest() const noexcept { return manifest_; }

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  friend ava::core::Result<PluginInstallSource> inspect_plugin_install_source(std::filesystem::path const& resolved_source);

  PluginInstallSource(std::filesystem::path directory, PluginManifest manifest) : directory_(std::move(directory)), manifest_(std::move(manifest)) { }

  std::filesystem::path directory_;
  PluginManifest manifest_;
};

struct PluginInstallResult
{
  PluginManifest manifest;
  std::filesystem::path source_directory;
  std::filesystem::path destination_directory;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PluginRemovalResult
{
  PluginManifest manifest;
  std::filesystem::path removed_directory;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Classify a caller-resolved local path as a plugin install source directory.
// Accepts a real directory or a real plugin.json whose parent is a real directory.
[[nodiscard]] ava::core::Result<PluginInstallSource> inspect_plugin_install_source(std::filesystem::path const& resolved_source);

// Copy a validated local source into global_root/<id> via staging, without enabling or starting it.
[[nodiscard]] ava::core::Result<PluginInstallResult> install_global_plugin(PluginInstallSource source, std::filesystem::path const& global_root);

// Remove a disabled global plugin directory under global_root.
[[nodiscard]] ava::core::Result<PluginRemovalResult> remove_global_plugin(PluginStatus status, std::filesystem::path const& global_root);

}  // namespace ava::plugin
