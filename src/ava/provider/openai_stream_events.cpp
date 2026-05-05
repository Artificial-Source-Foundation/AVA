#include "ava/provider/openai_stream_events.h"

#include <optional>
#include <utility>

#include "ava/core/json.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_reasoning.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider::detail {

void OpenAIStreamEventState::reset()
{
  data.clear();
  saw_content = false;
  reasoning_open = false;
  reasoning_text_seen = false;
  active_reasoning_item_id.clear();
  active_reasoning_text.clear();
  completed_reasoning_item_ids.clear();
  completed_reasoning_texts.clear();
  done_seen = false;
  error_seen = false;
}

bool is_ignored_openai_lifecycle_event(std::string_view type)
{
  return type == "response.created" || type == "response.in_progress" || type == "response.output_item.added" ||
         type == "response.output_item.done" || type == "response.content_part.added" ||
         type == "response.content_part.done" || type == "response.output_text.done" ||
         type == "response.function_call_arguments.done";
}

void append_openai_event_for_data(std::vector<StreamEvent>& events, OpenAIStreamEventState& state,
                                  std::string_view data)
{
  if (data == "[DONE]") {
    if (state.done_seen) return;
    state.done_seen = true;
    append_openai_reasoning_end_if_open(events, state);
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt});
    return;
  }
  if (!is_json_object_shape(data)) {
    state.error_seen = true;
    append_openai_reasoning_end_if_open(events, state);
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "malformed OpenAI SSE event",
                                 .usage = std::nullopt});
    return;
  }
  auto const type = ava::core::json::string_field(data, "type").value_or("");
  if (type == "response.output_item.added") {
    auto const item = ava::core::json::object_field(data, "item");
    auto const item_type = item ? ava::core::json::string_field(*item, "type").value_or("") : "";
    if (item_type == "reasoning") {
      auto const item_id = openai_reasoning_item_id_from_event(data, *item);
      if (openai_stream_remembers(state.completed_reasoning_item_ids, item_id)) return;
      state.saw_content = true;
      set_active_openai_reasoning_item_id(state, item_id);
      append_openai_reasoning_start_if_needed(events, state);
    } else if (item_type == "function_call") {
      state.saw_content = true;
      append_openai_reasoning_end_if_open(events, state);
      events.push_back(StreamEvent{
          .type = StreamEventType::ToolCallStart,
          .text = "",
          .tool_call_id = detail::first_string_field(*item, {"id", "item_id", "call_id"})
                              .or_else([&data]() { return detail::first_string_field(data, {"item_id", "call_id"}); })
                              .value_or(""),
          .tool_name = ava::core::json::string_field(*item, "name")
                           .or_else([&data]() { return ava::core::json::string_field(data, "name"); })
                           .value_or(""),
          .error_message = "",
          .usage = std::nullopt});
    }
    return;
  }
  if (type == "response.output_item.done") {
    auto const item = ava::core::json::object_field(data, "item");
    if (item && ava::core::json::string_field(*item, "type").value_or("") == "reasoning") {
      auto const item_id = openai_reasoning_item_id_from_event(data, *item);
      auto const summary = detail::reasoning_summary_text_from_object(*item);
      if (openai_stream_remembers(state.completed_reasoning_item_ids, item_id) ||
          openai_stream_remembers(state.completed_reasoning_texts, summary)) {
        return;
      }
      state.saw_content = true;
      set_active_openai_reasoning_item_id(state, item_id);
      if (!state.reasoning_text_seen) {
        append_openai_reasoning_delta(events, state, summary);
      }
      append_openai_reasoning_end_if_open(events, state);
    }
    return;
  }
  if (type == "response.reasoning_summary_part.added") {
    auto const item_id = openai_reasoning_item_id_from_event(data);
    if (openai_stream_remembers(state.completed_reasoning_item_ids, item_id)) return;
    state.saw_content = true;
    set_active_openai_reasoning_item_id(state, item_id);
    append_openai_reasoning_start_if_needed(events, state);
    return;
  }
  if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta") {
    auto const item_id = openai_reasoning_item_id_from_event(data);
    if (openai_stream_remembers(state.completed_reasoning_item_ids, item_id)) return;
    state.saw_content = true;
    set_active_openai_reasoning_item_id(state, item_id);
    append_openai_reasoning_delta(events, state, detail::first_string_field(data, {"delta", "text"}).value_or(""));
    return;
  }
  if (type == "response.reasoning_summary_text.done" || type == "response.reasoning_summary_part.done" ||
      type == "response.reasoning_text.done") {
    auto const item_id = openai_reasoning_item_id_from_event(data);
    auto const summary = detail::reasoning_summary_text_from_object(data);
    if (openai_stream_remembers(state.completed_reasoning_item_ids, item_id) ||
        openai_stream_remembers(state.completed_reasoning_texts, summary)) {
      return;
    }
    state.saw_content = true;
    set_active_openai_reasoning_item_id(state, item_id);
    if (!state.reasoning_text_seen) {
      append_openai_reasoning_delta(events, state, summary);
    }
    append_openai_reasoning_end_if_open(events, state);
    return;
  }
  if (is_ignored_openai_lifecycle_event(type)) return;
  if (type == "response.output_text.delta" || type == "response.text.delta") {
    state.saw_content = true;
    append_openai_reasoning_end_if_open(events, state);
    events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                 .text = ava::core::json::string_field(data, "delta")
                                             .or_else([&data]() { return ava::core::json::string_field(data, "text"); })
                                             .value_or(""),
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt});
    return;
  }
  if (type == "response.function_call_arguments.delta") {
    state.saw_content = true;
    append_openai_reasoning_end_if_open(events, state);
    events.push_back(
        StreamEvent{.type = StreamEventType::ToolCallDelta,
                    .text = ava::core::json::string_field(data, "delta").value_or(""),
                    .tool_call_id = ava::core::json::string_field(data, "item_id")
                                        .or_else([&data]() { return ava::core::json::string_field(data, "call_id"); })
                                        .value_or(""),
                    .tool_name = "",
                    .error_message = "",
                    .usage = std::nullopt});
    return;
  }
  if (type == "response.function_call.added") {
    state.saw_content = true;
    append_openai_reasoning_end_if_open(events, state);
    events.push_back(
        StreamEvent{.type = StreamEventType::ToolCallStart,
                    .text = "",
                    .tool_call_id = ava::core::json::string_field(data, "item_id")
                                        .or_else([&data]() { return ava::core::json::string_field(data, "call_id"); })
                                        .value_or(""),
                    .tool_name = ava::core::json::string_field(data, "name").value_or(""),
                    .error_message = "",
                    .usage = std::nullopt});
    return;
  }
  if (type == "response.function_call.done" || type == "response.function_call.completed") {
    state.saw_content = true;
    append_openai_reasoning_end_if_open(events, state);
    events.push_back(
        StreamEvent{.type = StreamEventType::ToolCallEnd,
                    .text = "",
                    .tool_call_id = ava::core::json::string_field(data, "item_id")
                                        .or_else([&data]() { return ava::core::json::string_field(data, "call_id"); })
                                        .value_or(""),
                    .tool_name = "",
                    .error_message = "",
                    .usage = std::nullopt});
    return;
  }
  if (type == "response.completed" || type == "response.incomplete") {
    state.done_seen = true;
    append_openai_reasoning_end_if_open(events, state);
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = parse_openai_usage(data),
                                 .stop_reason = detail::openai_response_stop_reason(data)});
    return;
  }
  if (type == "response.error" || type == "response.failed") {
    state.error_seen = true;
    append_openai_reasoning_end_if_open(events, state);
    auto const error_object = ava::core::json::object_field(data, "error");
    events.push_back(
        StreamEvent{.type = StreamEventType::Error,
                    .text = "",
                    .tool_call_id = "",
                    .tool_name = "",
                    .error_message = error_object ? ava::core::json::string_field(*error_object, "message").value_or("")
                                                  : ava::core::json::string_field(data, "message").value_or(""),
                    .usage = std::nullopt});
    return;
  }
  // Providers may add non-content lifecycle events without changing the assistant turn.
}

void append_openai_events_for_sse_line(std::vector<StreamEvent>& events, OpenAIStreamEventState& state,
                                       std::string line)
{
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!state.data.empty()) {
      append_openai_event_for_data(events, state, state.data);
      state.data.clear();
    }
    return;
  }
  if (line.starts_with("data:")) {
    if (!state.data.empty()) state.data.push_back('\n');
    auto value = std::string_view(line).substr(5);
    if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    state.data.append(value);
  }
}

void finish_openai_stream_events(std::vector<StreamEvent>& events, OpenAIStreamEventState& state)
{
  append_openai_reasoning_end_if_open(events, state);
  if (state.saw_content && !state.done_seen && !state.error_seen) {
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "OpenAI SSE stream ended before done marker",
                                 .usage = std::nullopt});
  }
  state.reset();
}

}  // namespace ava::provider::detail
