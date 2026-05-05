#include "ava/app/rpc/query_handlers.h"

#include "ava/app/rpc/serialization.h"

namespace ava::app::rpc {
namespace {

ava::core::VoidResult reject_active_run(RpcOutput& output, RpcRunState& run_state, RpcCommand const& command)
{
  if (active_run(run_state)) return write_error(output, command.id, active_run_reject_error(command.type));
  return {};
}

}  // namespace

ava::core::VoidResult handle_get_state_command(RpcOutput& output, RuntimeSession const& session,
                                               std::mutex& session_mutex, RpcRunState& run_state,
                                               RpcCommand const& command)
{
  bool const canceled = cancel_requested(run_state);
  std::lock_guard lock(session_mutex);
  return write_success(output, command.id, state_result_json(session, canceled));
}

ava::core::VoidResult handle_list_sessions_command(RpcOutput& output, RuntimeSession const& session,
                                                   std::mutex& session_mutex, RpcCommand const& command)
{
  std::lock_guard lock(session_mutex);
  auto sessions_json = list_sessions_result_json(session);
  if (!sessions_json) return write_error(output, command.id, sessions_json.error());
  return write_success(output, command.id, *sessions_json);
}

ava::core::VoidResult handle_list_models_command(RpcOutput& output, RuntimeSession const& session,
                                                 std::mutex& session_mutex, RpcCommand const& command)
{
  std::lock_guard lock(session_mutex);
  auto models_json = list_models_result_json(session);
  if (!models_json) return write_error(output, command.id, models_json.error());
  return write_success(output, command.id, *models_json);
}

ava::core::VoidResult handle_get_messages_command(RpcOutput& output, RuntimeSession const& session,
                                                  std::mutex& session_mutex, RpcRunState& run_state,
                                                  RpcCommand const& command)
{
  if (auto rejected = reject_active_run(output, run_state, command); !rejected) return rejected;
  std::lock_guard lock(session_mutex);
  auto messages_json = messages_result_json(session);
  if (!messages_json) return write_error(output, command.id, messages_json.error());
  return write_success(output, command.id, *messages_json);
}

ava::core::VoidResult handle_get_session_stats_command(RpcOutput& output, RuntimeSession const& session,
                                                       std::mutex& session_mutex, RpcRunState& run_state,
                                                       RpcCommand const& command)
{
  if (auto rejected = reject_active_run(output, run_state, command); !rejected) return rejected;
  std::lock_guard lock(session_mutex);
  auto stats_json = session_stats_result_json(session);
  if (!stats_json) return write_error(output, command.id, stats_json.error());
  return write_success(output, command.id, *stats_json);
}

ava::core::VoidResult handle_validate_session_command(RpcOutput& output, RuntimeSession const& session,
                                                      std::mutex& session_mutex, RpcRunState& run_state,
                                                      RpcCommand const& command)
{
  if (auto rejected = reject_active_run(output, run_state, command); !rejected) return rejected;
  std::lock_guard lock(session_mutex);
  auto validation_json = session_validation_result_json(session);
  if (!validation_json) return write_error(output, command.id, validation_json.error());
  return write_success(output, command.id, *validation_json);
}

}  // namespace ava::app::rpc
