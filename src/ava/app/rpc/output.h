#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include "ava/app/events.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app::rpc {

[[nodiscard]] ava::core::VoidResult write_record(RpcOutput& output, std::string_view record);
[[nodiscard]] ava::core::VoidResult write_success(RpcOutput& output, std::string_view id, std::string_view result_json);
[[nodiscard]] ava::core::VoidResult write_error(RpcOutput& output, std::string_view id, const ava::core::Error& error);

void subscribe_event_envelope_writer(EventBus& bus, RpcOutput& output);
[[nodiscard]] EventEnvelopeContext rpc_event_context(std::string_view request_id);
[[nodiscard]] std::string session_id_snapshot(const RuntimeSession& session, std::mutex& session_mutex);
[[nodiscard]] EventEnvelope resolver_event_envelope(std::string name, std::string request_id,
                                                    std::string correlation_id, std::string session_id,
                                                    std::string payload_json);

[[nodiscard]] ava::core::VoidResult write_queue_event(RpcOutput& output, const RuntimeSession& session,
                                                      std::mutex& session_mutex, std::string_view name,
                                                      const QueuedRpcMessage& queued, std::string_view reason = {});
[[nodiscard]] ava::core::VoidResult write_skipped_queue_events(RpcOutput& output, const RuntimeSession& session,
                                                               std::mutex& session_mutex,
                                                               const ClearedRpcQueues& cleared,
                                                               std::string_view reason);

}  // namespace ava::app::rpc
