#include "ava/provider/openai_provider.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

#include "ava/core/json.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider {
namespace {

constexpr std::string_view kCodexResponsesUrl = "https://chatgpt.com/backend-api/codex/responses";
constexpr std::string_view kOpenAIResponsesReasoningFormat = "openai_responses";

std::string input_item_json(const ChatMessage& message) {
  return "{\"role\":\"" + ava::core::json::escape(message.role) + "\",\"content\":\"" +
         ava::core::json::escape(message.content) + "\"}";
}

ava::core::VoidResult validate_tools_json(const ProviderRequest& request) {
  for (std::size_t index = 0; index < request.tools_json.size(); ++index) {
    if (is_json_object_shape(request.tools_json[index])) continue;
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI tool JSON must be an object");
    error.with_context("tool_index", std::to_string(index));
    return std::unexpected(std::move(error));
  }
  return {};
}

bool valid_openai_reasoning_effort(std::string_view effort) {
  return effort == "none" || effort == "minimal" || effort == "low" || effort == "medium" || effort == "high" ||
         effort == "xhigh";
}

ava::core::VoidResult validate_reasoning_options(const ProviderRequest& request) {
  if (!request.reasoning) return {};
  if (!valid_openai_reasoning_effort(request.reasoning->type)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "OpenAI reasoning effort is not supported by this model");
    error.with_context("effort", request.reasoning->type);
    return std::unexpected(std::move(error));
  }
  if (request.reasoning->budget_tokens || !request.reasoning->display.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI Responses reasoning supports effort only"));
  }
  return {};
}

std::string reasoning_options_json(const ProviderRequest& request) {
  if (!request.reasoning) return {};
  std::string json = ",\"reasoning\":{\"effort\":\"" + ava::core::json::escape(request.reasoning->type) + "\"";
  if (request.reasoning->type != "none") {
    json += ",\"summary\":\"auto\"";
  }
  json += '}';
  return json;
}

std::string request_body_json(const ProviderRequest& request) {
  std::string body = "{\"model\":\"" + ava::core::json::escape(request.model_id) + "\",\"instructions\":\"" +
                     ava::core::json::escape(request.system_prompt) +
                     "\",\"stream\":" + (request.stream ? "true" : "false") + ",\"input\":[";
  for (std::size_t index = 0; index < request.messages.size(); ++index) {
    if (index > 0) body += ',';
    body += input_item_json(request.messages[index]);
  }
  body += ']';
  if (request.max_output_tokens && *request.max_output_tokens > 0) {
    body += ",\"max_output_tokens\":";
    body += std::to_string(*request.max_output_tokens);
  }
  body += reasoning_options_json(request);
  body += ",\"tools\":[";
  for (std::size_t index = 0; index < request.tools_json.size(); ++index) {
    if (index > 0) body += ',';
    body += request.tools_json[index];
  }
  body += "]}";
  return body;
}

void apply_codex_oauth_request_options(HttpRequest& request) {
  request.url = std::string(kCodexResponsesUrl);
  request.headers["OpenAI-Beta"] = "responses=experimental";
  request.headers["originator"] = "ava";
  if (!request.body.empty() && request.body.back() == '}') {
    request.body.insert(request.body.size() - 1, ",\"store\":false");
  }
}

bool is_ignored_lifecycle_event(std::string_view type) {
  return type == "response.created" || type == "response.in_progress" || type == "response.output_item.added" ||
         type == "response.output_item.done" || type == "response.content_part.added" ||
         type == "response.content_part.done" || type == "response.output_text.done" ||
         type == "response.function_call_arguments.done";
}

std::string normalized_openai_stop_reason(std::string_view reason) {
  if (reason == "completed") return "completed";
  if (reason == "incomplete") return "incomplete";
  if (reason == "max_output_tokens" || reason == "max_tokens") return "max_tokens";
  if (reason == "content_filter") return "content_filter";
  if (reason == "refusal") return "refusal";
  if (reason == "failed" || reason == "cancelled" || reason == "canceled") return std::string(reason);
  return std::string(reason);
}

