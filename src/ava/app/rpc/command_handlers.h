#pragma once

#include <mutex>
#include <string_view>

#include "ava/app/rpc/output.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::app::rpc {

[[nodiscard]] bool is_command_bridge_command(std::string_view type);

[[nodiscard]] ava::core::VoidResult handle_command_bridge_command(
    RpcOutput& output, RuntimeSession& session, std::mutex& session_mutex, RpcRunState& run_state,
    RpcCommand const& command, RuntimeRunOptions const& runtime_options, ava::provider::Provider const& provider,
    ava::provider::Transport& transport, ava::provider::Transport& auth_transport,
    std::string_view injected_provider_id);

}  // namespace ava::app::rpc
