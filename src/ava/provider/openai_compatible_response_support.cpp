#include "ava/provider/openai_compatible_response_support.h"

#include <utility>

#include "ava/core/json.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider::detail {

std::string normalized_openai_compatible_finish_reason(std::string_view reason)
{
  if (reason == "stop") return "completed";
  if (reason == "length") return "max_tokens";
  if (reason == "tool_calls" || reason == "function_call") return "tool_calls";
  if (reason == "content_filter") return "content_filter";
  if (reason == "refusal") return "refusal";
  return std::string(reason);
}

std::string sanitized_openai_compatible_snippet(std::string_view body)
{
  return sanitized_body_snippet(body, {"access_token", "refresh_token", "api_key", "Authorization", "authorization",
                                       "reasoning_content", "thinking"});
}

std::vector<StreamEvent> finish_reasoning_if_open(bool& reasoning_open, std::string_view reasoning_format)
{
  if (!reasoning_open) return {};
  reasoning_open = false;
  return {StreamEvent{.type = StreamEventType::ReasoningEnd,
                      .text = "",
                      .tool_call_id = "",
                      .tool_name = "",
                      .error_message = "",
                      .usage = std::nullopt,
                      .reasoning_format = std::string(reasoning_format)}};
}

void append_finish_reasoning_if_open(std::vector<StreamEvent>& events, bool& reasoning_open,
                                     std::string_view reasoning_format)
{
  auto reasoning_end = finish_reasoning_if_open(reasoning_open, reasoning_format);
  events.insert(events.end(), reasoning_end.begin(), reasoning_end.end());
}

void append_openai_compatible_done(std::vector<StreamEvent>& events, std::optional<TokenUsage> usage,
                                   std::string stop_reason)
{
  events.push_back(StreamEvent{.type = StreamEventType::Done,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::move(usage),
                               .stop_reason = std::move(stop_reason)});
}

void append_openai_compatible_tool_call_end_events(std::vector<StreamEvent>& events,
                                                   std::map<int, std::string>& open_tool_call_ids)
{
  for (auto const& [_, id] : open_tool_call_ids) {
    events.push_back(StreamEvent{.type = StreamEventType::ToolCallEnd,
                                 .text = "",
                                 .tool_call_id = id,
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt});
  }
  open_tool_call_ids.clear();
}

void append_openai_compatible_tool_call_delta_events(std::vector<StreamEvent>& events,
                                                     std::map<int, std::string>& open_tool_call_ids,
                                                     std::string_view delta)
{
  for (auto const& call : ava::core::json::objects_in_array_field(delta, "tool_calls")) {
    auto const index_value = ava::core::json::integer_field(call, "index").value_or(0);
    auto const index = static_cast<int>(index_value);
    auto const function = ava::core::json::object_field(call, "function");
    auto const id = ava::core::json::string_field(call, "id").value_or("call_" + std::to_string(index));
    auto const name = function ? ava::core::json::string_field(*function, "name").value_or("") : "";
    if (!open_tool_call_ids.contains(index)) {
      open_tool_call_ids[index] = id;
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallStart,
                                   .text = "",
                                   .tool_call_id = id,
                                   .tool_name = name,
                                   .error_message = "",
                                   .usage = std::nullopt});
    }
    if (function) {
      if (auto arguments = ava::core::json::string_field(*function, "arguments")) {
        events.push_back(StreamEvent{.type = StreamEventType::ToolCallDelta,
                                     .text = *arguments,
                                     .tool_call_id = open_tool_call_ids[index],
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt});
      }
    }
  }
}

void append_openai_compatible_choice_delta_events(std::vector<StreamEvent>& events,
                                                  std::map<int, std::string>& open_tool_call_ids, bool& reasoning_open,
                                                  std::string& stop_reason, std::string_view choice,
                                                  std::string_view reasoning_format)
{
  if (auto finish_reason = ava::core::json::string_field(choice, "finish_reason")) {
    stop_reason = normalized_openai_compatible_finish_reason(*finish_reason);
    if (*finish_reason == "tool_calls" || *finish_reason == "function_call") {
      append_openai_compatible_tool_call_end_events(events, open_tool_call_ids);
    }
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_format);
  }
  auto const delta = ava::core::json::object_field(choice, "delta");
  if (!delta) return;
  if (auto reasoning = ava::core::json::string_field(*delta, "reasoning_content")) {
    if (!reasoning_open) {
      reasoning_open = true;
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = std::string(reasoning_format)});
    }
    if (!reasoning->empty()) {
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                                   .text = *reasoning,
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = std::string(reasoning_format)});
    }
  }
  if (auto content = ava::core::json::string_field(*delta, "content")) {
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_format);
    if (!content->empty()) {
      events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                   .text = *content,
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
    }
  }
  append_openai_compatible_tool_call_delta_events(events, open_tool_call_ids, *delta);
}

