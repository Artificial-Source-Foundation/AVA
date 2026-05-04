#include "ava/provider/openai_compatible_provider.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

#include "ava/core/json.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider {
namespace {

std::string join_url(std::string_view base_url, std::string_view path) {
  std::string result(base_url);
  while (!result.empty() && result.back() == '/') result.pop_back();
  if (path.empty()) return result;
  if (path.front() != '/') result.push_back('/');
  result.append(path);
  return result;
}

std::string normalized_finish_reason(std::string_view reason) {
  if (reason == "stop") return "completed";
  if (reason == "length") return "max_tokens";
  if (reason == "tool_calls" || reason == "function_call") return "tool_calls";
  if (reason == "content_filter") return "content_filter";
  if (reason == "refusal") return "refusal";
  return std::string(reason);
}

bool true_field(std::string_view object, std::string_view key) {
  bool in_string = false;
  bool escaped = false;
  int object_depth = 0;
  int array_depth = 0;
  for (std::size_t index = 0; index < object.size(); ++index) {
    char const ch = object[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      if (!in_string && object_depth == 1 && array_depth == 0) {
        std::size_t end = index + 1;
        bool key_escaped = false;
        while (end < object.size()) {
          char const key_ch = object[end++];
          if (key_escaped) {
            key_escaped = false;
            continue;
          }
          if (key_ch == '\\') {
            key_escaped = true;
            continue;
          }
          if (key_ch == '"') break;
        }
        auto const candidate = object.substr(index + 1, end - index - 2);
        if (candidate == key) {
          auto value = end;
          while (value < object.size() && std::isspace(static_cast<unsigned char>(object[value])) != 0) ++value;
          if (value >= object.size() || object[value] != ':') {
            index = end - 1;
            continue;
          }
          ++value;
          while (value < object.size() && std::isspace(static_cast<unsigned char>(object[value])) != 0) ++value;
          if (object.substr(value, 4) != "true") return false;
          value += 4;
          while (value < object.size() && std::isspace(static_cast<unsigned char>(object[value])) != 0) ++value;
          return value >= object.size() || object[value] == ',' || object[value] == '}';
        }
        index = end - 1;
        continue;
      }
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == '{') ++object_depth;
    if (ch == '}') --object_depth;
    if (ch == '[') ++array_depth;
    if (ch == ']') --array_depth;
  }
  return false;
}

std::string temperature_json(double value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << value;
  return stream.str();
}

std::string sanitized_openai_compatible_snippet(std::string_view body) {
  return sanitized_body_snippet(body, {"access_token", "refresh_token", "api_key", "Authorization", "authorization",
                                       "reasoning_content", "thinking"});
}

ava::core::VoidResult validate_tools_json(ProviderRequest const& request) {
  for (std::size_t index = 0; index < request.tools_json.size(); ++index) {
    if (is_valid_json_object(request.tools_json[index])) continue;
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI-compatible tool JSON must be valid JSON");
    error.with_context("tool_index", std::to_string(index));
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<std::string> chat_completion_tool_json(std::string_view schema) {
  if (!is_valid_json_object(schema)) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI-compatible tool JSON must be valid JSON"));
  }
  if (auto const function = ava::core::json::object_field(schema, "function")) {
    if (!is_valid_json_object(*function)) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "OpenAI-compatible function tool must be valid JSON"));
    }
    auto const name = ava::core::json::string_field(*function, "name");
    if (!name || name->empty()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "OpenAI-compatible tool JSON requires a function name"));
    }
    return std::string(schema);
  }

  auto const name = ava::core::json::string_field(schema, "name");
  if (!name || name->empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "OpenAI-compatible tool JSON requires a function name"));
  }
  auto const description = ava::core::json::string_field(schema, "description").value_or("");
  auto const parameters = ava::core::json::object_field(schema, "parameters").value_or("{\"type\":\"object\"}");
  if (!is_valid_json_object(parameters)) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "OpenAI-compatible tool parameters must be valid JSON"));
  }
  std::string function = "{\"name\":\"" + ava::core::json::escape(*name) + "\",\"description\":\"" +
                         ava::core::json::escape(description) + "\",\"parameters\":" + parameters;
  if (true_field(schema, "strict")) function += ",\"strict\":true";
  function += '}';
  return "{\"type\":\"function\",\"function\":" + function + "}";
}

std::string tool_call_json(ContentPart const& part, std::size_t index) {
  return "{\"id\":\"" +
         ava::core::json::escape(part.tool_call_id.empty() ? "call_" + std::to_string(index) : part.tool_call_id) +
         "\",\"type\":\"function\",\"function\":{\"name\":\"" + ava::core::json::escape(part.tool_name) +
         "\",\"arguments\":\"" + ava::core::json::escape(part.input_json.empty() ? "{}" : part.input_json) + "\"}}";
}

