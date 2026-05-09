#pragma once

#include "ava/app/events.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"

#include "ava/core/result.h"

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app::rpc {

struct ResolverEventPayload
{
  RuntimePayloadType payload_type = RuntimePayloadType::Queue;
  std::string json = "{}";
};

[[nodiscard]] ResolverEventPayload resolver_permission_payload(std::string payload_json);
[[nodiscard]] ResolverEventPayload resolver_question_payload(std::string payload_json);
[[nodiscard]] ResolverEventPayload resolver_queue_payload(std::string payload_json);

[[nodiscard]] ava::core::VoidResult write_record(RpcOutput& output, std::string_view record);
[[nodiscard]] ava::core::VoidResult write_success(RpcOutput& output, std::string_view id, std::string_view result_json);
[[nodiscard]] ava::core::VoidResult write_error(RpcOutput& output, std::string_view id, ava::core::Error const& error);

void subscribe_event_envelope_writer(EventBus& bus, RpcOutput& output);
[[nodiscard]] EventEnvelopeContext rpc_event_context(std::string_view request_id);
[[nodiscard]] std::string session_id_snapshot(RuntimeSession const& session, std::mutex& session_mutex);
[[nodiscard]] EventEnvelope resolver_event_envelope(std::string name, std::string request_id, std::string correlation_id, std::string session_id,
                                                    std::string payload_json);
[[nodiscard]] EventEnvelope resolver_event_envelope(std::string name, std::string request_id, std::string correlation_id, std::string session_id,
                                                    ResolverEventPayload payload);

[[nodiscard]] ava::core::VoidResult write_queue_event(RpcOutput& output, RuntimeSession const& session, std::mutex& session_mutex, std::string_view name,
                                                      QueuedRpcMessage const& queued, std::string_view reason = {});
[[nodiscard]] ava::core::VoidResult write_skipped_queue_events(RpcOutput& output, RuntimeSession const& session, std::mutex& session_mutex,
                                                               ClearedRpcQueues const& cleared, std::string_view reason);
[[nodiscard]] ava::core::VoidResult write_follow_up_errors(RpcOutput& output, std::vector<QueuedRpcMessage> const& follow_ups, std::string_view reason);

}  // namespace ava::app::rpc
