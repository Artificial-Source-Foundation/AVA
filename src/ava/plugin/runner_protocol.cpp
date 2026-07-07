#include "ava/plugin/runner_protocol.h"
#include "ava/core/json.h"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ava::plugin {
namespace {

constexpr int kMaxPluginJsonDepth = 128;

std::optional<bool> bool_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return std::nullopt;
  auto const valid_terminator = [](std::string_view value, std::size_t offset) {
    while (offset < value.size() && std::isspace(static_cast<unsigned char>(value[offset])) != 0) ++offset;
    return offset >= value.size() || value[offset] == ',' || value[offset] == '}';
  };
  if (object.substr(*start, 4) == "true" && valid_terminator(object, *start + 4))
    return true;
  if (object.substr(*start, 5) == "false" && valid_terminator(object, *start + 5))
    return false;
  return std::nullopt;
}

bool json_depth_within_limit(std::string_view value, int max_depth)
{
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (char const ch : value)
  {
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string)
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
    {
      in_string = !in_string;
      continue;
    }
    if (in_string)
      continue;
    if (ch == '{' || ch == '[')
    {
      ++depth;
      if (depth > max_depth)
        return false;
    }
    else if (ch == '}' || ch == ']')
    {
      --depth;
      if (depth < 0)
        return false;
    }
  }
  return true;
}

std::optional<std::string> parse_balanced(std::string_view text, std::size_t start, char open, char close)
{
  if (start >= text.size() || text[start] != open)
    return std::nullopt;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t index = start; index < text.size(); ++index)
  {
    char const ch = text[index];
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string)
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
    {
      in_string = !in_string;
      continue;
    }
    if (in_string)
      continue;
    if (ch == open)
      ++depth;
    if (ch == close)
    {
      --depth;
      if (depth == 0)
        return std::string(text.substr(start, index - start + 1));
    }
  }
  return std::nullopt;
}

void skip_json_ws(std::string_view text, std::size_t& index)
{
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
}

std::optional<std::vector<std::string>> object_array_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start || *start >= object.size() || object[*start] != '[')
    return std::nullopt;
  auto array = parse_balanced(object, *start, '[', ']');
  if (!array)
    return std::nullopt;

  std::vector<std::string> result;
  std::size_t index = 1;
  while (true)
  {
    skip_json_ws(*array, index);
    if (index >= array->size())
      return std::nullopt;
    if ((*array)[index] == ']')
      return result;
    if ((*array)[index] != '{')
      return std::nullopt;
    auto parsed = parse_balanced(*array, index, '{', '}');
    if (!parsed || !ava::core::json::is_valid_object(*parsed))
      return std::nullopt;
    result.push_back(*parsed);
    index += parsed->size();
    skip_json_ws(*array, index);
    if (index >= array->size())
      return std::nullopt;
    if ((*array)[index] == ',')
    {
      ++index;
      continue;
    }
    if ((*array)[index] == ']')
      return result;
    return std::nullopt;
  }
}

}  // namespace

std::optional<PluginInitialization> parse_initialized_response(std::string_view record)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth))
    return std::nullopt;
  if (!ava::core::json::is_valid_object(record))
    return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto api_version = ava::core::json::string_field(record, "api_version");
  auto plugin_version = ava::core::json::string_field(record, "plugin_version");
  auto contributions = ava::core::json::object_field(record, "contributions");
  if (!id || *id != "ava_1" || !type || *type != "initialized" || !api_version || !plugin_version || plugin_version->empty() || !contributions)
  {
    return std::nullopt;
  }
  if (*api_version != kPluginApiVersion)
    return std::nullopt;
  return PluginInitialization{.api_version = std::move(*api_version),
                              .plugin_version = std::move(*plugin_version),
                              .contributions_json = std::move(*contributions),
                              .raw_json = std::string(record)};
}

std::optional<PluginToolCallResult> parse_tool_result_response(std::string_view record, std::string_view request_id)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth))
    return std::nullopt;
  if (!ava::core::json::is_valid_object(record))
    return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  auto content = ava::core::json::string_field(record, "content");
  if (!id || *id != request_id || !type || *type != "tool.result" || !ok || !content)
    return std::nullopt;
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginToolCallResult{.ok = *ok, .content = std::move(*content), .metadata_json = metadata.value_or(std::string{}), .raw_json = std::string(record)};
}

