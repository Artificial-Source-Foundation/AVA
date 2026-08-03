#include "sys.h"
#include "ava/config/builtin_generic_providers.h"
#include "ava/config/model_profiles.h"
#include "ava/config/provider_profiles.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::config {
namespace {

// Model metadata is manually maintained. When changing pricing, context windows,
// or max-output values, cross-check the Pi reference catalog under
// docs/reference-code/pi/packages/ai/src/providers/ and add focused assertions.

ModelInfo text_model(std::string provider_id, std::string model_id, std::string display_name, std::string family,
                     std::optional<long long> context_window_tokens, std::optional<long long> max_output_tokens, std::optional<ModelPricing> pricing,
                     ProviderProfile const& provider, std::optional<bool> supports_reasoning, std::vector<std::string> reasoning_levels = {},
                     std::string reasoning_format = {}, std::vector<std::string> compatibility_quirks = {},
                     std::vector<ModelReasoningLevelMapping> reasoning_level_mappings = {})
{
  return ModelInfo{.provider_id = std::move(provider_id),
                   .model_id = std::move(model_id),
                   .display_name = std::move(display_name),
                   .display_name_is_configured = true,
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
                   .reasoning_format = std::move(reasoning_format),
                   .reasoning_level_mappings = std::move(reasoning_level_mappings)};
}

ModelInfo reasoning_model(std::string model_id, std::string display_name, std::string family, std::optional<long long> context_window_tokens,
                          std::optional<long long> max_output_tokens, ProviderProfile const& provider, std::vector<std::string> compatibility_quirks = {},
                          std::optional<ModelPricing> pricing = std::nullopt, std::vector<ModelReasoningLevelMapping> reasoning_level_mappings = {},
                          std::vector<std::string> reasoning_levels = {})
{
  if (compatibility_quirks.empty())
    compatibility_quirks = provider.default_compatibility_quirks;
  if (reasoning_levels.empty())
    reasoning_levels = provider.default_reasoning_levels;
  return text_model(provider.provider_id, std::move(model_id), std::move(display_name), std::move(family), context_window_tokens, max_output_tokens,
                    std::move(pricing), provider, true, std::move(reasoning_levels), provider.default_reasoning_format, std::move(compatibility_quirks),
                    std::move(reasoning_level_mappings));
}

ModelInfo image_capable(ModelInfo model)
{
  model.input_modalities = {"text", "image"};
  return model;
}

ModelReasoningLevelMapping mapped_reasoning_level(std::string level, std::string provider_level)
{
  return ModelReasoningLevelMapping{.level = std::move(level), .provider_level = std::move(provider_level), .supported = true};
}

ModelReasoningLevelMapping omitted_reasoning_level(std::string level)
{
  return ModelReasoningLevelMapping{.level = std::move(level), .provider_level = std::nullopt, .supported = true};
}

ModelReasoningLevelMapping blocked_reasoning_level(std::string level)
{
  return ModelReasoningLevelMapping{.level = std::move(level), .provider_level = std::nullopt, .supported = false};
}

std::vector<ModelReasoningLevelMapping> gpt55_reasoning_level_mappings()
{
  return {mapped_reasoning_level("off", "none"),      blocked_reasoning_level("minimal"),     mapped_reasoning_level("low", "low"),
          mapped_reasoning_level("medium", "medium"), mapped_reasoning_level("high", "high"), mapped_reasoning_level("xhigh", "xhigh")};
}

std::vector<ModelReasoningLevelMapping> gpt56_reasoning_level_mappings()
{
  return {mapped_reasoning_level("off", "none"),      mapped_reasoning_level("minimal", "low"), mapped_reasoning_level("low", "low"),
          mapped_reasoning_level("medium", "medium"), mapped_reasoning_level("high", "high"),   mapped_reasoning_level("xhigh", "xhigh"),
          mapped_reasoning_level("max", "max")};
}

std::vector<ModelReasoningLevelMapping> deepseek_v4_reasoning_level_mappings()
{
  return {omitted_reasoning_level("off"),    blocked_reasoning_level("minimal"),     blocked_reasoning_level("low"),
          blocked_reasoning_level("medium"), mapped_reasoning_level("high", "high"), mapped_reasoning_level("xhigh", "max")};
}

std::vector<ModelReasoningLevelMapping> anthropic_enabled_reasoning_level_mappings()
{
  return {omitted_reasoning_level("off"), mapped_reasoning_level("enabled", "enabled"), blocked_reasoning_level("adaptive")};
}

std::vector<ModelReasoningLevelMapping> blocked_provider_reasoning_level_mappings()
{
  return {omitted_reasoning_level("off"),    blocked_reasoning_level("minimal"), blocked_reasoning_level("low"),
          blocked_reasoning_level("medium"), blocked_reasoning_level("high"),    blocked_reasoning_level("xhigh")};
}

std::vector<ModelReasoningLevelMapping> zai_glm52_reasoning_level_mappings()
{
  // AVA minimal enables thinking without reasoning_effort; low/medium/high map to
  // provider effort high; xhigh maps to max.
  return {omitted_reasoning_level("off"),           omitted_reasoning_level("minimal"),     mapped_reasoning_level("low", "high"),
          mapped_reasoning_level("medium", "high"), mapped_reasoning_level("high", "high"), mapped_reasoning_level("xhigh", "max")};
}

ModelPricing coding_plan_zero_pricing()
{
  return ModelPricing{.input_per_million = 0.0L,
                      .output_per_million = 0.0L,
                      .cache_read_per_million = 0.0L,
                      .cache_write_per_million = std::nullopt,
                      .reasoning_per_million = std::nullopt};
}

std::vector<std::string> zai_model_quirks(ProviderProfile const& provider, bool tool_stream, bool reasoning_effort)
{
  auto quirks = provider.default_compatibility_quirks;
  if (tool_stream)
    quirks.emplace_back("tool_stream");
  if (reasoning_effort)
    quirks.emplace_back("zai_reasoning_effort");
  return quirks;
}

std::vector<ModelInfo> zai_coding_plan_models(ProviderProfile const& provider)
{
  auto const pricing = coding_plan_zero_pricing();
  return {
      reasoning_model("glm-4.5-air", "GLM-4.5-Air", "glm-4.5", 131'072, 98'304, provider, zai_model_quirks(provider, false, false), pricing),
      reasoning_model("glm-4.7", "GLM-4.7", "glm-4.7", 204'800, 131'072, provider, zai_model_quirks(provider, true, false), pricing),
      reasoning_model("glm-5-turbo", "GLM-5-Turbo", "glm-5", 200'000, 131'072, provider, zai_model_quirks(provider, true, false), pricing),
      reasoning_model("glm-5.1", "GLM-5.1", "glm-5.1", 200'000, 131'072, provider, zai_model_quirks(provider, true, false), pricing),
      reasoning_model("glm-5.2", "GLM-5.2", "glm-5.2", 1'000'000, 131'072, provider, zai_model_quirks(provider, true, true), pricing,
                      zai_glm52_reasoning_level_mappings(), {"minimal", "low", "medium", "high", "xhigh"}),
      image_capable(reasoning_model("glm-5v-turbo", "GLM-5V-Turbo", "glm-5v", 200'000, 131'072, provider, zai_model_quirks(provider, true, false), pricing))};
}

}  // namespace

