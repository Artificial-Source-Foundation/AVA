#include "sys.h"
#include "ExtensionResourcePolicy.h"
#include "Session.h"
#include "ava/app/command_catalog.h"
#include "ava/app/command_format.h"
#include "ava/app/command_tools.h"
#include "ava/app/runtime/command_names.h"
#include "ava/app/runtime/markdown_files.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/static_resources.h"
#include "ava/context/markdown_resource.h"
#include "ava/context/skill_loader.h"
#include "ava/core/string_utils.h"

namespace ava::app::runtime {
namespace {

struct RegistryBuilder
{
  CommandRegistry registry;
  std::unordered_map<std::string, std::size_t> occupied;
};

bool valid_command_token(std::string_view command)
{
  if (!command.starts_with('/') || command.size() < 2 || command.size() > kMaxCommandTokenBytes)
    return false;
  for (char const ch : command)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::isalnum(byte) != 0 || ch == '/' || ch == '.' || ch == '_' || ch == '-' || ch == ':';
    if (!allowed || byte < 0x20 || byte == 0x7F)
      return false;
  }
  return true;
}

void add_diagnostic(RegistryBuilder& builder, CommandRegistryDiagnostic diagnostic)
{
  builder.registry.diagnostics.push_back(std::move(diagnostic));
}

bool add_entry(RegistryBuilder& builder, CommandRegistryEntry entry)
{
  std::vector<std::string> tokens;
  tokens.push_back(entry.command);
  for (auto const& alias : entry.aliases) tokens.push_back(alias);
  for (auto const& token : tokens)
  {
    if (!valid_command_token(token))
    {
      add_diagnostic(builder, CommandRegistryDiagnostic{.command = token,
                                                        .source = to_string(entry.source),
                                                        .source_id = entry.source_id,
                                                        .path = entry.source_path,
                                                        .message = "command token is invalid"});
      return false;
    }
    auto const existing = builder.occupied.find(token);
    if (existing == builder.occupied.end())
      continue;
    auto const& winner = builder.registry.entries[existing->second];
    add_diagnostic(builder, CommandRegistryDiagnostic{.command = token,
                                                      .source = to_string(entry.source),
                                                      .source_id = entry.source_id,
                                                      .path = entry.source_path,
                                                      .message = "command collision; existing command kept",
                                                      .winner_source = to_string(winner.source),
                                                      .winner_source_id = winner.source_id,
                                                      .winner_path = winner.source_path});
    return false;
  }

  auto const index = builder.registry.entries.size();
  builder.registry.entries.push_back(std::move(entry));
  for (auto& token : tokens) builder.occupied.emplace(std::move(token), index);
  return true;
}

void load_builtin_commands(RegistryBuilder& builder)
{
  for (auto const& entry : command_catalog())
  {
    add_entry(builder, CommandRegistryEntry{.command = entry.command,
                                            .aliases = entry.aliases,
                                            .description = entry.description,
                                            .hint = entry.hint,
                                            .category = entry.category,
                                            .enabled = entry.enabled,
                                            .disabled_reason = entry.disabled_reason,
                                            .source = UnifiedCommandSource::Builtin,
                                            .kind = UnifiedCommandKind::Backend,
                                            .source_id = "builtin"});
  }
}

void load_prompt_command_dir(RegistryBuilder& builder, std::filesystem::path const& root, UnifiedCommandSource source, std::string source_scope)
{
  auto files = markdown_files(root, builder.registry.diagnostics, source);
  for (auto const& file : files)
  {
    auto name = command_name_for_file(root, file);
    if (!name)
    {
      add_diagnostic(builder,
                     CommandRegistryDiagnostic{.source = to_string(source), .path = file, .message = "command file name does not form a safe slash command"});
      continue;
    }
    auto content = ava::context::read_resource_file(file, {.max_bytes = kMaxCommandFileBytes, .resource_description = "command file"});
    if (!content)
    {
      add_diagnostic(builder,
                     CommandRegistryDiagnostic{.command = "/" + *name, .source = to_string(source), .path = file, .message = content.error().format()});
      continue;
    }
    auto parsed = ava::context::parse_markdown(*content);
    auto description = ava::context::markdown_field(parsed, "description");
    auto hint = ava::context::markdown_field(parsed, "argument-hint");
    if (hint.empty())
      hint = ava::context::markdown_field(parsed, "argument_hint");
    if (hint.empty())
      hint = ava::context::markdown_field(parsed, "hint");
    auto body = ava::context::markdown_field(parsed, "template");
    if (body.empty())
      body = std::move(parsed.body);
    if (core::trim_view(body).empty())
    {
      add_diagnostic(builder,
                     CommandRegistryDiagnostic{.command = "/" + *name, .source = to_string(source), .path = file, .message = "command template is empty"});
      continue;
    }
    add_entry(builder, CommandRegistryEntry{.command = "/" + *name,
                                            .description = description.empty() ? "Prompt command " + *name : description,
                                            .hint = std::move(hint),
                                            .category = "Prompts",
                                            .source = source,
                                            .kind = UnifiedCommandKind::PromptTemplate,
                                            .source_id = std::move(*name),
                                            .source_path = file,
                                            .source_scope = source_scope,
                                            .template_text = std::move(body)});
  }
}

void load_prompt_commands(RegistryBuilder& builder, runtime::Session const& session, ExtensionResourcePolicy const& policy)
{
  if (policy.include_project_resources)
  {
    load_prompt_command_dir(builder, session.workspace_dir() / ".ava" / "commands", UnifiedCommandSource::PromptProject, "project");
    load_prompt_command_dir(builder, session.workspace_dir() / ".ava" / "command", UnifiedCommandSource::PromptProject, "project");
  }
  load_prompt_command_dir(builder, session.paths().ava_config_dir / "commands", UnifiedCommandSource::PromptGlobal, "global");
  load_prompt_command_dir(builder, session.paths().ava_config_dir / "command", UnifiedCommandSource::PromptGlobal, "global");
}

std::string namespaced_command(std::string_view prefix, std::string_view id, std::string_view name = {})
{
  std::string command = "/";
  command += prefix;
  command += ':';
  command += id;
  if (!name.empty())
  {
    command += ':';
    command += name;
  }
  return command;
}

void load_plugin_commands(RegistryBuilder& builder, runtime::Session const& session, ExtensionResourcePolicy const& policy)
{
  auto diagnostics = ava::plugin::collect_plugin_diagnostics(policy.plugin_discovery, policy.plugin_enablement_file, session.workspace_dir());
  for (auto const& failure : diagnostics.failures)
  {
    add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::PluginCommand),
                                                      .path = failure.path,
                                                      .message = failure.details.empty() ? failure.message : failure.details});
  }

  for (auto const& status : diagnostics.plugins)
  {
    auto const& manifest = status.plugin.manifest;
    for (auto const& command : manifest.contributes.commands)
    {
      if (!valid_command_segment(command.name) || !valid_command_segment(manifest.id))
      {
        add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::PluginCommand),
                                                          .source_id = manifest.id,
                                                          .path = manifest.path,
                                                          .message = "plugin command id does not form a safe slash command"});
        continue;
      }
      CommandRegistryEntry entry{.command = namespaced_command("plugin", manifest.id, command.name),
                                 .description = command.description.empty() ? "Plugin command " + command.name : command.description,
                                 .category = "Plugins",
                                 .enabled = status.enabled,
                                 .disabled_reason = status.enabled ? "" : "plugin is disabled",
                                 .source = UnifiedCommandSource::PluginCommand,
                                 .kind = UnifiedCommandKind::PluginCommand,
                                 .source_id = manifest.id,
                                 .source_path = manifest.path,
                                 .source_scope = std::string(ava::plugin::to_string(status.plugin.scope)),
                                 .plugin_id = manifest.id,
                                 .plugin_command_name = command.name};
      add_entry(builder, entry);
      entry.command = "/" + command.name;
      add_entry(builder, std::move(entry));
    }
  }
}

