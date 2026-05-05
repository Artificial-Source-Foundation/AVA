#include "ava/mcp/protocol.h"

#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace ava::mcp {
namespace {

ava::core::Error mcp_protocol_error(std::string message, McpServerConfig const& server)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Tool, std::move(message));
  error.with_context("mcp_server", server.id);
  if (!server.source_path.empty()) error.with_context("config", server.source_path.string());
  return error;
}

std::string trim_ascii(std::string text)
{
  auto first = text.begin();
  while (first != text.end() && std::isspace(static_cast<unsigned char>(*first)) != 0) ++first;
  auto last = text.end();
  while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) --last;
  return std::string(first, last);
}

std::string lowercase_ascii(std::string text)
{
  for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

std::optional<std::size_t> field_value_start_any_depth(std::string_view object, std::string_view key)
{
  std::string const needle = "\"" + ava::core::json::escape(key) + "\"";
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = 0; index < object.size(); ++index) {
    char const ch = object[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == '"') in_string = false;
      continue;
    }
    if (ch != '"') continue;
    if (object.substr(index, needle.size()) == needle) {
      auto colon = index + needle.size();
      while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
      if (colon < object.size() && object[colon] == ':') {
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
  if (!start) return std::nullopt;
  auto const valid_terminator = [](std::string_view value, std::size_t offset) {
    while (offset < value.size() && std::isspace(static_cast<unsigned char>(value[offset])) != 0) ++offset;
    return offset >= value.size() || value[offset] == ',' || value[offset] == '}';
  };
  if (object.substr(*start, 4) == "true" && valid_terminator(object, *start + 4)) return true;
  if (object.substr(*start, 5) == "false" && valid_terminator(object, *start + 5)) return false;
  return std::nullopt;
}

bool mcp_json_depth_within_limit(std::string_view value, int max_depth)
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

std::optional<std::size_t> mcp_header_end_offset(std::string_view buffer)
{
  if (auto const crlf = buffer.find("\r\n\r\n"); crlf != std::string_view::npos) return crlf + 4;
  if (auto const lf = buffer.find("\n\n"); lf != std::string_view::npos) return lf + 2;
  return std::nullopt;
}

ava::core::Result<std::size_t> parse_mcp_content_length(std::string_view headers, McpServerConfig const& server,
                                                        std::size_t max_message_bytes)
{
  std::optional<std::size_t> content_length;
  std::size_t line_start = 0;
  while (line_start <= headers.size()) {
    auto const line_end = headers.find('\n', line_start);
    auto line = headers.substr(
        line_start, line_end == std::string_view::npos ? headers.size() - line_start : line_end - line_start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    auto const colon = line.find(':');
    if (colon != std::string_view::npos) {
      auto key = lowercase_ascii(trim_ascii(std::string(line.substr(0, colon))));
      if (key == "content-length") {
        auto value = trim_ascii(std::string(line.substr(colon + 1)));
        if (value.empty() ||
            !std::ranges::all_of(value, [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; })) {
          return std::unexpected(mcp_protocol_error("MCP Content-Length header is invalid", server));
        }
        try {
          content_length = static_cast<std::size_t>(std::stoull(value));
        } catch (...) {
          return std::unexpected(mcp_protocol_error("MCP Content-Length header is out of range", server));
        }
      }
    }
    if (line_end == std::string_view::npos) break;
    line_start = line_end + 1;
  }
  if (!content_length) return std::unexpected(mcp_protocol_error("MCP message is missing Content-Length", server));
  if (*content_length > max_message_bytes) {
    auto error = mcp_protocol_error("MCP message exceeds size cap", server);
    error.with_context("max_bytes", std::to_string(max_message_bytes));
    return std::unexpected(std::move(error));
  }
  return *content_length;
}

std::optional<std::string> mcp_response_id(std::string_view message)
{
  auto id = ava::core::json::string_field(message, "id");
  if (id) return id;
  auto const numeric_start = field_value_start_any_depth(message, "id");
  if (!numeric_start) return std::nullopt;
  std::size_t end = *numeric_start;
  while (end < message.size() && std::isdigit(static_cast<unsigned char>(message[end])) != 0) ++end;
  if (end == *numeric_start) return std::nullopt;
  return std::string(message.substr(*numeric_start, end - *numeric_start));
}

std::optional<std::string> mcp_error_message_from_response(std::string_view error_json)
{
  return ava::core::json::string_field(error_json, "message");
}

bool is_valid_mcp_tool_name(std::string_view name)
{
  if (name.empty() || name.size() > 128) return false;
  for (char const ch : name) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) return false;
  }
  return true;
}

std::string mcp_text_content_from_result(std::string_view result_json)
{
  std::string content;
  for (auto const& item : ava::core::json::objects_in_array_field(result_json, "content")) {
    auto const type = ava::core::json::string_field(item, "type");
    auto const text = ava::core::json::string_field(item, "text");
    if (type && *type == "text" && text) {
      if (!content.empty()) content += '\n';
      content += *text;
    }
  }
  if (!content.empty()) return content;
  if (auto const structured = ava::core::json::object_field(result_json, "structuredContent")) return *structured;
  return {};
}

}  // namespace ava::mcp
