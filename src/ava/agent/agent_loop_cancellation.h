#pragma once

#include <functional>
#include <mutex>
#include <string_view>

#include "ava/core/error.h"
#include "ava/core/result.h"
#include "ava/session/session_store.h"

namespace ava::agent {

using AgentLoopCancelRequested = std::function<bool()>;

[[nodiscard]] bool agent_loop_canceled(AgentLoopCancelRequested const& cancel_requested);
[[nodiscard]] ava::core::Error agent_loop_canceled_error(std::string_view boundary);
[[nodiscard]] ava::core::VoidResult check_agent_loop_canceled(AgentLoopCancelRequested const& cancel_requested,
                                                              ava::session::SessionStore& store,
                                                              std::mutex* session_mutex, std::string_view boundary);

}  // namespace ava::agent
