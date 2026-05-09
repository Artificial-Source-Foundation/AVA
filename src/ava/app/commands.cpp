#include "ava/app/command_connect.h"
#include "ava/app/command_format.h"
#include "ava/app/command_help.h"
#include "ava/app/command_mcp.h"
#include "ava/app/command_models.h"
#include "ava/app/command_plugins.h"
#include "ava/app/command_registry.h"
#include "ava/app/command_sessions.h"
#include "ava/app/command_tools.h"
#include "ava/app/commands.h"
#include "ava/app/plugin_event_hooks.h"
#include "ava/tools/file_tools.h"
#include "ava/mcp/config.h"
#include "ava/mcp/stdio_client.h"
#include "ava/permissions/permission.h"
#include "ava/context/skill_loader.h"
#include "ava/core/json.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace ava::app {
namespace {

bool starts_with_command(std::string_view line, std::string_view command) noexcept
{
  return line == command || (line.starts_with(command) && line.size() > command.size() && line[command.size()] == ' ');
}

std::string_view command_token(std::string_view line) noexcept
{
  auto const end = line.find_first_of(" \t\r\n");
  return line.substr(0, end == std::string_view::npos ? line.size() : end);
}

CommandResult handled_text(std::string text)
{
  CommandResult result;
  result.handled = true;
  add_output(result, std::move(text));
  return result;
}

CommandResult handled_prompt(std::string command, std::string source, std::string message)
{
  CommandResult result;
  result.handled = true;
  result.prompt_command = std::move(command);
  result.prompt_source = std::move(source);
  result.prompt_message = std::move(message);
  return result;
}

std::string dynamic_command_argument(std::string_view line)
{
  auto const token = command_token(line);
  if (line.size() <= token.size())
    return {};
  auto rest = line.substr(token.size());
  while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
  return std::string(rest);
}

ava::core::Result<std::string> skill_prompt_message(RuntimeSession& session, CommandRequest const& request, CommandRegistryEntry const& entry)
{
  auto loaded = ava::context::load_skills(ava::context::SkillLoadOptions{.workspace_root = session.workspace_dir});
  auto const match = std::ranges::find_if(loaded.skills, [&](ava::context::LoadedSkill const& skill) { return skill.name == entry.skill_name; });
  if (match == loaded.skills.end())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "skill not found");
    error.with_context("skill", entry.skill_name);
    return std::unexpected(std::move(error));
  }

  auto context = make_tool_context(session, request.permission_resolver);
  context.permission_tool_name = "skill";
  context.current_tool_name = "skill";
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::SkillLoad, match->path, match->name, "skill",
                                                      "skill loading requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  auto sampled_files = ava::context::sample_skill_files(match->directory);
  return ava::context::format_loaded_skill_for_tool(*match, sampled_files);
}

std::string mcp_command_text(ava::mcp::McpServerConfig const& server)
{
  std::string text = server.command;
  for (auto const& arg : server.args) text += " " + arg;
  return text;
}

ava::core::VoidResult ensure_mcp_prompt_permissions(ava::tools::ToolContext const& context, ava::mcp::McpServerConfig const& server,
                                                    CommandRegistryEntry const& entry)
{
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerLaunch, server.source_path, mcp_command_text(server),
                                                      "mcp_prompt", "MCP server launch requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerConnect, server.source_path, server.id, "mcp_prompt",
                                                      "MCP server connection requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  auto const command = entry.mcp_server_id + ":" + entry.mcp_prompt_name;
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpToolCall, server.source_path, command, "mcp_prompt",
                                                      "MCP prompt retrieval requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  return {};
}

