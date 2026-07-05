#include "sys.h"
#include "ava/app/command_catalog.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ava::app {
namespace {

bool token_matches(std::string_view token, CommandCatalogEntry const& entry) noexcept
{
  if (token == entry.command)
    return true;
  return std::ranges::find(entry.aliases, token) != entry.aliases.end();
}

std::string_view command_token(std::string_view line) noexcept
{
  auto const end = line.find_first_of(" \t\r\n");
  return line.substr(0, end == std::string_view::npos ? line.size() : end);
}

}  // namespace

std::vector<CommandCatalogEntry> const& command_catalog()
{
  static auto const catalog = std::vector<CommandCatalogEntry>{
      CommandCatalogEntry{.command = "/help", .description = "Show commands and effective hotkeys", .category = "General"},
      CommandCatalogEntry{.command = "/hotkeys",
                          .aliases = {"/keybindings"},
                          .description = "Show, initialize, import, edit, reset, or validate effective TUI keybindings",
                          .category = "General"},
      CommandCatalogEntry{.command = "/settings", .description = "Open TUI settings", .category = "General"},
      CommandCatalogEntry{.command = "/theme",
                          .description = "Persist the TUI display theme",
                          .hint = "[dark|light|plain|custom-name|reset]",
                          .category = "General"},
      CommandCatalogEntry{.command = "/mode", .description = "Toggle build/plan mode", .category = "General"},
      CommandCatalogEntry{.command = "/details", .description = "Toggle tool detail view", .category = "General"},
      CommandCatalogEntry{.command = "/tool",
                          .aliases = {"/tools"},
                          .description = "Show the latest or matching tool details in the TUI",
                          .hint = "[query]",
                          .category = "General"},
      CommandCatalogEntry{.command = "/diff",
                          .description = "Show the latest or matching tool diff in the TUI",
                          .hint = "[query]",
                          .category = "General"},
      CommandCatalogEntry{.command = "/copy",
                          .description = "Copy the latest AVA message, matching tool, diff, or permission details in the TUI",
                          .category = "General"},
      CommandCatalogEntry{.command = "/thinking", .description = "Toggle inline thinking block visibility", .category = "General"},
      CommandCatalogEntry{
          .command = "/attach", .aliases = {"/image"}, .description = "Attach an image to the next TUI prompt", .hint = "<path>", .category = "Files"},
      CommandCatalogEntry{
          .command = "/connect", .aliases = {"/login"}, .description = "Store provider credentials or start OpenAI OAuth", .category = "General"},
      CommandCatalogEntry{.command = "/quit", .aliases = {"/exit"}, .description = "Exit", .category = "General"},
      CommandCatalogEntry{
          .command = "/sessions",
          .aliases = {"/tree"},
          .description = "Show, rename, label, archive, or unarchive sessions in this workspace",
          .hint = "[--archived] [query|id] | rename <id> <name|--clear> | labels <id> <label...|--clear> | archive <id> --confirm | unarchive <id>",
          .category = "Sessions"},
      CommandCatalogEntry{.command = "/fork", .description = "Fork the current session at its latest entry", .hint = "[name]", .category = "Sessions"},
      CommandCatalogEntry{.command = "/clone", .description = "Clone the full current session", .hint = "[name]", .category = "Sessions"},
      CommandCatalogEntry{.command = "/new", .description = "Start a new session", .hint = "[name]", .category = "Sessions"},
      CommandCatalogEntry{.command = "/resume", .description = "Resume a session", .hint = "<id>", .category = "Sessions"},
      CommandCatalogEntry{.command = "/name", .aliases = {"/rename"}, .description = "Set or clear the current session name", .hint = "<name|--clear>", .category = "Sessions"},
      CommandCatalogEntry{
          .command = "/labels", .aliases = {"/label"}, .description = "Set or clear current session labels", .hint = "<label...|--clear>", .category = "Sessions"},
      CommandCatalogEntry{.command = "/context",
                          .description = "List prompt, context, command, skill, and plugin freshness",
                          .hint = "[query|source]",
                          .category = "Sessions"},
      CommandCatalogEntry{.command = "/stats", .description = "Show session counts, usage, and cost", .category = "Sessions"},
      CommandCatalogEntry{.command = "/status", .description = "Alias for /stats session status", .category = "Sessions"},
      CommandCatalogEntry{.command = "/compact", .description = "Generate and record a provider summary", .hint = "[instructions]", .category = "Sessions"},
      CommandCatalogEntry{.command = "/export",
                          .description = "Export this session as Markdown or HTML",
                          .hint = "[markdown|html] [path]",
                          .category = "Sessions"},
      CommandCatalogEntry{.command = "/permissions",
                          .aliases = {"/permission-rules", "/perms"},
                          .description = "List rules and audit permission decisions",
                          .hint = "<list|audit|diagnose|explain|add|remove> ...",
                          .category = "Safety"},
      CommandCatalogEntry{.command = "/glob", .description = "List files matching a glob pattern", .hint = "<pattern>", .category = "Files"},
      CommandCatalogEntry{.command = "/find", .description = "Pi-style alias for /glob", .hint = "<pattern>", .category = "Files"},
      CommandCatalogEntry{.command = "/ls", .description = "List directory entries through the permissioned list tool", .hint = "[path]", .category = "Files"},
      CommandCatalogEntry{.command = "/grep", .description = "Search matching files for literal text", .hint = "<text> [glob]", .category = "Files"},
      CommandCatalogEntry{.command = "/read", .description = "Read a file through the permissioned read tool", .hint = "<path>", .category = "Files"},
      CommandCatalogEntry{.command = "/write", .description = "Write text through the permissioned write tool", .hint = "<path> <txt>", .category = "Files"},
      CommandCatalogEntry{.command = "/bash", .description = "Run a permissioned shell command", .hint = "<command>", .category = "Shell"},
      CommandCatalogEntry{.command = "/models",
                          .aliases = {"/model"},
                          .description = "List configured models and capabilities",
                          .hint = "[query|provider/model]",
                          .category = "Models"},
      CommandCatalogEntry{.command = "/scoped-models",
                          .description = "Enable, disable, order, and save models for Ctrl+P cycling",
                          .category = "Models"},
      CommandCatalogEntry{.command = "/plugins",
                          .description = "List, inspect, enable, disable, and validate plugins",
                          .hint = "<list|inspect|enable|disable|validate|failures|prompts|prompt|skills|skill> ...",
                          .category = "Plugins"},
      CommandCatalogEntry{.command = "/trust",
                          .description = "Inspect or save project resource trust",
                          .hint = "[status|project|deny|clear]",
                          .category = "Plugins"},
      CommandCatalogEntry{.command = "/mcp",
                          .description = "List, inspect, discover, and restart MCP servers",
                          .hint = "<list|inspect|tools|restart> ...",
                          .category = "Plugins"},
      CommandCatalogEntry{
          .command = "/plugin", .description = "Run an enabled plugin command", .hint = "run <plugin_id> <command> [arguments_json]", .category = "Plugins"},
      CommandCatalogEntry{.command = "/import",
                          .description = "Import a session archive",
                          .hint = "<path>",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "session import is planned but is not implemented yet"},
      CommandCatalogEntry{.command = "/reload",
                          .description = "Reload keybindings or display theme in the running TUI",
                          .hint = "[keybindings|theme]",
                          .category = "General"},
      CommandCatalogEntry{.command = "/logout",
                          .description = "Clear provider login",
                          .category = "Planned",
                          .enabled = false,
                          .disabled_reason = "provider logout from the TUI is planned but unavailable"},
  };
  return catalog;
}

CommandCatalogEntry const* find_command_catalog_entry(std::string_view line) noexcept
{
  if (!line.starts_with('/'))
    return nullptr;
  auto const token = command_token(line);
  for (auto const& entry : command_catalog())
  {
    if (token_matches(token, entry))
      return &entry;
  }
  return nullptr;
}

std::string normalize_command_line(std::string_view line, CommandCatalogEntry const& entry)
{
  auto const token = command_token(line);
  if (token == entry.command)
    return std::string(line);
  auto const rest = line.substr(token.size());
  return entry.command + std::string(rest);
}

}  // namespace ava::app
