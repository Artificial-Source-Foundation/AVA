#pragma once

#include "ava/observability/run_observer.h"
#include "ava/agent/agent_loop.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "debug.h"

namespace ava::agent::detail {

enum class ProviderEventPublishMode
{
  All,
  ReasoningOnly,
};

// Accumulates one synchronous provider attempt. The borrowed options, trace
// context, and finalized-ID set must outlive this accumulator; the provider
// iteration is captured by value for the attempt.
class ProviderEventAccumulator final
{
 public:
  ProviderEventAccumulator(AgentLoopOptions const& options, ava::observability::TraceContext const& trace_context,
                           std::unordered_set<std::string> const& finalized_provider_tool_call_ids, std::size_t provider_iteration) noexcept;

  [[nodiscard]] ava::core::VoidResult append(std::vector<ava::provider::StreamEvent> events, ProviderEventPublishMode publish_mode);
  [[nodiscard]] std::vector<ava::provider::StreamEvent> const& events() const noexcept;
  [[nodiscard]] std::unordered_set<std::string> const& current_provider_tool_call_ids() const noexcept;

  // Retains transient tool arguments and provider-native reasoning metadata;
  // never generate debug output for this accumulator.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  AgentLoopOptions const& options_;
  ava::observability::TraceContext const& trace_context_;
  std::unordered_set<std::string> const& finalized_provider_tool_call_ids_;
  std::size_t provider_iteration_;
  std::vector<ava::provider::StreamEvent> events_;
  std::size_t assistant_and_reasoning_bytes_ = 0;
  std::map<std::string, std::size_t> tool_argument_bytes_;
  std::unordered_map<std::string, std::string> streamed_tool_names_;
  std::unordered_set<std::string> current_provider_tool_call_ids_;
};

}  // namespace ava::agent::detail
