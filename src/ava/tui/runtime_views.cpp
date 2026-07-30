#include "sys.h"
#include "ava/agent/question.h"
#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_views_internal.h"
#include "ava/tui/terminal_image.h"
#include "ava/tui/theme.h"
#include "ava/permissions/permission.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::tui::runtime_views {
namespace {

std::vector<std::string> split_key_display(std::string_view keys)
{
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= keys.size())
  {
    auto const comma = keys.find(',', start);
    auto end = comma == std::string_view::npos ? keys.size() : comma;
    while (start < end && std::isspace(static_cast<unsigned char>(keys[start])) != 0) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(keys[end - 1])) != 0) --end;
    if (start < end)
      parts.emplace_back(keys.substr(start, end - start));
    if (comma == std::string_view::npos)
      break;
    start = comma + 1;
  }
  return parts;
}

std::string first_key_display(TuiKeyBindings const& bindings, TuiAction action)
{
  for (auto const& [configured_action, keys] : bindings.bindings)
  {
    if (configured_action == action && !keys.empty())
      return key_display(keys.front());
  }
  return {};
}

std::size_t shared_key_count(std::vector<TuiKeyBindingHelpItem> const& items, std::string_view key)
{
  return static_cast<std::size_t>(std::ranges::count_if(items, [&](TuiKeyBindingHelpItem const& item) {
    auto const keys = split_key_display(item.keys);
    return std::ranges::find(keys, key) != keys.end();
  }));
}

std::string value_or_unknown(std::string value)
{
  return value.empty() ? std::string("unknown") : std::move(value);
}

void add_settings_row(SelectListView& view, std::string group, std::string label, std::string description, std::string detail = {}, std::string badge = {})
{
  view.items.push_back(SelectListItemView{.value = group + ":" + label,
                                          .label = std::move(label),
                                          .description = std::move(description),
                                          .group = std::move(group),
                                          .detail = std::move(detail),
                                          .badge = std::move(badge),
                                          .current = false,
                                          .enabled = true,
                                          .disabled_reason = {}});
}

void add_settings_action_row(SelectListView& view, std::string value, std::string group, std::string label, std::string description, std::string detail = {},
                             std::string badge = {})
{
  view.items.push_back(SelectListItemView{.value = std::move(value),
                                          .label = std::move(label),
                                          .description = std::move(description),
                                          .group = std::move(group),
                                          .detail = std::move(detail),
                                          .badge = std::move(badge),
                                          .current = false,
                                          .enabled = true,
                                          .disabled_reason = {}});
}

void add_theme_settings_action(SelectListView& view, std::string value, std::string label, std::string description, bool current)
{
  view.items.push_back(SelectListItemView{.value = "theme:" + value,
                                          .label = std::move(label),
                                          .description = std::move(description),
                                          .group = "Display",
                                          .detail = "persist to display.json",
                                          .badge = current ? std::string("current") : std::string("select"),
                                          .current = current,
                                          .enabled = true,
                                          .disabled_reason = {}});
}

void add_custom_theme_settings_action(SelectListView& view, ThemeOptionItem const& theme, bool current)
{
  view.items.push_back(SelectListItemView{.value = "theme:" + theme.name,
                                          .label = "Theme " + theme.name,
                                          .description = "custom theme",
                                          .group = "Display",
                                          .detail = theme.detail.empty() ? std::string("persist to display.json") : theme.detail,
                                          .badge = current ? std::string("current") : std::string("select"),
                                          .current = current,
                                          .enabled = true,
                                          .disabled_reason = {}});
}

}  // namespace

ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_view(QuestionPromptView const& prompt)
{
  ava::agent::QuestionAnswer answer;
  for (auto const& option : prompt.options)
  {
    if (option.selected)
      answer.selected_options.push_back(option.value);
  }

  if (prompt.allow_custom && !prompt.custom_text.empty() && (!prompt.searchable || answer.selected_options.empty()))
  {
    answer.custom_text = prompt.custom_text;
  }

  if (!prompt.multiple && answer.selected_options.empty() && answer.custom_text.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt resolved without an answer"));
  }

  return answer;
}