void append_openai_compatible_event_for_data(std::vector<StreamEvent>& events,
                                             std::map<int, std::string>& open_tool_call_ids, bool& reasoning_open,
                                             std::optional<TokenUsage>& usage, std::string& stop_reason, bool& saw_data,
                                             bool& done_seen, bool& error_seen, std::string_view data,
                                             std::string_view reasoning_format)
{
  if (data == "[DONE]") {
    done_seen = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_format);
    append_openai_compatible_tool_call_end_events(events, open_tool_call_ids);
    append_openai_compatible_done(events, std::move(usage), std::move(stop_reason));
    usage = std::nullopt;
    stop_reason.clear();
    return;
  }
  saw_data = true;
  if (!is_json_object_shape(data)) {
    error_seen = true;
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "malformed OpenAI-compatible SSE event",
                                 .usage = std::nullopt});
    return;
  }
  if (auto const parsed_usage = parse_openai_usage(data)) usage = parsed_usage;
  for (auto const& choice : ava::core::json::objects_in_array_field(data, "choices")) {
    append_openai_compatible_choice_delta_events(events, open_tool_call_ids, reasoning_open, stop_reason, choice,
                                                 reasoning_format);
  }
  if (auto const error_object = ava::core::json::object_field(data, "error")) {
    done_seen = true;
    error_seen = true;
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = sanitized_openai_compatible_snippet(
                                     ava::core::json::string_field(*error_object, "message").value_or("")),
                                 .usage = std::nullopt});
  }
}

void append_openai_compatible_events_for_sse_line(std::vector<StreamEvent>& events,
                                                  std::map<int, std::string>& open_tool_call_ids, bool& reasoning_open,
                                                  std::optional<TokenUsage>& usage, std::string& stop_reason,
                                                  bool& saw_data, bool& done_seen, bool& error_seen, std::string& data,
                                                  std::string line, std::string_view reasoning_format)
{
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!data.empty()) {
      append_openai_compatible_event_for_data(events, open_tool_call_ids, reasoning_open, usage, stop_reason, saw_data,
                                              done_seen, error_seen, data, reasoning_format);
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

ava::core::Result<std::vector<StreamEvent>> parse_openai_compatible_chat_response(std::string_view body,
                                                                                  std::string_view reasoning_format)
{
  std::vector<StreamEvent> events;
  auto const usage = parse_openai_usage(body);
  std::string stop_reason;
  bool parsed_message = false;
  for (auto const& choice : ava::core::json::objects_in_array_field(body, "choices")) {
    if (auto finish_reason = ava::core::json::string_field(choice, "finish_reason")) {
      stop_reason = normalized_openai_compatible_finish_reason(*finish_reason);
    }
    auto const message = ava::core::json::object_field(choice, "message");
    if (!message) {
      if (stop_reason == "content_filter" || stop_reason == "refusal") {
        parsed_message = true;
        break;
      }
      continue;
    }
    bool parsed_content = false;
    if (auto reasoning = ava::core::json::string_field(*message, "reasoning_content")) {
      parsed_content = true;
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = std::string(reasoning_format)});
      if (!reasoning->empty()) {
        events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                                     .text = *reasoning,
                                     .tool_call_id = "",
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt,
                                     .reasoning_format = std::string(reasoning_format)});
      }
      events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                                   .text = "",
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt,
                                   .reasoning_format = std::string(reasoning_format)});
    }
    if (auto content = ava::core::json::string_field(*message, "content")) {
      parsed_content = true;
      if (!content->empty()) {
        events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                     .text = *content,
                                     .tool_call_id = "",
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt});
      }
    }
    auto const tool_calls = ava::core::json::objects_in_array_field(*message, "tool_calls");
    if (!tool_calls.empty()) parsed_content = true;
    for (auto const& tool_call : tool_calls) {
      auto const function = ava::core::json::object_field(tool_call, "function");
      auto const id = ava::core::json::string_field(tool_call, "id").value_or("");
      auto const name = function ? ava::core::json::string_field(*function, "name").value_or("") : "";
      auto const arguments = function ? ava::core::json::string_field(*function, "arguments").value_or("") : "";
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallStart,
                                   .text = "",
                                   .tool_call_id = id,
                                   .tool_name = name,
                                   .error_message = "",
                                   .usage = std::nullopt});
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallDelta,
                                   .text = arguments,
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
    if (!parsed_content && stop_reason != "content_filter" && stop_reason != "refusal") {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI-compatible response content is missing"));
    }
    parsed_message = true;
    break;
  }
  if (!parsed_message) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI-compatible response message is missing"));
  }
  append_openai_compatible_done(events, usage, std::move(stop_reason));
  return events;
}

}  // namespace ava::provider::detail
