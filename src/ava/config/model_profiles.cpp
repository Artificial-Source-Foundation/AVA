#include "ava/config/model_profiles.h"

#include "ava/config/provider_profiles.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::config {
namespace {

ModelInfo text_model(std::string provider_id, std::string model_id, std::string display_name, std::string family,
                     std::optional<long long> context_window_tokens, std::optional<long long> max_output_tokens,
                     std::optional<ModelPricing> pricing, ProviderProfile const& provider,
                     std::optional<bool> supports_reasoning, std::vector<std::string> reasoning_levels = {},
                     std::string reasoning_format = {}, std::vector<std::string> compatibility_quirks = {})
{
  return ModelInfo{.provider_id = std::move(provider_id),
                   .model_id = std::move(model_id),
                   .display_name = std::move(display_name),
                   .family = std::move(family),
                   .context_window_tokens = context_window_tokens,
                   .max_output_tokens = max_output_tokens,
                   .pricing = std::move(pricing),
                   .api_family = provider.api_family,
                   .input_modalities = {"text"},
                   .supports_tools = true,
                   .supports_streaming = true,
                   .supports_reasoning = supports_reasoning,
                   .reports_usage = true,
                   .reasoning_levels = std::move(reasoning_levels),
                   .compatibility_quirks = std::move(compatibility_quirks),
                   .output_modalities = {"text"},
                   .reasoning_format = std::move(reasoning_format)};
}

ModelInfo reasoning_model(std::string model_id, std::string display_name, std::string family,
                          std::optional<long long> context_window_tokens, std::optional<long long> max_output_tokens,
                          ProviderProfile const& provider, std::vector<std::string> compatibility_quirks = {})
{
  if (compatibility_quirks.empty()) compatibility_quirks = provider.default_compatibility_quirks;
  return text_model(provider.provider_id, std::move(model_id), std::move(display_name), std::move(family),
                    context_window_tokens, max_output_tokens, std::nullopt, provider, true,
                    provider.default_reasoning_levels, provider.default_reasoning_format,
                    std::move(compatibility_quirks));
}

}  // namespace

ModelRegistry builtin_model_profiles()
{
  auto const& openai = openai_provider_profile();
  auto const& anthropic = anthropic_provider_profile();
  auto const& kimi = kimi_provider_profile();
  auto const& moonshot = moonshot_provider_profile();
  auto const& openrouter = openrouter_provider_profile();

  return ModelRegistry{
      .default_provider_id = openai.provider_id,
      .default_model_id = "gpt-5.5",
      .models = {reasoning_model("gpt-5.5", "GPT-5.5", "gpt-5", 200'000, std::nullopt, openai, {}),
                 text_model(openai.provider_id, "gpt-4.1-mini", "GPT-4.1 mini", "gpt-4.1", 1'048'576, 32'768,
                            ModelPricing{.input_per_million = 0.40L,
                                         .output_per_million = 1.60L,
                                         .cache_read_per_million = 0.10L,
                                         .cache_write_per_million = std::nullopt,
                                         .reasoning_per_million = std::nullopt},
                            openai, false),
                 text_model(anthropic.provider_id, "claude-sonnet-4-5", "Claude Sonnet 4.5", "claude-sonnet", 200'000,
                            64'000, std::nullopt, anthropic, false, {}, {}, anthropic.default_compatibility_quirks),
                 reasoning_model("kimi-k2-thinking", "Kimi K2 Thinking", "kimi-thinking", 262'144, 32'768, kimi),
                 reasoning_model("kimi-for-coding", "Kimi For Coding", "kimi-coding", 262'144, 32'768, kimi),
                 reasoning_model("kimi-k2.6", "Kimi K2.6", "kimi-thinking", 262'144, 32'768, moonshot),
                 reasoning_model("moonshotai/kimi-k2.6", "Kimi K2.6 (OpenRouter)", "kimi-thinking", 262'144, 32'768,
                                 openrouter)}};
}

std::string model_display_label(std::string_view provider_id, std::string_view model_id)
{
  for (auto const& model : builtin_model_profiles().models) {
    if (model.provider_id == provider_id && model.model_id == model_id && !model.display_name.empty()) {
      return model.display_name;
    }
  }

  std::string label(model_id);
  if (label.rfind("gpt-", 0) == 0) label.replace(0, 3, "GPT");
  return label;
}

std::string model_display_label(std::string_view model_id)
{
  for (auto const& model : builtin_model_profiles().models) {
    if (model.model_id == model_id && !model.display_name.empty()) return model.display_name;
  }
  return model_display_label({}, model_id);
}

}  // namespace ava::config
