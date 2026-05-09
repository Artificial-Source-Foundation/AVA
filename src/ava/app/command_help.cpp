#include "ava/app/command_help.h"
#include "ava/tui/keybindings.h"

#include <algorithm>

namespace ava::app {
namespace {

std::vector<CommandHotkey> default_command_hotkeys()
{
  std::vector<CommandHotkey> hotkeys;
  for (auto const& item : ava::tui::key_binding_help_items(ava::tui::default_key_bindings()))
  {
    hotkeys.push_back(CommandHotkey{.action = item.action, .description = item.description, .keys = item.keys});
  }
  return hotkeys;
}

std::vector<CommandHotkey> effective_hotkeys(std::vector<CommandHotkey> const& hotkeys)
{
  return hotkeys.empty() ? default_command_hotkeys() : hotkeys;
}

std::string aliases_text(CommandCatalogEntry const& entry)
{
  std::string text;
  for (auto const& alias : entry.aliases)
  {
    if (!text.empty())
      text += ", ";
    text += alias;
  }
  return text;
}

std::string command_display(CommandCatalogEntry const& entry)
{
  auto text = entry.command;
  if (!entry.hint.empty())
    text += " " + entry.hint;
  auto const aliases = aliases_text(entry);
  if (!aliases.empty())
    text += " (alias: " + aliases + ")";
  return text;
}

std::string command_rows(bool enabled)
{
  std::size_t width = 0;
  std::vector<CommandCatalogEntry const*> entries;
  for (auto const& entry : command_catalog())
  {
    if (entry.enabled != enabled)
      continue;
    entries.push_back(&entry);
    width = std::max(width, command_display(entry).size());
  }

  std::string output;
  for (auto const* entry : entries)
  {
    auto display = command_display(*entry);
    output += "  " + display;
    if (display.size() < width)
      output += std::string(width - display.size(), ' ');
    output += "  " + entry->description;
    if (!entry->enabled && !entry->disabled_reason.empty())
      output += " — disabled: " + entry->disabled_reason;
    output += '\n';
  }
  return output;
}

}  // namespace

std::string command_hotkeys_text(std::vector<CommandHotkey> const& hotkeys)
{
  auto const items = effective_hotkeys(hotkeys);
  std::size_t action_width = 0;
  std::size_t keys_width = 0;
  for (auto const& item : items)
  {
    action_width = std::max(action_width, item.action.size());
    keys_width = std::max(keys_width, item.keys.size());
  }

  std::string output = "Hotkeys:\n";
  for (auto const& item : items)
  {
    output += "  " + item.action;
    if (item.action.size() < action_width)
      output += std::string(action_width - item.action.size(), ' ');
    output += "  " + item.keys;
    if (item.keys.size() < keys_width)
      output += std::string(keys_width - item.keys.size(), ' ');
    output += "  " + item.description + '\n';
  }
  return output;
}

std::string command_help_text(std::vector<CommandHotkey> const& hotkeys)
{
  std::string output = "Commands:\n";
  output += command_rows(true);
  output += "\nUnavailable commands:\n";
  output += command_rows(false);
  output += '\n';
  output += command_hotkeys_text(hotkeys);
  if (!output.empty() && output.back() == '\n')
    output.pop_back();
  return output;
}

}  // namespace ava::app
