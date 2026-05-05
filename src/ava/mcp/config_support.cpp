#include "ava/mcp/config_support.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

#include "ava/core/json.h"

namespace ava::mcp::detail {
namespace {

std::optional<std::string> parse_string_literal(std::string_view literal)
{
  return ava::core::json::string_field(std::string("{\"value\":") + std::string(literal) + '}', "value");
}

}  // namespace

ava::core::Error config_error(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

bool has_forbidden_byte(std::string_view value)
{
  for (char const ch : value) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) return true;
  }
  return false;
}

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

std::optional<std::string> array_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start || *start >= object.size() || object[*start] != '[') return std::nullopt;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t index = *start; index < object.size(); ++index) {
    char const ch = object[index];
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
    if (ch == '[') ++depth;
    if (ch == ']') {
      --depth;
      if (depth == 0) return std::string(object.substr(*start, index - *start + 1));
    }
  }
  return std::nullopt;
}

ava::core::Result<std::vector<std::string>> string_array_field(std::string_view object, std::string_view key)
{
  std::vector<std::string> values;
  if (!ava::core::json::field_value_start(object, key)) return values;
  auto const array = array_field(object, key);
  if (!array) {
    return std::unexpected(
        config_error("MCP config field must be an array of strings").with_context("field", std::string(key)));
  }
  std::size_t index = 1;
  while (index + 1 < array->size()) {
    while (index + 1 < array->size() && std::isspace(static_cast<unsigned char>((*array)[index])) != 0) ++index;
    if (index + 1 >= array->size() || (*array)[index] == ']') break;
    if ((*array)[index] != '"') {
      return std::unexpected(
          config_error("MCP config field must contain only strings").with_context("field", std::string(key)));
    }
    std::size_t end = index + 1;
    bool escaped = false;
    while (end < array->size()) {
      char const ch = (*array)[end];
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        break;
      }
      ++end;
    }
    if (end >= array->size()) {
      return std::unexpected(
          config_error("MCP config string array has unterminated string").with_context("field", std::string(key)));
    }
    auto value = parse_string_literal(array->substr(index, end - index + 1));
    if (!value) {
      return std::unexpected(
          config_error("MCP config string array has invalid string escape").with_context("field", std::string(key)));
    }
    values.push_back(std::move(*value));
    index = end + 1;
    while (index + 1 < array->size() && std::isspace(static_cast<unsigned char>((*array)[index])) != 0) ++index;
    if (index + 1 < array->size() && (*array)[index] == ',') {
      ++index;
      continue;
    }
    if (index < array->size() && (*array)[index] == ']') break;
    return std::unexpected(
        config_error("MCP config string array has invalid separator").with_context("field", std::string(key)));
  }
  return values;
}

ava::core::Result<std::string> read_mcp_config_file(std::filesystem::path const& path)
{
  std::error_code status_error;
  auto const status = std::filesystem::status(path, status_error);
  if (status_error) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect MCP config")
                               .with_context("path", path.string())
                               .with_context("cause", status_error.message()));
  }
  if (!std::filesystem::is_regular_file(status)) {
    return std::unexpected(config_error("MCP config must be a regular file").with_context("path", path.string()));
  }
  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect MCP config size")
                               .with_context("path", path.string())
                               .with_context("cause", size_error.message()));
  }
  if (size > kMaxMcpConfigBytes) {
    return std::unexpected(config_error("MCP config exceeds maximum size")
                               .with_context("path", path.string())
                               .with_context("max_bytes", std::to_string(kMaxMcpConfigBytes)));
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open MCP config")
                               .with_context("path", path.string()));
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read MCP config")
                               .with_context("path", path.string()));
  }
  return buffer.str();
}

ava::core::Result<McpConfig> load_optional_mcp_config(std::filesystem::path const& path, McpServerScope scope)
{
  McpConfig config;
  if (scope == McpServerScope::Global) {
    config.global_config_file = path;
  } else {
    config.project_config_file = path;
  }
  if (path.empty()) return config;
  std::error_code exists_error;
  bool const exists = std::filesystem::exists(path, exists_error);
  if (exists_error) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect MCP config")
                               .with_context("path", path.string())
                               .with_context("cause", exists_error.message()));
  }
  if (!exists) return config;
  auto content = read_mcp_config_file(path);
  if (!content) return std::unexpected(std::move(content.error()));
  return parse_mcp_config(*content, path, scope);
}

ava::core::VoidResult append_mcp_config(McpConfig& target, McpConfig source)
{
  if (!source.global_config_file.empty()) target.global_config_file = std::move(source.global_config_file);
  if (!source.project_config_file.empty()) target.project_config_file = std::move(source.project_config_file);
  for (auto& server : source.servers) {
    auto const duplicate =
        std::ranges::find_if(target.servers, [&](McpServerConfig const& existing) { return existing.id == server.id; });
    if (duplicate != target.servers.end()) {
      return std::unexpected(config_error("duplicate MCP server id")
                                 .with_context("server", server.id)
                                 .with_context("first_config", duplicate->source_path.string())
                                 .with_context("second_config", server.source_path.string()));
    }
    target.servers.push_back(std::move(server));
  }
  return {};
}

}  // namespace ava::mcp::detail