std::string role_content_message_json(std::string_view role, std::string_view content) {
  return "{\"role\":\"" + ava::core::json::escape(role) + "\",\"content\":\"" + ava::core::json::escape(content) +
         "\"}";
}

std::string tool_result_message_json(ContentPart const& part) {
  return "{\"role\":\"tool\",\"tool_call_id\":\"" + ava::core::json::escape(part.tool_call_id) + "\",\"content\":\"" +
         ava::core::json::escape(part.text) + "\"}";
}

std::vector<std::string> chat_messages_for_message(ChatMessage const& message, std::string_view reasoning_format,
                                                   bool preserve_reasoning_content) {
  if (message.content_parts.empty()) return {role_content_message_json(message.role, message.content)};

  std::vector<std::string> result;
  if (message.role == "assistant") {
    std::string text;
    std::string reasoning_content;
    std::vector<std::string> tool_calls;
    for (auto const& part : message.content_parts) {
      if (part.type == ContentPartType::Text) {
        if (!text.empty()) text += "\n\n";
        text += part.text;
      } else if (preserve_reasoning_content && part.type == ContentPartType::Reasoning && !part.redacted &&
                 (part.reasoning_format.empty() || part.reasoning_format == reasoning_format)) {
        if (!reasoning_content.empty()) reasoning_content += "\n\n";
        reasoning_content += part.text;
      } else if (part.type == ContentPartType::ToolUse) {
        tool_calls.push_back(tool_call_json(part, tool_calls.size()));
      }
    }
    if (text.empty()) text = message.content;
    std::string assistant = "{\"role\":\"assistant\",\"content\":\"" + ava::core::json::escape(text) + "\"";
    if (!reasoning_content.empty()) {
      assistant += ",\"reasoning_content\":\"" + ava::core::json::escape(reasoning_content) + "\"";
    }
    if (!tool_calls.empty()) {
      assistant += ",\"tool_calls\":[";
      for (std::size_t index = 0; index < tool_calls.size(); ++index) {
        if (index > 0) assistant += ',';
        assistant += tool_calls[index];
      }
      assistant += ']';
    }
    assistant += '}';
    result.push_back(std::move(assistant));
    return result;
  }

  std::string text;
  bool has_tool_result = false;
  for (auto const& part : message.content_parts) {
    if (part.type == ContentPartType::Text) {
      if (!text.empty()) text += "\n\n";
      text += part.text;
    } else if (part.type == ContentPartType::ToolResult) {
      has_tool_result = true;
      result.push_back(tool_result_message_json(part));
    }
  }
  if (has_tool_result) return result;
  if (result.empty() || !text.empty() || !message.content.empty()) {
    result.insert(result.begin(), role_content_message_json(message.role, text.empty() ? message.content : text));
  }
  return result;
}

std::string reasoning_options_json(ProviderRequest const& request, bool preserve_reasoning_content,
                                   std::string_view reasoning_request_field) {
  if (!request.reasoning || reasoning_request_field.empty()) return {};
  auto const type = request.reasoning->type.empty() ? "enabled" : request.reasoning->type;
  std::string json = ",\"" + ava::core::json::escape(reasoning_request_field) + "\":{\"type\":\"" +
                     ava::core::json::escape(type) + "\"";
  if (request.reasoning && request.reasoning->budget_tokens) {
    json += ",\"budget_tokens\":" + std::to_string(*request.reasoning->budget_tokens);
  }
  if (request.reasoning && !request.reasoning->display.empty()) {
    json += ",\"display\":\"" + ava::core::json::escape(request.reasoning->display) + "\"";
  }
  if (preserve_reasoning_content) json += ",\"keep\":\"all\"";
  json += '}';
  return json;
}

bool has_replayable_reasoning(std::vector<ChatMessage> const& messages, std::string_view reasoning_format) {
  for (auto const& message : messages) {
    if (message.role != "assistant") continue;
    for (auto const& part : message.content_parts) {
      if (part.type == ContentPartType::Reasoning && !part.redacted && !part.text.empty() &&
          (part.reasoning_format.empty() || part.reasoning_format == reasoning_format)) {
        return true;
      }
    }
  }
  return false;
}

