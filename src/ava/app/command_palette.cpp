#include "ava/app/command_palette.h"
#include "ava/app/runtime.h"
#include "ava/plugin/diagnostics.h"
#include "ava/mcp/config.h"
#include "ava/config/model_config.h"
#include "ava/config/provider_profiles.h"
#include "ava/session/session_store.h"
#include "ava/provider/registry.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

std::string hotkeys_for_action(std::vector<CommandHotkey> const& hotkeys, std::string_view action)
{
  for (auto const& hotkey : hotkeys)
  {
    if (hotkey.action == action)
      return hotkey.keys;
  }
  return "";
}

bool completion_exists(std::vector<tui::SlashCommandArgumentCompletion> const& completions, std::size_t argument_index,
                       std::vector<std::string> const& previous_args, std::string_view value)
{
  return std::ranges::any_of(completions, [&](auto const& completion) {
    return completion.argument_index == argument_index && completion.required_previous_args == previous_args && completion.value == value;
  });
}

void add_completion(tui::SlashCommandItem& item, std::size_t argument_index, std::string value, std::string description = {}, std::string category = {},
                    std::vector<std::string> previous_args = {}, bool append_space = true, bool enabled = true, std::string disabled_reason = {})
{
  if (value.empty() || completion_exists(item.argument_completions, argument_index, previous_args, value))
    return;
  item.argument_completions.push_back(tui::SlashCommandArgumentCompletion{.value = std::move(value),
                                                                          .description = std::move(description),
                                                                          .category = std::move(category),
                                                                          .required_previous_args = std::move(previous_args),
                                                                          .argument_index = argument_index,
                                                                          .append_space = append_space,
                                                                          .enabled = enabled,
                                                                          .disabled_reason = std::move(disabled_reason)});
}

std::optional<std::size_t> find_item_index(std::vector<tui::SlashCommandItem> const& items, std::string_view command)
{
  for (std::size_t index = 0; index < items.size(); ++index)
  {
    if (items[index].command == command)
      return index;
  }
  return std::nullopt;
}

std::vector<ava::config::ModelInfo> effective_models(ava::config::ModelRegistry const& registry)
{
  std::vector<ava::config::ModelInfo> models;
  std::vector<std::string> seen;
  for (auto model = registry.models.rbegin(); model != registry.models.rend(); ++model)
  {
    auto const key = model->provider_id + "\n" + model->model_id;
    if (std::ranges::find(seen, key) != seen.end())
      continue;
    seen.push_back(key);
    models.push_back(*model);
  }
  std::reverse(models.begin(), models.end());
  return models;
}

std::string model_completion_description(ava::config::ModelInfo const& model, bool registered)
{
  auto description = model.display_name.empty() ? model.model_id : model.display_name;
  if (!registered)
  {
    if (!description.empty())
      description += " - ";
    description += "provider unavailable";
  }
  else if (model.supports_reasoning.value_or(false) && !model.reasoning_levels.empty())
  {
    description += " - reasoning levels: ";
    for (std::size_t index = 0; index < model.reasoning_levels.size(); ++index)
    {
      if (index > 0)
        description += ", ";
      description += model.reasoning_levels[index];
    }
  }
  return description;
}

std::string optional_capability_text(std::string_view label, std::optional<bool> value)
{
  if (!value)
    return std::string(label) + " ?";
  return std::string(label) + (*value ? " yes" : " no");
}

std::string model_selector_detail(ava::config::ModelInfo const& model)
{
  std::string detail = optional_capability_text("tools", model.supports_tools) + " · " + optional_capability_text("stream", model.supports_streaming) + " · " +
                       optional_capability_text("reasoning", model.supports_reasoning);
  if (model.context_window_tokens)
    detail += " · ctx " + std::to_string(*model.context_window_tokens);
  if (!model.reasoning_levels.empty())
  {
    detail += " · levels ";
    for (std::size_t index = 0; index < model.reasoning_levels.size(); ++index)
    {
      if (index > 0)
        detail += "/";
      detail += model.reasoning_levels[index];
    }
  }
  return detail;
}

tui::SelectListItemView model_selector_item(ava::config::ModelInfo const& model, ava::config::ModelInfo const& current_model, bool registered)
{
  auto const current = model.provider_id == current_model.provider_id && model.model_id == current_model.model_id;
  auto label = model.display_name.empty() ? model.model_id : model.display_name;
  auto description = model.provider_id + "/" + model.model_id;
  return tui::SelectListItemView{.value = description,
                                 .label = std::move(label),
                                 .description = std::move(description),
                                 .group = model.provider_id,
                                 .detail = model_selector_detail(model),
                                 .badge = model.supports_reasoning.value_or(false) ? std::string("reasoning") : std::string{},
                                 .current = current,
                                 .enabled = registered,
                                 .disabled_reason = registered ? std::string{} : std::string("provider unavailable")};
}

