#pragma once

#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::provider::detail {

[[nodiscard]] bool valid_anthropic_cache_control_ttl(std::string_view ttl);
[[nodiscard]] bool valid_anthropic_reasoning_type(std::string_view type);
[[nodiscard]] bool valid_anthropic_reasoning_display(std::string_view display);

[[nodiscard]] ava::core::VoidResult validate_anthropic_content_parts(std::vector<ChatMessage> const& messages);
[[nodiscard]] ava::core::VoidResult validate_anthropic_request_options(ProviderRequest const& request);
[[nodiscard]] ava::core::VoidResult validate_anthropic_cache_control_order(ProviderRequest const& request,
                                                                           std::vector<ChatMessage> const& messages);

}  // namespace ava::provider::detail
