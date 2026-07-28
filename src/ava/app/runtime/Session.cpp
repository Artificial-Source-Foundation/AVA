#include "sys.h"
#include "Session.h"
#include "command_names.h"
#include "markdown_files.h"
#include "ava/app/command_catalog.h"
#include "ava/app/command_format.h"
#include "ava/app/command_tools.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/rpc/serialization_detail.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_model.h"
#include "ava/app/runtime_prompt.h"
#include "ava/app/runtime_reasoning.h"
#include "ava/app/subagent_delivery_manager.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/static_resources.h"
#include "ava/app/runtime.h"
#include "ava/context/skill_loader.h"
#include "ava/core/ids.h"
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

// Comma-separated list of reasoning levels acceptable on the CLI for `model`,
// always including "off" first; non-empty, non-"off" supported levels follow.
std::string cli_supported_reasoning_levels(ava::config::ModelInfo const& model)
{
  std::string levels = "off";
  for (auto const& level : ava::config::supported_reasoning_levels(model))
  {
    if (level.empty() || level == "off")
      continue;
    levels += ", ";
    levels += level;
  }
  return levels;
}

// Attach the --thinking option name and the model's supported levels to `error`
// so CLI failures point the user at the levels they may pass.
void add_cli_reasoning_context(ava::core::Error& error, ava::config::ModelInfo const& model)
{
  error.with_context("option", "--thinking");
  error.with_context("supported_levels", cli_supported_reasoning_levels(model));
}

