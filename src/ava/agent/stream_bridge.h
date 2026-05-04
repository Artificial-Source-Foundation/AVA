#pragma once

#include "ava/agent/agent_loop.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::agent {

[[nodiscard]] ava::core::VoidResult publish_stream_event(AgentLoopOptions const& options,
                                                         ava::provider::StreamEvent const& event);

}  // namespace ava::agent
