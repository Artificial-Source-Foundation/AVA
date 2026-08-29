#include "sys.h"
#include "ava/agent/question.h"
#include "ava/tui/command_output.h"
#include "ava/tui/event_state.h"
#include "ava/tui/runtime_actions_internal.h"
#include "ava/tui/runtime_active_run_internal.h"
#include "ava/tui/runtime_active_run_state_internal.h"
#include "ava/tui/runtime_commands_internal.h"
#include "ava/tui/runtime_draft_internal.h"
#include "ava/tui/runtime_input_internal.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/runtime_navigation_internal.h"
#include "ava/tui/runtime_plugin_ui_internal.h"
#include "ava/tui/runtime_prompts_internal.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/runtime_subagent_workspace_internal.h"
#include "ava/tui/runtime_transcript_internal.h"
#include "ava/tui/runtime_transcript_search_internal.h"
#include "ava/tui/terminal.h"
#include "ava/permissions/permission.h"
#include "ava/core/ids.h"
#include "ava/core/thread.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <curses.h>

namespace ava::tui {
namespace detail {
namespace {

std::size_t elapsed_intervals(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point next,
                              std::chrono::steady_clock::duration interval)
{
  if (now < next)
    return 0;
  return 1 + static_cast<std::size_t>((now - next) / interval);
}

}  // namespace

ActiveRunCadence::ActiveRunCadence(std::chrono::steady_clock::time_point started_at)
    : next_frame_(started_at + kActiveRunFrameDelay), next_spinner_(started_at + kProcessingIndicatorFrameDelay)
{
}

bool ActiveRunCadence::frame_due(std::chrono::steady_clock::time_point now) const
{
  return now >= next_frame_;
}

std::chrono::milliseconds ActiveRunCadence::wait_duration(std::chrono::steady_clock::time_point now) const
{
  if (frame_due(now))
    return std::chrono::milliseconds::zero();
  return std::chrono::ceil<std::chrono::milliseconds>(next_frame_ - now);
}

ActiveRunCadenceTick ActiveRunCadence::advance(std::chrono::steady_clock::time_point now)
{
  auto const frame_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(kActiveRunFrameDelay);
  auto const spinner_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(kProcessingIndicatorFrameDelay);
  auto const frames = elapsed_intervals(now, next_frame_, frame_interval);
  auto const spinner_frames = elapsed_intervals(now, next_spinner_, spinner_interval);
  next_frame_ += frame_interval * frames;
  next_spinner_ += spinner_interval * spinner_frames;
  return ActiveRunCadenceTick{.elapsed_frames = frames, .elapsed_spinner_frames = spinner_frames};
}

void ActiveRunCadence::frame_painted(std::chrono::steady_clock::time_point completed_at)
{
  next_frame_ = completed_at + kActiveRunFrameDelay;
}

ActiveRunInputReadDecision active_run_input_read_decision(bool input_buffered, bool frame_due)
{
  if (frame_due)
    return ActiveRunInputReadDecision::ServiceFrame;
  return input_buffered ? ActiveRunInputReadDecision::DrainBufferedInput : ActiveRunInputReadDecision::WaitForNextFrame;
}

RetainedInputDispatchResult dispatch_retained_input_with_prompt_precedence(std::function<PendingPromptServiceResult()> const& service_pending_prompt,
                                                                           std::function<bool()> const& dispatch_input)
{
  auto const prompt_result = service_pending_prompt();
  if (prompt_result == PendingPromptServiceResult::Failed)
    return RetainedInputDispatchResult::Failed;
  if (prompt_result == PendingPromptServiceResult::Serviced)
    return RetainedInputDispatchResult::PromptServiced;
  return dispatch_input() ? RetainedInputDispatchResult::InputHandled : RetainedInputDispatchResult::Failed;
}

}  // namespace detail

using runtime_commands::is_compact_command;
using runtime_commands::shell_helper_submission;
using runtime_input::poll_curses_input;
using runtime_transcript::apply_assistant_turn_meta;
using runtime_transcript::assistant_meta_for_snapshot;
using runtime_transcript::capture_thinking_expansion;
using runtime_transcript::capture_tool_detail_visibility;
using runtime_transcript::carry_thinking_expansion;
using runtime_transcript::carry_tool_detail_visibility;
using runtime_transcript::push_fallback_assistant_outputs;
using runtime_transcript::push_history;
using runtime_transcript::push_transcript;

RuntimeActiveRunState::RuntimeActiveRunState(std::string submitted_in, bool is_command_submission_in, bool supports_active_queue_in)
    : submitted(std::move(submitted_in)), is_command_submission(is_command_submission_in), supports_active_queue(supports_active_queue_in)
{
}

void RuntimeActiveRunState::discard_for_session_transition()
{
  submitted_transcript.clear();
  turn_snapshot_leading_evictions = 0;
  command_events_released = false;
  event_projection_pending = false;
  event_queue.discard();
  event_state = {};
}

RuntimeActiveRunController::RuntimeActiveRunController(TuiRuntimeOptions& options, RuntimePresentationState& presentation_state, RuntimeDraftState& draft_state,
                                                       RuntimeRenderer& renderer, RuntimePromptCoordinator& prompt_coordinator,
                                                       RuntimePromptStashController& prompt_stash, RuntimePluginUiCoordinator& plugin_ui,
                                                       RuntimeNavigationController& navigation, RuntimeActionController& action_controller,
                                                       TranscriptSearchController& transcript_search, RuntimeSubagentWorkspaceController& subagent_workspace,
                                                       std::function<bool()> service_mermaid_presentation)
    : options_(options),
      presentation_state_(presentation_state),
      draft_state_(draft_state),
      renderer_(renderer),
      prompt_coordinator_(prompt_coordinator),
      prompt_stash_(prompt_stash),
      plugin_ui_(plugin_ui),
      navigation_(navigation),
      action_controller_(action_controller),
      transcript_search_(transcript_search),
      subagent_workspace_(subagent_workspace),
      service_mermaid_presentation_(service_mermaid_presentation ? std::move(service_mermaid_presentation) : std::function<bool()>{[] { return true; }})
{
}

void RuntimeActiveRunController::upsert_stopping_activity()
{
  auto& sidebar = presentation_state_.sidebar;
  auto item = SidebarActivityItem{
      .id = "stopping", .label = "stopping", .detail = "waiting for active work to stop; queued drafts skip on stop", .status = ToolTimelineStatus::Running};
  auto existing = std::ranges::find_if(sidebar.activity, [&](SidebarActivityItem const& activity) { return activity.id == item.id; });
  if (existing == sidebar.activity.end())
  {
    sidebar.activity.push_back(std::move(item));
  }
  else
  {
    *existing = std::move(item);
  }
}

void RuntimeActiveRunController::settle_turn_activity(RuntimeActiveRunState& state)
{
  auto& sidebar = presentation_state_.sidebar;
  auto responding = std::ranges::find_if(sidebar.activity, [](SidebarActivityItem const& activity) { return activity.id == "responding"; });
  if (responding == sidebar.activity.end() || responding->status != ToolTimelineStatus::Running)
    return;
  if (state.run_cancel_requested.load() || state.event_state.run_status == TuiEventRunStatus::Canceled)
  {
    responding->status = ToolTimelineStatus::Canceled;
    responding->detail = "assistant stopped";
    return;
  }
  if (state.event_state.run_status == TuiEventRunStatus::Error)
  {
    responding->status = ToolTimelineStatus::Error;
    responding->detail = "assistant failed";
    return;
  }
  responding->status = ToolTimelineStatus::Success;
  responding->detail = "assistant responded";
}

bool RuntimeActiveRunController::request_stop(RuntimeActiveRunState& state)
{
  bool const was_already_requested = state.run_cancel_requested.exchange(true);
  plugin_ui_.cancel_active();
  prompt_coordinator_.fail_pending_requests();
  if (!was_already_requested)
    static_cast<void>(beep());
  {
    std::lock_guard<std::recursive_mutex> lock(renderer_.ui_mutex);
    presentation_state_.snapshot.status = "stop requested";
    upsert_stopping_activity();
  }
  return renderer_.request_render();
}

void RuntimeActiveRunController::request_close_after_submit(RuntimeActiveRunState& state)
{
  state.run_cancel_requested.store(true);
  state.close_after_submit = true;
  plugin_ui_.cancel_active();
  prompt_coordinator_.fail_pending_requests();
}

RuntimeEventDrainResult RuntimeActiveRunController::drain_events(RuntimeActiveRunState& state)
{
  auto& event_queue = state.event_queue;
  auto& event_state = state.event_state;
  auto& submitted_transcript = state.submitted_transcript;
  auto& turn_snapshot_leading_evictions = state.turn_snapshot_leading_evictions;
  auto& run_cancel_requested = state.run_cancel_requested;
  auto const turn_started_at = state.turn_started_at;
  auto& snapshot = presentation_state_.snapshot;
  auto& sidebar = presentation_state_.sidebar;
  auto& transcript_scroll_offset = renderer_.transcript_scroll_offset;
  auto& detached_new_output_count = renderer_.detached_new_output_count;
  auto& completion_cache = renderer_.completion_cache;
  auto& transcript_layout_cache = renderer_.transcript_layout_cache;
  auto& detached_sidebar_snapshot = renderer_.detached_sidebar_snapshot;
  auto& ui_mutex = renderer_.ui_mutex;
  auto events = event_queue.drain();
  for (auto const& event : events)
  {
    std::visit(
        [&](auto const& queued) {
          using Queued = std::remove_cvref_t<decltype(queued)>;
          if constexpr (std::same_as<Queued, QueuedRuntimeEvent>)
            apply_runtime_event(event_state, queued.event, queued.context);
          else if constexpr (std::same_as<Queued, ava::event::EventEnvelope>)
            apply_control_event_envelope(event_state, queued);
          else
            apply_subagent_launch_notification(event_state, queued);
        },
        event);
  }
  if (!events.empty())
    state.event_projection_pending = true;
  if (state.is_command_submission && !state.command_events_released)
    return events.empty() ? RuntimeEventDrainResult::NoEvents : RuntimeEventDrainResult::UpdatedNoRender;
  if (!state.event_projection_pending)
    return RuntimeEventDrainResult::NoEvents;
  state.event_projection_pending = false;
  auto turn_transcript = event_state_transcript_snapshot(event_state, PendingTextProjection::Unparsed);
  if (state.is_command_submission)
    remove_literal_command_invocation(turn_transcript, state.submitted);
  {
    std::lock_guard<std::recursive_mutex> lock(ui_mutex);
    auto const preserve_viewport = transcript_scroll_offset > 0;
    auto old_anchor = detail::TranscriptViewportAnchor{};
    if (preserve_viewport && !renderer_.has_deferred_detached_transcript_update())
    {
      auto const old_max_scroll =
          detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, completion_cache, snapshot.file_references_generation,
                                                               transcript_layout_cache, snapshot.transcript_generation);
      old_anchor = detail::capture_transcript_viewport_anchor(transcript_layout_cache.layout, old_max_scroll, transcript_scroll_offset);
    }
    if (preserve_viewport && !detached_sidebar_snapshot)
      detached_sidebar_snapshot = sidebar;
    apply_assistant_turn_meta(turn_transcript, assistant_meta_for_snapshot(snapshot, std::chrono::steady_clock::now() - turn_started_at),
                              snapshot.thinking_visible);
    auto const tool_detail_overrides = capture_tool_detail_visibility(snapshot.transcript);
    auto const thinking_expansion_overrides = capture_thinking_expansion(snapshot.transcript);
    auto const capped_update =
        apply_capped_transcript_snapshot(snapshot.transcript, submitted_transcript, std::move(turn_transcript), turn_snapshot_leading_evictions);
    carry_tool_detail_visibility(tool_detail_overrides, snapshot.transcript);
    carry_thinking_expansion(thinking_expansion_overrides, snapshot.transcript, capped_update.item_index_shift);
    turn_snapshot_leading_evictions = capped_update.leading_evictions;
    ++snapshot.transcript_generation;
    auto const changed_from_item_index =
        submitted_transcript.size() > capped_update.leading_evictions ? submitted_transcript.size() - capped_update.leading_evictions : std::size_t{0};
    transcript_search_.refresh_after_transcript_mutation(capped_update.item_index_shift, changed_from_item_index);
    snapshot.queued_messages = event_state.queued_messages;
    sidebar.activity = event_state.activity;
    sidebar.todos = event_state.todos;
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
      sidebar.modified_files.erase(sidebar.modified_files.begin(),
                                   sidebar.modified_files.begin() + static_cast<std::ptrdiff_t>(sidebar.modified_files.size() - kMaxSidebarModifiedFiles));
    }
    if (preserve_viewport)
    {
      renderer_.defer_detached_transcript_update(old_anchor, capped_update.item_index_shift);
      detached_new_output_count += events.size();
      snapshot.transcript_scroll_offset = transcript_scroll_offset;
    }
    else
    {
      renderer_.note_live_transcript_selection_item_shift(capped_update.item_index_shift);
      renderer_.discard_deferred_detached_transcript_update();
      transcript_scroll_offset = 0;
    }
  }
  if (subagent_workspace_.active() && !snapshot.permission_prompt && !snapshot.question_prompt)
    return RuntimeEventDrainResult::UpdatedNoRender;
  if (transcript_scroll_offset > 0 && !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list)
    return RuntimeEventDrainResult::UpdatedNoRender;
  return renderer_.request_render() ? RuntimeEventDrainResult::UpdatedNoRender : RuntimeEventDrainResult::RenderFailed;
}

