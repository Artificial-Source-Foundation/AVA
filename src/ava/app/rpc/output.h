#pragma once

#include "run_state.h"
#include "ava/app/runtime.h"
#include "ava/event/events.h"
#include "ava/core/result.h"

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app::rpc {

struct ResolverEventPayload
{
  ava::event::PayloadType payload_type = ava::event::PayloadType::Queue;
  std::string json = "{}";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ResolverEventPayload resolver_permission_payload(std::string payload_json);
[[nodiscard]] ResolverEventPayload resolver_question_payload(std::string payload_json);
[[nodiscard]] ResolverEventPayload resolver_queue_payload(std::string payload_json);

[[nodiscard]] ava::core::VoidResult write_success(output_ts& output, std::string_view id, std::string_view result_json);
[[nodiscard]] ava::core::VoidResult write_error(output_ts& output, std::string_view id, ava::core::Error const& error);

void subscribe_event_envelope_writer(ava::event::EventBus& bus, output_ts& output);
[[nodiscard]] ava::event::EventEnvelopeContext rpc_event_context(std::string_view request_id);
[[nodiscard]] std::string session_id_snapshot(runtime::Session const& session, std::mutex& session_mutex);
[[nodiscard]] ava::event::EventEnvelope resolver_event_envelope(std::string name, std::string request_id, std::string correlation_id, std::string session_id,
                                                               std::string payload_json);
[[nodiscard]] ava::event::EventEnvelope resolver_event_envelope(std::string name, std::string request_id, std::string correlation_id, std::string session_id,
                                                               ResolverEventPayload payload);

[[nodiscard]] ava::core::VoidResult write_queue_event(output_ts& output, runtime::Session const& session, std::mutex& session_mutex, std::string_view name,
                                                      QueuedRpcMessage const& queued, std::string_view reason = {});
[[nodiscard]] ava::core::VoidResult write_skipped_queue_events(output_ts& output, runtime::Session const& session, std::mutex& session_mutex,
                                                               ClearedRpcQueues const& cleared, std::string_view reason);
[[nodiscard]] ava::core::VoidResult write_follow_up_errors(output_ts& output, RpcRunState& run_state, std::vector<QueuedRpcMessage> const& follow_ups,
                                                           std::string_view reason);

}  // namespace ava::app::rpc
