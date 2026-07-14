#include "sys.h"
#include "ava/mcp/protocol.h"
#include "ava/core/json.h"

#include <cctype>
#include <string>

namespace ava::mcp {
namespace {

std::optional<std::size_t> field_value_start_any_depth(std::string_view object, std::string_view key)
{
  std::string const needle = "\"" + ava::core::json::escape(key) + "\"";
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = 0; index < object.size(); ++index)
  {
    char const ch = object[index];
    if (in_string)
    {
      if (escaped)
      {
        escaped = false;
        continue;
      }
      if (ch == '\\')
      {
        escaped = true;
        continue;
      }
      if (ch == '"')
        in_string = false;
      continue;
    }
    if (ch != '"')
      continue;
    if (object.substr(index, needle.size()) == needle)
    {
      auto colon = index + needle.size();
      while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
      if (colon < object.size() && object[colon] == ':')
      {
        ++colon;
        while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
        return colon;
      }
    }
    in_string = true;
  }
  return std::nullopt;
}

}  // namespace

std::optional<bool> mcp_bool_field(std::string_view object, std::string_view key)
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

bool mcp_json_depth_within_limit(std::string_view value, int max_depth)
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

std::optional<std::string> mcp_response_id(std::string_view message)
{
  auto id = ava::core::json::string_field(message, "id");
  if (id)
    return id;
  auto const numeric_start = field_value_start_any_depth(message, "id");
  if (!numeric_start)
    return std::nullopt;
  std::size_t end = *numeric_start;
  while (end < message.size() && std::isdigit(static_cast<unsigned char>(message[end])) != 0) ++end;
  if (end == *numeric_start)
    return std::nullopt;
  return std::string(message.substr(*numeric_start, end - *numeric_start));
}

std::optional<std::string> mcp_error_message_from_response(std::string_view error_json)
{
  return ava::core::json::string_field(error_json, "message");
}

bool is_valid_mcp_tool_name(std::string_view name)
{
  if (name.empty() || name.size() > 128)
    return false;
  for (char const ch : name)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F)
      return false;
  }
  return true;
}

bool is_valid_mcp_resource_uri(std::string_view uri)
{
  if (uri.empty() || uri.size() > 2048)
    return false;
  for (char const ch : uri)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F)
      return false;
  }
  return true;
}

std::string mcp_text_content_from_result(std::string_view result_json)
{
  std::string content;
  for (auto const& item : ava::core::json::objects_in_array_field(result_json, "content"))
  {
    auto const type = ava::core::json::string_field(item, "type");
    auto const text = ava::core::json::string_field(item, "text");
    if (type && *type == "text" && text)
    {
      if (!content.empty())
        content += '\n';
      content += *text;
    }
  }
  if (!content.empty())
    return content;
  if (auto const structured = ava::core::json::object_field(result_json, "structuredContent"))
    return *structured;
  return {};
}

std::string mcp_prompt_text_from_result(std::string_view result_json)
{
  std::string content;
  for (auto const& message : ava::core::json::objects_in_array_field(result_json, "messages"))
  {
    if (auto const item = ava::core::json::object_field(message, "content"))
    {
      auto const type = ava::core::json::string_field(*item, "type");
      auto const text = ava::core::json::string_field(*item, "text");
      if (type && *type == "text" && text)
      {
        if (!content.empty())
          content += '\n';
        content += *text;
      }
    }
  }
  return content;
}

std::string mcp_resource_text_from_result(std::string_view result_json)
{
  std::string content;
  for (auto const& item : ava::core::json::objects_in_array_field(result_json, "contents"))
  {
    auto const text = ava::core::json::string_field(item, "text");
    if (text)
    {
      if (!content.empty())
        content += '\n';
      content += *text;
    }
  }
  return content;
}

}  // namespace ava::mcp
