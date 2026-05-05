#pragma once

#include <string>
#include <string_view>

#include "ava/app/events.h"

namespace ava::app {

[[nodiscard]] std::string runtime_event_payload_json(RuntimeEvent const& event);
void append_runtime_event_payload_aliases(std::string& out, std::string_view payload_json);

}  // namespace ava::app
