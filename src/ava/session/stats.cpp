#include "ava/session/stats.h"

#include <cctype>
#include <initializer_list>
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

std::optional<long long> integer_field_from_usage_or_entry(std::string_view data_json,
                                                           std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    if (const auto value = integer_field_from_usage_or_entry(data_json, key)) return value;
  }
  return std::nullopt;
}

bool bool_field_is_true(std::string_view object, std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  return start && object.substr(*start, 4) == "true";
}

bool usage_is_estimated(std::string_view usage) {
  if (bool_field_is_true(usage, "estimated")) return true;
  return ava::core::json::string_field(usage, "source").value_or("") == "estimated";
}

bool positive_integer_field(std::string_view object, std::string_view key) {
  const auto value = ava::core::json::integer_field(object, key);
  return value && *value > 0;
}

bool object_has_billable_tokens(std::string_view object) {
  return positive_integer_field(object, "input_tokens") || positive_integer_field(object, "output_tokens") ||
         positive_integer_field(object, "reasoning_tokens") || positive_integer_field(object, "cache_read_tokens") ||
         positive_integer_field(object, "cache_read_input_tokens") ||
         positive_integer_field(object, "cache_write_tokens") ||
         positive_integer_field(object, "cache_creation_input_tokens") ||
         positive_integer_field(object, "total_tokens");
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
        if (!is_internal_replay_user_message(entry)) ++stats.counts.user_message;
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

    const auto usage = ava::core::json::object_field(entry.data_json, "usage");
    if (usage) {
      if (usage_is_estimated(*usage)) {
        add_optional_integer(stats.estimated_input_bytes,
                             integer_field_from_usage_or_entry(entry.data_json, "estimated_input_bytes"));
        add_optional_integer(stats.estimated_output_bytes,
                             integer_field_from_usage_or_entry(entry.data_json, "estimated_output_bytes"));
        add_optional_integer(stats.estimated_total_bytes,
                             integer_field_from_usage_or_entry(entry.data_json, "estimated_total_bytes"));
        ++stats.estimated_usage_entries;
        continue;
      } else {
        ++stats.exact_usage_entries;
      }
    }

    add_optional_integer(stats.input_tokens, integer_field_from_usage_or_entry(entry.data_json, "input_tokens"));
    add_optional_integer(stats.output_tokens, integer_field_from_usage_or_entry(entry.data_json, "output_tokens"));
    add_optional_integer(stats.reasoning_tokens,
                         integer_field_from_usage_or_entry(entry.data_json, "reasoning_tokens"));
    add_optional_integer(
        stats.cache_read_tokens,
        integer_field_from_usage_or_entry(entry.data_json, {"cache_read_tokens", "cache_read_input_tokens"}));
    add_optional_integer(
        stats.cache_write_tokens,
        integer_field_from_usage_or_entry(entry.data_json, {"cache_write_tokens", "cache_creation_input_tokens"}));
    add_optional_integer(stats.total_tokens, integer_field_from_usage_or_entry(entry.data_json, "total_tokens"));

    const auto cost = cost_field_from_usage_or_entry(entry.data_json);
    const bool has_known_cost = cost && *cost >= 0.0L;
    if (has_known_cost) add_optional_cost(stats.known_cost_usd, cost);
    const bool has_billable_usage_tokens = usage && object_has_billable_tokens(*usage);
    const bool has_billable_legacy_assistant_tokens =
        !usage && entry.type == EntryType::AssistantMessage && object_has_billable_tokens(entry.data_json);
    if ((has_billable_usage_tokens || has_billable_legacy_assistant_tokens) && !has_known_cost) {
      ++stats.unknown_cost_entries;
    }
  }

  stats.cost_complete = stats.unknown_cost_entries == 0;
  if (stats.cost_complete) stats.total_cost_usd = stats.known_cost_usd;

  return stats;
}

}  // namespace ava::session
