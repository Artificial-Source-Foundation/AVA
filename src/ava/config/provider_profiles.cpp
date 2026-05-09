#include "ava/config/provider_profiles.h"

#include "ava/config/model_config.h"
#include "ava/config/reasoning_profiles.h"

#include "ava/core/error.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ava::config {
namespace {

constexpr long long kDefaultAnthropicThinkingBudgetTokens = 4096;
constexpr long long kMinimumAnthropicThinkingBudgetTokens = 1024;

std::optional<ProviderProfile> fallback_profile_for_api_family(std::string_view api_family,
                                                               std::string_view reasoning_format)
{
  if (api_family == openai_responses_reasoning_profile().api_family) {
    return openai_provider_profile();
  }
  if (api_family == openai_compatible_reasoning_content_profile().api_family &&
      (reasoning_format.empty() || reasoning_format == openai_compatible_reasoning_content_profile().format)) {
    return ProviderProfile{
        .provider_id = "openai-compatible",
        .display_name = "OpenAI-compatible",
        .api_family = openai_compatible_reasoning_content_profile().api_family,
        .default_reasoning_levels = openai_compatible_reasoning_content_profile().levels,
        .default_reasoning_format = openai_compatible_reasoning_content_profile().format,
        .reasoning_request_parameters = openai_compatible_reasoning_content_profile().request_parameters,
        .runtime_selectable = false};
  }
  if (api_family == anthropic_thinking_reasoning_profile().api_family) {
    return anthropic_provider_profile();
  }
  return std::nullopt;
}

bool display_value_allowed(ProviderProfile const& profile, std::string_view display)
{
  if (display.empty()) return true;
  return std::ranges::find(profile.reasoning_display_values, display) != profile.reasoning_display_values.end();
}

bool has_compatibility_quirk(ModelInfo const& model, std::string_view quirk)
{
  return std::ranges::find(model.compatibility_quirks, quirk) != model.compatibility_quirks.end();
}

}  // namespace

ProviderProfile const& anthropic_provider_profile()
{
  static ProviderProfile const profile{
      .provider_id = "anthropic",
      .display_name = "Anthropic",
      .connect_detail = "Claude API key; OAuth bearer via env or auth.json",
      .api_family = anthropic_thinking_reasoning_profile().api_family,
      .default_compatibility_quirks = {"anthropic_messages"},
      .default_reasoning_levels = anthropic_thinking_reasoning_profile().levels,
      .default_reasoning_format = anthropic_thinking_reasoning_profile().format,
      .reasoning_request_parameters = anthropic_thinking_reasoning_profile().request_parameters,
      .reasoning_level_only = false,
      .enabled_reasoning_requires_budget_tokens = true,
      .adaptive_reasoning_rejects_budget_tokens = true,
      .default_reasoning_budget_tokens = kDefaultAnthropicThinkingBudgetTokens,
      .minimum_reasoning_budget_tokens = kMinimumAnthropicThinkingBudgetTokens,
      .reasoning_display_values = {"summarized", "omitted"},
      .supports_oauth = true};
  return profile;
}

ProviderProfile const& kimi_provider_profile()
{
  static ProviderProfile const profile{
      .provider_id = "kimi",
      .display_name = "Kimi",
      .connect_detail = "API key",
      .api_family = openai_compatible_reasoning_content_profile().api_family,
      .default_base_url_env = "KIMI_BASE_URL",
      .default_base_url = "https://api.kimi.com/coding",
      .chat_completions_path = "/v1/chat/completions",
      .user_agent = "KimiCLI/1.5",
      .default_compatibility_quirks = {"kimi", "reasoning_content", "preserve_reasoning_content", "temperature_1"},
      .default_reasoning_levels = openai_compatible_reasoning_content_profile().levels,
      .default_reasoning_format = openai_compatible_reasoning_content_profile().format,
      .reasoning_request_parameters = openai_compatible_reasoning_content_profile().request_parameters,
      .preserve_reasoning_content = true,
      .include_stream_usage = true,
      .default_temperature = 1.0};
  return profile;
}

ProviderProfile const& moonshot_provider_profile()
{
  static ProviderProfile const profile{
      .provider_id = "moonshot",
      .display_name = "Moonshot",
      .connect_detail = "API key",
      .api_family = openai_compatible_reasoning_content_profile().api_family,
      .default_base_url_env = "MOONSHOT_BASE_URL",
      .default_base_url = "https://api.moonshot.ai",
      .chat_completions_path = "/v1/chat/completions",
      .default_compatibility_quirks = {"moonshot", "reasoning_content"},
      .default_reasoning_levels = openai_compatible_reasoning_content_profile().levels,
      .default_reasoning_format = openai_compatible_reasoning_content_profile().format,
      .reasoning_request_parameters = openai_compatible_reasoning_content_profile().request_parameters,
      .include_stream_usage = true};
  return profile;
}

