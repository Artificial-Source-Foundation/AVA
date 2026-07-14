#pragma once

#include "ava/debug/print_members_on.h"

#include <array>
#include <string_view>

namespace ava::provider {

enum class ProviderProtocol
{
  OpenAIChat,
  OpenAIResponses,
  Anthropic,
  Gemini,
};

enum class ProviderFinishReason
{
  Completed,
  MaxTokens,
  ToolCalls,
  Refusal,
  Cancelled,
  Error,
};

struct ProviderFinishReasonMapping
{
  ProviderProtocol protocol;
  std::string_view raw_reason;
  ProviderFinishReason reason;
  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Closed catalog of provider values accepted as terminal outcomes. Unknown
// values normalize to Error and can never become a successful end turn.
inline constexpr std::array kProviderFinishReasonCatalog{
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIChat, "stop", ProviderFinishReason::Completed},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIChat, "length", ProviderFinishReason::MaxTokens},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIChat, "tool_calls", ProviderFinishReason::ToolCalls},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIChat, "function_call", ProviderFinishReason::ToolCalls},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIChat, "content_filter", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIChat, "refusal", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIChat, "cancelled", ProviderFinishReason::Cancelled},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIChat, "canceled", ProviderFinishReason::Cancelled},

    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "completed", ProviderFinishReason::Completed},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "max_output_tokens", ProviderFinishReason::MaxTokens},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "max_tokens", ProviderFinishReason::MaxTokens},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "content_filter", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "refusal", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "cancelled", ProviderFinishReason::Cancelled},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "canceled", ProviderFinishReason::Cancelled},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "incomplete", ProviderFinishReason::Error},
    ProviderFinishReasonMapping{ProviderProtocol::OpenAIResponses, "failed", ProviderFinishReason::Error},

    ProviderFinishReasonMapping{ProviderProtocol::Anthropic, "end_turn", ProviderFinishReason::Completed},
    ProviderFinishReasonMapping{ProviderProtocol::Anthropic, "stop_sequence", ProviderFinishReason::Completed},
    ProviderFinishReasonMapping{ProviderProtocol::Anthropic, "max_tokens", ProviderFinishReason::MaxTokens},
    ProviderFinishReasonMapping{ProviderProtocol::Anthropic, "model_context_window_exceeded", ProviderFinishReason::MaxTokens},
    ProviderFinishReasonMapping{ProviderProtocol::Anthropic, "tool_use", ProviderFinishReason::ToolCalls},
    ProviderFinishReasonMapping{ProviderProtocol::Anthropic, "refusal", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Anthropic, "content_filter", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Anthropic, "pause_turn", ProviderFinishReason::Error},

    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "STOP", ProviderFinishReason::Completed},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "MAX_TOKENS", ProviderFinishReason::MaxTokens},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "SAFETY", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "RECITATION", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "LANGUAGE", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "BLOCKLIST", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "PROHIBITED_CONTENT", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "SPII", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "IMAGE_SAFETY", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "IMAGE_PROHIBITED_CONTENT", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "IMAGE_RECITATION", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "NO_IMAGE", ProviderFinishReason::Refusal},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "FINISH_REASON_UNSPECIFIED", ProviderFinishReason::Error},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "OTHER", ProviderFinishReason::Error},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "MALFORMED_FUNCTION_CALL", ProviderFinishReason::Error},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "UNEXPECTED_TOOL_CALL", ProviderFinishReason::Error},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "TOO_MANY_TOOL_CALLS", ProviderFinishReason::Error},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "MISSING_THOUGHT_SIGNATURE", ProviderFinishReason::Error},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "MALFORMED_RESPONSE", ProviderFinishReason::Error},
    ProviderFinishReasonMapping{ProviderProtocol::Gemini, "IMAGE_OTHER", ProviderFinishReason::Error},
};

[[nodiscard]] constexpr ProviderFinishReason normalize_provider_finish_reason(ProviderProtocol protocol, std::string_view raw_reason) noexcept
{
  for (auto const& mapping : kProviderFinishReasonCatalog)
    if (mapping.protocol == protocol && mapping.raw_reason == raw_reason)
      return mapping.reason;
  return ProviderFinishReason::Error;
}

[[nodiscard]] constexpr std::string_view to_string(ProviderFinishReason reason) noexcept
{
  switch (reason)
  {
    case ProviderFinishReason::Completed:
      return "completed";
    case ProviderFinishReason::MaxTokens:
      return "max_tokens";
    case ProviderFinishReason::ToolCalls:
      return "tool_calls";
    case ProviderFinishReason::Refusal:
      return "refusal";
    case ProviderFinishReason::Cancelled:
      return "cancelled";
    case ProviderFinishReason::Error:
      return "error";
  }
  return "error";
}

}  // namespace ava::provider
