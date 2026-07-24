#include "sys.h"
#include "tests/support/tui_test_support.h"

#include <utility>

namespace tui_test_support {

ScopedTerminalCapabilityProfile::ScopedTerminalCapabilityProfile(std::string term_program_value)
    : term("TERM", "xterm-256color"),
      term_program("TERM_PROGRAM", std::move(term_program_value)),
      terminal_emulator("TERMINAL_EMULATOR", ""),
      tmux("TMUX", ""),
      kitty_window_id("KITTY_WINDOW_ID", ""),
      ghostty_resources_dir("GHOSTTY_RESOURCES_DIR", ""),
      wezterm_pane("WEZTERM_PANE", ""),
      warp_session_id("WARP_SESSION_ID", ""),
      warp_terminal_session_uuid("WARP_TERMINAL_SESSION_UUID", ""),
      iterm_session_id("ITERM_SESSION_ID", ""),
      wt_session("WT_SESSION", ""),
      tmux_hyperlinks("AVA_TUI_TMUX_HYPERLINKS", "")
{
}

std::vector<ava::tui::SlashCommandItem> const& standard_slash_commands()
{
  static std::vector<ava::tui::SlashCommandItem> const commands = {
      ava::tui::SlashCommandItem{.command = "/help", .description = "Show help", .category = "General"},
      ava::tui::SlashCommandItem{.command = "/grep", .description = "Search files", .hint = "<text> [glob]", .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/glob", .description = "List matching files", .hint = "<pattern>", .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/quit", .description = "Exit", .category = "General"}};
  return commands;
}

std::vector<std::string> plain_lines(std::vector<std::string> lines)
{
  for (auto& line : lines) line = strip_sgr(line);
  return lines;
}

std::string join_plain_lines(std::vector<std::string> const& lines)
{
  std::string text;
  for (auto const& line : lines) text += line + '\n';
  return text;
}

std::string join_visible_lines(std::vector<std::string> const& lines)
{
  std::string text;
  for (auto const& line : lines) text += strip_sgr(line) + '\n';
  return text;
}

std::size_t count_occurrences(std::string_view text, std::string_view needle)
{
  std::size_t count = 0;
  for (auto offset = text.find(needle); offset != std::string_view::npos; offset = text.find(needle, offset + needle.size())) ++count;
  return count;
}

}  // namespace tui_test_support
