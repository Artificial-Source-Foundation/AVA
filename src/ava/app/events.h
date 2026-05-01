#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ava/agent/mode.h"
#include "ava/core/result.h"

namespace ava::app {

enum class RuntimeEventType {
  SessionStart,
  UserMessage,
  AssistantMessage,
  MessageUpdate,
  MessageEnd,
  ProviderEvent,
  ToolStart,
  ToolResult,
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
  std::string status;
  std::string error_category;
  std::string error_message;
  std::string error_details;
  std::string stop_reason;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
};

using RuntimeEventSink = std::function<ava::core::VoidResult(const RuntimeEvent&)>;

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

using EventEnvelopeSink = std::function<ava::core::VoidResult(const EventEnvelope&)>;

class EventBus {
 public:
  void subscribe(EventEnvelopeSink sink);
  // Subscribers are called synchronously in registration order. Publishing stops on the first failure.
  [[nodiscard]] ava::core::VoidResult publish(const EventEnvelope& envelope) const;

 private:
  std::vector<EventEnvelopeSink> sinks_;
};

[[nodiscard]] std::string to_string(RuntimeEventType type);
[[nodiscard]] std::string serialize_event_json(const RuntimeEvent& event);
[[nodiscard]] std::string serialize_event_jsonl(const RuntimeEvent& event);
[[nodiscard]] ava::core::VoidResult emit_event(const RuntimeEventSink& sink, const RuntimeEvent& event);

[[nodiscard]] EventEnvelope to_event_envelope(const RuntimeEvent& event, const EventEnvelopeContext& context = {});
[[nodiscard]] std::string serialize_event_envelope_json(const EventEnvelope& envelope);
[[nodiscard]] std::string serialize_event_envelope_jsonl(const EventEnvelope& envelope);
// The returned sink captures `bus` by reference and must not outlive it.
[[nodiscard]] RuntimeEventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context = {},
                                                              RuntimeEventSink legacy_sink = nullptr);

}  // namespace ava::app
