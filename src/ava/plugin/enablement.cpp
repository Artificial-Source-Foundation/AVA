#include "ava/plugin/enablement.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "ava/config/xdg_paths.h"
#include "ava/plugin/enablement_support.h"
#include "ava/plugin/manifest_support.h"

namespace ava::plugin {

std::filesystem::path default_plugin_enablement_file()
{
  return ava::config::xdg_paths().ava_state_dir / "plugin-enablement.json";
}

std::filesystem::path canonical_workspace_key(std::filesystem::path const& workspace_root)
{
  std::error_code canonical_error;
  auto canonical = std::filesystem::weakly_canonical(workspace_root, canonical_error);
  if (!canonical_error && canonical.is_absolute()) return canonical.lexically_normal();
  auto absolute = std::filesystem::absolute(workspace_root, canonical_error);
  if (!canonical_error) return absolute.lexically_normal();
  return workspace_root.lexically_normal();
}

ava::core::Result<std::vector<PluginEnablementRecord>> load_plugin_enablement(std::filesystem::path const& state_file)
{
  if (state_file.empty() || !std::filesystem::exists(state_file)) return std::vector<PluginEnablementRecord>{};
  auto json = detail::read_plugin_enablement_file(state_file);
  if (!json) return std::unexpected(std::move(json.error()));
  return detail::parse_plugin_enablement_json(*json, state_file);
}

ava::core::Result<bool> plugin_enabled(std::filesystem::path const& state_file,
                                       std::filesystem::path const& workspace_root, std::string_view plugin_id,
                                       PluginScope scope)
{
  auto records = load_plugin_enablement(state_file);
  if (!records) return std::unexpected(std::move(records.error()));
  auto const workspace = canonical_workspace_key(workspace_root);
  for (auto const& record : *records) {
    if (record.workspace == workspace && record.plugin_id == plugin_id && record.scope == scope) return record.enabled;
  }
  return false;
}

ava::core::VoidResult set_plugin_enabled(std::filesystem::path const& state_file,
                                         std::filesystem::path const& workspace_root, std::string_view plugin_id,
                                         bool enabled, PluginScope scope)
{
  if (!detail::is_valid_plugin_id(plugin_id)) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin id is invalid for enablement state"));
  }
  auto records = load_plugin_enablement(state_file);
  if (!records) return std::unexpected(std::move(records.error()));
  detail::upsert_plugin_enablement_record(*records, canonical_workspace_key(workspace_root), plugin_id, scope, enabled);
  detail::sort_plugin_enablement_records(*records);
  return detail::write_plugin_enablement_file_atomic(state_file, detail::plugin_enablement_json(*records));
}

}  // namespace ava::plugin
