#include "ava/mcp/tool_broker.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>

#include "ava/core/json.h"
#include "ava/mcp/config.h"
#include "ava/mcp/stdio_client.h"
#include "ava/permissions/permission.h"

namespace ava::mcp {
namespace {

struct McpToolBinding {
  McpServerConfig server;
  McpToolDescription tool;
  std::string model_tool_name;
};

std::string json_bool(bool value) { return value ? "true" : "false"; }

std::string error_json(std::string_view tool, ava::core::Error const& error) {
  return "{\"tool\":\"" + ava::core::json::escape(tool) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
         ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) +
         "\"}}";
}

bool is_canceled_error(ava::core::Error const& error) {
  return error.message().find("canceled") != std::string::npos ||
         error.message().find("cancelled") != std::string::npos;
}

bool is_canceled(ava::tools::ToolContext const& context) {
  return context.cancel_requested && context.cancel_requested();
}

ava::agent::ToolDispatchResult tool_error_result(ava::agent::ProviderToolCall const& call,
                                                 ava::core::Error const& error) {
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

ava::core::Error mcp_tool_error(ava::core::ErrorCategory category, std::string message, McpToolBinding const& binding) {
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("mcp_server", binding.server.id);
  error.with_context("mcp_tool", binding.tool.name);
  error.with_context("tool", binding.model_tool_name);
  if (!binding.server.source_path.empty()) error.with_context("config", binding.server.source_path.string());
  return error;
}

ava::core::Error mcp_server_error(ava::core::ErrorCategory category, std::string message,
                                  McpServerConfig const& server) {
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("mcp_server", server.id);
  if (!server.source_path.empty()) error.with_context("config", server.source_path.string());
  return error;
}

std::string command_text(McpServerConfig const& server) {
  std::string text = server.command;
  for (auto const& arg : server.args) text += " " + arg;
  return text;
}

ava::core::VoidResult ensure_mcp_server_permission(ava::tools::ToolContext const& context,
                                                   McpServerConfig const& server, std::string_view tool_name) {
  if (auto permission =
          ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerLaunch, server.source_path,
                                        command_text(server), tool_name, "MCP server launch requires permission");
      !permission) {
    return std::unexpected(std::move(permission.error()));
  }
  if (auto permission =
          ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerConnect, server.source_path,
                                        server.id, tool_name, "MCP server connection requires permission");
      !permission) {
    return std::unexpected(std::move(permission.error()));
  }
  return {};
}

McpStdioClientOptions client_options_for_context(ava::tools::ToolContext const& context) {
  McpStdioClientOptions options;
  options.workspace_dir = context.workspace_dir;
  return options;
}

std::string result_json(ava::agent::ProviderToolCall const& call, McpToolBinding const& binding,
                        McpToolCallResult const& result) {
  return "{\"tool\":\"" + ava::core::json::escape(call.name) + "\",\"ok\":" + json_bool(!result.is_error) +
         ",\"server\":\"" + ava::core::json::escape(binding.server.id) + "\",\"mcp_tool\":\"" +
         ava::core::json::escape(binding.tool.name) + "\",\"content\":\"" + ava::core::json::escape(result.content) +
         "\"}";
}

ava::agent::ToolDispatchResult dispatch_mcp_tool(ava::tools::ToolContext const& context,
                                                 ava::agent::ProviderToolCall const& call,
                                                 McpToolBinding const& binding) {
  if (is_canceled(context)) {
    return tool_error_result(call,
                             mcp_tool_error(ava::core::ErrorCategory::Unknown, "MCP tool call canceled", binding));
  }
  if (!ava::core::json::is_valid_object(call.arguments_json)) {
    auto error =
        mcp_tool_error(ava::core::ErrorCategory::InvalidArgument, "MCP tool arguments must be a JSON object", binding);
    return tool_error_result(call, error);
  }

  auto tool_context = context;
  tool_context.permission_tool_name = call.name;
  tool_context.current_tool_name = call.name;
  tool_context.current_call_id = call.id;
  if (auto permission = ensure_mcp_server_permission(tool_context, binding.server, call.name); !permission) {
    return tool_error_result(call, permission.error());
  }
  if (is_canceled(tool_context)) {
    return tool_error_result(call,
                             mcp_tool_error(ava::core::ErrorCategory::Unknown, "MCP tool call canceled", binding));
  }
  auto const command = binding.server.id + ":" + binding.tool.name;
  if (auto permission = ava::tools::ensure_permission(tool_context, ava::permissions::Operation::McpToolCall,
                                                      binding.server.source_path, command, call.name,
                                                      "MCP tool call requires permission");
      !permission) {
    return tool_error_result(call, permission.error());
  }
  if (is_canceled(tool_context)) {
    return tool_error_result(call,
                             mcp_tool_error(ava::core::ErrorCategory::Unknown, "MCP tool call canceled", binding));
  }

  auto client = McpStdioClient::start(binding.server, client_options_for_context(context), context.cancel_requested);
  if (!client) return tool_error_result(call, client.error());
  auto result = (*client)->call_tool(binding.tool.name, call.arguments_json, context.cancel_requested);
  auto shutdown = (*client)->shutdown();
  if (!result) return tool_error_result(call, result.error());
  if (!shutdown) return tool_error_result(call, shutdown.error());

  return ava::agent::ToolDispatchResult{.call_id = call.id,
                                        .name = call.name,
                                        .success = !result->is_error,
                                        .result_text = result_json(call, binding, *result)};
}

