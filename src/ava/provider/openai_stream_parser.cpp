#include "ava/provider/openai_stream_parser.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ava/core/json.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider {
namespace {

bool is_ignored_lifecycle_event(std::string_view type) {
  return type == "response.created" || type == "response.in_progress" || type == "response.output_item.added" ||
         type == "response.output_item.done" || type == "response.content_part.added" ||
         type == "response.content_part.done" || type == "response.output_text.done" ||
         type == "response.function_call_arguments.done";
}

bool contains_string(const std::vector<std::string>& values, std::string_view value) {
  return std::ranges::find(values, value) != values.end();
}

void remember_string(std::vector<std::string>& values, std::string value) {
  if (value.empty() || contains_string(values, value)) return;
  values.push_back(std::move(value));
}

std::string reasoning_item_id_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt) {
  if (auto id = detail::first_string_field(data, {"item_id", "output_item_id"})) return *id;
  if (item) {
    if (auto id = detail::first_string_field(*item, {"id", "item_id"})) return *id;
  }
  return {};
}

void set_active_reasoning_item_id(std::string& active_item_id, std::string_view item_id) {
  if (item_id.empty() || !active_item_id.empty()) return;
  active_item_id = std::string(item_id);
}

void append_start_reasoning_if_needed(std::vector<StreamEvent>& events, bool& reasoning_open) {
  if (reasoning_open) return;
  reasoning_open = true;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void append_reasoning_delta(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen,
                            std::string& active_reasoning_text, std::string_view text) {
  append_start_reasoning_if_needed(events, reasoning_open);
  if (text.empty()) return;
  reasoning_text_seen = true;
  active_reasoning_text += text;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                               .text = std::string(text),
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void append_finish_reasoning_if_open(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen,
                                     std::string& active_reasoning_item_id, std::string& active_reasoning_text,
                                     std::vector<std::string>& completed_reasoning_item_ids,
                                     std::vector<std::string>& completed_reasoning_texts) {
  if (!reasoning_open) return;
  reasoning_open = false;
  reasoning_text_seen = false;
  remember_string(completed_reasoning_item_ids, std::move(active_reasoning_item_id));
  remember_string(completed_reasoning_texts, std::move(active_reasoning_text));
  active_reasoning_item_id.clear();
  active_reasoning_text.clear();
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void append_event_for_data(std::vector<StreamEvent>& events, std::string_view data, bool& saw_content,
                           bool& reasoning_open, bool& reasoning_text_seen, std::string& active_reasoning_item_id,
                           std::string& active_reasoning_text, std::vector<std::string>& completed_reasoning_item_ids,
                           std::vector<std::string>& completed_reasoning_texts, bool& done_seen, bool& error_seen) {
  if (data == "[DONE]") {
    if (done_seen) return;
    done_seen = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt});
    return;
  }
  if (!is_json_object_shape(data)) {
    error_seen = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "malformed OpenAI SSE event",
                                 .usage = std::nullopt});
    return;
  }
  const auto type = ava::core::json::string_field(data, "type").value_or("");
  if (type == "response.output_item.added") {
    const auto item = ava::core::json::object_field(data, "item");
    const auto item_type = item ? ava::core::json::string_field(*item, "type").value_or("") : "";
    if (item_type == "reasoning") {
      const auto item_id = reasoning_item_id_from_event(data, *item);
      if (contains_string(completed_reasoning_item_ids, item_id)) return;
      saw_content = true;
      set_active_reasoning_item_id(active_reasoning_item_id, item_id);
      append_start_reasoning_if_needed(events, reasoning_open);
    } else if (item_type == "function_call") {
      saw_content = true;
      append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                      active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
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
    const auto item = ava::core::json::object_field(data, "item");
    if (item && ava::core::json::string_field(*item, "type").value_or("") == "reasoning") {
      const auto item_id = reasoning_item_id_from_event(data, *item);
      const auto summary = detail::reasoning_summary_text_from_object(*item);
      if (contains_string(completed_reasoning_item_ids, item_id) ||
          contains_string(completed_reasoning_texts, summary)) {
        return;
      }
      saw_content = true;
      set_active_reasoning_item_id(active_reasoning_item_id, item_id);
      if (!reasoning_text_seen) {
        append_reasoning_delta(events, reasoning_open, reasoning_text_seen, active_reasoning_text, summary);
      }
      append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                      active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
    }
    return;
  }
  if (type == "response.reasoning_summary_part.added") {
    const auto item_id = reasoning_item_id_from_event(data);
    if (contains_string(completed_reasoning_item_ids, item_id)) return;
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    append_start_reasoning_if_needed(events, reasoning_open);
    return;
  }
  if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta") {
    const auto item_id = reasoning_item_id_from_event(data);
    if (contains_string(completed_reasoning_item_ids, item_id)) return;
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    append_reasoning_delta(events, reasoning_open, reasoning_text_seen, active_reasoning_text,
                           detail::first_string_field(data, {"delta", "text"}).value_or(""));
    return;
  }
  if (type == "response.reasoning_summary_text.done" || type == "response.reasoning_summary_part.done" ||
      type == "response.reasoning_text.done") {
    const auto item_id = reasoning_item_id_from_event(data);
    const auto summary = detail::reasoning_summary_text_from_object(data);
    if (contains_string(completed_reasoning_item_ids, item_id) || contains_string(completed_reasoning_texts, summary)) {
      return;
    }
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    if (!reasoning_text_seen) {
      append_reasoning_delta(events, reasoning_open, reasoning_text_seen, active_reasoning_text, summary);
    }
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
    return;
  }
  if (is_ignored_lifecycle_event(type)) return;
  if (type == "response.output_text.delta" || type == "response.text.delta") {
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
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
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
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
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
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
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
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
    done_seen = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
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
    error_seen = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                                    active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
    const auto error_object = ava::core::json::object_field(data, "error");
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
  // OpenAI may add non-content lifecycle events without changing the assistant turn.
  // Ignore unknown event types unless the provider explicitly reports an error.
}

