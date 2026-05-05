#pragma once

#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

#include "ava/app/rpc/output.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/resolvers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::app::rpc {

void reap_prompt_worker(std::optional<std::jthread>& prompt_worker, RpcRunState& run_state);

[[nodiscard]] ava::core::VoidResult handle_prompt_command(
    RpcOutput& output, RuntimeSession& session, std::mutex& session_mutex, RpcRunState& run_state,
    PendingResolverState& pending_state, std::optional<std::jthread>& prompt_worker, RpcCommand const& command,
    RuntimeRunOptions const& runtime_options, ava::provider::Provider const& provider,
    ava::provider::Transport& transport, ava::provider::Transport& auth_transport,
    std::string_view injected_provider_id);

[[nodiscard]] ava::core::VoidResult close_prompt_worker(RpcOutput& output, RuntimeSession& session,
                                                        std::mutex& session_mutex, RpcRunState& run_state,
                                                        PendingResolverState& pending_state,
                                                        std::optional<std::jthread>& prompt_worker,
                                                        std::string_view reason);

}  // namespace ava::app::rpc
