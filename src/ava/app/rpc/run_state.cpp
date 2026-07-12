#include "sys.h"
#include "protocol.h"
#include "run_state.h"

#include <utility>

namespace ava::app::rpc {
namespace {

ava::core::Error requires_active_prompt_error(std::string_view command_type)
{
  auto error = invalid_rpc("RPC command requires an active prompt");
  error.with_context("type", std::string(command_type));
  return error;
}

ava::core::Error input_closed_error(std::string_view command_type)
{
  auto error = invalid_rpc("RPC input is closed");
  error.with_context("type", std::string(command_type));
  return error;
}

ava::core::Error queue_limit_error(std::string_view command_type)
{
  auto error = invalid_rpc("RPC queued message limit exceeded");
  error.with_context("type", std::string(command_type));
  error.with_context("max_entries", std::to_string(kMaxRpcQueuedMessages));
  error.with_context("max_message_bytes", std::to_string(kMaxRpcQueuedMessageBytes));
  return error;
}

std::size_t queued_message_bytes(std::deque<QueuedRpcMessage> const& queue)
{
  std::size_t bytes = 0;
  for (auto const& queued : queue) bytes += queued.message.size();
  return bytes;
}

ClearedRpcQueues clear_queued_messages_locked(RpcRunState& state)
{
  ClearedRpcQueues cleared;
  cleared.steering_messages.reserve(state.steering_messages.size());
  while (!state.steering_messages.empty())
  {
    cleared.steering_messages.push_back(std::move(state.steering_messages.front()));
    state.steering_messages.pop_front();
  }
  cleared.follow_up_messages.reserve(state.follow_up_messages.size());
  while (!state.follow_up_messages.empty())
  {
    cleared.follow_up_messages.push_back(std::move(state.follow_up_messages.front()));
    state.follow_up_messages.pop_front();
  }
  return cleared;
}

}  // namespace

ava::core::Error canceled_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
}

ava::core::Error skipped_follow_up_error(std::string_view reason)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "queued follow_up skipped");
  error.with_context("reason", std::string(reason));
  return error;
}

ava::core::Error active_run_reject_error(std::string_view command_type)
{
  auto error = invalid_rpc("RPC command is unavailable while a prompt is active");
  error.with_context("type", std::string(command_type));
  return error;
}

bool cancel_requested(RpcRunState& state)
{
  std::lock_guard lock(state.mutex);
  return state.cancel_requested.load(std::memory_order_relaxed);
}

bool active_run(RpcRunState& state)
{
  std::lock_guard lock(state.mutex);
  return state.active_run;
}

bool input_closed(RpcRunState& state)
{
  std::lock_guard lock(state.mutex);
  return state.input_closed;
}

void set_active_run(RpcRunState& state, bool active, std::string request_id)
{
  std::lock_guard lock(state.mutex);
  state.active_run = active;
  if (active)
    state.cancel_requested.store(false, std::memory_order_relaxed);
  state.active_request_id = active ? std::move(request_id) : std::string{};
}

void set_active_request_id(RpcRunState& state, std::string request_id)
{
  std::lock_guard lock(state.mutex);
  state.active_run = true;
  state.active_request_id = std::move(request_id);
}

ava::core::Result<QueuedRpcMessage> queue_rpc_message(std::deque<QueuedRpcMessage>& queue, RpcRunState& state, std::string command_type, std::string request_id,
                                                      std::string message)
{
  std::lock_guard lock(state.mutex);
  if (state.input_closed)
    return std::unexpected(input_closed_error(command_type));
  if (state.cancel_requested.load(std::memory_order_relaxed))
    return std::unexpected(canceled_error());
  if (!state.active_run || state.active_request_id.empty())
  {
    return std::unexpected(requires_active_prompt_error(command_type));
  }
  if (queue.size() >= kMaxRpcQueuedMessages || message.size() > kMaxRpcQueuedMessageBytes ||
      queued_message_bytes(queue) + message.size() > kMaxRpcQueuedMessageBytes)
  {
    return std::unexpected(queue_limit_error(command_type));
  }
  QueuedRpcMessage queued{.request_id = std::move(request_id), .correlation_id = state.active_request_id, .message = std::move(message)};
  queue.push_back(queued);
  return queued;
}

std::vector<QueuedRpcMessage> take_queued_steering_messages(RpcRunState& state, std::string_view correlation_id)
{
  std::lock_guard lock(state.mutex);
  std::vector<QueuedRpcMessage> queued;
  std::deque<QueuedRpcMessage> remaining;
  while (!state.steering_messages.empty())
  {
    if (state.steering_messages.front().correlation_id == correlation_id)
    {
      queued.push_back(std::move(state.steering_messages.front()));
    }
    else
    {
      remaining.push_back(std::move(state.steering_messages.front()));
    }
    state.steering_messages.pop_front();
  }
  state.steering_messages = std::move(remaining);
  return queued;
}

std::optional<QueuedRpcMessage> take_next_follow_up_message(RpcRunState& state)
{
  std::lock_guard lock(state.mutex);
  if (state.cancel_requested.load(std::memory_order_relaxed) || state.input_closed)
    return std::nullopt;
  if (state.follow_up_messages.empty())
    return std::nullopt;
  auto queued = std::move(state.follow_up_messages.front());
  state.follow_up_messages.pop_front();
  return queued;
}

std::vector<QueuedRpcMessage> clear_queued_steering_messages(RpcRunState& state)
{
  std::lock_guard lock(state.mutex);
  std::vector<QueuedRpcMessage> cleared;
  cleared.reserve(state.steering_messages.size());
  while (!state.steering_messages.empty())
  {
    cleared.push_back(std::move(state.steering_messages.front()));
    state.steering_messages.pop_front();
  }
  return cleared;
}

ClearedRpcQueues deactivate_and_clear_queued_messages(RpcRunState& state)
{
  std::lock_guard lock(state.mutex);
  state.active_run = false;
  state.active_request_id.clear();
  return clear_queued_messages_locked(state);
}

ClearedRpcQueues close_input_and_cancel(RpcRunState& state)
{
  std::lock_guard lock(state.mutex);
  state.input_closed = true;
  state.cancel_requested.store(true, std::memory_order_relaxed);
  return clear_queued_messages_locked(state);
}

void record_async_error(RpcRunState& state, ava::core::Error error)
{
  std::lock_guard lock(state.mutex);
  if (!state.async_error)
    state.async_error = std::move(error);
}

std::optional<ava::core::Error> take_async_error(RpcRunState& state)
{
  std::lock_guard lock(state.mutex);
  auto error = std::move(state.async_error);
  state.async_error.reset();
  return error;
}

}  // namespace ava::app::rpc
