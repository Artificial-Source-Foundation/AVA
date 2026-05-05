#pragma once

#include <mutex>

#include "ava/app/rpc/output.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app::rpc {

[[nodiscard]] ava::core::VoidResult handle_model_command(RpcOutput& output, RuntimeSession& session,
                                                         std::mutex& session_mutex, RpcRunState& run_state,
                                                         RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_reasoning_command(RpcOutput& output, RuntimeSession& session,
                                                             std::mutex& session_mutex, RpcRunState& run_state,
                                                             RpcCommand const& command);

}  // namespace ava::app::rpc
