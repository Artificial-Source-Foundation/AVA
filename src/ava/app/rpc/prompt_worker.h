#pragma once

#include "ava/app/rpc/output.h"
#include "ava/app/rpc/resolvers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/provider/provider.h"

#include <mutex>
#include <string>
#include <thread>

namespace ava::app::rpc {

struct RpcPromptWorkerOptions
{
  RuntimeSession& session;
  std::mutex& session_mutex;
  RpcOutput& output;
  RpcRunState& run_state;
  PendingResolverState& pending_state;
  ava::provider::Provider const& injected_provider;
  std::string injected_provider_id;
  ava::provider::Transport& transport;
  ava::provider::Transport& auth_transport;
  RuntimeRunOptions runtime_options;
  ava::config::XdgPaths paths;
  std::string request_id;
  std::string message;
};

[[nodiscard]] std::jthread make_rpc_prompt_worker(RpcPromptWorkerOptions options);

}  // namespace ava::app::rpc
