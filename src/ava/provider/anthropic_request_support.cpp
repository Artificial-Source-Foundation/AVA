#include "ava/provider/anthropic_request_support.h"

#include <cstddef>
#include <string_view>
#include <utility>

#include "ava/core/json.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider::detail {
namespace {

std::string cache_control_json(std::string_view ttl)
{
  return ",\"cache_control\":{\"type\":\"ephemeral\",\"ttl\":\"" + std::string(ttl) + "\"}";
}

std::string cache_control_suffix(std::string_view ttl)
{
  return ttl.empty() ? std::string{} : cache_control_json(ttl);
}

std::string message_json(ChatMessage const& message)
{
  std::string const role = anthropic_message_role(message);
  if (message.content_parts.empty()) {
    return "{\"role\":\"" + role + "\",\"content\":\"" + ava::core::json::escape(message.content) + "\"}";
  }
  std::string json = "{\"role\":\"" + role + "\",\"content\":[";
  bool first = true;
  for (auto const& part : message.content_parts) {
    if (part.type == ContentPartType::Text && part.text.empty()) continue;
    if (!first) json += ',';
    first = false;
    json += anthropic_content_part_json(part);
  }
  if (first) json += "{\"type\":\"text\",\"text\":\"\"}";
  json += "]}";
  return json;
}

}  // namespace

std::string anthropic_message_role(ChatMessage const& message)
{
  return message.role == "assistant" ? "assistant" : "user";
}

std::vector<ContentPart> anthropic_message_content_parts(ChatMessage const& message)
{
  if (!message.content_parts.empty()) return message.content_parts;
  if (message.content.empty()) return {};
  return {ContentPart{.type = ContentPartType::Text,
                      .text = message.content,
                      .tool_call_id = "",
                      .tool_name = "",
                      .input_json = "",
                      .is_error = false}};
}

ContentPart anthropic_text_separator_part()
{
  return ContentPart{.type = ContentPartType::Text,
                     .text = "\n\n",
                     .tool_call_id = "",
                     .tool_name = "",
                     .input_json = "",
                     .is_error = false};
}

void append_anthropic_content_parts(std::vector<ContentPart>& target, std::vector<ContentPart> const& source,
                                    bool separate_text)
{
  for (auto const& part : source) {
    if (part.type == ContentPartType::Text && part.text.empty()) continue;
    if (separate_text && part.type == ContentPartType::Text && !target.empty() &&
        target.back().type == ContentPartType::Text) {
      if (!target.back().cache_control_ttl.empty() || !part.cache_control_ttl.empty()) {
        target.push_back(anthropic_text_separator_part());
        target.push_back(part);
      } else {
        target.back().text += "\n\n";
        target.back().text += part.text;
      }
      separate_text = false;
      continue;
    }
    target.push_back(part);
    separate_text = false;
  }
}

std::vector<ChatMessage> collapse_consecutive_anthropic_roles(std::vector<ChatMessage> const& messages)
{
  std::vector<ChatMessage> collapsed;
  for (auto const& message : messages) {
    std::string const role = anthropic_message_role(message);
    if (!collapsed.empty() && collapsed.back().role == role) {
      if (collapsed.back().content_parts.empty() && message.content_parts.empty()) {
        collapsed.back().content += "\n\n";
        collapsed.back().content += message.content;
        continue;
      }
      if (collapsed.back().content_parts.empty()) {
        collapsed.back().content_parts = anthropic_message_content_parts(collapsed.back());
      }
      append_anthropic_content_parts(collapsed.back().content_parts, anthropic_message_content_parts(message), true);
      if (!message.content.empty()) {
        collapsed.back().content += "\n\n";
        collapsed.back().content += message.content;
      }
      continue;
    }
    collapsed.push_back(ChatMessage{.role = role, .content = message.content, .content_parts = message.content_parts});
  }
  return collapsed;
}

long long anthropic_max_tokens_for_request(ProviderRequest const& request)
{
  long long const value = request.max_output_tokens.value_or(kDefaultAnthropicMaxTokens);
  return value > 0 ? value : kDefaultAnthropicMaxTokens;
}

