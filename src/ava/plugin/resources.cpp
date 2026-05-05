#include "ava/plugin/resources.h"

#include <array>
#include <fstream>

namespace ava::plugin {
namespace {

bool path_is_within(std::filesystem::path const& base, std::filesystem::path const& target)
{
  if (target == base) return true;
  std::error_code relative_error;
  auto const relative = std::filesystem::relative(target, base, relative_error);
  if (relative_error || relative.empty()) return false;
  return *relative.begin() != "..";
}

}  // namespace

PluginResourceContribution const* find_plugin_resource(std::vector<PluginResourceContribution> const& resources,
                                                       std::string_view name)
{
  for (auto const& resource : resources) {
    if (resource.name == name) return &resource;
  }
  return nullptr;
}

ava::core::Result<std::filesystem::path> resolve_plugin_resource_path(PluginManifest const& manifest,
                                                                      PluginResourceContribution const& resource)
{
  std::error_code base_error;
  auto const canonical_base = std::filesystem::weakly_canonical(manifest.directory, base_error);
  auto const base_path = base_error ? std::filesystem::absolute(manifest.directory).lexically_normal() : canonical_base;
  auto const raw_target = manifest.directory / resource.path;
  std::error_code target_error;
  auto const canonical_target = std::filesystem::weakly_canonical(raw_target, target_error);
  auto const target_path = target_error ? std::filesystem::absolute(raw_target).lexically_normal() : canonical_target;
  if (!path_is_within(base_path, target_path)) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource path escapes plugin directory");
    error.with_context("plugin", manifest.id);
    error.with_context("resource", resource.name);
    error.with_context("path", resource.path);
    return std::unexpected(std::move(error));
  }
  return target_path;
}

ava::core::Result<std::string> read_plugin_resource_text(PluginManifest const& manifest,
                                                         PluginResourceContribution const& resource)
{
  auto path = resolve_plugin_resource_path(manifest, resource);
  if (!path) return std::unexpected(std::move(path.error()));

  std::error_code type_error;
  if (!std::filesystem::is_regular_file(*path, type_error)) {
    auto error = ava::core::Error(type_error ? ava::core::ErrorCategory::Io : ava::core::ErrorCategory::InvalidArgument,
                                  "plugin resource must be a regular file");
    error.with_context("plugin", manifest.id);
    error.with_context("resource", resource.name);
    error.with_context("path", path->string());
    if (type_error) error.with_context("cause", type_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(*path, size_error);
  if (size_error) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect plugin resource size")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string())
                               .with_context("cause", size_error.message()));
  }
  if (size > kMaxPluginResourceBytes) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource exceeds size cap")
            .with_context("plugin", manifest.id)
            .with_context("resource", resource.name)
            .with_context("path", path->string())
            .with_context("max_bytes", std::to_string(kMaxPluginResourceBytes)));
  }

  std::ifstream file(*path, std::ios::binary);
  if (!file) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open plugin resource")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string()));
  }
  std::string contents;
  std::array<char, 4096> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto const count = file.gcount();
    if (count <= 0) continue;
    if (contents.size() + static_cast<std::size_t>(count) > kMaxPluginResourceBytes) {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource exceeds size cap while reading")
              .with_context("plugin", manifest.id)
              .with_context("resource", resource.name)
              .with_context("path", path->string())
              .with_context("max_bytes", std::to_string(kMaxPluginResourceBytes)));
    }
    contents.append(buffer.data(), static_cast<std::size_t>(count));
  }
  if (file.bad()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read plugin resource")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string()));
  }
  return contents;
}

}  // namespace ava::plugin
