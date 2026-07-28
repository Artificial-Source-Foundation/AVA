#pragma once

#include "ava/event/events.h"
#include "ava/tui/event_state.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_state_internal.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "debug.h"

namespace ava::tui {

struct RuntimeActiveRunState final
{
  RuntimeActiveRunState(std::string submitted_in, bool is_command_submission_in, bool supports_active_queue_in);
  RuntimeActiveRunState(RuntimeActiveRunState const&) = delete;
  RuntimeActiveRunState& operator=(RuntimeActiveRunState const&) = delete;

  std::string submitted;
  bool const is_command_submission;
  bool const supports_active_queue;
  std::vector<TranscriptItem> submitted_transcript;
  std::size_t turn_snapshot_leading_evictions = 0;
  std::vector<ava::session::ImageAttachmentRef> submit_image_attachments;
  RuntimeEventQueue event_queue;
  TuiEventState event_state;
  std::atomic_bool run_cancel_requested{false};
  bool close_after_submit = false;
  std::optional<TuiActiveRunQueues> active_queues;
  std::mutex event_context_mutex;
  ava::event::EventEnvelopeContext current_event_context;
  ava::event::RuntimeEventSink event_sink;
  std::chrono::steady_clock::time_point turn_started_at;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::tui