// Compare two optional reasoning selections for field-wise equality so the
// setter can short-circuit a no-op change without writing an entry.
bool same_reasoning_selection(std::optional<ReasoningSelection> const& left, std::optional<ReasoningSelection> const& right)
{
  if (!left || !right)
    return !left && !right;
  return left->level == right->level && left->provider_level == right->provider_level && left->budget_tokens == right->budget_tokens &&
         left->display == right->display;
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
    auto content = read_bounded_file(file);
    if (!content)
    {
      add_diagnostic(builder,
                     CommandRegistryDiagnostic{.command = "/" + *name, .source = to_string(source), .path = file, .message = content.error().format()});
      continue;
    }
    auto parsed = parse_markdown(*content);
    auto description = markdown_field(parsed, "description");
    auto hint = markdown_field(parsed, "argument-hint");
    if (hint.empty())
      hint = markdown_field(parsed, "argument_hint");
    if (hint.empty())
      hint = markdown_field(parsed, "hint");
    auto body = markdown_field(parsed, "template");
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

void load_prompt_commands(RegistryBuilder& builder, runtime::Session const& session)
{
  if (project_resources_trusted(session.project_trust()))
  {
    load_prompt_command_dir(builder, session.workspace_dir() / ".ava" / "commands", UnifiedCommandSource::PromptProject, "project");
    load_prompt_command_dir(builder, session.workspace_dir() / ".ava" / "command", UnifiedCommandSource::PromptProject, "project");
  }
  load_prompt_command_dir(builder, session.paths().ava_config_dir / "commands", UnifiedCommandSource::PromptGlobal, "global");
  load_prompt_command_dir(builder, session.paths().ava_config_dir / "command", UnifiedCommandSource::PromptGlobal, "global");
}

ava::plugin::PluginDiscoveryOptions plugin_discovery_options(runtime::Session const& session)
{
  return ava::plugin::PluginDiscoveryOptions{
      .global_plugins_dir = session.paths().ava_config_dir / "plugins",
      .project_plugins_dir = project_resources_trusted(session.project_trust()) ? session.workspace_dir() / ".ava" / "plugins" : std::filesystem::path{}};
}

std::filesystem::path plugin_enablement_file(runtime::Session const& session)
{
  return session.paths().ava_state_dir / "plugin-enablement.json";
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

void load_plugin_commands(RegistryBuilder& builder, runtime::Session const& session)
{
  auto diagnostics = ava::plugin::collect_plugin_diagnostics(plugin_discovery_options(session), plugin_enablement_file(session), session.workspace_dir());
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

void load_mcp_prompt_commands(RegistryBuilder& builder, runtime::Session& session, CommandRegistryOptions const& options)
{
  auto config_options = ava::mcp::default_mcp_config_options(session.workspace_dir());
  config_options.global_config_file = session.paths().ava_config_dir / "mcp.json";
  config_options.project_config_file =
      project_resources_trusted(session.project_trust()) ? session.workspace_dir() / ".ava" / "mcp.json" : std::filesystem::path{};
  auto config = ava::mcp::load_mcp_config(config_options);
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

void load_skill_commands(RegistryBuilder& builder, runtime::Session const& session)
{
  auto plugin_diagnostics =
      ava::plugin::collect_plugin_diagnostics(plugin_discovery_options(session), plugin_enablement_file(session), session.workspace_dir());
  auto loaded = ava::context::load_skills(ava::context::SkillLoadOptions{
      .workspace_root = session.workspace_dir(),
      .declared_skill_files = declared_plugin_skill_files(plugin_diagnostics),
      .include_project_skills = project_resources_trusted(session.project_trust()),
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
  if (options.include_builtins)
    load_builtin_commands(builder);
  if (options.include_prompt_commands)
    load_prompt_commands(builder, *this);
  if (options.include_plugin_commands)
    load_plugin_commands(builder, *this);
  if (options.include_mcp_prompts)
    load_mcp_prompt_commands(builder, *this, options);
  if (options.include_skills)
    load_skill_commands(builder, *this);
  return std::move(builder.registry);
}

ava::core::VoidResult Session::refresh_parent_configuration() const
{
  auto const& manager = subagent_delivery_manager();
  return manager ? manager->refresh_parent_configuration(*this) : ava::core::VoidResult{};
}

ava::core::VoidResult Session::replace_with(runtime::Session&& replacement)
{
  // Background ownership is application-scoped. Retire only the visible
  // session controller and preserve the exact coordinator across navigation.
  auto coordinator = resources().subagent_coordinator;
  auto delivery_manager = resources().subagent_delivery_manager;
  auto title_coordinator = resources().session_title_coordinator;
  auto const detached_parent_id = sessionless() ? std::string{} : store.session_id();
  bool const leaves_detached_parent = !detached_parent_id.empty() && (replacement.sessionless() || replacement.store.session_id() != detached_parent_id);
  resources().run_controller.reset();
  *this = std::move(replacement);
  if (delivery_manager)
  {
    resources().subagent_delivery_manager = delivery_manager;
    resources().subagent_coordinator = resources().subagent_delivery_manager->coordinator();
  }
  else if (coordinator)
    resources().subagent_coordinator = coordinator;
  if (title_coordinator)
    resources().session_title_coordinator = std::move(title_coordinator);

  // The delivery manager keeps a capsule and journal owner when work remains;
  // otherwise its exact generation release allows another AVA process to
  // activate this history.
  if (leaves_detached_parent)
  {
    if (delivery_manager)
      delivery_manager->release_detached_parent(detached_parent_id);
    else if (coordinator)
      static_cast<void>(coordinator->release_parent_if_idle(detached_parent_id));
  }
  return {};
}

ava::core::Result<ava::session::SessionMetadataView> Session::append_runtime_session_metadata(ava::session::SessionMetadataUpdate update)
{
  auto read_authority = this->read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  auto entries = read_authority->load();
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  auto entry = ava::session::make_session_metadata_entry(std::move(update), entries->empty() ? std::string{} : entries->back().id);
  if (!entry)
    return std::unexpected(std::move(entry.error()));
  if (auto appended = append_owned(*entry); !appended)
    return std::unexpected(std::move(appended.error()));
  entries->push_back(std::move(*entry));
  return ava::session::session_metadata_from_entries(store.session_id(), *entries);
}

ava::core::VoidResult Session::append_runtime_mode_change(ava::agent::Mode mode)
{
  return append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::ModeChange,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}"});
}

ava::core::VoidResult Session::apply_initial_reasoning_level(std::string_view requested_level)
{
  auto level = core::trim(requested_level);
  if (level.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning level is required");
    add_cli_reasoning_context(error, model());
    return std::unexpected(std::move(error));
  }

  std::optional<ReasoningSelection> selection = std::nullopt;
  if (level != "off")
  {
    auto selected = reasoning_selection_for_level(model(), std::move(level));
    if (!selected)
    {
      auto error = std::move(selected.error());
      add_cli_reasoning_context(error, model());
      return std::unexpected(std::move(error));
    }
    selection = std::move(*selected);
  }

  auto changed = set_runtime_reasoning(std::move(selection));
  if (!changed)
  {
    auto error = std::move(changed.error());
    add_cli_reasoning_context(error, model());
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult Session::apply_runtime_prompt_state(runtime::PromptState prompt_state)
{
  resolve_prompt_state() = ResolvedPromptState{.mode = prompt_state.mode,
                                               .base_prompt = std::move(prompt_state.base_prompt),
                                               .context_sources = std::move(prompt_state.context_sources),
                                               .freshness_sources = std::move(prompt_state.freshness_sources),
                                               .system_prompt = std::move(prompt_state.system_prompt)};
  return refresh_parent_configuration();
}

ava::core::Result<bool> Session::switch_runtime_model(ava::config::ModelInfo model)
{
  if (this->model().provider_id == model.provider_id && this->model().model_id == model.model_id)
    return false;

  auto prompt_state = load_runtime_prompt_state(paths(), model, mode(), workspace_dir(), current_dir(),
                                                project_resources_trusted(project_trust()), prompt_overrides());
  if (!prompt_state)
    return std::unexpected(std::move(prompt_state.error()));

  auto const previous = this->model();
  auto appended = append_owned(make_model_change_entry(previous, model));
  if (!appended)
    return std::unexpected(std::move(appended.error()));

  model_selection().model = std::move(model);
  resolve_prompt_state() = ResolvedPromptState{.mode = prompt_state->mode,
                                               .base_prompt = std::move(prompt_state->base_prompt),
                                               .context_sources = std::move(prompt_state->context_sources),
                                               .freshness_sources = std::move(prompt_state->freshness_sources),
                                               .system_prompt = std::move(prompt_state->system_prompt)};
  model_selection().reasoning = std::nullopt;
  if (auto refreshed = refresh_parent_configuration(); !refreshed)
    return std::unexpected(std::move(refreshed.error()));
  return true;
}

ava::core::Result<bool> Session::set_runtime_reasoning(std::optional<ReasoningSelection> selection)
{
  if (selection)
  {
    selection->level = core::trim(selection->level);
    selection->display = core::trim(selection->display);
    auto resolved = resolve_runtime_reasoning_selection(model(), std::move(*selection));
    if (!resolved)
    {
      return std::unexpected(std::move(resolved.error()));
    }
    selection = std::move(*resolved);
  }
  if (same_reasoning_selection(reasoning(), selection))
    return false;

  auto appended = append_owned(make_reasoning_change_entry(model(), selection));
  if (!appended)
    return std::unexpected(std::move(appended.error()));
  model_selection().reasoning = std::move(selection);
  if (auto refreshed = refresh_parent_configuration(); !refreshed)
    return std::unexpected(std::move(refreshed.error()));
  return true;
}

ava::core::Result<ava::session::SessionMetadataView> Session::load_runtime_metadata() const
{
  auto read_authority = this->read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  auto entries = read_authority->load();
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  return ava::session::session_metadata_from_entries(store.session_id(), *entries);
}

std::string Session::state_result_json(bool cancel_requested) const
{
  return rpc::detail::SessionResultSerializer({}, *this).state_result_json(cancel_requested);
}

ava::core::Result<std::string> Session::messages_result_json() const
{
  return rpc::detail::MessagesResultSerializer({}, *this).run();
}

ava::core::Result<std::string> Session::list_sessions_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).list_sessions_result_json();
}

ava::core::Result<std::string> Session::session_tree_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).session_tree_result_json();
}

ava::core::Result<std::string> Session::list_models_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).list_models_result_json();
}

ava::core::Result<std::string> Session::session_stats_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).session_stats_result_json();
}

ava::core::Result<std::string> Session::session_validation_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).session_validation_result_json();
}

}  // namespace ava::app::runtime
