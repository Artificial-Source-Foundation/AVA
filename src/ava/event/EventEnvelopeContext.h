#pragma once

#include "ava/debug/print_members_on.h"

#include <optional>
#include <string>

namespace ava::event {

struct EventEnvelopeContext
{
  std::optional<std::string> event_id;
  std::optional<std::string> run_id;
  std::optional<std::string> turn_id;
  std::optional<std::string> message_id;
  std::optional<std::string> request_id;
  std::optional<std::string> correlation_id;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::event
