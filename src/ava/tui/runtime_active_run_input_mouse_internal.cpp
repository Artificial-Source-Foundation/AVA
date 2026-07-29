#include "sys.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime_actions_internal.h"
#include "ava/tui/runtime_active_run_internal.h"
#include "ava/tui/runtime_draft_internal.h"
#include "ava/tui/runtime_input_internal.h"
#include "ava/tui/runtime_navigation_internal.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/terminal.h"

#include <chrono>
#include <cstddef>
#include <utility>
#include <curses.h>

namespace ava::tui {

namespace {

constexpr std::size_t kMouseWheelScrollRows = 1;

}  // namespace

// Mouse hit-testing/wheel.
RuntimeActiveRunController::InputHandling RuntimeActiveRunController::handle_mouse_input(runtime_input::RuntimeInput const& active_input)
{
  auto const& active_event = active_input.event;
  auto& snapshot = presentation_state_.snapshot;
  auto& draft = draft_state_.draft;
  auto& draft_state = draft_state_;
  auto& selected_slash_command_index = draft_state_.selected_slash_command_index;
  auto& path_completion_force_active = draft_state_.path_completion_force_active;
  auto& completion_cache = renderer_.completion_cache;

  if (active_event.key == Key::MouseLeftClick)
  {
    renderer_.synchronize_detached_transcript_layout();
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
      return to_input_handling(renderer_.request_render());
    }
    if (auto const clicked = detail::file_reference_palette_selection_for_screen_position_cached(snapshot, active_event.mouse_row, active_event.mouse_column,
                                                                                                 completion_cache, snapshot.file_references_generation))
    {
      selected_slash_command_index = *clicked;
      if (auto const disabled_reason = navigation_.selected_completion_disabled_reason(*clicked))
      {
        snapshot.status = "reference disabled: " + *disabled_reason;
        static_cast<void>(beep());
      }
      else
      {
        auto selection = navigation_.selected_completion_text(*clicked);
        draft_state.clear_selection();
        static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
        selected_slash_command_index = 0;
        snapshot.status = "file reference selected";
      }
      return to_input_handling(renderer_.request_render());
    }
    if (auto const clicked = detail::path_completion_palette_selection_for_screen_position_cached(snapshot, active_event.mouse_row, active_event.mouse_column,
                                                                                                  completion_cache, snapshot.file_references_generation))
    {
      selected_slash_command_index = *clicked;
      if (auto const disabled_reason = navigation_.selected_completion_disabled_reason(*clicked))
      {
        snapshot.status = "path disabled: " + *disabled_reason;
        static_cast<void>(beep());
      }
      else
      {
        auto selection = navigation_.selected_completion_text(*clicked);
        draft_state.clear_selection();
        static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
        selected_slash_command_index = 0;
        path_completion_force_active = false;
        snapshot.status = "path selected";
      }
      return to_input_handling(renderer_.request_render());
    }
    if (auto const tool_index = detail::transcript_tool_card_header_for_screen_position(snapshot, active_event.mouse_row, active_event.mouse_column))
    {
      if (navigation_.toggle_tool_details_at(*tool_index))
        return to_input_handling(renderer_.request_render());
    }
    if (auto const thinking_index = detail::transcript_thinking_header_for_screen_position(snapshot, active_event.mouse_row, active_event.mouse_column))
    {
      if (navigation_.toggle_thinking_at(*thinking_index))
        return to_input_handling(renderer_.request_render());
    }
    if (auto const cursor = composer_input_cursor_for_screen_position(snapshot, active_event.mouse_row, active_event.mouse_column))
    {
      draft.cursor = clamp_composer_draft_cursor_to_atomic_boundary(draft, *cursor);
      draft_state.clear_selection();
      snapshot.status = "cursor moved";
      return to_input_handling(renderer_.request_render());
    }
  }
  if (active_event.key == Key::MouseWheelUp)
  {
    navigation_.scroll_up(kMouseWheelScrollRows);
    return to_input_handling(renderer_.request_render());
  }
  if (active_event.key == Key::MouseWheelDown)
  {
    navigation_.scroll_down(kMouseWheelScrollRows);
    return to_input_handling(renderer_.request_render());
  }
  return InputHandling::Unhandled;
}

// Active-run restricted toggles/model actions and Space fallback.
RuntimeActiveRunController::InputHandling RuntimeActiveRunController::handle_restricted_toggle_input(runtime_input::RuntimeInput const& active_input)
{
  auto const& active_event = active_input.event;
  auto& snapshot = presentation_state_.snapshot;

  if (is_action(active_event, TuiAction::DetailsToggle))
  {
    renderer_.synchronize_detached_transcript_layout();
    snapshot.tool_presentation = snapshot.tool_presentation == ToolPresentation::Expanded ? ToolPresentation::Rich : ToolPresentation::Expanded;
    snapshot.status = "tool details " + std::string(to_string(snapshot.tool_presentation));
    return to_input_handling(renderer_.request_render());
  }
  if (is_action(active_event, TuiAction::VariantCycle))
  {
    snapshot.status = "reasoning can be changed between turns";
    return to_input_handling(renderer_.request_render());
  }
  if (is_action(active_event, TuiAction::ThinkingToggle))
  {
    action_controller_.toggle_thinking_visibility();
    return to_input_handling(renderer_.request_render());
  }
  if (is_action(active_event, TuiAction::ModelSelect))
  {
    snapshot.status = "model selection is available between turns";
    return to_input_handling(renderer_.request_render());
  }
  if (is_action(active_event, TuiAction::ModelCycleForward) || is_action(active_event, TuiAction::ModelCycleBackward))
  {
    snapshot.status = "model cycling is available between turns";
    return to_input_handling(renderer_.request_render());
  }
  if (is_action(active_event, TuiAction::MessageDequeue))
  {
    snapshot.status = "queued-message restore is available during active runs";
    return to_input_handling(renderer_.request_render());
  }
  if (active_event.key == Key::Space)
  {
    insert_active_text(active_input);
    return to_input_handling(renderer_.request_render());
  }
  return InputHandling::Unhandled;
}

}  // namespace ava::tui
