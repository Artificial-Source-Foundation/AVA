#include "ava/provider/anthropic_response_support.h"

#include <utility>

#include "ava/core/json.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider::detail {

std::string normalized_anthropic_stop_reason(std::string_view reason)
{
  if (reason == "end_turn") return "completed";
  if (reason == "tool_use") return "tool_calls";
  if (reason == "max_tokens") return "max_tokens";
  if (reason == "stop_sequence") return "stop_sequence";
  if (reason == "content_filter") return "content_filter";
  if (reason == "pause_turn") return "pause_turn";
  if (reason == "refusal") return "refusal";
  return std::string(reason);
}

std::string stream_error_message(std::string_view message)
{
  return message.empty() ? "unrecognized Anthropic stream event" : std::string(message);
}

std::string sanitized_anthropic_body_snippet(std::string_view body)
{
  return sanitized_body_snippet(body, {"access_token", "refresh_token", "api_key", "x-api-key", "Authorization",
                                       "signature", "redacted_data", "data", "thinking"});
}

std::string stop_details_explanation(std::string_view object)
{
  auto const stop_details = ava::core::json::object_field(object, "stop_details");
  if (!stop_details) return {};
  return ava::core::json::string_field(*stop_details, "explanation").value_or("");
}

bool has_stop_details(std::string_view object)
{
  return ava::core::json::object_field(object, "stop_details").has_value();
}

void append_stream_error(std::vector<StreamEvent>& events, std::string_view message)
{
  events.push_back(StreamEvent{.type = StreamEventType::Error,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = stream_error_message(message),
                               .usage = std::nullopt});
}

std::optional<long long> non_negative_integer_field(std::string_view object, std::string_view key)
{
  auto const value = ava::core::json::integer_field(object, key);
  if (!value || *value < 0) return std::nullopt;
  return value;
}

void merge_usage(TokenUsage& target, TokenUsage const& source)
{
  if (source.input_tokens) target.input_tokens = source.input_tokens;
  if (source.output_tokens) target.output_tokens = source.output_tokens;
  if (source.cache_read_tokens) target.cache_read_tokens = source.cache_read_tokens;
  if (source.cache_write_tokens) target.cache_write_tokens = source.cache_write_tokens;
  if (source.total_tokens) target.total_tokens = source.total_tokens;
}