std::string session_sort_label(SessionSelectorSort sort)
{
  switch (sort)
  {
    case SessionSelectorSort::Recent:
      return "recent";
    case SessionSelectorSort::Name:
      return "name";
    case SessionSelectorSort::Path:
      return "path";
  }
  return "recent";
}

std::string session_selector_detail(ava::session::SessionSummary const& summary)
{
  std::string detail = "entries " + std::to_string(summary.entry_count);
  if (!summary.last_updated.empty())
    detail += " · updated " + summary.last_updated;
  return detail;
}

tui::SelectListItemView session_selector_item(ava::session::SessionSummary const& summary, std::string_view current_session_id)
{
  auto const current = !current_session_id.empty() && summary.session_id == current_session_id;
  auto path = summary.path.empty() ? std::string("path unavailable") : summary.path.generic_string();
  return tui::SelectListItemView{.value = summary.session_id,
                                 .label = summary.session_id,
                                 .description = std::move(path),
                                 .group = "Sessions",
                                 .detail = session_selector_detail(summary),
                                 .badge = current ? std::string("current") : std::string{},
                                 .current = current,
                                 .enabled = true,
                                 .disabled_reason = {}};
}

void sort_session_summaries(std::vector<ava::session::SessionSummary>& summaries, SessionSelectorSort sort)
{
  std::ranges::sort(summaries, [&](ava::session::SessionSummary const& left, ava::session::SessionSummary const& right) {
    switch (sort)
    {
      case SessionSelectorSort::Recent:
        if (left.last_updated != right.last_updated)
          return left.last_updated > right.last_updated;
        return left.session_id > right.session_id;
      case SessionSelectorSort::Name:
        return left.session_id < right.session_id;
      case SessionSelectorSort::Path:
        if (left.path.generic_string() != right.path.generic_string())
        {
          return left.path.generic_string() < right.path.generic_string();
        }
        return left.session_id < right.session_id;
    }
    return left.session_id < right.session_id;
  });
}

ava::mcp::McpConfigLoadOptions mcp_config_options(RuntimeSession const& session)
{
  auto options = ava::mcp::default_mcp_config_options(session.workspace_dir);
  options.global_config_file = session.paths.ava_config_dir / "mcp.json";
  options.project_config_file = session.workspace_dir / ".ava" / "mcp.json";
  return options;
}

ava::plugin::PluginDiscoveryOptions plugin_discovery_options(RuntimeSession const& session)
{
  return ava::plugin::PluginDiscoveryOptions{.global_plugins_dir = session.paths.ava_config_dir / "plugins",
                                             .project_plugins_dir = session.workspace_dir / ".ava" / "plugins"};
}

std::filesystem::path plugin_enablement_file(RuntimeSession const& session)
{
  return session.paths.ava_state_dir / "plugin-enablement.json";
}