std::string anthropic_content_part_json(ContentPart const& part)
{
  switch (part.type) {
    case ContentPartType::Text:
      return "{\"type\":\"text\",\"text\":\"" + ava::core::json::escape(part.text) + "\"" +
             cache_control_suffix(part.cache_control_ttl) + "}";
    case ContentPartType::Reasoning: {
      if (part.redacted) {
        return "{\"type\":\"redacted_thinking\",\"data\":\"" + ava::core::json::escape(part.reasoning_redacted_data) +
               "\"}";
      }
      std::string json = "{\"type\":\"thinking\",\"thinking\":\"" + ava::core::json::escape(part.text) + "\"";
      if (!part.reasoning_signature.empty()) {
        json += ",\"signature\":\"" + ava::core::json::escape(part.reasoning_signature) + "\"";
      }
      json += '}';
      return json;
    }
    case ContentPartType::ToolUse: {
      auto const input = is_valid_json_object(part.input_json) ? part.input_json : std::string("{}");
      return "{\"type\":\"tool_use\",\"id\":\"" + ava::core::json::escape(part.tool_call_id) + "\",\"name\":\"" +
             ava::core::json::escape(part.tool_name) + "\",\"input\":" + input +
             cache_control_suffix(part.cache_control_ttl) + "}";
    }
    case ContentPartType::ToolResult: {
      std::string json = "{\"type\":\"tool_result\",\"tool_use_id\":\"" + ava::core::json::escape(part.tool_call_id) +
                         "\",\"content\":\"" + ava::core::json::escape(part.text) + "\"";
      if (part.is_error) json += ",\"is_error\":true";
      json += cache_control_suffix(part.cache_control_ttl);
      json += '}';
      return json;
    }
  }
  return "{\"type\":\"text\",\"text\":\"\"}";
}

std::string anthropic_system_prompt_json(ProviderRequest const& request)
{
  if (request.system_prompt.empty() && request.system_prompt_cache_ttl.empty()) return {};
  if (request.system_prompt_cache_ttl.empty()) {
    return "\"system\":\"" + ava::core::json::escape(request.system_prompt) + "\"";
  }
  return "\"system\":[{\"type\":\"text\",\"text\":\"" + ava::core::json::escape(request.system_prompt) + "\"" +
         cache_control_json(request.system_prompt_cache_ttl) + "}]";
}

std::string anthropic_reasoning_options_json(ProviderReasoningOptions const& reasoning)
{
  std::string json = ",\"thinking\":{\"type\":\"" + ava::core::json::escape(reasoning.type) + "\"";
  if (reasoning.budget_tokens) json += ",\"budget_tokens\":" + std::to_string(*reasoning.budget_tokens);
  if (!reasoning.display.empty()) json += ",\"display\":\"" + ava::core::json::escape(reasoning.display) + "\"";
  json += '}';
  return json;
}

std::string anthropic_tool_json(std::string_view tool_json)
{
  auto const name = ava::core::json::string_field(tool_json, "name").value_or("");
  auto const description = ava::core::json::string_field(tool_json, "description").value_or("");
  auto const parameters = ava::core::json::object_field(tool_json, "parameters").value_or("{\"type\":\"object\"}");
  return "{\"name\":\"" + ava::core::json::escape(name) + "\",\"description\":\"" +
         ava::core::json::escape(description) + "\",\"input_schema\":" + parameters + "}";
}

std::string anthropic_request_body_json_unchecked(ProviderRequest const& request,
                                                  std::vector<ChatMessage> const& messages)
{
  std::string body = "{\"model\":\"" + ava::core::json::escape(request.model_id) +
                     "\",\"max_tokens\":" + std::to_string(anthropic_max_tokens_for_request(request)) +
                     ",\"stream\":" + (request.stream ? "true" : "false");
  if (auto const system = anthropic_system_prompt_json(request); !system.empty()) body += "," + system;
  body += ",\"messages\":[";
  for (std::size_t index = 0; index < messages.size(); ++index) {
    if (index > 0) body += ',';
    body += message_json(messages[index]);
  }
  body += ']';
  if (!request.tools_json.empty()) {
    body += ",\"tools\":[";
    for (std::size_t index = 0; index < request.tools_json.size(); ++index) {
      if (index > 0) body += ',';
      body += anthropic_tool_json(request.tools_json[index]);
    }
    body += ']';
  }
  if (request.reasoning) body += anthropic_reasoning_options_json(*request.reasoning);
  body += '}';
  return body;
}

}  // namespace ava::provider::detail
