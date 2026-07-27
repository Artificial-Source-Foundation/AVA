#pragma once

#include "EventEnvelope.h"
#include "EventEnvelopeContext.h"
#include "runtime/Event.h"
#include "ava/event/events.h"
#include "ava/core/result.h"

#include <string>

namespace ava::app::runtime {

struct Event;

// Migration-only aliases for app consumers that have not moved to ava::event yet.
using PayloadType = ava::event::PayloadType;
using SessionPayload = ava::event::SessionPayload;
using MessagePayload = ava::event::MessagePayload;
using ReasoningPayload = ava::event::ReasoningPayload;
using ProviderPayload = ava::event::ProviderPayload;
using ToolPayload = ava::event::ToolPayload;
using CompactionPayload = ava::event::CompactionPayload;
using RetryPayload = ava::event::RetryPayload;
using CancellationPayload = ava::event::CancellationPayload;
using ErrorPayload = ava::event::ErrorPayload;
using CompletionPayload = ava::event::CompletionPayload;

}  // namespace ava::app::runtime

namespace ava::app {

// Migration-only aliases and using-declarations preserve current app consumer syntax.
using EventEnvelopeSink = ava::event::EventEnvelopeSink;
using EventBus = ava::event::EventBus;
using ava::event::make_runtime_event_bus_adapter;
using ava::event::payload_type_for_event;
using ava::event::serialize_event_envelope_json;
using ava::event::serialize_event_envelope_jsonl;
using ava::event::serialize_payload_json;
using ava::event::to_string;

[[nodiscard]] runtime::SessionPayload session_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::MessagePayload message_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::ReasoningPayload reasoning_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::ProviderPayload provider_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::ToolPayload tool_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::CompactionPayload compaction_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::RetryPayload retry_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::CancellationPayload cancellation_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::ErrorPayload error_payload_from_event(runtime::Event const& event);
[[nodiscard]] runtime::CompletionPayload completion_payload_from_event(runtime::Event const& event);
[[nodiscard]] ava::event::RuntimeEvent to_runtime_event(runtime::Event const& event);

[[nodiscard]] std::string serialize_event_json(runtime::Event const& event);
[[nodiscard]] std::string serialize_event_jsonl(runtime::Event const& event);
[[nodiscard]] ava::core::VoidResult emit_event(ava::event::RuntimeEventSink const& sink, runtime::Event const& event);

[[nodiscard]] EventEnvelope to_event_envelope(runtime::Event const& event, EventEnvelopeContext const& context = {});

}  // namespace ava::app
