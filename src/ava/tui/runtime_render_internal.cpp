#include "sys.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/terminal.h"

#include <algorithm>
#include <chrono>
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

bool WheelBurstGovernor::accept(WheelDirection, Clock::time_point now)
{
  if (last_accepted_at_ && now < *last_accepted_at_ + kAcceptedEventInterval)
    return false;
  last_accepted_at_ = now;
  return true;
}

void WheelBurstGovernor::reset()
{
  last_accepted_at_.reset();
}

bool runtime_wheel_input_accepted(WheelBurstGovernor& governor, Key key, WheelBurstGovernor::Clock::time_point now)
{
  if (key == Key::MouseWheelUp)
    return governor.accept(WheelDirection::Up, now);
  if (key == Key::MouseWheelDown)
    return governor.accept(WheelDirection::Down, now);
  governor.reset();
  return true;
}

void FrameScheduler::request(FrameRenderKind kind, Clock::time_point now)
{
  if (failed_)
    return;
  if (!pending_kind_)
  {
    pending_kind_ = kind;
    deadline_ = last_paint_completed_.value_or(now) + kFrameInterval;
    return;
  }
  if (kind == FrameRenderKind::Full)
    pending_kind_ = FrameRenderKind::Full;
}

bool FrameScheduler::pending() const
{
  return pending_kind_.has_value();
}

std::optional<FrameRenderKind> FrameScheduler::pending_kind() const
{
  return pending_kind_;
}

FrameScheduler::Clock::duration FrameScheduler::time_until_due(Clock::time_point now) const
{
  if (!deadline_ || now >= *deadline_)
    return Clock::duration::zero();
  return *deadline_ - now;
}

std::optional<FrameRenderKind> FrameScheduler::take_due(Clock::time_point now)
{
  if (!pending_kind_ || time_until_due(now) > Clock::duration::zero())
    return std::nullopt;
  return take_pending();
}

std::optional<FrameRenderKind> FrameScheduler::take_pending()
{
  auto kind = pending_kind_;
  pending_kind_.reset();
  deadline_.reset();
  return kind;
}

void FrameScheduler::paint_completed(Clock::time_point completed_at, bool succeeded)
{
  last_paint_completed_ = completed_at;
  if (!succeeded)
    failed_ = true;
}

void FrameScheduler::discard_pending()
{
  pending_kind_.reset();
  deadline_.reset();
}

bool FrameScheduler::failed() const
{
  return failed_;
}

RuntimeRenderer::RuntimeRenderer(ComposerSnapshot& snapshot, SidebarSnapshot& sidebar, RuntimeDraftState& draft_state)
    : snapshot_(snapshot), sidebar_(sidebar), draft_state_(draft_state)
{
}

void RuntimeRenderer::defer_detached_transcript_update(detail::TranscriptViewportAnchor anchor, std::ptrdiff_t item_index_shift)
{
  if (transcript_scroll_offset == 0)
    return;
  if (!deferred_detached_viewport_)
  {
    deferred_detached_viewport_ = DeferredDetachedViewport{.anchor = anchor, .item_index_shift = item_index_shift};
    return;
  }
  deferred_detached_viewport_->item_index_shift += item_index_shift;
}

void RuntimeRenderer::synchronize_detached_transcript_layout()
{
  if (!deferred_detached_viewport_)
    return;
  if (transcript_scroll_offset == 0)
  {
    deferred_detached_viewport_.reset();
    return;
  }
  auto deferred = *deferred_detached_viewport_;
  auto const max_scroll =
      detail::composer_max_transcript_scroll_offset_cached(snapshot_, snapshot_.width, snapshot_.height, completion_cache, snapshot_.file_references_generation,
                                                           transcript_layout_cache, snapshot_.transcript_generation);
  transcript_scroll_offset = detail::restore_transcript_viewport_anchor(deferred.anchor, transcript_layout_cache.layout, max_scroll, deferred.item_index_shift);
  snapshot_.transcript_scroll_offset = transcript_scroll_offset;
  deferred_detached_viewport_.reset();
}

void RuntimeRenderer::discard_deferred_detached_transcript_update()
{
  deferred_detached_viewport_.reset();
}

bool RuntimeRenderer::has_deferred_detached_transcript_update() const
{
  return deferred_detached_viewport_.has_value();
}

bool RuntimeRenderer::render()
{
  if (frame_scheduler_.failed())
    return false;
  frame_scheduler_.discard_pending();
  auto const succeeded = render_full(false);
  frame_scheduler_.paint_completed(std::chrono::steady_clock::now(), succeeded);
  return succeeded;
}

