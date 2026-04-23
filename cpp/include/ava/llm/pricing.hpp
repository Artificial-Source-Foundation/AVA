#pragma once

#include <cstddef>
#include <string_view>

namespace ava::llm {

[[nodiscard]] std::size_t estimate_tokens(std::string_view input);
[[nodiscard]] double estimate_cost_usd(
    std::string_view provider,
    std::string_view model,
    std::size_t input_tokens,
    std::size_t output_tokens,
    bool subscription_billed = false
);

}  // namespace ava::llm
