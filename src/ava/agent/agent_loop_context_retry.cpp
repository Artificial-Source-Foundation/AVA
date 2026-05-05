#include "ava/agent/agent_loop_context_retry.h"

#include <string>
#include <utility>

#include "ava/provider/provider.h"

namespace ava::agent {

ava::core::Result<bool> prepare_context_overflow_retry_attempt(ava::core::Error const& error, bool compaction_available,
                                                               ContextOverflowRetryState& state,
                                                               ContextOverflowRetryCallbacks const& callbacks)
{
  if (!ava::provider::is_context_overflow_error(error) || state.retry_used || !compaction_available) {
    return false;
  }
  state.retry_used = true;

  if (callbacks.check_canceled) {
    if (auto not_canceled = callbacks.check_canceled("before_context_overflow_compaction"); !not_canceled) {
      return std::unexpected(std::move(not_canceled.error()));
    }
  }

  auto compacted =
      callbacks.compact_context ? callbacks.compact_context("context_overflow") : ava::core::Result<bool>{false};
  if (!compacted) {
    auto compact_error = ava::core::Error(ava::core::ErrorCategory::Provider, "context overflow compaction failed");
    compact_error.with_context("provider_error", error.format());
    compact_error.with_context("compaction_error", compacted.error().format());
    return std::unexpected(std::move(compact_error));
  }
  if (*compacted) {
    if (callbacks.replay_active_turn_user_messages) {
      if (auto replayed = callbacks.replay_active_turn_user_messages(); !replayed) {
        return std::unexpected(std::move(replayed.error()));
      }
    }
    state.skip_next_auto_compaction = true;
  }

  if (callbacks.check_canceled) {
    if (auto not_canceled = callbacks.check_canceled("after_context_overflow_compaction"); !not_canceled) {
      return std::unexpected(std::move(not_canceled.error()));
    }
  }
  return true;
}

}  // namespace ava::agent