bool RuntimeRenderer::render_full(bool freeze_transcript_layout)
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
      discard_deferred_detached_transcript_update();
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
    }
    snapshot.sidebar = transcript_scroll_offset > 0 && detached_sidebar_snapshot ? *detached_sidebar_snapshot : sidebar;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const compact_spacing = detail::composer_layout_policy(snapshot, height).compact_transcript_spacing;
    auto const presentation_settings_compatible = transcript_layout_cache.valid && transcript_layout_cache.tool_presentation == snapshot.tool_presentation &&
                                                  transcript_layout_cache.thinking_visible == snapshot.thinking_visible &&
                                                  transcript_layout_cache.compact_spacing == compact_spacing;
    auto const frozen_cache_compatible = presentation_settings_compatible && transcript_layout_cache.width == composer_canvas_layout(snapshot).content_width;
    auto const freeze_modal_transcript_layout =
        snapshot.select_list.has_value() && snapshot.select_list->freeze_underlying_transcript_layout && presentation_settings_compatible;
    // Detached freeze keeps a width-compatible cache across scheduled paints. Modal freeze keeps the
    // pre-modal underlying layout even when the active selector canvas width differs, and freezes the
    // draw itself whether or not a deferred detached viewport is pending.
    auto const freeze_detached_viewport =
        freeze_modal_transcript_layout || (freeze_transcript_layout && deferred_detached_viewport_.has_value() && frozen_cache_compatible);
    auto const synchronized_detached_viewport = deferred_detached_viewport_.has_value() && !freeze_detached_viewport;
    if (synchronized_detached_viewport)
      synchronize_detached_transcript_layout();
    snapshot.sidebar_drawer_scroll_offset = std::min(snapshot.sidebar_drawer_scroll_offset, sidebar_drawer_max_scroll_offset(snapshot));
    draft_scroll_offset = std::min(draft_scroll_offset, draft_state.max_draft_scroll_offset(snapshot, height));
    snapshot.draft_scroll_offset = draft_scroll_offset;
    if (transcript_scroll_offset > 0 && !synchronized_detached_viewport && !freeze_detached_viewport)
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
    wrote = detail::draw_screen_cached(snapshot, completion_cache, snapshot.file_references_generation, transcript_layout_cache, snapshot.transcript_generation,
                                       screen_row_cache, freeze_detached_viewport, freeze_modal_transcript_layout);
  }
  return wrote && !terminal_signal_received();
}

bool RuntimeRenderer::render_processing_frame()
{
  if (frame_scheduler_.failed())
    return false;
  frame_scheduler_.discard_pending();
  auto const succeeded = paint(FrameRenderKind::Footer, false);
  frame_scheduler_.paint_completed(std::chrono::steady_clock::now(), succeeded);
  return succeeded;
}

bool RuntimeRenderer::request_render(FrameRenderKind kind)
{
  frame_scheduler_.request(kind, std::chrono::steady_clock::now());
  return !frame_scheduler_.failed();
}

bool RuntimeRenderer::flush_pending_render_if_due()
{
  auto const kind = frame_scheduler_.take_due(std::chrono::steady_clock::now());
  if (!kind)
    return !frame_scheduler_.failed();
  auto const succeeded = paint(*kind, true);
  frame_scheduler_.paint_completed(std::chrono::steady_clock::now(), succeeded);
  return succeeded;
}

bool RuntimeRenderer::flush_pending_render()
{
  auto const kind = frame_scheduler_.take_pending();
  if (!kind)
    return !frame_scheduler_.failed();
  auto const succeeded = paint(*kind, true);
  frame_scheduler_.paint_completed(std::chrono::steady_clock::now(), succeeded);
  return succeeded;
}

bool RuntimeRenderer::has_pending_render() const
{
  return frame_scheduler_.pending();
}

std::chrono::steady_clock::duration RuntimeRenderer::time_until_pending_render() const
{
  return frame_scheduler_.time_until_due(std::chrono::steady_clock::now());
}

bool RuntimeRenderer::render_failed() const
{
  return frame_scheduler_.failed();
}

bool RuntimeRenderer::paint(FrameRenderKind kind, bool freeze_transcript_layout)
{
  if (kind == FrameRenderKind::Full)
    return render_full(freeze_transcript_layout);

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
                                                  snapshot.transcript_generation, screen_row_cache);
  }
  return wrote && !terminal_signal_received();
}

}  // namespace ava::tui
