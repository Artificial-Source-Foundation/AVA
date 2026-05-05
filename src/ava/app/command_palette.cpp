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
  for (auto const& hotkey : hotkeys) {
    if (hotkey.action == action) return hotkey.keys;
  }
  return "";
}

bool completion_exists(std::vector<tui::SlashCommandArgumentCompletion> const& completions, std::size_t argument_index,
                       std::vector<std::string> const& previous_args, std::string_view value)
{
  return std::ranges::any_of(completions, [&](auto const& completion) {
    return completion.argument_index == argument_index && completion.required_previous_args == previous_args &&
           completion.value == value;
  });
}

void add_completion(tui::SlashCommandItem& item, std::size_t argument_index, std::string value,
                    std::string description = {}, std::string category = {},
                    std::vector<std::string> previous_args = {}, bool append_space = true, bool enabled = true,
                    std::string disabled_reason = {})
{
  if (value.empty() || completion_exists(item.argument_completions, argument_index, previous_args, value)) return;
  item.argument_completions.push_back(
      tui::SlashCommandArgumentCompletion{.value = std::move(value),
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
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (items[index].command == command) return index;
  }
  return std::nullopt;
}

std::vector<ava::config::ModelInfo> effective_models(ava::config::ModelRegistry const& registry)
{
  std::vector<ava::config::ModelInfo> models;
  std::vector<std::string> seen;
  for (auto model = registry.models.rbegin(); model != registry.models.rend(); ++model) {
    auto const key = model->provider_id + "\n" + model->model_id;
    if (std::ranges::find(seen, key) != seen.end()) continue;
    seen.push_back(key);
    models.push_back(*model);
  }
  std::reverse(models.begin(), models.end());
  return models;
}

std::string model_completion_description(ava::config::ModelInfo const& model, bool registered)
{
  auto description = model.display_name.empty() ? model.model_id : model.display_name;
  if (!registered) {
    if (!description.empty()) description += " - ";
    description += "provider unavailable";
  } else if (model.supports_reasoning.value_or(false) && !model.reasoning_levels.empty()) {
    description += " - reasoning levels: ";
    for (std::size_t index = 0; index < model.reasoning_levels.size(); ++index) {
      if (index > 0) description += ", ";
      description += model.reasoning_levels[index];
    }
  }
  return description;
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
  if (auto index = find_item_index(items, "/models")) {
    auto& item = items[*index];
    auto const providers = ava::provider::builtin_provider_registry();
    if (auto registry = ava::config::load_model_registry(session.paths)) {
      for (auto const& model : effective_models(*registry)) {
        auto const registered = providers.contains(model.provider_id);
        add_completion(item, 0, model.provider_id + "/" + model.model_id,
                       model_completion_description(model, registered), "Models", {}, false, registered,
                       registered ? "" : "provider is not registered");
        add_completion(item, 0, model.model_id, model.provider_id + "/" + model.display_name, "Models", {}, false,
                       registered, registered ? "" : "provider is not registered");
      }
    }
  }

  if (auto index = find_item_index(items, "/sessions")) {
    auto& item = items[*index];
    if (auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir)) {
      for (auto const& summary : *sessions) {
        auto description = "entries=" + std::to_string(summary.entry_count);
        if (!summary.last_updated.empty()) description += " updated=" + summary.last_updated;
        add_completion(item, 0, summary.session_id, std::move(description), "Sessions", {}, false);
      }
    }
  }

  if (auto index = find_item_index(items, "/context")) {
    auto& item = items[*index];
    for (auto const& source : session.context_sources) {
      add_completion(item, 0, source.path.generic_string(), std::to_string(source.byte_count) + " bytes", "Context", {},
                     false);
    }
  }

  if (auto index = find_item_index(items, "/mcp")) {
    auto& item = items[*index];
    for (auto const& subcommand : {"list", "inspect", "tools", "restart"}) {
      add_completion(item, 0, subcommand, "MCP command", "MCP");
    }
    if (auto config = ava::mcp::load_mcp_config(mcp_config_options(session))) {
      for (auto const& server : config->servers) {
        auto const description = server.name.empty() ? std::string("configured MCP server") : server.name;
        for (auto const& subcommand : {"inspect", "tools", "restart"}) {
          add_completion(item, 1, server.id, description, "MCP", {subcommand});
        }
      }
    }
  }

  auto const diagnostics = ava::plugin::collect_plugin_diagnostics(
      plugin_discovery_options(session), plugin_enablement_file(session), session.workspace_dir);
  if (auto index = find_item_index(items, "/plugins")) {
    auto& item = items[*index];
    for (auto const& subcommand :
         {"list", "inspect", "enable", "disable", "validate", "failures", "prompts", "prompt", "skills", "skill"}) {
      add_completion(item, 0, subcommand, "Plugin command", "Plugins");
    }
    for (auto const& status : diagnostics.plugins) {
      auto const& manifest = status.plugin.manifest;
      auto description = manifest.name;
      if (!status.enabled) description += " (disabled)";
      for (auto const& subcommand : {"inspect", "enable", "disable", "prompts", "prompt", "skills", "skill"}) {
        add_completion(item, 1, manifest.id, description, "Plugins", {subcommand});
      }
      for (auto const& prompt : manifest.contributes.prompts) {
        add_completion(item, 2, prompt.name, prompt.description, "Prompts", {"prompt", manifest.id});
      }
      for (auto const& skill : manifest.contributes.skills) {
        add_completion(item, 2, skill.name, skill.description, "Skills", {"skill", manifest.id});
      }
    }
  }

  if (auto index = find_item_index(items, "/plugin")) {
    auto& item = items[*index];
    add_completion(item, 0, "run", "Run an enabled plugin command", "Plugins");
    for (auto const& status : diagnostics.plugins) {
      auto const& manifest = status.plugin.manifest;
      add_completion(item, 1, manifest.id, status.enabled ? manifest.name : manifest.name + " (disabled)", "Plugins",
                     {"run"}, true, status.enabled, status.enabled ? "" : "plugin is disabled");
      for (auto const& command : manifest.contributes.commands) {
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
  for (auto const& entry : command_catalog()) {
    std::string key_display;
    if (entry.command == "/mode") key_display = hotkeys_for_action(hotkeys, "mode_toggle");
    if (entry.command == "/details") key_display = hotkeys_for_action(hotkeys, "details_toggle");
    if (entry.command == "/quit") key_display = hotkeys_for_action(hotkeys, "exit");
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

std::vector<tui::SlashCommandItem> command_catalog_slash_items(RuntimeSession const& session,
                                                               std::vector<CommandHotkey> const& hotkeys)
{
  auto items = command_catalog_slash_items(hotkeys);
  add_backend_argument_completions(items, session);
  return items;
}

}  // namespace ava::app
