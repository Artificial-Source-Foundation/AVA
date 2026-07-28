#include "sys.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_actions_internal.h"
#include "ava/tui/runtime_active_run_internal.h"
#include "ava/tui/runtime_draft_internal.h"
#include "ava/tui/runtime_input_internal.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/runtime_navigation_internal.h"
#include "ava/tui/runtime_prompts_internal.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/runtime_state_internal.h"
#include "ava/tui/runtime_submit_internal.h"
#include "ava/tui/runtime_transcript_search_internal.h"
#include "ava/tui/runtime_views_internal.h"
#include "ava/tui/session_grants.h"
#include "ava/tui/terminal.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <curses.h>

namespace ava::tui {
using runtime_input::printable_jump_target;
using runtime_input::read_curses_input_with_timeout;
using runtime_transcript::assistant_meta_for_snapshot;
using runtime_transcript::copy_text_from_answer;
using runtime_transcript::push_history;
using runtime_transcript::push_transcript;
using runtime_views::active_run_hint_for;
using runtime_views::kSettingsEditKeybindings;
using runtime_views::kSettingsOpenKeybindings;
using runtime_views::kSettingsOpenModels;
using runtime_views::kSettingsOpenScopedModels;
using runtime_views::kSettingsReloadKeybindings;

namespace {

constexpr std::size_t kKeyboardScrollRows = 3;
constexpr std::size_t kMouseWheelScrollRows = 1;
constexpr auto kIdleInputPollDelay = std::chrono::milliseconds(250);
class ComposerTerminalGraphicsGuard
{
 public:
  ComposerTerminalGraphicsGuard() = default;
  ComposerTerminalGraphicsGuard(ComposerTerminalGraphicsGuard const&) = delete;
  ComposerTerminalGraphicsGuard& operator=(ComposerTerminalGraphicsGuard const&) = delete;
  ~ComposerTerminalGraphicsGuard() { detail::clear_composer_terminal_graphics(); }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

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
  auto& command_session_grants = presentation_state.command_session_grants;

  auto refresh_token_status = [&]() { presentation_state.refresh_token_status(options); };
  auto refresh_active_context_status = [&]() { presentation_state.refresh_active_context_status(options); };
  auto refresh_reasoning_status = [&]() { presentation_state.refresh_reasoning_status(options); };
  auto apply_runtime_state_snapshot = [&](TuiRuntimeStateSnapshot state) { presentation_state.apply_runtime_state_snapshot(options, std::move(state)); };
  refresh_token_status();
  refresh_active_context_status();
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
  RuntimeNavigationController navigation(options, snapshot, sidebar, draft_state, renderer);
  auto& transcript_scroll_offset = renderer.transcript_scroll_offset;
  auto& completion_cache = renderer.completion_cache;
  ActiveSelectList active_select_list = ActiveSelectList::None;
  TranscriptSearchController transcript_search(presentation_state, renderer, navigation, active_select_list);
  std::optional<PendingSessionArchiveAction> session_archive_confirmation;
  RuntimePromptCoordinator prompt_coordinator(options, snapshot, command_session_grants, renderer);
  [[maybe_unused]] auto permission_resolver = prompt_coordinator.permission_resolver();
  [[maybe_unused]] auto question_resolver = prompt_coordinator.question_resolver();
  auto render = [&]() -> bool { return renderer.render(); };

  RuntimeActionController action_controller(options, presentation_state, draft_state, renderer, active_select_list, session_archive_confirmation);
  RuntimeActiveRunController active_run_controller(options, presentation_state, draft_state, renderer, prompt_coordinator, navigation, action_controller,
                                                   transcript_search);
  auto maybe_reload_display_settings = [&]() -> bool { return action_controller.maybe_reload_display_settings(); };
  auto clear_draft_for_interrupt = [&]() { return action_controller.clear_draft_for_interrupt(); };
  auto open_external_editor = [&]() -> bool { return action_controller.open_external_editor(); };
  auto suspend_to_background = [&]() -> bool { return action_controller.suspend_to_background(); };
  auto paste_clipboard_image = [&]() -> bool { return action_controller.paste_clipboard_image(); };
  auto cycle_reasoning = [&]() { action_controller.cycle_reasoning(); };
  auto toggle_thinking_visibility = [&]() { action_controller.toggle_thinking_visibility(); };
  auto open_model_selector = [&]() -> bool { return action_controller.open_model_selector(); };
  auto open_scoped_model_selector = [&]() -> bool { return action_controller.open_scoped_model_selector(); };
  auto open_session_selector = [&]() -> bool { return action_controller.open_session_selector(); };
  auto cycle_model = [&](bool forward) { action_controller.cycle_model(forward); };

