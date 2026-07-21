#include "sys.h"
#include "ava/agent/usage_accounting.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace ava::agent {
namespace {

std::string decimal_json(long double value)
{
  std::ostringstream out;
  out << std::setprecision(12) << value;
  return out.str();
}

void append_optional_integer_field(std::string& json, std::string_view key, std::optional<long long> const& value, bool& first)
{
  if (!value || *value < 0)
    return;
  if (!first)
    json += ',';
  first = false;
  json += '"';
  json += key;
  json += "\":";
  json += std::to_string(*value);
}

long long saturating_add(long long left, long long right) noexcept
{
  constexpr auto maximum = std::numeric_limits<long long>::max();
  constexpr auto minimum = std::numeric_limits<long long>::min();
  if (right > 0 && left > maximum - right)
    return maximum;
  if (right < 0 && left < minimum - right)
    return minimum;
  return left + right;
}

std::size_t saturating_add(std::size_t left, std::size_t right) noexcept
{
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  return right > maximum - left ? maximum : left + right;
}

long long clamped_byte_count(std::size_t value) noexcept
{
  constexpr auto maximum = std::numeric_limits<long long>::max();
  return value > static_cast<std::size_t>(maximum) ? maximum : static_cast<long long>(value);
}

std::size_t output_estimate_bytes(ParsedAssistantTurn const& turn)
{
  std::size_t bytes = turn.text.size();
  for (auto const& reasoning : turn.reasoning_blocks)
  {
    bytes = saturating_add(bytes, reasoning.text.size());
    bytes = saturating_add(bytes, reasoning.signature.size());
    bytes = saturating_add(bytes, reasoning.redacted_data.size());
    bytes = saturating_add(bytes, reasoning.native_item_json.size());
  }
  for (auto const& call : turn.tool_calls)
  {
    bytes = saturating_add(bytes, call.id.size());
    bytes = saturating_add(bytes, call.name.size());
    bytes = saturating_add(bytes, call.arguments_json.size());
  }
  return bytes;
}

void add_optional_usage_field(std::optional<long long>& total, std::optional<long long> const& value)
{
  if (!value || *value < 0)
    return;
  if (!total)
    total = 0;
  *total = saturating_add(*total, *value);
}

}  // namespace

std::string usage_json(ava::provider::TokenUsage const& usage, std::optional<long double> const& cost_usd)
{
  std::string json = "{";
  bool first = true;
  append_optional_integer_field(json, "input_tokens", usage.input_tokens, first);
  append_optional_integer_field(json, "output_tokens", usage.output_tokens, first);
  append_optional_integer_field(json, "reasoning_tokens", usage.reasoning_tokens, first);
  append_optional_integer_field(json, "cache_read_tokens", usage.cache_read_tokens, first);
  append_optional_integer_field(json, "cache_write_tokens", usage.cache_write_tokens, first);
  append_optional_integer_field(json, "total_tokens", usage.total_tokens, first);
  append_optional_integer_field(json, "estimated_input_bytes", usage.estimated_input_bytes, first);
  append_optional_integer_field(json, "estimated_output_bytes", usage.estimated_output_bytes, first);
  append_optional_integer_field(json, "estimated_total_bytes", usage.estimated_total_bytes, first);
  if (!first)
    json += ',';
  json += "\"estimated\":";
  json += usage.estimated ? "true" : "false";
  json += ",\"source\":\"";
  json += usage.estimated ? "estimated" : "provider";
  json += '"';
  if (usage.estimated)
  {
    json += ",\"estimation_method\":\"byte_count\"";
  }
  if (cost_usd)
  {
    json += ",\"cost_usd\":";
    json += decimal_json(*cost_usd);
    json += ",\"cost_estimated\":";
    json += usage.estimated ? "true" : "false";
  }
  json += '}';
  return json;
}

ava::provider::TokenUsage with_total_tokens(ava::provider::TokenUsage usage)
{
  if (!usage.total_tokens && usage.input_tokens && usage.output_tokens)
  {
    usage.total_tokens = saturating_add(*usage.input_tokens, *usage.output_tokens);
  }
  return usage;
}

ava::provider::TokenUsage estimate_usage_from_turn(std::string_view request_body, ParsedAssistantTurn const& turn)
{
  auto const input_bytes = clamped_byte_count(request_body.size());
  auto const output_bytes = clamped_byte_count(output_estimate_bytes(turn));
  return ava::provider::TokenUsage{.input_tokens = std::nullopt,
                                   .output_tokens = std::nullopt,
                                   .reasoning_tokens = std::nullopt,
                                   .cache_read_tokens = std::nullopt,
                                   .cache_write_tokens = std::nullopt,
                                   .total_tokens = std::nullopt,
                                   .estimated_input_bytes = input_bytes,
                                   .estimated_output_bytes = output_bytes,
                                   .estimated_total_bytes = saturating_add(input_bytes, output_bytes),
                                   .estimated = true};
}

void accumulate_usage(std::optional<ava::provider::TokenUsage>& total, ava::provider::TokenUsage const& usage)
{
  if (!total)
    total = ava::provider::TokenUsage{};
  add_optional_usage_field(total->input_tokens, usage.input_tokens);
  add_optional_usage_field(total->output_tokens, usage.output_tokens);
  add_optional_usage_field(total->reasoning_tokens, usage.reasoning_tokens);
  add_optional_usage_field(total->cache_read_tokens, usage.cache_read_tokens);
  add_optional_usage_field(total->cache_write_tokens, usage.cache_write_tokens);
  add_optional_usage_field(total->total_tokens, usage.total_tokens);
  add_optional_usage_field(total->estimated_input_bytes, usage.estimated_input_bytes);
  add_optional_usage_field(total->estimated_output_bytes, usage.estimated_output_bytes);
  add_optional_usage_field(total->estimated_total_bytes, usage.estimated_total_bytes);
  total->estimated = total->estimated || usage.estimated;
}

}  // namespace ava::agent