std::string mcp_command_text(ava::mcp::McpServerConfig const& server)
{
  std::string text = server.command;
  for (auto const& arg : server.args) text += " " + arg;
  return text;
}

ava::core::VoidResult ensure_mcp_prompt_server_permission(ava::tools::ToolContext const& context, ava::mcp::McpServerConfig const& server)
{
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerLaunch, server.source_path, mcp_command_text(server),
                                                      "mcp_prompts", "MCP server launch requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerConnect, server.source_path, server.id, "mcp_prompts",
                                                      "MCP server connection requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  return {};
}

void load_mcp_prompt_commands(RegistryBuilder& builder, runtime::Session& session, CommandRegistryOptions const& options, ExtensionResourcePolicy const& policy)
{
  auto config = ava::mcp::load_mcp_config(policy.mcp_config);
  if (!config)
  {
    add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt), .message = config.error().format()});
    return;
  }

  auto context = make_tool_context(session, options.permission_resolver);
  context.permission_tool_name = "mcp_prompts";
  context.current_tool_name = "mcp_prompts";
  context.cancel_requested = options.cancel_requested;

  for (auto const& server : config->servers)
  {
    if (!server.enabled)
      continue;
    if (!valid_command_segment(server.id))
    {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = "MCP server id does not form a safe slash command"});
      continue;
    }
    if (auto permission = ensure_mcp_prompt_server_permission(context, server); !permission)
    {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = permission.error().format()});
      continue;
    }
    auto client = ava::mcp::McpStdioClient::start(server, ava::mcp::McpStdioClientOptions{.workspace_dir = session.workspace_dir()}, options.cancel_requested);
    if (!client)
    {
      add_diagnostic(
          builder,
          CommandRegistryDiagnostic{
              .source = to_string(UnifiedCommandSource::McpPrompt), .source_id = server.id, .path = server.source_path, .message = client.error().format()});
      continue;
    }
    auto prompts = (*client)->list_prompts(options.cancel_requested);
    auto shutdown = (*client)->shutdown();
    if (!prompts)
    {
      add_diagnostic(
          builder,
          CommandRegistryDiagnostic{
              .source = to_string(UnifiedCommandSource::McpPrompt), .source_id = server.id, .path = server.source_path, .message = prompts.error().format()});
      continue;
    }
    if (!shutdown)
    {
      add_diagnostic(
          builder,
          CommandRegistryDiagnostic{
              .source = to_string(UnifiedCommandSource::McpPrompt), .source_id = server.id, .path = server.source_path, .message = shutdown.error().format()});
      continue;
    }
    for (auto const& prompt : *prompts)
    {
      if (!valid_command_segment(prompt.name))
      {
        add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                          .source_id = server.id,
                                                          .path = server.source_path,
                                                          .message = "MCP prompt name does not form a safe slash command"});
        continue;
      }
      CommandRegistryEntry entry{.command = namespaced_command("mcp", server.id, prompt.name),
                                 .description = prompt.description.empty() ? "MCP prompt " + prompt.name : prompt.description,
                                 .category = "MCP",
                                 .source = UnifiedCommandSource::McpPrompt,
                                 .kind = UnifiedCommandKind::McpPrompt,
                                 .source_id = server.id,
                                 .source_path = server.source_path,
                                 .source_scope = std::string(ava::mcp::to_string(server.scope)),
                                 .mcp_server_id = server.id,
                                 .mcp_prompt_name = prompt.name,
                                 .mcp_arguments = prompt.arguments};
      std::vector<std::string> required;
      for (auto const& argument : prompt.arguments)
      {
        if (argument.required)
          required.push_back(argument.name);
      }
      if (!prompt.arguments.empty())
        entry.hint = "<" + joined_strings(required.empty() ? std::vector<std::string>{"args"} : required, "> <") + ">";
      add_entry(builder, entry);
      entry.command = "/" + prompt.name;
      add_entry(builder, std::move(entry));
    }
  }
}

