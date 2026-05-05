#include "ava/app/rpc/control_handlers.h"

#include <string>

#include "ava/app/events.h"
#include "ava/app/rpc/serialization.h"

namespace ava::app::rpc {

ava::core::VoidResult write_follow_up_errors(RpcOutput& output, std::vector<QueuedRpcMessage> const& follow_ups,
                                             std::string_view reason)
{
  for (auto const& queued : follow_ups) {
    auto const error = reason == "canceled" ? canceled_error() : skipped_follow_up_error(reason);
    if (auto written = write_error(output, queued.request_id, error); !written) return written;
  }
  return {};
}

ava::core::VoidResult handle_steer_command(RpcOutput& output, RuntimeSession const& session, std::mutex& session_mutex,
                                           RpcRunState& run_state, RpcCommand const& command)
{
  if (!command.message) {
    return write_error(output, command.id, invalid_rpc("steer requires message"));
  }
  auto queued = queue_rpc_message(run_state.steering_messages, run_state, command.type, command.id, *command.message);
  if (!queued) return write_error(output, command.id, queued.error());
  if (auto written = write_queue_event(output, session, session_mutex, "steer_queued", *queued); !written) {
    return written;
  }

  std::string json = "{";
  json += bool_field_json("queued", true);
  json += ',';
  json += string_field_json("correlation_id", queued->correlation_id);
  json += '}';
  return write_success(output, command.id, json);
}

ava::core::VoidResult handle_follow_up_command(RpcOutput& output, RuntimeSession const& session,
                                               std::mutex& session_mutex, RpcRunState& run_state,
                                               RpcCommand const& command)
{
  if (!command.message) {
    return write_error(output, command.id, invalid_rpc("follow_up requires message"));
  }
  auto queued = queue_rpc_message(run_state.follow_up_messages, run_state, command.type, command.id, *command.message);
  if (!queued) return write_error(output, command.id, queued.error());
  return write_queue_event(output, session, session_mutex, "follow_up_queued", *queued);
}

ava::core::VoidResult handle_cancel_command(RpcOutput& output, RuntimeSession const& session, std::mutex& session_mutex,
                                            RpcRunState& run_state, PendingResolverState& pending_state,
                                            RpcCommand const& command)
{
  auto snapshot = request_cancel_and_clear_queued_messages(run_state);
  static_cast<void>(cancel_pending_resolvers(pending_state));
  if (auto written = write_skipped_queue_events(output, session, session_mutex, snapshot.cleared, "canceled");
      !written) {
    return written;
  }
  auto cancel_event = resolver_event_envelope(
      "cancel_requested", command.id, snapshot.active_request_id.empty() ? command.id : snapshot.active_request_id,
      session_id_snapshot(session, session_mutex),
      cancel_requested_payload_json(snapshot.was_active, snapshot.cleared.steering_messages.size(),
                                    snapshot.cleared.follow_up_messages.size(), snapshot.active_request_id));
  if (auto written = write_record(output, serialize_event_envelope_jsonl(cancel_event)); !written) {
    return written;
  }

  std::string json = "{";
  json += bool_field_json("cancel_requested", true);
  json += ',';
  json += bool_field_json("active_run", snapshot.was_active);
  json += ',';
  json += number_field_json("cleared_steer", snapshot.cleared.steering_messages.size());
  json += ',';
  json += number_field_json("cleared_follow_up", snapshot.cleared.follow_up_messages.size());
  json += '}';
  if (auto written = write_success(output, command.id, json); !written) return written;
  return write_follow_up_errors(output, snapshot.cleared.follow_up_messages, "canceled");
}

}  // namespace ava::app::rpc