std::string openai_response_stop_reason(std::string_view object) {
  std::string status = ava::core::json::string_field(object, "status").value_or("");
  if (const auto response = ava::core::json::object_field(object, "response")) {
    if (status.empty()) status = ava::core::json::string_field(*response, "status").value_or("");
    if (const auto details = ava::core::json::object_field(*response, "incomplete_details")) {
      if (auto reason = ava::core::json::string_field(*details, "reason"))
        return normalized_openai_stop_reason(*reason);
    }
  }
  if (const auto details = ava::core::json::object_field(object, "incomplete_details")) {
    if (auto reason = ava::core::json::string_field(*details, "reason")) return normalized_openai_stop_reason(*reason);
  }
  return normalized_openai_stop_reason(status);
}

std::optional<long long> non_negative_integer_field(std::string_view object, std::string_view key) {
  const auto value = ava::core::json::integer_field(object, key);
  if (!value || *value < 0) return std::nullopt;
  return value;
}

std::optional<long long> first_integer_field(std::string_view object, std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    if (const auto value = non_negative_integer_field(object, key)) return value;
  }
  return std::nullopt;
}

std::optional<std::string> first_string_field(std::string_view object, std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    if (auto value = ava::core::json::string_field(object, key)) return value;
  }
  return std::nullopt;
}

void append_joined_text(std::string& output, std::string_view text) {
  if (text.empty()) return;
  if (!output.empty()) output += "\n\n";
  output += text;
}

std::string reasoning_summary_text_from_object(std::string_view object) {
  std::string text;
  if (auto summary_text = ava::core::json::string_field(object, "summary_text")) {
    append_joined_text(text, *summary_text);
  }
  if (auto direct_text = ava::core::json::string_field(object, "text")) {
    append_joined_text(text, *direct_text);
  }
  for (const auto& summary : ava::core::json::objects_in_array_field(object, "summary")) {
    if (auto summary_text = ava::core::json::string_field(summary, "text")) {
      append_joined_text(text, *summary_text);
    } else if (auto nested_summary = ava::core::json::string_field(summary, "summary_text")) {
      append_joined_text(text, *nested_summary);
    }
  }
  if (const auto part = ava::core::json::object_field(object, "part")) {
    if (auto part_text = ava::core::json::string_field(*part, "text")) append_joined_text(text, *part_text);
  }
  if (const auto item = ava::core::json::object_field(object, "item")) {
    append_joined_text(text, reasoning_summary_text_from_object(*item));
  }
  return text;
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
                               .reasoning_format = std::string(kOpenAIResponsesReasoningFormat)});
}

void append_reasoning_delta(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen,
                            std::string_view text) {
  append_start_reasoning_if_needed(events, reasoning_open);
  if (text.empty()) return;
  reasoning_text_seen = true;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                               .text = std::string(text),
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(kOpenAIResponsesReasoningFormat)});
}

void append_finish_reasoning_if_open(std::vector<StreamEvent>& events, bool& reasoning_open,
                                     bool& reasoning_text_seen) {
  if (!reasoning_open) return;
  reasoning_open = false;
  reasoning_text_seen = false;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(kOpenAIResponsesReasoningFormat)});
}