std::vector<ava::context::DeclaredSkillFileOptions> declared_plugin_skill_files(ava::plugin::PluginDiagnostics const& diagnostics)
{
  std::vector<ava::context::DeclaredSkillFileOptions> files;
  for (auto const& skill : ava::plugin::enabled_plugin_static_skill_files(diagnostics))
  {
    files.push_back(ava::context::DeclaredSkillFileOptions{.path = skill.path,
                                                           .name = skill.name,
                                                           .description = skill.description,
                                                           .source_type = ava::context::SkillSourceType::Plugin,
                                                           .preloaded_content = skill.content});
  }
  return files;
}

void load_skill_commands(RegistryBuilder& builder, runtime::Session const& session, ExtensionResourcePolicy const& policy)
{
  auto plugin_diagnostics = ava::plugin::collect_plugin_diagnostics(policy.plugin_discovery, policy.plugin_enablement_file, session.workspace_dir());
  auto loaded = ava::context::load_skills(ava::context::SkillLoadOptions{
      .workspace_root = session.workspace_dir(),
      .declared_skill_files = declared_plugin_skill_files(plugin_diagnostics),
      .include_project_skills = policy.include_project_resources,
  });
  for (auto const& diagnostic : loaded.diagnostics)
  {
    add_diagnostic(builder,
                   CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::Skill), .path = diagnostic.path, .message = diagnostic.message});
  }
  for (auto const& skill : loaded.skills)
  {
    CommandRegistryEntry entry{.command = namespaced_command("skill", skill.name),
                               .description = skill.description,
                               .category = "Skills",
                               .source = UnifiedCommandSource::Skill,
                               .kind = UnifiedCommandKind::SkillPrompt,
                               .source_id = skill.name,
                               .source_path = skill.path,
                               .source_scope = ava::context::to_string(skill.source_type),
                               .template_text = skill.content,
                               .skill_name = skill.name};
    add_entry(builder, entry);
    entry.command = "/" + skill.name;
    add_entry(builder, std::move(entry));
  }
}

} // namespace

CommandRegistry Session::load_command_registry(CommandRegistryOptions options)
{
  RegistryBuilder builder;
  auto const resource_policy = make_extension_resource_policy(*this);
  if (options.include_builtins)
    load_builtin_commands(builder);
  if (options.include_prompt_commands)
    load_prompt_commands(builder, *this, resource_policy);
  if (options.include_plugin_commands)
    load_plugin_commands(builder, *this, resource_policy);
  if (options.include_mcp_prompts)
    load_mcp_prompt_commands(builder, *this, options, resource_policy);
  if (options.include_skills)
    load_skill_commands(builder, *this, resource_policy);
  return std::move(builder.registry);
}

}  // namespace ava::app::runtime
