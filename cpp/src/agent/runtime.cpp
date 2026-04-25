#include "ava/agent/runtime.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

#include "runtime_run_loop.hpp"

namespace ava::agent {

AgentRuntime::AgentRuntime(const ava::llm::Provider& provider, const ava::tools::ToolRegistry& tools, AgentConfig config)
    : provider_(provider), tools_(tools), config_(std::move(config)) {}

std::string AgentRuntime::build_system_prompt() const {
  std::ostringstream prompt;
  prompt << config_.system_prompt_preamble << "\n\n";
  prompt << "Use tools when they materially improve correctness.\n";
  prompt << "When you are done, provide a direct assistant answer.\n\n";
  prompt << "Available tools:\n";

  const auto tools = tools_.list_tools();
  for(const auto& tool : tools) {
    prompt << "- " << tool.name << ": " << tool.description << "\n";
  }

  return prompt.str();
}

AgentRunResult AgentRuntime::run(
    ava::types::SessionRecord& session,
    const AgentRunInput& input,
    AgentEventSink on_event
) const {
  if(input.goal.empty() && session.messages.empty()) {
    throw std::invalid_argument("agent runtime requires a goal or an existing session history");
  }

  return run_agent_loop(provider_, tools_, config_, session, input, std::move(on_event), build_system_prompt());
}

const char* completion_reason_to_string(AgentCompletionReason reason) {
  switch(reason) {
    case AgentCompletionReason::Completed:
      return "completed";
    case AgentCompletionReason::Cancelled:
      return "cancelled";
    case AgentCompletionReason::MaxTurns:
      return "max_turns";
    case AgentCompletionReason::Stuck:
      return "stuck";
    case AgentCompletionReason::BudgetExceeded:
      return "budget_exceeded";
    case AgentCompletionReason::Error:
      return "error";
  }
  return "error";
}

}  // namespace ava::agent