std::optional<TokenUsage> usage_from_object(std::string_view usage_object) {
  TokenUsage usage;
  usage.input_tokens = first_integer_field(usage_object, {"input_tokens", "prompt_tokens"});
  usage.output_tokens = first_integer_field(usage_object, {"output_tokens", "completion_tokens"});
  usage.total_tokens = first_integer_field(usage_object, {"total_tokens"});

  if (const auto output_details = ava::core::json::object_field(usage_object, "output_tokens_details")) {
    usage.reasoning_tokens = first_integer_field(*output_details, {"reasoning_tokens"});
  }
  if (!usage.reasoning_tokens) {
    if (const auto completion_details = ava::core::json::object_field(usage_object, "completion_tokens_details")) {
      usage.reasoning_tokens = first_integer_field(*completion_details, {"reasoning_tokens"});
    }
  }
  if (!usage.reasoning_tokens) usage.reasoning_tokens = first_integer_field(usage_object, {"reasoning_tokens"});

  if (const auto input_details = ava::core::json::object_field(usage_object, "input_tokens_details")) {
    usage.cache_read_tokens = first_integer_field(*input_details, {"cached_tokens", "cache_read_tokens"});
    usage.cache_write_tokens = first_integer_field(*input_details, {"cache_creation_tokens", "cache_write_tokens"});
  }
  if (!usage.cache_read_tokens || !usage.cache_write_tokens) {
    if (const auto prompt_details = ava::core::json::object_field(usage_object, "prompt_tokens_details")) {
      if (!usage.cache_read_tokens) {
        usage.cache_read_tokens = first_integer_field(*prompt_details, {"cached_tokens", "cache_read_tokens"});
      }
      if (!usage.cache_write_tokens) {
        usage.cache_write_tokens =
            first_integer_field(*prompt_details, {"cache_creation_tokens", "cache_write_tokens"});
      }
    }
  }
  if (!usage.cache_read_tokens) {
    usage.cache_read_tokens =
        first_integer_field(usage_object, {"cached_tokens", "cache_read_tokens", "cache_read_input_tokens"});
  }
  if (!usage.cache_write_tokens) {
    usage.cache_write_tokens = first_integer_field(usage_object, {"cache_write_tokens", "cache_creation_input_tokens"});
  }

  if (!usage.input_tokens && !usage.output_tokens && !usage.reasoning_tokens && !usage.cache_read_tokens &&
      !usage.cache_write_tokens && !usage.total_tokens) {
    return std::nullopt;
  }
  return usage;
}

void append_event_for_data(std::vector<StreamEvent>& events, std::string_view data, bool& saw_content,
                           bool& reasoning_open, bool& reasoning_text_seen, bool& done_seen, bool& error_seen) {
  if (data == "[DONE]") {
    if (done_seen) return;
    done_seen = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
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
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
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
      saw_content = true;
      append_start_reasoning_if_needed(events, reasoning_open);
    } else if (item_type == "function_call") {
      saw_content = true;
      append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
      events.push_back(StreamEvent{
          .type = StreamEventType::ToolCallStart,
          .text = "",
          .tool_call_id = first_string_field(*item, {"id", "item_id", "call_id"})
                              .or_else([&data]() { return first_string_field(data, {"item_id", "call_id"}); })
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
      saw_content = true;
      if (!reasoning_text_seen) {
        append_reasoning_delta(events, reasoning_open, reasoning_text_seen, reasoning_summary_text_from_object(*item));
      }
      append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
    }
    return;
  }
  if (type == "response.reasoning_summary_part.added") {
    saw_content = true;
    append_start_reasoning_if_needed(events, reasoning_open);
    return;
  }
  if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta") {
    saw_content = true;
    append_reasoning_delta(events, reasoning_open, reasoning_text_seen,
                           first_string_field(data, {"delta", "text"}).value_or(""));
    return;
  }
  if (type == "response.reasoning_summary_text.done" || type == "response.reasoning_summary_part.done" ||
      type == "response.reasoning_text.done") {
    saw_content = true;
    if (!reasoning_text_seen) {
      append_reasoning_delta(events, reasoning_open, reasoning_text_seen, reasoning_summary_text_from_object(data));
    }
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
    return;
  }
  if (is_ignored_lifecycle_event(type)) return;
  if (type == "response.output_text.delta" || type == "response.text.delta") {
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
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
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
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
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
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
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
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
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = parse_openai_usage(data),
                                 .stop_reason = openai_response_stop_reason(data)});
    return;
  }
  if (type == "response.error" || type == "response.failed") {
    error_seen = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen);
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
                                bool& reasoning_open, bool& reasoning_text_seen, bool& done_seen, bool& error_seen,
                                std::string line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!data.empty()) {
      append_event_for_data(events, data, saw_content, reasoning_open, reasoning_text_seen, done_seen, error_seen);
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

ava::core::Result<std::vector<StreamEvent>> parse_openai_non_stream_response(std::string_view body);

OpenAIProvider::OpenAIProvider(std::string base_url) : base_url_(std::move(base_url)) {}

ava::core::Result<HttpRequest> OpenAIProvider::build_request(const ProviderRequest& request,
                                                             std::string_view access_token) const {
  if (request.model_id.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model id is required"));
  }
  if (access_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "OpenAI bearer token is required"));
  }
  if (auto valid_tools = validate_tools_json(request); !valid_tools) {
    return std::unexpected(std::move(valid_tools.error()));
  }
  if (auto valid_reasoning = validate_reasoning_options(request); !valid_reasoning) {
    return std::unexpected(std::move(valid_reasoning.error()));
  }

  return HttpRequest{
      .method = "POST",
      .url = base_url_ + "/v1/responses",
      .headers = {{"Authorization", "Bearer " + std::string(access_token)},
                  {"Content-Type", "application/json"},
                  {"Accept", "text/event-stream"}},
      .body = request_body_json(request),
      .timeout_ms = 60000,
      .follow_redirects = true,
      .include_response_headers = false,
      .resolve_hosts = {},
  };
}

