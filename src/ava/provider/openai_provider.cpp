#include "ava/provider/openai_provider.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <utility>

#include "ava/core/json.h"

namespace ava::provider {
namespace {

constexpr std::string_view kCodexResponsesUrl = "https://chatgpt.com/backend-api/codex/responses";

std::string input_item_json(const ChatMessage& message) {
  return "{\"role\":\"" + ava::core::json::escape(message.role) + "\",\"content\":\"" +
         ava::core::json::escape(message.content) + "\"}";
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

bool is_json_object_shape(std::string_view value) {
  value = trim(value);
  if (value.size() < 2 || value.front() != '{') return false;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  std::size_t end = std::string_view::npos;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char ch = value[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == '{') ++depth;
    if (ch == '}') {
      --depth;
      if (depth == 0) {
        end = index;
        break;
      }
      if (depth < 0) return false;
    }
  }
  if (in_string || depth != 0 || end == std::string_view::npos) return false;
  if (!trim(value.substr(end + 1)).empty()) return false;
  const auto first_member = trim(value.substr(1, end - 1));
  return first_member.empty() || first_member.front() == '"';
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

std::string request_body_json(const ProviderRequest& request) {
  std::string body = "{\"model\":\"" + ava::core::json::escape(request.model_id) + "\",\"instructions\":\"" +
                     ava::core::json::escape(request.system_prompt) +
                     "\",\"stream\":" + (request.stream ? "true" : "false") + ",\"input\":[";
  for (std::size_t index = 0; index < request.messages.size(); ++index) {
    if (index > 0) body += ',';
    body += input_item_json(request.messages[index]);
  }
  body += "],\"tools\":[";
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
    usage.cache_read_tokens = first_integer_field(usage_object, {"cache_read_tokens", "cache_read_input_tokens"});
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

void redact_json_string_value(std::string& snippet, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t position = 0;
  while ((position = snippet.find(needle, position)) != std::string::npos) {
    auto value = position + needle.size();
    while (value < snippet.size() && std::isspace(static_cast<unsigned char>(snippet[value])) != 0) ++value;
    if (value >= snippet.size() || snippet[value] != ':') {
      position += needle.size();
      continue;
    }
    ++value;
    while (value < snippet.size() && std::isspace(static_cast<unsigned char>(snippet[value])) != 0) ++value;
    if (value >= snippet.size() || snippet[value] != '"') {
      position = value;
      continue;
    }
    bool escaped = false;
    bool redacted = false;
    for (std::size_t end = value + 1; end < snippet.size(); ++end) {
      const char ch = snippet[end];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == '"') {
        snippet.replace(value + 1, end - value - 1, "[redacted]");
        position = value + std::string_view("[redacted]").size() + 2;
        redacted = true;
        break;
      }
    }
    if (!redacted) position = value + 1;
  }
}

std::string sanitized_body_snippet(std::string_view body) {
  constexpr std::size_t kMaxSnippet = 256;
  std::string snippet(body.substr(0, std::min(body.size(), kMaxSnippet)));
  for (const std::string_view secret_key : {"access_token", "refresh_token", "api_key", "Authorization"}) {
    redact_json_string_value(snippet, secret_key);
  }
  std::size_t bearer = 0;
  while ((bearer = snippet.find("Bearer ", bearer)) != std::string::npos) {
    auto end = bearer + std::string_view("Bearer ").size();
    while (end < snippet.size() && std::isspace(static_cast<unsigned char>(snippet[end])) == 0 && snippet[end] != '"')
      ++end;
    snippet.replace(bearer, end - bearer, "Bearer [redacted]");
    bearer += std::string_view("Bearer [redacted]").size();
  }
  return snippet;
}

void append_event_for_data(std::vector<StreamEvent>& events, std::string_view data) {
  if (data == "[DONE]") {
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt});
    return;
  }
  if (!is_json_object_shape(data)) {
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "malformed OpenAI SSE event",
                                 .usage = std::nullopt});
    return;
  }
  const auto type = ava::core::json::string_field(data, "type").value_or("");
  if (is_ignored_lifecycle_event(type)) return;
  if (type == "response.output_text.delta" || type == "response.text.delta") {
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
  if (type == "response.completed") {
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = parse_openai_usage(data)});
    return;
  }
  if (type == "response.error" || type == "response.failed") {
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

void append_events_for_sse_line(std::vector<StreamEvent>& events, std::string& data, std::string line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!data.empty()) {
      append_event_for_data(events, data);
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

ava::core::Result<HttpRequest> OpenAIProvider::build_request(const ProviderRequest& request,
                                                             const ava::config::OpenAICredential& credential,
                                                             long long now_seconds) const {
  auto access_token = ava::config::openai_access_token_for_request(credential, now_seconds);
  if (!access_token) return std::unexpected(std::move(access_token.error()));
  auto http_request = build_request(request, *access_token);
  if (!http_request || credential.type != ava::config::OpenAICredentialType::OAuth) return http_request;

  apply_codex_oauth_request_options(*http_request);
  if (!credential.account_id.empty()) {
    http_request->headers["ChatGPT-Account-Id"] = credential.account_id;
    http_request->headers["chatgpt-account-id"] = credential.account_id;
  }
  return http_request;
}

ava::core::Result<HttpRequest> OpenAIProvider::build_request(const ProviderRequest& request,
                                                             const ava::config::OpenAICredential& credential) const {
  auto access_token = ava::config::openai_access_token_for_request(credential);
  if (!access_token) return std::unexpected(std::move(access_token.error()));
  return build_request(request, *access_token);
}

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::append(std::string_view chunk) {
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true) {
    const auto newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos) break;
    append_events_for_sse_line(events, data_, pending_line_.substr(line_start, newline - line_start));
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
    append_events_for_sse_line(events, data_, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty()) {
    append_event_for_data(events, data_);
    data_.clear();
  }
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
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider,
                                  "OpenAI HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    if (!response.body.empty()) error.with_context("body_snippet", sanitized_body_snippet(response.body));
    return std::unexpected(std::move(error));
  }
  return parse_openai_sse(response.body);
}

ava::core::Result<std::string> parse_openai_response_text(std::string_view body) {
  if (auto output = ava::core::json::string_field(body, "output_text")) return *output;
  if (auto text = ava::core::json::string_field(body, "text")) return *text;
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI response text is missing"));
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
