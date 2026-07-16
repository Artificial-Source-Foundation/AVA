#pragma once

#include "EventEnvelopeContext.h"
#include "runtime/RuntimeEvent.h"       // runtime::RuntimeEventType

#include "ava/core/result.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct EventEnvelope;

namespace runtime {
struct RuntimeToolPayload;
struct RuntimeRetryPayload;
struct RuntimeCancellationPayload;
struct RuntimeErrorPayload;
struct RuntimeCompletionPayload;
struct RuntimeEvent;

using RuntimeEventSink = std::function<ava::core::VoidResult(runtime::RuntimeEvent const&)>;

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

} // namespace runtime

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

[[nodiscard]] std::string to_string(runtime::RuntimeEventType type);
[[nodiscard]] std::string_view to_string(runtime::RuntimePayloadType type) noexcept;
[[nodiscard]] runtime::RuntimePayloadType payload_type_for_event(runtime::RuntimeEventType type) noexcept;
[[nodiscard]] runtime::RuntimeToolPayload tool_payload_from_event(runtime::RuntimeEvent const& event);
[[nodiscard]] runtime::RuntimeRetryPayload retry_payload_from_event(runtime::RuntimeEvent const& event);
[[nodiscard]] runtime::RuntimeCancellationPayload cancellation_payload_from_event(runtime::RuntimeEvent const& event);
[[nodiscard]] runtime::RuntimeErrorPayload error_payload_from_event(runtime::RuntimeEvent const& event);
[[nodiscard]] runtime::RuntimeCompletionPayload completion_payload_from_event(runtime::RuntimeEvent const& event);
[[nodiscard]] std::string serialize_payload_json(runtime::RuntimeToolPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(runtime::RuntimeRetryPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(runtime::RuntimeCancellationPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(runtime::RuntimeErrorPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(runtime::RuntimeCompletionPayload const& payload);
[[nodiscard]] std::string serialize_event_json(runtime::RuntimeEvent const& event);
[[nodiscard]] std::string serialize_event_jsonl(runtime::RuntimeEvent const& event);
[[nodiscard]] ava::core::VoidResult emit_event(runtime::RuntimeEventSink const& sink, runtime::RuntimeEvent const& event);

[[nodiscard]] EventEnvelope to_event_envelope(runtime::RuntimeEvent const& event, EventEnvelopeContext const& context = {});
[[nodiscard]] std::string serialize_event_envelope_json(EventEnvelope const& envelope);
[[nodiscard]] std::string serialize_event_envelope_jsonl(EventEnvelope const& envelope);
// The returned sink captures `bus` by reference and must not outlive it.
[[nodiscard]] runtime::RuntimeEventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context = {}, runtime::RuntimeEventSink legacy_sink = nullptr);

}  // namespace ava::app