std::string request_body_json(ProviderRequest const& request, OpenAICompatibleProviderOptions const& options) {
  std::string body = "{\"model\":\"" + ava::core::json::escape(request.model_id) +
                     "\",\"stream\":" + (request.stream ? "true" : "false") + ",\"messages\":[";
  bool first_message = true;
  auto append_message = [&](std::string const& json) {
    if (!first_message) body += ',';
    first_message = false;
    body += json;
  };
  if (!request.system_prompt.empty()) append_message(role_content_message_json("system", request.system_prompt));
  for (auto const& message : request.messages) {
    for (auto const& json :
         chat_messages_for_message(message, options.reasoning_format, options.preserve_reasoning_content)) {
      append_message(json);
    }
  }
  body += ']';
  if (request.max_output_tokens && *request.max_output_tokens > 0) {
    body += ",\"max_tokens\":";
    body += std::to_string(*request.max_output_tokens);
  }
  if (options.default_temperature) body += ",\"temperature\":" + temperature_json(*options.default_temperature);
  if (request.stream && options.include_stream_usage) body += ",\"stream_options\":{\"include_usage\":true}";
  bool const preserve_replayed_reasoning = options.preserve_reasoning_content && request.reasoning &&
                                           has_replayable_reasoning(request.messages, options.reasoning_format);
  body += reasoning_options_json(request, preserve_replayed_reasoning, options.reasoning_request_field);
  body += ",\"tools\":[";
  for (std::size_t index = 0; index < request.tools_json.size(); ++index) {
    if (index > 0) body += ',';
    auto tool = chat_completion_tool_json(request.tools_json[index]);
    if (tool) body += *tool;
  }
  body += "]}";
  return body;
}

std::vector<StreamEvent> finish_reasoning_if_open(bool& reasoning_open, std::string_view reasoning_format) {
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
                                     std::string_view reasoning_format) {
  auto reasoning_end = finish_reasoning_if_open(reasoning_open, reasoning_format);
  events.insert(events.end(), reasoning_end.begin(), reasoning_end.end());
}

void append_done(std::vector<StreamEvent>& events, std::optional<TokenUsage> usage, std::string stop_reason) {
  events.push_back(StreamEvent{.type = StreamEventType::Done,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::move(usage),
                               .stop_reason = std::move(stop_reason)});
}

