#pragma once

#include "ava/app/command_palette.h"
#include "ava/app/events.h"
#include "ava/app/project_trust.h"
#include "ava/app/runtime/Session.h"
#include "ava/agent/agent_loop.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/session/attachments.h"
#include "ava/permissions/permission_rules.h"
#include "ava/core/result.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace ava::app::line_shell_internal {

struct ShellState
{
 public:
  // Lifetime contract: the borrowed session must outlive each run loop invocation.
  runtime::Session& session;

  // Runtime sessions can contain provider credentials and must not be debug-printed.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct LineResult
{
 public:
  bool quit = false;
  bool session_tree_changed = false;
  bool ordinary_turn_committed = false;
  std::vector<std::string> output;
  std::vector<ava::agent::ToolTimelineEntry> tool_timeline;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

void append_status_line(std::string& target, std::string line);
[[nodiscard]] std::string git_branch_for_sidebar(std::filesystem::path const& workspace);
[[nodiscard]] std::vector<CommandHotkey> command_hotkeys_from_key_bindings(ava::tui::TuiKeyBindings const& key_bindings);
[[nodiscard]] std::string display_theme_status(std::string_view prefix);
[[nodiscard]] ava::tui::ProjectTrustSnapshot project_trust_snapshot(ProjectTrustState const& state);

[[nodiscard]] ava::core::Result<std::optional<std::string>> edit_text_with_external_editor(std::string_view initial_text);

[[nodiscard]] std::vector<ava::tui::ToolTimelineItem> tui_tool_timeline(std::vector<ava::agent::ToolTimelineEntry> const& entries);
[[nodiscard]] std::optional<std::string> token_status_for_session(runtime::Session const& session);
[[nodiscard]] std::optional<std::string> active_context_status_for_session(runtime::Session const& session);
[[nodiscard]] std::string session_selector_footer_hint(SessionSelectorSort sort, bool named_only, bool show_paths, bool show_archived, bool show_label_time);
[[nodiscard]] std::string scoped_model_selector_footer_hint();
[[nodiscard]] ava::core::Result<ava::tui::SelectListView> toggle_scoped_model(runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                              std::string_view value);
[[nodiscard]] ava::core::Result<ava::tui::SelectListView> enable_scoped_models(runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                               std::vector<std::string> targets);
[[nodiscard]] ava::core::Result<ava::tui::SelectListView> clear_scoped_models(runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                              std::vector<std::string> targets);
[[nodiscard]] ava::core::Result<ava::tui::SelectListView> toggle_scoped_model_provider(runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                                       std::string_view selected_value);
[[nodiscard]] ava::core::Result<ava::tui::SelectListView> reorder_scoped_model(runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                               std::string_view selected_value, bool up);
[[nodiscard]] ava::core::Result<std::string> save_scoped_model_cycle(runtime::Session& session);
[[nodiscard]] ava::core::Result<ava::tui::TuiRememberedPermissionRule> remember_permission_rule_for_prompt(runtime::Session const& session,
                                                                                                           ava::permissions::PermissionPrompt const& prompt,
                                                                                                           ava::permissions::PermissionAction action);

[[nodiscard]] bool workspace_catalog_changed(LineResult const& result);
[[nodiscard]] bool workspace_catalog_reload_requested(std::string_view submitted);
[[nodiscard]] bool is_display_settings_command(std::string_view line) noexcept;
void add_output(LineResult& result, std::string text);
[[nodiscard]] LineResult handle_line(ShellState& state, std::string const& line, ava::permissions::PermissionResolver permission_resolver = nullptr,
                                     ava::agent::QuestionResolver question_resolver = nullptr, std::vector<CommandHotkey> const& hotkeys = {},
                                     ava::event::RuntimeEventSink event_sink = nullptr, std::function<bool()> cancel_requested = nullptr,
                                     std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr,
                                     std::vector<ava::session::ImageAttachmentRef> image_attachments = {});

[[nodiscard]] int run_tui(ShellState state);

}  // namespace ava::app::line_shell_internal
