#pragma once

#include "ava/app/EventEnvelopeContext.h"
#include "ava/app/RuntimeEvent.h"

#include "ava/core/result.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct EventEnvelope;
struct RuntimeToolPayload;
struct RuntimeRetryPayload;
struct RuntimeCancellationPayload;
struct RuntimeErrorPayload;
struct RuntimeCompletionPayload;

enum class RuntimePayloadType
{
  Session,
  Message,
  Reasoning,
  Provider,
  Tool,
  Compaction,
  Retry,
  Cancellation,
  Error,
  Completion,
  Permission,
  Question,
  Queue,
};

using RuntimeEventSink = std::function<ava::core::VoidResult(RuntimeEvent const&)>;

using EventEnvelopeSink = std::function<ava::core::VoidResult(EventEnvelope const&)>;

class EventBus
{
 public:
  void subscribe(EventEnvelopeSink sink);
  // Subscribers are called synchronously in registration order. Publishing stops on the first failure.
  [[nodiscard]] ava::core::VoidResult publish(EventEnvelope const& envelope) const;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  std::vector<EventEnvelopeSink> sinks_;
};

[[nodiscard]] std::string to_string(RuntimeEventType type);
[[nodiscard]] std::string_view to_string(RuntimePayloadType type) noexcept;
[[nodiscard]] RuntimePayloadType payload_type_for_event(RuntimeEventType type) noexcept;
[[nodiscard]] RuntimeToolPayload tool_payload_from_event(RuntimeEvent const& event);
[[nodiscard]] RuntimeRetryPayload retry_payload_from_event(RuntimeEvent const& event);
[[nodiscard]] RuntimeCancellationPayload cancellation_payload_from_event(RuntimeEvent const& event);
[[nodiscard]] RuntimeErrorPayload error_payload_from_event(RuntimeEvent const& event);
[[nodiscard]] RuntimeCompletionPayload completion_payload_from_event(RuntimeEvent const& event);
[[nodiscard]] std::string serialize_payload_json(RuntimeToolPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(RuntimeRetryPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(RuntimeCancellationPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(RuntimeErrorPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(RuntimeCompletionPayload const& payload);
[[nodiscard]] std::string serialize_event_json(RuntimeEvent const& event);
[[nodiscard]] std::string serialize_event_jsonl(RuntimeEvent const& event);
[[nodiscard]] ava::core::VoidResult emit_event(RuntimeEventSink const& sink, RuntimeEvent const& event);

[[nodiscard]] EventEnvelope to_event_envelope(RuntimeEvent const& event, EventEnvelopeContext const& context = {});
[[nodiscard]] std::string serialize_event_envelope_json(EventEnvelope const& envelope);
[[nodiscard]] std::string serialize_event_envelope_jsonl(EventEnvelope const& envelope);
// The returned sink captures `bus` by reference and must not outlive it.
[[nodiscard]] RuntimeEventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context = {}, RuntimeEventSink legacy_sink = nullptr);

}  // namespace ava::app
