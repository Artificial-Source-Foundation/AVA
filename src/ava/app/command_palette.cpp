#include "sys.h"
#include "ava/app/command_models.h"
#include "ava/app/command_palette.h"
#include "ava/app/display_settings.h"
#include "ava/app/runtime.h"
#include "ava/plugin/diagnostics.h"
#include "ava/mcp/config.h"
#include "ava/config/model_config.h"
#include "ava/config/provider_profiles.h"
#include "ava/session/session_store.h"
#include "ava/session/session_tree.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/registry.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

constexpr std::size_t kMaxPathCompletionVisited = 20000;
constexpr std::size_t kMaxPathCompletions = 2000;
constexpr std::size_t kMaxPathCompletionDepth = 8;

struct PathCompletionCandidate
{
  std::string value;
  std::string description;
  bool directory = false;
};

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

bool has_ascii_space(std::string_view text)
{
  return text.find_first_of(" \t\r\n") != std::string_view::npos;
}

bool has_unquotable_file_reference_char(std::string_view text)
{
  return text.find_first_of("\"\t\r\n") != std::string_view::npos;
}

bool path_is_under(std::filesystem::path const& path, std::filesystem::path const& root)
{
  auto const relative = path.lexically_normal().lexically_relative(root.lexically_normal());
  return !relative.empty() && relative.native().find("..") != 0 && !relative.is_absolute();
}

bool is_reference_code_path(runtime::Session const& session, std::filesystem::path const& path)
{
  return path_is_under(path, session.workspace_dir / "docs" / "reference-code") ||
         path.lexically_normal() == (session.workspace_dir / "docs" / "reference-code").lexically_normal();
}

bool should_skip_path_completion_entry(runtime::Session const& session, std::filesystem::directory_entry const& entry)
{
  auto const name = entry.path().filename().generic_string();
  if (name == ".git" || name == "node_modules" || name == ".cache" || name == ".ccache")
    return true;
  if (name == "build" || name.starts_with("build-"))
    return true;
  return is_reference_code_path(session, entry.path());
}

std::optional<std::string> completion_relative_path(std::filesystem::path const& base, std::filesystem::path const& path, bool directory, bool allow_spaces)
{
  std::error_code error;
  auto relative = std::filesystem::relative(path, base, error);
  if (error || relative.empty() || relative == ".")
    return std::nullopt;
  auto value = relative.generic_string();
  if (value.empty() || value.starts_with("../") || value == ".." || value.starts_with('/'))
    return std::nullopt;
  if (directory && !value.ends_with('/'))
    value.push_back('/');
  if (has_unquotable_file_reference_char(value))
    return std::nullopt;
  if (!allow_spaces && has_ascii_space(value))
    return std::nullopt;
  return value;
}

std::string path_completion_description(std::filesystem::directory_entry const& entry, bool directory)
{
  if (directory)
    return "directory";

  std::error_code error;
  auto const size = entry.file_size(error);
  if (error)
    return "file";
  return "file " + std::to_string(size) + " bytes";
}

std::vector<PathCompletionCandidate> workspace_path_completions(runtime::Session const& session, bool allow_spaces = false)
{
  std::vector<PathCompletionCandidate> candidates;
  std::error_code error;
  if (!std::filesystem::is_directory(session.current_dir, error) || error)
    return candidates;

  std::size_t visited = 0;
  for (std::filesystem::recursive_directory_iterator it(session.current_dir, error), end; it != end && visited < kMaxPathCompletionVisited; it.increment(error))
  {
    if (error)
    {
      error.clear();
      continue;
    }

    auto const entry = *it;
    ++visited;
    if (should_skip_path_completion_entry(session, entry))
    {
      if (entry.is_directory(error))
        it.disable_recursion_pending();
      error.clear();
      continue;
    }

    auto const directory = entry.is_directory(error);
    if (error)
    {
      error.clear();
      continue;
    }
    if (static_cast<std::size_t>(it.depth()) >= kMaxPathCompletionDepth)
      it.disable_recursion_pending();

    auto value = completion_relative_path(session.current_dir, entry.path(), directory, allow_spaces);
    if (!value)
      continue;
    candidates.push_back(
        PathCompletionCandidate{.value = std::move(*value), .description = path_completion_description(entry, directory), .directory = directory});
  }

  std::ranges::sort(candidates, [](PathCompletionCandidate const& left, PathCompletionCandidate const& right) {
    if (left.directory != right.directory)
      return left.directory > right.directory;
    return left.value < right.value;
  });
  if (candidates.size() > kMaxPathCompletions)
    candidates.resize(kMaxPathCompletions);
  return candidates;
}

