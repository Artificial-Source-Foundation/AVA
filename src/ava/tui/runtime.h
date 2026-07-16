#pragma once

#include "ava/app/events.h"
#include "ava/agent/question.h"
#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"
#include "ava/session/attachments.h"
#include "ava/permissions/permission.h"
#include "ava/core/result.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui {

struct TuiSubmitResult
{
  bool quit = false;
  std::vector<std::string> output;
  std::vector<ToolTimelineItem> tool_timeline;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiQueuedFollowUp
{
  std::string request_id;
  std::string message;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiRestoredQueuedMessage
{
  std::string message;
  bool steering = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiRememberedPermissionRule
{
  std::string rule_id;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiActiveRunQueues
{
  std::string active_request_id;
  std::function<ava::core::VoidResult(std::string)> queue_steering;
  std::function<ava::core::VoidResult(std::string)> queue_follow_up;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages;
  std::function<ava::core::VoidResult(std::string_view)> skip_active_steering;
  std::function<std::optional<TuiQueuedFollowUp>()> take_next_follow_up;
  std::function<ava::core::VoidResult(TuiQueuedFollowUp const&)> mark_follow_up_started;
  std::function<ava::core::Result<TuiRestoredQueuedMessage>()> restore_latest;
  std::function<ava::core::VoidResult(bool)> finish;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiRuntimeStateSnapshot
{
  std::string mode;
  std::string provider;
  std::string model;
  std::string session_id;
  std::string session_path;
  std::string workspace;
  std::string git_branch;
  std::optional<std::size_t> context_source_count = std::nullopt;
  std::string status;
  std::vector<SlashCommandItem> slash_commands = {};
  std::vector<FileReferenceItem> file_references = {};
  std::vector<ThemeOptionItem> custom_themes = {};
  std::optional<ProjectTrustSnapshot> project_trust = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiKeyBindingReloadResult
{
  TuiKeyBindings key_bindings = default_key_bindings();
  TuiRuntimeStateSnapshot state;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiSubmitContext
{
  ava::permissions::PermissionResolver permission_resolver;
  ava::agent::QuestionResolver question_resolver;
  ava::app::runtime::EventSink event_sink;
  std::function<bool()> cancel_requested;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages;
  std::function<ava::core::VoidResult(std::string_view)> skip_active_steering;
  std::function<std::optional<TuiQueuedFollowUp>()> take_next_follow_up;
  std::function<ava::core::VoidResult(TuiQueuedFollowUp const&)> mark_follow_up_started;
  std::vector<ava::session::ImageAttachmentRef> image_attachments;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiRuntimeOptions
{
  std::string mode;
  std::string provider;
  std::string model;
  std::string session_id;
  std::string session_path;
  std::string workspace;
  std::string git_branch;
  std::string app_version;
  std::optional<std::size_t> context_source_count = std::nullopt;
  std::string initial_status = "";
  std::vector<TranscriptItem> initial_transcript = {};
  std::vector<SlashCommandItem> slash_commands = {};
  std::vector<FileReferenceItem> file_references = {};
  std::vector<ThemeOptionItem> custom_themes = {};
  std::optional<ProjectTrustSnapshot> project_trust = std::nullopt;
  TuiKeyBindings key_bindings = default_key_bindings();
  // Called on the TUI main thread at startup and after a submit worker completes; never from render/spinner loops.
  std::function<std::optional<std::string>()> token_status_provider;
  std::function<std::optional<std::string>()> reasoning_status_provider;
  std::function<TuiActiveRunQueues(ava::app::EventEnvelopeSink)> create_active_run_queues;
  std::function<TuiSubmitResult(std::string const&, TuiSubmitContext)> on_submit;
  std::function<ava::core::Result<ava::session::ImageAttachmentRef>(std::string const&)> on_attach_image;
  std::function<ava::core::Result<std::optional<ava::session::ImageAttachmentRef>>()> on_paste_clipboard_image;
  std::function<ava::core::Result<std::optional<std::string>>(std::string_view)> on_external_editor;
  std::function<ava::core::Result<ava::session::LoadedImageAttachment>(ava::session::ImageAttachmentRef const&)> on_load_image_attachment;
  std::function<ava::core::Result<std::string>()> on_toggle_mode;
  std::function<ava::core::Result<std::string>()> on_cycle_reasoning;
  std::function<ava::core::Result<TuiRuntimeStateSnapshot>(bool)> on_cycle_model;
  std::function<ava::core::Result<TuiKeyBindingReloadResult>()> on_reload_key_bindings;
  std::function<ava::core::Result<TuiRuntimeStateSnapshot>()> on_reload_display_settings;
  std::function<ava::core::Result<std::optional<TuiRuntimeStateSnapshot>>()> on_maybe_reload_display_settings;
  std::function<SelectListView()> model_selector_view;
  std::function<SelectListView()> scoped_model_selector_view;
  std::function<SelectListView()> session_selector_view;
  std::function<SelectListView()> on_session_selector_sort_cycle;
  std::function<SelectListView()> on_session_selector_named_filter_toggle;
  std::function<SelectListView()> on_session_selector_path_display_toggle;
  std::function<SelectListView()> on_session_selector_archived_filter_toggle;
  std::function<SelectListView()> on_session_selector_label_timestamp_toggle;
  std::function<ava::core::Result<SelectListView>(std::string_view)> on_session_selector_archive;
  std::function<ava::core::Result<SelectListView>(std::string_view)> on_session_selector_unarchive;
  std::function<ava::core::Result<TuiRuntimeStateSnapshot>(std::string_view)> on_session_selector_branch_parent;
  std::function<ava::core::Result<TuiRuntimeStateSnapshot>(std::string_view)> on_session_selector_branch_child;
  std::function<ava::core::Result<TuiRememberedPermissionRule>(ava::permissions::PermissionPrompt const&, ava::permissions::PermissionAction)>
      remember_permission_rule;
  std::function<ava::core::Result<TuiRuntimeStateSnapshot>(std::string_view)> on_settings_selected;
  std::function<ava::core::Result<TuiRuntimeStateSnapshot>(std::string_view)> on_model_selected;
  std::function<ava::core::Result<SelectListView>(SelectListView const&, std::string_view)> on_scoped_model_toggled;
  std::function<ava::core::Result<SelectListView>(SelectListView const&, std::vector<std::string>)> on_scoped_model_enable_all;
  std::function<ava::core::Result<SelectListView>(SelectListView const&, std::vector<std::string>)> on_scoped_model_clear_all;
  std::function<ava::core::Result<SelectListView>(SelectListView const&, std::string_view)> on_scoped_model_toggle_provider;
  std::function<ava::core::Result<SelectListView>(SelectListView const&, std::string_view, bool)> on_scoped_model_reorder;
  std::function<ava::core::Result<std::string>()> on_scoped_model_save;
  std::function<ava::core::Result<TuiRuntimeStateSnapshot>(std::string_view)> on_session_selected;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] int run_interactive_composer(TuiRuntimeOptions options);
[[nodiscard]] SelectListView hotkeys_select_list_view(TuiKeyBindings const& bindings, std::string footer_hint = {});
[[nodiscard]] SelectListView settings_select_list_view(ComposerSnapshot const& snapshot, TuiKeyBindings const& bindings, std::string footer_hint = {});
[[nodiscard]] SelectListView settings_select_list_view(ComposerSnapshot const& snapshot, std::string footer_hint = {});
[[nodiscard]] ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_prompt_view(QuestionPromptView const& prompt);

}  // namespace ava::tui
