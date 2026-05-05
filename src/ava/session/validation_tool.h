#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include "ava/session/session_store.h"
#include "ava/session/validation.h"

namespace ava::session {

struct SessionReplayToolCallState {
  std::string entry_id;
  std::string tool_name;
  bool result_seen = false;
};

using SessionReplayToolCalls = std::unordered_map<std::string, SessionReplayToolCallState>;

void validate_tool_call_entry(SessionReplayValidation& validation, SessionReplayToolCalls& tool_calls,
                              SessionReplayValidationOptions const& options, std::size_t index,
                              SessionEntry const& entry);
void validate_tool_result_entry(SessionReplayValidation& validation, SessionReplayToolCalls& tool_calls,
                                SessionReplayValidationOptions const& options, std::size_t index,
                                SessionEntry const& entry);
void add_compaction_tool_boundary_issues(SessionReplayValidation& validation, SessionReplayToolCalls const& tool_calls,
                                         std::size_t index, SessionEntry const& entry);
void add_unresolved_tool_calls(SessionReplayValidation& validation, SessionReplayToolCalls const& tool_calls,
                               std::size_t entry_index);

}  // namespace ava::session
