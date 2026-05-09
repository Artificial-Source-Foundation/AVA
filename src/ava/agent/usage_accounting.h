#pragma once

#include "ava/agent/assistant_turn.h"
#include "ava/provider/provider.h"

#include <optional>
#include <string>
#include <string_view>

namespace ava::agent {

[[nodiscard]] std::string usage_json(ava::provider::TokenUsage const& usage, std::optional<long double> const& cost_usd);
[[nodiscard]] ava::provider::TokenUsage with_total_tokens(ava::provider::TokenUsage usage);
[[nodiscard]] ava::provider::TokenUsage estimate_usage_from_turn(std::string_view request_body, ParsedAssistantTurn const& turn);
void accumulate_usage(std::optional<ava::provider::TokenUsage>& total, ava::provider::TokenUsage const& usage);

}  // namespace ava::agent
