#pragma once

#include "ava/debug/print_members_on.h"

#include <cstddef>
#include <string>

namespace ava::app {

// Extracted retry slice of a RuntimeEvent: human-readable text, status, the error classification fields, the triggering reason and the attempt/backoff accounting.
struct RuntimeRetryPayload
{
  std::string text;
  std::string status;
  std::string error_category;
  std::string error_code;
  std::string error_message;
  std::string error_details;
  std::string trigger;
  std::string reason;
  std::size_t attempt = 0;
  std::size_t max_attempts = 0;
  std::size_t delay_ms = 0;
  std::size_t remaining_ms = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app
