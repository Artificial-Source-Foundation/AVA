#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "ava/agent/budget_tracker.hpp"
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
  double max_budget_usd{0.0};
  bool auto_compact{false};
  std::size_t max_context_tokens{0};
  double compaction_threshold{0.8};
  std::size_t preserve_recent_messages{6};
};

enum class AgentEventKind {
  RunStarted,
  TurnStarted,
  AssistantResponseDelta,
  AssistantResponse,
  ToolCall,
  ToolResult,
  SubagentComplete,
  TokenUsage,
  BudgetWarning,
  ContextCompacted,
  Checkpoint,
  Completion,
  Error,
};

enum class AgentCompletionReason {
  Completed,
  Cancelled,
  MaxTurns,
  Stuck,
  BudgetExceeded,
  Error,
};

[[nodiscard]] const char* completion_reason_to_string(AgentCompletionReason reason);

struct AgentEvent {
  AgentEventKind kind{AgentEventKind::RunStarted};
  std::optional<std::string> run_id;
  std::size_t turn{0};
  std::string message;
  std::optional<ava::types::ToolCall> tool_call;
  std::optional<ava::types::ToolResult> tool_result;
  std::optional<std::string> subagent_call_id;
  std::optional<std::string> subagent_session_id;
  std::optional<std::string> subagent_description;
  std::optional<std::size_t> subagent_message_count;
  std::optional<ava::types::TokenUsage> token_usage;
  std::optional<double> token_cost_usd;
  std::optional<BudgetWarning> budget_warning;
  std::optional<std::size_t> compacted_message_count;
  std::optional<std::size_t> compacted_token_estimate;
  std::optional<AgentCompletionReason> completion_reason;
  // True when an AssistantResponse carries full text already emitted as
  // AssistantResponseDelta events; visual consumers should not render it again.
  bool replays_stream_deltas{false};
};

using AgentEventSink = std::function<void(const AgentEvent&)>;

struct AgentRunInput {
  std::string goal;
  MessageQueue* queue{nullptr};
  std::optional<std::string> run_id;
  std::function<bool()> is_cancelled;
  bool stream{true};
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
  const ava::llm::Provider& provider_;
  const ava::tools::ToolRegistry& tools_;
  AgentConfig config_;
};

}  // namespace ava::agent
