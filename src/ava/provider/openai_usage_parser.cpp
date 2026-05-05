#include "ava/provider/openai_usage_parser.h"

#include <initializer_list>

#include "ava/core/json.h"

namespace ava::provider {
namespace {

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

std::optional<TokenUsage> parse_openai_usage(std::string_view body)
{
  if (auto const usage = ava::core::json::object_field(body, "usage")) return usage_from_object(*usage);
  if (auto const response = ava::core::json::object_field(body, "response")) {
    if (auto const usage = ava::core::json::object_field(*response, "usage")) return usage_from_object(*usage);
  }
  return usage_from_object(body);
}

}  // namespace ava::provider