void append_tool_call_end_events(std::vector<StreamEvent>& events, std::map<int, std::string>& open_tool_call_ids) {
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

void append_tool_call_delta_events(std::vector<StreamEvent>& events, std::map<int, std::string>& open_tool_call_ids,
                                   std::string_view delta) {
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

void append_choice_delta_events(std::vector<StreamEvent>& events, std::map<int, std::string>& open_tool_call_ids,
                                bool& reasoning_open, std::string& stop_reason, std::string_view choice,
                                std::string_view reasoning_format) {
  if (auto finish_reason = ava::core::json::string_field(choice, "finish_reason")) {
    stop_reason = normalized_finish_reason(*finish_reason);
    if (*finish_reason == "tool_calls" || *finish_reason == "function_call") {
      append_tool_call_end_events(events, open_tool_call_ids);
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
  append_tool_call_delta_events(events, open_tool_call_ids, *delta);
}

void append_event_for_data(std::vector<StreamEvent>& events, std::map<int, std::string>& open_tool_call_ids,
                           bool& reasoning_open, std::optional<TokenUsage>& usage, std::string& stop_reason,
                           bool& saw_data, bool& done_seen, bool& error_seen, std::string_view data,
                           std::string_view reasoning_format) {
  if (data == "[DONE]") {
    done_seen = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_format);
    append_tool_call_end_events(events, open_tool_call_ids);
    append_done(events, std::move(usage), std::move(stop_reason));
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
    append_choice_delta_events(events, open_tool_call_ids, reasoning_open, stop_reason, choice, reasoning_format);
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

void append_events_for_sse_line(std::vector<StreamEvent>& events, std::map<int, std::string>& open_tool_call_ids,
                                bool& reasoning_open, std::optional<TokenUsage>& usage, std::string& stop_reason,
                                bool& saw_data, bool& done_seen, bool& error_seen, std::string& data, std::string line,
                                std::string_view reasoning_format) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!data.empty()) {
      append_event_for_data(events, open_tool_call_ids, reasoning_open, usage, stop_reason, saw_data, done_seen,
                            error_seen, data, reasoning_format);
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

ava::core::Result<std::vector<StreamEvent>> parse_chat_completion_message(std::string_view body,
                                                                          std::string_view reasoning_format) {
  std::vector<StreamEvent> events;
  auto const usage = parse_openai_usage(body);
  std::string stop_reason;
  bool parsed_message = false;
  for (auto const& choice : ava::core::json::objects_in_array_field(body, "choices")) {
    if (auto finish_reason = ava::core::json::string_field(choice, "finish_reason")) {
      stop_reason = normalized_finish_reason(*finish_reason);
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
  append_done(events, usage, std::move(stop_reason));
  return events;
}

}  // namespace

OpenAICompatibleStreamParser::OpenAICompatibleStreamParser(std::string reasoning_format)
    : reasoning_format_(std::move(reasoning_format)) {}

ava::core::Result<std::vector<StreamEvent>> OpenAICompatibleStreamParser::append(std::string_view chunk) {
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true) {
    auto const newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos) break;
    append_events_for_sse_line(events, open_tool_call_ids_, reasoning_open_, usage_, stop_reason_, saw_data_,
                               done_seen_, error_seen_, data_, pending_line_.substr(line_start, newline - line_start),
                               reasoning_format_);
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0) pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  return events;
}

ava::core::Result<std::vector<StreamEvent>> OpenAICompatibleStreamParser::finish() {
  std::vector<StreamEvent> events;
  if (!pending_line_.empty()) {
    append_events_for_sse_line(events, open_tool_call_ids_, reasoning_open_, usage_, stop_reason_, saw_data_,
                               done_seen_, error_seen_, data_, std::move(pending_line_), reasoning_format_);
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty()) {
    append_event_for_data(events, open_tool_call_ids_, reasoning_open_, usage_, stop_reason_, saw_data_, done_seen_,
                          error_seen_, data_, reasoning_format_);
    data_.clear();
  }
  append_tool_call_end_events(events, open_tool_call_ids_);
  append_finish_reasoning_if_open(events, reasoning_open_, reasoning_format_);
  if (saw_data_ && !done_seen_ && !error_seen_) {
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "OpenAI-compatible SSE stream ended before done marker",
                                 .usage = std::nullopt});
  }
  saw_data_ = false;
  done_seen_ = false;
  error_seen_ = false;
  usage_ = std::nullopt;
  stop_reason_.clear();
  open_tool_call_ids_.clear();
  reasoning_open_ = false;
  return events;
}

OpenAICompatibleProvider::OpenAICompatibleProvider(OpenAICompatibleProviderOptions options)
    : options_(std::move(options)) {}

ava::core::Result<HttpRequest> OpenAICompatibleProvider::build_request(ProviderRequest const& request,
                                                                       std::string_view access_token) const {
  if (request.model_id.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model id is required"));
  }
  if (access_token.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                            options_.provider_name + " bearer token is required"));
  }
  if (auto valid_tools = validate_tools_json(request); !valid_tools)
    return std::unexpected(std::move(valid_tools.error()));
  for (auto const& tool : request.tools_json) {
    auto converted = chat_completion_tool_json(tool);
    if (!converted) return std::unexpected(std::move(converted.error()));
  }

  std::map<std::string, std::string> headers{{"Authorization", "Bearer " + std::string(access_token)},
                                             {"Content-Type", "application/json"},
                                             {"Accept", request.stream ? "text/event-stream" : "application/json"}};
  if (!options_.user_agent.empty()) headers["User-Agent"] = options_.user_agent;
  return HttpRequest{.method = "POST",
                     .url = join_url(options_.base_url, options_.chat_completions_path),
                     .headers = std::move(headers),
                     .body = request_body_json(request, options_),
                     .timeout_ms = 60000,
                     .follow_redirects = true,
                     .include_response_headers = false,
                     .resolve_hosts = {}};
}

std::unique_ptr<StreamParser> OpenAICompatibleProvider::create_stream_parser() const {
  return std::make_unique<OpenAICompatibleStreamParser>(options_.reasoning_format);
}

ava::core::Result<std::vector<StreamEvent>> OpenAICompatibleProvider::parse_response(HttpResponse const& response,
                                                                                     bool stream) const {
  if (response.status_code < 200 || response.status_code >= 300) {
    auto const kind = classify_provider_error(response);
    auto error = ava::core::Error(
        ava::core::ErrorCategory::Provider,
        options_.provider_name + " HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(kind));
    if (auto const retry_after = retry_after_header(response)) error.with_context("retry_after", *retry_after);
    if (!response.body.empty()) {
      error.with_context("body_snippet", sanitized_openai_compatible_snippet(response.body));
    }
    return std::unexpected(std::move(error));
  }
  if (stream) return parse_openai_compatible_sse(response.body, options_.reasoning_format);
  return parse_chat_completion_message(response.body, options_.reasoning_format);
}

ava::core::Result<std::vector<StreamEvent>> parse_openai_compatible_sse(std::string_view sse,
                                                                        std::string reasoning_format) {
  OpenAICompatibleStreamParser parser(std::move(reasoning_format));
  auto events = parser.append(sse);
  if (!events) return std::unexpected(std::move(events.error()));
  auto final_events = parser.finish();
  if (!final_events) return std::unexpected(std::move(final_events.error()));
  events->insert(events->end(), final_events->begin(), final_events->end());
  return events;
}

}  // namespace ava::provider
