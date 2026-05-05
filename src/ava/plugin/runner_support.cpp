#include "ava/plugin/runner_support.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <utility>

namespace ava::plugin::detail {

ava::core::Error plugin_error(ava::core::ErrorCategory category, std::string message, PluginManifest const& manifest)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("plugin", manifest.id);
  if (!manifest.path.empty()) error.with_context("manifest", manifest.path.string());
  return error;
}

ava::core::Error errno_error(std::string message, PluginManifest const& manifest)
{
  auto error = plugin_error(ava::core::ErrorCategory::Io, std::move(message), manifest);
  error.with_context("cause", std::strerror(errno));
  return error;
}

ava::core::Error protocol_error(std::string message, PluginManifest const& manifest)
{
  return plugin_error(ava::core::ErrorCategory::Tool, std::move(message), manifest);
}

ava::core::Error canceled_error(std::string message, PluginManifest const& manifest)
{
  auto error = plugin_error(ava::core::ErrorCategory::Unknown, std::move(message), manifest);
  error.with_context("canceled", "true");
  return error;
}

bool is_canceled(CancelCallback const& cancel_requested)
{
  return cancel_requested && cancel_requested();
}

std::vector<std::string> plugin_argv(PluginManifest const& manifest)
{
  std::vector<std::string> argv;
  argv.reserve(manifest.entrypoint.args.size() + 1);
  argv.push_back(manifest.entrypoint.command);
  argv.insert(argv.end(), manifest.entrypoint.args.begin(), manifest.entrypoint.args.end());
  return argv;
}

std::filesystem::path child_working_dir(PluginManifest const& manifest, PluginRunnerOptions const& options)
{
  if (!manifest.directory.empty()) return manifest.directory;
  if (!options.workspace_dir.empty()) return options.workspace_dir;
  return std::filesystem::current_path();
}

ava::core::VoidResult validate_start_request(PluginManifest const& manifest, PluginRunnerOptions& options,
                                             CancelCallback const& cancel_requested)
{
  if (manifest.entrypoint.command.empty()) {
    return std::unexpected(plugin_error(ava::core::ErrorCategory::InvalidArgument,
                                        "plugin entrypoint command must not be empty", manifest));
  }
  if (options.workspace_dir.empty()) options.workspace_dir = std::filesystem::current_path();
  if (options.startup_timeout < std::chrono::milliseconds(50) || options.startup_timeout > std::chrono::seconds(30)) {
    auto error =
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin startup timeout is out of bounds", manifest);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.request_timeout < std::chrono::milliseconds(50) || options.request_timeout > std::chrono::seconds(30)) {
    auto error =
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin request timeout is out of bounds", manifest);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.max_record_bytes == 0 || options.max_stderr_bytes == 0) {
    return std::unexpected(plugin_error(ava::core::ErrorCategory::InvalidArgument,
                                        "plugin runner byte limits must be non-zero", manifest));
  }
  if (is_canceled(cancel_requested)) {
    return std::unexpected(canceled_error("plugin startup canceled", manifest));
  }
  return {};
}

}  // namespace ava::plugin::detail
