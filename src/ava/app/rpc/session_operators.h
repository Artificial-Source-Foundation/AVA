#pragma once
#include "protocol.h"
#include "ava/http/transport.h"
#include "ava/app/runtime.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <memory>
#include <string>
#include <string_view>

namespace ava::app::rpc {

struct ProviderHandle
{
  ava::provider::Provider const* provider = nullptr;
  std::unique_ptr<ava::provider::Provider> owned;

  [[nodiscard]] ava::provider::Provider const& get() const { return owned ? *owned : *provider; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<runtime::RunOptions> ensure_prompt_runtime_options(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                                   runtime::RunOptions options, ava::http::Transport& auth_transport,
                                                                                   std::string_view purpose);

[[nodiscard]] ava::core::Result<runtime::Session> create_new_session(runtime::Session const& current, runtime::OpenOptions const& base_options);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> resolve_requested_model(runtime::Session const& session, RpcCommand const& command);
[[nodiscard]] ava::core::Result<ProviderHandle> provider_for_session_model(runtime::Session const& session, std::string_view injected_provider_id,
                                                                           ava::provider::Provider const& injected_provider);
[[nodiscard]] bool is_plugin_rpc_command(std::string_view type);
[[nodiscard]] bool is_mcp_rpc_command(std::string_view type);
[[nodiscard]] ava::core::Result<std::string> plugin_rpc_slash_command(RpcCommand const& command);
[[nodiscard]] ava::core::Result<std::string> mcp_rpc_slash_command(RpcCommand const& command);

}  // namespace ava::app::rpc
