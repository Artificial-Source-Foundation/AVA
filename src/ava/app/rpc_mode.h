#pragma once

#include "ava/app/headless_policy.h"
#include "ava/app/rpc/input.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/runtime.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <iosfwd>

namespace ava::app {

struct RpcModeOptions
{
  RuntimeOpenOptions open_options;
  HeadlessPermissionPolicyOptions permission_policy;
};

[[nodiscard]] ava::core::VoidResult run_rpc_loop(RuntimeSession& session, RuntimeOpenOptions const& open_options, ava::provider::Provider const& provider,
                                                 ava::provider::Transport& transport, RuntimeRunOptions runtime_options, std::istream& in, std::ostream& out);
[[nodiscard]] ava::core::VoidResult run_rpc_loop(RuntimeSession& session, RuntimeOpenOptions const& open_options, ava::provider::Provider const& provider,
                                                 ava::provider::Transport& transport, ava::provider::Transport& auth_transport,
                                                 RuntimeRunOptions runtime_options, std::istream& in, std::ostream& out);
[[nodiscard]] ava::core::VoidResult run_rpc_loop(RuntimeSession& session, RuntimeOpenOptions const& open_options, ava::provider::Provider const& provider,
                                                 ava::provider::Transport& transport, ava::provider::Transport& auth_transport,
                                                 RuntimeRunOptions runtime_options, rpc::RpcLineReader& input, std::ostream& out);

[[nodiscard]] int run_rpc_mode(RpcModeOptions const& options, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace ava::app
