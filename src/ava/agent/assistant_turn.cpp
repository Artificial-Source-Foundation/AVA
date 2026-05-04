#include "ava/agent/assistant_turn.h"

#include <optional>
#include <string_view>
#include <utility>

#include "ava/agent/provider_output_validation.h"
#include "ava/agent/usage_accounting.h"

namespace ava::agent {
namespace {

ProviderToolCall& pending_call_for(std::vector<ProviderToolCall>& calls, std::string_view id) {
  for (auto& call : calls) {
    if (call.id == id) return call;
  }
  calls.push_back(ProviderToolCall{.id = std::string(id), .name = "", .arguments_json = ""});
  return calls.back();
}

std::size_t reasoning_block_bytes(const ParsedReasoningBlock& block) {
  return block.text.size() + block.signature.size() + block.redacted_data.size();
}

}  // namespace

ava::core::Result<ParsedAssistantTurn> parse_assistant_turn(const std::vector<ava::provider::StreamEvent>& events,
                                                            ProviderOutputLimits limits) {
  if (limits.max_events > 0 && events.size() > limits.max_events) {
    return std::unexpected(
        output_limit_error("provider output event limit exceeded", "max_provider_events", limits.max_events));
  }
  ParsedAssistantTurn turn;
  std::optional<ParsedReasoningBlock> current_reasoning;
  auto finish_reasoning = [&]() {
    if (!current_reasoning) return;
    if (!current_reasoning->text.empty() || !current_reasoning->signature.empty() ||
        !current_reasoning->redacted_data.empty()) {
      turn.reasoning_blocks.push_back(std::move(*current_reasoning));
    }
    current_reasoning = std::nullopt;
  };
  bool done = false;
  for (const auto& event : events) {
    if (event.usage) turn.usage = with_total_tokens(*event.usage);
    if (event.type == ava::provider::StreamEventType::TextDelta) {
      if (would_exceed(turn.text.size(), event.text.size(), limits.max_assistant_text_bytes)) {
        return std::unexpected(output_limit_error("assistant text byte limit exceeded", "max_assistant_text_bytes",
                                                  limits.max_assistant_text_bytes));
      }
      turn.text += event.text;
    } else if (event.type == ava::provider::StreamEventType::ReasoningStart) {
      finish_reasoning();
      current_reasoning = ParsedReasoningBlock{.text = "",
                                               .format = event.reasoning_format,
                                               .signature = event.reasoning_signature,
                                               .redacted_data = event.reasoning_redacted_data,
                                               .redacted = event.redacted};
      const auto private_bytes = event.reasoning_signature.size() + event.reasoning_redacted_data.size();
      if (would_exceed(std::size_t{0}, private_bytes, limits.max_assistant_text_bytes)) {
        return std::unexpected(output_limit_error("reasoning byte limit exceeded", "max_assistant_text_bytes",
                                                  limits.max_assistant_text_bytes));
      }
    } else if (event.type == ava::provider::StreamEventType::ReasoningDelta) {
      if (!current_reasoning) {
        current_reasoning = ParsedReasoningBlock{.text = "",
                                                 .format = event.reasoning_format,
                                                 .signature = "",
                                                 .redacted_data = "",
                                                 .redacted = event.redacted};
      }
      if (current_reasoning->format.empty()) current_reasoning->format = event.reasoning_format;
      current_reasoning->redacted = current_reasoning->redacted || event.redacted;
      if (would_exceed(current_reasoning->text.size(), event.text.size(), limits.max_assistant_text_bytes)) {
        return std::unexpected(output_limit_error("reasoning text byte limit exceeded", "max_assistant_text_bytes",
                                                  limits.max_assistant_text_bytes));
      }
      current_reasoning->text += event.text;
      if (limits.max_assistant_text_bytes > 0 &&
          reasoning_block_bytes(*current_reasoning) > limits.max_assistant_text_bytes) {
        return std::unexpected(output_limit_error("reasoning byte limit exceeded", "max_assistant_text_bytes",
                                                  limits.max_assistant_text_bytes));
      }
    } else if (event.type == ava::provider::StreamEventType::ReasoningEnd) {
      if (!current_reasoning) {
        current_reasoning = ParsedReasoningBlock{.text = "",
                                                 .format = event.reasoning_format,
                                                 .signature = "",
                                                 .redacted_data = "",
                                                 .redacted = event.redacted};
      }
      if (current_reasoning->format.empty()) current_reasoning->format = event.reasoning_format;
      if (!event.reasoning_signature.empty()) current_reasoning->signature = event.reasoning_signature;
      if (!event.reasoning_redacted_data.empty()) current_reasoning->redacted_data = event.reasoning_redacted_data;
      current_reasoning->redacted = current_reasoning->redacted || event.redacted;
      const auto private_bytes = event.reasoning_signature.size() + event.reasoning_redacted_data.size();
      if (would_exceed(std::size_t{0}, private_bytes, limits.max_assistant_text_bytes)) {
        return std::unexpected(output_limit_error("reasoning byte limit exceeded", "max_assistant_text_bytes",
                                                  limits.max_assistant_text_bytes));
      }
      if (limits.max_assistant_text_bytes > 0 &&
          reasoning_block_bytes(*current_reasoning) > limits.max_assistant_text_bytes) {
        return std::unexpected(output_limit_error("reasoning byte limit exceeded", "max_assistant_text_bytes",
                                                  limits.max_assistant_text_bytes));
      }
      finish_reasoning();
    } else if (event.type == ava::provider::StreamEventType::ToolCallStart) {
      if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id) {
        return std::unexpected(std::move(valid_id.error()));
      }
      auto& call = pending_call_for(turn.tool_calls, event.tool_call_id);
      if (call.name.empty()) call.name = event.tool_name;
    } else if (event.type == ava::provider::StreamEventType::ToolCallDelta) {
      if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id) {
        return std::unexpected(std::move(valid_id.error()));
      }
      auto& call = pending_call_for(turn.tool_calls, event.tool_call_id);
      if (would_exceed(call.arguments_json.size(), event.text.size(), limits.max_tool_argument_bytes)) {
        return std::unexpected(output_limit_error("tool argument byte limit exceeded", "max_tool_argument_bytes",
                                                  limits.max_tool_argument_bytes));
      }
      call.arguments_json += event.text;
    } else if (event.type == ava::provider::StreamEventType::Done) {
      done = true;
      if (!event.stop_reason.empty()) turn.stop_reason = event.stop_reason;
    } else if (event.type == ava::provider::StreamEventType::Error) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "provider stream error");
      error.with_context("message", event.error_message);
      return std::unexpected(std::move(error));
    }
  }
  finish_reasoning();
  if (!done && turn.text.empty() && turn.reasoning_blocks.empty() && turn.tool_calls.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "provider response was empty"));
  }
  return turn;
}

}  // namespace ava::agent
