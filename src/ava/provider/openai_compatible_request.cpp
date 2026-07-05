#include "sys.h"
#include "ava/provider/openai_compatible_request.h"

#include "ava/provider/openai_compatible_provider.h"
#include "ava/provider/provider_utils.h"

#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>

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

std::string temperature_json(double value)
{
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << value;
  return stream.str();
}

std::string tool_call_json(ContentPart const& part, std::size_t index)
{
  return "{\"id\":\"" +
         ava::core::json::escape(part.tool_call_id.empty() ? "call_" + std::to_string(index) : part.tool_call_id) +
         "\",\"type\":\"function\",\"function\":{\"name\":\"" + ava::core::json::escape(part.tool_name) +
         "\",\"arguments\":\"" + ava::core::json::escape(part.input_json.empty() ? "{}" : part.input_json) + "\"}}";
}

std::string role_content_message_json(std::string_view role, std::string_view content)
{
  return "{\"role\":\"" + ava::core::json::escape(role) + "\",\"content\":\"" + ava::core::json::escape(content) +
         "\"}";
}

std::string tool_result_message_json(ContentPart const& part)
{
  return "{\"role\":\"tool\",\"tool_call_id\":\"" + ava::core::json::escape(part.tool_call_id) + "\",\"content\":\"" +
         ava::core::json::escape(part.text) + "\"}";
}

std::string image_data_url(ContentPart const& part)
{
  return "data:" + ava::core::json::escape(part.mime_type) + ";base64," + ava::core::json::escape(part.data_base64);
}

std::string multimodal_content_message_json(std::string_view role, std::vector<ContentPart> const& parts,
                                            std::string_view fallback)
{
  std::string json = "{\"role\":\"" + ava::core::json::escape(role) + "\",\"content\":[";
  bool first = true;
  for (auto const& part : parts) {
    if (part.type == ContentPartType::Text) {
      if (part.text.empty()) continue;
      if (!first) json += ',';
      first = false;
      json += "{\"type\":\"text\",\"text\":\"" + ava::core::json::escape(part.text) + "\"}";
    } else if (part.type == ContentPartType::Image) {
      if (!first) json += ',';
      first = false;
      json += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"" + image_data_url(part) + "\"}}";
    }
  }
  if (first) json += "{\"type\":\"text\",\"text\":\"" + ava::core::json::escape(fallback) + "\"}";
  json += "]}";
  return json;
}

std::vector<std::string> chat_messages_for_message(ChatMessage const& message, std::string_view reasoning_format,
                                                   bool preserve_reasoning_content)
{
  if (message.content_parts.empty()) return {role_content_message_json(message.role, message.content)};

  std::vector<std::string> result;
  if (message.role == "assistant") {
    std::string text;
    std::string reasoning_content;
    std::vector<std::string> tool_calls;
    for (auto const& part : message.content_parts) {
      if (part.type == ContentPartType::Text) {
        if (!text.empty()) text += "\n\n";
        text += part.text;
      } else if (preserve_reasoning_content && part.type == ContentPartType::Reasoning && !part.redacted &&
                 (part.reasoning_format.empty() || part.reasoning_format == reasoning_format)) {
        if (!reasoning_content.empty()) reasoning_content += "\n\n";
        reasoning_content += part.text;
      } else if (part.type == ContentPartType::ToolUse) {
        tool_calls.push_back(tool_call_json(part, tool_calls.size()));
      }
    }
    if (text.empty()) text = message.content;
    std::string assistant = "{\"role\":\"assistant\",\"content\":\"" + ava::core::json::escape(text) + "\"";
    if (!reasoning_content.empty()) {
      assistant += ",\"reasoning_content\":\"" + ava::core::json::escape(reasoning_content) + "\"";
    }
    if (!tool_calls.empty()) {
      assistant += ",\"tool_calls\":[";
      for (std::size_t index = 0; index < tool_calls.size(); ++index) {
        if (index > 0) assistant += ',';
        assistant += tool_calls[index];
      }
      assistant += ']';
    }
    assistant += '}';
    result.push_back(std::move(assistant));
    return result;
  }

  std::string text;
  bool has_tool_result = false;
  bool has_image = false;
  for (auto const& part : message.content_parts) {
    if (part.type == ContentPartType::Text) {
      if (!text.empty()) text += "\n\n";
      text += part.text;
    } else if (part.type == ContentPartType::ToolResult) {
      has_tool_result = true;
      result.push_back(tool_result_message_json(part));
    } else if (part.type == ContentPartType::Image) {
      has_image = true;
    }
  }
  if (has_tool_result) return result;
  if (has_image) {
    result.push_back(multimodal_content_message_json(message.role, message.content_parts,
                                                     text.empty() ? message.content : text));
    return result;
  }
  if (result.empty() || !text.empty() || !message.content.empty()) {
    result.insert(result.begin(), role_content_message_json(message.role, text.empty() ? message.content : text));
  }
  return result;
}

