#pragma once

#include "ava/debug/print_members_on.h"

#include <string>

namespace ava::app::runtime {

// Extracted cancellation slice of an Event: human-readable text, status, the error classification fields and the triggering reason for the cancellation.
struct CancellationPayload
{
  std::string text;
  std::string status;
  std::string error_category;
  std::string error_code;
  std::string error_message;
  std::string error_details;
  std::string trigger;
  std::string reason;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
