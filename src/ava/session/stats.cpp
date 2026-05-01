#include "ava/session/stats.h"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "ava/core/json.h"

namespace ava::session {
namespace {

void add_optional_integer(std::optional<long long>& total, std::optional<long long> value) {
  if (!value || *value < 0) return;
  if (!total) total = 0;
  *total += *value;
}

std::optional<long long> integer_field_from_usage_or_entry(std::string_view data_json, std::string_view key) {
  if (const auto usage = ava::core::json::object_field(data_json, "usage")) {
    if (const auto value = ava::core::json::integer_field(*usage, key)) return value;
  }
  return ava::core::json::integer_field(data_json, key);
}

bool is_number_delimiter(char ch) {
  return ch == ',' || ch == '}' || ch == ']' || std::isspace(static_cast<unsigned char>(ch)) != 0;
}

std::optional<long double> number_field(std::string_view object, std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  if (!start || *start >= object.size()) return std::nullopt;

  std::size_t index = *start;
  if (object[index] == '-') ++index;
  const auto digits_start = index;
  while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
  if (index == digits_start) return std::nullopt;
  if (index < object.size() && object[index] == '.') {
    ++index;
    const auto fraction_start = index;
    while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
    if (index == fraction_start) return std::nullopt;
  }
  if (index < object.size() && (object[index] == 'e' || object[index] == 'E')) {
    ++index;
    if (index < object.size() && (object[index] == '+' || object[index] == '-')) ++index;
    const auto exponent_start = index;
    while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
    if (index == exponent_start) return std::nullopt;
  }
  if (index < object.size() && !is_number_delimiter(object[index])) return std::nullopt;

  try {
    return std::stold(std::string(object.substr(*start, index - *start)));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<long double> cost_field_from_usage_or_entry(std::string_view data_json) {
  if (const auto usage = ava::core::json::object_field(data_json, "usage")) {
    if (const auto value = number_field(*usage, "cost_usd")) return value;
    if (const auto value = number_field(*usage, "total_cost_usd")) return value;
  }
  if (const auto value = number_field(data_json, "cost_usd")) return value;
  return number_field(data_json, "total_cost_usd");
}

void add_optional_cost(std::optional<long double>& total, std::optional<long double> value) {
  if (!value || *value < 0.0L) return;
  if (!total) total = 0.0L;
  *total += *value;
}

}  // namespace

SessionStats compute_session_stats(const std::vector<SessionEntry>& entries) {
  SessionStats stats;
  stats.entry_count = entries.size();

  for (const auto& entry : entries) {
    if (stats.first_timestamp.empty()) stats.first_timestamp = entry.timestamp;
    stats.last_timestamp = entry.timestamp;

    switch (entry.type) {
      case EntryType::SessionStart:
        ++stats.counts.session_start;
        break;
      case EntryType::UserMessage:
        ++stats.counts.user_message;
        break;
      case EntryType::AssistantMessage:
        ++stats.counts.assistant_message;
        break;
      case EntryType::ToolCall:
        ++stats.counts.tool_call;
        break;
      case EntryType::ToolResult:
        ++stats.counts.tool_result;
        break;
      case EntryType::PermissionDecision:
        ++stats.counts.permission_decision;
        break;
      case EntryType::ModeChange:
        ++stats.counts.mode_change;
        break;
      case EntryType::Compaction:
        ++stats.counts.compaction;
        break;
      case EntryType::Error:
        ++stats.counts.error;
        break;
      case EntryType::Cancel:
        ++stats.counts.cancel;
        break;
    }

    add_optional_integer(stats.input_tokens, integer_field_from_usage_or_entry(entry.data_json, "input_tokens"));
    add_optional_integer(stats.output_tokens, integer_field_from_usage_or_entry(entry.data_json, "output_tokens"));
    add_optional_integer(stats.total_tokens, integer_field_from_usage_or_entry(entry.data_json, "total_tokens"));
    add_optional_cost(stats.total_cost_usd, cost_field_from_usage_or_entry(entry.data_json));
  }

  return stats;
}

}  // namespace ava::session
