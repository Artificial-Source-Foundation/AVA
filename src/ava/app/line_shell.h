#pragma once

#include "ava/app/runtime/Session.h"
#include "ava/agent/agent_loop.h"
#include "ava/tui/composer.h"

#include <vector>

namespace ava::app {

[[nodiscard]] std::vector<ava::tui::ToolTimelineItem> tool_timeline_for_tui(std::vector<ava::agent::ToolTimelineEntry> const& entries);
[[nodiscard]] int run_interactive(runtime::Session& session);

}  // namespace ava::app