ava::core::VoidResult OpenAIProvider::apply_auth_options(HttpRequest& request, const ProviderAuthContext& auth) const {
  if (auth.credential_type != "oauth") return {};
  apply_codex_oauth_request_options(request);
  if (!auth.account_id.empty()) {
    request.headers["ChatGPT-Account-Id"] = auth.account_id;
    request.headers["chatgpt-account-id"] = auth.account_id;
  }
  return {};
}

std::unique_ptr<StreamParser> OpenAIProvider::create_stream_parser() const {
  return std::make_unique<OpenAIStreamParser>();
}

ava::core::Result<std::vector<StreamEvent>> OpenAIProvider::parse_response(const HttpResponse& response,
                                                                           bool stream) const {
  if (stream) return parse_openai_sse_response(response);
  if (response.status_code < 200 || response.status_code >= 300) return parse_openai_sse_response(response);
  return parse_openai_non_stream_response(response.body);
}

ava::core::Result<HttpRequest> OpenAIProvider::build_request(const ProviderRequest& request,
                                                             const ava::config::OpenAICredential& credential,
                                                             long long now_seconds) const {
  auto access_token = ava::config::openai_access_token_for_request(credential, now_seconds);
  if (!access_token) return std::unexpected(std::move(access_token.error()));
  auto http_request = build_request(request, *access_token);
  if (!http_request || credential.type != ava::config::OpenAICredentialType::OAuth) return http_request;

  if (auto applied = apply_auth_options(*http_request, ProviderAuthContext{.access_token = *access_token,
                                                                           .credential_type = "oauth",
                                                                           .account_id = credential.account_id});
      !applied) {
    return std::unexpected(std::move(applied.error()));
  }
  return http_request;
}

