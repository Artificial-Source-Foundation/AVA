#pragma once

#include <functional>
#include <string_view>

#include "ava/core/error.h"
#include "ava/core/result.h"

namespace ava::agent {

struct ContextOverflowRetryState {
  bool retry_used = false;
  bool skip_next_auto_compaction = false;
};

struct ContextOverflowRetryCallbacks {
  std::function<ava::core::VoidResult(std::string_view boundary)> check_canceled = nullptr;
  std::function<ava::core::Result<bool>(std::string_view trigger)> compact_context = nullptr;
  std::function<ava::core::VoidResult()> replay_active_turn_user_messages = nullptr;
};

[[nodiscard]] ava::core::Result<bool> prepare_context_overflow_retry_attempt(
    ava::core::Error const& error, bool compaction_available, ContextOverflowRetryState& state,
    ContextOverflowRetryCallbacks const& callbacks);

}  // namespace ava::agent
