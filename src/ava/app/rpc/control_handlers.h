#pragma once

#include <mutex>
#include <string_view>
#include <vector>

#include "ava/app/rpc/output.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/resolvers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app::rpc {

[[nodiscard]] ava::core::VoidResult write_follow_up_errors(RpcOutput& output,
                                                           std::vector<QueuedRpcMessage> const& follow_ups,
                                                           std::string_view reason);

[[nodiscard]] ava::core::VoidResult handle_permission_grants_command(RpcOutput& output,
                                                                     PendingResolverState& pending_state,
                                                                     RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_permission_grant_revoke_command(RpcOutput& output,
                                                                           RuntimeSession const& session,
                                                                           std::mutex& session_mutex,
                                                                           PendingResolverState& pending_state,
                                                                           RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_permission_grants_clear_command(RpcOutput& output,
                                                                           RuntimeSession const& session,
                                                                           std::mutex& session_mutex,
                                                                           PendingResolverState& pending_state,
                                                                           RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_steer_command(RpcOutput& output, RuntimeSession const& session,
                                                         std::mutex& session_mutex, RpcRunState& run_state,
                                                         RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_follow_up_command(RpcOutput& output, RuntimeSession const& session,
                                                             std::mutex& session_mutex, RpcRunState& run_state,
                                                             RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_permission_reply_command(RpcOutput& output, RuntimeSession const& session,
                                                                    std::mutex& session_mutex,
                                                                    PendingResolverState& pending_state,
                                                                    RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_question_reply_command(RpcOutput& output, RuntimeSession const& session,
                                                                  std::mutex& session_mutex,
                                                                  PendingResolverState& pending_state,
                                                                  RpcCommand const& command);

[[nodiscard]] ava::core::VoidResult handle_cancel_command(RpcOutput& output, RuntimeSession const& session,
                                                          std::mutex& session_mutex, RpcRunState& run_state,
                                                          PendingResolverState& pending_state,
                                                          RpcCommand const& command);

}  // namespace ava::app::rpc