ava::core::Result<HttpRequest> OpenAIProvider::build_request(const ProviderRequest& request,
                                                             const ava::config::OpenAICredential& credential) const {
  auto access_token = ava::config::openai_access_token_for_request(credential);
  if (!access_token) return std::unexpected(std::move(access_token.error()));
  auto http_request = build_request(request, *access_token);
  if (!http_request || credential.type != ava::config::OpenAICredentialType::OAuth) return http_request;

  if (auto applied = apply_auth_options(*http_request, ProviderAuthContext{.access_token = *access_token,
                                                                           .credential_type = "oauth",
                                                                           .account_id = credential.account_id});
      !applied) {
    return std::unexpected(std::move(applied.error()));
  }
  return http_request;
}

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::append(std::string_view chunk) {
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true) {
    const auto newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos) break;
    append_events_for_sse_line(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_, done_seen_,
                               error_seen_, pending_line_.substr(line_start, newline - line_start));
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
    append_events_for_sse_line(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_, done_seen_,
                               error_seen_, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty()) {
    append_event_for_data(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_, done_seen_, error_seen_);
    data_.clear();
  }
  append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_);
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
  done_seen_ = false;
  error_seen_ = false;
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_openai_sse(std::string_view sse) {
  OpenAIStreamParser parser;
  auto events = parser.append(sse);
  if (!events) return std::unexpected(std::move(events.error()));
  auto final_events = parser.finish();
  if (!final_events) return std::unexpected(std::move(final_events.error()));
  events->insert(events->end(), final_events->begin(), final_events->end());
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_openai_sse_response(const HttpResponse& response) {
  if (response.status_code < 200 || response.status_code >= 300) {
    const auto kind = classify_provider_error(response);
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider,
                                  "OpenAI HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(kind));
    if (const auto retry_after = retry_after_header(response)) error.with_context("retry_after", *retry_after);
    if (!response.body.empty()) {
      error.with_context("body_snippet", sanitized_body_snippet(response.body, {"access_token", "refresh_token",
                                                                                "api_key", "Authorization"}));
    }
    return std::unexpected(std::move(error));
  }
  return parse_openai_sse(response.body);
}

ava::core::Result<std::string> parse_openai_response_text(std::string_view body) {
  if (auto output = ava::core::json::string_field(body, "output_text")) return *output;
  if (auto text = ava::core::json::string_field(body, "text")) return *text;
  for (const auto& item : ava::core::json::objects_in_array_field(body, "output")) {
    if (ava::core::json::string_field(item, "type").value_or("") != "message") continue;
    for (const auto& content : ava::core::json::objects_in_array_field(item, "content")) {
      if (auto text = ava::core::json::string_field(content, "text")) return *text;
    }
  }
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI response text is missing"));
}

ava::core::Result<std::vector<StreamEvent>> parse_openai_non_stream_response(std::string_view body) {
  std::vector<StreamEvent> events;
  const auto stop_reason = openai_response_stop_reason(body);
  for (const auto& item : ava::core::json::objects_in_array_field(body, "output")) {
    if (ava::core::json::string_field(item, "type").value_or("") != "reasoning") continue;
    const auto summary = reasoning_summary_text_from_object(item);
    if (summary.empty()) continue;
    events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt,
                                 .reasoning_format = std::string(kOpenAIResponsesReasoningFormat)});
    events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                                 .text = summary,
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt,
                                 .reasoning_format = std::string(kOpenAIResponsesReasoningFormat)});
    events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt,
                                 .reasoning_format = std::string(kOpenAIResponsesReasoningFormat)});
  }
  if (auto text = parse_openai_response_text(body)) {
    events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                 .text = *text,
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt});
  }
  for (const auto& item : ava::core::json::objects_in_array_field(body, "output")) {
    if (ava::core::json::string_field(item, "type").value_or("") != "function_call") continue;
    const auto id = first_string_field(item, {"id", "item_id", "call_id"}).value_or("");
    const auto name = ava::core::json::string_field(item, "name").value_or("");
    const auto arguments = ava::core::json::string_field(item, "arguments").value_or("");
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
  const bool allows_empty_terminal = stop_reason == "incomplete" || stop_reason == "max_tokens" ||
                                     stop_reason == "content_filter" || stop_reason == "refusal";
  if (events.empty() && !allows_empty_terminal) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI response text is missing"));
  }
  events.push_back(StreamEvent{.type = StreamEventType::Done,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = parse_openai_usage(body),
                               .stop_reason = stop_reason});
  return events;
}

std::optional<TokenUsage> parse_openai_usage(std::string_view body) {
  if (const auto usage = ava::core::json::object_field(body, "usage")) return usage_from_object(*usage);
  if (const auto response = ava::core::json::object_field(body, "response")) {
    if (const auto usage = ava::core::json::object_field(*response, "usage")) return usage_from_object(*usage);
  }
  return usage_from_object(body);
}

bool is_retryable_status(int status_code) noexcept {
  return status_code == 408 || status_code == 409 || status_code == 429 || (status_code >= 500 && status_code < 600);
}

bool is_auth_status(int status_code) noexcept { return status_code == 401 || status_code == 403; }

}  // namespace ava::provider
