#pragma once

#include "ava/core/result.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::config {

struct ModelInfo;

struct ProviderProfile {
  std::string provider_id = {};
  std::string display_name = {};
  std::string connect_detail = {};
  std::string api_family = {};
  std::string default_base_url_env = {};
  std::string default_base_url = {};
  std::string chat_completions_path = "/v1/chat/completions";
  std::string user_agent = {};
  std::vector<std::string> default_compatibility_quirks = {};
  std::vector<std::string> default_reasoning_levels = {};
  std::string default_reasoning_format = {};
  std::string reasoning_request_parameters = {};
  bool reasoning_level_only = true;
  bool enabled_reasoning_requires_budget_tokens = false;
  bool adaptive_reasoning_rejects_budget_tokens = false;
  long long default_reasoning_budget_tokens = 0;
  long long minimum_reasoning_budget_tokens = 0;
  std::vector<std::string> reasoning_display_values = {};
  bool preserve_reasoning_content = false;
  bool include_stream_usage = false;
  std::optional<double> default_temperature = std::nullopt;
  bool supports_oauth = false;
  // True when the profile has a built-in runtime Provider factory and can back
  // model selection. Some profiles are intentionally connect/auth metadata only
  // until their runtime provider is implemented.
  bool runtime_selectable = true;
};

[[nodiscard]] ProviderProfile const& anthropic_provider_profile();
[[nodiscard]] ProviderProfile const& kimi_provider_profile();
[[nodiscard]] ProviderProfile const& moonshot_provider_profile();
[[nodiscard]] ProviderProfile const& openai_provider_profile();
[[nodiscard]] ProviderProfile const& openrouter_provider_profile();
[[nodiscard]] ProviderProfile const& vercel_provider_profile();

[[nodiscard]] std::vector<ProviderProfile> builtin_provider_profiles();
[[nodiscard]] std::optional<ProviderProfile> find_provider_profile(std::string_view provider_id);
[[nodiscard]] std::optional<ProviderProfile> provider_profile_for_model(ModelInfo const& model);
[[nodiscard]] std::optional<ProviderProfile> reasoning_provider_profile_for_model(ModelInfo const& model);
[[nodiscard]] std::string provider_display_name(std::string_view provider_id);
[[nodiscard]] bool provider_accepts_reasoning_format(ModelInfo const& model, std::string_view format);

[[nodiscard]] ava::core::VoidResult validate_reasoning_request(ModelInfo const& model, std::string_view level,
                                                               std::optional<long long> budget_tokens,
                                                               std::string_view display);

}  // namespace ava::config
