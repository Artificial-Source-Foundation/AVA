#pragma once

#include <string>

#include "ava/agent/runtime.hpp"

namespace ava::agent {

[[nodiscard]] AgentRunResult run_agent_loop(
    const ava::llm::Provider& provider,
    const ava::tools::ToolRegistry& tools,
    const AgentConfig& config,
    ava::types::SessionRecord& session,
    const AgentRunInput& input,
    AgentEventSink on_event,
    std::string system_prompt
);

}  // namespace ava::agent
