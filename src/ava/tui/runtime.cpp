#include "sys.h"
#include "ava/app/EventEnvelope.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/event_state.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_draft_internal.h"
#include "ava/tui/runtime_input_internal.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/runtime_state_internal.h"
#include "ava/tui/runtime_views_internal.h"
#include "ava/tui/session_grants.h"
#include "ava/tui/terminal.h"
#include "ava/tui/terminal_image.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <curses.h>

namespace ava::tui {
using runtime_commands::attach_command_argument;
using runtime_commands::copy_command_argument;
using runtime_commands::diff_command_argument;
using runtime_commands::exact_command;
using runtime_commands::is_compact_command;
using runtime_commands::parse_copy_target;
using runtime_commands::reload_command_argument;
using runtime_commands::reload_target_from_argument;
using runtime_commands::ReloadTarget;
using runtime_commands::session_switching_command;
using runtime_commands::shell_helper_submission;
using runtime_commands::should_echo_slash_command;
using runtime_commands::should_show_slash_command_output_as_status;
using runtime_commands::tool_command_argument;
using runtime_input::poll_curses_input;
using runtime_input::printable_jump_target;
using runtime_input::read_curses_input;
using runtime_input::read_curses_input_with_timeout;
using runtime_input::RuntimeInput;
using runtime_transcript::apply_assistant_turn_meta;
using runtime_transcript::assistant_meta_for_snapshot;
using runtime_transcript::base64_encode;
using runtime_transcript::capture_tool_detail_visibility;
using runtime_transcript::carry_tool_detail_visibility;
using runtime_transcript::copy_text_from_answer;
using runtime_transcript::copy_text_to_terminal_clipboard;
using runtime_transcript::diff_transcript_text;
using runtime_transcript::latest_ava_message_copy_text;
using runtime_transcript::latest_permission_copy_text;
using runtime_transcript::latest_tool_copy_text;
using runtime_transcript::latest_tool_diff_copy_text;
using runtime_transcript::push_history;
using runtime_transcript::push_transcript;
using runtime_transcript::question_answer_audit_detail;
using runtime_views::active_run_hint_for;
using runtime_views::compact_path_leaf;
using runtime_views::kSettingsEditKeybindings;
using runtime_views::kSettingsOpenKeybindings;
using runtime_views::kSettingsOpenModels;
using runtime_views::kSettingsOpenScopedModels;
using runtime_views::kSettingsReloadKeybindings;
using runtime_views::permission_prompt_status;
using runtime_views::permission_prompt_view;
using runtime_views::question_answer_from_view;
using runtime_views::question_prompt_view;

namespace {

constexpr std::size_t kKeyboardScrollRows = 3;
constexpr std::size_t kMouseWheelScrollRows = 1;
constexpr auto kIdleInputPollDelay = std::chrono::milliseconds(250);
constexpr auto kDisplayReloadPollInterval = std::chrono::milliseconds(500);
class ComposerTerminalGraphicsGuard
{
 public:
  ComposerTerminalGraphicsGuard() = default;
  ComposerTerminalGraphicsGuard(ComposerTerminalGraphicsGuard const&) = delete;
  ComposerTerminalGraphicsGuard& operator=(ComposerTerminalGraphicsGuard const&) = delete;
  ~ComposerTerminalGraphicsGuard() { detail::clear_composer_terminal_graphics(); }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

std::string attachment_detail(ava::session::ImageAttachmentRef const& attachment, TerminalImageCapabilities const& capabilities)
{
  return "(" + attachment.mime_type + ", " + std::to_string(attachment.byte_size) + " bytes) preview " + terminal_image_settings_description(capabilities);
}

std::optional<std::size_t> kitty_image_id_from_sha(std::string_view sha)
{
  std::size_t value = 0;
  std::size_t digits = 0;
  for (char const ch : sha)
  {
    auto const byte = static_cast<unsigned char>(ch);
    int nibble = -1;
    if (byte >= '0' && byte <= '9')
    {
      nibble = byte - '0';
    }
    else if (byte >= 'a' && byte <= 'f')
    {
      nibble = 10 + byte - 'a';
    }
    else if (byte >= 'A' && byte <= 'F')
    {
      nibble = 10 + byte - 'A';
    }
    else
    {
      break;
    }
    value = (value << 4U) | static_cast<std::size_t>(nibble);
    ++digits;
    if (digits == 8)
      break;
  }
  if (digits == 0)
    return std::nullopt;
  return value == 0 ? std::size_t{1} : value;
}

std::optional<PendingAttachmentItem::Preview> attachment_preview(
    ava::session::ImageAttachmentRef const& attachment, TerminalImageCapabilities const& capabilities,
    std::function<ava::core::Result<ava::session::LoadedImageAttachment>(ava::session::ImageAttachmentRef const&)> const& load_image_attachment)
{
  if (capabilities.images == TerminalImageProtocol::None || !load_image_attachment)
    return std::nullopt;
  auto loaded = load_image_attachment(attachment);
  if (!loaded)
    return std::nullopt;
  auto dimensions = image_dimensions_from_bytes(loaded->bytes, attachment.mime_type).value_or(ImageDimensions{.width_px = 800, .height_px = 600});
  return PendingAttachmentItem::Preview{.protocol = capabilities.images,
                                        .base64_data = std::make_shared<std::string const>(base64_encode(loaded->bytes)),
                                        .dimensions = dimensions,
                                        .image_id = kitty_image_id_from_sha(attachment.sha256)};
}

}  // namespace

void apply_reasoning_cycle_success(ComposerSnapshot& snapshot, std::string feedback)
{
  snapshot.status.clear();
  snapshot.reasoning_feedback = std::move(feedback);
}

void clear_reasoning_feedback_for_user_input(ComposerSnapshot& snapshot)
{
  snapshot.reasoning_feedback.reset();
}

int run_interactive_composer(TuiRuntimeOptions options)
{
  if (!terminal_is_tty())
  {
    std::cerr << "interactive TUI requires stdin and stdout to be terminals\n";
    return 1;
  }

  clear_terminal_signal();
  auto curses = CursesSession::enter();
  if (!curses)
  {
    std::cerr << curses.error().format() << '\n';
    return 1;
  }
  ComposerTerminalGraphicsGuard graphics_cleanup;

  RuntimePresentationState presentation_state(options);
  auto& snapshot = presentation_state.snapshot;
  auto& sidebar = presentation_state.sidebar;
  auto& pending_image_attachments = presentation_state.pending_image_attachments;
  auto& command_session_grants = presentation_state.command_session_grants;

  auto refresh_token_status = [&]() { presentation_state.refresh_token_status(options); };
  auto refresh_reasoning_status = [&]() { presentation_state.refresh_reasoning_status(options); };
  auto apply_runtime_state_snapshot = [&](TuiRuntimeStateSnapshot state) { presentation_state.apply_runtime_state_snapshot(options, std::move(state)); };
  refresh_token_status();
  refresh_reasoning_status();

  bool terminal_write_failed = false;
  RuntimeDraftState draft_state;
  auto& input_history = draft_state.input_history;
  auto& history_index = draft_state.history_index;
  auto& draft_input = draft_state.draft_input;
  auto& draft = draft_state.draft;
  auto& jump_mode = draft_state.jump_mode;
  auto& selected_slash_command_index = draft_state.selected_slash_command_index;
  auto& slash_palette_suppressed = draft_state.slash_palette_suppressed;
  auto& path_completion_force_active = draft_state.path_completion_force_active;
  auto& draft_scroll_offset = draft_state.draft_scroll_offset;
  auto& draft_selection_anchor = draft_state.draft_selection_anchor;
  auto& draft_selection_cursor = draft_state.draft_selection_cursor;
  auto& pending_escape_clear = draft_state.pending_escape_clear;
  RuntimeRenderer renderer(snapshot, sidebar, draft_state);
  auto& transcript_scroll_offset = renderer.transcript_scroll_offset;
  auto& detached_new_output_count = renderer.detached_new_output_count;
  auto& completion_cache = renderer.completion_cache;
  auto& transcript_layout_cache = renderer.transcript_layout_cache;
  auto& detached_sidebar_snapshot = renderer.detached_sidebar_snapshot;
  auto& ui_mutex = renderer.ui_mutex;
  ActiveSelectList active_select_list = ActiveSelectList::None;
  std::optional<PendingSessionArchiveAction> session_archive_confirmation;
  std::mutex prompt_request_mutex;
  std::deque<std::shared_ptr<PendingPermissionRequest>> pending_permission_requests;
  std::deque<std::shared_ptr<PendingQuestionRequest>> pending_question_requests;
  std::atomic_bool accept_prompt_requests{true};
  std::mutex prompt_audit_mutex;
  ava::app::runtime::EventSink prompt_audit_sink;

  auto emit_prompt_audit = [&](std::string status, std::string text, std::string permission_request_id = {}, std::string tool_name = {},
                               std::string reason = {}, std::string resolution_reason = {}) {
    ava::app::runtime::EventSink sink;
    {
      std::lock_guard<std::mutex> lock(prompt_audit_mutex);
      sink = prompt_audit_sink;
    }
    if (!sink)
      return;
    ava::app::runtime::Event event;
    event.type = ava::app::runtime::EventType::ProviderEvent;
    event.status = std::move(status);
    event.text = std::move(text);
    event.tool_name = std::move(tool_name);
    event.reason = std::move(reason);
    event.error_details = std::move(resolution_reason);
    if (!permission_request_id.empty())
      event.permission_request_ids.push_back(std::move(permission_request_id));
    static_cast<void>(sink(event));
  };

  auto render = [&]() -> bool { return renderer.render(); };

  auto next_display_reload_check = std::chrono::steady_clock::now() + kDisplayReloadPollInterval;
  std::optional<std::string> last_display_reload_error;
  auto maybe_reload_display_settings = [&]() -> bool {
    if (!options.on_maybe_reload_display_settings)
      return true;
    auto const now = std::chrono::steady_clock::now();
    if (now < next_display_reload_check)
      return true;
    next_display_reload_check = now + kDisplayReloadPollInterval;

    auto reloaded = options.on_maybe_reload_display_settings();
    if (!reloaded)
    {
      auto status = reloaded.error().format();
      if (last_display_reload_error && *last_display_reload_error == status)
        return true;
      last_display_reload_error = status;
      static_cast<void>(beep());
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.status = std::move(status);
      }
      return render();
    }
    last_display_reload_error.reset();
    if (!*reloaded)
      return true;

    auto state = std::move(**reloaded);
    auto status = state.status.empty() ? std::string("display theme auto-reloaded") : state.status;
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      apply_runtime_state_snapshot(std::move(state));
      if (!snapshot.processing && !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list)
      {
        push_transcript(snapshot, TranscriptItem{.label = "ava", .text = status});
        transcript_scroll_offset = 0;
      }
      snapshot.status = std::move(status);
    }
    return render();
  };

