#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::provider::detail {

inline constexpr int kDefaultAnthropicMaxTokens = 4096;

[[nodiscard]] std::string anthropic_message_role(ChatMessage const& message);
[[nodiscard]] std::vector<ContentPart> anthropic_message_content_parts(ChatMessage const& message);
[[nodiscard]] ContentPart anthropic_text_separator_part();
void append_anthropic_content_parts(std::vector<ContentPart>& target, std::vector<ContentPart> const& source,
                                    bool separate_text);
[[nodiscard]] std::vector<ChatMessage> collapse_consecutive_anthropic_roles(std::vector<ChatMessage> const& messages);

[[nodiscard]] bool valid_anthropic_cache_control_ttl(std::string_view ttl);
[[nodiscard]] bool valid_anthropic_reasoning_type(std::string_view type);
[[nodiscard]] bool valid_anthropic_reasoning_display(std::string_view display);
[[nodiscard]] long long anthropic_max_tokens_for_request(ProviderRequest const& request);

[[nodiscard]] ava::core::VoidResult validate_anthropic_content_parts(std::vector<ChatMessage> const& messages);
[[nodiscard]] ava::core::VoidResult validate_anthropic_request_options(ProviderRequest const& request);
[[nodiscard]] ava::core::VoidResult validate_anthropic_cache_control_order(ProviderRequest const& request,
                                                                           std::vector<ChatMessage> const& messages);

[[nodiscard]] std::string anthropic_content_part_json(ContentPart const& part);
[[nodiscard]] std::string anthropic_system_prompt_json(ProviderRequest const& request);
[[nodiscard]] std::string anthropic_reasoning_options_json(ProviderReasoningOptions const& reasoning);
[[nodiscard]] std::string anthropic_tool_json(std::string_view tool_json);
[[nodiscard]] std::string anthropic_request_body_json_unchecked(ProviderRequest const& request,
                                                                std::vector<ChatMessage> const& messages);

}  // namespace ava::provider::detail
