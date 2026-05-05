#include "ava/plugin/manifest.h"

#include <string>
#include <utility>

#include "ava/core/json.h"
#include "ava/plugin/manifest_support.h"

namespace ava::plugin {

std::string_view to_string(PluginScope scope)
{
  switch (scope) {
    case PluginScope::Global:
      return "global";
    case PluginScope::Project:
      return "project";
  }
  return "unknown";
}

ava::core::Result<PluginManifest> parse_plugin_manifest(std::string_view json, std::filesystem::path manifest_path)
{
  if (json.size() > detail::kMaxPluginManifestBytes) {
    return std::unexpected(detail::manifest_error("plugin manifest exceeds maximum size")
                               .with_context("max_bytes", std::to_string(detail::kMaxPluginManifestBytes)));
  }
  if (!ava::core::json::is_valid_object(json)) {
    return std::unexpected(detail::manifest_error("plugin manifest must be a valid JSON object"));
  }

  auto const schema_version = ava::core::json::integer_field(json, "schema_version");
  if (!schema_version || *schema_version != 1) {
    return std::unexpected(detail::manifest_error("plugin manifest schema_version must be 1"));
  }
  auto id = ava::core::json::string_field(json, "id");
  if (!id || !detail::is_valid_plugin_id(*id)) {
    return std::unexpected(detail::manifest_error("plugin manifest id must be lowercase and stable"));
  }
  auto name = ava::core::json::string_field(json, "name");
  if (!name || name->empty()) return std::unexpected(detail::manifest_error("plugin manifest requires name"));
  auto version = ava::core::json::string_field(json, "version");
  if (!version || version->empty()) return std::unexpected(detail::manifest_error("plugin manifest requires version"));
  auto api_version = ava::core::json::string_field(json, "api_version");
  if (!api_version || *api_version != kPluginApiVersion) {
    return std::unexpected(detail::manifest_error("plugin manifest api_version is unsupported")
                               .with_context("expected", std::string(kPluginApiVersion)));
  }
  auto entrypoint = detail::parse_plugin_entrypoint(json);
  if (!entrypoint) return std::unexpected(entrypoint.error());
  auto capabilities = detail::string_array_field(json, "capabilities");
  if (!capabilities) return std::unexpected(capabilities.error());
  auto contributes = detail::parse_plugin_contributions(json);
  if (!contributes) return std::unexpected(contributes.error());

  auto const manifest_directory = manifest_path.empty() ? std::filesystem::path{} : manifest_path.parent_path();
  return PluginManifest{.schema_version = static_cast<int>(*schema_version),
                        .id = std::move(*id),
                        .name = std::move(*name),
                        .version = std::move(*version),
                        .api_version = std::move(*api_version),
                        .description = ava::core::json::string_field(json, "description").value_or(""),
                        .entrypoint = std::move(*entrypoint),
                        .capabilities = std::move(*capabilities),
                        .contributes = std::move(*contributes),
                        .path = std::move(manifest_path),
                        .directory = manifest_directory};
}

ava::core::Result<PluginManifest> load_plugin_manifest(std::filesystem::path const& manifest_path)
{
  auto contents = detail::read_plugin_manifest_file(manifest_path);
  if (!contents) return std::unexpected(std::move(contents.error()));
  auto parsed = parse_plugin_manifest(*contents, manifest_path);
  if (!parsed) parsed.error().with_context("path", manifest_path.string());
  return parsed;
}

}  // namespace ava::plugin
