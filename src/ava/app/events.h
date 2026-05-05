#pragma once

#include "ava/agent/mode.h"

#include "ava/core/result.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ava::app {

enum class RuntimeEventType {
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

struct RuntimeEvent {
  RuntimeEventType type = RuntimeEventType::Done;
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
  bool spill_truncated = false;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
  std::size_t attempt = 0;
  std::size_t max_attempts = 0;
  std::size_t delay_ms = 0;
  std::size_t remaining_ms = 0;
  std::size_t estimated_tokens = 0;
  std::size_t threshold_tokens = 0;
  std::size_t summary_bytes = 0;
  std::size_t snapshot_entries = 0;
  std::size_t current_entries = 0;
  std::size_t output_bytes = 0;
  std::size_t total_bytes = 0;
  std::size_t omitted_bytes = 0;
  std::size_t omitted_lines = 0;
  std::size_t visible_matches = 0;
  std::size_t total_matches = 0;
};

using RuntimeEventSink = std::function<ava::core::VoidResult(RuntimeEvent const&)>;

struct EventEnvelope {
  int schema_version = 1;
  std::string event_id;
  std::string timestamp;
  std::string session_id;
  std::optional<std::string> run_id;
  std::optional<std::string> turn_id;
  std::optional<std::string> message_id;
  std::optional<std::string> request_id;
  std::optional<std::string> correlation_id;
  std::string name;
  std::string payload_json = "{}";
};

struct EventEnvelopeContext {
  std::optional<std::string> event_id;
  std::optional<std::string> run_id;
  std::optional<std::string> turn_id;
  std::optional<std::string> message_id;
  std::optional<std::string> request_id;
  std::optional<std::string> correlation_id;
};

using EventEnvelopeSink = std::function<ava::core::VoidResult(EventEnvelope const&)>;

class EventBus {
 public:
  void subscribe(EventEnvelopeSink sink);
  // Subscribers are called synchronously in registration order. Publishing stops on the first failure.
  [[nodiscard]] ava::core::VoidResult publish(EventEnvelope const& envelope) const;

 private:
  std::vector<EventEnvelopeSink> sinks_;
};

[[nodiscard]] std::string to_string(RuntimeEventType type);
[[nodiscard]] std::string serialize_event_json(RuntimeEvent const& event);
[[nodiscard]] std::string serialize_event_jsonl(RuntimeEvent const& event);
[[nodiscard]] ava::core::VoidResult emit_event(RuntimeEventSink const& sink, RuntimeEvent const& event);

[[nodiscard]] EventEnvelope to_event_envelope(RuntimeEvent const& event, EventEnvelopeContext const& context = {});
[[nodiscard]] std::string serialize_event_envelope_json(EventEnvelope const& envelope);
[[nodiscard]] std::string serialize_event_envelope_jsonl(EventEnvelope const& envelope);
// The returned sink captures `bus` by reference and must not outlive it.
[[nodiscard]] RuntimeEventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context = {},
                                                              RuntimeEventSink legacy_sink = nullptr);

}  // namespace ava::app