RuntimeActiveRunOutcome RuntimeActiveRunController::run(std::string submitted_value)
{
  auto& options = options_;
  auto& renderer = renderer_;
  auto& prompt_coordinator = prompt_coordinator_;
  auto& snapshot = presentation_state_.snapshot;
  auto& sidebar = presentation_state_.sidebar;
  auto& pending_image_attachments = presentation_state_.pending_image_attachments;
  auto& input_history = draft_state_.input_history;
  auto& draft = draft_state_.draft;
  auto& selected_slash_command_index = draft_state_.selected_slash_command_index;
  auto& transcript_scroll_offset = renderer_.transcript_scroll_offset;
  auto& ui_mutex = renderer_.ui_mutex;
  auto refresh_plugin_surface_fit = [&]() {
    refresh_terminal_geometry_from_kernel();
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    return plugin_ui_.cancel_unfittable_surfaces(snapshot);
  };
  auto render = [&]() -> bool {
    static_cast<void>(refresh_plugin_surface_fit());
    return renderer_.render();
  };
  auto maybe_reload_display_settings = [&]() -> bool {
    auto const outcome = action_controller_.maybe_reload_display_settings();
    if (outcome == DisplaySettingsReloadPollOutcome::TerminalFailure)
      return false;
    // Active-run has no settings preview; still render an applied reload once.
    if (outcome == DisplaySettingsReloadPollOutcome::Applied)
      return render();
    return true;
  };
  auto clear_draft_for_interrupt = [&]() { return action_controller_.clear_draft_for_interrupt(); };
  auto apply_runtime_state_snapshot = [&](TuiRuntimeStateSnapshot runtime_state) {
    // Same shared overview sync path as the idle runtime loop.
    return apply_runtime_state_snapshot_with_presentation_transition(options, presentation_state_, draft_state_, renderer_, transcript_search_,
                                                                     subagent_workspace_, action_controller_.active_select_list(), std::move(runtime_state));
  };
  auto refresh_token_status = [&]() { presentation_state_.refresh_token_status(options); };
  auto refresh_active_context_status = [&]() { presentation_state_.refresh_active_context_status(options); };

  auto const is_command_submission = submitted_value.starts_with('/') || shell_helper_submission(submitted_value);
  if (is_command_submission && runtime_commands::session_switching_command(submitted_value) && !pending_image_attachments.empty())
  {
    pending_image_attachments.clear();
    snapshot.pending_attachments.clear();
  }
  auto const supports_active_queue = !is_command_submission || is_compact_command(submitted_value);
  RuntimeActiveRunState state(std::move(submitted_value), is_command_submission, supports_active_queue);
  auto& submitted = state.submitted;
  auto& submitted_transcript = state.submitted_transcript;
  submitted_transcript = snapshot.transcript;
  auto& submit_image_attachments = state.submit_image_attachments;
  if (!is_command_submission && !pending_image_attachments.empty())
  {
    submit_image_attachments = std::move(pending_image_attachments);
    pending_image_attachments.clear();
    snapshot.pending_attachments.clear();
  }
  auto& event_queue = state.event_queue;
  auto& event_state = state.event_state;
  // Carry hydrated/live todos into this run so a non-todo turn cannot clear them.
  event_state.todos = sidebar.todos;
  auto& run_cancel_requested = state.run_cancel_requested;
  auto& close_after_submit = state.close_after_submit;
  auto& active_queues = state.active_queues;
  auto& event_context_mutex = state.event_context_mutex;
  auto& current_event_context = state.current_event_context;
  auto control_sink = event_queue.envelope_sink();
  auto set_current_request_id = [&](std::string request_id) {
    std::lock_guard lock(event_context_mutex);
    current_event_context.request_id = request_id;
    current_event_context.correlation_id = std::move(request_id);
  };
  std::string submit_request_id;
  if (supports_active_queue && options.create_active_run_queues)
  {
    active_queues = options.create_active_run_queues(control_sink);
    if (!active_queues->active_request_id.empty())
    {
      submit_request_id = active_queues->active_request_id;
      auto mark_follow_up_started = active_queues->mark_follow_up_started;
      active_queues->mark_follow_up_started = [&, mark_follow_up_started](TuiQueuedFollowUp const& follow_up) {
        set_current_request_id(follow_up.request_id);
        if (mark_follow_up_started)
          return mark_follow_up_started(follow_up);
        return ava::core::VoidResult{};
      };
    }
  }
  if (submit_request_id.empty())
    submit_request_id = ava::core::make_id("request");
  set_current_request_id(submit_request_id);
  auto plugin_ui_endpoint = plugin_ui_.begin_submission(submitted, submit_request_id);
  auto cancel_requested = [&run_cancel_requested, this]() { return run_cancel_requested.load() || plugin_ui_.deadline_reached(); };
  auto runtime_event_queue_sink = [&]() -> ava::event::RuntimeEventSink {
    return [&](ava::event::RuntimeEvent const& event) {
      ava::event::EventEnvelopeContext context_snapshot;
      {
        std::lock_guard lock(event_context_mutex);
        context_snapshot = current_event_context;
      }
      return event_queue.enqueue(event, std::move(context_snapshot));
    };
  };
  auto& event_sink = state.event_sink;
  // Command events are always collected, but remain projection-buffered until
  // completion identifies whether a genuine ordinary turn was committed.
  event_sink = runtime_event_queue_sink();
  prompt_coordinator_.set_audit_sink(event_sink);
  state.turn_started_at = std::chrono::steady_clock::now();
  auto const turn_started_at = state.turn_started_at;
  auto settle_turn_activity = [&]() { this->settle_turn_activity(state); };
  auto request_stop = [&]() -> bool { return this->request_stop(state); };
  auto request_close_after_submit = [&]() { this->request_close_after_submit(state); };
  bool terminal_write_failed = false;
  ava::permissions::PermissionResolver permission_resolver;
  ava::agent::QuestionResolver question_resolver;
  push_history(input_history, submitted);
  snapshot.status = is_command_submission ? "running command..." : "thinking...";
  snapshot.processing = true;
  if (!render())
  {
    plugin_ui_.finish_submission(snapshot);
    terminal_write_failed = true;
    return RuntimeActiveRunOutcome{.break_loop = true, .terminal_write_failed = terminal_write_failed};
  }
  auto result = TuiSubmitResult{};
  bool session_changed = false;
  if (options.on_submit)
  {
    permission_resolver = prompt_coordinator.permission_resolver();
    question_resolver = prompt_coordinator.question_resolver();
    auto submit_future = ava::core::make_async("tui_submit", [&]() {
      auto take_steering_messages = active_queues ? active_queues->take_steering_messages : std::function<ava::core::Result<std::vector<std::string>>()>{};
      auto skip_active_steering = active_queues ? active_queues->skip_active_steering : std::function<ava::core::VoidResult(std::string_view)>{};
      auto take_next_follow_up = active_queues ? active_queues->take_next_follow_up : std::function<std::optional<TuiQueuedFollowUp>()>{};
      auto mark_follow_up_started = active_queues ? active_queues->mark_follow_up_started : std::function<ava::core::VoidResult(TuiQueuedFollowUp const&)>{};
      return options.on_submit(submitted, TuiSubmitContext{.request_id = submit_request_id,
                                                           .permission_resolver = permission_resolver,
                                                           .question_resolver = question_resolver,
                                                           .event_sink = event_sink,
                                                           .cancel_requested = cancel_requested,
                                                           .take_steering_messages = take_steering_messages,
                                                           .skip_active_steering = skip_active_steering,
                                                           .take_next_follow_up = take_next_follow_up,
                                                           .mark_follow_up_started = mark_follow_up_started,
                                                           .on_subagent_launch = event_queue.subagent_launch_sink(),
                                                           .plugin_ui = plugin_ui_endpoint,
                                                           .image_attachments = submit_image_attachments});
    });
    bool render_failed = false;
    auto fail_active_run = [&]() {
      terminal_write_failed = true;
      render_failed = true;
      run_cancel_requested.store(true);
      plugin_ui_.cancel_active();
      prompt_coordinator.fail_pending_requests();
    };
    auto service_pending_prompt = [&]() {
      if (!prompt_coordinator.service_pending_request(cancel_requested, request_stop, [&]() {
            transcript_search_.close_before_prompt();
            // Permission/question authority must explicitly close overview so it cannot hide and reappear.
            close_startup_overview_presentation(presentation_state_.snapshot, action_controller_.active_select_list());
          }))
        return detail::PendingPromptServiceResult::None;
      if (drain_events(state) == RuntimeEventDrainResult::RenderFailed)
        return detail::PendingPromptServiceResult::Failed;
      return detail::PendingPromptServiceResult::Serviced;
    };
    auto plugin_host_modal_conflict = [&]() {
      return snapshot.permission_prompt || snapshot.question_prompt || snapshot.command_output || snapshot.select_list || snapshot.subagent_workspace ||
             snapshot.sidebar_drawer_visible || transcript_search_.is_open() ||
             slash_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.slash_commands) ||
             file_reference_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.file_references) ||
             path_completion_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.file_references, snapshot.path_completion_force_active);
    };
    auto poll_plugin_ui = [&]() {
      auto const geometry_changed = refresh_plugin_surface_fit();
      auto const polled = plugin_ui_.poll(snapshot, plugin_host_modal_conflict());
      if (polled.deadline_expired && !request_stop())
        return false;
      return (!geometry_changed && !polled.changed) || renderer.request_render();
    };
    auto dispatch_retained_input = [&](runtime_input::RuntimeInput const& input) {
      if (snapshot.plugin_ui_modal)
      {
        // Ctrl+C cannot authorize a plugin action and retains its stop semantics.
        // Every other key uses freshly queried terminal geometry before the
        // modal can navigate or resolve; KEY_RESIZE always repaints afterward.
        auto const geometry_changed = input.event.key != Key::CtrlC && refresh_plugin_surface_fit();
        if (input.resize)
          return renderer.render();
        if (geometry_changed)
          return renderer.request_render();
        auto const plugin_result = plugin_ui_.handle_input(snapshot, input.event);
        if (plugin_result == TuiPluginUiInputResult::RequestStop)
          return request_stop();
        if (plugin_result == TuiPluginUiInputResult::Redraw)
          return renderer.request_render();
        if (plugin_result == TuiPluginUiInputResult::Handled)
          return true;
      }
      if (transcript_search_.is_open())
      {
        auto const result = prompt_coordinator.dispatch_search_input_with_prompt_precedence(
            [&]() { return handle_transcript_search_input(input).value_or(false); }, cancel_requested, request_stop,
            [&]() {
              transcript_search_.close_before_prompt();
              close_startup_overview_presentation(presentation_state_.snapshot, action_controller_.active_select_list());
            });
        if (result == SearchInputPromptDispatchResult::PromptServiced)
        {
          if (drain_events(state) != RuntimeEventDrainResult::RenderFailed)
            return true;
        }
        else if (result == SearchInputPromptDispatchResult::InputHandled)
        {
          return true;
        }
        fail_active_run();
        return false;
      }
      auto const result = detail::dispatch_retained_input_with_prompt_precedence(service_pending_prompt, [&]() { return handle_input(state, input); });
      if (result == detail::RetainedInputDispatchResult::Failed)
      {
        fail_active_run();
        return false;
      }
      return true;
    };
    detail::ActiveRunCadence cadence(std::chrono::steady_clock::now());
    while (submit_future.wait_for(std::chrono::milliseconds::zero()) != std::future_status::ready)
    {
      if (terminal_signal_received())
      {
        if (terminal_signal_number() == SIGINT && !draft.text.empty())
        {
          clear_terminal_signal();
          static_cast<void>(clear_draft_for_interrupt());
          snapshot.selected_slash_command_index = selected_slash_command_index;
          if (!render())
          {
            fail_active_run();
            break;
          }
          continue;
        }
        request_close_after_submit();
        break;
      }
      auto const prompt_result = service_pending_prompt();
      if (prompt_result == detail::PendingPromptServiceResult::Failed)
      {
        fail_active_run();
        break;
      }
      if (prompt_result == detail::PendingPromptServiceResult::Serviced)
        continue;
      if (!poll_plugin_ui())
      {
        fail_active_run();
        break;
      }
      auto active_input = poll_curses_input();
      auto const input_checked_at = std::chrono::steady_clock::now();
      auto const pending_frame_due = renderer.has_pending_render() && renderer.time_until_pending_render() <= std::chrono::steady_clock::duration::zero();
      auto const input_decision = detail::active_run_input_read_decision(active_input.has_value(), cadence.frame_due(input_checked_at) || pending_frame_due);
      if (input_decision == detail::ActiveRunInputReadDecision::DrainBufferedInput)
      {
        if (!dispatch_retained_input(*active_input))
        {
          fail_active_run();
          break;
        }
        if (close_after_submit)
          break;
        continue;
      }
      if (input_decision == detail::ActiveRunInputReadDecision::WaitForNextFrame)
      {
        auto wait_duration = cadence.wait_duration(input_checked_at);
        if (renderer.has_pending_render())
        {
          wait_duration = std::min(wait_duration, std::chrono::ceil<std::chrono::milliseconds>(renderer.time_until_pending_render()));
        }
        if (auto workspace_wait = subagent_workspace_.time_until_poll(input_checked_at))
          wait_duration = std::min(wait_duration, std::chrono::ceil<std::chrono::milliseconds>(*workspace_wait));
        if (submit_future.wait_for(wait_duration) == std::future_status::ready)
          break;
      }

      auto const frame_now = std::chrono::steady_clock::now();
      auto const cadence_tick = cadence.advance(frame_now);
      auto const spinner_advanced = cadence_tick.elapsed_spinner_frames > 0;
      if (subagent_workspace_.poll(frame_now) && !renderer.request_render())
      {
        fail_active_run();
        break;
      }
      if (spinner_advanced)
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        if (snapshot.processing)
          snapshot.spinner_frame += cadence_tick.elapsed_spinner_frames;
      }

      auto const drain_result = drain_events(state);
      if (drain_result == RuntimeEventDrainResult::RenderFailed || !service_mermaid_presentation_())
      {
        fail_active_run();
        break;
      }
      if (!subagent_workspace_.active() && transcript_scroll_offset == 0 && !maybe_reload_display_settings())
      {
        fail_active_run();
        break;
      }
      if (spinner_advanced && !subagent_workspace_.active() && !renderer.request_render(FrameRenderKind::Footer))
      {
        fail_active_run();
        break;
      }
      if (active_input)
      {
        if (!dispatch_retained_input(*active_input))
        {
          fail_active_run();
          break;
        }
        if (close_after_submit)
          break;
      }
      auto const frame_was_pending = renderer.has_pending_render();
      static_cast<void>(refresh_plugin_surface_fit());
      if (!renderer.flush_pending_render_if_due())
      {
        fail_active_run();
        break;
      }
      if (frame_was_pending && !renderer.has_pending_render())
        cadence.frame_painted(std::chrono::steady_clock::now());
    }
    // Clear and unblock the TUI-local presentation before joining the submit
    // worker or applying an application/session transition.
    plugin_ui_.finish_submission(snapshot);
    result = submit_future.get();
    state.command_events_released = !is_command_submission || result.ordinary_turn_committed;
    if (result.state_snapshot)
    {
      // Submit workers own ShellState. Apply their authoritative snapshot
      // on the TUI thread before another prompt can consult UI-local
      // session grants or attachments.
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      session_changed = apply_runtime_state_snapshot(std::move(*result.state_snapshot));
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
    if (session_changed)
    {
      // The completed worker and queue are bound to the prior session. Drop
      // everything they emitted, including finish receipts, before fallback
      // presentation considers whether any runtime event was received.
      state.discard_for_session_transition();
    }
    else if (drain_events(state) == RuntimeEventDrainResult::RenderFailed)
    {
      terminal_write_failed = true;
      render_failed = true;
    }
    prompt_coordinator.set_audit_sink(nullptr);
    if (render_failed)
      return RuntimeActiveRunOutcome{.break_loop = true, .terminal_write_failed = terminal_write_failed};
  }
  plugin_ui_.finish_submission(snapshot);
  prompt_coordinator.set_audit_sink(nullptr);
  if (terminal_signal_received())
    return RuntimeActiveRunOutcome{.break_loop = true, .terminal_write_failed = terminal_write_failed};
  auto const events_received = event_queue.received_any();
  auto settlement = settle_tui_submission(submitted_transcript, event_state, submitted, is_command_submission, result.ordinary_turn_committed, result.output,
                                          result.tool_timeline, run_cancel_requested.load());
  if (settlement.policy.preserve_transcript && !session_changed)
  {
    // Local command event projection is withheld for the entire run. Restore the
    // exact pre-submit sequence defensively so no independent receipt path can leak.
    snapshot.transcript = settlement.transcript;
    ++snapshot.transcript_generation;

    if (!event_state.activity.empty())
      sidebar.activity = event_state.activity;
    if (!event_state.todos.empty())
      sidebar.todos = event_state.todos;
    for (auto const& file : event_state.modified_files)
    {
      auto const exists = std::ranges::any_of(sidebar.modified_files, [&](SidebarModifiedFile const& existing) { return existing.path == file.path; });
      if (!exists)
        sidebar.modified_files.push_back(file);
    }
    for (auto const& tool : result.tool_timeline)
    {
      for (auto const& path : tool.changed_paths)
      {
        auto const exists = std::ranges::any_of(sidebar.modified_files, [&](SidebarModifiedFile const& existing) { return existing.path == path; });
        if (!exists)
          sidebar.modified_files.push_back(SidebarModifiedFile{.path = path});
      }
    }
    constexpr auto kMaxLocalCommandModifiedFiles = std::size_t{50};
    if (sidebar.modified_files.size() > kMaxLocalCommandModifiedFiles)
    {
      sidebar.modified_files.erase(sidebar.modified_files.begin(),
                                   sidebar.modified_files.begin() + static_cast<std::ptrdiff_t>(sidebar.modified_files.size() - kMaxLocalCommandModifiedFiles));
    }
  }
  {
    std::lock_guard<std::recursive_mutex> lock(ui_mutex);
    settle_turn_activity();
  }
  if (settlement.policy.project_conversation && !events_received)
  {
    auto const turn_elapsed = std::chrono::steady_clock::now() - turn_started_at;
    auto const assistant_meta = assistant_meta_for_snapshot(snapshot, turn_elapsed);
    auto const transcript_size_before_fallback = snapshot.transcript.size();
    std::ptrdiff_t item_index_shift = 0;
    if (!is_command_submission && !session_changed)
    {
      item_index_shift += push_transcript(snapshot, TranscriptItem{.label = "you", .text = submitted});
    }
    if (run_cancel_requested.load() && !session_changed)
    {
      item_index_shift += push_fallback_assistant_outputs(snapshot, {"stopped by user"}, assistant_meta);
    }
    else
    {
      for (auto const& tool : result.tool_timeline)
      {
        item_index_shift += push_transcript(snapshot, TranscriptItem{.tool = tool});
      }
      auto const bounded_bash_output_is_in_card =
          std::ranges::any_of(result.tool_timeline, [](auto const& tool) { return tool.name == "bash" && tool.truncated && !tool.spill_path.empty(); });
      if (!bounded_bash_output_is_in_card)
        item_index_shift += push_fallback_assistant_outputs(snapshot, result.output, assistant_meta);
    }
    auto const shifted_first_new_item = static_cast<std::ptrdiff_t>(transcript_size_before_fallback) + item_index_shift;
    auto const changed_from_item_index = shifted_first_new_item > 0 ? static_cast<std::size_t>(shifted_first_new_item) : std::size_t{0};
    transcript_search_.refresh_after_transcript_mutation(item_index_shift, std::min(changed_from_item_index, snapshot.transcript.size()));
    renderer_.note_live_transcript_selection_item_shift(item_index_shift);
  }
  if (settlement.policy.present_command_output)
  {
    settle_local_command_completion(snapshot, submitted, std::move(settlement.command_output), std::move(settlement.command_tools));
  }
  else
  {
    if (!events_received && !transcript_search_.is_open())
      transcript_scroll_offset = 0;
    snapshot.status = events_received ? (event_state.run_status == TuiEventRunStatus::Error      ? "error"
                                         : event_state.run_status == TuiEventRunStatus::Canceled ? "stopped"
                                                                                                 : "done")
                                      : (result.output.empty() ? "ok" : "done");
  }
  snapshot.processing = false;
  if (result.context_source_count)
  {
    snapshot.context_source_count = result.context_source_count;
    sidebar.context_source_count = result.context_source_count;
  }
  refresh_token_status();
  refresh_active_context_status();
  if (!service_mermaid_presentation_() || !render())
  {
    terminal_write_failed = true;
    return RuntimeActiveRunOutcome{.break_loop = true, .terminal_write_failed = terminal_write_failed};
  }
  if (close_after_submit)
    return RuntimeActiveRunOutcome{.break_loop = true, .terminal_write_failed = terminal_write_failed};
  if (result.quit)
    return RuntimeActiveRunOutcome{.break_loop = true, .terminal_write_failed = terminal_write_failed};
  return RuntimeActiveRunOutcome{.break_loop = false, .terminal_write_failed = terminal_write_failed};
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

}  // namespace ava::tui
