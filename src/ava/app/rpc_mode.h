#pragma once

#include "ava/app/headless_policy.h"
#include "ava/app/rpc/input.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/session_ts.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <iosfwd>

namespace ava::app {

struct RpcModeOptions
{
  runtime::OpenOptions open_options;
  HeadlessPermissionPolicyOptions permission_policy;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::VoidResult run_rpc_loop(runtime::session_ts& session, runtime::OpenOptions const& open_options, ava::provider::Provider const& provider,
                                                 ava::provider::Transport& transport, runtime::RunOptions runtime_options, std::istream& in, std::ostream& out,
                                                 rpc::RpcInputWake wake);
[[nodiscard]] ava::core::VoidResult run_rpc_loop(runtime::session_ts& session, runtime::OpenOptions const& open_options, ava::provider::Provider const& provider,
                                                 ava::provider::Transport& transport, ava::provider::Transport& auth_transport,
                                                 runtime::RunOptions runtime_options, std::istream& in, std::ostream& out, rpc::RpcInputWake wake);

// Finite streams only. Supply an explicit wake callback for any stream that can block.
[[deprecated("finite streams only; pass an explicit rpc::RpcInputWake for blocking input")]] [[nodiscard]] ava::core::VoidResult run_rpc_loop(
    runtime::session_ts& session, runtime::OpenOptions const& open_options, ava::provider::Provider const& provider, ava::provider::Transport& transport,
    runtime::RunOptions runtime_options, std::istream& in, std::ostream& out);
// Finite streams only. Supply an explicit wake callback for any stream that can block.
[[deprecated("finite streams only; pass an explicit rpc::RpcInputWake for blocking input")]] [[nodiscard]] ava::core::VoidResult run_rpc_loop(
    runtime::session_ts& session, runtime::OpenOptions const& open_options, ava::provider::Provider const& provider, ava::provider::Transport& transport,
    ava::provider::Transport& auth_transport, runtime::RunOptions runtime_options, std::istream& in, std::ostream& out);
[[nodiscard]] ava::core::VoidResult run_rpc_loop(runtime::session_ts& session, runtime::OpenOptions const& open_options, ava::provider::Provider const& provider,
                                                 ava::provider::Transport& transport, ava::provider::Transport& auth_transport,
                                                 runtime::RunOptions runtime_options, rpc::RpcLineReader& input, std::ostream& out);

[[nodiscard]] int run_rpc_mode(RpcModeOptions const& options, std::istream& in, std::ostream& out, std::ostream& err, rpc::RpcInputWake wake);

// Finite streams only. Supply an explicit wake callback for any stream that can block.
[[deprecated("finite streams only; pass an explicit rpc::RpcInputWake for blocking input")]] [[nodiscard]] int run_rpc_mode(RpcModeOptions const& options,
                                                                                                                            std::istream& in, std::ostream& out,
                                                                                                                            std::ostream& err);

}  // namespace ava::app
