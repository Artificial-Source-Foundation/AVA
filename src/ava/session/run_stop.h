#pragma once

#include "ava/session/session_store.h"

#include <cstdint>
#include <string>

namespace ava::session {

inline constexpr std::size_t kMaxRunStopReasonBytes = 1024;

// A completed runtime boundary, never a suspended executor. Schema v1 has
// exactly one classification (max_turn_requests) and status (paused).
struct RunStop
{
  std::string reason;
  std::uint64_t round_count;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] ava::core::Result<RunStop> parse_run_stop(SessionEntry const& entry);
[[nodiscard]] ava::core::Result<SessionEntry> make_run_stop_entry(RunStop const& stop);
[[nodiscard]] std::string run_stop_display(RunStop const& stop);

}  // namespace ava::session
