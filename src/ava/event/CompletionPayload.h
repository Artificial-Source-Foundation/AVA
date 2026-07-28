#pragma once

#include "ava/debug/print_members_on.h"

#include <cstddef>
#include <string>

namespace ava::event {

// Extracted completion slice of an Event: terminal status, stop reason, a human-readable reason and the provider/tool call accounting for the finished run.
struct CompletionPayload
{
  std::string status;
  std::string stop_reason;
  std::string reason;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::event
