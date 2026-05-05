#include "ava/provider/openai_response_parser.h"

#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/provider_utils.h"

#include "ava/core/json.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::provider {
namespace {

std::string normalized_openai_stop_reason(std::string_view reason)
{
  if (reason == "completed") return "completed";
  if (reason == "incomplete") return "incomplete";
  if (reason == "max_output_tokens" || reason == "max_tokens") return "max_tokens";
  if (reason == "content_filter") return "content_filter";
  if (reason == "refusal") return "refusal";
  if (reason == "failed" || reason == "cancelled" || reason == "canceled") return std::string(reason);
  return std::string(reason);
}

std::optional<long long> non_negative_integer_field(std::string_view object, std::string_view key)
{
  auto const value = ava::core::json::integer_field(object, key);
  if (!value || *value < 0) return std::nullopt;
  return value;
}

std::optional<long long> first_integer_field(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys) {
    if (auto const value = non_negative_integer_field(object, key)) return value;
  }
  return std::nullopt;
}

void append_joined_text(std::string& output, std::string_view text)
{
  if (text.empty()) return;
  if (!output.empty()) output += "\n\n";
  output += text;
}

std::optional<TokenUsage> usage_from_object(std::string_view usage_object)
{
  TokenUsage usage;
  usage.input_tokens = first_integer_field(usage_object, {"input_tokens", "prompt_tokens"});
  usage.output_tokens = first_integer_field(usage_object, {"output_tokens", "completion_tokens"});
  usage.total_tokens = first_integer_field(usage_object, {"total_tokens"});

  if (auto const output_details = ava::core::json::object_field(usage_object, "output_tokens_details")) {
    usage.reasoning_tokens = first_integer_field(*output_details, {"reasoning_tokens"});
  }
  if (!usage.reasoning_tokens) {
    if (auto const completion_details = ava::core::json::object_field(usage_object, "completion_tokens_details")) {
      usage.reasoning_tokens = first_integer_field(*completion_details, {"reasoning_tokens"});
    }
  }
  if (!usage.reasoning_tokens) usage.reasoning_tokens = first_integer_field(usage_object, {"reasoning_tokens"});

  if (auto const input_details = ava::core::json::object_field(usage_object, "input_tokens_details")) {
    usage.cache_read_tokens = first_integer_field(*input_details, {"cached_tokens", "cache_read_tokens"});
    usage.cache_write_tokens = first_integer_field(*input_details, {"cache_creation_tokens", "cache_write_tokens"});
  }
  if (!usage.cache_read_tokens || !usage.cache_write_tokens) {
    if (auto const prompt_details = ava::core::json::object_field(usage_object, "prompt_tokens_details")) {
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

}  // namespace

namespace detail {

std::optional<std::string> first_string_field(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys) {
    if (auto value = ava::core::json::string_field(object, key)) return value;
  }
  return std::nullopt;
}

std::string openai_response_stop_reason(std::string_view object)
{
  std::string status = ava::core::json::string_field(object, "status").value_or("");
  if (auto const response = ava::core::json::object_field(object, "response")) {
    if (status.empty()) status = ava::core::json::string_field(*response, "status").value_or("");
    if (auto const details = ava::core::json::object_field(*response, "incomplete_details")) {
      if (auto reason = ava::core::json::string_field(*details, "reason"))
        return normalized_openai_stop_reason(*reason);
    }
  }
  if (auto const details = ava::core::json::object_field(object, "incomplete_details")) {
    if (auto reason = ava::core::json::string_field(*details, "reason")) return normalized_openai_stop_reason(*reason);
  }
  return normalized_openai_stop_reason(status);
}

std::string reasoning_summary_text_from_object(std::string_view object)
{
  std::string text;
  if (auto summary_text = ava::core::json::string_field(object, "summary_text")) {
    append_joined_text(text, *summary_text);
  }
  if (auto direct_text = ava::core::json::string_field(object, "text")) {
    append_joined_text(text, *direct_text);
  }
  for (auto const& summary : ava::core::json::objects_in_array_field(object, "summary")) {
    if (auto summary_text = ava::core::json::string_field(summary, "text")) {
      append_joined_text(text, *summary_text);
    } else if (auto nested_summary = ava::core::json::string_field(summary, "summary_text")) {
      append_joined_text(text, *nested_summary);
    }
  }
  if (auto const part = ava::core::json::object_field(object, "part")) {
    if (auto part_text = ava::core::json::string_field(*part, "text")) append_joined_text(text, *part_text);
  }
  if (auto const item = ava::core::json::object_field(object, "item")) {
    append_joined_text(text, reasoning_summary_text_from_object(*item));
  }
  return text;
}

}  // namespace detail

ava::core::Result<std::vector<StreamEvent>> parse_openai_sse(std::string_view sse)
{
  OpenAIStreamParser parser;
  auto events = parser.append(sse);
  if (!events) return std::unexpected(std::move(events.error()));
  auto final_events = parser.finish();
  if (!final_events) return std::unexpected(std::move(final_events.error()));
  events->insert(events->end(), final_events->begin(), final_events->end());
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_openai_sse_response(HttpResponse const& response)
{
  if (response.status_code < 200 || response.status_code >= 300) {
    auto const kind = classify_provider_error(response);
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider,
                                  "OpenAI HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(kind));
    if (auto const retry_after = retry_after_header(response)) error.with_context("retry_after", *retry_after);
    if (!response.body.empty()) {
      error.with_context("body_snippet", sanitized_body_snippet(response.body, {"access_token", "refresh_token",
                                                                                "api_key", "Authorization"}));
    }
    return std::unexpected(std::move(error));
  }
  return parse_openai_sse(response.body);
}

ava::core::Result<std::string> parse_openai_response_text(std::string_view body)
{
  if (auto output = ava::core::json::string_field(body, "output_text")) return *output;
  if (auto text = ava::core::json::string_field(body, "text")) return *text;
  for (auto const& item : ava::core::json::objects_in_array_field(body, "output")) {
    if (ava::core::json::string_field(item, "type").value_or("") != "message") continue;
    for (auto const& content : ava::core::json::objects_in_array_field(item, "content")) {
      if (auto text = ava::core::json::string_field(content, "text")) return *text;
    }
  }
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI response text is missing"));
}

namespace detail {

ava::core::Result<std::vector<StreamEvent>> parse_openai_non_stream_response(std::string_view body)
{
  std::vector<StreamEvent> events;
  auto const stop_reason = openai_response_stop_reason(body);
  for (auto const& item : ava::core::json::objects_in_array_field(body, "output")) {
    if (ava::core::json::string_field(item, "type").value_or("") != "reasoning") continue;
    auto const summary = reasoning_summary_text_from_object(item);
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
  for (auto const& item : ava::core::json::objects_in_array_field(body, "output")) {
    if (ava::core::json::string_field(item, "type").value_or("") != "function_call") continue;
    auto const id = first_string_field(item, {"id", "item_id", "call_id"}).value_or("");
    auto const name = ava::core::json::string_field(item, "name").value_or("");
    auto const arguments = ava::core::json::string_field(item, "arguments").value_or("");
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
  bool const allows_empty_terminal = stop_reason == "incomplete" || stop_reason == "max_tokens" ||
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

}  // namespace detail

std::optional<TokenUsage> parse_openai_usage(std::string_view body)
{
  if (auto const usage = ava::core::json::object_field(body, "usage")) return usage_from_object(*usage);
  if (auto const response = ava::core::json::object_field(body, "response")) {
    if (auto const usage = ava::core::json::object_field(*response, "usage")) return usage_from_object(*usage);
  }
  return usage_from_object(body);
}

bool is_retryable_status(int status_code) noexcept
{
  return status_code == 408 || status_code == 409 || status_code == 429 || (status_code >= 500 && status_code < 600);
}

bool is_auth_status(int status_code) noexcept
{
  return status_code == 401 || status_code == 403;
}

}  // namespace ava::provider
