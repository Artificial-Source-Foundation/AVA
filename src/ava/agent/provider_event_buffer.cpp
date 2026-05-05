#include "ava/agent/provider_event_buffer.h"

#include <optional>
#include <string>
#include <utility>

#include "ava/agent/provider_output_validation.h"

namespace ava::agent {
namespace {

bool is_reasoning_event(ava::provider::StreamEventType type)
{
  return type == ava::provider::StreamEventType::ReasoningStart ||
         type == ava::provider::StreamEventType::ReasoningDelta || type == ava::provider::StreamEventType::ReasoningEnd;
}

std::size_t accounted_assistant_bytes(ava::provider::StreamEvent const& event)
{
  if (event.type == ava::provider::StreamEventType::ReasoningEnd) {
    return event.reasoning_signature.size() + event.reasoning_redacted_data.size();
  }
  if (is_reasoning_event(event.type)) {
    return event.text.size() + event.reasoning_signature.size() + event.reasoning_redacted_data.size();
  }
  if (event.type == ava::provider::StreamEventType::TextDelta) return event.text.size();
  return 0;
}

}  // namespace

ProviderEventBuffer::ProviderEventBuffer(ProviderOutputLimits limits) : limits_(limits)
{
}

std::vector<ava::provider::StreamEvent> const& ProviderEventBuffer::events() const noexcept
{
  return events_;
}

bool ProviderEventBuffer::empty() const noexcept
{
  return events_.empty();
}

ava::core::VoidResult ProviderEventBuffer::append(std::vector<ava::provider::StreamEvent> new_events,
                                                  ProviderEventPublisher const& publisher, bool publish_all_events)
{
  for (auto& event : new_events) {
    if (limits_.max_events > 0 && events_.size() >= limits_.max_events) {
      return std::unexpected(
          output_limit_error("provider output event limit exceeded", "max_provider_events", limits_.max_events));
    }

    auto const assistant_bytes = accounted_assistant_bytes(event);
    std::optional<std::string> tool_argument_call_id;
    std::size_t tool_argument_delta_bytes = 0;
    if (assistant_bytes > 0) {
      auto const message =
          is_reasoning_event(event.type) ? "reasoning byte limit exceeded" : "assistant text byte limit exceeded";
      if (would_exceed(assistant_text_bytes_, assistant_bytes, limits_.max_assistant_text_bytes)) {
        return std::unexpected(
            output_limit_error(std::string(message), "max_assistant_text_bytes", limits_.max_assistant_text_bytes));
      }
    } else if (event.type == ava::provider::StreamEventType::ToolCallStart) {
      if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id) {
        return std::unexpected(std::move(valid_id.error()));
      }
    } else if (event.type == ava::provider::StreamEventType::ToolCallDelta) {
      if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id) {
        return std::unexpected(std::move(valid_id.error()));
      }
      auto const bytes = tool_argument_bytes_.find(event.tool_call_id);
      auto const current_bytes = bytes == tool_argument_bytes_.end() ? 0 : bytes->second;
      if (would_exceed(current_bytes, event.text.size(), limits_.max_tool_argument_bytes)) {
        return std::unexpected(output_limit_error("tool argument byte limit exceeded", "max_tool_argument_bytes",
                                                  limits_.max_tool_argument_bytes));
      }
      tool_argument_call_id = event.tool_call_id;
      tool_argument_delta_bytes = event.text.size();
    }

    bool const should_publish = publish_all_events || is_reasoning_event(event.type);
    if (should_publish && publisher) {
      if (auto published = publisher(event); !published) {
        return std::unexpected(std::move(published.error()));
      }
    }
    assistant_text_bytes_ += assistant_bytes;
    if (tool_argument_call_id) tool_argument_bytes_[*tool_argument_call_id] += tool_argument_delta_bytes;
    events_.push_back(std::move(event));
  }
  return {};
}

}  // namespace ava::agent