ModelRegistry builtin_model_profiles()
{
  auto const& openai = openai_provider_profile();
  auto const& anthropic = anthropic_provider_profile();
  auto const& deepseek = deepseek_provider_profile();
  auto const& gemini = gemini_provider_profile();
  auto const& kimi = kimi_provider_profile();
  auto const& moonshot = moonshot_provider_profile();
  auto const& openrouter = openrouter_provider_profile();
  auto const& zai = zai_provider_profile();
  auto const& zai_coding_cn = zai_coding_cn_provider_profile();

  std::vector<ModelInfo> models = {
      image_capable(reasoning_model("gpt-5.5", "GPT-5.5", "gpt-5", 272'000, 128'000, openai, {},
                                    ModelPricing{.input_per_million = 5.0L,
                                                 .output_per_million = 30.0L,
                                                 .cache_read_per_million = 0.50L,
                                                 .cache_write_per_million = std::nullopt,
                                                 .reasoning_per_million = std::nullopt},
                                    gpt55_reasoning_level_mappings())),
      image_capable(reasoning_model("gpt-5.6-sol", "GPT-5.6 Sol", "gpt-5.6", 272'000, 128'000, openai, {},
                                    ModelPricing{.input_per_million = 5.0L,
                                                 .output_per_million = 30.0L,
                                                 .cache_read_per_million = 0.50L,
                                                 .cache_write_per_million = 6.25L,
                                                 .reasoning_per_million = std::nullopt},
                                    gpt56_reasoning_level_mappings(), {"minimal", "low", "medium", "high", "xhigh", "max"})),
      image_capable(reasoning_model("gpt-5.6-terra", "GPT-5.6 Terra", "gpt-5.6", 272'000, 128'000, openai, {},
                                    ModelPricing{.input_per_million = 2.5L,
                                                 .output_per_million = 15.0L,
                                                 .cache_read_per_million = 0.25L,
                                                 .cache_write_per_million = 3.125L,
                                                 .reasoning_per_million = std::nullopt},
                                    gpt56_reasoning_level_mappings(), {"minimal", "low", "medium", "high", "xhigh", "max"})),
      image_capable(reasoning_model("gpt-5.6-luna", "GPT-5.6 Luna", "gpt-5.6", 272'000, 128'000, openai, {},
                                    ModelPricing{.input_per_million = 1.0L,
                                                 .output_per_million = 6.0L,
                                                 .cache_read_per_million = 0.10L,
                                                 .cache_write_per_million = 1.25L,
                                                 .reasoning_per_million = std::nullopt},
                                    gpt56_reasoning_level_mappings(), {"minimal", "low", "medium", "high", "xhigh", "max"})),
      image_capable(text_model(openai.provider_id, "gpt-4.1-mini", "GPT-4.1 mini", "gpt-4.1", 1'047'576, 32'768,
                               ModelPricing{.input_per_million = 0.40L,
                                            .output_per_million = 1.60L,
                                            .cache_read_per_million = 0.10L,
                                            .cache_write_per_million = std::nullopt,
                                            .reasoning_per_million = std::nullopt},
                               openai, false)),
      image_capable(text_model(anthropic.provider_id, "claude-sonnet-4-5", "Claude Sonnet 4.5", "claude-sonnet", 200'000, 64'000,
                               ModelPricing{.input_per_million = 3.0L,
                                            .output_per_million = 15.0L,
                                            .cache_read_per_million = 0.30L,
                                            .cache_write_per_million = std::nullopt,
                                            .reasoning_per_million = std::nullopt},
                               anthropic, true, {"enabled"}, anthropic.default_reasoning_format, anthropic.default_compatibility_quirks,
                               anthropic_enabled_reasoning_level_mappings())),
      text_model(deepseek.provider_id, "deepseek-v4-flash", "DeepSeek V4 Flash", "deepseek-v4", 1'000'000, 384'000,
                 ModelPricing{.input_per_million = 0.14L,
                              .output_per_million = 0.28L,
                              .cache_read_per_million = 0.0028L,
                              .cache_write_per_million = std::nullopt,
                              .reasoning_per_million = std::nullopt},
                 deepseek, true, deepseek.default_reasoning_levels, deepseek.default_reasoning_format, deepseek.default_compatibility_quirks,
                 deepseek_v4_reasoning_level_mappings()),
      text_model(deepseek.provider_id, "deepseek-v4-pro", "DeepSeek V4 Pro", "deepseek-v4", 1'000'000, 384'000,
                 ModelPricing{.input_per_million = 0.435L,
                              .output_per_million = 0.87L,
                              .cache_read_per_million = 0.003625L,
                              .cache_write_per_million = std::nullopt,
                              .reasoning_per_million = std::nullopt},
                 deepseek, true, deepseek.default_reasoning_levels, deepseek.default_reasoning_format, deepseek.default_compatibility_quirks,
                 deepseek_v4_reasoning_level_mappings()),
      image_capable(text_model(gemini.provider_id, "gemini-2.5-pro", "Gemini 2.5 Pro", "gemini-2.5", 1'048'576, 65'536, std::nullopt, gemini, false, {}, {},
                               gemini.default_compatibility_quirks, blocked_provider_reasoning_level_mappings())),
      reasoning_model("kimi-k2-thinking", "Kimi K2 Thinking", "kimi-thinking", 262'144, 32'768, kimi, {},
                      ModelPricing{.input_per_million = 0.0L,
                                   .output_per_million = 0.0L,
                                   .cache_read_per_million = 0.0L,
                                   .cache_write_per_million = std::nullopt,
                                   .reasoning_per_million = std::nullopt}),
      reasoning_model("kimi-for-coding", "Kimi For Coding", "kimi-coding", 262'144, 32'768, kimi, {},
                      ModelPricing{.input_per_million = 0.0L,
                                   .output_per_million = 0.0L,
                                   .cache_read_per_million = 0.0L,
                                   .cache_write_per_million = std::nullopt,
                                   .reasoning_per_million = std::nullopt}),
      reasoning_model("kimi-k2.6", "Kimi K2.6", "kimi-thinking", 262'144, 262'144, moonshot, {},
                      ModelPricing{.input_per_million = 0.95L,
                                   .output_per_million = 4.0L,
                                   .cache_read_per_million = 0.16L,
                                   .cache_write_per_million = std::nullopt,
                                   .reasoning_per_million = std::nullopt}),
      text_model(openrouter.provider_id, "moonshotai/kimi-k2.6", "Kimi K2.6 (OpenRouter)", "kimi-thinking", 262'144, 262'144, std::nullopt, openrouter, false,
                 {}, {}, openrouter.default_compatibility_quirks, blocked_provider_reasoning_level_mappings())};

  for (auto& model : zai_coding_plan_models(zai)) models.push_back(std::move(model));
  for (auto& model : zai_coding_plan_models(zai_coding_cn)) models.push_back(std::move(model));
  // Declarative generic built-ins (xAI, Groq, Cerebras, Together, Fireworks, Mistral).
  for (auto& model : builtin_generic_model_infos()) models.push_back(std::move(model));

  return ModelRegistry{.default_provider_id = openai.provider_id, .default_model_id = "gpt-5.5", .models = std::move(models)};
}

std::string model_display_label(std::string_view provider_id, std::string_view model_id)
{
  for (auto const& model : builtin_model_profiles().models)
  {
    if (model.provider_id == provider_id && model.model_id == model_id && !model.display_name.empty())
    {
      return model.display_name;
    }
  }

  std::string label(model_id);
  if (label.rfind("gpt-", 0) == 0)
    label.replace(0, 3, "GPT");
  return label;
}

std::string model_display_label(std::string_view model_id)
{
  for (auto const& model : builtin_model_profiles().models)
  {
    if (model.model_id == model_id && !model.display_name.empty())
      return model.display_name;
  }
  return model_display_label({}, model_id);
}

}  // namespace ava::config
