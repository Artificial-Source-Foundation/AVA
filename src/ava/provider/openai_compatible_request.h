#pragma once

#include "ava/core/result.h"

#include <string>
#include <string_view>

namespace ava::provider {

struct OpenAICompatibleProviderOptions;
struct ProviderRequest;

[[nodiscard]] std::string join_openai_compatible_url(std::string_view base_url, std::string_view path);
[[nodiscard]] ava::core::VoidResult validate_openai_compatible_tools_json(ProviderRequest const& request);
[[nodiscard]] ava::core::Result<std::string> chat_completion_tool_json(std::string_view schema);
[[nodiscard]] std::string openai_compatible_request_body_json(ProviderRequest const& request, OpenAICompatibleProviderOptions const& options);

}  // namespace ava::provider
