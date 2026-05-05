#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/app/runtime.h"
#include "ava/core/result.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/manifest.h"
#include "ava/plugin/resources.h"

namespace ava::app::detail {

struct PluginRunArguments {
  std::string plugin_id;
  std::string command_name;
  std::string arguments_json = "{}";
};

[[nodiscard]] std::string plugin_display_path(std::filesystem::path const& path, RuntimeSession const& session);
[[nodiscard]] ava::plugin::PluginDiscoveryOptions plugin_discovery_options(RuntimeSession const& session);
[[nodiscard]] std::filesystem::path plugin_enablement_file(RuntimeSession const& session);
[[nodiscard]] ava::plugin::PluginDiagnostics plugin_diagnostics(RuntimeSession const& session);
[[nodiscard]] std::string plugin_scope_text(ava::plugin::PluginScope scope);
[[nodiscard]] std::string plugin_status_text(bool enabled);
[[nodiscard]] std::string plugin_capabilities_text(ava::plugin::PluginManifest const& manifest);
[[nodiscard]] std::string plugin_entrypoint_text(ava::plugin::PluginEntrypoint const& entrypoint);
[[nodiscard]] ava::plugin::PluginCommandContribution const* find_plugin_command(
    ava::plugin::PluginManifest const& manifest, std::string_view command_name);
[[nodiscard]] std::string format_plugin_resource_list_text(
    ava::plugin::PluginStatus const& status, RuntimeSession const& session, std::string_view label,
    std::vector<ava::plugin::PluginResourceContribution> const& resources);
[[nodiscard]] std::string format_plugin_resource_text(ava::plugin::PluginManifest const& manifest,
                                                      ava::plugin::PluginResourceContribution const& resource,
                                                      std::string_view label, RuntimeSession const& session,
                                                      std::string content);
[[nodiscard]] ava::plugin::PluginStatus const* find_plugin_status(ava::plugin::PluginDiagnostics const& diagnostics,
                                                                  std::string_view plugin_id);
[[nodiscard]] bool has_duplicate_plugin_failure(ava::plugin::PluginDiagnostics const& diagnostics,
                                                std::string_view plugin_id);
[[nodiscard]] std::string plugin_not_found_text(ava::plugin::PluginDiagnostics const& diagnostics,
                                                std::string_view plugin_id);
[[nodiscard]] std::string format_plugin_list_text(ava::plugin::PluginDiagnostics const& diagnostics,
                                                  RuntimeSession const& session);
[[nodiscard]] std::string format_plugin_failure_text(ava::plugin::PluginFailure const& failure,
                                                     RuntimeSession const& session);
[[nodiscard]] std::string format_plugin_failures_text(ava::plugin::PluginDiagnostics const& diagnostics,
                                                      RuntimeSession const& session);
[[nodiscard]] std::string format_plugin_inspect_text(ava::plugin::PluginStatus const& status,
                                                     RuntimeSession const& session);
[[nodiscard]] std::string format_valid_plugin_manifest_text(ava::plugin::PluginManifest const& manifest,
                                                            RuntimeSession const& session);
[[nodiscard]] std::filesystem::path plugin_validate_path(RuntimeSession const& session, std::string_view path_text);
[[nodiscard]] std::string trim_ascii_whitespace(std::string_view text);
[[nodiscard]] std::string plugin_validate_argument(std::string_view plugins_argument);
[[nodiscard]] std::optional<std::string_view> consume_token(std::string_view& text);
[[nodiscard]] ava::core::Result<PluginRunArguments> parse_plugin_run_arguments(std::string_view argument);

}  // namespace ava::app::detail
