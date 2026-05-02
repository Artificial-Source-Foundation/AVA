#include "ava/app/command_catalog.h"

#include <algorithm>

namespace ava::app {
namespace {

bool token_matches(std::string_view token, const CommandCatalogEntry& entry) noexcept {
  if (token == entry.command) return true;
  return std::ranges::find(entry.aliases, token) != entry.aliases.end();
}

std::string_view command_token(std::string_view line) noexcept {
  const auto end = line.find_first_of(" \t\r\n");
  return line.substr(0, end == std::string_view::npos ? line.size() : end);
}

std::string hotkeys_for_action(const std::vector<CommandHotkey>& hotkeys, std::string_view action) {
  for (const auto& hotkey : hotkeys) {
    if (hotkey.action == action) return hotkey.keys;
  }
  return "";
}

}  // namespace

const std::vector<CommandCatalogEntry>& command_catalog() {
  static const auto catalog = std::vector<CommandCatalogEntry>{
      CommandCatalogEntry{
          .command = "/help", .description = "Show commands and effective hotkeys", .category = "General"},
      CommandCatalogEntry{.command = "/hotkeys", .description = "Show effective TUI hotkeys", .category = "General"},
      CommandCatalogEntry{.command = "/mode", .description = "Toggle build/plan mode", .category = "General"},
      CommandCatalogEntry{.command = "/quit", .aliases = {"/exit"}, .description = "Exit", .category = "General"},
      CommandCatalogEntry{
          .command = "/sessions", .description = "List sessions for this workspace", .category = "Sessions"},
      CommandCatalogEntry{.command = "/context", .description = "List loaded context sources", .category = "Sessions"},
      CommandCatalogEntry{.command = "/compact",
                          .description = "Generate and record a provider summary",
                          .hint = "[instructions]",
                          .category = "Sessions"},
      CommandCatalogEntry{
          .command = "/export", .description = "Export this session as markdown", .category = "Sessions"},
      CommandCatalogEntry{.command = "/glob",
                          .description = "List files matching a glob pattern",
                          .hint = "<pattern>",
                          .category = "Files"},
      CommandCatalogEntry{.command = "/grep",
                          .description = "Search matching files for literal text",
                          .hint = "<text> [glob]",
                          .category = "Files"},
      CommandCatalogEntry{.command = "/read",
                          .description = "Read a file through the permissioned read tool",
                          .hint = "<path>",
                          .category = "Files"},
      CommandCatalogEntry{.command = "/write",
                          .description = "Write text through the permissioned write tool",
                          .hint = "<path> <txt>",
                          .category = "Files"},
      CommandCatalogEntry{.command = "/bash",
                          .description = "Run a permissioned shell command",
                          .hint = "<command>",
                          .category = "Shell"},
      CommandCatalogEntry{.command = "/models",
                          .aliases = {"/model"},
                          .description = "Select or list configured models",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "model switching is planned but is not available in v1 yet"},
      CommandCatalogEntry{.command = "/import",
                          .description = "Import a session archive",
                          .hint = "<path>",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "session import is planned but is not implemented yet"},
      CommandCatalogEntry{.command = "/new",
                          .description = "Start a new session",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "new-session creation from this command is planned but unavailable"},
      CommandCatalogEntry{.command = "/resume",
                          .description = "Resume a session",
                          .hint = "<id>",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "session resume by slash command is planned but unavailable"},
      CommandCatalogEntry{.command = "/reload",
                          .description = "Reload context and configuration",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "reload support is planned but unavailable"},
      CommandCatalogEntry{.command = "/login",
                          .description = "Start provider login",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "provider login from the TUI is planned but unavailable"},
      CommandCatalogEntry{.command = "/logout",
                          .description = "Clear provider login",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "provider logout from the TUI is planned but unavailable"},
      CommandCatalogEntry{.command = "/stats",
                          .description = "Show session statistics",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "session stats are planned for the slash surface but unavailable"},
  };
  return catalog;
}

const CommandCatalogEntry* find_command_catalog_entry(std::string_view line) noexcept {
  if (!line.starts_with('/')) return nullptr;
  const auto token = command_token(line);
  for (const auto& entry : command_catalog()) {
    if (token_matches(token, entry)) return &entry;
  }
  return nullptr;
}

std::string normalize_command_line(std::string_view line, const CommandCatalogEntry& entry) {
  const auto token = command_token(line);
  if (token == entry.command) return std::string(line);
  const auto rest = line.substr(token.size());
  return entry.command + std::string(rest);
}

std::vector<tui::SlashCommandItem> command_catalog_slash_items(const std::vector<CommandHotkey>& hotkeys) {
  std::vector<tui::SlashCommandItem> items;
  items.reserve(command_catalog().size());
  for (const auto& entry : command_catalog()) {
    std::string key_display;
    if (entry.command == "/mode") key_display = hotkeys_for_action(hotkeys, "mode_toggle");
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

}  // namespace ava::app
