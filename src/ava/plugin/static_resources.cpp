#include "ava/plugin/static_resources.h"
#include "ava/core/error.h"

#include <system_error>

namespace ava::plugin {
namespace {

std::filesystem::path normalized_absolute(std::filesystem::path const& path)
{
  std::error_code error;
  auto normalized = std::filesystem::weakly_canonical(path, error);
  if (!error)
    return normalized.lexically_normal();
  return std::filesystem::absolute(path, error).lexically_normal();
}

bool path_is_within(std::filesystem::path const& base, std::filesystem::path const& target)
{
  if (target == base)
    return true;
  std::error_code relative_error;
  auto const relative = std::filesystem::relative(target, base, relative_error);
  if (relative_error || relative.empty())
    return false;
  auto const first = relative.begin();
  if (first == relative.end())
    return false;
  auto const first_part = *first;
  return first_part != ".." && !first_part.is_absolute();
}

}  // namespace

std::filesystem::path plugin_static_resource_display_path(PluginManifest const& manifest, PluginResourceContribution const& resource)
{
  std::error_code error;
  return std::filesystem::absolute(manifest.directory / resource.path, error).lexically_normal();
}

ava::core::Result<std::filesystem::path> plugin_static_resource_path(PluginManifest const& manifest, PluginResourceContribution const& resource)
{
  auto const base_path = normalized_absolute(manifest.directory);
  auto const raw_target = plugin_static_resource_display_path(manifest, resource);
  auto const target_path = normalized_absolute(raw_target);
  if (!path_is_within(base_path, target_path))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource path escapes plugin directory");
    error.with_context("plugin", manifest.id);
    error.with_context("resource", resource.name);
    error.with_context("path", resource.path);
    return std::unexpected(std::move(error));
  }
  return raw_target;
}

std::vector<PluginStaticSkillFile> enabled_plugin_static_skill_files(PluginDiagnostics const& diagnostics)
{
  std::vector<PluginStaticSkillFile> files;
  for (auto const& status : diagnostics.plugins)
  {
    if (!status.enabled)
      continue;
    auto const& manifest = status.plugin.manifest;
    for (auto const& skill : manifest.contributes.skills)
    {
      auto path = plugin_static_resource_path(manifest, skill);
      if (!path)
        continue;
      files.push_back(PluginStaticSkillFile{.scope = std::string(to_string(status.plugin.scope)),
                                            .plugin_id = manifest.id,
                                            .name = skill.name,
                                            .description = skill.description,
                                            .path = std::move(*path)});
    }
  }
  return files;
}

}  // namespace ava::plugin
