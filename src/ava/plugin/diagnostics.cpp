#include "ava/plugin/diagnostics.h"

#include <algorithm>
#include <system_error>
#include <utility>

#include "ava/plugin/enablement.h"

namespace ava::plugin {
namespace {

PluginFailure make_failure(PluginScope scope, const std::filesystem::path& path, const ava::core::Error& error) {
  return PluginFailure{.scope = scope, .path = path, .message = error.message(), .details = error.format()};
}

PluginFailure make_failure(PluginScope scope, const std::filesystem::path& path, std::string message,
                           std::string details = {}) {
  return PluginFailure{.scope = scope, .path = path, .message = std::move(message), .details = std::move(details)};
}

void collect_from_dir(const std::filesystem::path& root, PluginScope scope, PluginDiagnostics& diagnostics) {
  if (root.empty()) return;
  std::error_code exists_error;
  if (!std::filesystem::exists(root, exists_error)) return;
  if (exists_error) {
    diagnostics.failures.push_back(
        make_failure(scope, root, "failed to inspect plugin directory", exists_error.message()));
    return;
  }

  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(root, iter_error), end; !iter_error && it != end;
       it.increment(iter_error)) {
    std::error_code entry_error;
    if (it->is_symlink(entry_error) || entry_error) continue;
    if (!it->is_directory(entry_error) || entry_error) continue;
    const auto manifest_path = it->path() / "plugin.json";
    std::error_code manifest_error;
    if (!std::filesystem::exists(manifest_path, manifest_error)) continue;
    if (manifest_error) {
      diagnostics.failures.push_back(
          make_failure(scope, manifest_path, "failed to inspect plugin manifest", manifest_error.message()));
      continue;
    }
    auto manifest = load_plugin_manifest(manifest_path);
    if (!manifest) {
      diagnostics.failures.push_back(make_failure(scope, manifest_path, manifest.error()));
      continue;
    }
    diagnostics.plugins.push_back(
        PluginStatus{.plugin = DiscoveredPlugin{.manifest = std::move(*manifest), .scope = scope}});
  }
  if (iter_error) {
    diagnostics.failures.push_back(
        make_failure(scope, root, "failed to iterate plugin directory", iter_error.message()));
  }
}

void disable_duplicate_ids(PluginDiagnostics& diagnostics) {
  std::ranges::sort(diagnostics.plugins, [](const PluginStatus& left, const PluginStatus& right) {
    if (left.plugin.manifest.id != right.plugin.manifest.id) return left.plugin.manifest.id < right.plugin.manifest.id;
    return static_cast<int>(left.plugin.scope) < static_cast<int>(right.plugin.scope);
  });

  std::vector<std::string> duplicate_ids;
  for (std::size_t index = 0; index < diagnostics.plugins.size();) {
    const auto start = index;
    while (index < diagnostics.plugins.size() &&
           diagnostics.plugins[index].plugin.manifest.id == diagnostics.plugins[start].plugin.manifest.id) {
      ++index;
    }
    if (index - start > 1) duplicate_ids.push_back(diagnostics.plugins[start].plugin.manifest.id);
  }
  if (duplicate_ids.empty()) return;

  for (const auto& status : diagnostics.plugins) {
    if (std::ranges::find(duplicate_ids, status.plugin.manifest.id) == duplicate_ids.end()) continue;
    diagnostics.failures.push_back(make_failure(status.plugin.scope, status.plugin.manifest.path,
                                                "duplicate plugin id discovered",
                                                "plugin=" + status.plugin.manifest.id));
  }
  std::erase_if(diagnostics.plugins, [&](const PluginStatus& status) {
    return std::ranges::find(duplicate_ids, status.plugin.manifest.id) != duplicate_ids.end();
  });
}

void apply_enablement(const std::filesystem::path& enablement_file, const std::filesystem::path& workspace_root,
                      PluginDiagnostics& diagnostics) {
  auto records = load_plugin_enablement(enablement_file);
  if (!records) {
    diagnostics.failures.push_back(PluginFailure{.scope = PluginScope::Project,
                                                 .path = enablement_file,
                                                 .message = records.error().message(),
                                                 .details = records.error().format()});
    return;
  }
  const auto workspace = canonical_workspace_key(workspace_root);
  for (auto& status : diagnostics.plugins) {
    for (const auto& record : *records) {
      if (record.workspace == workspace && record.plugin_id == status.plugin.manifest.id &&
          record.scope == status.plugin.scope) {
        status.enabled = record.enabled;
        break;
      }
    }
  }
}

}  // namespace

PluginDiagnostics collect_plugin_diagnostics(const PluginDiscoveryOptions& options,
                                             const std::filesystem::path& enablement_file,
                                             const std::filesystem::path& workspace_root) {
  PluginDiagnostics diagnostics{
      .discovery_options = options, .enablement_file = enablement_file, .plugins = {}, .failures = {}};
  collect_from_dir(options.global_plugins_dir, PluginScope::Global, diagnostics);
  collect_from_dir(options.project_plugins_dir, PluginScope::Project, diagnostics);
  disable_duplicate_ids(diagnostics);
  apply_enablement(enablement_file, workspace_root, diagnostics);
  return diagnostics;
}

}  // namespace ava::plugin
