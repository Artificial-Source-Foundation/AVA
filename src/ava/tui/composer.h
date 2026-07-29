#pragma once

#include "ava/tui/terminal.h"
#include "ava/tui/terminal_image.h"
#include "ava/tui/text.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::permissions {
struct PermissionPrompt;
}  // namespace ava::permissions

namespace ava::tui {

struct TuiKeyBindings;

enum class ToolTimelineStatus
{
  Running,
  Success,
  Canceled,
  Error,
};

enum class ToolLifecycleState
{
  ProviderAnnounced,
  ArgumentsStreaming,
  ArgumentsComplete,
  ExecutionStarted,
  Progress,
  Complete,
  Canceled,
  Error,
};

struct ToolPermissionAuditItem
{
  std::string permission_request_id = {};
  std::string resolver_request_id = {};
  std::string decision = {};
  std::string operation = {};
  std::string tool_name = {};
  std::string risk = {};
  std::string reason = {};
  std::string target = {};
  std::string command = {};
  std::string resolution_reason = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ToolTimelineItem
{
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
  std::vector<std::string> permission_request_ids = {};
  std::vector<ToolPermissionAuditItem> permissions = {};
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

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TranscriptItem
{
  std::string label = {};
  std::string text = {};
  Text text_model = {};
  std::string meta = {};
  std::string thinking = {};
  Text thinking_model = {};
  std::optional<ToolTimelineItem> tool = std::nullopt;
  // Runtime event snapshots set this only when they can prove that one stable
  // stream item retains its complete previous source prefix.
  std::string stream_id = {};
  bool append_only_stream = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SidebarActivityItem
{
  std::string id = {};
  std::string label = {};
  std::string detail = {};
  ToolTimelineStatus status = ToolTimelineStatus::Running;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SidebarModifiedFile
{
  std::string path = {};
  std::optional<int> added = std::nullopt;
  std::optional<int> removed = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct QueuedMessageItem
{
  std::string id = {};
  std::string kind = {};
  std::string text = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PendingAttachmentItem
{
  std::string label = {};
  std::string detail = {};
  struct Preview
  {
    TerminalImageProtocol protocol = TerminalImageProtocol::None;
    std::shared_ptr<std::string const> base64_data = {};
    ImageDimensions dimensions = {};
    std::optional<std::size_t> image_id = std::nullopt;

    AVA_DEBUG_PRINT_MEMBERS_ON
  };
  std::optional<Preview> preview = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TerminalGraphicOverlay
{
  TerminalImageProtocol protocol = TerminalImageProtocol::None;
  std::size_t row = 0;
  std::size_t column = 0;
  std::size_t rows = 1;
  std::size_t columns = 1;
  std::optional<std::size_t> image_id = std::nullopt;
  std::string sequence = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ComposerFrame
{
  std::vector<std::string> lines;
  std::vector<TerminalGraphicOverlay> graphics;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SlashCommandArgumentCompletion
{
  std::string value = {};
  // Renderer-only human label. Selection and execution always retain value.
  std::string display_label = {};
  std::string description = {};
  std::string category = {};
  std::vector<std::string> required_previous_args = {};
  std::size_t argument_index = 0;
  bool append_space = true;
  bool enabled = true;
  std::string disabled_reason = "";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SidebarSnapshot
{
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

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SlashCommandItem
{
  std::string command;
  // Renderer-only human label for command argument completions.
  std::string display_label = {};
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

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct FileReferenceItem
{
  std::string value;
  std::string description;
  std::string category = "Files";
  bool directory = false;
  bool enabled = true;
  std::string disabled_reason = "";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ThemeOptionItem
{
  std::string name;
  std::string detail;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ProjectTrustSnapshot
{
  std::string decision;
  std::string project_resources;
  std::string workspace;
  std::string matched_path;
  std::string trust_file;
  std::size_t protected_resource_count = 0;
  std::string diagnostic = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

enum class PermissionPromptChoice
{
  Deny,
  Allow,
  AllowSession,
  DenyRemember,
  AllowRemember,
};

enum class PermissionPromptInputAction
{
  None,
  Redraw,
  ResolveAllow,
  ResolveDeny,
  ResolveAllowSession,
  ResolveAllowRemember,
  ResolveDenyRemember,
};

struct PermissionPromptInputResult
{
  PermissionPromptChoice selected_choice = PermissionPromptChoice::Deny;
  PermissionPromptInputAction action = PermissionPromptInputAction::None;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PermissionPromptRememberAvailability
{
  bool allow_remember_available = false;
  bool deny_remember_available = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PermissionPromptView
{
  std::string tool_name;
  std::string operation;
  std::string target;
  std::string command = {};
  std::string reason;
  std::string risk = {};
  std::string diff_preview = {};
  bool diff_truncated = false;
  bool allow_session_available = false;
  bool allow_remember_available = false;
  bool deny_remember_available = false;
  std::string recipe_display = {};
  std::string workspace_recipe_key = {};
  std::string effective_allowed_scopes = {};
  PermissionPromptChoice selected_choice = PermissionPromptChoice::Deny;
  std::string request_id = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct QuestionPromptOptionView
{
  std::string value;
  std::string label;
  bool selected = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

enum class QuestionPromptInputAction
{
  None,
  Redraw,
  Copy,
  Resolve,
  Cancel,
};

struct QuestionPromptInputResult
{
  std::size_t selected_option_index = 0;
  std::vector<QuestionPromptOptionView> options;
  std::string custom_text;
  std::string copy_text;
  QuestionPromptInputAction action = QuestionPromptInputAction::None;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct QuestionPromptView
{
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

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SelectListItemView
{
  std::string value;
  std::string label;
  std::string description;
  std::string group;
  std::string detail;
  std::string badge;
  bool current = false;
  bool enabled = true;
  std::string disabled_reason;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

enum class SelectListInputAction
{
  None,
  Redraw,
  Resolve,
  Cancel,
  CycleSort,
  ToggleNamedFilter,
  TogglePathDisplay,
  ToggleArchivedFilter,
  ToggleLabelTimestamp,
  Rename,
  Label,
  Archive,
  ArchiveNoninvasive,
  BranchParent,
  BranchChild,
  ModelsSave,
  ModelsEnableAll,
  ModelsClearAll,
  ModelsToggleProvider,
  ModelsReorderUp,
  ModelsReorderDown,
};

struct SelectListInputResult
{
  std::size_t selected_item_index = 0;
  std::string query;
  SelectListInputAction action = SelectListInputAction::None;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SelectListView
{
  std::string title;
  std::string subtitle;
  std::vector<SelectListItemView> items;
  std::size_t selected_item_index = 0;
  std::string query;
  std::string placeholder = "Search";
  std::string empty_text = "No matches";
  std::string footer_hint;
  // When set, RuntimeRenderer freezes the pre-modal transcript layout cache for draws
  // while this selector is shown, even if the modal canvas width differs.
  bool freeze_underlying_transcript_layout = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ActiveRunHint
{
  std::string submit_or_queue = {};
  std::string follow_up = {};
  std::string dequeue = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

enum class ToolPresentation
{
  Compact,
  Rich,
  Expanded,
};

[[nodiscard]] std::string_view to_string(ToolPresentation presentation) noexcept;

struct ComposerSnapshot
{
  std::string mode;
  std::string provider;
  std::string model;
  std::string session_id;
  std::string input;
  std::string status;
  // One-action presentation feedback from a successful reasoning cycle. It is
  // neither persistent runtime status nor session/transcript content.
  std::optional<std::string> reasoning_feedback = std::nullopt;
  bool processing = false;
  ActiveRunHint active_run_hint = {};
  std::size_t spinner_frame = 0;
  std::optional<std::string> token_status = std::nullopt;
  std::optional<std::string> active_context_status = std::nullopt;
  std::optional<std::string> reasoning_status = std::nullopt;
  std::optional<std::size_t> context_source_count = std::nullopt;
  std::vector<TranscriptItem> transcript;
  std::size_t transcript_generation = 0;
  std::vector<SlashCommandItem> slash_commands = {};
  std::vector<FileReferenceItem> file_references = {};
  std::size_t file_references_generation = 0;
  std::vector<ThemeOptionItem> custom_themes = {};
  std::optional<PermissionPromptView> permission_prompt = std::nullopt;
  std::optional<QuestionPromptView> question_prompt = std::nullopt;
  std::optional<SelectListView> select_list = std::nullopt;
  std::size_t selected_slash_command_index = 0;
  bool slash_palette_suppressed = false;
  bool path_completion_force_active = false;
  std::size_t transcript_scroll_offset = 0;
  std::size_t transcript_new_output_count = 0;
  std::size_t width = 80;
  std::size_t height = 24;
  std::size_t input_cursor = std::string::npos;
  std::size_t input_selection_start = std::string::npos;
  std::size_t input_selection_end = std::string::npos;
  std::optional<SidebarSnapshot> sidebar = std::nullopt;
  bool sidebar_drawer_visible = false;
  std::size_t sidebar_drawer_scroll_offset = 0;
  std::vector<QueuedMessageItem> queued_messages = {};
  std::vector<PendingAttachmentItem> pending_attachments = {};
  std::size_t draft_scroll_offset = 0;
  ToolPresentation tool_presentation = ToolPresentation::Rich;
  bool thinking_visible = true;
  std::optional<ProjectTrustSnapshot> project_trust = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ComposerCanvasLayout
{
  std::size_t content_width = 0;
  std::size_t left = 0;
  bool rail_visible = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ComposerCanvasLayout composer_canvas_layout(ComposerSnapshot const& snapshot);
[[nodiscard]] std::vector<SlashCommandItem> filter_slash_commands(std::string_view input, std::vector<SlashCommandItem> const& commands);
[[nodiscard]] std::vector<SlashCommandItem> filter_slash_commands(std::string_view input, std::size_t cursor, std::vector<SlashCommandItem> const& commands);
[[nodiscard]] bool slash_palette_visible(std::string_view input, std::vector<SlashCommandItem> const& commands);
[[nodiscard]] bool slash_palette_visible(std::string_view input, std::size_t cursor, std::vector<SlashCommandItem> const& commands);
[[nodiscard]] std::size_t clamp_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index);
[[nodiscard]] std::size_t clamp_slash_palette_selection(std::string_view input, std::size_t cursor, std::vector<SlashCommandItem> const& commands,
                                                        std::size_t selected_index);
[[nodiscard]] std::size_t previous_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index);
[[nodiscard]] std::size_t previous_slash_palette_selection(std::string_view input, std::size_t cursor, std::vector<SlashCommandItem> const& commands,
                                                           std::size_t selected_index);
[[nodiscard]] std::size_t next_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index);
[[nodiscard]] std::size_t next_slash_palette_selection(std::string_view input, std::size_t cursor, std::vector<SlashCommandItem> const& commands,
                                                       std::size_t selected_index);
struct SlashCommandSelectionText
{
  std::string text;
  std::size_t cursor = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};
[[nodiscard]] std::string slash_command_selection_text(std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index);
[[nodiscard]] SlashCommandSelectionText slash_command_selection_text(std::string_view input, std::size_t cursor, std::vector<SlashCommandItem> const& commands,
                                                                     std::size_t selected_index);
[[nodiscard]] std::optional<std::string> slash_command_selection_disabled_reason(std::string_view input, std::vector<SlashCommandItem> const& commands,
                                                                                 std::size_t selected_index);
[[nodiscard]] std::optional<std::string> slash_command_selection_disabled_reason(std::string_view input, std::size_t cursor,
                                                                                 std::vector<SlashCommandItem> const& commands, std::size_t selected_index);
[[nodiscard]] std::optional<std::size_t> slash_palette_selection_for_screen_position(ComposerSnapshot const& snapshot, std::size_t row, std::size_t column);
[[nodiscard]] std::vector<FileReferenceItem> filter_file_references(std::string_view input, std::size_t cursor,
                                                                    std::vector<FileReferenceItem> const& references);
[[nodiscard]] bool file_reference_palette_visible(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references);
[[nodiscard]] std::size_t clamp_file_reference_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                                         std::size_t selected_index);
[[nodiscard]] std::size_t previous_file_reference_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                                            std::size_t selected_index);
[[nodiscard]] std::size_t next_file_reference_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                                        std::size_t selected_index);
struct FileReferenceSelectionText
{
  std::string text;
  std::size_t cursor = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};
[[nodiscard]] FileReferenceSelectionText file_reference_selection_text(std::string_view input, std::size_t cursor,
                                                                       std::vector<FileReferenceItem> const& references, std::size_t selected_index);
[[nodiscard]] std::optional<std::string> file_reference_selection_disabled_reason(std::string_view input, std::size_t cursor,
                                                                                  std::vector<FileReferenceItem> const& references, std::size_t selected_index);
[[nodiscard]] std::optional<std::size_t> file_reference_palette_selection_for_screen_position(ComposerSnapshot const& snapshot, std::size_t row,
                                                                                              std::size_t column);
[[nodiscard]] std::vector<FileReferenceItem> filter_path_completions(std::string_view input, std::size_t cursor,
                                                                     std::vector<FileReferenceItem> const& references, bool force = false);
[[nodiscard]] bool path_completion_palette_visible(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                                   bool force = false);
[[nodiscard]] std::size_t clamp_path_completion_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                                          std::size_t selected_index, bool force = false);
[[nodiscard]] std::size_t previous_path_completion_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                                             std::size_t selected_index, bool force = false);
[[nodiscard]] std::size_t next_path_completion_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                                         std::size_t selected_index, bool force = false);
struct PathCompletionSelectionText
{
  std::string text;
  std::size_t cursor = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};
[[nodiscard]] PathCompletionSelectionText path_completion_selection_text(std::string_view input, std::size_t cursor,
                                                                         std::vector<FileReferenceItem> const& references, std::size_t selected_index,
                                                                         bool force = false);
[[nodiscard]] std::optional<std::string> path_completion_selection_disabled_reason(std::string_view input, std::size_t cursor,
                                                                                   std::vector<FileReferenceItem> const& references, std::size_t selected_index,
                                                                                   bool force = false);
[[nodiscard]] std::optional<std::size_t> path_completion_palette_selection_for_screen_position(ComposerSnapshot const& snapshot, std::size_t row,
                                                                                               std::size_t column);

struct ComposerPaletteScreenLayout
{
  std::size_t first_item_row = 0;
  std::size_t item_count = 0;
  std::size_t first_item_index = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};
[[nodiscard]] std::optional<ComposerPaletteScreenLayout> composer_palette_screen_layout(ComposerSnapshot const& snapshot);
[[nodiscard]] ComposerFrame render_composer_frame(ComposerSnapshot const& snapshot);
[[nodiscard]] std::vector<std::string> render_composer(ComposerSnapshot const& snapshot);
[[nodiscard]] std::size_t composer_main_width(ComposerSnapshot const& snapshot);
[[nodiscard]] std::size_t composer_max_transcript_scroll_offset(ComposerSnapshot const& snapshot, std::size_t width, std::size_t height);
[[nodiscard]] std::size_t sidebar_drawer_max_scroll_offset(ComposerSnapshot const& snapshot);
[[nodiscard]] bool draw_screen(ComposerSnapshot const& snapshot);
[[nodiscard]] std::string sanitize_terminal_text(std::string_view text);
[[nodiscard]] std::vector<std::string> split_lines(std::string_view text);
[[nodiscard]] PermissionPromptRememberAvailability permission_prompt_remember_availability(ava::permissions::PermissionPrompt const& prompt,
                                                                                           bool rule_storage_available) noexcept;
[[nodiscard]] PermissionPromptInputResult handle_permission_prompt_input(PermissionPromptChoice selected_choice, InputEvent event,
                                                                         bool allow_session_available = false, bool allow_remember_available = false,
                                                                         bool deny_remember_available = false);
[[nodiscard]] QuestionPromptInputResult handle_question_prompt_input(QuestionPromptView const& prompt, InputEvent event);
[[nodiscard]] QuestionPromptInputResult activate_question_option(QuestionPromptView const& prompt, std::size_t option_index);
[[nodiscard]] std::vector<std::size_t> filter_select_list_items(SelectListView const& view);
[[nodiscard]] std::size_t clamp_select_list_selection(SelectListView const& view, std::size_t selected_index);
[[nodiscard]] std::size_t previous_select_list_selection(SelectListView const& view, std::size_t selected_index);
[[nodiscard]] std::size_t next_select_list_selection(SelectListView const& view, std::size_t selected_index);
[[nodiscard]] std::optional<std::size_t> select_list_selection_for_screen_position(ComposerSnapshot const& snapshot, std::size_t row, std::size_t column);
[[nodiscard]] std::optional<std::size_t> question_option_for_screen_position(ComposerSnapshot const& snapshot, std::size_t row, std::size_t column);
[[nodiscard]] std::optional<std::size_t> composer_input_cursor_for_screen_position(ComposerSnapshot const& snapshot, std::size_t row, std::size_t column);
[[nodiscard]] SelectListInputResult handle_select_list_input(SelectListView const& view, InputEvent event);
[[nodiscard]] SelectListInputResult handle_select_list_input(SelectListView const& view, InputEvent event, TuiKeyBindings const& bindings);
[[nodiscard]] std::string to_string(ToolTimelineStatus status);
[[nodiscard]] std::string to_string(ToolLifecycleState state);

}  // namespace ava::tui
