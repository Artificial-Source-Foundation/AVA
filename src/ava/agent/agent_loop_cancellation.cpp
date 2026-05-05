#include "ava/agent/agent_loop_cancellation.h"

#include <string>
#include <utility>

#include "ava/agent/session_recorder.h"
#include "ava/core/error.h"

namespace ava::agent {
namespace {

ava::core::VoidResult check_canceled_unlocked(AgentLoopCancelRequested const& cancel_requested,
                                              ava::session::SessionStore& store, std::string_view boundary)
{
  if (!agent_loop_canceled(cancel_requested)) return {};
  static_cast<void>(append_cancel(store, boundary));
  return std::unexpected(agent_loop_canceled_error(boundary));
}

}  // namespace

bool agent_loop_canceled(AgentLoopCancelRequested const& cancel_requested)
{
  return cancel_requested && cancel_requested();
}

ava::core::Error agent_loop_canceled_error(std::string_view boundary)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
  error.with_context("boundary", std::string(boundary));
  return error;
}

ava::core::VoidResult check_agent_loop_canceled(AgentLoopCancelRequested const& cancel_requested,
                                                ava::session::SessionStore& store, std::mutex* session_mutex,
                                                std::string_view boundary)
{
  if (session_mutex) {
    std::lock_guard lock(*session_mutex);
    return check_canceled_unlocked(cancel_requested, store, boundary);
  }
  return check_canceled_unlocked(cancel_requested, store, boundary);
}

}  // namespace ava::agent
