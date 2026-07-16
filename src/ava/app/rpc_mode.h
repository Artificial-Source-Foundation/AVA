#pragma once

#include "ava/app/headless_policy.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/runtime.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <iosfwd>

namespace ava::app {

struct RpcModeOptions
{
  runtime::RuntimeOpenOptions open_options;
  HeadlessPermissionPolicyOptions permission_policy;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::VoidResult run_rpc_loop(runtime::RuntimeSession& session, runtime::RuntimeOpenOptions const& open_options, ava::provider::Provider const& provider,
                                                 ava::provider::Transport& transport, runtime::RuntimeRunOptions runtime_options, std::istream& in, std::ostream& out);
[[nodiscard]] ava::core::VoidResult run_rpc_loop(runtime::RuntimeSession& session, runtime::RuntimeOpenOptions const& open_options, ava::provider::Provider const& provider,
                                                 ava::provider::Transport& transport, ava::provider::Transport& auth_transport,
                                                 runtime::RuntimeRunOptions runtime_options, std::istream& in, std::ostream& out);

[[nodiscard]] int run_rpc_mode(RpcModeOptions const& options, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace ava::app
