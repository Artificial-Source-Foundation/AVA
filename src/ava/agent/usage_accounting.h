#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ava/agent/assistant_turn.h"
#include "ava/provider/provider.h"

namespace ava::agent {

[[nodiscard]] std::string usage_json(const ava::provider::TokenUsage& usage,
                                     const std::optional<long double>& cost_usd);
[[nodiscard]] ava::provider::TokenUsage with_total_tokens(ava::provider::TokenUsage usage);
[[nodiscard]] ava::provider::TokenUsage estimate_usage_from_turn(std::string_view request_body,
                                                                 const ParsedAssistantTurn& turn);
void accumulate_usage(std::optional<ava::provider::TokenUsage>& total, const ava::provider::TokenUsage& usage);

}  // namespace ava::agent
