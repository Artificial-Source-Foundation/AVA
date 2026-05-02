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
  ToolTimelineItem item;
};

struct TuiEventState {
  std::vector<TranscriptItem> transcript;
  std::string pending_assistant_text;
  std::vector<PendingToolItem> pending_tools;
  std::vector<SidebarActivityItem> activity;
  std::vector<SidebarModifiedFile> modified_files;
  TuiEventRunStatus run_status = TuiEventRunStatus::Idle;
  std::string stop_reason;
  std::string error_text;
  std::string error_details;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;

  // RuntimeEvent is sufficient for the current single-turn TUI path. EventEnvelope can be introduced later if the
  // live TUI needs richer run/turn/message correlation.
  std::optional<std::size_t> stream_assistant_transcript_index = std::nullopt;
};

void apply_runtime_event(TuiEventState& state, const ava::app::RuntimeEvent& event);

[[nodiscard]] std::vector<TranscriptItem> event_state_transcript_snapshot(const TuiEventState& state);

}  // namespace ava::tui
