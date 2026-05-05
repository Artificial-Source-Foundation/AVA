#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/openai_compatible_tool_schema.h"
#include "ava/provider/provider.h"

namespace ava::provider {

struct OpenAICompatibleProviderOptions {
  std::string base_url = "https://api.openai.com";
  std::string chat_completions_path = "/v1/chat/completions";
  std::string provider_name = "OpenAI-compatible";
  std::string reasoning_format = "reasoning_content";
  std::string user_agent = {};
  std::optional<double> default_temperature = std::nullopt;
  std::string reasoning_request_field = "thinking";
  bool preserve_reasoning_content = false;
  bool include_stream_usage = false;
};

[[nodiscard]] std::string openai_compatible_join_url(std::string_view base_url, std::string_view path);
[[nodiscard]] std::string openai_compatible_temperature_json(double value);
[[nodiscard]] std::vector<std::string> openai_compatible_chat_messages_for_message(ChatMessage const& message,
                                                                                   std::string_view reasoning_format,
                                                                                   bool preserve_reasoning_content);
[[nodiscard]] std::string openai_compatible_request_body_json(ProviderRequest const& request,
                                                              OpenAICompatibleProviderOptions const& options);

}  // namespace ava::provider