ProviderProfile const& openai_provider_profile()
{
  static ProviderProfile const profile{
      .provider_id = "openai",
      .display_name = "OpenAI",
      .connect_detail = "ChatGPT Pro/Plus or API key",
      .api_family = openai_responses_reasoning_profile().api_family,
      .default_reasoning_levels = openai_responses_reasoning_profile().levels,
      .default_reasoning_format = openai_responses_reasoning_profile().format,
      .reasoning_request_parameters = openai_responses_reasoning_profile().request_parameters,
      .supports_oauth = true};
  return profile;
}

ProviderProfile const& openrouter_provider_profile()
{
  static ProviderProfile const profile{
      .provider_id = "openrouter",
      .display_name = "OpenRouter",
      .connect_detail = "API key",
      .api_family = openai_compatible_reasoning_content_profile().api_family,
      .default_base_url_env = "OPENROUTER_BASE_URL",
      .default_base_url = "https://openrouter.ai/api",
      .chat_completions_path = "/v1/chat/completions",
      .default_compatibility_quirks = {"openai_compatible", "reasoning_content"},
      .default_reasoning_levels = openai_compatible_reasoning_content_profile().levels,
      .default_reasoning_format = openai_compatible_reasoning_content_profile().format,
      .reasoning_request_parameters = openai_compatible_reasoning_content_profile().request_parameters,
      .include_stream_usage = true};
  return profile;
}

ProviderProfile const& vercel_provider_profile()
{
  static ProviderProfile const profile{
      .provider_id = "vercel",
      .display_name = "Vercel AI Gateway",
      .connect_detail = "API key",
      .runtime_selectable = false};
  return profile;
}

std::vector<ProviderProfile> builtin_provider_profiles()
{
  return {openai_provider_profile(), anthropic_provider_profile(),  moonshot_provider_profile(),
          kimi_provider_profile(),   openrouter_provider_profile(), vercel_provider_profile()};
}

std::optional<ProviderProfile> find_provider_profile(std::string_view provider_id)
{
  for (auto const& profile : builtin_provider_profiles()) {
    if (profile.provider_id == provider_id) return profile;
  }
  return std::nullopt;
}

std::optional<ProviderProfile> provider_profile_for_model(ModelInfo const& model)
{
  return find_provider_profile(model.provider_id);
}

std::optional<ProviderProfile> reasoning_provider_profile_for_model(ModelInfo const& model)
{
  auto profile = provider_profile_for_model(model);
  if (profile && !model.api_family.empty() && profile->api_family != model.api_family) profile = std::nullopt;
  if (profile) return profile;
  return fallback_profile_for_api_family(model.api_family, model.reasoning_format);
}

std::string provider_display_name(std::string_view provider_id)
{
  if (auto profile = find_provider_profile(provider_id)) return profile->display_name;
  std::string label(provider_id);
  if (!label.empty()) label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
  return label;
}

bool provider_accepts_reasoning_format(ModelInfo const& model, std::string_view format)
{
  if (format.empty()) return false;
  auto profile = reasoning_provider_profile_for_model(model);
  if (!profile || profile->api_family != model.api_family || profile->default_reasoning_format != format) return false;

  if (!profile->reasoning_level_only) return true;
  if (!profile->preserve_reasoning_content && !has_compatibility_quirk(model, "preserve_reasoning_content")) {
    return false;
  }
  return model.reasoning_format == format;
}

ava::core::VoidResult validate_reasoning_request(ModelInfo const& model, std::string_view level,
                                                 std::optional<long long> budget_tokens, std::string_view display)
{
  auto profile = reasoning_provider_profile_for_model(model);
  if (!profile) return {};
  auto const provider_label = provider_display_name(model.provider_id);

  if (profile->reasoning_level_only) {
    if (level == "disabled" && profile->api_family == openai_compatible_reasoning_content_profile().api_family) {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use clear_reasoning to disable reasoning"));
    }
    if (budget_tokens || !display.empty()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              provider_label + " reasoning supports level only"));
    }
    return {};
  }

  if (level == "enabled" && profile->enabled_reasoning_requires_budget_tokens && !budget_tokens) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            provider_label + " enabled reasoning requires budget_tokens"));
  }
  if (level == "enabled" && budget_tokens && profile->minimum_reasoning_budget_tokens > 0 &&
      *budget_tokens < profile->minimum_reasoning_budget_tokens) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            provider_label + " reasoning budget must be at least " +
                                                std::to_string(profile->minimum_reasoning_budget_tokens) + " tokens"));
  }
  if (level == "adaptive" && profile->adaptive_reasoning_rejects_budget_tokens && budget_tokens) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            provider_label + " adaptive reasoning does not accept budget_tokens"));
  }
  auto const max_output_tokens = model.max_output_tokens.value_or(
      profile->default_reasoning_budget_tokens > 0 ? profile->default_reasoning_budget_tokens : 4096);
  if (budget_tokens && *budget_tokens >= max_output_tokens) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "reasoning budget must be below max output tokens"));
  }
  if (!display_value_allowed(*profile, display)) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            provider_label + " reasoning display must be summarized or omitted"));
  }
  return {};
}

}  // namespace ava::config
