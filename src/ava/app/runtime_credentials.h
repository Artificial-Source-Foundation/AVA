#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/runtime.h"

#include <functional>
#include <memory>
#include <string_view>

namespace ava::app {

struct RuntimeProviderRunBundle
{
  std::unique_ptr<ava::provider::Provider> provider;
  std::unique_ptr<ava::provider::Transport> transport;
  std::unique_ptr<ava::provider::Transport> auth_transport;
  runtime::RunOptions options;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

using RuntimeProviderRunBundleFactory = std::function<ava::core::Result<RuntimeProviderRunBundle>(runtime::Session const&, runtime::RunOptions, std::string_view)>;

// Resolves provider credentials without assuming a frontend protocol. Errors
// include the provider, auth file, and concrete setup guidance.
[[nodiscard]] ava::core::Result<runtime::RunOptions> prepare_runtime_credentials(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                               runtime::RunOptions options, ava::provider::Transport& auth_transport,
                                                                               std::string_view purpose);

// Creates an isolated provider/transport/auth bundle for one active run. The
// returned transports are never shared with another session or run.
[[nodiscard]] ava::core::Result<RuntimeProviderRunBundle> create_runtime_provider_run_bundle(runtime::Session const& session, runtime::RunOptions options,
                                                                                             std::string_view purpose);

}  // namespace ava::app
