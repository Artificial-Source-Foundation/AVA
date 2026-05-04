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
  std::string meta = {};
  std::optional<ToolTimelineItem> tool = std::nullopt;
};

struct SidebarActivityItem {
  std::string id = {};
  std::string label = {};
  std::string detail = {};
  ToolTimelineStatus status = ToolTimelineStatus::Running;
};

struct SidebarModifiedFile {
  std::string path = {};
  std::optional<int> added = std::nullopt;
  std::optional<int> removed = std::nullopt;
};

struct SidebarSnapshot {
  std::vector<SidebarActivityItem> activity = {};
  std::vector<SidebarModifiedFile> modified_files = {};
  std::string session_id = {};
  std::string mode = {};
  std::string provider = {};
  std::string model = {};
  std::string workspace = {};
  std::string git_branch = {};
  std::string version = {};
  std::optional<std::string> token_status = std::nullopt;
  std::optional<std::size_t> context_source_count = std::nullopt;
};

struct SlashCommandItem {
  std::string command;
  std::string description;
  std::string hint = "";
  std::string category = "";
  std::vector<std::string> aliases = {};
  std::string key_display = "";
  bool enabled = true;
  std::string disabled_reason = "";
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

struct QuestionPromptOptionView {
  std::string value;
  std::string label;
  bool selected = false;
};

enum class QuestionPromptInputAction {
  None,
  Redraw,
  Resolve,
  Cancel,
};

struct QuestionPromptInputResult {
  std::size_t selected_option_index = 0;
  std::vector<QuestionPromptOptionView> options;
  std::string custom_text;
  QuestionPromptInputAction action = QuestionPromptInputAction::None;
};

struct QuestionPromptView {
  std::string header;
  std::string question;
  std::vector<QuestionPromptOptionView> options;
  bool multiple = false;
  bool allow_custom = false;
  bool secret = false;
  bool modal = false;
  bool searchable = false;
  std::size_t selected_option_index = 0;
  std::string custom_text;
};

struct ComposerSnapshot {
  std::string mode;
  std::string provider;
  std::string model;
  std::string session_id;
  std::string input;
  std::string status;
  bool processing = false;
  std::size_t spinner_frame = 0;
  std::optional<std::string> token_status = std::nullopt;
  std::optional<std::string> reasoning_status = std::nullopt;
  std::vector<TranscriptItem> transcript;
  std::vector<SlashCommandItem> slash_commands = {};
  std::optional<PermissionPromptView> permission_prompt = std::nullopt;
  std::optional<QuestionPromptView> question_prompt = std::nullopt;
  std::size_t selected_slash_command_index = 0;
  bool slash_palette_suppressed = false;
  std::size_t transcript_scroll_offset = 0;
  std::size_t width = 80;
  std::size_t height = 24;
  std::size_t input_cursor = std::string::npos;
  std::optional<SidebarSnapshot> sidebar = std::nullopt;
  std::size_t draft_scroll_offset = 0;
  bool tool_details_visible = false;
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
[[nodiscard]] std::optional<std::string> slash_command_selection_disabled_reason(
    std::string_view input, const std::vector<SlashCommandItem>& commands, std::size_t selected_index);
[[nodiscard]] std::optional<std::size_t> slash_palette_selection_for_screen_row(const ComposerSnapshot& snapshot,
                                                                                std::size_t row);
[[nodiscard]] std::vector<std::string> render_composer(const ComposerSnapshot& snapshot);
[[nodiscard]] std::size_t composer_main_width(const ComposerSnapshot& snapshot);
[[nodiscard]] bool draw_screen(const ComposerSnapshot& snapshot);
[[nodiscard]] std::string sanitize_terminal_text(std::string_view text);
[[nodiscard]] std::vector<std::string> split_lines(std::string_view text);
[[nodiscard]] PermissionPromptInputResult handle_permission_prompt_input(PermissionPromptChoice selected_choice,
                                                                         InputEvent event);
[[nodiscard]] QuestionPromptInputResult handle_question_prompt_input(const QuestionPromptView& prompt,
                                                                     InputEvent event);
[[nodiscard]] std::string to_string(ToolTimelineStatus status);

}  // namespace ava::tui
