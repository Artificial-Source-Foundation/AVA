#pragma once

#include "ava/agent/agent_loop.h"
#include "ava/agent/tool_types.h"
#include "ava/core/result.h"

namespace ava::agent {

void publish_tool_event(AgentLoopOptions const& options, ToolTimelineEntry const& event);
[[nodiscard]] ava::core::VoidResult publish_tool_progress(AgentLoopOptions const& options, ToolProgressEntry const& event);
void populate_tool_timeline_metadata(ToolTimelineEntry& entry, ToolDispatchResult const& result);

}  // namespace ava::agent
