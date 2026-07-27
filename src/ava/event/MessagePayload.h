#pragma once

#include "ava/debug/print_members_on.h"

#include <string>

namespace ava::event {

// Extracted message slice of an Event: streamed or final text, status, optional error message, and stop reason as emitted by the runtime prompt message path.
struct MessagePayload
{
  std::string text;
  std::string status;
  std::string error_message;
  std::string stop_reason;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::event
