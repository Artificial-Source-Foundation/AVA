#include "ava/app/rpc/session_handlers.h"

#include <atomic>
#include <utility>

#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/serialization.h"

namespace ava::app::rpc {
namespace {

void reset_cancel_requested(RpcRunState& run_state)
{
  std::lock_guard state_lock(run_state.mutex);
  run_state.cancel_requested.store(false, std::memory_order_relaxed);
}

}  // namespace

ava::core::VoidResult handle_new_session_command(RpcOutput& output, RuntimeSession& session,
                                                 RuntimeOpenOptions const& open_options, std::mutex& session_mutex,
                                                 RpcRunState& run_state, RpcCommand const& command)
{
  if (active_run(run_state)) return write_error(output, command.id, active_run_reject_error(command.type));

  std::lock_guard lock(session_mutex);
  auto created = create_new_session(session, open_options);
  if (!created) return write_error(output, command.id, created.error());
  session = std::move(*created);
  reset_cancel_requested(run_state);
  return write_success(output, command.id, state_result_json(session, false));
}

ava::core::VoidResult handle_open_session_command(RpcOutput& output, RuntimeSession& session,
                                                  RuntimeOpenOptions const& open_options, std::mutex& session_mutex,
                                                  RpcRunState& run_state, RpcCommand const& command)
{
  if (!command.session_id || command.session_id->empty()) {
    return write_error(output, command.id, invalid_rpc(command.type + " requires session_id"));
  }
  if (active_run(run_state)) return write_error(output, command.id, active_run_reject_error(command.type));

  std::lock_guard lock(session_mutex);
  auto opened = open_requested_session(session, open_options, *command.session_id);
  if (!opened) return write_error(output, command.id, opened.error());
  session = std::move(*opened);
  reset_cancel_requested(run_state);
  return write_success(output, command.id, state_result_json(session, false));
}

}  // namespace ava::app::rpc
