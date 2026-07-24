#include "sys.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime_actions_internal.h"
#include "ava/tui/runtime_active_run_internal.h"
#include "ava/tui/runtime_active_run_state_internal.h"
#include "ava/tui/runtime_commands_internal.h"
#include "ava/tui/runtime_draft_internal.h"
#include "ava/tui/runtime_input_internal.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/runtime_navigation_internal.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/runtime_transcript_internal.h"
#include "ava/tui/terminal.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <curses.h>

namespace ava::tui {
using runtime_commands::shell_helper_submission;
using runtime_input::printable_jump_target;
using runtime_transcript::assistant_meta_for_snapshot;
using runtime_transcript::push_history;
using runtime_transcript::push_transcript;

namespace {

constexpr std::size_t kKeyboardScrollRows = 3;
constexpr std::size_t kMouseWheelScrollRows = 1;

}  // namespace

bool RuntimeActiveRunController::handle_input(RuntimeActiveRunState& state, runtime_input::RuntimeInput const& active_input)
{
  auto& options = options_;
  auto& draft_state = draft_state_;
  auto& snapshot = presentation_state_.snapshot;
  auto& draft = draft_state_.draft;
  auto& input_history = draft_state_.input_history;
  auto& history_index = draft_state_.history_index;
  auto& draft_input = draft_state_.draft_input;
  auto& jump_mode = draft_state_.jump_mode;
  auto& selected_slash_command_index = draft_state_.selected_slash_command_index;
  auto& slash_palette_suppressed = draft_state_.slash_palette_suppressed;
  auto& path_completion_force_active = draft_state_.path_completion_force_active;
  auto& draft_scroll_offset = draft_state_.draft_scroll_offset;
  auto& pending_escape_clear = draft_state_.pending_escape_clear;
  auto& transcript_scroll_offset = renderer_.transcript_scroll_offset;
  auto& detached_new_output_count = renderer_.detached_new_output_count;
  auto& completion_cache = renderer_.completion_cache;
  auto& detached_sidebar_snapshot = renderer_.detached_sidebar_snapshot;
  auto& active_queues = state.active_queues;
  auto& run_cancel_requested = state.run_cancel_requested;
  auto render = [&]() -> bool { return renderer_.render(); };
  auto request_stop = [&]() -> bool { return this->request_stop(state); };
  auto request_close_after_submit = [&]() { this->request_close_after_submit(state); };
  auto clear_draft_for_interrupt = [&]() { return action_controller_.clear_draft_for_interrupt(); };
  auto open_external_editor = [&]() -> bool { return action_controller_.open_external_editor(); };
  auto suspend_to_background = [&]() -> bool { return action_controller_.suspend_to_background(); };
  auto paste_clipboard_image = [&]() -> bool { return action_controller_.paste_clipboard_image(); };
  auto toggle_thinking_visibility = [&]() { action_controller_.toggle_thinking_visibility(); };
  auto slash_palette_active = [&]() { return navigation_.slash_palette_active(); };
  auto file_reference_palette_active = [&]() { return navigation_.file_reference_palette_active(); };
  auto path_completion_palette_active = [&]() { return navigation_.path_completion_palette_active(); };
  auto completion_match_count = [&]() { return navigation_.completion_match_count(); };
  auto clamp_completion = [&](std::size_t selected) { return navigation_.clamp_completion(selected); };
  auto previous_completion = [&](std::size_t selected) { return navigation_.previous_completion(selected); };
  auto next_completion = [&](std::size_t selected) { return navigation_.next_completion(selected); };
  auto selected_completion_disabled_reason = [&](std::size_t selected) { return navigation_.selected_completion_disabled_reason(selected); };
  auto selected_completion_text = [&](std::size_t selected) { return navigation_.selected_completion_text(selected); };
  auto scroll_up = [&](std::size_t amount) { navigation_.scroll_up(amount); };
  auto scroll_down = [&](std::size_t amount) { navigation_.scroll_down(amount); };
  auto toggle_tool_details_at = [&](std::size_t item_index) { return navigation_.toggle_tool_details_at(item_index); };
  auto handle_sidebar_drawer_input = [&](InputEvent const& event) -> std::optional<bool> { return navigation_.handle_sidebar_drawer_input(event); };
  auto jump_to_bottom = [&](std::string status) { navigation_.jump_to_bottom(std::move(status)); };
  auto scroll_to_message_boundary = [&](bool previous) { navigation_.scroll_to_message_boundary(previous); };
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
    auto queued = steering_draft ? active_queues->queue_steering(queued_text.substr(steering_prefix.size())) : active_queues->queue_follow_up(queued_text);
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
    if (auto const disabled_reason = slash_command_selection_disabled_reason(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index))
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
      active_is_action(TuiAction::DeleteWordForward) || active_is_action(TuiAction::DeleteToLineStart) || active_is_action(TuiAction::DeleteToLineEnd) ||
      active_is_action(TuiAction::ClearInput) || active_is_action(TuiAction::CursorLeft) || active_is_action(TuiAction::CursorRight) ||
      active_is_action(TuiAction::CursorLineStart) || active_is_action(TuiAction::CursorLineEnd) || active_is_action(TuiAction::CursorWordLeft) ||
      active_is_action(TuiAction::CursorWordRight) || active_is_action(TuiAction::Undo) || active_is_action(TuiAction::Redo) ||
      active_is_action(TuiAction::Yank) || active_is_action(TuiAction::YankPop))
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
    if (auto const clicked = detail::file_reference_palette_selection_for_screen_position_cached(snapshot, active_event.mouse_row, active_event.mouse_column,
                                                                                                 completion_cache, snapshot.file_references_generation))
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
    if (auto const clicked = detail::path_completion_palette_selection_for_screen_position_cached(snapshot, active_event.mouse_row, active_event.mouse_column,
                                                                                                  completion_cache, snapshot.file_references_generation))
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
}

}  // namespace ava::tui