std::string reasoning_options_json(ProviderRequest const& request, bool preserve_reasoning_content,
                                   std::string_view reasoning_request_field)
{
  if (!request.reasoning || reasoning_request_field.empty()) return {};
  auto const type = request.reasoning->type.empty() ? "enabled" : request.reasoning->type;
  std::string json = ",\"" + ava::core::json::escape(reasoning_request_field) + "\":{\"type\":\"" +
                     ava::core::json::escape(type) + "\"";
  if (request.reasoning && request.reasoning->budget_tokens) {
    json += ",\"budget_tokens\":" + std::to_string(*request.reasoning->budget_tokens);
  }
  if (request.reasoning && !request.reasoning->display.empty()) {
    json += ",\"display\":\"" + ava::core::json::escape(request.reasoning->display) + "\"";
  }
  if (preserve_reasoning_content) json += ",\"keep\":\"all\"";
  json += '}';
  return json;
}

bool has_replayable_reasoning(std::vector<ChatMessage> const& messages, std::string_view reasoning_format)
{
  for (auto const& message : messages) {
    if (message.role != "assistant") continue;
    for (auto const& part : message.content_parts) {
      if (part.type == ContentPartType::Reasoning && !part.redacted && !part.text.empty() &&
          (part.reasoning_format.empty() || part.reasoning_format == reasoning_format)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

std::string join_openai_compatible_url(std::string_view base_url, std::string_view path)
{
  std::string result(base_url);
  while (!result.empty() && result.back() == '/') result.pop_back();
  if (path.empty()) return result;
  if (path.front() != '/') result.push_back('/');
  result.append(path);
  return result;
}

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

std::string openai_compatible_request_body_json(ProviderRequest const& request,
                                                OpenAICompatibleProviderOptions const& options)
{
  std::string body = "{\"model\":\"" + ava::core::json::escape(request.model_id) +
                     "\",\"stream\":" + (request.stream ? "true" : "false") + ",\"messages\":[";
  bool first_message = true;
  auto append_message = [&](std::string const& json) {
    if (!first_message) body += ',';
    first_message = false;
    body += json;
  };
  if (!request.system_prompt.empty()) append_message(role_content_message_json("system", request.system_prompt));
  for (auto const& message : request.messages) {
    for (auto const& json :
         chat_messages_for_message(message, options.reasoning_format, options.preserve_reasoning_content)) {
      append_message(json);
    }
  }
  body += ']';
  if (request.max_output_tokens && *request.max_output_tokens > 0) {
    body += ",\"max_tokens\":";
    body += std::to_string(*request.max_output_tokens);
  }
  if (options.default_temperature) body += ",\"temperature\":" + temperature_json(*options.default_temperature);
  if (request.stream && options.include_stream_usage) body += ",\"stream_options\":{\"include_usage\":true}";
  bool const preserve_replayed_reasoning = options.preserve_reasoning_content && request.reasoning &&
                                           has_replayable_reasoning(request.messages, options.reasoning_format);
  body += reasoning_options_json(request, preserve_replayed_reasoning, options.reasoning_request_field);
  body += ",\"tools\":[";
  for (std::size_t index = 0; index < request.tools_json.size(); ++index) {
    if (index > 0) body += ',';
    auto tool = chat_completion_tool_json(request.tools_json[index]);
    if (tool) body += *tool;
  }
  body += "]}";
  return body;
}

}  // namespace ava::provider
