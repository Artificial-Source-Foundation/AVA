#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/tui/terminal.h"

namespace ava::tui {

enum class ToolTimelineStatus {
  Running,
  Success,
  Error,
};

struct ToolTimelineItem {
  ToolTimelineStatus status = ToolTimelineStatus::Running;
  std::string name = {};
  std::string argument_summary = {};
  std::string result_summary = {};
};

struct TranscriptItem {
  std::string label = {};
  std::string text = {};
  std::optional<ToolTimelineItem> tool = std::nullopt;
};

struct SlashCommandItem {
  std::string command;
  std::string description;
  std::string hint = "";
  std::string category = "";
};

enum class PermissionPromptChoice {
  Deny,
  Allow,
};

enum class PermissionPromptInputAction {
  None,
  Redraw,
  ResolveAllow,
  ResolveDeny,
};

struct PermissionPromptInputResult {
  PermissionPromptChoice selected_choice = PermissionPromptChoice::Deny;
  PermissionPromptInputAction action = PermissionPromptInputAction::None;
};

struct PermissionPromptView {
  std::string tool_name;
  std::string operation;
  std::string target;
  std::string command;
  std::string reason;
  PermissionPromptChoice selected_choice = PermissionPromptChoice::Deny;
};

struct ComposerSnapshot {
  std::string mode;
  std::string provider;
  std::string model;
  std::string session_id;
  std::string input;
  std::string status;
  std::vector<TranscriptItem> transcript;
  std::vector<SlashCommandItem> slash_commands = {};
  std::optional<PermissionPromptView> permission_prompt = std::nullopt;
  std::size_t selected_slash_command_index = 0;
  std::size_t transcript_scroll_offset = 0;
  std::size_t width = 80;
  std::size_t height = 24;
  std::size_t input_cursor = std::string::npos;
};

[[nodiscard]] std::vector<SlashCommandItem> filter_slash_commands(std::string_view input,
                                                                  const std::vector<SlashCommandItem>& commands);
[[nodiscard]] bool slash_palette_visible(std::string_view input, const std::vector<SlashCommandItem>& commands);
[[nodiscard]] std::size_t clamp_slash_palette_selection(std::string_view input,
                                                        const std::vector<SlashCommandItem>& commands,
                                                        std::size_t selected_index);
[[nodiscard]] std::size_t previous_slash_palette_selection(std::string_view input,
                                                           const std::vector<SlashCommandItem>& commands,
                                                           std::size_t selected_index);
[[nodiscard]] std::size_t next_slash_palette_selection(std::string_view input,
                                                       const std::vector<SlashCommandItem>& commands,
                                                       std::size_t selected_index);
[[nodiscard]] std::string slash_command_selection_text(std::string_view input,
                                                       const std::vector<SlashCommandItem>& commands,
                                                       std::size_t selected_index);
[[nodiscard]] std::optional<std::size_t> slash_palette_selection_for_screen_row(const ComposerSnapshot& snapshot,
                                                                                 std::size_t row);
[[nodiscard]] std::vector<std::string> render_composer(const ComposerSnapshot& snapshot);
[[nodiscard]] bool draw_screen(const ComposerSnapshot& snapshot);
[[nodiscard]] std::string sanitize_terminal_text(std::string_view text);
[[nodiscard]] std::vector<std::string> split_lines(std::string_view text);
[[nodiscard]] PermissionPromptInputResult handle_permission_prompt_input(PermissionPromptChoice selected_choice,
                                                                         InputEvent event);
[[nodiscard]] std::string to_string(ToolTimelineStatus status);

}  // namespace ava::tui
