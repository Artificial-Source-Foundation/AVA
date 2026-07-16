#pragma once

#include "output.h"
#include "resolvers.h"
#include "run_state.h"
#include "ava/app/runtime.h"
#include "ava/session/attachments.h"
#include "ava/provider/provider.h"

#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ava::app::rpc {

struct RpcPromptWorkerOptions
{
  runtime::Session& session;
  std::mutex& session_mutex;
  output_ts& output;                                    // Reference to threadsafe output stream.
  RpcRunState& run_state;
  PendingResolverState& pending_state;
  ava::provider::Provider const& injected_provider;
  std::string injected_provider_id;
  ava::provider::Transport& transport;
  ava::provider::Transport& auth_transport;
  runtime::RunOptions runtime_options;
  ava::config::XdgPaths paths;
  std::string request_id;
  std::string message;
  std::vector<ava::session::ImageAttachmentRef> image_attachments = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::jthread make_rpc_prompt_worker(RpcPromptWorkerOptions options);

}  // namespace ava::app::rpc
