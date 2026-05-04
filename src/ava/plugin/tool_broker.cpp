#include "ava/plugin/tool_broker.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "ava/core/error.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/discovery.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/runner.h"

namespace ava::plugin {
namespace {

struct PluginToolBinding {
  PluginManifest manifest;
  PluginToolContribution contribution;
  std::string model_tool_name;
};

std::string json_bool(bool value)
{
  return value ? "true" : "false";
}

std::string error_json(std::string_view tool, ava::core::Error const& error)
{
  return "{\"tool\":\"" + ava::core::json::escape(tool) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
         ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) +
         "\"}}";
}

bool is_canceled_error(ava::core::Error const& error)
{
  return error.message().find("canceled") != std::string::npos ||
         error.message().find("cancelled") != std::string::npos;
}

bool is_canceled(ava::tools::ToolContext const& context)
{
  return context.cancel_requested && context.cancel_requested();
}

ava::agent::ToolDispatchResult tool_error_result(ava::agent::ProviderToolCall const& call,
                                                 ava::core::Error const& error)
{
  return ava::agent::ToolDispatchResult{.call_id = call.id,
                                        .name = call.name,
                                        .success = false,
                                        .result_text = error_json(call.name, error),
                                        .payload = [&] {
                                          ava::agent::ToolResultPayload payload;
                                          if (is_canceled_error(error)) {
                                            payload.status = ava::agent::ToolResultStatus::Canceled;
                                          }
                                          return payload;
                                        }()};
}

std::string result_json(ava::agent::ProviderToolCall const& call, PluginToolBinding const& binding,
                        PluginToolCallResult const& result)
{
  std::string text = "{\"tool\":\"" + ava::core::json::escape(call.name) + "\",\"ok\":" + json_bool(result.ok) +
                     ",\"plugin\":\"" + ava::core::json::escape(binding.manifest.id) + "\",\"plugin_tool\":\"" +
                     ava::core::json::escape(binding.contribution.name) + "\",\"content\":\"" +
                     ava::core::json::escape(result.content) + "\"";
  if (!result.metadata_json.empty()) text += ",\"metadata\":" + result.metadata_json;
  text += '}';
  return text;
}

ava::core::Error plugin_tool_error(ava::core::ErrorCategory category, std::string message,
                                   PluginToolBinding const& binding)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("plugin", binding.manifest.id);
  error.with_context("plugin_tool", binding.contribution.name);
  error.with_context("tool", binding.model_tool_name);
  if (!binding.manifest.path.empty()) error.with_context("manifest", binding.manifest.path.string());
  return error;
}

ava::agent::ToolDispatchResult dispatch_plugin_tool(ava::tools::ToolContext const& context,
                                                    ava::agent::ProviderToolCall const& call,
                                                    PluginToolBinding const& binding)
{
  if (is_canceled(context)) {
    return tool_error_result(
        call, plugin_tool_error(ava::core::ErrorCategory::Unknown, "plugin tool call canceled", binding));
  }
  if (!ava::core::json::is_valid_object(call.arguments_json)) {
    auto error = plugin_tool_error(ava::core::ErrorCategory::InvalidArgument,
                                   "plugin tool arguments must be a JSON object", binding);
    return tool_error_result(call, error);
  }

  auto tool_context = context;
  tool_context.permission_tool_name = call.name;
  tool_context.current_tool_name = call.name;
  tool_context.current_call_id = call.id;
  auto const command = binding.manifest.id + ":" + binding.contribution.name;
  if (auto permission =
          ava::tools::ensure_permission(tool_context, ava::permissions::Operation::PluginExecute, binding.manifest.path,
                                        binding.manifest.id, call.name, "plugin process launch requires permission");
      !permission) {
    return tool_error_result(call, permission.error());
  }
  if (is_canceled(tool_context)) {
    return tool_error_result(
        call, plugin_tool_error(ava::core::ErrorCategory::Unknown, "plugin tool call canceled", binding));
  }
  if (auto permission = ava::tools::ensure_permission(tool_context, ava::permissions::Operation::PluginToolCall,
                                                      binding.manifest.path, command, call.name,
                                                      "plugin tool call requires permission");
      !permission) {
    return tool_error_result(call, permission.error());
  }
  if (is_canceled(tool_context)) {
    return tool_error_result(
        call, plugin_tool_error(ava::core::ErrorCategory::Unknown, "plugin tool call canceled", binding));
  }

  PluginRunnerOptions options;
  options.workspace_dir = context.workspace_dir;
  auto process = PluginProcess::start(binding.manifest, options, context.cancel_requested);
  if (!process) return tool_error_result(call, process.error());

  auto result =
      (*process)->call_tool(binding.contribution.name, call.arguments_json, call.id, context.cancel_requested);
  auto shutdown = (*process)->shutdown();
  if (!result) return tool_error_result(call, result.error());
  if (!shutdown) return tool_error_result(call, shutdown.error());

  return ava::agent::ToolDispatchResult{
      .call_id = call.id, .name = call.name, .success = result->ok, .result_text = result_json(call, binding, *result)};
}