void append_events_for_sse_line(std::vector<StreamEvent>& events, std::string& data, bool& saw_content,
                                bool& reasoning_open, bool& reasoning_text_seen, std::string& active_reasoning_item_id,
                                std::string& active_reasoning_text,
                                std::vector<std::string>& completed_reasoning_item_ids,
                                std::vector<std::string>& completed_reasoning_texts, bool& done_seen, bool& error_seen,
                                std::string line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!data.empty()) {
      append_event_for_data(events, data, saw_content, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                            active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts, done_seen,
                            error_seen);
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

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::append(std::string_view chunk) {
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true) {
    const auto newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos) break;
    append_events_for_sse_line(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_,
                               active_reasoning_item_id_, active_reasoning_text_, completed_reasoning_item_ids_,
                               completed_reasoning_texts_, done_seen_, error_seen_,
                               pending_line_.substr(line_start, newline - line_start));
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0) pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  return events;
}

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::finish() {
  std::vector<StreamEvent> events;
  if (!pending_line_.empty()) {
    append_events_for_sse_line(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_,
                               active_reasoning_item_id_, active_reasoning_text_, completed_reasoning_item_ids_,
                               completed_reasoning_texts_, done_seen_, error_seen_, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty()) {
    append_event_for_data(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_,
                          active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_, done_seen_,
                          error_seen_);
    data_.clear();
  }
  append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_,
                                  active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
  if (saw_content_ && !done_seen_ && !error_seen_) {
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "OpenAI SSE stream ended before done marker",
                                 .usage = std::nullopt});
  }
  saw_content_ = false;
  reasoning_open_ = false;
  reasoning_text_seen_ = false;
  active_reasoning_item_id_.clear();
  active_reasoning_text_.clear();
  completed_reasoning_item_ids_.clear();
  completed_reasoning_texts_.clear();
  done_seen_ = false;
  error_seen_ = false;
  return events;
}

}  // namespace ava::provider
