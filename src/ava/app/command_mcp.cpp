#include "sys.h"
#include "ava/app/command_format.h"
#include "ava/app/command_mcp.h"
#include "ava/app/command_tools.h"
#include "ava/app/runtime/ExtensionResourcePolicy.h"
#include "ava/app/runtime/Session.h"
#include "ava/mcp/config.h"
#include "ava/mcp/stdio_client.h"
#include "ava/mcp/tool_broker.h"
#include "ava/core/ids.h"

#include <sstream>
#include <utility>

namespace ava::app {
namespace {

std::string mcp_scope_text(ava::mcp::McpServerScope scope)
{
  return std::string(ava::mcp::to_string(scope));
}

std::string mcp_status_text(bool enabled)
{
  return enabled ? "enabled" : "disabled";
}

ava::mcp::McpServerConfig const* find_mcp_server(ava::mcp::McpConfig const& config, std::string_view server_id)
{
  for (auto const& server : config.servers)
  {
    if (server.id == server_id)
      return &server;
  }
  return nullptr;
}

std::string mcp_command_text(ava::mcp::McpServerConfig const& server)
{
  std::string text = sanitize_inline_text(server.command);
  for (auto const& arg : server.args) text += " " + sanitize_inline_text(arg);
  return text;
}

std::string mcp_display_path(std::filesystem::path const& path, runtime::session_ts const& unlocked_session)
{
  auto const current_dir = runtime::session_ts::crat(unlocked_session)->current_dir();
  return sanitize_inline_text(display_path(path, current_dir));
}

std::string mcp_config_path_text(std::filesystem::path const& path, runtime::session_ts const& unlocked_session)
{
  if (path.empty())
    return "none";
  return mcp_display_path(path, unlocked_session);
}

std::string format_mcp_server_not_found_text(ava::mcp::McpConfig const& config, std::string_view server_id)
{
  std::string output = "MCP server not found: " + sanitize_inline_text(std::string(server_id));
  if (!config.servers.empty())
  {
    output += "\nConfigured MCP servers:";
    for (auto const& server : config.servers) output += "\n  " + sanitize_inline_text(server.id);
  }
  return output;
}

std::string format_mcp_list_text(ava::mcp::McpConfig const& config, runtime::session_ts const& unlocked_session)
{
  std::ostringstream output;
  output << "MCP servers:\n";
  output << "  global config: " << mcp_config_path_text(config.global_config_file, unlocked_session) << "\n";
  output << "  project config: " << mcp_config_path_text(config.project_config_file, unlocked_session) << "\n";
  if (config.servers.empty())
  {
    output << "  none";
    return output.str();
  }
  for (auto const& server : config.servers)
  {
    output << "  " << sanitize_inline_text(server.id) << "  " << mcp_status_text(server.enabled) << "  " << mcp_scope_text(server.scope) << "  "
           << sanitize_inline_text(server.name) << "\n";
  }
  auto text = output.str();
  if (!text.empty() && text.back() == '\n')
    text.pop_back();
  return text;
}

std::string format_mcp_inspect_text(ava::mcp::McpServerConfig const& server, runtime::session_ts const& unlocked_session)
{
  std::ostringstream output;
  output << "MCP server " << sanitize_inline_text(server.id) << "\n";
  output << "  name: " << sanitize_inline_text(server.name) << "\n";
  output << "  status: " << mcp_status_text(server.enabled) << "\n";
  output << "  scope: " << mcp_scope_text(server.scope) << "\n";
  output << "  config: " << mcp_config_path_text(server.source_path, unlocked_session) << "\n";
  output << "  command: " << mcp_command_text(server) << "\n";
  output << "  note: stdio MCP servers are launched per discovery or tool call and are not kept resident.";
  return output.str();
}

std::string format_mcp_tools_text(ava::mcp::McpServerConfig const& server, ava::mcp::McpInitialization const& initialization,
                                  std::vector<ava::mcp::McpToolDescription> const& tools)
{
  std::ostringstream output;
  output << "MCP tools for " << sanitize_inline_text(server.id) << "\n";
  output << "  server: " << sanitize_inline_text(initialization.server_name);
  if (!initialization.server_version.empty())
    output << " " << sanitize_inline_text(initialization.server_version);
  output << "\n";
  if (tools.empty())
  {
    output << "  none";
    return output.str();
  }
  for (auto const& tool : tools)
  {
    output << "  " << sanitize_inline_text(tool.name) << "  " << sanitize_inline_text(ava::mcp::mcp_model_tool_name(server.id, tool.name));
    if (!tool.description.empty())
      output << "  " << sanitize_inline_text(tool.description);
    output << "\n";
  }
  auto text = output.str();
  if (!text.empty() && text.back() == '\n')
    text.pop_back();
  return text;
}

}  // namespace

ava::core::Result<CommandResult> run_mcp_command(runtime::session_ts& unlocked_session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto const usage = [&]() {
    add_output(result, missing_argument("/mcp <list|inspect|tools|restart> [server_id]"));
    return result;
  };

  auto const argument = command_argument(request.command, "/mcp");
  auto const args = split_command_arguments(argument);
  if (args.empty())
    return usage();

  auto const resource_policy = runtime::make_extension_resource_policy_1(*runtime::session_ts::rat(unlocked_session));
  auto config = ava::mcp::load_mcp_config(resource_policy.mcp_config);
  if (!config)
  {
    add_output(result, config.error().format());
    return result;
  }

  auto const& subcommand = args[0];
  if (subcommand == "list")
  {
    if (args.size() != 1)
      return usage();
    add_output(result, format_mcp_list_text(*config, unlocked_session));
    return result;
  }

  if ((subcommand == "inspect" || subcommand == "tools" || subcommand == "restart") && args.size() != 2)
  {
    return usage();
  }

  if (subcommand == "inspect")
  {
    auto const* server = find_mcp_server(*config, args[1]);
    if (!server)
    {
      add_output(result, format_mcp_server_not_found_text(*config, args[1]));
      return result;
    }
    add_output(result, format_mcp_inspect_text(*server, unlocked_session));
    return result;
  }

  if (subcommand == "restart")
  {
    auto const* server = find_mcp_server(*config, args[1]);
    if (!server)
    {
      add_output(result, format_mcp_server_not_found_text(*config, args[1]));
      return result;
    }
    add_output(result, "MCP server " + sanitize_inline_text(server->id) +
                           " uses per-request stdio processes; the next discovery or tool call will launch a fresh "
                           "process.");
    return result;
  }

  if (subcommand == "tools")
  {
    auto const* server = find_mcp_server(*config, args[1]);
    if (!server)
    {
      add_output(result, format_mcp_server_not_found_text(*config, args[1]));
      return result;
    }
    if (!server->enabled)
    {
      add_output(result, "MCP server is disabled: " + sanitize_inline_text(server->id));
      return result;
    }

    auto context = make_tool_context(unlocked_session, request.permission_resolver);
    context.permission_tool_name = "mcp_tools";
    auto const call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(unlocked_session, request.event_sink, result, call_id, "mcp_tools", server->id); !recorded)
    {
      return std::unexpected(std::move(recorded.error()));
    }

    auto fail = [&](ava::core::Error const& error) -> ava::core::Result<CommandResult> {
      auto const text = error.format();
      if (auto recorded = record_tool_result(unlocked_session, request.event_sink, result, call_id, "mcp_tools", ava::agent::ToolTimelineStatus::Error, text);
          !recorded)
      {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    };

    if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerLaunch, server->source_path, mcp_command_text(*server),
                                                        "mcp_tools", "MCP server launch requires permission");
        !permission)
    {
      return fail(permission.error());
    }
    if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerConnect, server->source_path, server->id, "mcp_tools",
                                                        "MCP server connection requires permission");
        !permission)
    {
      return fail(permission.error());
    }

    ava::mcp::McpStdioClientOptions options;
    options.workspace_dir = runtime::session_ts::rat(unlocked_session)->workspace_dir();
    auto client = ava::mcp::McpStdioClient::start(*server, options);
    if (!client)
      return fail(client.error());
    auto tools = (*client)->list_tools();
    auto const initialization = (*client)->initialization();
    auto shutdown = (*client)->shutdown();
    if (!tools)
      return fail(tools.error());
    if (!shutdown)
      return fail(shutdown.error());

    if (auto recorded = record_tool_result(unlocked_session, request.event_sink, result, call_id, "mcp_tools", ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(tools->size()) + " tools");
        !recorded)
    {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, format_mcp_tools_text(*server, initialization, *tools));
    return result;
  }

  return usage();
}

}  // namespace ava::app
