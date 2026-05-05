#include "ava/provider/anthropic_response_support.h"

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

}  // namespace ava::provider::detail