std::string permission_prompt_status(bool allow_session_available, bool allow_remember_available, bool deny_remember_available)
{
  bool const remember_available = allow_remember_available || deny_remember_available;
  std::string status = "permission required: A=allow once";
  if (allow_session_available)
    status += " S=allow session";
  status += " D=reject G=guide rejection";
  if (remember_available)
    status += " R=remember";
  status += " Tab/Left/Right choose Enter confirm Esc reject";
  return status;
}

ActiveRunHint active_run_hint_for(TuiKeyBindings const& bindings)
{
  return ActiveRunHint{.submit_or_queue = first_key_display(bindings, TuiAction::Submit),
                       .follow_up = first_key_display(bindings, TuiAction::MessageFollowUp),
                       .dequeue = first_key_display(bindings, TuiAction::MessageDequeue),
                       .interrupt = first_key_display(bindings, TuiAction::Cancel),
                       .jump_to_bottom = first_key_display(bindings, TuiAction::JumpToBottom)};
}

std::string compact_path_leaf(std::string path)
{
  while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back();
  auto const slash = path.find_last_of("/\\");
  if (slash == std::string::npos)
    return path;
  if (slash + 1 >= path.size())
    return path;
  return path.substr(slash + 1);
}

PermissionPromptView permission_prompt_view(ava::permissions::PermissionPrompt const& prompt)
{
  PermissionPromptView view;
  view.tool_name = prompt.tool_name;
  view.operation = ava::permissions::to_string(prompt.operation);
  view.target = prompt.target_path.empty() ? std::string{} : prompt.target_path.generic_string();
  view.command = prompt.command;
  view.reason = prompt.reason;
  view.risk = ava::permissions::to_string(prompt.risk);
  view.request_id = prompt.permission_request_id;
  view.diff_preview = prompt.diff_preview;
  view.diff_truncated = prompt.diff_truncated;
  if (prompt.command_metadata)
  {
    view.recipe_display = prompt.command_metadata->recipe_display;
    view.workspace_recipe_key = prompt.command_metadata->workspace_recipe_key;
    for (std::size_t index = 0; index < prompt.command_metadata->effective_allowed_scopes.size(); ++index)
    {
      if (index > 0)
        view.effective_allowed_scopes += ", ";
      view.effective_allowed_scopes += ava::command::to_string(prompt.command_metadata->effective_allowed_scopes[index]);
    }
  }
  return view;
}

QuestionPromptView question_prompt_view(ava::agent::QuestionPrompt const& prompt)
{
  QuestionPromptView view;
  view.header = prompt.header;
  view.question = prompt.question;
  view.multiple = prompt.multiple;
  view.allow_custom = prompt.allow_custom;
  view.secret = prompt.secret;
  view.modal = prompt.modal;
  view.searchable = prompt.searchable;
  view.options.reserve(prompt.options.size());
  for (auto const& option : prompt.options)
  {
    view.options.push_back(QuestionPromptOptionView{.value = option.value, .label = option.label, .selected = false});
  }
  return view;
}

}  // namespace ava::tui::runtime_views

