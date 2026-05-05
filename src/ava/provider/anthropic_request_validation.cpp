#include "ava/provider/anthropic_request_validation.h"

#include <cstddef>
#include <string_view>
#include <utility>

#include "ava/core/json.h"
#include "ava/provider/anthropic_request_support.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider::detail {
namespace {

struct PendingToolUse {
  std::string id;
  std::size_t message_index = 0;
  std::size_t part_index = 0;
};

ava::core::Error invalid_anthropic_content_part_error(std::string message, std::size_t message_index,
                                                      std::size_t part_index)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("provider", "anthropic");
  error.with_context("message_index", std::to_string(message_index));
  error.with_context("content_part_index", std::to_string(part_index));
  return error;
}

bool contains_tool_use_id(std::vector<std::string> const& ids, std::string_view id)
{
  for (auto const& seen : ids) {
    if (seen == id) return true;
  }
  return false;
}

}  // namespace

bool valid_anthropic_cache_control_ttl(std::string_view ttl)
{
  return ttl == "5m" || ttl == "1h";
}

bool valid_anthropic_reasoning_type(std::string_view type)
{
  return type == "enabled" || type == "adaptive";
}

bool valid_anthropic_reasoning_display(std::string_view display)
{
  return display == "summarized" || display == "omitted";
}

ava::core::VoidResult validate_anthropic_content_parts(std::vector<ChatMessage> const& messages)
{
  std::vector<PendingToolUse> pending_tool_uses;
  std::vector<std::string> seen_tool_use_ids;
  for (std::size_t message_index = 0; message_index < messages.size(); ++message_index) {
    auto const& message = messages[message_index];
    auto const role = anthropic_message_role(message);
    if (role == "assistant" && !pending_tool_uses.empty()) {
      auto const& pending = pending_tool_uses.front();
      return std::unexpected(invalid_anthropic_content_part_error(
          "Anthropic tool_use content requires matching tool_result before the next assistant message",
          pending.message_index, pending.part_index));
    }
    for (std::size_t part_index = 0; part_index < message.content_parts.size(); ++part_index) {
      auto const& part = message.content_parts[part_index];
      if (!part.cache_control_ttl.empty() && !valid_anthropic_cache_control_ttl(part.cache_control_ttl)) {
        return std::unexpected(invalid_anthropic_content_part_error("Anthropic cache_control ttl must be 5m or 1h",
                                                                    message_index, part_index));
      }
      if (part.type == ContentPartType::Text && part.text.empty()) {
        if (!part.cache_control_ttl.empty()) {
          return std::unexpected(invalid_anthropic_content_part_error(
              "Anthropic cache_control requires non-empty text content", message_index, part_index));
        }
        continue;
      }
      if (role == "user" && !pending_tool_uses.empty() && part.type != ContentPartType::ToolResult) {
        return std::unexpected(invalid_anthropic_content_part_error(
            "Anthropic tool_result content must precede ordinary user content after tool_use", message_index,
            part_index));
      }
      switch (part.type) {
        case ContentPartType::Text:
          break;
        case ContentPartType::Reasoning:
          if (role != "assistant") {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic reasoning content requires assistant role", message_index, part_index));
          }
          if (!part.cache_control_ttl.empty()) {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic cache_control is not supported on reasoning content", message_index, part_index));
          }
          if (!part.reasoning_format.empty() && part.reasoning_format != "anthropic_thinking") {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic reasoning content requires anthropic_thinking format", message_index, part_index));
          }
          if (part.redacted && part.reasoning_redacted_data.empty()) {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic redacted reasoning content requires provider redacted data", message_index, part_index));
          }
          break;
        case ContentPartType::ToolUse:
          if (role != "assistant") {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic tool_use content requires assistant role", message_index, part_index));
          }
          if (part.tool_call_id.empty() || part.tool_name.empty()) {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic tool_use content requires id and name", message_index, part_index));
          }
          if (!is_valid_json_object(part.input_json)) {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic tool_use input must be a valid JSON object", message_index, part_index));
          }
          if (contains_tool_use_id(seen_tool_use_ids, part.tool_call_id)) {
            return std::unexpected(invalid_anthropic_content_part_error("Anthropic tool_use id must be unique",
                                                                        message_index, part_index));
          }
          pending_tool_uses.push_back(
              PendingToolUse{.id = part.tool_call_id, .message_index = message_index, .part_index = part_index});
          seen_tool_use_ids.push_back(part.tool_call_id);
          break;
        case ContentPartType::ToolResult:
          if (role != "user") {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic tool_result content requires user role", message_index, part_index));
          }
          if (part.tool_call_id.empty()) {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic tool_result content requires tool_use_id", message_index, part_index));
          }
          if (pending_tool_uses.empty() || pending_tool_uses.front().id != part.tool_call_id) {
            return std::unexpected(invalid_anthropic_content_part_error(
                "Anthropic tool_result content requires a preceding unmatched tool_use", message_index, part_index));
          }
          pending_tool_uses.erase(pending_tool_uses.begin());
          break;
      }
    }
    if (role == "user" && !pending_tool_uses.empty()) {
      auto const& pending = pending_tool_uses.front();
      return std::unexpected(invalid_anthropic_content_part_error(
          "Anthropic tool_use content requires matching tool_result in the next user message", pending.message_index,
          pending.part_index));
    }
  }
  if (!pending_tool_uses.empty()) {
    auto const& pending = pending_tool_uses.front();
    return std::unexpected(invalid_anthropic_content_part_error(
        "Anthropic tool_use content requires matching tool_result", pending.message_index, pending.part_index));
  }
  return {};
}