  auto slash_palette_active = [&]() { return navigation.slash_palette_active(); };
  auto file_reference_palette_active = [&]() { return navigation.file_reference_palette_active(); };
  auto path_completion_palette_active = [&]() { return navigation.path_completion_palette_active(); };
  auto completion_match_count = [&]() { return navigation.completion_match_count(); };
  auto clamp_completion = [&](std::size_t selected) { return navigation.clamp_completion(selected); };
  auto previous_completion = [&](std::size_t selected) { return navigation.previous_completion(selected); };
  auto next_completion = [&](std::size_t selected) { return navigation.next_completion(selected); };
  auto selected_completion_disabled_reason = [&](std::size_t selected) { return navigation.selected_completion_disabled_reason(selected); };
  auto selected_completion_text = [&](std::size_t selected) { return navigation.selected_completion_text(selected); };

  auto scroll_up = [&](std::size_t amount) { navigation.scroll_up(amount); };
  auto scroll_down = [&](std::size_t amount) { navigation.scroll_down(amount); };
  auto toggle_tool_details_at = [&](std::size_t item_index) { return navigation.toggle_tool_details_at(item_index); };

  auto handle_sidebar_drawer_input = [&](InputEvent const& event) -> std::optional<bool> { return navigation.handle_sidebar_drawer_input(event); };

  auto jump_to_bottom = [&](std::string status) { navigation.jump_to_bottom(std::move(status)); };
  auto scroll_to_message_boundary = [&](bool previous) { navigation.scroll_to_message_boundary(previous); };

  RuntimeSubmitController submit_controller(options, presentation_state, draft_state, renderer, navigation, action_controller, active_run_controller,
                                            transcript_search, active_select_list);
  auto handle_submit = [&](std::optional<std::string> forced_submission = std::nullopt) {
    auto const outcome = submit_controller.submit(std::move(forced_submission));
    terminal_write_failed = outcome.terminal_write_failed;
    return outcome.disposition;
  };

  if (terminal_signal_received())
    return 130;
  if (!render())
    return 1;

  while (true)
  {
    if (!renderer.flush_pending_render_if_due())
    {
      terminal_write_failed = true;
      break;
    }
    auto input_poll_delay = kIdleInputPollDelay;
    if (renderer.has_pending_render())
    {
      input_poll_delay = std::min(input_poll_delay, std::chrono::ceil<std::chrono::milliseconds>(renderer.time_until_pending_render()));
    }
    auto const maybe_input = read_curses_input_with_timeout(input_poll_delay);
    if (!maybe_input)
    {
      if (!renderer.flush_pending_render_if_due() || !maybe_reload_display_settings())
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
      renderer.wheel_governor.reset();
      transcript_search.refresh();
      if (!render())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    if (!runtime_wheel_input_accepted(renderer.wheel_governor, input.event.key))
      continue;
    clear_reasoning_feedback_for_user_input(snapshot);
    if (auto handled = transcript_search.handle_input(input.event))
    {
      if (!*handled)
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
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
      if (!renderer.request_render())
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
        if (!renderer.request_render())
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
      if (action == RuntimeSubmitDisposition::BreakLoop)
        break;
      if (action == RuntimeSubmitDisposition::ContinueLoop)
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
      auto const action = handle_submit(is_action(TuiAction::SessionNew) ? "/new" : "/fork");
      if (action == RuntimeSubmitDisposition::BreakLoop)
        break;
      if (action == RuntimeSubmitDisposition::ContinueLoop)
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
      renderer.synchronize_detached_transcript_layout();
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
      renderer.synchronize_detached_transcript_layout();
      snapshot.tool_presentation = snapshot.tool_presentation == ToolPresentation::Expanded ? ToolPresentation::Rich : ToolPresentation::Expanded;
      snapshot.status = "tool details " + std::string(to_string(snapshot.tool_presentation));
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
        if (!renderer.request_render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      auto const action = handle_submit();
      if (action == RuntimeSubmitDisposition::BreakLoop)
        break;
      if (action == RuntimeSubmitDisposition::ContinueLoop)
        continue;
    }
    else if (event.key == Key::Space)
    {
      insert_input_text();
    }
    snapshot.selected_slash_command_index = selected_slash_command_index;
    if (!renderer.request_render())
    {
      terminal_write_failed = true;
      break;
    }
  }

  return terminal_signal_received() ? 130 : (terminal_write_failed ? 1 : 0);
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
