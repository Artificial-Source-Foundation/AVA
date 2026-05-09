#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/output.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc/session_commands.h"

#include <optional>
#include <utility>

namespace ava::app::rpc {
namespace {

ava::core::Result<bool> handled(ava::core::VoidResult result)
{
  if (!result)
    return std::unexpected(std::move(result.error()));
  return true;
}

ava::core::Result<bool> reject_active_run_if_needed(RpcSessionCommandContext const& context)
{
  if (!active_run(context.run_state))
    return false;
  return handled(write_error(context.output, context.command.id, active_run_reject_error(context.command.type)));
}

void reset_cancel_after_session_switch(RpcRunState& run_state)
{
  std::lock_guard state_lock(run_state.mutex);
  run_state.cancel_requested.store(false, std::memory_order_relaxed);
}

}  // namespace

ava::core::Result<bool> handle_session_rpc_command(RpcSessionCommandContext context)
{
  auto const& command = context.command;

  if (command.type == "get_state")
  {
    bool const canceled = cancel_requested(context.run_state);
    std::lock_guard lock(context.session_mutex);
    return handled(write_success(context.output, command.id, state_result_json(context.session, canceled)));
  }

  if (command.type == "list_sessions")
  {
    std::lock_guard lock(context.session_mutex);
    auto sessions_json = list_sessions_result_json(context.session);
    if (!sessions_json)
      return handled(write_error(context.output, command.id, sessions_json.error()));
    return handled(write_success(context.output, command.id, *sessions_json));
  }

  if (command.type == "list_models")
  {
    std::lock_guard lock(context.session_mutex);
    auto models_json = list_models_result_json(context.session);
    if (!models_json)
      return handled(write_error(context.output, command.id, models_json.error()));
    return handled(write_success(context.output, command.id, *models_json));
  }

  if (command.type == "get_messages")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto messages_json = messages_result_json(context.session);
    if (!messages_json)
      return handled(write_error(context.output, command.id, messages_json.error()));
    return handled(write_success(context.output, command.id, *messages_json));
  }

  if (command.type == "get_session_stats")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto stats_json = session_stats_result_json(context.session);
    if (!stats_json)
      return handled(write_error(context.output, command.id, stats_json.error()));
    return handled(write_success(context.output, command.id, *stats_json));
  }

  if (command.type == "validate_session")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto validation_json = session_validation_result_json(context.session);
    if (!validation_json)
      return handled(write_error(context.output, command.id, validation_json.error()));
    return handled(write_success(context.output, command.id, *validation_json));
  }

  if (command.type == "set_model" || command.type == "cycle_model")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    ava::core::Result<ava::config::ModelInfo> selected =
        command.type == "set_model" ? resolve_requested_model(context.session, command) : next_runtime_model(context.session);
    if (!selected)
      return handled(write_error(context.output, command.id, selected.error()));

    auto switched = switch_runtime_model(context.session, std::move(*selected));
    if (!switched)
      return handled(write_error(context.output, command.id, switched.error()));
    return handled(write_success(context.output, command.id, state_result_json(context.session, cancel_requested(context.run_state))));
  }

  if (command.type == "set_reasoning" || command.type == "clear_reasoning")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::optional<RuntimeReasoningSelection> selection = std::nullopt;
    if (command.type == "set_reasoning")
    {
      if (!command.reasoning_level || command.reasoning_level->empty())
      {
        return handled(write_error(context.output, command.id, invalid_rpc("set_reasoning requires reasoning_level")));
      }
      selection = RuntimeReasoningSelection{
          .level = *command.reasoning_level, .budget_tokens = command.reasoning_budget_tokens, .display = command.reasoning_display.value_or("")};
    }

    std::lock_guard lock(context.session_mutex);
    auto changed = set_runtime_reasoning(context.session, std::move(selection));
    if (!changed)
      return handled(write_error(context.output, command.id, changed.error()));
    return handled(write_success(context.output, command.id, state_result_json(context.session, cancel_requested(context.run_state))));
  }

  if (command.type == "new_session")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto created = create_new_session(context.session, context.open_options);
    if (!created)
      return handled(write_error(context.output, command.id, created.error()));
    context.session = std::move(*created);
    reset_cancel_after_session_switch(context.run_state);
    return handled(write_success(context.output, command.id, state_result_json(context.session, false)));
  }

  if (command.type == "open_session" || command.type == "switch_session")
  {
    if (!command.session_id || command.session_id->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc(command.type + " requires session_id")));
    }
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto opened = open_requested_session(context.session, context.open_options, *command.session_id);
    if (!opened)
      return handled(write_error(context.output, command.id, opened.error()));
    context.session = std::move(*opened);
    reset_cancel_after_session_switch(context.run_state);
    return handled(write_success(context.output, command.id, state_result_json(context.session, false)));
  }

  return false;
}

}  // namespace ava::app::rpc
