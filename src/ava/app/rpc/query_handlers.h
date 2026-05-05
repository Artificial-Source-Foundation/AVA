#pragma once

#include <mutex>

#include "ava/app/rpc/output.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app::rpc {

[[nodiscard]] ava::core::VoidResult handle_get_state_command(RpcOutput& output, RuntimeSession const& session,
                                                             std::mutex& session_mutex, RpcRunState& run_state,
                                                             RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_list_sessions_command(RpcOutput& output, RuntimeSession const& session,
                                                                 std::mutex& session_mutex, RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_list_models_command(RpcOutput& output, RuntimeSession const& session,
                                                               std::mutex& session_mutex, RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_get_messages_command(RpcOutput& output, RuntimeSession const& session,
                                                                std::mutex& session_mutex, RpcRunState& run_state,
                                                                RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_get_session_stats_command(RpcOutput& output, RuntimeSession const& session,
                                                                     std::mutex& session_mutex, RpcRunState& run_state,
                                                                     RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_validate_session_command(RpcOutput& output, RuntimeSession const& session,
                                                                    std::mutex& session_mutex, RpcRunState& run_state,
                                                                    RpcCommand const& command);

}  // namespace ava::app::rpc
