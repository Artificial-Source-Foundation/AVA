#pragma once

#include <nlohmann/json.hpp>

#include "ava/agent/runtime.hpp"

namespace ava::app {

[[nodiscard]] nlohmann::json headless_event_to_ndjson(const ava::agent::AgentEvent& event);
void print_headless_event_text(const ava::agent::AgentEvent& event);

}  // namespace ava::app
