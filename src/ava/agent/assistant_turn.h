#pragma once

#include "ava/agent/tool_types.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ava::agent {

struct ParsedReasoningBlock
{
  std::string text;
  std::string format;
  std::string signature;
  std::string redacted_data;
  // Opaque provider-only JSON retained only for exact native replay.
  std::string native_item_json;
  bool redacted = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ParsedAssistantTurn
{
  std::string text;
  std::vector<ParsedReasoningBlock> reasoning_blocks;
  std::vector<ProviderToolCall> tool_calls;
  std::optional<ava::provider::TokenUsage> usage;
  std::optional<ava::provider::ProviderFinishReason> finish_reason;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ProviderOutputLimits
{
  std::size_t max_events = 0;
  std::size_t max_assistant_text_bytes = 0;
  std::size_t max_tool_argument_bytes = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<ParsedAssistantTurn> parse_assistant_turn(std::vector<ava::provider::StreamEvent> const& events, ProviderOutputLimits limits);

}  // namespace ava::agent