ava::core::Result<std::string> mcp_prompt_message(RuntimeSession& session, CommandRequest const& request, CommandRegistryEntry const& entry,
                                                  std::string_view argument_text)
{
  auto config_options = ava::mcp::default_mcp_config_options(session.workspace_dir);
  config_options.global_config_file = session.paths.ava_config_dir / "mcp.json";
  config_options.project_config_file = session.workspace_dir / ".ava" / "mcp.json";
  auto config = ava::mcp::load_mcp_config(config_options);
  if (!config)
    return std::unexpected(std::move(config.error()));
  auto const server = std::ranges::find_if(config->servers, [&](ava::mcp::McpServerConfig const& item) { return item.id == entry.mcp_server_id; });
  if (server == config->servers.end() || !server->enabled)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "MCP server not found");
    error.with_context("mcp_server", entry.mcp_server_id);
    return std::unexpected(std::move(error));
  }

  auto arguments = mcp_prompt_arguments_json(entry, argument_text);
  if (!arguments)
    return std::unexpected(std::move(arguments.error()));

  auto context = make_tool_context(session, request.permission_resolver);
  context.permission_tool_name = "mcp_prompt";
  context.current_tool_name = "mcp_prompt";
  context.cancel_requested = request.cancel_requested;
  if (auto permission = ensure_mcp_prompt_permissions(context, *server, entry); !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }

  auto client = ava::mcp::McpStdioClient::start(*server, ava::mcp::McpStdioClientOptions{.workspace_dir = session.workspace_dir}, request.cancel_requested);
  if (!client)
    return std::unexpected(std::move(client.error()));
  auto prompt = (*client)->get_prompt(entry.mcp_prompt_name, *arguments, request.cancel_requested);
  auto shutdown = (*client)->shutdown();
  if (!prompt)
    return std::unexpected(std::move(prompt.error()));
  if (!shutdown)
    return std::unexpected(std::move(shutdown.error()));
  if (prompt->content.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "MCP prompt returned no text");
    error.with_context("mcp_server", entry.mcp_server_id);
    error.with_context("mcp_prompt", entry.mcp_prompt_name);
    return std::unexpected(std::move(error));
  }
  return std::move(prompt->content);
}

ava::core::Result<CommandResult> run_registry_command(RuntimeSession& session, CommandRequest request, CommandRegistryEntry const& entry)
{
  if (!entry.enabled)
    return handled_text(entry.command + " is disabled: " + entry.disabled_reason);
  auto const argument = dynamic_command_argument(request.command);
  switch (entry.kind)
  {
    case UnifiedCommandKind::Backend:
      return CommandResult{};
    case UnifiedCommandKind::PromptTemplate: {
      auto prompt = expand_prompt_command_template(entry.template_text, argument);
      if (!prompt)
        return std::unexpected(std::move(prompt.error()));
      return handled_prompt(entry.command, to_string(entry.source), std::move(*prompt));
    }
    case UnifiedCommandKind::SkillPrompt: {
      auto prompt = skill_prompt_message(session, request, entry);
      if (!prompt)
        return std::unexpected(std::move(prompt.error()));
      return handled_prompt(entry.command, to_string(entry.source), std::move(*prompt));
    }
    case UnifiedCommandKind::McpPrompt: {
      auto prompt = mcp_prompt_message(session, request, entry, argument);
      if (!prompt)
        return std::unexpected(std::move(prompt.error()));
      return handled_prompt(entry.command, to_string(entry.source), std::move(*prompt));
    }
    case UnifiedCommandKind::PluginCommand: {
      auto delegated = request;
      auto args = argument.empty() ? std::string("{}") : argument;
      delegated.command = "/plugin run " + entry.plugin_id + " " + entry.plugin_command_name + " " + args;
      return run_plugin_command(session, delegated);
    }
  }
  return CommandResult{};
}

}  // namespace

bool is_backend_command(std::string_view line) noexcept
{
  return find_command_catalog_entry(line) != nullptr;
}

bool is_backend_command(std::string_view line, RuntimeSession& session)
{
  if (is_backend_command(line))
    return true;
  return command_registry_contains(session, line);
}

