#include "ava/llm/providers/anthropic_protocol.hpp"

#include <cstddef>
#include <optional>

namespace ava::llm::anthropic {
namespace {

struct ParsedToolResult {
  std::string call_id;
  std::string content;
  bool is_error{false};
};

[[nodiscard]] std::string fallback_tool_call_id(std::size_t index) {
  return "tool_call_" + std::to_string(index);
}

[[nodiscard]] nlohmann::json coerce_tool_arguments(const nlohmann::json& arguments) {
  if(arguments.is_object() || arguments.is_array()) {
    return arguments;
  }

  if(arguments.is_string()) {
    try {
      const auto parsed = nlohmann::json::parse(arguments.get<std::string>());
      if(parsed.is_object() || parsed.is_array()) {
        return parsed;
      }
      return nlohmann::json::object();
    } catch(const std::exception&) {
      return nlohmann::json::object();
    }
  }

  return nlohmann::json::object();
}

[[nodiscard]] ParsedToolResult parse_tool_result(const ChatMessage& message) {
  ParsedToolResult parsed{
      .call_id = message.tool_call_id.value_or("unknown_call"),
      .content = message.content,
      .is_error = false,
  };

  try {
    const auto payload = nlohmann::json::parse(message.content);
    if(payload.is_object()) {
      if(payload.contains("call_id") && payload.at("call_id").is_string()) {
        parsed.call_id = payload.at("call_id").get<std::string>();
      }
      if(payload.contains("content") && payload.at("content").is_string()) {
        parsed.content = payload.at("content").get<std::string>();
      }
      parsed.is_error = payload.value("is_error", false);
    }
  } catch(const std::exception&) {
    // Treat the tool payload as plain text when it is not JSON.
  }

  if(message.tool_call_id.has_value() && !message.tool_call_id->empty()) {
    parsed.call_id = *message.tool_call_id;
  }

  return parsed;
}

[[nodiscard]] std::optional<types::TokenUsage> parse_usage(const nlohmann::json& payload) {
  if(!payload.contains("usage") || !payload.at("usage").is_object()) {
    return std::nullopt;
  }

  const auto& usage = payload.at("usage");
  types::TokenUsage parsed;
  parsed.input_tokens = usage.value("input_tokens", 0U);
  parsed.output_tokens = usage.value("output_tokens", 0U);
  parsed.cache_read_tokens = usage.value("cache_read_input_tokens", 0U);
  parsed.cache_creation_tokens = usage.value("cache_creation_input_tokens", 0U);

  if(
      parsed.input_tokens == 0 && parsed.output_tokens == 0 && parsed.cache_read_tokens == 0
      && parsed.cache_creation_tokens == 0
  ) {
    return std::nullopt;
  }

  return parsed;
}

}  // namespace

nlohmann::json build_messages_request(
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    std::uint32_t max_tokens,
    ThinkingConfig thinking,
    bool stream
) {
  nlohmann::json body{
      {"model", model},
      {"max_tokens", max_tokens},
      {"messages", nlohmann::json::array()},
  };

  std::string system_prompt;
  nlohmann::json pending_tool_results = nlohmann::json::array();
  auto flush_tool_results = [&] {
    if(pending_tool_results.empty()) {
      return;
    }
    body["messages"].push_back(nlohmann::json{
        {"role", "user"},
        {"content", std::move(pending_tool_results)},
    });
    pending_tool_results = nlohmann::json::array();
  };

  for(const auto& message : messages) {
    switch(message.role) {
      case types::Role::System: {
        flush_tool_results();
        if(message.content.empty()) {
          break;
        }
        if(!system_prompt.empty()) {
          system_prompt += "\n\n";
        }
        system_prompt += message.content;
        break;
      }
      case types::Role::User:
        flush_tool_results();
        body["messages"].push_back(nlohmann::json{{"role", "user"}, {"content", message.content}});
        break;
      case types::Role::Assistant: {
        flush_tool_results();
        nlohmann::json wire_message{{"role", "assistant"}};
        if(message.tool_calls.empty()) {
          wire_message["content"] = message.content;
        } else {
          wire_message["content"] = nlohmann::json::array();

          if(!message.content.empty()) {
            wire_message["content"].push_back(nlohmann::json{{"type", "text"}, {"text", message.content}});
          }

          for(std::size_t index = 0; index < message.tool_calls.size(); ++index) {
            const auto& tool_call = message.tool_calls.at(index);
            wire_message["content"].push_back(nlohmann::json{
                {"type", "tool_use"},
                {"id", tool_call.id.empty() ? fallback_tool_call_id(index) : tool_call.id},
                {"name", tool_call.name},
                {"input", coerce_tool_arguments(tool_call.arguments)},
            });
          }
        }

        body["messages"].push_back(std::move(wire_message));
        break;
      }
      case types::Role::Tool: {
        const auto parsed_tool_result = parse_tool_result(message);
        nlohmann::json block{
            {"type", "tool_result"},
            {"tool_use_id", parsed_tool_result.call_id},
            {"content", parsed_tool_result.content},
        };
        if(parsed_tool_result.is_error) {
          block["is_error"] = true;
        }

        pending_tool_results.push_back(std::move(block));
        break;
      }
    }
  }
  flush_tool_results();

  if(!system_prompt.empty()) {
    body["system"] = std::move(system_prompt);
  }

  if(!tools.empty()) {
    body["tools"] = tools_to_anthropic_format(tools);
  }

  if(stream) {
    body["stream"] = true;
  }

  // Thinking request-body parity remains outside this provider streaming milestone.
  (void)thinking;
  return body;
}

LlmResponse parse_messages_response(const nlohmann::json& payload) {
  if(!payload.contains("content")) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "anthropic",
        .message = "missing Anthropic content blocks",
    });
  }

  LlmResponse response;

  const auto parse_block = [&](const nlohmann::json& block, std::size_t index) {
    if(!block.is_object()) {
      return;
    }

    const auto type = block.value("type", std::string{});
    if(type == "text") {
      if(block.contains("text") && block.at("text").is_string()) {
        response.content += block.at("text").get<std::string>();
      }
      return;
    }

    if(type == "tool_use") {
      response.tool_calls.push_back(types::ToolCall{
          .id = block.value("id", fallback_tool_call_id(index)),
          .name = block.value("name", std::string{}),
          .arguments = block.contains("input") ? coerce_tool_arguments(block.at("input")) : nlohmann::json::object(),
      });
      return;
    }

    if(type == "thinking" && block.contains("thinking") && block.at("thinking").is_string()) {
      const auto thinking_text = block.at("thinking").get<std::string>();
      if(response.thinking.has_value()) {
        response.thinking = *response.thinking + thinking_text;
      } else {
        response.thinking = thinking_text;
      }
    }
  };

  const auto& content = payload.at("content");
  if(content.is_array()) {
    for(std::size_t index = 0; index < content.size(); ++index) {
      parse_block(content.at(index), index);
    }
  } else if(content.is_object()) {
    parse_block(content, 0);
  } else if(content.is_string()) {
    response.content = content.get<std::string>();
  }

  response.usage = parse_usage(payload);
  return response;
}

