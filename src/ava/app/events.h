#pragma once

#include "EventEnvelopeContext.h"
#include "runtime/Event.h"       // runtime::EventType
#include "ava/core/result.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct EventEnvelope;

namespace runtime {
struct ToolPayload;
struct RetryPayload;
struct CancellationPayload;
struct ErrorPayload;
struct CompletionPayload;
struct Event;

using EventSink = std::function<ava::core::VoidResult(runtime::Event const&)>;

enum class PayloadType
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

[[nodiscard]] std::string to_string(runtime::EventType type);
[[nodiscard]] std::string_view to_string(runtime::PayloadType type) noexcept;
[[nodiscard]] runtime::PayloadType payload_type_for_event(runtime::EventType type) noexcept;
[[nodiscard]] runtime::ToolPayload tool_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::RetryPayload retry_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::CancellationPayload cancellation_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::ErrorPayload error_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::CompletionPayload completion_payload_from_event(runtime::Event const& event);
[[nodiscard]] std::string serialize_payload_json(runtime::ToolPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(runtime::RetryPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(runtime::CancellationPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(runtime::ErrorPayload const& payload);
[[nodiscard]] std::string serialize_payload_json(runtime::CompletionPayload const& payload);
[[nodiscard]] std::string serialize_event_json(runtime::Event const& event);
[[nodiscard]] std::string serialize_event_jsonl(runtime::Event const& event);
[[nodiscard]] ava::core::VoidResult emit_event(runtime::EventSink const& sink, runtime::Event const& event);

[[nodiscard]] EventEnvelope to_event_envelope(runtime::Event const& event, EventEnvelopeContext const& context = {});
[[nodiscard]] std::string serialize_event_envelope_json(EventEnvelope const& envelope);
[[nodiscard]] std::string serialize_event_envelope_jsonl(EventEnvelope const& envelope);
// The returned sink captures `bus` by reference and must not outlive it.
[[nodiscard]] runtime::EventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context = {}, runtime::EventSink legacy_sink = nullptr);

}  // namespace ava::app
