#include "sys.h"
#include "ava/session/run_stop.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace ava::session {
namespace {

bool valid_reason(std::string_view reason)
{
  return !reason.empty() && reason.size() <= kMaxRunStopReasonBytes && ava::core::json::is_valid_utf8(reason) &&
         std::ranges::none_of(reason, [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; });
}

// This closed, flat schema contains only strings and unsigned integer literals.
// Validate JSON first, then recognize each literal key exactly once. Escaped
// key aliases, nested values and unknown keys are deliberately unsupported.
bool strict_shape(std::string_view json)
{
  if (json.size() > 8192 || !ava::core::json::is_valid_object(json))
    return false;
  constexpr std::array<std::string_view, 5> keys = {"schema_version", "classification", "status", "reason", "round_count"};
  std::array<bool, 5> seen{};
  std::size_t cursor = json.find('{') + 1;
  auto ws = [&] {
    while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\n' || json[cursor] == '\r' || json[cursor] == '\t'))
      ++cursor;
  };
  for (std::size_t field = 0; field < keys.size(); ++field)
  {
    ws();
    if (cursor >= json.size() || json[cursor++] != '"')
      return false;
    auto end = json.find('"', cursor);
    if (end == std::string_view::npos)
      return false;
    auto found = std::ranges::find(keys, json.substr(cursor, end - cursor));
    if (found == keys.end())
      return false;
    auto const index = static_cast<std::size_t>(found - keys.begin());
    if (seen[index])
      return false;
    seen[index] = true;
    cursor = end + 1;
    ws();
    if (cursor >= json.size() || json[cursor++] != ':')
      return false;
    ws();
    if (index == 0 || index == 4)
    {
      auto const start = cursor;
      while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9')
        ++cursor;
      if (cursor == start)
        return false;
    }
    else
    {
      if (cursor >= json.size() || json[cursor++] != '"')
        return false;
      while (cursor < json.size() && json[cursor] != '"')
      {
        if (json[cursor] == '\\')
          ++cursor;
        ++cursor;
      }
      if (cursor >= json.size())
        return false;
      ++cursor;
    }
    ws();
    if (cursor >= json.size() || json[cursor++] != (field + 1 == keys.size() ? '}' : ','))
      return false;
  }
  ws();
  return cursor == json.size();
}

}  // namespace

ava::core::Result<RunStop> parse_run_stop(SessionEntry const& entry)
{
  auto const& json = entry.data_json;
  auto invalid = [] { return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "run_stop entry has malformed payload")); };
  if (entry.type != EntryType::RunStop || !strict_shape(json) || ava::core::json::integer_field(json, "schema_version") != 1 ||
      ava::core::json::string_field(json, "classification") != "max_turn_requests" || ava::core::json::string_field(json, "status") != "paused")
    return invalid();
  auto reason = ava::core::json::string_field(json, "reason");
  auto count = ava::core::json::integer_field(json, "round_count");
  if (!reason || !valid_reason(*reason) || !count || *count <= 0)
    return invalid();
  return RunStop{.reason = std::move(*reason), .round_count = static_cast<std::uint64_t>(*count)};
}

ava::core::Result<SessionEntry> make_run_stop_entry(RunStop const& stop)
{
  if (!valid_reason(stop.reason) || stop.round_count == 0 || stop.round_count > static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "invalid completed run stop"));
  return SessionEntry{.id = ava::core::make_id("entry"),
                      .parent_id = "",
                      .type = EntryType::RunStop,
                      .timestamp = now_timestamp(),
                      .data_json = R"({"schema_version":1,"classification":"max_turn_requests","status":"paused","reason":")" +
                                   ava::core::json::escape(stop.reason) + R"(","round_count":)" + std::to_string(stop.round_count) + "}"};
}

std::string run_stop_display(RunStop const& stop)
{
  return stop.reason + " Continue to resume.";
}

}  // namespace ava::session
