#pragma once

#include "ava/core/result.h"

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>
#include "threadsafe/threadsafe.h"

namespace ava::app::rpc {

// Forward declaration.
class Output;

// Output sink for RPC JSONL records.
using output_ts = threadsafe::Unlocked<Output, threadsafe::policy::Primitive<std::mutex>>;

class Output
{
 private:
  std::ostream& out_;                           // The stream to write new-line delimited JSON records to.
  std::function<void()> on_write_failure_;      // Called when the stream goes bad.

 public:
  // Construct an Output object for writing to `out`.
  //
  // Note: `on_write_failure` must not attempt to write to this same Output, even though
  // the lock on this object was dropped (because it touches other mutexes).
  explicit Output(std::ostream& out, std::function<void()> on_write_failure) : out_(out), on_write_failure_(std::move(on_write_failure)) { }

  // Write one JSONL record to the stream and flush.
  //
  // `record` must be a a complete JSONL record not containing any new-lines, also not a trailing new-line.
  //
  // Calls on_write_failure_ (if set) and returns an IO error when the stream is left in a bad state.
  [[nodiscard]] static ava::core::VoidResult write_record(output_ts& output, std::string_view record);

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct QueuedRpcMessage
{
  std::string request_id;
  std::string correlation_id;
  std::string message;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ClearedRpcQueues
{
  std::vector<QueuedRpcMessage> steering_messages;
  std::vector<QueuedRpcMessage> follow_up_messages;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RpcRunState
{
  std::mutex mutex;
  std::atomic_bool cancel_requested = false;
  bool active_run = false;
  bool input_closed = false;
  std::string active_request_id;
  std::deque<QueuedRpcMessage> steering_messages;
  std::deque<QueuedRpcMessage> follow_up_messages;
  std::optional<ava::core::Error> async_error;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Error canceled_error();
[[nodiscard]] ava::core::Error skipped_follow_up_error(std::string_view reason);
[[nodiscard]] ava::core::Error active_run_reject_error(std::string_view command_type);

[[nodiscard]] bool cancel_requested(RpcRunState& state);
[[nodiscard]] bool active_run(RpcRunState& state);
[[nodiscard]] bool input_closed(RpcRunState& state);
void set_active_run(RpcRunState& state, bool active, std::string request_id = {});
void set_active_request_id(RpcRunState& state, std::string request_id);

[[nodiscard]] ava::core::Result<QueuedRpcMessage> queue_rpc_message(std::deque<QueuedRpcMessage>& queue, RpcRunState& state, std::string command_type,
                                                                    std::string request_id, std::string message);
[[nodiscard]] std::vector<QueuedRpcMessage> take_queued_steering_messages(RpcRunState& state, std::string_view correlation_id);
[[nodiscard]] std::optional<QueuedRpcMessage> take_next_follow_up_message(RpcRunState& state);
[[nodiscard]] std::vector<QueuedRpcMessage> clear_queued_steering_messages(RpcRunState& state);
[[nodiscard]] ClearedRpcQueues deactivate_and_clear_queued_messages(RpcRunState& state);
[[nodiscard]] ClearedRpcQueues close_input_and_cancel(RpcRunState& state);

void record_async_error(RpcRunState& state, ava::core::Error error);
[[nodiscard]] std::optional<ava::core::Error> take_async_error(RpcRunState& state);

}  // namespace ava::app::rpc
