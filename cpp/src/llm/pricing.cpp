#include "ava/llm/pricing.hpp"

#include <algorithm>
#include <cctype>

#include "ava/config/model_registry.hpp"

namespace ava::llm {
namespace {

struct PricePoint {
  double input_per_million;
  double output_per_million;
};

[[nodiscard]] std::string lower(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

}  // namespace

std::size_t estimate_tokens(std::string_view input) {
  if(input.empty()) {
    return 0;
  }
  return std::max<std::size_t>(1, (input.size() + 3U) / 4U);
}

double estimate_cost_usd(
    std::string_view provider,
    std::string_view model,
    std::size_t input_tokens,
    std::size_t output_tokens,
    bool subscription_billed
) {
  if(subscription_billed) {
    return 0.0;
  }

  PricePoint point{1.0, 4.0};

  if(const auto* registered = ava::config::registry().find_for_provider(std::string(provider), std::string(model));
     registered != nullptr) {
    point = PricePoint{registered->cost.input_per_million, registered->cost.output_per_million};
  } else if(const auto registered_price = ava::config::registry().pricing(std::string(model)); registered_price.has_value()) {
    point = PricePoint{registered_price->first, registered_price->second};
  } else {
    const auto provider_lower = lower(provider);
    if(provider_lower != "openai") {
      return static_cast<double>(input_tokens + output_tokens) * 0.0000005;
    }
  }

  const auto input_cost = (static_cast<double>(input_tokens) / 1'000'000.0) * point.input_per_million;
  const auto output_cost = (static_cast<double>(output_tokens) / 1'000'000.0) * point.output_per_million;
  return input_cost + output_cost;
}

}  // namespace ava::llm
