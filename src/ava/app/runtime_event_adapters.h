#pragma once

#include "ava/event/RuntimeEvent.h"
#include "ava/agent/agent_loop.h"
#include "ava/provider/provider.h"

namespace ava::app {

// Private app adapters that map agent/provider source records onto immutable
// RuntimeEvent alternatives without the migration-only runtime::Event bag.

[[nodiscard]] ava::event::RuntimeEvent runtime_event_from_tool_timeline_entry(ava::event::RuntimeEventMetadata metadata,
                                                                              ava::agent::ToolTimelineEntry const& entry);

[[nodiscard]] ava::event::RuntimeEvent runtime_event_from_tool_progress_entry(ava::event::RuntimeEventMetadata metadata,
                                                                              ava::agent::ToolProgressEntry const& entry);

[[nodiscard]] ava::event::RuntimeEvent runtime_event_from_provider_stream_event(ava::event::RuntimeEventMetadata metadata,
                                                                                ava::provider::StreamEvent const& stream_event);

}  // namespace ava::app