std::string schema_json(std::string_view model_tool_name, PluginToolContribution const& contribution)
{
  auto const description =
      contribution.description.empty() ? std::string("Plugin tool ") + contribution.name : contribution.description;
  return "{\"type\":\"function\",\"name\":\"" + ava::core::json::escape(model_tool_name) + "\",\"description\":\"" +
         ava::core::json::escape(description) + "\",\"parameters\":" + contribution.input_schema_json + '}';
}

ava::agent::RegisteredToolMetadata metadata_for_tool(std::string model_tool_name,
                                                     PluginToolContribution const& contribution)
{
  auto const description =
      contribution.description.empty() ? std::string("Plugin tool ") + contribution.name : contribution.description;
  auto const schema = schema_json(model_tool_name, contribution);
  return ava::agent::RegisteredToolMetadata{
      .name = std::move(model_tool_name),
      .description = description,
      .schema_json = schema,
      .permission_category = "plugin.tool.call",
      .output_bound_summary = "Plugin tool output is bounded by JSONL record size",
      .execution_mode = "plugin_process",
      .event_rendering_hint = "plugin_tool",
      .description_family = "plugin"};
}

PluginDiscoveryOptions discovery_options_for_context(ava::tools::ToolContext const& context)
{
  auto options = default_plugin_discovery_options(context.workspace_dir);
  if (!context.plugin_global_plugins_dir.empty()) options.global_plugins_dir = context.plugin_global_plugins_dir;
  if (!context.plugin_project_plugins_dir.empty()) options.project_plugins_dir = context.plugin_project_plugins_dir;
  return options;
}

std::filesystem::path enablement_file_for_context(ava::tools::ToolContext const& context)
{
  if (!context.plugin_enablement_file.empty()) return context.plugin_enablement_file;
  return default_plugin_enablement_file();
}

}  // namespace

std::string plugin_model_tool_name(std::string_view plugin_id, std::string_view tool_name)
{
  auto sanitize = [](std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    bool last_was_separator = false;
    for (char const ch : value) {
      auto const byte = static_cast<unsigned char>(ch);
      if (std::isalnum(byte) != 0) {
        sanitized.push_back(static_cast<char>(std::tolower(byte)));
        last_was_separator = false;
      } else if (!last_was_separator) {
        sanitized.push_back('_');
        last_was_separator = true;
      }
    }
    while (!sanitized.empty() && sanitized.back() == '_') sanitized.pop_back();
    if (sanitized.empty()) return std::string("tool");
    return sanitized;
  };
  return "plugin_" + sanitize(plugin_id) + "_" + sanitize(tool_name);
}

void register_enabled_plugin_tools(ava::agent::ToolRegistry& registry, ava::tools::ToolContext const& context)
{
  if (context.workspace_dir.empty()) return;

  auto diagnostics = collect_plugin_diagnostics(discovery_options_for_context(context),
                                                enablement_file_for_context(context), context.workspace_dir);
  for (auto const& status : diagnostics.plugins) {
    if (!status.enabled) continue;
    auto const& manifest = status.plugin.manifest;
    for (auto const& contribution : manifest.contributes.tools) {
      auto const model_tool_name = plugin_model_tool_name(manifest.id, contribution.name);
      if (registry.find(model_tool_name) != nullptr) continue;

      auto binding = std::make_shared<PluginToolBinding const>(
          PluginToolBinding{.manifest = manifest, .contribution = contribution, .model_tool_name = model_tool_name});
      auto registered = registry.register_tool(ava::agent::RegisteredTool{
          .metadata = metadata_for_tool(model_tool_name, contribution),
          .executor =
              [binding](ava::tools::ToolContext const& tool_context, ava::agent::ProviderToolCall const& call) {
                return dispatch_plugin_tool(tool_context, call, *binding);
              },
          .source = ava::agent::ToolSource::Plugin,
          .source_id = manifest.id,
          .brokered_external = true,
          .requires_lsp_diagnostics = false});
      (void)registered;
    }
  }
}

}  // namespace ava::plugin
