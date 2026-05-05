#include "ava/app/rpc/runtime_handlers.h"

#include <optional>
#include <utility>

#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/serialization.h"

namespace ava::app::rpc {

ava::core::VoidResult handle_model_command(RpcOutput& output, RuntimeSession& session, std::mutex& session_mutex,
                                           RpcRunState& run_state, RpcCommand const& command)
{
  if (active_run(run_state)) return write_error(output, command.id, active_run_reject_error(command.type));

  std::lock_guard lock(session_mutex);
  ava::core::Result<ava::config::ModelInfo> selected =
      command.type == "set_model" ? resolve_requested_model(session, command) : next_runtime_model(session);
  if (!selected) return write_error(output, command.id, selected.error());

  auto switched = switch_runtime_model(session, std::move(*selected));
  if (!switched) return write_error(output, command.id, switched.error());
  return write_success(output, command.id, state_result_json(session, cancel_requested(run_state)));
}

ava::core::VoidResult handle_reasoning_command(RpcOutput& output, RuntimeSession& session, std::mutex& session_mutex,
                                               RpcRunState& run_state, RpcCommand const& command)
{
  if (active_run(run_state)) return write_error(output, command.id, active_run_reject_error(command.type));

  std::optional<RuntimeReasoningSelection> selection = std::nullopt;
  if (command.type == "set_reasoning") {
    if (!command.reasoning_level || command.reasoning_level->empty()) {
      return write_error(output, command.id, invalid_rpc("set_reasoning requires reasoning_level"));
    }
    selection = RuntimeReasoningSelection{.level = *command.reasoning_level,
                                          .budget_tokens = command.reasoning_budget_tokens,
                                          .display = command.reasoning_display.value_or("")};
  }

  std::lock_guard lock(session_mutex);
  auto changed = set_runtime_reasoning(session, std::move(selection));
  if (!changed) return write_error(output, command.id, changed.error());
  return write_success(output, command.id, state_result_json(session, cancel_requested(run_state)));
}

}  // namespace ava::app::rpc
