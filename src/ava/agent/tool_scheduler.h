#pragma once

#include "ava/agent/tool_metadata.h"
#include "ava/agent/tool_types.h"
#include "ava/core/result.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace ava::agent {

enum class ToolScheduleEligibility
{
  ReadOnlyCandidate,
  Barrier,
  Deferred,
};

struct ToolScheduleClassification
{
  ToolScheduleEligibility eligibility = ToolScheduleEligibility::Deferred;
  std::string reason = "unknown_tool";
};

struct ToolScheduleSlot
{
  std::size_t provider_index = 0;
  ProviderToolCall call;
  ToolScheduleClassification classification;
};

struct ToolScheduleOutcome
{
  ToolScheduleSlot slot;
  ToolDispatchResult result;
};

using ToolScheduleExecutor = std::function<ava::core::Result<ToolDispatchResult>(ToolScheduleSlot const& slot)>;

[[nodiscard]] ToolScheduleClassification classify_tool_for_scheduling(ProviderToolCall const& call, std::span<ToolMetadata const> tool_metadata);

[[nodiscard]] std::vector<ToolScheduleSlot> build_sequential_tool_schedule(std::span<ProviderToolCall const> calls,
                                                                           std::span<ToolMetadata const> tool_metadata);

[[nodiscard]] ava::core::Result<std::vector<ToolScheduleOutcome>> run_sequential_tool_schedule(std::span<ToolScheduleSlot const> schedule,
                                                                                               ToolScheduleExecutor const& executor);

}  // namespace ava::agent
