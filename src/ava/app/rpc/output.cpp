#include "sys.h"
#include "output.h"
#include "protocol.h"
#include "serialization.h"
#include "ava/app/EventEnvelope.h"
#include "ava/core/ids.h"

#include <utility>
#include "debug.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

namespace ava::app::rpc {
namespace {

std::string payload_type_for_resolver_event(std::string_view name)
{
  if (name == "permission_requested" || name == "permission_replied" || name == "permission_rule_added" || name == "permission_rule_removed" ||
      name == "permission_grant_revoked" || name == "permission_grants_cleared")
  {
    return std::string(ava::app::to_string(RuntimePayloadType::Permission));
  }
  if (name == "question_requested" || name == "question_replied")
  {
    return std::string(ava::app::to_string(RuntimePayloadType::Question));
  }
  if (name == "cancel_requested")
  {
    return std::string(ava::app::to_string(RuntimePayloadType::Cancellation));
  }
  if (name == "steer_queued" || name == "steer_applied" || name == "steer_skipped" || name == "follow_up_queued" || name == "follow_up_started" ||
      name == "follow_up_skipped")
  {
    return std::string(ava::app::to_string(RuntimePayloadType::Queue));
  }
  return {};
}

}  // namespace

ResolverEventPayload resolver_permission_payload(std::string payload_json)
{
  return ResolverEventPayload{.payload_type = RuntimePayloadType::Permission, .json = std::move(payload_json)};
}

ResolverEventPayload resolver_question_payload(std::string payload_json)
{
  return ResolverEventPayload{.payload_type = RuntimePayloadType::Question, .json = std::move(payload_json)};
}

ResolverEventPayload resolver_queue_payload(std::string payload_json)
{
  return ResolverEventPayload{.payload_type = RuntimePayloadType::Queue, .json = std::move(payload_json)};
}

//static
ava::core::VoidResult Output::write_record(output_ts& output, std::string_view record)
{
  DoutEntering(dc::rpc, "Output::write_record(output [" << (void*)&output << "], JSON-record:[" << libcwd::buf2str(record.data(), record.length()) << "])");

  {
    output_ts::wat output_w(output);

    if (output_w->out_ << record << std::flush)
      return {};

    if (output_w->on_write_failure_)
    {
      // The on_write_failure handler touches run_state/pending_state mutexes; running it under the
      // output lock would invert lock order against paths that hold those mutexes and then write.
      // Copy the handler out while still locked, drop the output lock, then invoke the handler lock-free.
      auto on_write_failure_copy = output_w->on_write_failure_;
      output_w.unlock();
      on_write_failure_copy();
    }
  }

  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write RPC JSONL record"));
}

ava::core::VoidResult write_success(output_ts& output, std::string_view id, std::string_view result_json)
{
  return Output::write_record(output, ava::app::serialize_rpc_success_jsonl(id, result_json));
}

ava::core::VoidResult write_error(output_ts& output, std::string_view id, ava::core::Error const& error)
{
  return Output::write_record(output, ava::app::serialize_rpc_error_jsonl(id, error));
}

void subscribe_event_envelope_writer(EventBus& bus, output_ts& output)
{
  bus.subscribe([&output](EventEnvelope const& envelope) { return Output::write_record(output, serialize_event_envelope_jsonl(envelope)); });
}

EventEnvelopeContext rpc_event_context(std::string_view request_id)
{
  auto const id = std::string(request_id);
  EventEnvelopeContext context;
  context.request_id = id;
  context.correlation_id = id;
  return context;
}

std::string session_id_snapshot(RuntimeSession const& session, std::mutex& session_mutex)
{
  std::lock_guard lock(session_mutex);
  return session.store.session_id();
}

EventEnvelope resolver_event_envelope(std::string name, std::string request_id, std::string correlation_id, std::string session_id, std::string payload_json)
{
  EventEnvelope envelope;
  envelope.schema_version = 1;
  envelope.event_id = ava::core::make_id("event");
  envelope.timestamp = ava::session::now_timestamp();
  envelope.session_id = std::move(session_id);
  envelope.request_id = std::move(request_id);
  envelope.correlation_id = std::move(correlation_id);
  envelope.name = std::move(name);
  envelope.payload_type = payload_type_for_resolver_event(envelope.name);
  envelope.payload_json = std::move(payload_json);
  return envelope;
}

EventEnvelope resolver_event_envelope(std::string name, std::string request_id, std::string correlation_id, std::string session_id,
                                      ResolverEventPayload payload)
{
  EventEnvelope envelope;
  envelope.schema_version = 1;
  envelope.event_id = ava::core::make_id("event");
  envelope.timestamp = ava::session::now_timestamp();
  envelope.session_id = std::move(session_id);
  envelope.request_id = std::move(request_id);
  envelope.correlation_id = std::move(correlation_id);
  envelope.name = std::move(name);
  envelope.payload_type = std::string(ava::app::to_string(payload.payload_type));
  envelope.payload_json = std::move(payload.json);
  return envelope;
}

ava::core::VoidResult write_queue_event(output_ts& output, RuntimeSession const& session, std::mutex& session_mutex, std::string_view name,
                                        QueuedRpcMessage const& queued, std::string_view reason)
{
  auto envelope = resolver_event_envelope(std::string(name), queued.request_id, queued.correlation_id, session_id_snapshot(session, session_mutex),
                                          queued_message_payload_json(queued.message, reason));
  return Output::write_record(output, serialize_event_envelope_jsonl(envelope));
}

ava::core::VoidResult write_skipped_queue_events(output_ts& output, RuntimeSession const& session, std::mutex& session_mutex, ClearedRpcQueues const& cleared,
                                                 std::string_view reason)
{
  for (auto const& queued : cleared.steering_messages)
  {
    if (auto written = write_queue_event(output, session, session_mutex, "steer_skipped", queued, reason); !written)
    {
      return written;
    }
  }
  for (auto const& queued : cleared.follow_up_messages)
  {
    if (auto written = write_queue_event(output, session, session_mutex, "follow_up_skipped", queued, reason); !written)
    {
      return written;
    }
  }
  return {};
}

ava::core::VoidResult write_follow_up_errors(output_ts& output, std::vector<QueuedRpcMessage> const& follow_ups, std::string_view reason)
{
  for (auto const& queued : follow_ups)
  {
    auto const error = reason == "canceled" ? canceled_error() : skipped_follow_up_error(reason);
    if (auto written = write_error(output, queued.request_id, error); !written)
      return written;
  }
  return {};
}

}  // namespace ava::app::rpc
