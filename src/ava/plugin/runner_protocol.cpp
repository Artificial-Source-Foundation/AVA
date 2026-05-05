#include "ava/plugin/runner_protocol.h"

#include "ava/core/json.h"

#include <cctype>
#include <string>
#include <utility>

namespace ava::plugin {
namespace {

constexpr int kMaxPluginJsonDepth = 128;

std::optional<bool> bool_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return std::nullopt;
  auto const valid_terminator = [](std::string_view value, std::size_t offset) {
    while (offset < value.size() && std::isspace(static_cast<unsigned char>(value[offset])) != 0) ++offset;
    return offset >= value.size() || value[offset] == ',' || value[offset] == '}';
  };
  if (object.substr(*start, 4) == "true" && valid_terminator(object, *start + 4)) return true;
  if (object.substr(*start, 5) == "false" && valid_terminator(object, *start + 5)) return false;
  return std::nullopt;
}

bool json_depth_within_limit(std::string_view value, int max_depth)
{
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (char const ch : value) {
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
    if (ch == '{' || ch == '[') {
      ++depth;
      if (depth > max_depth) return false;
    } else if (ch == '}' || ch == ']') {
      --depth;
      if (depth < 0) return false;
    }
  }
  return true;
}

}  // namespace

std::optional<PluginInitialization> parse_initialized_response(std::string_view record)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth)) return std::nullopt;
  if (!ava::core::json::is_valid_object(record)) return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto api_version = ava::core::json::string_field(record, "api_version");
  auto plugin_version = ava::core::json::string_field(record, "plugin_version");
  auto contributions = ava::core::json::object_field(record, "contributions");
  if (!id || *id != "ava_1" || !type || *type != "initialized" || !api_version || !plugin_version ||
      plugin_version->empty() || !contributions) {
    return std::nullopt;
  }
  if (*api_version != kPluginApiVersion) return std::nullopt;
  return PluginInitialization{.api_version = std::move(*api_version),
                              .plugin_version = std::move(*plugin_version),
                              .contributions_json = std::move(*contributions),
                              .raw_json = std::string(record)};
}

std::optional<PluginToolCallResult> parse_tool_result_response(std::string_view record, std::string_view request_id)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth)) return std::nullopt;
  if (!ava::core::json::is_valid_object(record)) return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  auto content = ava::core::json::string_field(record, "content");
  if (!id || *id != request_id || !type || *type != "tool.result" || !ok || !content) return std::nullopt;
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginToolCallResult{.ok = *ok,
                              .content = std::move(*content),
                              .metadata_json = metadata.value_or(std::string{}),
                              .raw_json = std::string(record)};
}

std::optional<PluginCommandCallResult> parse_command_result_response(std::string_view record,
                                                                     std::string_view request_id)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth)) return std::nullopt;
  if (!ava::core::json::is_valid_object(record)) return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  auto content = ava::core::json::string_field(record, "content");
  if (!id || *id != request_id || !type || *type != "command.result" || !ok || !content) return std::nullopt;
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginCommandCallResult{.ok = *ok,
                                 .content = std::move(*content),
                                 .metadata_json = metadata.value_or(std::string{}),
                                 .raw_json = std::string(record)};
}

std::optional<PluginEventObserveResult> parse_event_observed_response(std::string_view record,
                                                                      std::string_view request_id)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth)) return std::nullopt;
  if (!ava::core::json::is_valid_object(record)) return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  if (!id || *id != request_id || !type || *type != "event.observed" || !ok) return std::nullopt;
  auto content = ava::core::json::string_field(record, "content").value_or("");
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginEventObserveResult{.ok = *ok,
                                  .content = std::move(content),
                                  .metadata_json = metadata.value_or(std::string{}),
                                  .raw_json = std::string(record)};
}

}  // namespace ava::plugin
