#include "ava/provider/openai_compatible_tool_schema.h"

#include <cctype>
#include <utility>

#include "ava/core/json.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider {
namespace {

bool true_field(std::string_view object, std::string_view key)
{
  bool in_string = false;
  bool escaped = false;
  int object_depth = 0;
  int array_depth = 0;
  for (std::size_t index = 0; index < object.size(); ++index) {
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
      if (!in_string && object_depth == 1 && array_depth == 0) {
        std::size_t end = index + 1;
        bool key_escaped = false;
        while (end < object.size()) {
          char const key_ch = object[end++];
          if (key_escaped) {
            key_escaped = false;
            continue;
          }
          if (key_ch == '\\') {
            key_escaped = true;
            continue;
          }
          if (key_ch == '"') break;
        }
        auto const candidate = object.substr(index + 1, end - index - 2);
        if (candidate == key) {
          auto value = end;
          while (value < object.size() && std::isspace(static_cast<unsigned char>(object[value])) != 0) ++value;
          if (value >= object.size() || object[value] != ':') {
            index = end - 1;
            continue;
          }
          ++value;
          while (value < object.size() && std::isspace(static_cast<unsigned char>(object[value])) != 0) ++value;
          if (object.substr(value, 4) != "true") return false;
          value += 4;
          while (value < object.size() && std::isspace(static_cast<unsigned char>(object[value])) != 0) ++value;
          return value >= object.size() || object[value] == ',' || object[value] == '}';
        }
        index = end - 1;
        continue;
      }
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == '{') ++object_depth;
    if (ch == '}') --object_depth;
    if (ch == '[') ++array_depth;
    if (ch == ']') --array_depth;
  }
  return false;
}

}  // namespace

ava::core::VoidResult validate_openai_compatible_tools_json(ProviderRequest const& request)
{
  for (std::size_t index = 0; index < request.tools_json.size(); ++index) {
    if (is_valid_json_object(request.tools_json[index])) continue;
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI-compatible tool JSON must be valid JSON");
    error.with_context("tool_index", std::to_string(index));
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<std::string> chat_completion_tool_json(std::string_view schema)
{
  if (!is_valid_json_object(schema)) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI-compatible tool JSON must be valid JSON"));
  }
  if (auto const function = ava::core::json::object_field(schema, "function")) {
    if (!is_valid_json_object(*function)) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "OpenAI-compatible function tool must be valid JSON"));
    }
    auto const name = ava::core::json::string_field(*function, "name");
    if (!name || name->empty()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "OpenAI-compatible tool JSON requires a function name"));
    }
    return std::string(schema);
  }

  auto const name = ava::core::json::string_field(schema, "name");
  if (!name || name->empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "OpenAI-compatible tool JSON requires a function name"));
  }
  auto const description = ava::core::json::string_field(schema, "description").value_or("");
  auto const parameters = ava::core::json::object_field(schema, "parameters").value_or("{\"type\":\"object\"}");
  if (!is_valid_json_object(parameters)) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "OpenAI-compatible tool parameters must be valid JSON"));
  }
  std::string function = "{\"name\":\"" + ava::core::json::escape(*name) + "\",\"description\":\"" +
                         ava::core::json::escape(description) + "\",\"parameters\":" + parameters;
  if (true_field(schema, "strict")) function += ",\"strict\":true";
  function += '}';
  return "{\"type\":\"function\",\"function\":" + function + "}";
}

}  // namespace ava::provider
