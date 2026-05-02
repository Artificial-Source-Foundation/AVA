#include "ava/provider/anthropic_provider.h"

#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/core/json.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider {
namespace {

constexpr std::string_view kDefaultAnthropicBaseUrl = "https://api.anthropic.com";
constexpr std::string_view kAnthropicVersion = "2023-06-01";
constexpr int kDefaultMaxTokens = 4096;

std::string configured_base_url() {
  if (const char* value = std::getenv("ANTHROPIC_BASE_URL"); value && value[0] != '\0') return value;
  return std::string(kDefaultAnthropicBaseUrl);
}

std::string trim_trailing_slashes(std::string value) {
  while (value.size() > 1 && value.back() == '/') value.pop_back();
  return value;
}

std::string message_role(const ChatMessage& message) { return message.role == "assistant" ? "assistant" : "user"; }

std::string message_json(const ChatMessage& message) {
  const std::string role = message_role(message);
  return "{\"role\":\"" + role + "\",\"content\":\"" + ava::core::json::escape(message.content) + "\"}";
}

std::vector<ChatMessage> collapse_consecutive_roles(const std::vector<ChatMessage>& messages) {
  std::vector<ChatMessage> collapsed;
  for (const auto& message : messages) {
    const std::string role = message_role(message);
    if (!collapsed.empty() && collapsed.back().role == role) {
      collapsed.back().content += "\n\n";
      collapsed.back().content += message.content;
      continue;
    }
    collapsed.push_back(ChatMessage{.role = role, .content = message.content});
  }
  return collapsed;
}

long long max_tokens_for_request(const ProviderRequest& request) {
  const long long value = request.max_output_tokens.value_or(kDefaultMaxTokens);
  return value > 0 ? value : kDefaultMaxTokens;
}

std::string stream_error_message(std::string_view message) {
  return message.empty() ? "unrecognized Anthropic stream event" : std::string(message);
}

void append_stream_error(std::vector<StreamEvent>& events, std::string_view message) {
  events.push_back(StreamEvent{.type = StreamEventType::Error,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = stream_error_message(message),
                               .usage = std::nullopt});
}

std::string anthropic_tool_json(std::string_view tool_json) {
  const auto name = ava::core::json::string_field(tool_json, "name").value_or("");
  const auto description = ava::core::json::string_field(tool_json, "description").value_or("");
  const auto parameters = ava::core::json::object_field(tool_json, "parameters").value_or("{\"type\":\"object\"}");
  return "{\"name\":\"" + ava::core::json::escape(name) + "\",\"description\":\"" +
         ava::core::json::escape(description) + "\",\"input_schema\":" + parameters + "}";
}

std::string request_body_json(const ProviderRequest& request) {
  const auto messages = collapse_consecutive_roles(request.messages);
  std::string body = "{\"model\":\"" + ava::core::json::escape(request.model_id) +
                     "\",\"max_tokens\":" + std::to_string(max_tokens_for_request(request)) +
                     ",\"stream\":" + (request.stream ? "true" : "false") + ",\"system\":\"" +
                     ava::core::json::escape(request.system_prompt) + "\",\"messages\":[";
  for (std::size_t index = 0; index < messages.size(); ++index) {
    if (index > 0) body += ',';
    body += message_json(messages[index]);
  }
  body += ']';
  if (!request.tools_json.empty()) {
    body += ",\"tools\":[";
    for (std::size_t index = 0; index < request.tools_json.size(); ++index) {
      if (index > 0) body += ',';
      body += anthropic_tool_json(request.tools_json[index]);
    }
    body += ']';
  }
  body += '}';
  return body;
}

std::optional<long long> non_negative_integer_field(std::string_view object, std::string_view key) {
  const auto value = ava::core::json::integer_field(object, key);
  if (!value || *value < 0) return std::nullopt;
  return value;
}

void merge_usage(TokenUsage& target, const TokenUsage& source) {
  if (source.input_tokens) target.input_tokens = source.input_tokens;
  if (source.output_tokens) target.output_tokens = source.output_tokens;
  if (source.cache_read_tokens) target.cache_read_tokens = source.cache_read_tokens;
  if (source.cache_write_tokens) target.cache_write_tokens = source.cache_write_tokens;
  if (source.total_tokens) target.total_tokens = source.total_tokens;
}

void append_event_for_data(std::vector<StreamEvent>& events,
                           std::map<long long, AnthropicStreamParser::ToolBlock>& tools,
                           std::optional<TokenUsage>& usage, std::string_view data) {
  if (!is_json_object_shape(data)) {
    append_stream_error(events, "malformed Anthropic stream event");
    return;
  }
  const auto type = ava::core::json::string_field(data, "type").value_or("");
  if (type == "ping" || type == "message_start") {
    if (const auto message = ava::core::json::object_field(data, "message")) {
      if (const auto parsed = parse_anthropic_usage(*message)) {
        if (!usage) usage = TokenUsage{};
        merge_usage(*usage, *parsed);
      }
    }
    return;
  }
  if (type == "content_block_start") {
    const auto block = ava::core::json::object_field(data, "content_block");
    const auto index = non_negative_integer_field(data, "index");
    if (block && index && ava::core::json::string_field(*block, "type").value_or("") == "tool_use") {
      const auto id = ava::core::json::string_field(*block, "id").value_or("");
      const auto name = ava::core::json::string_field(*block, "name").value_or("");
      tools[*index] = AnthropicStreamParser::ToolBlock{.id = id, .name = name};
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallStart,
                                   .text = "",
                                   .tool_call_id = id,
                                   .tool_name = name,
                                   .error_message = "",
                                   .usage = std::nullopt});
    }
    return;
  }
  if (type == "content_block_delta") {
    const auto delta = ava::core::json::object_field(data, "delta");
    if (!delta) {
      append_stream_error(events, "Anthropic content_block_delta is missing delta");
      return;
    }
    const auto delta_type = ava::core::json::string_field(*delta, "type").value_or("");
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
      const auto index = non_negative_integer_field(data, "index");
      const auto tool = index ? tools.find(*index) : tools.end();
      events.push_back(StreamEvent{.type = StreamEventType::ToolCallDelta,
                                   .text = ava::core::json::string_field(*delta, "partial_json").value_or(""),
                                   .tool_call_id = tool == tools.end() ? "" : tool->second.id,
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
      return;
    }
    append_stream_error(events, "unrecognized Anthropic content_block_delta");
    return;
  }
  if (type == "content_block_stop") {
    const auto index = non_negative_integer_field(data, "index");
    if (!index) return;
    const auto tool = tools.find(*index);
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
    if (const auto parsed = parse_anthropic_usage(data)) {
      if (!usage) usage = TokenUsage{};
      merge_usage(*usage, *parsed);
    }
    return;
  }
  if (type == "message_stop") {
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::move(usage)});
    usage = std::nullopt;
    return;
  }
  if (type == "error") {
    const auto error = ava::core::json::object_field(data, "error");
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = error ? ava::core::json::string_field(*error, "message").value_or("")
                                                        : ava::core::json::string_field(data, "message").value_or(""),
                                 .usage = std::nullopt});
    return;
  }
  append_stream_error(events, "unrecognized Anthropic stream event");
}

