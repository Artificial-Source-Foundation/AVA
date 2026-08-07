#pragma once
#include "ava/http/transport.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"

#include <string_view>
#include <vector>

namespace ava::app::runtime {

// Compact unlocked_session using read_authority, trigger, provider, transport, options, and replayed_user_messages.
//
// Returns true when a compaction entry was appended and false when automatic compaction was unnecessary.
[[nodiscard]] ava::core::Result<bool> compact_runtime_context(session_ts& unlocked_session, ava::session::SessionReadAuthority read_authority,
                                                              std::string_view trigger,
                                                              ava::provider::Provider const& provider, ava::http::Transport& transport,
                                                              RunOptions const& options, std::vector<std::string> const& replayed_user_messages);

}  // namespace ava::app::runtime