  auto resolve_permission_prompt = [&](ava::permissions::PermissionPrompt const& prompt, std::function<bool()> const& stop_requested = {},
                                       std::function<bool()> const& request_stop = {}) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    auto permission_label = std::string("permission requested");
    if (!prompt.tool_name.empty())
      permission_label += ": " + prompt.tool_name;
    if (!prompt.command.empty())
      permission_label += " " + prompt.command;
    if (prompt.target_path.has_filename())
      permission_label += " " + prompt.target_path.generic_string();
    emit_prompt_audit("tui:permission_request", std::move(permission_label), prompt.permission_request_id, prompt.tool_name, prompt.reason);
    // A durable Deny never grants execution authority, so preserve it even
    // when one-shot Critical/unverified commands cannot be remembered as Allows.
    auto const remember_availability = permission_prompt_remember_availability(prompt, static_cast<bool>(options.remember_permission_rule));
    auto const allow_remember_available = remember_availability.allow_remember_available;
    auto const deny_remember_available = remember_availability.deny_remember_available;
    bool const allow_session_available = tui_session_grant_eligible(prompt);
    if (allow_session_available && command_session_grants.matches(snapshot.session_id, prompt))
    {
      emit_prompt_audit("tui:permission_allow", "permission allowed for this session: " + prompt.tool_name, prompt.permission_request_id, prompt.tool_name,
                        prompt.reason, "reused tui session grant");
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.status = "permission allowed for this session";
      }
      static_cast<void>(render());
      ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::AllowSessionGrant};
      decision.resolution_source = "tui_session_grant";
      return decision;
    }
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      snapshot.permission_prompt = permission_prompt_view(prompt);
      snapshot.permission_prompt->selected_choice = PermissionPromptChoice::Deny;
      snapshot.permission_prompt->allow_session_available = allow_session_available;
      snapshot.permission_prompt->allow_remember_available = allow_remember_available;
      snapshot.permission_prompt->deny_remember_available = deny_remember_available;
      snapshot.status = allow_session_available || allow_remember_available || deny_remember_available
                            ? permission_prompt_status(allow_session_available, allow_remember_available, deny_remember_available)
                            : "permission required: A=allow D=reject Tab/Left/Right choose Enter/Space confirm Esc reject";
    }
    static_cast<void>(beep());
    if (!render())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
    }

    auto resolve_choice = [&](PermissionPromptChoice selected) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
      auto const allow =
          selected == PermissionPromptChoice::Allow || selected == PermissionPromptChoice::AllowSession || selected == PermissionPromptChoice::AllowRemember;
      auto const remember = selected == PermissionPromptChoice::AllowRemember || selected == PermissionPromptChoice::DenyRemember;
      std::string remembered_rule_id;
      if (remember)
      {
        if (!options.remember_permission_rule || (allow ? !allow_remember_available : !deny_remember_available))
        {
          emit_prompt_audit("tui:permission_deny", "permission denied: remember unavailable", prompt.permission_request_id, prompt.tool_name, prompt.reason,
                            "remember unavailable");
          {
            std::lock_guard<std::recursive_mutex> lock(ui_mutex);
            snapshot.permission_prompt.reset();
            snapshot.status = "permission rule unavailable; denied";
          }
          if (!render())
          {
            return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
          }
          ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny, "permission rule storage is unavailable"};
          decision.resolution_source = "tui_remember_unavailable";
          return decision;
        }
        auto remembered =
            options.remember_permission_rule(prompt, allow ? ava::permissions::PermissionAction::Allow : ava::permissions::PermissionAction::Deny);
        if (!remembered)
        {
          auto reason = "failed to remember permission rule: " + remembered.error().format();
          emit_prompt_audit("tui:permission_deny", "permission denied: " + prompt.tool_name, prompt.permission_request_id, prompt.tool_name, prompt.reason,
                            reason);
          {
            std::lock_guard<std::recursive_mutex> lock(ui_mutex);
            snapshot.permission_prompt.reset();
            snapshot.status = "permission rule failed; denied";
          }
          if (!render())
          {
            return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
          }
          ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny, std::move(reason)};
          decision.resolution_source = "tui_remember_failed";
          return decision;
        }
        remembered_rule_id = remembered->rule_id;
      }

      if (selected == PermissionPromptChoice::AllowSession)
      {
        auto const grant_result = command_session_grants.add(snapshot.session_id, prompt);
        if (grant_result == TuiSessionGrantInsertResult::Ineligible || grant_result == TuiSessionGrantInsertResult::Full)
        {
          bool const cap_reached = grant_result == TuiSessionGrantInsertResult::Full;
          emit_prompt_audit("tui:permission_deny", "permission denied: session grant unavailable", prompt.permission_request_id, prompt.tool_name,
                            prompt.reason, cap_reached ? "session grant cap reached" : "session grant no longer eligible");
          {
            std::lock_guard<std::recursive_mutex> lock(ui_mutex);
            snapshot.permission_prompt.reset();
            snapshot.status = cap_reached ? "permission session grant cap reached; denied" : "permission session grant no longer eligible; denied";
          }
          if (!render())
          {
            return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
          }
          ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny};
          decision.resolution_source = cap_reached ? "tui_session_grant_cap_reached" : "tui_session_grant_ineligible";
          return decision;
        }
        emit_prompt_audit("tui:permission_allow", "permission allowed for this session: " + prompt.tool_name, prompt.permission_request_id, prompt.tool_name,
                          prompt.reason, grant_result == TuiSessionGrantInsertResult::Added ? "selected allow session" : "reused tui session grant");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.permission_prompt.reset();
          snapshot.status = "permission allowed for this session";
        }
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
        }
        ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::AllowSessionGrant};
        decision.resolution_source = "tui_session_grant";
        return decision;
      }

      if (allow)
      {
        emit_prompt_audit("tui:permission_allow", "permission allowed: " + prompt.tool_name, prompt.permission_request_id, prompt.tool_name, prompt.reason,
                          remember ? "selected allow and remember" : "selected allow");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.permission_prompt.reset();
          snapshot.status = remember ? "permission allow rule saved" : "permission allowed once";
        }
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
        }
        ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Allow};
        if (remember)
        {
          decision.reason = "remembered allow rule";
          decision.resolution_source = "persistent_rule_added";
          decision.rule_id = std::move(remembered_rule_id);
        }
        return decision;
      }
      emit_prompt_audit("tui:permission_deny", "permission denied: " + prompt.tool_name, prompt.permission_request_id, prompt.tool_name, prompt.reason,
                        remember ? "selected deny and remember" : "selected deny");
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.permission_prompt.reset();
        snapshot.status = remember ? "permission deny rule saved" : "permission denied";
      }
      if (!render())
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
      }
      ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny};
      if (remember)
      {
        decision.reason = "remembered deny rule";
        decision.resolution_source = "persistent_rule_added";
        decision.rule_id = std::move(remembered_rule_id);
      }
      return decision;
    };

    while (true)
    {
      auto const choice_input = read_curses_input();
      if (stop_requested && stop_requested())
      {
        return resolve_choice(PermissionPromptChoice::Deny);
      }
      if (terminal_signal_received())
      {
        emit_prompt_audit("tui:permission_deny", "permission denied: interrupted", prompt.permission_request_id, prompt.tool_name, prompt.reason,
                          "interrupted");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.permission_prompt.reset();
          snapshot.status = "interrupted";
        }
        static_cast<void>(render());
        return ava::permissions::PermissionResolution::Deny;
      }
      if (choice_input.resize)
      {
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
        }
        continue;
      }

      if (choice_input.event.key == Key::Escape && request_stop)
      {
        static_cast<void>(request_stop());
        return resolve_choice(PermissionPromptChoice::Deny);
      }

      auto input_result = snapshot.permission_prompt
                              ? handle_permission_prompt_input(
                                    snapshot.permission_prompt->selected_choice, choice_input.event, snapshot.permission_prompt->allow_session_available,
                                    snapshot.permission_prompt->allow_remember_available, snapshot.permission_prompt->deny_remember_available)
                              : PermissionPromptInputResult{};
      if (input_result.action == PermissionPromptInputAction::ResolveAllow)
      {
        return resolve_choice(PermissionPromptChoice::Allow);
      }
      if (input_result.action == PermissionPromptInputAction::ResolveAllowSession)
      {
        return resolve_choice(PermissionPromptChoice::AllowSession);
      }
      if (input_result.action == PermissionPromptInputAction::ResolveDeny)
      {
        return resolve_choice(PermissionPromptChoice::Deny);
      }
      if (input_result.action == PermissionPromptInputAction::ResolveAllowRemember)
      {
        return resolve_choice(PermissionPromptChoice::AllowRemember);
      }
      if (input_result.action == PermissionPromptInputAction::ResolveDenyRemember)
      {
        return resolve_choice(PermissionPromptChoice::DenyRemember);
      }
      if (input_result.action == PermissionPromptInputAction::Redraw && snapshot.permission_prompt)
      {
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          if (snapshot.permission_prompt)
            snapshot.permission_prompt->selected_choice = input_result.selected_choice;
        }
        static_cast<void>(render());
        continue;
      }

      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        bool const has_extended =
            snapshot.permission_prompt && (snapshot.permission_prompt->allow_session_available || snapshot.permission_prompt->allow_remember_available ||
                                           snapshot.permission_prompt->deny_remember_available);
        snapshot.status =
            has_extended ? permission_prompt_status(snapshot.permission_prompt->allow_session_available, snapshot.permission_prompt->allow_remember_available,
                                                    snapshot.permission_prompt->deny_remember_available)
                         : "permission required: A=allow D=reject Tab/Left/Right choose Enter/Space confirm Esc reject";
      }
      if (!render())
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
      }
    }
  };

  auto resolve_question_prompt = [&](ava::agent::QuestionPrompt const& prompt, std::function<bool()> const& stop_requested = {},
                                     std::function<bool()> const& request_stop = {}) -> ava::core::Result<ava::agent::QuestionAnswer> {
    static_cast<void>(request_stop);
    emit_prompt_audit("tui:question_request", prompt.question.empty() ? std::string("question requested") : "question requested: " + prompt.question);
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      snapshot.question_prompt = question_prompt_view(prompt);
      snapshot.status =
          prompt.multiple ? "question required: Space toggles, Enter sends, Esc cancels" : "question required: Enter sends, numbers choose, Esc cancels";
    }
    static_cast<void>(beep());
    if (!render())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
    }

    auto cancel_question = [&]() -> ava::core::Result<ava::agent::QuestionAnswer> {
      emit_prompt_audit("tui:question_cancel", prompt.question.empty() ? std::string("question canceled") : "question canceled: " + prompt.question);
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.question_prompt.reset();
        snapshot.status = "question canceled";
      }
      static_cast<void>(render());
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt canceled"));
    };

    auto auto_resolve_question = [&]() -> std::optional<ava::core::Result<ava::agent::QuestionAnswer>> {
      if (!prompt.auto_resolve || !prompt.auto_resolve())
        return std::nullopt;
      ava::agent::QuestionAnswer answer{.selected_options = {"done"}, .custom_text = ""};
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.question_prompt.reset();
        snapshot.status = "question answered";
      }
      if (!render())
      {
        return ava::core::Result<ava::agent::QuestionAnswer>{
            std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear question prompt"))};
      }
      emit_prompt_audit("tui:question_answer", "auto");
      return ava::core::Result<ava::agent::QuestionAnswer>{std::move(answer)};
    };

    auto read_question_input = [&]() {
      if (!prompt.auto_resolve)
        return read_curses_input();
      static_cast<void>(wtimeout(stdscr, 100));
      auto input = read_curses_input();
      static_cast<void>(wtimeout(stdscr, -1));
      return input;
    };

    auto no_input = [](RuntimeInput const& input) { return !input.resize && input.event.key == Key::Unknown && input.text.empty() && !input.bracketed_paste; };

    while (true)
    {
      if (auto answer = auto_resolve_question())
        return std::move(*answer);
      auto const question_input = read_question_input();
      if (auto answer = auto_resolve_question())
        return std::move(*answer);
      if (stop_requested && stop_requested())
        return cancel_question();
      if (terminal_signal_received())
      {
        emit_prompt_audit("tui:question_cancel", "question canceled: interrupted");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.question_prompt.reset();
          snapshot.status = "interrupted";
        }
        static_cast<void>(render());
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
      }
      if (question_input.resize)
      {
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }
      if (prompt.auto_resolve && no_input(question_input))
        continue;

      auto input_result = [&]() {
        if (!snapshot.question_prompt)
          return QuestionPromptInputResult{};
        if (question_input.event.key == Key::MouseLeftClick)
        {
          if (auto const clicked = question_option_for_screen_position(snapshot, question_input.event.mouse_row, question_input.event.mouse_column))
            return activate_question_option(*snapshot.question_prompt, *clicked);
        }
        return handle_question_prompt_input(*snapshot.question_prompt, question_input.event);
      }();
      if (input_result.action == QuestionPromptInputAction::Cancel)
        return cancel_question();

      if (snapshot.question_prompt && (input_result.action == QuestionPromptInputAction::Redraw || input_result.action == QuestionPromptInputAction::Copy ||
                                       input_result.action == QuestionPromptInputAction::Resolve))
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        if (snapshot.question_prompt)
        {
          snapshot.question_prompt->selected_option_index = input_result.selected_option_index;
          snapshot.question_prompt->options = std::move(input_result.options);
          snapshot.question_prompt->custom_text = std::move(input_result.custom_text);
        }
      }

      if (input_result.action == QuestionPromptInputAction::Copy)
      {
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.status = copy_text_to_terminal_clipboard(input_result.copy_text) ? "copied to clipboard" : "clipboard copy failed";
        }
        emit_prompt_audit("tui:question_copy", "question copy");
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }

      if (input_result.action == QuestionPromptInputAction::Resolve)
      {
        auto answer =
            ava::core::Result<ava::agent::QuestionAnswer>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt was dismissed"))};
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          if (snapshot.question_prompt)
            answer = question_answer_from_view(*snapshot.question_prompt);
          snapshot.question_prompt.reset();
          snapshot.status = answer ? "question answered" : "question canceled";
        }
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear question prompt"));
        }
        if (answer)
        {
          if (auto copy_text = copy_text_from_answer(*answer))
          {
            {
              std::lock_guard<std::recursive_mutex> lock(ui_mutex);
              snapshot.status = copy_text_to_terminal_clipboard(*copy_text) ? "copied to clipboard" : "clipboard copy failed";
            }
            static_cast<void>(render());
          }
          emit_prompt_audit("tui:question_answer", question_answer_audit_detail(*answer));
        }
        else
        {
          emit_prompt_audit("tui:question_cancel", "question canceled");
        }
        return answer;
      }

      if (input_result.action == QuestionPromptInputAction::Redraw)
      {
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }

      if (!render())
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
      }
    }
  };

  auto complete_permission_request = [](std::shared_ptr<PendingPermissionRequest> const& request,
                                        ava::core::Result<ava::permissions::PermissionResolutionDecision> result) {
    {
      std::lock_guard<std::mutex> lock(request->mutex);
      request->result = std::move(result);
    }
    request->ready.notify_one();
  };

  auto complete_question_request = [](std::shared_ptr<PendingQuestionRequest> const& request, ava::core::Result<ava::agent::QuestionAnswer> result) {
    {
      std::lock_guard<std::mutex> lock(request->mutex);
      request->result = std::move(result);
    }
    request->ready.notify_one();
  };

  auto fail_pending_prompt_requests = [&]() {
    accept_prompt_requests.store(false);
    std::deque<std::shared_ptr<PendingPermissionRequest>> permission_requests;
    std::deque<std::shared_ptr<PendingQuestionRequest>> question_requests;
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      permission_requests = std::move(pending_permission_requests);
      question_requests = std::move(pending_question_requests);
      pending_permission_requests.clear();
      pending_question_requests.clear();
    }
    for (auto const& permission_request : permission_requests)
    {
      complete_permission_request(permission_request, ava::permissions::PermissionResolution::Deny);
    }
    for (auto const& question_request : question_requests)
    {
      complete_question_request(question_request, std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted")));
    }
  };

  auto service_pending_prompt_request = [&](std::function<bool()> const& stop_requested = {}, std::function<bool()> const& request_stop = {}) -> bool {
    std::shared_ptr<PendingPermissionRequest> permission_request;
    std::shared_ptr<PendingQuestionRequest> question_request;
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!pending_permission_requests.empty())
      {
        permission_request = std::move(pending_permission_requests.front());
        pending_permission_requests.pop_front();
      }
      else if (!pending_question_requests.empty())
      {
        question_request = std::move(pending_question_requests.front());
        pending_question_requests.pop_front();
      }
    }
    if (permission_request)
    {
      complete_permission_request(permission_request, resolve_permission_prompt(permission_request->prompt, stop_requested, request_stop));
      return true;
    }
    if (question_request)
    {
      complete_question_request(question_request, resolve_question_prompt(question_request->prompt, stop_requested, request_stop));
      return true;
    }
    return false;
  };

  ava::permissions::PermissionResolver permission_resolver =
      [&](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    if (!accept_prompt_requests.load())
      return ava::permissions::PermissionResolution::Deny;
    auto request = std::make_shared<PendingPermissionRequest>(prompt);
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!accept_prompt_requests.load())
        return ava::permissions::PermissionResolution::Deny;
      pending_permission_requests.push_back(request);
    }
    std::unique_lock<std::mutex> lock(request->mutex);
    request->ready.wait(lock, [&]() { return request->result.has_value() || !accept_prompt_requests.load(); });
    if (!request->result)
      return ava::permissions::PermissionResolution::Deny;
    return std::move(*request->result);
  };

  ava::agent::QuestionResolver question_resolver = [&](ava::agent::QuestionPrompt const& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
    if (!accept_prompt_requests.load())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
    }
    auto request = std::make_shared<PendingQuestionRequest>(prompt);
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!accept_prompt_requests.load())
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
      }
      pending_question_requests.push_back(request);
    }
    std::unique_lock<std::mutex> lock(request->mutex);
    request->ready.wait(lock, [&]() { return request->result.has_value() || !accept_prompt_requests.load(); });
    if (!request->result)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
    }
    return std::move(*request->result);
  };

  auto clear_draft_for_interrupt = [&]() {
    pending_escape_clear = false;
    jump_mode = ComposerJumpMode::None;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    draft_state.clear_selection();
    snapshot.status.clear();
    return apply_composer_draft_action(draft, TuiAction::ClearInput);
  };

  auto open_external_editor = [&]() -> bool {
    if (!options.on_external_editor)
    {
      snapshot.status = "external editor unavailable";
      static_cast<void>(beep());
      return render();
    }
    pending_escape_clear = false;
    jump_mode = ComposerJumpMode::None;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    draft_state.clear_selection();
    snapshot.status = "opening external editor";
    if (!render())
      return false;

    def_prog_mode();
    endwin();
    auto edited = options.on_external_editor(draft.text);
    reset_prog_mode();
    clearok(stdscr, TRUE);
    refresh();

    if (!edited)
    {
      snapshot.status = edited.error().format();
      static_cast<void>(beep());
      return render();
    }
    if (!*edited)
    {
      snapshot.status = "external editor canceled";
      return render();
    }
    snapshot.status = replace_composer_draft(draft, std::move(**edited)) ? "external editor updated draft" : "external editor closed without changes";
    return render();
  };

  auto suspend_to_background = [&]() -> bool {
    pending_escape_clear = false;
    jump_mode = ComposerJumpMode::None;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    draft_state.clear_selection();

    {
      SignalBlockGuard block_signals;
      def_prog_mode();
      endwin();
      if (kill(0, SIGTSTP) != 0)
      {
        auto const saved_errno = errno;
        reset_prog_mode();
        clearok(stdscr, TRUE);
        refresh();
        snapshot.status = std::string("failed to suspend: ") + std::strerror(saved_errno);
        static_cast<void>(beep());
        return render();
      }
      reset_prog_mode();
      clearok(stdscr, TRUE);
      refresh();
    }

    snapshot.status = "resumed from background";
    return render();
  };

  auto queue_pending_image_attachment = [&](ava::session::ImageAttachmentRef const& imported, std::string label, std::string status,
                                            std::string transcript_prefix) -> bool {
    if (label.empty())
      label = imported.id;
    auto const image_capabilities = active_terminal_image_capabilities();
    auto const detail = attachment_detail(imported, image_capabilities);
    auto const preview = attachment_preview(imported, image_capabilities, options.on_load_image_attachment);
    pending_image_attachments.push_back(imported);
    snapshot.pending_attachments.push_back(PendingAttachmentItem{.label = label, .detail = detail, .preview = preview});
    snapshot.status = std::move(status);
    push_transcript(snapshot,
                    TranscriptItem{.label = "status", .text = transcript_prefix + ": " + label + " " + detail + "\nwill be sent with the next prompt"});
    transcript_scroll_offset = 0;
    return render();
  };

  auto paste_clipboard_image = [&]() -> bool {
    pending_escape_clear = false;
    jump_mode = ComposerJumpMode::None;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    draft_state.clear_selection();
    if (!options.on_paste_clipboard_image)
    {
      snapshot.status = "clipboard image paste unavailable";
      static_cast<void>(beep());
      return render();
    }
    snapshot.status = "reading clipboard image";
    if (!render())
      return false;

    auto imported = options.on_paste_clipboard_image();
    if (!imported)
    {
      snapshot.status = imported.error().format();
      static_cast<void>(beep());
      return render();
    }
    if (!*imported)
    {
      snapshot.status = "no clipboard image available";
      static_cast<void>(beep());
      return render();
    }
    return queue_pending_image_attachment(**imported, "clipboard image", "pasted clipboard image for next prompt", "attached clipboard image");
  };

  auto cycle_reasoning = [&]() {
    clear_reasoning_feedback_for_user_input(snapshot);
    if (!options.on_cycle_reasoning)
    {
      snapshot.status = "reasoning cycling unavailable";
      return;
    }
    auto result = options.on_cycle_reasoning();
    if (!result)
    {
      snapshot.status = result.error().format();
      return;
    }
    apply_reasoning_cycle_success(snapshot, std::move(*result));
    refresh_reasoning_status();
  };

  auto toggle_thinking_visibility = [&]() {
    snapshot.thinking_visible = !snapshot.thinking_visible;
    snapshot.status = snapshot.thinking_visible ? "thinking visible" : "thinking hidden";
    transcript_scroll_offset = 0;
  };

  auto open_model_selector = [&]() -> bool {
    if (!options.model_selector_view)
    {
      snapshot.status = "model selector unavailable";
      static_cast<void>(beep());
      return true;
    }
    pending_escape_clear = false;
    session_archive_confirmation.reset();
    snapshot.select_list = options.model_selector_view();
    active_select_list = ActiveSelectList::Model;
    snapshot.status = "model selector opened";
    transcript_scroll_offset = 0;
    return render();
  };

  auto open_scoped_model_selector = [&]() -> bool {
    if (!options.scoped_model_selector_view)
    {
      snapshot.status = "scoped model selector unavailable";
      static_cast<void>(beep());
      return true;
    }
    pending_escape_clear = false;
    session_archive_confirmation.reset();
    snapshot.select_list = options.scoped_model_selector_view();
    active_select_list = ActiveSelectList::ScopedModels;
    snapshot.status = "scoped model selector opened";
    transcript_scroll_offset = 0;
    return render();
  };

  auto open_session_selector = [&]() -> bool {
    if (!options.session_selector_view)
    {
      snapshot.status = "session selector unavailable";
      static_cast<void>(beep());
      return true;
    }
    pending_escape_clear = false;
    session_archive_confirmation.reset();
    snapshot.select_list = options.session_selector_view();
    active_select_list = ActiveSelectList::Session;
    snapshot.status = "session selector opened";
    transcript_scroll_offset = 0;
    return render();
  };

  auto cycle_model = [&](bool forward) {
    if (!options.on_cycle_model)
    {
      snapshot.status = "model cycling unavailable";
      static_cast<void>(beep());
      return;
    }
    auto result = options.on_cycle_model(forward);
    if (!result)
    {
      snapshot.status = result.error().format();
      static_cast<void>(beep());
      return;
    }
    apply_runtime_state_snapshot(std::move(*result));
    transcript_scroll_offset = 0;
  };

  auto refresh_completion_cache = [&]() -> detail::CompletionMatchModel const* {
    snapshot.input = draft.text;
    snapshot.input_cursor = draft.cursor;
    snapshot.slash_palette_suppressed = slash_palette_suppressed;
    snapshot.path_completion_force_active = path_completion_force_active;
    detail::refresh_completion_match_cache(completion_cache, snapshot, snapshot.file_references_generation);
    return completion_cache.model ? &*completion_cache.model : nullptr;
  };
  auto slash_palette_active = [&]() { return !slash_palette_suppressed && slash_palette_visible(draft.text, draft.cursor, snapshot.slash_commands); };
  auto file_reference_palette_active = [&]() {
    auto const* model = refresh_completion_cache();
    return model && model->surface == detail::CompletionSurface::FileReference && model->palette_visible;
  };
  auto path_completion_palette_active = [&]() {
    auto const* model = refresh_completion_cache();
    return model && model->surface == detail::CompletionSurface::PathCompletion && model->palette_visible;
  };
  auto completion_match_count = [&]() {
    auto const* model = refresh_completion_cache();
    return model ? model->ranked_source_indices.size() : std::size_t{0};
  };
  auto clamp_completion = [&](std::size_t selected) {
    static_cast<void>(refresh_completion_cache());
    return detail::clamp_completion_selection(completion_cache, selected);
  };
  auto previous_completion = [&](std::size_t selected) {
    static_cast<void>(refresh_completion_cache());
    return detail::previous_completion_selection(completion_cache, selected);
  };
  auto next_completion = [&](std::size_t selected) {
    static_cast<void>(refresh_completion_cache());
    return detail::next_completion_selection(completion_cache, selected);
  };
  auto selected_completion_disabled_reason = [&](std::size_t selected) {
    static_cast<void>(refresh_completion_cache());
    return detail::completion_selection_disabled_reason(completion_cache, snapshot.file_references, selected);
  };
  auto selected_completion_text = [&](std::size_t selected) {
    static_cast<void>(refresh_completion_cache());
    return detail::completion_selection_text(completion_cache, snapshot, selected);
  };

  auto scroll_up = [&](std::size_t amount) {
    pending_escape_clear = false;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const max_scroll = detail::composer_max_transcript_scroll_offset_cached(snapshot, width, height, completion_cache, snapshot.file_references_generation,
                                                                                 transcript_layout_cache, snapshot.transcript_generation);
    auto const clamped_scroll = std::min(transcript_scroll_offset, max_scroll);
    transcript_scroll_offset = std::min(max_scroll, clamped_scroll + amount);
    if (transcript_scroll_offset > 0 && !detached_sidebar_snapshot)
      detached_sidebar_snapshot = sidebar;
  };

  auto scroll_down = [&](std::size_t amount) {
    pending_escape_clear = false;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const max_scroll = detail::composer_max_transcript_scroll_offset_cached(snapshot, width, height, completion_cache, snapshot.file_references_generation,
                                                                                 transcript_layout_cache, snapshot.transcript_generation);
    auto const clamped_scroll = std::min(transcript_scroll_offset, max_scroll);
    transcript_scroll_offset = amount >= clamped_scroll ? 0 : clamped_scroll - amount;
    if (transcript_scroll_offset == 0)
    {
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
    }
  };

  auto toggle_tool_details_at = [&](std::size_t item_index) {
    if (item_index >= snapshot.transcript.size() || !snapshot.transcript[item_index].tool)
      return false;
    auto anchor = detail::TranscriptViewportAnchor{};
    auto const preserve_viewport = transcript_scroll_offset > 0;
    if (preserve_viewport)
    {
      auto const old_max_scroll =
          detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, completion_cache, snapshot.file_references_generation,
                                                               transcript_layout_cache, snapshot.transcript_generation);
      anchor = detail::capture_transcript_viewport_anchor(transcript_layout_cache.layout, old_max_scroll, transcript_scroll_offset);
    }
    auto& tool = *snapshot.transcript[item_index].tool;
    tool.details_visible = !detail::tool_card_details_visible(tool, snapshot.tool_details_visible);
    ++snapshot.transcript_generation;
    if (preserve_viewport)
    {
      auto const new_max_scroll =
          detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, completion_cache, snapshot.file_references_generation,
                                                               transcript_layout_cache, snapshot.transcript_generation);
      transcript_scroll_offset = detail::restore_transcript_viewport_anchor(anchor, transcript_layout_cache.layout, new_max_scroll, 0);
    }
    snapshot.status = detail::tool_card_details_visible(tool, snapshot.tool_details_visible) ? "tool details visible" : "tool details compact";
    return true;
  };

  auto toggle_matching_tool_details = [&](std::string_view query) -> std::optional<std::size_t> {
    auto anchor = detail::TranscriptViewportAnchor{};
    auto const preserve_viewport = transcript_scroll_offset > 0;
    if (preserve_viewport)
    {
      auto const old_max_scroll =
          detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, completion_cache, snapshot.file_references_generation,
                                                               transcript_layout_cache, snapshot.transcript_generation);
      anchor = detail::capture_transcript_viewport_anchor(transcript_layout_cache.layout, old_max_scroll, transcript_scroll_offset);
    }
    auto const item_index = toggle_latest_matching_tool_details(snapshot.transcript, query, snapshot.tool_details_visible);
    if (!item_index)
      return std::nullopt;
    ++snapshot.transcript_generation;
    if (preserve_viewport)
    {
      auto const new_max_scroll =
          detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, completion_cache, snapshot.file_references_generation,
                                                               transcript_layout_cache, snapshot.transcript_generation);
      transcript_scroll_offset = detail::restore_transcript_viewport_anchor(anchor, transcript_layout_cache.layout, new_max_scroll, 0);
    }
    return item_index;
  };

  auto sidebar_drawer_focused = [&]() {
    return snapshot.sidebar_drawer_visible && snapshot.sidebar.has_value() && !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list;
  };

  auto close_sidebar_drawer = [&]() {
    snapshot.sidebar_drawer_visible = false;
    snapshot.sidebar_drawer_scroll_offset = 0;
    pending_escape_clear = false;
    snapshot.status = "session overview closed";
  };

  auto sidebar_drawer_page_size = [&]() {
    auto const [width, height] = terminal_size();
    auto drawer_snapshot = snapshot;
    drawer_snapshot.width = width;
    drawer_snapshot.height = height;
    auto const composer_lines = detail::composer_block_line_count(drawer_snapshot, height, width);
    return height > composer_lines + 2 ? height - composer_lines - 2 : std::size_t{1};
  };

  auto handle_sidebar_drawer_input = [&](InputEvent const& event) -> std::optional<bool> {
    if (!sidebar_drawer_focused())
      return std::nullopt;
    if (event.key == Key::Escape || key_matches_action(options.key_bindings, TuiAction::Cancel, event.key))
    {
      close_sidebar_drawer();
      return render();
    }

    auto const max_scroll = sidebar_drawer_max_scroll_offset(snapshot);
    if (key_matches_action(options.key_bindings, TuiAction::PageUp, event.key))
    {
      auto const page = sidebar_drawer_page_size();
      snapshot.sidebar_drawer_scroll_offset = page >= snapshot.sidebar_drawer_scroll_offset ? 0 : snapshot.sidebar_drawer_scroll_offset - page;
      return render();
    }
    if (key_matches_action(options.key_bindings, TuiAction::PageDown, event.key))
    {
      snapshot.sidebar_drawer_scroll_offset = std::min(max_scroll, snapshot.sidebar_drawer_scroll_offset + sidebar_drawer_page_size());
      return render();
    }
    if (event.key == Key::Home)
    {
      snapshot.sidebar_drawer_scroll_offset = 0;
      return render();
    }
    if (event.key == Key::End)
    {
      snapshot.sidebar_drawer_scroll_offset = max_scroll;
      return render();
    }
    if (event.key == Key::MouseWheelUp)
    {
      if (snapshot.sidebar_drawer_scroll_offset > 0)
        --snapshot.sidebar_drawer_scroll_offset;
      return render();
    }
    if (event.key == Key::MouseWheelDown)
    {
      snapshot.sidebar_drawer_scroll_offset = std::min(max_scroll, snapshot.sidebar_drawer_scroll_offset + 1);
      return render();
    }
    static_cast<void>(beep());
    return true;
  };

  auto jump_to_bottom = [&](std::string status) {
    pending_escape_clear = false;
    transcript_scroll_offset = 0;
    detached_new_output_count = 0;
    detached_sidebar_snapshot.reset();
    snapshot.status = std::move(status);
  };

  auto scroll_to_message_boundary = [&](bool previous) {
    pending_escape_clear = false;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const max_scroll = detail::composer_max_transcript_scroll_offset_cached(snapshot, width, height, completion_cache, snapshot.file_references_generation,
                                                                                 transcript_layout_cache, snapshot.transcript_generation);
    if (max_scroll == 0)
    {
      transcript_scroll_offset = 0;
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
      snapshot.status = "transcript fits on screen";
      return;
    }

    auto const clamped_scroll = std::min(transcript_scroll_offset, max_scroll);
    auto const current_start = max_scroll - clamped_scroll;
    auto const& starts = transcript_layout_cache.layout.message_starts;
    if (starts.empty())
    {
      snapshot.status = "no message boundaries";
      return;
    }

    auto target_start = std::optional<std::size_t>{};
    if (previous)
    {
      for (auto const start : starts)
      {
        if (start >= current_start)
          break;
        target_start = start;
      }
      if (!target_start)
      {
        snapshot.status = "oldest message visible";
        return;
      }
    }
    else
    {
      for (auto const start : starts)
      {
        if (start > current_start)
        {
          target_start = start;
          break;
        }
      }
      if (!target_start || *target_start >= max_scroll)
      {
        jump_to_bottom("live tail");
        return;
      }
    }

    transcript_scroll_offset = max_scroll > *target_start ? max_scroll - *target_start : 0;
    if (transcript_scroll_offset > 0 && !detached_sidebar_snapshot)
      detached_sidebar_snapshot = sidebar;
    if (transcript_scroll_offset == 0)
    {
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
    }
    snapshot.status = previous ? "previous message" : "next message";
  };

  enum class InputLoopAction
  {
    None,
    ContinueLoop,
    BreakLoop
  };
  std::optional<std::string> forced_slash_submission;
  auto handle_submit = [&]() -> InputLoopAction {
    pending_escape_clear = false;
    std::optional<std::string> immediate_slash_submission;
    if (forced_slash_submission)
    {
      immediate_slash_submission = std::move(forced_slash_submission);
      forced_slash_submission.reset();
    }
    else if (((exact_command(draft.text, "/models") || exact_command(draft.text, "/model")) && options.model_selector_view) ||
             (exact_command(draft.text, "/scoped-models") && options.scoped_model_selector_view) ||
             ((exact_command(draft.text, "/sessions") || exact_command(draft.text, "/tree") || exact_command(draft.text, "/resume")) &&
              options.session_selector_view))
    {
      immediate_slash_submission = expanded_composer_draft_text(draft);
    }
    else if (!slash_palette_suppressed && slash_palette_visible(draft.text, draft.cursor, snapshot.slash_commands))
    {
      auto const matches = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands);
      if (!matches.empty())
      {
        selected_slash_command_index = clamp_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
        if (auto const disabled_reason =
                slash_command_selection_disabled_reason(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index))
        {
          snapshot.status = "command disabled: " + *disabled_reason;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        auto const& selected_item = matches[selected_slash_command_index];
        auto const selection_text = slash_command_selection_text(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
        if (!selected_item.argument_completion && selected_item.command == "/connect")
        {
          immediate_slash_submission = selection_text.text;
        }
        else
        {
          draft_state.clear_selection();
          static_cast<void>(replace_composer_draft(draft, std::move(selection_text.text), selection_text.cursor));
          selected_slash_command_index = 0;
          path_completion_force_active = false;
          draft_scroll_offset = 0;
          history_index.reset();
          draft_input.clear();
          snapshot.status = "command selected - press Enter to run";
          snapshot.selected_slash_command_index = selected_slash_command_index;
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        selected_slash_command_index = 0;
        path_completion_force_active = false;
        snapshot.selected_slash_command_index = selected_slash_command_index;
      }
    }
    if (file_reference_palette_active())
    {
      selected_slash_command_index = clamp_completion(selected_slash_command_index);
      if (auto const disabled_reason = selected_completion_disabled_reason(selected_slash_command_index))
      {
        snapshot.status = "reference disabled: " + *disabled_reason;
        static_cast<void>(beep());
        return InputLoopAction::ContinueLoop;
      }
      auto selection = selected_completion_text(selected_slash_command_index);
      draft_state.clear_selection();
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "file reference selected";
      snapshot.selected_slash_command_index = selected_slash_command_index;
      if (!render())
      {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      return InputLoopAction::ContinueLoop;
    }
    if (path_completion_palette_active())
    {
      selected_slash_command_index = clamp_completion(selected_slash_command_index);
      if (auto const disabled_reason = selected_completion_disabled_reason(selected_slash_command_index))
      {
        snapshot.status = "path disabled: " + *disabled_reason;
        static_cast<void>(beep());
        return InputLoopAction::ContinueLoop;
      }
      auto selection = selected_completion_text(selected_slash_command_index);
      draft_state.clear_selection();
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "path selected";
      snapshot.selected_slash_command_index = selected_slash_command_index;
      if (!render())
      {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      return InputLoopAction::ContinueLoop;
    }
    auto const submitted = immediate_slash_submission ? *immediate_slash_submission : expanded_composer_draft_text(draft);
    draft_state.clear_selection();
    reset_composer_draft(draft);
    jump_mode = ComposerJumpMode::None;
    draft_scroll_offset = 0;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    path_completion_force_active = false;
    if (!submitted.empty())
    {
      if ((exact_command(submitted, "/models") || exact_command(submitted, "/model")) && options.model_selector_view)
      {
        push_history(input_history, submitted);
        if (!open_model_selector())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (exact_command(submitted, "/scoped-models") && options.scoped_model_selector_view)
      {
        push_history(input_history, submitted);
        if (!open_scoped_model_selector())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if ((exact_command(submitted, "/sessions") || exact_command(submitted, "/tree") || exact_command(submitted, "/resume")) && options.session_selector_view)
      {
        push_history(input_history, submitted);
        if (!open_session_selector())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto reload_target = reload_command_argument(submitted))
      {
        push_history(input_history, submitted);
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        auto const parsed_reload_target = reload_target_from_argument(*reload_target);
        if (!parsed_reload_target)
        {
          snapshot.status = "invalid_argument: unsupported reload target\n  target: " + *reload_target + "\n  supported: keybindings, theme";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        if (*parsed_reload_target == ReloadTarget::DisplaySettings)
        {
          if (!options.on_reload_display_settings)
          {
            snapshot.status = "display reload unavailable";
            push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
            transcript_scroll_offset = 0;
            static_cast<void>(beep());
            if (!render())
            {
              terminal_write_failed = true;
              return InputLoopAction::BreakLoop;
            }
            return InputLoopAction::ContinueLoop;
          }
          auto reloaded = options.on_reload_display_settings();
          if (!reloaded)
          {
            snapshot.status = reloaded.error().format();
            push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
            transcript_scroll_offset = 0;
            static_cast<void>(beep());
            if (!render())
            {
              terminal_write_failed = true;
              return InputLoopAction::BreakLoop;
            }
            return InputLoopAction::ContinueLoop;
          }
          auto status = reloaded->status.empty() ? std::string("display theme reloaded") : reloaded->status;
          apply_runtime_state_snapshot(std::move(*reloaded));
          push_transcript(snapshot, TranscriptItem{.label = "ava", .text = std::move(status), .meta = assistant_meta_for_snapshot(snapshot)});
          transcript_scroll_offset = 0;
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        if (!options.on_reload_key_bindings)
        {
          snapshot.status = "reload unavailable";
          push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        auto reloaded = options.on_reload_key_bindings();
        if (!reloaded)
        {
          snapshot.status = reloaded.error().format();
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        options.key_bindings = std::move(reloaded->key_bindings);
        apply_runtime_state_snapshot(std::move(reloaded->state));
        push_transcript(snapshot, TranscriptItem{.label = "ava", .text = "keybindings reloaded", .meta = assistant_meta_for_snapshot(snapshot)});
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/hotkeys" || submitted == "/keybindings")
      {
        push_history(input_history, submitted);
        snapshot.select_list = hotkeys_select_list_view(options.key_bindings);
        active_select_list = ActiveSelectList::Hotkeys;
        snapshot.status = "keybindings opened";
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/sidebar")
      {
        push_history(input_history, submitted);
        snapshot.sidebar_drawer_visible = true;
        snapshot.sidebar_drawer_scroll_offset = 0;
        transcript_scroll_offset = 0;
        detached_new_output_count = 0;
        detached_sidebar_snapshot.reset();
        snapshot.status = "session overview opened";
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/settings")
      {
        push_history(input_history, submitted);
        refresh_token_status();
        refresh_reasoning_status();
        auto settings_snapshot = snapshot;
        settings_snapshot.sidebar = sidebar;
        snapshot.select_list = settings_select_list_view(settings_snapshot, options.key_bindings);
        active_select_list = ActiveSelectList::Settings;
        snapshot.status = "settings opened";
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto attach_target = attach_command_argument(submitted))
      {
        push_history(input_history, submitted);
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        if (attach_target->empty())
        {
          snapshot.status = "invalid_argument: usage: /attach <image-path>";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        if (!options.on_attach_image)
        {
          snapshot.status = "image attachment import unavailable";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        auto imported = options.on_attach_image(*attach_target);
        if (!imported)
        {
          snapshot.status = imported.error().format();
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        auto label = compact_path_leaf(*attach_target);
        if (!queue_pending_image_attachment(*imported, std::move(label), "attached image for next prompt", "attached image"))
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto tool_query = tool_command_argument(submitted))
      {
        push_history(input_history, submitted);
        auto const tool_index = toggle_matching_tool_details(*tool_query);
        if (tool_index)
        {
          auto const& tool = *snapshot.transcript[*tool_index].tool;
          snapshot.status = detail::tool_card_details_visible(tool, snapshot.tool_details_visible)
                                ? (tool_query->empty() ? "latest tool details visible" : "matching tool details visible")
                                : (tool_query->empty() ? "latest tool details compact" : "matching tool details compact");
        }
        else
        {
          snapshot.status = tool_query->empty() ? "no tool details to show" : "no matching tool details to show";
          static_cast<void>(beep());
        }
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto diff_query = diff_command_argument(submitted))
      {
        push_history(input_history, submitted);
        auto diff_text = latest_tool_diff_copy_text(snapshot.transcript, *diff_query);
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        if (diff_text)
        {
          auto const title = diff_query->empty() ? std::string_view("Latest tool diff:") : std::string_view("Matching tool diff:");
          push_transcript(snapshot,
                          TranscriptItem{.label = "ava", .text = diff_transcript_text(title, *diff_text), .meta = assistant_meta_for_snapshot(snapshot)});
          snapshot.status = diff_query->empty() ? "showing latest tool diff" : "showing matching tool diff";
        }
        else
        {
          snapshot.status = diff_query->empty() ? "no tool diff to show" : "no matching tool diff to show";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          static_cast<void>(beep());
        }
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto copy_target = copy_command_argument(submitted))
      {
        push_history(input_history, submitted);
        auto const target = parse_copy_target(*copy_target);
        std::optional<std::string> copy_text;
        std::string copied_status;
        std::string missing_status;
        bool valid_target = true;
        bool copied = false;
        if (target.name.empty() || target.name == "last")
        {
          copy_text = latest_ava_message_copy_text(snapshot.transcript);
          copied_status = "copied last AVA message to clipboard";
          missing_status = "no AVA messages to copy";
        }
        else if (target.name == "tool" || target.name == "tools")
        {
          copy_text = latest_tool_copy_text(snapshot.transcript, target.query);
          copied_status = target.query.empty() ? "copied latest tool details to clipboard" : "copied matching tool details to clipboard";
          missing_status = target.query.empty() ? "no tool details to copy" : "no matching tool details to copy";
        }
        else if (target.name == "diff" || target.name == "diffs")
        {
          copy_text = latest_tool_diff_copy_text(snapshot.transcript, target.query);
          copied_status = target.query.empty() ? "copied latest tool diff to clipboard" : "copied matching tool diff to clipboard";
          missing_status = target.query.empty() ? "no tool diff to copy" : "no matching tool diff to copy";
        }
        else if (target.name == "permission" || target.name == "permissions")
        {
          copy_text = latest_permission_copy_text(snapshot.transcript, target.query);
          copied_status = target.query.empty() ? "copied latest permission details to clipboard" : "copied matching permission details to clipboard";
          missing_status = target.query.empty() ? "no permission details to copy" : "no matching permission details to copy";
        }
        else
        {
          valid_target = false;
          snapshot.status =
              "invalid_argument: unsupported copy target\n  target: " + target.name + "\n  supported: tool [query], diff [query], permission [query]";
        }

        if (valid_target)
        {
          copied = copy_text && copy_text_to_terminal_clipboard(*copy_text);
          if (copied)
          {
            snapshot.status = std::move(copied_status);
          }
          else
          {
            snapshot.status = copy_text ? "clipboard copy failed" : std::move(missing_status);
          }
        }
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        push_transcript(snapshot, TranscriptItem{.label = copied ? "status" : "error", .text = snapshot.status});
        transcript_scroll_offset = 0;
        if (!copied)
          static_cast<void>(beep());
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/details")
      {
        push_history(input_history, submitted);
        snapshot.tool_details_visible = !snapshot.tool_details_visible;
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        push_transcript(snapshot, TranscriptItem{.label = "ava",
                                                 .text = snapshot.tool_details_visible ? "tool details are now visible" : "tool details are now compact",
                                                 .meta = assistant_meta_for_snapshot(snapshot)});
        snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/thinking")
      {
        push_history(input_history, submitted);
        toggle_thinking_visibility();
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        push_transcript(snapshot, TranscriptItem{.label = "ava",
                                                 .text = snapshot.thinking_visible ? "thinking blocks are now visible" : "thinking blocks are now hidden",
                                                 .meta = assistant_meta_for_snapshot(snapshot)});
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      auto const is_command_submission = submitted.starts_with('/') || shell_helper_submission(submitted);
      if (is_command_submission && session_switching_command(submitted) && !pending_image_attachments.empty())
      {
        pending_image_attachments.clear();
        snapshot.pending_attachments.clear();
      }
      auto const supports_active_queue = !is_command_submission || is_compact_command(submitted);
      auto const transcript_before_submit = snapshot.transcript;
      auto submitted_transcript = transcript_before_submit;
      std::size_t turn_snapshot_leading_evictions = 0;
      auto submit_image_attachments = std::vector<ava::session::ImageAttachmentRef>{};
      if (!is_command_submission && !pending_image_attachments.empty())
      {
        submit_image_attachments = std::move(pending_image_attachments);
        pending_image_attachments.clear();
        snapshot.pending_attachments.clear();
      }
      ava::app::EventBus event_bus;
      EventEnvelopeQueue event_queue;
      TuiEventState event_state;
      std::atomic_bool run_cancel_requested{false};
      bool close_after_submit = false;
      auto cancel_requested = [&run_cancel_requested]() { return run_cancel_requested.load(); };
      event_bus.subscribe(event_queue.sink());
      std::optional<TuiActiveRunQueues> active_queues;
      std::mutex event_context_mutex;
      std::string current_request_id;
      auto set_current_request_id = [&](std::string request_id) {
        std::lock_guard lock(event_context_mutex);
        current_request_id = std::move(request_id);
      };
      if (supports_active_queue && options.create_active_run_queues)
      {
        active_queues = options.create_active_run_queues([&event_bus](ava::app::EventEnvelope const& event) { return event_bus.publish(event); });
        if (!active_queues->active_request_id.empty())
        {
          set_current_request_id(active_queues->active_request_id);
          auto mark_follow_up_started = active_queues->mark_follow_up_started;
          active_queues->mark_follow_up_started = [&, mark_follow_up_started](TuiQueuedFollowUp const& follow_up) {
            set_current_request_id(follow_up.request_id);
            if (mark_follow_up_started)
              return mark_follow_up_started(follow_up);
            return ava::core::VoidResult{};
          };
        }
      }
      auto runtime_event_to_bus_sink = [&]() -> ava::app::runtime::EventSink {
        return [&](ava::app::runtime::Event const& event) {
          ava::app::EventEnvelopeContext event_context;
          {
            std::lock_guard lock(event_context_mutex);
            if (!current_request_id.empty())
            {
              event_context.request_id = current_request_id;
              event_context.correlation_id = current_request_id;
            }
          }
          return event_bus.publish(ava::app::to_event_envelope(event, event_context));
        };
      };
      auto event_sink = supports_active_queue ? runtime_event_to_bus_sink() : ava::app::runtime::EventSink{};
      {
        std::lock_guard<std::mutex> lock(prompt_audit_mutex);
        prompt_audit_sink = event_sink;
      }
      auto const turn_started_at = std::chrono::steady_clock::now();
      auto upsert_stopping_activity = [&]() {
        auto item = SidebarActivityItem{.id = "stopping",
                                        .label = "stopping",
                                        .detail = "waiting for active work to stop; queued drafts skip on stop",
                                        .status = ToolTimelineStatus::Running};
        auto existing = std::ranges::find_if(sidebar.activity, [&](SidebarActivityItem const& activity) { return activity.id == item.id; });
        if (existing == sidebar.activity.end())
        {
          sidebar.activity.push_back(std::move(item));
        }
        else
        {
          *existing = std::move(item);
        }
      };
      auto settle_turn_activity = [&]() {
        auto responding = std::ranges::find_if(sidebar.activity, [](SidebarActivityItem const& activity) { return activity.id == "responding"; });
        if (responding == sidebar.activity.end() || responding->status != ToolTimelineStatus::Running)
          return;
        if (run_cancel_requested.load() || event_state.run_status == TuiEventRunStatus::Canceled)
        {
          responding->status = ToolTimelineStatus::Canceled;
          responding->detail = "assistant stopped";
          return;
        }
        if (event_state.run_status == TuiEventRunStatus::Error)
        {
          responding->status = ToolTimelineStatus::Error;
          responding->detail = "assistant failed";
          return;
        }
        responding->status = ToolTimelineStatus::Success;
        responding->detail = "assistant responded";
      };
      auto request_stop = [&]() -> bool {
        bool const was_already_requested = run_cancel_requested.exchange(true);
        fail_pending_prompt_requests();
        if (!was_already_requested)
          static_cast<void>(beep());
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.status = "stop requested";
          upsert_stopping_activity();
        }
        return render();
      };
      auto request_close_after_submit = [&]() {
        run_cancel_requested.store(true);
        close_after_submit = true;
        fail_pending_prompt_requests();
      };
      auto drain_runtime_events = [&]() -> RuntimeEventDrainResult {
        auto events = event_queue.drain();
        if (events.empty())
          return RuntimeEventDrainResult::NoEvents;
        for (auto const& event : events)
        {
          apply_event_envelope(event_state, event);
        }
        auto turn_transcript = event_state_transcript_snapshot(event_state);
        auto detached_indicator_changed = false;
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          auto const preserve_viewport = transcript_scroll_offset > 0;
          auto old_anchor = detail::TranscriptViewportAnchor{};
          std::size_t old_rendered_lines = 0;
          if (preserve_viewport)
          {
            auto const old_max_scroll = detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, completion_cache,
                                                                                             snapshot.file_references_generation, transcript_layout_cache,
                                                                                             snapshot.transcript_generation);
            old_anchor = detail::capture_transcript_viewport_anchor(transcript_layout_cache.layout, old_max_scroll, transcript_scroll_offset);
            old_rendered_lines = transcript_layout_cache.layout.lines.size();
          }
          if (preserve_viewport && !detached_sidebar_snapshot)
            detached_sidebar_snapshot = sidebar;
          apply_assistant_turn_meta(turn_transcript, assistant_meta_for_snapshot(snapshot, std::chrono::steady_clock::now() - turn_started_at));
          auto const tool_detail_overrides = capture_tool_detail_visibility(snapshot.transcript);
          auto const capped_update =
              apply_capped_transcript_snapshot(snapshot.transcript, submitted_transcript, turn_transcript, turn_snapshot_leading_evictions);
          carry_tool_detail_visibility(tool_detail_overrides, snapshot.transcript);
          auto const item_index_shift = capped_update.item_index_shift;
          turn_snapshot_leading_evictions = capped_update.leading_evictions;
          ++snapshot.transcript_generation;
          snapshot.queued_messages = event_state.queued_messages;
          sidebar.activity = event_state.activity;
          if (run_cancel_requested.load() && event_state.run_status == TuiEventRunStatus::Running)
          {
            upsert_stopping_activity();
          }
          for (auto const& file : event_state.modified_files)
          {
            auto const exists = std::ranges::any_of(sidebar.modified_files, [&](SidebarModifiedFile const& existing) { return existing.path == file.path; });
            if (!exists)
              sidebar.modified_files.push_back(file);
          }
          constexpr auto kMaxSidebarModifiedFiles = std::size_t{50};
          if (sidebar.modified_files.size() > kMaxSidebarModifiedFiles)
          {
            sidebar.modified_files.erase(
                sidebar.modified_files.begin(),
                sidebar.modified_files.begin() + static_cast<std::ptrdiff_t>(sidebar.modified_files.size() - kMaxSidebarModifiedFiles));
          }
          if (preserve_viewport)
          {
            auto const new_max_scroll = detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, completion_cache,
                                                                                             snapshot.file_references_generation, transcript_layout_cache,
                                                                                             snapshot.transcript_generation);
            transcript_scroll_offset = detail::restore_transcript_viewport_anchor(old_anchor, transcript_layout_cache.layout, new_max_scroll, item_index_shift);
            auto const new_rendered_lines = transcript_layout_cache.layout.lines.size();
            detached_new_output_count += new_rendered_lines > old_rendered_lines ? new_rendered_lines - old_rendered_lines : std::size_t{1};
            detached_indicator_changed = true;
          }
          else
          {
            transcript_scroll_offset = 0;
          }
          if (transcript_scroll_offset > 0)
          {
            auto const [width, height] = terminal_size();
            snapshot.width = width;
            snapshot.height = height;
            transcript_scroll_offset = std::min(transcript_scroll_offset, detail::composer_max_transcript_scroll_offset_cached(
                                                                              snapshot, width, height, completion_cache, snapshot.file_references_generation,
                                                                              transcript_layout_cache, snapshot.transcript_generation));
            snapshot.transcript_scroll_offset = transcript_scroll_offset;
          }
        }
        if (transcript_scroll_offset > 0 && !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list)
        {
          if (!detached_indicator_changed)
            return RuntimeEventDrainResult::UpdatedNoRender;
          return render() ? RuntimeEventDrainResult::Rendered : RuntimeEventDrainResult::RenderFailed;
        }
        return render() ? RuntimeEventDrainResult::Rendered : RuntimeEventDrainResult::RenderFailed;
      };

      if (is_command_submission && should_echo_slash_command(submitted))
      {
        auto command_item = TranscriptItem{.label = "cmd", .text = submitted};
        submitted_transcript.push_back(command_item);
        push_transcript(snapshot, std::move(command_item));
      }
      push_history(input_history, submitted);
      snapshot.status = is_command_submission ? "running command..." : "thinking...";
      snapshot.processing = true;
      if (!render())
      {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      auto result = TuiSubmitResult{};
      if (options.on_submit)
      {
        accept_prompt_requests.store(true);
        auto submit_future = std::async(std::launch::async, [&]() {
          auto take_steering_messages = active_queues ? active_queues->take_steering_messages : std::function<ava::core::Result<std::vector<std::string>>()>{};
          auto skip_active_steering = active_queues ? active_queues->skip_active_steering : std::function<ava::core::VoidResult(std::string_view)>{};
          auto take_next_follow_up = active_queues ? active_queues->take_next_follow_up : std::function<std::optional<TuiQueuedFollowUp>()>{};
          auto mark_follow_up_started =
              active_queues ? active_queues->mark_follow_up_started : std::function<ava::core::VoidResult(TuiQueuedFollowUp const&)>{};
          return options.on_submit(submitted, TuiSubmitContext{.permission_resolver = permission_resolver,
                                                               .question_resolver = question_resolver,
                                                               .event_sink = event_sink,
                                                               .cancel_requested = cancel_requested,
                                                               .take_steering_messages = take_steering_messages,
                                                               .skip_active_steering = skip_active_steering,
                                                               .take_next_follow_up = take_next_follow_up,
                                                               .mark_follow_up_started = mark_follow_up_started,
                                                               .image_attachments = submit_image_attachments});
        });
        auto handle_active_input = [&](RuntimeInput const& active_input) -> bool {
          if (active_input.resize)
            return render();

          auto const active_event = active_input.event;
          auto active_is_action = [&](TuiAction action) { return key_matches_action(options.key_bindings, action, active_event.key); };
          auto restore_latest_queued_message = [&]() {
            if (!active_queues || !active_queues->restore_latest)
            {
              snapshot.status = "active-run restore unavailable";
              return render();
            }
            auto restored = active_queues->restore_latest();
            if (!restored)
            {
              snapshot.status = restored.error().format();
              static_cast<void>(beep());
              return render();
            }
            auto const restored_text = restored->steering ? "/steer " + restored->message : restored->message;
            draft_state.clear_selection();
            static_cast<void>(replace_composer_draft(draft, restored_text));
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            snapshot.status = restored->steering ? "steering restored" : "follow-up restored";
            return render();
          };
          auto completion_snapshot = [&]() -> ComposerSnapshot const& {
            snapshot.input = draft.text;
            snapshot.input_cursor = draft.cursor;
            snapshot.selected_slash_command_index = selected_slash_command_index;
            snapshot.slash_palette_suppressed = slash_palette_suppressed;
            snapshot.path_completion_force_active = path_completion_force_active;
            detail::refresh_completion_match_cache(completion_cache, snapshot, snapshot.file_references_generation);
            return snapshot;
          };
          auto run_active_command = [&]() -> std::optional<bool> {
            if (draft.text == "/sidebar")
            {
              push_history(input_history, draft.text);
              draft_state.clear_selection();
              reset_composer_draft(draft);
              jump_mode = ComposerJumpMode::None;
              draft_scroll_offset = 0;
              history_index.reset();
              draft_input.clear();
              selected_slash_command_index = 0;
              slash_palette_suppressed = false;
              path_completion_force_active = false;
              snapshot.sidebar_drawer_visible = true;
              snapshot.sidebar_drawer_scroll_offset = 0;
              transcript_scroll_offset = 0;
              detached_new_output_count = 0;
              detached_sidebar_snapshot.reset();
              snapshot.status = "session overview opened";
              return render();
            }
            if (!active_queues || !active_queues->run_nonblocking_command || draft.text.empty())
              return std::nullopt;
            auto const submitted_command = expanded_composer_draft_text(draft);
            auto const dispatch = dispatch_tui_active_nonblocking_command_gated(completion_snapshot(), *active_queues, submitted_command);
            if (dispatch.kind == TuiActiveNonblockingCommandDispatchKind::Unrecognized)
              return std::nullopt;
            if (dispatch.kind == TuiActiveNonblockingCommandDispatchKind::Blocked)
            {
              snapshot.status = dispatch.status;
              static_cast<void>(beep());
              return render();
            }
            push_history(input_history, submitted_command);
            push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted_command});
            for (auto const& output : dispatch.output)
              push_transcript(snapshot, TranscriptItem{.label = "ava", .text = output, .meta = assistant_meta_for_snapshot(snapshot)});
            draft_state.clear_selection();
            reset_composer_draft(draft);
            jump_mode = ComposerJumpMode::None;
            draft_scroll_offset = 0;
            transcript_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            snapshot.status = dispatch.output.empty() ? "job command complete" : dispatch.output.back();
            return render();
          };
          auto reject_disabled_visible_completion = [&]() -> std::optional<bool> {
            auto const& current = completion_snapshot();
            if (auto const disabled_status = detail::disabled_visible_completion_selection_status(current, completion_cache))
            {
              snapshot.status = *disabled_status;
              static_cast<void>(beep());
              return render();
            }
            return std::nullopt;
          };
          auto queue_active_draft = [&](bool follow_up_only) {
            if (auto handled = reject_disabled_visible_completion())
              return *handled;
            if (draft.text.empty())
            {
              snapshot.status = "type a follow-up before queueing";
              return render();
            }
            if (!follow_up_only && draft.text == "/restore")
            {
              return restore_latest_queued_message();
            }
            auto const steering_prefix = std::string_view("/steer ");
            bool const steering_draft = !follow_up_only && draft.text.starts_with(steering_prefix);
            if ((draft.text.starts_with('/') || shell_helper_submission(draft.text)) && !steering_draft)
            {
              snapshot.status = "commands run between turns";
              return render();
            }
            if (run_cancel_requested.load())
            {
              snapshot.status = "stop requested; queueing disabled";
              return render();
            }
            if (!active_queues)
            {
              snapshot.status = "active-run queue unavailable";
              return render();
            }
            if (steering_draft && !active_queues->queue_steering)
            {
              snapshot.status = "active-run steering unavailable";
              return render();
            }
            if (!steering_draft && !active_queues->queue_follow_up)
            {
              snapshot.status = "active-run follow-up unavailable";
              return render();
            }

            auto queued_text = expanded_composer_draft_text(draft);
            auto queued =
                steering_draft ? active_queues->queue_steering(queued_text.substr(steering_prefix.size())) : active_queues->queue_follow_up(queued_text);
            if (!queued)
            {
              snapshot.status = queued.error().format();
              static_cast<void>(beep());
              return render();
            }
            push_history(input_history, queued_text);
            draft_state.clear_selection();
            reset_composer_draft(draft);
            jump_mode = ComposerJumpMode::None;
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            snapshot.status = steering_draft ? "steering queued" : "follow-up queued";
            return render();
          };
          if (auto handled = handle_sidebar_drawer_input(active_event))
            return *handled;
          if (jump_mode != ComposerJumpMode::None)
          {
            if (active_is_action(TuiAction::JumpForward) || active_is_action(TuiAction::JumpBackward))
            {
              jump_mode = ComposerJumpMode::None;
              snapshot.status = "jump cancelled";
              return render();
            }
            if (auto const target = printable_jump_target(active_input))
            {
              bool const forward = jump_mode == ComposerJumpMode::Forward;
              jump_mode = ComposerJumpMode::None;
              pending_escape_clear = false;
              history_index.reset();
              draft_input.clear();
              selected_slash_command_index = 0;
              slash_palette_suppressed = false;
              path_completion_force_active = false;
              draft_scroll_offset = 0;
              draft_state.clear_selection();
              snapshot.status =
                  jump_composer_draft_to_character(draft, *target, forward) ? (forward ? "jumped forward" : "jumped backward") : "jump character not found";
              return render();
            }
            jump_mode = ComposerJumpMode::None;
          }
          if (active_event.key == Key::Escape || active_is_action(TuiAction::Cancel))
          {
            return request_stop();
          }
          if (active_is_action(TuiAction::CopySelection) && draft_state.selection_bounds())
          {
            pending_escape_clear = false;
            static_cast<void>(draft_state.copy_selection(snapshot));
            return render();
          }
          if (active_is_action(TuiAction::Interrupt))
          {
            if (!draft.text.empty())
            {
              static_cast<void>(clear_draft_for_interrupt());
              snapshot.selected_slash_command_index = selected_slash_command_index;
              return render();
            }
            request_close_after_submit();
            return true;
          }
          bool const active_ctrl_d_delete_forward = active_event.key == Key::CtrlD && active_is_action(TuiAction::DeleteForward) && !draft.text.empty();
          if (active_is_action(TuiAction::Exit) && !active_ctrl_d_delete_forward)
          {
            request_close_after_submit();
            return true;
          }
          auto insert_active_text = [&]() {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            draft_scroll_offset = 0;
            auto const text = active_input.text.empty() ? std::string(1, active_event.character) : active_input.text;
            if (active_input.bracketed_paste)
            {
              static_cast<void>(draft_state.delete_selection());
              static_cast<void>(insert_composer_paste_text(draft, text));
              snapshot.status = "pasted into draft safely";
            }
            else
            {
              if (!draft_state.replace_selection(text))
                static_cast<void>(insert_composer_draft_text(draft, text));
            }
          };
          if (active_event.key == Key::Character)
          {
            insert_active_text();
            return render();
          }
          if (draft_state.extend_selection_for_key(active_event.key, snapshot))
          {
            return render();
          }
          if (active_event.key == Key::CtrlHome || active_event.key == Key::CtrlEnd)
          {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            draft_state.clear_selection();
            draft.cursor = active_event.key == Key::CtrlHome ? 0 : draft.text.size();
            draft.vertical_column = std::string::npos;
            draft.yank_start = std::string::npos;
            draft.yank_end = std::string::npos;
            return render();
          }
          if (active_is_action(TuiAction::AutocompleteAccept) && slash_palette_active())
          {
            selected_slash_command_index = clamp_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
            if (auto const disabled_reason =
                    slash_command_selection_disabled_reason(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index))
            {
              snapshot.status = "command disabled: " + *disabled_reason;
              static_cast<void>(beep());
            }
            else
            {
              draft_state.clear_selection();
              auto selection = slash_command_selection_text(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
              static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
              selected_slash_command_index = 0;
              path_completion_force_active = false;
              draft_scroll_offset = 0;
              history_index.reset();
              draft_input.clear();
              snapshot.status = "command selected - press Enter to queue";
            }
            return render();
          }
          if (active_is_action(TuiAction::AutocompleteAccept) && file_reference_palette_active())
          {
            selected_slash_command_index = clamp_completion(selected_slash_command_index);
            if (auto const disabled_reason = selected_completion_disabled_reason(selected_slash_command_index))
            {
              snapshot.status = "reference disabled: " + *disabled_reason;
              static_cast<void>(beep());
              return render();
            }
            auto selection = selected_completion_text(selected_slash_command_index);
            draft_state.clear_selection();
            static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
            selected_slash_command_index = 0;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            snapshot.status = "file reference selected";
            return render();
          }
          if (active_is_action(TuiAction::AutocompleteAccept) && path_completion_palette_active())
          {
            selected_slash_command_index = clamp_completion(selected_slash_command_index);
            if (auto const disabled_reason = selected_completion_disabled_reason(selected_slash_command_index))
            {
              snapshot.status = "path disabled: " + *disabled_reason;
              static_cast<void>(beep());
              return render();
            }
            auto selection = selected_completion_text(selected_slash_command_index);
            draft_state.clear_selection();
            static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
            selected_slash_command_index = 0;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            snapshot.status = "path selected";
            return render();
          }
          if (active_is_action(TuiAction::AutocompleteAccept))
          {
            auto const was_suppressed = slash_palette_suppressed;
            slash_palette_suppressed = false;
            path_completion_force_active = true;
            auto const match_count = completion_match_count();
            if (match_count == 0)
            {
              slash_palette_suppressed = was_suppressed;
              path_completion_force_active = false;
            }
            else
            {
              if (match_count == 1)
              {
                if (auto const disabled_reason = selected_completion_disabled_reason(0))
                {
                  path_completion_force_active = false;
                  snapshot.status = "path disabled: " + *disabled_reason;
                  static_cast<void>(beep());
                  return render();
                }
                auto selection = selected_completion_text(0);
                draft_state.clear_selection();
                static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
                path_completion_force_active = false;
                draft_scroll_offset = 0;
                snapshot.status = "path selected";
              }
              else
              {
                selected_slash_command_index = 0;
                snapshot.status = "path suggestions";
              }
              return render();
            }
          }
          if (active_is_action(TuiAction::NewLine))
          {
            draft_state.insert_newline();
            return render();
          }
          if (active_is_action(TuiAction::ExternalEditor))
          {
            return open_external_editor();
          }
          if (active_is_action(TuiAction::Suspend))
          {
            return suspend_to_background();
          }
          if (active_is_action(TuiAction::ClipboardPasteImage))
          {
            return paste_clipboard_image();
          }
          if (active_is_action(TuiAction::MessageDequeue))
          {
            return restore_latest_queued_message();
          }
          if (active_is_action(TuiAction::JumpForward) || active_is_action(TuiAction::JumpBackward))
          {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            jump_mode = active_is_action(TuiAction::JumpForward) ? ComposerJumpMode::Forward : ComposerJumpMode::Backward;
            snapshot.status = active_is_action(TuiAction::JumpForward) ? "jump forward: type character" : "jump backward: type character";
            return render();
          }
          if (active_is_action(TuiAction::MessageFollowUp))
          {
            return queue_active_draft(true);
          }
          if (active_is_action(TuiAction::Submit))
          {
            if (auto handled = reject_disabled_visible_completion())
              return *handled;
            if (active_event.key == Key::Enter && draft_state.convert_backslash_enter_to_newline(snapshot))
              return render();
            if (auto handled = run_active_command())
              return *handled;
            return queue_active_draft(false);
          }
          bool const active_delete_forward = active_is_action(TuiAction::DeleteForward) && (active_event.key != Key::CtrlD || active_ctrl_d_delete_forward);
          if (active_is_action(TuiAction::DeleteBackward) || active_delete_forward || active_is_action(TuiAction::DeleteWordBackward) ||
              active_is_action(TuiAction::DeleteWordForward) || active_is_action(TuiAction::DeleteToLineStart) ||
              active_is_action(TuiAction::DeleteToLineEnd) || active_is_action(TuiAction::ClearInput) || active_is_action(TuiAction::CursorLeft) ||
              active_is_action(TuiAction::CursorRight) || active_is_action(TuiAction::CursorLineStart) || active_is_action(TuiAction::CursorLineEnd) ||
              active_is_action(TuiAction::CursorWordLeft) || active_is_action(TuiAction::CursorWordRight) || active_is_action(TuiAction::Undo) ||
              active_is_action(TuiAction::Redo) || active_is_action(TuiAction::Yank) || active_is_action(TuiAction::YankPop))
          {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            draft_scroll_offset = 0;
            if (active_is_action(TuiAction::DeleteBackward))
            {
              if (!draft_state.delete_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteBackward));
            }
            else if (active_delete_forward)
            {
              if (!draft_state.delete_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteForward));
            }
            else if (active_is_action(TuiAction::DeleteWordBackward))
            {
              if (!draft_state.delete_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordBackward));
            }
            else if (active_is_action(TuiAction::DeleteWordForward))
            {
              if (!draft_state.delete_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordForward));
            }
            else if (active_is_action(TuiAction::DeleteToLineStart))
            {
              if (!draft_state.delete_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineStart));
            }
            else if (active_is_action(TuiAction::DeleteToLineEnd))
            {
              if (!draft_state.delete_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineEnd));
            }
            else if (active_is_action(TuiAction::ClearInput))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::ClearInput));
            }
            else if (active_is_action(TuiAction::CursorLeft))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLeft));
            }
            else if (active_is_action(TuiAction::CursorRight))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorRight));
            }
            else if (active_is_action(TuiAction::CursorLineStart))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineStart));
            }
            else if (active_is_action(TuiAction::CursorLineEnd))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineEnd));
            }
            else if (active_is_action(TuiAction::CursorWordLeft))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordLeft));
            }
            else if (active_is_action(TuiAction::CursorWordRight))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordRight));
            }
            else if (active_is_action(TuiAction::Undo))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::Undo));
            }
            else if (active_is_action(TuiAction::Redo))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::Redo));
            }
            else if (active_is_action(TuiAction::Yank))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::Yank));
            }
            else if (active_is_action(TuiAction::YankPop))
            {
              draft_state.clear_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::YankPop));
            }
            return render();
          }
          if (active_is_action(TuiAction::PageUp))
          {
            auto const [_, height] = terminal_size();
            scroll_up(std::max<std::size_t>(1, height / 2));
            return render();
          }
          if (active_is_action(TuiAction::PageDown))
          {
            auto const [_, height] = terminal_size();
            scroll_down(std::max<std::size_t>(1, height / 2));
            return render();
          }
          if (active_is_action(TuiAction::MessagePrev))
          {
            scroll_to_message_boundary(true);
            return render();
          }
          if (active_is_action(TuiAction::MessageNext))
          {
            scroll_to_message_boundary(false);
            return render();
          }
          if (active_is_action(TuiAction::JumpToBottom))
          {
            jump_to_bottom("live tail");
            return render();
          }
          if (active_is_action(TuiAction::PalettePrev) && slash_palette_active())
          {
            pending_escape_clear = false;
            auto const match_count = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands).size();
            if (match_count == 0)
            {
              snapshot.status = "no matching slash commands";
            }
            else
            {
              selected_slash_command_index = previous_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
              snapshot.status = "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
            }
            return render();
          }
          if (active_is_action(TuiAction::PalettePrev) && file_reference_palette_active())
          {
            pending_escape_clear = false;
            auto const match_count = completion_match_count();
            if (match_count == 0)
            {
              snapshot.status = "no matching file references";
            }
            else
            {
              selected_slash_command_index = previous_completion(selected_slash_command_index);
              snapshot.status = "reference " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
            }
            return render();
          }
          if (active_is_action(TuiAction::PalettePrev) && path_completion_palette_active())
          {
            pending_escape_clear = false;
            auto const match_count = completion_match_count();
            if (match_count == 0)
            {
              snapshot.status = "no matching paths";
            }
            else
            {
              selected_slash_command_index = previous_completion(selected_slash_command_index);
              snapshot.status = "path " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
            }
            return render();
          }
          if (active_is_action(TuiAction::HistoryPrev))
          {
            pending_escape_clear = false;
            draft_state.clear_selection();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            if (browse_composer_input_history(draft, input_history, history_index, draft_input, true))
            {
              snapshot.status = "history previous";
            }
            else
            {
              scroll_up(kKeyboardScrollRows);
            }
            return render();
          }
          if (active_event.key == Key::ArrowUp)
          {
            scroll_up(kKeyboardScrollRows);
            return render();
          }
          if (active_is_action(TuiAction::CursorUp) && apply_composer_draft_action(draft, TuiAction::CursorUp))
          {
            pending_escape_clear = false;
            draft_state.clear_selection();
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            return render();
          }
          if (active_is_action(TuiAction::PaletteNext) && slash_palette_active())
          {
            pending_escape_clear = false;
            auto const match_count = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands).size();
            if (match_count == 0)
            {
              snapshot.status = "no matching slash commands";
            }
            else
            {
              selected_slash_command_index = next_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
              snapshot.status = "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
            }
            return render();
          }
          if (active_is_action(TuiAction::PaletteNext) && file_reference_palette_active())
          {
            pending_escape_clear = false;
            auto const match_count = completion_match_count();
            if (match_count == 0)
            {
              snapshot.status = "no matching file references";
            }
            else
            {
              selected_slash_command_index = next_completion(selected_slash_command_index);
              snapshot.status = "reference " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
            }
            return render();
          }
          if (active_is_action(TuiAction::PaletteNext) && path_completion_palette_active())
          {
            pending_escape_clear = false;
            auto const match_count = completion_match_count();
            if (match_count == 0)
            {
              snapshot.status = "no matching paths";
            }
            else
            {
              selected_slash_command_index = next_completion(selected_slash_command_index);
              snapshot.status = "path " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
            }
            return render();
          }
          if (active_is_action(TuiAction::HistoryNext))
          {
            pending_escape_clear = false;
            draft_state.clear_selection();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            if (browse_composer_input_history(draft, input_history, history_index, draft_input, false))
            {
              snapshot.status = history_index ? "history next" : "history draft";
            }
            else
            {
              scroll_down(kKeyboardScrollRows);
            }
            return render();
          }
          if (active_event.key == Key::ArrowDown)
          {
            scroll_down(kKeyboardScrollRows);
            return render();
          }
          if (active_is_action(TuiAction::CursorDown) && apply_composer_draft_action(draft, TuiAction::CursorDown))
          {
            pending_escape_clear = false;
            draft_state.clear_selection();
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            return render();
          }
          if (active_event.key == Key::MouseLeftClick)
          {
            if (auto const clicked = slash_palette_selection_for_screen_position(snapshot, active_event.mouse_row, active_event.mouse_column))
            {
              selected_slash_command_index = *clicked;
              if (auto const disabled_reason = slash_command_selection_disabled_reason(draft.text, draft.cursor, snapshot.slash_commands, *clicked))
              {
                snapshot.status = "command disabled: " + *disabled_reason;
                static_cast<void>(beep());
              }
              else
              {
                auto selection = slash_command_selection_text(draft.text, draft.cursor, snapshot.slash_commands, *clicked);
                draft_state.clear_selection();
                static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
                selected_slash_command_index = 0;
                snapshot.status = "command selected - press Enter to queue";
              }
              return render();
            }
            if (auto const clicked = detail::file_reference_palette_selection_for_screen_position_cached(
                    snapshot, active_event.mouse_row, active_event.mouse_column, completion_cache, snapshot.file_references_generation))
            {
              selected_slash_command_index = *clicked;
              if (auto const disabled_reason = selected_completion_disabled_reason(*clicked))
              {
                snapshot.status = "reference disabled: " + *disabled_reason;
                static_cast<void>(beep());
              }
              else
              {
                auto selection = selected_completion_text(*clicked);
                draft_state.clear_selection();
                static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
                selected_slash_command_index = 0;
                snapshot.status = "file reference selected";
              }
              return render();
            }
            if (auto const clicked = detail::path_completion_palette_selection_for_screen_position_cached(
                    snapshot, active_event.mouse_row, active_event.mouse_column, completion_cache, snapshot.file_references_generation))
            {
              selected_slash_command_index = *clicked;
              if (auto const disabled_reason = selected_completion_disabled_reason(*clicked))
              {
                snapshot.status = "path disabled: " + *disabled_reason;
                static_cast<void>(beep());
              }
              else
              {
                auto selection = selected_completion_text(*clicked);
                draft_state.clear_selection();
                static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
                selected_slash_command_index = 0;
                path_completion_force_active = false;
                snapshot.status = "path selected";
              }
              return render();
            }
            if (auto const tool_index = detail::transcript_tool_card_header_for_screen_position(snapshot, active_event.mouse_row, active_event.mouse_column))
            {
              if (toggle_tool_details_at(*tool_index))
                return render();
            }
            if (auto const cursor = composer_input_cursor_for_screen_position(snapshot, active_event.mouse_row, active_event.mouse_column))
            {
              draft.cursor = clamp_composer_draft_cursor_to_atomic_boundary(draft, *cursor);
              draft_state.clear_selection();
              snapshot.status = "cursor moved";
              return render();
            }
          }
          if (active_event.key == Key::MouseWheelUp)
          {
            scroll_up(kMouseWheelScrollRows);
            return render();
          }
          if (active_event.key == Key::MouseWheelDown)
          {
            scroll_down(kMouseWheelScrollRows);
            return render();
          }
          if (active_is_action(TuiAction::DetailsToggle))
          {
            snapshot.tool_details_visible = !snapshot.tool_details_visible;
            snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
            return render();
          }
          if (active_is_action(TuiAction::VariantCycle))
          {
            snapshot.status = "reasoning can be changed between turns";
            return render();
          }
          if (active_is_action(TuiAction::ThinkingToggle))
          {
            toggle_thinking_visibility();
            return render();
          }
          if (active_is_action(TuiAction::ModelSelect))
          {
            snapshot.status = "model selection is available between turns";
            return render();
          }
          if (active_is_action(TuiAction::ModelCycleForward) || active_is_action(TuiAction::ModelCycleBackward))
          {
            snapshot.status = "model cycling is available between turns";
            return render();
          }
          if (active_is_action(TuiAction::MessageDequeue))
          {
            snapshot.status = "queued-message restore is available during active runs";
            return render();
          }
          if (active_event.key == Key::Space)
          {
            insert_active_text();
            return render();
          }
          return true;
        };
        bool render_failed = false;
        auto last_processing_indicator_tick = std::chrono::steady_clock::now();
        while (submit_future.wait_for(detail::kProcessingIndicatorFrameDelay) != std::future_status::ready)
        {
          auto const now = std::chrono::steady_clock::now();
          auto const elapsed = now - last_processing_indicator_tick;
          auto const elapsed_frames = detail::processing_indicator_elapsed_frames(elapsed);
          if (elapsed_frames > 0)
          {
            std::lock_guard<std::recursive_mutex> lock(ui_mutex);
            if (snapshot.processing)
              snapshot.spinner_frame += elapsed_frames;
            last_processing_indicator_tick += detail::kProcessingIndicatorFrameDelay * elapsed_frames;
          }

          if (terminal_signal_received())
          {
            if (terminal_signal_number() == SIGINT && !draft.text.empty())
            {
              clear_terminal_signal();
              static_cast<void>(clear_draft_for_interrupt());
              snapshot.selected_slash_command_index = selected_slash_command_index;
              if (!render())
              {
                terminal_write_failed = true;
                render_failed = true;
                fail_pending_prompt_requests();
                break;
              }
              continue;
            }
            request_close_after_submit();
            break;
          }
          if (service_pending_prompt_request(cancel_requested, request_stop))
          {
            auto const drain_result = drain_runtime_events();
            if (drain_result == RuntimeEventDrainResult::RenderFailed)
            {
              terminal_write_failed = true;
              render_failed = true;
              fail_pending_prompt_requests();
              break;
            }
            continue;
          }
          if (auto active_input = poll_curses_input())
          {
            if (!handle_active_input(*active_input))
            {
              terminal_write_failed = true;
              render_failed = true;
              fail_pending_prompt_requests();
              break;
            }
            if (close_after_submit)
              break;
          }
          static_cast<void>(service_pending_prompt_request(cancel_requested, request_stop));
          auto const drain_result = drain_runtime_events();
          if (drain_result == RuntimeEventDrainResult::RenderFailed)
          {
            terminal_write_failed = true;
            render_failed = true;
            fail_pending_prompt_requests();
            break;
          }
          if (drain_result == RuntimeEventDrainResult::Rendered || transcript_scroll_offset > 0)
            continue;
          if (!maybe_reload_display_settings())
          {
            terminal_write_failed = true;
            render_failed = true;
            fail_pending_prompt_requests();
            break;
          }
          if (!renderer.render_processing_frame())
          {
            terminal_write_failed = true;
            render_failed = true;
            fail_pending_prompt_requests();
            break;
          }
        }
        result = submit_future.get();
        if (result.state_snapshot)
        {
          // Submit workers own ShellState. Apply their authoritative snapshot
          // on the TUI thread before another prompt can consult UI-local
          // session grants or attachments.
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          apply_runtime_state_snapshot(std::move(*result.state_snapshot));
        }
        if (active_queues && active_queues->finish)
        {
          if (auto finished = active_queues->finish(run_cancel_requested.load()); !finished)
          {
            {
              std::lock_guard<std::recursive_mutex> lock(ui_mutex);
              snapshot.status = finished.error().format();
            }
            render_failed = true;
          }
        }
        if (drain_runtime_events() == RuntimeEventDrainResult::RenderFailed)
        {
          terminal_write_failed = true;
          render_failed = true;
        }
        {
          std::lock_guard<std::mutex> lock(prompt_audit_mutex);
          prompt_audit_sink = nullptr;
        }
        if (render_failed)
          return InputLoopAction::BreakLoop;
      }
      {
        std::lock_guard<std::mutex> lock(prompt_audit_mutex);
        prompt_audit_sink = nullptr;
      }
      if (terminal_signal_received())
        return InputLoopAction::BreakLoop;
      auto const events_received = event_queue.received_any();
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        settle_turn_activity();
      }
      if (!events_received)
      {
        auto const turn_elapsed = std::chrono::steady_clock::now() - turn_started_at;
        auto const assistant_meta = assistant_meta_for_snapshot(snapshot, turn_elapsed);
        if (!is_command_submission)
        {
          push_transcript(snapshot, TranscriptItem{.label = "you", .text = submitted});
        }
        if (run_cancel_requested.load())
        {
          push_transcript(snapshot, TranscriptItem{.label = "ava", .text = "stopped by user", .meta = assistant_meta});
        }
        else
        {
          for (auto const& tool : result.tool_timeline)
          {
            push_transcript(snapshot, TranscriptItem{.tool = tool});
          }
          auto const bounded_bash_output_is_in_card =
              std::ranges::any_of(result.tool_timeline, [](auto const& tool) { return tool.name == "bash" && tool.truncated && !tool.spill_path.empty(); });
          if (!should_show_slash_command_output_as_status(submitted) && !bounded_bash_output_is_in_card)
          {
            for (auto const& output : result.output)
            {
              push_transcript(snapshot, TranscriptItem{.label = "ava", .text = output, .meta = assistant_meta});
            }
          }
        }
      }
      if (!events_received)
        transcript_scroll_offset = 0;
      auto const show_command_status =
          !events_received && !run_cancel_requested.load() && should_show_slash_command_output_as_status(submitted) && !result.output.empty();
      snapshot.status = show_command_status ? result.output.back()
                                            : (events_received ? (event_state.run_status == TuiEventRunStatus::Error      ? "error"
                                                                  : event_state.run_status == TuiEventRunStatus::Canceled ? "stopped"
                                                                                                                          : "done")
                                                               : (result.output.empty() ? "ok" : "done"));
      snapshot.processing = false;
      if (result.context_source_count)
      {
        snapshot.context_source_count = result.context_source_count;
        sidebar.context_source_count = result.context_source_count;
      }
      refresh_token_status();
      if (!render())
      {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      if (close_after_submit)
        return InputLoopAction::BreakLoop;
      if (result.quit)
        return InputLoopAction::BreakLoop;
      return InputLoopAction::ContinueLoop;
    }
    return InputLoopAction::None;
  };

  if (terminal_signal_received())
    return 130;
  if (!render())
    return 1;

  while (true)
  {
    auto const maybe_input = read_curses_input_with_timeout(kIdleInputPollDelay);
    if (!maybe_input)
    {
      if (!maybe_reload_display_settings())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    auto const input = *maybe_input;
    if (terminal_signal_received())
    {
      if (terminal_signal_number() == SIGINT && !draft.text.empty())
      {
        clear_terminal_signal();
        static_cast<void>(clear_draft_for_interrupt());
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      break;
    }
    if (input.resize)
    {
      if (!render())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    clear_reasoning_feedback_for_user_input(snapshot);
    if (snapshot.select_list)
    {
      auto input_result = [&]() {
        if (input.event.key == Key::MouseLeftClick)
        {
          if (auto const clicked = select_list_selection_for_screen_position(snapshot, input.event.mouse_row, input.event.mouse_column))
          {
            auto action = SelectListInputAction::Resolve;
            if (*clicked >= snapshot.select_list->items.size() || !snapshot.select_list->items[*clicked].enabled)
            {
              action = SelectListInputAction::Redraw;
            }
            return SelectListInputResult{.selected_item_index = *clicked, .query = snapshot.select_list->query, .action = action};
          }
        }
        if (active_select_list == ActiveSelectList::ScopedModels)
        {
          auto const scoped_action = [&](TuiAction action) { return key_matches_action(options.key_bindings, action, input.event.key); };
          if (scoped_action(TuiAction::ModelsSave))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsSave};
          }
          if (scoped_action(TuiAction::ModelsEnableAll))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsEnableAll};
          }
          if (scoped_action(TuiAction::ModelsClearAll))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsClearAll};
          }
          if (scoped_action(TuiAction::ModelsToggleProvider))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsToggleProvider};
          }
          if (scoped_action(TuiAction::ModelsReorderUp))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsReorderUp};
          }
          if (scoped_action(TuiAction::ModelsReorderDown))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsReorderDown};
          }
        }
        return handle_select_list_input(*snapshot.select_list, input.event, options.key_bindings);
      }();
      auto preserve_session_selector_state = [&](SelectListView next_view, std::string status) {
        session_archive_confirmation.reset();
        auto const query = input_result.query;
        std::string selected_value;
        if (input_result.selected_item_index < snapshot.select_list->items.size())
        {
          selected_value = snapshot.select_list->items[input_result.selected_item_index].value;
        }
        next_view.query = query;
        if (!selected_value.empty())
        {
          for (std::size_t index = 0; index < next_view.items.size(); ++index)
          {
            if (next_view.items[index].value == selected_value)
            {
              next_view.selected_item_index = index;
              break;
            }
          }
        }
        next_view.selected_item_index = clamp_select_list_selection(next_view, next_view.selected_item_index);
        snapshot.select_list = std::move(next_view);
        snapshot.status = std::move(status);
      };
      auto selected_select_list_item = [&]() -> SelectListItemView const* {
        if (!snapshot.select_list || input_result.selected_item_index >= snapshot.select_list->items.size())
          return nullptr;
        return &snapshot.select_list->items[input_result.selected_item_index];
      };
      auto visible_select_list_values = [&]() {
        std::vector<std::string> values;
        if (!snapshot.select_list)
          return values;
        auto current = *snapshot.select_list;
        current.query = input_result.query;
        current.selected_item_index = input_result.selected_item_index;
        for (auto const index : filter_select_list_items(current))
        {
          if (index < current.items.size() && current.items[index].enabled && !current.items[index].value.empty())
          {
            values.push_back(current.items[index].value);
          }
        }
        return values;
      };
      auto apply_opened_session_snapshot = [&](TuiRuntimeStateSnapshot state, bool announce) {
        auto status = state.status;
        snapshot.transcript.clear();
        ++snapshot.transcript_generation;
        draft_state.clear_selection();
        reset_composer_draft(draft);
        jump_mode = ComposerJumpMode::None;
        draft_input.clear();
        history_index.reset();
        apply_runtime_state_snapshot(std::move(state));
        if (announce && !status.empty())
        {
          push_transcript(snapshot, TranscriptItem{.label = "ava", .text = std::move(status), .meta = assistant_meta_for_snapshot(snapshot)});
        }
        transcript_scroll_offset = 0;
        draft_scroll_offset = 0;
      };
      if (input_result.action == SelectListInputAction::Redraw && snapshot.select_list)
      {
        session_archive_confirmation.reset();
        snapshot.select_list->selected_item_index = input_result.selected_item_index;
        snapshot.select_list->query = std::move(input_result.query);
      }
      else if (input_result.action == SelectListInputAction::Resolve && active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        auto const* selected_item = selected_select_list_item();
        if (!selected_item || !selected_item->enabled || selected_item->value.empty())
        {
          snapshot.status = "scoped model cannot be toggled from this row";
          static_cast<void>(beep());
        }
        else if (!options.on_scoped_model_toggled)
        {
          snapshot.status = "scoped model toggle unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated = options.on_scoped_model_toggled(*snapshot.select_list, selected_item->value);
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped model toggled");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::ModelsEnableAll && active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        if (!options.on_scoped_model_enable_all)
        {
          snapshot.status = "scoped model enable-all unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated = options.on_scoped_model_enable_all(*snapshot.select_list, visible_select_list_values());
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped models enabled");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::ModelsClearAll && active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        if (!options.on_scoped_model_clear_all)
        {
          snapshot.status = "scoped model clear-all unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated = options.on_scoped_model_clear_all(*snapshot.select_list, visible_select_list_values());
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped models cleared");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::ModelsToggleProvider && active_select_list == ActiveSelectList::ScopedModels &&
               snapshot.select_list)
      {
        auto const* selected_item = selected_select_list_item();
        if (!selected_item || selected_item->value.empty())
        {
          snapshot.status = "provider toggle unavailable from this row";
          static_cast<void>(beep());
        }
        else if (!options.on_scoped_model_toggle_provider)
        {
          snapshot.status = "provider toggle unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated = options.on_scoped_model_toggle_provider(*snapshot.select_list, selected_item->value);
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped provider toggled");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if ((input_result.action == SelectListInputAction::ModelsReorderUp || input_result.action == SelectListInputAction::ModelsReorderDown) &&
               active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        auto const* selected_item = selected_select_list_item();
        if (!selected_item || selected_item->value.empty())
        {
          snapshot.status = "scoped model reorder unavailable from this row";
          static_cast<void>(beep());
        }
        else if (!options.on_scoped_model_reorder)
        {
          snapshot.status = "scoped model reorder unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated =
              options.on_scoped_model_reorder(*snapshot.select_list, selected_item->value, input_result.action == SelectListInputAction::ModelsReorderUp);
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped model order updated");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::ModelsSave && active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        if (!options.on_scoped_model_save)
        {
          snapshot.status = "scoped model save unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto saved = options.on_scoped_model_save();
          if (saved)
          {
            snapshot.status = *saved;
          }
          else
          {
            snapshot.status = saved.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::CycleSort && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_sort_cycle)
      {
        preserve_session_selector_state(options.on_session_selector_sort_cycle(), "session selector sort cycled");
      }
      else if (input_result.action == SelectListInputAction::ToggleNamedFilter && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_named_filter_toggle)
      {
        preserve_session_selector_state(options.on_session_selector_named_filter_toggle(), "session selector filter toggled");
      }
      else if (input_result.action == SelectListInputAction::TogglePathDisplay && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_path_display_toggle)
      {
        preserve_session_selector_state(options.on_session_selector_path_display_toggle(), "session selector path display toggled");
      }
      else if (input_result.action == SelectListInputAction::ToggleArchivedFilter && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_archived_filter_toggle)
      {
        preserve_session_selector_state(options.on_session_selector_archived_filter_toggle(), "session selector archived filter toggled");
      }
      else if (input_result.action == SelectListInputAction::ToggleLabelTimestamp && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_label_timestamp_toggle)
      {
        preserve_session_selector_state(options.on_session_selector_label_timestamp_toggle(), "session selector label timestamps toggled");
      }
      else if (input_result.action == SelectListInputAction::Rename && active_select_list == ActiveSelectList::Session && snapshot.select_list)
      {
        if (input_result.selected_item_index < snapshot.select_list->items.size() && snapshot.select_list->items[input_result.selected_item_index].enabled &&
            !snapshot.select_list->items[input_result.selected_item_index].value.empty())
        {
          auto const selected_value = snapshot.select_list->items[input_result.selected_item_index].value;
          snapshot.select_list.reset();
          active_select_list = ActiveSelectList::None;
          auto draft_text = "/sessions rename " + selected_value + " ";
          draft_state.clear_selection();
          static_cast<void>(replace_composer_draft(draft, std::move(draft_text)));
          draft_input.clear();
          history_index.reset();
          draft_scroll_offset = 0;
          snapshot.status = "session rename draft ready";
        }
        else
        {
          snapshot.status = "session cannot be renamed from this row";
          static_cast<void>(beep());
        }
      }
      else if (input_result.action == SelectListInputAction::Label && active_select_list == ActiveSelectList::Session && snapshot.select_list)
      {
        if (input_result.selected_item_index < snapshot.select_list->items.size() && snapshot.select_list->items[input_result.selected_item_index].enabled &&
            !snapshot.select_list->items[input_result.selected_item_index].value.empty())
        {
          auto const selected_value = snapshot.select_list->items[input_result.selected_item_index].value;
          snapshot.select_list.reset();
          active_select_list = ActiveSelectList::None;
          auto draft_text = "/sessions labels " + selected_value + " ";
          draft_state.clear_selection();
          static_cast<void>(replace_composer_draft(draft, std::move(draft_text)));
          draft_input.clear();
          history_index.reset();
          draft_scroll_offset = 0;
          snapshot.status = "session labels draft ready";
        }
        else
        {
          snapshot.status = "session cannot be labeled from this row";
          static_cast<void>(beep());
        }
      }
      else if ((input_result.action == SelectListInputAction::BranchParent || input_result.action == SelectListInputAction::BranchChild) &&
               active_select_list == ActiveSelectList::Session && snapshot.select_list)
      {
        session_archive_confirmation.reset();
        auto const* selected_item = selected_select_list_item();
        if (!selected_item || !selected_item->enabled || selected_item->value.empty())
        {
          snapshot.status = "session branch navigation unavailable from this row";
          static_cast<void>(beep());
        }
        else if (input_result.action == SelectListInputAction::BranchParent && !options.on_session_selector_branch_parent)
        {
          snapshot.status = "session parent navigation unavailable";
          static_cast<void>(beep());
        }
        else if (input_result.action == SelectListInputAction::BranchChild && !options.on_session_selector_branch_child)
        {
          snapshot.status = "session child navigation unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto const selected_value = selected_item->value;
          auto opened = dispatch_tui_selector_authority(snapshot, "opening session…", render, [&]() {
            return input_result.action == SelectListInputAction::BranchParent ? options.on_session_selector_branch_parent(selected_value)
                                                                              : options.on_session_selector_branch_child(selected_value);
          });
          if (opened)
          {
            snapshot.select_list.reset();
            active_select_list = ActiveSelectList::None;
            apply_opened_session_snapshot(std::move(*opened), true);
          }
          else
          {
            snapshot.status = opened.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::Archive || input_result.action == SelectListInputAction::ArchiveNoninvasive)
      {
        bool const noninvasive_archive = input_result.action == SelectListInputAction::ArchiveNoninvasive;
        auto const* selected_item = selected_select_list_item();
        if (active_select_list != ActiveSelectList::Session || !snapshot.select_list)
        {
          snapshot.select_list.reset();
          active_select_list = ActiveSelectList::None;
          session_archive_confirmation.reset();
          snapshot.status = "view canceled";
        }
        else if (!selected_item || !selected_item->enabled || selected_item->value.empty())
        {
          session_archive_confirmation.reset();
          snapshot.status = "session cannot be archived or restored from this row";
          static_cast<void>(beep());
        }
        else
        {
          bool const archive = selected_item->badge != "archived";
          if (archive && selected_item->current)
          {
            session_archive_confirmation.reset();
            snapshot.status = "switch sessions before archiving the active session";
            static_cast<void>(beep());
          }
          else if (archive && !options.on_session_selector_archive)
          {
            session_archive_confirmation.reset();
            snapshot.status = "session archive unavailable";
            static_cast<void>(beep());
          }
          else if (!archive && !options.on_session_selector_unarchive)
          {
            session_archive_confirmation.reset();
            snapshot.status = "session restore unavailable";
            static_cast<void>(beep());
          }
          else if (session_archive_confirmation && session_archive_confirmation->session_id == selected_item->value &&
                   session_archive_confirmation->archive == archive)
          {
            auto updated = archive ? options.on_session_selector_archive(selected_item->value) : options.on_session_selector_unarchive(selected_item->value);
            session_archive_confirmation.reset();
            if (updated)
            {
              preserve_session_selector_state(std::move(*updated), archive ? "session archived" : "session restored");
            }
            else
            {
              snapshot.status = updated.error().format();
              static_cast<void>(beep());
            }
          }
          else
          {
            session_archive_confirmation = PendingSessionArchiveAction{.session_id = selected_item->value, .archive = archive};
            snapshot.status = std::string("press ") + (noninvasive_archive ? "Ctrl+Backspace" : "Ctrl+D") + " again to " + (archive ? "archive " : "restore ") +
                              selected_item->value;
          }
        }
      }
      else if (input_result.action == SelectListInputAction::Resolve || input_result.action == SelectListInputAction::Cancel)
      {
        std::string selected_value;
        if (input_result.action == SelectListInputAction::Resolve && snapshot.select_list &&
            input_result.selected_item_index < snapshot.select_list->items.size())
        {
          selected_value = snapshot.select_list->items[input_result.selected_item_index].value;
        }
        auto const resolved_list = active_select_list;
        snapshot.select_list.reset();
        active_select_list = ActiveSelectList::None;
        session_archive_confirmation.reset();
        if (input_result.action == SelectListInputAction::Cancel)
        {
          snapshot.status = "view canceled";
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsOpenModels)
        {
          if (!open_model_selector())
          {
            push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
            transcript_scroll_offset = 0;
          }
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsOpenScopedModels)
        {
          if (!open_scoped_model_selector())
          {
            push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
            transcript_scroll_offset = 0;
          }
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsOpenKeybindings)
        {
          snapshot.select_list = hotkeys_select_list_view(options.key_bindings);
          active_select_list = ActiveSelectList::Hotkeys;
          snapshot.status = "keybindings opened";
          transcript_scroll_offset = 0;
        }
        else if (resolved_list == ActiveSelectList::Hotkeys && !selected_value.empty())
        {
          auto draft_command = std::string("/keybindings set ") + selected_value + " ";
          draft_state.clear_selection();
          static_cast<void>(replace_composer_draft(draft, std::move(draft_command)));
          selected_slash_command_index = 0;
          path_completion_force_active = false;
          draft_scroll_offset = 0;
          history_index.reset();
          draft_input.clear();
          snapshot.status = "keybinding edit command drafted";
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsEditKeybindings)
        {
          draft_state.clear_selection();
          static_cast<void>(replace_composer_draft(draft, "/keybindings set "));
          selected_slash_command_index = 0;
          path_completion_force_active = false;
          draft_scroll_offset = 0;
          history_index.reset();
          draft_input.clear();
          snapshot.status = "keybinding edit command drafted";
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsReloadKeybindings)
        {
          if (!options.on_reload_key_bindings)
          {
            snapshot.status = "reload unavailable";
            push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
            transcript_scroll_offset = 0;
            static_cast<void>(beep());
          }
          else
          {
            auto reloaded = options.on_reload_key_bindings();
            if (!reloaded)
            {
              snapshot.status = reloaded.error().format();
              push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
              transcript_scroll_offset = 0;
              static_cast<void>(beep());
            }
            else
            {
              options.key_bindings = std::move(reloaded->key_bindings);
              snapshot.active_run_hint = active_run_hint_for(options.key_bindings);
              apply_runtime_state_snapshot(std::move(reloaded->state));
              push_transcript(snapshot, TranscriptItem{.label = "ava", .text = "keybindings reloaded", .meta = assistant_meta_for_snapshot(snapshot)});
              transcript_scroll_offset = 0;
            }
          }
        }
        else if (resolved_list == ActiveSelectList::Model && options.on_model_selected)
        {
          auto selected = dispatch_tui_selector_authority(snapshot, "switching model…", render, [&]() { return options.on_model_selected(selected_value); });
          if (selected)
          {
            apply_runtime_state_snapshot(std::move(*selected));
          }
          else
          {
            snapshot.status = selected.error().format();
            static_cast<void>(beep());
          }
        }
        else if (resolved_list == ActiveSelectList::Session && options.on_session_selected)
        {
          auto selected = dispatch_tui_selector_authority(snapshot, "opening session…", render, [&]() { return options.on_session_selected(selected_value); });
          if (selected)
          {
            apply_opened_session_snapshot(std::move(*selected), false);
          }
          else
          {
            snapshot.status = selected.error().format();
            static_cast<void>(beep());
          }
        }
        else if (resolved_list == ActiveSelectList::Settings && options.on_settings_selected)
        {
          auto selected = options.on_settings_selected(selected_value);
          if (selected)
          {
            auto status = selected->status;
            apply_runtime_state_snapshot(std::move(*selected));
            if (!status.empty())
            {
              push_transcript(snapshot, TranscriptItem{.label = "ava", .text = std::move(status), .meta = assistant_meta_for_snapshot(snapshot)});
              transcript_scroll_offset = 0;
            }
          }
          else
          {
            snapshot.status = selected.error().format();
            static_cast<void>(beep());
          }
        }
        else
        {
          snapshot.status = "view closed";
        }
      }
      if (!render())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    auto const event = input.event;
    if (auto handled = handle_sidebar_drawer_input(event))
    {
      if (!*handled)
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    auto is_action = [&](TuiAction action) { return key_matches_action(options.key_bindings, action, event.key); };
    auto select_slash_command = [&]() {
      selected_slash_command_index = clamp_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
      if (auto const disabled_reason = slash_command_selection_disabled_reason(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index))
      {
        snapshot.status = "command disabled: " + *disabled_reason;
        static_cast<void>(beep());
        return;
      }
      draft_state.clear_selection();
      auto selection = slash_command_selection_text(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "command selected - press Enter to run";
    };
    auto select_file_reference = [&]() {
      selected_slash_command_index = clamp_completion(selected_slash_command_index);
      if (auto const disabled_reason = selected_completion_disabled_reason(selected_slash_command_index))
      {
        snapshot.status = "reference disabled: " + *disabled_reason;
        static_cast<void>(beep());
        return;
      }
      auto selection = selected_completion_text(selected_slash_command_index);
      draft_state.clear_selection();
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "file reference selected";
    };
    auto select_path_completion = [&]() {
      selected_slash_command_index = clamp_completion(selected_slash_command_index);
      if (auto const disabled_reason = selected_completion_disabled_reason(selected_slash_command_index))
      {
        snapshot.status = "path disabled: " + *disabled_reason;
        static_cast<void>(beep());
        return;
      }
      auto selection = selected_completion_text(selected_slash_command_index);
      draft_state.clear_selection();
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "path selected";
    };
    auto force_path_completion = [&]() {
      auto const was_suppressed = slash_palette_suppressed;
      slash_palette_suppressed = false;
      path_completion_force_active = true;
      auto const match_count = completion_match_count();
      if (match_count == 0)
      {
        slash_palette_suppressed = was_suppressed;
        path_completion_force_active = false;
        return false;
      }
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      if (match_count == 1)
      {
        if (auto const disabled_reason = selected_completion_disabled_reason(0))
        {
          path_completion_force_active = false;
          snapshot.status = "path disabled: " + *disabled_reason;
          static_cast<void>(beep());
          return true;
        }
        auto selection = selected_completion_text(0);
        draft_state.clear_selection();
        static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
        path_completion_force_active = false;
        draft_scroll_offset = 0;
        snapshot.status = "path selected";
        return true;
      }
      draft_scroll_offset = 0;
      snapshot.status = "path suggestions";
      return true;
    };
    if (jump_mode != ComposerJumpMode::None)
    {
      if (is_action(TuiAction::JumpForward) || is_action(TuiAction::JumpBackward))
      {
        jump_mode = ComposerJumpMode::None;
        snapshot.status = "jump cancelled";
      }
      else if (auto const target = printable_jump_target(input))
      {
        bool const forward = jump_mode == ComposerJumpMode::Forward;
        jump_mode = ComposerJumpMode::None;
        pending_escape_clear = false;
        history_index.reset();
        draft_input.clear();
        selected_slash_command_index = 0;
        slash_palette_suppressed = false;
        path_completion_force_active = false;
        draft_scroll_offset = 0;
        draft_state.clear_selection();
        snapshot.status =
            jump_composer_draft_to_character(draft, *target, forward) ? (forward ? "jumped forward" : "jumped backward") : "jump character not found";
      }
      else
      {
        jump_mode = ComposerJumpMode::None;
      }
      if (event.key == Key::Character || event.key == Key::Space || is_action(TuiAction::JumpForward) || is_action(TuiAction::JumpBackward))
      {
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
    }
    bool const ctrl_d_delete_forward = event.key == Key::CtrlD && is_action(TuiAction::DeleteForward) && !draft.text.empty();
    bool const delete_forward_action = is_action(TuiAction::DeleteForward) && (event.key != Key::CtrlD || ctrl_d_delete_forward);

    auto insert_input_text = [&]() {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      auto const text = input.text.empty() ? std::string(1, event.character) : input.text;
      if (input.bracketed_paste)
      {
        static_cast<void>(draft_state.delete_selection());
        if (insert_composer_paste_text(draft, text))
          snapshot.status = "pasted into draft safely";
      }
      else if (!draft_state.replace_selection(text))
      {
        static_cast<void>(insert_composer_draft_text(draft, text));
      }
    };
    if (event.key == Key::Character)
    {
      insert_input_text();
    }
    else if (is_action(TuiAction::MessageFollowUp))
    {
      auto const action = handle_submit();
      if (action == InputLoopAction::BreakLoop)
        break;
      if (action == InputLoopAction::ContinueLoop)
        continue;
    }
    else if (is_action(TuiAction::NewLine))
    {
      draft_state.insert_newline();
    }
    else if (is_action(TuiAction::ExternalEditor))
    {
      if (!open_external_editor())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    else if (is_action(TuiAction::Suspend))
    {
      if (!suspend_to_background())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    else if (is_action(TuiAction::ClipboardPasteImage))
    {
      if (!paste_clipboard_image())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    else if (is_action(TuiAction::JumpForward) || is_action(TuiAction::JumpBackward))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      path_completion_force_active = false;
      jump_mode = is_action(TuiAction::JumpForward) ? ComposerJumpMode::Forward : ComposerJumpMode::Backward;
      snapshot.status = is_action(TuiAction::JumpForward) ? "jump forward: type character" : "jump backward: type character";
    }
    else if (is_action(TuiAction::DeleteBackward))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!draft_state.delete_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteBackward));
    }
    else if (delete_forward_action)
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!draft_state.delete_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteForward));
    }
    else if (is_action(TuiAction::DeleteWordBackward))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!draft_state.delete_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordBackward));
    }
    else if (is_action(TuiAction::DeleteWordForward))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!draft_state.delete_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordForward));
    }
    else if (is_action(TuiAction::DeleteToLineStart))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!draft_state.delete_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineStart));
    }
    else if (is_action(TuiAction::DeleteToLineEnd))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!draft_state.delete_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineEnd));
    }
    else if (is_action(TuiAction::CopySelection) && draft_state.selection_bounds())
    {
      pending_escape_clear = false;
      path_completion_force_active = false;
      static_cast<void>(draft_state.copy_selection(snapshot));
    }
    else if (is_action(TuiAction::ClearInput) && (!draft.text.empty() || !is_action(TuiAction::Interrupt)))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      draft_state.clear_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::ClearInput) ? "input cleared" : "input already empty";
    }
    else if (is_action(TuiAction::AutocompleteAccept) && slash_palette_active())
    {
      pending_escape_clear = false;
      select_slash_command();
    }
    else if (is_action(TuiAction::AutocompleteAccept) && file_reference_palette_active())
    {
      pending_escape_clear = false;
      select_file_reference();
    }
    else if (is_action(TuiAction::AutocompleteAccept) && path_completion_palette_active())
    {
      pending_escape_clear = false;
      select_path_completion();
    }
    else if (is_action(TuiAction::AutocompleteAccept) && force_path_completion())
    {
      pending_escape_clear = false;
    }
    else if (is_action(TuiAction::ModeToggle))
    {
      pending_escape_clear = false;
      path_completion_force_active = false;
      if (!options.on_toggle_mode)
      {
        snapshot.status = "mode toggle unavailable";
      }
      else if (auto result = options.on_toggle_mode(); !result)
      {
        snapshot.status = result.error().format();
      }
      else
      {
        snapshot.mode = *result;
        snapshot.status = "mode switched to " + snapshot.mode;
      }
    }
    else if (is_action(TuiAction::Interrupt))
    {
      path_completion_force_active = false;
      if (!draft.text.empty())
      {
        static_cast<void>(clear_draft_for_interrupt());
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      break;
    }
    else if (is_action(TuiAction::Exit))
    {
      break;
    }
    else if (is_action(TuiAction::VariantCycle))
    {
      pending_escape_clear = false;
      cycle_reasoning();
    }
    else if (is_action(TuiAction::ThinkingToggle))
    {
      pending_escape_clear = false;
      toggle_thinking_visibility();
    }
    else if (is_action(TuiAction::ModelSelect))
    {
      if (!open_model_selector())
      {
        terminal_write_failed = true;
        break;
      }
    }
    else if (is_action(TuiAction::ModelCycleForward))
    {
      pending_escape_clear = false;
      cycle_model(true);
    }
    else if (is_action(TuiAction::ModelCycleBackward))
    {
      pending_escape_clear = false;
      cycle_model(false);
    }
    else if (is_action(TuiAction::SessionResume) || is_action(TuiAction::SessionTree))
    {
      if (!open_session_selector())
      {
        terminal_write_failed = true;
        break;
      }
    }
    else if (is_action(TuiAction::SessionNew) || is_action(TuiAction::SessionFork))
    {
      forced_slash_submission = is_action(TuiAction::SessionNew) ? "/new" : "/fork";
      auto const action = handle_submit();
      if (action == InputLoopAction::BreakLoop)
        break;
      if (action == InputLoopAction::ContinueLoop)
        continue;
    }
    else if (is_action(TuiAction::MessageDequeue))
    {
      pending_escape_clear = false;
      snapshot.status = "queued-message restore is available during active runs";
    }
    else if (is_action(TuiAction::PageUp))
    {
      auto const [_, height] = terminal_size();
      scroll_up(std::max<std::size_t>(1, height / 2));
    }
    else if (is_action(TuiAction::PageDown))
    {
      auto const [_, height] = terminal_size();
      scroll_down(std::max<std::size_t>(1, height / 2));
    }
    else if (is_action(TuiAction::MessagePrev))
    {
      scroll_to_message_boundary(true);
    }
    else if (is_action(TuiAction::MessageNext))
    {
      scroll_to_message_boundary(false);
    }
    else if (is_action(TuiAction::JumpToBottom))
    {
      jump_to_bottom("live tail");
    }
    else if (event.key == Key::MouseWheelUp)
    {
      scroll_up(kMouseWheelScrollRows);
    }
    else if (event.key == Key::MouseWheelDown)
    {
      scroll_down(kMouseWheelScrollRows);
    }
    else if (event.key == Key::MouseLeftClick)
    {
      pending_escape_clear = false;
      if (auto const clicked = slash_palette_selection_for_screen_position(snapshot, event.mouse_row, event.mouse_column))
      {
        draft_state.clear_selection();
        selected_slash_command_index = *clicked;
        select_slash_command();
      }
      else if (auto const clicked = detail::file_reference_palette_selection_for_screen_position_cached(snapshot, event.mouse_row, event.mouse_column,
                                                                                                        completion_cache, snapshot.file_references_generation))
      {
        draft_state.clear_selection();
        selected_slash_command_index = *clicked;
        select_file_reference();
      }
      else if (auto const clicked = detail::path_completion_palette_selection_for_screen_position_cached(snapshot, event.mouse_row, event.mouse_column,
                                                                                                         completion_cache, snapshot.file_references_generation))
      {
        draft_state.clear_selection();
        selected_slash_command_index = *clicked;
        select_path_completion();
      }
      else if (auto const tool_index = detail::transcript_tool_card_header_for_screen_position(snapshot, event.mouse_row, event.mouse_column))
      {
        static_cast<void>(toggle_tool_details_at(*tool_index));
      }
      else if (auto const cursor = composer_input_cursor_for_screen_position(snapshot, event.mouse_row, event.mouse_column))
      {
        draft.cursor = clamp_composer_draft_cursor_to_atomic_boundary(draft, *cursor);
        draft_selection_anchor = draft.cursor;
        draft_selection_cursor = draft.cursor;
        draft.vertical_column = std::string::npos;
        draft.yank_start = std::string::npos;
        draft.yank_end = std::string::npos;
        history_index.reset();
        draft_input.clear();
        snapshot.status = "cursor moved";
      }
    }
    else if (event.key == Key::MouseLeftDrag || event.key == Key::MouseLeftRelease)
    {
      pending_escape_clear = false;
      if (auto const cursor = composer_input_cursor_for_screen_position(snapshot, event.mouse_row, event.mouse_column))
      {
        auto const next_cursor = clamp_composer_draft_cursor_to_atomic_boundary(draft, *cursor);
        if (draft_selection_anchor == std::string::npos)
          draft_selection_anchor = clamp_composer_draft_cursor_to_atomic_boundary(draft, draft.cursor);
        draft_selection_cursor = next_cursor;
        draft.cursor = next_cursor;
        draft.vertical_column = std::string::npos;
        draft.yank_start = std::string::npos;
        draft.yank_end = std::string::npos;
        history_index.reset();
        draft_input.clear();
        snapshot.status = draft_state.selection_bounds() ? "selection active" : "cursor moved";
      }
    }
    else if (draft_state.extend_selection_for_key(event.key, snapshot))
    {
      // Selection state was updated by the helper.
    }
    else if (event.key == Key::CtrlHome)
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      draft.cursor = 0;
      draft.vertical_column = std::string::npos;
      draft.yank_start = std::string::npos;
      draft.yank_end = std::string::npos;
    }
    else if (event.key == Key::CtrlEnd)
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      draft.cursor = draft.text.size();
      draft.vertical_column = std::string::npos;
      draft.yank_start = std::string::npos;
      draft.yank_end = std::string::npos;
    }
    else if (is_action(TuiAction::CursorLeft))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLeft));
    }
    else if (is_action(TuiAction::CursorRight))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorRight));
    }
    else if (is_action(TuiAction::CursorLineStart))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineStart));
    }
    else if (is_action(TuiAction::CursorLineEnd))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineEnd));
    }
    else if (is_action(TuiAction::CursorWordLeft))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordLeft));
    }
    else if (is_action(TuiAction::CursorWordRight))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordRight));
    }
    else if (is_action(TuiAction::PalettePrev) && slash_palette_active())
    {
      pending_escape_clear = false;
      auto const match_count = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands).size();
      if (match_count == 0)
      {
        snapshot.status = "no matching slash commands";
      }
      else
      {
        selected_slash_command_index = previous_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
        snapshot.status = "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
      }
    }
    else if (is_action(TuiAction::PalettePrev) && file_reference_palette_active())
    {
      pending_escape_clear = false;
      auto const match_count = completion_match_count();
      if (match_count == 0)
      {
        snapshot.status = "no matching file references";
      }
      else
      {
        selected_slash_command_index = previous_completion(selected_slash_command_index);
        snapshot.status = "reference " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
      }
    }
    else if (is_action(TuiAction::PalettePrev) && path_completion_palette_active())
    {
      pending_escape_clear = false;
      auto const match_count = completion_match_count();
      if (match_count == 0)
      {
        snapshot.status = "no matching paths";
      }
      else
      {
        selected_slash_command_index = previous_completion(selected_slash_command_index);
        snapshot.status = "path " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
      }
    }
    else if (is_action(TuiAction::HistoryPrev))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      if (browse_composer_input_history(draft, input_history, history_index, draft_input, true))
      {
        snapshot.status = "history previous";
      }
      else
      {
        scroll_up(kKeyboardScrollRows);
      }
    }
    else if (event.key == Key::ArrowUp)
    {
      scroll_up(kKeyboardScrollRows);
    }
    else if (is_action(TuiAction::CursorUp) && apply_composer_draft_action(draft, TuiAction::CursorUp))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
    }
    else if (is_action(TuiAction::PaletteNext) && slash_palette_active())
    {
      pending_escape_clear = false;
      auto const match_count = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands).size();
      if (match_count == 0)
      {
        snapshot.status = "no matching slash commands";
      }
      else
      {
        selected_slash_command_index = next_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
        snapshot.status = "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
      }
    }
    else if (is_action(TuiAction::PaletteNext) && file_reference_palette_active())
    {
      pending_escape_clear = false;
      auto const match_count = completion_match_count();
      if (match_count == 0)
      {
        snapshot.status = "no matching file references";
      }
      else
      {
        selected_slash_command_index = next_completion(selected_slash_command_index);
        snapshot.status = "reference " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
      }
    }
    else if (is_action(TuiAction::PaletteNext) && path_completion_palette_active())
    {
      pending_escape_clear = false;
      auto const match_count = completion_match_count();
      if (match_count == 0)
      {
        snapshot.status = "no matching paths";
      }
      else
      {
        selected_slash_command_index = next_completion(selected_slash_command_index);
        snapshot.status = "path " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(match_count);
      }
    }
    else if (is_action(TuiAction::HistoryNext))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      if (browse_composer_input_history(draft, input_history, history_index, draft_input, false))
      {
        snapshot.status = history_index ? "history next" : "history draft";
      }
      else
      {
        scroll_down(kKeyboardScrollRows);
      }
    }
    else if (event.key == Key::ArrowDown)
    {
      scroll_down(kKeyboardScrollRows);
    }
    else if (is_action(TuiAction::CursorDown) && apply_composer_draft_action(draft, TuiAction::CursorDown))
    {
      pending_escape_clear = false;
      draft_state.clear_selection();
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
    }
    else if (is_action(TuiAction::Undo))
    {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      draft_state.clear_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Undo) ? "undo" : "nothing to undo";
    }
    else if (is_action(TuiAction::Redo))
    {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      draft_state.clear_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Redo) ? "redo" : "nothing to redo";
    }
    else if (is_action(TuiAction::Yank))
    {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      draft_state.clear_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Yank) ? "yanked text" : "nothing to yank";
    }
    else if (is_action(TuiAction::YankPop))
    {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      draft_state.clear_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::YankPop) ? "yank-pop" : "nothing to yank-pop";
    }
    else if (is_action(TuiAction::DetailsToggle))
    {
      pending_escape_clear = false;
      snapshot.tool_details_visible = !snapshot.tool_details_visible;
      snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
    }
    else if (is_action(TuiAction::PromptAllow) || is_action(TuiAction::PromptDeny))
    {
      pending_escape_clear = false;
      snapshot.status = "prompt action is only available while a prompt is active";
    }
    else if (is_action(TuiAction::Cancel))
    {
      if (draft_state.selection_bounds())
      {
        draft_state.clear_selection();
        pending_escape_clear = false;
        snapshot.status.clear();
      }
      else if (slash_palette_active() || file_reference_palette_active() || path_completion_palette_active())
      {
        pending_escape_clear = false;
        slash_palette_suppressed = true;
        selected_slash_command_index = 0;
        path_completion_force_active = false;
        history_index.reset();
        draft_input.clear();
        snapshot.status.clear();
      }
      else if (!draft.text.empty())
      {
        if (pending_escape_clear)
        {
          static_cast<void>(apply_composer_draft_action(draft, TuiAction::ClearInput));
          selected_slash_command_index = 0;
          path_completion_force_active = false;
          history_index.reset();
          draft_input.clear();
          draft_scroll_offset = 0;
          pending_escape_clear = false;
          snapshot.status = "input cleared";
        }
        else
        {
          pending_escape_clear = true;
          snapshot.status = "press Esc again to clear";
        }
      }
      else
      {
        pending_escape_clear = false;
        snapshot.status = "escape ignored";
      }
    }
    else if (is_action(TuiAction::Submit))
    {
      if (event.key == Key::Enter && draft_state.convert_backslash_enter_to_newline(snapshot))
      {
        if (!render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      auto const action = handle_submit();
      if (action == InputLoopAction::BreakLoop)
        break;
      if (action == InputLoopAction::ContinueLoop)
        continue;
    }
    else if (event.key == Key::Space)
    {
      insert_input_text();
    }
    snapshot.selected_slash_command_index = selected_slash_command_index;
    if (!render())
    {
      terminal_write_failed = true;
      break;
    }
  }

  return terminal_signal_received() ? 130 : (terminal_write_failed ? 1 : 0);
}