void append_events_for_sse_line(std::vector<StreamEvent>& events,
                                std::map<long long, AnthropicStreamParser::ToolBlock>& tools,
                                std::optional<TokenUsage>& usage, std::string& data, std::string line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) {
    if (!data.empty()) {
      append_event_for_data(events, tools, usage, data);
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

AnthropicProvider::AnthropicProvider(std::string base_url)
    : base_url_(trim_trailing_slashes(base_url.empty() ? configured_base_url() : std::move(base_url))) {}

ava::core::Result<HttpRequest> AnthropicProvider::build_request(const ProviderRequest& request,
                                                                std::string_view access_token) const {
  if (request.model_id.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model id is required"));
  }
  if (access_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Anthropic API key is required"));
  }
  return HttpRequest{.method = "POST",
                     .url = base_url_ + "/v1/messages",
                     .headers = {{"x-api-key", std::string(access_token)},
                                 {"anthropic-version", std::string(kAnthropicVersion)},
                                 {"Content-Type", "application/json"},
                                 {"Accept", request.stream ? "text/event-stream" : "application/json"}},
                     .body = request_body_json(request),
                     .timeout_ms = 60000,
                     .follow_redirects = false,
                     .include_response_headers = false,
                     .resolve_hosts = {}};
}

std::unique_ptr<StreamParser> AnthropicProvider::create_stream_parser() const {
  return std::make_unique<AnthropicStreamParser>();
}

ava::core::VoidResult AnthropicProvider::apply_auth_options(HttpRequest& request,
                                                            const ProviderAuthContext& auth) const {
  if (auth.credential_type != "oauth") return {};
  request.headers.erase("x-api-key");
  request.headers["Authorization"] = "Bearer " + auth.access_token;
  request.headers["user-agent"] = "ava";
  request.headers["x-app"] = "cli";
  return {};
}

ava::core::Result<std::vector<StreamEvent>> AnthropicProvider::parse_response(const HttpResponse& response,
                                                                              bool stream) const {
  return stream ? parse_anthropic_sse_response(response) : parse_anthropic_response(response);
}

ava::core::Result<std::vector<StreamEvent>> AnthropicStreamParser::append(std::string_view chunk) {
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true) {
    const auto newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos) break;
    append_events_for_sse_line(events, tool_blocks_, usage_, data_,
                               pending_line_.substr(line_start, newline - line_start));
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0) pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  return events;
}

