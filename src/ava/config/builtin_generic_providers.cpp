#include "sys.h"
#include "ava/config/builtin_generic_providers.h"
#include "ava/config/provider_config_internal.h"
#include "ava/config/reasoning_profiles.h"

#include <array>
#include <cstdlib>
#include <utility>

namespace ava::config {
namespace {

// Reasoning levels for xAI Grok Responses-native reasoning.effort.
constexpr char const* kXaiReasoningLevels[] = {"low", "medium", "high"};

// Contracts verified 2026-08-03 from public vendor docs (no live API calls).
// Endpoint/env/protocol are authoritative for built-in registration tests.
constexpr BuiltinGenericProviderSpec kProviders[] = {
    {
        .provider_id = "xai",
        .display_name = "xAI",
        .connect_detail = "API key",
        .api_key_env = "XAI_API_KEY",
        .base_url_env = "XAI_BASE_URL",
        .default_base_url = "https://api.x.ai",
        .request_path = "/v1/responses",
        .protocol = BuiltinGenericProtocol::OpenAIResponses,
        .max_token_field = BuiltinMaxTokenField::MaxOutputTokens,
        .include_stream_usage = false,
        .supports_reasoning = true,
        .source_note = "xAI public Responses API; never enable OpenAI Codex OAuth",
    },
    {
        .provider_id = "groq",
        .display_name = "Groq",
        .connect_detail = "API key",
        .api_key_env = "GROQ_API_KEY",
        .base_url_env = "GROQ_BASE_URL",
        .default_base_url = "https://api.groq.com/openai",
        .request_path = "/v1/chat/completions",
        .protocol = BuiltinGenericProtocol::OpenAIChatCompletions,
        .max_token_field = BuiltinMaxTokenField::MaxCompletionTokens,
        .include_stream_usage = false,
        .supports_reasoning = false,
        .source_note = "Groq OpenAI-compatible chat; no AVA reasoning controls (include_reasoning not mapped)",
    },
    {
        .provider_id = "cerebras",
        .display_name = "Cerebras",
        .connect_detail = "API key",
        .api_key_env = "CEREBRAS_API_KEY",
        .base_url_env = "CEREBRAS_BASE_URL",
        .default_base_url = "https://api.cerebras.ai",
        .request_path = "/v1/chat/completions",
        .protocol = BuiltinGenericProtocol::OpenAIChatCompletions,
        .max_token_field = BuiltinMaxTokenField::MaxCompletionTokens,
        .include_stream_usage = false,
        .supports_reasoning = false,
        .source_note = "Cerebras OpenAI-compatible chat; context/max tier-dependent so omitted",
    },
    {
        .provider_id = "together",
        .display_name = "Together",
        .connect_detail = "API key",
        .api_key_env = "TOGETHER_API_KEY",
        .base_url_env = "TOGETHER_BASE_URL",
        .default_base_url = "https://api.together.ai",
        .request_path = "/v1/chat/completions",
        .protocol = BuiltinGenericProtocol::OpenAIChatCompletions,
        .max_token_field = BuiltinMaxTokenField::MaxTokens,
        .include_stream_usage = false,
        .supports_reasoning = false,
        .source_note = "Together OpenAI-compatible chat; max_tokens; no stream_options",
    },
    {
        .provider_id = "fireworks",
        .display_name = "Fireworks",
        .connect_detail = "API key",
        .api_key_env = "FIREWORKS_API_KEY",
        .base_url_env = "FIREWORKS_BASE_URL",
        .default_base_url = "https://api.fireworks.ai/inference",
        .request_path = "/v1/chat/completions",
        .protocol = BuiltinGenericProtocol::OpenAIChatCompletions,
        .max_token_field = BuiltinMaxTokenField::MaxTokens,
        .include_stream_usage = true,
        .supports_reasoning = false,
        .source_note = "Fireworks OpenAI-compatible chat; stream usage last-chunk; no advertised AVA reasoning controls",
    },
    {
        .provider_id = "mistral",
        .display_name = "Mistral",
        .connect_detail = "API key",
        .api_key_env = "MISTRAL_API_KEY",
        .base_url_env = "MISTRAL_BASE_URL",
        .default_base_url = "https://api.mistral.ai",
        .request_path = "/v1/chat/completions",
        .protocol = BuiltinGenericProtocol::OpenAIChatCompletions,
        .max_token_field = BuiltinMaxTokenField::MaxTokens,
        .include_stream_usage = true,
        .supports_reasoning = false,
        .source_note = "Mistral OpenAI-compatible chat; ThinkChunks not parsed so reasoning controls disabled",
    },
};

constexpr BuiltinGenericModelSpec kModels[] = {
    // xAI
    {
        .provider_id = "xai",
        .model_id = "grok-4.5",
        .display_name = "Grok 4.5",
        .family = "grok-4.5",
        .context_window_tokens = 500'000,
        .max_output_tokens = std::nullopt,  // unknown / tiered
        .input_per_million = std::nullopt,  // tiered at 200k — omit scalar pricing
        .output_per_million = std::nullopt,
        .cache_read_per_million = std::nullopt,
        .supports_tools = true,
        .supports_images = true,
        .supports_reasoning = true,
        .reasoning_levels = kXaiReasoningLevels,
    },
    // Groq
    {
        .provider_id = "groq",
        .model_id = "openai/gpt-oss-120b",
        .display_name = "GPT-OSS 120B (Groq)",
        .family = "gpt-oss",
        .context_window_tokens = 131'072,
        .max_output_tokens = 65'536,
        .input_per_million = 0.15L,
        .output_per_million = 0.60L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    {
        .provider_id = "groq",
        .model_id = "openai/gpt-oss-20b",
        .display_name = "GPT-OSS 20B (Groq)",
        .family = "gpt-oss",
        .context_window_tokens = 131'072,
        .max_output_tokens = 65'536,
        .input_per_million = 0.075L,
        .output_per_million = 0.30L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    // Cerebras
    {
        .provider_id = "cerebras",
        .model_id = "gpt-oss-120b",
        .display_name = "GPT-OSS 120B (Cerebras)",
        .family = "gpt-oss",
        .context_window_tokens = std::nullopt,  // account-tier dependent
        .max_output_tokens = std::nullopt,
        .input_per_million = 0.35L,
        .output_per_million = 0.75L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    // Together
    {
        .provider_id = "together",
        .model_id = "moonshotai/Kimi-K2.7-Code",
        .display_name = "Kimi K2.7 Code (Together)",
        .family = "kimi-k2.7",
        .context_window_tokens = 262'144,
        .max_output_tokens = std::nullopt,
        .input_per_million = 0.95L,
        .output_per_million = 4.00L,
        .cache_read_per_million = 0.19L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    {
        .provider_id = "together",
        .model_id = "openai/gpt-oss-120b",
        .display_name = "GPT-OSS 120B (Together)",
        .family = "gpt-oss",
        .context_window_tokens = 128'000,
        .max_output_tokens = std::nullopt,
        .input_per_million = 0.15L,
        .output_per_million = 0.60L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    // Fireworks
    {
        .provider_id = "fireworks",
        .model_id = "accounts/fireworks/models/deepseek-v4-pro",
        .display_name = "DeepSeek V4 Pro (Fireworks)",
        .family = "deepseek-v4",
        .context_window_tokens = std::nullopt,
        .max_output_tokens = std::nullopt,
        .input_per_million = 1.74L,
        .output_per_million = 3.48L,
        .cache_read_per_million = 0.145L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    {
        .provider_id = "fireworks",
        .model_id = "accounts/fireworks/models/deepseek-v4-flash",
        .display_name = "DeepSeek V4 Flash (Fireworks)",
        .family = "deepseek-v4",
        .context_window_tokens = std::nullopt,
        .max_output_tokens = std::nullopt,
        .input_per_million = 0.14L,
        .output_per_million = 0.28L,
        .cache_read_per_million = 0.028L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    // Mistral
    {
        .provider_id = "mistral",
        .model_id = "mistral-medium-latest",
        .display_name = "Mistral Medium",
        .family = "mistral-medium",
        .context_window_tokens = 256'000,
        .max_output_tokens = std::nullopt,
        .input_per_million = 1.5L,
        .output_per_million = 7.5L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    {
        .provider_id = "mistral",
        .model_id = "mistral-small-latest",
        .display_name = "Mistral Small",
        .family = "mistral-small",
        .context_window_tokens = 256'000,
        .max_output_tokens = std::nullopt,
        .input_per_million = 0.15L,
        .output_per_million = 0.6L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
    {
        .provider_id = "mistral",
        .model_id = "codestral-latest",
        .display_name = "Codestral",
        .family = "codestral",
        .context_window_tokens = 128'000,
        .max_output_tokens = std::nullopt,
        .input_per_million = 0.3L,
        .output_per_million = 0.9L,
        .supports_tools = true,
        .supports_images = false,
        .supports_reasoning = false,
    },
};

std::vector<std::string> default_quirks_for(BuiltinGenericProviderSpec const& spec)
{
  std::vector<std::string> quirks;
  if (spec.protocol == BuiltinGenericProtocol::OpenAIChatCompletions)
    quirks.emplace_back("openai_compatible");
  if (spec.max_token_field == BuiltinMaxTokenField::MaxCompletionTokens)
    quirks.emplace_back("max_completion_tokens");
  return quirks;
}

std::optional<ModelPricing> pricing_from(BuiltinGenericModelSpec const& model)
{
  if (!model.input_per_million && !model.output_per_million && !model.cache_read_per_million)
    return std::nullopt;
  return ModelPricing{.input_per_million = model.input_per_million,
                      .output_per_million = model.output_per_million,
                      .cache_read_per_million = model.cache_read_per_million,
                      .cache_write_per_million = std::nullopt,
                      .reasoning_per_million = std::nullopt};
}

}  // namespace

std::span<BuiltinGenericProviderSpec const> builtin_generic_provider_specs() noexcept
{
  return kProviders;
}

std::span<BuiltinGenericModelSpec const> builtin_generic_model_specs() noexcept
{
  return kModels;
}

ProviderProfile provider_profile_from_builtin_generic_spec(BuiltinGenericProviderSpec const& spec)
{
  ProviderProfile profile;
  profile.provider_id = std::string(spec.provider_id);
  profile.display_name = std::string(spec.display_name);
  profile.connect_detail = std::string(spec.connect_detail);
  profile.default_base_url_env = std::string(spec.base_url_env);
  profile.default_base_url = std::string(spec.default_base_url);
  profile.chat_completions_path = std::string(spec.request_path);
  profile.api_key_env = std::string(spec.api_key_env);
  profile.include_stream_usage = spec.include_stream_usage;
  profile.supports_oauth = false;
  profile.runtime_selectable = true;
  profile.user_defined = false;
  profile.auth_none = false;
  profile.default_compatibility_quirks = default_quirks_for(spec);
  // Canonical endpoint for diagnostics (base + path, single slash join).
  profile.endpoint = builtin_generic_canonical_endpoint(spec);

  if (spec.protocol == BuiltinGenericProtocol::OpenAIResponses)
  {
    auto const& reasoning = openai_responses_reasoning_profile();
    profile.api_family = reasoning.api_family;
    profile.default_reasoning_format = reasoning.format;
    profile.reasoning_request_parameters = reasoning.request_parameters;
    if (spec.supports_reasoning)
    {
      profile.default_reasoning_levels = {"low", "medium", "high"};
    }
  }
  else
  {
    auto const& chat = openai_compatible_reasoning_content_profile();
    profile.api_family = chat.api_family;
    // Do not advertise reasoning controls for chat generics in this phase.
    profile.default_reasoning_format = {};
    profile.default_reasoning_levels = {};
    profile.reasoning_request_parameters = {};
  }
  return profile;
}

std::vector<ProviderProfile> builtin_generic_provider_profiles()
{
  std::vector<ProviderProfile> profiles;
  profiles.reserve(builtin_generic_provider_specs().size());
  for (auto const& spec : builtin_generic_provider_specs()) profiles.push_back(provider_profile_from_builtin_generic_spec(spec));
  return profiles;
}

std::vector<ModelInfo> builtin_generic_model_infos()
{
  std::vector<ModelInfo> models;
  models.reserve(builtin_generic_model_specs().size());
  for (auto const& model_spec : builtin_generic_model_specs())
  {
    auto provider_spec = find_builtin_generic_provider_spec(model_spec.provider_id);
    if (!provider_spec)
      continue;
    auto const profile = provider_profile_from_builtin_generic_spec(*provider_spec);

    ModelInfo model;
    model.provider_id = std::string(model_spec.provider_id);
    model.model_id = std::string(model_spec.model_id);
    model.display_name = std::string(model_spec.display_name);
    model.display_name_is_configured = true;
    model.family = std::string(model_spec.family);
    model.context_window_tokens = model_spec.context_window_tokens;
    model.max_output_tokens = model_spec.max_output_tokens;
    model.pricing = pricing_from(model_spec);
    model.api_family = profile.api_family;
    model.input_modalities = model_spec.supports_images ? std::vector<std::string>{"text", "image"} : std::vector<std::string>{"text"};
    model.output_modalities = {"text"};
    model.supports_tools = model_spec.supports_tools;
    model.supports_streaming = true;
    model.supports_reasoning = model_spec.supports_reasoning;
    model.reports_usage = true;
    model.compatibility_quirks = profile.default_compatibility_quirks;
    if (model_spec.supports_reasoning)
    {
      model.reasoning_format = profile.default_reasoning_format;
      for (auto const* level : model_spec.reasoning_levels) model.reasoning_levels.emplace_back(level);
    }
    models.push_back(std::move(model));
  }
  return models;
}

std::optional<BuiltinGenericProviderSpec> find_builtin_generic_provider_spec(std::string_view provider_id) noexcept
{
  for (auto const& spec : builtin_generic_provider_specs())
  {
    if (spec.provider_id == provider_id)
      return spec;
  }
  return std::nullopt;
}

std::string builtin_generic_canonical_endpoint(BuiltinGenericProviderSpec const& spec)
{
  std::string base(spec.default_base_url);
  while (!base.empty() && base.back() == '/') base.pop_back();
  std::string path(spec.request_path);
  if (path.empty())
    return base;
  if (path.front() != '/')
    path.insert(path.begin(), '/');
  return base + path;
}

ava::core::Result<std::vector<ResolvedBuiltinGenericProvider>> resolve_builtin_generic_providers(bool read_base_url_env)
{
  using provider_config_detail::join_endpoint;
  using provider_config_detail::parse_and_validate_base_url;
  using provider_config_detail::validate_request_path;

  std::vector<ResolvedBuiltinGenericProvider> resolved;
  resolved.reserve(builtin_generic_provider_specs().size());

  for (auto const& spec : builtin_generic_provider_specs())
  {
    std::string_view raw_base = spec.default_base_url;
    if (read_base_url_env)
    {
      char const* env_value = std::getenv(std::string(spec.base_url_env).c_str());
      if (env_value != nullptr && env_value[0] != '\0')
        raw_base = env_value;
    }

    auto parsed_base = parse_and_validate_base_url(raw_base);
    if (!parsed_base)
    {
      // Sanitized failure: provider + env name only. Never echo the override value.
      auto error = ava::core::Error(ava::core::ErrorCategory::Configuration, "built-in provider base URL override is invalid");
      error.with_context("provider", std::string(spec.provider_id));
      error.with_context("env", std::string(spec.base_url_env));
      error.with_context("field", "base_url");
      return std::unexpected(std::move(error));
    }

    std::string request_path(spec.request_path);
    if (request_path.empty())
      request_path = "/";
    if (request_path.front() != '/')
      request_path.insert(request_path.begin(), '/');
    if (auto valid_path = validate_request_path(request_path, "request_path"); !valid_path)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Configuration, "built-in provider request path is invalid");
      error.with_context("provider", std::string(spec.provider_id));
      return std::unexpected(std::move(error));
    }

    ResolvedBuiltinGenericProvider item;
    item.provider_id = std::string(spec.provider_id);
    item.display_name = std::string(spec.display_name);
    item.base_url_env = std::string(spec.base_url_env);
    item.base_url = std::move(parsed_base->canonical_base);
    item.request_path = std::move(request_path);
    item.endpoint = join_endpoint(item.base_url, item.request_path);
    item.protocol = spec.protocol;
    item.include_stream_usage = spec.include_stream_usage;
    resolved.push_back(std::move(item));
  }

  return resolved;
}

void apply_resolved_builtin_generic_to_profile(ProviderProfile& profile, ResolvedBuiltinGenericProvider const& resolved) noexcept
{
  if (profile.provider_id != resolved.provider_id)
    return;
  profile.default_base_url = resolved.base_url;
  profile.endpoint = resolved.endpoint;
}

}  // namespace ava::config
