#include "ava/llm/providers/openai_protocol.hpp"

#include <random>

namespace ava::llm::openai {
namespace {

[[nodiscard]] std::string make_fallback_tool_call_id() {
  thread_local std::mt19937 generator{std::random_device{}()};
  std::uniform_int_distribution<int> distribution(0, 999999);
  return "call_" + std::to_string(distribution(generator));
}

[[nodiscard]] std::string role_to_wire(types::Role role) {
  return std::string(ava::types::role_to_string(role));
}

[[nodiscard]] std::string thinking_effort(types::ThinkingLevel level) {
  switch(level) {
    case types::ThinkingLevel::Off:
      return "low";
    case types::ThinkingLevel::Low:
      return "low";
    case types::ThinkingLevel::Medium:
      return "medium";
    case types::ThinkingLevel::High:
      return "high";
    case types::ThinkingLevel::Max:
      return "xhigh";
  }
  return "low";
}

[[nodiscard]] nlohmann::json message_to_wire(const ChatMessage& message) {
  nlohmann::json payload{
      {"role", role_to_wire(message.role)},
      {"content", message.content},
  };

  if(!message.tool_calls.empty()) {
    payload["tool_calls"] = nlohmann::json::array();
    for(const auto& call : message.tool_calls) {
      payload["tool_calls"].push_back(nlohmann::json{
          {"id", call.id},
          {"type", "function"},
          {"function", nlohmann::json{{"name", call.name}, {"arguments", call.arguments.dump()}}},
      });
    }
  }

  if(message.role == types::Role::Tool && message.tool_call_id.has_value()) {
    payload["tool_call_id"] = *message.tool_call_id;
  }

  return payload;
}

[[nodiscard]] std::optional<types::TokenUsage> parse_usage(const nlohmann::json& payload) {
  if(!payload.contains("usage") || !payload.at("usage").is_object()) {
    return std::nullopt;
  }

  const auto& usage = payload.at("usage");
  types::TokenUsage parsed;
  parsed.input_tokens = usage.value("prompt_tokens", 0U);
  parsed.output_tokens = usage.value("completion_tokens", 0U);

  if(usage.contains("prompt_tokens_details") && usage.at("prompt_tokens_details").is_object()) {
    parsed.cache_read_tokens = usage.at("prompt_tokens_details").value("cached_tokens", 0U);
  }

  if(parsed.input_tokens == 0 && parsed.output_tokens == 0 && parsed.cache_read_tokens == 0) {
    return std::nullopt;
  }

  return parsed;
}

}  // namespace

nlohmann::json build_chat_completions_request(
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    bool stream,
    ThinkingConfig thinking
) {
  nlohmann::json body{
      {"model", model},
      {"stream", stream},
      {"messages", nlohmann::json::array()},
  };

  for(const auto& message : messages) {
    body["messages"].push_back(message_to_wire(message));
  }

  if(!tools.empty()) {
    body["tools"] = tools_to_openai_format(tools);
  }

  if(stream) {
    body["stream_options"] = nlohmann::json{{"include_usage", true}};
  }

  if(thinking.is_enabled()) {
    body["reasoning_effort"] = thinking_effort(thinking.level);
  }

  return body;
}

LlmResponse parse_chat_completion_response(const nlohmann::json& payload) {
  if(!payload.contains("choices") || !payload.at("choices").is_array() || payload.at("choices").empty()) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "openai",
        .message = "missing OpenAI completion choices",
    });
  }

  const auto& choice = payload.at("choices").at(0);
  const auto& message = choice.contains("message") ? choice.at("message") : nlohmann::json::object();

  LlmResponse response;
  response.content = message.value("content", "");

  if(message.contains("tool_calls") && message.at("tool_calls").is_array()) {
    for(const auto& tc : message.at("tool_calls")) {
      if(!tc.contains("function") || !tc.at("function").is_object()) {
        continue;
      }
      const auto& function = tc.at("function");
      const auto raw_arguments = function.value("arguments", std::string{"{}"});

      nlohmann::json arguments = nlohmann::json::object();
      try {
        arguments = nlohmann::json::parse(raw_arguments);
      } catch(const std::exception&) {
        arguments = nlohmann::json::object();
      }

      response.tool_calls.push_back(types::ToolCall{
          .id = tc.value("id", make_fallback_tool_call_id()),
          .name = function.value("name", std::string{}),
          .arguments = std::move(arguments),
      });
    }
  }

  response.usage = parse_usage(payload);
  if(message.contains("reasoning_content") && message.at("reasoning_content").is_string()) {
    response.thinking = message.at("reasoning_content").get<std::string>();
  }

  return response;
}

std::optional<types::StreamChunk> parse_stream_event(const nlohmann::json& payload) {
  if(const auto usage = parse_usage(payload); usage.has_value()) {
    types::StreamChunk usage_chunk;
    usage_chunk.usage = usage;
    usage_chunk.done = true;
    return usage_chunk;
  }

  if(!payload.contains("choices") || !payload.at("choices").is_array() || payload.at("choices").empty()) {
    return std::nullopt;
  }

  const auto& choice = payload.at("choices").at(0);
  if(!choice.contains("delta") || !choice.at("delta").is_object()) {
    return std::nullopt;
  }

  const auto& delta = choice.at("delta");
  types::StreamChunk chunk;
  bool has_data = false;

  if(delta.contains("content") && delta.at("content").is_string()) {
    const auto content = delta.at("content").get<std::string>();
    if(!content.empty()) {
      chunk.content = content;
      has_data = true;
    }
  }

  if(delta.contains("reasoning_content") && delta.at("reasoning_content").is_string()) {
    const auto thinking = delta.at("reasoning_content").get<std::string>();
    if(!thinking.empty()) {
      chunk.thinking = thinking;
      has_data = true;
    }
  }

  if(delta.contains("tool_calls") && delta.at("tool_calls").is_array() && !delta.at("tool_calls").empty()) {
    const auto& tc = delta.at("tool_calls").at(0);
    types::StreamToolCall stream_tool_call;
    stream_tool_call.index = tc.value("index", 0U);
    if(tc.contains("id") && tc.at("id").is_string()) {
      stream_tool_call.id = tc.at("id").get<std::string>();
    }
    if(tc.contains("function") && tc.at("function").is_object()) {
      const auto& fn = tc.at("function");
      if(fn.contains("name") && fn.at("name").is_string()) {
        stream_tool_call.name = fn.at("name").get<std::string>();
      }
      if(fn.contains("arguments") && fn.at("arguments").is_string()) {
        stream_tool_call.arguments_delta = fn.at("arguments").get<std::string>();
      }
    }
    chunk.tool_call = stream_tool_call;
    has_data = true;
  }

  if(choice.contains("finish_reason") && choice.at("finish_reason").is_string()) {
    const auto reason = choice.at("finish_reason").get<std::string>();
    if(reason == "stop" || reason == "tool_calls") {
      chunk.done = true;
      has_data = true;
    }
  }

  if(!has_data) {
    return std::nullopt;
  }
  return chunk;
}

std::vector<nlohmann::json> tools_to_openai_format(const std::vector<types::Tool>& tools) {
  std::vector<nlohmann::json> output;
  output.reserve(tools.size());
  for(const auto& tool : tools) {
    output.push_back(nlohmann::json{
        {"type", "function"},
        {
            "function",
            nlohmann::json{{"name", tool.name}, {"description", tool.description}, {"parameters", tool.parameters}},
        },
    });
  }
  return output;
}

}  // namespace ava::llm::openai
