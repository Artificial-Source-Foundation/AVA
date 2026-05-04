#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "ava/app/rpc/protocol.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::app::rpc {

struct ProviderHandle {
  const ava::provider::Provider* provider = nullptr;
  std::unique_ptr<ava::provider::Provider> owned;

  [[nodiscard]] const ava::provider::Provider& get() const { return owned ? *owned : *provider; }
};

[[nodiscard]] ava::core::Result<RuntimeRunOptions> ensure_prompt_runtime_options(
    const ava::config::XdgPaths& paths, std::string_view provider_id, RuntimeRunOptions options,
    ava::provider::Transport& auth_transport, std::string_view purpose);

[[nodiscard]] ava::core::Result<RuntimeSession> create_new_session(const RuntimeSession& current,
                                                                   const RuntimeOpenOptions& base_options);
[[nodiscard]] ava::core::Result<RuntimeSession> open_requested_session(const RuntimeSession& current,
                                                                       const RuntimeOpenOptions& base_options,
                                                                       std::string_view requested_session_id);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> resolve_requested_model(const RuntimeSession& session,
                                                                                const RpcCommand& command);
[[nodiscard]] ava::core::Result<ava::config::ModelInfo> next_runtime_model(const RuntimeSession& session);
[[nodiscard]] ava::core::Result<ProviderHandle> provider_for_session_model(
    const RuntimeSession& session, std::string_view injected_provider_id,
    const ava::provider::Provider& injected_provider);

[[nodiscard]] bool is_plugin_rpc_command(std::string_view type);
[[nodiscard]] bool is_mcp_rpc_command(std::string_view type);
[[nodiscard]] ava::core::Result<std::string> plugin_rpc_slash_command(const RpcCommand& command);
[[nodiscard]] ava::core::Result<std::string> mcp_rpc_slash_command(const RpcCommand& command);

}  // namespace ava::app::rpc