std::string schema_json(std::string_view model_tool_name, McpToolDescription const& tool) {
  auto const description = tool.description.empty() ? std::string("MCP tool ") + tool.name : tool.description;
  return "{\"type\":\"function\",\"name\":\"" + ava::core::json::escape(model_tool_name) + "\",\"description\":\"" +
         ava::core::json::escape(description) + "\",\"parameters\":" + tool.input_schema_json + '}';
}

ava::agent::RegisteredToolMetadata metadata_for_tool(std::string model_tool_name, McpServerConfig const& server,
                                                     McpToolDescription const& tool) {
  auto const description =
      tool.description.empty() ? std::string("MCP tool ") + tool.name + " from " + server.id : tool.description;
  auto const schema = schema_json(model_tool_name, tool);
  return ava::agent::RegisteredToolMetadata{
      .name = std::move(model_tool_name),
      .description = description,
      .schema_json = schema,
      .permission_category = "mcp.tool.call",
      .output_bound_summary = "MCP tool output is bounded by JSON-RPC message size",
      .execution_mode = "mcp_stdio_process",
      .event_rendering_hint = "mcp_tool",
      .description_family = "mcp"};
}

McpConfigLoadOptions config_options_for_context(ava::tools::ToolContext const& context) {
  auto options = default_mcp_config_options(context.workspace_dir);
  if (!context.mcp_global_config_file.empty()) options.global_config_file = context.mcp_global_config_file;
  if (!context.mcp_project_config_file.empty()) options.project_config_file = context.mcp_project_config_file;
  return options;
}

ava::core::Result<std::vector<McpToolDescription>> discover_server_tools(ava::tools::ToolContext const& context,
                                                                         McpServerConfig const& server) {
  if (is_canceled(context)) {
    return std::unexpected(mcp_server_error(ava::core::ErrorCategory::Unknown, "MCP tool discovery canceled", server));
  }
  auto tool_context = context;
  tool_context.permission_tool_name = "mcp_discovery";
  tool_context.current_tool_name = "mcp_discovery";
  if (auto permission = ensure_mcp_server_permission(tool_context, server, "mcp_discovery"); !permission) {
    return std::unexpected(std::move(permission.error()));
  }
  if (is_canceled(tool_context)) {
    return std::unexpected(mcp_server_error(ava::core::ErrorCategory::Unknown, "MCP tool discovery canceled", server));
  }
  auto client = McpStdioClient::start(server, client_options_for_context(context), context.cancel_requested);
  if (!client) return std::unexpected(std::move(client.error()));
  auto tools = (*client)->list_tools(context.cancel_requested);
  auto shutdown = (*client)->shutdown();
  if (!tools) return std::unexpected(std::move(tools.error()));
  if (!shutdown) return std::unexpected(std::move(shutdown.error()));
  return tools;
}

}  // namespace

std::string mcp_model_tool_name(std::string_view server_id, std::string_view tool_name) {
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
  return "mcp_" + sanitize(server_id) + "_" + sanitize(tool_name);
}

void register_enabled_mcp_tools(ava::agent::ToolRegistry& registry, ava::tools::ToolContext const& context) {
  auto config = load_mcp_config(config_options_for_context(context));
  if (!config) return;

  for (auto const& server : config->servers) {
    if (!server.enabled) continue;
    auto tools = discover_server_tools(context, server);
    if (!tools) continue;
    for (auto const& tool : *tools) {
      auto const model_tool_name = mcp_model_tool_name(server.id, tool.name);
      if (registry.find(model_tool_name) != nullptr) continue;

      auto binding = std::make_shared<McpToolBinding const>(
          McpToolBinding{.server = server, .tool = tool, .model_tool_name = model_tool_name});
      auto registered = registry.register_tool(ava::agent::RegisteredTool{
          .metadata = metadata_for_tool(model_tool_name, server, tool),
          .executor =
              [binding](ava::tools::ToolContext const& tool_context, ava::agent::ProviderToolCall const& call) {
                return dispatch_mcp_tool(tool_context, call, *binding);
              },
          .source = ava::agent::ToolSource::Mcp,
          .source_id = server.id,
          .brokered_external = true,
          .requires_lsp_diagnostics = false});
      (void)registered;
    }
  }
}

}  // namespace ava::mcp