std::optional<std::vector<std::string>> dispatch_tui_active_nonblocking_command(TuiActiveRunQueues const& queues, std::string const& submitted)
{
  return queues.run_nonblocking_command ? queues.run_nonblocking_command(submitted) : std::nullopt;
}

TuiActiveNonblockingCommandDispatchResult dispatch_tui_active_nonblocking_command_gated(ComposerSnapshot const& completion_snapshot,
                                                                                        TuiActiveRunQueues const& queues, std::string const& submitted)
{
  if (auto const disabled_status = detail::disabled_visible_completion_selection_status(completion_snapshot))
  {
    return TuiActiveNonblockingCommandDispatchResult{.kind = TuiActiveNonblockingCommandDispatchKind::Blocked, .status = *disabled_status};
  }
  auto output = dispatch_tui_active_nonblocking_command(queues, submitted);
  if (!output)
    return {};
  return TuiActiveNonblockingCommandDispatchResult{.kind = TuiActiveNonblockingCommandDispatchKind::Handled, .output = std::move(*output)};
}

ava::core::Result<TuiRuntimeStateSnapshot> dispatch_tui_selector_authority(ComposerSnapshot& snapshot, std::string pending_status,
                                                                           std::function<bool()> const& render,
                                                                           std::function<ava::core::Result<TuiRuntimeStateSnapshot>()> const& callback)
{
  snapshot.select_list.reset();
  snapshot.status = std::move(pending_status);
  if (!render())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to paint selector authority status");
    snapshot.status = error.format();
    return std::unexpected(std::move(error));
  }
  auto result = callback();
  if (!result)
    snapshot.status = result.error().format();
  return result;
}

}  // namespace ava::tui