std::optional<PluginCommandCallResult> parse_command_result_response(std::string_view record, std::string_view request_id)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth))
    return std::nullopt;
  if (!ava::core::json::is_valid_object(record))
    return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  auto content = ava::core::json::string_field(record, "content");
  if (!id || *id != request_id || !type || *type != "command.result" || !ok || !content)
    return std::nullopt;
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginCommandCallResult{.ok = *ok, .content = std::move(*content), .metadata_json = metadata.value_or(std::string{}), .raw_json = std::string(record)};
}

std::optional<PluginEventObserveResult> parse_event_observed_response(std::string_view record, std::string_view request_id)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth))
    return std::nullopt;
  if (!ava::core::json::is_valid_object(record))
    return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  if (!id || *id != request_id || !type || *type != "event.observed" || !ok)
    return std::nullopt;
  auto content = ava::core::json::string_field(record, "content").value_or("");
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginEventObserveResult{.ok = *ok, .content = std::move(content), .metadata_json = metadata.value_or(std::string{}), .raw_json = std::string(record)};
}

std::optional<PluginDynamicResourceListResult> parse_resource_list_result_response(std::string_view record, std::string_view request_id,
                                                                                   PluginDynamicResourceKind kind)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth))
    return std::nullopt;
  if (!ava::core::json::is_valid_object(record))
    return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  auto response_kind = ava::core::json::string_field(record, "kind");
  if (!id || *id != request_id || !type || *type != "resource.list.result" || !ok || !response_kind ||
      *response_kind != plugin_dynamic_resource_kind_name(kind))
  {
    return std::nullopt;
  }

  auto metadata = ava::core::json::object_field(record, "metadata");
  if (!*ok)
  {
    auto content = ava::core::json::string_field(record, "content");
    if (!content || content->size() > kPluginResourceContentMaxBytes)
      return std::nullopt;
    return PluginDynamicResourceListResult{
        .ok = false, .resources = {}, .content = std::move(*content), .metadata_json = metadata.value_or(std::string{}), .raw_json = std::string(record)};
  }

  auto resource_objects = object_array_field(record, "resources");
  if (!resource_objects)
    return std::nullopt;
  PluginDynamicResourceListResult parsed{.ok = true,
                                         .resources = {},
                                         .content = ava::core::json::string_field(record, "content").value_or(""),
                                         .metadata_json = metadata.value_or(std::string{}),
                                         .raw_json = std::string(record)};
  if (parsed.content.size() > kPluginResourceContentMaxBytes)
    return std::nullopt;
  parsed.resources.reserve(resource_objects->size());
  for (auto const& resource : *resource_objects)
  {
    auto name = ava::core::json::string_field(resource, "name");
    if (!name || !ava::plugin::is_valid_dynamic_resource_name(*name))
      return std::nullopt;
    auto description = ava::core::json::string_field(resource, "description").value_or("");
    if (description.size() > 4096)
      return std::nullopt;
    parsed.resources.push_back(PluginDynamicResource{.name = std::move(*name), .description = std::move(description), .raw_json = resource});
  }
  return parsed;
}

std::optional<PluginDynamicResourceReadResult> parse_resource_read_result_response(std::string_view record, std::string_view request_id,
                                                                                   PluginDynamicResourceKind kind, std::string_view name)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth))
    return std::nullopt;
  if (!ava::core::json::is_valid_object(record))
    return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  auto response_kind = ava::core::json::string_field(record, "kind");
  auto response_name = ava::core::json::string_field(record, "name");
  auto content = ava::core::json::string_field(record, "content");
  if (!id || *id != request_id || !type || *type != "resource.read.result" || !ok || !response_kind ||
      *response_kind != plugin_dynamic_resource_kind_name(kind) || !response_name || *response_name != name || !content ||
      content->size() > kPluginResourceContentMaxBytes)
  {
    return std::nullopt;
  }
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginDynamicResourceReadResult{
      .ok = *ok, .content = std::move(*content), .metadata_json = metadata.value_or(std::string{}), .raw_json = std::string(record)};
}

std::optional<PluginProxyRequest> parse_proxy_request(std::string_view record)
{
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth))
    return std::nullopt;
  if (!ava::core::json::is_valid_object(record))
    return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto operation = ava::core::json::string_field(record, "operation");
  auto arguments = ava::core::json::object_field(record, "arguments");
  if (!id || id->empty() || !type || *type != "proxy.request" || !operation || operation->empty() || !arguments ||
      !ava::core::json::is_valid_object(*arguments))
  {
    return std::nullopt;
  }
  return PluginProxyRequest{.id = std::move(*id), .operation = std::move(*operation), .arguments_json = std::move(*arguments), .raw_json = std::string(record)};
}

}  // namespace ava::plugin