ava::core::VoidResult validate_anthropic_request_options(ProviderRequest const& request)
{
  if (!request.system_prompt_cache_ttl.empty() && !valid_anthropic_cache_control_ttl(request.system_prompt_cache_ttl)) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "Anthropic system cache ttl must be 5m or 1h"));
  }
  if (!request.system_prompt_cache_ttl.empty() && request.system_prompt.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "Anthropic system cache ttl requires a non-empty system prompt"));
  }
  if (!request.reasoning) return {};
  auto const& reasoning = *request.reasoning;
  if (!valid_anthropic_reasoning_type(reasoning.type)) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "Anthropic reasoning type must be enabled or adaptive"));
  }
  if (!reasoning.display.empty() && !valid_anthropic_reasoning_display(reasoning.display)) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "Anthropic reasoning display must be summarized or omitted"));
  }
  if (reasoning.budget_tokens && *reasoning.budget_tokens <= 0) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "Anthropic reasoning budget must be positive"));
  }
  if (reasoning.type == "enabled" && !reasoning.budget_tokens) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "Anthropic enabled reasoning requires a budget"));
  }
  if (reasoning.type == "enabled" && reasoning.budget_tokens && *reasoning.budget_tokens < 1024) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "Anthropic reasoning budget must be at least 1024 tokens"));
  }
  if (reasoning.type == "adaptive" && reasoning.budget_tokens) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "Anthropic adaptive reasoning does not accept a fixed budget"));
  }
  if (reasoning.type == "adaptive" && request.model_id == "claude-sonnet-4-5") {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "Anthropic adaptive reasoning is not supported by this model"));
  }
  if (reasoning.budget_tokens && *reasoning.budget_tokens >= anthropic_max_tokens_for_request(request)) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "Anthropic reasoning budget must be below max output tokens"));
  }
  return {};
}

ava::core::VoidResult validate_anthropic_cache_control_order(ProviderRequest const& request,
                                                             std::vector<ChatMessage> const& messages)
{
  bool saw_short_ttl = false;
  std::size_t breakpoints = 0;
  auto observe = [&](std::string_view ttl) -> ava::core::VoidResult {
    if (ttl.empty()) return {};
    ++breakpoints;
    if (breakpoints > 4) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "Anthropic supports at most four cache breakpoints"));
    }
    if (saw_short_ttl && ttl == "1h") {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "Anthropic 1h cache ttl cannot follow a 5m cache ttl"));
    }
    if (ttl == "5m") saw_short_ttl = true;
    return {};
  };

  if (auto observed = observe(request.system_prompt_cache_ttl); !observed) return observed;
  for (auto const& message : messages) {
    for (auto const& part : anthropic_message_content_parts(message)) {
      if (auto observed = observe(part.cache_control_ttl); !observed) return observed;
    }
  }
  return {};
}

}  // namespace ava::provider::detail