namespace ava::tui {
using runtime_views::add_custom_theme_settings_action;
using runtime_views::add_settings_action_row;
using runtime_views::add_settings_row;
using runtime_views::add_theme_settings_action;
using runtime_views::compact_path_leaf;
using runtime_views::kSettingsEditKeybindings;
using runtime_views::kSettingsOpenKeybindings;
using runtime_views::kSettingsOpenModels;
using runtime_views::kSettingsOpenScopedModels;
using runtime_views::kSettingsReloadKeybindings;
using runtime_views::kSettingsTrustClear;
using runtime_views::kSettingsTrustDeny;
using runtime_views::kSettingsTrustProject;
using runtime_views::kSettingsTrustStatus;
using runtime_views::kSettingsValidateKeybindings;
using runtime_views::question_answer_from_view;
using runtime_views::shared_key_count;
using runtime_views::split_key_display;
using runtime_views::value_or_unknown;

ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_prompt_view(QuestionPromptView const& prompt)
{
  return question_answer_from_view(prompt);
}

SelectListView hotkeys_select_list_view(TuiKeyBindings const& bindings, std::string footer_hint)
{
  auto const help_items = key_binding_help_items(bindings);
  SelectListView view{
      .title = "Keybindings",
      .subtitle =
          "Active TUI bindings · Enter drafts /keybindings set · config $XDG_CONFIG_HOME/ava/keybinds.json · init /keybindings init · reload /reload "
          "keybindings",
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search keybindings",
      .empty_text = "No keybindings match",
      .footer_hint = footer_hint.empty() ? std::string("Type to filter · PgUp/PgDn page · Enter draft edit · Esc close") : std::move(footer_hint)};
  view.items.reserve(help_items.size());
  for (auto const& item : help_items)
  {
    auto badge = std::string("active");
    for (auto const& key : split_key_display(item.keys))
    {
      if (shared_key_count(help_items, key) > 1)
      {
        badge = "shared key";
        break;
      }
    }
    auto const human_label = item.label.empty() ? item.action : item.label;
    // Keep the machine id in description so snake_case queries still match while the row leads with the human label.
    auto description = item.action;
    if (!item.description.empty())
    {
      if (!description.empty())
        description += " · ";
      description += item.description;
    }
    view.items.push_back(SelectListItemView{.value = item.action,
                                            .label = human_label,
                                            .description = std::move(description),
                                            .group = "Hotkeys",
                                            .detail = item.keys,
                                            .badge = std::move(badge),
                                            .current = false,
                                            .enabled = true,
                                            .disabled_reason = {}});
  }
  if (view.items.empty())
  {
    view.items.push_back(SelectListItemView{.value = {},
                                            .label = "No active hotkeys",
                                            .description = "Keybinding loader returned no displayable bindings",
                                            .group = "Hotkeys",
                                            .detail = {},
                                            .badge = {},
                                            .current = false,
                                            .enabled = false,
                                            .disabled_reason = "no bindings"});
  }
  return view;
}

SelectListView settings_select_list_view(ComposerSnapshot const& snapshot, std::string footer_hint)
{
  return settings_select_list_view(snapshot, default_key_bindings(), std::move(footer_hint));
}

SelectListView settings_select_list_view(ComposerSnapshot const& snapshot, TuiKeyBindings const& bindings, std::string footer_hint)
{
  SelectListView view{
      .title = "Settings",
      .subtitle = "Runtime view · Enter applies action rows · backend commands own config/session/provider changes",
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search settings",
      .empty_text = "No settings match",
      .footer_hint = footer_hint.empty() ? std::string("Type to filter · PgUp/PgDn page · Enter apply/open/draft · Esc close") : std::move(footer_hint)};
  view.items.reserve(25);

  auto const sidebar = snapshot.sidebar;
  auto const theme = active_tui_theme();
  add_settings_row(view, "Display", "Theme", theme.name, theme.detail, theme.badge);
  add_theme_settings_action(view, "dark", "Theme dark", "built-in dark ncurses token palette", theme.kind == TuiThemeKind::Dark);
  add_theme_settings_action(view, "light", "Theme light", "built-in light ncurses token palette", theme.kind == TuiThemeKind::Light);
  add_theme_settings_action(view, "plain", "Theme plain", "disable ANSI styling in rendered frames", theme.kind == TuiThemeKind::Plain);
  add_theme_settings_action(view, "reset", "Theme reset", "clear display.json theme and use built-in default", false);
  for (auto const& custom_theme : snapshot.custom_themes)
  {
    add_custom_theme_settings_action(view, custom_theme, theme.kind == TuiThemeKind::Custom && custom_theme.name == theme.name);
  }
  auto const image_capabilities = active_terminal_image_capabilities();
  add_settings_row(view, "Display", "Image preview", terminal_image_settings_description(image_capabilities),
                   terminal_image_settings_detail(image_capabilities), image_capabilities.badge);
  add_settings_row(view, "Display", "Tool details", std::string(to_string(snapshot.tool_presentation)),
                   "Ctrl+O or /details toggles rich/expanded; /details compact opts in");
  add_settings_row(view, "Display", "Thinking blocks", snapshot.thinking_visible ? "visible" : "hidden", "toggle with /thinking");

  auto const binding_count = key_binding_help_items(bindings).size();
  add_settings_action_row(view, std::string(kSettingsOpenKeybindings), "Input", "Keybindings",
                          std::to_string(binding_count) + (binding_count == 1 ? " active action" : " active actions"), "Enter opens active bindings", "open");
  add_settings_action_row(view, std::string(kSettingsValidateKeybindings), "Input", "Keybindings file", "$XDG_CONFIG_HOME/ava/keybinds.json",
                          "Enter validates with /keybindings validate", "validate");
  add_settings_action_row(view, std::string(kSettingsEditKeybindings), "Input", "Keybindings edit", "/keybindings set <action> <key>",
                          "Enter drafts the edit command; reset removes one override", "draft");
  add_settings_action_row(view, std::string(kSettingsReloadKeybindings), "Input", "Keybindings reload", "/reload keybindings",
                          "Enter applies valid keybinds.json edits", "live");

  add_settings_row(view, "Runtime", "Mode", value_or_unknown(snapshot.mode), "use /mode or Tab between turns");
  add_settings_row(view, "Runtime", "Model", value_or_unknown(snapshot.provider) + "/" + value_or_unknown(snapshot.model),
                   snapshot.reasoning_status.value_or("reasoning default"));
  add_settings_action_row(view, std::string(kSettingsOpenModels), "Runtime", "Model selector",
                          value_or_unknown(snapshot.provider) + "/" + value_or_unknown(snapshot.model), "Enter opens /models selector", "open");
  add_settings_action_row(view, std::string(kSettingsOpenScopedModels), "Runtime", "Model cycle scope", "Ctrl+P scoped cycle",
                          "Enter opens /scoped-models; Ctrl+S saves models.json", "open");
  add_settings_row(view, "Runtime", "Session", value_or_unknown(snapshot.session_id),
                   sidebar && !sidebar->session_path.empty() ? sidebar->session_path : std::string("path unavailable"));
  if (sidebar && sidebar->session_entry_count)
  {
    add_settings_row(view, "Runtime", "Session entries", std::to_string(*sidebar->session_entry_count));
  }
  add_settings_row(view, "Runtime", "Token status", snapshot.token_status.value_or("tokens unknown"));
  add_settings_row(view, "Runtime", "Context sources",
                   sidebar && sidebar->context_source_count ? std::to_string(*sidebar->context_source_count) : std::string("unknown"));

  add_settings_row(view, "Workspace", "Current directory", sidebar && !sidebar->workspace.empty() ? sidebar->workspace : std::string("unknown"),
                   sidebar && !sidebar->workspace.empty() ? compact_path_leaf(sidebar->workspace) : std::string{});
  add_settings_row(view, "Workspace", "Git branch", sidebar && !sidebar->git_branch.empty() ? sidebar->git_branch : std::string("not detected"));
  if (snapshot.project_trust)
  {
    auto const& trust = *snapshot.project_trust;
    auto const resources = trust.protected_resource_count == 1 ? std::string("1 protected project resource")
                                                               : std::to_string(trust.protected_resource_count) + " protected project resources";
    add_settings_row(view, "Workspace", "Project trust", value_or_unknown(trust.decision), "project resources " + value_or_unknown(trust.project_resources),
                     value_or_unknown(trust.decision));
    add_settings_row(view, "Workspace", "Protected resources", resources,
                     trust.matched_path.empty() ? std::string("no saved decision matched this workspace") : "matched " + trust.matched_path);
    if (!trust.diagnostic.empty())
      add_settings_row(view, "Workspace", "Trust diagnostic", trust.diagnostic);
    add_settings_action_row(view, std::string(kSettingsTrustStatus), "Workspace", "Trust status", "/trust status", "Enter prints project trust diagnostics",
                            "status");
    add_settings_action_row(view, std::string(kSettingsTrustProject), "Workspace", "Trust project", "allow this workspace's project resources",
                            "Enter runs /trust project", "trust");
    add_settings_action_row(view, std::string(kSettingsTrustDeny), "Workspace", "Deny project", "keep this workspace's project resources skipped",
                            "Enter runs /trust deny", "deny");
    add_settings_action_row(view, std::string(kSettingsTrustClear), "Workspace", "Clear trust decision", "remove the saved decision for this workspace",
                            "Enter runs /trust clear", "clear");
  }
  if (sidebar && !sidebar->version.empty())
  {
    add_settings_row(view, "About", "AVA version", sidebar->version);
  }

  return view;
}

}  // namespace ava::tui
