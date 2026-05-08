#pragma once

#include "ava/tui/terminal.h"
#include "ava/tui/text.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui {

enum class ToolTimelineStatus {
  Running,
  Success,
  Error,
};

enum class ToolLifecycleState {
  ProviderAnnounced,
  ArgumentsStreaming,
  ArgumentsComplete,
  ExecutionStarted,
  Progress,
  Complete,
  Error,
};

struct ToolTimelineItem {
  ToolTimelineStatus status = ToolTimelineStatus::Running;
  std::string name = {};
  std::string argument_summary = {};
  std::string result_summary = {};
  std::string arguments_json = {};
  std::string result_json = {};
  std::string call_id = {};
  std::string request_id = {};
  std::string correlation_id = {};
  ToolLifecycleState lifecycle = ToolLifecycleState::ExecutionStarted;
  std::optional<bool> details_visible = std::nullopt;
  std::string diff = {};
  bool diff_truncated = false;
  std::vector<std::string> changed_paths = {};
  bool truncated = false;
  bool byte_limited = false;
  bool line_limited = false;
  std::optional<std::size_t> output_bytes = std::nullopt;
  std::optional<std::size_t> total_bytes = std::nullopt;
  std::optional<std::size_t> output_lines = std::nullopt;
  std::optional<std::size_t> total_lines = std::nullopt;
  std::optional<std::size_t> start_line = std::nullopt;
  std::optional<std::size_t> end_line = std::nullopt;
  std::optional<std::size_t> next_offset_line = std::nullopt;
  std::optional<std::size_t> omitted_bytes = std::nullopt;
  std::optional<std::size_t> omitted_lines = std::nullopt;
  std::optional<std::size_t> visible_matches = std::nullopt;
  std::optional<std::size_t> total_matches = std::nullopt;
  std::string spill_path = {};
  bool spill_truncated = false;
};

struct TranscriptItem {
  std::string label = {};
  std::string text = {};
  Text text_model = {};
  std::string meta = {};
  std::string thinking = {};
  Text thinking_model = {};
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

struct QueuedMessageItem {
  std::string id = {};
  std::string kind = {};
  std::string text = {};
};

struct SlashCommandArgumentCompletion {
  std::string value = {};
  std::string description = {};
  std::string category = {};
  std::vector<std::string> required_previous_args = {};
  std::size_t argument_index = 0;
  bool append_space = true;
  bool enabled = true;
  std::string disabled_reason = "";
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
  std::optional<std::string> reasoning_status = std::nullopt;
  std::optional<std::size_t> context_source_count = std::nullopt;
  std::string session_path = {};
  std::optional<std::size_t> session_entry_count = std::nullopt;
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
  std::vector<SlashCommandArgumentCompletion> argument_completions = {};
  bool argument_completion = false;
  std::string completion_insert_text = "";
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
  std::string diff_preview = {};
  bool diff_truncated = false;
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
  Copy,
  Resolve,
  Cancel,
};

struct QuestionPromptInputResult {
  std::size_t selected_option_index = 0;
  std::vector<QuestionPromptOptionView> options;
  std::string custom_text;
  std::string copy_text;
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

struct SelectListItemView {
  std::string value;
  std::string label;
  std::string description;
  std::string group;
  std::string detail;
  std::string badge;
  bool current = false;
  bool enabled = true;
  std::string disabled_reason;
};

enum class SelectListInputAction {
  None,
  Redraw,
  Resolve,
  Cancel,
};

struct SelectListInputResult {
  std::size_t selected_item_index = 0;
  std::string query;
  SelectListInputAction action = SelectListInputAction::None;
};

struct SelectListView {
  std::string title;
  std::string subtitle;
  std::vector<SelectListItemView> items;
  std::size_t selected_item_index = 0;
  std::string query;
  std::string placeholder = "Search";
  std::string empty_text = "No matches";
  std::string footer_hint;
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
  std::optional<SelectListView> select_list = std::nullopt;
  std::size_t selected_slash_command_index = 0;
  bool slash_palette_suppressed = false;
  std::size_t transcript_scroll_offset = 0;
  std::size_t width = 80;
  std::size_t height = 24;
  std::size_t input_cursor = std::string::npos;
  std::optional<SidebarSnapshot> sidebar = std::nullopt;
  std::vector<QueuedMessageItem> queued_messages = {};
  std::size_t draft_scroll_offset = 0;
  bool tool_details_visible = false;
  bool thinking_visible = true;
};

[[nodiscard]] std::vector<SlashCommandItem> filter_slash_commands(std::string_view input,
                                                                  std::vector<SlashCommandItem> const& commands);
[[nodiscard]] bool slash_palette_visible(std::string_view input, std::vector<SlashCommandItem> const& commands);
[[nodiscard]] std::size_t clamp_slash_palette_selection(std::string_view input,
                                                        std::vector<SlashCommandItem> const& commands,
                                                        std::size_t selected_index);
[[nodiscard]] std::size_t previous_slash_palette_selection(std::string_view input,
                                                           std::vector<SlashCommandItem> const& commands,
                                                           std::size_t selected_index);
[[nodiscard]] std::size_t next_slash_palette_selection(std::string_view input,
                                                       std::vector<SlashCommandItem> const& commands,
                                                       std::size_t selected_index);
[[nodiscard]] std::string slash_command_selection_text(std::string_view input,
                                                       std::vector<SlashCommandItem> const& commands,
                                                       std::size_t selected_index);
[[nodiscard]] std::optional<std::string> slash_command_selection_disabled_reason(
    std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index);
[[nodiscard]] std::optional<std::size_t> slash_palette_selection_for_screen_row(ComposerSnapshot const& snapshot,
                                                                                std::size_t row);
[[nodiscard]] std::vector<std::string> render_composer(ComposerSnapshot const& snapshot);
[[nodiscard]] std::size_t composer_main_width(ComposerSnapshot const& snapshot);
[[nodiscard]] bool draw_screen(ComposerSnapshot const& snapshot);
[[nodiscard]] std::string sanitize_terminal_text(std::string_view text);
[[nodiscard]] std::vector<std::string> split_lines(std::string_view text);
[[nodiscard]] PermissionPromptInputResult handle_permission_prompt_input(PermissionPromptChoice selected_choice,
                                                                         InputEvent event);
[[nodiscard]] QuestionPromptInputResult handle_question_prompt_input(QuestionPromptView const& prompt,
                                                                     InputEvent event);
[[nodiscard]] std::vector<std::size_t> filter_select_list_items(SelectListView const& view);
[[nodiscard]] std::size_t clamp_select_list_selection(SelectListView const& view, std::size_t selected_index);
[[nodiscard]] std::size_t previous_select_list_selection(SelectListView const& view, std::size_t selected_index);
[[nodiscard]] std::size_t next_select_list_selection(SelectListView const& view, std::size_t selected_index);
[[nodiscard]] SelectListInputResult handle_select_list_input(SelectListView const& view, InputEvent event);
[[nodiscard]] std::string to_string(ToolTimelineStatus status);
[[nodiscard]] std::string to_string(ToolLifecycleState state);

}  // namespace ava::tui
