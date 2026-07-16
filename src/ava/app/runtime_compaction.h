#pragma once

#include "ava/app/runtime.h"
#include "ava/core/result.h"

#include <string_view>
#include <vector>

namespace ava::app::runtime {

[[nodiscard]] ava::core::Result<bool> compact_runtime_context(runtime::RuntimeSession& session, ava::session::SessionStore& store, std::string_view trigger,
                                                              ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                              runtime::RuntimeRunOptions const& options, std::vector<std::string> const& replayed_user_messages);

}  // namespace ava::app::runtime
