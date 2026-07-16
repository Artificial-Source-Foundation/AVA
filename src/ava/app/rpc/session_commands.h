#pragma once

#include "protocol.h"
#include "run_state.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"

#include <mutex>

namespace ava::app::rpc {

struct RpcSessionCommandContext
{
  RpcCommand const& command;
  runtime::RuntimeSession& session;
  runtime::RuntimeOpenOptions const& open_options;
  output_ts& output;                            // Reference to threadsafe output stream.
  RpcRunState& run_state;
  std::mutex& session_mutex;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<bool> handle_session_rpc_command(RpcSessionCommandContext context);

}  // namespace ava::app::rpc
