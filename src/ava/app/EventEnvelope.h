#pragma once

#include "ava/debug/print_members_on.h"

#include <optional>
#include <string>

namespace ava::app {

// A serialized runtime event wrapped for an EventBus subscriber: schema version, envelope identifiers, optional correlation IDs, a name and the JSON payload plus its declared payload type.
struct EventEnvelope
{
  int schema_version = 1;
  std::string event_id;
  std::string timestamp;
  std::string session_id;
  std::optional<std::string> run_id;
  std::optional<std::string> turn_id;
  std::optional<std::string> message_id;
  std::optional<std::string> request_id;
  std::optional<std::string> correlation_id;
  std::string name;
  std::string payload_json = "{}";
  std::string payload_type = "";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app