std::vector<types::StreamChunk> parse_stream_events(const nlohmann::json& payload) {
  std::vector<types::StreamChunk> chunks;
  const auto event_type = payload.value("type", std::string{});

  if(event_type == "message_start" || event_type == "content_block_stop" || event_type == "ping") {
    return chunks;
  }

  if(event_type == "content_block_delta") {
    if(!payload.contains("delta") || !payload.at("delta").is_object()) {
      return chunks;
    }

    const auto& delta = payload.at("delta");
    const auto delta_type = delta.value("type", std::string{});
    if(delta_type == "text_delta") {
      const auto text = delta.value("text", std::string{});
      if(!text.empty()) {
        chunks.push_back(types::StreamChunk::text(text));
      }
      return chunks;
    }

    if(delta_type == "thinking_delta") {
      const auto thinking = delta.value("thinking", std::string{});
      if(!thinking.empty()) {
        types::StreamChunk chunk;
        chunk.thinking = thinking;
        chunks.push_back(std::move(chunk));
      }
      return chunks;
    }

    if(delta_type == "input_json_delta") {
      const auto partial_json = delta.value("partial_json", std::string{});
      if(!partial_json.empty()) {
        types::StreamChunk chunk;
        chunk.tool_call = types::StreamToolCall{
            .index = payload.value("index", 0U),
            .id = std::nullopt,
            .name = std::nullopt,
            .arguments_delta = partial_json,
        };
        chunks.push_back(std::move(chunk));
      }
      return chunks;
    }

    return chunks;
  }

  if(event_type == "content_block_start") {
    if(!payload.contains("content_block") || !payload.at("content_block").is_object()) {
      return chunks;
    }

    const auto& block = payload.at("content_block");
    const auto block_type = block.value("type", std::string{});
    if(block_type == "text") {
      const auto text = block.value("text", std::string{});
      if(!text.empty()) {
        chunks.push_back(types::StreamChunk::text(text));
      }
      return chunks;
    }

    if(block_type == "thinking") {
      const auto thinking = block.value("thinking", std::string{});
      if(!thinking.empty()) {
        types::StreamChunk chunk;
        chunk.thinking = thinking;
        chunks.push_back(std::move(chunk));
      }
      return chunks;
    }

    if(block_type == "tool_use") {
      types::StreamChunk chunk;
      chunk.tool_call = types::StreamToolCall{
          .index = payload.value("index", 0U),
          .id = block.contains("id") && block.at("id").is_string()
                    ? std::optional<std::string>{block.at("id").get<std::string>()}
                    : std::nullopt,
          .name = block.contains("name") && block.at("name").is_string()
                      ? std::optional<std::string>{block.at("name").get<std::string>()}
                      : std::nullopt,
          .arguments_delta = std::nullopt,
      };
      chunks.push_back(std::move(chunk));
    }
    return chunks;
  }

  if(event_type == "message_delta") {
    auto usage = parse_usage(payload);
    if(
        !usage.has_value() && payload.contains("delta") && payload.at("delta").is_object()
        && payload.at("delta").contains("usage") && payload.at("delta").at("usage").is_object()
    ) {
      usage = parse_usage(nlohmann::json{{"usage", payload.at("delta").at("usage")}});
    }

    if(usage.has_value()) {
      chunks.push_back(types::StreamChunk::with_usage(*usage));
    }
    return chunks;
  }

  if(event_type == "message_stop") {
    chunks.push_back(types::StreamChunk::finished());
    return chunks;
  }

  if(event_type == "error") {
    std::string message = "Anthropic stream error";
    auto kind = ProviderErrorKind::Unknown;
    if(payload.contains("error") && payload.at("error").is_object()) {
      const auto& error = payload.at("error");
      message = error.value("message", message);
      const auto error_type = error.value("type", std::string{});
      if(error_type == "rate_limit_error") {
        kind = ProviderErrorKind::RateLimit;
      } else if(error_type == "overloaded_error" || error_type == "api_error") {
        kind = ProviderErrorKind::ServerError;
      } else if(error_type == "authentication_error" || error_type == "permission_error") {
        kind = ProviderErrorKind::AuthFailure;
      } else if(error_type == "not_found_error") {
        kind = ProviderErrorKind::ModelNotFound;
      } else if(error_type == "request_too_large") {
        kind = ProviderErrorKind::ContextWindowExceeded;
      }
    }
    throw ProviderException(ProviderError{
        .kind = kind,
        .provider = "anthropic",
        .message = std::move(message),
    });
  }

  return chunks;
}

std::optional<types::StreamChunk> parse_stream_event(const nlohmann::json& payload) {
  const auto chunks = parse_stream_events(payload);
  if(chunks.empty()) {
    return std::nullopt;
  }
  return chunks.front();
}

std::vector<nlohmann::json> tools_to_anthropic_format(const std::vector<types::Tool>& tools) {
  std::vector<nlohmann::json> output;
  output.reserve(tools.size());

  for(const auto& tool : tools) {
    const auto input_schema = tool.parameters.is_object() ? tool.parameters : nlohmann::json{{"type", "object"}};
    output.push_back(nlohmann::json{
        {"name", tool.name},
        {"description", tool.description},
        {"input_schema", input_schema},
    });
  }

  return output;
}

}  // namespace ava::llm::anthropic
