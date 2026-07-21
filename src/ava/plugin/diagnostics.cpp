#include "sys.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/enablement.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace ava::plugin {
namespace {

PluginFailure make_failure(PluginScope scope, std::filesystem::path const& path, ava::core::Error const& error,
                           PluginFailureKind kind = PluginFailureKind::Discovery)
{
  auto const failure = ava::diagnostics::safe_failure_from_error(ava::diagnostics::ComponentClass::Plugin, error);
  return PluginFailure{.scope = scope,
                       .path = path,
                       .kind = kind,
                       .failure = failure,
                       .message = ava::diagnostics::serialize_safe_failure_human(failure),
                       .details = ava::diagnostics::serialize_safe_failure_json(failure)};
}

PluginFailure make_failure(PluginScope scope, std::filesystem::path const& path, ava::core::ErrorCategory category,
                           PluginFailureKind kind = PluginFailureKind::Discovery)
{
  auto const error = ava::core::Error(category, "plugin diagnostic failure");
  return make_failure(scope, path, error, kind);
}

bool is_install_staging_directory(std::filesystem::path const& path)
{
  return path.filename().generic_string().find(".installing-") != std::string::npos;
}

void collect_from_dir(std::filesystem::path const& root, PluginScope scope, PluginDiagnostics& diagnostics)
{
  if (root.empty())
    return;
  std::error_code exists_error;
  if (!std::filesystem::exists(root, exists_error))
    return;
  if (exists_error)
  {
    diagnostics.failures.push_back(make_failure(scope, root, ava::core::ErrorCategory::Io));
    return;
  }

  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(root, iter_error), end; !iter_error && it != end; it.increment(iter_error))
  {
    std::error_code entry_error;
    if (it->is_symlink(entry_error) || entry_error)
      continue;
    if (!it->is_directory(entry_error) || entry_error)
      continue;
    if (scope == PluginScope::Global && is_install_staging_directory(it->path()))
      continue;
    auto const manifest_path = it->path() / "plugin.json";
    std::error_code manifest_error;
    if (!std::filesystem::exists(manifest_path, manifest_error))
      continue;
    if (manifest_error)
    {
      diagnostics.failures.push_back(make_failure(scope, manifest_path, ava::core::ErrorCategory::Io));
      continue;
    }
    auto manifest = load_plugin_manifest(manifest_path);
    if (!manifest)
    {
      diagnostics.failures.push_back(make_failure(scope, manifest_path, manifest.error()));
      continue;
    }
    diagnostics.plugins.push_back(PluginStatus{.plugin = DiscoveredPlugin{.manifest = std::move(*manifest), .scope = scope}});
  }
  if (iter_error)
  {
    diagnostics.failures.push_back(make_failure(scope, root, ava::core::ErrorCategory::Io));
  }
}

void disable_duplicate_ids(PluginDiagnostics& diagnostics)
{
  std::ranges::sort(diagnostics.plugins, [](PluginStatus const& left, PluginStatus const& right) {
    if (left.plugin.manifest.id != right.plugin.manifest.id)
      return left.plugin.manifest.id < right.plugin.manifest.id;
    return static_cast<int>(left.plugin.scope) < static_cast<int>(right.plugin.scope);
  });

  std::vector<std::string> duplicate_ids;
  for (std::size_t index = 0; index < diagnostics.plugins.size();)
  {
    auto const start = index;
    while (index < diagnostics.plugins.size() && diagnostics.plugins[index].plugin.manifest.id == diagnostics.plugins[start].plugin.manifest.id)
    {
      ++index;
    }
    if (index - start > 1)
      duplicate_ids.push_back(diagnostics.plugins[start].plugin.manifest.id);
  }
  if (duplicate_ids.empty())
    return;

  for (auto const& status : diagnostics.plugins)
  {
    if (std::ranges::find(duplicate_ids, status.plugin.manifest.id) == duplicate_ids.end())
      continue;
    diagnostics.failures.push_back(
        make_failure(status.plugin.scope, status.plugin.manifest.path, ava::core::ErrorCategory::InvalidArgument, PluginFailureKind::DuplicateId));
  }
  std::erase_if(diagnostics.plugins,
                [&](PluginStatus const& status) { return std::ranges::find(duplicate_ids, status.plugin.manifest.id) != duplicate_ids.end(); });
}

void apply_enablement(std::filesystem::path const& enablement_file, std::filesystem::path const& workspace_root, PluginDiagnostics& diagnostics)
{
  auto records = load_plugin_enablement(enablement_file);
  if (!records)
  {
    diagnostics.failures.push_back(make_failure(PluginScope::Project, enablement_file, records.error(), PluginFailureKind::Enablement));
    return;
  }
  auto const workspace = canonical_workspace_key(workspace_root);
  for (auto& status : diagnostics.plugins)
  {
    for (auto const& record : *records)
    {
      if (record.workspace == workspace && record.plugin_id == status.plugin.manifest.id && record.scope == status.plugin.scope)
      {
        status.enabled = record.enabled;
        break;
      }
    }
  }
}

}  // namespace

PluginDiagnostics collect_plugin_diagnostics(PluginDiscoveryOptions const& options, std::filesystem::path const& enablement_file,
                                             std::filesystem::path const& workspace_root)
{
  PluginDiagnostics diagnostics{.discovery_options = options, .enablement_file = enablement_file, .plugins = {}, .failures = {}};
  collect_from_dir(options.global_plugins_dir, PluginScope::Global, diagnostics);
  collect_from_dir(options.project_plugins_dir, PluginScope::Project, diagnostics);
  disable_duplicate_ids(diagnostics);
  apply_enablement(enablement_file, workspace_root, diagnostics);
  return diagnostics;
}

}  // namespace ava::plugin