ava::core::Result<std::vector<StreamEvent>> AnthropicStreamParser::finish() {
  std::vector<StreamEvent> events;
  if (!pending_line_.empty()) {
    append_events_for_sse_line(events, tool_blocks_, usage_, data_, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty()) {
    append_event_for_data(events, tool_blocks_, usage_, data_);
    data_.clear();
  }
  tool_blocks_.clear();
  usage_ = std::nullopt;
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_anthropic_sse(std::string_view sse) {
  AnthropicStreamParser parser;
  auto events = parser.append(sse);
  if (!events) return std::unexpected(std::move(events.error()));
  auto final_events = parser.finish();
  if (!final_events) return std::unexpected(std::move(final_events.error()));
  events->insert(events->end(), final_events->begin(), final_events->end());
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_anthropic_sse_response(const HttpResponse& response) {
  if (response.status_code < 200 || response.status_code >= 300) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider,
                                  "Anthropic HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(classify_provider_error(response)));
    if (const auto retry_after = retry_after_header(response)) error.with_context("retry_after", *retry_after);
    if (!response.body.empty()) {
      error.with_context("body_snippet",
                         sanitized_body_snippet(response.body, {"access_token", "refresh_token", "api_key", "x-api-key",
                                                                "Authorization"}));
    }
    return std::unexpected(std::move(error));
  }
  return parse_anthropic_sse(response.body);
}

ava::core::Result<std::vector<StreamEvent>> parse_anthropic_response(const HttpResponse& response) {
  if (response.status_code < 200 || response.status_code >= 300) return parse_anthropic_sse_response(response);
  std::vector<StreamEvent> events;
  bool parsed_content = false;
  for (const auto& block : ava::core::json::objects_in_array_field(response.body, "content")) {
    const auto type = ava::core::json::string_field(block, "type").value_or("");
    if (type == "text") {
      parsed_content = true;
      events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                   .text = ava::core::json::string_field(block, "text").value_or(""),
                                   .tool_call_id = "",
                                   .tool_name = "",
                                   .error_message = "",
                                   .usage = std::nullopt});
    } else if (type == "tool_use") {
      parsed_content = true;
      const auto id = ava::core::json::string_field(block, "id").value_or("");
      const auto name = ava::core::json::string_field(block, "name").value_or("");
      const auto input = ava::core::json::object_field(block, "input").value_or("{}");
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
  if (!parsed_content) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "Anthropic response content is missing");
    if (!response.body.empty()) {
      error.with_context("body_snippet",
                         sanitized_body_snippet(response.body, {"access_token", "refresh_token", "api_key", "x-api-key",
                                                                "Authorization"}));
    }
    return std::unexpected(std::move(error));
  }
  events.push_back(StreamEvent{.type = StreamEventType::Done,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = parse_anthropic_usage(response.body)});
  return events;
}

std::optional<TokenUsage> parse_anthropic_usage(std::string_view body) {
  const auto usage_object = ava::core::json::object_field(body, "usage");
  const auto usage_view = usage_object ? std::string_view(*usage_object) : body;
  TokenUsage usage;
  const auto regular_input_tokens = non_negative_integer_field(usage_view, "input_tokens");
  usage.output_tokens = non_negative_integer_field(usage_view, "output_tokens");
  usage.cache_read_tokens = non_negative_integer_field(usage_view, "cache_read_input_tokens");
  usage.cache_write_tokens = non_negative_integer_field(usage_view, "cache_creation_input_tokens");
  const long long input_total =
      regular_input_tokens.value_or(0) + usage.cache_read_tokens.value_or(0) + usage.cache_write_tokens.value_or(0);
  if (input_total > 0) usage.input_tokens = input_total;
  const long long total = input_total + usage.output_tokens.value_or(0);
  if (total > 0) usage.total_tokens = total;
  if (!usage.input_tokens && !usage.output_tokens && !usage.cache_read_tokens && !usage.cache_write_tokens) {
    return std::nullopt;
  }
  return usage;
}

}  // namespace ava::provider
