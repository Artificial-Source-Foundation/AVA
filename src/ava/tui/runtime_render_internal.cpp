#include "sys.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/terminal.h"

#include <algorithm>
#include <csignal>
#include <string>
#include <curses.h>

namespace ava::tui {

SignalBlockGuard::SignalBlockGuard()
{
  sigset_t blocked{};
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGINT);
  sigaddset(&blocked, SIGTERM);
  active_ = sigprocmask(SIG_BLOCK, &blocked, &previous_) == 0;
}

SignalBlockGuard::~SignalBlockGuard()
{
  if (active_)
    static_cast<void>(sigprocmask(SIG_SETMASK, &previous_, nullptr));
}

std::pair<std::size_t, std::size_t> terminal_size()
{
  int height = 0;
  int width = 0;
  getmaxyx(stdscr, height, width);
  if (width > 0 && height > 0)
    return {static_cast<std::size_t>(width), static_cast<std::size_t>(height)};
  return {80, 24};
}

RuntimeRenderer::RuntimeRenderer(ComposerSnapshot& snapshot, SidebarSnapshot& sidebar, RuntimeDraftState& draft_state)
    : snapshot_(snapshot), sidebar_(sidebar), draft_state_(draft_state)
{
}

bool RuntimeRenderer::render()
{
  auto& snapshot = snapshot_;
  auto& sidebar = sidebar_;
  auto& draft_state = draft_state_;
  auto& draft = draft_state.draft;
  auto& selected_slash_command_index = draft_state.selected_slash_command_index;
  auto& slash_palette_suppressed = draft_state.slash_palette_suppressed;
  auto& path_completion_force_active = draft_state.path_completion_force_active;
  auto& draft_scroll_offset = draft_state.draft_scroll_offset;

  if (terminal_signal_received())
    return false;
  bool wrote = false;
  {
    std::lock_guard<std::recursive_mutex> lock(ui_mutex);
    SignalBlockGuard block_signals;
    draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
    snapshot.input = draft.text;
    snapshot.input_cursor = draft.cursor;
    if (auto const selection = draft_state.selection_bounds())
    {
      snapshot.input_selection_start = selection->first;
      snapshot.input_selection_end = selection->second;
    }
    else
    {
      snapshot.input_selection_start = std::string::npos;
      snapshot.input_selection_end = std::string::npos;
    }
    snapshot.selected_slash_command_index = selected_slash_command_index;
    snapshot.slash_palette_suppressed = slash_palette_suppressed;
    snapshot.path_completion_force_active = path_completion_force_active;
    if (transcript_scroll_offset == 0)
    {
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
    }
    snapshot.sidebar = transcript_scroll_offset > 0 && detached_sidebar_snapshot ? *detached_sidebar_snapshot : sidebar;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    snapshot.sidebar_drawer_scroll_offset = std::min(snapshot.sidebar_drawer_scroll_offset, sidebar_drawer_max_scroll_offset(snapshot));
    draft_scroll_offset = std::min(draft_scroll_offset, draft_state.max_draft_scroll_offset(snapshot, height));
    snapshot.draft_scroll_offset = draft_scroll_offset;
    if (transcript_scroll_offset > 0)
    {
      transcript_scroll_offset = std::min(transcript_scroll_offset, detail::composer_max_transcript_scroll_offset_cached(
                                                                        snapshot, width, height, completion_cache, snapshot.file_references_generation,
                                                                        transcript_layout_cache, snapshot.transcript_generation));
    }
    if (transcript_scroll_offset == 0)
    {
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
      snapshot.sidebar = sidebar;
    }
    snapshot.transcript_scroll_offset = transcript_scroll_offset;
    snapshot.transcript_new_output_count = transcript_scroll_offset > 0 ? detached_new_output_count : 0;
    wrote =
        detail::draw_screen_cached(snapshot, completion_cache, snapshot.file_references_generation, transcript_layout_cache, snapshot.transcript_generation);
  }
  return wrote && !terminal_signal_received();
}

bool RuntimeRenderer::render_processing_frame()
{
  auto& snapshot = snapshot_;

  if (terminal_signal_received())
    return false;
  bool wrote = false;
  {
    std::lock_guard<std::recursive_mutex> lock(ui_mutex);
    SignalBlockGuard block_signals;
    if (snapshot.permission_prompt || snapshot.question_prompt || snapshot.select_list || snapshot.sidebar_drawer_visible || !snapshot.processing)
      return true;
    wrote = detail::draw_processing_footer_cached(snapshot, completion_cache, snapshot.file_references_generation, transcript_layout_cache,
                                                  snapshot.transcript_generation);
  }
  return wrote && !terminal_signal_received();
}

}  // namespace ava::tui
