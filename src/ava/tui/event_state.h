#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ava/app/events.h"
#include "ava/tui/composer.h"

namespace ava::tui {

enum class TuiEventRunStatus {
  Idle,
  Running,
  Completed,
  Canceled,
  Error,
  Done,
};

struct PendingToolItem {
  std::string call_id;
  std::string request_id;
  std::string correlation_id;
  ToolTimelineItem item;
};

struct TuiEventState {
  // Completed transcript, pending assistant/reasoning/tool state, prompt audit activity, and
  // sidebar/status data are intentionally separate so live runs and replayed event streams settle
  // into the same visible story without mutating completed transcript history.
  std::vector<TranscriptItem> transcript;
  std::string pending_assistant_text;
  std::string pending_assistant_meta;
  std::string pending_reasoning_text;
  bool pending_reasoning_redacted = false;
  std::vector<PendingToolItem> pending_tools;
  std::vector<QueuedMessageItem> queued_messages;
  std::vector<SidebarActivityItem> activity;
  std::vector<SidebarModifiedFile> modified_files;
  std::optional<std::string> active_run_id = std::nullopt;
  std::optional<std::string> active_turn_id = std::nullopt;
  std::optional<std::string> active_message_id = std::nullopt;
  std::optional<std::string> active_request_id = std::nullopt;
  std::optional<std::string> active_correlation_id = std::nullopt;
  ava::agent::Mode current_mode = ava::agent::Mode::Build;
  std::string current_provider_id;
  std::string current_model_id;
  TuiEventRunStatus run_status = TuiEventRunStatus::Idle;
  std::string stop_reason;
  std::string error_text;
  std::string error_details;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;

  std::optional<std::size_t> stream_assistant_transcript_index = std::nullopt;
};

void apply_runtime_event(TuiEventState& state, ava::app::RuntimeEvent const& event);
void apply_event_envelope(TuiEventState& state, ava::app::EventEnvelope const& envelope);

[[nodiscard]] std::vector<TranscriptItem> event_state_transcript_snapshot(TuiEventState const& state);

}  // namespace ava::tui
