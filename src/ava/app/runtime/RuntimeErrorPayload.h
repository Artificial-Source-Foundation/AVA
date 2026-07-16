#pragma once

#include "ava/debug/print_members_on.h"

#include <string>

namespace ava::app::runtime {

// Extracted error slice of a RuntimeEvent: human-readable text, status, the error classification fields, the content type and the triggering reason for the failure.
struct RuntimeErrorPayload
{
  std::string text;
  std::string status;
  std::string error_category;
  std::string error_code;
  std::string error_message;
  std::string error_details;
  std::string content_type;
  std::string trigger;
  std::string reason;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
