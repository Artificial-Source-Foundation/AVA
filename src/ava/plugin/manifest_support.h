#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/error.h"
#include "ava/core/result.h"
#include "ava/plugin/manifest.h"

namespace ava::plugin::detail {

inline constexpr std::size_t kMaxPluginManifestBytes = 256 * 1024;

[[nodiscard]] ava::core::Error manifest_error(std::string message);
[[nodiscard]] bool is_valid_plugin_id(std::string_view id);
[[nodiscard]] bool is_valid_contribution_name(std::string_view name);
[[nodiscard]] bool is_valid_resource_path(std::string_view path);
[[nodiscard]] std::optional<std::string> array_field(std::string_view object, std::string_view key);
[[nodiscard]] ava::core::Result<std::vector<std::string>> string_array_field(std::string_view object,
                                                                             std::string_view key);
[[nodiscard]] std::vector<std::string> object_array_field(std::string_view object, std::string_view key);
[[nodiscard]] ava::core::Result<PluginEntrypoint> parse_plugin_entrypoint(std::string_view manifest_json);
[[nodiscard]] ava::core::Result<PluginContributions> parse_plugin_contributions(std::string_view manifest_json);
[[nodiscard]] ava::core::Result<std::string> read_plugin_manifest_file(std::filesystem::path const& manifest_path);

}  // namespace ava::plugin::detail