void add_backend_argument_completions(std::vector<tui::SlashCommandItem>& items, RuntimeSession const& session)
{
  if (auto index = find_item_index(items, "/models"))
  {
    auto& item = items[*index];
    auto const providers = ava::provider::builtin_provider_registry();
    if (auto registry = ava::config::load_model_registry(session.paths))
    {
      for (auto const& model : effective_models(*registry))
      {
        auto const registered = providers.contains(model.provider_id);
        add_completion(item, 0, model.provider_id + "/" + model.model_id, model_completion_description(model, registered), "Models", {}, false, registered,
                       registered ? "" : "provider is not registered");
        add_completion(item, 0, model.model_id, model.provider_id + "/" + model.display_name, "Models", {}, false, registered,
                       registered ? "" : "provider is not registered");
      }
    }
  }

  if (auto index = find_item_index(items, "/sessions"))
  {
    auto& item = items[*index];
    if (auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir))
    {
      for (auto const& summary : *sessions)
      {
        auto description = "entries=" + std::to_string(summary.entry_count);
        if (!summary.last_updated.empty())
          description += " updated=" + summary.last_updated;
        add_completion(item, 0, summary.session_id, std::move(description), "Sessions", {}, false);
      }
    }
  }

  if (auto index = find_item_index(items, "/context"))
  {
    auto& item = items[*index];
    for (auto const& source : session.context_sources)
    {
      add_completion(item, 0, source.path.generic_string(), std::to_string(source.byte_count) + " bytes", "Context", {}, false);
    }
  }

  if (auto index = find_item_index(items, "/mcp"))
  {
    auto& item = items[*index];
    for (auto const& subcommand : {"list", "inspect", "tools", "restart"})
    {
      add_completion(item, 0, subcommand, "MCP command", "MCP");
    }
    if (auto config = ava::mcp::load_mcp_config(mcp_config_options(session)))
    {
      for (auto const& server : config->servers)
      {
        auto const description = server.name.empty() ? std::string("configured MCP server") : server.name;
        for (auto const& subcommand : {"inspect", "tools", "restart"})
        {
          add_completion(item, 1, server.id, description, "MCP", {subcommand});
        }
      }
    }
  }

  auto const diagnostics = ava::plugin::collect_plugin_diagnostics(plugin_discovery_options(session), plugin_enablement_file(session), session.workspace_dir);
  if (auto index = find_item_index(items, "/plugins"))
  {
    auto& item = items[*index];
    for (auto const& subcommand : {"list", "inspect", "enable", "disable", "validate", "failures", "prompts", "prompt", "skills", "skill"})
    {
      add_completion(item, 0, subcommand, "Plugin command", "Plugins");
    }
    for (auto const& status : diagnostics.plugins)
    {
      auto const& manifest = status.plugin.manifest;
      auto description = manifest.name;
      if (!status.enabled)
        description += " (disabled)";
      for (auto const& subcommand : {"inspect", "enable", "disable", "prompts", "prompt", "skills", "skill"})
      {
        add_completion(item, 1, manifest.id, description, "Plugins", {subcommand});
      }
      for (auto const& prompt : manifest.contributes.prompts)
      {
        add_completion(item, 2, prompt.name, prompt.description, "Prompts", {"prompt", manifest.id});
      }
      for (auto const& skill : manifest.contributes.skills)
      {
        add_completion(item, 2, skill.name, skill.description, "Skills", {"skill", manifest.id});
      }
    }
  }

  if (auto index = find_item_index(items, "/plugin"))
  {
    auto& item = items[*index];
    add_completion(item, 0, "run", "Run an enabled plugin command", "Plugins");
    for (auto const& status : diagnostics.plugins)
    {
      auto const& manifest = status.plugin.manifest;
      add_completion(item, 1, manifest.id, status.enabled ? manifest.name : manifest.name + " (disabled)", "Plugins", {"run"}, true, status.enabled,
                     status.enabled ? "" : "plugin is disabled");
      for (auto const& command : manifest.contributes.commands)
      {
        add_completion(item, 2, command.name, command.description, "Plugin commands", {"run", manifest.id});
      }
    }
  }
}

}  // namespace

std::vector<tui::SlashCommandItem> command_catalog_slash_items(std::vector<CommandHotkey> const& hotkeys)
{
  std::vector<tui::SlashCommandItem> items;
  items.reserve(command_catalog().size());
  for (auto const& entry : command_catalog())
  {
    std::string key_display;
    if (entry.command == "/mode")
      key_display = hotkeys_for_action(hotkeys, "mode_toggle");
    if (entry.command == "/details")
      key_display = hotkeys_for_action(hotkeys, "details_toggle");
    if (entry.command == "/quit")
      key_display = hotkeys_for_action(hotkeys, "exit");
    items.push_back(tui::SlashCommandItem{.command = entry.command,
                                          .description = entry.description,
                                          .hint = entry.hint,
                                          .category = entry.category,
                                          .aliases = entry.aliases,
                                          .key_display = std::move(key_display),
                                          .enabled = entry.enabled,
                                          .disabled_reason = entry.disabled_reason});
  }
  return items;
}

std::vector<tui::SlashCommandItem> command_catalog_slash_items(RuntimeSession const& session, std::vector<CommandHotkey> const& hotkeys)
{
  auto items = command_catalog_slash_items(hotkeys);
  add_backend_argument_completions(items, session);
  return items;
}

