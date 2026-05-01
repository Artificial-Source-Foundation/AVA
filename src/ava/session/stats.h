#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ava/session/session_store.h"

namespace ava::session {

struct SessionEntryCounts {
  std::size_t session_start = 0;
  std::size_t user_message = 0;
  std::size_t assistant_message = 0;
  std::size_t tool_call = 0;
  std::size_t tool_result = 0;
  std::size_t permission_decision = 0;
  std::size_t mode_change = 0;
  std::size_t compaction = 0;
  std::size_t error = 0;
  std::size_t cancel = 0;
};

struct SessionStats {
  std::size_t entry_count = 0;
  std::string first_timestamp;
  std::string last_timestamp;
  SessionEntryCounts counts;
  std::optional<long long> input_tokens;
  std::optional<long long> output_tokens;
  std::optional<long long> total_tokens;
  std::optional<long double> total_cost_usd;
};

[[nodiscard]] SessionStats compute_session_stats(const std::vector<SessionEntry>& entries);

}  // namespace ava::session