ava::core::Result<CommandResult> run_command(RuntimeSession& session, CommandRequest request)
{
  CommandResult result;
  if (request.command.empty())
    return result;

  auto const* entry = find_command_catalog_entry(request.command);
  if (!entry)
  {
    auto const token = command_token(request.command);
    auto registry = load_command_registry(session, CommandRegistryOptions{.include_builtins = false,
                                                                          .include_prompt_commands = true,
                                                                          .include_skills = true,
                                                                          .include_plugin_commands = true,
                                                                          .include_mcp_prompts = token.starts_with("/mcp:"),
                                                                          .permission_resolver = request.permission_resolver,
                                                                          .cancel_requested = request.cancel_requested});
    if (auto const* registry_entry = find_command_registry_entry(registry, request.command))
    {
      return run_registry_command(session, std::move(request), *registry_entry);
    }
    if (!token.starts_with("/skill:") && !token.starts_with("/mcp:") && !token.starts_with("/plugin:"))
    {
      registry = load_command_registry(session, CommandRegistryOptions{.include_builtins = false,
                                                                       .include_prompt_commands = false,
                                                                       .include_skills = false,
                                                                       .include_plugin_commands = false,
                                                                       .include_mcp_prompts = true,
                                                                       .permission_resolver = request.permission_resolver,
                                                                       .cancel_requested = request.cancel_requested});
      if (auto const* registry_entry = find_command_registry_entry(registry, request.command))
      {
        return run_registry_command(session, std::move(request), *registry_entry);
      }
    }
    if (token.starts_with("/skill:") || token.starts_with("/mcp:") || token.starts_with("/plugin:"))
    {
      if (!registry.diagnostics.empty())
        return handled_text(registry.diagnostics.front().message);
      return handled_text("command not found: " + std::string(token));
    }
    return result;
  }
  request.command = normalize_command_line(request.command, *entry);

  if (!entry->enabled)
  {
    return handled_text(entry->command + " is disabled: " + entry->disabled_reason);
  }

  // RPC command execution already serializes session-store access around run_command; reacquiring
  // the same mutex from event-hook permission audits would deadlock nested command events.
  request.event_sink =
      make_plugin_event_observer_sink(plugin_event_observer_options(session, request.permission_resolver, nullptr), std::move(request.event_sink));

  if (request.command == "/quit" || request.command == "/exit")
  {
    result.handled = true;
    result.quit = true;
    return result;
  }
  if (request.command == "/help")
  {
    return handled_text(command_help_text(request.hotkeys));
  }
  if (request.command == "/hotkeys")
  {
    return handled_text(command_hotkeys_text(request.hotkeys));
  }
  if (request.command == "/settings")
  {
    return handled_text(
        "Settings are shown as a read-only TUI view. Runtime-owned slash commands keep config changes in "
        "the backend.");
  }
  if (request.command == "/details")
  {
    return handled_text("Tool details are a TUI display toggle. Use /details inside the TUI to switch views.");
  }
  if (request.command == "/thinking")
  {
    return handled_text("Thinking visibility is a TUI display toggle. It does not change provider reasoning mode.");
  }
  if (starts_with_command(request.command, "/models"))
  {
    return run_models_command(session, command_argument(request.command, "/models"));
  }
  if (starts_with_command(request.command, "/connect"))
  {
    return run_connect_command(session, request);
  }
  if (starts_with_command(request.command, "/mcp"))
  {
    return run_mcp_command(session, request);
  }
  if (starts_with_command(request.command, "/plugins"))
  {
    return run_plugins_command(session, request);
  }
  if (starts_with_command(request.command, "/plugin"))
  {
    return run_plugin_command(session, request);
  }
  if (starts_with_command(request.command, "/sessions"))
  {
    return run_sessions_command(session, command_argument(request.command, "/sessions"));
  }
  if (request.command == "/mode")
  {
    return run_mode_command(session);
  }
  if (starts_with_command(request.command, "/context"))
  {
    return run_context_command(session, command_argument(request.command, "/context"));
  }
  if (request.command == "/stats" || request.command == "/status")
  {
    return run_stats_command(session);
  }
  if (starts_with_command(request.command, "/compact"))
  {
    return run_compact_command(session, request);
  }
  if (request.command == "/export")
  {
    return run_export_command(session);
  }

  if (entry->hint.empty() && starts_with_command(request.command, entry->command))
  {
    return handled_text(missing_argument(entry->command));
  }

  if (request.command == "/glob")
  {
    return handled_text(missing_argument("/glob <pattern>"));
  }
  if (request.command == "/grep")
  {
    return handled_text(missing_argument("/grep <text> [glob]"));
  }
  if (request.command == "/read")
  {
    return handled_text(missing_argument("/read <path>"));
  }
  if (request.command == "/write")
  {
    return handled_text(missing_argument("/write <path> <text>"));
  }
  if (request.command == "/bash")
  {
    return handled_text(missing_argument("/bash <command>"));
  }

  return run_tool_command(session, request);
}

}  // namespace ava::app