tui::SelectListView model_selector_view(ava::config::ModelRegistry const& registry, ava::config::ModelInfo const& current_model, std::string footer_hint)
{
  auto const providers = ava::provider::builtin_provider_registry();
  auto models = effective_models(registry);

  tui::SelectListView view{
      .title = "Select model",
      .subtitle = "Current " + current_model.provider_id + "/" + current_model.model_id + " · selection is validated by the backend before session mutation",
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search models",
      .empty_text = "No configured models match",
      .footer_hint = std::move(footer_hint)};
  view.items.reserve(models.size() + 1);

  bool current_in_catalog = false;
  for (auto const& model : models)
  {
    auto const current = model.provider_id == current_model.provider_id && model.model_id == current_model.model_id;
    current_in_catalog = current_in_catalog || current;
    if (current)
      view.selected_item_index = view.items.size();
    view.items.push_back(model_selector_item(model, current_model, providers.contains(model.provider_id)));
  }

  if (!current_in_catalog && !current_model.provider_id.empty() && !current_model.model_id.empty())
  {
    view.selected_item_index = view.items.size();
    view.items.push_back(model_selector_item(current_model, current_model, providers.contains(current_model.provider_id)));
  }

  return view;
}

tui::SelectListView model_selector_view(RuntimeSession const& session, std::string footer_hint)
{
  auto registry = ava::config::load_model_registry(session.paths);
  if (registry)
    return model_selector_view(*registry, session.model, std::move(footer_hint));

  return tui::SelectListView{.title = "Select model",
                             .subtitle = "Unable to load configured models",
                             .items = {tui::SelectListItemView{.value = {},
                                                               .label = "Model registry unavailable",
                                                               .description = registry.error().format(),
                                                               .group = "Models",
                                                               .detail = {},
                                                               .badge = {},
                                                               .current = false,
                                                               .enabled = false,
                                                               .disabled_reason = "model registry failed to load"}},
                             .selected_item_index = 0,
                             .query = {},
                             .placeholder = "Search models",
                             .empty_text = "No configured models match",
                             .footer_hint = std::move(footer_hint)};
}

tui::SelectListView session_selector_view(std::vector<ava::session::SessionSummary> summaries, std::string current_session_id, SessionSelectorSort sort,
                                          std::string footer_hint)
{
  sort_session_summaries(summaries, sort);

  tui::SelectListView view{
      .title = "Select session",
      .subtitle = "Linear sessions from the existing JSONL store · sort " + session_sort_label(sort) + " · tree/fork views need a backend session schema",
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search sessions",
      .empty_text = "No sessions match",
      .footer_hint = footer_hint.empty() ? std::string("Enter choose · type to filter · Esc cancel") : std::move(footer_hint)};
  view.items.reserve(summaries.size() + 1);

  bool current_found = false;
  for (auto const& summary : summaries)
  {
    if (!current_session_id.empty() && summary.session_id == current_session_id)
    {
      view.selected_item_index = view.items.size();
      current_found = true;
    }
    view.items.push_back(session_selector_item(summary, current_session_id));
  }

  if (!current_found && !current_session_id.empty())
  {
    view.selected_item_index = view.items.size();
    view.items.push_back(tui::SelectListItemView{.value = current_session_id,
                                                 .label = current_session_id,
                                                 .description = "current session metadata unavailable",
                                                 .group = "Sessions",
                                                 .detail = "path/model/message count unavailable from current view",
                                                 .badge = "current",
                                                 .current = true,
                                                 .enabled = true,
                                                 .disabled_reason = {}});
  }

  if (view.items.empty())
  {
    view.items.push_back(tui::SelectListItemView{.value = {},
                                                 .label = "No sessions found",
                                                 .description = "Start a conversation to create a session",
                                                 .group = "Sessions",
                                                 .detail = {},
                                                 .badge = {},
                                                 .current = false,
                                                 .enabled = false,
                                                 .disabled_reason = "session list is empty"});
  }

  return view;
}

tui::SelectListView session_selector_view(RuntimeSession const& session, SessionSelectorSort sort, std::string footer_hint)
{
  auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir);
  if (sessions)
    return session_selector_view(std::move(*sessions), session.store.session_id(), sort, std::move(footer_hint));

  return tui::SelectListView{.title = "Select session",
                             .subtitle = "Unable to load session list",
                             .items = {tui::SelectListItemView{.value = {},
                                                               .label = "Session list unavailable",
                                                               .description = sessions.error().format(),
                                                               .group = "Sessions",
                                                               .detail = {},
                                                               .badge = {},
                                                               .current = false,
                                                               .enabled = false,
                                                               .disabled_reason = "session list failed to load"}},
                             .selected_item_index = 0,
                             .query = {},
                             .placeholder = "Search sessions",
                             .empty_text = "No sessions match",
                             .footer_hint = std::move(footer_hint)};
}

}  // namespace ava::app
