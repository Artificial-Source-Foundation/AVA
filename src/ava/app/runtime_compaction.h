#pragma once

#include <string_view>
#include <vector>

#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app::runtime {

[[nodiscard]] ava::core::Result<bool> compact_runtime_context(
    RuntimeSession& session, ava::session::SessionStore& store, std::string_view trigger,
    const ava::provider::Provider& provider, ava::provider::Transport& transport, const RuntimeRunOptions& options,
    const std::vector<std::string>& replayed_user_messages);

}  // namespace ava::app::runtime
