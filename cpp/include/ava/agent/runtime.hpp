#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "ava/agent/message_queue.hpp"
#include "ava/agent/stuck_detector.hpp"
#include "ava/llm/provider.hpp"
#include "ava/types/session.hpp"
#include "ava/tools/registry.hpp"

namespace ava::agent {

struct AgentConfig {
  std::size_t max_turns{16};
  std::string system_prompt_preamble{"You are AVA, a headless-first coding assistant. Complete the user's request safely and efficiently."};
  ava::llm::ThinkingConfig thinking{ava::llm::ThinkingConfig::disabled()};
  bool enable_stuck_detector{true};
  StuckDetectorConfig stuck{};
};

enum class AgentEventKind {
  RunStarted,
  TurnStarted,
  AssistantResponse,
  ToolCall,
  ToolResult,
  Completion,
  Error,
};

enum class AgentCompletionReason {
  Completed,
  MaxTurns,
  Stuck,
  Error,
};

struct AgentEvent {
  AgentEventKind kind{AgentEventKind::RunStarted};
  std::size_t turn{0};
  std::string message;
  std::optional<ava::types::ToolCall> tool_call;
  std::optional<ava::types::ToolResult> tool_result;
  std::optional<AgentCompletionReason> completion_reason;
};

using AgentEventSink = std::function<void(const AgentEvent&)>;

struct AgentRunInput {
  std::string goal;
  MessageQueue* queue{nullptr};
};

struct AgentRunResult {
  AgentCompletionReason reason{AgentCompletionReason::Completed};
  std::size_t turns_used{0};
  std::string final_response;
  std::optional<std::string> error;
  std::optional<ava::types::TokenUsage> usage;
};

class AgentRuntime {
public:
  AgentRuntime(const ava::llm::Provider& provider, const ava::tools::ToolRegistry& tools, AgentConfig config = {});

  [[nodiscard]] std::string build_system_prompt() const;
  [[nodiscard]] AgentRunResult run(
      ava::types::SessionRecord& session,
      const AgentRunInput& input,
      AgentEventSink on_event = nullptr
  ) const;

private:
  [[nodiscard]] static std::string completion_reason_to_string(AgentCompletionReason reason);

  const ava::llm::Provider& provider_;
  const ava::tools::ToolRegistry& tools_;
  AgentConfig config_;
};

}  // namespace ava::agent
