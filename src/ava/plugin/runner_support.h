#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/plugin/manifest.h"
#include "ava/plugin/runner.h"

namespace ava::plugin::detail {

[[nodiscard]] ava::core::Error plugin_error(ava::core::ErrorCategory category, std::string message,
                                            PluginManifest const& manifest);
[[nodiscard]] ava::core::Error errno_error(std::string message, PluginManifest const& manifest);
[[nodiscard]] ava::core::Error protocol_error(std::string message, PluginManifest const& manifest);
[[nodiscard]] ava::core::Error canceled_error(std::string message, PluginManifest const& manifest);
[[nodiscard]] bool is_canceled(CancelCallback const& cancel_requested);
[[nodiscard]] std::vector<std::string> plugin_argv(PluginManifest const& manifest);
[[nodiscard]] std::filesystem::path child_working_dir(PluginManifest const& manifest,
                                                      PluginRunnerOptions const& options);
[[nodiscard]] ava::core::VoidResult validate_start_request(PluginManifest const& manifest, PluginRunnerOptions& options,
                                                           CancelCallback const& cancel_requested);

}  // namespace ava::plugin::detail
