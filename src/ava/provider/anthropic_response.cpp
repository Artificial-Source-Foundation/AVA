#include "ava/provider/anthropic_response.h"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/core/json.h"
#include "ava/provider/anthropic_response_support.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider {
namespace {

using detail::append_stream_error;
using detail::has_stop_details;
using detail::kMaxAnthropicReasoningOpaqueBytes;
using detail::merge_usage;
using detail::non_negative_integer_field;
using detail::normalized_anthropic_stop_reason;
using detail::sanitized_anthropic_body_snippet;
using detail::stop_details_explanation;

void append_event_for_data(std::vector<StreamEvent>& events,
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

void append_events_for_sse_line(std::vector<StreamEvent>& events,
                                std::map<long long, AnthropicStreamParser::ToolBlock>& tools,
                                std::map<long long, AnthropicStreamParser::ReasoningBlock>& reasoning_blocks,
                                std::optional<TokenUsage>& usage, std::string& stop_reason, std::string& data,
                                bool& saw_data, bool& message_stop_seen, bool& error_seen, std::string line)
{
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!data.empty()) {
      append_event_for_data(events, tools, reasoning_blocks, usage, stop_reason, saw_data, message_stop_seen,
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

}  // namespace

ava::core::Result<std::vector<StreamEvent>> AnthropicStreamParser::append(std::string_view chunk)
{
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true) {
    auto const newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos) break;
    append_events_for_sse_line(events, tool_blocks_, reasoning_blocks_, usage_, stop_reason_, data_, saw_data_,
                               message_stop_seen_, error_seen_, pending_line_.substr(line_start, newline - line_start));
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0) pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  return events;
}

ava::core::Result<std::vector<StreamEvent>> AnthropicStreamParser::finish()
{
  std::vector<StreamEvent> events;
  if (!pending_line_.empty()) {
    append_events_for_sse_line(events, tool_blocks_, reasoning_blocks_, usage_, stop_reason_, data_, saw_data_,
                               message_stop_seen_, error_seen_, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty()) {
    append_event_for_data(events, tool_blocks_, reasoning_blocks_, usage_, stop_reason_, saw_data_, message_stop_seen_,
                          error_seen_, data_);
    data_.clear();
  }
  if (saw_data_ && !message_stop_seen_ && !error_seen_) {
    append_stream_error(events, "Anthropic SSE stream ended before message_stop");
  }
  tool_blocks_.clear();
  reasoning_blocks_.clear();
  usage_ = std::nullopt;
  stop_reason_.clear();
  saw_data_ = false;
  message_stop_seen_ = false;
  error_seen_ = false;
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_anthropic_sse(std::string_view sse)
{
  AnthropicStreamParser parser;
  auto events = parser.append(sse);
  if (!events) return std::unexpected(std::move(events.error()));
  auto final_events = parser.finish();
  if (!final_events) return std::unexpected(std::move(final_events.error()));
  events->insert(events->end(), final_events->begin(), final_events->end());
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_anthropic_sse_response(HttpResponse const& response)
{
  if (response.status_code < 200 || response.status_code >= 300) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider,
                                  "Anthropic HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(classify_provider_error(response)));
    if (auto const retry_after = retry_after_header(response)) error.with_context("retry_after", *retry_after);
    if (!response.body.empty()) {
      error.with_context("body_snippet", sanitized_anthropic_body_snippet(response.body));
    }
    return std::unexpected(std::move(error));
  }
  return parse_anthropic_sse(response.body);
}

ava::core::Result<std::vector<StreamEvent>> parse_anthropic_response(HttpResponse const& response)
{
  if (response.status_code < 200 || response.status_code >= 300) return parse_anthropic_sse_response(response);
  std::vector<StreamEvent> events;
  bool parsed_content = false;
  std::string const stop_reason =
      normalized_anthropic_stop_reason(ava::core::json::string_field(response.body, "stop_reason").value_or(""));
  for (auto const& block : ava::core::json::objects_in_array_field(response.body, "content")) {
    auto const type = ava::core::json::string_field(block, "type").value_or("");
    if (type == "text") {
      parsed_content = true;
      events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                   .text = ava::core::json::string_field(block, "text").value_or(""),
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
    } else if (type == "refusal") {
      parsed_content = true;
      events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                   .text = ava::core::json::string_field(block, "refusal").value_or(""),
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
    } else if (type == "thinking") {
      parsed_content = true;
      auto const thinking = ava::core::json::string_field(block, "thinking").value_or("");
      auto const signature = ava::core::json::string_field(block, "signature").value_or("");
      if (signature.size() > kMaxAnthropicReasoningOpaqueBytes) {
        return std::unexpected(
            ava::core::Error(ava::core::ErrorCategory::Provider, "Anthropic thinking signature exceeded byte limit"));
      }
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = "anthropic_thinking"});
      if (!thinking.empty()) {
        events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                                     .text = thinking,
                                     .tool_call_id = "",
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt,
                                     .reasoning_format = "anthropic_thinking"});
      }
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = "anthropic_thinking",
                                   .reasoning_signature = signature});
    } else if (type == "redacted_thinking") {
      parsed_content = true;
      auto const redacted_data = ava::core::json::string_field(block, "data").value_or("");
      if (redacted_data.size() > kMaxAnthropicReasoningOpaqueBytes) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider,
                                                "Anthropic redacted thinking payload exceeded byte limit"));
      }
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = "anthropic_thinking",
                                   .redacted = true});
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = "anthropic_thinking",
                                   .reasoning_redacted_data = redacted_data,
                                   .redacted = true});
    } else if (type == "tool_use") {
      parsed_content = true;
      auto const id = ava::core::json::string_field(block, "id").value_or("");
      auto const name = ava::core::json::string_field(block, "name").value_or("");
      auto const input = ava::core::json::object_field(block, "input").value_or("{}");
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallStart,
                                   .text = "",
                                   .tool_call_id = id,
                                   .tool_name = name,
                                   .error_message = "",
                                   .usage = std::nullopt});
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallDelta,
                                   .text = input,
                                   .tool_call_id = id,
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallEnd,
                                   .text = "",
                                   .tool_call_id = id,
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
    }
  }
  if (!parsed_content && stop_reason == "refusal") {
    if (has_stop_details(response.body)) {
      parsed_content = true;
      if (auto explanation = stop_details_explanation(response.body); !explanation.empty()) {
        events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                     .text = std::move(explanation),
                                     .tool_call_id = "",
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt});
      }
    }
  }
  if (!parsed_content) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "Anthropic response content is missing");
    if (!response.body.empty()) {
      error.with_context("body_snippet", sanitized_anthropic_body_snippet(response.body));
    }
    return std::unexpected(std::move(error));
  }
  events.push_back(StreamEvent{.type = StreamEventType::Done,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = parse_anthropic_usage(response.body),
                               .stop_reason = stop_reason});
  return events;
}

std::optional<TokenUsage> parse_anthropic_usage(std::string_view body)
{
  auto const usage_object = ava::core::json::object_field(body, "usage");
  auto const usage_view = usage_object ? std::string_view(*usage_object) : body;
  TokenUsage usage;
  auto const regular_input_tokens = non_negative_integer_field(usage_view, "input_tokens");
  usage.output_tokens = non_negative_integer_field(usage_view, "output_tokens");
  usage.cache_read_tokens = non_negative_integer_field(usage_view, "cache_read_input_tokens");
  usage.cache_write_tokens = non_negative_integer_field(usage_view, "cache_creation_input_tokens");
  long long const input_total =
      regular_input_tokens.value_or(0) + usage.cache_read_tokens.value_or(0) + usage.cache_write_tokens.value_or(0);
  if (input_total > 0) usage.input_tokens = input_total;
  long long const total = input_total + usage.output_tokens.value_or(0);
  if (total > 0) usage.total_tokens = total;
  if (!usage.input_tokens && !usage.output_tokens && !usage.cache_read_tokens && !usage.cache_write_tokens) {
    return std::nullopt;
  }
  return usage;
}

}  // namespace ava::provider
