#include "ava/plugin/discovery.h"

#include <algorithm>
#include <system_error>

#include "ava/config/xdg_paths.h"

namespace ava::plugin {
namespace {

ava::core::Error discovery_error(std::string message, const std::filesystem::path& path) {
  return ava::core::Error(ava::core::ErrorCategory::Io, std::move(message)).with_context("path", path.string());
}

ava::core::VoidResult discover_from_dir(const std::filesystem::path& root, PluginScope scope,
                                        std::vector<DiscoveredPlugin>& out) {
  if (root.empty()) return {};
  std::error_code exists_error;
  if (!std::filesystem::exists(root, exists_error)) return {};
  if (exists_error) return std::unexpected(discovery_error("failed to inspect plugin directory", root));

  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(root, iter_error), end; !iter_error && it != end;
       it.increment(iter_error)) {
    std::error_code entry_error;
    if (it->is_symlink(entry_error) || entry_error) continue;
    if (!it->is_directory(entry_error) || entry_error) continue;
    const auto manifest_path = it->path() / "plugin.json";
    std::error_code manifest_error;
    if (!std::filesystem::exists(manifest_path, manifest_error)) continue;
    if (manifest_error) return std::unexpected(discovery_error("failed to inspect plugin manifest", manifest_path));
    auto manifest = load_plugin_manifest(manifest_path);
    if (!manifest) return std::unexpected(manifest.error());
    out.push_back(DiscoveredPlugin{.manifest = std::move(*manifest), .scope = scope});
  }
  if (iter_error) return std::unexpected(discovery_error("failed to iterate plugin directory", root));
  return {};
}

}  // namespace

PluginDiscoveryOptions default_plugin_discovery_options(const std::filesystem::path& workspace_root) {
  const auto xdg = ava::config::xdg_paths();
  return PluginDiscoveryOptions{.global_plugins_dir = xdg.ava_config_dir / "plugins",
                                .project_plugins_dir = workspace_root / ".ava" / "plugins"};
}

ava::core::Result<std::vector<DiscoveredPlugin>> discover_plugins(const PluginDiscoveryOptions& options) {
  std::vector<DiscoveredPlugin> plugins;
  if (auto result = discover_from_dir(options.global_plugins_dir, PluginScope::Global, plugins); !result) {
    return std::unexpected(result.error());
  }
  if (auto result = discover_from_dir(options.project_plugins_dir, PluginScope::Project, plugins); !result) {
    return std::unexpected(result.error());
  }
  std::ranges::sort(plugins, [](const DiscoveredPlugin& left, const DiscoveredPlugin& right) {
    if (left.manifest.id != right.manifest.id) return left.manifest.id < right.manifest.id;
    return static_cast<int>(left.scope) < static_cast<int>(right.scope);
  });
  for (std::size_t index = 1; index < plugins.size(); ++index) {
    if (plugins[index - 1].manifest.id == plugins[index].manifest.id) {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "duplicate plugin id discovered")
              .with_context("plugin", plugins[index].manifest.id)
              .with_context("first_path", plugins[index - 1].manifest.path.string())
              .with_context("second_path", plugins[index].manifest.path.string()));
    }
  }
  return plugins;
}

}  // namespace ava::plugin
