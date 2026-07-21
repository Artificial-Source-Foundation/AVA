#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/agent/mode.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ava::app::runtime {

// Categorize the kind of runtime lifecycle or notification event being emitted, driving event-sink dispatch and payload selection.
enum class EventType
{
  SessionStart,
  UserMessage,
  AssistantMessage,
  MessageUpdate,
  MessageEnd,
  ReasoningStart,
  ReasoningDelta,
  ReasoningEnd,
  ProviderEvent,
  ToolStart,
  ToolProgress,
  ToolResult,
  CompactionStart,
  CompactionEnd,
  Retry,
  RetryTick,
  Canceled,
  Error,
  Done,
};

// One discrete runtime event for an agent run: its type, identifying and session fields, the active agent mode, and the broad set of text, flag and numeric
// fields consumed by event sinks, payload extraction and JSON serialization.
struct Event
{
  EventType type = EventType::Done;
  std::string timestamp;
  std::string session_id;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  std::string provider_id;
  std::string model_id;
  std::string text;
  std::string call_id;
  std::string tool_name;
  std::string tool_arguments_json;
  std::string tool_result_json;
  std::string tool_structured_result_json;
  std::string status;
  std::string error_category;
  std::string error_code;
  std::string error_message;
  std::string error_details;
  std::string content_type;
  std::string stop_reason;
  std::string trigger;
  std::string reason;
  std::string reasoning_format;
  std::string diff;
  std::vector<std::string> changed_paths;
  std::vector<std::string> permission_request_ids;
  std::string spill_path;
  bool reasoning_redacted = false;
  bool reasoning_signature_present = false;
  bool diff_truncated = false;
  bool truncated = false;
  bool byte_limited = false;
  bool line_limited = false;
  bool spill_truncated = false;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
  std::size_t attempt = 0;
  std::size_t max_attempts = 0;
  std::size_t delay_ms = 0;
  std::size_t remaining_ms = 0;
  std::size_t estimated_tokens = 0;
  std::size_t threshold_tokens = 0;
  std::size_t retained_tokens = 0;
  std::size_t post_compaction_tokens = 0;
  std::size_t summary_bytes = 0;
  std::size_t snapshot_entries = 0;
  std::size_t current_entries = 0;
  std::size_t output_bytes = 0;
  std::size_t total_bytes = 0;
  std::size_t output_lines = 0;
  std::size_t total_lines = 0;
  std::size_t start_line = 0;
  std::size_t end_line = 0;
  std::size_t next_offset_line = 0;
  std::size_t omitted_bytes = 0;
  std::size_t omitted_lines = 0;
  std::size_t visible_matches = 0;
  std::size_t total_matches = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