void append_anthropic_event_for_data(std::vector<StreamEvent>& events,
                                     std::map<long long, AnthropicStreamParser::ToolBlock>& tools,
                                     std::map<long long, AnthropicStreamParser::ReasoningBlock>& reasoning_blocks,
                                     std::optional<TokenUsage>& usage, std::string& stop_reason, bool& saw_data,
                                     bool& message_stop_seen, bool& error_seen, std::string_view data)
{
  auto append_terminal_error = [&](std::string_view message) {
    error_seen = true;
    append_stream_error(events, message);
  };
  if (!is_json_object_shape(data)) {
    append_terminal_error("malformed Anthropic stream event");
    return;
  }
  auto const type = ava::core::json::string_field(data, "type").value_or("");
  if (type == "ping") return;
  saw_data = true;
  if (type == "message_start") {
    if (auto const message = ava::core::json::object_field(data, "message")) {
      if (auto const parsed = parse_anthropic_usage(*message)) {
        if (!usage) usage = TokenUsage{};
        merge_usage(*usage, *parsed);
      }
    }
    return;
  }
  if (type == "content_block_start") {
    auto const block = ava::core::json::object_field(data, "content_block");
    auto const index = non_negative_integer_field(data, "index");
    if (!block || !index) return;
    auto const block_type = ava::core::json::string_field(*block, "type").value_or("");
    if (block_type == "tool_use") {
      auto const id = ava::core::json::string_field(*block, "id").value_or("");
      auto const name = ava::core::json::string_field(*block, "name").value_or("");
      tools[*index] = AnthropicStreamParser::ToolBlock{.id = id, .name = name};
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallStart,
                                   .text = "",
                                   .tool_call_id = id,
                                   .tool_name = name,
                                   .error_message = "",
                                   .usage = std::nullopt});
    } else if (block_type == "thinking") {
      auto const signature = ava::core::json::string_field(*block, "signature").value_or("");
      if (signature.size() > kMaxAnthropicReasoningOpaqueBytes) {
        append_terminal_error("Anthropic thinking signature exceeded byte limit");
        return;
      }
      reasoning_blocks[*index] =
          AnthropicStreamParser::ReasoningBlock{.signature = signature, .redacted_data = "", .redacted = false};
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = "anthropic_thinking"});
    } else if (block_type == "redacted_thinking") {
      auto const redacted_data = ava::core::json::string_field(*block, "data").value_or("");
      if (redacted_data.size() > kMaxAnthropicReasoningOpaqueBytes) {
        append_terminal_error("Anthropic redacted thinking payload exceeded byte limit");
        return;
      }
      reasoning_blocks[*index] =
          AnthropicStreamParser::ReasoningBlock{.signature = "", .redacted_data = redacted_data, .redacted = true};
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = "anthropic_thinking",
                                   .redacted = true});
    }
    return;
  }
  if (type == "content_block_delta") {
    auto const delta = ava::core::json::object_field(data, "delta");
    if (!delta) {
      append_terminal_error("Anthropic content_block_delta is missing delta");
      return;
    }
    auto const delta_type = ava::core::json::string_field(*delta, "type").value_or("");
    if (delta_type == "text_delta") {
      events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                   .text = ava::core::json::string_field(*delta, "text").value_or(""),
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
      return;
    }
    if (delta_type == "input_json_delta") {
      auto const index = non_negative_integer_field(data, "index");
      auto const tool = index ? tools.find(*index) : tools.end();
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallDelta,
                                   .text = ava::core::json::string_field(*delta, "partial_json").value_or(""),
                                   .tool_call_id = tool == tools.end() ? "" : tool->second.id,
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
      return;
    }
    if (delta_type == "thinking_delta") {
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                                   .text = ava::core::json::string_field(*delta, "thinking").value_or(""),
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = "anthropic_thinking"});
      return;
    }
    if (delta_type == "signature_delta") {
      auto const index = non_negative_integer_field(data, "index");
      auto const reasoning = index ? reasoning_blocks.find(*index) : reasoning_blocks.end();
      if (reasoning != reasoning_blocks.end()) {
        auto const signature_delta = ava::core::json::string_field(*delta, "signature").value_or("");
        if (reasoning->second.signature.size() + signature_delta.size() > kMaxAnthropicReasoningOpaqueBytes) {
          reasoning_blocks.erase(reasoning);
          append_terminal_error("Anthropic thinking signature exceeded byte limit");
          return;
        }
        reasoning->second.signature += signature_delta;
      }
      return;
    }
    append_terminal_error("unrecognized Anthropic content_block_delta");
    return;
  }
  if (type == "content_block_stop") {
    auto const index = non_negative_integer_field(data, "index");
    if (!index) return;
    auto const tool = tools.find(*index);
    auto const reasoning = reasoning_blocks.find(*index);
    if (reasoning != reasoning_blocks.end()) {
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = "anthropic_thinking",
                                   .reasoning_signature = reasoning->second.signature,
                                   .reasoning_redacted_data = reasoning->second.redacted_data,
                                   .redacted = reasoning->second.redacted});
      reasoning_blocks.erase(reasoning);
      return;
    }
    if (tool == tools.end()) return;
    events.push_back(StreamEvent{.type = StreamEventType::ToolCallEnd,
                                 .text = "",
                                 .tool_call_id = tool->second.id,
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt});
    return;
  }
  if (type == "message_delta") {
    if (auto const parsed = parse_anthropic_usage(data)) {
      if (!usage) usage = TokenUsage{};
      merge_usage(*usage, *parsed);
    }
    if (auto const delta = ava::core::json::object_field(data, "delta")) {
      if (auto const raw_stop_reason = ava::core::json::string_field(*delta, "stop_reason"); raw_stop_reason) {
        stop_reason = normalized_anthropic_stop_reason(*raw_stop_reason);
        if (stop_reason == "refusal") {
          if (auto explanation = stop_details_explanation(*delta); !explanation.empty()) {
            events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                         .text = std::move(explanation),
                                         .tool_call_id = "",
                                         .tool_name = "",
                                         .error_message = "",
                                         .usage = std::nullopt});
          }
        }
      }
    }
    return;
  }
  if (type == "message_stop") {
    message_stop_seen = true;
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::move(usage),
                                 .stop_reason = stop_reason});
    usage = std::nullopt;
    stop_reason.clear();
    return;
  }
  if (type == "error") {
    error_seen = true;
    message_stop_seen = true;
    auto const error = ava::core::json::object_field(data, "error");
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = error ? ava::core::json::string_field(*error, "message").value_or("")
                                                        : ava::core::json::string_field(data, "message").value_or(""),
                                 .usage = std::nullopt});
    return;
  }
  append_terminal_error("unrecognized Anthropic stream event");
}

void append_anthropic_events_for_sse_line(std::vector<StreamEvent>& events,
                                          std::map<long long, AnthropicStreamParser::ToolBlock>& tools,
                                          std::map<long long, AnthropicStreamParser::ReasoningBlock>& reasoning_blocks,
                                          std::optional<TokenUsage>& usage, std::string& stop_reason, std::string& data,
                                          bool& saw_data, bool& message_stop_seen, bool& error_seen, std::string line)
{
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!data.empty()) {
      append_anthropic_event_for_data(events, tools, reasoning_blocks, usage, stop_reason, saw_data, message_stop_seen,
                                      error_seen, data);
      data.clear();
    }
    return;
  }
  if (line.starts_with("data:")) {
    if (!data.empty()) data.push_back('\n');
    auto value = std::string_view(line).substr(5);
    if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    data.append(value);
  }
}

}  // namespace ava::provider::detail