std::string glob_completion_value(PathCompletionCandidate const& candidate)
{
  if (!candidate.directory)
    return candidate.value;
  auto value = candidate.value;
  if (!value.empty() && value.ends_with('/'))
    value.pop_back();
  value += "/**";
  return value;
}

void add_path_completions(tui::SlashCommandItem& item, std::vector<PathCompletionCandidate> const& candidates, std::size_t argument_index,
                          bool file_append_space)
{
  for (auto const& candidate : candidates)
  {
    add_completion(item, argument_index, candidate.value, candidate.description, "Files", {}, candidate.directory ? false : file_append_space);
  }
}

void add_glob_completions(tui::SlashCommandItem& item, std::vector<PathCompletionCandidate> const& candidates, std::size_t argument_index)
{
  for (auto const& candidate : candidates)
  {
    auto description = candidate.directory ? std::string("directory glob") : candidate.description;
    add_completion(item, argument_index, glob_completion_value(candidate), std::move(description), "Files", {}, false);
  }
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
  auto const diagnostics = model_configuration_diagnostics(model, registered);
  if (!registered)
  {
    if (!description.empty())
      description += " - ";
    description += "provider unavailable";
  }
  else if (!diagnostics.empty())
  {
    description += " - diagnostics " + std::to_string(diagnostics.size());
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

std::string model_selector_detail(ava::config::ModelInfo const& model, std::vector<std::string> const& diagnostics)
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
  if (!diagnostics.empty())
  {
    detail += " · diagnostics " + std::to_string(diagnostics.size()) + ": " + diagnostics.front();
  }
  return detail;
}

tui::SelectListItemView model_selector_item(ava::config::ModelInfo const& model, ava::config::ModelInfo const& current_model, bool registered)
{
  auto const current = model.provider_id == current_model.provider_id && model.model_id == current_model.model_id;
  auto label = model.display_name.empty() ? model.model_id : model.display_name;
  auto description = model.provider_id + "/" + model.model_id;
  auto const diagnostics = model_configuration_diagnostics(model, registered);
  if (!diagnostics.empty())
    description += " · diagnostics " + std::to_string(diagnostics.size());
  auto badge = std::string{};
  if (registered && !diagnostics.empty())
    badge = "diagnostics";
  else if (model.supports_reasoning.value_or(false))
    badge = "reasoning";
  return tui::SelectListItemView{.value = description,
                                 .label = std::move(label),
                                 .description = std::move(description),
                                 .group = model.provider_id,
                                 .detail = model_selector_detail(model, diagnostics),
                                 .badge = std::move(badge),
                                 .current = current,
                                 .enabled = registered,
                                 .disabled_reason = registered ? std::string{} : std::string("provider unavailable")};
}

std::string model_selector_value(ava::config::ModelInfo const& model)
{
  return model.provider_id + "/" + model.model_id;
}

bool scoped_model_enabled(std::optional<std::vector<std::string>> const& scoped_model_cycle, std::string_view value)
{
  if (!scoped_model_cycle)
    return true;
  return std::ranges::find_if(*scoped_model_cycle, [&](auto const& existing) { return existing == value; }) != scoped_model_cycle->end();
}

std::vector<ava::config::ModelInfo> scoped_model_selector_models(std::vector<ava::config::ModelInfo> models,
                                                                 std::optional<std::vector<std::string>> const& scoped_model_cycle)
{
  if (!scoped_model_cycle)
    return models;

  std::vector<ava::config::ModelInfo> sorted;
  sorted.reserve(models.size());
  for (auto const& id : *scoped_model_cycle)
  {
    auto const found = std::ranges::find_if(models, [&](auto const& model) { return model_selector_value(model) == id; });
    if (found != models.end())
      sorted.push_back(*found);
  }
  for (auto const& model : models)
  {
    auto const value = model_selector_value(model);
    auto const already_added =
        std::ranges::find_if(sorted, [&](auto const& existing) { return existing.provider_id == model.provider_id && existing.model_id == model.model_id; });
    if (already_added == sorted.end())
      sorted.push_back(model);
  }
  return sorted;
}

tui::SelectListItemView scoped_model_selector_item(ava::config::ModelInfo const& model, ava::config::ModelInfo const& current_model,
                                                   std::optional<std::vector<std::string>> const& scoped_model_cycle, bool registered)
{
  auto item = model_selector_item(model, current_model, registered);
  auto const value = model_selector_value(model);
  bool const enabled_for_cycle = scoped_model_enabled(scoped_model_cycle, value);
  item.value = value;
  item.description = value + (enabled_for_cycle ? " · enabled" : " · disabled");
  if (!registered)
    item.description += " · provider unavailable";
  item.badge = enabled_for_cycle ? (scoped_model_cycle ? std::string("enabled") : std::string("all-enabled")) : std::string("disabled");
  item.detail += enabled_for_cycle ? " · scoped cycle enabled" : " · scoped cycle disabled";
  item.enabled = registered;
  item.disabled_reason = registered ? std::string{} : std::string("provider unavailable");
  return item;
}

std::string session_sort_label(SessionSelectorSort sort)
{
  return session_selector_sort_label(sort);
}

std::string session_selector_detail(ava::session::SessionSummary const& summary)
{
  std::string detail = "entries " + std::to_string(summary.entry_count);
  if (!summary.last_updated.empty())
    detail += " · updated " + summary.last_updated;
  return detail;
}

std::string labels_text(std::vector<std::string> const& labels)
{
  std::string text;
  for (std::size_t index = 0; index < labels.size(); ++index)
  {
    if (index > 0)
      text += ", ";
    text += labels[index];
  }
  return text;
}

std::string session_completion_description(ava::session::SessionTreeNode const& node)
{
  std::string description;
  if (!node.metadata.name.empty())
    description += node.metadata.name + " · ";
  description += "entries=" + std::to_string(node.summary.entry_count);
  if (!node.summary.last_updated.empty())
    description += " updated=" + node.summary.last_updated;
  if (!node.metadata.branch_origin.empty())
    description += " origin=" + node.metadata.branch_origin;
  if (node.metadata.archived)
    description += " archived";
  if (!node.metadata.labels.empty())
    description += " labels=" + labels_text(node.metadata.labels);
  return description;
}

std::string session_node_label(ava::session::SessionTreeNode const& node, std::size_t depth)
{
  auto label = node.metadata.name.empty() ? node.summary.session_id : node.metadata.name;
  if (depth == 0)
    return label;
  return std::string(depth * 2, ' ') + "+ " + label;
}

std::string session_node_description(ava::session::SessionTreeNode const& node, bool show_paths)
{
  auto path = node.summary.path.empty() ? std::string("path unavailable") : node.summary.path.generic_string();
  if (!show_paths)
    return node.metadata.name.empty() ? std::string{} : node.summary.session_id;
  if (node.metadata.name.empty())
    return path;
  return node.summary.session_id + " · " + path;
}

std::string session_node_detail(ava::session::SessionTreeNode const& node, bool current_path, bool show_label_time)
{
  auto detail = session_selector_detail(node.summary);
  if (!node.metadata.branch_origin.empty())
    detail += " · origin " + node.metadata.branch_origin;
  if (!node.metadata.parent_session_id.empty())
    detail += " · parent " + node.metadata.parent_session_id;
  if (!node.metadata.branch_from_entry_id.empty())
    detail += " · from " + node.metadata.branch_from_entry_id;
  if (!node.metadata.labels.empty())
  {
    detail += " · labels " + labels_text(node.metadata.labels);
    if (show_label_time && !node.metadata.labels_updated.empty())
      detail += " updated " + node.metadata.labels_updated;
  }
  if (node.metadata.archived)
    detail += " · archived";
  if (!node.metadata.actor.empty())
    detail += " · actor " + node.metadata.actor;
  if (current_path)
    detail += " · current path";
  return detail;
}

tui::SelectListItemView session_selector_item(ava::session::SessionSummary const& summary, std::string_view current_session_id, bool show_paths)
{
  auto const current = !current_session_id.empty() && summary.session_id == current_session_id;
  auto path = summary.path.empty() ? std::string("path unavailable") : summary.path.generic_string();
  return tui::SelectListItemView{.value = summary.session_id,
                                 .label = summary.session_id,
                                 .description = show_paths ? std::move(path) : std::string{},
                                 .group = "Sessions",
                                 .detail = session_selector_detail(summary),
                                 .badge = current ? std::string("current") : std::string{},
                                 .current = current,
                                 .enabled = true,
                                 .disabled_reason = {}};
}

bool node_less(ava::session::SessionTreeNode const& left, ava::session::SessionTreeNode const& right, SessionSelectorSort sort)
{
  switch (sort)
  {
    case SessionSelectorSort::Recent:
      if (left.summary.last_updated != right.summary.last_updated)
        return left.summary.last_updated > right.summary.last_updated;
      return left.summary.session_id > right.summary.session_id;
    case SessionSelectorSort::Name: {
      auto const left_name = left.metadata.name.empty() ? left.summary.session_id : left.metadata.name;
      auto const right_name = right.metadata.name.empty() ? right.summary.session_id : right.metadata.name;
      if (left_name != right_name)
        return left_name < right_name;
      return left.summary.session_id < right.summary.session_id;
    }
    case SessionSelectorSort::Path:
      if (left.summary.path.generic_string() != right.summary.path.generic_string())
      {
        return left.summary.path.generic_string() < right.summary.path.generic_string();
      }
      return left.summary.session_id < right.summary.session_id;
  }
  return left.summary.session_id < right.summary.session_id;
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

std::unordered_map<std::string, std::size_t> session_tree_index_by_id(std::vector<ava::session::SessionTreeNode> const& nodes)
{
  std::unordered_map<std::string, std::size_t> index;
  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index)
  {
    index.emplace(nodes[node_index].summary.session_id, node_index);
  }
  return index;
}

std::vector<std::string> sorted_tree_ids(std::vector<std::string> ids, std::vector<ava::session::SessionTreeNode> const& nodes,
                                         std::unordered_map<std::string, std::size_t> const& index_by_id, SessionSelectorSort sort)
{
  std::erase_if(ids, [&](std::string const& id) { return index_by_id.find(id) == index_by_id.end(); });
  std::ranges::sort(
      ids, [&](std::string const& left, std::string const& right) { return node_less(nodes[index_by_id.at(left)], nodes[index_by_id.at(right)], sort); });
  return ids;
}

void append_session_tree_items(tui::SelectListView& view, std::vector<ava::session::SessionTreeNode> const& nodes,
                               std::unordered_map<std::string, std::size_t> const& index_by_id, std::vector<std::string> ids,
                               std::vector<std::string> const& current_path, SessionSelectorSort sort, std::size_t depth, bool named_only, bool show_paths,
                               bool show_archived, bool show_label_time)
{
  ids = sorted_tree_ids(std::move(ids), nodes, index_by_id, sort);
  for (auto const& id : ids)
  {
    auto const found = index_by_id.find(id);
    if (found == index_by_id.end())
      continue;
    auto const& node = nodes[found->second];
    auto const current_path_node = std::ranges::find(current_path, node.summary.session_id) != current_path.end();
    auto const visible = show_archived || !node.metadata.archived;
    if (visible && (!named_only || !node.metadata.name.empty()))
    {
      if (node.current)
        view.selected_item_index = view.items.size();
      view.items.push_back(tui::SelectListItemView{.value = node.summary.session_id,
                                                   .label = session_node_label(node, depth),
                                                   .description = session_node_description(node, show_paths),
                                                   .group = depth == 0 ? std::string("Root sessions") : std::string("Branches"),
                                                   .detail = session_node_detail(node, current_path_node, show_label_time),
                                                   .badge = node.current                           ? std::string("current")
                                                            : node.metadata.archived               ? std::string("archived")
                                                            : !node.metadata.branch_origin.empty() ? node.metadata.branch_origin
                                                                                                   : std::string("root"),
                                                   .current = node.current,
                                                   .enabled = true,
                                                   .disabled_reason = {}});
    }
    append_session_tree_items(view, nodes, index_by_id, node.children, current_path, sort, depth + (visible ? 1 : 0), named_only, show_paths, show_archived,
                              show_label_time);
  }
}

ava::mcp::McpConfigLoadOptions mcp_config_options(runtime::Session const& session)
{
  auto options = ava::mcp::default_mcp_config_options(session.workspace_dir);
  options.global_config_file = session.paths.ava_config_dir / "mcp.json";
  options.project_config_file = project_resources_trusted(session.project_trust) ? session.workspace_dir / ".ava" / "mcp.json" : std::filesystem::path{};
  return options;
}

ava::plugin::PluginDiscoveryOptions plugin_discovery_options(runtime::Session const& session)
{
  return ava::plugin::PluginDiscoveryOptions{
      .global_plugins_dir = session.paths.ava_config_dir / "plugins",
      .project_plugins_dir = project_resources_trusted(session.project_trust) ? session.workspace_dir / ".ava" / "plugins" : std::filesystem::path{}};
}

std::filesystem::path plugin_enablement_file(runtime::Session const& session)
{
  return session.paths.ava_state_dir / "plugin-enablement.json";
}

void add_backend_argument_completions(std::vector<tui::SlashCommandItem>& items, runtime::Session const& session, std::vector<CommandHotkey> const& hotkeys)
{
  auto const path_completions = workspace_path_completions(session);
  if (!path_completions.empty())
  {
    if (auto index = find_item_index(items, "/read"))
      add_path_completions(items[*index], path_completions, 0, false);
    if (auto index = find_item_index(items, "/attach"))
      add_path_completions(items[*index], path_completions, 0, false);
    if (auto index = find_item_index(items, "/write"))
      add_path_completions(items[*index], path_completions, 0, true);
    if (auto index = find_item_index(items, "/glob"))
      add_glob_completions(items[*index], path_completions, 0);
    if (auto index = find_item_index(items, "/find"))
      add_glob_completions(items[*index], path_completions, 0);
    if (auto index = find_item_index(items, "/ls"))
      add_path_completions(items[*index], path_completions, 0, false);
    if (auto index = find_item_index(items, "/grep"))
      add_glob_completions(items[*index], path_completions, 1);
    if (auto index = find_item_index(items, "/import"))
      add_path_completions(items[*index], path_completions, 0, false);
  }

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
      }
    }
  }

  if (auto index = find_item_index(items, "/hotkeys"))
  {
    auto& item = items[*index];
    add_completion(item, 0, "init", "Create $XDG_CONFIG_HOME/ava/keybinds.json from AVA defaults", "General");
    add_completion(item, 0, "import", "Import a validated keybindings JSON file", "General", {}, false);
    add_completion(item, 0, "set", "Set one keybinding action in keybinds.json", "General");
    add_completion(item, 0, "reset", "Remove one action override from keybinds.json", "General");
    add_completion(item, 0, "unset", "Alias for reset", "General");
    add_completion(item, 0, "validate", "Validate $XDG_CONFIG_HOME/ava/keybinds.json without reloading", "General", {}, false);
    for (auto const& hotkey : hotkeys)
    {
      add_completion(item, 1, hotkey.action, hotkey.description + " (" + hotkey.keys + ")", "Keybindings", {"set"}, true);
      add_completion(item, 1, hotkey.action, "Remove override for " + hotkey.description, "Keybindings", {"reset"}, true);
      add_completion(item, 1, hotkey.action, "Remove override for " + hotkey.description, "Keybindings", {"unset"}, true);
    }
    add_completion(item, 1, "--force", "Replace an existing keybinds.json starter file", "General", {"init"}, false);
    add_completion(item, 2, "--force", "Replace the existing keybinds.json with the imported file", "General", {"import"}, false);
  }

  if (auto index = find_item_index(items, "/theme"))
  {
    auto& item = items[*index];
    add_completion(item, 0, "dark", "Persist the built-in dark palette", "General", {}, false);
    add_completion(item, 0, "light", "Persist the built-in light palette", "General", {}, false);
    add_completion(item, 0, "plain", "Persist no-ANSI output", "General", {}, false);
    add_completion(item, 0, "reset", "Use the built-in default unless an environment override is set", "General", {}, false);
    for (auto const& theme : available_tui_custom_themes(session.paths))
    {
      add_completion(item, 0, theme.name, "Persist custom theme from " + theme.path.string(), "Themes", {}, false);
    }
  }

  if (auto index = find_item_index(items, "/reload"))
  {
    auto& item = items[*index];
    add_completion(item, 0, "keybindings", "Reload $XDG_CONFIG_HOME/ava/keybinds.json inside the TUI", "General", {}, false);
    add_completion(item, 0, "theme", "Reload $XDG_CONFIG_HOME/ava/display.json and repaint the TUI", "General", {}, false);
  }

  if (auto index = find_item_index(items, "/export"))
  {
    auto& item = items[*index];
    add_completion(item, 0, "markdown", "Return or write a Markdown session export", "Sessions", {}, false);
    add_completion(item, 0, "html", "Return or write a self-contained HTML session export", "Sessions", {}, false);
    add_completion(item, 0, "jsonl", "Return or write a sanitized portable AVA JSONL archive", "Sessions", {}, false);
  }

  if (auto index = find_item_index(items, "/import"))
  {
    auto& item = items[*index];
    add_completion(item, 1, "--confirm", "Create a new local session from the validated JSONL archive", "Sessions", {}, false);
  }

  auto add_session_completions_for = [&](std::string_view command) {
    auto index = find_item_index(items, command);
    if (!index)
      return;
    auto& item = items[*index];
    if (command == "/sessions")
    {
      add_completion(item, 0, "rename", "Rename a session without switching to it", "Sessions");
      add_completion(item, 0, "labels", "Set or clear labels on a session without switching to it", "Sessions");
      add_completion(item, 0, "archive", "Archive a session after confirmation", "Sessions");
      add_completion(item, 0, "unarchive", "Restore an archived session to default views", "Sessions");
      add_completion(item, 0, "--archived", "Include archived sessions in the list", "Sessions");
    }
    if (auto tree = ava::session::build_session_tree(session.workspace_dir, session.paths.sessions_dir, session.store.session_id()))
    {
      for (auto const& node : tree->sessions)
      {
        add_completion(item, 0, node.summary.session_id, session_completion_description(node), "Sessions", {}, false);
        if (command == "/sessions")
        {
          add_completion(item, 1, node.summary.session_id, session_completion_description(node), "Sessions", {"rename"}, false);
          add_completion(item, 1, node.summary.session_id, session_completion_description(node), "Sessions", {"labels"}, false);
          add_completion(item, 1, node.summary.session_id, session_completion_description(node), "Sessions", {"archive"}, false, true);
          add_completion(item, 1, node.summary.session_id, session_completion_description(node), "Sessions", {"unarchive"}, false, node.metadata.archived,
                         node.metadata.archived ? "" : "session is not archived");
          add_completion(item, 2, "--clear", "Clear labels without switching sessions", "Sessions", {"labels", node.summary.session_id}, false);
          add_completion(item, 2, "--confirm", "Confirm archive without deleting session files", "Sessions", {"archive", node.summary.session_id}, false);
        }
      }
    }
  };

  add_session_completions_for("/sessions");
  add_session_completions_for("/resume");

  if (auto index = find_item_index(items, "/permissions"))
  {
    auto& item = items[*index];
    for (auto const& subcommand : {"list", "audit", "diagnose", "explain", "add", "remove"})
    {
      add_completion(item, 0, subcommand, "Permission rule command", "Safety");
    }
    add_completion(item, 1, "export", "Render matching permission decisions as a Markdown table", "Safety", {"audit"}, false);
    add_completion(item, 1, "summary", "Summarize matching permission audit decisions", "Safety", {"audit"}, false);
    add_completion(item, 1, "show", "Inspect one permission audit entry or request", "Safety", {"audit"}, false);
    for (auto const& value : {"action=allow", "action=deny"})
    {
      add_completion(item, 1, value, "Persistent rule action", "Safety", {"add"});
    }
    for (auto const& value : {"operation=read", "operation=search", "operation=edit", "operation=bash", "operation=network.fetch", "operation=network.search",
                              "operation=skill", "operation=mcp.tool.call"})
    {
      add_completion(item, 1, value, "Persistent rule operation", "Safety", {"add"});
    }
    for (auto const& value : {"scope=workspace", "scope=global", "mode=any", "mode=build", "mode=plan"})
    {
      add_completion(item, 1, value, "Persistent rule field", "Safety", {"add"});
    }
    for (auto const& value : {"path=", "command=", "tool=", "reason="})
    {
      add_completion(item, 1, value, "Persistent rule field", "Safety", {"add"}, false);
    }
    auto const store = ava::permissions::PermissionRuleStore{
        .global_rules_file = session.paths.ava_config_dir / "permission-rules.json",
        .workspace_rules_file = session.workspace_dir / ".ava" / "permission-rules.json",
        .workspace_dir = session.workspace_dir,
    };
    if (auto rules = ava::permissions::load_persistent_permission_rules(store))
    {
      for (auto const& rule : *rules)
      {
        auto description =
            ava::permissions::to_string(rule.action) + " " + ava::permissions::to_string(rule.operation) + " " + ava::permissions::to_string(rule.scope);
        add_completion(item, 1, rule.rule_id, description, "Safety", {"explain"}, false);
        add_completion(item, 1, rule.rule_id, description, "Safety", {"remove"}, false);
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

  if (auto index = find_item_index(items, "/trust"))
  {
    auto& item = items[*index];
    add_completion(item, 0, "status", "Show current project trust decision", "Trust");
    add_completion(item, 0, "project", "Trust project resources for this workspace", "Trust");
    add_completion(item, 0, "deny", "Skip project resources for this workspace", "Trust");
    add_completion(item, 0, "clear", "Remove this workspace trust decision", "Trust");
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

SessionSelectorSort next_session_selector_sort(SessionSelectorSort sort) noexcept
{
  switch (sort)
  {
    case SessionSelectorSort::Recent:
      return SessionSelectorSort::Name;
    case SessionSelectorSort::Name:
      return SessionSelectorSort::Path;
    case SessionSelectorSort::Path:
      return SessionSelectorSort::Recent;
  }
  return SessionSelectorSort::Recent;
}

std::string session_selector_sort_label(SessionSelectorSort sort)
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

std::vector<tui::SlashCommandItem> command_catalog_slash_items(runtime::Session const& session, std::vector<CommandHotkey> const& hotkeys)
{
  auto items = command_catalog_slash_items(hotkeys);
  add_backend_argument_completions(items, session, hotkeys);
  return items;
}

std::vector<tui::FileReferenceItem> file_reference_items(runtime::Session const& session)
{
  std::vector<tui::FileReferenceItem> items;
  auto const candidates = workspace_path_completions(session, true);
  items.reserve(candidates.size());
  for (auto const& candidate : candidates)
  {
    items.push_back(tui::FileReferenceItem{.value = candidate.value,
                                           .description = candidate.description,
                                           .category = "Files",
                                           .directory = candidate.directory,
                                           .enabled = true,
                                           .disabled_reason = {}});
  }
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

tui::SelectListView model_selector_view(runtime::Session const& session, std::string footer_hint)
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

tui::SelectListView scoped_model_selector_view(ava::config::ModelRegistry const& registry, ava::config::ModelInfo const& current_model,
                                               std::optional<std::vector<std::string>> const& scoped_model_cycle, std::string footer_hint)
{
  auto const providers = ava::provider::builtin_provider_registry();
  auto models = scoped_model_selector_models(effective_models(registry), scoped_model_cycle);
  auto const enabled_count = scoped_model_cycle ? scoped_model_cycle->size() : models.size();

  tui::SelectListView view{
      .title = "Scoped model cycle",
      .subtitle = "Current Ctrl+P cycle scope · " + (scoped_model_cycle ? std::to_string(enabled_count) + "/" + std::to_string(models.size()) + " enabled"
                                                                        : std::string("all configured registered models enabled")),
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search models",
      .empty_text = "No configured models match",
      .footer_hint = std::move(footer_hint)};
  view.items.reserve(models.size());

  bool current_in_catalog = false;
  for (auto const& model : models)
  {
    auto const current = model.provider_id == current_model.provider_id && model.model_id == current_model.model_id;
    current_in_catalog = current_in_catalog || current;
    if (current)
      view.selected_item_index = view.items.size();
    view.items.push_back(scoped_model_selector_item(model, current_model, scoped_model_cycle, providers.contains(model.provider_id)));
  }

  if (!current_in_catalog && !current_model.provider_id.empty() && !current_model.model_id.empty())
  {
    view.selected_item_index = view.items.size();
    view.items.push_back(scoped_model_selector_item(current_model, current_model, scoped_model_cycle, providers.contains(current_model.provider_id)));
  }

  return view;
}

tui::SelectListView scoped_model_selector_view(runtime::Session const& session, std::string footer_hint)
{
  auto registry = ava::config::load_model_registry(session.paths);
  if (registry)
    return scoped_model_selector_view(*registry, session.model, session.scoped_model_cycle, std::move(footer_hint));

  return tui::SelectListView{.title = "Scoped model cycle",
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
                                          std::string footer_hint, bool show_paths)
{
  sort_session_summaries(summaries, sort);

  tui::SelectListView view{
      .title = "Select session",
      .subtitle = "Linear sessions from the existing JSONL store · sort " + session_sort_label(sort) +
                  (show_paths ? std::string(" · paths shown") : std::string(" · paths hidden")),
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search sessions",
      .empty_text = "No sessions match",
      .footer_hint = footer_hint.empty() ? std::string("Enter choose · PgUp/PgDn page · type to filter · Esc cancel") : std::move(footer_hint)};
  view.items.reserve(summaries.size() + 1);

  bool current_found = false;
  for (auto const& summary : summaries)
  {
    if (!current_session_id.empty() && summary.session_id == current_session_id)
    {
      view.selected_item_index = view.items.size();
      current_found = true;
    }
    view.items.push_back(session_selector_item(summary, current_session_id, show_paths));
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

tui::SelectListView session_selector_view(ava::session::SessionTreeIndex tree, SessionSelectorSort sort, std::string footer_hint, bool named_only,
                                          bool show_paths, bool show_archived, bool show_label_time)
{
  auto const index_by_id = session_tree_index_by_id(tree.sessions);
  tui::SelectListView view{
      .title = "Select session",
      .subtitle = "Session tree from JSONL branch metadata · sort " + session_sort_label(sort) +
                  (named_only ? std::string(" · named only") : std::string(" · all sessions")) +
                  (show_paths ? std::string(" · paths shown") : std::string(" · paths hidden")) +
                  (show_archived ? std::string(" · archived shown") : std::string(" · archived hidden")) +
                  (show_label_time ? std::string(" · label time shown") : std::string(" · label time hidden")),
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search sessions, labels, branches",
      .empty_text = named_only ? std::string("No named sessions match") : std::string("No sessions match"),
      .footer_hint = footer_hint.empty() ? std::string("Enter choose · PgUp/PgDn page · type to filter · Esc cancel") : std::move(footer_hint)};
  view.items.reserve(tree.sessions.size() + 1);

  append_session_tree_items(view, tree.sessions, index_by_id, tree.roots, tree.current_path, sort, 0, named_only, show_paths, show_archived, show_label_time);

  if (!named_only && view.items.empty() && !tree.current_session_id.empty())
  {
    view.items.push_back(tui::SelectListItemView{.value = tree.current_session_id,
                                                 .label = tree.current_session_id,
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
                                                 .label = named_only ? std::string("No named sessions found") : std::string("No sessions found"),
                                                 .description = named_only ? std::string("Use /name <name> to make a session appear in this filter")
                                                                           : std::string("Start a conversation to create a session"),
                                                 .group = "Sessions",
                                                 .detail = {},
                                                 .badge = {},
                                                 .current = false,
                                                 .enabled = false,
                                                 .disabled_reason = named_only ? std::string("no sessions have names") : std::string("session tree is empty")});
  }

  return view;
}

tui::SelectListView session_selector_view(runtime::Session const& session, SessionSelectorSort sort, std::string footer_hint, bool named_only, bool show_paths,
                                          bool show_archived, bool show_label_time)
{
  auto tree = ava::session::build_session_tree(session.workspace_dir, session.paths.sessions_dir, session.store.session_id());
  if (tree)
    return session_selector_view(std::move(*tree), sort, std::move(footer_hint), named_only, show_paths, show_archived, show_label_time);

  return tui::SelectListView{.title = "Select session",
                             .subtitle = "Unable to load session list",
                             .items = {tui::SelectListItemView{.value = {},
                                                               .label = "Session list unavailable",
                                                               .description = tree.error().format(),
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

std::optional<std::string> session_selector_parent_target(ava::session::SessionTreeIndex const& tree, std::string_view session_id)
{
  auto const index_by_id = session_tree_index_by_id(tree.sessions);
  auto const found = index_by_id.find(std::string(session_id));
  if (found == index_by_id.end())
    return std::nullopt;
  auto const& parent_id = tree.sessions[found->second].metadata.parent_session_id;
  if (parent_id.empty() || index_by_id.find(parent_id) == index_by_id.end())
    return std::nullopt;
  return parent_id;
}

std::optional<std::string> session_selector_child_target(ava::session::SessionTreeIndex const& tree, std::string_view session_id, SessionSelectorSort sort,
                                                         bool include_archived)
{
  auto const index_by_id = session_tree_index_by_id(tree.sessions);
  auto const found = index_by_id.find(std::string(session_id));
  if (found == index_by_id.end())
    return std::nullopt;

  auto children = sorted_tree_ids(tree.sessions[found->second].children, tree.sessions, index_by_id, sort);
  for (auto const& child_id : children)
  {
    auto const child = index_by_id.find(child_id);
    if (child == index_by_id.end())
      continue;
    if (include_archived || !tree.sessions[child->second].metadata.archived)
      return child_id;
  }
  return std::nullopt;
}

}  // namespace ava::app
